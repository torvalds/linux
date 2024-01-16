/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl12xx
 *
 * Copyright (C) 1998-2009, 2011 Texas Instruments. All rights reserved.
 * Copyright (C) 2009 Nokia Corporation
 */

#ifndef __WL12XX_CMD_H__
#define __WL12XX_CMD_H__

#include "conf.h"

#define TEST_CMD_INI_FILE_RADIO_PARAM       0x19
#define TEST_CMD_INI_FILE_GENERAL_PARAM     0x1E

struct wl1271_general_parms_cmd {
	struct wl1271_cmd_header header;

	struct wl1271_cmd_test_header test;

	struct wl1271_ini_general_params general_params;

	u8 sr_debug_table[WL1271_INI_MAX_SMART_REFLEX_PARAM];
	u8 sr_sen_n_p;
	u8 sr_sen_n_p_gain;
	u8 sr_sen_nrn;
	u8 sr_sen_prn;
	u8 padding[3];
} __packed;

struct wl128x_general_parms_cmd {
	struct wl1271_cmd_header header;

	struct wl1271_cmd_test_header test;

	struct wl128x_ini_general_params general_params;

	u8 sr_debug_table[WL1271_INI_MAX_SMART_REFLEX_PARAM];
	u8 sr_sen_n_p;
	u8 sr_sen_n_p_gain;
	u8 sr_sen_nrn;
	u8 sr_sen_prn;
	u8 padding[3];
} __packed;

struct wl1271_radio_parms_cmd {
	struct wl1271_cmd_header header;

	struct wl1271_cmd_test_header test;

	/* Static radio parameters */
	struct wl1271_ini_band_params_2 static_params_2;
	struct wl1271_ini_band_params_5 static_params_5;

	/* Dynamic radio parameters */
	struct wl1271_ini_fem_params_2 dyn_params_2;
	u8 padding2;
	struct wl1271_ini_fem_params_5 dyn_params_5;
	u8 padding3[2];
} __packed;

struct wl128x_radio_parms_cmd {
	struct wl1271_cmd_header header;

	struct wl1271_cmd_test_header test;

	/* Static radio parameters */
	struct wl128x_ini_band_params_2 static_params_2;
	struct wl128x_ini_band_params_5 static_params_5;

	u8 fem_vendor_and_options;

	/* Dynamic radio parameters */
	struct wl128x_ini_fem_params_2 dyn_params_2;
	u8 padding2;
	struct wl128x_ini_fem_params_5 dyn_params_5;
} __packed;

#define TEST_CMD_INI_FILE_RF_EXTENDED_PARAM 0x26

struct wl1271_ext_radio_parms_cmd {
	struct wl1271_cmd_header header;

	struct wl1271_cmd_test_header test;

	u8 tx_per_channel_power_compensation_2[CONF_TX_PWR_COMPENSATION_LEN_2];
	u8 tx_per_channel_power_compensation_5[CONF_TX_PWR_COMPENSATION_LEN_5];
	u8 padding[3];
} __packed;

struct wl12xx_cmd_channel_switch {
	struct wl1271_cmd_header header;

	u8 role_id;

	/* The new serving channel */
	u8 channel;
	/* Relative time of the serving channel switch in TBTT units */
	u8 switch_time;
	/* Stop the role TX, should expect it after radar detection */
	u8 stop_tx;
	/* The target channel tx status 1-stopped 0-open*/
	u8 post_switch_tx_disable;

	u8 padding[3];
} __packed;

int wl1271_cmd_general_parms(struct wl1271 *wl);
int wl128x_cmd_general_parms(struct wl1271 *wl);
int wl1271_cmd_radio_parms(struct wl1271 *wl);
int wl128x_cmd_radio_parms(struct wl1271 *wl);
int wl1271_cmd_ext_radio_parms(struct wl1271 *wl);
int wl12xx_cmd_channel_switch(struct wl1271 *wl,
			      struct wl12xx_vif *wlvif,
			      struct ieee80211_channel_switch *ch_switch);

#endif /* __WL12XX_CMD_H__ */
