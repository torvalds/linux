/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL1271_INIT_H__
#define __WL1271_INIT_H__

#include "wl1271.h"

int wl1271_hw_init_power_auth(struct wl1271 *wl);
int wl1271_hw_init(struct wl1271 *wl);

/* These are not really a TEST_CMD, but the ref driver uses them as such */
#define TEST_CMD_INI_FILE_RADIO_PARAM   0x19
#define TEST_CMD_INI_FILE_GENERAL_PARAM 0x1E

struct wl1271_general_parms {
	u8 id;
	u8 padding[3];

	u8 ref_clk;
	u8 settling_time;
	u8 clk_valid_on_wakeup;
	u8 dc2dcmode;
	u8 single_dual_band;

	u8 tx_bip_fem_autodetect;
	u8 tx_bip_fem_manufacturer;
	u8 settings;
} __attribute__ ((packed));

struct wl1271_radio_parms {
	u8 id;
	u8 padding[3];

	/* Static radio parameters */
	/* 2.4GHz */
	u8 rx_trace_loss;
	u8 tx_trace_loss;
	s8 rx_rssi_and_proc_compens[CONF_RSSI_AND_PROCESS_COMPENSATION_SIZE];

	/* 5GHz */
	u8 rx_trace_loss_5[CONF_NUMBER_OF_SUB_BANDS_5];
	u8 tx_trace_loss_5[CONF_NUMBER_OF_SUB_BANDS_5];
	s8 rx_rssi_and_proc_compens_5[CONF_RSSI_AND_PROCESS_COMPENSATION_SIZE];

	/* Dynamic radio parameters */
	/* 2.4GHz */
	__le16 tx_ref_pd_voltage;
	s8  tx_ref_power;
	s8  tx_offset_db;

	s8  tx_rate_limits_normal[CONF_NUMBER_OF_RATE_GROUPS];
	s8  tx_rate_limits_degraded[CONF_NUMBER_OF_RATE_GROUPS];

	s8  tx_channel_limits_11b[CONF_NUMBER_OF_CHANNELS_2_4];
	s8  tx_channel_limits_ofdm[CONF_NUMBER_OF_CHANNELS_2_4];
	s8  tx_pdv_rate_offsets[CONF_NUMBER_OF_RATE_GROUPS];

	u8  tx_ibias[CONF_NUMBER_OF_RATE_GROUPS];
	u8  rx_fem_insertion_loss;

	u8 padding2;

	/* 5GHz */
	__le16 tx_ref_pd_voltage_5[CONF_NUMBER_OF_SUB_BANDS_5];
	s8  tx_ref_power_5[CONF_NUMBER_OF_SUB_BANDS_5];
	s8  tx_offset_db_5[CONF_NUMBER_OF_SUB_BANDS_5];

	s8  tx_rate_limits_normal_5[CONF_NUMBER_OF_RATE_GROUPS];
	s8  tx_rate_limits_degraded_5[CONF_NUMBER_OF_RATE_GROUPS];

	s8  tx_channel_limits_ofdm_5[CONF_NUMBER_OF_CHANNELS_5];
	s8  tx_pdv_rate_offsets_5[CONF_NUMBER_OF_RATE_GROUPS];

	/* FIXME: this is inconsistent with the types for 2.4GHz */
	s8  tx_ibias_5[CONF_NUMBER_OF_RATE_GROUPS];
	s8  rx_fem_insertion_loss_5[CONF_NUMBER_OF_SUB_BANDS_5];

	u8 padding3[2];
} __attribute__ ((packed));

#endif
