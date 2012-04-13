/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2010 Nokia Corporation
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

#include <linux/err.h>

#include <linux/wl12xx.h>

#include "../wlcore/wlcore.h"
#include "../wlcore/debug.h"
#include "../wlcore/io.h"
#include "../wlcore/acx.h"
#include "../wlcore/tx.h"
#include "../wlcore/rx.h"
#include "../wlcore/io.h"
#include "../wlcore/boot.h"

#include "wl12xx.h"
#include "reg.h"
#include "cmd.h"
#include "acx.h"

static struct wlcore_conf wl12xx_conf = {
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
		.tx_compl_timeout            = 700,
		.tx_compl_threshold          = 4,
		.basic_rate                  = CONF_HW_BIT_RATE_1MBPS,
		.basic_rate_5                = CONF_HW_BIT_RATE_6MBPS,
		.tmpl_short_retry_limit      = 10,
		.tmpl_long_retry_limit       = 10,
		.tx_watchdog_timeout         = 5000,
	},
	.conn = {
		.wake_up_event               = CONF_WAKE_UP_EVENT_DTIM,
		.listen_interval             = 1,
		.suspend_wake_up_event       = CONF_WAKE_UP_EVENT_N_DTIM,
		.suspend_listen_interval     = 3,
		.bcn_filt_mode               = CONF_BCN_FILT_MODE_ENABLED,
		.bcn_filt_ie_count           = 2,
		.bcn_filt_ie = {
			[0] = {
				.ie          = WLAN_EID_CHANNEL_SWITCH,
				.rule        = CONF_BCN_RULE_PASS_ON_APPEARANCE,
			},
			[1] = {
				.ie          = WLAN_EID_HT_OPERATION,
				.rule        = CONF_BCN_RULE_PASS_ON_CHANGE,
			},
		},
		.synch_fail_thold            = 10,
		.bss_lose_timeout            = 100,
		.beacon_rx_timeout           = 10000,
		.broadcast_timeout           = 20000,
		.rx_broadcast_in_ps          = 1,
		.ps_poll_threshold           = 10,
		.bet_enable                  = CONF_BET_MODE_ENABLE,
		.bet_max_consecutive         = 50,
		.psm_entry_retries           = 8,
		.psm_exit_retries            = 16,
		.psm_entry_nullfunc_retries  = 3,
		.dynamic_ps_timeout          = 40,
		.forced_ps                   = false,
		.keep_alive_interval         = 55000,
		.max_listen_interval         = 20,
	},
	.itrim = {
		.enable = false,
		.timeout = 50000,
	},
	.pm_config = {
		.host_clk_settling_time = 5000,
		.host_fast_wakeup_support = false
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
		.min_dwell_time_passive       = 100000,
		.max_dwell_time_passive       = 100000,
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
		.rx_ba_win_size = 8,
		.tx_ba_win_size = 64,
		.inactivity_timeout = 10000,
		.tx_ba_tid_bitmap = CONF_TX_BA_ENABLED_TID_BITMAP,
	},
	/*
	 * Memory config for wl127x chips is given in the
	 * wl12xx_default_priv_conf struct. The below configuration is
	 * for wl128x chips.
	 */
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
		.m_divider_fref_set_1         = 148,
		.m_divider_fref_set_2         = 0xffff,     /* default */
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
		.mode                         = WL12XX_FWLOG_ON_DEMAND,
		.mem_blocks                   = 2,
		.severity                     = 0,
		.timestamp                    = WL12XX_FWLOG_TIMESTAMP_DISABLED,
		.output                       = WL12XX_FWLOG_OUTPUT_HOST,
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
};

static struct wl12xx_priv_conf wl12xx_default_priv_conf = {
	.rf = {
		.tx_per_channel_power_compensation_2 = {
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		},
		.tx_per_channel_power_compensation_5 = {
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		},
	},
	.mem_wl127x = {
		.num_stations                 = 1,
		.ssid_profiles                = 1,
		.rx_block_num                 = 70,
		.tx_min_block_num             = 40,
		.dynamic_memory               = 1,
		.min_req_tx_blocks            = 100,
		.min_req_rx_blocks            = 22,
		.tx_min                       = 27,
	},

};

#define WL12XX_TX_HW_BLOCK_SPARE_DEFAULT        1
#define WL12XX_TX_HW_BLOCK_GEM_SPARE            2
#define WL12XX_TX_HW_BLOCK_SIZE                 252

static const u8 wl12xx_rate_to_idx_2ghz[] = {
	/* MCS rates are used only with 11n */
	7,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS7_SGI */
	7,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS7 */
	6,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS6 */
	5,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS5 */
	4,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS4 */
	3,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS3 */
	2,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS2 */
	1,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS1 */
	0,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS0 */

	11,                            /* WL12XX_CONF_HW_RXTX_RATE_54   */
	10,                            /* WL12XX_CONF_HW_RXTX_RATE_48   */
	9,                             /* WL12XX_CONF_HW_RXTX_RATE_36   */
	8,                             /* WL12XX_CONF_HW_RXTX_RATE_24   */

	/* TI-specific rate */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* WL12XX_CONF_HW_RXTX_RATE_22   */

	7,                             /* WL12XX_CONF_HW_RXTX_RATE_18   */
	6,                             /* WL12XX_CONF_HW_RXTX_RATE_12   */
	3,                             /* WL12XX_CONF_HW_RXTX_RATE_11   */
	5,                             /* WL12XX_CONF_HW_RXTX_RATE_9    */
	4,                             /* WL12XX_CONF_HW_RXTX_RATE_6    */
	2,                             /* WL12XX_CONF_HW_RXTX_RATE_5_5  */
	1,                             /* WL12XX_CONF_HW_RXTX_RATE_2    */
	0                              /* WL12XX_CONF_HW_RXTX_RATE_1    */
};

static const u8 wl12xx_rate_to_idx_5ghz[] = {
	/* MCS rates are used only with 11n */
	7,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS7_SGI */
	7,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS7 */
	6,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS6 */
	5,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS5 */
	4,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS4 */
	3,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS3 */
	2,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS2 */
	1,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS1 */
	0,                             /* WL12XX_CONF_HW_RXTX_RATE_MCS0 */

	7,                             /* WL12XX_CONF_HW_RXTX_RATE_54   */
	6,                             /* WL12XX_CONF_HW_RXTX_RATE_48   */
	5,                             /* WL12XX_CONF_HW_RXTX_RATE_36   */
	4,                             /* WL12XX_CONF_HW_RXTX_RATE_24   */

	/* TI-specific rate */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* WL12XX_CONF_HW_RXTX_RATE_22   */

	3,                             /* WL12XX_CONF_HW_RXTX_RATE_18   */
	2,                             /* WL12XX_CONF_HW_RXTX_RATE_12   */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* WL12XX_CONF_HW_RXTX_RATE_11   */
	1,                             /* WL12XX_CONF_HW_RXTX_RATE_9    */
	0,                             /* WL12XX_CONF_HW_RXTX_RATE_6    */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* WL12XX_CONF_HW_RXTX_RATE_5_5  */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* WL12XX_CONF_HW_RXTX_RATE_2    */
	CONF_HW_RXTX_RATE_UNSUPPORTED  /* WL12XX_CONF_HW_RXTX_RATE_1    */
};

static const u8 *wl12xx_band_rate_to_idx[] = {
	[IEEE80211_BAND_2GHZ] = wl12xx_rate_to_idx_2ghz,
	[IEEE80211_BAND_5GHZ] = wl12xx_rate_to_idx_5ghz
};

enum wl12xx_hw_rates {
	WL12XX_CONF_HW_RXTX_RATE_MCS7_SGI = 0,
	WL12XX_CONF_HW_RXTX_RATE_MCS7,
	WL12XX_CONF_HW_RXTX_RATE_MCS6,
	WL12XX_CONF_HW_RXTX_RATE_MCS5,
	WL12XX_CONF_HW_RXTX_RATE_MCS4,
	WL12XX_CONF_HW_RXTX_RATE_MCS3,
	WL12XX_CONF_HW_RXTX_RATE_MCS2,
	WL12XX_CONF_HW_RXTX_RATE_MCS1,
	WL12XX_CONF_HW_RXTX_RATE_MCS0,
	WL12XX_CONF_HW_RXTX_RATE_54,
	WL12XX_CONF_HW_RXTX_RATE_48,
	WL12XX_CONF_HW_RXTX_RATE_36,
	WL12XX_CONF_HW_RXTX_RATE_24,
	WL12XX_CONF_HW_RXTX_RATE_22,
	WL12XX_CONF_HW_RXTX_RATE_18,
	WL12XX_CONF_HW_RXTX_RATE_12,
	WL12XX_CONF_HW_RXTX_RATE_11,
	WL12XX_CONF_HW_RXTX_RATE_9,
	WL12XX_CONF_HW_RXTX_RATE_6,
	WL12XX_CONF_HW_RXTX_RATE_5_5,
	WL12XX_CONF_HW_RXTX_RATE_2,
	WL12XX_CONF_HW_RXTX_RATE_1,
	WL12XX_CONF_HW_RXTX_RATE_MAX,
};

static struct wlcore_partition_set wl12xx_ptable[PART_TABLE_LEN] = {
	[PART_DOWN] = {
		.mem = {
			.start = 0x00000000,
			.size  = 0x000177c0
		},
		.reg = {
			.start = REGISTERS_BASE,
			.size  = 0x00008800
		},
		.mem2 = {
			.start = 0x00000000,
			.size  = 0x00000000
		},
		.mem3 = {
			.start = 0x00000000,
			.size  = 0x00000000
		},
	},

	[PART_BOOT] = { /* in wl12xx we can use a mix of work and down
			 * partition here */
		.mem = {
			.start = 0x00040000,
			.size  = 0x00014fc0
		},
		.reg = {
			.start = REGISTERS_BASE,
			.size  = 0x00008800
		},
		.mem2 = {
			.start = 0x00000000,
			.size  = 0x00000000
		},
		.mem3 = {
			.start = 0x00000000,
			.size  = 0x00000000
		},
	},

	[PART_WORK] = {
		.mem = {
			.start = 0x00040000,
			.size  = 0x00014fc0
		},
		.reg = {
			.start = REGISTERS_BASE,
			.size  = 0x0000a000
		},
		.mem2 = {
			.start = 0x003004f8,
			.size  = 0x00000004
		},
		.mem3 = {
			.start = 0x00040404,
			.size  = 0x00000000
		},
	},

	[PART_DRPW] = {
		.mem = {
			.start = 0x00040000,
			.size  = 0x00014fc0
		},
		.reg = {
			.start = DRPW_BASE,
			.size  = 0x00006000
		},
		.mem2 = {
			.start = 0x00000000,
			.size  = 0x00000000
		},
		.mem3 = {
			.start = 0x00000000,
			.size  = 0x00000000
		}
	}
};

static const int wl12xx_rtable[REG_TABLE_LEN] = {
	[REG_ECPU_CONTROL]		= WL12XX_REG_ECPU_CONTROL,
	[REG_INTERRUPT_NO_CLEAR]	= WL12XX_REG_INTERRUPT_NO_CLEAR,
	[REG_INTERRUPT_ACK]		= WL12XX_REG_INTERRUPT_ACK,
	[REG_COMMAND_MAILBOX_PTR]	= WL12XX_REG_COMMAND_MAILBOX_PTR,
	[REG_EVENT_MAILBOX_PTR]		= WL12XX_REG_EVENT_MAILBOX_PTR,
	[REG_INTERRUPT_TRIG]		= WL12XX_REG_INTERRUPT_TRIG,
	[REG_INTERRUPT_MASK]		= WL12XX_REG_INTERRUPT_MASK,
	[REG_PC_ON_RECOVERY]		= WL12XX_SCR_PAD4,
	[REG_CHIP_ID_B]			= WL12XX_CHIP_ID_B,
	[REG_CMD_MBOX_ADDRESS]		= WL12XX_CMD_MBOX_ADDRESS,

	/* data access memory addresses, used with partition translation */
	[REG_SLV_MEM_DATA]		= WL1271_SLV_MEM_DATA,
	[REG_SLV_REG_DATA]		= WL1271_SLV_REG_DATA,

	/* raw data access memory addresses */
	[REG_RAW_FW_STATUS_ADDR]	= FW_STATUS_ADDR,
};

/* TODO: maybe move to a new header file? */
#define WL127X_FW_NAME_MULTI	"ti-connectivity/wl127x-fw-4-mr.bin"
#define WL127X_FW_NAME_SINGLE	"ti-connectivity/wl127x-fw-4-sr.bin"
#define WL127X_PLT_FW_NAME	"ti-connectivity/wl127x-fw-4-plt.bin"

#define WL128X_FW_NAME_MULTI	"ti-connectivity/wl128x-fw-4-mr.bin"
#define WL128X_FW_NAME_SINGLE	"ti-connectivity/wl128x-fw-4-sr.bin"
#define WL128X_PLT_FW_NAME	"ti-connectivity/wl128x-fw-4-plt.bin"

static void wl127x_prepare_read(struct wl1271 *wl, u32 rx_desc, u32 len)
{
	if (wl->chip.id != CHIP_ID_1283_PG20) {
		struct wl1271_acx_mem_map *wl_mem_map = wl->target_mem_map;
		struct wl1271_rx_mem_pool_addr rx_mem_addr;

		/*
		 * Choose the block we want to read
		 * For aggregated packets, only the first memory block
		 * should be retrieved. The FW takes care of the rest.
		 */
		u32 mem_block = rx_desc & RX_MEM_BLOCK_MASK;

		rx_mem_addr.addr = (mem_block << 8) +
			le32_to_cpu(wl_mem_map->packet_memory_pool_start);

		rx_mem_addr.addr_extra = rx_mem_addr.addr + 4;

		wl1271_write(wl, WL1271_SLV_REG_DATA,
			     &rx_mem_addr, sizeof(rx_mem_addr), false);
	}
}

static int wl12xx_identify_chip(struct wl1271 *wl)
{
	int ret = 0;

	switch (wl->chip.id) {
	case CHIP_ID_1271_PG10:
		wl1271_warning("chip id 0x%x (1271 PG10) support is obsolete",
			       wl->chip.id);

		/* clear the alignment quirk, since we don't support it */
		wl->quirks &= ~WLCORE_QUIRK_TX_BLOCKSIZE_ALIGN;

		wl->quirks |= WLCORE_QUIRK_LEGACY_NVS;
		wl->sr_fw_name = WL127X_FW_NAME_SINGLE;
		wl->mr_fw_name = WL127X_FW_NAME_MULTI;
		memcpy(&wl->conf.mem, &wl12xx_default_priv_conf.mem_wl127x,
		       sizeof(wl->conf.mem));

		/* read data preparation is only needed by wl127x */
		wl->ops->prepare_read = wl127x_prepare_read;

		break;

	case CHIP_ID_1271_PG20:
		wl1271_debug(DEBUG_BOOT, "chip id 0x%x (1271 PG20)",
			     wl->chip.id);

		/* clear the alignment quirk, since we don't support it */
		wl->quirks &= ~WLCORE_QUIRK_TX_BLOCKSIZE_ALIGN;

		wl->quirks |= WLCORE_QUIRK_LEGACY_NVS;
		wl->plt_fw_name = WL127X_PLT_FW_NAME;
		wl->sr_fw_name = WL127X_FW_NAME_SINGLE;
		wl->mr_fw_name = WL127X_FW_NAME_MULTI;
		memcpy(&wl->conf.mem, &wl12xx_default_priv_conf.mem_wl127x,
		       sizeof(wl->conf.mem));

		/* read data preparation is only needed by wl127x */
		wl->ops->prepare_read = wl127x_prepare_read;

		break;

	case CHIP_ID_1283_PG20:
		wl1271_debug(DEBUG_BOOT, "chip id 0x%x (1283 PG20)",
			     wl->chip.id);
		wl->plt_fw_name = WL128X_PLT_FW_NAME;
		wl->sr_fw_name = WL128X_FW_NAME_SINGLE;
		wl->mr_fw_name = WL128X_FW_NAME_MULTI;
		break;
	case CHIP_ID_1283_PG10:
	default:
		wl1271_warning("unsupported chip id: 0x%x", wl->chip.id);
		ret = -ENODEV;
		goto out;
	}

out:
	return ret;
}

static void wl12xx_top_reg_write(struct wl1271 *wl, int addr, u16 val)
{
	/* write address >> 1 + 0x30000 to OCP_POR_CTR */
	addr = (addr >> 1) + 0x30000;
	wl1271_write32(wl, WL12XX_OCP_POR_CTR, addr);

	/* write value to OCP_POR_WDATA */
	wl1271_write32(wl, WL12XX_OCP_DATA_WRITE, val);

	/* write 1 to OCP_CMD */
	wl1271_write32(wl, WL12XX_OCP_CMD, OCP_CMD_WRITE);
}

static u16 wl12xx_top_reg_read(struct wl1271 *wl, int addr)
{
	u32 val;
	int timeout = OCP_CMD_LOOP;

	/* write address >> 1 + 0x30000 to OCP_POR_CTR */
	addr = (addr >> 1) + 0x30000;
	wl1271_write32(wl, WL12XX_OCP_POR_CTR, addr);

	/* write 2 to OCP_CMD */
	wl1271_write32(wl, WL12XX_OCP_CMD, OCP_CMD_READ);

	/* poll for data ready */
	do {
		val = wl1271_read32(wl, WL12XX_OCP_DATA_READ);
	} while (!(val & OCP_READY_MASK) && --timeout);

	if (!timeout) {
		wl1271_warning("Top register access timed out.");
		return 0xffff;
	}

	/* check data status and return if OK */
	if ((val & OCP_STATUS_MASK) == OCP_STATUS_OK)
		return val & 0xffff;
	else {
		wl1271_warning("Top register access returned error.");
		return 0xffff;
	}
}

static int wl128x_switch_tcxo_to_fref(struct wl1271 *wl)
{
	u16 spare_reg;

	/* Mask bits [2] & [8:4] in the sys_clk_cfg register */
	spare_reg = wl12xx_top_reg_read(wl, WL_SPARE_REG);
	if (spare_reg == 0xFFFF)
		return -EFAULT;
	spare_reg |= (BIT(3) | BIT(5) | BIT(6));
	wl12xx_top_reg_write(wl, WL_SPARE_REG, spare_reg);

	/* Enable FREF_CLK_REQ & mux MCS and coex PLLs to FREF */
	wl12xx_top_reg_write(wl, SYS_CLK_CFG_REG,
			     WL_CLK_REQ_TYPE_PG2 | MCS_PLL_CLK_SEL_FREF);

	/* Delay execution for 15msec, to let the HW settle */
	mdelay(15);

	return 0;
}

static bool wl128x_is_tcxo_valid(struct wl1271 *wl)
{
	u16 tcxo_detection;

	tcxo_detection = wl12xx_top_reg_read(wl, TCXO_CLK_DETECT_REG);
	if (tcxo_detection & TCXO_DET_FAILED)
		return false;

	return true;
}

static bool wl128x_is_fref_valid(struct wl1271 *wl)
{
	u16 fref_detection;

	fref_detection = wl12xx_top_reg_read(wl, FREF_CLK_DETECT_REG);
	if (fref_detection & FREF_CLK_DETECT_FAIL)
		return false;

	return true;
}

static int wl128x_manually_configure_mcs_pll(struct wl1271 *wl)
{
	wl12xx_top_reg_write(wl, MCS_PLL_M_REG, MCS_PLL_M_REG_VAL);
	wl12xx_top_reg_write(wl, MCS_PLL_N_REG, MCS_PLL_N_REG_VAL);
	wl12xx_top_reg_write(wl, MCS_PLL_CONFIG_REG, MCS_PLL_CONFIG_REG_VAL);

	return 0;
}

static int wl128x_configure_mcs_pll(struct wl1271 *wl, int clk)
{
	u16 spare_reg;
	u16 pll_config;
	u8 input_freq;

	/* Mask bits [3:1] in the sys_clk_cfg register */
	spare_reg = wl12xx_top_reg_read(wl, WL_SPARE_REG);
	if (spare_reg == 0xFFFF)
		return -EFAULT;
	spare_reg |= BIT(2);
	wl12xx_top_reg_write(wl, WL_SPARE_REG, spare_reg);

	/* Handle special cases of the TCXO clock */
	if (wl->tcxo_clock == WL12XX_TCXOCLOCK_16_8 ||
	    wl->tcxo_clock == WL12XX_TCXOCLOCK_33_6)
		return wl128x_manually_configure_mcs_pll(wl);

	/* Set the input frequency according to the selected clock source */
	input_freq = (clk & 1) + 1;

	pll_config = wl12xx_top_reg_read(wl, MCS_PLL_CONFIG_REG);
	if (pll_config == 0xFFFF)
		return -EFAULT;
	pll_config |= (input_freq << MCS_SEL_IN_FREQ_SHIFT);
	pll_config |= MCS_PLL_ENABLE_HP;
	wl12xx_top_reg_write(wl, MCS_PLL_CONFIG_REG, pll_config);

	return 0;
}

/*
 * WL128x has two clocks input - TCXO and FREF.
 * TCXO is the main clock of the device, while FREF is used to sync
 * between the GPS and the cellular modem.
 * In cases where TCXO is 32.736MHz or 16.368MHz, the FREF will be used
 * as the WLAN/BT main clock.
 */
static int wl128x_boot_clk(struct wl1271 *wl, int *selected_clock)
{
	u16 sys_clk_cfg;

	/* For XTAL-only modes, FREF will be used after switching from TCXO */
	if (wl->ref_clock == WL12XX_REFCLOCK_26_XTAL ||
	    wl->ref_clock == WL12XX_REFCLOCK_38_XTAL) {
		if (!wl128x_switch_tcxo_to_fref(wl))
			return -EINVAL;
		goto fref_clk;
	}

	/* Query the HW, to determine which clock source we should use */
	sys_clk_cfg = wl12xx_top_reg_read(wl, SYS_CLK_CFG_REG);
	if (sys_clk_cfg == 0xFFFF)
		return -EINVAL;
	if (sys_clk_cfg & PRCM_CM_EN_MUX_WLAN_FREF)
		goto fref_clk;

	/* If TCXO is either 32.736MHz or 16.368MHz, switch to FREF */
	if (wl->tcxo_clock == WL12XX_TCXOCLOCK_16_368 ||
	    wl->tcxo_clock == WL12XX_TCXOCLOCK_32_736) {
		if (!wl128x_switch_tcxo_to_fref(wl))
			return -EINVAL;
		goto fref_clk;
	}

	/* TCXO clock is selected */
	if (!wl128x_is_tcxo_valid(wl))
		return -EINVAL;
	*selected_clock = wl->tcxo_clock;
	goto config_mcs_pll;

fref_clk:
	/* FREF clock is selected */
	if (!wl128x_is_fref_valid(wl))
		return -EINVAL;
	*selected_clock = wl->ref_clock;

config_mcs_pll:
	return wl128x_configure_mcs_pll(wl, *selected_clock);
}

static int wl127x_boot_clk(struct wl1271 *wl)
{
	u32 pause;
	u32 clk;

	if (WL127X_PG_GET_MAJOR(wl->hw_pg_ver) < 3)
		wl->quirks |= WLCORE_QUIRK_END_OF_TRANSACTION;

	if (wl->ref_clock == CONF_REF_CLK_19_2_E ||
	    wl->ref_clock == CONF_REF_CLK_38_4_E ||
	    wl->ref_clock == CONF_REF_CLK_38_4_M_XTAL)
		/* ref clk: 19.2/38.4/38.4-XTAL */
		clk = 0x3;
	else if (wl->ref_clock == CONF_REF_CLK_26_E ||
		 wl->ref_clock == CONF_REF_CLK_52_E)
		/* ref clk: 26/52 */
		clk = 0x5;
	else
		return -EINVAL;

	if (wl->ref_clock != CONF_REF_CLK_19_2_E) {
		u16 val;
		/* Set clock type (open drain) */
		val = wl12xx_top_reg_read(wl, OCP_REG_CLK_TYPE);
		val &= FREF_CLK_TYPE_BITS;
		wl12xx_top_reg_write(wl, OCP_REG_CLK_TYPE, val);

		/* Set clock pull mode (no pull) */
		val = wl12xx_top_reg_read(wl, OCP_REG_CLK_PULL);
		val |= NO_PULL;
		wl12xx_top_reg_write(wl, OCP_REG_CLK_PULL, val);
	} else {
		u16 val;
		/* Set clock polarity */
		val = wl12xx_top_reg_read(wl, OCP_REG_CLK_POLARITY);
		val &= FREF_CLK_POLARITY_BITS;
		val |= CLK_REQ_OUTN_SEL;
		wl12xx_top_reg_write(wl, OCP_REG_CLK_POLARITY, val);
	}

	wl1271_write32(wl, WL12XX_PLL_PARAMETERS, clk);

	pause = wl1271_read32(wl, WL12XX_PLL_PARAMETERS);

	wl1271_debug(DEBUG_BOOT, "pause1 0x%x", pause);

	pause &= ~(WU_COUNTER_PAUSE_VAL);
	pause |= WU_COUNTER_PAUSE_VAL;
	wl1271_write32(wl, WL12XX_WU_COUNTER_PAUSE, pause);

	return 0;
}

static int wl1271_boot_soft_reset(struct wl1271 *wl)
{
	unsigned long timeout;
	u32 boot_data;

	/* perform soft reset */
	wl1271_write32(wl, WL12XX_SLV_SOFT_RESET, ACX_SLV_SOFT_RESET_BIT);

	/* SOFT_RESET is self clearing */
	timeout = jiffies + usecs_to_jiffies(SOFT_RESET_MAX_TIME);
	while (1) {
		boot_data = wl1271_read32(wl, WL12XX_SLV_SOFT_RESET);
		wl1271_debug(DEBUG_BOOT, "soft reset bootdata 0x%x", boot_data);
		if ((boot_data & ACX_SLV_SOFT_RESET_BIT) == 0)
			break;

		if (time_after(jiffies, timeout)) {
			/* 1.2 check pWhalBus->uSelfClearTime if the
			 * timeout was reached */
			wl1271_error("soft reset timeout");
			return -1;
		}

		udelay(SOFT_RESET_STALL_TIME);
	}

	/* disable Rx/Tx */
	wl1271_write32(wl, WL12XX_ENABLE, 0x0);

	/* disable auto calibration on start*/
	wl1271_write32(wl, WL12XX_SPARE_A2, 0xffff);

	return 0;
}

static int wl12xx_pre_boot(struct wl1271 *wl)
{
	int ret = 0;
	u32 clk;
	int selected_clock = -1;

	if (wl->chip.id == CHIP_ID_1283_PG20) {
		ret = wl128x_boot_clk(wl, &selected_clock);
		if (ret < 0)
			goto out;
	} else {
		ret = wl127x_boot_clk(wl);
		if (ret < 0)
			goto out;
	}

	/* Continue the ELP wake up sequence */
	wl1271_write32(wl, WL12XX_WELP_ARM_COMMAND, WELP_ARM_COMMAND_VAL);
	udelay(500);

	wlcore_set_partition(wl, &wl->ptable[PART_DRPW]);

	/* Read-modify-write DRPW_SCRATCH_START register (see next state)
	   to be used by DRPw FW. The RTRIM value will be added by the FW
	   before taking DRPw out of reset */

	clk = wl1271_read32(wl, WL12XX_DRPW_SCRATCH_START);

	wl1271_debug(DEBUG_BOOT, "clk2 0x%x", clk);

	if (wl->chip.id == CHIP_ID_1283_PG20)
		clk |= ((selected_clock & 0x3) << 1) << 4;
	else
		clk |= (wl->ref_clock << 1) << 4;

	wl1271_write32(wl, WL12XX_DRPW_SCRATCH_START, clk);

	wlcore_set_partition(wl, &wl->ptable[PART_WORK]);

	/* Disable interrupts */
	wlcore_write_reg(wl, REG_INTERRUPT_MASK, WL1271_ACX_INTR_ALL);

	ret = wl1271_boot_soft_reset(wl);
	if (ret < 0)
		goto out;

out:
	return ret;
}

static void wl12xx_pre_upload(struct wl1271 *wl)
{
	u32 tmp;

	/* write firmware's last address (ie. it's length) to
	 * ACX_EEPROMLESS_IND_REG */
	wl1271_debug(DEBUG_BOOT, "ACX_EEPROMLESS_IND_REG");

	wl1271_write32(wl, WL12XX_EEPROMLESS_IND, WL12XX_EEPROMLESS_IND);

	tmp = wlcore_read_reg(wl, REG_CHIP_ID_B);

	wl1271_debug(DEBUG_BOOT, "chip id 0x%x", tmp);

	/* 6. read the EEPROM parameters */
	tmp = wl1271_read32(wl, WL12XX_SCR_PAD2);

	/* WL1271: The reference driver skips steps 7 to 10 (jumps directly
	 * to upload_fw) */

	if (wl->chip.id == CHIP_ID_1283_PG20)
		wl12xx_top_reg_write(wl, SDIO_IO_DS, HCI_IO_DS_6MA);
}

static void wl12xx_enable_interrupts(struct wl1271 *wl)
{
	u32 polarity;

	polarity = wl12xx_top_reg_read(wl, OCP_REG_POLARITY);

	/* We use HIGH polarity, so unset the LOW bit */
	polarity &= ~POLARITY_LOW;
	wl12xx_top_reg_write(wl, OCP_REG_POLARITY, polarity);

	wlcore_write_reg(wl, REG_INTERRUPT_MASK, WL1271_ACX_ALL_EVENTS_VECTOR);

	wlcore_enable_interrupts(wl);
	wlcore_write_reg(wl, REG_INTERRUPT_MASK,
			 WL1271_ACX_INTR_ALL & ~(WL1271_INTR_MASK));

	wl1271_write32(wl, WL12XX_HI_CFG, HI_CFG_DEF_VAL);
}

static int wl12xx_boot(struct wl1271 *wl)
{
	int ret;

	ret = wl12xx_pre_boot(wl);
	if (ret < 0)
		goto out;

	ret = wlcore_boot_upload_nvs(wl);
	if (ret < 0)
		goto out;

	wl12xx_pre_upload(wl);

	ret = wlcore_boot_upload_firmware(wl);
	if (ret < 0)
		goto out;

	ret = wlcore_boot_run_firmware(wl);
	if (ret < 0)
		goto out;

	wl12xx_enable_interrupts(wl);

out:
	return ret;
}

static void wl12xx_trigger_cmd(struct wl1271 *wl, int cmd_box_addr,
			       void *buf, size_t len)
{
	wl1271_write(wl, cmd_box_addr, buf, len, false);
	wlcore_write_reg(wl, REG_INTERRUPT_TRIG, WL12XX_INTR_TRIG_CMD);
}

static void wl12xx_ack_event(struct wl1271 *wl)
{
	wlcore_write_reg(wl, REG_INTERRUPT_TRIG, WL12XX_INTR_TRIG_EVENT_ACK);
}

static u32 wl12xx_calc_tx_blocks(struct wl1271 *wl, u32 len, u32 spare_blks)
{
	u32 blk_size = WL12XX_TX_HW_BLOCK_SIZE;
	u32 align_len = wlcore_calc_packet_alignment(wl, len);

	return (align_len + blk_size - 1) / blk_size + spare_blks;
}

static void
wl12xx_set_tx_desc_blocks(struct wl1271 *wl, struct wl1271_tx_hw_descr *desc,
			  u32 blks, u32 spare_blks)
{
	if (wl->chip.id == CHIP_ID_1283_PG20) {
		desc->wl128x_mem.total_mem_blocks = blks;
	} else {
		desc->wl127x_mem.extra_blocks = spare_blks;
		desc->wl127x_mem.total_mem_blocks = blks;
	}
}

static void
wl12xx_set_tx_desc_data_len(struct wl1271 *wl, struct wl1271_tx_hw_descr *desc,
			    struct sk_buff *skb)
{
	u32 aligned_len = wlcore_calc_packet_alignment(wl, skb->len);

	if (wl->chip.id == CHIP_ID_1283_PG20) {
		desc->wl128x_mem.extra_bytes = aligned_len - skb->len;
		desc->length = cpu_to_le16(aligned_len >> 2);

		wl1271_debug(DEBUG_TX,
			     "tx_fill_hdr: hlid: %d len: %d life: %d mem: %d extra: %d",
			     desc->hlid,
			     le16_to_cpu(desc->length),
			     le16_to_cpu(desc->life_time),
			     desc->wl128x_mem.total_mem_blocks,
			     desc->wl128x_mem.extra_bytes);
	} else {
		/* calculate number of padding bytes */
		int pad = aligned_len - skb->len;
		desc->tx_attr |=
			cpu_to_le16(pad << TX_HW_ATTR_OFST_LAST_WORD_PAD);

		/* Store the aligned length in terms of words */
		desc->length = cpu_to_le16(aligned_len >> 2);

		wl1271_debug(DEBUG_TX,
			     "tx_fill_hdr: pad: %d hlid: %d len: %d life: %d mem: %d",
			     pad, desc->hlid,
			     le16_to_cpu(desc->length),
			     le16_to_cpu(desc->life_time),
			     desc->wl127x_mem.total_mem_blocks);
	}
}

static enum wl_rx_buf_align
wl12xx_get_rx_buf_align(struct wl1271 *wl, u32 rx_desc)
{
	if (rx_desc & RX_BUF_UNALIGNED_PAYLOAD)
		return WLCORE_RX_BUF_UNALIGNED;

	return WLCORE_RX_BUF_ALIGNED;
}

static u32 wl12xx_get_rx_packet_len(struct wl1271 *wl, void *rx_data,
				    u32 data_len)
{
	struct wl1271_rx_descriptor *desc = rx_data;

	/* invalid packet */
	if (data_len < sizeof(*desc) ||
	    data_len < sizeof(*desc) + desc->pad_len)
		return 0;

	return data_len - sizeof(*desc) - desc->pad_len;
}

static void wl12xx_tx_delayed_compl(struct wl1271 *wl)
{
	if (wl->fw_status->tx_results_counter == (wl->tx_results_count & 0xff))
		return;

	wl1271_tx_complete(wl);
}

static int wl12xx_hw_init(struct wl1271 *wl)
{
	int ret;

	if (wl->chip.id == CHIP_ID_1283_PG20) {
		u32 host_cfg_bitmap = HOST_IF_CFG_RX_FIFO_ENABLE;

		ret = wl128x_cmd_general_parms(wl);
		if (ret < 0)
			goto out;
		ret = wl128x_cmd_radio_parms(wl);
		if (ret < 0)
			goto out;

		if (wl->quirks & WLCORE_QUIRK_TX_BLOCKSIZE_ALIGN)
			/* Enable SDIO padding */
			host_cfg_bitmap |= HOST_IF_CFG_TX_PAD_TO_SDIO_BLK;

		/* Must be before wl1271_acx_init_mem_config() */
		ret = wl1271_acx_host_if_cfg_bitmap(wl, host_cfg_bitmap);
		if (ret < 0)
			goto out;
	} else {
		ret = wl1271_cmd_general_parms(wl);
		if (ret < 0)
			goto out;
		ret = wl1271_cmd_radio_parms(wl);
		if (ret < 0)
			goto out;
		ret = wl1271_cmd_ext_radio_parms(wl);
		if (ret < 0)
			goto out;
	}
out:
	return ret;
}

static u32 wl12xx_sta_get_ap_rate_mask(struct wl1271 *wl,
				       struct wl12xx_vif *wlvif)
{
	return wlvif->rate_set;
}

static int wl12xx_identify_fw(struct wl1271 *wl)
{
	unsigned int *fw_ver = wl->chip.fw_ver;

	/* Only new station firmwares support routing fw logs to the host */
	if ((fw_ver[FW_VER_IF_TYPE] == FW_VER_IF_TYPE_STA) &&
	    (fw_ver[FW_VER_MINOR] < FW_VER_MINOR_FWLOG_STA_MIN))
		wl->quirks |= WLCORE_QUIRK_FWLOG_NOT_IMPLEMENTED;

	/* This feature is not yet supported for AP mode */
	if (fw_ver[FW_VER_IF_TYPE] == FW_VER_IF_TYPE_AP)
		wl->quirks |= WLCORE_QUIRK_FWLOG_NOT_IMPLEMENTED;

	return 0;
}

static void wl12xx_conf_init(struct wl1271 *wl)
{
	struct wl12xx_priv *priv = wl->priv;

	/* apply driver default configuration */
	memcpy(&wl->conf, &wl12xx_conf, sizeof(wl12xx_conf));

	/* apply default private configuration */
	memcpy(&priv->conf, &wl12xx_default_priv_conf, sizeof(priv->conf));
}

static bool wl12xx_mac_in_fuse(struct wl1271 *wl)
{
	bool supported = false;
	u8 major, minor;

	if (wl->chip.id == CHIP_ID_1283_PG20) {
		major = WL128X_PG_GET_MAJOR(wl->hw_pg_ver);
		minor = WL128X_PG_GET_MINOR(wl->hw_pg_ver);

		/* in wl128x we have the MAC address if the PG is >= (2, 1) */
		if (major > 2 || (major == 2 && minor >= 1))
			supported = true;
	} else {
		major = WL127X_PG_GET_MAJOR(wl->hw_pg_ver);
		minor = WL127X_PG_GET_MINOR(wl->hw_pg_ver);

		/* in wl127x we have the MAC address if the PG is >= (3, 1) */
		if (major == 3 && minor >= 1)
			supported = true;
	}

	wl1271_debug(DEBUG_PROBE,
		     "PG Ver major = %d minor = %d, MAC %s present",
		     major, minor, supported ? "is" : "is not");

	return supported;
}

static void wl12xx_get_fuse_mac(struct wl1271 *wl)
{
	u32 mac1, mac2;

	wlcore_set_partition(wl, &wl->ptable[PART_DRPW]);

	mac1 = wl1271_read32(wl, WL12XX_REG_FUSE_BD_ADDR_1);
	mac2 = wl1271_read32(wl, WL12XX_REG_FUSE_BD_ADDR_2);

	/* these are the two parts of the BD_ADDR */
	wl->fuse_oui_addr = ((mac2 & 0xffff) << 8) +
		((mac1 & 0xff000000) >> 24);
	wl->fuse_nic_addr = mac1 & 0xffffff;

	wlcore_set_partition(wl, &wl->ptable[PART_DOWN]);
}

static s8 wl12xx_get_pg_ver(struct wl1271 *wl)
{
	u32 die_info;

	if (wl->chip.id == CHIP_ID_1283_PG20)
		die_info = wl12xx_top_reg_read(wl, WL128X_REG_FUSE_DATA_2_1);
	else
		die_info = wl12xx_top_reg_read(wl, WL127X_REG_FUSE_DATA_2_1);

	return (s8) (die_info & PG_VER_MASK) >> PG_VER_OFFSET;
}

static void wl12xx_get_mac(struct wl1271 *wl)
{
	if (wl12xx_mac_in_fuse(wl))
		wl12xx_get_fuse_mac(wl);
}

static struct wlcore_ops wl12xx_ops = {
	.identify_chip		= wl12xx_identify_chip,
	.identify_fw		= wl12xx_identify_fw,
	.boot			= wl12xx_boot,
	.trigger_cmd		= wl12xx_trigger_cmd,
	.ack_event		= wl12xx_ack_event,
	.calc_tx_blocks		= wl12xx_calc_tx_blocks,
	.set_tx_desc_blocks	= wl12xx_set_tx_desc_blocks,
	.set_tx_desc_data_len	= wl12xx_set_tx_desc_data_len,
	.get_rx_buf_align	= wl12xx_get_rx_buf_align,
	.get_rx_packet_len	= wl12xx_get_rx_packet_len,
	.tx_immediate_compl	= NULL,
	.tx_delayed_compl	= wl12xx_tx_delayed_compl,
	.hw_init		= wl12xx_hw_init,
	.init_vif		= NULL,
	.sta_get_ap_rate_mask	= wl12xx_sta_get_ap_rate_mask,
	.get_pg_ver		= wl12xx_get_pg_ver,
	.get_mac		= wl12xx_get_mac,
};

static struct ieee80211_sta_ht_cap wl12xx_ht_cap = {
	.cap = IEEE80211_HT_CAP_GRN_FLD | IEEE80211_HT_CAP_SGI_20 |
	       (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT),
	.ht_supported = true,
	.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_8,
	.mcs = {
		.rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		.rx_highest = cpu_to_le16(72),
		.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
};

static int __devinit wl12xx_probe(struct platform_device *pdev)
{
	struct wl1271 *wl;
	struct ieee80211_hw *hw;
	struct wl12xx_priv *priv;

	hw = wlcore_alloc_hw(sizeof(*priv));
	if (IS_ERR(hw)) {
		wl1271_error("can't allocate hw");
		return PTR_ERR(hw);
	}

	wl = hw->priv;
	wl->ops = &wl12xx_ops;
	wl->ptable = wl12xx_ptable;
	wl->rtable = wl12xx_rtable;
	wl->num_tx_desc = 16;
	wl->normal_tx_spare = WL12XX_TX_HW_BLOCK_SPARE_DEFAULT;
	wl->gem_tx_spare = WL12XX_TX_HW_BLOCK_GEM_SPARE;
	wl->band_rate_to_idx = wl12xx_band_rate_to_idx;
	wl->hw_tx_rate_tbl_size = WL12XX_CONF_HW_RXTX_RATE_MAX;
	wl->hw_min_ht_rate = WL12XX_CONF_HW_RXTX_RATE_MCS0;
	wl->fw_status_priv_len = 0;
	memcpy(&wl->ht_cap, &wl12xx_ht_cap, sizeof(wl12xx_ht_cap));
	wl12xx_conf_init(wl);

	return wlcore_probe(wl, pdev);
}

static const struct platform_device_id wl12xx_id_table[] __devinitconst = {
	{ "wl12xx", 0 },
	{  } /* Terminating Entry */
};
MODULE_DEVICE_TABLE(platform, wl12xx_id_table);

static struct platform_driver wl12xx_driver = {
	.probe		= wl12xx_probe,
	.remove		= __devexit_p(wlcore_remove),
	.id_table	= wl12xx_id_table,
	.driver = {
		.name	= "wl12xx_driver",
		.owner	= THIS_MODULE,
	}
};

static int __init wl12xx_init(void)
{
	return platform_driver_register(&wl12xx_driver);
}
module_init(wl12xx_init);

static void __exit wl12xx_exit(void)
{
	platform_driver_unregister(&wl12xx_driver);
}
module_exit(wl12xx_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Luciano Coelho <coelho@ti.com>");
MODULE_FIRMWARE(WL127X_FW_NAME_SINGLE);
MODULE_FIRMWARE(WL127X_FW_NAME_MULTI);
MODULE_FIRMWARE(WL127X_PLT_FW_NAME);
MODULE_FIRMWARE(WL128X_FW_NAME_SINGLE);
MODULE_FIRMWARE(WL128X_FW_NAME_MULTI);
MODULE_FIRMWARE(WL128X_PLT_FW_NAME);
