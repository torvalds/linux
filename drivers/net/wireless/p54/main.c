/*
 * mac80211 glue code for mac80211 Prism54 drivers
 *
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

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>

#include <net/mac80211.h>

#include "p54.h"
#include "lmac.h"

static int modparam_nohwcrypt;
module_param_named(nohwcrypt, modparam_nohwcrypt, bool, S_IRUGO);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption.");
MODULE_AUTHOR("Michael Wu <flamingice@sourmilk.net>");
MODULE_DESCRIPTION("Softmac Prism54 common code");
MODULE_LICENSE("GPL");
MODULE_ALIAS("prism54common");

static int p54_sta_add_remove(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta)
{
	struct p54_common *priv = hw->priv;

	/*
	 * Notify the firmware that we don't want or we don't
	 * need to buffer frames for this station anymore.
	 */

	p54_sta_unlock(priv, sta->addr);

	return 0;
}

static void p54_sta_notify(struct ieee80211_hw *dev, struct ieee80211_vif *vif,
			      enum sta_notify_cmd notify_cmd,
			      struct ieee80211_sta *sta)
{
	struct p54_common *priv = dev->priv;

	switch (notify_cmd) {
	case STA_NOTIFY_AWAKE:
		/* update the firmware's filter table */
		p54_sta_unlock(priv, sta->addr);
		break;
	default:
		break;
	}
}

static int p54_set_tim(struct ieee80211_hw *dev, struct ieee80211_sta *sta,
			bool set)
{
	struct p54_common *priv = dev->priv;

	return p54_update_beacon_tim(priv, sta->aid, set);
}

u8 *p54_find_ie(struct sk_buff *skb, u8 ie)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	u8 *pos, *end;

	if (skb->len <= sizeof(mgmt))
		return NULL;

	pos = (u8 *)mgmt->u.beacon.variable;
	end = skb->data + skb->len;
	while (pos < end) {
		if (pos + 2 + pos[1] > end)
			return NULL;

		if (pos[0] == ie)
			return pos;

		pos += 2 + pos[1];
	}
	return NULL;
}

static int p54_beacon_format_ie_tim(struct sk_buff *skb)
{
	/*
	 * the good excuse for this mess is ... the firmware.
	 * The dummy TIM MUST be at the end of the beacon frame,
	 * because it'll be overwritten!
	 */
	u8 *tim;
	u8 dtim_len;
	u8 dtim_period;
	u8 *next;

	tim = p54_find_ie(skb, WLAN_EID_TIM);
	if (!tim)
		return 0;

	dtim_len = tim[1];
	dtim_period = tim[3];
	next = tim + 2 + dtim_len;

	if (dtim_len < 3)
		return -EINVAL;

	memmove(tim, next, skb_tail_pointer(skb) - next);
	tim = skb_tail_pointer(skb) - (dtim_len + 2);

	/* add the dummy at the end */
	tim[0] = WLAN_EID_TIM;
	tim[1] = 3;
	tim[2] = 0;
	tim[3] = dtim_period;
	tim[4] = 0;

	if (dtim_len > 3)
		skb_trim(skb, skb->len - (dtim_len - 3));

	return 0;
}

static int p54_beacon_update(struct p54_common *priv,
			struct ieee80211_vif *vif)
{
	struct sk_buff *beacon;
	int ret;

	beacon = ieee80211_beacon_get(priv->hw, vif);
	if (!beacon)
		return -ENOMEM;
	ret = p54_beacon_format_ie_tim(beacon);
	if (ret)
		return ret;

	/*
	 * During operation, the firmware takes care of beaconing.
	 * The driver only needs to upload a new beacon template, once
	 * the template was changed by the stack or userspace.
	 *
	 * LMAC API 3.2.2 also specifies that the driver does not need
	 * to cancel the old beacon template by hand, instead the firmware
	 * will release the previous one through the feedback mechanism.
	 */
	WARN_ON(p54_tx_80211(priv->hw, beacon));
	priv->tsf_high32 = 0;
	priv->tsf_low32 = 0;

	return 0;
}

static int p54_start(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	int err;

	mutex_lock(&priv->conf_mutex);
	err = priv->open(dev);
	if (err)
		goto out;
	P54_SET_QUEUE(priv->qos_params[0], 0x0002, 0x0003, 0x0007, 47);
	P54_SET_QUEUE(priv->qos_params[1], 0x0002, 0x0007, 0x000f, 94);
	P54_SET_QUEUE(priv->qos_params[2], 0x0003, 0x000f, 0x03ff, 0);
	P54_SET_QUEUE(priv->qos_params[3], 0x0007, 0x000f, 0x03ff, 0);
	err = p54_set_edcf(priv);
	if (err)
		goto out;

	memset(priv->bssid, ~0, ETH_ALEN);
	priv->mode = NL80211_IFTYPE_MONITOR;
	err = p54_setup_mac(priv);
	if (err) {
		priv->mode = NL80211_IFTYPE_UNSPECIFIED;
		goto out;
	}

	ieee80211_queue_delayed_work(dev, &priv->work, 0);

	priv->softled_state = 0;
	err = p54_set_leds(priv);

out:
	mutex_unlock(&priv->conf_mutex);
	return err;
}

static void p54_stop(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	int i;

	mutex_lock(&priv->conf_mutex);
	priv->mode = NL80211_IFTYPE_UNSPECIFIED;
	priv->softled_state = 0;
	p54_set_leds(priv);

	cancel_delayed_work_sync(&priv->work);

	priv->stop(dev);
	skb_queue_purge(&priv->tx_pending);
	skb_queue_purge(&priv->tx_queue);
	for (i = 0; i < P54_QUEUE_NUM; i++) {
		priv->tx_stats[i].count = 0;
		priv->tx_stats[i].len = 0;
	}

	priv->beacon_req_id = cpu_to_le32(0);
	priv->tsf_high32 = priv->tsf_low32 = 0;
	mutex_unlock(&priv->conf_mutex);
}

static int p54_add_interface(struct ieee80211_hw *dev,
			     struct ieee80211_vif *vif)
{
	struct p54_common *priv = dev->priv;

	mutex_lock(&priv->conf_mutex);
	if (priv->mode != NL80211_IFTYPE_MONITOR) {
		mutex_unlock(&priv->conf_mutex);
		return -EOPNOTSUPP;
	}

	priv->vif = vif;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		priv->mode = vif->type;
		break;
	default:
		mutex_unlock(&priv->conf_mutex);
		return -EOPNOTSUPP;
	}

	memcpy(priv->mac_addr, vif->addr, ETH_ALEN);
	p54_setup_mac(priv);
	mutex_unlock(&priv->conf_mutex);
	return 0;
}

static void p54_remove_interface(struct ieee80211_hw *dev,
				 struct ieee80211_vif *vif)
{
	struct p54_common *priv = dev->priv;

	mutex_lock(&priv->conf_mutex);
	priv->vif = NULL;

	/*
	 * LMAC API 3.2.2 states that any active beacon template must be
	 * canceled by the driver before attempting a mode transition.
	 */
	if (le32_to_cpu(priv->beacon_req_id) != 0) {
		p54_tx_cancel(priv, priv->beacon_req_id);
		wait_for_completion_interruptible_timeout(&priv->beacon_comp, HZ);
	}
	priv->mode = NL80211_IFTYPE_MONITOR;
	memset(priv->mac_addr, 0, ETH_ALEN);
	memset(priv->bssid, 0, ETH_ALEN);
	p54_setup_mac(priv);
	mutex_unlock(&priv->conf_mutex);
}

static int p54_config(struct ieee80211_hw *dev, u32 changed)
{
	int ret = 0;
	struct p54_common *priv = dev->priv;
	struct ieee80211_conf *conf = &dev->conf;

	mutex_lock(&priv->conf_mutex);
	if (changed & IEEE80211_CONF_CHANGE_POWER)
		priv->output_power = conf->power_level << 2;
	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ret = p54_scan(priv, P54_SCAN_EXIT, 0);
		if (ret)
			goto out;
	}
	if (changed & IEEE80211_CONF_CHANGE_PS) {
		ret = p54_set_ps(priv);
		if (ret)
			goto out;
	}
	if (changed & IEEE80211_CONF_CHANGE_IDLE) {
		ret = p54_setup_mac(priv);
		if (ret)
			goto out;
	}

out:
	mutex_unlock(&priv->conf_mutex);
	return ret;
}

static void p54_configure_filter(struct ieee80211_hw *dev,
				 unsigned int changed_flags,
				 unsigned int *total_flags,
				 u64 multicast)
{
	struct p54_common *priv = dev->priv;

	*total_flags &= FIF_PROMISC_IN_BSS |
			FIF_OTHER_BSS;

	priv->filter_flags = *total_flags;

	if (changed_flags & (FIF_PROMISC_IN_BSS | FIF_OTHER_BSS))
		p54_setup_mac(priv);
}

static int p54_conf_tx(struct ieee80211_hw *dev, u16 queue,
		       const struct ieee80211_tx_queue_params *params)
{
	struct p54_common *priv = dev->priv;
	int ret;

	mutex_lock(&priv->conf_mutex);
	if (queue < dev->queues) {
		P54_SET_QUEUE(priv->qos_params[queue], params->aifs,
			params->cw_min, params->cw_max, params->txop);
		ret = p54_set_edcf(priv);
	} else
		ret = -EINVAL;
	mutex_unlock(&priv->conf_mutex);
	return ret;
}

static void p54_work(struct work_struct *work)
{
	struct p54_common *priv = container_of(work, struct p54_common,
					       work.work);

	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED))
		return ;

	/*
	 * TODO: walk through tx_queue and do the following tasks
	 * 	1. initiate bursts.
	 *      2. cancel stuck frames / reset the device if necessary.
	 */

	p54_fetch_statistics(priv);
}

static int p54_get_stats(struct ieee80211_hw *dev,
			 struct ieee80211_low_level_stats *stats)
{
	struct p54_common *priv = dev->priv;

	memcpy(stats, &priv->stats, sizeof(*stats));
	return 0;
}

static void p54_bss_info_changed(struct ieee80211_hw *dev,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *info,
				 u32 changed)
{
	struct p54_common *priv = dev->priv;

	mutex_lock(&priv->conf_mutex);
	if (changed & BSS_CHANGED_BSSID) {
		memcpy(priv->bssid, info->bssid, ETH_ALEN);
		p54_setup_mac(priv);
	}

	if (changed & BSS_CHANGED_BEACON) {
		p54_scan(priv, P54_SCAN_EXIT, 0);
		p54_setup_mac(priv);
		p54_beacon_update(priv, vif);
		p54_set_edcf(priv);
	}

	if (changed & (BSS_CHANGED_ERP_SLOT | BSS_CHANGED_BEACON)) {
		priv->use_short_slot = info->use_short_slot;
		p54_set_edcf(priv);
	}
	if (changed & BSS_CHANGED_BASIC_RATES) {
		if (dev->conf.channel->band == IEEE80211_BAND_5GHZ)
			priv->basic_rate_mask = (info->basic_rates << 4);
		else
			priv->basic_rate_mask = info->basic_rates;
		p54_setup_mac(priv);
		if (priv->fw_var >= 0x500)
			p54_scan(priv, P54_SCAN_EXIT, 0);
	}
	if (changed & BSS_CHANGED_ASSOC) {
		if (info->assoc) {
			priv->aid = info->aid;
			priv->wakeup_timer = info->beacon_int *
					     info->dtim_period * 5;
			p54_setup_mac(priv);
		} else {
			priv->wakeup_timer = 500;
			priv->aid = 0;
		}
	}

	mutex_unlock(&priv->conf_mutex);
}

static int p54_set_key(struct ieee80211_hw *dev, enum set_key_cmd cmd,
		       struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		       struct ieee80211_key_conf *key)
{
	struct p54_common *priv = dev->priv;
	int slot, ret = 0;
	u8 algo = 0;
	u8 *addr = NULL;

	if (modparam_nohwcrypt)
		return -EOPNOTSUPP;

	mutex_lock(&priv->conf_mutex);
	if (cmd == SET_KEY) {
		switch (key->alg) {
		case ALG_TKIP:
			if (!(priv->privacy_caps & (BR_DESC_PRIV_CAP_MICHAEL |
			      BR_DESC_PRIV_CAP_TKIP))) {
				ret = -EOPNOTSUPP;
				goto out_unlock;
			}
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			algo = P54_CRYPTO_TKIPMICHAEL;
			break;
		case ALG_WEP:
			if (!(priv->privacy_caps & BR_DESC_PRIV_CAP_WEP)) {
				ret = -EOPNOTSUPP;
				goto out_unlock;
			}
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			algo = P54_CRYPTO_WEP;
			break;
		case ALG_CCMP:
			if (!(priv->privacy_caps & BR_DESC_PRIV_CAP_AESCCMP)) {
				ret = -EOPNOTSUPP;
				goto out_unlock;
			}
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			algo = P54_CRYPTO_AESCCMP;
			break;
		default:
			ret = -EOPNOTSUPP;
			goto out_unlock;
		}
		slot = bitmap_find_free_region(priv->used_rxkeys,
					       priv->rx_keycache_size, 0);

		if (slot < 0) {
			/*
			 * The device supports the choosen algorithm, but the
			 * firmware does not provide enough key slots to store
			 * all of them.
			 * But encryption offload for outgoing frames is always
			 * possible, so we just pretend that the upload was
			 * successful and do the decryption in software.
			 */

			/* mark the key as invalid. */
			key->hw_key_idx = 0xff;
			goto out_unlock;
		}
	} else {
		slot = key->hw_key_idx;

		if (slot == 0xff) {
			/* This key was not uploaded into the rx key cache. */

			goto out_unlock;
		}

		bitmap_release_region(priv->used_rxkeys, slot, 0);
		algo = 0;
	}

	if (sta)
		addr = sta->addr;

	ret = p54_upload_key(priv, algo, slot, key->keyidx,
			     key->keylen, addr, key->key);
	if (ret) {
		bitmap_release_region(priv->used_rxkeys, slot, 0);
		ret = -EOPNOTSUPP;
		goto out_unlock;
	}

	key->hw_key_idx = slot;

out_unlock:
	mutex_unlock(&priv->conf_mutex);
	return ret;
}

static const struct ieee80211_ops p54_ops = {
	.tx			= p54_tx_80211,
	.start			= p54_start,
	.stop			= p54_stop,
	.add_interface		= p54_add_interface,
	.remove_interface	= p54_remove_interface,
	.set_tim		= p54_set_tim,
	.sta_notify		= p54_sta_notify,
	.sta_add		= p54_sta_add_remove,
	.sta_remove		= p54_sta_add_remove,
	.set_key		= p54_set_key,
	.config			= p54_config,
	.bss_info_changed	= p54_bss_info_changed,
	.configure_filter	= p54_configure_filter,
	.conf_tx		= p54_conf_tx,
	.get_stats		= p54_get_stats,
};

struct ieee80211_hw *p54_init_common(size_t priv_data_len)
{
	struct ieee80211_hw *dev;
	struct p54_common *priv;

	dev = ieee80211_alloc_hw(priv_data_len, &p54_ops);
	if (!dev)
		return NULL;

	priv = dev->priv;
	priv->hw = dev;
	priv->mode = NL80211_IFTYPE_UNSPECIFIED;
	priv->basic_rate_mask = 0x15f;
	spin_lock_init(&priv->tx_stats_lock);
	skb_queue_head_init(&priv->tx_queue);
	skb_queue_head_init(&priv->tx_pending);
	dev->flags = IEEE80211_HW_RX_INCLUDES_FCS |
		     IEEE80211_HW_SIGNAL_DBM |
		     IEEE80211_HW_SUPPORTS_PS |
		     IEEE80211_HW_PS_NULLFUNC_STACK |
		     IEEE80211_HW_BEACON_FILTER |
		     IEEE80211_HW_REPORTS_TX_ACK_STATUS;

	dev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				      BIT(NL80211_IFTYPE_ADHOC) |
				      BIT(NL80211_IFTYPE_AP) |
				      BIT(NL80211_IFTYPE_MESH_POINT);

	dev->channel_change_time = 1000;	/* TODO: find actual value */
	priv->beacon_req_id = cpu_to_le32(0);
	priv->tx_stats[P54_QUEUE_BEACON].limit = 1;
	priv->tx_stats[P54_QUEUE_FWSCAN].limit = 1;
	priv->tx_stats[P54_QUEUE_MGMT].limit = 3;
	priv->tx_stats[P54_QUEUE_CAB].limit = 3;
	priv->tx_stats[P54_QUEUE_DATA].limit = 5;
	dev->queues = 1;
	priv->noise = -94;
	/*
	 * We support at most 8 tries no matter which rate they're at,
	 * we cannot support max_rates * max_rate_tries as we set it
	 * here, but setting it correctly to 4/2 or so would limit us
	 * artificially if the RC algorithm wants just two rates, so
	 * let's say 4/7, we'll redistribute it at TX time, see the
	 * comments there.
	 */
	dev->max_rates = 4;
	dev->max_rate_tries = 7;
	dev->extra_tx_headroom = sizeof(struct p54_hdr) + 4 +
				 sizeof(struct p54_tx_data);

	/*
	 * For now, disable PS by default because it affects
	 * link stability significantly.
	 */
	dev->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	mutex_init(&priv->conf_mutex);
	mutex_init(&priv->eeprom_mutex);
	init_completion(&priv->eeprom_comp);
	init_completion(&priv->beacon_comp);
	INIT_DELAYED_WORK(&priv->work, p54_work);

	return dev;
}
EXPORT_SYMBOL_GPL(p54_init_common);

int p54_register_common(struct ieee80211_hw *dev, struct device *pdev)
{
	struct p54_common *priv = dev->priv;
	int err;

	err = ieee80211_register_hw(dev);
	if (err) {
		dev_err(pdev, "Cannot register device (%d).\n", err);
		return err;
	}

#ifdef CONFIG_P54_LEDS
	err = p54_init_leds(priv);
	if (err)
		return err;
#endif /* CONFIG_P54_LEDS */

	dev_info(pdev, "is registered as '%s'\n", wiphy_name(dev->wiphy));
	return 0;
}
EXPORT_SYMBOL_GPL(p54_register_common);

void p54_free_common(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	unsigned int i;

	for (i = 0; i < IEEE80211_NUM_BANDS; i++)
		kfree(priv->band_table[i]);

	kfree(priv->iq_autocal);
	kfree(priv->output_limit);
	kfree(priv->curve_data);
	kfree(priv->used_rxkeys);
	priv->iq_autocal = NULL;
	priv->output_limit = NULL;
	priv->curve_data = NULL;
	priv->used_rxkeys = NULL;
	ieee80211_free_hw(dev);
}
EXPORT_SYMBOL_GPL(p54_free_common);

void p54_unregister_common(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;

#ifdef CONFIG_P54_LEDS
	p54_unregister_leds(priv);
#endif /* CONFIG_P54_LEDS */

	ieee80211_unregister_hw(dev);
	mutex_destroy(&priv->conf_mutex);
	mutex_destroy(&priv->eeprom_mutex);
}
EXPORT_SYMBOL_GPL(p54_unregister_common);
