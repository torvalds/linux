/* ZD1211 USB-WLAN driver for Linux
 *
 * Copyright (C) 2005-2007 Ulrich Kunitz <kune@deine-taler.de>
 * Copyright (C) 2006-2007 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2006-2007 Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2007 Luis R. Rodriguez <mcgrof@winlab.rutgers.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/usb.h>
#include <linux/jiffies.h>
#include <net/ieee80211_radiotap.h>

#include "zd_def.h"
#include "zd_chip.h"
#include "zd_mac.h"
#include "zd_ieee80211.h"
#include "zd_rf.h"

/* This table contains the hardware specific values for the modulation rates. */
static const struct ieee80211_rate zd_rates[] = {
	{ .bitrate = 10,
	  .hw_value = ZD_CCK_RATE_1M, },
	{ .bitrate = 20,
	  .hw_value = ZD_CCK_RATE_2M,
	  .hw_value_short = ZD_CCK_RATE_2M | ZD_CCK_PREA_SHORT,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,
	  .hw_value = ZD_CCK_RATE_5_5M,
	  .hw_value_short = ZD_CCK_RATE_5_5M | ZD_CCK_PREA_SHORT,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110,
	  .hw_value = ZD_CCK_RATE_11M,
	  .hw_value_short = ZD_CCK_RATE_11M | ZD_CCK_PREA_SHORT,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60,
	  .hw_value = ZD_OFDM_RATE_6M,
	  .flags = 0 },
	{ .bitrate = 90,
	  .hw_value = ZD_OFDM_RATE_9M,
	  .flags = 0 },
	{ .bitrate = 120,
	  .hw_value = ZD_OFDM_RATE_12M,
	  .flags = 0 },
	{ .bitrate = 180,
	  .hw_value = ZD_OFDM_RATE_18M,
	  .flags = 0 },
	{ .bitrate = 240,
	  .hw_value = ZD_OFDM_RATE_24M,
	  .flags = 0 },
	{ .bitrate = 360,
	  .hw_value = ZD_OFDM_RATE_36M,
	  .flags = 0 },
	{ .bitrate = 480,
	  .hw_value = ZD_OFDM_RATE_48M,
	  .flags = 0 },
	{ .bitrate = 540,
	  .hw_value = ZD_OFDM_RATE_54M,
	  .flags = 0 },
};

static const struct ieee80211_channel zd_channels[] = {
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

static void housekeeping_init(struct zd_mac *mac);
static void housekeeping_enable(struct zd_mac *mac);
static void housekeeping_disable(struct zd_mac *mac);

int zd_mac_preinit_hw(struct ieee80211_hw *hw)
{
	int r;
	u8 addr[ETH_ALEN];
	struct zd_mac *mac = zd_hw_mac(hw);

	r = zd_chip_read_mac_addr_fw(&mac->chip, addr);
	if (r)
		return r;

	SET_IEEE80211_PERM_ADDR(hw, addr);

	return 0;
}

int zd_mac_init_hw(struct ieee80211_hw *hw)
{
	int r;
	struct zd_mac *mac = zd_hw_mac(hw);
	struct zd_chip *chip = &mac->chip;
	u8 default_regdomain;

	r = zd_chip_enable_int(chip);
	if (r)
		goto out;
	r = zd_chip_init_hw(chip);
	if (r)
		goto disable_int;

	ZD_ASSERT(!irqs_disabled());

	r = zd_read_regdomain(chip, &default_regdomain);
	if (r)
		goto disable_int;
	spin_lock_irq(&mac->lock);
	mac->regdomain = mac->default_regdomain = default_regdomain;
	spin_unlock_irq(&mac->lock);

	/* We must inform the device that we are doing encryption/decryption in
	 * software at the moment. */
	r = zd_set_encryption_type(chip, ENC_SNIFFER);
	if (r)
		goto disable_int;

	zd_geo_init(hw, mac->regdomain);

	r = 0;
disable_int:
	zd_chip_disable_int(chip);
out:
	return r;
}

void zd_mac_clear(struct zd_mac *mac)
{
	flush_workqueue(zd_workqueue);
	zd_chip_clear(&mac->chip);
	ZD_ASSERT(!spin_is_locked(&mac->lock));
	ZD_MEMCLEAR(mac, sizeof(struct zd_mac));
}

static int set_rx_filter(struct zd_mac *mac)
{
	unsigned long flags;
	u32 filter = STA_RX_FILTER;

	spin_lock_irqsave(&mac->lock, flags);
	if (mac->pass_ctrl)
		filter |= RX_FILTER_CTRL;
	spin_unlock_irqrestore(&mac->lock, flags);

	return zd_iowrite32(&mac->chip, CR_RX_FILTER, filter);
}

static int set_mc_hash(struct zd_mac *mac)
{
	struct zd_mc_hash hash;
	zd_mc_clear(&hash);
	return zd_chip_set_multicast_hash(&mac->chip, &hash);
}

static int zd_op_start(struct ieee80211_hw *hw)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	struct zd_chip *chip = &mac->chip;
	struct zd_usb *usb = &chip->usb;
	int r;

	if (!usb->initialized) {
		r = zd_usb_init_hw(usb);
		if (r)
			goto out;
	}

	r = zd_chip_enable_int(chip);
	if (r < 0)
		goto out;

	r = zd_chip_set_basic_rates(chip, CR_RATES_80211B | CR_RATES_80211G);
	if (r < 0)
		goto disable_int;
	r = set_rx_filter(mac);
	if (r)
		goto disable_int;
	r = set_mc_hash(mac);
	if (r)
		goto disable_int;
	r = zd_chip_switch_radio_on(chip);
	if (r < 0)
		goto disable_int;
	r = zd_chip_enable_rxtx(chip);
	if (r < 0)
		goto disable_radio;
	r = zd_chip_enable_hwint(chip);
	if (r < 0)
		goto disable_rxtx;

	housekeeping_enable(mac);
	return 0;
disable_rxtx:
	zd_chip_disable_rxtx(chip);
disable_radio:
	zd_chip_switch_radio_off(chip);
disable_int:
	zd_chip_disable_int(chip);
out:
	return r;
}

static void zd_op_stop(struct ieee80211_hw *hw)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	struct zd_chip *chip = &mac->chip;
	struct sk_buff *skb;
	struct sk_buff_head *ack_wait_queue = &mac->ack_wait_queue;

	/* The order here deliberately is a little different from the open()
	 * method, since we need to make sure there is no opportunity for RX
	 * frames to be processed by mac80211 after we have stopped it.
	 */

	zd_chip_disable_rxtx(chip);
	housekeeping_disable(mac);
	flush_workqueue(zd_workqueue);

	zd_chip_disable_hwint(chip);
	zd_chip_switch_radio_off(chip);
	zd_chip_disable_int(chip);


	while ((skb = skb_dequeue(ack_wait_queue)))
		dev_kfree_skb_any(skb);
}

/**
 * tx_status - reports tx status of a packet if required
 * @hw - a &struct ieee80211_hw pointer
 * @skb - a sk-buffer
 * @flags: extra flags to set in the TX status info
 * @ackssi: ACK signal strength
 * @success - True for successfull transmission of the frame
 *
 * This information calls ieee80211_tx_status_irqsafe() if required by the
 * control information. It copies the control information into the status
 * information.
 *
 * If no status information has been requested, the skb is freed.
 */
static void tx_status(struct ieee80211_hw *hw, struct sk_buff *skb,
		      u32 flags, int ackssi, bool success)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	memset(&info->status, 0, sizeof(info->status));

	if (!success)
		info->status.excessive_retries = 1;
	info->flags |= flags;
	info->status.ack_signal = ackssi;
	ieee80211_tx_status_irqsafe(hw, skb);
}

/**
 * zd_mac_tx_failed - callback for failed frames
 * @dev: the mac80211 wireless device
 *
 * This function is called if a frame couldn't be succesfully be
 * transferred. The first frame from the tx queue, will be selected and
 * reported as error to the upper layers.
 */
void zd_mac_tx_failed(struct ieee80211_hw *hw)
{
	struct sk_buff_head *q = &zd_hw_mac(hw)->ack_wait_queue;
	struct sk_buff *skb;

	skb = skb_dequeue(q);
	if (skb == NULL)
		return;

	tx_status(hw, skb, 0, 0, 0);
}

/**
 * zd_mac_tx_to_dev - callback for USB layer
 * @skb: a &sk_buff pointer
 * @error: error value, 0 if transmission successful
 *
 * Informs the MAC layer that the frame has successfully transferred to the
 * device. If an ACK is required and the transfer to the device has been
 * successful, the packets are put on the @ack_wait_queue with
 * the control set removed.
 */
void zd_mac_tx_to_dev(struct sk_buff *skb, int error)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hw *hw = info->driver_data[0];

	skb_pull(skb, sizeof(struct zd_ctrlset));
	if (unlikely(error ||
	    (info->flags & IEEE80211_TX_CTL_NO_ACK))) {
		tx_status(hw, skb, 0, 0, !error);
	} else {
		struct sk_buff_head *q =
			&zd_hw_mac(hw)->ack_wait_queue;

		skb_queue_tail(q, skb);
		while (skb_queue_len(q) > ZD_MAC_MAX_ACK_WAITERS)
			zd_mac_tx_failed(hw);
	}
}

static int zd_calc_tx_length_us(u8 *service, u8 zd_rate, u16 tx_length)
{
	/* ZD_PURE_RATE() must be used to remove the modulation type flag of
	 * the zd-rate values.
	 */
	static const u8 rate_divisor[] = {
		[ZD_PURE_RATE(ZD_CCK_RATE_1M)]   =  1,
		[ZD_PURE_RATE(ZD_CCK_RATE_2M)]	 =  2,
		/* Bits must be doubled. */
		[ZD_PURE_RATE(ZD_CCK_RATE_5_5M)] = 11,
		[ZD_PURE_RATE(ZD_CCK_RATE_11M)]	 = 11,
		[ZD_PURE_RATE(ZD_OFDM_RATE_6M)]  =  6,
		[ZD_PURE_RATE(ZD_OFDM_RATE_9M)]  =  9,
		[ZD_PURE_RATE(ZD_OFDM_RATE_12M)] = 12,
		[ZD_PURE_RATE(ZD_OFDM_RATE_18M)] = 18,
		[ZD_PURE_RATE(ZD_OFDM_RATE_24M)] = 24,
		[ZD_PURE_RATE(ZD_OFDM_RATE_36M)] = 36,
		[ZD_PURE_RATE(ZD_OFDM_RATE_48M)] = 48,
		[ZD_PURE_RATE(ZD_OFDM_RATE_54M)] = 54,
	};

	u32 bits = (u32)tx_length * 8;
	u32 divisor;

	divisor = rate_divisor[ZD_PURE_RATE(zd_rate)];
	if (divisor == 0)
		return -EINVAL;

	switch (zd_rate) {
	case ZD_CCK_RATE_5_5M:
		bits = (2*bits) + 10; /* round up to the next integer */
		break;
	case ZD_CCK_RATE_11M:
		if (service) {
			u32 t = bits % 11;
			*service &= ~ZD_PLCP_SERVICE_LENGTH_EXTENSION;
			if (0 < t && t <= 3) {
				*service |= ZD_PLCP_SERVICE_LENGTH_EXTENSION;
			}
		}
		bits += 10; /* round up to the next integer */
		break;
	}

	return bits/divisor;
}

static void cs_set_control(struct zd_mac *mac, struct zd_ctrlset *cs,
	                   struct ieee80211_hdr *header, u32 flags)
{
	/*
	 * CONTROL TODO:
	 * - if backoff needed, enable bit 0
	 * - if burst (backoff not needed) disable bit 0
	 */

	cs->control = 0;

	/* First fragment */
	if (flags & IEEE80211_TX_CTL_FIRST_FRAGMENT)
		cs->control |= ZD_CS_NEED_RANDOM_BACKOFF;

	/* Multicast */
	if (is_multicast_ether_addr(header->addr1))
		cs->control |= ZD_CS_MULTICAST;

	/* PS-POLL */
	if (ieee80211_is_pspoll(header->frame_control))
		cs->control |= ZD_CS_PS_POLL_FRAME;

	if (flags & IEEE80211_TX_CTL_USE_RTS_CTS)
		cs->control |= ZD_CS_RTS;

	if (flags & IEEE80211_TX_CTL_USE_CTS_PROTECT)
		cs->control |= ZD_CS_SELF_CTS;

	/* FIXME: Management frame? */
}

static int zd_mac_config_beacon(struct ieee80211_hw *hw, struct sk_buff *beacon)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	int r;
	u32 tmp, j = 0;
	/* 4 more bytes for tail CRC */
	u32 full_len = beacon->len + 4;

	r = zd_iowrite32(&mac->chip, CR_BCN_FIFO_SEMAPHORE, 0);
	if (r < 0)
		return r;
	r = zd_ioread32(&mac->chip, CR_BCN_FIFO_SEMAPHORE, &tmp);
	if (r < 0)
		return r;

	while (tmp & 0x2) {
		r = zd_ioread32(&mac->chip, CR_BCN_FIFO_SEMAPHORE, &tmp);
		if (r < 0)
			return r;
		if ((++j % 100) == 0) {
			printk(KERN_ERR "CR_BCN_FIFO_SEMAPHORE not ready\n");
			if (j >= 500)  {
				printk(KERN_ERR "Giving up beacon config.\n");
				return -ETIMEDOUT;
			}
		}
		msleep(1);
	}

	r = zd_iowrite32(&mac->chip, CR_BCN_FIFO, full_len - 1);
	if (r < 0)
		return r;
	if (zd_chip_is_zd1211b(&mac->chip)) {
		r = zd_iowrite32(&mac->chip, CR_BCN_LENGTH, full_len - 1);
		if (r < 0)
			return r;
	}

	for (j = 0 ; j < beacon->len; j++) {
		r = zd_iowrite32(&mac->chip, CR_BCN_FIFO,
				*((u8 *)(beacon->data + j)));
		if (r < 0)
			return r;
	}

	for (j = 0; j < 4; j++) {
		r = zd_iowrite32(&mac->chip, CR_BCN_FIFO, 0x0);
		if (r < 0)
			return r;
	}

	r = zd_iowrite32(&mac->chip, CR_BCN_FIFO_SEMAPHORE, 1);
	if (r < 0)
		return r;

	/* 802.11b/g 2.4G CCK 1Mb
	 * 802.11a, not yet implemented, uses different values (see GPL vendor
	 * driver)
	 */
	return zd_iowrite32(&mac->chip, CR_BCN_PLCP_CFG, 0x00000400 |
			(full_len << 19));
}

static int fill_ctrlset(struct zd_mac *mac,
			struct sk_buff *skb)
{
	int r;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	unsigned int frag_len = skb->len + FCS_LEN;
	unsigned int packet_length;
	struct ieee80211_rate *txrate;
	struct zd_ctrlset *cs = (struct zd_ctrlset *)
		skb_push(skb, sizeof(struct zd_ctrlset));
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	ZD_ASSERT(frag_len <= 0xffff);

	txrate = ieee80211_get_tx_rate(mac->hw, info);

	cs->modulation = txrate->hw_value;
	if (info->flags & IEEE80211_TX_CTL_SHORT_PREAMBLE)
		cs->modulation = txrate->hw_value_short;

	cs->tx_length = cpu_to_le16(frag_len);

	cs_set_control(mac, cs, hdr, info->flags);

	packet_length = frag_len + sizeof(struct zd_ctrlset) + 10;
	ZD_ASSERT(packet_length <= 0xffff);
	/* ZD1211B: Computing the length difference this way, gives us
	 * flexibility to compute the packet length.
	 */
	cs->packet_length = cpu_to_le16(zd_chip_is_zd1211b(&mac->chip) ?
			packet_length - frag_len : packet_length);

	/*
	 * CURRENT LENGTH:
	 * - transmit frame length in microseconds
	 * - seems to be derived from frame length
	 * - see Cal_Us_Service() in zdinlinef.h
	 * - if macp->bTxBurstEnable is enabled, then multiply by 4
	 *  - bTxBurstEnable is never set in the vendor driver
	 *
	 * SERVICE:
	 * - "for PLCP configuration"
	 * - always 0 except in some situations at 802.11b 11M
	 * - see line 53 of zdinlinef.h
	 */
	cs->service = 0;
	r = zd_calc_tx_length_us(&cs->service, ZD_RATE(cs->modulation),
		                 le16_to_cpu(cs->tx_length));
	if (r < 0)
		return r;
	cs->current_length = cpu_to_le16(r);
	cs->next_frame_length = 0;

	return 0;
}

/**
 * zd_op_tx - transmits a network frame to the device
 *
 * @dev: mac80211 hardware device
 * @skb: socket buffer
 * @control: the control structure
 *
 * This function transmit an IEEE 802.11 network frame to the device. The
 * control block of the skbuff will be initialized. If necessary the incoming
 * mac80211 queues will be stopped.
 */
static int zd_op_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int r;

	r = fill_ctrlset(mac, skb);
	if (r)
		return r;

	info->driver_data[0] = hw;

	r = zd_usb_tx(&mac->chip.usb, skb);
	if (r)
		return r;
	return 0;
}

/**
 * filter_ack - filters incoming packets for acknowledgements
 * @dev: the mac80211 device
 * @rx_hdr: received header
 * @stats: the status for the received packet
 *
 * This functions looks for ACK packets and tries to match them with the
 * frames in the tx queue. If a match is found the frame will be dequeued and
 * the upper layers is informed about the successful transmission. If
 * mac80211 queues have been stopped and the number of frames still to be
 * transmitted is low the queues will be opened again.
 *
 * Returns 1 if the frame was an ACK, 0 if it was ignored.
 */
static int filter_ack(struct ieee80211_hw *hw, struct ieee80211_hdr *rx_hdr,
		      struct ieee80211_rx_status *stats)
{
	struct sk_buff *skb;
	struct sk_buff_head *q;
	unsigned long flags;

	if (!ieee80211_is_ack(rx_hdr->frame_control))
		return 0;

	q = &zd_hw_mac(hw)->ack_wait_queue;
	spin_lock_irqsave(&q->lock, flags);
	for (skb = q->next; skb != (struct sk_buff *)q; skb = skb->next) {
		struct ieee80211_hdr *tx_hdr;

		tx_hdr = (struct ieee80211_hdr *)skb->data;
		if (likely(!compare_ether_addr(tx_hdr->addr2, rx_hdr->addr1)))
		{
			__skb_unlink(skb, q);
			tx_status(hw, skb, IEEE80211_TX_STAT_ACK, stats->signal, 1);
			goto out;
		}
	}
out:
	spin_unlock_irqrestore(&q->lock, flags);
	return 1;
}

int zd_mac_rx(struct ieee80211_hw *hw, const u8 *buffer, unsigned int length)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	struct ieee80211_rx_status stats;
	const struct rx_status *status;
	struct sk_buff *skb;
	int bad_frame = 0;
	__le16 fc;
	int need_padding;
	int i;
	u8 rate;

	if (length < ZD_PLCP_HEADER_SIZE + 10 /* IEEE80211_1ADDR_LEN */ +
	             FCS_LEN + sizeof(struct rx_status))
		return -EINVAL;

	memset(&stats, 0, sizeof(stats));

	/* Note about pass_failed_fcs and pass_ctrl access below:
	 * mac locking intentionally omitted here, as this is the only unlocked
	 * reader and the only writer is configure_filter. Plus, if there were
	 * any races accessing these variables, it wouldn't really matter.
	 * If mac80211 ever provides a way for us to access filter flags
	 * from outside configure_filter, we could improve on this. Also, this
	 * situation may change once we implement some kind of DMA-into-skb
	 * RX path. */

	/* Caller has to ensure that length >= sizeof(struct rx_status). */
	status = (struct rx_status *)
		(buffer + (length - sizeof(struct rx_status)));
	if (status->frame_status & ZD_RX_ERROR) {
		if (mac->pass_failed_fcs &&
				(status->frame_status & ZD_RX_CRC32_ERROR)) {
			stats.flag |= RX_FLAG_FAILED_FCS_CRC;
			bad_frame = 1;
		} else {
			return -EINVAL;
		}
	}

	stats.freq = zd_channels[_zd_chip_get_channel(&mac->chip) - 1].center_freq;
	stats.band = IEEE80211_BAND_2GHZ;
	stats.signal = status->signal_strength;
	stats.qual = zd_rx_qual_percent(buffer,
		                          length - sizeof(struct rx_status),
		                          status);

	rate = zd_rx_rate(buffer, status);

	/* todo: return index in the big switches in zd_rx_rate instead */
	for (i = 0; i < mac->band.n_bitrates; i++)
		if (rate == mac->band.bitrates[i].hw_value)
			stats.rate_idx = i;

	length -= ZD_PLCP_HEADER_SIZE + sizeof(struct rx_status);
	buffer += ZD_PLCP_HEADER_SIZE;

	/* Except for bad frames, filter each frame to see if it is an ACK, in
	 * which case our internal TX tracking is updated. Normally we then
	 * bail here as there's no need to pass ACKs on up to the stack, but
	 * there is also the case where the stack has requested us to pass
	 * control frames on up (pass_ctrl) which we must consider. */
	if (!bad_frame &&
			filter_ack(hw, (struct ieee80211_hdr *)buffer, &stats)
			&& !mac->pass_ctrl)
		return 0;

	fc = *(__le16 *)buffer;
	need_padding = ieee80211_is_data_qos(fc) ^ ieee80211_has_a4(fc);

	skb = dev_alloc_skb(length + (need_padding ? 2 : 0));
	if (skb == NULL)
		return -ENOMEM;
	if (need_padding) {
		/* Make sure the the payload data is 4 byte aligned. */
		skb_reserve(skb, 2);
	}

	memcpy(skb_put(skb, length), buffer, length);

	ieee80211_rx_irqsafe(hw, skb, &stats);
	return 0;
}

static int zd_op_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_if_init_conf *conf)
{
	struct zd_mac *mac = zd_hw_mac(hw);

	/* using IEEE80211_IF_TYPE_INVALID to indicate no mode selected */
	if (mac->type != IEEE80211_IF_TYPE_INVALID)
		return -EOPNOTSUPP;

	switch (conf->type) {
	case IEEE80211_IF_TYPE_MNTR:
	case IEEE80211_IF_TYPE_MESH_POINT:
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		mac->type = conf->type;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return zd_write_mac_addr(&mac->chip, conf->mac_addr);
}

static void zd_op_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_if_init_conf *conf)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	mac->type = IEEE80211_IF_TYPE_INVALID;
	zd_set_beacon_interval(&mac->chip, 0);
	zd_write_mac_addr(&mac->chip, NULL);
}

static int zd_op_config(struct ieee80211_hw *hw, struct ieee80211_conf *conf)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	return zd_chip_set_channel(&mac->chip, conf->channel->hw_value);
}

static int zd_op_config_interface(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				   struct ieee80211_if_conf *conf)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	int associated;
	int r;

	if (mac->type == IEEE80211_IF_TYPE_MESH_POINT ||
	    mac->type == IEEE80211_IF_TYPE_IBSS) {
		associated = true;
		if (conf->changed & IEEE80211_IFCC_BEACON) {
			struct sk_buff *beacon = ieee80211_beacon_get(hw, vif);

			if (!beacon)
				return -ENOMEM;
			r = zd_mac_config_beacon(hw, beacon);
			if (r < 0)
				return r;
			r = zd_set_beacon_interval(&mac->chip, BCN_MODE_IBSS |
					hw->conf.beacon_int);
			if (r < 0)
				return r;
			kfree_skb(beacon);
		}
	} else
		associated = is_valid_ether_addr(conf->bssid);

	spin_lock_irq(&mac->lock);
	mac->associated = associated;
	spin_unlock_irq(&mac->lock);

	/* TODO: do hardware bssid filtering */
	return 0;
}

void zd_process_intr(struct work_struct *work)
{
	u16 int_status;
	struct zd_mac *mac = container_of(work, struct zd_mac, process_intr);

	int_status = le16_to_cpu(*(__le16 *)(mac->intr_buffer+4));
	if (int_status & INT_CFG_NEXT_BCN) {
		if (net_ratelimit())
			dev_dbg_f(zd_mac_dev(mac), "INT_CFG_NEXT_BCN\n");
	} else
		dev_dbg_f(zd_mac_dev(mac), "Unsupported interrupt\n");

	zd_chip_enable_hwint(&mac->chip);
}


static void set_multicast_hash_handler(struct work_struct *work)
{
	struct zd_mac *mac =
		container_of(work, struct zd_mac, set_multicast_hash_work);
	struct zd_mc_hash hash;

	spin_lock_irq(&mac->lock);
	hash = mac->multicast_hash;
	spin_unlock_irq(&mac->lock);

	zd_chip_set_multicast_hash(&mac->chip, &hash);
}

static void set_rx_filter_handler(struct work_struct *work)
{
	struct zd_mac *mac =
		container_of(work, struct zd_mac, set_rx_filter_work);
	int r;

	dev_dbg_f(zd_mac_dev(mac), "\n");
	r = set_rx_filter(mac);
	if (r)
		dev_err(zd_mac_dev(mac), "set_rx_filter_handler error %d\n", r);
}

#define SUPPORTED_FIF_FLAGS \
	(FIF_PROMISC_IN_BSS | FIF_ALLMULTI | FIF_FCSFAIL | FIF_CONTROL | \
	FIF_OTHER_BSS | FIF_BCN_PRBRESP_PROMISC)
static void zd_op_configure_filter(struct ieee80211_hw *hw,
			unsigned int changed_flags,
			unsigned int *new_flags,
			int mc_count, struct dev_mc_list *mclist)
{
	struct zd_mc_hash hash;
	struct zd_mac *mac = zd_hw_mac(hw);
	unsigned long flags;
	int i;

	/* Only deal with supported flags */
	changed_flags &= SUPPORTED_FIF_FLAGS;
	*new_flags &= SUPPORTED_FIF_FLAGS;

	/* changed_flags is always populated but this driver
	 * doesn't support all FIF flags so its possible we don't
	 * need to do anything */
	if (!changed_flags)
		return;

	if (*new_flags & (FIF_PROMISC_IN_BSS | FIF_ALLMULTI)) {
		zd_mc_add_all(&hash);
	} else {
		DECLARE_MAC_BUF(macbuf);

		zd_mc_clear(&hash);
		for (i = 0; i < mc_count; i++) {
			if (!mclist)
				break;
			dev_dbg_f(zd_mac_dev(mac), "mc addr %s\n",
				  print_mac(macbuf, mclist->dmi_addr));
			zd_mc_add_addr(&hash, mclist->dmi_addr);
			mclist = mclist->next;
		}
	}

	spin_lock_irqsave(&mac->lock, flags);
	mac->pass_failed_fcs = !!(*new_flags & FIF_FCSFAIL);
	mac->pass_ctrl = !!(*new_flags & FIF_CONTROL);
	mac->multicast_hash = hash;
	spin_unlock_irqrestore(&mac->lock, flags);
	queue_work(zd_workqueue, &mac->set_multicast_hash_work);

	if (changed_flags & FIF_CONTROL)
		queue_work(zd_workqueue, &mac->set_rx_filter_work);

	/* no handling required for FIF_OTHER_BSS as we don't currently
	 * do BSSID filtering */
	/* FIXME: in future it would be nice to enable the probe response
	 * filter (so that the driver doesn't see them) until
	 * FIF_BCN_PRBRESP_PROMISC is set. however due to atomicity here, we'd
	 * have to schedule work to enable prbresp reception, which might
	 * happen too late. For now we'll just listen and forward them all the
	 * time. */
}

static void set_rts_cts_work(struct work_struct *work)
{
	struct zd_mac *mac =
		container_of(work, struct zd_mac, set_rts_cts_work);
	unsigned long flags;
	unsigned int short_preamble;

	mutex_lock(&mac->chip.mutex);

	spin_lock_irqsave(&mac->lock, flags);
	mac->updating_rts_rate = 0;
	short_preamble = mac->short_preamble;
	spin_unlock_irqrestore(&mac->lock, flags);

	zd_chip_set_rts_cts_rate_locked(&mac->chip, short_preamble);
	mutex_unlock(&mac->chip.mutex);
}

static void zd_op_bss_info_changed(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *bss_conf,
				   u32 changes)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	unsigned long flags;

	dev_dbg_f(zd_mac_dev(mac), "changes: %x\n", changes);

	if (changes & BSS_CHANGED_ERP_PREAMBLE) {
		spin_lock_irqsave(&mac->lock, flags);
		mac->short_preamble = bss_conf->use_short_preamble;
		if (!mac->updating_rts_rate) {
			mac->updating_rts_rate = 1;
			/* FIXME: should disable TX here, until work has
			 * completed and RTS_CTS reg is updated */
			queue_work(zd_workqueue, &mac->set_rts_cts_work);
		}
		spin_unlock_irqrestore(&mac->lock, flags);
	}
}

static const struct ieee80211_ops zd_ops = {
	.tx			= zd_op_tx,
	.start			= zd_op_start,
	.stop			= zd_op_stop,
	.add_interface		= zd_op_add_interface,
	.remove_interface	= zd_op_remove_interface,
	.config			= zd_op_config,
	.config_interface	= zd_op_config_interface,
	.configure_filter	= zd_op_configure_filter,
	.bss_info_changed	= zd_op_bss_info_changed,
};

struct ieee80211_hw *zd_mac_alloc_hw(struct usb_interface *intf)
{
	struct zd_mac *mac;
	struct ieee80211_hw *hw;

	hw = ieee80211_alloc_hw(sizeof(struct zd_mac), &zd_ops);
	if (!hw) {
		dev_dbg_f(&intf->dev, "out of memory\n");
		return NULL;
	}

	mac = zd_hw_mac(hw);

	memset(mac, 0, sizeof(*mac));
	spin_lock_init(&mac->lock);
	mac->hw = hw;

	mac->type = IEEE80211_IF_TYPE_INVALID;

	memcpy(mac->channels, zd_channels, sizeof(zd_channels));
	memcpy(mac->rates, zd_rates, sizeof(zd_rates));
	mac->band.n_bitrates = ARRAY_SIZE(zd_rates);
	mac->band.bitrates = mac->rates;
	mac->band.n_channels = ARRAY_SIZE(zd_channels);
	mac->band.channels = mac->channels;

	hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &mac->band;

	hw->flags = IEEE80211_HW_RX_INCLUDES_FCS |
		    IEEE80211_HW_SIGNAL_DB;

	hw->max_signal = 100;
	hw->queues = 1;
	hw->extra_tx_headroom = sizeof(struct zd_ctrlset);

	skb_queue_head_init(&mac->ack_wait_queue);

	zd_chip_init(&mac->chip, hw, intf);
	housekeeping_init(mac);
	INIT_WORK(&mac->set_multicast_hash_work, set_multicast_hash_handler);
	INIT_WORK(&mac->set_rts_cts_work, set_rts_cts_work);
	INIT_WORK(&mac->set_rx_filter_work, set_rx_filter_handler);
	INIT_WORK(&mac->process_intr, zd_process_intr);

	SET_IEEE80211_DEV(hw, &intf->dev);
	return hw;
}

#define LINK_LED_WORK_DELAY HZ

static void link_led_handler(struct work_struct *work)
{
	struct zd_mac *mac =
		container_of(work, struct zd_mac, housekeeping.link_led_work.work);
	struct zd_chip *chip = &mac->chip;
	int is_associated;
	int r;

	spin_lock_irq(&mac->lock);
	is_associated = mac->associated;
	spin_unlock_irq(&mac->lock);

	r = zd_chip_control_leds(chip,
		                 is_associated ? LED_ASSOCIATED : LED_SCANNING);
	if (r)
		dev_dbg_f(zd_mac_dev(mac), "zd_chip_control_leds error %d\n", r);

	queue_delayed_work(zd_workqueue, &mac->housekeeping.link_led_work,
		           LINK_LED_WORK_DELAY);
}

static void housekeeping_init(struct zd_mac *mac)
{
	INIT_DELAYED_WORK(&mac->housekeeping.link_led_work, link_led_handler);
}

static void housekeeping_enable(struct zd_mac *mac)
{
	dev_dbg_f(zd_mac_dev(mac), "\n");
	queue_delayed_work(zd_workqueue, &mac->housekeeping.link_led_work,
			   0);
}

static void housekeeping_disable(struct zd_mac *mac)
{
	dev_dbg_f(zd_mac_dev(mac), "\n");
	cancel_rearming_delayed_workqueue(zd_workqueue,
		&mac->housekeeping.link_led_work);
	zd_chip_control_leds(&mac->chip, LED_OFF);
}
