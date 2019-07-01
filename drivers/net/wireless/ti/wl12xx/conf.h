/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2011 Texas Instruments Inc.
 */

#ifndef __WL12XX_CONF_H__
#define __WL12XX_CONF_H__

/* these are number of channels on the band divided by two, rounded up */
#define CONF_TX_PWR_COMPENSATION_LEN_2 7
#define CONF_TX_PWR_COMPENSATION_LEN_5 18

struct wl12xx_conf_rf {
	/*
	 * Per channel power compensation for 2.4GHz
	 *
	 * Range: s8
	 */
	u8 tx_per_channel_power_compensation_2[CONF_TX_PWR_COMPENSATION_LEN_2];

	/*
	 * Per channel power compensation for 5GHz
	 *
	 * Range: s8
	 */
	u8 tx_per_channel_power_compensation_5[CONF_TX_PWR_COMPENSATION_LEN_5];
};

struct wl12xx_priv_conf {
	struct wl12xx_conf_rf rf;
	struct conf_memory_settings mem_wl127x;
};

enum wl12xx_sg_params {
	/*
	* Configure the min and max time BT gains the antenna
	* in WLAN / BT master basic rate
	*
	* Range: 0 - 255 (ms)
	*/
	WL12XX_CONF_SG_ACL_BT_MASTER_MIN_BR = 0,
	WL12XX_CONF_SG_ACL_BT_MASTER_MAX_BR,

	/*
	* Configure the min and max time BT gains the antenna
	* in WLAN / BT slave basic rate
	*
	* Range: 0 - 255 (ms)
	*/
	WL12XX_CONF_SG_ACL_BT_SLAVE_MIN_BR,
	WL12XX_CONF_SG_ACL_BT_SLAVE_MAX_BR,

	/*
	* Configure the min and max time BT gains the antenna
	* in WLAN / BT master EDR
	*
	* Range: 0 - 255 (ms)
	*/
	WL12XX_CONF_SG_ACL_BT_MASTER_MIN_EDR,
	WL12XX_CONF_SG_ACL_BT_MASTER_MAX_EDR,

	/*
	* Configure the min and max time BT gains the antenna
	* in WLAN / BT slave EDR
	*
	* Range: 0 - 255 (ms)
	*/
	WL12XX_CONF_SG_ACL_BT_SLAVE_MIN_EDR,
	WL12XX_CONF_SG_ACL_BT_SLAVE_MAX_EDR,

	/*
	* The maximum time WLAN can gain the antenna
	* in WLAN PSM / BT master/slave BR
	*
	* Range: 0 - 255 (ms)
	*/
	WL12XX_CONF_SG_ACL_WLAN_PS_MASTER_BR,
	WL12XX_CONF_SG_ACL_WLAN_PS_SLAVE_BR,

	/*
	* The maximum time WLAN can gain the antenna
	* in WLAN PSM / BT master/slave EDR
	*
	* Range: 0 - 255 (ms)
	*/
	WL12XX_CONF_SG_ACL_WLAN_PS_MASTER_EDR,
	WL12XX_CONF_SG_ACL_WLAN_PS_SLAVE_EDR,

	/* TODO: explain these values */
	WL12XX_CONF_SG_ACL_WLAN_ACTIVE_MASTER_MIN_BR,
	WL12XX_CONF_SG_ACL_WLAN_ACTIVE_MASTER_MAX_BR,
	WL12XX_CONF_SG_ACL_WLAN_ACTIVE_SLAVE_MIN_BR,
	WL12XX_CONF_SG_ACL_WLAN_ACTIVE_SLAVE_MAX_BR,
	WL12XX_CONF_SG_ACL_WLAN_ACTIVE_MASTER_MIN_EDR,
	WL12XX_CONF_SG_ACL_WLAN_ACTIVE_MASTER_MAX_EDR,
	WL12XX_CONF_SG_ACL_WLAN_ACTIVE_SLAVE_MIN_EDR,
	WL12XX_CONF_SG_ACL_WLAN_ACTIVE_SLAVE_MAX_EDR,

	WL12XX_CONF_SG_ACL_ACTIVE_SCAN_WLAN_BR,
	WL12XX_CONF_SG_ACL_ACTIVE_SCAN_WLAN_EDR,
	WL12XX_CONF_SG_ACL_PASSIVE_SCAN_BT_BR,
	WL12XX_CONF_SG_ACL_PASSIVE_SCAN_WLAN_BR,
	WL12XX_CONF_SG_ACL_PASSIVE_SCAN_BT_EDR,
	WL12XX_CONF_SG_ACL_PASSIVE_SCAN_WLAN_EDR,

	/*
	* Compensation percentage of probe requests when scan initiated
	* during BT voice/ACL link.
	*
	* Range: 0 - 255 (%)
	*/
	WL12XX_CONF_SG_AUTO_SCAN_PROBE_REQ,

	/*
	* Compensation percentage of probe requests when active scan initiated
	* during BT voice
	*
	* Range: 0 - 255 (%)
	*/
	WL12XX_CONF_SG_ACTIVE_SCAN_DURATION_FACTOR_HV3,

	/*
	* Compensation percentage of WLAN active scan window if initiated
	* during BT A2DP
	*
	* Range: 0 - 1000 (%)
	*/
	WL12XX_CONF_SG_ACTIVE_SCAN_DURATION_FACTOR_A2DP,

	/*
	* Compensation percentage of WLAN passive scan window if initiated
	* during BT A2DP BR
	*
	* Range: 0 - 1000 (%)
	*/
	WL12XX_CONF_SG_PASSIVE_SCAN_DUR_FACTOR_A2DP_BR,

	/*
	* Compensation percentage of WLAN passive scan window if initiated
	* during BT A2DP EDR
	*
	* Range: 0 - 1000 (%)
	*/
	WL12XX_CONF_SG_PASSIVE_SCAN_DUR_FACTOR_A2DP_EDR,

	/*
	* Compensation percentage of WLAN passive scan window if initiated
	* during BT voice
	*
	* Range: 0 - 1000 (%)
	*/
	WL12XX_CONF_SG_PASSIVE_SCAN_DUR_FACTOR_HV3,

	/* TODO: explain these values */
	WL12XX_CONF_SG_CONSECUTIVE_HV3_IN_PASSIVE_SCAN,
	WL12XX_CONF_SG_BCN_HV3_COLL_THR_IN_PASSIVE_SCAN,
	WL12XX_CONF_SG_TX_RX_PROTECT_BW_IN_PASSIVE_SCAN,

	/*
	* Defines whether the SG will force WLAN host to enter/exit PSM
	*
	* Range: 1 - SG can force, 0 - host handles PSM
	*/
	WL12XX_CONF_SG_STA_FORCE_PS_IN_BT_SCO,

	/*
	* Defines antenna configuration (single/dual antenna)
	*
	* Range: 0 - single antenna, 1 - dual antenna
	*/
	WL12XX_CONF_SG_ANTENNA_CONFIGURATION,

	/*
	* The threshold (percent) of max consecutive beacon misses before
	* increasing priority of beacon reception.
	*
	* Range: 0 - 100 (%)
	*/
	WL12XX_CONF_SG_BEACON_MISS_PERCENT,

	/*
	* Protection time of the DHCP procedure.
	*
	* Range: 0 - 100000 (ms)
	*/
	WL12XX_CONF_SG_DHCP_TIME,

	/*
	* RX guard time before the beginning of a new BT voice frame during
	* which no new WLAN trigger frame is transmitted.
	*
	* Range: 0 - 100000 (us)
	*/
	WL12XX_CONF_SG_RXT,

	/*
	* TX guard time before the beginning of a new BT voice frame during
	* which no new WLAN frame is transmitted.
	*
	* Range: 0 - 100000 (us)
	*/
	WL12XX_CONF_SG_TXT,

	/*
	* Enable adaptive RXT/TXT algorithm. If disabled, the host values
	* will be utilized.
	*
	* Range: 0 - disable, 1 - enable
	*/
	WL12XX_CONF_SG_ADAPTIVE_RXT_TXT,

	/* TODO: explain this value */
	WL12XX_CONF_SG_GENERAL_USAGE_BIT_MAP,

	/*
	* Number of consecutive BT voice frames not interrupted by WLAN
	*
	* Range: 0 - 100
	*/
	WL12XX_CONF_SG_HV3_MAX_SERVED,

	/*
	* The used WLAN legacy service period during active BT ACL link
	*
	* Range: 0 - 255 (ms)
	*/
	WL12XX_CONF_SG_PS_POLL_TIMEOUT,

	/*
	* The used WLAN UPSD service period during active BT ACL link
	*
	* Range: 0 - 255 (ms)
	*/
	WL12XX_CONF_SG_UPSD_TIMEOUT,

	WL12XX_CONF_SG_CONSECUTIVE_CTS_THRESHOLD,
	WL12XX_CONF_SG_STA_RX_WINDOW_AFTER_DTIM,
	WL12XX_CONF_SG_STA_CONNECTION_PROTECTION_TIME,

	/* AP params */
	WL12XX_CONF_AP_BEACON_MISS_TX,
	WL12XX_CONF_AP_RX_WINDOW_AFTER_BEACON,
	WL12XX_CONF_AP_BEACON_WINDOW_INTERVAL,
	WL12XX_CONF_AP_CONNECTION_PROTECTION_TIME,
	WL12XX_CONF_AP_BT_ACL_VAL_BT_SERVE_TIME,
	WL12XX_CONF_AP_BT_ACL_VAL_WL_SERVE_TIME,

	/* CTS Diluting params */
	WL12XX_CONF_SG_CTS_DILUTED_BAD_RX_PACKETS_TH,
	WL12XX_CONF_SG_CTS_CHOP_IN_DUAL_ANT_SCO_MASTER,

	WL12XX_CONF_SG_TEMP_PARAM_1,
	WL12XX_CONF_SG_TEMP_PARAM_2,
	WL12XX_CONF_SG_TEMP_PARAM_3,
	WL12XX_CONF_SG_TEMP_PARAM_4,
	WL12XX_CONF_SG_TEMP_PARAM_5,
	WL12XX_CONF_SG_TEMP_PARAM_6,
	WL12XX_CONF_SG_TEMP_PARAM_7,
	WL12XX_CONF_SG_TEMP_PARAM_8,
	WL12XX_CONF_SG_TEMP_PARAM_9,
	WL12XX_CONF_SG_TEMP_PARAM_10,

	WL12XX_CONF_SG_PARAMS_MAX,
	WL12XX_CONF_SG_PARAMS_ALL = 0xff
};

#endif /* __WL12XX_CONF_H__ */
