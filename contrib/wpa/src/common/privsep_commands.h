/*
 * WPA Supplicant - privilege separation commands
 * Copyright (c) 2007-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef PRIVSEP_COMMANDS_H
#define PRIVSEP_COMMANDS_H

#include "drivers/driver.h"
#include "common/ieee802_11_defs.h"

enum privsep_cmd {
	PRIVSEP_CMD_REGISTER,
	PRIVSEP_CMD_UNREGISTER,
	PRIVSEP_CMD_SCAN,
	PRIVSEP_CMD_GET_SCAN_RESULTS,
	PRIVSEP_CMD_ASSOCIATE,
	PRIVSEP_CMD_GET_BSSID,
	PRIVSEP_CMD_GET_SSID,
	PRIVSEP_CMD_SET_KEY,
	PRIVSEP_CMD_GET_CAPA,
	PRIVSEP_CMD_L2_REGISTER,
	PRIVSEP_CMD_L2_UNREGISTER,
	PRIVSEP_CMD_L2_NOTIFY_AUTH_START,
	PRIVSEP_CMD_L2_SEND,
	PRIVSEP_CMD_SET_COUNTRY,
	PRIVSEP_CMD_AUTHENTICATE,
};

#define PRIVSEP_MAX_SCAN_FREQS 50

struct privsep_cmd_scan {
	unsigned int num_ssids;
	u8 ssids[WPAS_MAX_SCAN_SSIDS][32];
	u8 ssid_lens[WPAS_MAX_SCAN_SSIDS];
	unsigned int num_freqs;
	u16 freqs[PRIVSEP_MAX_SCAN_FREQS];
};

struct privsep_cmd_authenticate {
	int freq;
	u8 bssid[ETH_ALEN];
	u8 ssid[SSID_MAX_LEN];
	size_t ssid_len;
	int auth_alg;
	size_t ie_len;
	u8 wep_key[4][16];
	size_t wep_key_len[4];
	int wep_tx_keyidx;
	int local_state_change;
	int p2p;
	size_t auth_data_len;
	/* followed by ie_len bytes of ie */
	/* followed by auth_data_len bytes of auth_data */
};

struct privsep_cmd_associate {
	u8 bssid[ETH_ALEN];
	u8 ssid[SSID_MAX_LEN];
	size_t ssid_len;
	int hwmode;
	int freq;
	int channel;
	int pairwise_suite;
	int group_suite;
	int key_mgmt_suite;
	int auth_alg;
	int mode;
	size_t wpa_ie_len;
	/* followed by wpa_ie_len bytes of wpa_ie */
};

struct privsep_cmd_set_key {
	int alg;
	u8 addr[ETH_ALEN];
	int key_idx;
	int set_tx;
	u8 seq[8];
	size_t seq_len;
	u8 key[32];
	size_t key_len;
};

enum privsep_event {
	PRIVSEP_EVENT_SCAN_RESULTS,
	PRIVSEP_EVENT_ASSOC,
	PRIVSEP_EVENT_DISASSOC,
	PRIVSEP_EVENT_ASSOCINFO,
	PRIVSEP_EVENT_MICHAEL_MIC_FAILURE,
	PRIVSEP_EVENT_INTERFACE_STATUS,
	PRIVSEP_EVENT_PMKID_CANDIDATE,
	PRIVSEP_EVENT_FT_RESPONSE,
	PRIVSEP_EVENT_RX_EAPOL,
	PRIVSEP_EVENT_SCAN_STARTED,
	PRIVSEP_EVENT_AUTH,
};

struct privsep_event_auth {
	u8 peer[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	u16 auth_type;
	u16 auth_transaction;
	u16 status_code;
	size_t ies_len;
	/* followed by ies_len bytes of ies */
};

#endif /* PRIVSEP_COMMANDS_H */
