/*
 * Copyright (c) 2010 Broadcom Corporation
 * Copyright (c) 2013 Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define __UNDEF_NO_VERSION__
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/etherdevice.h>
#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/bcma/bcma.h>
#include <net/mac80211.h>
#include <defs.h>
#include "phy/phy_int.h"
#include "d11.h"
#include "channel.h"
#include "scb.h"
#include "pub.h"
#include "ucode_loader.h"
#include "mac80211_if.h"
#include "main.h"
#include "debug.h"
#include "led.h"

#define N_TX_QUEUES	4 /* #tx queues on mac80211<->driver interface */
#define BRCMS_FLUSH_TIMEOUT	500 /* msec */

/* Flags we support */
#define MAC_FILTERS (FIF_ALLMULTI | \
	FIF_FCSFAIL | \
	FIF_CONTROL | \
	FIF_OTHER_BSS | \
	FIF_BCN_PRBRESP_PROMISC | \
	FIF_PSPOLL)

#define CHAN2GHZ(channel, freqency, chflags)  { \
	.band = NL80211_BAND_2GHZ, \
	.center_freq = (freqency), \
	.hw_value = (channel), \
	.flags = chflags, \
	.max_antenna_gain = 0, \
	.max_power = 19, \
}

#define CHAN5GHZ(channel, chflags)  { \
	.band = NL80211_BAND_5GHZ, \
	.center_freq = 5000 + 5*(channel), \
	.hw_value = (channel), \
	.flags = chflags, \
	.max_antenna_gain = 0, \
	.max_power = 21, \
}

#define RATE(rate100m, _flags) { \
	.bitrate = (rate100m), \
	.flags = (_flags), \
	.hw_value = (rate100m / 5), \
}

struct firmware_hdr {
	__le32 offset;
	__le32 len;
	__le32 idx;
};

static const char * const brcms_firmwares[MAX_FW_IMAGES] = {
	"brcm/bcm43xx",
	NULL
};

static int n_adapters_found;

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom 802.11n wireless LAN driver.");
MODULE_SUPPORTED_DEVICE("Broadcom 802.11n WLAN cards");
MODULE_LICENSE("Dual BSD/GPL");
/* This needs to be adjusted when brcms_firmwares changes */
MODULE_FIRMWARE("brcm/bcm43xx-0.fw");
MODULE_FIRMWARE("brcm/bcm43xx_hdr-0.fw");

/* recognized BCMA Core IDs */
static struct bcma_device_id brcms_coreid_table[] = {
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 17, BCMA_ANY_CLASS),
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 23, BCMA_ANY_CLASS),
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 24, BCMA_ANY_CLASS),
	{},
};
MODULE_DEVICE_TABLE(bcma, brcms_coreid_table);

#if defined(CONFIG_BRCMDBG)
/*
 * Module parameter for setting the debug message level. Available
 * flags are specified by the BRCM_DL_* macros in
 * drivers/net/wireless/brcm80211/include/defs.h.
 */
module_param_named(debug, brcm_msg_level, uint, 0644);
#endif

static struct ieee80211_channel brcms_2ghz_chantable[] = {
	CHAN2GHZ(1, 2412, IEEE80211_CHAN_NO_HT40MINUS),
	CHAN2GHZ(2, 2417, IEEE80211_CHAN_NO_HT40MINUS),
	CHAN2GHZ(3, 2422, IEEE80211_CHAN_NO_HT40MINUS),
	CHAN2GHZ(4, 2427, IEEE80211_CHAN_NO_HT40MINUS),
	CHAN2GHZ(5, 2432, 0),
	CHAN2GHZ(6, 2437, 0),
	CHAN2GHZ(7, 2442, 0),
	CHAN2GHZ(8, 2447, IEEE80211_CHAN_NO_HT40PLUS),
	CHAN2GHZ(9, 2452, IEEE80211_CHAN_NO_HT40PLUS),
	CHAN2GHZ(10, 2457, IEEE80211_CHAN_NO_HT40PLUS),
	CHAN2GHZ(11, 2462, IEEE80211_CHAN_NO_HT40PLUS),
	CHAN2GHZ(12, 2467,
		 IEEE80211_CHAN_NO_IR |
		 IEEE80211_CHAN_NO_HT40PLUS),
	CHAN2GHZ(13, 2472,
		 IEEE80211_CHAN_NO_IR |
		 IEEE80211_CHAN_NO_HT40PLUS),
	CHAN2GHZ(14, 2484,
		 IEEE80211_CHAN_NO_IR |
		 IEEE80211_CHAN_NO_HT40PLUS | IEEE80211_CHAN_NO_HT40MINUS |
		 IEEE80211_CHAN_NO_OFDM)
};

static struct ieee80211_channel brcms_5ghz_nphy_chantable[] = {
	/* UNII-1 */
	CHAN5GHZ(36, IEEE80211_CHAN_NO_HT40MINUS),
	CHAN5GHZ(40, IEEE80211_CHAN_NO_HT40PLUS),
	CHAN5GHZ(44, IEEE80211_CHAN_NO_HT40MINUS),
	CHAN5GHZ(48, IEEE80211_CHAN_NO_HT40PLUS),
	/* UNII-2 */
	CHAN5GHZ(52,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40MINUS),
	CHAN5GHZ(56,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40PLUS),
	CHAN5GHZ(60,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40MINUS),
	CHAN5GHZ(64,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40PLUS),
	/* MID */
	CHAN5GHZ(100,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40MINUS),
	CHAN5GHZ(104,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40PLUS),
	CHAN5GHZ(108,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40MINUS),
	CHAN5GHZ(112,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40PLUS),
	CHAN5GHZ(116,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40MINUS),
	CHAN5GHZ(120,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40PLUS),
	CHAN5GHZ(124,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40MINUS),
	CHAN5GHZ(128,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40PLUS),
	CHAN5GHZ(132,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40MINUS),
	CHAN5GHZ(136,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40PLUS),
	CHAN5GHZ(140,
		 IEEE80211_CHAN_RADAR |
		 IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_HT40PLUS |
		 IEEE80211_CHAN_NO_HT40MINUS),
	/* UNII-3 */
	CHAN5GHZ(149, IEEE80211_CHAN_NO_HT40MINUS),
	CHAN5GHZ(153, IEEE80211_CHAN_NO_HT40PLUS),
	CHAN5GHZ(157, IEEE80211_CHAN_NO_HT40MINUS),
	CHAN5GHZ(161, IEEE80211_CHAN_NO_HT40PLUS),
	CHAN5GHZ(165, IEEE80211_CHAN_NO_HT40PLUS | IEEE80211_CHAN_NO_HT40MINUS)
};

/*
 * The rate table is used for both 2.4G and 5G rates. The
 * latter being a subset as it does not support CCK rates.
 */
static struct ieee80211_rate legacy_ratetable[] = {
	RATE(10, 0),
	RATE(20, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(55, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(110, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(60, 0),
	RATE(90, 0),
	RATE(120, 0),
	RATE(180, 0),
	RATE(240, 0),
	RATE(360, 0),
	RATE(480, 0),
	RATE(540, 0),
};

static const struct ieee80211_supported_band brcms_band_2GHz_nphy_template = {
	.band = NL80211_BAND_2GHZ,
	.channels = brcms_2ghz_chantable,
	.n_channels = ARRAY_SIZE(brcms_2ghz_chantable),
	.bitrates = legacy_ratetable,
	.n_bitrates = ARRAY_SIZE(legacy_ratetable),
	.ht_cap = {
		   /* from include/linux/ieee80211.h */
		   .cap = IEEE80211_HT_CAP_GRN_FLD |
			  IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40,
		   .ht_supported = true,
		   .ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K,
		   .ampdu_density = AMPDU_DEF_MPDU_DENSITY,
		   .mcs = {
			   /* placeholders for now */
			   .rx_mask = {0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0},
			   .rx_highest = cpu_to_le16(500),
			   .tx_params = IEEE80211_HT_MCS_TX_DEFINED}
		   }
};

static const struct ieee80211_supported_band brcms_band_5GHz_nphy_template = {
	.band = NL80211_BAND_5GHZ,
	.channels = brcms_5ghz_nphy_chantable,
	.n_channels = ARRAY_SIZE(brcms_5ghz_nphy_chantable),
	.bitrates = legacy_ratetable + BRCMS_LEGACY_5G_RATE_OFFSET,
	.n_bitrates = ARRAY_SIZE(legacy_ratetable) -
			BRCMS_LEGACY_5G_RATE_OFFSET,
	.ht_cap = {
		   .cap = IEEE80211_HT_CAP_GRN_FLD | IEEE80211_HT_CAP_SGI_20 |
			  IEEE80211_HT_CAP_SGI_40,
		   .ht_supported = true,
		   .ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K,
		   .ampdu_density = AMPDU_DEF_MPDU_DENSITY,
		   .mcs = {
			   /* placeholders for now */
			   .rx_mask = {0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0},
			   .rx_highest = cpu_to_le16(500),
			   .tx_params = IEEE80211_HT_MCS_TX_DEFINED}
		   }
};

/* flags the given rate in rateset as requested */
static void brcms_set_basic_rate(struct brcm_rateset *rs, u16 rate, bool is_br)
{
	u32 i;

	for (i = 0; i < rs->count; i++) {
		if (rate != (rs->rates[i] & 0x7f))
			continue;

		if (is_br)
			rs->rates[i] |= BRCMS_RATE_FLAG;
		else
			rs->rates[i] &= BRCMS_RATE_MASK;
		return;
	}
}

/**
 * This function frees the WL per-device resources.
 *
 * This function frees resources owned by the WL device pointed to
 * by the wl parameter.
 *
 * precondition: can both be called locked and unlocked
 *
 */
static void brcms_free(struct brcms_info *wl)
{
	struct brcms_timer *t, *next;

	/* free ucode data */
	if (wl->fw.fw_cnt)
		brcms_ucode_data_free(&wl->ucode);
	if (wl->irq)
		free_irq(wl->irq, wl);

	/* kill dpc */
	tasklet_kill(&wl->tasklet);

	if (wl->pub) {
		brcms_debugfs_detach(wl->pub);
		brcms_c_module_unregister(wl->pub, "linux", wl);
	}

	/* free common resources */
	if (wl->wlc) {
		brcms_c_detach(wl->wlc);
		wl->wlc = NULL;
		wl->pub = NULL;
	}

	/* virtual interface deletion is deferred so we cannot spinwait */

	/* wait for all pending callbacks to complete */
	while (atomic_read(&wl->callbacks) > 0)
		schedule();

	/* free timers */
	for (t = wl->timers; t; t = next) {
		next = t->next;
#ifdef DEBUG
		kfree(t->name);
#endif
		kfree(t);
	}
}

/*
* called from both kernel as from this kernel module (error flow on attach)
* precondition: perimeter lock is not acquired.
*/
static void brcms_remove(struct bcma_device *pdev)
{
	struct ieee80211_hw *hw = bcma_get_drvdata(pdev);
	struct brcms_info *wl = hw->priv;

	if (wl->wlc) {
		brcms_led_unregister(wl);
		wiphy_rfkill_set_hw_state(wl->pub->ieee_hw->wiphy, false);
		wiphy_rfkill_stop_polling(wl->pub->ieee_hw->wiphy);
		ieee80211_unregister_hw(hw);
	}

	brcms_free(wl);

	bcma_set_drvdata(pdev, NULL);
	ieee80211_free_hw(hw);
}

/*
 * Precondition: Since this function is called in brcms_pci_probe() context,
 * no locking is required.
 */
static void brcms_release_fw(struct brcms_info *wl)
{
	int i;
	for (i = 0; i < MAX_FW_IMAGES; i++) {
		release_firmware(wl->fw.fw_bin[i]);
		release_firmware(wl->fw.fw_hdr[i]);
	}
}

/*
 * Precondition: Since this function is called in brcms_pci_probe() context,
 * no locking is required.
 */
static int brcms_request_fw(struct brcms_info *wl, struct bcma_device *pdev)
{
	int status;
	struct device *device = &pdev->dev;
	char fw_name[100];
	int i;

	memset(&wl->fw, 0, sizeof(struct brcms_firmware));
	for (i = 0; i < MAX_FW_IMAGES; i++) {
		if (brcms_firmwares[i] == NULL)
			break;
		sprintf(fw_name, "%s-%d.fw", brcms_firmwares[i],
			UCODE_LOADER_API_VER);
		status = request_firmware(&wl->fw.fw_bin[i], fw_name, device);
		if (status) {
			wiphy_err(wl->wiphy, "%s: fail to load firmware %s\n",
				  KBUILD_MODNAME, fw_name);
			return status;
		}
		sprintf(fw_name, "%s_hdr-%d.fw", brcms_firmwares[i],
			UCODE_LOADER_API_VER);
		status = request_firmware(&wl->fw.fw_hdr[i], fw_name, device);
		if (status) {
			wiphy_err(wl->wiphy, "%s: fail to load firmware %s\n",
				  KBUILD_MODNAME, fw_name);
			return status;
		}
		wl->fw.hdr_num_entries[i] =
		    wl->fw.fw_hdr[i]->size / (sizeof(struct firmware_hdr));
	}
	wl->fw.fw_cnt = i;
	status = brcms_ucode_data_init(wl, &wl->ucode);
	brcms_release_fw(wl);
	return status;
}

static void brcms_ops_tx(struct ieee80211_hw *hw,
			 struct ieee80211_tx_control *control,
			 struct sk_buff *skb)
{
	struct brcms_info *wl = hw->priv;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	spin_lock_bh(&wl->lock);
	if (!wl->pub->up) {
		brcms_err(wl->wlc->hw->d11core, "ops->tx called while down\n");
		kfree_skb(skb);
		goto done;
	}
	if (brcms_c_sendpkt_mac80211(wl->wlc, skb, hw))
		tx_info->rate_driver_data[0] = control->sta;
 done:
	spin_unlock_bh(&wl->lock);
}

static int brcms_ops_start(struct ieee80211_hw *hw)
{
	struct brcms_info *wl = hw->priv;
	bool blocked;
	int err;

	if (!wl->ucode.bcm43xx_bomminor) {
		err = brcms_request_fw(wl, wl->wlc->hw->d11core);
		if (err)
			return -ENOENT;
	}

	ieee80211_wake_queues(hw);
	spin_lock_bh(&wl->lock);
	blocked = brcms_rfkill_set_hw_state(wl);
	spin_unlock_bh(&wl->lock);
	if (!blocked)
		wiphy_rfkill_stop_polling(wl->pub->ieee_hw->wiphy);

	spin_lock_bh(&wl->lock);
	/* avoid acknowledging frames before a non-monitor device is added */
	wl->mute_tx = true;

	if (!wl->pub->up)
		if (!blocked)
			err = brcms_up(wl);
		else
			err = -ERFKILL;
	else
		err = -ENODEV;
	spin_unlock_bh(&wl->lock);

	if (err != 0)
		brcms_err(wl->wlc->hw->d11core, "%s: brcms_up() returned %d\n",
			  __func__, err);

	bcma_core_pci_power_save(wl->wlc->hw->d11core->bus, true);
	return err;
}

static void brcms_ops_stop(struct ieee80211_hw *hw)
{
	struct brcms_info *wl = hw->priv;
	int status;

	ieee80211_stop_queues(hw);

	if (wl->wlc == NULL)
		return;

	spin_lock_bh(&wl->lock);
	status = brcms_c_chipmatch(wl->wlc->hw->d11core);
	spin_unlock_bh(&wl->lock);
	if (!status) {
		brcms_err(wl->wlc->hw->d11core,
			  "wl: brcms_ops_stop: chipmatch failed\n");
		return;
	}

	bcma_core_pci_power_save(wl->wlc->hw->d11core->bus, false);

	/* put driver in down state */
	spin_lock_bh(&wl->lock);
	brcms_down(wl);
	spin_unlock_bh(&wl->lock);
}

static int
brcms_ops_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct brcms_info *wl = hw->priv;

	/* Just STA, AP and ADHOC for now */
	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP &&
	    vif->type != NL80211_IFTYPE_ADHOC) {
		brcms_err(wl->wlc->hw->d11core,
			  "%s: Attempt to add type %d, only STA, AP and AdHoc for now\n",
			  __func__, vif->type);
		return -EOPNOTSUPP;
	}

	spin_lock_bh(&wl->lock);
	wl->mute_tx = false;
	brcms_c_mute(wl->wlc, false);
	if (vif->type == NL80211_IFTYPE_STATION)
		brcms_c_start_station(wl->wlc, vif->addr);
	else if (vif->type == NL80211_IFTYPE_AP)
		brcms_c_start_ap(wl->wlc, vif->addr, vif->bss_conf.bssid,
				 vif->bss_conf.ssid, vif->bss_conf.ssid_len);
	else if (vif->type == NL80211_IFTYPE_ADHOC)
		brcms_c_start_adhoc(wl->wlc, vif->addr);
	spin_unlock_bh(&wl->lock);

	return 0;
}

static void
brcms_ops_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
}

static int brcms_ops_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ieee80211_conf *conf = &hw->conf;
	struct brcms_info *wl = hw->priv;
	struct bcma_device *core = wl->wlc->hw->d11core;
	int err = 0;
	int new_int;

	spin_lock_bh(&wl->lock);
	if (changed & IEEE80211_CONF_CHANGE_LISTEN_INTERVAL) {
		brcms_c_set_beacon_listen_interval(wl->wlc,
						   conf->listen_interval);
	}
	if (changed & IEEE80211_CONF_CHANGE_MONITOR)
		brcms_dbg_info(core, "%s: change monitor mode: %s\n",
			       __func__, conf->flags & IEEE80211_CONF_MONITOR ?
			       "true" : "false");
	if (changed & IEEE80211_CONF_CHANGE_PS)
		brcms_err(core, "%s: change power-save mode: %s (implement)\n",
			  __func__, conf->flags & IEEE80211_CONF_PS ?
			  "true" : "false");

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		err = brcms_c_set_tx_power(wl->wlc, conf->power_level);
		if (err < 0) {
			brcms_err(core, "%s: Error setting power_level\n",
				  __func__);
			goto config_out;
		}
		new_int = brcms_c_get_tx_power(wl->wlc);
		if (new_int != conf->power_level)
			brcms_err(core,
				  "%s: Power level req != actual, %d %d\n",
				  __func__, conf->power_level,
				  new_int);
	}
	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		if (conf->chandef.width == NL80211_CHAN_WIDTH_20 ||
		    conf->chandef.width == NL80211_CHAN_WIDTH_20_NOHT)
			err = brcms_c_set_channel(wl->wlc,
						  conf->chandef.chan->hw_value);
		else
			err = -ENOTSUPP;
	}
	if (changed & IEEE80211_CONF_CHANGE_RETRY_LIMITS)
		err = brcms_c_set_rate_limit(wl->wlc,
					     conf->short_frame_max_tx_count,
					     conf->long_frame_max_tx_count);

 config_out:
	spin_unlock_bh(&wl->lock);
	return err;
}

static void
brcms_ops_bss_info_changed(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *info, u32 changed)
{
	struct brcms_info *wl = hw->priv;
	struct bcma_device *core = wl->wlc->hw->d11core;

	if (changed & BSS_CHANGED_ASSOC) {
		/* association status changed (associated/disassociated)
		 * also implies a change in the AID.
		 */
		brcms_err(core, "%s: %s: %sassociated\n", KBUILD_MODNAME,
			  __func__, info->assoc ? "" : "dis");
		spin_lock_bh(&wl->lock);
		brcms_c_associate_upd(wl->wlc, info->assoc);
		spin_unlock_bh(&wl->lock);
	}
	if (changed & BSS_CHANGED_ERP_SLOT) {
		s8 val;

		/* slot timing changed */
		if (info->use_short_slot)
			val = 1;
		else
			val = 0;
		spin_lock_bh(&wl->lock);
		brcms_c_set_shortslot_override(wl->wlc, val);
		spin_unlock_bh(&wl->lock);
	}

	if (changed & BSS_CHANGED_HT) {
		/* 802.11n parameters changed */
		u16 mode = info->ht_operation_mode;

		spin_lock_bh(&wl->lock);
		brcms_c_protection_upd(wl->wlc, BRCMS_PROT_N_CFG,
			mode & IEEE80211_HT_OP_MODE_PROTECTION);
		brcms_c_protection_upd(wl->wlc, BRCMS_PROT_N_NONGF,
			mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
		brcms_c_protection_upd(wl->wlc, BRCMS_PROT_N_OBSS,
			mode & IEEE80211_HT_OP_MODE_NON_HT_STA_PRSNT);
		spin_unlock_bh(&wl->lock);
	}
	if (changed & BSS_CHANGED_BASIC_RATES) {
		struct ieee80211_supported_band *bi;
		u32 br_mask, i;
		u16 rate;
		struct brcm_rateset rs;
		int error;

		/* retrieve the current rates */
		spin_lock_bh(&wl->lock);
		brcms_c_get_current_rateset(wl->wlc, &rs);
		spin_unlock_bh(&wl->lock);

		br_mask = info->basic_rates;
		bi = hw->wiphy->bands[brcms_c_get_curband(wl->wlc)];
		for (i = 0; i < bi->n_bitrates; i++) {
			/* convert to internal rate value */
			rate = (bi->bitrates[i].bitrate << 1) / 10;

			/* set/clear basic rate flag */
			brcms_set_basic_rate(&rs, rate, br_mask & 1);
			br_mask >>= 1;
		}

		/* update the rate set */
		spin_lock_bh(&wl->lock);
		error = brcms_c_set_rateset(wl->wlc, &rs);
		spin_unlock_bh(&wl->lock);
		if (error)
			brcms_err(core, "changing basic rates failed: %d\n",
				  error);
	}
	if (changed & BSS_CHANGED_BEACON_INT) {
		/* Beacon interval changed */
		spin_lock_bh(&wl->lock);
		brcms_c_set_beacon_period(wl->wlc, info->beacon_int);
		spin_unlock_bh(&wl->lock);
	}
	if (changed & BSS_CHANGED_BSSID) {
		/* BSSID changed, for whatever reason (IBSS and managed mode) */
		spin_lock_bh(&wl->lock);
		brcms_c_set_addrmatch(wl->wlc, RCM_BSSID_OFFSET, info->bssid);
		spin_unlock_bh(&wl->lock);
	}
	if (changed & BSS_CHANGED_SSID) {
		/* BSSID changed, for whatever reason (IBSS and managed mode) */
		spin_lock_bh(&wl->lock);
		brcms_c_set_ssid(wl->wlc, info->ssid, info->ssid_len);
		spin_unlock_bh(&wl->lock);
	}
	if (changed & BSS_CHANGED_BEACON) {
		/* Beacon data changed, retrieve new beacon (beaconing modes) */
		struct sk_buff *beacon;
		u16 tim_offset = 0;

		spin_lock_bh(&wl->lock);
		beacon = ieee80211_beacon_get_tim(hw, vif, &tim_offset, NULL);
		brcms_c_set_new_beacon(wl->wlc, beacon, tim_offset,
				       info->dtim_period);
		spin_unlock_bh(&wl->lock);
	}

	if (changed & BSS_CHANGED_AP_PROBE_RESP) {
		struct sk_buff *probe_resp;

		spin_lock_bh(&wl->lock);
		probe_resp = ieee80211_proberesp_get(hw, vif);
		brcms_c_set_new_probe_resp(wl->wlc, probe_resp);
		spin_unlock_bh(&wl->lock);
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		/* Beaconing should be enabled/disabled (beaconing modes) */
		brcms_err(core, "%s: Beacon enabled: %s\n", __func__,
			  info->enable_beacon ? "true" : "false");
		if (info->enable_beacon &&
		    hw->wiphy->flags & WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD) {
			brcms_c_enable_probe_resp(wl->wlc, true);
		} else {
			brcms_c_enable_probe_resp(wl->wlc, false);
		}
	}

	if (changed & BSS_CHANGED_CQM) {
		/* Connection quality monitor config changed */
		brcms_err(core, "%s: cqm change: threshold %d, hys %d "
			  " (implement)\n", __func__, info->cqm_rssi_thold,
			  info->cqm_rssi_hyst);
	}

	if (changed & BSS_CHANGED_IBSS) {
		/* IBSS join status changed */
		brcms_err(core, "%s: IBSS joined: %s (implement)\n",
			  __func__, info->ibss_joined ? "true" : "false");
	}

	if (changed & BSS_CHANGED_ARP_FILTER) {
		/* Hardware ARP filter address list or state changed */
		brcms_err(core, "%s: arp filtering: %d addresses"
			  " (implement)\n", __func__, info->arp_addr_cnt);
	}

	if (changed & BSS_CHANGED_QOS) {
		/*
		 * QoS for this association was enabled/disabled.
		 * Note that it is only ever disabled for station mode.
		 */
		brcms_err(core, "%s: qos enabled: %s (implement)\n",
			  __func__, info->qos ? "true" : "false");
	}
	return;
}

static void
brcms_ops_configure_filter(struct ieee80211_hw *hw,
			unsigned int changed_flags,
			unsigned int *total_flags, u64 multicast)
{
	struct brcms_info *wl = hw->priv;
	struct bcma_device *core = wl->wlc->hw->d11core;

	changed_flags &= MAC_FILTERS;
	*total_flags &= MAC_FILTERS;

	if (changed_flags & FIF_ALLMULTI)
		brcms_dbg_info(core, "FIF_ALLMULTI\n");
	if (changed_flags & FIF_FCSFAIL)
		brcms_dbg_info(core, "FIF_FCSFAIL\n");
	if (changed_flags & FIF_CONTROL)
		brcms_dbg_info(core, "FIF_CONTROL\n");
	if (changed_flags & FIF_OTHER_BSS)
		brcms_dbg_info(core, "FIF_OTHER_BSS\n");
	if (changed_flags & FIF_PSPOLL)
		brcms_dbg_info(core, "FIF_PSPOLL\n");
	if (changed_flags & FIF_BCN_PRBRESP_PROMISC)
		brcms_dbg_info(core, "FIF_BCN_PRBRESP_PROMISC\n");

	spin_lock_bh(&wl->lock);
	brcms_c_mac_promisc(wl->wlc, *total_flags);
	spin_unlock_bh(&wl->lock);
	return;
}

static void brcms_ops_sw_scan_start(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    const u8 *mac_addr)
{
	struct brcms_info *wl = hw->priv;
	spin_lock_bh(&wl->lock);
	brcms_c_scan_start(wl->wlc);
	spin_unlock_bh(&wl->lock);
	return;
}

static void brcms_ops_sw_scan_complete(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct brcms_info *wl = hw->priv;
	spin_lock_bh(&wl->lock);
	brcms_c_scan_stop(wl->wlc);
	spin_unlock_bh(&wl->lock);
	return;
}

static int
brcms_ops_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u16 queue,
		  const struct ieee80211_tx_queue_params *params)
{
	struct brcms_info *wl = hw->priv;

	spin_lock_bh(&wl->lock);
	brcms_c_wme_setparams(wl->wlc, queue, params, true);
	spin_unlock_bh(&wl->lock);

	return 0;
}

static int
brcms_ops_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       struct ieee80211_sta *sta)
{
	struct brcms_info *wl = hw->priv;
	struct scb *scb = &wl->wlc->pri_scb;

	brcms_c_init_scb(scb);

	wl->pub->global_ampdu = &(scb->scb_ampdu);
	wl->pub->global_ampdu->scb = scb;
	wl->pub->global_ampdu->max_pdu = 16;

	/*
	 * minstrel_ht initiates addBA on our behalf by calling
	 * ieee80211_start_tx_ba_session()
	 */
	return 0;
}

static int
brcms_ops_ampdu_action(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params)
{
	struct brcms_info *wl = hw->priv;
	struct scb *scb = &wl->wlc->pri_scb;
	int status;
	struct ieee80211_sta *sta = params->sta;
	enum ieee80211_ampdu_mlme_action action = params->action;
	u16 tid = params->tid;
	u8 buf_size = params->buf_size;

	if (WARN_ON(scb->magic != SCB_MAGIC))
		return -EIDRM;
	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		break;
	case IEEE80211_AMPDU_RX_STOP:
		break;
	case IEEE80211_AMPDU_TX_START:
		spin_lock_bh(&wl->lock);
		status = brcms_c_aggregatable(wl->wlc, tid);
		spin_unlock_bh(&wl->lock);
		if (!status) {
			brcms_err(wl->wlc->hw->d11core,
				  "START: tid %d is not agg\'able\n", tid);
			return -EINVAL;
		}
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;

	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		spin_lock_bh(&wl->lock);
		brcms_c_ampdu_flush(wl->wlc, sta, tid);
		spin_unlock_bh(&wl->lock);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		/*
		 * BA window size from ADDBA response ('buf_size') defines how
		 * many outstanding MPDUs are allowed for the BA stream by
		 * recipient and traffic class. 'ampdu_factor' gives maximum
		 * AMPDU size.
		 */
		spin_lock_bh(&wl->lock);
		brcms_c_ampdu_tx_operational(wl->wlc, tid, buf_size,
			(1 << (IEEE80211_HT_MAX_AMPDU_FACTOR +
			 sta->ht_cap.ampdu_factor)) - 1);
		spin_unlock_bh(&wl->lock);
		/* Power save wakeup */
		break;
	default:
		brcms_err(wl->wlc->hw->d11core,
			  "%s: Invalid command, ignoring\n", __func__);
	}

	return 0;
}

static void brcms_ops_rfkill_poll(struct ieee80211_hw *hw)
{
	struct brcms_info *wl = hw->priv;
	bool blocked;

	spin_lock_bh(&wl->lock);
	blocked = brcms_c_check_radio_disabled(wl->wlc);
	spin_unlock_bh(&wl->lock);

	wiphy_rfkill_set_hw_state(wl->pub->ieee_hw->wiphy, blocked);
}

static bool brcms_tx_flush_completed(struct brcms_info *wl)
{
	bool result;

	spin_lock_bh(&wl->lock);
	result = brcms_c_tx_flush_completed(wl->wlc);
	spin_unlock_bh(&wl->lock);
	return result;
}

static void brcms_ops_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    u32 queues, bool drop)
{
	struct brcms_info *wl = hw->priv;
	int ret;

	no_printk("%s: drop = %s\n", __func__, drop ? "true" : "false");

	ret = wait_event_timeout(wl->tx_flush_wq,
				 brcms_tx_flush_completed(wl),
				 msecs_to_jiffies(BRCMS_FLUSH_TIMEOUT));

	brcms_dbg_mac80211(wl->wlc->hw->d11core,
			   "ret=%d\n", jiffies_to_msecs(ret));
}

static u64 brcms_ops_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct brcms_info *wl = hw->priv;
	u64 tsf;

	spin_lock_bh(&wl->lock);
	tsf = brcms_c_tsf_get(wl->wlc);
	spin_unlock_bh(&wl->lock);

	return tsf;
}

static void brcms_ops_set_tsf(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif, u64 tsf)
{
	struct brcms_info *wl = hw->priv;

	spin_lock_bh(&wl->lock);
	brcms_c_tsf_set(wl->wlc, tsf);
	spin_unlock_bh(&wl->lock);
}

static const struct ieee80211_ops brcms_ops = {
	.tx = brcms_ops_tx,
	.start = brcms_ops_start,
	.stop = brcms_ops_stop,
	.add_interface = brcms_ops_add_interface,
	.remove_interface = brcms_ops_remove_interface,
	.config = brcms_ops_config,
	.bss_info_changed = brcms_ops_bss_info_changed,
	.configure_filter = brcms_ops_configure_filter,
	.sw_scan_start = brcms_ops_sw_scan_start,
	.sw_scan_complete = brcms_ops_sw_scan_complete,
	.conf_tx = brcms_ops_conf_tx,
	.sta_add = brcms_ops_sta_add,
	.ampdu_action = brcms_ops_ampdu_action,
	.rfkill_poll = brcms_ops_rfkill_poll,
	.flush = brcms_ops_flush,
	.get_tsf = brcms_ops_get_tsf,
	.set_tsf = brcms_ops_set_tsf,
};

void brcms_dpc(unsigned long data)
{
	struct brcms_info *wl;

	wl = (struct brcms_info *) data;

	spin_lock_bh(&wl->lock);

	/* call the common second level interrupt handler */
	if (wl->pub->up) {
		if (wl->resched) {
			unsigned long flags;

			spin_lock_irqsave(&wl->isr_lock, flags);
			brcms_c_intrsupd(wl->wlc);
			spin_unlock_irqrestore(&wl->isr_lock, flags);
		}

		wl->resched = brcms_c_dpc(wl->wlc, true);
	}

	/* brcms_c_dpc() may bring the driver down */
	if (!wl->pub->up)
		goto done;

	/* re-schedule dpc */
	if (wl->resched)
		tasklet_schedule(&wl->tasklet);
	else
		/* re-enable interrupts */
		brcms_intrson(wl);

 done:
	spin_unlock_bh(&wl->lock);
	wake_up(&wl->tx_flush_wq);
}

static irqreturn_t brcms_isr(int irq, void *dev_id)
{
	struct brcms_info *wl;
	irqreturn_t ret = IRQ_NONE;

	wl = (struct brcms_info *) dev_id;

	spin_lock(&wl->isr_lock);

	/* call common first level interrupt handler */
	if (brcms_c_isr(wl->wlc)) {
		/* schedule second level handler */
		tasklet_schedule(&wl->tasklet);
		ret = IRQ_HANDLED;
	}

	spin_unlock(&wl->isr_lock);

	return ret;
}

/*
 * is called in brcms_pci_probe() context, therefore no locking required.
 */
static int ieee_hw_rate_init(struct ieee80211_hw *hw)
{
	struct brcms_info *wl = hw->priv;
	struct brcms_c_info *wlc = wl->wlc;
	struct ieee80211_supported_band *band;
	int has_5g = 0;
	u16 phy_type;

	hw->wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	hw->wiphy->bands[NL80211_BAND_5GHZ] = NULL;

	phy_type = brcms_c_get_phy_type(wl->wlc, 0);
	if (phy_type == PHY_TYPE_N || phy_type == PHY_TYPE_LCN) {
		band = &wlc->bandstate[BAND_2G_INDEX]->band;
		*band = brcms_band_2GHz_nphy_template;
		if (phy_type == PHY_TYPE_LCN) {
			/* Single stream */
			band->ht_cap.mcs.rx_mask[1] = 0;
			band->ht_cap.mcs.rx_highest = cpu_to_le16(72);
		}
		hw->wiphy->bands[NL80211_BAND_2GHZ] = band;
	} else {
		return -EPERM;
	}

	/* Assume all bands use the same phy.  True for 11n devices. */
	if (wl->pub->_nbands > 1) {
		has_5g++;
		if (phy_type == PHY_TYPE_N || phy_type == PHY_TYPE_LCN) {
			band = &wlc->bandstate[BAND_5G_INDEX]->band;
			*band = brcms_band_5GHz_nphy_template;
			hw->wiphy->bands[NL80211_BAND_5GHZ] = band;
		} else {
			return -EPERM;
		}
	}
	return 0;
}

/*
 * is called in brcms_pci_probe() context, therefore no locking required.
 */
static int ieee_hw_init(struct ieee80211_hw *hw)
{
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);

	hw->extra_tx_headroom = brcms_c_get_header_len();
	hw->queues = N_TX_QUEUES;
	hw->max_rates = 2;	/* Primary rate and 1 fallback rate */

	/* channel change time is dependent on chip and band  */
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				     BIT(NL80211_IFTYPE_AP) |
				     BIT(NL80211_IFTYPE_ADHOC);

	/*
	 * deactivate sending probe responses by ucude, because this will
	 * cause problems when WPS is used.
	 *
	 * hw->wiphy->flags |= WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD;
	 */

	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_CQM_RSSI_LIST);

	hw->rate_control_algorithm = "minstrel_ht";

	hw->sta_data_size = 0;
	return ieee_hw_rate_init(hw);
}

/**
 * attach to the WL device.
 *
 * Attach to the WL device identified by vendor and device parameters.
 * regs is a host accessible memory address pointing to WL device registers.
 *
 * is called in brcms_bcma_probe() context, therefore no locking required.
 */
static struct brcms_info *brcms_attach(struct bcma_device *pdev)
{
	struct brcms_info *wl = NULL;
	int unit, err;
	struct ieee80211_hw *hw;
	u8 perm[ETH_ALEN];

	unit = n_adapters_found;
	err = 0;

	if (unit < 0)
		return NULL;

	/* allocate private info */
	hw = bcma_get_drvdata(pdev);
	if (hw != NULL)
		wl = hw->priv;
	if (WARN_ON(hw == NULL) || WARN_ON(wl == NULL))
		return NULL;
	wl->wiphy = hw->wiphy;

	atomic_set(&wl->callbacks, 0);

	init_waitqueue_head(&wl->tx_flush_wq);

	/* setup the bottom half handler */
	tasklet_init(&wl->tasklet, brcms_dpc, (unsigned long) wl);

	spin_lock_init(&wl->lock);
	spin_lock_init(&wl->isr_lock);

	/* common load-time initialization */
	wl->wlc = brcms_c_attach((void *)wl, pdev, unit, false, &err);
	if (!wl->wlc) {
		wiphy_err(wl->wiphy, "%s: attach() failed with code %d\n",
			  KBUILD_MODNAME, err);
		goto fail;
	}
	wl->pub = brcms_c_pub(wl->wlc);

	wl->pub->ieee_hw = hw;

	/* register our interrupt handler */
	if (request_irq(pdev->irq, brcms_isr,
			IRQF_SHARED, KBUILD_MODNAME, wl)) {
		wiphy_err(wl->wiphy, "wl%d: request_irq() failed\n", unit);
		goto fail;
	}
	wl->irq = pdev->irq;

	/* register module */
	brcms_c_module_register(wl->pub, "linux", wl, NULL);

	if (ieee_hw_init(hw)) {
		wiphy_err(wl->wiphy, "wl%d: %s: ieee_hw_init failed!\n", unit,
			  __func__);
		goto fail;
	}

	brcms_c_regd_init(wl->wlc);

	memcpy(perm, &wl->pub->cur_etheraddr, ETH_ALEN);
	if (WARN_ON(!is_valid_ether_addr(perm)))
		goto fail;
	SET_IEEE80211_PERM_ADDR(hw, perm);

	err = ieee80211_register_hw(hw);
	if (err)
		wiphy_err(wl->wiphy, "%s: ieee80211_register_hw failed, status"
			  "%d\n", __func__, err);

	if (wl->pub->srom_ccode[0] &&
	    regulatory_hint(wl->wiphy, wl->pub->srom_ccode))
		wiphy_err(wl->wiphy, "%s: regulatory hint failed\n", __func__);

	brcms_debugfs_attach(wl->pub);
	brcms_debugfs_create_files(wl->pub);
	n_adapters_found++;
	return wl;

fail:
	brcms_free(wl);
	return NULL;
}



/**
 * determines if a device is a WL device, and if so, attaches it.
 *
 * This function determines if a device pointed to by pdev is a WL device,
 * and if so, performs a brcms_attach() on it.
 *
 * Perimeter lock is initialized in the course of this function.
 */
static int brcms_bcma_probe(struct bcma_device *pdev)
{
	struct brcms_info *wl;
	struct ieee80211_hw *hw;

	dev_info(&pdev->dev, "mfg %x core %x rev %d class %d irq %d\n",
		 pdev->id.manuf, pdev->id.id, pdev->id.rev, pdev->id.class,
		 pdev->irq);

	if ((pdev->id.manuf != BCMA_MANUF_BCM) ||
	    (pdev->id.id != BCMA_CORE_80211))
		return -ENODEV;

	hw = ieee80211_alloc_hw(sizeof(struct brcms_info), &brcms_ops);
	if (!hw) {
		pr_err("%s: ieee80211_alloc_hw failed\n", __func__);
		return -ENOMEM;
	}

	SET_IEEE80211_DEV(hw, &pdev->dev);

	bcma_set_drvdata(pdev, hw);

	memset(hw->priv, 0, sizeof(*wl));

	wl = brcms_attach(pdev);
	if (!wl) {
		pr_err("%s: brcms_attach failed!\n", __func__);
		return -ENODEV;
	}
	brcms_led_register(wl);

	return 0;
}

static int brcms_suspend(struct bcma_device *pdev)
{
	struct brcms_info *wl;
	struct ieee80211_hw *hw;

	hw = bcma_get_drvdata(pdev);
	wl = hw->priv;
	if (!wl) {
		pr_err("%s: %s: no driver private struct!\n", KBUILD_MODNAME,
		       __func__);
		return -ENODEV;
	}

	/* only need to flag hw is down for proper resume */
	spin_lock_bh(&wl->lock);
	wl->pub->hw_up = false;
	spin_unlock_bh(&wl->lock);

	brcms_dbg_info(wl->wlc->hw->d11core, "brcms_suspend ok\n");

	return 0;
}

static int brcms_resume(struct bcma_device *pdev)
{
	return 0;
}

static struct bcma_driver brcms_bcma_driver = {
	.name     = KBUILD_MODNAME,
	.probe    = brcms_bcma_probe,
	.suspend  = brcms_suspend,
	.resume   = brcms_resume,
	.remove   = brcms_remove,
	.id_table = brcms_coreid_table,
};

/**
 * This is the main entry point for the brcmsmac driver.
 *
 * This function is scheduled upon module initialization and
 * does the driver registration, which result in brcms_bcma_probe()
 * call resulting in the driver bringup.
 */
static void brcms_driver_init(struct work_struct *work)
{
	int error;

	error = bcma_driver_register(&brcms_bcma_driver);
	if (error)
		pr_err("%s: register returned %d\n", __func__, error);
}

static DECLARE_WORK(brcms_driver_work, brcms_driver_init);

static int __init brcms_module_init(void)
{
	brcms_debugfs_init();
	if (!schedule_work(&brcms_driver_work))
		return -EBUSY;

	return 0;
}

/**
 * This function unloads the brcmsmac driver from the system.
 *
 * This function unconditionally unloads the brcmsmac driver module from the
 * system.
 *
 */
static void __exit brcms_module_exit(void)
{
	cancel_work_sync(&brcms_driver_work);
	bcma_driver_unregister(&brcms_bcma_driver);
	brcms_debugfs_exit();
}

module_init(brcms_module_init);
module_exit(brcms_module_exit);

/*
 * precondition: perimeter lock has been acquired
 */
void brcms_txflowcontrol(struct brcms_info *wl, struct brcms_if *wlif,
			 bool state, int prio)
{
	brcms_err(wl->wlc->hw->d11core, "Shouldn't be here %s\n", __func__);
}

/*
 * precondition: perimeter lock has been acquired
 */
void brcms_init(struct brcms_info *wl)
{
	brcms_dbg_info(wl->wlc->hw->d11core, "Initializing wl%d\n",
		       wl->pub->unit);
	brcms_reset(wl);
	brcms_c_init(wl->wlc, wl->mute_tx);
}

/*
 * precondition: perimeter lock has been acquired
 */
uint brcms_reset(struct brcms_info *wl)
{
	brcms_dbg_info(wl->wlc->hw->d11core, "Resetting wl%d\n", wl->pub->unit);
	brcms_c_reset(wl->wlc);

	/* dpc will not be rescheduled */
	wl->resched = false;

	/* inform publicly that interface is down */
	wl->pub->up = false;

	return 0;
}

void brcms_fatal_error(struct brcms_info *wl)
{
	brcms_err(wl->wlc->hw->d11core, "wl%d: fatal error, reinitializing\n",
		  wl->wlc->pub->unit);
	brcms_reset(wl);
	ieee80211_restart_hw(wl->pub->ieee_hw);
}

/*
 * These are interrupt on/off entry points. Disable interrupts
 * during interrupt state transition.
 */
void brcms_intrson(struct brcms_info *wl)
{
	unsigned long flags;

	spin_lock_irqsave(&wl->isr_lock, flags);
	brcms_c_intrson(wl->wlc);
	spin_unlock_irqrestore(&wl->isr_lock, flags);
}

u32 brcms_intrsoff(struct brcms_info *wl)
{
	unsigned long flags;
	u32 status;

	spin_lock_irqsave(&wl->isr_lock, flags);
	status = brcms_c_intrsoff(wl->wlc);
	spin_unlock_irqrestore(&wl->isr_lock, flags);
	return status;
}

void brcms_intrsrestore(struct brcms_info *wl, u32 macintmask)
{
	unsigned long flags;

	spin_lock_irqsave(&wl->isr_lock, flags);
	brcms_c_intrsrestore(wl->wlc, macintmask);
	spin_unlock_irqrestore(&wl->isr_lock, flags);
}

/*
 * precondition: perimeter lock has been acquired
 */
int brcms_up(struct brcms_info *wl)
{
	int error = 0;

	if (wl->pub->up)
		return 0;

	error = brcms_c_up(wl->wlc);

	return error;
}

/*
 * precondition: perimeter lock has been acquired
 */
void brcms_down(struct brcms_info *wl)
{
	uint callbacks, ret_val = 0;

	/* call common down function */
	ret_val = brcms_c_down(wl->wlc);
	callbacks = atomic_read(&wl->callbacks) - ret_val;

	/* wait for down callbacks to complete */
	spin_unlock_bh(&wl->lock);

	/* For HIGH_only driver, it's important to actually schedule other work,
	 * not just spin wait since everything runs at schedule level
	 */
	SPINWAIT((atomic_read(&wl->callbacks) > callbacks), 100 * 1000);

	spin_lock_bh(&wl->lock);
}

/*
* precondition: perimeter lock is not acquired
 */
static void _brcms_timer(struct work_struct *work)
{
	struct brcms_timer *t = container_of(work, struct brcms_timer,
					     dly_wrk.work);

	spin_lock_bh(&t->wl->lock);

	if (t->set) {
		if (t->periodic) {
			atomic_inc(&t->wl->callbacks);
			ieee80211_queue_delayed_work(t->wl->pub->ieee_hw,
						     &t->dly_wrk,
						     msecs_to_jiffies(t->ms));
		} else {
			t->set = false;
		}

		t->fn(t->arg);
	}

	atomic_dec(&t->wl->callbacks);

	spin_unlock_bh(&t->wl->lock);
}

/*
 * Adds a timer to the list. Caller supplies a timer function.
 * Is called from wlc.
 *
 * precondition: perimeter lock has been acquired
 */
struct brcms_timer *brcms_init_timer(struct brcms_info *wl,
				     void (*fn) (void *arg),
				     void *arg, const char *name)
{
	struct brcms_timer *t;

	t = kzalloc(sizeof(struct brcms_timer), GFP_ATOMIC);
	if (!t)
		return NULL;

	INIT_DELAYED_WORK(&t->dly_wrk, _brcms_timer);
	t->wl = wl;
	t->fn = fn;
	t->arg = arg;
	t->next = wl->timers;
	wl->timers = t;

#ifdef DEBUG
	t->name = kstrdup(name, GFP_ATOMIC);
#endif

	return t;
}

/*
 * adds only the kernel timer since it's going to be more accurate
 * as well as it's easier to make it periodic
 *
 * precondition: perimeter lock has been acquired
 */
void brcms_add_timer(struct brcms_timer *t, uint ms, int periodic)
{
	struct ieee80211_hw *hw = t->wl->pub->ieee_hw;

#ifdef DEBUG
	if (t->set)
		brcms_dbg_info(t->wl->wlc->hw->d11core,
			       "%s: Already set. Name: %s, per %d\n",
			       __func__, t->name, periodic);
#endif
	t->ms = ms;
	t->periodic = (bool) periodic;
	if (!t->set) {
		t->set = true;
		atomic_inc(&t->wl->callbacks);
	}

	ieee80211_queue_delayed_work(hw, &t->dly_wrk, msecs_to_jiffies(ms));
}

/*
 * return true if timer successfully deleted, false if still pending
 *
 * precondition: perimeter lock has been acquired
 */
bool brcms_del_timer(struct brcms_timer *t)
{
	if (t->set) {
		t->set = false;
		if (!cancel_delayed_work(&t->dly_wrk))
			return false;

		atomic_dec(&t->wl->callbacks);
	}

	return true;
}

/*
 * precondition: perimeter lock has been acquired
 */
void brcms_free_timer(struct brcms_timer *t)
{
	struct brcms_info *wl = t->wl;
	struct brcms_timer *tmp;

	/* delete the timer in case it is active */
	brcms_del_timer(t);

	if (wl->timers == t) {
		wl->timers = wl->timers->next;
#ifdef DEBUG
		kfree(t->name);
#endif
		kfree(t);
		return;

	}

	tmp = wl->timers;
	while (tmp) {
		if (tmp->next == t) {
			tmp->next = t->next;
#ifdef DEBUG
			kfree(t->name);
#endif
			kfree(t);
			return;
		}
		tmp = tmp->next;
	}

}

/*
 * precondition: perimeter lock has been acquired
 */
int brcms_ucode_init_buf(struct brcms_info *wl, void **pbuf, u32 idx)
{
	int i, entry;
	const u8 *pdata;
	struct firmware_hdr *hdr;
	for (i = 0; i < wl->fw.fw_cnt; i++) {
		hdr = (struct firmware_hdr *)wl->fw.fw_hdr[i]->data;
		for (entry = 0; entry < wl->fw.hdr_num_entries[i];
		     entry++, hdr++) {
			u32 len = le32_to_cpu(hdr->len);
			if (le32_to_cpu(hdr->idx) == idx) {
				pdata = wl->fw.fw_bin[i]->data +
					le32_to_cpu(hdr->offset);
				*pbuf = kmemdup(pdata, len, GFP_ATOMIC);
				if (*pbuf == NULL)
					goto fail;

				return 0;
			}
		}
	}
	brcms_err(wl->wlc->hw->d11core,
		  "ERROR: ucode buf tag:%d can not be found!\n", idx);
	*pbuf = NULL;
fail:
	return -ENODATA;
}

/*
 * Precondition: Since this function is called in brcms_bcma_probe() context,
 * no locking is required.
 */
int brcms_ucode_init_uint(struct brcms_info *wl, size_t *n_bytes, u32 idx)
{
	int i, entry;
	const u8 *pdata;
	struct firmware_hdr *hdr;
	for (i = 0; i < wl->fw.fw_cnt; i++) {
		hdr = (struct firmware_hdr *)wl->fw.fw_hdr[i]->data;
		for (entry = 0; entry < wl->fw.hdr_num_entries[i];
		     entry++, hdr++) {
			if (le32_to_cpu(hdr->idx) == idx) {
				pdata = wl->fw.fw_bin[i]->data +
					le32_to_cpu(hdr->offset);
				if (le32_to_cpu(hdr->len) != 4) {
					brcms_err(wl->wlc->hw->d11core,
						  "ERROR: fw hdr len\n");
					return -ENOMSG;
				}
				*n_bytes = le32_to_cpu(*((__le32 *) pdata));
				return 0;
			}
		}
	}
	brcms_err(wl->wlc->hw->d11core,
		  "ERROR: ucode tag:%d can not be found!\n", idx);
	return -ENOMSG;
}

/*
 * precondition: can both be called locked and unlocked
 */
void brcms_ucode_free_buf(void *p)
{
	kfree(p);
}

/*
 * checks validity of all firmware images loaded from user space
 *
 * Precondition: Since this function is called in brcms_bcma_probe() context,
 * no locking is required.
 */
int brcms_check_firmwares(struct brcms_info *wl)
{
	int i;
	int entry;
	int rc = 0;
	const struct firmware *fw;
	const struct firmware *fw_hdr;
	struct firmware_hdr *ucode_hdr;
	for (i = 0; i < MAX_FW_IMAGES && rc == 0; i++) {
		fw =  wl->fw.fw_bin[i];
		fw_hdr = wl->fw.fw_hdr[i];
		if (fw == NULL && fw_hdr == NULL) {
			break;
		} else if (fw == NULL || fw_hdr == NULL) {
			wiphy_err(wl->wiphy, "%s: invalid bin/hdr fw\n",
				  __func__);
			rc = -EBADF;
		} else if (fw_hdr->size % sizeof(struct firmware_hdr)) {
			wiphy_err(wl->wiphy, "%s: non integral fw hdr file "
				"size %zu/%zu\n", __func__, fw_hdr->size,
				sizeof(struct firmware_hdr));
			rc = -EBADF;
		} else if (fw->size < MIN_FW_SIZE || fw->size > MAX_FW_SIZE) {
			wiphy_err(wl->wiphy, "%s: out of bounds fw file size %zu\n",
				  __func__, fw->size);
			rc = -EBADF;
		} else {
			/* check if ucode section overruns firmware image */
			ucode_hdr = (struct firmware_hdr *)fw_hdr->data;
			for (entry = 0; entry < wl->fw.hdr_num_entries[i] &&
			     !rc; entry++, ucode_hdr++) {
				if (le32_to_cpu(ucode_hdr->offset) +
				    le32_to_cpu(ucode_hdr->len) >
				    fw->size) {
					wiphy_err(wl->wiphy,
						  "%s: conflicting bin/hdr\n",
						  __func__);
					rc = -EBADF;
				}
			}
		}
	}
	if (rc == 0 && wl->fw.fw_cnt != i) {
		wiphy_err(wl->wiphy, "%s: invalid fw_cnt=%d\n", __func__,
			wl->fw.fw_cnt);
		rc = -EBADF;
	}
	return rc;
}

/*
 * precondition: perimeter lock has been acquired
 */
bool brcms_rfkill_set_hw_state(struct brcms_info *wl)
{
	bool blocked = brcms_c_check_radio_disabled(wl->wlc);

	spin_unlock_bh(&wl->lock);
	wiphy_rfkill_set_hw_state(wl->pub->ieee_hw->wiphy, blocked);
	if (blocked)
		wiphy_rfkill_start_polling(wl->pub->ieee_hw->wiphy);
	spin_lock_bh(&wl->lock);
	return blocked;
}
