/*
 * mac80211 glue code for mac80211 ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * Based on:
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2007-2009, Christian Lamparter <chunkeey@web.de>
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * Based on:
 * - the islsm (softmac prism54) driver, which is:
 *   Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 * - stlc45xx driver
 *   Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <net/mac80211.h>

#include "cw1200.h"
#include "txrx.h"
#include "hwbus.h"
#include "fwio.h"
#include "hwio.h"
#include "bh.h"
#include "sta.h"
#include "scan.h"
#include "debug.h"
#include "pm.h"

MODULE_AUTHOR("Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>");
MODULE_DESCRIPTION("Softmac ST-Ericsson CW1200 common code");
MODULE_LICENSE("GPL");
MODULE_ALIAS("cw1200_core");

/* Accept MAC address of the form macaddr=0x00,0x80,0xE1,0x30,0x40,0x50 */
static u8 cw1200_mac_template[ETH_ALEN] = {0x02, 0x80, 0xe1, 0x00, 0x00, 0x00};
module_param_array_named(macaddr, cw1200_mac_template, byte, NULL, S_IRUGO);
MODULE_PARM_DESC(macaddr, "Override platform_data MAC address");

static char *cw1200_sdd_path;
module_param(cw1200_sdd_path, charp, 0644);
MODULE_PARM_DESC(cw1200_sdd_path, "Override platform_data SDD file");
static int cw1200_refclk;
module_param(cw1200_refclk, int, 0644);
MODULE_PARM_DESC(cw1200_refclk, "Override platform_data reference clock");

int cw1200_power_mode = wsm_power_mode_quiescent;
module_param(cw1200_power_mode, int, 0644);
MODULE_PARM_DESC(cw1200_power_mode, "WSM power mode.  0 == active, 1 == doze, 2 == quiescent (default)");

#define RATETAB_ENT(_rate, _rateid, _flags)		\
	{						\
		.bitrate	= (_rate),		\
		.hw_value	= (_rateid),		\
		.flags		= (_flags),		\
	}

static struct ieee80211_rate cw1200_rates[] = {
	RATETAB_ENT(10,  0,   0),
	RATETAB_ENT(20,  1,   0),
	RATETAB_ENT(55,  2,   0),
	RATETAB_ENT(110, 3,   0),
	RATETAB_ENT(60,  6,  0),
	RATETAB_ENT(90,  7,  0),
	RATETAB_ENT(120, 8,  0),
	RATETAB_ENT(180, 9,  0),
	RATETAB_ENT(240, 10, 0),
	RATETAB_ENT(360, 11, 0),
	RATETAB_ENT(480, 12, 0),
	RATETAB_ENT(540, 13, 0),
};

static struct ieee80211_rate cw1200_mcs_rates[] = {
	RATETAB_ENT(65,  14, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(130, 15, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(195, 16, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(260, 17, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(390, 18, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(520, 19, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(585, 20, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(650, 21, IEEE80211_TX_RC_MCS),
};

#define cw1200_a_rates		(cw1200_rates + 4)
#define cw1200_a_rates_size	(ARRAY_SIZE(cw1200_rates) - 4)
#define cw1200_g_rates		(cw1200_rates + 0)
#define cw1200_g_rates_size	(ARRAY_SIZE(cw1200_rates))
#define cw1200_n_rates		(cw1200_mcs_rates)
#define cw1200_n_rates_size	(ARRAY_SIZE(cw1200_mcs_rates))


#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= IEEE80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel, _flags) {				\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq	= 5000 + (5 * (_channel)),		\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

static struct ieee80211_channel cw1200_2ghz_chantable[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

static struct ieee80211_channel cw1200_5ghz_chantable[] = {
	CHAN5G(34, 0),		CHAN5G(36, 0),
	CHAN5G(38, 0),		CHAN5G(40, 0),
	CHAN5G(42, 0),		CHAN5G(44, 0),
	CHAN5G(46, 0),		CHAN5G(48, 0),
	CHAN5G(52, 0),		CHAN5G(56, 0),
	CHAN5G(60, 0),		CHAN5G(64, 0),
	CHAN5G(100, 0),		CHAN5G(104, 0),
	CHAN5G(108, 0),		CHAN5G(112, 0),
	CHAN5G(116, 0),		CHAN5G(120, 0),
	CHAN5G(124, 0),		CHAN5G(128, 0),
	CHAN5G(132, 0),		CHAN5G(136, 0),
	CHAN5G(140, 0),		CHAN5G(149, 0),
	CHAN5G(153, 0),		CHAN5G(157, 0),
	CHAN5G(161, 0),		CHAN5G(165, 0),
	CHAN5G(184, 0),		CHAN5G(188, 0),
	CHAN5G(192, 0),		CHAN5G(196, 0),
	CHAN5G(200, 0),		CHAN5G(204, 0),
	CHAN5G(208, 0),		CHAN5G(212, 0),
	CHAN5G(216, 0),
};

static struct ieee80211_supported_band cw1200_band_2ghz = {
	.channels = cw1200_2ghz_chantable,
	.n_channels = ARRAY_SIZE(cw1200_2ghz_chantable),
	.bitrates = cw1200_g_rates,
	.n_bitrates = cw1200_g_rates_size,
	.ht_cap = {
		.cap = IEEE80211_HT_CAP_GRN_FLD |
			(1 << IEEE80211_HT_CAP_RX_STBC_SHIFT) |
			IEEE80211_HT_CAP_MAX_AMSDU,
		.ht_supported = 1,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE,
		.mcs = {
			.rx_mask[0] = 0xFF,
			.rx_highest = __cpu_to_le16(0x41),
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
};

static struct ieee80211_supported_band cw1200_band_5ghz = {
	.channels = cw1200_5ghz_chantable,
	.n_channels = ARRAY_SIZE(cw1200_5ghz_chantable),
	.bitrates = cw1200_a_rates,
	.n_bitrates = cw1200_a_rates_size,
	.ht_cap = {
		.cap = IEEE80211_HT_CAP_GRN_FLD |
			(1 << IEEE80211_HT_CAP_RX_STBC_SHIFT) |
			IEEE80211_HT_CAP_MAX_AMSDU,
		.ht_supported = 1,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE,
		.mcs = {
			.rx_mask[0] = 0xFF,
			.rx_highest = __cpu_to_le16(0x41),
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
};

static const unsigned long cw1200_ttl[] = {
	1 * HZ,	/* VO */
	2 * HZ,	/* VI */
	5 * HZ, /* BE */
	10 * HZ	/* BK */
};

static const struct ieee80211_ops cw1200_ops = {
	.start			= cw1200_start,
	.stop			= cw1200_stop,
	.add_interface		= cw1200_add_interface,
	.remove_interface	= cw1200_remove_interface,
	.change_interface	= cw1200_change_interface,
	.tx			= cw1200_tx,
	.hw_scan		= cw1200_hw_scan,
	.set_tim		= cw1200_set_tim,
	.sta_notify		= cw1200_sta_notify,
	.sta_add		= cw1200_sta_add,
	.sta_remove		= cw1200_sta_remove,
	.set_key		= cw1200_set_key,
	.set_rts_threshold	= cw1200_set_rts_threshold,
	.config			= cw1200_config,
	.bss_info_changed	= cw1200_bss_info_changed,
	.prepare_multicast	= cw1200_prepare_multicast,
	.configure_filter	= cw1200_configure_filter,
	.conf_tx		= cw1200_conf_tx,
	.get_stats		= cw1200_get_stats,
	.ampdu_action		= cw1200_ampdu_action,
	.flush			= cw1200_flush,
#ifdef CONFIG_PM
	.suspend		= cw1200_wow_suspend,
	.resume			= cw1200_wow_resume,
#endif
	/* Intentionally not offloaded:					*/
	/*.channel_switch	= cw1200_channel_switch,		*/
	/*.remain_on_channel	= cw1200_remain_on_channel,		*/
	/*.cancel_remain_on_channel = cw1200_cancel_remain_on_channel,	*/
};

static int cw1200_ba_rx_tids = -1;
static int cw1200_ba_tx_tids = -1;
module_param(cw1200_ba_rx_tids, int, 0644);
module_param(cw1200_ba_tx_tids, int, 0644);
MODULE_PARM_DESC(cw1200_ba_rx_tids, "Block ACK RX TIDs");
MODULE_PARM_DESC(cw1200_ba_tx_tids, "Block ACK TX TIDs");

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support cw1200_wowlan_support = {
	/* Support only for limited wowlan functionalities */
	.flags = WIPHY_WOWLAN_ANY | WIPHY_WOWLAN_DISCONNECT,
};
#endif


static struct ieee80211_hw *cw1200_init_common(const u8 *macaddr,
						const bool have_5ghz)
{
	int i, band;
	struct ieee80211_hw *hw;
	struct cw1200_common *priv;

	hw = ieee80211_alloc_hw(sizeof(struct cw1200_common), &cw1200_ops);
	if (!hw)
		return NULL;

	priv = hw->priv;
	priv->hw = hw;
	priv->hw_type = -1;
	priv->mode = NL80211_IFTYPE_UNSPECIFIED;
	priv->rates = cw1200_rates; /* TODO: fetch from FW */
	priv->mcs_rates = cw1200_n_rates;
	if (cw1200_ba_rx_tids != -1)
		priv->ba_rx_tid_mask = cw1200_ba_rx_tids;
	else
		priv->ba_rx_tid_mask = 0xFF; /* Enable RX BLKACK for all TIDs */
	if (cw1200_ba_tx_tids != -1)
		priv->ba_tx_tid_mask = cw1200_ba_tx_tids;
	else
		priv->ba_tx_tid_mask = 0xff; /* Enable TX BLKACK for all TIDs */

	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_SUPPORTS_PS |
		    IEEE80211_HW_SUPPORTS_DYNAMIC_PS |
		    IEEE80211_HW_REPORTS_TX_ACK_STATUS |
		    IEEE80211_HW_CONNECTION_MONITOR |
		    IEEE80211_HW_AMPDU_AGGREGATION |
		    IEEE80211_HW_TX_AMPDU_SETUP_IN_HW |
		    IEEE80211_HW_NEED_DTIM_BEFORE_ASSOC;

	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
					  BIT(NL80211_IFTYPE_ADHOC) |
					  BIT(NL80211_IFTYPE_AP) |
					  BIT(NL80211_IFTYPE_MESH_POINT) |
					  BIT(NL80211_IFTYPE_P2P_CLIENT) |
					  BIT(NL80211_IFTYPE_P2P_GO);

#ifdef CONFIG_PM
	hw->wiphy->wowlan = &cw1200_wowlan_support;
#endif

	hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD;

	hw->queues = 4;

	priv->rts_threshold = -1;

	hw->max_rates = 8;
	hw->max_rate_tries = 15;
	hw->extra_tx_headroom = WSM_TX_EXTRA_HEADROOM +
		8;  /* TKIP IV */

	hw->sta_data_size = sizeof(struct cw1200_sta_priv);

	hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &cw1200_band_2ghz;
	if (have_5ghz)
		hw->wiphy->bands[IEEE80211_BAND_5GHZ] = &cw1200_band_5ghz;

	/* Channel params have to be cleared before registering wiphy again */
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		struct ieee80211_supported_band *sband = hw->wiphy->bands[band];
		if (!sband)
			continue;
		for (i = 0; i < sband->n_channels; i++) {
			sband->channels[i].flags = 0;
			sband->channels[i].max_antenna_gain = 0;
			sband->channels[i].max_power = 30;
		}
	}

	hw->wiphy->max_scan_ssids = 2;
	hw->wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;

	if (macaddr)
		SET_IEEE80211_PERM_ADDR(hw, (u8 *)macaddr);
	else
		SET_IEEE80211_PERM_ADDR(hw, cw1200_mac_template);

	/* Fix up mac address if necessary */
	if (hw->wiphy->perm_addr[3] == 0 &&
	    hw->wiphy->perm_addr[4] == 0 &&
	    hw->wiphy->perm_addr[5] == 0) {
		get_random_bytes(&hw->wiphy->perm_addr[3], 3);
	}

	mutex_init(&priv->wsm_cmd_mux);
	mutex_init(&priv->conf_mutex);
	priv->workqueue = create_singlethread_workqueue("cw1200_wq");
	sema_init(&priv->scan.lock, 1);
	INIT_WORK(&priv->scan.work, cw1200_scan_work);
	INIT_DELAYED_WORK(&priv->scan.probe_work, cw1200_probe_work);
	INIT_DELAYED_WORK(&priv->scan.timeout, cw1200_scan_timeout);
	INIT_DELAYED_WORK(&priv->clear_recent_scan_work,
			  cw1200_clear_recent_scan_work);
	INIT_DELAYED_WORK(&priv->join_timeout, cw1200_join_timeout);
	INIT_WORK(&priv->unjoin_work, cw1200_unjoin_work);
	INIT_WORK(&priv->join_complete_work, cw1200_join_complete_work);
	INIT_WORK(&priv->wep_key_work, cw1200_wep_key_work);
	INIT_WORK(&priv->tx_policy_upload_work, tx_policy_upload_work);
	spin_lock_init(&priv->event_queue_lock);
	INIT_LIST_HEAD(&priv->event_queue);
	INIT_WORK(&priv->event_handler, cw1200_event_handler);
	INIT_DELAYED_WORK(&priv->bss_loss_work, cw1200_bss_loss_work);
	INIT_WORK(&priv->bss_params_work, cw1200_bss_params_work);
	spin_lock_init(&priv->bss_loss_lock);
	spin_lock_init(&priv->ps_state_lock);
	INIT_WORK(&priv->set_cts_work, cw1200_set_cts_work);
	INIT_WORK(&priv->set_tim_work, cw1200_set_tim_work);
	INIT_WORK(&priv->multicast_start_work, cw1200_multicast_start_work);
	INIT_WORK(&priv->multicast_stop_work, cw1200_multicast_stop_work);
	INIT_WORK(&priv->link_id_work, cw1200_link_id_work);
	INIT_DELAYED_WORK(&priv->link_id_gc_work, cw1200_link_id_gc_work);
	INIT_WORK(&priv->linkid_reset_work, cw1200_link_id_reset);
	INIT_WORK(&priv->update_filtering_work, cw1200_update_filtering_work);
	INIT_WORK(&priv->set_beacon_wakeup_period_work,
		  cw1200_set_beacon_wakeup_period_work);
	setup_timer(&priv->mcast_timeout, cw1200_mcast_timeout,
		    (unsigned long)priv);

	if (cw1200_queue_stats_init(&priv->tx_queue_stats,
				    CW1200_LINK_ID_MAX,
				    cw1200_skb_dtor,
				    priv)) {
		ieee80211_free_hw(hw);
		return NULL;
	}

	for (i = 0; i < 4; ++i) {
		if (cw1200_queue_init(&priv->tx_queue[i],
				      &priv->tx_queue_stats, i, 16,
				      cw1200_ttl[i])) {
			for (; i > 0; i--)
				cw1200_queue_deinit(&priv->tx_queue[i - 1]);
			cw1200_queue_stats_deinit(&priv->tx_queue_stats);
			ieee80211_free_hw(hw);
			return NULL;
		}
	}

	init_waitqueue_head(&priv->channel_switch_done);
	init_waitqueue_head(&priv->wsm_cmd_wq);
	init_waitqueue_head(&priv->wsm_startup_done);
	init_waitqueue_head(&priv->ps_mode_switch_done);
	wsm_buf_init(&priv->wsm_cmd_buf);
	spin_lock_init(&priv->wsm_cmd.lock);
	priv->wsm_cmd.done = 1;
	tx_policy_init(priv);

	return hw;
}

static int cw1200_register_common(struct ieee80211_hw *dev)
{
	struct cw1200_common *priv = dev->priv;
	int err;

#ifdef CONFIG_PM
	err = cw1200_pm_init(&priv->pm_state, priv);
	if (err) {
		pr_err("Cannot init PM. (%d).\n",
		       err);
		return err;
	}
#endif

	err = ieee80211_register_hw(dev);
	if (err) {
		pr_err("Cannot register device (%d).\n",
		       err);
#ifdef CONFIG_PM
		cw1200_pm_deinit(&priv->pm_state);
#endif
		return err;
	}

	cw1200_debug_init(priv);

	pr_info("Registered as '%s'\n", wiphy_name(dev->wiphy));
	return 0;
}

static void cw1200_free_common(struct ieee80211_hw *dev)
{
	ieee80211_free_hw(dev);
}

static void cw1200_unregister_common(struct ieee80211_hw *dev)
{
	struct cw1200_common *priv = dev->priv;
	int i;

	ieee80211_unregister_hw(dev);

	del_timer_sync(&priv->mcast_timeout);
	cw1200_unregister_bh(priv);

	cw1200_debug_release(priv);

	mutex_destroy(&priv->conf_mutex);

	wsm_buf_deinit(&priv->wsm_cmd_buf);

	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;

	if (priv->sdd) {
		release_firmware(priv->sdd);
		priv->sdd = NULL;
	}

	for (i = 0; i < 4; ++i)
		cw1200_queue_deinit(&priv->tx_queue[i]);

	cw1200_queue_stats_deinit(&priv->tx_queue_stats);
#ifdef CONFIG_PM
	cw1200_pm_deinit(&priv->pm_state);
#endif
}

/* Clock is in KHz */
u32 cw1200_dpll_from_clk(u16 clk_khz)
{
	switch (clk_khz) {
	case 0x32C8: /* 13000 KHz */
		return 0x1D89D241;
	case 0x3E80: /* 16000 KHz */
		return 0x000001E1;
	case 0x41A0: /* 16800 KHz */
		return 0x124931C1;
	case 0x4B00: /* 19200 KHz */
		return 0x00000191;
	case 0x5DC0: /* 24000 KHz */
		return 0x00000141;
	case 0x6590: /* 26000 KHz */
		return 0x0EC4F121;
	case 0x8340: /* 33600 KHz */
		return 0x092490E1;
	case 0x9600: /* 38400 KHz */
		return 0x100010C1;
	case 0x9C40: /* 40000 KHz */
		return 0x000000C1;
	case 0xBB80: /* 48000 KHz */
		return 0x000000A1;
	case 0xCB20: /* 52000 KHz */
		return 0x07627091;
	default:
		pr_err("Unknown Refclk freq (0x%04x), using 26000KHz\n",
		       clk_khz);
		return 0x0EC4F121;
	}
}

int cw1200_core_probe(const struct hwbus_ops *hwbus_ops,
		      struct hwbus_priv *hwbus,
		      struct device *pdev,
		      struct cw1200_common **core,
		      int ref_clk, const u8 *macaddr,
		      const char *sdd_path, bool have_5ghz)
{
	int err = -EINVAL;
	struct ieee80211_hw *dev;
	struct cw1200_common *priv;
	struct wsm_operational_mode mode = {
		.power_mode = cw1200_power_mode,
		.disable_more_flag_usage = true,
	};

	dev = cw1200_init_common(macaddr, have_5ghz);
	if (!dev)
		goto err;

	priv = dev->priv;
	priv->hw_refclk = ref_clk;
	if (cw1200_refclk)
		priv->hw_refclk = cw1200_refclk;

	priv->sdd_path = (char *)sdd_path;
	if (cw1200_sdd_path)
		priv->sdd_path = cw1200_sdd_path;

	priv->hwbus_ops = hwbus_ops;
	priv->hwbus_priv = hwbus;
	priv->pdev = pdev;
	SET_IEEE80211_DEV(priv->hw, pdev);

	/* Pass struct cw1200_common back up */
	*core = priv;

	err = cw1200_register_bh(priv);
	if (err)
		goto err1;

	err = cw1200_load_firmware(priv);
	if (err)
		goto err2;

	if (wait_event_interruptible_timeout(priv->wsm_startup_done,
					     priv->firmware_ready,
					     3*HZ) <= 0) {
		/* TODO: Need to find how to reset device
		   in QUEUE mode properly.
		*/
		pr_err("Timeout waiting on device startup\n");
		err = -ETIMEDOUT;
		goto err2;
	}

	/* Set low-power mode. */
	wsm_set_operational_mode(priv, &mode);

	/* Enable multi-TX confirmation */
	wsm_use_multi_tx_conf(priv, true);

	err = cw1200_register_common(dev);
	if (err)
		goto err2;

	return err;

err2:
	cw1200_unregister_bh(priv);
err1:
	cw1200_free_common(dev);
err:
	*core = NULL;
	return err;
}
EXPORT_SYMBOL_GPL(cw1200_core_probe);

void cw1200_core_release(struct cw1200_common *self)
{
	/* Disable device interrupts */
	self->hwbus_ops->lock(self->hwbus_priv);
	__cw1200_irq_enable(self, 0);
	self->hwbus_ops->unlock(self->hwbus_priv);

	/* And then clean up */
	cw1200_unregister_common(self->hw);
	cw1200_free_common(self->hw);
	return;
}
EXPORT_SYMBOL_GPL(cw1200_core_release);
