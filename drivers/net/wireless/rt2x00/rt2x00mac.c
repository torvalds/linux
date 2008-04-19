/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
				struct sk_buff *frag_skb,
				struct ieee80211_tx_control *control)
{
	struct skb_frame_desc *skbdesc;
	struct sk_buff *skb;
	int size;

	if (control->flags & IEEE80211_TXCTL_USE_CTS_PROTECT)
		size = sizeof(struct ieee80211_cts);
	else
		size = sizeof(struct ieee80211_rts);

	skb = dev_alloc_skb(size + rt2x00dev->hw->extra_tx_headroom);
	if (!skb) {
		WARNING(rt2x00dev, "Failed to create RTS/CTS frame.\n");
		return NETDEV_TX_BUSY;
	}

	skb_reserve(skb, rt2x00dev->hw->extra_tx_headroom);
	skb_put(skb, size);

	if (control->flags & IEEE80211_TXCTL_USE_CTS_PROTECT)
		ieee80211_ctstoself_get(rt2x00dev->hw, control->vif,
					frag_skb->data, frag_skb->len, control,
					(struct ieee80211_cts *)(skb->data));
	else
		ieee80211_rts_get(rt2x00dev->hw, control->vif,
				  frag_skb->data, frag_skb->len, control,
				  (struct ieee80211_rts *)(skb->data));

	/*
	 * Initialize skb descriptor
	 */
	skbdesc = get_skb_frame_desc(skb);
	memset(skbdesc, 0, sizeof(*skbdesc));
	skbdesc->flags |= FRAME_DESC_DRIVER_GENERATED;

	if (rt2x00dev->ops->lib->write_tx_data(rt2x00dev, queue, skb, control)) {
		WARNING(rt2x00dev, "Failed to send RTS/CTS frame.\n");
		return NETDEV_TX_BUSY;
	}

	return NETDEV_TX_OK;
}

int rt2x00mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb,
		 struct ieee80211_tx_control *control)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct ieee80211_hdr *ieee80211hdr = (struct ieee80211_hdr *)skb->data;
	struct data_queue *queue;
	struct skb_frame_desc *skbdesc;
	u16 frame_control;

	/*
	 * Mac80211 might be calling this function while we are trying
	 * to remove the device or perhaps suspending it.
	 * Note that we can only stop the TX queues inside the TX path
	 * due to possible race conditions in mac80211.
	 */
	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags)) {
		ieee80211_stop_queues(hw);
		return NETDEV_TX_OK;
	}

	/*
	 * Determine which queue to put packet on.
	 */
	if (control->flags & IEEE80211_TXCTL_SEND_AFTER_DTIM &&
	    test_bit(DRIVER_REQUIRE_ATIM_QUEUE, &rt2x00dev->flags))
		queue = rt2x00queue_get_queue(rt2x00dev, RT2X00_BCN_QUEUE_ATIM);
	else
		queue = rt2x00queue_get_queue(rt2x00dev, control->queue);
	if (unlikely(!queue)) {
		ERROR(rt2x00dev,
		      "Attempt to send packet over invalid queue %d.\n"
		      "Please file bug report to %s.\n",
		      control->queue, DRV_PROJECT);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/*
	 * If CTS/RTS is required. and this frame is not CTS or RTS,
	 * create and queue that frame first. But make sure we have
	 * at least enough entries available to send this CTS/RTS
	 * frame as well as the data frame.
	 */
	frame_control = le16_to_cpu(ieee80211hdr->frame_control);
	if (!is_rts_frame(frame_control) && !is_cts_frame(frame_control) &&
	    (control->flags & (IEEE80211_TXCTL_USE_RTS_CTS |
			       IEEE80211_TXCTL_USE_CTS_PROTECT))) {
		if (rt2x00queue_available(queue) <= 1) {
			ieee80211_stop_queue(rt2x00dev->hw, control->queue);
			return NETDEV_TX_BUSY;
		}

		if (rt2x00mac_tx_rts_cts(rt2x00dev, queue, skb, control)) {
			ieee80211_stop_queue(rt2x00dev->hw, control->queue);
			return NETDEV_TX_BUSY;
		}
	}

	/*
	 * Initialize skb descriptor
	 */
	skbdesc = get_skb_frame_desc(skb);
	memset(skbdesc, 0, sizeof(*skbdesc));

	if (rt2x00dev->ops->lib->write_tx_data(rt2x00dev, queue, skb, control)) {
		ieee80211_stop_queue(rt2x00dev->hw, control->queue);
		return NETDEV_TX_BUSY;
	}

	if (rt2x00queue_full(queue))
		ieee80211_stop_queue(rt2x00dev->hw, control->queue);

	if (rt2x00dev->ops->lib->kick_tx_queue)
		rt2x00dev->ops->lib->kick_tx_queue(rt2x00dev, control->queue);

	return NETDEV_TX_OK;
}
EXPORT_SYMBOL_GPL(rt2x00mac_tx);

int rt2x00mac_start(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags))
		return 0;

	return rt2x00lib_start(rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00mac_start);

void rt2x00mac_stop(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags))
		return;

	rt2x00lib_stop(rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00mac_stop);

int rt2x00mac_add_interface(struct ieee80211_hw *hw,
			    struct ieee80211_if_init_conf *conf)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct rt2x00_intf *intf = vif_to_intf(conf->vif);
	struct data_queue *queue =
	    rt2x00queue_get_queue(rt2x00dev, RT2X00_BCN_QUEUE_BEACON);
	struct queue_entry *entry = NULL;
	unsigned int i;

	/*
	 * Don't allow interfaces to be added
	 * the device has disappeared.
	 */
	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags) ||
	    !test_bit(DEVICE_STARTED, &rt2x00dev->flags))
		return -ENODEV;

	/*
	 * When we don't support mixed interfaces (a combination
	 * of sta and ap virtual interfaces) then we can only
	 * add this interface when the rival interface count is 0.
	 */
	if (!test_bit(DRIVER_SUPPORT_MIXED_INTERFACES, &rt2x00dev->flags) &&
	    ((conf->type == IEEE80211_IF_TYPE_AP && rt2x00dev->intf_sta_count) ||
	     (conf->type != IEEE80211_IF_TYPE_AP && rt2x00dev->intf_ap_count)))
		return -ENOBUFS;

	/*
	 * Check if we exceeded the maximum amount of supported interfaces.
	 */
	if ((conf->type == IEEE80211_IF_TYPE_AP &&
	     rt2x00dev->intf_ap_count >= rt2x00dev->ops->max_ap_intf) ||
	    (conf->type != IEEE80211_IF_TYPE_AP &&
	     rt2x00dev->intf_sta_count >= rt2x00dev->ops->max_sta_intf))
		return -ENOBUFS;

	/*
	 * Loop through all beacon queues to find a free
	 * entry. Since there are as much beacon entries
	 * as the maximum interfaces, this search shouldn't
	 * fail.
	 */
	for (i = 0; i < queue->limit; i++) {
		entry = &queue->entries[i];
		if (!__test_and_set_bit(ENTRY_BCN_ASSIGNED, &entry->flags))
			break;
	}

	if (unlikely(i == queue->limit))
		return -ENOBUFS;

	/*
	 * We are now absolutely sure the interface can be created,
	 * increase interface count and start initialization.
	 */

	if (conf->type == IEEE80211_IF_TYPE_AP)
		rt2x00dev->intf_ap_count++;
	else
		rt2x00dev->intf_sta_count++;

	spin_lock_init(&intf->lock);
	intf->beacon = entry;

	if (conf->type == IEEE80211_IF_TYPE_AP)
		memcpy(&intf->bssid, conf->mac_addr, ETH_ALEN);
	memcpy(&intf->mac, conf->mac_addr, ETH_ALEN);

	/*
	 * The MAC adddress must be configured after the device
	 * has been initialized. Otherwise the device can reset
	 * the MAC registers.
	 */
	rt2x00lib_config_intf(rt2x00dev, intf, conf->type, intf->mac, NULL);

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
				struct ieee80211_if_init_conf *conf)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct rt2x00_intf *intf = vif_to_intf(conf->vif);

	/*
	 * Don't allow interfaces to be remove while
	 * either the device has disappeared or when
	 * no interface is present.
	 */
	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags) ||
	    (conf->type == IEEE80211_IF_TYPE_AP && !rt2x00dev->intf_ap_count) ||
	    (conf->type != IEEE80211_IF_TYPE_AP && !rt2x00dev->intf_sta_count))
		return;

	if (conf->type == IEEE80211_IF_TYPE_AP)
		rt2x00dev->intf_ap_count--;
	else
		rt2x00dev->intf_sta_count--;

	/*
	 * Release beacon entry so it is available for
	 * new interfaces again.
	 */
	__clear_bit(ENTRY_BCN_ASSIGNED, &intf->beacon->flags);

	/*
	 * Make sure the bssid and mac address registers
	 * are cleared to prevent false ACKing of frames.
	 */
	rt2x00lib_config_intf(rt2x00dev, intf,
			      IEEE80211_IF_TYPE_INVALID, NULL, NULL);
}
EXPORT_SYMBOL_GPL(rt2x00mac_remove_interface);

int rt2x00mac_config(struct ieee80211_hw *hw, struct ieee80211_conf *conf)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	/*
	 * Mac80211 might be calling this function while we are trying
	 * to remove the device or perhaps suspending it.
	 */
	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags))
		return 0;

	/*
	 * Check if we need to disable the radio,
	 * if this is not the case, at least the RX must be disabled.
	 */
	if (test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags)) {
		if (!conf->radio_enabled)
			rt2x00lib_disable_radio(rt2x00dev);
		else
			rt2x00lib_toggle_rx(rt2x00dev, STATE_RADIO_RX_OFF);
	}

	rt2x00lib_config(rt2x00dev, conf, 0);

	/*
	 * Reenable RX only if the radio should be on.
	 */
	if (test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2x00lib_toggle_rx(rt2x00dev, STATE_RADIO_RX_ON);
	else if (conf->radio_enabled)
		return rt2x00lib_enable_radio(rt2x00dev);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_config);

int rt2x00mac_config_interface(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_if_conf *conf)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct rt2x00_intf *intf = vif_to_intf(vif);
	int status;

	/*
	 * Mac80211 might be calling this function while we are trying
	 * to remove the device or perhaps suspending it.
	 */
	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags))
		return 0;

	spin_lock(&intf->lock);

	/*
	 * If the interface does not work in master mode,
	 * then the bssid value in the interface structure
	 * should now be set.
	 */
	if (conf->type != IEEE80211_IF_TYPE_AP)
		memcpy(&intf->bssid, conf->bssid, ETH_ALEN);

	spin_unlock(&intf->lock);

	/*
	 * Call rt2x00_config_intf() outside of the spinlock context since
	 * the call will sleep for USB drivers. By using the ieee80211_if_conf
	 * values as arguments we make keep access to rt2x00_intf thread safe
	 * even without the lock.
	 */
	rt2x00lib_config_intf(rt2x00dev, intf, conf->type, NULL, conf->bssid);

	/*
	 * We only need to initialize the beacon when master mode is enabled.
	 */
	if (conf->type != IEEE80211_IF_TYPE_AP || !conf->beacon)
		return 0;

	status = rt2x00dev->ops->hw->beacon_update(rt2x00dev->hw,
						   conf->beacon,
						   conf->beacon_control);
	if (status)
		dev_kfree_skb(conf->beacon);

	return status;
}
EXPORT_SYMBOL_GPL(rt2x00mac_config_interface);

void rt2x00mac_configure_filter(struct ieee80211_hw *hw,
				unsigned int changed_flags,
				unsigned int *total_flags,
				int mc_count, struct dev_addr_list *mc_list)
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
	    FIF_OTHER_BSS |
	    FIF_PROMISC_IN_BSS;

	/*
	 * Apply some rules to the filters:
	 * - Some filters imply different filters to be set.
	 * - Some things we can't filter out at all.
	 * - Multicast filter seems to kill broadcast traffic so never use it.
	 */
	*total_flags |= FIF_ALLMULTI;
	if (*total_flags & FIF_OTHER_BSS ||
	    *total_flags & FIF_PROMISC_IN_BSS)
		*total_flags |= FIF_PROMISC_IN_BSS | FIF_OTHER_BSS;

	/*
	 * Check if there is any work left for us.
	 */
	if (rt2x00dev->packet_filter == *total_flags)
		return;
	rt2x00dev->packet_filter = *total_flags;

	if (!test_bit(DRIVER_REQUIRE_SCHEDULED, &rt2x00dev->flags))
		rt2x00dev->ops->lib->config_filter(rt2x00dev, *total_flags);
	else
		queue_work(rt2x00dev->hw->workqueue, &rt2x00dev->filter_work);
}
EXPORT_SYMBOL_GPL(rt2x00mac_configure_filter);

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

int rt2x00mac_get_tx_stats(struct ieee80211_hw *hw,
			   struct ieee80211_tx_queue_stats *stats)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	unsigned int i;

	for (i = 0; i < hw->queues; i++) {
		stats->data[i].len = rt2x00dev->tx[i].length;
		stats->data[i].limit = rt2x00dev->tx[i].limit;
		stats->data[i].count = rt2x00dev->tx[i].count;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_get_tx_stats);

void rt2x00mac_bss_info_changed(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *bss_conf,
				u32 changes)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct rt2x00_intf *intf = vif_to_intf(vif);
	unsigned int delayed = 0;

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

		if (!test_bit(DRIVER_REQUIRE_SCHEDULED, &rt2x00dev->flags))
			rt2x00leds_led_assoc(rt2x00dev,
					     !!rt2x00dev->intf_associated);
		else
			delayed |= DELAYED_LED_ASSOC;
	}

	/*
	 * When the erp information has changed, we should perform
	 * additional configuration steps. For all other changes we are done.
	 */
	if (changes & BSS_CHANGED_ERP_PREAMBLE) {
		if (!test_bit(DRIVER_REQUIRE_SCHEDULED, &rt2x00dev->flags))
			rt2x00lib_config_erp(rt2x00dev, intf, bss_conf);
		else
			delayed |= DELAYED_CONFIG_ERP;
	}

	spin_lock(&intf->lock);
	memcpy(&intf->conf, bss_conf, sizeof(*bss_conf));
	if (delayed) {
		intf->delayed_flags |= delayed;
		queue_work(rt2x00dev->hw->workqueue, &rt2x00dev->intf_work);
	}
	spin_unlock(&intf->lock);
}
EXPORT_SYMBOL_GPL(rt2x00mac_bss_info_changed);

int rt2x00mac_conf_tx(struct ieee80211_hw *hw, int queue_idx,
		      const struct ieee80211_tx_queue_params *params)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct data_queue *queue;

	queue = rt2x00queue_get_queue(rt2x00dev, queue_idx);
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

	if (params->aifs >= 0)
		queue->aifs = params->aifs;
	else
		queue->aifs = 2;

	INFO(rt2x00dev,
	     "Configured TX queue %d - CWmin: %d, CWmax: %d, Aifs: %d.\n",
	     queue_idx, queue->cw_min, queue->cw_max, queue->aifs);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_conf_tx);
