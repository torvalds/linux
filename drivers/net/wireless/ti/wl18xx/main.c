/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2011 Texas Instruments
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ip.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>

#include "../wlcore/wlcore.h"
#include "../wlcore/debug.h"
#include "../wlcore/io.h"
#include "../wlcore/acx.h"
#include "../wlcore/tx.h"
#include "../wlcore/rx.h"
#include "../wlcore/boot.h"

#include "reg.h"
#include "conf.h"
#include "cmd.h"
#include "acx.h"
#include "tx.h"
#include "wl18xx.h"
#include "io.h"
#include "scan.h"
#include "event.h"
#include "debugfs.h"

#define WL18XX_RX_CHECKSUM_MASK      0x40

static char *ht_mode_param = NULL;
static char *board_type_param = NULL;
static bool checksum_param = false;
static int num_rx_desc_param = -1;

/* phy paramters */
static int dc2dc_param = -1;
static int n_antennas_2_param = -1;
static int n_antennas_5_param = -1;
static int low_band_component_param = -1;
static int low_band_component_type_param = -1;
static int high_band_component_param = -1;
static int high_band_component_type_param = -1;
static int pwr_limit_reference_11_abg_param = -1;

static const u8 wl18xx_rate_to_idx_2ghz[] = {
	/* MCS rates are used only with 11n */
	15,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS15 */
	14,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS14 */
	13,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS13 */
	12,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS12 */
	11,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS11 */
	10,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS10 */
	9,                             /* WL18XX_CONF_HW_RXTX_RATE_MCS9 */
	8,                             /* WL18XX_CONF_HW_RXTX_RATE_MCS8 */
	7,                             /* WL18XX_CONF_HW_RXTX_RATE_MCS7 */
	6,                             /* WL18XX_CONF_HW_RXTX_RATE_MCS6 */
	5,                             /* WL18XX_CONF_HW_RXTX_RATE_MCS5 */
	4,                             /* WL18XX_CONF_HW_RXTX_RATE_MCS4 */
	3,                             /* WL18XX_CONF_HW_RXTX_RATE_MCS3 */
	2,                             /* WL18XX_CONF_HW_RXTX_RATE_MCS2 */
	1,                             /* WL18XX_CONF_HW_RXTX_RATE_MCS1 */
	0,                             /* WL18XX_CONF_HW_RXTX_RATE_MCS0 */

	11,                            /* WL18XX_CONF_HW_RXTX_RATE_54   */
	10,                            /* WL18XX_CONF_HW_RXTX_RATE_48   */
	9,                             /* WL18XX_CONF_HW_RXTX_RATE_36   */
	8,                             /* WL18XX_CONF_HW_RXTX_RATE_24   */

	/* TI-specific rate */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* WL18XX_CONF_HW_RXTX_RATE_22   */

	7,                             /* WL18XX_CONF_HW_RXTX_RATE_18   */
	6,                             /* WL18XX_CONF_HW_RXTX_RATE_12   */
	3,                             /* WL18XX_CONF_HW_RXTX_RATE_11   */
	5,                             /* WL18XX_CONF_HW_RXTX_RATE_9    */
	4,                             /* WL18XX_CONF_HW_RXTX_RATE_6    */
	2,                             /* WL18XX_CONF_HW_RXTX_RATE_5_5  */
	1,                             /* WL18XX_CONF_HW_RXTX_RATE_2    */
	0                              /* WL18XX_CONF_HW_RXTX_RATE_1    */
};

static const u8 wl18xx_rate_to_idx_5ghz[] = {
	/* MCS rates are used only with 11n */
	15,                           /* WL18XX_CONF_HW_RXTX_RATE_MCS15 */
	14,                           /* WL18XX_CONF_HW_RXTX_RATE_MCS14 */
	13,                           /* WL18XX_CONF_HW_RXTX_RATE_MCS13 */
	12,                           /* WL18XX_CONF_HW_RXTX_RATE_MCS12 */
	11,                           /* WL18XX_CONF_HW_RXTX_RATE_MCS11 */
	10,                           /* WL18XX_CONF_HW_RXTX_RATE_MCS10 */
	9,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS9 */
	8,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS8 */
	7,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS7 */
	6,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS6 */
	5,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS5 */
	4,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS4 */
	3,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS3 */
	2,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS2 */
	1,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS1 */
	0,                            /* WL18XX_CONF_HW_RXTX_RATE_MCS0 */

	7,                             /* WL18XX_CONF_HW_RXTX_RATE_54   */
	6,                             /* WL18XX_CONF_HW_RXTX_RATE_48   */
	5,                             /* WL18XX_CONF_HW_RXTX_RATE_36   */
	4,                             /* WL18XX_CONF_HW_RXTX_RATE_24   */

	/* TI-specific rate */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* WL18XX_CONF_HW_RXTX_RATE_22   */

	3,                             /* WL18XX_CONF_HW_RXTX_RATE_18   */
	2,                             /* WL18XX_CONF_HW_RXTX_RATE_12   */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* WL18XX_CONF_HW_RXTX_RATE_11   */
	1,                             /* WL18XX_CONF_HW_RXTX_RATE_9    */
	0,                             /* WL18XX_CONF_HW_RXTX_RATE_6    */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* WL18XX_CONF_HW_RXTX_RATE_5_5  */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* WL18XX_CONF_HW_RXTX_RATE_2    */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* WL18XX_CONF_HW_RXTX_RATE_1    */
};

static const u8 *wl18xx_band_rate_to_idx[] = {
	[IEEE80211_BAND_2GHZ] = wl18xx_rate_to_idx_2ghz,
	[IEEE80211_BAND_5GHZ] = wl18xx_rate_to_idx_5ghz
};

enum wl18xx_hw_rates {
	WL18XX_CONF_HW_RXTX_RATE_MCS15 = 0,
	WL18XX_CONF_HW_RXTX_RATE_MCS14,
	WL18XX_CONF_HW_RXTX_RATE_MCS13,
	WL18XX_CONF_HW_RXTX_RATE_MCS12,
	WL18XX_CONF_HW_RXTX_RATE_MCS11,
	WL18XX_CONF_HW_RXTX_RATE_MCS10,
	WL18XX_CONF_HW_RXTX_RATE_MCS9,
	WL18XX_CONF_HW_RXTX_RATE_MCS8,
	WL18XX_CONF_HW_RXTX_RATE_MCS7,
	WL18XX_CONF_HW_RXTX_RATE_MCS6,
	WL18XX_CONF_HW_RXTX_RATE_MCS5,
	WL18XX_CONF_HW_RXTX_RATE_MCS4,
	WL18XX_CONF_HW_RXTX_RATE_MCS3,
	WL18XX_CONF_HW_RXTX_RATE_MCS2,
	WL18XX_CONF_HW_RXTX_RATE_MCS1,
	WL18XX_CONF_HW_RXTX_RATE_MCS0,
	WL18XX_CONF_HW_RXTX_RATE_54,
	WL18XX_CONF_HW_RXTX_RATE_48,
	WL18XX_CONF_HW_RXTX_RATE_36,
	WL18XX_CONF_HW_RXTX_RATE_24,
	WL18XX_CONF_HW_RXTX_RATE_22,
	WL18XX_CONF_HW_RXTX_RATE_18,
	WL18XX_CONF_HW_RXTX_RATE_12,
	WL18XX_CONF_HW_RXTX_RATE_11,
	WL18XX_CONF_HW_RXTX_RATE_9,
	WL18XX_CONF_HW_RXTX_RATE_6,
	WL18XX_CONF_HW_RXTX_RATE_5_5,
	WL18XX_CONF_HW_RXTX_RATE_2,
	WL18XX_CONF_HW_RXTX_RATE_1,
	WL18XX_CONF_HW_RXTX_RATE_MAX,
};

static struct wlcore_conf wl18xx_conf = {
	.sg = {
		.params = {
			[CONF_SG_ACL_BT_MASTER_MIN_BR] = 10,
			[CONF_SG_ACL_BT_MASTER_MAX_BR] = 180,
			[CONF_SG_ACL_BT_SLAVE_MIN_BR] = 10,
			[CONF_SG_ACL_BT_SLAVE_MAX_BR] = 180,
			[CONF_SG_ACL_BT_MASTER_MIN_EDR] = 10,
			[CONF_SG_ACL_BT_MASTER_MAX_EDR] = 80,
			[CONF_SG_ACL_BT_SLAVE_MIN_EDR] = 10,
			[CONF_SG_ACL_BT_SLAVE_MAX_EDR] = 80,
			[CONF_SG_ACL_WLAN_PS_MASTER_BR] = 8,
			[CONF_SG_ACL_WLAN_PS_SLAVE_BR] = 8,
			[CONF_SG_ACL_WLAN_PS_MASTER_EDR] = 20,
			[CONF_SG_ACL_WLAN_PS_SLAVE_EDR] = 20,
			[CONF_SG_ACL_WLAN_ACTIVE_MASTER_MIN_BR] = 20,
			[CONF_SG_ACL_WLAN_ACTIVE_MASTER_MAX_BR] = 35,
			[CONF_SG_ACL_WLAN_ACTIVE_SLAVE_MIN_BR] = 16,
			[CONF_SG_ACL_WLAN_ACTIVE_SLAVE_MAX_BR] = 35,
			[CONF_SG_ACL_WLAN_ACTIVE_MASTER_MIN_EDR] = 32,
			[CONF_SG_ACL_WLAN_ACTIVE_MASTER_MAX_EDR] = 50,
			[CONF_SG_ACL_WLAN_ACTIVE_SLAVE_MIN_EDR] = 28,
			[CONF_SG_ACL_WLAN_ACTIVE_SLAVE_MAX_EDR] = 50,
			[CONF_SG_ACL_ACTIVE_SCAN_WLAN_BR] = 10,
			[CONF_SG_ACL_ACTIVE_SCAN_WLAN_EDR] = 20,
			[CONF_SG_ACL_PASSIVE_SCAN_BT_BR] = 75,
			[CONF_SG_ACL_PASSIVE_SCAN_WLAN_BR] = 15,
			[CONF_SG_ACL_PASSIVE_SCAN_BT_EDR] = 27,
			[CONF_SG_ACL_PASSIVE_SCAN_WLAN_EDR] = 17,
			/* active scan params */
			[CONF_SG_AUTO_SCAN_PROBE_REQ] = 170,
			[CONF_SG_ACTIVE_SCAN_DURATION_FACTOR_HV3] = 50,
			[CONF_SG_ACTIVE_SCAN_DURATION_FACTOR_A2DP] = 100,
			/* passive scan params */
			[CONF_SG_PASSIVE_SCAN_DURATION_FACTOR_A2DP_BR] = 800,
			[CONF_SG_PASSIVE_SCAN_DURATION_FACTOR_A2DP_EDR] = 200,
			[CONF_SG_PASSIVE_SCAN_DURATION_FACTOR_HV3] = 200,
			/* passive scan in dual antenna params */
			[CONF_SG_CONSECUTIVE_HV3_IN_PASSIVE_SCAN] = 0,
			[CONF_SG_BCN_HV3_COLLISION_THRESH_IN_PASSIVE_SCAN] = 0,
			[CONF_SG_TX_RX_PROTECTION_BWIDTH_IN_PASSIVE_SCAN] = 0,
			/* general params */
			[CONF_SG_STA_FORCE_PS_IN_BT_SCO] = 1,
			[CONF_SG_ANTENNA_CONFIGURATION] = 0,
			[CONF_SG_BEACON_MISS_PERCENT] = 60,
			[CONF_SG_DHCP_TIME] = 5000,
			[CONF_SG_RXT] = 1200,
			[CONF_SG_TXT] = 1000,
			[CONF_SG_ADAPTIVE_RXT_TXT] = 1,
			[CONF_SG_GENERAL_USAGE_BIT_MAP] = 3,
			[CONF_SG_HV3_MAX_SERVED] = 6,
			[CONF_SG_PS_POLL_TIMEOUT] = 10,
			[CONF_SG_UPSD_TIMEOUT] = 10,
			[CONF_SG_CONSECUTIVE_CTS_THRESHOLD] = 2,
			[CONF_SG_STA_RX_WINDOW_AFTER_DTIM] = 5,
			[CONF_SG_STA_CONNECTION_PROTECTION_TIME] = 30,
			/* AP params */
			[CONF_AP_BEACON_MISS_TX] = 3,
			[CONF_AP_RX_WINDOW_AFTER_BEACON] = 10,
			[CONF_AP_BEACON_WINDOW_INTERVAL] = 2,
			[CONF_AP_CONNECTION_PROTECTION_TIME] = 0,
			[CONF_AP_BT_ACL_VAL_BT_SERVE_TIME] = 25,
			[CONF_AP_BT_ACL_VAL_WL_SERVE_TIME] = 25,
			/* CTS Diluting params */
			[CONF_SG_CTS_DILUTED_BAD_RX_PACKETS_TH] = 0,
			[CONF_SG_CTS_CHOP_IN_DUAL_ANT_SCO_MASTER] = 0,
		},
		.state = CONF_SG_PROTECTIVE,
	},
	.rx = {
		.rx_msdu_life_time           = 512000,
		.packet_detection_threshold  = 0,
		.ps_poll_timeout             = 15,
		.upsd_timeout                = 15,
		.rts_threshold               = IEEE80211_MAX_RTS_THRESHOLD,
		.rx_cca_threshold            = 0,
		.irq_blk_threshold           = 0xFFFF,
		.irq_pkt_threshold           = 0,
		.irq_timeout                 = 600,
		.queue_type                  = CONF_RX_QUEUE_TYPE_LOW_PRIORITY,
	},
	.tx = {
		.tx_energy_detection         = 0,
		.sta_rc_conf                 = {
			.enabled_rates       = 0,
			.short_retry_limit   = 10,
			.long_retry_limit    = 10,
			.aflags              = 0,
		},
		.ac_conf_count               = 4,
		.ac_conf                     = {
			[CONF_TX_AC_BE] = {
				.ac          = CONF_TX_AC_BE,
				.cw_min      = 15,
				.cw_max      = 63,
				.aifsn       = 3,
				.tx_op_limit = 0,
			},
			[CONF_TX_AC_BK] = {
				.ac          = CONF_TX_AC_BK,
				.cw_min      = 15,
				.cw_max      = 63,
				.aifsn       = 7,
				.tx_op_limit = 0,
			},
			[CONF_TX_AC_VI] = {
				.ac          = CONF_TX_AC_VI,
				.cw_min      = 15,
				.cw_max      = 63,
				.aifsn       = CONF_TX_AIFS_PIFS,
				.tx_op_limit = 3008,
			},
			[CONF_TX_AC_VO] = {
				.ac          = CONF_TX_AC_VO,
				.cw_min      = 15,
				.cw_max      = 63,
				.aifsn       = CONF_TX_AIFS_PIFS,
				.tx_op_limit = 1504,
			},
		},
		.max_tx_retries = 100,
		.ap_aging_period = 300,
		.tid_conf_count = 4,
		.tid_conf = {
			[CONF_TX_AC_BE] = {
				.queue_id    = CONF_TX_AC_BE,
				.channel_type = CONF_CHANNEL_TYPE_EDCF,
				.tsid        = CONF_TX_AC_BE,
				.ps_scheme   = CONF_PS_SCHEME_LEGACY,
				.ack_policy  = CONF_ACK_POLICY_LEGACY,
				.apsd_conf   = {0, 0},
			},
			[CONF_TX_AC_BK] = {
				.queue_id    = CONF_TX_AC_BK,
				.channel_type = CONF_CHANNEL_TYPE_EDCF,
				.tsid        = CONF_TX_AC_BK,
				.ps_scheme   = CONF_PS_SCHEME_LEGACY,
				.ack_policy  = CONF_ACK_POLICY_LEGACY,
				.apsd_conf   = {0, 0},
			},
			[CONF_TX_AC_VI] = {
				.queue_id    = CONF_TX_AC_VI,
				.channel_type = CONF_CHANNEL_TYPE_EDCF,
				.tsid        = CONF_TX_AC_VI,
				.ps_scheme   = CONF_PS_SCHEME_LEGACY,
				.ack_policy  = CONF_ACK_POLICY_LEGACY,
				.apsd_conf   = {0, 0},
			},
			[CONF_TX_AC_VO] = {
				.queue_id    = CONF_TX_AC_VO,
				.channel_type = CONF_CHANNEL_TYPE_EDCF,
				.tsid        = CONF_TX_AC_VO,
				.ps_scheme   = CONF_PS_SCHEME_LEGACY,
				.ack_policy  = CONF_ACK_POLICY_LEGACY,
				.apsd_conf   = {0, 0},
			},
		},
		.frag_threshold              = IEEE80211_MAX_FRAG_THRESHOLD,
		.tx_compl_timeout            = 350,
		.tx_compl_threshold          = 10,
		.basic_rate                  = CONF_HW_BIT_RATE_1MBPS,
		.basic_rate_5                = CONF_HW_BIT_RATE_6MBPS,
		.tmpl_short_retry_limit      = 10,
		.tmpl_long_retry_limit       = 10,
		.tx_watchdog_timeout         = 5000,
		.slow_link_thold             = 3,
		.fast_link_thold             = 30,
	},
	.conn = {
		.wake_up_event               = CONF_WAKE_UP_EVENT_DTIM,
		.listen_interval             = 1,
		.suspend_wake_up_event       = CONF_WAKE_UP_EVENT_N_DTIM,
		.suspend_listen_interval     = 3,
		.bcn_filt_mode               = CONF_BCN_FILT_MODE_ENABLED,
		.bcn_filt_ie_count           = 3,
		.bcn_filt_ie = {
			[0] = {
				.ie          = WLAN_EID_CHANNEL_SWITCH,
				.rule        = CONF_BCN_RULE_PASS_ON_APPEARANCE,
			},
			[1] = {
				.ie          = WLAN_EID_HT_OPERATION,
				.rule        = CONF_BCN_RULE_PASS_ON_CHANGE,
			},
			[2] = {
				.ie	     = WLAN_EID_ERP_INFO,
				.rule	     = CONF_BCN_RULE_PASS_ON_CHANGE,
			},
		},
		.synch_fail_thold            = 12,
		.bss_lose_timeout            = 400,
		.beacon_rx_timeout           = 10000,
		.broadcast_timeout           = 20000,
		.rx_broadcast_in_ps          = 1,
		.ps_poll_threshold           = 10,
		.bet_enable                  = CONF_BET_MODE_ENABLE,
		.bet_max_consecutive         = 50,
		.psm_entry_retries           = 8,
		.psm_exit_retries            = 16,
		.psm_entry_nullfunc_retries  = 3,
		.dynamic_ps_timeout          = 1500,
		.forced_ps                   = false,
		.keep_alive_interval         = 55000,
		.max_listen_interval         = 20,
		.sta_sleep_auth              = WL1271_PSM_ILLEGAL,
	},
	.itrim = {
		.enable = false,
		.timeout = 50000,
	},
	.pm_config = {
		.host_clk_settling_time = 5000,
		.host_fast_wakeup_support = CONF_FAST_WAKEUP_DISABLE,
	},
	.roam_trigger = {
		.trigger_pacing               = 1,
		.avg_weight_rssi_beacon       = 20,
		.avg_weight_rssi_data         = 10,
		.avg_weight_snr_beacon        = 20,
		.avg_weight_snr_data          = 10,
	},
	.scan = {
		.min_dwell_time_active        = 7500,
		.max_dwell_time_active        = 30000,
		.min_dwell_time_active_long   = 25000,
		.max_dwell_time_active_long   = 50000,
		.dwell_time_passive           = 100000,
		.dwell_time_dfs               = 150000,
		.num_probe_reqs               = 2,
		.split_scan_timeout           = 50000,
	},
	.sched_scan = {
		/*
		 * Values are in TU/1000 but since sched scan FW command
		 * params are in TUs rounding up may occur.
		 */
		.base_dwell_time		= 7500,
		.max_dwell_time_delta		= 22500,
		/* based on 250bits per probe @1Mbps */
		.dwell_time_delta_per_probe	= 2000,
		/* based on 250bits per probe @6Mbps (plus a bit more) */
		.dwell_time_delta_per_probe_5	= 350,
		.dwell_time_passive		= 100000,
		.dwell_time_dfs			= 150000,
		.num_probe_reqs			= 2,
		.rssi_threshold			= -90,
		.snr_threshold			= 0,
	},
	.ht = {
		.rx_ba_win_size = 32,
		.tx_ba_win_size = 64,
		.inactivity_timeout = 10000,
		.tx_ba_tid_bitmap = CONF_TX_BA_ENABLED_TID_BITMAP,
	},
	.mem = {
		.num_stations                 = 1,
		.ssid_profiles                = 1,
		.rx_block_num                 = 40,
		.tx_min_block_num             = 40,
		.dynamic_memory               = 1,
		.min_req_tx_blocks            = 45,
		.min_req_rx_blocks            = 22,
		.tx_min                       = 27,
	},
	.fm_coex = {
		.enable                       = true,
		.swallow_period               = 5,
		.n_divider_fref_set_1         = 0xff,       /* default */
		.n_divider_fref_set_2         = 12,
		.m_divider_fref_set_1         = 0xffff,
		.m_divider_fref_set_2         = 148,        /* default */
		.coex_pll_stabilization_time  = 0xffffffff, /* default */
		.ldo_stabilization_time       = 0xffff,     /* default */
		.fm_disturbed_band_margin     = 0xff,       /* default */
		.swallow_clk_diff             = 0xff,       /* default */
	},
	.rx_streaming = {
		.duration                      = 150,
		.queues                        = 0x1,
		.interval                      = 20,
		.always                        = 0,
	},
	.fwlog = {
		.mode                         = WL12XX_FWLOG_CONTINUOUS,
		.mem_blocks                   = 2,
		.severity                     = 0,
		.timestamp                    = WL12XX_FWLOG_TIMESTAMP_DISABLED,
		.output                       = WL12XX_FWLOG_OUTPUT_DBG_PINS,
		.threshold                    = 0,
	},
	.rate = {
		.rate_retry_score = 32000,
		.per_add = 8192,
		.per_th1 = 2048,
		.per_th2 = 4096,
		.max_per = 8100,
		.inverse_curiosity_factor = 5,
		.tx_fail_low_th = 4,
		.tx_fail_high_th = 10,
		.per_alpha_shift = 4,
		.per_add_shift = 13,
		.per_beta1_shift = 10,
		.per_beta2_shift = 8,
		.rate_check_up = 2,
		.rate_check_down = 12,
		.rate_retry_policy = {
			0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00,
		},
	},
	.hangover = {
		.recover_time               = 0,
		.hangover_period            = 20,
		.dynamic_mode               = 1,
		.early_termination_mode     = 1,
		.max_period                 = 20,
		.min_period                 = 1,
		.increase_delta             = 1,
		.decrease_delta             = 2,
		.quiet_time                 = 4,
		.increase_time              = 1,
		.window_size                = 16,
	},
	.recovery = {
		.bug_on_recovery	    = 0,
		.no_recovery		    = 0,
	},
};

static struct wl18xx_priv_conf wl18xx_default_priv_conf = {
	.ht = {
		.mode				= HT_MODE_WIDE,
	},
	.phy = {
		.phy_standalone			= 0x00,
		.primary_clock_setting_time	= 0x05,
		.clock_valid_on_wake_up		= 0x00,
		.secondary_clock_setting_time	= 0x05,
		.board_type 			= BOARD_TYPE_HDK_18XX,
		.auto_detect			= 0x00,
		.dedicated_fem			= FEM_NONE,
		.low_band_component		= COMPONENT_3_WAY_SWITCH,
		.low_band_component_type	= 0x05,
		.high_band_component		= COMPONENT_2_WAY_SWITCH,
		.high_band_component_type	= 0x09,
		.tcxo_ldo_voltage		= 0x00,
		.xtal_itrim_val			= 0x04,
		.srf_state			= 0x00,
		.io_configuration		= 0x01,
		.sdio_configuration		= 0x00,
		.settings			= 0x00,
		.enable_clpc			= 0x00,
		.enable_tx_low_pwr_on_siso_rdl	= 0x00,
		.rx_profile			= 0x00,
		.pwr_limit_reference_11_abg	= 0x64,
		.per_chan_pwr_limit_arr_11abg	= {
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
		.pwr_limit_reference_11p	= 0x64,
		.per_chan_bo_mode_11_abg	= { 0x00, 0x00, 0x00, 0x00,
						    0x00, 0x00, 0x00, 0x00,
						    0x00, 0x00, 0x00, 0x00,
						    0x00 },
		.per_chan_bo_mode_11_p		= { 0x00, 0x00, 0x00, 0x00 },
		.per_chan_pwr_limit_arr_11p	= { 0xff, 0xff, 0xff, 0xff,
						    0xff, 0xff, 0xff },
		.psat				= 0,
		.external_pa_dc2dc		= 0,
		.number_of_assembled_ant2_4	= 2,
		.number_of_assembled_ant5	= 1,
		.low_power_val			= 0xff,
		.med_power_val			= 0xff,
		.high_power_val			= 0xff,
		.low_power_val_2nd		= 0xff,
		.med_power_val_2nd		= 0xff,
		.high_power_val_2nd		= 0xff,
		.tx_rf_margin			= 1,
	},
};

static const struct wlcore_partition_set wl18xx_ptable[PART_TABLE_LEN] = {
	[PART_TOP_PRCM_ELP_SOC] = {
		.mem  = { .start = 0x00A02000, .size  = 0x00010000 },
		.reg  = { .start = 0x00807000, .size  = 0x00005000 },
		.mem2 = { .start = 0x00800000, .size  = 0x0000B000 },
		.mem3 = { .start = 0x00000000, .size  = 0x00000000 },
	},
	[PART_DOWN] = {
		.mem  = { .start = 0x00000000, .size  = 0x00014000 },
		.reg  = { .start = 0x00810000, .size  = 0x0000BFFF },
		.mem2 = { .start = 0x00000000, .size  = 0x00000000 },
		.mem3 = { .start = 0x00000000, .size  = 0x00000000 },
	},
	[PART_BOOT] = {
		.mem  = { .start = 0x00700000, .size = 0x0000030c },
		.reg  = { .start = 0x00802000, .size = 0x00014578 },
		.mem2 = { .start = 0x00B00404, .size = 0x00001000 },
		.mem3 = { .start = 0x00C00000, .size = 0x00000400 },
	},
	[PART_WORK] = {
		.mem  = { .start = 0x00800000, .size  = 0x000050FC },
		.reg  = { .start = 0x00B00404, .size  = 0x00001000 },
		.mem2 = { .start = 0x00C00000, .size  = 0x00000400 },
		.mem3 = { .start = 0x00000000, .size  = 0x00000000 },
	},
	[PART_PHY_INIT] = {
		.mem  = { .start = WL18XX_PHY_INIT_MEM_ADDR,
			  .size  = WL18XX_PHY_INIT_MEM_SIZE },
		.reg  = { .start = 0x00000000, .size = 0x00000000 },
		.mem2 = { .start = 0x00000000, .size = 0x00000000 },
		.mem3 = { .start = 0x00000000, .size = 0x00000000 },
	},
};

static const int wl18xx_rtable[REG_TABLE_LEN] = {
	[REG_ECPU_CONTROL]		= WL18XX_REG_ECPU_CONTROL,
	[REG_INTERRUPT_NO_CLEAR]	= WL18XX_REG_INTERRUPT_NO_CLEAR,
	[REG_INTERRUPT_ACK]		= WL18XX_REG_INTERRUPT_ACK,
	[REG_COMMAND_MAILBOX_PTR]	= WL18XX_REG_COMMAND_MAILBOX_PTR,
	[REG_EVENT_MAILBOX_PTR]		= WL18XX_REG_EVENT_MAILBOX_PTR,
	[REG_INTERRUPT_TRIG]		= WL18XX_REG_INTERRUPT_TRIG_H,
	[REG_INTERRUPT_MASK]		= WL18XX_REG_INTERRUPT_MASK,
	[REG_PC_ON_RECOVERY]		= WL18XX_SCR_PAD4,
	[REG_CHIP_ID_B]			= WL18XX_REG_CHIP_ID_B,
	[REG_CMD_MBOX_ADDRESS]		= WL18XX_CMD_MBOX_ADDRESS,

	/* data access memory addresses, used with partition translation */
	[REG_SLV_MEM_DATA]		= WL18XX_SLV_MEM_DATA,
	[REG_SLV_REG_DATA]		= WL18XX_SLV_REG_DATA,

	/* raw data access memory addresses */
	[REG_RAW_FW_STATUS_ADDR]	= WL18XX_FW_STATUS_ADDR,
};

static const struct wl18xx_clk_cfg wl18xx_clk_table_coex[NUM_CLOCK_CONFIGS] = {
	[CLOCK_CONFIG_16_2_M]	= { 8,  121, 0, 0, false },
	[CLOCK_CONFIG_16_368_M]	= { 8,  120, 0, 0, false },
	[CLOCK_CONFIG_16_8_M]	= { 8,  117, 0, 0, false },
	[CLOCK_CONFIG_19_2_M]	= { 10, 128, 0, 0, false },
	[CLOCK_CONFIG_26_M]	= { 11, 104, 0, 0, false },
	[CLOCK_CONFIG_32_736_M]	= { 8,  120, 0, 0, false },
	[CLOCK_CONFIG_33_6_M]	= { 8,  117, 0, 0, false },
	[CLOCK_CONFIG_38_468_M]	= { 10, 128, 0, 0, false },
	[CLOCK_CONFIG_52_M]	= { 11, 104, 0, 0, false },
};

static const struct wl18xx_clk_cfg wl18xx_clk_table[NUM_CLOCK_CONFIGS] = {
	[CLOCK_CONFIG_16_2_M]	= { 7,  104,  801, 4,  true },
	[CLOCK_CONFIG_16_368_M]	= { 9,  132, 3751, 4,  true },
	[CLOCK_CONFIG_16_8_M]	= { 7,  100,    0, 0, false },
	[CLOCK_CONFIG_19_2_M]	= { 8,  100,    0, 0, false },
	[CLOCK_CONFIG_26_M]	= { 13, 120,    0, 0, false },
	[CLOCK_CONFIG_32_736_M]	= { 9,  132, 3751, 4,  true },
	[CLOCK_CONFIG_33_6_M]	= { 7,  100,    0, 0, false },
	[CLOCK_CONFIG_38_468_M]	= { 8,  100,    0, 0, false },
	[CLOCK_CONFIG_52_M]	= { 13, 120,    0, 0, false },
};

/* TODO: maybe move to a new header file? */
#define WL18XX_FW_NAME "ti-connectivity/wl18xx-fw-3.bin"

static int wl18xx_identify_chip(struct wl1271 *wl)
{
	int ret = 0;

	switch (wl->chip.id) {
	case CHIP_ID_185x_PG20:
		wl1271_debug(DEBUG_BOOT, "chip id 0x%x (185x PG20)",
				 wl->chip.id);
		wl->sr_fw_name = WL18XX_FW_NAME;
		/* wl18xx uses the same firmware for PLT */
		wl->plt_fw_name = WL18XX_FW_NAME;
		wl->quirks |= WLCORE_QUIRK_RX_BLOCKSIZE_ALIGN |
			      WLCORE_QUIRK_TX_BLOCKSIZE_ALIGN |
			      WLCORE_QUIRK_NO_SCHED_SCAN_WHILE_CONN |
			      WLCORE_QUIRK_TX_PAD_LAST_FRAME |
			      WLCORE_QUIRK_REGDOMAIN_CONF |
			      WLCORE_QUIRK_DUAL_PROBE_TMPL;

		wlcore_set_min_fw_ver(wl, WL18XX_CHIP_VER,
				      WL18XX_IFTYPE_VER,  WL18XX_MAJOR_VER,
				      WL18XX_SUBTYPE_VER, WL18XX_MINOR_VER,
				      /* there's no separate multi-role FW */
				      0, 0, 0, 0);
		break;
	case CHIP_ID_185x_PG10:
		wl1271_warning("chip id 0x%x (185x PG10) is deprecated",
			       wl->chip.id);
		ret = -ENODEV;
		goto out;

	default:
		wl1271_warning("unsupported chip id: 0x%x", wl->chip.id);
		ret = -ENODEV;
		goto out;
	}

	wl->fw_mem_block_size = 272;
	wl->fwlog_end = 0x40000000;

	wl->scan_templ_id_2_4 = CMD_TEMPL_CFG_PROBE_REQ_2_4;
	wl->scan_templ_id_5 = CMD_TEMPL_CFG_PROBE_REQ_5;
	wl->sched_scan_templ_id_2_4 = CMD_TEMPL_PROBE_REQ_2_4_PERIODIC;
	wl->sched_scan_templ_id_5 = CMD_TEMPL_PROBE_REQ_5_PERIODIC;
	wl->max_channels_5 = WL18XX_MAX_CHANNELS_5GHZ;
	wl->ba_rx_session_count_max = WL18XX_RX_BA_MAX_SESSIONS;
out:
	return ret;
}

static int wl18xx_set_clk(struct wl1271 *wl)
{
	u16 clk_freq;
	int ret;

	ret = wlcore_set_partition(wl, &wl->ptable[PART_TOP_PRCM_ELP_SOC]);
	if (ret < 0)
		goto out;

	/* TODO: PG2: apparently we need to read the clk type */

	ret = wl18xx_top_reg_read(wl, PRIMARY_CLK_DETECT, &clk_freq);
	if (ret < 0)
		goto out;

	wl1271_debug(DEBUG_BOOT, "clock freq %d (%d, %d, %d, %d, %s)", clk_freq,
		     wl18xx_clk_table[clk_freq].n, wl18xx_clk_table[clk_freq].m,
		     wl18xx_clk_table[clk_freq].p, wl18xx_clk_table[clk_freq].q,
		     wl18xx_clk_table[clk_freq].swallow ? "swallow" : "spit");

	/* coex PLL configuration */
	ret = wl18xx_top_reg_write(wl, PLLSH_COEX_PLL_N,
				   wl18xx_clk_table_coex[clk_freq].n);
	if (ret < 0)
		goto out;

	ret = wl18xx_top_reg_write(wl, PLLSH_COEX_PLL_M,
				   wl18xx_clk_table_coex[clk_freq].m);
	if (ret < 0)
		goto out;

	/* bypass the swallowing logic */
	ret = wl18xx_top_reg_write(wl, PLLSH_COEX_PLL_SWALLOW_EN,
				   PLLSH_COEX_PLL_SWALLOW_EN_VAL1);
	if (ret < 0)
		goto out;

	ret = wl18xx_top_reg_write(wl, PLLSH_WCS_PLL_N,
				   wl18xx_clk_table[clk_freq].n);
	if (ret < 0)
		goto out;

	ret = wl18xx_top_reg_write(wl, PLLSH_WCS_PLL_M,
				   wl18xx_clk_table[clk_freq].m);
	if (ret < 0)
		goto out;

	if (wl18xx_clk_table[clk_freq].swallow) {
		/* first the 16 lower bits */
		ret = wl18xx_top_reg_write(wl, PLLSH_WCS_PLL_Q_FACTOR_CFG_1,
					   wl18xx_clk_table[clk_freq].q &
					   PLLSH_WCS_PLL_Q_FACTOR_CFG_1_MASK);
		if (ret < 0)
			goto out;

		/* then the 16 higher bits, masked out */
		ret = wl18xx_top_reg_write(wl, PLLSH_WCS_PLL_Q_FACTOR_CFG_2,
					(wl18xx_clk_table[clk_freq].q >> 16) &
					PLLSH_WCS_PLL_Q_FACTOR_CFG_2_MASK);
		if (ret < 0)
			goto out;

		/* first the 16 lower bits */
		ret = wl18xx_top_reg_write(wl, PLLSH_WCS_PLL_P_FACTOR_CFG_1,
					   wl18xx_clk_table[clk_freq].p &
					   PLLSH_WCS_PLL_P_FACTOR_CFG_1_MASK);
		if (ret < 0)
			goto out;

		/* then the 16 higher bits, masked out */
		ret = wl18xx_top_reg_write(wl, PLLSH_WCS_PLL_P_FACTOR_CFG_2,
					(wl18xx_clk_table[clk_freq].p >> 16) &
					PLLSH_WCS_PLL_P_FACTOR_CFG_2_MASK);
	} else {
		ret = wl18xx_top_reg_write(wl, PLLSH_WCS_PLL_SWALLOW_EN,
					   PLLSH_WCS_PLL_SWALLOW_EN_VAL2);
	}

	/* choose WCS PLL */
	ret = wl18xx_top_reg_write(wl, PLLSH_WL_PLL_SEL,
				   PLLSH_WL_PLL_SEL_WCS_PLL);
	if (ret < 0)
		goto out;

	/* enable both PLLs */
	ret = wl18xx_top_reg_write(wl, PLLSH_WL_PLL_EN, PLLSH_WL_PLL_EN_VAL1);
	if (ret < 0)
		goto out;

	udelay(1000);

	/* disable coex PLL */
	ret = wl18xx_top_reg_write(wl, PLLSH_WL_PLL_EN, PLLSH_WL_PLL_EN_VAL2);
	if (ret < 0)
		goto out;

	/* reset the swallowing logic */
	ret = wl18xx_top_reg_write(wl, PLLSH_COEX_PLL_SWALLOW_EN,
				   PLLSH_COEX_PLL_SWALLOW_EN_VAL2);
	if (ret < 0)
		goto out;

out:
	return ret;
}

static int wl18xx_boot_soft_reset(struct wl1271 *wl)
{
	int ret;

	/* disable Rx/Tx */
	ret = wlcore_write32(wl, WL18XX_ENABLE, 0x0);
	if (ret < 0)
		goto out;

	/* disable auto calibration on start*/
	ret = wlcore_write32(wl, WL18XX_SPARE_A2, 0xffff);

out:
	return ret;
}

static int wl18xx_pre_boot(struct wl1271 *wl)
{
	int ret;

	ret = wl18xx_set_clk(wl);
	if (ret < 0)
		goto out;

	/* Continue the ELP wake up sequence */
	ret = wlcore_write32(wl, WL18XX_WELP_ARM_COMMAND, WELP_ARM_COMMAND_VAL);
	if (ret < 0)
		goto out;

	udelay(500);

	ret = wlcore_set_partition(wl, &wl->ptable[PART_BOOT]);
	if (ret < 0)
		goto out;

	/* Disable interrupts */
	ret = wlcore_write_reg(wl, REG_INTERRUPT_MASK, WL1271_ACX_INTR_ALL);
	if (ret < 0)
		goto out;

	ret = wl18xx_boot_soft_reset(wl);

out:
	return ret;
}

static int wl18xx_pre_upload(struct wl1271 *wl)
{
	u32 tmp;
	int ret;

	BUILD_BUG_ON(sizeof(struct wl18xx_mac_and_phy_params) >
		WL18XX_PHY_INIT_MEM_SIZE);

	ret = wlcore_set_partition(wl, &wl->ptable[PART_BOOT]);
	if (ret < 0)
		goto out;

	/* TODO: check if this is all needed */
	ret = wlcore_write32(wl, WL18XX_EEPROMLESS_IND, WL18XX_EEPROMLESS_IND);
	if (ret < 0)
		goto out;

	ret = wlcore_read_reg(wl, REG_CHIP_ID_B, &tmp);
	if (ret < 0)
		goto out;

	wl1271_debug(DEBUG_BOOT, "chip id 0x%x", tmp);

	ret = wlcore_read32(wl, WL18XX_SCR_PAD2, &tmp);
	if (ret < 0)
		goto out;

	/*
	 * Workaround for FDSP code RAM corruption (needed for PG2.1
	 * and newer; for older chips it's a NOP).  Change FDSP clock
	 * settings so that it's muxed to the ATGP clock instead of
	 * its own clock.
	 */

	ret = wlcore_set_partition(wl, &wl->ptable[PART_PHY_INIT]);
	if (ret < 0)
		goto out;

	/* disable FDSP clock */
	ret = wlcore_write32(wl, WL18XX_PHY_FPGA_SPARE_1,
			     MEM_FDSP_CLK_120_DISABLE);
	if (ret < 0)
		goto out;

	/* set ATPG clock toward FDSP Code RAM rather than its own clock */
	ret = wlcore_write32(wl, WL18XX_PHY_FPGA_SPARE_1,
			     MEM_FDSP_CODERAM_FUNC_CLK_SEL);
	if (ret < 0)
		goto out;

	/* re-enable FDSP clock */
	ret = wlcore_write32(wl, WL18XX_PHY_FPGA_SPARE_1,
			     MEM_FDSP_CLK_120_ENABLE);

out:
	return ret;
}

static int wl18xx_set_mac_and_phy(struct wl1271 *wl)
{
	struct wl18xx_priv *priv = wl->priv;
	struct wl18xx_mac_and_phy_params *params;
	int ret;

	params = kmemdup(&priv->conf.phy, sizeof(*params), GFP_KERNEL);
	if (!params) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wlcore_set_partition(wl, &wl->ptable[PART_PHY_INIT]);
	if (ret < 0)
		goto out;

	ret = wlcore_write(wl, WL18XX_PHY_INIT_MEM_ADDR, params,
			   sizeof(*params), false);

out:
	kfree(params);
	return ret;
}

static int wl18xx_enable_interrupts(struct wl1271 *wl)
{
	u32 event_mask, intr_mask;
	int ret;

	event_mask = WL18XX_ACX_EVENTS_VECTOR;
	intr_mask = WL18XX_INTR_MASK;

	ret = wlcore_write_reg(wl, REG_INTERRUPT_MASK, event_mask);
	if (ret < 0)
		goto out;

	wlcore_enable_interrupts(wl);

	ret = wlcore_write_reg(wl, REG_INTERRUPT_MASK,
			       WL1271_ACX_INTR_ALL & ~intr_mask);
	if (ret < 0)
		goto disable_interrupts;

	return ret;

disable_interrupts:
	wlcore_disable_interrupts(wl);

out:
	return ret;
}

static int wl18xx_boot(struct wl1271 *wl)
{
	int ret;

	ret = wl18xx_pre_boot(wl);
	if (ret < 0)
		goto out;

	ret = wl18xx_pre_upload(wl);
	if (ret < 0)
		goto out;

	ret = wlcore_boot_upload_firmware(wl);
	if (ret < 0)
		goto out;

	ret = wl18xx_set_mac_and_phy(wl);
	if (ret < 0)
		goto out;

	wl->event_mask = BSS_LOSS_EVENT_ID |
		SCAN_COMPLETE_EVENT_ID |
		RSSI_SNR_TRIGGER_0_EVENT_ID |
		PERIODIC_SCAN_COMPLETE_EVENT_ID |
		PERIODIC_SCAN_REPORT_EVENT_ID |
		DUMMY_PACKET_EVENT_ID |
		PEER_REMOVE_COMPLETE_EVENT_ID |
		BA_SESSION_RX_CONSTRAINT_EVENT_ID |
		REMAIN_ON_CHANNEL_COMPLETE_EVENT_ID |
		INACTIVE_STA_EVENT_ID |
		CHANNEL_SWITCH_COMPLETE_EVENT_ID |
		DFS_CHANNELS_CONFIG_COMPLETE_EVENT |
		SMART_CONFIG_SYNC_EVENT_ID |
		SMART_CONFIG_DECODE_EVENT_ID;
;

	wl->ap_event_mask = MAX_TX_FAILURE_EVENT_ID;

	ret = wlcore_boot_run_firmware(wl);
	if (ret < 0)
		goto out;

	ret = wl18xx_enable_interrupts(wl);

out:
	return ret;
}

static int wl18xx_trigger_cmd(struct wl1271 *wl, int cmd_box_addr,
			       void *buf, size_t len)
{
	struct wl18xx_priv *priv = wl->priv;

	memcpy(priv->cmd_buf, buf, len);
	memset(priv->cmd_buf + len, 0, WL18XX_CMD_MAX_SIZE - len);

	return wlcore_write(wl, cmd_box_addr, priv->cmd_buf,
			    WL18XX_CMD_MAX_SIZE, false);
}

static int wl18xx_ack_event(struct wl1271 *wl)
{
	return wlcore_write_reg(wl, REG_INTERRUPT_TRIG,
				WL18XX_INTR_TRIG_EVENT_ACK);
}

static u32 wl18xx_calc_tx_blocks(struct wl1271 *wl, u32 len, u32 spare_blks)
{
	u32 blk_size = WL18XX_TX_HW_BLOCK_SIZE;
	return (len + blk_size - 1) / blk_size + spare_blks;
}

static void
wl18xx_set_tx_desc_blocks(struct wl1271 *wl, struct wl1271_tx_hw_descr *desc,
			  u32 blks, u32 spare_blks)
{
	desc->wl18xx_mem.total_mem_blocks = blks;
}

static void
wl18xx_set_tx_desc_data_len(struct wl1271 *wl, struct wl1271_tx_hw_descr *desc,
			    struct sk_buff *skb)
{
	desc->length = cpu_to_le16(skb->len);

	/* if only the last frame is to be padded, we unset this bit on Tx */
	if (wl->quirks & WLCORE_QUIRK_TX_PAD_LAST_FRAME)
		desc->wl18xx_mem.ctrl = WL18XX_TX_CTRL_NOT_PADDED;
	else
		desc->wl18xx_mem.ctrl = 0;

	wl1271_debug(DEBUG_TX, "tx_fill_hdr: hlid: %d "
		     "len: %d life: %d mem: %d", desc->hlid,
		     le16_to_cpu(desc->length),
		     le16_to_cpu(desc->life_time),
		     desc->wl18xx_mem.total_mem_blocks);
}

static enum wl_rx_buf_align
wl18xx_get_rx_buf_align(struct wl1271 *wl, u32 rx_desc)
{
	if (rx_desc & RX_BUF_PADDED_PAYLOAD)
		return WLCORE_RX_BUF_PADDED;

	return WLCORE_RX_BUF_ALIGNED;
}

static u32 wl18xx_get_rx_packet_len(struct wl1271 *wl, void *rx_data,
				    u32 data_len)
{
	struct wl1271_rx_descriptor *desc = rx_data;

	/* invalid packet */
	if (data_len < sizeof(*desc))
		return 0;

	return data_len - sizeof(*desc);
}

static void wl18xx_tx_immediate_completion(struct wl1271 *wl)
{
	wl18xx_tx_immediate_complete(wl);
}

static int wl18xx_set_host_cfg_bitmap(struct wl1271 *wl, u32 extra_mem_blk)
{
	int ret;
	u32 sdio_align_size = 0;
	u32 host_cfg_bitmap = HOST_IF_CFG_RX_FIFO_ENABLE |
			      HOST_IF_CFG_ADD_RX_ALIGNMENT;

	/* Enable Tx SDIO padding */
	if (wl->quirks & WLCORE_QUIRK_TX_BLOCKSIZE_ALIGN) {
		host_cfg_bitmap |= HOST_IF_CFG_TX_PAD_TO_SDIO_BLK;
		sdio_align_size = WL12XX_BUS_BLOCK_SIZE;
	}

	/* Enable Rx SDIO padding */
	if (wl->quirks & WLCORE_QUIRK_RX_BLOCKSIZE_ALIGN) {
		host_cfg_bitmap |= HOST_IF_CFG_RX_PAD_TO_SDIO_BLK;
		sdio_align_size = WL12XX_BUS_BLOCK_SIZE;
	}

	ret = wl18xx_acx_host_if_cfg_bitmap(wl, host_cfg_bitmap,
					    sdio_align_size, extra_mem_blk,
					    WL18XX_HOST_IF_LEN_SIZE_FIELD);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl18xx_hw_init(struct wl1271 *wl)
{
	int ret;
	struct wl18xx_priv *priv = wl->priv;

	/* (re)init private structures. Relevant on recovery as well. */
	priv->last_fw_rls_idx = 0;
	priv->extra_spare_key_count = 0;

	/* set the default amount of spare blocks in the bitmap */
	ret = wl18xx_set_host_cfg_bitmap(wl, WL18XX_TX_HW_BLOCK_SPARE);
	if (ret < 0)
		return ret;

	if (checksum_param) {
		ret = wl18xx_acx_set_checksum_state(wl);
		if (ret != 0)
			return ret;
	}

	return ret;
}

static void wl18xx_convert_fw_status(struct wl1271 *wl, void *raw_fw_status,
				     struct wl_fw_status *fw_status)
{
	struct wl18xx_fw_status *int_fw_status = raw_fw_status;

	fw_status->intr = le32_to_cpu(int_fw_status->intr);
	fw_status->fw_rx_counter = int_fw_status->fw_rx_counter;
	fw_status->drv_rx_counter = int_fw_status->drv_rx_counter;
	fw_status->tx_results_counter = int_fw_status->tx_results_counter;
	fw_status->rx_pkt_descs = int_fw_status->rx_pkt_descs;

	fw_status->fw_localtime = le32_to_cpu(int_fw_status->fw_localtime);
	fw_status->link_ps_bitmap = le32_to_cpu(int_fw_status->link_ps_bitmap);
	fw_status->link_fast_bitmap =
			le32_to_cpu(int_fw_status->link_fast_bitmap);
	fw_status->total_released_blks =
			le32_to_cpu(int_fw_status->total_released_blks);
	fw_status->tx_total = le32_to_cpu(int_fw_status->tx_total);

	fw_status->counters.tx_released_pkts =
			int_fw_status->counters.tx_released_pkts;
	fw_status->counters.tx_lnk_free_pkts =
			int_fw_status->counters.tx_lnk_free_pkts;
	fw_status->counters.tx_voice_released_blks =
			int_fw_status->counters.tx_voice_released_blks;
	fw_status->counters.tx_last_rate =
			int_fw_status->counters.tx_last_rate;

	fw_status->log_start_addr = le32_to_cpu(int_fw_status->log_start_addr);

	fw_status->priv = &int_fw_status->priv;
}

static void wl18xx_set_tx_desc_csum(struct wl1271 *wl,
				    struct wl1271_tx_hw_descr *desc,
				    struct sk_buff *skb)
{
	u32 ip_hdr_offset;
	struct iphdr *ip_hdr;

	if (!checksum_param) {
		desc->wl18xx_checksum_data = 0;
		return;
	}

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		desc->wl18xx_checksum_data = 0;
		return;
	}

	ip_hdr_offset = skb_network_header(skb) - skb_mac_header(skb);
	if (WARN_ON(ip_hdr_offset >= (1<<7))) {
		desc->wl18xx_checksum_data = 0;
		return;
	}

	desc->wl18xx_checksum_data = ip_hdr_offset << 1;

	/* FW is interested only in the LSB of the protocol  TCP=0 UDP=1 */
	ip_hdr = (void *)skb_network_header(skb);
	desc->wl18xx_checksum_data |= (ip_hdr->protocol & 0x01);
}

static void wl18xx_set_rx_csum(struct wl1271 *wl,
			       struct wl1271_rx_descriptor *desc,
			       struct sk_buff *skb)
{
	if (desc->status & WL18XX_RX_CHECKSUM_MASK)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
}

static bool wl18xx_is_mimo_supported(struct wl1271 *wl)
{
	struct wl18xx_priv *priv = wl->priv;

	/* only support MIMO with multiple antennas, and when SISO
	 * is not forced through config
	 */
	return (priv->conf.phy.number_of_assembled_ant2_4 >= 2) &&
	       (priv->conf.ht.mode != HT_MODE_WIDE) &&
	       (priv->conf.ht.mode != HT_MODE_SISO20);
}

/*
 * TODO: instead of having these two functions to get the rate mask,
 * we should modify the wlvif->rate_set instead
 */
static u32 wl18xx_sta_get_ap_rate_mask(struct wl1271 *wl,
				       struct wl12xx_vif *wlvif)
{
	u32 hw_rate_set = wlvif->rate_set;

	if (wlvif->channel_type == NL80211_CHAN_HT40MINUS ||
	    wlvif->channel_type == NL80211_CHAN_HT40PLUS) {
		wl1271_debug(DEBUG_ACX, "using wide channel rate mask");
		hw_rate_set |= CONF_TX_RATE_USE_WIDE_CHAN;

		/* we don't support MIMO in wide-channel mode */
		hw_rate_set &= ~CONF_TX_MIMO_RATES;
	} else if (wl18xx_is_mimo_supported(wl)) {
		wl1271_debug(DEBUG_ACX, "using MIMO channel rate mask");
		hw_rate_set |= CONF_TX_MIMO_RATES;
	}

	return hw_rate_set;
}

static u32 wl18xx_ap_get_mimo_wide_rate_mask(struct wl1271 *wl,
					     struct wl12xx_vif *wlvif)
{
	if (wlvif->channel_type == NL80211_CHAN_HT40MINUS ||
	    wlvif->channel_type == NL80211_CHAN_HT40PLUS) {
		wl1271_debug(DEBUG_ACX, "using wide channel rate mask");

		/* sanity check - we don't support this */
		if (WARN_ON(wlvif->band != IEEE80211_BAND_5GHZ))
			return 0;

		return CONF_TX_RATE_USE_WIDE_CHAN;
	} else if (wl18xx_is_mimo_supported(wl) &&
		   wlvif->band == IEEE80211_BAND_2GHZ) {
		wl1271_debug(DEBUG_ACX, "using MIMO rate mask");
		/*
		 * we don't care about HT channel here - if a peer doesn't
		 * support MIMO, we won't enable it in its rates
		 */
		return CONF_TX_MIMO_RATES;
	} else {
		return 0;
	}
}

static const char *wl18xx_rdl_name(enum wl18xx_rdl_num rdl_num)
{
	switch (rdl_num) {
	case RDL_1_HP:
		return "183xH";
	case RDL_2_SP:
		return "183x or 180x";
	case RDL_3_HP:
		return "187xH";
	case RDL_4_SP:
		return "187x";
	case RDL_5_SP:
		return "RDL11 - Not Supported";
	case RDL_6_SP:
		return "180xD";
	case RDL_7_SP:
		return "RDL13 - Not Supported (1893Q)";
	case RDL_8_SP:
		return "18xxQ";
	case RDL_NONE:
		return "UNTRIMMED";
	default:
		return "UNKNOWN";
	}
}

static int wl18xx_get_pg_ver(struct wl1271 *wl, s8 *ver)
{
	u32 fuse;
	s8 rom = 0, metal = 0, pg_ver = 0, rdl_ver = 0, package_type = 0;
	int ret;

	ret = wlcore_set_partition(wl, &wl->ptable[PART_TOP_PRCM_ELP_SOC]);
	if (ret < 0)
		goto out;

	ret = wlcore_read32(wl, WL18XX_REG_FUSE_DATA_2_3, &fuse);
	if (ret < 0)
		goto out;

	package_type = (fuse >> WL18XX_PACKAGE_TYPE_OFFSET) & 1;

	ret = wlcore_read32(wl, WL18XX_REG_FUSE_DATA_1_3, &fuse);
	if (ret < 0)
		goto out;

	pg_ver = (fuse & WL18XX_PG_VER_MASK) >> WL18XX_PG_VER_OFFSET;
	rom = (fuse & WL18XX_ROM_VER_MASK) >> WL18XX_ROM_VER_OFFSET;

	if ((rom <= 0xE) && (package_type == WL18XX_PACKAGE_TYPE_WSP))
		metal = (fuse & WL18XX_METAL_VER_MASK) >>
			WL18XX_METAL_VER_OFFSET;
	else
		metal = (fuse & WL18XX_NEW_METAL_VER_MASK) >>
			WL18XX_NEW_METAL_VER_OFFSET;

	ret = wlcore_read32(wl, WL18XX_REG_FUSE_DATA_2_3, &fuse);
	if (ret < 0)
		goto out;

	rdl_ver = (fuse & WL18XX_RDL_VER_MASK) >> WL18XX_RDL_VER_OFFSET;

	wl1271_info("wl18xx HW: %s, PG %d.%d (ROM 0x%x)",
		    wl18xx_rdl_name(rdl_ver), pg_ver, metal, rom);

	if (ver)
		*ver = pg_ver;

	ret = wlcore_set_partition(wl, &wl->ptable[PART_BOOT]);

out:
	return ret;
}

#define WL18XX_CONF_FILE_NAME "ti-connectivity/wl18xx-conf.bin"
static int wl18xx_conf_init(struct wl1271 *wl, struct device *dev)
{
	struct wl18xx_priv *priv = wl->priv;
	struct wlcore_conf_file *conf_file;
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, WL18XX_CONF_FILE_NAME, dev);
	if (ret < 0) {
		wl1271_error("could not get configuration binary %s: %d",
			     WL18XX_CONF_FILE_NAME, ret);
		goto out_fallback;
	}

	if (fw->size != WL18XX_CONF_SIZE) {
		wl1271_error("configuration binary file size is wrong, expected %zu got %zu",
			     WL18XX_CONF_SIZE, fw->size);
		ret = -EINVAL;
		goto out;
	}

	conf_file = (struct wlcore_conf_file *) fw->data;

	if (conf_file->header.magic != cpu_to_le32(WL18XX_CONF_MAGIC)) {
		wl1271_error("configuration binary file magic number mismatch, "
			     "expected 0x%0x got 0x%0x", WL18XX_CONF_MAGIC,
			     conf_file->header.magic);
		ret = -EINVAL;
		goto out;
	}

	if (conf_file->header.version != cpu_to_le32(WL18XX_CONF_VERSION)) {
		wl1271_error("configuration binary file version not supported, "
			     "expected 0x%08x got 0x%08x",
			     WL18XX_CONF_VERSION, conf_file->header.version);
		ret = -EINVAL;
		goto out;
	}

	memcpy(&wl->conf, &conf_file->core, sizeof(wl18xx_conf));
	memcpy(&priv->conf, &conf_file->priv, sizeof(priv->conf));

	goto out;

out_fallback:
	wl1271_warning("falling back to default config");

	/* apply driver default configuration */
	memcpy(&wl->conf, &wl18xx_conf, sizeof(wl18xx_conf));
	/* apply default private configuration */
	memcpy(&priv->conf, &wl18xx_default_priv_conf, sizeof(priv->conf));

	/* For now we just fallback */
	return 0;

out:
	release_firmware(fw);
	return ret;
}

static int wl18xx_plt_init(struct wl1271 *wl)
{
	int ret;

	/* calibrator based auto/fem detect not supported for 18xx */
	if (wl->plt_mode == PLT_FEM_DETECT) {
		wl1271_error("wl18xx_plt_init: PLT FEM_DETECT not supported");
		return -EINVAL;
	}

	ret = wlcore_write32(wl, WL18XX_SCR_PAD8, WL18XX_SCR_PAD8_PLT);
	if (ret < 0)
		return ret;

	return wl->ops->boot(wl);
}

static int wl18xx_get_mac(struct wl1271 *wl)
{
	u32 mac1, mac2;
	int ret;

	ret = wlcore_set_partition(wl, &wl->ptable[PART_TOP_PRCM_ELP_SOC]);
	if (ret < 0)
		goto out;

	ret = wlcore_read32(wl, WL18XX_REG_FUSE_BD_ADDR_1, &mac1);
	if (ret < 0)
		goto out;

	ret = wlcore_read32(wl, WL18XX_REG_FUSE_BD_ADDR_2, &mac2);
	if (ret < 0)
		goto out;

	/* these are the two parts of the BD_ADDR */
	wl->fuse_oui_addr = ((mac2 & 0xffff) << 8) +
		((mac1 & 0xff000000) >> 24);
	wl->fuse_nic_addr = (mac1 & 0xffffff);

	if (!wl->fuse_oui_addr && !wl->fuse_nic_addr) {
		u8 mac[ETH_ALEN];

		eth_random_addr(mac);

		wl->fuse_oui_addr = (mac[0] << 16) + (mac[1] << 8) + mac[2];
		wl->fuse_nic_addr = (mac[3] << 16) + (mac[4] << 8) + mac[5];
		wl1271_warning("MAC address from fuse not available, using random locally administered addresses.");
	}

	ret = wlcore_set_partition(wl, &wl->ptable[PART_DOWN]);

out:
	return ret;
}

static int wl18xx_handle_static_data(struct wl1271 *wl,
				     struct wl1271_static_data *static_data)
{
	struct wl18xx_static_data_priv *static_data_priv =
		(struct wl18xx_static_data_priv *) static_data->priv;

	strncpy(wl->chip.phy_fw_ver_str, static_data_priv->phy_version,
		sizeof(wl->chip.phy_fw_ver_str));

	/* make sure the string is NULL-terminated */
	wl->chip.phy_fw_ver_str[sizeof(wl->chip.phy_fw_ver_str) - 1] = '\0';

	wl1271_info("PHY firmware version: %s", static_data_priv->phy_version);

	return 0;
}

static int wl18xx_get_spare_blocks(struct wl1271 *wl, bool is_gem)
{
	struct wl18xx_priv *priv = wl->priv;

	/* If we have keys requiring extra spare, indulge them */
	if (priv->extra_spare_key_count)
		return WL18XX_TX_HW_EXTRA_BLOCK_SPARE;

	return WL18XX_TX_HW_BLOCK_SPARE;
}

static int wl18xx_set_key(struct wl1271 *wl, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key_conf)
{
	struct wl18xx_priv *priv = wl->priv;
	bool change_spare = false, special_enc;
	int ret;

	wl1271_debug(DEBUG_CRYPT, "extra spare keys before: %d",
		     priv->extra_spare_key_count);

	special_enc = key_conf->cipher == WL1271_CIPHER_SUITE_GEM ||
		      key_conf->cipher == WLAN_CIPHER_SUITE_TKIP;

	ret = wlcore_set_key(wl, cmd, vif, sta, key_conf);
	if (ret < 0)
		goto out;

	/*
	 * when adding the first or removing the last GEM/TKIP key,
	 * we have to adjust the number of spare blocks.
	 */
	if (special_enc) {
		if (cmd == SET_KEY) {
			/* first key */
			change_spare = (priv->extra_spare_key_count == 0);
			priv->extra_spare_key_count++;
		} else if (cmd == DISABLE_KEY) {
			/* last key */
			change_spare = (priv->extra_spare_key_count == 1);
			priv->extra_spare_key_count--;
		}
	}

	wl1271_debug(DEBUG_CRYPT, "extra spare keys after: %d",
		     priv->extra_spare_key_count);

	if (!change_spare)
		goto out;

	/* key is now set, change the spare blocks */
	if (priv->extra_spare_key_count)
		ret = wl18xx_set_host_cfg_bitmap(wl,
					WL18XX_TX_HW_EXTRA_BLOCK_SPARE);
	else
		ret = wl18xx_set_host_cfg_bitmap(wl,
					WL18XX_TX_HW_BLOCK_SPARE);

out:
	return ret;
}

static u32 wl18xx_pre_pkt_send(struct wl1271 *wl,
			       u32 buf_offset, u32 last_len)
{
	if (wl->quirks & WLCORE_QUIRK_TX_PAD_LAST_FRAME) {
		struct wl1271_tx_hw_descr *last_desc;

		/* get the last TX HW descriptor written to the aggr buf */
		last_desc = (struct wl1271_tx_hw_descr *)(wl->aggr_buf +
							buf_offset - last_len);

		/* the last frame is padded up to an SDIO block */
		last_desc->wl18xx_mem.ctrl &= ~WL18XX_TX_CTRL_NOT_PADDED;
		return ALIGN(buf_offset, WL12XX_BUS_BLOCK_SIZE);
	}

	/* no modifications */
	return buf_offset;
}

static void wl18xx_sta_rc_update(struct wl1271 *wl,
				 struct wl12xx_vif *wlvif,
				 struct ieee80211_sta *sta,
				 u32 changed)
{
	bool wide = sta->bandwidth >= IEEE80211_STA_RX_BW_40;

	wl1271_debug(DEBUG_MAC80211, "mac80211 sta_rc_update wide %d", wide);

	if (!(changed & IEEE80211_RC_BW_CHANGED))
		return;

	mutex_lock(&wl->mutex);

	/* sanity */
	if (WARN_ON(wlvif->bss_type != BSS_TYPE_STA_BSS))
		goto out;

	/* ignore the change before association */
	if (!test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
		goto out;

	/*
	 * If we started out as wide, we can change the operation mode. If we
	 * thought this was a 20mhz AP, we have to reconnect
	 */
	if (wlvif->sta.role_chan_type == NL80211_CHAN_HT40MINUS ||
	    wlvif->sta.role_chan_type == NL80211_CHAN_HT40PLUS)
		wl18xx_acx_peer_ht_operation_mode(wl, wlvif->sta.hlid, wide);
	else
		ieee80211_connection_loss(wl12xx_wlvif_to_vif(wlvif));

out:
	mutex_unlock(&wl->mutex);
}

static int wl18xx_set_peer_cap(struct wl1271 *wl,
			       struct ieee80211_sta_ht_cap *ht_cap,
			       bool allow_ht_operation,
			       u32 rate_set, u8 hlid)
{
	return wl18xx_acx_set_peer_cap(wl, ht_cap, allow_ht_operation,
				       rate_set, hlid);
}

static bool wl18xx_lnk_high_prio(struct wl1271 *wl, u8 hlid,
				 struct wl1271_link *lnk)
{
	u8 thold;
	struct wl18xx_fw_status_priv *status_priv =
		(struct wl18xx_fw_status_priv *)wl->fw_status->priv;
	u32 suspend_bitmap = le32_to_cpu(status_priv->link_suspend_bitmap);

	/* suspended links are never high priority */
	if (test_bit(hlid, (unsigned long *)&suspend_bitmap))
		return false;

	/* the priority thresholds are taken from FW */
	if (test_bit(hlid, (unsigned long *)&wl->fw_fast_lnk_map) &&
	    !test_bit(hlid, (unsigned long *)&wl->ap_fw_ps_map))
		thold = status_priv->tx_fast_link_prio_threshold;
	else
		thold = status_priv->tx_slow_link_prio_threshold;

	return lnk->allocated_pkts < thold;
}

static bool wl18xx_lnk_low_prio(struct wl1271 *wl, u8 hlid,
				struct wl1271_link *lnk)
{
	u8 thold;
	struct wl18xx_fw_status_priv *status_priv =
		(struct wl18xx_fw_status_priv *)wl->fw_status->priv;
	u32 suspend_bitmap = le32_to_cpu(status_priv->link_suspend_bitmap);

	if (test_bit(hlid, (unsigned long *)&suspend_bitmap))
		thold = status_priv->tx_suspend_threshold;
	else if (test_bit(hlid, (unsigned long *)&wl->fw_fast_lnk_map) &&
		 !test_bit(hlid, (unsigned long *)&wl->ap_fw_ps_map))
		thold = status_priv->tx_fast_stop_threshold;
	else
		thold = status_priv->tx_slow_stop_threshold;

	return lnk->allocated_pkts < thold;
}

static u32 wl18xx_convert_hwaddr(struct wl1271 *wl, u32 hwaddr)
{
	return hwaddr & ~0x80000000;
}

static int wl18xx_setup(struct wl1271 *wl);

static struct wlcore_ops wl18xx_ops = {
	.setup		= wl18xx_setup,
	.identify_chip	= wl18xx_identify_chip,
	.boot		= wl18xx_boot,
	.plt_init	= wl18xx_plt_init,
	.trigger_cmd	= wl18xx_trigger_cmd,
	.ack_event	= wl18xx_ack_event,
	.wait_for_event	= wl18xx_wait_for_event,
	.process_mailbox_events = wl18xx_process_mailbox_events,
	.calc_tx_blocks = wl18xx_calc_tx_blocks,
	.set_tx_desc_blocks = wl18xx_set_tx_desc_blocks,
	.set_tx_desc_data_len = wl18xx_set_tx_desc_data_len,
	.get_rx_buf_align = wl18xx_get_rx_buf_align,
	.get_rx_packet_len = wl18xx_get_rx_packet_len,
	.tx_immediate_compl = wl18xx_tx_immediate_completion,
	.tx_delayed_compl = NULL,
	.hw_init	= wl18xx_hw_init,
	.convert_fw_status = wl18xx_convert_fw_status,
	.set_tx_desc_csum = wl18xx_set_tx_desc_csum,
	.get_pg_ver	= wl18xx_get_pg_ver,
	.set_rx_csum = wl18xx_set_rx_csum,
	.sta_get_ap_rate_mask = wl18xx_sta_get_ap_rate_mask,
	.ap_get_mimo_wide_rate_mask = wl18xx_ap_get_mimo_wide_rate_mask,
	.get_mac	= wl18xx_get_mac,
	.debugfs_init	= wl18xx_debugfs_add_files,
	.scan_start	= wl18xx_scan_start,
	.scan_stop	= wl18xx_scan_stop,
	.sched_scan_start	= wl18xx_sched_scan_start,
	.sched_scan_stop	= wl18xx_scan_sched_scan_stop,
	.handle_static_data	= wl18xx_handle_static_data,
	.get_spare_blocks = wl18xx_get_spare_blocks,
	.set_key	= wl18xx_set_key,
	.channel_switch	= wl18xx_cmd_channel_switch,
	.pre_pkt_send	= wl18xx_pre_pkt_send,
	.sta_rc_update	= wl18xx_sta_rc_update,
	.set_peer_cap	= wl18xx_set_peer_cap,
	.convert_hwaddr = wl18xx_convert_hwaddr,
	.lnk_high_prio	= wl18xx_lnk_high_prio,
	.lnk_low_prio	= wl18xx_lnk_low_prio,
	.smart_config_start = wl18xx_cmd_smart_config_start,
	.smart_config_stop  = wl18xx_cmd_smart_config_stop,
	.smart_config_set_group_key = wl18xx_cmd_smart_config_set_group_key,
};

/* HT cap appropriate for wide channels in 2Ghz */
static struct ieee80211_sta_ht_cap wl18xx_siso40_ht_cap_2ghz = {
	.cap = IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40 |
	       IEEE80211_HT_CAP_SUP_WIDTH_20_40 | IEEE80211_HT_CAP_DSSSCCK40 |
	       IEEE80211_HT_CAP_GRN_FLD,
	.ht_supported = true,
	.ampdu_factor = IEEE80211_HT_MAX_AMPDU_16K,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
	.mcs = {
		.rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		.rx_highest = cpu_to_le16(150),
		.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
};

/* HT cap appropriate for wide channels in 5Ghz */
static struct ieee80211_sta_ht_cap wl18xx_siso40_ht_cap_5ghz = {
	.cap = IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40 |
	       IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
	       IEEE80211_HT_CAP_GRN_FLD,
	.ht_supported = true,
	.ampdu_factor = IEEE80211_HT_MAX_AMPDU_16K,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
	.mcs = {
		.rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		.rx_highest = cpu_to_le16(150),
		.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
};

/* HT cap appropriate for SISO 20 */
static struct ieee80211_sta_ht_cap wl18xx_siso20_ht_cap = {
	.cap = IEEE80211_HT_CAP_SGI_20 |
	       IEEE80211_HT_CAP_GRN_FLD,
	.ht_supported = true,
	.ampdu_factor = IEEE80211_HT_MAX_AMPDU_16K,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
	.mcs = {
		.rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		.rx_highest = cpu_to_le16(72),
		.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
};

/* HT cap appropriate for MIMO rates in 20mhz channel */
static struct ieee80211_sta_ht_cap wl18xx_mimo_ht_cap_2ghz = {
	.cap = IEEE80211_HT_CAP_SGI_20 |
	       IEEE80211_HT_CAP_GRN_FLD,
	.ht_supported = true,
	.ampdu_factor = IEEE80211_HT_MAX_AMPDU_16K,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
	.mcs = {
		.rx_mask = { 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, },
		.rx_highest = cpu_to_le16(144),
		.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
};

static const struct ieee80211_iface_limit wl18xx_iface_limits[] = {
	{
		.max = 3,
		.types = BIT(NL80211_IFTYPE_STATION),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_AP) |
			 BIT(NL80211_IFTYPE_P2P_GO) |
			 BIT(NL80211_IFTYPE_P2P_CLIENT),
	},
};

static const struct ieee80211_iface_limit wl18xx_iface_ap_limits[] = {
	{
		.max = 2,
		.types = BIT(NL80211_IFTYPE_AP),
	},
};

static const struct ieee80211_iface_combination
wl18xx_iface_combinations[] = {
	{
		.max_interfaces = 3,
		.limits = wl18xx_iface_limits,
		.n_limits = ARRAY_SIZE(wl18xx_iface_limits),
		.num_different_channels = 2,
	},
	{
		.max_interfaces = 2,
		.limits = wl18xx_iface_ap_limits,
		.n_limits = ARRAY_SIZE(wl18xx_iface_ap_limits),
		.num_different_channels = 1,
	}
};

static int wl18xx_setup(struct wl1271 *wl)
{
	struct wl18xx_priv *priv = wl->priv;
	int ret;

	BUILD_BUG_ON(WL18XX_MAX_LINKS > WLCORE_MAX_LINKS);
	BUILD_BUG_ON(WL18XX_MAX_AP_STATIONS > WL18XX_MAX_LINKS);

	wl->rtable = wl18xx_rtable;
	wl->num_tx_desc = WL18XX_NUM_TX_DESCRIPTORS;
	wl->num_rx_desc = WL18XX_NUM_RX_DESCRIPTORS;
	wl->num_links = WL18XX_MAX_LINKS;
	wl->max_ap_stations = WL18XX_MAX_AP_STATIONS;
	wl->iface_combinations = wl18xx_iface_combinations;
	wl->n_iface_combinations = ARRAY_SIZE(wl18xx_iface_combinations);
	wl->num_mac_addr = WL18XX_NUM_MAC_ADDRESSES;
	wl->band_rate_to_idx = wl18xx_band_rate_to_idx;
	wl->hw_tx_rate_tbl_size = WL18XX_CONF_HW_RXTX_RATE_MAX;
	wl->hw_min_ht_rate = WL18XX_CONF_HW_RXTX_RATE_MCS0;
	wl->fw_status_len = sizeof(struct wl18xx_fw_status);
	wl->fw_status_priv_len = sizeof(struct wl18xx_fw_status_priv);
	wl->stats.fw_stats_len = sizeof(struct wl18xx_acx_statistics);
	wl->static_data_priv_len = sizeof(struct wl18xx_static_data_priv);

	if (num_rx_desc_param != -1)
		wl->num_rx_desc = num_rx_desc_param;

	ret = wl18xx_conf_init(wl, wl->dev);
	if (ret < 0)
		return ret;

	/* If the module param is set, update it in conf */
	if (board_type_param) {
		if (!strcmp(board_type_param, "fpga")) {
			priv->conf.phy.board_type = BOARD_TYPE_FPGA_18XX;
		} else if (!strcmp(board_type_param, "hdk")) {
			priv->conf.phy.board_type = BOARD_TYPE_HDK_18XX;
		} else if (!strcmp(board_type_param, "dvp")) {
			priv->conf.phy.board_type = BOARD_TYPE_DVP_18XX;
		} else if (!strcmp(board_type_param, "evb")) {
			priv->conf.phy.board_type = BOARD_TYPE_EVB_18XX;
		} else if (!strcmp(board_type_param, "com8")) {
			priv->conf.phy.board_type = BOARD_TYPE_COM8_18XX;
		} else {
			wl1271_error("invalid board type '%s'",
				board_type_param);
			return -EINVAL;
		}
	}

	if (priv->conf.phy.board_type >= NUM_BOARD_TYPES) {
		wl1271_error("invalid board type '%d'",
			priv->conf.phy.board_type);
		return -EINVAL;
	}

	if (low_band_component_param != -1)
		priv->conf.phy.low_band_component = low_band_component_param;
	if (low_band_component_type_param != -1)
		priv->conf.phy.low_band_component_type =
			low_band_component_type_param;
	if (high_band_component_param != -1)
		priv->conf.phy.high_band_component = high_band_component_param;
	if (high_band_component_type_param != -1)
		priv->conf.phy.high_band_component_type =
			high_band_component_type_param;
	if (pwr_limit_reference_11_abg_param != -1)
		priv->conf.phy.pwr_limit_reference_11_abg =
			pwr_limit_reference_11_abg_param;
	if (n_antennas_2_param != -1)
		priv->conf.phy.number_of_assembled_ant2_4 = n_antennas_2_param;
	if (n_antennas_5_param != -1)
		priv->conf.phy.number_of_assembled_ant5 = n_antennas_5_param;
	if (dc2dc_param != -1)
		priv->conf.phy.external_pa_dc2dc = dc2dc_param;

	if (ht_mode_param) {
		if (!strcmp(ht_mode_param, "default"))
			priv->conf.ht.mode = HT_MODE_DEFAULT;
		else if (!strcmp(ht_mode_param, "wide"))
			priv->conf.ht.mode = HT_MODE_WIDE;
		else if (!strcmp(ht_mode_param, "siso20"))
			priv->conf.ht.mode = HT_MODE_SISO20;
		else {
			wl1271_error("invalid ht_mode '%s'", ht_mode_param);
			return -EINVAL;
		}
	}

	if (priv->conf.ht.mode == HT_MODE_DEFAULT) {
		/*
		 * Only support mimo with multiple antennas. Fall back to
		 * siso40.
		 */
		if (wl18xx_is_mimo_supported(wl))
			wlcore_set_ht_cap(wl, IEEE80211_BAND_2GHZ,
					  &wl18xx_mimo_ht_cap_2ghz);
		else
			wlcore_set_ht_cap(wl, IEEE80211_BAND_2GHZ,
					  &wl18xx_siso40_ht_cap_2ghz);

		/* 5Ghz is always wide */
		wlcore_set_ht_cap(wl, IEEE80211_BAND_5GHZ,
				  &wl18xx_siso40_ht_cap_5ghz);
	} else if (priv->conf.ht.mode == HT_MODE_WIDE) {
		wlcore_set_ht_cap(wl, IEEE80211_BAND_2GHZ,
				  &wl18xx_siso40_ht_cap_2ghz);
		wlcore_set_ht_cap(wl, IEEE80211_BAND_5GHZ,
				  &wl18xx_siso40_ht_cap_5ghz);
	} else if (priv->conf.ht.mode == HT_MODE_SISO20) {
		wlcore_set_ht_cap(wl, IEEE80211_BAND_2GHZ,
				  &wl18xx_siso20_ht_cap);
		wlcore_set_ht_cap(wl, IEEE80211_BAND_5GHZ,
				  &wl18xx_siso20_ht_cap);
	}

	if (!checksum_param) {
		wl18xx_ops.set_rx_csum = NULL;
		wl18xx_ops.init_vif = NULL;
	}

	/* Enable 11a Band only if we have 5G antennas */
	wl->enable_11a = (priv->conf.phy.number_of_assembled_ant5 != 0);

	return 0;
}

static int wl18xx_probe(struct platform_device *pdev)
{
	struct wl1271 *wl;
	struct ieee80211_hw *hw;
	int ret;

	hw = wlcore_alloc_hw(sizeof(struct wl18xx_priv),
			     WL18XX_AGGR_BUFFER_SIZE,
			     sizeof(struct wl18xx_event_mailbox));
	if (IS_ERR(hw)) {
		wl1271_error("can't allocate hw");
		ret = PTR_ERR(hw);
		goto out;
	}

	wl = hw->priv;
	wl->ops = &wl18xx_ops;
	wl->ptable = wl18xx_ptable;
	ret = wlcore_probe(wl, pdev);
	if (ret)
		goto out_free;

	return ret;

out_free:
	wlcore_free_hw(wl);
out:
	return ret;
}

static const struct platform_device_id wl18xx_id_table[] = {
	{ "wl18xx", 0 },
	{  } /* Terminating Entry */
};
MODULE_DEVICE_TABLE(platform, wl18xx_id_table);

static struct platform_driver wl18xx_driver = {
	.probe		= wl18xx_probe,
	.remove		= wlcore_remove,
	.id_table	= wl18xx_id_table,
	.driver = {
		.name	= "wl18xx_driver",
		.owner	= THIS_MODULE,
	}
};

module_platform_driver(wl18xx_driver);
module_param_named(ht_mode, ht_mode_param, charp, S_IRUSR);
MODULE_PARM_DESC(ht_mode, "Force HT mode: wide or siso20");

module_param_named(board_type, board_type_param, charp, S_IRUSR);
MODULE_PARM_DESC(board_type, "Board type: fpga, hdk (default), evb, com8 or "
		 "dvp");

module_param_named(checksum, checksum_param, bool, S_IRUSR);
MODULE_PARM_DESC(checksum, "Enable TCP checksum: boolean (defaults to false)");

module_param_named(dc2dc, dc2dc_param, int, S_IRUSR);
MODULE_PARM_DESC(dc2dc, "External DC2DC: u8 (defaults to 0)");

module_param_named(n_antennas_2, n_antennas_2_param, int, S_IRUSR);
MODULE_PARM_DESC(n_antennas_2,
		 "Number of installed 2.4GHz antennas: 1 (default) or 2");

module_param_named(n_antennas_5, n_antennas_5_param, int, S_IRUSR);
MODULE_PARM_DESC(n_antennas_5,
		 "Number of installed 5GHz antennas: 1 (default) or 2");

module_param_named(low_band_component, low_band_component_param, int,
		   S_IRUSR);
MODULE_PARM_DESC(low_band_component, "Low band component: u8 "
		 "(default is 0x01)");

module_param_named(low_band_component_type, low_band_component_type_param,
		   int, S_IRUSR);
MODULE_PARM_DESC(low_band_component_type, "Low band component type: u8 "
		 "(default is 0x05 or 0x06 depending on the board_type)");

module_param_named(high_band_component, high_band_component_param, int,
		   S_IRUSR);
MODULE_PARM_DESC(high_band_component, "High band component: u8, "
		 "(default is 0x01)");

module_param_named(high_band_component_type, high_band_component_type_param,
		   int, S_IRUSR);
MODULE_PARM_DESC(high_band_component_type, "High band component type: u8 "
		 "(default is 0x09)");

module_param_named(pwr_limit_reference_11_abg,
		   pwr_limit_reference_11_abg_param, int, S_IRUSR);
MODULE_PARM_DESC(pwr_limit_reference_11_abg, "Power limit reference: u8 "
		 "(default is 0xc8)");

module_param_named(num_rx_desc,
		   num_rx_desc_param, int, S_IRUSR);
MODULE_PARM_DESC(num_rx_desc_param,
		 "Number of Rx descriptors: u8 (default is 32)");

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Luciano Coelho <coelho@ti.com>");
MODULE_FIRMWARE(WL18XX_FW_NAME);
