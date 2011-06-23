/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2010 Nokia Corporation
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

#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/crc32.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/wl12xx.h>

#include "wl12xx.h"
#include "wl12xx_80211.h"
#include "reg.h"
#include "io.h"
#include "event.h"
#include "tx.h"
#include "rx.h"
#include "ps.h"
#include "init.h"
#include "debugfs.h"
#include "cmd.h"
#include "boot.h"
#include "testmode.h"
#include "scan.h"

#define WL1271_BOOT_RETRIES 3

static struct conf_drv_settings default_conf = {
	.sg = {
		.sta_params = {
			[CONF_SG_BT_PER_THRESHOLD]                  = 7500,
			[CONF_SG_HV3_MAX_OVERRIDE]                  = 0,
			[CONF_SG_BT_NFS_SAMPLE_INTERVAL]            = 400,
			[CONF_SG_BT_LOAD_RATIO]                     = 200,
			[CONF_SG_AUTO_PS_MODE]                      = 1,
			[CONF_SG_AUTO_SCAN_PROBE_REQ]               = 170,
			[CONF_SG_ACTIVE_SCAN_DURATION_FACTOR_HV3]   = 50,
			[CONF_SG_ANTENNA_CONFIGURATION]             = 0,
			[CONF_SG_BEACON_MISS_PERCENT]               = 60,
			[CONF_SG_RATE_ADAPT_THRESH]                 = 12,
			[CONF_SG_RATE_ADAPT_SNR]                    = 0,
			[CONF_SG_WLAN_PS_BT_ACL_MASTER_MIN_BR]      = 10,
			[CONF_SG_WLAN_PS_BT_ACL_MASTER_MAX_BR]      = 30,
			[CONF_SG_WLAN_PS_MAX_BT_ACL_MASTER_BR]      = 8,
			[CONF_SG_WLAN_PS_BT_ACL_SLAVE_MIN_BR]       = 20,
			[CONF_SG_WLAN_PS_BT_ACL_SLAVE_MAX_BR]       = 50,
			/* Note: with UPSD, this should be 4 */
			[CONF_SG_WLAN_PS_MAX_BT_ACL_SLAVE_BR]       = 8,
			[CONF_SG_WLAN_PS_BT_ACL_MASTER_MIN_EDR]     = 7,
			[CONF_SG_WLAN_PS_BT_ACL_MASTER_MAX_EDR]     = 25,
			[CONF_SG_WLAN_PS_MAX_BT_ACL_MASTER_EDR]     = 20,
			/* Note: with UPDS, this should be 15 */
			[CONF_SG_WLAN_PS_BT_ACL_SLAVE_MIN_EDR]      = 8,
			/* Note: with UPDS, this should be 50 */
			[CONF_SG_WLAN_PS_BT_ACL_SLAVE_MAX_EDR]      = 40,
			/* Note: with UPDS, this should be 10 */
			[CONF_SG_WLAN_PS_MAX_BT_ACL_SLAVE_EDR]      = 20,
			[CONF_SG_RXT]                               = 1200,
			[CONF_SG_TXT]                               = 1000,
			[CONF_SG_ADAPTIVE_RXT_TXT]                  = 1,
			[CONF_SG_PS_POLL_TIMEOUT]                   = 10,
			[CONF_SG_UPSD_TIMEOUT]                      = 10,
			[CONF_SG_WLAN_ACTIVE_BT_ACL_MASTER_MIN_EDR] = 7,
			[CONF_SG_WLAN_ACTIVE_BT_ACL_MASTER_MAX_EDR] = 15,
			[CONF_SG_WLAN_ACTIVE_MAX_BT_ACL_MASTER_EDR] = 15,
			[CONF_SG_WLAN_ACTIVE_BT_ACL_SLAVE_MIN_EDR]  = 8,
			[CONF_SG_WLAN_ACTIVE_BT_ACL_SLAVE_MAX_EDR]  = 20,
			[CONF_SG_WLAN_ACTIVE_MAX_BT_ACL_SLAVE_EDR]  = 15,
			[CONF_SG_WLAN_ACTIVE_BT_ACL_MIN_BR]         = 20,
			[CONF_SG_WLAN_ACTIVE_BT_ACL_MAX_BR]         = 50,
			[CONF_SG_WLAN_ACTIVE_MAX_BT_ACL_BR]         = 10,
			[CONF_SG_PASSIVE_SCAN_DURATION_FACTOR_HV3]  = 200,
			[CONF_SG_PASSIVE_SCAN_DURATION_FACTOR_A2DP] = 800,
			[CONF_SG_PASSIVE_SCAN_A2DP_BT_TIME]         = 75,
			[CONF_SG_PASSIVE_SCAN_A2DP_WLAN_TIME]       = 15,
			[CONF_SG_HV3_MAX_SERVED]                    = 6,
			[CONF_SG_DHCP_TIME]                         = 5000,
			[CONF_SG_ACTIVE_SCAN_DURATION_FACTOR_A2DP]  = 100,
		},
		.ap_params = {
			[CONF_SG_BT_PER_THRESHOLD]                  = 7500,
			[CONF_SG_HV3_MAX_OVERRIDE]                  = 0,
			[CONF_SG_BT_NFS_SAMPLE_INTERVAL]            = 400,
			[CONF_SG_BT_LOAD_RATIO]                     = 50,
			[CONF_SG_AUTO_PS_MODE]                      = 1,
			[CONF_SG_AUTO_SCAN_PROBE_REQ]               = 170,
			[CONF_SG_ACTIVE_SCAN_DURATION_FACTOR_HV3]   = 50,
			[CONF_SG_ANTENNA_CONFIGURATION]             = 0,
			[CONF_SG_BEACON_MISS_PERCENT]               = 60,
			[CONF_SG_RATE_ADAPT_THRESH]                 = 64,
			[CONF_SG_RATE_ADAPT_SNR]                    = 1,
			[CONF_SG_WLAN_PS_BT_ACL_MASTER_MIN_BR]      = 10,
			[CONF_SG_WLAN_PS_BT_ACL_MASTER_MAX_BR]      = 25,
			[CONF_SG_WLAN_PS_MAX_BT_ACL_MASTER_BR]      = 25,
			[CONF_SG_WLAN_PS_BT_ACL_SLAVE_MIN_BR]       = 20,
			[CONF_SG_WLAN_PS_BT_ACL_SLAVE_MAX_BR]       = 25,
			[CONF_SG_WLAN_PS_MAX_BT_ACL_SLAVE_BR]       = 25,
			[CONF_SG_WLAN_PS_BT_ACL_MASTER_MIN_EDR]     = 7,
			[CONF_SG_WLAN_PS_BT_ACL_MASTER_MAX_EDR]     = 25,
			[CONF_SG_WLAN_PS_MAX_BT_ACL_MASTER_EDR]     = 25,
			[CONF_SG_WLAN_PS_BT_ACL_SLAVE_MIN_EDR]      = 8,
			[CONF_SG_WLAN_PS_BT_ACL_SLAVE_MAX_EDR]      = 25,
			[CONF_SG_WLAN_PS_MAX_BT_ACL_SLAVE_EDR]      = 25,
			[CONF_SG_RXT]                               = 1200,
			[CONF_SG_TXT]                               = 1000,
			[CONF_SG_ADAPTIVE_RXT_TXT]                  = 1,
			[CONF_SG_PS_POLL_TIMEOUT]                   = 10,
			[CONF_SG_UPSD_TIMEOUT]                      = 10,
			[CONF_SG_WLAN_ACTIVE_BT_ACL_MASTER_MIN_EDR] = 7,
			[CONF_SG_WLAN_ACTIVE_BT_ACL_MASTER_MAX_EDR] = 15,
			[CONF_SG_WLAN_ACTIVE_MAX_BT_ACL_MASTER_EDR] = 15,
			[CONF_SG_WLAN_ACTIVE_BT_ACL_SLAVE_MIN_EDR]  = 8,
			[CONF_SG_WLAN_ACTIVE_BT_ACL_SLAVE_MAX_EDR]  = 20,
			[CONF_SG_WLAN_ACTIVE_MAX_BT_ACL_SLAVE_EDR]  = 15,
			[CONF_SG_WLAN_ACTIVE_BT_ACL_MIN_BR]         = 20,
			[CONF_SG_WLAN_ACTIVE_BT_ACL_MAX_BR]         = 50,
			[CONF_SG_WLAN_ACTIVE_MAX_BT_ACL_BR]         = 10,
			[CONF_SG_PASSIVE_SCAN_DURATION_FACTOR_HV3]  = 200,
			[CONF_SG_PASSIVE_SCAN_DURATION_FACTOR_A2DP] = 800,
			[CONF_SG_PASSIVE_SCAN_A2DP_BT_TIME]         = 75,
			[CONF_SG_PASSIVE_SCAN_A2DP_WLAN_TIME]       = 15,
			[CONF_SG_HV3_MAX_SERVED]                    = 6,
			[CONF_SG_DHCP_TIME]                         = 5000,
			[CONF_SG_ACTIVE_SCAN_DURATION_FACTOR_A2DP]  = 100,
			[CONF_SG_TEMP_PARAM_1]                      = 0,
			[CONF_SG_TEMP_PARAM_2]                      = 0,
			[CONF_SG_TEMP_PARAM_3]                      = 0,
			[CONF_SG_TEMP_PARAM_4]                      = 0,
			[CONF_SG_TEMP_PARAM_5]                      = 0,
			[CONF_SG_AP_BEACON_MISS_TX]                 = 3,
			[CONF_SG_RX_WINDOW_LENGTH]                  = 6,
			[CONF_SG_AP_CONNECTION_PROTECTION_TIME]     = 50,
			[CONF_SG_TEMP_PARAM_6]                      = 1,
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
		.ap_max_tx_retries = 100,
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
	},
	.conn = {
		.wake_up_event               = CONF_WAKE_UP_EVENT_DTIM,
		.listen_interval             = 1,
		.bcn_filt_mode               = CONF_BCN_FILT_MODE_ENABLED,
		.bcn_filt_ie_count           = 2,
		.bcn_filt_ie = {
			[0] = {
				.ie          = WLAN_EID_CHANNEL_SWITCH,
				.rule        = CONF_BCN_RULE_PASS_ON_APPEARANCE,
			},
			[1] = {
				.ie          = WLAN_EID_HT_INFORMATION,
				.rule        = CONF_BCN_RULE_PASS_ON_CHANGE,
			},
		},
		.synch_fail_thold            = 10,
		.bss_lose_timeout            = 100,
		.beacon_rx_timeout           = 10000,
		.broadcast_timeout           = 20000,
		.rx_broadcast_in_ps          = 1,
		.ps_poll_threshold           = 10,
		.ps_poll_recovery_period     = 700,
		.bet_enable                  = CONF_BET_MODE_ENABLE,
		.bet_max_consecutive         = 50,
		.psm_entry_retries           = 5,
		.psm_exit_retries            = 16,
		.psm_entry_nullfunc_retries  = 3,
		.psm_entry_hangover_period   = 1,
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
	},
	.sched_scan = {
		/* sched_scan requires dwell times in TU instead of TU/1000 */
		.min_dwell_time_active = 8,
		.max_dwell_time_active = 30,
		.dwell_time_passive    = 100,
		.dwell_time_dfs        = 150,
		.num_probe_reqs        = 2,
		.rssi_threshold        = -90,
		.snr_threshold         = 0,
	},
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
	.ht = {
		.tx_ba_win_size = 64,
		.inactivity_timeout = 10000,
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
	.mem_wl128x = {
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
	.hci_io_ds = HCI_IO_DS_6MA,
};

static void __wl1271_op_remove_interface(struct wl1271 *wl,
					 bool reset_tx_queues);
static void wl1271_free_ap_keys(struct wl1271 *wl);


static void wl1271_device_release(struct device *dev)
{

}

static struct platform_device wl1271_device = {
	.name           = "wl1271",
	.id             = -1,

	/* device model insists to have a release function */
	.dev            = {
		.release = wl1271_device_release,
	},
};

static DEFINE_MUTEX(wl_list_mutex);
static LIST_HEAD(wl_list);

static int wl1271_dev_notify(struct notifier_block *me, unsigned long what,
			     void *arg)
{
	struct net_device *dev = arg;
	struct wireless_dev *wdev;
	struct wiphy *wiphy;
	struct ieee80211_hw *hw;
	struct wl1271 *wl;
	struct wl1271 *wl_temp;
	int ret = 0;

	/* Check that this notification is for us. */
	if (what != NETDEV_CHANGE)
		return NOTIFY_DONE;

	wdev = dev->ieee80211_ptr;
	if (wdev == NULL)
		return NOTIFY_DONE;

	wiphy = wdev->wiphy;
	if (wiphy == NULL)
		return NOTIFY_DONE;

	hw = wiphy_priv(wiphy);
	if (hw == NULL)
		return NOTIFY_DONE;

	wl_temp = hw->priv;
	mutex_lock(&wl_list_mutex);
	list_for_each_entry(wl, &wl_list, list) {
		if (wl == wl_temp)
			break;
	}
	mutex_unlock(&wl_list_mutex);
	if (wl != wl_temp)
		return NOTIFY_DONE;

	mutex_lock(&wl->mutex);

	if (wl->state == WL1271_STATE_OFF)
		goto out;

	if (!test_bit(WL1271_FLAG_STA_ASSOCIATED, &wl->flags))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	if ((dev->operstate == IF_OPER_UP) &&
	    !test_and_set_bit(WL1271_FLAG_STA_STATE_SENT, &wl->flags)) {
		wl1271_cmd_set_sta_state(wl);
		wl1271_info("Association completed.");
	}

	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);

	return NOTIFY_OK;
}

static int wl1271_reg_notify(struct wiphy *wiphy,
			     struct regulatory_request *request)
{
	struct ieee80211_supported_band *band;
	struct ieee80211_channel *ch;
	int i;

	band = wiphy->bands[IEEE80211_BAND_5GHZ];
	for (i = 0; i < band->n_channels; i++) {
		ch = &band->channels[i];
		if (ch->flags & IEEE80211_CHAN_DISABLED)
			continue;

		if (ch->flags & IEEE80211_CHAN_RADAR)
			ch->flags |= IEEE80211_CHAN_NO_IBSS |
				     IEEE80211_CHAN_PASSIVE_SCAN;

	}

	return 0;
}

static void wl1271_conf_init(struct wl1271 *wl)
{

	/*
	 * This function applies the default configuration to the driver. This
	 * function is invoked upon driver load (spi probe.)
	 *
	 * The configuration is stored in a run-time structure in order to
	 * facilitate for run-time adjustment of any of the parameters. Making
	 * changes to the configuration structure will apply the new values on
	 * the next interface up (wl1271_op_start.)
	 */

	/* apply driver default configuration */
	memcpy(&wl->conf, &default_conf, sizeof(default_conf));
}


static int wl1271_plt_init(struct wl1271 *wl)
{
	struct conf_tx_ac_category *conf_ac;
	struct conf_tx_tid *conf_tid;
	int ret, i;

	if (wl->chip.id == CHIP_ID_1283_PG20)
		ret = wl128x_cmd_general_parms(wl);
	else
		ret = wl1271_cmd_general_parms(wl);
	if (ret < 0)
		return ret;

	if (wl->chip.id == CHIP_ID_1283_PG20)
		ret = wl128x_cmd_radio_parms(wl);
	else
		ret = wl1271_cmd_radio_parms(wl);
	if (ret < 0)
		return ret;

	if (wl->chip.id != CHIP_ID_1283_PG20) {
		ret = wl1271_cmd_ext_radio_parms(wl);
		if (ret < 0)
			return ret;
	}
	if (ret < 0)
		return ret;

	/* Chip-specific initializations */
	ret = wl1271_chip_specific_init(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_sta_init_templates_config(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_init_mem_config(wl);
	if (ret < 0)
		return ret;

	/* PHY layer config */
	ret = wl1271_init_phy_config(wl);
	if (ret < 0)
		goto out_free_memmap;

	ret = wl1271_acx_dco_itrim_params(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Initialize connection monitoring thresholds */
	ret = wl1271_acx_conn_monit_params(wl, false);
	if (ret < 0)
		goto out_free_memmap;

	/* Bluetooth WLAN coexistence */
	ret = wl1271_init_pta(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* FM WLAN coexistence */
	ret = wl1271_acx_fm_coex(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Energy detection */
	ret = wl1271_init_energy_detection(wl);
	if (ret < 0)
		goto out_free_memmap;

	ret = wl1271_acx_sta_mem_cfg(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Default fragmentation threshold */
	ret = wl1271_acx_frag_threshold(wl, wl->conf.tx.frag_threshold);
	if (ret < 0)
		goto out_free_memmap;

	/* Default TID/AC configuration */
	BUG_ON(wl->conf.tx.tid_conf_count != wl->conf.tx.ac_conf_count);
	for (i = 0; i < wl->conf.tx.tid_conf_count; i++) {
		conf_ac = &wl->conf.tx.ac_conf[i];
		ret = wl1271_acx_ac_cfg(wl, conf_ac->ac, conf_ac->cw_min,
					conf_ac->cw_max, conf_ac->aifsn,
					conf_ac->tx_op_limit);
		if (ret < 0)
			goto out_free_memmap;

		conf_tid = &wl->conf.tx.tid_conf[i];
		ret = wl1271_acx_tid_cfg(wl, conf_tid->queue_id,
					 conf_tid->channel_type,
					 conf_tid->tsid,
					 conf_tid->ps_scheme,
					 conf_tid->ack_policy,
					 conf_tid->apsd_conf[0],
					 conf_tid->apsd_conf[1]);
		if (ret < 0)
			goto out_free_memmap;
	}

	/* Enable data path */
	ret = wl1271_cmd_data_path(wl, 1);
	if (ret < 0)
		goto out_free_memmap;

	/* Configure for CAM power saving (ie. always active) */
	ret = wl1271_acx_sleep_auth(wl, WL1271_PSM_CAM);
	if (ret < 0)
		goto out_free_memmap;

	/* configure PM */
	ret = wl1271_acx_pm_config(wl);
	if (ret < 0)
		goto out_free_memmap;

	return 0;

 out_free_memmap:
	kfree(wl->target_mem_map);
	wl->target_mem_map = NULL;

	return ret;
}

static void wl1271_irq_ps_regulate_link(struct wl1271 *wl, u8 hlid, u8 tx_blks)
{
	bool fw_ps;

	/* only regulate station links */
	if (hlid < WL1271_AP_STA_HLID_START)
		return;

	fw_ps = test_bit(hlid, (unsigned long *)&wl->ap_fw_ps_map);

	/*
	 * Wake up from high level PS if the STA is asleep with too little
	 * blocks in FW or if the STA is awake.
	 */
	if (!fw_ps || tx_blks < WL1271_PS_STA_MAX_BLOCKS)
		wl1271_ps_link_end(wl, hlid);

	/* Start high-level PS if the STA is asleep with enough blocks in FW */
	else if (fw_ps && tx_blks >= WL1271_PS_STA_MAX_BLOCKS)
		wl1271_ps_link_start(wl, hlid, true);
}

static void wl1271_irq_update_links_status(struct wl1271 *wl,
				       struct wl1271_fw_ap_status *status)
{
	u32 cur_fw_ps_map;
	u8 hlid;

	cur_fw_ps_map = le32_to_cpu(status->link_ps_bitmap);
	if (wl->ap_fw_ps_map != cur_fw_ps_map) {
		wl1271_debug(DEBUG_PSM,
			     "link ps prev 0x%x cur 0x%x changed 0x%x",
			     wl->ap_fw_ps_map, cur_fw_ps_map,
			     wl->ap_fw_ps_map ^ cur_fw_ps_map);

		wl->ap_fw_ps_map = cur_fw_ps_map;
	}

	for (hlid = WL1271_AP_STA_HLID_START; hlid < AP_MAX_LINKS; hlid++) {
		u8 cnt = status->tx_lnk_free_blks[hlid] -
			wl->links[hlid].prev_freed_blks;

		wl->links[hlid].prev_freed_blks =
			status->tx_lnk_free_blks[hlid];
		wl->links[hlid].allocated_blks -= cnt;

		wl1271_irq_ps_regulate_link(wl, hlid,
					    wl->links[hlid].allocated_blks);
	}
}

static void wl1271_fw_status(struct wl1271 *wl,
			     struct wl1271_fw_full_status *full_status)
{
	struct wl1271_fw_common_status *status = &full_status->common;
	struct timespec ts;
	u32 old_tx_blk_count = wl->tx_blocks_available;
	u32 freed_blocks = 0;
	int i;

	if (wl->bss_type == BSS_TYPE_AP_BSS) {
		wl1271_raw_read(wl, FW_STATUS_ADDR, status,
				sizeof(struct wl1271_fw_ap_status), false);
	} else {
		wl1271_raw_read(wl, FW_STATUS_ADDR, status,
				sizeof(struct wl1271_fw_sta_status), false);
	}

	wl1271_debug(DEBUG_IRQ, "intr: 0x%x (fw_rx_counter = %d, "
		     "drv_rx_counter = %d, tx_results_counter = %d)",
		     status->intr,
		     status->fw_rx_counter,
		     status->drv_rx_counter,
		     status->tx_results_counter);

	/* update number of available TX blocks */
	for (i = 0; i < NUM_TX_QUEUES; i++) {
		freed_blocks += le32_to_cpu(status->tx_released_blks[i]) -
				wl->tx_blocks_freed[i];

		wl->tx_blocks_freed[i] =
			le32_to_cpu(status->tx_released_blks[i]);
	}

	wl->tx_allocated_blocks -= freed_blocks;

	if (wl->bss_type == BSS_TYPE_AP_BSS) {
		/* Update num of allocated TX blocks per link and ps status */
		wl1271_irq_update_links_status(wl, &full_status->ap);
		wl->tx_blocks_available += freed_blocks;
	} else {
		int avail = full_status->sta.tx_total - wl->tx_allocated_blocks;

		/*
		 * The FW might change the total number of TX memblocks before
		 * we get a notification about blocks being released. Thus, the
		 * available blocks calculation might yield a temporary result
		 * which is lower than the actual available blocks. Keeping in
		 * mind that only blocks that were allocated can be moved from
		 * TX to RX, tx_blocks_available should never decrease here.
		 */
		wl->tx_blocks_available = max((int)wl->tx_blocks_available,
					      avail);
	}

	/* if more blocks are available now, tx work can be scheduled */
	if (wl->tx_blocks_available > old_tx_blk_count)
		clear_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags);

	/* update the host-chipset time offset */
	getnstimeofday(&ts);
	wl->time_offset = (timespec_to_ns(&ts) >> 10) -
		(s64)le32_to_cpu(status->fw_localtime);
}

static void wl1271_flush_deferred_work(struct wl1271 *wl)
{
	struct sk_buff *skb;

	/* Pass all received frames to the network stack */
	while ((skb = skb_dequeue(&wl->deferred_rx_queue)))
		ieee80211_rx_ni(wl->hw, skb);

	/* Return sent skbs to the network stack */
	while ((skb = skb_dequeue(&wl->deferred_tx_queue)))
		ieee80211_tx_status(wl->hw, skb);
}

static void wl1271_netstack_work(struct work_struct *work)
{
	struct wl1271 *wl =
		container_of(work, struct wl1271, netstack_work);

	do {
		wl1271_flush_deferred_work(wl);
	} while (skb_queue_len(&wl->deferred_rx_queue));
}

#define WL1271_IRQ_MAX_LOOPS 256

irqreturn_t wl1271_irq(int irq, void *cookie)
{
	int ret;
	u32 intr;
	int loopcount = WL1271_IRQ_MAX_LOOPS;
	struct wl1271 *wl = (struct wl1271 *)cookie;
	bool done = false;
	unsigned int defer_count;
	unsigned long flags;

	/* TX might be handled here, avoid redundant work */
	set_bit(WL1271_FLAG_TX_PENDING, &wl->flags);
	cancel_work_sync(&wl->tx_work);

	/*
	 * In case edge triggered interrupt must be used, we cannot iterate
	 * more than once without introducing race conditions with the hardirq.
	 */
	if (wl->platform_quirks & WL12XX_PLATFORM_QUIRK_EDGE_IRQ)
		loopcount = 1;

	mutex_lock(&wl->mutex);

	wl1271_debug(DEBUG_IRQ, "IRQ work");

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	while (!done && loopcount--) {
		/*
		 * In order to avoid a race with the hardirq, clear the flag
		 * before acknowledging the chip. Since the mutex is held,
		 * wl1271_ps_elp_wakeup cannot be called concurrently.
		 */
		clear_bit(WL1271_FLAG_IRQ_RUNNING, &wl->flags);
		smp_mb__after_clear_bit();

		wl1271_fw_status(wl, wl->fw_status);
		intr = le32_to_cpu(wl->fw_status->common.intr);
		intr &= WL1271_INTR_MASK;
		if (!intr) {
			done = true;
			continue;
		}

		if (unlikely(intr & WL1271_ACX_INTR_WATCHDOG)) {
			wl1271_error("watchdog interrupt received! "
				     "starting recovery.");
			ieee80211_queue_work(wl->hw, &wl->recovery_work);

			/* restarting the chip. ignore any other interrupt. */
			goto out;
		}

		if (likely(intr & WL1271_ACX_INTR_DATA)) {
			wl1271_debug(DEBUG_IRQ, "WL1271_ACX_INTR_DATA");

			wl1271_rx(wl, &wl->fw_status->common);

			/* Check if any tx blocks were freed */
			spin_lock_irqsave(&wl->wl_lock, flags);
			if (!test_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags) &&
			    wl->tx_queue_count) {
				spin_unlock_irqrestore(&wl->wl_lock, flags);
				/*
				 * In order to avoid starvation of the TX path,
				 * call the work function directly.
				 */
				wl1271_tx_work_locked(wl);
			} else {
				spin_unlock_irqrestore(&wl->wl_lock, flags);
			}

			/* check for tx results */
			if (wl->fw_status->common.tx_results_counter !=
			    (wl->tx_results_count & 0xff))
				wl1271_tx_complete(wl);

			/* Make sure the deferred queues don't get too long */
			defer_count = skb_queue_len(&wl->deferred_tx_queue) +
				      skb_queue_len(&wl->deferred_rx_queue);
			if (defer_count > WL1271_DEFERRED_QUEUE_LIMIT)
				wl1271_flush_deferred_work(wl);
		}

		if (intr & WL1271_ACX_INTR_EVENT_A) {
			wl1271_debug(DEBUG_IRQ, "WL1271_ACX_INTR_EVENT_A");
			wl1271_event_handle(wl, 0);
		}

		if (intr & WL1271_ACX_INTR_EVENT_B) {
			wl1271_debug(DEBUG_IRQ, "WL1271_ACX_INTR_EVENT_B");
			wl1271_event_handle(wl, 1);
		}

		if (intr & WL1271_ACX_INTR_INIT_COMPLETE)
			wl1271_debug(DEBUG_IRQ,
				     "WL1271_ACX_INTR_INIT_COMPLETE");

		if (intr & WL1271_ACX_INTR_HW_AVAILABLE)
			wl1271_debug(DEBUG_IRQ, "WL1271_ACX_INTR_HW_AVAILABLE");
	}

	wl1271_ps_elp_sleep(wl);

out:
	spin_lock_irqsave(&wl->wl_lock, flags);
	/* In case TX was not handled here, queue TX work */
	clear_bit(WL1271_FLAG_TX_PENDING, &wl->flags);
	if (!test_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags) &&
	    wl->tx_queue_count)
		ieee80211_queue_work(wl->hw, &wl->tx_work);
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	mutex_unlock(&wl->mutex);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(wl1271_irq);

static int wl1271_fetch_firmware(struct wl1271 *wl)
{
	const struct firmware *fw;
	const char *fw_name;
	int ret;

	switch (wl->bss_type) {
	case BSS_TYPE_AP_BSS:
		if (wl->chip.id == CHIP_ID_1283_PG20)
			fw_name = WL128X_AP_FW_NAME;
		else
			fw_name = WL127X_AP_FW_NAME;
		break;
	case BSS_TYPE_IBSS:
	case BSS_TYPE_STA_BSS:
		if (wl->chip.id == CHIP_ID_1283_PG20)
			fw_name = WL128X_FW_NAME;
		else
			fw_name	= WL1271_FW_NAME;
		break;
	default:
		wl1271_error("no compatible firmware for bss_type %d",
			     wl->bss_type);
		return -EINVAL;
	}

	wl1271_debug(DEBUG_BOOT, "booting firmware %s", fw_name);

	ret = request_firmware(&fw, fw_name, wl1271_wl_to_dev(wl));

	if (ret < 0) {
		wl1271_error("could not get firmware: %d", ret);
		return ret;
	}

	if (fw->size % 4) {
		wl1271_error("firmware size is not multiple of 32 bits: %zu",
			     fw->size);
		ret = -EILSEQ;
		goto out;
	}

	vfree(wl->fw);
	wl->fw_len = fw->size;
	wl->fw = vmalloc(wl->fw_len);

	if (!wl->fw) {
		wl1271_error("could not allocate memory for the firmware");
		ret = -ENOMEM;
		goto out;
	}

	memcpy(wl->fw, fw->data, wl->fw_len);
	wl->fw_bss_type = wl->bss_type;
	ret = 0;

out:
	release_firmware(fw);

	return ret;
}

static int wl1271_fetch_nvs(struct wl1271 *wl)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, WL12XX_NVS_NAME, wl1271_wl_to_dev(wl));

	if (ret < 0) {
		wl1271_error("could not get nvs file: %d", ret);
		return ret;
	}

	wl->nvs = kmemdup(fw->data, fw->size, GFP_KERNEL);

	if (!wl->nvs) {
		wl1271_error("could not allocate memory for the nvs file");
		ret = -ENOMEM;
		goto out;
	}

	wl->nvs_len = fw->size;

out:
	release_firmware(fw);

	return ret;
}

static void wl1271_recovery_work(struct work_struct *work)
{
	struct wl1271 *wl =
		container_of(work, struct wl1271, recovery_work);

	mutex_lock(&wl->mutex);

	if (wl->state != WL1271_STATE_ON)
		goto out;

	wl1271_info("Hardware recovery in progress. FW ver: %s pc: 0x%x",
		    wl->chip.fw_ver_str, wl1271_read32(wl, SCR_PAD4));

	if (test_bit(WL1271_FLAG_STA_ASSOCIATED, &wl->flags))
		ieee80211_connection_loss(wl->vif);

	/* Prevent spurious TX during FW restart */
	ieee80211_stop_queues(wl->hw);

	if (wl->sched_scanning) {
		ieee80211_sched_scan_stopped(wl->hw);
		wl->sched_scanning = false;
	}

	/* reboot the chipset */
	__wl1271_op_remove_interface(wl, false);
	ieee80211_restart_hw(wl->hw);

	/*
	 * Its safe to enable TX now - the queues are stopped after a request
	 * to restart the HW.
	 */
	ieee80211_wake_queues(wl->hw);

out:
	mutex_unlock(&wl->mutex);
}

static void wl1271_fw_wakeup(struct wl1271 *wl)
{
	u32 elp_reg;

	elp_reg = ELPCTRL_WAKE_UP;
	wl1271_raw_write32(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, elp_reg);
}

static int wl1271_setup(struct wl1271 *wl)
{
	wl->fw_status = kmalloc(sizeof(*wl->fw_status), GFP_KERNEL);
	if (!wl->fw_status)
		return -ENOMEM;

	wl->tx_res_if = kmalloc(sizeof(*wl->tx_res_if), GFP_KERNEL);
	if (!wl->tx_res_if) {
		kfree(wl->fw_status);
		return -ENOMEM;
	}

	return 0;
}

static int wl1271_chip_wakeup(struct wl1271 *wl)
{
	struct wl1271_partition_set partition;
	int ret = 0;

	msleep(WL1271_PRE_POWER_ON_SLEEP);
	ret = wl1271_power_on(wl);
	if (ret < 0)
		goto out;
	msleep(WL1271_POWER_ON_SLEEP);
	wl1271_io_reset(wl);
	wl1271_io_init(wl);

	/* We don't need a real memory partition here, because we only want
	 * to use the registers at this point. */
	memset(&partition, 0, sizeof(partition));
	partition.reg.start = REGISTERS_BASE;
	partition.reg.size = REGISTERS_DOWN_SIZE;
	wl1271_set_partition(wl, &partition);

	/* ELP module wake up */
	wl1271_fw_wakeup(wl);

	/* whal_FwCtrl_BootSm() */

	/* 0. read chip id from CHIP_ID */
	wl->chip.id = wl1271_read32(wl, CHIP_ID_B);

	/* 1. check if chip id is valid */

	switch (wl->chip.id) {
	case CHIP_ID_1271_PG10:
		wl1271_warning("chip id 0x%x (1271 PG10) support is obsolete",
			       wl->chip.id);

		ret = wl1271_setup(wl);
		if (ret < 0)
			goto out;
		break;
	case CHIP_ID_1271_PG20:
		wl1271_debug(DEBUG_BOOT, "chip id 0x%x (1271 PG20)",
			     wl->chip.id);

		/* end-of-transaction flag should be set in wl127x AP mode */
		if (wl->bss_type == BSS_TYPE_AP_BSS)
			wl->quirks |= WL12XX_QUIRK_END_OF_TRANSACTION;

		ret = wl1271_setup(wl);
		if (ret < 0)
			goto out;
		break;
	case CHIP_ID_1283_PG20:
		wl1271_debug(DEBUG_BOOT, "chip id 0x%x (1283 PG20)",
			     wl->chip.id);

		ret = wl1271_setup(wl);
		if (ret < 0)
			goto out;
		if (wl1271_set_block_size(wl))
			wl->quirks |= WL12XX_QUIRK_BLOCKSIZE_ALIGNMENT;
		break;
	case CHIP_ID_1283_PG10:
	default:
		wl1271_warning("unsupported chip id: 0x%x", wl->chip.id);
		ret = -ENODEV;
		goto out;
	}

	/* Make sure the firmware type matches the BSS type */
	if (wl->fw == NULL || wl->fw_bss_type != wl->bss_type) {
		ret = wl1271_fetch_firmware(wl);
		if (ret < 0)
			goto out;
	}

	/* No NVS from netlink, try to get it from the filesystem */
	if (wl->nvs == NULL) {
		ret = wl1271_fetch_nvs(wl);
		if (ret < 0)
			goto out;
	}

out:
	return ret;
}

static unsigned int wl1271_get_fw_ver_quirks(struct wl1271 *wl)
{
	unsigned int quirks = 0;
	unsigned int *fw_ver = wl->chip.fw_ver;

	/* Only for wl127x */
	if ((fw_ver[FW_VER_CHIP] == FW_VER_CHIP_WL127X) &&
	    /* Check STA version */
	    (((fw_ver[FW_VER_IF_TYPE] == FW_VER_IF_TYPE_STA) &&
	      (fw_ver[FW_VER_MINOR] < FW_VER_MINOR_1_SPARE_STA_MIN)) ||
	     /* Check AP version */
	     ((fw_ver[FW_VER_IF_TYPE] == FW_VER_IF_TYPE_AP) &&
	      (fw_ver[FW_VER_MINOR] < FW_VER_MINOR_1_SPARE_AP_MIN))))
		quirks |= WL12XX_QUIRK_USE_2_SPARE_BLOCKS;

	return quirks;
}

int wl1271_plt_start(struct wl1271 *wl)
{
	int retries = WL1271_BOOT_RETRIES;
	int ret;

	mutex_lock(&wl->mutex);

	wl1271_notice("power up");

	if (wl->state != WL1271_STATE_OFF) {
		wl1271_error("cannot go into PLT state because not "
			     "in off state: %d", wl->state);
		ret = -EBUSY;
		goto out;
	}

	wl->bss_type = BSS_TYPE_STA_BSS;

	while (retries) {
		retries--;
		ret = wl1271_chip_wakeup(wl);
		if (ret < 0)
			goto power_off;

		ret = wl1271_boot(wl);
		if (ret < 0)
			goto power_off;

		ret = wl1271_plt_init(wl);
		if (ret < 0)
			goto irq_disable;

		wl->state = WL1271_STATE_PLT;
		wl1271_notice("firmware booted in PLT mode (%s)",
			      wl->chip.fw_ver_str);

		/* Check if any quirks are needed with older fw versions */
		wl->quirks |= wl1271_get_fw_ver_quirks(wl);
		goto out;

irq_disable:
		mutex_unlock(&wl->mutex);
		/* Unlocking the mutex in the middle of handling is
		   inherently unsafe. In this case we deem it safe to do,
		   because we need to let any possibly pending IRQ out of
		   the system (and while we are WL1271_STATE_OFF the IRQ
		   work function will not do anything.) Also, any other
		   possible concurrent operations will fail due to the
		   current state, hence the wl1271 struct should be safe. */
		wl1271_disable_interrupts(wl);
		wl1271_flush_deferred_work(wl);
		cancel_work_sync(&wl->netstack_work);
		mutex_lock(&wl->mutex);
power_off:
		wl1271_power_off(wl);
	}

	wl1271_error("firmware boot in PLT mode failed despite %d retries",
		     WL1271_BOOT_RETRIES);
out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static int __wl1271_plt_stop(struct wl1271 *wl)
{
	int ret = 0;

	wl1271_notice("power down");

	if (wl->state != WL1271_STATE_PLT) {
		wl1271_error("cannot power down because not in PLT "
			     "state: %d", wl->state);
		ret = -EBUSY;
		goto out;
	}

	wl1271_power_off(wl);

	wl->state = WL1271_STATE_OFF;
	wl->rx_counter = 0;

	mutex_unlock(&wl->mutex);
	wl1271_disable_interrupts(wl);
	wl1271_flush_deferred_work(wl);
	cancel_work_sync(&wl->netstack_work);
	cancel_work_sync(&wl->recovery_work);
	mutex_lock(&wl->mutex);
out:
	return ret;
}

int wl1271_plt_stop(struct wl1271 *wl)
{
	int ret;

	mutex_lock(&wl->mutex);
	ret = __wl1271_plt_stop(wl);
	mutex_unlock(&wl->mutex);
	return ret;
}

static void wl1271_op_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct wl1271 *wl = hw->priv;
	unsigned long flags;
	int q;
	u8 hlid = 0;

	q = wl1271_tx_get_queue(skb_get_queue_mapping(skb));

	if (wl->bss_type == BSS_TYPE_AP_BSS)
		hlid = wl1271_tx_get_hlid(skb);

	spin_lock_irqsave(&wl->wl_lock, flags);

	wl->tx_queue_count++;

	/*
	 * The workqueue is slow to process the tx_queue and we need stop
	 * the queue here, otherwise the queue will get too long.
	 */
	if (wl->tx_queue_count >= WL1271_TX_QUEUE_HIGH_WATERMARK) {
		wl1271_debug(DEBUG_TX, "op_tx: stopping queues");
		ieee80211_stop_queues(wl->hw);
		set_bit(WL1271_FLAG_TX_QUEUE_STOPPED, &wl->flags);
	}

	/* queue the packet */
	if (wl->bss_type == BSS_TYPE_AP_BSS) {
		wl1271_debug(DEBUG_TX, "queue skb hlid %d q %d", hlid, q);
		skb_queue_tail(&wl->links[hlid].tx_queue[q], skb);
	} else {
		skb_queue_tail(&wl->tx_queue[q], skb);
	}

	/*
	 * The chip specific setup must run before the first TX packet -
	 * before that, the tx_work will not be initialized!
	 */

	if (!test_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags) &&
	    !test_bit(WL1271_FLAG_TX_PENDING, &wl->flags))
		ieee80211_queue_work(wl->hw, &wl->tx_work);

	spin_unlock_irqrestore(&wl->wl_lock, flags);
}

int wl1271_tx_dummy_packet(struct wl1271 *wl)
{
	unsigned long flags;

	spin_lock_irqsave(&wl->wl_lock, flags);
	set_bit(WL1271_FLAG_DUMMY_PACKET_PENDING, &wl->flags);
	wl->tx_queue_count++;
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	/* The FW is low on RX memory blocks, so send the dummy packet asap */
	if (!test_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags))
		wl1271_tx_work_locked(wl);

	/*
	 * If the FW TX is busy, TX work will be scheduled by the threaded
	 * interrupt handler function
	 */
	return 0;
}

/*
 * The size of the dummy packet should be at least 1400 bytes. However, in
 * order to minimize the number of bus transactions, aligning it to 512 bytes
 * boundaries could be beneficial, performance wise
 */
#define TOTAL_TX_DUMMY_PACKET_SIZE (ALIGN(1400, 512))

static struct sk_buff *wl12xx_alloc_dummy_packet(struct wl1271 *wl)
{
	struct sk_buff *skb;
	struct ieee80211_hdr_3addr *hdr;
	unsigned int dummy_packet_size;

	dummy_packet_size = TOTAL_TX_DUMMY_PACKET_SIZE -
			    sizeof(struct wl1271_tx_hw_descr) - sizeof(*hdr);

	skb = dev_alloc_skb(TOTAL_TX_DUMMY_PACKET_SIZE);
	if (!skb) {
		wl1271_warning("Failed to allocate a dummy packet skb");
		return NULL;
	}

	skb_reserve(skb, sizeof(struct wl1271_tx_hw_descr));

	hdr = (struct ieee80211_hdr_3addr *) skb_put(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					 IEEE80211_STYPE_NULLFUNC |
					 IEEE80211_FCTL_TODS);

	memset(skb_put(skb, dummy_packet_size), 0, dummy_packet_size);

	/* Dummy packets require the TID to be management */
	skb->priority = WL1271_TID_MGMT;

	/* Initialize all fields that might be used */
	skb_set_queue_mapping(skb, 0);
	memset(IEEE80211_SKB_CB(skb), 0, sizeof(struct ieee80211_tx_info));

	return skb;
}


static struct notifier_block wl1271_dev_notifier = {
	.notifier_call = wl1271_dev_notify,
};

#ifdef CONFIG_PM
static int wl1271_configure_suspend(struct wl1271 *wl)
{
	int ret;

	if (wl->bss_type != BSS_TYPE_STA_BSS)
		return 0;

	mutex_lock(&wl->mutex);

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out_unlock;

	/* enter psm if needed*/
	if (!test_bit(WL1271_FLAG_PSM, &wl->flags)) {
		DECLARE_COMPLETION_ONSTACK(compl);

		wl->ps_compl = &compl;
		ret = wl1271_ps_set_mode(wl, STATION_POWER_SAVE_MODE,
				   wl->basic_rate, true);
		if (ret < 0)
			goto out_sleep;

		/* we must unlock here so we will be able to get events */
		wl1271_ps_elp_sleep(wl);
		mutex_unlock(&wl->mutex);

		ret = wait_for_completion_timeout(
			&compl, msecs_to_jiffies(WL1271_PS_COMPLETE_TIMEOUT));
		if (ret <= 0) {
			wl1271_warning("couldn't enter ps mode!");
			ret = -EBUSY;
			goto out;
		}

		/* take mutex again, and wakeup */
		mutex_lock(&wl->mutex);

		ret = wl1271_ps_elp_wakeup(wl);
		if (ret < 0)
			goto out_unlock;
	}
out_sleep:
	wl1271_ps_elp_sleep(wl);
out_unlock:
	mutex_unlock(&wl->mutex);
out:
	return ret;

}

static void wl1271_configure_resume(struct wl1271 *wl)
{
	int ret;

	if (wl->bss_type != BSS_TYPE_STA_BSS)
		return;

	mutex_lock(&wl->mutex);
	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	/* exit psm if it wasn't configured */
	if (!test_bit(WL1271_FLAG_PSM_REQUESTED, &wl->flags))
		wl1271_ps_set_mode(wl, STATION_ACTIVE_MODE,
				   wl->basic_rate, true);

	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);
}

static int wl1271_op_suspend(struct ieee80211_hw *hw,
			    struct cfg80211_wowlan *wow)
{
	struct wl1271 *wl = hw->priv;
	wl1271_debug(DEBUG_MAC80211, "mac80211 suspend wow=%d", !!wow);
	wl->wow_enabled = !!wow;
	if (wl->wow_enabled) {
		int ret;
		ret = wl1271_configure_suspend(wl);
		if (ret < 0) {
			wl1271_warning("couldn't prepare device to suspend");
			return ret;
		}
		/* flush any remaining work */
		wl1271_debug(DEBUG_MAC80211, "flushing remaining works");
		flush_delayed_work(&wl->scan_complete_work);

		/*
		 * disable and re-enable interrupts in order to flush
		 * the threaded_irq
		 */
		wl1271_disable_interrupts(wl);

		/*
		 * set suspended flag to avoid triggering a new threaded_irq
		 * work. no need for spinlock as interrupts are disabled.
		 */
		set_bit(WL1271_FLAG_SUSPENDED, &wl->flags);

		wl1271_enable_interrupts(wl);
		flush_work(&wl->tx_work);
		flush_delayed_work(&wl->pspoll_work);
		flush_delayed_work(&wl->elp_work);
	}
	return 0;
}

static int wl1271_op_resume(struct ieee80211_hw *hw)
{
	struct wl1271 *wl = hw->priv;
	wl1271_debug(DEBUG_MAC80211, "mac80211 resume wow=%d",
		     wl->wow_enabled);

	/*
	 * re-enable irq_work enqueuing, and call irq_work directly if
	 * there is a pending work.
	 */
	if (wl->wow_enabled) {
		struct wl1271 *wl = hw->priv;
		unsigned long flags;
		bool run_irq_work = false;

		spin_lock_irqsave(&wl->wl_lock, flags);
		clear_bit(WL1271_FLAG_SUSPENDED, &wl->flags);
		if (test_and_clear_bit(WL1271_FLAG_PENDING_WORK, &wl->flags))
			run_irq_work = true;
		spin_unlock_irqrestore(&wl->wl_lock, flags);

		if (run_irq_work) {
			wl1271_debug(DEBUG_MAC80211,
				     "run postponed irq_work directly");
			wl1271_irq(0, wl);
			wl1271_enable_interrupts(wl);
		}

		wl1271_configure_resume(wl);
	}

	return 0;
}
#endif

static int wl1271_op_start(struct ieee80211_hw *hw)
{
	wl1271_debug(DEBUG_MAC80211, "mac80211 start");

	/*
	 * We have to delay the booting of the hardware because
	 * we need to know the local MAC address before downloading and
	 * initializing the firmware. The MAC address cannot be changed
	 * after boot, and without the proper MAC address, the firmware
	 * will not function properly.
	 *
	 * The MAC address is first known when the corresponding interface
	 * is added. That is where we will initialize the hardware.
	 *
	 * In addition, we currently have different firmwares for AP and managed
	 * operation. We will know which to boot according to interface type.
	 */

	return 0;
}

static void wl1271_op_stop(struct ieee80211_hw *hw)
{
	wl1271_debug(DEBUG_MAC80211, "mac80211 stop");
}

static int wl1271_op_add_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct wl1271 *wl = hw->priv;
	struct wiphy *wiphy = hw->wiphy;
	int retries = WL1271_BOOT_RETRIES;
	int ret = 0;
	bool booted = false;

	wl1271_debug(DEBUG_MAC80211, "mac80211 add interface type %d mac %pM",
		     vif->type, vif->addr);

	mutex_lock(&wl->mutex);
	if (wl->vif) {
		wl1271_debug(DEBUG_MAC80211,
			     "multiple vifs are not supported yet");
		ret = -EBUSY;
		goto out;
	}

	/*
	 * in some very corner case HW recovery scenarios its possible to
	 * get here before __wl1271_op_remove_interface is complete, so
	 * opt out if that is the case.
	 */
	if (test_bit(WL1271_FLAG_IF_INITIALIZED, &wl->flags)) {
		ret = -EBUSY;
		goto out;
	}

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		wl->bss_type = BSS_TYPE_STA_BSS;
		wl->set_bss_type = BSS_TYPE_STA_BSS;
		break;
	case NL80211_IFTYPE_ADHOC:
		wl->bss_type = BSS_TYPE_IBSS;
		wl->set_bss_type = BSS_TYPE_STA_BSS;
		break;
	case NL80211_IFTYPE_AP:
		wl->bss_type = BSS_TYPE_AP_BSS;
		break;
	default:
		ret = -EOPNOTSUPP;
		goto out;
	}

	memcpy(wl->mac_addr, vif->addr, ETH_ALEN);

	if (wl->state != WL1271_STATE_OFF) {
		wl1271_error("cannot start because not in off state: %d",
			     wl->state);
		ret = -EBUSY;
		goto out;
	}

	while (retries) {
		retries--;
		ret = wl1271_chip_wakeup(wl);
		if (ret < 0)
			goto power_off;

		ret = wl1271_boot(wl);
		if (ret < 0)
			goto power_off;

		ret = wl1271_hw_init(wl);
		if (ret < 0)
			goto irq_disable;

		booted = true;
		break;

irq_disable:
		mutex_unlock(&wl->mutex);
		/* Unlocking the mutex in the middle of handling is
		   inherently unsafe. In this case we deem it safe to do,
		   because we need to let any possibly pending IRQ out of
		   the system (and while we are WL1271_STATE_OFF the IRQ
		   work function will not do anything.) Also, any other
		   possible concurrent operations will fail due to the
		   current state, hence the wl1271 struct should be safe. */
		wl1271_disable_interrupts(wl);
		wl1271_flush_deferred_work(wl);
		cancel_work_sync(&wl->netstack_work);
		mutex_lock(&wl->mutex);
power_off:
		wl1271_power_off(wl);
	}

	if (!booted) {
		wl1271_error("firmware boot failed despite %d retries",
			     WL1271_BOOT_RETRIES);
		goto out;
	}

	wl->vif = vif;
	wl->state = WL1271_STATE_ON;
	set_bit(WL1271_FLAG_IF_INITIALIZED, &wl->flags);
	wl1271_info("firmware booted (%s)", wl->chip.fw_ver_str);

	/* update hw/fw version info in wiphy struct */
	wiphy->hw_version = wl->chip.id;
	strncpy(wiphy->fw_version, wl->chip.fw_ver_str,
		sizeof(wiphy->fw_version));

	/* Check if any quirks are needed with older fw versions */
	wl->quirks |= wl1271_get_fw_ver_quirks(wl);

	/*
	 * Now we know if 11a is supported (info from the NVS), so disable
	 * 11a channels if not supported
	 */
	if (!wl->enable_11a)
		wiphy->bands[IEEE80211_BAND_5GHZ]->n_channels = 0;

	wl1271_debug(DEBUG_MAC80211, "11a is %ssupported",
		     wl->enable_11a ? "" : "not ");

out:
	mutex_unlock(&wl->mutex);

	mutex_lock(&wl_list_mutex);
	if (!ret)
		list_add(&wl->list, &wl_list);
	mutex_unlock(&wl_list_mutex);

	return ret;
}

static void __wl1271_op_remove_interface(struct wl1271 *wl,
					 bool reset_tx_queues)
{
	int i;

	wl1271_debug(DEBUG_MAC80211, "mac80211 remove interface");

	/* because of hardware recovery, we may get here twice */
	if (wl->state != WL1271_STATE_ON)
		return;

	wl1271_info("down");

	mutex_lock(&wl_list_mutex);
	list_del(&wl->list);
	mutex_unlock(&wl_list_mutex);

	/* enable dyn ps just in case (if left on due to fw crash etc) */
	if (wl->bss_type == BSS_TYPE_STA_BSS)
		ieee80211_enable_dyn_ps(wl->vif);

	if (wl->scan.state != WL1271_SCAN_STATE_IDLE) {
		wl->scan.state = WL1271_SCAN_STATE_IDLE;
		memset(wl->scan.scanned_ch, 0, sizeof(wl->scan.scanned_ch));
		wl->scan.req = NULL;
		ieee80211_scan_completed(wl->hw, true);
	}

	/*
	 * this must be before the cancel_work calls below, so that the work
	 * functions don't perform further work.
	 */
	wl->state = WL1271_STATE_OFF;

	mutex_unlock(&wl->mutex);

	wl1271_disable_interrupts(wl);
	wl1271_flush_deferred_work(wl);
	cancel_delayed_work_sync(&wl->scan_complete_work);
	cancel_work_sync(&wl->netstack_work);
	cancel_work_sync(&wl->tx_work);
	cancel_delayed_work_sync(&wl->pspoll_work);
	cancel_delayed_work_sync(&wl->elp_work);

	mutex_lock(&wl->mutex);

	/* let's notify MAC80211 about the remaining pending TX frames */
	wl1271_tx_reset(wl, reset_tx_queues);
	wl1271_power_off(wl);

	memset(wl->bssid, 0, ETH_ALEN);
	memset(wl->ssid, 0, IW_ESSID_MAX_SIZE + 1);
	wl->ssid_len = 0;
	wl->bss_type = MAX_BSS_TYPE;
	wl->set_bss_type = MAX_BSS_TYPE;
	wl->band = IEEE80211_BAND_2GHZ;

	wl->rx_counter = 0;
	wl->psm_entry_retry = 0;
	wl->power_level = WL1271_DEFAULT_POWER_LEVEL;
	wl->tx_blocks_available = 0;
	wl->tx_allocated_blocks = 0;
	wl->tx_results_count = 0;
	wl->tx_packets_count = 0;
	wl->tx_security_last_seq = 0;
	wl->tx_security_seq = 0;
	wl->time_offset = 0;
	wl->session_counter = 0;
	wl->rate_set = CONF_TX_RATE_MASK_BASIC;
	wl->vif = NULL;
	wl->filters = 0;
	wl1271_free_ap_keys(wl);
	memset(wl->ap_hlid_map, 0, sizeof(wl->ap_hlid_map));
	wl->ap_fw_ps_map = 0;
	wl->ap_ps_map = 0;
	wl->sched_scanning = false;

	/*
	 * this is performed after the cancel_work calls and the associated
	 * mutex_lock, so that wl1271_op_add_interface does not accidentally
	 * get executed before all these vars have been reset.
	 */
	wl->flags = 0;

	for (i = 0; i < NUM_TX_QUEUES; i++)
		wl->tx_blocks_freed[i] = 0;

	wl1271_debugfs_reset(wl);

	kfree(wl->fw_status);
	wl->fw_status = NULL;
	kfree(wl->tx_res_if);
	wl->tx_res_if = NULL;
	kfree(wl->target_mem_map);
	wl->target_mem_map = NULL;
}

static void wl1271_op_remove_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct wl1271 *wl = hw->priv;

	mutex_lock(&wl->mutex);
	/*
	 * wl->vif can be null here if someone shuts down the interface
	 * just when hardware recovery has been started.
	 */
	if (wl->vif) {
		WARN_ON(wl->vif != vif);
		__wl1271_op_remove_interface(wl, true);
	}

	mutex_unlock(&wl->mutex);
	cancel_work_sync(&wl->recovery_work);
}

void wl1271_configure_filters(struct wl1271 *wl, unsigned int filters)
{
	wl1271_set_default_filters(wl);

	/* combine requested filters with current filter config */
	filters = wl->filters | filters;

	wl1271_debug(DEBUG_FILTERS, "RX filters set: ");

	if (filters & FIF_PROMISC_IN_BSS) {
		wl1271_debug(DEBUG_FILTERS, " - FIF_PROMISC_IN_BSS");
		wl->rx_config &= ~CFG_UNI_FILTER_EN;
		wl->rx_config |= CFG_BSSID_FILTER_EN;
	}
	if (filters & FIF_BCN_PRBRESP_PROMISC) {
		wl1271_debug(DEBUG_FILTERS, " - FIF_BCN_PRBRESP_PROMISC");
		wl->rx_config &= ~CFG_BSSID_FILTER_EN;
		wl->rx_config &= ~CFG_SSID_FILTER_EN;
	}
	if (filters & FIF_OTHER_BSS) {
		wl1271_debug(DEBUG_FILTERS, " - FIF_OTHER_BSS");
		wl->rx_config &= ~CFG_BSSID_FILTER_EN;
	}
	if (filters & FIF_CONTROL) {
		wl1271_debug(DEBUG_FILTERS, " - FIF_CONTROL");
		wl->rx_filter |= CFG_RX_CTL_EN;
	}
	if (filters & FIF_FCSFAIL) {
		wl1271_debug(DEBUG_FILTERS, " - FIF_FCSFAIL");
		wl->rx_filter |= CFG_RX_FCS_ERROR;
	}
}

static int wl1271_dummy_join(struct wl1271 *wl)
{
	int ret = 0;
	/* we need to use a dummy BSSID for now */
	static const u8 dummy_bssid[ETH_ALEN] = { 0x0b, 0xad, 0xde,
						  0xad, 0xbe, 0xef };

	memcpy(wl->bssid, dummy_bssid, ETH_ALEN);

	/* pass through frames from all BSS */
	wl1271_configure_filters(wl, FIF_OTHER_BSS);

	ret = wl1271_cmd_join(wl, wl->set_bss_type);
	if (ret < 0)
		goto out;

	set_bit(WL1271_FLAG_JOINED, &wl->flags);

out:
	return ret;
}

static int wl1271_join(struct wl1271 *wl, bool set_assoc)
{
	int ret;

	/*
	 * One of the side effects of the JOIN command is that is clears
	 * WPA/WPA2 keys from the chipset. Performing a JOIN while associated
	 * to a WPA/WPA2 access point will therefore kill the data-path.
	 * Currently the only valid scenario for JOIN during association
	 * is on roaming, in which case we will also be given new keys.
	 * Keep the below message for now, unless it starts bothering
	 * users who really like to roam a lot :)
	 */
	if (test_bit(WL1271_FLAG_STA_ASSOCIATED, &wl->flags))
		wl1271_info("JOIN while associated.");

	if (set_assoc)
		set_bit(WL1271_FLAG_STA_ASSOCIATED, &wl->flags);

	ret = wl1271_cmd_join(wl, wl->set_bss_type);
	if (ret < 0)
		goto out;

	set_bit(WL1271_FLAG_JOINED, &wl->flags);

	if (!test_bit(WL1271_FLAG_STA_ASSOCIATED, &wl->flags))
		goto out;

	/*
	 * The join command disable the keep-alive mode, shut down its process,
	 * and also clear the template config, so we need to reset it all after
	 * the join. The acx_aid starts the keep-alive process, and the order
	 * of the commands below is relevant.
	 */
	ret = wl1271_acx_keep_alive_mode(wl, true);
	if (ret < 0)
		goto out;

	ret = wl1271_acx_aid(wl, wl->aid);
	if (ret < 0)
		goto out;

	ret = wl1271_cmd_build_klv_null_data(wl);
	if (ret < 0)
		goto out;

	ret = wl1271_acx_keep_alive_config(wl, CMD_TEMPL_KLV_IDX_NULL_DATA,
					   ACX_KEEP_ALIVE_TPL_VALID);
	if (ret < 0)
		goto out;

out:
	return ret;
}

static int wl1271_unjoin(struct wl1271 *wl)
{
	int ret;

	/* to stop listening to a channel, we disconnect */
	ret = wl1271_cmd_disconnect(wl);
	if (ret < 0)
		goto out;

	clear_bit(WL1271_FLAG_JOINED, &wl->flags);
	memset(wl->bssid, 0, ETH_ALEN);

	/* stop filtering packets based on bssid */
	wl1271_configure_filters(wl, FIF_OTHER_BSS);

out:
	return ret;
}

static void wl1271_set_band_rate(struct wl1271 *wl)
{
	if (wl->band == IEEE80211_BAND_2GHZ)
		wl->basic_rate_set = wl->conf.tx.basic_rate;
	else
		wl->basic_rate_set = wl->conf.tx.basic_rate_5;
}

static int wl1271_sta_handle_idle(struct wl1271 *wl, bool idle)
{
	int ret;

	if (idle) {
		if (test_bit(WL1271_FLAG_JOINED, &wl->flags)) {
			ret = wl1271_unjoin(wl);
			if (ret < 0)
				goto out;
		}
		wl->rate_set = wl1271_tx_min_rate_get(wl);
		ret = wl1271_acx_sta_rate_policies(wl);
		if (ret < 0)
			goto out;
		ret = wl1271_acx_keep_alive_config(
			wl, CMD_TEMPL_KLV_IDX_NULL_DATA,
			ACX_KEEP_ALIVE_TPL_INVALID);
		if (ret < 0)
			goto out;
		set_bit(WL1271_FLAG_IDLE, &wl->flags);
	} else {
		/* increment the session counter */
		wl->session_counter++;
		if (wl->session_counter >= SESSION_COUNTER_MAX)
			wl->session_counter = 0;

		/* The current firmware only supports sched_scan in idle */
		if (wl->sched_scanning) {
			wl1271_scan_sched_scan_stop(wl);
			ieee80211_sched_scan_stopped(wl->hw);
		}

		ret = wl1271_dummy_join(wl);
		if (ret < 0)
			goto out;
		clear_bit(WL1271_FLAG_IDLE, &wl->flags);
	}

out:
	return ret;
}

static int wl1271_op_config(struct ieee80211_hw *hw, u32 changed)
{
	struct wl1271 *wl = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	int channel, ret = 0;
	bool is_ap;

	channel = ieee80211_frequency_to_channel(conf->channel->center_freq);

	wl1271_debug(DEBUG_MAC80211, "mac80211 config ch %d psm %s power %d %s"
		     " changed 0x%x",
		     channel,
		     conf->flags & IEEE80211_CONF_PS ? "on" : "off",
		     conf->power_level,
		     conf->flags & IEEE80211_CONF_IDLE ? "idle" : "in use",
			 changed);

	/*
	 * mac80211 will go to idle nearly immediately after transmitting some
	 * frames, such as the deauth. To make sure those frames reach the air,
	 * wait here until the TX queue is fully flushed.
	 */
	if ((changed & IEEE80211_CONF_CHANGE_IDLE) &&
	    (conf->flags & IEEE80211_CONF_IDLE))
		wl1271_tx_flush(wl);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF)) {
		/* we support configuring the channel and band while off */
		if ((changed & IEEE80211_CONF_CHANGE_CHANNEL)) {
			wl->band = conf->channel->band;
			wl->channel = channel;
		}

		goto out;
	}

	is_ap = (wl->bss_type == BSS_TYPE_AP_BSS);

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	/* if the channel changes while joined, join again */
	if (changed & IEEE80211_CONF_CHANGE_CHANNEL &&
	    ((wl->band != conf->channel->band) ||
	     (wl->channel != channel))) {
		wl->band = conf->channel->band;
		wl->channel = channel;

		if (!is_ap) {
			/*
			 * FIXME: the mac80211 should really provide a fixed
			 * rate to use here. for now, just use the smallest
			 * possible rate for the band as a fixed rate for
			 * association frames and other control messages.
			 */
			if (!test_bit(WL1271_FLAG_STA_ASSOCIATED, &wl->flags))
				wl1271_set_band_rate(wl);

			wl->basic_rate = wl1271_tx_min_rate_get(wl);
			ret = wl1271_acx_sta_rate_policies(wl);
			if (ret < 0)
				wl1271_warning("rate policy for channel "
					       "failed %d", ret);

			if (test_bit(WL1271_FLAG_JOINED, &wl->flags)) {
				ret = wl1271_join(wl, false);
				if (ret < 0)
					wl1271_warning("cmd join on channel "
						       "failed %d", ret);
			}
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_IDLE && !is_ap) {
		ret = wl1271_sta_handle_idle(wl,
					conf->flags & IEEE80211_CONF_IDLE);
		if (ret < 0)
			wl1271_warning("idle mode change failed %d", ret);
	}

	/*
	 * if mac80211 changes the PSM mode, make sure the mode is not
	 * incorrectly changed after the pspoll failure active window.
	 */
	if (changed & IEEE80211_CONF_CHANGE_PS)
		clear_bit(WL1271_FLAG_PSPOLL_FAILURE, &wl->flags);

	if (conf->flags & IEEE80211_CONF_PS &&
	    !test_bit(WL1271_FLAG_PSM_REQUESTED, &wl->flags)) {
		set_bit(WL1271_FLAG_PSM_REQUESTED, &wl->flags);

		/*
		 * We enter PSM only if we're already associated.
		 * If we're not, we'll enter it when joining an SSID,
		 * through the bss_info_changed() hook.
		 */
		if (test_bit(WL1271_FLAG_STA_ASSOCIATED, &wl->flags)) {
			wl1271_debug(DEBUG_PSM, "psm enabled");
			ret = wl1271_ps_set_mode(wl, STATION_POWER_SAVE_MODE,
						 wl->basic_rate, true);
		}
	} else if (!(conf->flags & IEEE80211_CONF_PS) &&
		   test_bit(WL1271_FLAG_PSM_REQUESTED, &wl->flags)) {
		wl1271_debug(DEBUG_PSM, "psm disabled");

		clear_bit(WL1271_FLAG_PSM_REQUESTED, &wl->flags);

		if (test_bit(WL1271_FLAG_PSM, &wl->flags))
			ret = wl1271_ps_set_mode(wl, STATION_ACTIVE_MODE,
						 wl->basic_rate, true);
	}

	if (conf->power_level != wl->power_level) {
		ret = wl1271_acx_tx_power(wl, conf->power_level);
		if (ret < 0)
			goto out_sleep;

		wl->power_level = conf->power_level;
	}

out_sleep:
	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

struct wl1271_filter_params {
	bool enabled;
	int mc_list_length;
	u8 mc_list[ACX_MC_ADDRESS_GROUP_MAX][ETH_ALEN];
};

static u64 wl1271_op_prepare_multicast(struct ieee80211_hw *hw,
				       struct netdev_hw_addr_list *mc_list)
{
	struct wl1271_filter_params *fp;
	struct netdev_hw_addr *ha;
	struct wl1271 *wl = hw->priv;

	if (unlikely(wl->state == WL1271_STATE_OFF))
		return 0;

	fp = kzalloc(sizeof(*fp), GFP_ATOMIC);
	if (!fp) {
		wl1271_error("Out of memory setting filters.");
		return 0;
	}

	/* update multicast filtering parameters */
	fp->mc_list_length = 0;
	if (netdev_hw_addr_list_count(mc_list) > ACX_MC_ADDRESS_GROUP_MAX) {
		fp->enabled = false;
	} else {
		fp->enabled = true;
		netdev_hw_addr_list_for_each(ha, mc_list) {
			memcpy(fp->mc_list[fp->mc_list_length],
					ha->addr, ETH_ALEN);
			fp->mc_list_length++;
		}
	}

	return (u64)(unsigned long)fp;
}

#define WL1271_SUPPORTED_FILTERS (FIF_PROMISC_IN_BSS | \
				  FIF_ALLMULTI | \
				  FIF_FCSFAIL | \
				  FIF_BCN_PRBRESP_PROMISC | \
				  FIF_CONTROL | \
				  FIF_OTHER_BSS)

static void wl1271_op_configure_filter(struct ieee80211_hw *hw,
				       unsigned int changed,
				       unsigned int *total, u64 multicast)
{
	struct wl1271_filter_params *fp = (void *)(unsigned long)multicast;
	struct wl1271 *wl = hw->priv;
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 configure filter changed %x"
		     " total %x", changed, *total);

	mutex_lock(&wl->mutex);

	*total &= WL1271_SUPPORTED_FILTERS;
	changed &= WL1271_SUPPORTED_FILTERS;

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	if (wl->bss_type != BSS_TYPE_AP_BSS) {
		if (*total & FIF_ALLMULTI)
			ret = wl1271_acx_group_address_tbl(wl, false, NULL, 0);
		else if (fp)
			ret = wl1271_acx_group_address_tbl(wl, fp->enabled,
							   fp->mc_list,
							   fp->mc_list_length);
		if (ret < 0)
			goto out_sleep;
	}

	/* determine, whether supported filter values have changed */
	if (changed == 0)
		goto out_sleep;

	/* configure filters */
	wl->filters = *total;
	wl1271_configure_filters(wl, 0);

	/* apply configured filters */
	ret = wl1271_acx_rx_config(wl, wl->rx_config, wl->rx_filter);
	if (ret < 0)
		goto out_sleep;

out_sleep:
	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);
	kfree(fp);
}

static int wl1271_record_ap_key(struct wl1271 *wl, u8 id, u8 key_type,
			u8 key_size, const u8 *key, u8 hlid, u32 tx_seq_32,
			u16 tx_seq_16)
{
	struct wl1271_ap_key *ap_key;
	int i;

	wl1271_debug(DEBUG_CRYPT, "record ap key id %d", (int)id);

	if (key_size > MAX_KEY_SIZE)
		return -EINVAL;

	/*
	 * Find next free entry in ap_keys. Also check we are not replacing
	 * an existing key.
	 */
	for (i = 0; i < MAX_NUM_KEYS; i++) {
		if (wl->recorded_ap_keys[i] == NULL)
			break;

		if (wl->recorded_ap_keys[i]->id == id) {
			wl1271_warning("trying to record key replacement");
			return -EINVAL;
		}
	}

	if (i == MAX_NUM_KEYS)
		return -EBUSY;

	ap_key = kzalloc(sizeof(*ap_key), GFP_KERNEL);
	if (!ap_key)
		return -ENOMEM;

	ap_key->id = id;
	ap_key->key_type = key_type;
	ap_key->key_size = key_size;
	memcpy(ap_key->key, key, key_size);
	ap_key->hlid = hlid;
	ap_key->tx_seq_32 = tx_seq_32;
	ap_key->tx_seq_16 = tx_seq_16;

	wl->recorded_ap_keys[i] = ap_key;
	return 0;
}

static void wl1271_free_ap_keys(struct wl1271 *wl)
{
	int i;

	for (i = 0; i < MAX_NUM_KEYS; i++) {
		kfree(wl->recorded_ap_keys[i]);
		wl->recorded_ap_keys[i] = NULL;
	}
}

static int wl1271_ap_init_hwenc(struct wl1271 *wl)
{
	int i, ret = 0;
	struct wl1271_ap_key *key;
	bool wep_key_added = false;

	for (i = 0; i < MAX_NUM_KEYS; i++) {
		if (wl->recorded_ap_keys[i] == NULL)
			break;

		key = wl->recorded_ap_keys[i];
		ret = wl1271_cmd_set_ap_key(wl, KEY_ADD_OR_REPLACE,
					    key->id, key->key_type,
					    key->key_size, key->key,
					    key->hlid, key->tx_seq_32,
					    key->tx_seq_16);
		if (ret < 0)
			goto out;

		if (key->key_type == KEY_WEP)
			wep_key_added = true;
	}

	if (wep_key_added) {
		ret = wl1271_cmd_set_ap_default_wep_key(wl, wl->default_key);
		if (ret < 0)
			goto out;
	}

out:
	wl1271_free_ap_keys(wl);
	return ret;
}

static int wl1271_set_key(struct wl1271 *wl, u16 action, u8 id, u8 key_type,
		       u8 key_size, const u8 *key, u32 tx_seq_32,
		       u16 tx_seq_16, struct ieee80211_sta *sta)
{
	int ret;
	bool is_ap = (wl->bss_type == BSS_TYPE_AP_BSS);

	if (is_ap) {
		struct wl1271_station *wl_sta;
		u8 hlid;

		if (sta) {
			wl_sta = (struct wl1271_station *)sta->drv_priv;
			hlid = wl_sta->hlid;
		} else {
			hlid = WL1271_AP_BROADCAST_HLID;
		}

		if (!test_bit(WL1271_FLAG_AP_STARTED, &wl->flags)) {
			/*
			 * We do not support removing keys after AP shutdown.
			 * Pretend we do to make mac80211 happy.
			 */
			if (action != KEY_ADD_OR_REPLACE)
				return 0;

			ret = wl1271_record_ap_key(wl, id,
					     key_type, key_size,
					     key, hlid, tx_seq_32,
					     tx_seq_16);
		} else {
			ret = wl1271_cmd_set_ap_key(wl, action,
					     id, key_type, key_size,
					     key, hlid, tx_seq_32,
					     tx_seq_16);
		}

		if (ret < 0)
			return ret;
	} else {
		const u8 *addr;
		static const u8 bcast_addr[ETH_ALEN] = {
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff
		};

		addr = sta ? sta->addr : bcast_addr;

		if (is_zero_ether_addr(addr)) {
			/* We dont support TX only encryption */
			return -EOPNOTSUPP;
		}

		/* The wl1271 does not allow to remove unicast keys - they
		   will be cleared automatically on next CMD_JOIN. Ignore the
		   request silently, as we dont want the mac80211 to emit
		   an error message. */
		if (action == KEY_REMOVE && !is_broadcast_ether_addr(addr))
			return 0;

		ret = wl1271_cmd_set_sta_key(wl, action,
					     id, key_type, key_size,
					     key, addr, tx_seq_32,
					     tx_seq_16);
		if (ret < 0)
			return ret;

		/* the default WEP key needs to be configured at least once */
		if (key_type == KEY_WEP) {
			ret = wl1271_cmd_set_sta_default_wep_key(wl,
							wl->default_key);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int wl1271_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct ieee80211_key_conf *key_conf)
{
	struct wl1271 *wl = hw->priv;
	int ret;
	u32 tx_seq_32 = 0;
	u16 tx_seq_16 = 0;
	u8 key_type;

	wl1271_debug(DEBUG_MAC80211, "mac80211 set key");

	wl1271_debug(DEBUG_CRYPT, "CMD: 0x%x sta: %p", cmd, sta);
	wl1271_debug(DEBUG_CRYPT, "Key: algo:0x%x, id:%d, len:%d flags 0x%x",
		     key_conf->cipher, key_conf->keyidx,
		     key_conf->keylen, key_conf->flags);
	wl1271_dump(DEBUG_CRYPT, "KEY: ", key_conf->key, key_conf->keylen);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF)) {
		ret = -EAGAIN;
		goto out_unlock;
	}

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out_unlock;

	switch (key_conf->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		key_type = KEY_WEP;

		key_conf->hw_key_idx = key_conf->keyidx;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key_type = KEY_TKIP;

		key_conf->hw_key_idx = key_conf->keyidx;
		tx_seq_32 = WL1271_TX_SECURITY_HI32(wl->tx_security_seq);
		tx_seq_16 = WL1271_TX_SECURITY_LO16(wl->tx_security_seq);
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key_type = KEY_AES;

		key_conf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
		tx_seq_32 = WL1271_TX_SECURITY_HI32(wl->tx_security_seq);
		tx_seq_16 = WL1271_TX_SECURITY_LO16(wl->tx_security_seq);
		break;
	case WL1271_CIPHER_SUITE_GEM:
		key_type = KEY_GEM;
		tx_seq_32 = WL1271_TX_SECURITY_HI32(wl->tx_security_seq);
		tx_seq_16 = WL1271_TX_SECURITY_LO16(wl->tx_security_seq);
		break;
	default:
		wl1271_error("Unknown key algo 0x%x", key_conf->cipher);

		ret = -EOPNOTSUPP;
		goto out_sleep;
	}

	switch (cmd) {
	case SET_KEY:
		ret = wl1271_set_key(wl, KEY_ADD_OR_REPLACE,
				 key_conf->keyidx, key_type,
				 key_conf->keylen, key_conf->key,
				 tx_seq_32, tx_seq_16, sta);
		if (ret < 0) {
			wl1271_error("Could not add or replace key");
			goto out_sleep;
		}
		break;

	case DISABLE_KEY:
		ret = wl1271_set_key(wl, KEY_REMOVE,
				     key_conf->keyidx, key_type,
				     key_conf->keylen, key_conf->key,
				     0, 0, sta);
		if (ret < 0) {
			wl1271_error("Could not remove key");
			goto out_sleep;
		}
		break;

	default:
		wl1271_error("Unsupported key cmd 0x%x", cmd);
		ret = -EOPNOTSUPP;
		break;
	}

out_sleep:
	wl1271_ps_elp_sleep(wl);

out_unlock:
	mutex_unlock(&wl->mutex);

	return ret;
}

static int wl1271_op_hw_scan(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct cfg80211_scan_request *req)
{
	struct wl1271 *wl = hw->priv;
	int ret;
	u8 *ssid = NULL;
	size_t len = 0;

	wl1271_debug(DEBUG_MAC80211, "mac80211 hw scan");

	if (req->n_ssids) {
		ssid = req->ssids[0].ssid;
		len = req->ssids[0].ssid_len;
	}

	mutex_lock(&wl->mutex);

	if (wl->state == WL1271_STATE_OFF) {
		/*
		 * We cannot return -EBUSY here because cfg80211 will expect
		 * a call to ieee80211_scan_completed if we do - in this case
		 * there won't be any call.
		 */
		ret = -EAGAIN;
		goto out;
	}

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wl1271_scan(hw->priv, ssid, len, req);

	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static int wl1271_op_sched_scan_start(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct cfg80211_sched_scan_request *req,
				      struct ieee80211_sched_scan_ies *ies)
{
	struct wl1271 *wl = hw->priv;
	int ret;

	wl1271_debug(DEBUG_MAC80211, "wl1271_op_sched_scan_start");

	mutex_lock(&wl->mutex);

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wl1271_scan_sched_scan_config(wl, req, ies);
	if (ret < 0)
		goto out_sleep;

	ret = wl1271_scan_sched_scan_start(wl);
	if (ret < 0)
		goto out_sleep;

	wl->sched_scanning = true;

out_sleep:
	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);
	return ret;
}

static void wl1271_op_sched_scan_stop(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif)
{
	struct wl1271 *wl = hw->priv;
	int ret;

	wl1271_debug(DEBUG_MAC80211, "wl1271_op_sched_scan_stop");

	mutex_lock(&wl->mutex);

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	wl1271_scan_sched_scan_stop(wl);

	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);
}

static int wl1271_op_set_frag_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct wl1271 *wl = hw->priv;
	int ret = 0;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wl1271_acx_frag_threshold(wl, value);
	if (ret < 0)
		wl1271_warning("wl1271_op_set_frag_threshold failed: %d", ret);

	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static int wl1271_op_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct wl1271 *wl = hw->priv;
	int ret = 0;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wl1271_acx_rts_threshold(wl, value);
	if (ret < 0)
		wl1271_warning("wl1271_op_set_rts_threshold failed: %d", ret);

	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static int wl1271_ssid_set(struct wl1271 *wl, struct sk_buff *skb,
			    int offset)
{
	u8 ssid_len;
	const u8 *ptr = cfg80211_find_ie(WLAN_EID_SSID, skb->data + offset,
					 skb->len - offset);

	if (!ptr) {
		wl1271_error("No SSID in IEs!");
		return -ENOENT;
	}

	ssid_len = ptr[1];
	if (ssid_len > IEEE80211_MAX_SSID_LEN) {
		wl1271_error("SSID is too long!");
		return -EINVAL;
	}

	wl->ssid_len = ssid_len;
	memcpy(wl->ssid, ptr+2, ssid_len);
	return 0;
}

static int wl1271_bss_erp_info_changed(struct wl1271 *wl,
				       struct ieee80211_bss_conf *bss_conf,
				       u32 changed)
{
	int ret = 0;

	if (changed & BSS_CHANGED_ERP_SLOT) {
		if (bss_conf->use_short_slot)
			ret = wl1271_acx_slot(wl, SLOT_TIME_SHORT);
		else
			ret = wl1271_acx_slot(wl, SLOT_TIME_LONG);
		if (ret < 0) {
			wl1271_warning("Set slot time failed %d", ret);
			goto out;
		}
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		if (bss_conf->use_short_preamble)
			wl1271_acx_set_preamble(wl, ACX_PREAMBLE_SHORT);
		else
			wl1271_acx_set_preamble(wl, ACX_PREAMBLE_LONG);
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		if (bss_conf->use_cts_prot)
			ret = wl1271_acx_cts_protect(wl, CTSPROTECT_ENABLE);
		else
			ret = wl1271_acx_cts_protect(wl, CTSPROTECT_DISABLE);
		if (ret < 0) {
			wl1271_warning("Set ctsprotect failed %d", ret);
			goto out;
		}
	}

out:
	return ret;
}

static int wl1271_bss_beacon_info_changed(struct wl1271 *wl,
					  struct ieee80211_vif *vif,
					  struct ieee80211_bss_conf *bss_conf,
					  u32 changed)
{
	bool is_ap = (wl->bss_type == BSS_TYPE_AP_BSS);
	int ret = 0;

	if ((changed & BSS_CHANGED_BEACON_INT)) {
		wl1271_debug(DEBUG_MASTER, "beacon interval updated: %d",
			bss_conf->beacon_int);

		wl->beacon_int = bss_conf->beacon_int;
	}

	if ((changed & BSS_CHANGED_BEACON)) {
		struct ieee80211_hdr *hdr;
		int ieoffset = offsetof(struct ieee80211_mgmt,
					u.beacon.variable);
		struct sk_buff *beacon = ieee80211_beacon_get(wl->hw, vif);
		u16 tmpl_id;

		if (!beacon)
			goto out;

		wl1271_debug(DEBUG_MASTER, "beacon updated");

		ret = wl1271_ssid_set(wl, beacon, ieoffset);
		if (ret < 0) {
			dev_kfree_skb(beacon);
			goto out;
		}
		tmpl_id = is_ap ? CMD_TEMPL_AP_BEACON :
				  CMD_TEMPL_BEACON;
		ret = wl1271_cmd_template_set(wl, tmpl_id,
					      beacon->data,
					      beacon->len, 0,
					      wl1271_tx_min_rate_get(wl));
		if (ret < 0) {
			dev_kfree_skb(beacon);
			goto out;
		}

		hdr = (struct ieee80211_hdr *) beacon->data;
		hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						 IEEE80211_STYPE_PROBE_RESP);

		tmpl_id = is_ap ? CMD_TEMPL_AP_PROBE_RESPONSE :
				  CMD_TEMPL_PROBE_RESPONSE;
		ret = wl1271_cmd_template_set(wl,
					      tmpl_id,
					      beacon->data,
					      beacon->len, 0,
					      wl1271_tx_min_rate_get(wl));
		dev_kfree_skb(beacon);
		if (ret < 0)
			goto out;
	}

out:
	return ret;
}

/* AP mode changes */
static void wl1271_bss_info_changed_ap(struct wl1271 *wl,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u32 changed)
{
	int ret = 0;

	if ((changed & BSS_CHANGED_BASIC_RATES)) {
		u32 rates = bss_conf->basic_rates;

		wl->basic_rate_set = wl1271_tx_enabled_rates_get(wl, rates);
		wl->basic_rate = wl1271_tx_min_rate_get(wl);

		ret = wl1271_init_ap_rates(wl);
		if (ret < 0) {
			wl1271_error("AP rate policy change failed %d", ret);
			goto out;
		}

		ret = wl1271_ap_init_templates(wl);
		if (ret < 0)
			goto out;
	}

	ret = wl1271_bss_beacon_info_changed(wl, vif, bss_conf, changed);
	if (ret < 0)
		goto out;

	if ((changed & BSS_CHANGED_BEACON_ENABLED)) {
		if (bss_conf->enable_beacon) {
			if (!test_bit(WL1271_FLAG_AP_STARTED, &wl->flags)) {
				ret = wl1271_cmd_start_bss(wl);
				if (ret < 0)
					goto out;

				set_bit(WL1271_FLAG_AP_STARTED, &wl->flags);
				wl1271_debug(DEBUG_AP, "started AP");

				ret = wl1271_ap_init_hwenc(wl);
				if (ret < 0)
					goto out;
			}
		} else {
			if (test_bit(WL1271_FLAG_AP_STARTED, &wl->flags)) {
				ret = wl1271_cmd_stop_bss(wl);
				if (ret < 0)
					goto out;

				clear_bit(WL1271_FLAG_AP_STARTED, &wl->flags);
				wl1271_debug(DEBUG_AP, "stopped AP");
			}
		}
	}

	if (changed & BSS_CHANGED_IBSS) {
		wl1271_debug(DEBUG_ADHOC, "ibss_joined: %d",
			     bss_conf->ibss_joined);

		if (bss_conf->ibss_joined) {
			u32 rates = bss_conf->basic_rates;
			wl->basic_rate_set = wl1271_tx_enabled_rates_get(wl,
									 rates);
			wl->basic_rate = wl1271_tx_min_rate_get(wl);

			/* by default, use 11b rates */
			wl->rate_set = CONF_TX_IBSS_DEFAULT_RATES;
			ret = wl1271_acx_sta_rate_policies(wl);
			if (ret < 0)
				goto out;
		}
	}

	ret = wl1271_bss_erp_info_changed(wl, bss_conf, changed);
	if (ret < 0)
		goto out;
out:
	return;
}

/* STA/IBSS mode changes */
static void wl1271_bss_info_changed_sta(struct wl1271 *wl,
					struct ieee80211_vif *vif,
					struct ieee80211_bss_conf *bss_conf,
					u32 changed)
{
	bool do_join = false, set_assoc = false;
	bool is_ibss = (wl->bss_type == BSS_TYPE_IBSS);
	u32 sta_rate_set = 0;
	int ret;
	struct ieee80211_sta *sta;
	bool sta_exists = false;
	struct ieee80211_sta_ht_cap sta_ht_cap;

	if (is_ibss) {
		ret = wl1271_bss_beacon_info_changed(wl, vif, bss_conf,
						     changed);
		if (ret < 0)
			goto out;
	}

	if ((changed & BSS_CHANGED_BEACON_INT)  && is_ibss)
		do_join = true;

	/* Need to update the SSID (for filtering etc) */
	if ((changed & BSS_CHANGED_BEACON) && is_ibss)
		do_join = true;

	if ((changed & BSS_CHANGED_BEACON_ENABLED) && is_ibss) {
		wl1271_debug(DEBUG_ADHOC, "ad-hoc beaconing: %s",
			     bss_conf->enable_beacon ? "enabled" : "disabled");

		if (bss_conf->enable_beacon)
			wl->set_bss_type = BSS_TYPE_IBSS;
		else
			wl->set_bss_type = BSS_TYPE_STA_BSS;
		do_join = true;
	}

	if ((changed & BSS_CHANGED_CQM)) {
		bool enable = false;
		if (bss_conf->cqm_rssi_thold)
			enable = true;
		ret = wl1271_acx_rssi_snr_trigger(wl, enable,
						  bss_conf->cqm_rssi_thold,
						  bss_conf->cqm_rssi_hyst);
		if (ret < 0)
			goto out;
		wl->rssi_thold = bss_conf->cqm_rssi_thold;
	}

	if ((changed & BSS_CHANGED_BSSID) &&
	    /*
	     * Now we know the correct bssid, so we send a new join command
	     * and enable the BSSID filter
	     */
	    memcmp(wl->bssid, bss_conf->bssid, ETH_ALEN)) {
		memcpy(wl->bssid, bss_conf->bssid, ETH_ALEN);

		if (!is_zero_ether_addr(wl->bssid)) {
			ret = wl1271_cmd_build_null_data(wl);
			if (ret < 0)
				goto out;

			ret = wl1271_build_qos_null_data(wl);
			if (ret < 0)
				goto out;

			/* filter out all packets not from this BSSID */
			wl1271_configure_filters(wl, 0);

			/* Need to update the BSSID (for filtering etc) */
			do_join = true;
		}
	}

	rcu_read_lock();
	sta = ieee80211_find_sta(vif, bss_conf->bssid);
	if (sta)  {
		/* save the supp_rates of the ap */
		sta_rate_set = sta->supp_rates[wl->hw->conf.channel->band];
		if (sta->ht_cap.ht_supported)
			sta_rate_set |=
			    (sta->ht_cap.mcs.rx_mask[0] << HW_HT_RATES_OFFSET);
		sta_ht_cap = sta->ht_cap;
		sta_exists = true;
	}
	rcu_read_unlock();

	if (sta_exists) {
		/* handle new association with HT and HT information change */
		if ((changed & BSS_CHANGED_HT) &&
		    (bss_conf->channel_type != NL80211_CHAN_NO_HT)) {
			ret = wl1271_acx_set_ht_capabilities(wl, &sta_ht_cap,
							     true);
			if (ret < 0) {
				wl1271_warning("Set ht cap true failed %d",
					       ret);
				goto out;
			}
			ret = wl1271_acx_set_ht_information(wl,
						bss_conf->ht_operation_mode);
			if (ret < 0) {
				wl1271_warning("Set ht information failed %d",
					       ret);
				goto out;
			}
		}
		/* handle new association without HT and disassociation */
		else if (changed & BSS_CHANGED_ASSOC) {
			ret = wl1271_acx_set_ht_capabilities(wl, &sta_ht_cap,
							     false);
			if (ret < 0) {
				wl1271_warning("Set ht cap false failed %d",
					       ret);
				goto out;
			}
		}
	}

	if ((changed & BSS_CHANGED_ASSOC)) {
		if (bss_conf->assoc) {
			u32 rates;
			int ieoffset;
			wl->aid = bss_conf->aid;
			set_assoc = true;

			wl->ps_poll_failures = 0;

			/*
			 * use basic rates from AP, and determine lowest rate
			 * to use with control frames.
			 */
			rates = bss_conf->basic_rates;
			wl->basic_rate_set = wl1271_tx_enabled_rates_get(wl,
									 rates);
			wl->basic_rate = wl1271_tx_min_rate_get(wl);
			if (sta_rate_set)
				wl->rate_set = wl1271_tx_enabled_rates_get(wl,
								sta_rate_set);
			ret = wl1271_acx_sta_rate_policies(wl);
			if (ret < 0)
				goto out;

			/*
			 * with wl1271, we don't need to update the
			 * beacon_int and dtim_period, because the firmware
			 * updates it by itself when the first beacon is
			 * received after a join.
			 */
			ret = wl1271_cmd_build_ps_poll(wl, wl->aid);
			if (ret < 0)
				goto out;

			/*
			 * Get a template for hardware connection maintenance
			 */
			dev_kfree_skb(wl->probereq);
			wl->probereq = wl1271_cmd_build_ap_probe_req(wl, NULL);
			ieoffset = offsetof(struct ieee80211_mgmt,
					    u.probe_req.variable);
			wl1271_ssid_set(wl, wl->probereq, ieoffset);

			/* enable the connection monitoring feature */
			ret = wl1271_acx_conn_monit_params(wl, true);
			if (ret < 0)
				goto out;

			/* If we want to go in PSM but we're not there yet */
			if (test_bit(WL1271_FLAG_PSM_REQUESTED, &wl->flags) &&
			    !test_bit(WL1271_FLAG_PSM, &wl->flags)) {
				enum wl1271_cmd_ps_mode mode;

				mode = STATION_POWER_SAVE_MODE;
				ret = wl1271_ps_set_mode(wl, mode,
							 wl->basic_rate,
							 true);
				if (ret < 0)
					goto out;
			}
		} else {
			/* use defaults when not associated */
			bool was_assoc =
			    !!test_and_clear_bit(WL1271_FLAG_STA_ASSOCIATED,
						 &wl->flags);
			clear_bit(WL1271_FLAG_STA_STATE_SENT, &wl->flags);
			wl->aid = 0;

			/* free probe-request template */
			dev_kfree_skb(wl->probereq);
			wl->probereq = NULL;

			/* re-enable dynamic ps - just in case */
			ieee80211_enable_dyn_ps(wl->vif);

			/* revert back to minimum rates for the current band */
			wl1271_set_band_rate(wl);
			wl->basic_rate = wl1271_tx_min_rate_get(wl);
			ret = wl1271_acx_sta_rate_policies(wl);
			if (ret < 0)
				goto out;

			/* disable connection monitor features */
			ret = wl1271_acx_conn_monit_params(wl, false);

			/* Disable the keep-alive feature */
			ret = wl1271_acx_keep_alive_mode(wl, false);
			if (ret < 0)
				goto out;

			/* restore the bssid filter and go to dummy bssid */
			if (was_assoc) {
				wl1271_unjoin(wl);
				wl1271_dummy_join(wl);
			}
		}
	}

	ret = wl1271_bss_erp_info_changed(wl, bss_conf, changed);
	if (ret < 0)
		goto out;

	if (changed & BSS_CHANGED_ARP_FILTER) {
		__be32 addr = bss_conf->arp_addr_list[0];
		WARN_ON(wl->bss_type != BSS_TYPE_STA_BSS);

		if (bss_conf->arp_addr_cnt == 1 &&
		    bss_conf->arp_filter_enabled) {
			/*
			 * The template should have been configured only upon
			 * association. however, it seems that the correct ip
			 * isn't being set (when sending), so we have to
			 * reconfigure the template upon every ip change.
			 */
			ret = wl1271_cmd_build_arp_rsp(wl, addr);
			if (ret < 0) {
				wl1271_warning("build arp rsp failed: %d", ret);
				goto out;
			}

			ret = wl1271_acx_arp_ip_filter(wl,
				ACX_ARP_FILTER_ARP_FILTERING,
				addr);
		} else
			ret = wl1271_acx_arp_ip_filter(wl, 0, addr);

		if (ret < 0)
			goto out;
	}

	if (do_join) {
		ret = wl1271_join(wl, set_assoc);
		if (ret < 0) {
			wl1271_warning("cmd join failed %d", ret);
			goto out;
		}
	}

out:
	return;
}

static void wl1271_op_bss_info_changed(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u32 changed)
{
	struct wl1271 *wl = hw->priv;
	bool is_ap = (wl->bss_type == BSS_TYPE_AP_BSS);
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 bss info changed 0x%x",
		     (int)changed);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	if (is_ap)
		wl1271_bss_info_changed_ap(wl, vif, bss_conf, changed);
	else
		wl1271_bss_info_changed_sta(wl, vif, bss_conf, changed);

	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);
}

static int wl1271_op_conf_tx(struct ieee80211_hw *hw, u16 queue,
			     const struct ieee80211_tx_queue_params *params)
{
	struct wl1271 *wl = hw->priv;
	u8 ps_scheme;
	int ret = 0;

	mutex_lock(&wl->mutex);

	wl1271_debug(DEBUG_MAC80211, "mac80211 conf tx %d", queue);

	if (params->uapsd)
		ps_scheme = CONF_PS_SCHEME_UPSD_TRIGGER;
	else
		ps_scheme = CONF_PS_SCHEME_LEGACY;

	if (wl->state == WL1271_STATE_OFF) {
		/*
		 * If the state is off, the parameters will be recorded and
		 * configured on init. This happens in AP-mode.
		 */
		struct conf_tx_ac_category *conf_ac =
			&wl->conf.tx.ac_conf[wl1271_tx_get_queue(queue)];
		struct conf_tx_tid *conf_tid =
			&wl->conf.tx.tid_conf[wl1271_tx_get_queue(queue)];

		conf_ac->ac = wl1271_tx_get_queue(queue);
		conf_ac->cw_min = (u8)params->cw_min;
		conf_ac->cw_max = params->cw_max;
		conf_ac->aifsn = params->aifs;
		conf_ac->tx_op_limit = params->txop << 5;

		conf_tid->queue_id = wl1271_tx_get_queue(queue);
		conf_tid->channel_type = CONF_CHANNEL_TYPE_EDCF;
		conf_tid->tsid = wl1271_tx_get_queue(queue);
		conf_tid->ps_scheme = ps_scheme;
		conf_tid->ack_policy = CONF_ACK_POLICY_LEGACY;
		conf_tid->apsd_conf[0] = 0;
		conf_tid->apsd_conf[1] = 0;
		goto out;
	}

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	/*
	 * the txop is confed in units of 32us by the mac80211,
	 * we need us
	 */
	ret = wl1271_acx_ac_cfg(wl, wl1271_tx_get_queue(queue),
				params->cw_min, params->cw_max,
				params->aifs, params->txop << 5);
	if (ret < 0)
		goto out_sleep;

	ret = wl1271_acx_tid_cfg(wl, wl1271_tx_get_queue(queue),
				 CONF_CHANNEL_TYPE_EDCF,
				 wl1271_tx_get_queue(queue),
				 ps_scheme, CONF_ACK_POLICY_LEGACY,
				 0, 0);

out_sleep:
	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static u64 wl1271_op_get_tsf(struct ieee80211_hw *hw)
{

	struct wl1271 *wl = hw->priv;
	u64 mactime = ULLONG_MAX;
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 get tsf");

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wl1271_acx_tsf_info(wl, &mactime);
	if (ret < 0)
		goto out_sleep;

out_sleep:
	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);
	return mactime;
}

static int wl1271_op_get_survey(struct ieee80211_hw *hw, int idx,
				struct survey_info *survey)
{
	struct wl1271 *wl = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;

	if (idx != 0)
		return -ENOENT;

	survey->channel = conf->channel;
	survey->filled = SURVEY_INFO_NOISE_DBM;
	survey->noise = wl->noise;

	return 0;
}

static int wl1271_allocate_sta(struct wl1271 *wl,
			     struct ieee80211_sta *sta,
			     u8 *hlid)
{
	struct wl1271_station *wl_sta;
	int id;

	id = find_first_zero_bit(wl->ap_hlid_map, AP_MAX_STATIONS);
	if (id >= AP_MAX_STATIONS) {
		wl1271_warning("could not allocate HLID - too much stations");
		return -EBUSY;
	}

	wl_sta = (struct wl1271_station *)sta->drv_priv;
	__set_bit(id, wl->ap_hlid_map);
	wl_sta->hlid = WL1271_AP_STA_HLID_START + id;
	*hlid = wl_sta->hlid;
	memcpy(wl->links[wl_sta->hlid].addr, sta->addr, ETH_ALEN);
	return 0;
}

static void wl1271_free_sta(struct wl1271 *wl, u8 hlid)
{
	int id = hlid - WL1271_AP_STA_HLID_START;

	if (WARN_ON(!test_bit(id, wl->ap_hlid_map)))
		return;

	__clear_bit(id, wl->ap_hlid_map);
	memset(wl->links[hlid].addr, 0, ETH_ALEN);
	wl1271_tx_reset_link_queues(wl, hlid);
	__clear_bit(hlid, &wl->ap_ps_map);
	__clear_bit(hlid, (unsigned long *)&wl->ap_fw_ps_map);
}

static int wl1271_op_sta_add(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta)
{
	struct wl1271 *wl = hw->priv;
	int ret = 0;
	u8 hlid;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	if (wl->bss_type != BSS_TYPE_AP_BSS)
		goto out;

	wl1271_debug(DEBUG_MAC80211, "mac80211 add sta %d", (int)sta->aid);

	ret = wl1271_allocate_sta(wl, sta, &hlid);
	if (ret < 0)
		goto out;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out_free_sta;

	ret = wl1271_cmd_add_sta(wl, sta, hlid);
	if (ret < 0)
		goto out_sleep;

out_sleep:
	wl1271_ps_elp_sleep(wl);

out_free_sta:
	if (ret < 0)
		wl1271_free_sta(wl, hlid);

out:
	mutex_unlock(&wl->mutex);
	return ret;
}

static int wl1271_op_sta_remove(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta)
{
	struct wl1271 *wl = hw->priv;
	struct wl1271_station *wl_sta;
	int ret = 0, id;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	if (wl->bss_type != BSS_TYPE_AP_BSS)
		goto out;

	wl1271_debug(DEBUG_MAC80211, "mac80211 remove sta %d", (int)sta->aid);

	wl_sta = (struct wl1271_station *)sta->drv_priv;
	id = wl_sta->hlid - WL1271_AP_STA_HLID_START;
	if (WARN_ON(!test_bit(id, wl->ap_hlid_map)))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wl1271_cmd_remove_sta(wl, wl_sta->hlid);
	if (ret < 0)
		goto out_sleep;

	wl1271_free_sta(wl, wl_sta->hlid);

out_sleep:
	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);
	return ret;
}

static int wl1271_op_ampdu_action(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  enum ieee80211_ampdu_mlme_action action,
				  struct ieee80211_sta *sta, u16 tid, u16 *ssn,
				  u8 buf_size)
{
	struct wl1271 *wl = hw->priv;
	int ret;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		if (wl->ba_support) {
			ret = wl1271_acx_set_ba_receiver_session(wl, tid, *ssn,
								 true);
			if (!ret)
				wl->ba_rx_bitmap |= BIT(tid);
		} else {
			ret = -ENOTSUPP;
		}
		break;

	case IEEE80211_AMPDU_RX_STOP:
		ret = wl1271_acx_set_ba_receiver_session(wl, tid, 0, false);
		if (!ret)
			wl->ba_rx_bitmap &= ~BIT(tid);
		break;

	/*
	 * The BA initiator session management in FW independently.
	 * Falling break here on purpose for all TX APDU commands.
	 */
	case IEEE80211_AMPDU_TX_START:
	case IEEE80211_AMPDU_TX_STOP:
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		ret = -EINVAL;
		break;

	default:
		wl1271_error("Incorrect ampdu action id=%x\n", action);
		ret = -EINVAL;
	}

	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static bool wl1271_tx_frames_pending(struct ieee80211_hw *hw)
{
	struct wl1271 *wl = hw->priv;
	bool ret = false;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	/* packets are considered pending if in the TX queue or the FW */
	ret = (wl->tx_queue_count > 0) || (wl->tx_frames_cnt > 0);

	/* the above is appropriate for STA mode for PS purposes */
	WARN_ON(wl->bss_type != BSS_TYPE_STA_BSS);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

/* can't be const, mac80211 writes to this */
static struct ieee80211_rate wl1271_rates[] = {
	{ .bitrate = 10,
	  .hw_value = CONF_HW_BIT_RATE_1MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_1MBPS, },
	{ .bitrate = 20,
	  .hw_value = CONF_HW_BIT_RATE_2MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_2MBPS,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,
	  .hw_value = CONF_HW_BIT_RATE_5_5MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_5_5MBPS,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110,
	  .hw_value = CONF_HW_BIT_RATE_11MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_11MBPS,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60,
	  .hw_value = CONF_HW_BIT_RATE_6MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_6MBPS, },
	{ .bitrate = 90,
	  .hw_value = CONF_HW_BIT_RATE_9MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_9MBPS, },
	{ .bitrate = 120,
	  .hw_value = CONF_HW_BIT_RATE_12MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_12MBPS, },
	{ .bitrate = 180,
	  .hw_value = CONF_HW_BIT_RATE_18MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_18MBPS, },
	{ .bitrate = 240,
	  .hw_value = CONF_HW_BIT_RATE_24MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_24MBPS, },
	{ .bitrate = 360,
	 .hw_value = CONF_HW_BIT_RATE_36MBPS,
	 .hw_value_short = CONF_HW_BIT_RATE_36MBPS, },
	{ .bitrate = 480,
	  .hw_value = CONF_HW_BIT_RATE_48MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_48MBPS, },
	{ .bitrate = 540,
	  .hw_value = CONF_HW_BIT_RATE_54MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_54MBPS, },
};

/* can't be const, mac80211 writes to this */
static struct ieee80211_channel wl1271_channels[] = {
	{ .hw_value = 1, .center_freq = 2412, .max_power = 25 },
	{ .hw_value = 2, .center_freq = 2417, .max_power = 25 },
	{ .hw_value = 3, .center_freq = 2422, .max_power = 25 },
	{ .hw_value = 4, .center_freq = 2427, .max_power = 25 },
	{ .hw_value = 5, .center_freq = 2432, .max_power = 25 },
	{ .hw_value = 6, .center_freq = 2437, .max_power = 25 },
	{ .hw_value = 7, .center_freq = 2442, .max_power = 25 },
	{ .hw_value = 8, .center_freq = 2447, .max_power = 25 },
	{ .hw_value = 9, .center_freq = 2452, .max_power = 25 },
	{ .hw_value = 10, .center_freq = 2457, .max_power = 25 },
	{ .hw_value = 11, .center_freq = 2462, .max_power = 25 },
	{ .hw_value = 12, .center_freq = 2467, .max_power = 25 },
	{ .hw_value = 13, .center_freq = 2472, .max_power = 25 },
	{ .hw_value = 14, .center_freq = 2484, .max_power = 25 },
};

/* mapping to indexes for wl1271_rates */
static const u8 wl1271_rate_to_idx_2ghz[] = {
	/* MCS rates are used only with 11n */
	7,                            /* CONF_HW_RXTX_RATE_MCS7 */
	6,                            /* CONF_HW_RXTX_RATE_MCS6 */
	5,                            /* CONF_HW_RXTX_RATE_MCS5 */
	4,                            /* CONF_HW_RXTX_RATE_MCS4 */
	3,                            /* CONF_HW_RXTX_RATE_MCS3 */
	2,                            /* CONF_HW_RXTX_RATE_MCS2 */
	1,                            /* CONF_HW_RXTX_RATE_MCS1 */
	0,                            /* CONF_HW_RXTX_RATE_MCS0 */

	11,                            /* CONF_HW_RXTX_RATE_54   */
	10,                            /* CONF_HW_RXTX_RATE_48   */
	9,                             /* CONF_HW_RXTX_RATE_36   */
	8,                             /* CONF_HW_RXTX_RATE_24   */

	/* TI-specific rate */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_22   */

	7,                             /* CONF_HW_RXTX_RATE_18   */
	6,                             /* CONF_HW_RXTX_RATE_12   */
	3,                             /* CONF_HW_RXTX_RATE_11   */
	5,                             /* CONF_HW_RXTX_RATE_9    */
	4,                             /* CONF_HW_RXTX_RATE_6    */
	2,                             /* CONF_HW_RXTX_RATE_5_5  */
	1,                             /* CONF_HW_RXTX_RATE_2    */
	0                              /* CONF_HW_RXTX_RATE_1    */
};

/* 11n STA capabilities */
#define HW_RX_HIGHEST_RATE	72

#ifdef CONFIG_WL12XX_HT
#define WL12XX_HT_CAP { \
	.cap = IEEE80211_HT_CAP_GRN_FLD | IEEE80211_HT_CAP_SGI_20 | \
	       (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT), \
	.ht_supported = true, \
	.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K, \
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_8, \
	.mcs = { \
		.rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, }, \
		.rx_highest = cpu_to_le16(HW_RX_HIGHEST_RATE), \
		.tx_params = IEEE80211_HT_MCS_TX_DEFINED, \
		}, \
}
#else
#define WL12XX_HT_CAP { \
	.ht_supported = false, \
}
#endif

/* can't be const, mac80211 writes to this */
static struct ieee80211_supported_band wl1271_band_2ghz = {
	.channels = wl1271_channels,
	.n_channels = ARRAY_SIZE(wl1271_channels),
	.bitrates = wl1271_rates,
	.n_bitrates = ARRAY_SIZE(wl1271_rates),
	.ht_cap	= WL12XX_HT_CAP,
};

/* 5 GHz data rates for WL1273 */
static struct ieee80211_rate wl1271_rates_5ghz[] = {
	{ .bitrate = 60,
	  .hw_value = CONF_HW_BIT_RATE_6MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_6MBPS, },
	{ .bitrate = 90,
	  .hw_value = CONF_HW_BIT_RATE_9MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_9MBPS, },
	{ .bitrate = 120,
	  .hw_value = CONF_HW_BIT_RATE_12MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_12MBPS, },
	{ .bitrate = 180,
	  .hw_value = CONF_HW_BIT_RATE_18MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_18MBPS, },
	{ .bitrate = 240,
	  .hw_value = CONF_HW_BIT_RATE_24MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_24MBPS, },
	{ .bitrate = 360,
	 .hw_value = CONF_HW_BIT_RATE_36MBPS,
	 .hw_value_short = CONF_HW_BIT_RATE_36MBPS, },
	{ .bitrate = 480,
	  .hw_value = CONF_HW_BIT_RATE_48MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_48MBPS, },
	{ .bitrate = 540,
	  .hw_value = CONF_HW_BIT_RATE_54MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_54MBPS, },
};

/* 5 GHz band channels for WL1273 */
static struct ieee80211_channel wl1271_channels_5ghz[] = {
	{ .hw_value = 7, .center_freq = 5035},
	{ .hw_value = 8, .center_freq = 5040},
	{ .hw_value = 9, .center_freq = 5045},
	{ .hw_value = 11, .center_freq = 5055},
	{ .hw_value = 12, .center_freq = 5060},
	{ .hw_value = 16, .center_freq = 5080},
	{ .hw_value = 34, .center_freq = 5170},
	{ .hw_value = 36, .center_freq = 5180},
	{ .hw_value = 38, .center_freq = 5190},
	{ .hw_value = 40, .center_freq = 5200},
	{ .hw_value = 42, .center_freq = 5210},
	{ .hw_value = 44, .center_freq = 5220},
	{ .hw_value = 46, .center_freq = 5230},
	{ .hw_value = 48, .center_freq = 5240},
	{ .hw_value = 52, .center_freq = 5260},
	{ .hw_value = 56, .center_freq = 5280},
	{ .hw_value = 60, .center_freq = 5300},
	{ .hw_value = 64, .center_freq = 5320},
	{ .hw_value = 100, .center_freq = 5500},
	{ .hw_value = 104, .center_freq = 5520},
	{ .hw_value = 108, .center_freq = 5540},
	{ .hw_value = 112, .center_freq = 5560},
	{ .hw_value = 116, .center_freq = 5580},
	{ .hw_value = 120, .center_freq = 5600},
	{ .hw_value = 124, .center_freq = 5620},
	{ .hw_value = 128, .center_freq = 5640},
	{ .hw_value = 132, .center_freq = 5660},
	{ .hw_value = 136, .center_freq = 5680},
	{ .hw_value = 140, .center_freq = 5700},
	{ .hw_value = 149, .center_freq = 5745},
	{ .hw_value = 153, .center_freq = 5765},
	{ .hw_value = 157, .center_freq = 5785},
	{ .hw_value = 161, .center_freq = 5805},
	{ .hw_value = 165, .center_freq = 5825},
};

/* mapping to indexes for wl1271_rates_5ghz */
static const u8 wl1271_rate_to_idx_5ghz[] = {
	/* MCS rates are used only with 11n */
	7,                            /* CONF_HW_RXTX_RATE_MCS7 */
	6,                            /* CONF_HW_RXTX_RATE_MCS6 */
	5,                            /* CONF_HW_RXTX_RATE_MCS5 */
	4,                            /* CONF_HW_RXTX_RATE_MCS4 */
	3,                            /* CONF_HW_RXTX_RATE_MCS3 */
	2,                            /* CONF_HW_RXTX_RATE_MCS2 */
	1,                            /* CONF_HW_RXTX_RATE_MCS1 */
	0,                            /* CONF_HW_RXTX_RATE_MCS0 */

	7,                             /* CONF_HW_RXTX_RATE_54   */
	6,                             /* CONF_HW_RXTX_RATE_48   */
	5,                             /* CONF_HW_RXTX_RATE_36   */
	4,                             /* CONF_HW_RXTX_RATE_24   */

	/* TI-specific rate */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_22   */

	3,                             /* CONF_HW_RXTX_RATE_18   */
	2,                             /* CONF_HW_RXTX_RATE_12   */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_11   */
	1,                             /* CONF_HW_RXTX_RATE_9    */
	0,                             /* CONF_HW_RXTX_RATE_6    */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_5_5  */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_2    */
	CONF_HW_RXTX_RATE_UNSUPPORTED  /* CONF_HW_RXTX_RATE_1    */
};

static struct ieee80211_supported_band wl1271_band_5ghz = {
	.channels = wl1271_channels_5ghz,
	.n_channels = ARRAY_SIZE(wl1271_channels_5ghz),
	.bitrates = wl1271_rates_5ghz,
	.n_bitrates = ARRAY_SIZE(wl1271_rates_5ghz),
	.ht_cap	= WL12XX_HT_CAP,
};

static const u8 *wl1271_band_rate_to_idx[] = {
	[IEEE80211_BAND_2GHZ] = wl1271_rate_to_idx_2ghz,
	[IEEE80211_BAND_5GHZ] = wl1271_rate_to_idx_5ghz
};

static const struct ieee80211_ops wl1271_ops = {
	.start = wl1271_op_start,
	.stop = wl1271_op_stop,
	.add_interface = wl1271_op_add_interface,
	.remove_interface = wl1271_op_remove_interface,
#ifdef CONFIG_PM
	.suspend = wl1271_op_suspend,
	.resume = wl1271_op_resume,
#endif
	.config = wl1271_op_config,
	.prepare_multicast = wl1271_op_prepare_multicast,
	.configure_filter = wl1271_op_configure_filter,
	.tx = wl1271_op_tx,
	.set_key = wl1271_op_set_key,
	.hw_scan = wl1271_op_hw_scan,
	.sched_scan_start = wl1271_op_sched_scan_start,
	.sched_scan_stop = wl1271_op_sched_scan_stop,
	.bss_info_changed = wl1271_op_bss_info_changed,
	.set_frag_threshold = wl1271_op_set_frag_threshold,
	.set_rts_threshold = wl1271_op_set_rts_threshold,
	.conf_tx = wl1271_op_conf_tx,
	.get_tsf = wl1271_op_get_tsf,
	.get_survey = wl1271_op_get_survey,
	.sta_add = wl1271_op_sta_add,
	.sta_remove = wl1271_op_sta_remove,
	.ampdu_action = wl1271_op_ampdu_action,
	.tx_frames_pending = wl1271_tx_frames_pending,
	CFG80211_TESTMODE_CMD(wl1271_tm_cmd)
};


u8 wl1271_rate_to_idx(int rate, enum ieee80211_band band)
{
	u8 idx;

	BUG_ON(band >= sizeof(wl1271_band_rate_to_idx)/sizeof(u8 *));

	if (unlikely(rate >= CONF_HW_RXTX_RATE_MAX)) {
		wl1271_error("Illegal RX rate from HW: %d", rate);
		return 0;
	}

	idx = wl1271_band_rate_to_idx[band][rate];
	if (unlikely(idx == CONF_HW_RXTX_RATE_UNSUPPORTED)) {
		wl1271_error("Unsupported RX rate from HW: %d", rate);
		return 0;
	}

	return idx;
}

static ssize_t wl1271_sysfs_show_bt_coex_state(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct wl1271 *wl = dev_get_drvdata(dev);
	ssize_t len;

	len = PAGE_SIZE;

	mutex_lock(&wl->mutex);
	len = snprintf(buf, len, "%d\n\n0 - off\n1 - on\n",
		       wl->sg_enabled);
	mutex_unlock(&wl->mutex);

	return len;

}

static ssize_t wl1271_sysfs_store_bt_coex_state(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct wl1271 *wl = dev_get_drvdata(dev);
	unsigned long res;
	int ret;

	ret = kstrtoul(buf, 10, &res);
	if (ret < 0) {
		wl1271_warning("incorrect value written to bt_coex_mode");
		return count;
	}

	mutex_lock(&wl->mutex);

	res = !!res;

	if (res == wl->sg_enabled)
		goto out;

	wl->sg_enabled = res;

	if (wl->state == WL1271_STATE_OFF)
		goto out;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	wl1271_acx_sg_enable(wl, wl->sg_enabled);
	wl1271_ps_elp_sleep(wl);

 out:
	mutex_unlock(&wl->mutex);
	return count;
}

static DEVICE_ATTR(bt_coex_state, S_IRUGO | S_IWUSR,
		   wl1271_sysfs_show_bt_coex_state,
		   wl1271_sysfs_store_bt_coex_state);

static ssize_t wl1271_sysfs_show_hw_pg_ver(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct wl1271 *wl = dev_get_drvdata(dev);
	ssize_t len;

	len = PAGE_SIZE;

	mutex_lock(&wl->mutex);
	if (wl->hw_pg_ver >= 0)
		len = snprintf(buf, len, "%d\n", wl->hw_pg_ver);
	else
		len = snprintf(buf, len, "n/a\n");
	mutex_unlock(&wl->mutex);

	return len;
}

static DEVICE_ATTR(hw_pg_ver, S_IRUGO | S_IWUSR,
		   wl1271_sysfs_show_hw_pg_ver, NULL);

int wl1271_register_hw(struct wl1271 *wl)
{
	int ret;

	if (wl->mac80211_registered)
		return 0;

	ret = wl1271_fetch_nvs(wl);
	if (ret == 0) {
		/* NOTE: The wl->nvs->nvs element must be first, in
		 * order to simplify the casting, we assume it is at
		 * the beginning of the wl->nvs structure.
		 */
		u8 *nvs_ptr = (u8 *)wl->nvs;

		wl->mac_addr[0] = nvs_ptr[11];
		wl->mac_addr[1] = nvs_ptr[10];
		wl->mac_addr[2] = nvs_ptr[6];
		wl->mac_addr[3] = nvs_ptr[5];
		wl->mac_addr[4] = nvs_ptr[4];
		wl->mac_addr[5] = nvs_ptr[3];
	}

	SET_IEEE80211_PERM_ADDR(wl->hw, wl->mac_addr);

	ret = ieee80211_register_hw(wl->hw);
	if (ret < 0) {
		wl1271_error("unable to register mac80211 hw: %d", ret);
		return ret;
	}

	wl->mac80211_registered = true;

	wl1271_debugfs_init(wl);

	register_netdevice_notifier(&wl1271_dev_notifier);

	wl1271_notice("loaded");

	return 0;
}
EXPORT_SYMBOL_GPL(wl1271_register_hw);

void wl1271_unregister_hw(struct wl1271 *wl)
{
	if (wl->state == WL1271_STATE_PLT)
		__wl1271_plt_stop(wl);

	unregister_netdevice_notifier(&wl1271_dev_notifier);
	ieee80211_unregister_hw(wl->hw);
	wl->mac80211_registered = false;

}
EXPORT_SYMBOL_GPL(wl1271_unregister_hw);

int wl1271_init_ieee80211(struct wl1271 *wl)
{
	static const u32 cipher_suites[] = {
		WLAN_CIPHER_SUITE_WEP40,
		WLAN_CIPHER_SUITE_WEP104,
		WLAN_CIPHER_SUITE_TKIP,
		WLAN_CIPHER_SUITE_CCMP,
		WL1271_CIPHER_SUITE_GEM,
	};

	/* The tx descriptor buffer and the TKIP space. */
	wl->hw->extra_tx_headroom = WL1271_TKIP_IV_SPACE +
		sizeof(struct wl1271_tx_hw_descr);

	/* unit us */
	/* FIXME: find a proper value */
	wl->hw->channel_change_time = 10000;
	wl->hw->max_listen_interval = wl->conf.conn.max_listen_interval;

	wl->hw->flags = IEEE80211_HW_SIGNAL_DBM |
		IEEE80211_HW_BEACON_FILTER |
		IEEE80211_HW_SUPPORTS_PS |
		IEEE80211_HW_SUPPORTS_UAPSD |
		IEEE80211_HW_HAS_RATE_CONTROL |
		IEEE80211_HW_CONNECTION_MONITOR |
		IEEE80211_HW_SUPPORTS_CQM_RSSI |
		IEEE80211_HW_REPORTS_TX_ACK_STATUS |
		IEEE80211_HW_SPECTRUM_MGMT |
		IEEE80211_HW_AP_LINK_PS;

	wl->hw->wiphy->cipher_suites = cipher_suites;
	wl->hw->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

	wl->hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC) | BIT(NL80211_IFTYPE_AP);
	wl->hw->wiphy->max_scan_ssids = 1;
	/*
	 * Maximum length of elements in scanning probe request templates
	 * should be the maximum length possible for a template, without
	 * the IEEE80211 header of the template
	 */
	wl->hw->wiphy->max_scan_ie_len = WL1271_CMD_TEMPL_MAX_SIZE -
			sizeof(struct ieee80211_header);

	/* make sure all our channels fit in the scanned_ch bitmask */
	BUILD_BUG_ON(ARRAY_SIZE(wl1271_channels) +
		     ARRAY_SIZE(wl1271_channels_5ghz) >
		     WL1271_MAX_CHANNELS);
	/*
	 * We keep local copies of the band structs because we need to
	 * modify them on a per-device basis.
	 */
	memcpy(&wl->bands[IEEE80211_BAND_2GHZ], &wl1271_band_2ghz,
	       sizeof(wl1271_band_2ghz));
	memcpy(&wl->bands[IEEE80211_BAND_5GHZ], &wl1271_band_5ghz,
	       sizeof(wl1271_band_5ghz));

	wl->hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
		&wl->bands[IEEE80211_BAND_2GHZ];
	wl->hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
		&wl->bands[IEEE80211_BAND_5GHZ];

	wl->hw->queues = 4;
	wl->hw->max_rates = 1;

	wl->hw->wiphy->reg_notifier = wl1271_reg_notify;

	SET_IEEE80211_DEV(wl->hw, wl1271_wl_to_dev(wl));

	wl->hw->sta_data_size = sizeof(struct wl1271_station);

	wl->hw->max_rx_aggregation_subframes = 8;

	return 0;
}
EXPORT_SYMBOL_GPL(wl1271_init_ieee80211);

#define WL1271_DEFAULT_CHANNEL 0

struct ieee80211_hw *wl1271_alloc_hw(void)
{
	struct ieee80211_hw *hw;
	struct platform_device *plat_dev = NULL;
	struct wl1271 *wl;
	int i, j, ret;
	unsigned int order;

	hw = ieee80211_alloc_hw(sizeof(*wl), &wl1271_ops);
	if (!hw) {
		wl1271_error("could not alloc ieee80211_hw");
		ret = -ENOMEM;
		goto err_hw_alloc;
	}

	plat_dev = kmemdup(&wl1271_device, sizeof(wl1271_device), GFP_KERNEL);
	if (!plat_dev) {
		wl1271_error("could not allocate platform_device");
		ret = -ENOMEM;
		goto err_plat_alloc;
	}

	wl = hw->priv;
	memset(wl, 0, sizeof(*wl));

	INIT_LIST_HEAD(&wl->list);

	wl->hw = hw;
	wl->plat_dev = plat_dev;

	for (i = 0; i < NUM_TX_QUEUES; i++)
		skb_queue_head_init(&wl->tx_queue[i]);

	for (i = 0; i < NUM_TX_QUEUES; i++)
		for (j = 0; j < AP_MAX_LINKS; j++)
			skb_queue_head_init(&wl->links[j].tx_queue[i]);

	skb_queue_head_init(&wl->deferred_rx_queue);
	skb_queue_head_init(&wl->deferred_tx_queue);

	INIT_DELAYED_WORK(&wl->elp_work, wl1271_elp_work);
	INIT_DELAYED_WORK(&wl->pspoll_work, wl1271_pspoll_work);
	INIT_WORK(&wl->netstack_work, wl1271_netstack_work);
	INIT_WORK(&wl->tx_work, wl1271_tx_work);
	INIT_WORK(&wl->recovery_work, wl1271_recovery_work);
	INIT_DELAYED_WORK(&wl->scan_complete_work, wl1271_scan_complete_work);
	wl->channel = WL1271_DEFAULT_CHANNEL;
	wl->beacon_int = WL1271_DEFAULT_BEACON_INT;
	wl->default_key = 0;
	wl->rx_counter = 0;
	wl->rx_config = WL1271_DEFAULT_STA_RX_CONFIG;
	wl->rx_filter = WL1271_DEFAULT_STA_RX_FILTER;
	wl->psm_entry_retry = 0;
	wl->power_level = WL1271_DEFAULT_POWER_LEVEL;
	wl->basic_rate_set = CONF_TX_RATE_MASK_BASIC;
	wl->basic_rate = CONF_TX_RATE_MASK_BASIC;
	wl->rate_set = CONF_TX_RATE_MASK_BASIC;
	wl->band = IEEE80211_BAND_2GHZ;
	wl->vif = NULL;
	wl->flags = 0;
	wl->sg_enabled = true;
	wl->hw_pg_ver = -1;
	wl->bss_type = MAX_BSS_TYPE;
	wl->set_bss_type = MAX_BSS_TYPE;
	wl->fw_bss_type = MAX_BSS_TYPE;
	wl->last_tx_hlid = 0;
	wl->ap_ps_map = 0;
	wl->ap_fw_ps_map = 0;
	wl->quirks = 0;
	wl->platform_quirks = 0;
	wl->sched_scanning = false;

	memset(wl->tx_frames_map, 0, sizeof(wl->tx_frames_map));
	for (i = 0; i < ACX_TX_DESCRIPTORS; i++)
		wl->tx_frames[i] = NULL;

	spin_lock_init(&wl->wl_lock);

	wl->state = WL1271_STATE_OFF;
	mutex_init(&wl->mutex);

	/* Apply default driver configuration. */
	wl1271_conf_init(wl);

	order = get_order(WL1271_AGGR_BUFFER_SIZE);
	wl->aggr_buf = (u8 *)__get_free_pages(GFP_KERNEL, order);
	if (!wl->aggr_buf) {
		ret = -ENOMEM;
		goto err_hw;
	}

	wl->dummy_packet = wl12xx_alloc_dummy_packet(wl);
	if (!wl->dummy_packet) {
		ret = -ENOMEM;
		goto err_aggr;
	}

	/* Register platform device */
	ret = platform_device_register(wl->plat_dev);
	if (ret) {
		wl1271_error("couldn't register platform device");
		goto err_dummy_packet;
	}
	dev_set_drvdata(&wl->plat_dev->dev, wl);

	/* Create sysfs file to control bt coex state */
	ret = device_create_file(&wl->plat_dev->dev, &dev_attr_bt_coex_state);
	if (ret < 0) {
		wl1271_error("failed to create sysfs file bt_coex_state");
		goto err_platform;
	}

	/* Create sysfs file to get HW PG version */
	ret = device_create_file(&wl->plat_dev->dev, &dev_attr_hw_pg_ver);
	if (ret < 0) {
		wl1271_error("failed to create sysfs file hw_pg_ver");
		goto err_bt_coex_state;
	}

	return hw;

err_bt_coex_state:
	device_remove_file(&wl->plat_dev->dev, &dev_attr_bt_coex_state);

err_platform:
	platform_device_unregister(wl->plat_dev);

err_dummy_packet:
	dev_kfree_skb(wl->dummy_packet);

err_aggr:
	free_pages((unsigned long)wl->aggr_buf, order);

err_hw:
	wl1271_debugfs_exit(wl);
	kfree(plat_dev);

err_plat_alloc:
	ieee80211_free_hw(hw);

err_hw_alloc:

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(wl1271_alloc_hw);

int wl1271_free_hw(struct wl1271 *wl)
{
	platform_device_unregister(wl->plat_dev);
	dev_kfree_skb(wl->dummy_packet);
	free_pages((unsigned long)wl->aggr_buf,
			get_order(WL1271_AGGR_BUFFER_SIZE));
	kfree(wl->plat_dev);

	wl1271_debugfs_exit(wl);

	vfree(wl->fw);
	wl->fw = NULL;
	kfree(wl->nvs);
	wl->nvs = NULL;

	kfree(wl->fw_status);
	kfree(wl->tx_res_if);

	ieee80211_free_hw(wl->hw);

	return 0;
}
EXPORT_SYMBOL_GPL(wl1271_free_hw);

u32 wl12xx_debug_level = DEBUG_NONE;
EXPORT_SYMBOL_GPL(wl12xx_debug_level);
module_param_named(debug_level, wl12xx_debug_level, uint, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(debug_level, "wl12xx debugging level");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luciano Coelho <coelho@ti.com>");
MODULE_AUTHOR("Juuso Oikarinen <juuso.oikarinen@nokia.com>");
