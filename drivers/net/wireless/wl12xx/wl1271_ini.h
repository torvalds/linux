/*
 * This file is part of wl1271
 *
 * Copyright (C) 2010 Nokia Corporation
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

#ifndef __WL1271_INI_H__
#define __WL1271_INI_H__

#define WL1271_INI_MAX_SMART_REFLEX_PARAM 16

struct wl1271_ini_general_params {
	u8 ref_clock;
	u8 settling_time;
	u8 clk_valid_on_wakeup;
	u8 dc2dc_mode;
	u8 dual_mode_select;
	u8 tx_bip_fem_auto_detect;
	u8 tx_bip_fem_manufacturer;
	u8 general_settings;
	u8 sr_state;
	u8 srf1[WL1271_INI_MAX_SMART_REFLEX_PARAM];
	u8 srf2[WL1271_INI_MAX_SMART_REFLEX_PARAM];
	u8 srf3[WL1271_INI_MAX_SMART_REFLEX_PARAM];
} __packed;

#define WL1271_INI_RSSI_PROCESS_COMPENS_SIZE 15

struct wl1271_ini_band_params_2 {
	u8 rx_trace_insertion_loss;
	u8 tx_trace_loss;
	u8 rx_rssi_process_compens[WL1271_INI_RSSI_PROCESS_COMPENS_SIZE];
} __packed;

#define WL1271_INI_RATE_GROUP_COUNT 6
#define WL1271_INI_CHANNEL_COUNT_2 14

struct wl1271_ini_fem_params_2 {
	__le16 tx_bip_ref_pd_voltage;
	u8 tx_bip_ref_power;
	u8 tx_bip_ref_offset;
	u8 tx_per_rate_pwr_limits_normal[WL1271_INI_RATE_GROUP_COUNT];
	u8 tx_per_rate_pwr_limits_degraded[WL1271_INI_RATE_GROUP_COUNT];
	u8 tx_per_rate_pwr_limits_extreme[WL1271_INI_RATE_GROUP_COUNT];
	u8 tx_per_chan_pwr_limits_11b[WL1271_INI_CHANNEL_COUNT_2];
	u8 tx_per_chan_pwr_limits_ofdm[WL1271_INI_CHANNEL_COUNT_2];
	u8 tx_pd_vs_rate_offsets[WL1271_INI_RATE_GROUP_COUNT];
	u8 tx_ibias[WL1271_INI_RATE_GROUP_COUNT];
	u8 rx_fem_insertion_loss;
	u8 degraded_low_to_normal_thr;
	u8 normal_to_degraded_high_thr;
} __packed;

#define WL1271_INI_CHANNEL_COUNT_5 35
#define WL1271_INI_SUB_BAND_COUNT_5 7

struct wl1271_ini_band_params_5 {
	u8 rx_trace_insertion_loss[WL1271_INI_SUB_BAND_COUNT_5];
	u8 tx_trace_loss[WL1271_INI_SUB_BAND_COUNT_5];
	u8 rx_rssi_process_compens[WL1271_INI_RSSI_PROCESS_COMPENS_SIZE];
} __packed;

struct wl1271_ini_fem_params_5 {
	__le16 tx_bip_ref_pd_voltage[WL1271_INI_SUB_BAND_COUNT_5];
	u8 tx_bip_ref_power[WL1271_INI_SUB_BAND_COUNT_5];
	u8 tx_bip_ref_offset[WL1271_INI_SUB_BAND_COUNT_5];
	u8 tx_per_rate_pwr_limits_normal[WL1271_INI_RATE_GROUP_COUNT];
	u8 tx_per_rate_pwr_limits_degraded[WL1271_INI_RATE_GROUP_COUNT];
	u8 tx_per_rate_pwr_limits_extreme[WL1271_INI_RATE_GROUP_COUNT];
	u8 tx_per_chan_pwr_limits_ofdm[WL1271_INI_CHANNEL_COUNT_5];
	u8 tx_pd_vs_rate_offsets[WL1271_INI_RATE_GROUP_COUNT];
	u8 tx_ibias[WL1271_INI_RATE_GROUP_COUNT];
	u8 rx_fem_insertion_loss[WL1271_INI_SUB_BAND_COUNT_5];
	u8 degraded_low_to_normal_thr;
	u8 normal_to_degraded_high_thr;
} __packed;


/* NVS data structure */
#define WL1271_INI_NVS_SECTION_SIZE		     468
#define WL1271_INI_FEM_MODULE_COUNT                  2

#define WL1271_INI_LEGACY_NVS_FILE_SIZE              800

struct wl1271_nvs_file {
	/* NVS section */
	u8 nvs[WL1271_INI_NVS_SECTION_SIZE];

	/* INI section */
	struct wl1271_ini_general_params general_params;
	u8 padding1;
	struct wl1271_ini_band_params_2 stat_radio_params_2;
	u8 padding2;
	struct {
		struct wl1271_ini_fem_params_2 params;
		u8 padding;
	} dyn_radio_params_2[WL1271_INI_FEM_MODULE_COUNT];
	struct wl1271_ini_band_params_5 stat_radio_params_5;
	u8 padding3;
	struct {
		struct wl1271_ini_fem_params_5 params;
		u8 padding;
	} dyn_radio_params_5[WL1271_INI_FEM_MODULE_COUNT];
} __packed;

#endif
