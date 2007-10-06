/*
	Copyright (C) 2004 - 2007 rt2x00 SourceForge Project
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

/*
 * Set enviroment defines for rt2x00.h
 */
#define DRV_NAME "rt2x00lib"

#include <linux/kernel.h>
#include <linux/module.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

static int rt2x00mac_tx_rts_cts(struct rt2x00_dev *rt2x00dev,
				struct data_ring *ring,
				struct sk_buff *frag_skb,
				struct ieee80211_tx_control *control)
{
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
		ieee80211_ctstoself_get(rt2x00dev->hw, rt2x00dev->interface.id,
					frag_skb->data, frag_skb->len, control,
					(struct ieee80211_cts *)(skb->data));
	else
		ieee80211_rts_get(rt2x00dev->hw, rt2x00dev->interface.id,
				  frag_skb->data, frag_skb->len, control,
				  (struct ieee80211_rts *)(skb->data));

	if (rt2x00dev->ops->lib->write_tx_data(rt2x00dev, ring, skb, control)) {
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
	struct data_ring *ring;
	u16 frame_control;

	/*
	 * Mac80211 might be calling this function while we are trying
	 * to remove the device or perhaps suspending it.
	 * Note that we can only stop the TX queues inside the TX path
	 * due to possible race conditions in mac80211.
	 */
	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags)) {
		ieee80211_stop_queues(hw);
		return 0;
	}

	/*
	 * Determine which ring to put packet on.
	 */
	ring = rt2x00lib_get_ring(rt2x00dev, control->queue);
	if (unlikely(!ring)) {
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
		if (rt2x00_ring_free(ring) <= 1)
			return NETDEV_TX_BUSY;

		if (rt2x00mac_tx_rts_cts(rt2x00dev, ring, skb, control))
			return NETDEV_TX_BUSY;
	}

	if (rt2x00dev->ops->lib->write_tx_data(rt2x00dev, ring, skb, control))
		return NETDEV_TX_BUSY;

	if (rt2x00dev->ops->lib->kick_tx_queue)
		rt2x00dev->ops->lib->kick_tx_queue(rt2x00dev, control->queue);

	return NETDEV_TX_OK;
}
EXPORT_SYMBOL_GPL(rt2x00mac_tx);

int rt2x00mac_start(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	int status;

	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags) ||
	    test_bit(DEVICE_STARTED, &rt2x00dev->flags))
		return 0;

	/*
	 * If this is the first interface which is added,
	 * we should load the firmware now.
	 */
	if (test_bit(DRIVER_REQUIRE_FIRMWARE, &rt2x00dev->flags)) {
		status = rt2x00lib_load_firmware(rt2x00dev);
		if (status)
			return status;
	}

	/*
	 * Initialize the device.
	 */
	status = rt2x00lib_initialize(rt2x00dev);
	if (status)
		return status;

	/*
	 * Enable radio.
	 */
	status = rt2x00lib_enable_radio(rt2x00dev);
	if (status) {
		rt2x00lib_uninitialize(rt2x00dev);
		return status;
	}

	__set_bit(DEVICE_STARTED, &rt2x00dev->flags);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_start);

void rt2x00mac_stop(struct ieee80211_hw *hw)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;

	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags))
		return;

	/*
	 * Perhaps we can add something smarter here,
	 * but for now just disabling the radio should do.
	 */
	rt2x00lib_disable_radio(rt2x00dev);

	__clear_bit(DEVICE_STARTED, &rt2x00dev->flags);
}
EXPORT_SYMBOL_GPL(rt2x00mac_stop);

int rt2x00mac_add_interface(struct ieee80211_hw *hw,
			    struct ieee80211_if_init_conf *conf)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct interface *intf = &rt2x00dev->interface;

	/*
	 * Don't allow interfaces to be added while
	 * either the device has disappeared or when
	 * another interface is already present.
	 */
	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags) ||
	    is_interface_present(intf))
		return -ENOBUFS;

	intf->id = conf->if_id;
	intf->type = conf->type;
	if (conf->type == IEEE80211_IF_TYPE_AP)
		memcpy(&intf->bssid, conf->mac_addr, ETH_ALEN);
	memcpy(&intf->mac, conf->mac_addr, ETH_ALEN);

	/*
	 * The MAC adddress must be configured after the device
	 * has been initialized. Otherwise the device can reset
	 * the MAC registers.
	 */
	rt2x00lib_config_mac_addr(rt2x00dev, intf->mac);
	rt2x00lib_config_type(rt2x00dev, conf->type);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_add_interface);

void rt2x00mac_remove_interface(struct ieee80211_hw *hw,
				struct ieee80211_if_init_conf *conf)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct interface *intf = &rt2x00dev->interface;

	/*
	 * Don't allow interfaces to be remove while
	 * either the device has disappeared or when
	 * no interface is present.
	 */
	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags) ||
	    !is_interface_present(intf))
		return;

	intf->id = 0;
	intf->type = INVALID_INTERFACE;
	memset(&intf->bssid, 0x00, ETH_ALEN);
	memset(&intf->mac, 0x00, ETH_ALEN);

	/*
	 * Make sure the bssid and mac address registers
	 * are cleared to prevent false ACKing of frames.
	 */
	rt2x00lib_config_mac_addr(rt2x00dev, intf->mac);
	rt2x00lib_config_bssid(rt2x00dev, intf->bssid);
	rt2x00lib_config_type(rt2x00dev, intf->type);
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

int rt2x00mac_config_interface(struct ieee80211_hw *hw, int if_id,
			       struct ieee80211_if_conf *conf)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct interface *intf = &rt2x00dev->interface;
	int status;

	/*
	 * Mac80211 might be calling this function while we are trying
	 * to remove the device or perhaps suspending it.
	 */
	if (!test_bit(DEVICE_PRESENT, &rt2x00dev->flags))
		return 0;

	/*
	 * If the given type does not match the configured type,
	 * there has been a problem.
	 */
	if (conf->type != intf->type)
		return -EINVAL;

	/*
	 * If the interface does not work in master mode,
	 * then the bssid value in the interface structure
	 * should now be set.
	 */
	if (conf->type != IEEE80211_IF_TYPE_AP)
		memcpy(&intf->bssid, conf->bssid, ETH_ALEN);
	rt2x00lib_config_bssid(rt2x00dev, intf->bssid);

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

	for (i = 0; i < hw->queues; i++)
		memcpy(&stats->data[i], &rt2x00dev->tx[i].stats,
		       sizeof(rt2x00dev->tx[i].stats));

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_get_tx_stats);

void rt2x00mac_erp_ie_changed(struct ieee80211_hw *hw, u8 changes,
			      int cts_protection, int preamble)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	int short_preamble;
	int ack_timeout;
	int ack_consume_time;
	int difs;

	/*
	 * We only support changing preamble mode.
	 */
	if (!(changes & IEEE80211_ERP_CHANGE_PREAMBLE))
		return;

	short_preamble = !preamble;
	preamble = !!(preamble) ? PREAMBLE : SHORT_PREAMBLE;

	difs = (hw->conf.flags & IEEE80211_CONF_SHORT_SLOT_TIME) ?
		SHORT_DIFS : DIFS;
	ack_timeout = difs + PLCP + preamble + get_duration(ACK_SIZE, 10);

	ack_consume_time = SIFS + PLCP + preamble + get_duration(ACK_SIZE, 10);

	if (short_preamble)
		__set_bit(CONFIG_SHORT_PREAMBLE, &rt2x00dev->flags);
	else
		__clear_bit(CONFIG_SHORT_PREAMBLE, &rt2x00dev->flags);

	rt2x00dev->ops->lib->config_preamble(rt2x00dev, short_preamble,
					     ack_timeout, ack_consume_time);
}
EXPORT_SYMBOL_GPL(rt2x00mac_erp_ie_changed);

int rt2x00mac_conf_tx(struct ieee80211_hw *hw, int queue,
		      const struct ieee80211_tx_queue_params *params)
{
	struct rt2x00_dev *rt2x00dev = hw->priv;
	struct data_ring *ring;

	ring = rt2x00lib_get_ring(rt2x00dev, queue);
	if (unlikely(!ring))
		return -EINVAL;

	/*
	 * The passed variables are stored as real value ((2^n)-1).
	 * Ralink registers require to know the bit number 'n'.
	 */
	if (params->cw_min)
		ring->tx_params.cw_min = fls(params->cw_min);
	else
		ring->tx_params.cw_min = 5; /* cw_min: 2^5 = 32. */

	if (params->cw_max)
		ring->tx_params.cw_max = fls(params->cw_max);
	else
		ring->tx_params.cw_max = 10; /* cw_min: 2^10 = 1024. */

	if (params->aifs)
		ring->tx_params.aifs = params->aifs;
	else
		ring->tx_params.aifs = 2;

	INFO(rt2x00dev,
	     "Configured TX ring %d - CWmin: %d, CWmax: %d, Aifs: %d.\n",
	     queue, ring->tx_params.cw_min, ring->tx_params.cw_max,
	     ring->tx_params.aifs);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00mac_conf_tx);
