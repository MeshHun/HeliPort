/* SPDX-License-Identifier: BSD-3-Clause
 * rtwcorectl — Command-line control and diagnostic tool for RTWCore / itlwm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "Api.h"

static const char *phy_mode_str(enum itl_phy_mode mode) {
    switch (mode) {
        case ITL80211_MODE_11A:  return "802.11a";
        case ITL80211_MODE_11B:  return "802.11b";
        case ITL80211_MODE_11G:  return "802.11g";
        case ITL80211_MODE_11N:  return "802.11n (Wi-Fi 4)";
        case ITL80211_MODE_11AC: return "802.11ac (Wi-Fi 5)";
        case ITL80211_MODE_11AX: return "802.11ax (Wi-Fi 6)";
        default: return "Unknown";
    }
}

static const char *state_str(uint32_t state) {
    switch (state) {
        case ITL80211_S_INIT:  return "INIT (Idle / Initialized)";
        case ITL80211_S_SCAN:  return "SCAN (Scanning)";
        case ITL80211_S_AUTH:  return "AUTH (Authenticating)";
        case ITL80211_S_ASSOC: return "ASSOC (Associating)";
        case ITL80211_S_RUN:   return "RUN (Connected / Associated)";
        default: return "Unknown";
    }
}

static const char *cipher_str(enum itl80211_cipher cipher) {
    if (cipher & ITL80211_CIPHER_CCMP) return "WPA2/WPA3 (CCMP/AES)";
    if (cipher & ITL80211_CIPHER_TKIP) return "WPA (TKIP)";
    if (cipher & (ITL80211_CIPHER_WEP40 | ITL80211_CIPHER_WEP104)) return "WEP";
    if (cipher == ITL80211_CIPHER_NONE) return "Open";
    return "Other";
}

void print_usage(const char *prog) {
    printf("Usage: %s <command> [arguments]\n\n", prog);
    printf("Commands:\n");
    printf("  status                 Display current driver, hardware, and link status\n");
    printf("  scan                   Scan surrounding Wi-Fi networks and list results\n");
    printf("  connect <ssid> [pwd]   Connect to a Wi-Fi network\n");
    printf("  disconnect             Disconnect from the current network\n");
    printf("  power <on|off>         Turn wireless hardware power on or off\n");
    printf("  help                   Display this help message\n");
}

int cmd_status(void) {
    platform_info_t pinfo;
    if (!get_platform_info(&pinfo)) {
        fprintf(stderr, "Error: Unable to connect to RTWCore driver. Is RTWCore.kext loaded?\n");
        return 1;
    }

    printf("================ RTWCore Hardware & Driver Status ================\n");
    printf("Interface (BSD)   : %s\n", pinfo.device_info_str);
    printf("Driver / Firmware : %s\n", pinfo.driver_info_str);

    bool power = false;
    if (get_power_state(&power)) {
        printf("Hardware Power    : %s\n", power ? "ON" : "OFF");
    }

    uint32_t state = 0;
    if (get_80211_state(&state)) {
        printf("802.11 State      : %s\n", state_str(state));
    }

    if (state == ITL80211_S_RUN) {
        station_info_t sta;
        if (get_station_info(&sta) == KERN_SUCCESS) {
            printf("\n--- Current Active Connection ---\n");
            printf("SSID              : %s\n", (char *)sta.ssid);
            printf("BSSID             : %02x:%02x:%02x:%02x:%02x:%02x\n",
                   sta.bssid[0], sta.bssid[1], sta.bssid[2],
                   sta.bssid[3], sta.bssid[4], sta.bssid[5]);
            printf("PHY Mode          : %s\n", phy_mode_str(sta.op_mode));
            printf("Channel           : %u\n", sta.channel);
            printf("Channel Bandwidth : %u MHz\n", sta.band_width);
            printf("Current MCS       : %d (Max: %d)\n", sta.cur_mcs, sta.max_mcs);
            printf("Tx Rate           : %u Mbps\n", sta.rate / 2);
            printf("Signal (RSSI)     : %d dBm\n", sta.rssi);
            printf("Noise Level       : %d dBm\n", sta.noise);
        }
    } else {
        printf("\nNot connected to any Wi-Fi network.\n");
    }
    printf("==================================================================\n");
    return 0;
}

int cmd_scan(void) {
    printf("Scanning for Wi-Fi networks...\n");
    network_info_list_t list;
    if (!get_network_list(&list)) {
        fprintf(stderr, "Error: Failed to perform scan or retrieve network list.\n");
        return 1;
    }

    printf("Found %d network(s):\n\n", list.count);
    printf("%-32s  %-17s  %-4s  %-8s  %-16s\n", "SSID", "BSSID", "CHAN", "RSSI", "SECURITY");
    printf("----------------------------------------------------------------------------------\n");

    for (int i = 0; i < list.count; i++) {
        struct ioctl_network_info *n = &list.networks[i];
        char ssid[33] = {0};
        strncpy(ssid, (char *)n->ssid, 32);
        if (strlen(ssid) == 0) strcpy(ssid, "<Hidden Network>");

        char bssid[18];
        snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
                 n->bssid[0], n->bssid[1], n->bssid[2],
                 n->bssid[3], n->bssid[4], n->bssid[5]);

        printf("%-32s  %-17s  %-4u  %-4d dBm  %-16s\n",
               ssid, bssid, n->channel, n->rssi, cipher_str(n->rsn_groupcipher));
    }
    return 0;
}

int cmd_connect(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: rtwcorectl connect <ssid> [password]\n");
        return 1;
    }
    const char *ssid = argv[2];
    const char *pwd = (argc >= 4) ? argv[3] : "";

    printf("Connecting to '%s'...\n", ssid);
    if (connect_network(ssid, pwd)) {
        printf("Connection request accepted. Checking status...\n");
        sleep(2);
        cmd_status();
        return 0;
    } else {
        fprintf(stderr, "Error: Failed to initiate connection to '%s'.\n", ssid);
        return 1;
    }
}

int cmd_disconnect(void) {
    char ssid[33] = {0};
    if (get_network_ssid(ssid) && strlen(ssid) > 0) {
        printf("Disconnecting from '%s'...\n", ssid);
        dis_associate_ssid(ssid);
        printf("Disconnected.\n");
        return 0;
    } else {
        printf("Not currently connected.\n");
        return 0;
    }
}

int cmd_power(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: rtwcorectl power <on|off>\n");
        return 1;
    }
    if (strcmp(argv[2], "on") == 0) {
        power_on();
        printf("Power ON command sent.\n");
    } else if (strcmp(argv[2], "off") == 0) {
        power_off();
        printf("Power OFF command sent.\n");
    } else {
        fprintf(stderr, "Invalid power option: %s (expected 'on' or 'off')\n", argv[2]);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "status") == 0) {
        return cmd_status();
    } else if (strcmp(argv[1], "scan") == 0 || strcmp(argv[1], "list") == 0) {
        return cmd_scan();
    } else if (strcmp(argv[1], "connect") == 0) {
        return cmd_connect(argc, argv);
    } else if (strcmp(argv[1], "disconnect") == 0) {
        return cmd_disconnect();
    } else if (strcmp(argv[1], "power") == 0) {
        return cmd_power(argc, argv);
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }
}
