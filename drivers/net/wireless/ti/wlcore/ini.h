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

#ifndef __INI_H__
#define __INI_H__

#define GENERAL_SETTINGS_DRPW_LPD 0xc0
#define SCRATCH_ENABLE_LPD        BIT(25)

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

#define WL128X_INI_MAX_SETTINGS_PARAM 4

struct wl128x_ini_general_params {
	u8 ref_clock;
	u8 settling_time;
	u8 clk_valid_on_wakeup;
	u8 tcxo_ref_clock;
	u8 tcxo_settling_time;
	u8 tcxo_valid_on_wakeup;
	u8 tcxo_ldo_voltage;
	u8 xtal_itrim_val;
	u8 platform_conf;
	u8 dual_mode_select;
	u8 tx_bip_fem_auto_detect;
	u8 tx_bip_fem_manufacturer;
	u8 general_settings[WL128X_INI_MAX_SETTINGS_PARAM];
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

#define WL1271_INI_CHANNEL_COUNT_2 14

struct wl128x_ini_band_params_2 {
	u8 rx_trace_insertion_loss;
	u8 tx_trace_loss[WL1271_INI_CHANNEL_COUNT_2];
	u8 rx_rssi_process_compens[WL1271_INI_RSSI_PROCESS_COMPENS_SIZE];
} __packed;

#define WL1271_INI_RATE_GROUP_COUNT 6

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

#define WL128X_INI_RATE_GROUP_COUNT 7
/* low and high temperatures */
#define WL128X_INI_PD_VS_TEMPERATURE_RANGES 2

struct wl128x_ini_fem_params_2 {
	__le16 tx_bip_ref_pd_voltage;
	u8 tx_bip_ref_power;
	u8 tx_bip_ref_offset;
	u8 tx_per_rate_pwr_limits_normal[WL128X_INI_RATE_GROUP_COUNT];
	u8 tx_per_rate_pwr_limits_degraded[WL128X_INI_RATE_GROUP_COUNT];
	u8 tx_per_rate_pwr_limits_extreme[WL128X_INI_RATE_GROUP_COUNT];
	u8 tx_per_chan_pwr_limits_11b[WL1271_INI_CHANNEL_COUNT_2];
	u8 tx_per_chan_pwr_limits_ofdm[WL1271_INI_CHANNEL_COUNT_2];
	u8 tx_pd_vs_rate_offsets[WL128X_INI_RATE_GROUP_COUNT];
	u8 tx_ibias[WL128X_INI_RATE_GROUP_COUNT + 1];
	u8 tx_pd_vs_chan_offsets[WL1271_INI_CHANNEL_COUNT_2];
	u8 tx_pd_vs_temperature[WL128X_INI_PD_VS_TEMPERATURE_RANGES];
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

struct wl128x_ini_band_params_5 {
	u8 rx_trace_insertion_loss[WL1271_INI_SUB_BAND_COUNT_5];
	u8 tx_trace_loss[WL1271_INI_CHANNEL_COUNT_5];
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

struct wl128x_ini_fem_params_5 {
	__le16 tx_bip_ref_pd_voltage[WL1271_INI_SUB_BAND_COUNT_5];
	u8 tx_bip_ref_power[WL1271_INI_SUB_BAND_COUNT_5];
	u8 tx_bip_ref_offset[WL1271_INI_SUB_BAND_COUNT_5];
	u8 tx_per_rate_pwr_limits_normal[WL128X_INI_RATE_GROUP_COUNT];
	u8 tx_per_rate_pwr_limits_degraded[WL128X_INI_RATE_GROUP_COUNT];
	u8 tx_per_rate_pwr_limits_extreme[WL128X_INI_RATE_GROUP_COUNT];
	u8 tx_per_chan_pwr_limits_ofdm[WL1271_INI_CHANNEL_COUNT_5];
	u8 tx_pd_vs_rate_offsets[WL128X_INI_RATE_GROUP_COUNT];
	u8 tx_ibias[WL128X_INI_RATE_GROUP_COUNT];
	u8 tx_pd_vs_chan_offsets[WL1271_INI_CHANNEL_COUNT_5];
	u8 tx_pd_vs_temperature[WL1271_INI_SUB_BAND_COUNT_5 *
		WL128X_INI_PD_VS_TEMPERATURE_RANGES];
	u8 rx_fem_insertion_loss[WL1271_INI_SUB_BAND_COUNT_5];
	u8 degraded_low_to_normal_thr;
	u8 normal_to_degraded_high_thr;
} __packed;

/* NVS data structure */
#define WL1271_INI_NVS_SECTION_SIZE		     468

/* We have four FEM module types: 0-RFMD, 1-TQS, 2-SKW, 3-TQS_HP */
#define WL1271_INI_FEM_MODULE_COUNT                  4

/*
 * In NVS we only store two FEM module entries -
 *	  FEM modules 0,2,3 are stored in entry 0
 *	  FEM module 1 is stored in entry 1
 */
#define WL12XX_NVS_FEM_MODULE_COUNT                  2

#define WL12XX_FEM_TO_NVS_ENTRY(ini_fem_module)      \
	((ini_fem_module) == 1 ? 1 : 0)

#define WL1271_INI_LEGACY_NVS_FILE_SIZE              800

struct wl1271_nvs_file {
	/* NVS section - must be first! */
	u8 nvs[WL1271_INI_NVS_SECTION_SIZE];

	/* INI section */
	struct wl1271_ini_general_params general_params;
	u8 padding1;
	struct wl1271_ini_band_params_2 stat_radio_params_2;
	u8 padding2;
	struct {
		struct wl1271_ini_fem_params_2 params;
		u8 padding;
	} dyn_radio_params_2[WL12XX_NVS_FEM_MODULE_COUNT];
	struct wl1271_ini_band_params_5 stat_radio_params_5;
	u8 padding3;
	struct {
		struct wl1271_ini_fem_params_5 params;
		u8 padding;
	} dyn_radio_params_5[WL12XX_NVS_FEM_MODULE_COUNT];
} __packed;

struct wl128x_nvs_file {
	/* NVS section - must be first! */
	u8 nvs[WL1271_INI_NVS_SECTION_SIZE];

	/* INI section */
	struct wl128x_ini_general_params general_params;
	u8 fem_vendor_and_options;
	struct wl128x_ini_band_params_2 stat_radio_params_2;
	u8 padding2;
	struct {
		struct wl128x_ini_fem_params_2 params;
		u8 padding;
	} dyn_radio_params_2[WL12XX_NVS_FEM_MODULE_COUNT];
	struct wl128x_ini_band_params_5 stat_radio_params_5;
	u8 padding3;
	struct {
		struct wl128x_ini_fem_params_5 params;
		u8 padding;
	} dyn_radio_params_5[WL12XX_NVS_FEM_MODULE_COUNT];
} __packed;
#endif
