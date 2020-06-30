// SPDX-License-Identifier: GPL-2.0-or-later
/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

 */

/*
	Module: rt2x00mac
	Abstract: rt2x00 generic mac80211 routines.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

static int rt2x00mac_tx_rts_cts(struct rt2x00_dev *rt2x00dev,
				struct data_queue *queue,
				struct sk_buff *frag_skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(frag_skb);
	struct ieee80211_tx_info *rts_info;
	struct sk_buff *skb;
	unsigned int data_length;
	int retval = 0;

	if (tx_info->control.rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT)
		data_length = sizeof(struct ieee80211_cts);
	else
		data_length = sizeof(struct ieee80211_rts);

	skb = dev_alloc_skb(data_length + rt2x00dev->hw->extra_tx_headroom);
	if (unlikely(!skb)) {
		rt2x00_warn(rt2x00dev, "Failed to create RTS/CTS frame\n");
		return -ENOMEM;
	}

	skb_reserve(skb, rt2x00dev->hw->extra_tx_headroom);
	skb_put(skb, data_length);

	/*
	 * Copy TX information over from original frame to
	 * RTS/CTS frame. Note that we set the no encryption flag
	 * since we don't want this frame to be encrypted.
	 * RTS frames should be acked, while CTS-to-self frames
	 * should not. The ready for TX flag is cleared to prevent
	 * it being automatically send when the descriptor is
	 * written to the hardware.
	 */
	memcpy(skb->cb, frag_skb->cb, sizeof(skb->cb));
	rts_info = IEEE80211_SKB_CB(skb);
	rts_info->control.rates[0].flags &= ~IEEE80211_TX_RC_USE_RTS_CTS;
	rts_info->control.rates[0].flags &= ~IEEE80211_TX_RC_USE_CTS_PROTECT;

	if (tx_info->control.rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT)
		rts_info->flags |= IEEE80211_TX_CTL_NO_ACK;
	else
		rts_info->flags &= ~IEEE80211_TX_CTL_NO_ACK;

	/* Disable hardware encryption */
	rts_info->control.hw_key = NULL;

	/*
	 * RTS/CTS frame should use the length of the frame plus any
	 * encryption overhead that will be added by the hardware.
	 */
	data_length += rt2x00crypto_tx_overhead(rt2x00dev, skb);

	if (tx_info->control.rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT)
		ieee80211_ctstoself_get(rt2x00dev->hw, tx_info->control.vif,
					frag_skb->data, data_length, tx_info,
					(struct ieee80211_cts *)(skb->data));
	else
		ieee80211_rts_get(rt2x00dev->hw, tx_info->control.vif,
				  frag_skb->data, data_length, tx_info,
				  (struct ieee80211_rts *)(skb->data));

	retval = rt2x00queue_write_tx_frame(queue, skb, NULL, true);
	if (retval) {
		dev_kfree_skb_any(skb);
		rt2x00_warn(rt2x00dev, "Failed to send RTS/CTS frame\n");
	}

	return retval;
}

void rt2x00mac_tx(struct ieee80211_hw *hw,
		  struct ieee80211_tx_control *control,
		  struct sk_buff *skb)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	enum data_queue_qid qid = skb_get_queue_mapping(skb);
	struct data_queue *queue = NULL;

	/*
	 * Mac80211 might be calling this function while we are trying
	 * to remove the device or perhaps suspending it.
	 * Note that we can only stop the TX queues inside the TX path
	 * due to possible race conditions in mac80211.
	 */
	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		goto exit_free_skb;

	/*
	 * Use the ATIM queue if appropriate and present.
	 */
	if (tx_info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM &&
	    rt2x00_has_cap_flag(rt2x00dev, REQUIRE_ATIM_QUEUE))
		qid = QID_ATIM;

	queue = rt2x00queue_get_tx_queue(rt2x00dev, qid);
	if (unlikely(!queue)) {
		rt2x00_err(rt2x00dev,
			   "Attempt to send packet over invalid queue %d\n"
			   "Please file bug report to %s\n", qid, DRV_PROJECT);
		goto exit_free_skb;
	}

	/*
	 * If CTS/RTS is required. create and queue that frame first.
	 * Make sure we have at least enough entries available to send
	 * this CTS/RTS frame as well as the data frame.
	 * Note that when the driver has set the set_rts_threshold()
	 * callback function it doesn't need software generation of
	 * either RTS or CTS-to-self frame and handles everything
	 * inside the hardware.
	 */
	if (!rt2x00dev->ops->hw->set_rts_threshold &&
	    (tx_info->control.rates[0].flags & (IEEE80211_TX_RC_USE_RTS_CTS |
						IEEE80211_TX_RC_USE_CTS_PROTECT))) {
		if (rt2x00queue_available(queue) <= 1) {
			/*
			 * Recheck for full queue under lock to avoid race
			 * conditions with rt2x00lib_txdone().
			 */
			spin_lock(&queue->tx_lock);
			if (rt2x00queue_threshold(queue))
				rt2x00queue_pause_queue(queue);
			spin_unlock(&queue->tx_lock);

			goto exit_free_skb;
		}

		if (rt2x00mac_tx_rts_cts(rt2x00dev, queue, skb))
			goto exit_free_skb;
	}

	if (unlikely(rt2x00queue_write_tx_frame(queue, skb, control->sta, false)))
		goto exit_free_skb;

	return;

 exit_free_skb:
	ieee80211_free_txskb(hw, skb);
}
EXPORT_SYMBOL_GPL(rt2x00mac_tx);

int rt2x00mac_start(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return 0;

	if (test_bit(DEVICE_STATE_STARTED, &rt2x00dev->flags)) {
		/*
		 * This is special case for ieee80211_restart_hw(), otherwise
		 * mac80211 never call start() two times in row without stop();
		 */
		set_bit(DEVICE_STATE_RESET, &rt2x00dev->flags);
		rt2x00dev->ops->lib->pre_reset_hw(rt2x00dev);
		rt2x00lib_stop(rt2x00dev);
	}
	return rt2x00lib_start(rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00mac_start);

void rt2x00mac_stop(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return;

	rt2x00lib_stop(rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00mac_stop);

void
rt2x00mac_reconfig_complete(struct ieee80211_hw *hw,
			    enum ieee80211_reconfig_type reconfig_type)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	if (reconfig_type == IEEE80211_RECONFIG_TYPE_RESTART)
		clear_bit(DEVICE_STATE_RESET, &rt2x00dev->flags);
}
EXPORT_SYMBOL_GPL(rt2x00mac_reconfig_complete);

int rt2x00mac_add_interface(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct rt2x00_intf *intf = vif_to_intf(vif);
	struct data_queue *queue = rt2x00dev->bcn;
	struct queue_entry *entry = NULL;
	unsigned int i;

	/*
	 * Don't allow interfaces to be added
	 * the device has disappeared.
	 */
	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags) ||
	    !test_bit(DEVICE_STATE_STARTED, &rt2x00dev->flags))
		return -ENODEV;

	/*
	 * Loop through all beacon queues to find a free
	 * entry. Since there are as much beacon entries
	 * as the maximum interfaces, this search shouldn't
	 * fail.
	 */
	for (i = 0; i < queue->limit; i++) {
		entry = &queue->entries[i];
		if (!test_and_set_bit(ENTRY_BCN_ASSIGNED, &entry->flags))
			break;
	}

	if (unlikely(i == queue->limit))
		return -ENOBUFS;

	/*
	 * We are now absolutely sure the interface can be created,
	 * increase interface count and start initialization.
	 */

	if (vif->type == NL80211_IFTYPE_AP)
		rt2x00dev->intf_ap_count++;
	else
		rt2x00dev->intf_sta_count++;

	mutex_init(&intf->beacon_skb_mutex);
	intf->beacon = entry;

	/*
	 * The MAC address must be configured after the device
	 * has been initialized. Otherwise the device can reset
	 * the MAC registers.
	 * The BSSID address must only be configured in AP mode,
	 * however we should not send an empty BSSID address for
	 * STA interfaces at this time, since this can cause
	 * invalid behavior in the device.
	 */
	rt2x00lib_config_intf(rt2x00dev, intf, vif->type,
			      vif->addr, NULL);

	/*
	 * Some filters depend on the current working mode. We can force
	 * an update during the next configure_filter() run by mac80211 by
	 * resetting the current packet_filter state.
	 */
	rt2x00dev->packet_filter = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_add_interface);

void rt2x00mac_remove_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct rt2x00_intf *intf = vif_to_intf(vif);

	/*
	 * Don't allow interfaces to be remove while
	 * either the device has disappeared or when
	 * no interface is present.
	 */
	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags) ||
	    (vif->type == NL80211_IFTYPE_AP && !rt2x00dev->intf_ap_count) ||
	    (vif->type != NL80211_IFTYPE_AP && !rt2x00dev->intf_sta_count))
		return;

	if (vif->type == NL80211_IFTYPE_AP)
		rt2x00dev->intf_ap_count--;
	else
		rt2x00dev->intf_sta_count--;

	/*
	 * Release beacon entry so it is available for
	 * new interfaces again.
	 */
	clear_bit(ENTRY_BCN_ASSIGNED, &intf->beacon->flags);

	/*
	 * Make sure the bssid and mac address registers
	 * are cleared to prevent false ACKing of frames.
	 */
	rt2x00lib_config_intf(rt2x00dev, intf,
			      NL80211_IFTYPE_UNSPECIFIED, NULL, NULL);
}
EXPORT_SYMBOL_GPL(rt2x00mac_remove_interface);

int rt2x00mac_config(struct ieee80211_hw *hw, u32 changed)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;

	/*
	 * mac80211 might be calling this function while we are trying
	 * to remove the device or perhaps suspending it.
	 */
	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return 0;

	/*
	 * Some configuration parameters (e.g. channel and antenna values) can
	 * only be set when the radio is enabled, but do require the RX to
	 * be off. During this period we should keep link tuning enabled,
	 * if for any reason the link tuner must be reset, this will be
	 * handled by rt2x00lib_config().
	 */
	rt2x00queue_stop_queue(rt2x00dev->rx);

	/* Do not race with with link tuner. */
	mutex_lock(&rt2x00dev->conf_mutex);

	/*
	 * When we've just turned on the radio, we want to reprogram
	 * everything to ensure a consistent state
	 */
	rt2x00lib_config(rt2x00dev, conf, changed);

	/*
	 * After the radio has been enabled we need to configure
	 * the antenna to the default settings. rt2x00lib_config_antenna()
	 * should determine if any action should be taken based on
	 * checking if diversity has been enabled or no antenna changes
	 * have been made since the last configuration change.
	 */
	rt2x00lib_config_antenna(rt2x00dev, rt2x00dev->default_ant);

	mutex_unlock(&rt2x00dev->conf_mutex);

	/* Turn RX back on */
	rt2x00queue_start_queue(rt2x00dev->rx);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_config);

void rt2x00mac_configure_filter(struct ieee80211_hw *hw,
				unsigned int changed_flags,
				unsigned int *total_flags,
				u64 multicast)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	/*
	 * Mask off any flags we are going to ignore
	 * from the total_flags field.
	 */
	*total_flags &=
	    FIF_ALLMULTI |
	    FIF_FCSFAIL |
	    FIF_PLCPFAIL |
	    FIF_CONTROL |
	    FIF_PSPOLL |
	    FIF_OTHER_BSS;

	/*
	 * Apply some rules to the filters:
	 * - Some filters imply different filters to be set.
	 * - Some things we can't filter out at all.
	 * - Multicast filter seems to kill broadcast traffic so never use it.
	 */
	*total_flags |= FIF_ALLMULTI;

	/*
	 * If the device has a single filter for all control frames,
	 * FIF_CONTROL and FIF_PSPOLL flags imply each other.
	 * And if the device has more than one filter for control frames
	 * of different types, but has no a separate filter for PS Poll frames,
	 * FIF_CONTROL flag implies FIF_PSPOLL.
	 */
	if (!rt2x00_has_cap_control_filters(rt2x00dev)) {
		if (*total_flags & FIF_CONTROL || *total_flags & FIF_PSPOLL)
			*total_flags |= FIF_CONTROL | FIF_PSPOLL;
	}
	if (!rt2x00_has_cap_control_filter_pspoll(rt2x00dev)) {
		if (*total_flags & FIF_CONTROL)
			*total_flags |= FIF_PSPOLL;
	}

	rt2x00dev->packet_filter = *total_flags;

	rt2x00dev->ops->lib->config_filter(rt2x00dev, *total_flags);
}
EXPORT_SYMBOL_GPL(rt2x00mac_configure_filter);

static void rt2x00mac_set_tim_iter(void *data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct rt2x00_intf *intf = vif_to_intf(vif);

	if (vif->type != NL80211_IFTYPE_AP &&
	    vif->type != NL80211_IFTYPE_ADHOC &&
	    vif->type != NL80211_IFTYPE_MESH_POINT &&
	    vif->type != NL80211_IFTYPE_WDS)
		return;

	set_bit(DELAYED_UPDATE_BEACON, &intf->delayed_flags);
}

int rt2x00mac_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
		      bool set)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return 0;

	ieee80211_iterate_active_interfaces_atomic(
		rt2x00dev->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
		rt2x00mac_set_tim_iter, rt2x00dev);

	/* queue work to upodate the beacon template */
	ieee80211_queue_work(rt2x00dev->hw, &rt2x00dev->intf_work);
	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_set_tim);

#ifdef CONFIG_RT2X00_LIB_CRYPTO
static void memcpy_tkip(struct rt2x00lib_crypto *crypto, u8 *key, u8 key_len)
{
	if (key_len > NL80211_TKIP_DATA_OFFSET_ENCR_KEY)
		memcpy(crypto->key,
		       &key[NL80211_TKIP_DATA_OFFSET_ENCR_KEY],
		       sizeof(crypto->key));

	if (key_len > NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY)
		memcpy(crypto->tx_mic,
		       &key[NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY],
		       sizeof(crypto->tx_mic));

	if (key_len > NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY)
		memcpy(crypto->rx_mic,
		       &key[NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY],
		       sizeof(crypto->rx_mic));
}

int rt2x00mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		      struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		      struct ieee80211_key_conf *key)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	int (*set_key) (struct rt2x00_dev *rt2x00dev,
			struct rt2x00lib_crypto *crypto,
			struct ieee80211_key_conf *key);
	struct rt2x00lib_crypto crypto;
	static const u8 bcast_addr[ETH_ALEN] =
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, };
	struct rt2x00_sta *sta_priv = NULL;

	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return 0;

	/* The hardware can't do MFP */
	if (!rt2x00_has_cap_hw_crypto(rt2x00dev) || (sta && sta->mfp))
		return -EOPNOTSUPP;

	/*
	 * To support IBSS RSN, don't program group keys in IBSS, the
	 * hardware will then not attempt to decrypt the frames.
	 */
	if (vif->type == NL80211_IFTYPE_ADHOC &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return -EOPNOTSUPP;

	if (key->keylen > 32)
		return -ENOSPC;

	memset(&crypto, 0, sizeof(crypto));

	crypto.bssidx = rt2x00lib_get_bssidx(rt2x00dev, vif);
	crypto.cipher = rt2x00crypto_key_to_cipher(key);
	if (crypto.cipher == CIPHER_NONE)
		return -EOPNOTSUPP;
	if (crypto.cipher == CIPHER_TKIP && rt2x00_is_usb(rt2x00dev))
		return -EOPNOTSUPP;

	crypto.cmd = cmd;

	if (sta) {
		crypto.address = sta->addr;
		sta_priv = sta_to_rt2x00_sta(sta);
		crypto.wcid = sta_priv->wcid;
	} else
		crypto.address = bcast_addr;

	if (crypto.cipher == CIPHER_TKIP)
		memcpy_tkip(&crypto, &key->key[0], key->keylen);
	else
		memcpy(crypto.key, &key->key[0], key->keylen);
	/*
	 * Each BSS has a maximum of 4 shared keys.
	 * Shared key index values:
	 *	0) BSS0 key0
	 *	1) BSS0 key1
	 *	...
	 *	4) BSS1 key0
	 *	...
	 *	8) BSS2 key0
	 *	...
	 * Both pairwise as shared key indeces are determined by
	 * driver. This is required because the hardware requires
	 * keys to be assigned in correct order (When key 1 is
	 * provided but key 0 is not, then the key is not found
	 * by the hardware during RX).
	 */
	if (cmd == SET_KEY)
		key->hw_key_idx = 0;

	if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
		set_key = rt2x00dev->ops->lib->config_pairwise_key;
	else
		set_key = rt2x00dev->ops->lib->config_shared_key;

	if (!set_key)
		return -EOPNOTSUPP;

	return set_key(rt2x00dev, &crypto, key);
}
EXPORT_SYMBOL_GPL(rt2x00mac_set_key);
#endif /* CONFIG_RT2X00_LIB_CRYPTO */

void rt2x00mac_sw_scan_start(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     const u8 *mac_addr)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	set_bit(DEVICE_STATE_SCANNING, &rt2x00dev->flags);
	rt2x00link_stop_tuner(rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00mac_sw_scan_start);

void rt2x00mac_sw_scan_complete(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	clear_bit(DEVICE_STATE_SCANNING, &rt2x00dev->flags);
	rt2x00link_start_tuner(rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00mac_sw_scan_complete);

int rt2x00mac_get_stats(struct ieee80211_hw *hw,
			struct ieee80211_low_level_stats *stats)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	/*
	 * The dot11ACKFailureCount, dot11RTSFailureCount and
	 * dot11RTSSuccessCount are updated in interrupt time.
	 * dot11FCSErrorCount is updated in the link tuner.
	 */
	memcpy(stats, &rt2x00dev->low_level_stats, sizeof(*stats));

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_get_stats);

void rt2x00mac_bss_info_changed(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *bss_conf,
				u32 changes)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct rt2x00_intf *intf = vif_to_intf(vif);

	/*
	 * mac80211 might be calling this function while we are trying
	 * to remove the device or perhaps suspending it.
	 */
	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return;

	/*
	 * Update the BSSID.
	 */
	if (changes & BSS_CHANGED_BSSID)
		rt2x00lib_config_intf(rt2x00dev, intf, vif->type, NULL,
				      bss_conf->bssid);

	/*
	 * Start/stop beaconing.
	 */
	if (changes & BSS_CHANGED_BEACON_ENABLED) {
		mutex_lock(&intf->beacon_skb_mutex);
		if (!bss_conf->enable_beacon && intf->enable_beacon) {
			rt2x00dev->intf_beaconing--;
			intf->enable_beacon = false;

			if (rt2x00dev->intf_beaconing == 0) {
				/*
				 * Last beaconing interface disabled
				 * -> stop beacon queue.
				 */
				rt2x00queue_stop_queue(rt2x00dev->bcn);
			}
			/*
			 * Clear beacon in the H/W for this vif. This is needed
			 * to disable beaconing on this particular interface
			 * and keep it running on other interfaces.
			 */
			rt2x00queue_clear_beacon(rt2x00dev, vif);
		} else if (bss_conf->enable_beacon && !intf->enable_beacon) {
			rt2x00dev->intf_beaconing++;
			intf->enable_beacon = true;
			/*
			 * Upload beacon to the H/W. This is only required on
			 * USB devices. PCI devices fetch beacons periodically.
			 */
			if (rt2x00_is_usb(rt2x00dev))
				rt2x00queue_update_beacon(rt2x00dev, vif);

			if (rt2x00dev->intf_beaconing == 1) {
				/*
				 * First beaconing interface enabled
				 * -> start beacon queue.
				 */
				rt2x00queue_start_queue(rt2x00dev->bcn);
			}
		}
		mutex_unlock(&intf->beacon_skb_mutex);
	}

	/*
	 * When the association status has changed we must reset the link
	 * tuner counter. This is because some drivers determine if they
	 * should perform link tuning based on the number of seconds
	 * while associated or not associated.
	 */
	if (changes & BSS_CHANGED_ASSOC) {
		rt2x00dev->link.count = 0;

		if (bss_conf->assoc)
			rt2x00dev->intf_associated++;
		else
			rt2x00dev->intf_associated--;

		rt2x00leds_led_assoc(rt2x00dev, !!rt2x00dev->intf_associated);
	}

	/*
	 * When the erp information has changed, we should perform
	 * additional configuration steps. For all other changes we are done.
	 */
	if (changes & (BSS_CHANGED_ERP_CTS_PROT | BSS_CHANGED_ERP_PREAMBLE |
		       BSS_CHANGED_ERP_SLOT | BSS_CHANGED_BASIC_RATES |
		       BSS_CHANGED_BEACON_INT | BSS_CHANGED_HT))
		rt2x00lib_config_erp(rt2x00dev, intf, bss_conf, changes);
}
EXPORT_SYMBOL_GPL(rt2x00mac_bss_info_changed);

int rt2x00mac_conf_tx(struct ieee80211_hw *hw,
		      struct ieee80211_vif *vif, u16 queue_idx,
		      const struct ieee80211_tx_queue_params *params)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct data_queue *queue;

	queue = rt2x00queue_get_tx_queue(rt2x00dev, queue_idx);
	if (unlikely(!queue))
		return -EINVAL;

	/*
	 * The passed variables are stored as real value ((2^n)-1).
	 * Ralink registers require to know the bit number 'n'.
	 */
	if (params->cw_min > 0)
		queue->cw_min = fls(params->cw_min);
	else
		queue->cw_min = 5; /* cw_min: 2^5 = 32. */

	if (params->cw_max > 0)
		queue->cw_max = fls(params->cw_max);
	else
		queue->cw_max = 10; /* cw_min: 2^10 = 1024. */

	queue->aifs = params->aifs;
	queue->txop = params->txop;

	rt2x00_dbg(rt2x00dev,
		   "Configured TX queue %d - CWmin: %d, CWmax: %d, Aifs: %d, TXop: %d\n",
		   queue_idx, queue->cw_min, queue->cw_max, queue->aifs,
		   queue->txop);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_conf_tx);

void rt2x00mac_rfkill_poll(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	bool active = !!rt2x00dev->ops->lib->rfkill_poll(rt2x00dev);

	wiphy_rfkill_set_hw_state(hw->wiphy, !active);
}
EXPORT_SYMBOL_GPL(rt2x00mac_rfkill_poll);

void rt2x00mac_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		     u32 queues, bool drop)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct data_queue *queue;

	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return;

	set_bit(DEVICE_STATE_FLUSHING, &rt2x00dev->flags);

	tx_queue_for_each(rt2x00dev, queue)
		rt2x00queue_flush_queue(queue, drop);

	clear_bit(DEVICE_STATE_FLUSHING, &rt2x00dev->flags);
}
EXPORT_SYMBOL_GPL(rt2x00mac_flush);

int rt2x00mac_set_antenna(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct link_ant *ant = &rt2x00dev->link.ant;
	struct antenna_setup *def = &rt2x00dev->default_ant;
	struct antenna_setup setup;

	// The antenna value is not supposed to be 0,
	// or exceed the maximum number of antenna's.
	if (!tx_ant || (tx_ant & ~3) || !rx_ant || (rx_ant & ~3))
		return -EINVAL;

	// When the client tried to configure the antenna to or from
	// diversity mode, we must reset the default antenna as well
	// as that controls the diversity switch.
	if (ant->flags & ANTENNA_TX_DIVERSITY && tx_ant != 3)
		ant->flags &= ~ANTENNA_TX_DIVERSITY;
	if (ant->flags & ANTENNA_RX_DIVERSITY && rx_ant != 3)
		ant->flags &= ~ANTENNA_RX_DIVERSITY;

	// If diversity is being enabled, check if we need hardware
	// or software diversity. In the latter case, reset the value,
	// and make sure we update the antenna flags to have the
	// link tuner pick up the diversity tuning.
	if (tx_ant == 3 && def->tx == ANTENNA_SW_DIVERSITY) {
		tx_ant = ANTENNA_SW_DIVERSITY;
		ant->flags |= ANTENNA_TX_DIVERSITY;
	}

	if (rx_ant == 3 && def->rx == ANTENNA_SW_DIVERSITY) {
		rx_ant = ANTENNA_SW_DIVERSITY;
		ant->flags |= ANTENNA_RX_DIVERSITY;
	}

	setup.tx = tx_ant;
	setup.rx = rx_ant;
	setup.rx_chain_num = 0;
	setup.tx_chain_num = 0;

	rt2x00lib_config_antenna(rt2x00dev, setup);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_set_antenna);

int rt2x00mac_get_antenna(struct ieee80211_hw *hw, u32 *tx_ant, u32 *rx_ant)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct link_ant *ant = &rt2x00dev->link.ant;
	struct antenna_setup *active = &rt2x00dev->link.ant.active;

	// When software diversity is active, we must report this to the
	// client and not the current active antenna state.
	if (ant->flags & ANTENNA_TX_DIVERSITY)
		*tx_ant = ANTENNA_HW_DIVERSITY;
	else
		*tx_ant = active->tx;

	if (ant->flags & ANTENNA_RX_DIVERSITY)
		*rx_ant = ANTENNA_HW_DIVERSITY;
	else
		*rx_ant = active->rx;

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_get_antenna);

void rt2x00mac_get_ringparam(struct ieee80211_hw *hw,
			     u32 *tx, u32 *tx_max, u32 *rx, u32 *rx_max)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct data_queue *queue;

	tx_queue_for_each(rt2x00dev, queue) {
		*tx += queue->length;
		*tx_max += queue->limit;
	}

	*rx = rt2x00dev->rx->length;
	*rx_max = rt2x00dev->rx->limit;
}
EXPORT_SYMBOL_GPL(rt2x00mac_get_ringparam);

bool rt2x00mac_tx_frames_pending(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct data_queue *queue;

	tx_queue_for_each(rt2x00dev, queue) {
		if (!rt2x00queue_empty(queue))
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(rt2x00mac_tx_frames_pending);
