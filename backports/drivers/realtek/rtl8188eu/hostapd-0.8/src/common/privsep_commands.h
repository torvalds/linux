/*
 * WPA Supplicant - privilege separation commands
 * Copyright (c) 2007-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef PRIVSEP_COMMANDS_H
#define PRIVSEP_COMMANDS_H

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
};

struct privsep_cmd_associate
{
	u8 bssid[ETH_ALEN];
	u8 ssid[32];
	size_t ssid_len;
	int freq;
	int pairwise_suite;
	int group_suite;
	int key_mgmt_suite;
	int auth_alg;
	int mode;
	size_t wpa_ie_len;
	/* followed by wpa_ie_len bytes of wpa_ie */
};

struct privsep_cmd_set_key
{
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
	PRIVSEP_EVENT_STKSTART,
	PRIVSEP_EVENT_FT_RESPONSE,
	PRIVSEP_EVENT_RX_EAPOL,
};

#endif /* PRIVSEP_COMMANDS_H */
