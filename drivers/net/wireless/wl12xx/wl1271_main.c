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

#include "wl1271.h"
#include "wl12xx_80211.h"
#include "wl1271_reg.h"
#include "wl1271_io.h"
#include "wl1271_event.h"
#include "wl1271_tx.h"
#include "wl1271_rx.h"
#include "wl1271_ps.h"
#include "wl1271_init.h"
#include "wl1271_debugfs.h"
#include "wl1271_cmd.h"
#include "wl1271_boot.h"
#include "wl1271_testmode.h"
#include "wl1271_scan.h"

#define WL1271_BOOT_RETRIES 3

static struct conf_drv_settings default_conf = {
	.sg = {
		.params = {
			[CONF_SG_BT_PER_THRESHOLD]                  = 7500,
			[CONF_SG_HV3_MAX_OVERRIDE]                  = 0,
			[CONF_SG_BT_NFS_SAMPLE_INTERVAL]            = 400,
			[CONF_SG_BT_LOAD_RATIO]                     = 50,
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
		.state = CONF_SG_PROTECTIVE,
	},
	.rx = {
		.rx_msdu_life_time           = 512000,
		.packet_detection_threshold  = 0,
		.ps_poll_timeout             = 15,
		.upsd_timeout                = 15,
		.rts_threshold               = 2347,
		.rx_cca_threshold            = 0,
		.irq_blk_threshold           = 0xFFFF,
		.irq_pkt_threshold           = 0,
		.irq_timeout                 = 600,
		.queue_type                  = CONF_RX_QUEUE_TYPE_LOW_PRIORITY,
	},
	.tx = {
		.tx_energy_detection         = 0,
		.rc_conf                     = {
			.enabled_rates       = 0,
			.short_retry_limit   = 10,
			.long_retry_limit    = 10,
			.aflags              = 0
		},
		.ac_conf_count               = 4,
		.ac_conf                     = {
			[0] = {
				.ac          = CONF_TX_AC_BE,
				.cw_min      = 15,
				.cw_max      = 63,
				.aifsn       = 3,
				.tx_op_limit = 0,
			},
			[1] = {
				.ac          = CONF_TX_AC_BK,
				.cw_min      = 15,
				.cw_max      = 63,
				.aifsn       = 7,
				.tx_op_limit = 0,
			},
			[2] = {
				.ac          = CONF_TX_AC_VI,
				.cw_min      = 15,
				.cw_max      = 63,
				.aifsn       = CONF_TX_AIFS_PIFS,
				.tx_op_limit = 3008,
			},
			[3] = {
				.ac          = CONF_TX_AC_VO,
				.cw_min      = 15,
				.cw_max      = 63,
				.aifsn       = CONF_TX_AIFS_PIFS,
				.tx_op_limit = 1504,
			},
		},
		.tid_conf_count = 7,
		.tid_conf = {
			[0] = {
				.queue_id    = 0,
				.channel_type = CONF_CHANNEL_TYPE_DCF,
				.tsid        = CONF_TX_AC_BE,
				.ps_scheme   = CONF_PS_SCHEME_LEGACY,
				.ack_policy  = CONF_ACK_POLICY_LEGACY,
				.apsd_conf   = {0, 0},
			},
			[1] = {
				.queue_id    = 1,
				.channel_type = CONF_CHANNEL_TYPE_DCF,
				.tsid        = CONF_TX_AC_BE,
				.ps_scheme   = CONF_PS_SCHEME_LEGACY,
				.ack_policy  = CONF_ACK_POLICY_LEGACY,
				.apsd_conf   = {0, 0},
			},
			[2] = {
				.queue_id    = 2,
				.channel_type = CONF_CHANNEL_TYPE_DCF,
				.tsid        = CONF_TX_AC_BE,
				.ps_scheme   = CONF_PS_SCHEME_LEGACY,
				.ack_policy  = CONF_ACK_POLICY_LEGACY,
				.apsd_conf   = {0, 0},
			},
			[3] = {
				.queue_id    = 3,
				.channel_type = CONF_CHANNEL_TYPE_DCF,
				.tsid        = CONF_TX_AC_BE,
				.ps_scheme   = CONF_PS_SCHEME_LEGACY,
				.ack_policy  = CONF_ACK_POLICY_LEGACY,
				.apsd_conf   = {0, 0},
			},
			[4] = {
				.queue_id    = 4,
				.channel_type = CONF_CHANNEL_TYPE_DCF,
				.tsid        = CONF_TX_AC_BE,
				.ps_scheme   = CONF_PS_SCHEME_LEGACY,
				.ack_policy  = CONF_ACK_POLICY_LEGACY,
				.apsd_conf   = {0, 0},
			},
			[5] = {
				.queue_id    = 5,
				.channel_type = CONF_CHANNEL_TYPE_DCF,
				.tsid        = CONF_TX_AC_BE,
				.ps_scheme   = CONF_PS_SCHEME_LEGACY,
				.ack_policy  = CONF_ACK_POLICY_LEGACY,
				.apsd_conf   = {0, 0},
			},
			[6] = {
				.queue_id    = 6,
				.channel_type = CONF_CHANNEL_TYPE_DCF,
				.tsid        = CONF_TX_AC_BE,
				.ps_scheme   = CONF_PS_SCHEME_LEGACY,
				.ack_policy  = CONF_ACK_POLICY_LEGACY,
				.apsd_conf   = {0, 0},
			}
		},
		.frag_threshold              = IEEE80211_MAX_FRAG_THRESHOLD,
		.tx_compl_timeout            = 700,
		.tx_compl_threshold          = 4,
		.basic_rate                  = CONF_HW_BIT_RATE_1MBPS,
		.basic_rate_5                = CONF_HW_BIT_RATE_6MBPS,
	},
	.conn = {
		.wake_up_event               = CONF_WAKE_UP_EVENT_DTIM,
		.listen_interval             = 1,
		.bcn_filt_mode               = CONF_BCN_FILT_MODE_ENABLED,
		.bcn_filt_ie_count           = 1,
		.bcn_filt_ie = {
			[0] = {
				.ie          = WLAN_EID_CHANNEL_SWITCH,
				.rule        = CONF_BCN_RULE_PASS_ON_APPEARANCE,
			}
		},
		.synch_fail_thold            = 10,
		.bss_lose_timeout            = 100,
		.beacon_rx_timeout           = 10000,
		.broadcast_timeout           = 20000,
		.rx_broadcast_in_ps          = 1,
		.ps_poll_threshold           = 10,
		.ps_poll_recovery_period     = 700,
		.bet_enable                  = CONF_BET_MODE_ENABLE,
		.bet_max_consecutive         = 10,
		.psm_entry_retries           = 3,
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
		/* FIXME: due to firmware bug, must use value 1 for now */
		.trigger_pacing               = 1,
		.avg_weight_rssi_beacon       = 20,
		.avg_weight_rssi_data         = 10,
		.avg_weight_snr_beacon        = 20,
		.avg_weight_snr_data          = 10
	}
};

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
	list_for_each_entry(wl, &wl_list, list) {
		if (wl == wl_temp)
			break;
	}
	if (wl != wl_temp)
		return NOTIFY_DONE;

	mutex_lock(&wl->mutex);

	if (wl->state == WL1271_STATE_OFF)
		goto out;

	if (!test_bit(WL1271_FLAG_STA_ASSOCIATED, &wl->flags))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl, false);
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

	ret = wl1271_cmd_general_parms(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_radio_parms(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_init_templates_config(wl);
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

	/* Energy detection */
	ret = wl1271_init_energy_detection(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Default fragmentation threshold */
	ret = wl1271_acx_frag_threshold(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Default TID configuration */
	for (i = 0; i < wl->conf.tx.tid_conf_count; i++) {
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

	/* Default AC configuration */
	for (i = 0; i < wl->conf.tx.ac_conf_count; i++) {
		conf_ac = &wl->conf.tx.ac_conf[i];
		ret = wl1271_acx_ac_cfg(wl, conf_ac->ac, conf_ac->cw_min,
					conf_ac->cw_max, conf_ac->aifsn,
					conf_ac->tx_op_limit);
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

static void wl1271_fw_status(struct wl1271 *wl,
			     struct wl1271_fw_status *status)
{
	struct timespec ts;
	u32 total = 0;
	int i;

	wl1271_raw_read(wl, FW_STATUS_ADDR, status, sizeof(*status), false);

	wl1271_debug(DEBUG_IRQ, "intr: 0x%x (fw_rx_counter = %d, "
		     "drv_rx_counter = %d, tx_results_counter = %d)",
		     status->intr,
		     status->fw_rx_counter,
		     status->drv_rx_counter,
		     status->tx_results_counter);

	/* update number of available TX blocks */
	for (i = 0; i < NUM_TX_QUEUES; i++) {
		u32 cnt = le32_to_cpu(status->tx_released_blks[i]) -
			wl->tx_blocks_freed[i];

		wl->tx_blocks_freed[i] =
			le32_to_cpu(status->tx_released_blks[i]);
		wl->tx_blocks_available += cnt;
		total += cnt;
	}

	/* if more blocks are available now, schedule some tx work */
	if (total && !skb_queue_empty(&wl->tx_queue))
		ieee80211_queue_work(wl->hw, &wl->tx_work);

	/* update the host-chipset time offset */
	getnstimeofday(&ts);
	wl->time_offset = (timespec_to_ns(&ts) >> 10) -
		(s64)le32_to_cpu(status->fw_localtime);
}

#define WL1271_IRQ_MAX_LOOPS 10

static void wl1271_irq_work(struct work_struct *work)
{
	int ret;
	u32 intr;
	int loopcount = WL1271_IRQ_MAX_LOOPS;
	unsigned long flags;
	struct wl1271 *wl =
		container_of(work, struct wl1271, irq_work);

	mutex_lock(&wl->mutex);

	wl1271_debug(DEBUG_IRQ, "IRQ work");

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl, true);
	if (ret < 0)
		goto out;

	spin_lock_irqsave(&wl->wl_lock, flags);
	while (test_bit(WL1271_FLAG_IRQ_PENDING, &wl->flags) && loopcount) {
		clear_bit(WL1271_FLAG_IRQ_PENDING, &wl->flags);
		spin_unlock_irqrestore(&wl->wl_lock, flags);
		loopcount--;

		wl1271_fw_status(wl, wl->fw_status);
		intr = le32_to_cpu(wl->fw_status->intr);
		if (!intr) {
			wl1271_debug(DEBUG_IRQ, "Zero interrupt received.");
			spin_lock_irqsave(&wl->wl_lock, flags);
			continue;
		}

		intr &= WL1271_INTR_MASK;

		if (intr & WL1271_ACX_INTR_DATA) {
			wl1271_debug(DEBUG_IRQ, "WL1271_ACX_INTR_DATA");

			/* check for tx results */
			if (wl->fw_status->tx_results_counter !=
			    (wl->tx_results_count & 0xff))
				wl1271_tx_complete(wl);

			wl1271_rx(wl, wl->fw_status);
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

		spin_lock_irqsave(&wl->wl_lock, flags);
	}

	if (test_bit(WL1271_FLAG_IRQ_PENDING, &wl->flags))
		ieee80211_queue_work(wl->hw, &wl->irq_work);
	else
		clear_bit(WL1271_FLAG_IRQ_RUNNING, &wl->flags);
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);
}

static int wl1271_fetch_firmware(struct wl1271 *wl)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, WL1271_FW_NAME, wl1271_wl_to_dev(wl));

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

	wl->fw_len = fw->size;
	wl->fw = vmalloc(wl->fw_len);

	if (!wl->fw) {
		wl1271_error("could not allocate memory for the firmware");
		ret = -ENOMEM;
		goto out;
	}

	memcpy(wl->fw, fw->data, wl->fw_len);

	ret = 0;

out:
	release_firmware(fw);

	return ret;
}

static int wl1271_fetch_nvs(struct wl1271 *wl)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, WL1271_NVS_NAME, wl1271_wl_to_dev(wl));

	if (ret < 0) {
		wl1271_error("could not get nvs file: %d", ret);
		return ret;
	}

	/*
	 * FIXME: the LEGACY NVS image support (NVS's missing the 5GHz band
	 * configurations) can be removed when those NVS files stop floating
	 * around.
	 */
	if (fw->size != sizeof(struct wl1271_nvs_file) &&
	    (fw->size != WL1271_INI_LEGACY_NVS_FILE_SIZE ||
	     wl1271_11a_enabled())) {
		wl1271_error("nvs size is not as expected: %zu != %zu",
			     fw->size, sizeof(struct wl1271_nvs_file));
		ret = -EILSEQ;
		goto out;
	}

	wl->nvs = kmemdup(fw->data, sizeof(struct wl1271_nvs_file), GFP_KERNEL);

	if (!wl->nvs) {
		wl1271_error("could not allocate memory for the nvs file");
		ret = -ENOMEM;
		goto out;
	}

out:
	release_firmware(fw);

	return ret;
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

	INIT_WORK(&wl->irq_work, wl1271_irq_work);
	INIT_WORK(&wl->tx_work, wl1271_tx_work);
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

		ret = wl1271_setup(wl);
		if (ret < 0)
			goto out;
		break;
	default:
		wl1271_warning("unsupported chip id: 0x%x", wl->chip.id);
		ret = -ENODEV;
		goto out;
	}

	if (wl->fw == NULL) {
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
			      wl->chip.fw_ver);
		goto out;

irq_disable:
		wl1271_disable_interrupts(wl);
		mutex_unlock(&wl->mutex);
		/* Unlocking the mutex in the middle of handling is
		   inherently unsafe. In this case we deem it safe to do,
		   because we need to let any possibly pending IRQ out of
		   the system (and while we are WL1271_STATE_OFF the IRQ
		   work function will not do anything.) Also, any other
		   possible concurrent operations will fail due to the
		   current state, hence the wl1271 struct should be safe. */
		cancel_work_sync(&wl->irq_work);
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

int wl1271_plt_stop(struct wl1271 *wl)
{
	int ret = 0;

	mutex_lock(&wl->mutex);

	wl1271_notice("power down");

	if (wl->state != WL1271_STATE_PLT) {
		wl1271_error("cannot power down because not in PLT "
			     "state: %d", wl->state);
		ret = -EBUSY;
		goto out;
	}

	wl1271_disable_interrupts(wl);
	wl1271_power_off(wl);

	wl->state = WL1271_STATE_OFF;
	wl->rx_counter = 0;

out:
	mutex_unlock(&wl->mutex);

	return ret;
}


static int wl1271_op_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct wl1271 *wl = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	struct ieee80211_tx_info *txinfo = IEEE80211_SKB_CB(skb);
	struct ieee80211_sta *sta = txinfo->control.sta;
	unsigned long flags;

	/* peek into the rates configured in the STA entry */
	spin_lock_irqsave(&wl->wl_lock, flags);
	if (sta && sta->supp_rates[conf->channel->band] != wl->sta_rate_set) {
		wl->sta_rate_set = sta->supp_rates[conf->channel->band];
		set_bit(WL1271_FLAG_STA_RATES_CHANGED, &wl->flags);
	}
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	/* queue the packet */
	skb_queue_tail(&wl->tx_queue, skb);

	/*
	 * The chip specific setup must run before the first TX packet -
	 * before that, the tx_work will not be initialized!
	 */

	ieee80211_queue_work(wl->hw, &wl->tx_work);

	/*
	 * The workqueue is slow to process the tx_queue and we need stop
	 * the queue here, otherwise the queue will get too long.
	 */
	if (skb_queue_len(&wl->tx_queue) >= WL1271_TX_QUEUE_HIGH_WATERMARK) {
		wl1271_debug(DEBUG_TX, "op_tx: stopping queues");

		spin_lock_irqsave(&wl->wl_lock, flags);
		ieee80211_stop_queues(wl->hw);
		set_bit(WL1271_FLAG_TX_QUEUE_STOPPED, &wl->flags);
		spin_unlock_irqrestore(&wl->wl_lock, flags);
	}

	return NETDEV_TX_OK;
}

static struct notifier_block wl1271_dev_notifier = {
	.notifier_call = wl1271_dev_notify,
};

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

	wl1271_debug(DEBUG_MAC80211, "mac80211 add interface type %d mac %pM",
		     vif->type, vif->addr);

	mutex_lock(&wl->mutex);
	if (wl->vif) {
		ret = -EBUSY;
		goto out;
	}

	wl->vif = vif;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		wl->bss_type = BSS_TYPE_STA_BSS;
		wl->set_bss_type = BSS_TYPE_STA_BSS;
		break;
	case NL80211_IFTYPE_ADHOC:
		wl->bss_type = BSS_TYPE_IBSS;
		wl->set_bss_type = BSS_TYPE_STA_BSS;
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

		wl->state = WL1271_STATE_ON;
		wl1271_info("firmware booted (%s)", wl->chip.fw_ver);

		/* update hw/fw version info in wiphy struct */
		wiphy->hw_version = wl->chip.id;
		strncpy(wiphy->fw_version, wl->chip.fw_ver,
			sizeof(wiphy->fw_version));

		goto out;

irq_disable:
		wl1271_disable_interrupts(wl);
		mutex_unlock(&wl->mutex);
		/* Unlocking the mutex in the middle of handling is
		   inherently unsafe. In this case we deem it safe to do,
		   because we need to let any possibly pending IRQ out of
		   the system (and while we are WL1271_STATE_OFF the IRQ
		   work function will not do anything.) Also, any other
		   possible concurrent operations will fail due to the
		   current state, hence the wl1271 struct should be safe. */
		cancel_work_sync(&wl->irq_work);
		mutex_lock(&wl->mutex);
power_off:
		wl1271_power_off(wl);
	}

	wl1271_error("firmware boot failed despite %d retries",
		     WL1271_BOOT_RETRIES);
out:
	mutex_unlock(&wl->mutex);

	if (!ret)
		list_add(&wl->list, &wl_list);

	return ret;
}

static void wl1271_op_remove_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct wl1271 *wl = hw->priv;
	int i;

	mutex_lock(&wl->mutex);
	wl1271_debug(DEBUG_MAC80211, "mac80211 remove interface");

	wl1271_info("down");

	list_del(&wl->list);

	WARN_ON(wl->state != WL1271_STATE_ON);

	/* enable dyn ps just in case (if left on due to fw crash etc) */
	if (wl->bss_type == BSS_TYPE_STA_BSS)
		ieee80211_enable_dyn_ps(wl->vif);

	if (wl->scan.state != WL1271_SCAN_STATE_IDLE) {
		ieee80211_scan_completed(wl->hw, true);
		wl->scan.state = WL1271_SCAN_STATE_IDLE;
		kfree(wl->scan.scanned_ch);
		wl->scan.scanned_ch = NULL;
	}

	wl->state = WL1271_STATE_OFF;

	wl1271_disable_interrupts(wl);

	mutex_unlock(&wl->mutex);

	cancel_work_sync(&wl->irq_work);
	cancel_work_sync(&wl->tx_work);
	cancel_delayed_work_sync(&wl->pspoll_work);

	mutex_lock(&wl->mutex);

	/* let's notify MAC80211 about the remaining pending TX frames */
	wl1271_tx_reset(wl);
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
	wl->tx_results_count = 0;
	wl->tx_packets_count = 0;
	wl->tx_security_last_seq = 0;
	wl->tx_security_seq = 0;
	wl->time_offset = 0;
	wl->session_counter = 0;
	wl->rate_set = CONF_TX_RATE_MASK_BASIC;
	wl->sta_rate_set = 0;
	wl->flags = 0;
	wl->vif = NULL;
	wl->filters = 0;

	for (i = 0; i < NUM_TX_QUEUES; i++)
		wl->tx_blocks_freed[i] = 0;

	wl1271_debugfs_reset(wl);

	kfree(wl->fw_status);
	wl->fw_status = NULL;
	kfree(wl->tx_res_if);
	wl->tx_res_if = NULL;
	kfree(wl->target_mem_map);
	wl->target_mem_map = NULL;

	mutex_unlock(&wl->mutex);
}

static void wl1271_configure_filters(struct wl1271 *wl, unsigned int filters)
{
	wl->rx_config = WL1271_DEFAULT_RX_CONFIG;
	wl->rx_filter = WL1271_DEFAULT_RX_FILTER;

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
	 * Currently there is no supported scenario for JOIN during
	 * association - if it becomes a supported scenario, the WPA/WPA2 keys
	 * must be handled somehow.
	 *
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

	/* stop filterting packets based on bssid */
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

static u32 wl1271_min_rate_get(struct wl1271 *wl)
{
	int i;
	u32 rate = 0;

	if (!wl->basic_rate_set) {
		WARN_ON(1);
		wl->basic_rate_set = wl->conf.tx.basic_rate;
	}

	for (i = 0; !rate; i++) {
		if ((wl->basic_rate_set >> i) & 0x1)
			rate = 1 << i;
	}

	return rate;
}

static int wl1271_handle_idle(struct wl1271 *wl, bool idle)
{
	int ret;

	if (idle) {
		if (test_bit(WL1271_FLAG_JOINED, &wl->flags)) {
			ret = wl1271_unjoin(wl);
			if (ret < 0)
				goto out;
		}
		wl->rate_set = wl1271_min_rate_get(wl);
		wl->sta_rate_set = 0;
		ret = wl1271_acx_rate_policies(wl);
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

	channel = ieee80211_frequency_to_channel(conf->channel->center_freq);

	wl1271_debug(DEBUG_MAC80211, "mac80211 config ch %d psm %s power %d %s",
		     channel,
		     conf->flags & IEEE80211_CONF_PS ? "on" : "off",
		     conf->power_level,
		     conf->flags & IEEE80211_CONF_IDLE ? "idle" : "in use");

	/*
	 * mac80211 will go to idle nearly immediately after transmitting some
	 * frames, such as the deauth. To make sure those frames reach the air,
	 * wait here until the TX queue is fully flushed.
	 */
	if ((changed & IEEE80211_CONF_CHANGE_IDLE) &&
	    (conf->flags & IEEE80211_CONF_IDLE))
		wl1271_tx_flush(wl);

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl, false);
	if (ret < 0)
		goto out;

	/* if the channel changes while joined, join again */
	if (changed & IEEE80211_CONF_CHANGE_CHANNEL &&
	    ((wl->band != conf->channel->band) ||
	     (wl->channel != channel))) {
		wl->band = conf->channel->band;
		wl->channel = channel;

		/*
		 * FIXME: the mac80211 should really provide a fixed rate
		 * to use here. for now, just use the smallest possible rate
		 * for the band as a fixed rate for association frames and
		 * other control messages.
		 */
		if (!test_bit(WL1271_FLAG_STA_ASSOCIATED, &wl->flags))
			wl1271_set_band_rate(wl);

		wl->basic_rate = wl1271_min_rate_get(wl);
		ret = wl1271_acx_rate_policies(wl);
		if (ret < 0)
			wl1271_warning("rate policy for update channel "
				       "failed %d", ret);

		if (test_bit(WL1271_FLAG_JOINED, &wl->flags)) {
			ret = wl1271_join(wl, false);
			if (ret < 0)
				wl1271_warning("cmd join to update channel "
					       "failed %d", ret);
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_IDLE) {
		ret = wl1271_handle_idle(wl, conf->flags & IEEE80211_CONF_IDLE);
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
						 true);
		}
	} else if (!(conf->flags & IEEE80211_CONF_PS) &&
		   test_bit(WL1271_FLAG_PSM_REQUESTED, &wl->flags)) {
		wl1271_debug(DEBUG_PSM, "psm disabled");

		clear_bit(WL1271_FLAG_PSM_REQUESTED, &wl->flags);

		if (test_bit(WL1271_FLAG_PSM, &wl->flags))
			ret = wl1271_ps_set_mode(wl, STATION_ACTIVE_MODE,
						 true);
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

	wl1271_debug(DEBUG_MAC80211, "mac80211 configure filter");

	mutex_lock(&wl->mutex);

	*total &= WL1271_SUPPORTED_FILTERS;
	changed &= WL1271_SUPPORTED_FILTERS;

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl, false);
	if (ret < 0)
		goto out;


	if (*total & FIF_ALLMULTI)
		ret = wl1271_acx_group_address_tbl(wl, false, NULL, 0);
	else if (fp)
		ret = wl1271_acx_group_address_tbl(wl, fp->enabled,
						   fp->mc_list,
						   fp->mc_list_length);
	if (ret < 0)
		goto out_sleep;

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

static int wl1271_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct ieee80211_key_conf *key_conf)
{
	struct wl1271 *wl = hw->priv;
	const u8 *addr;
	int ret;
	u32 tx_seq_32 = 0;
	u16 tx_seq_16 = 0;
	u8 key_type;

	static const u8 bcast_addr[ETH_ALEN] =
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	wl1271_debug(DEBUG_MAC80211, "mac80211 set key");

	addr = sta ? sta->addr : bcast_addr;

	wl1271_debug(DEBUG_CRYPT, "CMD: 0x%x", cmd);
	wl1271_dump(DEBUG_CRYPT, "ADDR: ", addr, ETH_ALEN);
	wl1271_debug(DEBUG_CRYPT, "Key: algo:0x%x, id:%d, len:%d flags 0x%x",
		     key_conf->cipher, key_conf->keyidx,
		     key_conf->keylen, key_conf->flags);
	wl1271_dump(DEBUG_CRYPT, "KEY: ", key_conf->key, key_conf->keylen);

	if (is_zero_ether_addr(addr)) {
		/* We dont support TX only encryption */
		ret = -EOPNOTSUPP;
		goto out;
	}

	mutex_lock(&wl->mutex);

	ret = wl1271_ps_elp_wakeup(wl, false);
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
	default:
		wl1271_error("Unknown key algo 0x%x", key_conf->cipher);

		ret = -EOPNOTSUPP;
		goto out_sleep;
	}

	switch (cmd) {
	case SET_KEY:
		ret = wl1271_cmd_set_key(wl, KEY_ADD_OR_REPLACE,
					 key_conf->keyidx, key_type,
					 key_conf->keylen, key_conf->key,
					 addr, tx_seq_32, tx_seq_16);
		if (ret < 0) {
			wl1271_error("Could not add or replace key");
			goto out_sleep;
		}

		/* the default WEP key needs to be configured at least once */
		if (key_type == KEY_WEP) {
			ret = wl1271_cmd_set_default_wep_key(wl,
							     wl->default_key);
			if (ret < 0)
				goto out_sleep;
		}
		break;

	case DISABLE_KEY:
		/* The wl1271 does not allow to remove unicast keys - they
		   will be cleared automatically on next CMD_JOIN. Ignore the
		   request silently, as we dont want the mac80211 to emit
		   an error message. */
		if (!is_broadcast_ether_addr(addr))
			break;

		ret = wl1271_cmd_set_key(wl, KEY_REMOVE,
					 key_conf->keyidx, key_type,
					 key_conf->keylen, key_conf->key,
					 addr, 0, 0);
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

out:
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

	ret = wl1271_ps_elp_wakeup(wl, false);
	if (ret < 0)
		goto out;

	if (wl1271_11a_enabled())
		ret = wl1271_scan(hw->priv, ssid, len, req);
	else
		ret = wl1271_scan(hw->priv, ssid, len, req);

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

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl, false);
	if (ret < 0)
		goto out;

	ret = wl1271_acx_rts_threshold(wl, (u16) value);
	if (ret < 0)
		wl1271_warning("wl1271_op_set_rts_threshold failed: %d", ret);

	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static void wl1271_ssid_set(struct wl1271 *wl, struct sk_buff *beacon)
{
	u8 *ptr = beacon->data +
		offsetof(struct ieee80211_mgmt, u.beacon.variable);

	/* find the location of the ssid in the beacon */
	while (ptr < beacon->data + beacon->len) {
		if (ptr[0] == WLAN_EID_SSID) {
			wl->ssid_len = ptr[1];
			memcpy(wl->ssid, ptr+2, wl->ssid_len);
			return;
		}
		ptr += ptr[1];
	}
	wl1271_error("ad-hoc beacon template has no SSID!\n");
}

static void wl1271_op_bss_info_changed(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u32 changed)
{
	enum wl1271_cmd_ps_mode mode;
	struct wl1271 *wl = hw->priv;
	bool do_join = false;
	bool set_assoc = false;
	int ret;

	wl1271_debug(DEBUG_MAC80211, "mac80211 bss info changed");

	mutex_lock(&wl->mutex);

	ret = wl1271_ps_elp_wakeup(wl, false);
	if (ret < 0)
		goto out;

	if ((changed & BSS_CHANGED_BEACON_INT) &&
	    (wl->bss_type == BSS_TYPE_IBSS)) {
		wl1271_debug(DEBUG_ADHOC, "ad-hoc beacon interval updated: %d",
			bss_conf->beacon_int);

		wl->beacon_int = bss_conf->beacon_int;
		do_join = true;
	}

	if ((changed & BSS_CHANGED_BEACON) &&
	    (wl->bss_type == BSS_TYPE_IBSS)) {
		struct sk_buff *beacon = ieee80211_beacon_get(hw, vif);

		wl1271_debug(DEBUG_ADHOC, "ad-hoc beacon updated");

		if (beacon) {
			struct ieee80211_hdr *hdr;

			wl1271_ssid_set(wl, beacon);
			ret = wl1271_cmd_template_set(wl, CMD_TEMPL_BEACON,
						      beacon->data,
						      beacon->len, 0,
						      wl1271_min_rate_get(wl));

			if (ret < 0) {
				dev_kfree_skb(beacon);
				goto out_sleep;
			}

			hdr = (struct ieee80211_hdr *) beacon->data;
			hdr->frame_control = cpu_to_le16(
				IEEE80211_FTYPE_MGMT |
				IEEE80211_STYPE_PROBE_RESP);

			ret = wl1271_cmd_template_set(wl,
						      CMD_TEMPL_PROBE_RESPONSE,
						      beacon->data,
						      beacon->len, 0,
						      wl1271_min_rate_get(wl));
			dev_kfree_skb(beacon);
			if (ret < 0)
				goto out_sleep;

			/* Need to update the SSID (for filtering etc) */
			do_join = true;
		}
	}

	if ((changed & BSS_CHANGED_BEACON_ENABLED) &&
	    (wl->bss_type == BSS_TYPE_IBSS)) {
		wl1271_debug(DEBUG_ADHOC, "ad-hoc beaconing: %s",
			     bss_conf->enable_beacon ? "enabled" : "disabled");

		if (bss_conf->enable_beacon)
			wl->set_bss_type = BSS_TYPE_IBSS;
		else
			wl->set_bss_type = BSS_TYPE_STA_BSS;
		do_join = true;
	}

	if (changed & BSS_CHANGED_CQM) {
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

			ret = wl1271_cmd_build_null_data(wl);
			if (ret < 0)
				goto out_sleep;

			ret = wl1271_build_qos_null_data(wl);
			if (ret < 0)
				goto out_sleep;

			/* filter out all packets not from this BSSID */
			wl1271_configure_filters(wl, 0);

			/* Need to update the BSSID (for filtering etc) */
			do_join = true;
	}

	if (changed & BSS_CHANGED_ASSOC) {
		if (bss_conf->assoc) {
			u32 rates;
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
			wl->basic_rate = wl1271_min_rate_get(wl);
			ret = wl1271_acx_rate_policies(wl);
			if (ret < 0)
				goto out_sleep;

			/*
			 * with wl1271, we don't need to update the
			 * beacon_int and dtim_period, because the firmware
			 * updates it by itself when the first beacon is
			 * received after a join.
			 */
			ret = wl1271_cmd_build_ps_poll(wl, wl->aid);
			if (ret < 0)
				goto out_sleep;

			/*
			 * The SSID is intentionally set to NULL here - the
			 * firmware will set the probe request with a
			 * broadcast SSID regardless of what we set in the
			 * template.
			 */
			ret = wl1271_cmd_build_probe_req(wl, NULL, 0,
							 NULL, 0, wl->band);

			/* enable the connection monitoring feature */
			ret = wl1271_acx_conn_monit_params(wl, true);
			if (ret < 0)
				goto out_sleep;

			/* If we want to go in PSM but we're not there yet */
			if (test_bit(WL1271_FLAG_PSM_REQUESTED, &wl->flags) &&
			    !test_bit(WL1271_FLAG_PSM, &wl->flags)) {
				mode = STATION_POWER_SAVE_MODE;
				ret = wl1271_ps_set_mode(wl, mode, true);
				if (ret < 0)
					goto out_sleep;
			}
		} else {
			/* use defaults when not associated */
			clear_bit(WL1271_FLAG_STA_STATE_SENT, &wl->flags);
			clear_bit(WL1271_FLAG_STA_ASSOCIATED, &wl->flags);
			wl->aid = 0;

			/* re-enable dynamic ps - just in case */
			ieee80211_enable_dyn_ps(wl->vif);

			/* revert back to minimum rates for the current band */
			wl1271_set_band_rate(wl);
			wl->basic_rate = wl1271_min_rate_get(wl);
			ret = wl1271_acx_rate_policies(wl);
			if (ret < 0)
				goto out_sleep;

			/* disable connection monitor features */
			ret = wl1271_acx_conn_monit_params(wl, false);

			/* Disable the keep-alive feature */
			ret = wl1271_acx_keep_alive_mode(wl, false);

			if (ret < 0)
				goto out_sleep;
		}

	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		if (bss_conf->use_short_slot)
			ret = wl1271_acx_slot(wl, SLOT_TIME_SHORT);
		else
			ret = wl1271_acx_slot(wl, SLOT_TIME_LONG);
		if (ret < 0) {
			wl1271_warning("Set slot time failed %d", ret);
			goto out_sleep;
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
			goto out_sleep;
		}
	}

	if (changed & BSS_CHANGED_ARP_FILTER) {
		__be32 addr = bss_conf->arp_addr_list[0];
		WARN_ON(wl->bss_type != BSS_TYPE_STA_BSS);

		if (bss_conf->arp_addr_cnt == 1 && bss_conf->arp_filter_enabled)
			ret = wl1271_acx_arp_ip_filter(wl, true, addr);
		else
			ret = wl1271_acx_arp_ip_filter(wl, false, addr);

		if (ret < 0)
			goto out_sleep;
	}

	if (do_join) {
		ret = wl1271_join(wl, set_assoc);
		if (ret < 0) {
			wl1271_warning("cmd join failed %d", ret);
			goto out_sleep;
		}
	}

out_sleep:
	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);
}

static int wl1271_op_conf_tx(struct ieee80211_hw *hw, u16 queue,
			     const struct ieee80211_tx_queue_params *params)
{
	struct wl1271 *wl = hw->priv;
	u8 ps_scheme;
	int ret;

	mutex_lock(&wl->mutex);

	wl1271_debug(DEBUG_MAC80211, "mac80211 conf tx %d", queue);

	ret = wl1271_ps_elp_wakeup(wl, false);
	if (ret < 0)
		goto out;

	/* the txop is confed in units of 32us by the mac80211, we need us */
	ret = wl1271_acx_ac_cfg(wl, wl1271_tx_get_queue(queue),
				params->cw_min, params->cw_max,
				params->aifs, params->txop << 5);
	if (ret < 0)
		goto out_sleep;

	if (params->uapsd)
		ps_scheme = CONF_PS_SCHEME_UPSD_TRIGGER;
	else
		ps_scheme = CONF_PS_SCHEME_LEGACY;

	ret = wl1271_acx_tid_cfg(wl, wl1271_tx_get_queue(queue),
				 CONF_CHANNEL_TYPE_EDCF,
				 wl1271_tx_get_queue(queue),
				 ps_scheme, CONF_ACK_POLICY_LEGACY, 0, 0);
	if (ret < 0)
		goto out_sleep;

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

	ret = wl1271_ps_elp_wakeup(wl, false);
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
};

/* mapping to indexes for wl1271_rates */
static const u8 wl1271_rate_to_idx_2ghz[] = {
	/* MCS rates are used only with 11n */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS7 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS6 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS5 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS4 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS3 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS2 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS1 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS0 */

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

/* can't be const, mac80211 writes to this */
static struct ieee80211_supported_band wl1271_band_2ghz = {
	.channels = wl1271_channels,
	.n_channels = ARRAY_SIZE(wl1271_channels),
	.bitrates = wl1271_rates,
	.n_bitrates = ARRAY_SIZE(wl1271_rates),
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
	{ .hw_value = 183, .center_freq = 4915},
	{ .hw_value = 184, .center_freq = 4920},
	{ .hw_value = 185, .center_freq = 4925},
	{ .hw_value = 187, .center_freq = 4935},
	{ .hw_value = 188, .center_freq = 4940},
	{ .hw_value = 189, .center_freq = 4945},
	{ .hw_value = 192, .center_freq = 4960},
	{ .hw_value = 196, .center_freq = 4980},
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
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS7 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS6 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS5 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS4 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS3 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS2 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS1 */
	CONF_HW_RXTX_RATE_UNSUPPORTED, /* CONF_HW_RXTX_RATE_MCS0 */

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
	.config = wl1271_op_config,
	.prepare_multicast = wl1271_op_prepare_multicast,
	.configure_filter = wl1271_op_configure_filter,
	.tx = wl1271_op_tx,
	.set_key = wl1271_op_set_key,
	.hw_scan = wl1271_op_hw_scan,
	.bss_info_changed = wl1271_op_bss_info_changed,
	.set_rts_threshold = wl1271_op_set_rts_threshold,
	.conf_tx = wl1271_op_conf_tx,
	.get_tsf = wl1271_op_get_tsf,
	.get_survey = wl1271_op_get_survey,
	CFG80211_TESTMODE_CMD(wl1271_tm_cmd)
};


u8 wl1271_rate_to_idx(struct wl1271 *wl, int rate)
{
	u8 idx;

	BUG_ON(wl->band >= sizeof(wl1271_band_rate_to_idx)/sizeof(u8 *));

	if (unlikely(rate >= CONF_HW_RXTX_RATE_MAX)) {
		wl1271_error("Illegal RX rate from HW: %d", rate);
		return 0;
	}

	idx = wl1271_band_rate_to_idx[wl->band][rate];
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

	/* FIXME: what's the maximum length of buf? page size?*/
	len = 500;

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

	ret = strict_strtoul(buf, 10, &res);

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

	ret = wl1271_ps_elp_wakeup(wl, false);
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

	/* FIXME: what's the maximum length of buf? page size?*/
	len = 500;

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

	SET_IEEE80211_PERM_ADDR(wl->hw, wl->mac_addr);

	ret = ieee80211_register_hw(wl->hw);
	if (ret < 0) {
		wl1271_error("unable to register mac80211 hw: %d", ret);
		return ret;
	}

	wl->mac80211_registered = true;

	register_netdevice_notifier(&wl1271_dev_notifier);

	wl1271_notice("loaded");

	return 0;
}
EXPORT_SYMBOL_GPL(wl1271_register_hw);

void wl1271_unregister_hw(struct wl1271 *wl)
{
	unregister_netdevice_notifier(&wl1271_dev_notifier);
	ieee80211_unregister_hw(wl->hw);
	wl->mac80211_registered = false;

}
EXPORT_SYMBOL_GPL(wl1271_unregister_hw);

int wl1271_init_ieee80211(struct wl1271 *wl)
{
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
		IEEE80211_HW_SUPPORTS_CQM_RSSI;

	wl->hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC);
	wl->hw->wiphy->max_scan_ssids = 1;
	wl->hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &wl1271_band_2ghz;

	if (wl1271_11a_enabled())
		wl->hw->wiphy->bands[IEEE80211_BAND_5GHZ] = &wl1271_band_5ghz;

	wl->hw->queues = 4;
	wl->hw->max_rates = 1;

	SET_IEEE80211_DEV(wl->hw, wl1271_wl_to_dev(wl));

	return 0;
}
EXPORT_SYMBOL_GPL(wl1271_init_ieee80211);

#define WL1271_DEFAULT_CHANNEL 0

struct ieee80211_hw *wl1271_alloc_hw(void)
{
	struct ieee80211_hw *hw;
	struct platform_device *plat_dev = NULL;
	struct wl1271 *wl;
	int i, ret;

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

	skb_queue_head_init(&wl->tx_queue);

	INIT_DELAYED_WORK(&wl->elp_work, wl1271_elp_work);
	INIT_DELAYED_WORK(&wl->pspoll_work, wl1271_pspoll_work);
	wl->channel = WL1271_DEFAULT_CHANNEL;
	wl->beacon_int = WL1271_DEFAULT_BEACON_INT;
	wl->default_key = 0;
	wl->rx_counter = 0;
	wl->rx_config = WL1271_DEFAULT_RX_CONFIG;
	wl->rx_filter = WL1271_DEFAULT_RX_FILTER;
	wl->psm_entry_retry = 0;
	wl->power_level = WL1271_DEFAULT_POWER_LEVEL;
	wl->basic_rate_set = CONF_TX_RATE_MASK_BASIC;
	wl->basic_rate = CONF_TX_RATE_MASK_BASIC;
	wl->rate_set = CONF_TX_RATE_MASK_BASIC;
	wl->sta_rate_set = 0;
	wl->band = IEEE80211_BAND_2GHZ;
	wl->vif = NULL;
	wl->flags = 0;
	wl->sg_enabled = true;
	wl->hw_pg_ver = -1;

	for (i = 0; i < ACX_TX_DESCRIPTORS; i++)
		wl->tx_frames[i] = NULL;

	spin_lock_init(&wl->wl_lock);

	wl->state = WL1271_STATE_OFF;
	mutex_init(&wl->mutex);

	/* Apply default driver configuration. */
	wl1271_conf_init(wl);

	wl1271_debugfs_init(wl);

	/* Register platform device */
	ret = platform_device_register(wl->plat_dev);
	if (ret) {
		wl1271_error("couldn't register platform device");
		goto err_hw;
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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luciano Coelho <luciano.coelho@nokia.com>");
MODULE_AUTHOR("Juuso Oikarinen <juuso.oikarinen@nokia.com>");
