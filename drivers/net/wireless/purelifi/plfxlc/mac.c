// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 pureLiFi
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/gpio.h>
#include <linux/jiffies.h>
#include <net/ieee80211_radiotap.h>

#include "chip.h"
#include "mac.h"
#include "usb.h"

static const struct ieee80211_rate plfxlc_rates[] = {
	{ .bitrate = 10,
		.hw_value = PURELIFI_CCK_RATE_1M,
		.flags = 0 },
	{ .bitrate = 20,
		.hw_value = PURELIFI_CCK_RATE_2M,
		.hw_value_short = PURELIFI_CCK_RATE_2M
			| PURELIFI_CCK_PREA_SHORT,
		.flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,
		.hw_value = PURELIFI_CCK_RATE_5_5M,
		.hw_value_short = PURELIFI_CCK_RATE_5_5M
			| PURELIFI_CCK_PREA_SHORT,
		.flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110,
		.hw_value = PURELIFI_CCK_RATE_11M,
		.hw_value_short = PURELIFI_CCK_RATE_11M
			| PURELIFI_CCK_PREA_SHORT,
		.flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60,
		.hw_value = PURELIFI_OFDM_RATE_6M,
		.flags = 0 },
	{ .bitrate = 90,
		.hw_value = PURELIFI_OFDM_RATE_9M,
		.flags = 0 },
	{ .bitrate = 120,
		.hw_value = PURELIFI_OFDM_RATE_12M,
		.flags = 0 },
	{ .bitrate = 180,
		.hw_value = PURELIFI_OFDM_RATE_18M,
		.flags = 0 },
	{ .bitrate = 240,
		.hw_value = PURELIFI_OFDM_RATE_24M,
		.flags = 0 },
	{ .bitrate = 360,
		.hw_value = PURELIFI_OFDM_RATE_36M,
		.flags = 0 },
	{ .bitrate = 480,
		.hw_value = PURELIFI_OFDM_RATE_48M,
		.flags = 0 },
	{ .bitrate = 540,
		.hw_value = PURELIFI_OFDM_RATE_54M,
		.flags = 0 }
};

static const struct ieee80211_channel plfxlc_channels[] = {
	{ .center_freq = 2412, .hw_value = 1 },
	{ .center_freq = 2417, .hw_value = 2 },
	{ .center_freq = 2422, .hw_value = 3 },
	{ .center_freq = 2427, .hw_value = 4 },
	{ .center_freq = 2432, .hw_value = 5 },
	{ .center_freq = 2437, .hw_value = 6 },
	{ .center_freq = 2442, .hw_value = 7 },
	{ .center_freq = 2447, .hw_value = 8 },
	{ .center_freq = 2452, .hw_value = 9 },
	{ .center_freq = 2457, .hw_value = 10 },
	{ .center_freq = 2462, .hw_value = 11 },
	{ .center_freq = 2467, .hw_value = 12 },
	{ .center_freq = 2472, .hw_value = 13 },
	{ .center_freq = 2484, .hw_value = 14 },
};

int plfxlc_mac_preinit_hw(struct ieee80211_hw *hw, const u8 *hw_address)
{
	SET_IEEE80211_PERM_ADDR(hw, hw_address);
	return 0;
}

int plfxlc_mac_init_hw(struct ieee80211_hw *hw)
{
	struct plfxlc_mac *mac = plfxlc_hw_mac(hw);
	struct plfxlc_chip *chip = &mac->chip;
	int r;

	r = plfxlc_chip_init_hw(chip);
	if (r) {
		dev_warn(plfxlc_mac_dev(mac), "init hw failed (%d)\n", r);
		return r;
	}

	dev_dbg(plfxlc_mac_dev(mac), "irq_disabled (%d)\n", irqs_disabled());
	regulatory_hint(hw->wiphy, "00");
	return r;
}

void plfxlc_mac_release(struct plfxlc_mac *mac)
{
	plfxlc_chip_release(&mac->chip);
	lockdep_assert_held(&mac->lock);
}

int plfxlc_op_start(struct ieee80211_hw *hw)
{
	plfxlc_hw_mac(hw)->chip.usb.initialized = 1;
	return 0;
}

void plfxlc_op_stop(struct ieee80211_hw *hw)
{
	struct plfxlc_mac *mac = plfxlc_hw_mac(hw);

	clear_bit(PURELIFI_DEVICE_RUNNING, &mac->flags);
}

int plfxlc_restore_settings(struct plfxlc_mac *mac)
{
	int beacon_interval, beacon_period;
	struct sk_buff *beacon;

	spin_lock_irq(&mac->lock);
	beacon_interval = mac->beacon.interval;
	beacon_period = mac->beacon.period;
	spin_unlock_irq(&mac->lock);

	if (mac->type != NL80211_IFTYPE_ADHOC)
		return 0;

	if (mac->vif) {
		beacon = ieee80211_beacon_get(mac->hw, mac->vif, 0);
		if (beacon) {
			/*beacon is hardcoded in firmware */
			kfree_skb(beacon);
			/* Returned skb is used only once and lowlevel
			 * driver is responsible for freeing it.
			 */
		}
	}

	plfxlc_set_beacon_interval(&mac->chip, beacon_interval,
				   beacon_period, mac->type);

	spin_lock_irq(&mac->lock);
	mac->beacon.last_update = jiffies;
	spin_unlock_irq(&mac->lock);

	return 0;
}

static void plfxlc_mac_tx_status(struct ieee80211_hw *hw,
				 struct sk_buff *skb,
				 int ackssi,
				 struct tx_status *tx_status)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int success = 1;

	ieee80211_tx_info_clear_status(info);
	if (tx_status)
		success = !tx_status->failure;

	if (success)
		info->flags |= IEEE80211_TX_STAT_ACK;
	else
		info->flags &= ~IEEE80211_TX_STAT_ACK;

	info->status.ack_signal = 50;
	ieee80211_tx_status_irqsafe(hw, skb);
}

void plfxlc_mac_tx_to_dev(struct sk_buff *skb, int error)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hw *hw = info->rate_driver_data[0];
	struct plfxlc_mac *mac = plfxlc_hw_mac(hw);
	struct sk_buff_head *q = NULL;

	ieee80211_tx_info_clear_status(info);
	skb_pull(skb, sizeof(struct plfxlc_ctrlset));

	if (unlikely(error ||
		     (info->flags & IEEE80211_TX_CTL_NO_ACK))) {
		ieee80211_tx_status_irqsafe(hw, skb);
		return;
	}

	q = &mac->ack_wait_queue;

	skb_queue_tail(q, skb);
	while (skb_queue_len(q)/* > PURELIFI_MAC_MAX_ACK_WAITERS*/) {
		plfxlc_mac_tx_status(hw, skb_dequeue(q),
				     mac->ack_pending ?
				     mac->ack_signal : 0,
				     NULL);
		mac->ack_pending = 0;
	}
}

static int plfxlc_fill_ctrlset(struct plfxlc_mac *mac, struct sk_buff *skb)
{
	unsigned int frag_len = skb->len;
	struct plfxlc_ctrlset *cs;
	u32 temp_payload_len = 0;
	unsigned int tmp;
	u32 temp_len = 0;

	if (skb_headroom(skb) < sizeof(struct plfxlc_ctrlset)) {
		dev_dbg(plfxlc_mac_dev(mac), "Not enough hroom(1)\n");
		return 1;
	}

	cs = (void *)skb_push(skb, sizeof(struct plfxlc_ctrlset));
	temp_payload_len = frag_len;
	temp_len = temp_payload_len +
		  sizeof(struct plfxlc_ctrlset) -
		  sizeof(cs->id) - sizeof(cs->len);

	/* Data packet lengths must be multiple of four bytes and must
	 * not be a multiple of 512 bytes. First, it is attempted to
	 * append the data packet in the tailroom of the skb. In rare
	 * occasions, the tailroom is too small. In this case, the
	 * content of the packet is shifted into the headroom of the skb
	 * by memcpy. Headroom is allocated at startup (below in this
	 * file). Therefore, there will be always enough headroom. The
	 * call skb_headroom is an additional safety which might be
	 * dropped.
	 */
	/* check if 32 bit aligned and align data */
	tmp = skb->len & 3;
	if (tmp) {
		if (skb_tailroom(skb) < (3 - tmp)) {
			if (skb_headroom(skb) >= 4 - tmp) {
				u8 len;
				u8 *src_pt;
				u8 *dest_pt;

				len = skb->len;
				src_pt = skb->data;
				dest_pt = skb_push(skb, 4 - tmp);
				memmove(dest_pt, src_pt, len);
			} else {
				return -ENOBUFS;
			}
		} else {
			skb_put(skb, 4 - tmp);
		}
		temp_len += 4 - tmp;
	}

	/* check if not multiple of 512 and align data */
	tmp = skb->len & 0x1ff;
	if (!tmp) {
		if (skb_tailroom(skb) < 4) {
			if (skb_headroom(skb) >= 4) {
				u8 len = skb->len;
				u8 *src_pt = skb->data;
				u8 *dest_pt = skb_push(skb, 4);

				memmove(dest_pt, src_pt, len);
			} else {
				/* should never happen because
				 * sufficient headroom was reserved
				 */
				return -ENOBUFS;
			}
		} else {
			skb_put(skb, 4);
		}
		temp_len += 4;
	}

	cs->id = cpu_to_be32(USB_REQ_DATA_TX);
	cs->len = cpu_to_be32(temp_len);
	cs->payload_len_nw = cpu_to_be32(temp_payload_len);

	return 0;
}

static void plfxlc_op_tx(struct ieee80211_hw *hw,
			 struct ieee80211_tx_control *control,
			 struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct plfxlc_header *plhdr = (void *)skb->data;
	struct plfxlc_mac *mac = plfxlc_hw_mac(hw);
	struct plfxlc_usb *usb = &mac->chip.usb;
	unsigned long flags;
	int r;

	r = plfxlc_fill_ctrlset(mac, skb);
	if (r)
		goto fail;

	info->rate_driver_data[0] = hw;

	if (plhdr->frametype  == IEEE80211_FTYPE_DATA) {
		u8 *dst_mac = plhdr->dmac;
		u8 sidx;
		bool found = false;
		struct plfxlc_usb_tx *tx = &usb->tx;

		for (sidx = 0; sidx < MAX_STA_NUM; sidx++) {
			if (!(tx->station[sidx].flag & STATION_CONNECTED_FLAG))
				continue;
			if (memcmp(tx->station[sidx].mac, dst_mac, ETH_ALEN))
				continue;
			found = true;
			break;
		}

		/* Default to broadcast address for unknown MACs */
		if (!found)
			sidx = STA_BROADCAST_INDEX;

		/* Stop OS from sending packets, if the queue is half full */
		if (skb_queue_len(&tx->station[sidx].data_list) > 60)
			ieee80211_stop_queues(plfxlc_usb_to_hw(usb));

		/* Schedule packet for transmission if queue is not full */
		if (skb_queue_len(&tx->station[sidx].data_list) > 256)
			goto fail;
		skb_queue_tail(&tx->station[sidx].data_list, skb);
		plfxlc_send_packet_from_data_queue(usb);

	} else {
		spin_lock_irqsave(&usb->tx.lock, flags);
		r = plfxlc_usb_wreq_async(&mac->chip.usb, skb->data, skb->len,
					  USB_REQ_DATA_TX, plfxlc_tx_urb_complete, skb);
		spin_unlock_irqrestore(&usb->tx.lock, flags);
		if (r)
			goto fail;
	}
	return;

fail:
	dev_kfree_skb(skb);
}

static int plfxlc_filter_ack(struct ieee80211_hw *hw, struct ieee80211_hdr *rx_hdr,
			     struct ieee80211_rx_status *stats)
{
	struct plfxlc_mac *mac = plfxlc_hw_mac(hw);
	struct sk_buff_head *q;
	int i, position = 0;
	unsigned long flags;
	struct sk_buff *skb;
	bool found = false;

	if (!ieee80211_is_ack(rx_hdr->frame_control))
		return 0;

	dev_dbg(plfxlc_mac_dev(mac), "ACK Received\n");

	/* code based on zy driver, this logic may need fix */
	q = &mac->ack_wait_queue;
	spin_lock_irqsave(&q->lock, flags);

	skb_queue_walk(q, skb) {
		struct ieee80211_hdr *tx_hdr;

		position++;

		if (mac->ack_pending && skb_queue_is_first(q, skb))
			continue;
		if (mac->ack_pending == 0)
			break;

		tx_hdr = (struct ieee80211_hdr *)skb->data;
		if (likely(ether_addr_equal(tx_hdr->addr2, rx_hdr->addr1))) {
			found = 1;
			break;
		}
	}

	if (found) {
		for (i = 1; i < position; i++)
			skb = __skb_dequeue(q);
		if (i == position) {
			plfxlc_mac_tx_status(hw, skb,
					     mac->ack_pending ?
					     mac->ack_signal : 0,
					     NULL);
			mac->ack_pending = 0;
		}

		mac->ack_pending = skb_queue_len(q) ? 1 : 0;
		mac->ack_signal = stats->signal;
	}

	spin_unlock_irqrestore(&q->lock, flags);
	return 1;
}

int plfxlc_mac_rx(struct ieee80211_hw *hw, const u8 *buffer,
		  unsigned int length)
{
	struct plfxlc_mac *mac = plfxlc_hw_mac(hw);
	struct ieee80211_rx_status stats;
	const struct rx_status *status;
	unsigned int payload_length;
	struct plfxlc_usb_tx *tx;
	struct sk_buff *skb;
	int need_padding;
	__le16 fc;
	int sidx;

	/* Packet blockade during disabled interface. */
	if (!mac->vif)
		return 0;

	status = (struct rx_status *)buffer;

	memset(&stats, 0, sizeof(stats));

	stats.flag     = 0;
	stats.freq     = 2412;
	stats.band     = NL80211_BAND_LC;
	mac->rssi      = -15 * be16_to_cpu(status->rssi) / 10;

	stats.signal   = mac->rssi;

	if (status->rate_idx > 7)
		stats.rate_idx = 0;
	else
		stats.rate_idx = status->rate_idx;

	mac->crc_errors = be64_to_cpu(status->crc_error_count);

	/* TODO bad frame check for CRC error*/
	if (plfxlc_filter_ack(hw, (struct ieee80211_hdr *)buffer, &stats) &&
	    !mac->pass_ctrl)
		return 0;

	buffer += sizeof(struct rx_status);
	payload_length = get_unaligned_be32(buffer);

	if (payload_length > 1560) {
		dev_err(plfxlc_mac_dev(mac), " > MTU %u\n", payload_length);
		return 0;
	}
	buffer += sizeof(u32);

	fc = get_unaligned((__le16 *)buffer);
	need_padding = ieee80211_is_data_qos(fc) ^ ieee80211_has_a4(fc);

	tx = &mac->chip.usb.tx;

	for (sidx = 0; sidx < MAX_STA_NUM - 1; sidx++) {
		if (memcmp(&buffer[10], tx->station[sidx].mac, ETH_ALEN))
			continue;
		if (tx->station[sidx].flag & STATION_CONNECTED_FLAG) {
			tx->station[sidx].flag |= STATION_HEARTBEAT_FLAG;
			break;
		}
	}

	if (sidx == MAX_STA_NUM - 1) {
		for (sidx = 0; sidx < MAX_STA_NUM - 1; sidx++) {
			if (tx->station[sidx].flag & STATION_CONNECTED_FLAG)
				continue;
			memcpy(tx->station[sidx].mac, &buffer[10], ETH_ALEN);
			tx->station[sidx].flag |= STATION_CONNECTED_FLAG;
			tx->station[sidx].flag |= STATION_HEARTBEAT_FLAG;
			break;
		}
	}

	switch (buffer[0]) {
	case IEEE80211_STYPE_PROBE_REQ:
		dev_dbg(plfxlc_mac_dev(mac), "Probe request\n");
		break;
	case IEEE80211_STYPE_ASSOC_REQ:
		dev_dbg(plfxlc_mac_dev(mac), "Association request\n");
		break;
	case IEEE80211_STYPE_AUTH:
		dev_dbg(plfxlc_mac_dev(mac), "Authentication req\n");
		break;
	case IEEE80211_FTYPE_DATA:
		dev_dbg(plfxlc_mac_dev(mac), "802.11 data frame\n");
		break;
	}

	skb = dev_alloc_skb(payload_length + (need_padding ? 2 : 0));
	if (!skb)
		return -ENOMEM;

	if (need_padding)
		/* Make sure that the payload data is 4 byte aligned. */
		skb_reserve(skb, 2);

	skb_put_data(skb, buffer, payload_length);
	memcpy(IEEE80211_SKB_RXCB(skb), &stats, sizeof(stats));
	ieee80211_rx_irqsafe(hw, skb);
	return 0;
}

static int plfxlc_op_add_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct plfxlc_mac *mac = plfxlc_hw_mac(hw);
	static const char * const iftype80211[] = {
		[NL80211_IFTYPE_STATION]	= "Station",
		[NL80211_IFTYPE_ADHOC]		= "Adhoc"
	};

	if (mac->type != NL80211_IFTYPE_UNSPECIFIED)
		return -EOPNOTSUPP;

	if (vif->type == NL80211_IFTYPE_ADHOC ||
	    vif->type == NL80211_IFTYPE_STATION) {
		dev_dbg(plfxlc_mac_dev(mac), "%s %s\n", __func__,
			iftype80211[vif->type]);
		mac->type = vif->type;
		mac->vif = vif;
		return 0;
	}
	dev_dbg(plfxlc_mac_dev(mac), "unsupported iftype\n");
	return -EOPNOTSUPP;
}

static void plfxlc_op_remove_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct plfxlc_mac *mac = plfxlc_hw_mac(hw);

	mac->type = NL80211_IFTYPE_UNSPECIFIED;
	mac->vif = NULL;
}

static int plfxlc_op_config(struct ieee80211_hw *hw, u32 changed)
{
	return 0;
}

#define SUPPORTED_FIF_FLAGS \
	(FIF_ALLMULTI | FIF_FCSFAIL | FIF_CONTROL | \
	 FIF_OTHER_BSS | FIF_BCN_PRBRESP_PROMISC)
static void plfxlc_op_configure_filter(struct ieee80211_hw *hw,
				       unsigned int changed_flags,
				       unsigned int *new_flags,
				       u64 multicast)
{
	struct plfxlc_mc_hash hash = {
		.low = multicast,
		.high = multicast >> 32,
	};
	struct plfxlc_mac *mac = plfxlc_hw_mac(hw);
	unsigned long flags;

	/* Only deal with supported flags */
	*new_flags &= SUPPORTED_FIF_FLAGS;

	/* If multicast parameter
	 * (as returned by plfxlc_op_prepare_multicast)
	 * has changed, no bit in changed_flags is set. To handle this
	 * situation, we do not return if changed_flags is 0. If we do so,
	 * we will have some issue with IPv6 which uses multicast for link
	 * layer address resolution.
	 */
	if (*new_flags & (FIF_ALLMULTI))
		plfxlc_mc_add_all(&hash);

	spin_lock_irqsave(&mac->lock, flags);
	mac->pass_failed_fcs = !!(*new_flags & FIF_FCSFAIL);
	mac->pass_ctrl = !!(*new_flags & FIF_CONTROL);
	mac->multicast_hash = hash;
	spin_unlock_irqrestore(&mac->lock, flags);

	/* no handling required for FIF_OTHER_BSS as we don't currently
	 * do BSSID filtering
	 */
	/* FIXME: in future it would be nice to enable the probe response
	 * filter (so that the driver doesn't see them) until
	 * FIF_BCN_PRBRESP_PROMISC is set. however due to atomicity here, we'd
	 * have to schedule work to enable prbresp reception, which might
	 * happen too late. For now we'll just listen and forward them all the
	 * time.
	 */
}

static void plfxlc_op_bss_info_changed(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u64 changes)
{
	struct plfxlc_mac *mac = plfxlc_hw_mac(hw);
	int associated;

	dev_dbg(plfxlc_mac_dev(mac), "changes: %llx\n", changes);

	if (mac->type != NL80211_IFTYPE_ADHOC) { /* for STATION */
		associated = is_valid_ether_addr(bss_conf->bssid);
		goto exit_all;
	}
	/* for ADHOC */
	associated = true;
	if (changes & BSS_CHANGED_BEACON) {
		struct sk_buff *beacon = ieee80211_beacon_get(hw, vif, 0);

		if (beacon) {
			/*beacon is hardcoded in firmware */
			kfree_skb(beacon);
			/*Returned skb is used only once and
			 * low-level driver is
			 * responsible for freeing it.
			 */
		}
	}

	if (changes & BSS_CHANGED_BEACON_ENABLED) {
		u16 interval = 0;
		u8 period = 0;

		if (bss_conf->enable_beacon) {
			period = bss_conf->dtim_period;
			interval = bss_conf->beacon_int;
		}

		spin_lock_irq(&mac->lock);
		mac->beacon.period = period;
		mac->beacon.interval = interval;
		mac->beacon.last_update = jiffies;
		spin_unlock_irq(&mac->lock);

		plfxlc_set_beacon_interval(&mac->chip, interval,
					   period, mac->type);
	}
exit_all:
	spin_lock_irq(&mac->lock);
	mac->associated = associated;
	spin_unlock_irq(&mac->lock);
}

static int plfxlc_get_stats(struct ieee80211_hw *hw,
			    struct ieee80211_low_level_stats *stats)
{
	stats->dot11ACKFailureCount = 0;
	stats->dot11RTSFailureCount = 0;
	stats->dot11FCSErrorCount   = 0;
	stats->dot11RTSSuccessCount = 0;
	return 0;
}

static const char et_strings[][ETH_GSTRING_LEN] = {
	"phy_rssi",
	"phy_rx_crc_err"
};

static int plfxlc_get_et_sset_count(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif, int sset)
{
	if (sset == ETH_SS_STATS)
		return ARRAY_SIZE(et_strings);

	return 0;
}

static void plfxlc_get_et_strings(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  u32 sset, u8 *data)
{
	if (sset == ETH_SS_STATS)
		memcpy(data, et_strings, sizeof(et_strings));
}

static void plfxlc_get_et_stats(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ethtool_stats *stats, u64 *data)
{
	struct plfxlc_mac *mac = plfxlc_hw_mac(hw);

	data[0] = mac->rssi;
	data[1] = mac->crc_errors;
}

static int plfxlc_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	return 0;
}

static const struct ieee80211_ops plfxlc_ops = {
	.tx = plfxlc_op_tx,
	.wake_tx_queue = ieee80211_handle_wake_tx_queue,
	.start = plfxlc_op_start,
	.stop = plfxlc_op_stop,
	.add_interface = plfxlc_op_add_interface,
	.remove_interface = plfxlc_op_remove_interface,
	.set_rts_threshold = plfxlc_set_rts_threshold,
	.config = plfxlc_op_config,
	.configure_filter = plfxlc_op_configure_filter,
	.bss_info_changed = plfxlc_op_bss_info_changed,
	.get_stats = plfxlc_get_stats,
	.get_et_sset_count = plfxlc_get_et_sset_count,
	.get_et_stats = plfxlc_get_et_stats,
	.get_et_strings = plfxlc_get_et_strings,
};

struct ieee80211_hw *plfxlc_mac_alloc_hw(struct usb_interface *intf)
{
	struct ieee80211_hw *hw;
	struct plfxlc_mac *mac;

	hw = ieee80211_alloc_hw(sizeof(struct plfxlc_mac), &plfxlc_ops);
	if (!hw) {
		dev_dbg(&intf->dev, "out of memory\n");
		return NULL;
	}
	set_wiphy_dev(hw->wiphy, &intf->dev);

	mac = plfxlc_hw_mac(hw);
	memset(mac, 0, sizeof(*mac));
	spin_lock_init(&mac->lock);
	mac->hw = hw;

	mac->type = NL80211_IFTYPE_UNSPECIFIED;

	memcpy(mac->channels, plfxlc_channels, sizeof(plfxlc_channels));
	memcpy(mac->rates, plfxlc_rates, sizeof(plfxlc_rates));
	mac->band.n_bitrates = ARRAY_SIZE(plfxlc_rates);
	mac->band.bitrates = mac->rates;
	mac->band.n_channels = ARRAY_SIZE(plfxlc_channels);
	mac->band.channels = mac->channels;
	hw->wiphy->bands[NL80211_BAND_LC] = &mac->band;
	hw->conf.chandef.width = NL80211_CHAN_WIDTH_20;

	ieee80211_hw_set(hw, RX_INCLUDES_FCS);
	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, HOST_BROADCAST_PS_BUFFERING);
	ieee80211_hw_set(hw, MFP_CAPABLE);

	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC);
	hw->max_signal = 100;
	hw->queues = 1;
	/* 4 for 32 bit alignment if no tailroom */
	hw->extra_tx_headroom = sizeof(struct plfxlc_ctrlset) + 4;
	/* Tell mac80211 that we support multi rate retries */
	hw->max_rates = IEEE80211_TX_MAX_RATES;
	hw->max_rate_tries = 18;   /* 9 rates * 2 retries/rate */

	skb_queue_head_init(&mac->ack_wait_queue);
	mac->ack_pending = 0;

	plfxlc_chip_init(&mac->chip, hw, intf);

	SET_IEEE80211_DEV(hw, &intf->dev);
	return hw;
}
