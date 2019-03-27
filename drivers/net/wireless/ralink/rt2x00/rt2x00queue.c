/*
	Copyright (C) 2010 Willow Garage <http://www.willowgarage.com>
	Copyright (C) 2004 - 2010 Ivo van Doorn <IvDoorn@gmail.com>
	Copyright (C) 2004 - 2009 Gertjan van Wingerde <gwingerde@gmail.com>
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
	along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
	Module: rt2x00lib
	Abstract: rt2x00 queue specific routines.
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

struct sk_buff *rt2x00queue_alloc_rxskb(struct queue_entry *entry, gfp_t gfp)
{
	struct data_queue *queue = entry->queue;
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	struct sk_buff *skb;
	struct skb_frame_desc *skbdesc;
	unsigned int frame_size;
	unsigned int head_size = 0;
	unsigned int tail_size = 0;

	/*
	 * The frame size includes descriptor size, because the
	 * hardware directly receive the frame into the skbuffer.
	 */
	frame_size = queue->data_size + queue->desc_size + queue->winfo_size;

	/*
	 * The payload should be aligned to a 4-byte boundary,
	 * this means we need at least 3 bytes for moving the frame
	 * into the correct offset.
	 */
	head_size = 4;

	/*
	 * For IV/EIV/ICV assembly we must make sure there is
	 * at least 8 bytes bytes available in headroom for IV/EIV
	 * and 8 bytes for ICV data as tailroon.
	 */
	if (rt2x00_has_cap_hw_crypto(rt2x00dev)) {
		head_size += 8;
		tail_size += 8;
	}

	/*
	 * Allocate skbuffer.
	 */
	skb = __dev_alloc_skb(frame_size + head_size + tail_size, gfp);
	if (!skb)
		return NULL;

	/*
	 * Make sure we not have a frame with the requested bytes
	 * available in the head and tail.
	 */
	skb_reserve(skb, head_size);
	skb_put(skb, frame_size);

	/*
	 * Populate skbdesc.
	 */
	skbdesc = get_skb_frame_desc(skb);
	memset(skbdesc, 0, sizeof(*skbdesc));

	if (rt2x00_has_cap_flag(rt2x00dev, REQUIRE_DMA)) {
		dma_addr_t skb_dma;

		skb_dma = dma_map_single(rt2x00dev->dev, skb->data, skb->len,
					 DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(rt2x00dev->dev, skb_dma))) {
			dev_kfree_skb_any(skb);
			return NULL;
		}

		skbdesc->skb_dma = skb_dma;
		skbdesc->flags |= SKBDESC_DMA_MAPPED_RX;
	}

	return skb;
}

int rt2x00queue_map_txskb(struct queue_entry *entry)
{
	struct device *dev = entry->queue->rt2x00dev->dev;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);

	skbdesc->skb_dma =
	    dma_map_single(dev, entry->skb->data, entry->skb->len, DMA_TO_DEVICE);

	if (unlikely(dma_mapping_error(dev, skbdesc->skb_dma)))
		return -ENOMEM;

	skbdesc->flags |= SKBDESC_DMA_MAPPED_TX;
	rt2x00lib_dmadone(entry);
	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00queue_map_txskb);

void rt2x00queue_unmap_skb(struct queue_entry *entry)
{
	struct device *dev = entry->queue->rt2x00dev->dev;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);

	if (skbdesc->flags & SKBDESC_DMA_MAPPED_RX) {
		dma_unmap_single(dev, skbdesc->skb_dma, entry->skb->len,
				 DMA_FROM_DEVICE);
		skbdesc->flags &= ~SKBDESC_DMA_MAPPED_RX;
	} else if (skbdesc->flags & SKBDESC_DMA_MAPPED_TX) {
		dma_unmap_single(dev, skbdesc->skb_dma, entry->skb->len,
				 DMA_TO_DEVICE);
		skbdesc->flags &= ~SKBDESC_DMA_MAPPED_TX;
	}
}
EXPORT_SYMBOL_GPL(rt2x00queue_unmap_skb);

void rt2x00queue_free_skb(struct queue_entry *entry)
{
	if (!entry->skb)
		return;

	rt2x00queue_unmap_skb(entry);
	dev_kfree_skb_any(entry->skb);
	entry->skb = NULL;
}

void rt2x00queue_align_frame(struct sk_buff *skb)
{
	unsigned int frame_length = skb->len;
	unsigned int align = ALIGN_SIZE(skb, 0);

	if (!align)
		return;

	skb_push(skb, align);
	memmove(skb->data, skb->data + align, frame_length);
	skb_trim(skb, frame_length);
}

/*
 * H/W needs L2 padding between the header and the paylod if header size
 * is not 4 bytes aligned.
 */
void rt2x00queue_insert_l2pad(struct sk_buff *skb, unsigned int hdr_len)
{
	unsigned int l2pad = (skb->len > hdr_len) ? L2PAD_SIZE(hdr_len) : 0;

	if (!l2pad)
		return;

	skb_push(skb, l2pad);
	memmove(skb->data, skb->data + l2pad, hdr_len);
}

void rt2x00queue_remove_l2pad(struct sk_buff *skb, unsigned int hdr_len)
{
	unsigned int l2pad = (skb->len > hdr_len) ? L2PAD_SIZE(hdr_len) : 0;

	if (!l2pad)
		return;

	memmove(skb->data + l2pad, skb->data, hdr_len);
	skb_pull(skb, l2pad);
}

static void rt2x00queue_create_tx_descriptor_seq(struct rt2x00_dev *rt2x00dev,
						 struct sk_buff *skb,
						 struct txentry_desc *txdesc)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct rt2x00_intf *intf = vif_to_intf(tx_info->control.vif);
	u16 seqno;

	if (!(tx_info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ))
		return;

	__set_bit(ENTRY_TXD_GENERATE_SEQ, &txdesc->flags);

	if (!rt2x00_has_cap_flag(rt2x00dev, REQUIRE_SW_SEQNO)) {
		/*
		 * rt2800 has a H/W (or F/W) bug, device incorrectly increase
		 * seqno on retransmitted data (non-QOS) and management frames.
		 * To workaround the problem let's generate seqno in software.
		 * Except for beacons which are transmitted periodically by H/W
		 * hence hardware has to assign seqno for them.
		 */
	    	if (ieee80211_is_beacon(hdr->frame_control)) {
			__set_bit(ENTRY_TXD_GENERATE_SEQ, &txdesc->flags);
			/* H/W will generate sequence number */
			return;
		}

		__clear_bit(ENTRY_TXD_GENERATE_SEQ, &txdesc->flags);
	}

	/*
	 * The hardware is not able to insert a sequence number. Assign a
	 * software generated one here.
	 *
	 * This is wrong because beacons are not getting sequence
	 * numbers assigned properly.
	 *
	 * A secondary problem exists for drivers that cannot toggle
	 * sequence counting per-frame, since those will override the
	 * sequence counter given by mac80211.
	 */
	if (test_bit(ENTRY_TXD_FIRST_FRAGMENT, &txdesc->flags))
		seqno = atomic_add_return(0x10, &intf->seqno);
	else
		seqno = atomic_read(&intf->seqno);

	hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
	hdr->seq_ctrl |= cpu_to_le16(seqno);
}

static void rt2x00queue_create_tx_descriptor_plcp(struct rt2x00_dev *rt2x00dev,
						  struct sk_buff *skb,
						  struct txentry_desc *txdesc,
						  const struct rt2x00_rate *hwrate)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *txrate = &tx_info->control.rates[0];
	unsigned int data_length;
	unsigned int duration;
	unsigned int residual;

	/*
	 * Determine with what IFS priority this frame should be send.
	 * Set ifs to IFS_SIFS when the this is not the first fragment,
	 * or this fragment came after RTS/CTS.
	 */
	if (test_bit(ENTRY_TXD_FIRST_FRAGMENT, &txdesc->flags))
		txdesc->u.plcp.ifs = IFS_BACKOFF;
	else
		txdesc->u.plcp.ifs = IFS_SIFS;

	/* Data length + CRC + Crypto overhead (IV/EIV/ICV/MIC) */
	data_length = skb->len + 4;
	data_length += rt2x00crypto_tx_overhead(rt2x00dev, skb);

	/*
	 * PLCP setup
	 * Length calculation depends on OFDM/CCK rate.
	 */
	txdesc->u.plcp.signal = hwrate->plcp;
	txdesc->u.plcp.service = 0x04;

	if (hwrate->flags & DEV_RATE_OFDM) {
		txdesc->u.plcp.length_high = (data_length >> 6) & 0x3f;
		txdesc->u.plcp.length_low = data_length & 0x3f;
	} else {
		/*
		 * Convert length to microseconds.
		 */
		residual = GET_DURATION_RES(data_length, hwrate->bitrate);
		duration = GET_DURATION(data_length, hwrate->bitrate);

		if (residual != 0) {
			duration++;

			/*
			 * Check if we need to set the Length Extension
			 */
			if (hwrate->bitrate == 110 && residual <= 30)
				txdesc->u.plcp.service |= 0x80;
		}

		txdesc->u.plcp.length_high = (duration >> 8) & 0xff;
		txdesc->u.plcp.length_low = duration & 0xff;

		/*
		 * When preamble is enabled we should set the
		 * preamble bit for the signal.
		 */
		if (txrate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			txdesc->u.plcp.signal |= 0x08;
	}
}

static void rt2x00queue_create_tx_descriptor_ht(struct rt2x00_dev *rt2x00dev,
						struct sk_buff *skb,
						struct txentry_desc *txdesc,
						struct ieee80211_sta *sta,
						const struct rt2x00_rate *hwrate)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *txrate = &tx_info->control.rates[0];
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct rt2x00_sta *sta_priv = NULL;
	u8 density = 0;

	if (sta) {
		sta_priv = sta_to_rt2x00_sta(sta);
		txdesc->u.ht.wcid = sta_priv->wcid;
		density = sta->ht_cap.ampdu_density;
	}

	/*
	 * If IEEE80211_TX_RC_MCS is set txrate->idx just contains the
	 * mcs rate to be used
	 */
	if (txrate->flags & IEEE80211_TX_RC_MCS) {
		txdesc->u.ht.mcs = txrate->idx;

		/*
		 * MIMO PS should be set to 1 for STA's using dynamic SM PS
		 * when using more then one tx stream (>MCS7).
		 */
		if (sta && txdesc->u.ht.mcs > 7 &&
		    sta->smps_mode == IEEE80211_SMPS_DYNAMIC)
			__set_bit(ENTRY_TXD_HT_MIMO_PS, &txdesc->flags);
	} else {
		txdesc->u.ht.mcs = rt2x00_get_rate_mcs(hwrate->mcs);
		if (txrate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			txdesc->u.ht.mcs |= 0x08;
	}

	if (test_bit(CONFIG_HT_DISABLED, &rt2x00dev->flags)) {
		if (!(tx_info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT))
			txdesc->u.ht.txop = TXOP_SIFS;
		else
			txdesc->u.ht.txop = TXOP_BACKOFF;

		/* Left zero on all other settings. */
		return;
	}

	/*
	 * Only one STBC stream is supported for now.
	 */
	if (tx_info->flags & IEEE80211_TX_CTL_STBC)
		txdesc->u.ht.stbc = 1;

	/*
	 * This frame is eligible for an AMPDU, however, don't aggregate
	 * frames that are intended to probe a specific tx rate.
	 */
	if (tx_info->flags & IEEE80211_TX_CTL_AMPDU &&
	    !(tx_info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)) {
		__set_bit(ENTRY_TXD_HT_AMPDU, &txdesc->flags);
		txdesc->u.ht.mpdu_density = density;
		txdesc->u.ht.ba_size = 7; /* FIXME: What value is needed? */
	}

	/*
	 * Set 40Mhz mode if necessary (for legacy rates this will
	 * duplicate the frame to both channels).
	 */
	if (txrate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH ||
	    txrate->flags & IEEE80211_TX_RC_DUP_DATA)
		__set_bit(ENTRY_TXD_HT_BW_40, &txdesc->flags);
	if (txrate->flags & IEEE80211_TX_RC_SHORT_GI)
		__set_bit(ENTRY_TXD_HT_SHORT_GI, &txdesc->flags);

	/*
	 * Determine IFS values
	 * - Use TXOP_BACKOFF for management frames except beacons
	 * - Use TXOP_SIFS for fragment bursts
	 * - Use TXOP_HTTXOP for everything else
	 *
	 * Note: rt2800 devices won't use CTS protection (if used)
	 * for frames not transmitted with TXOP_HTTXOP
	 */
	if (ieee80211_is_mgmt(hdr->frame_control) &&
	    !ieee80211_is_beacon(hdr->frame_control))
		txdesc->u.ht.txop = TXOP_BACKOFF;
	else if (!(tx_info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT))
		txdesc->u.ht.txop = TXOP_SIFS;
	else
		txdesc->u.ht.txop = TXOP_HTTXOP;
}

static void rt2x00queue_create_tx_descriptor(struct rt2x00_dev *rt2x00dev,
					     struct sk_buff *skb,
					     struct txentry_desc *txdesc,
					     struct ieee80211_sta *sta)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_rate *txrate = &tx_info->control.rates[0];
	struct ieee80211_rate *rate;
	const struct rt2x00_rate *hwrate = NULL;

	memset(txdesc, 0, sizeof(*txdesc));

	/*
	 * Header and frame information.
	 */
	txdesc->length = skb->len;
	txdesc->header_length = ieee80211_get_hdrlen_from_skb(skb);

	/*
	 * Check whether this frame is to be acked.
	 */
	if (!(tx_info->flags & IEEE80211_TX_CTL_NO_ACK))
		__set_bit(ENTRY_TXD_ACK, &txdesc->flags);

	/*
	 * Check if this is a RTS/CTS frame
	 */
	if (ieee80211_is_rts(hdr->frame_control) ||
	    ieee80211_is_cts(hdr->frame_control)) {
		__set_bit(ENTRY_TXD_BURST, &txdesc->flags);
		if (ieee80211_is_rts(hdr->frame_control))
			__set_bit(ENTRY_TXD_RTS_FRAME, &txdesc->flags);
		else
			__set_bit(ENTRY_TXD_CTS_FRAME, &txdesc->flags);
		if (tx_info->control.rts_cts_rate_idx >= 0)
			rate =
			    ieee80211_get_rts_cts_rate(rt2x00dev->hw, tx_info);
	}

	/*
	 * Determine retry information.
	 */
	txdesc->retry_limit = tx_info->control.rates[0].count - 1;
	if (txdesc->retry_limit >= rt2x00dev->long_retry)
		__set_bit(ENTRY_TXD_RETRY_MODE, &txdesc->flags);

	/*
	 * Check if more fragments are pending
	 */
	if (ieee80211_has_morefrags(hdr->frame_control)) {
		__set_bit(ENTRY_TXD_BURST, &txdesc->flags);
		__set_bit(ENTRY_TXD_MORE_FRAG, &txdesc->flags);
	}

	/*
	 * Check if more frames (!= fragments) are pending
	 */
	if (tx_info->flags & IEEE80211_TX_CTL_MORE_FRAMES)
		__set_bit(ENTRY_TXD_BURST, &txdesc->flags);

	/*
	 * Beacons and probe responses require the tsf timestamp
	 * to be inserted into the frame.
	 */
	if (ieee80211_is_beacon(hdr->frame_control) ||
	    ieee80211_is_probe_resp(hdr->frame_control))
		__set_bit(ENTRY_TXD_REQ_TIMESTAMP, &txdesc->flags);

	if ((tx_info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT) &&
	    !test_bit(ENTRY_TXD_RTS_FRAME, &txdesc->flags))
		__set_bit(ENTRY_TXD_FIRST_FRAGMENT, &txdesc->flags);

	/*
	 * Determine rate modulation.
	 */
	if (txrate->flags & IEEE80211_TX_RC_GREEN_FIELD)
		txdesc->rate_mode = RATE_MODE_HT_GREENFIELD;
	else if (txrate->flags & IEEE80211_TX_RC_MCS)
		txdesc->rate_mode = RATE_MODE_HT_MIX;
	else {
		rate = ieee80211_get_tx_rate(rt2x00dev->hw, tx_info);
		hwrate = rt2x00_get_rate(rate->hw_value);
		if (hwrate->flags & DEV_RATE_OFDM)
			txdesc->rate_mode = RATE_MODE_OFDM;
		else
			txdesc->rate_mode = RATE_MODE_CCK;
	}

	/*
	 * Apply TX descriptor handling by components
	 */
	rt2x00crypto_create_tx_descriptor(rt2x00dev, skb, txdesc);
	rt2x00queue_create_tx_descriptor_seq(rt2x00dev, skb, txdesc);

	if (rt2x00_has_cap_flag(rt2x00dev, REQUIRE_HT_TX_DESC))
		rt2x00queue_create_tx_descriptor_ht(rt2x00dev, skb, txdesc,
						   sta, hwrate);
	else
		rt2x00queue_create_tx_descriptor_plcp(rt2x00dev, skb, txdesc,
						      hwrate);
}

static int rt2x00queue_write_tx_data(struct queue_entry *entry,
				     struct txentry_desc *txdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;

	/*
	 * This should not happen, we already checked the entry
	 * was ours. When the hardware disagrees there has been
	 * a queue corruption!
	 */
	if (unlikely(rt2x00dev->ops->lib->get_entry_state &&
		     rt2x00dev->ops->lib->get_entry_state(entry))) {
		rt2x00_err(rt2x00dev,
			   "Corrupt queue %d, accessing entry which is not ours\n"
			   "Please file bug report to %s\n",
			   entry->queue->qid, DRV_PROJECT);
		return -EINVAL;
	}

	/*
	 * Add the requested extra tx headroom in front of the skb.
	 */
	skb_push(entry->skb, rt2x00dev->extra_tx_headroom);
	memset(entry->skb->data, 0, rt2x00dev->extra_tx_headroom);

	/*
	 * Call the driver's write_tx_data function, if it exists.
	 */
	if (rt2x00dev->ops->lib->write_tx_data)
		rt2x00dev->ops->lib->write_tx_data(entry, txdesc);

	/*
	 * Map the skb to DMA.
	 */
	if (rt2x00_has_cap_flag(rt2x00dev, REQUIRE_DMA) &&
	    rt2x00queue_map_txskb(entry))
		return -ENOMEM;

	return 0;
}

static void rt2x00queue_write_tx_descriptor(struct queue_entry *entry,
					    struct txentry_desc *txdesc)
{
	struct data_queue *queue = entry->queue;

	queue->rt2x00dev->ops->lib->write_tx_desc(entry, txdesc);

	/*
	 * All processing on the frame has been completed, this means
	 * it is now ready to be dumped to userspace through debugfs.
	 */
	rt2x00debug_dump_frame(queue->rt2x00dev, DUMP_FRAME_TX, entry);
}

static void rt2x00queue_kick_tx_queue(struct data_queue *queue,
				      struct txentry_desc *txdesc)
{
	/*
	 * Check if we need to kick the queue, there are however a few rules
	 *	1) Don't kick unless this is the last in frame in a burst.
	 *	   When the burst flag is set, this frame is always followed
	 *	   by another frame which in some way are related to eachother.
	 *	   This is true for fragments, RTS or CTS-to-self frames.
	 *	2) Rule 1 can be broken when the available entries
	 *	   in the queue are less then a certain threshold.
	 */
	if (rt2x00queue_threshold(queue) ||
	    !test_bit(ENTRY_TXD_BURST, &txdesc->flags))
		queue->rt2x00dev->ops->lib->kick_queue(queue);
}

static void rt2x00queue_bar_check(struct queue_entry *entry)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct ieee80211_bar *bar = (void *) (entry->skb->data +
				    rt2x00dev->extra_tx_headroom);
	struct rt2x00_bar_list_entry *bar_entry;

	if (likely(!ieee80211_is_back_req(bar->frame_control)))
		return;

	bar_entry = kmalloc(sizeof(*bar_entry), GFP_ATOMIC);

	/*
	 * If the alloc fails we still send the BAR out but just don't track
	 * it in our bar list. And as a result we will report it to mac80211
	 * back as failed.
	 */
	if (!bar_entry)
		return;

	bar_entry->entry = entry;
	bar_entry->block_acked = 0;

	/*
	 * Copy the relevant parts of the 802.11 BAR into out check list
	 * such that we can use RCU for less-overhead in the RX path since
	 * sending BARs and processing the according BlockAck should be
	 * the exception.
	 */
	memcpy(bar_entry->ra, bar->ra, sizeof(bar->ra));
	memcpy(bar_entry->ta, bar->ta, sizeof(bar->ta));
	bar_entry->control = bar->control;
	bar_entry->start_seq_num = bar->start_seq_num;

	/*
	 * Insert BAR into our BAR check list.
	 */
	spin_lock_bh(&rt2x00dev->bar_list_lock);
	list_add_tail_rcu(&bar_entry->list, &rt2x00dev->bar_list);
	spin_unlock_bh(&rt2x00dev->bar_list_lock);
}

int rt2x00queue_write_tx_frame(struct data_queue *queue, struct sk_buff *skb,
			       struct ieee80211_sta *sta, bool local)
{
	struct ieee80211_tx_info *tx_info;
	struct queue_entry *entry;
	struct txentry_desc txdesc;
	struct skb_frame_desc *skbdesc;
	u8 rate_idx, rate_flags;
	int ret = 0;

	/*
	 * Copy all TX descriptor information into txdesc,
	 * after that we are free to use the skb->cb array
	 * for our information.
	 */
	rt2x00queue_create_tx_descriptor(queue->rt2x00dev, skb, &txdesc, sta);

	/*
	 * All information is retrieved from the skb->cb array,
	 * now we should claim ownership of the driver part of that
	 * array, preserving the bitrate index and flags.
	 */
	tx_info = IEEE80211_SKB_CB(skb);
	rate_idx = tx_info->control.rates[0].idx;
	rate_flags = tx_info->control.rates[0].flags;
	skbdesc = get_skb_frame_desc(skb);
	memset(skbdesc, 0, sizeof(*skbdesc));
	skbdesc->tx_rate_idx = rate_idx;
	skbdesc->tx_rate_flags = rate_flags;

	if (local)
		skbdesc->flags |= SKBDESC_NOT_MAC80211;

	/*
	 * When hardware encryption is supported, and this frame
	 * is to be encrypted, we should strip the IV/EIV data from
	 * the frame so we can provide it to the driver separately.
	 */
	if (test_bit(ENTRY_TXD_ENCRYPT, &txdesc.flags) &&
	    !test_bit(ENTRY_TXD_ENCRYPT_IV, &txdesc.flags)) {
		if (rt2x00_has_cap_flag(queue->rt2x00dev, REQUIRE_COPY_IV))
			rt2x00crypto_tx_copy_iv(skb, &txdesc);
		else
			rt2x00crypto_tx_remove_iv(skb, &txdesc);
	}

	/*
	 * When DMA allocation is required we should guarantee to the
	 * driver that the DMA is aligned to a 4-byte boundary.
	 * However some drivers require L2 padding to pad the payload
	 * rather then the header. This could be a requirement for
	 * PCI and USB devices, while header alignment only is valid
	 * for PCI devices.
	 */
	if (rt2x00_has_cap_flag(queue->rt2x00dev, REQUIRE_L2PAD))
		rt2x00queue_insert_l2pad(skb, txdesc.header_length);
	else if (rt2x00_has_cap_flag(queue->rt2x00dev, REQUIRE_DMA))
		rt2x00queue_align_frame(skb);

	/*
	 * That function must be called with bh disabled.
	 */
	spin_lock(&queue->tx_lock);

	if (unlikely(rt2x00queue_full(queue))) {
		rt2x00_dbg(queue->rt2x00dev, "Dropping frame due to full tx queue %d\n",
			   queue->qid);
		ret = -ENOBUFS;
		goto out;
	}

	entry = rt2x00queue_get_entry(queue, Q_INDEX);

	if (unlikely(test_and_set_bit(ENTRY_OWNER_DEVICE_DATA,
				      &entry->flags))) {
		rt2x00_err(queue->rt2x00dev,
			   "Arrived at non-free entry in the non-full queue %d\n"
			   "Please file bug report to %s\n",
			   queue->qid, DRV_PROJECT);
		ret = -EINVAL;
		goto out;
	}

	entry->skb = skb;

	/*
	 * It could be possible that the queue was corrupted and this
	 * call failed. Since we always return NETDEV_TX_OK to mac80211,
	 * this frame will simply be dropped.
	 */
	if (unlikely(rt2x00queue_write_tx_data(entry, &txdesc))) {
		clear_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags);
		entry->skb = NULL;
		ret = -EIO;
		goto out;
	}

	/*
	 * Put BlockAckReqs into our check list for driver BA processing.
	 */
	rt2x00queue_bar_check(entry);

	set_bit(ENTRY_DATA_PENDING, &entry->flags);

	rt2x00queue_index_inc(entry, Q_INDEX);
	rt2x00queue_write_tx_descriptor(entry, &txdesc);
	rt2x00queue_kick_tx_queue(queue, &txdesc);

out:
	/*
	 * Pausing queue has to be serialized with rt2x00lib_txdone(), so we
	 * do this under queue->tx_lock. Bottom halve was already disabled
	 * before ieee80211_xmit() call.
	 */
	if (rt2x00queue_threshold(queue))
		rt2x00queue_pause_queue(queue);

	spin_unlock(&queue->tx_lock);
	return ret;
}

int rt2x00queue_clear_beacon(struct rt2x00_dev *rt2x00dev,
			     struct ieee80211_vif *vif)
{
	struct rt2x00_intf *intf = vif_to_intf(vif);

	if (unlikely(!intf->beacon))
		return -ENOBUFS;

	/*
	 * Clean up the beacon skb.
	 */
	rt2x00queue_free_skb(intf->beacon);

	/*
	 * Clear beacon (single bssid devices don't need to clear the beacon
	 * since the beacon queue will get stopped anyway).
	 */
	if (rt2x00dev->ops->lib->clear_beacon)
		rt2x00dev->ops->lib->clear_beacon(intf->beacon);

	return 0;
}

int rt2x00queue_update_beacon(struct rt2x00_dev *rt2x00dev,
			      struct ieee80211_vif *vif)
{
	struct rt2x00_intf *intf = vif_to_intf(vif);
	struct skb_frame_desc *skbdesc;
	struct txentry_desc txdesc;

	if (unlikely(!intf->beacon))
		return -ENOBUFS;

	/*
	 * Clean up the beacon skb.
	 */
	rt2x00queue_free_skb(intf->beacon);

	intf->beacon->skb = ieee80211_beacon_get(rt2x00dev->hw, vif);
	if (!intf->beacon->skb)
		return -ENOMEM;

	/*
	 * Copy all TX descriptor information into txdesc,
	 * after that we are free to use the skb->cb array
	 * for our information.
	 */
	rt2x00queue_create_tx_descriptor(rt2x00dev, intf->beacon->skb, &txdesc, NULL);

	/*
	 * Fill in skb descriptor
	 */
	skbdesc = get_skb_frame_desc(intf->beacon->skb);
	memset(skbdesc, 0, sizeof(*skbdesc));

	/*
	 * Send beacon to hardware.
	 */
	rt2x00dev->ops->lib->write_beacon(intf->beacon, &txdesc);

	return 0;

}

bool rt2x00queue_for_each_entry(struct data_queue *queue,
				enum queue_index start,
				enum queue_index end,
				void *data,
				bool (*fn)(struct queue_entry *entry,
					   void *data))
{
	unsigned long irqflags;
	unsigned int index_start;
	unsigned int index_end;
	unsigned int i;

	if (unlikely(start >= Q_INDEX_MAX || end >= Q_INDEX_MAX)) {
		rt2x00_err(queue->rt2x00dev,
			   "Entry requested from invalid index range (%d - %d)\n",
			   start, end);
		return true;
	}

	/*
	 * Only protect the range we are going to loop over,
	 * if during our loop a extra entry is set to pending
	 * it should not be kicked during this run, since it
	 * is part of another TX operation.
	 */
	spin_lock_irqsave(&queue->index_lock, irqflags);
	index_start = queue->index[start];
	index_end = queue->index[end];
	spin_unlock_irqrestore(&queue->index_lock, irqflags);

	/*
	 * Start from the TX done pointer, this guarantees that we will
	 * send out all frames in the correct order.
	 */
	if (index_start < index_end) {
		for (i = index_start; i < index_end; i++) {
			if (fn(&queue->entries[i], data))
				return true;
		}
	} else {
		for (i = index_start; i < queue->limit; i++) {
			if (fn(&queue->entries[i], data))
				return true;
		}

		for (i = 0; i < index_end; i++) {
			if (fn(&queue->entries[i], data))
				return true;
		}
	}

	return false;
}
EXPORT_SYMBOL_GPL(rt2x00queue_for_each_entry);

struct queue_entry *rt2x00queue_get_entry(struct data_queue *queue,
					  enum queue_index index)
{
	struct queue_entry *entry;
	unsigned long irqflags;

	if (unlikely(index >= Q_INDEX_MAX)) {
		rt2x00_err(queue->rt2x00dev, "Entry requested from invalid index type (%d)\n",
			   index);
		return NULL;
	}

	spin_lock_irqsave(&queue->index_lock, irqflags);

	entry = &queue->entries[queue->index[index]];

	spin_unlock_irqrestore(&queue->index_lock, irqflags);

	return entry;
}
EXPORT_SYMBOL_GPL(rt2x00queue_get_entry);

void rt2x00queue_index_inc(struct queue_entry *entry, enum queue_index index)
{
	struct data_queue *queue = entry->queue;
	unsigned long irqflags;

	if (unlikely(index >= Q_INDEX_MAX)) {
		rt2x00_err(queue->rt2x00dev,
			   "Index change on invalid index type (%d)\n", index);
		return;
	}

	spin_lock_irqsave(&queue->index_lock, irqflags);

	queue->index[index]++;
	if (queue->index[index] >= queue->limit)
		queue->index[index] = 0;

	entry->last_action = jiffies;

	if (index == Q_INDEX) {
		queue->length++;
	} else if (index == Q_INDEX_DONE) {
		queue->length--;
		queue->count++;
	}

	spin_unlock_irqrestore(&queue->index_lock, irqflags);
}

static void rt2x00queue_pause_queue_nocheck(struct data_queue *queue)
{
	switch (queue->qid) {
	case QID_AC_VO:
	case QID_AC_VI:
	case QID_AC_BE:
	case QID_AC_BK:
		/*
		 * For TX queues, we have to disable the queue
		 * inside mac80211.
		 */
		ieee80211_stop_queue(queue->rt2x00dev->hw, queue->qid);
		break;
	default:
		break;
	}
}
void rt2x00queue_pause_queue(struct data_queue *queue)
{
	if (!test_bit(DEVICE_STATE_PRESENT, &queue->rt2x00dev->flags) ||
	    !test_bit(QUEUE_STARTED, &queue->flags) ||
	    test_and_set_bit(QUEUE_PAUSED, &queue->flags))
		return;

	rt2x00queue_pause_queue_nocheck(queue);
}
EXPORT_SYMBOL_GPL(rt2x00queue_pause_queue);

void rt2x00queue_unpause_queue(struct data_queue *queue)
{
	if (!test_bit(DEVICE_STATE_PRESENT, &queue->rt2x00dev->flags) ||
	    !test_bit(QUEUE_STARTED, &queue->flags) ||
	    !test_and_clear_bit(QUEUE_PAUSED, &queue->flags))
		return;

	switch (queue->qid) {
	case QID_AC_VO:
	case QID_AC_VI:
	case QID_AC_BE:
	case QID_AC_BK:
		/*
		 * For TX queues, we have to enable the queue
		 * inside mac80211.
		 */
		ieee80211_wake_queue(queue->rt2x00dev->hw, queue->qid);
		break;
	case QID_RX:
		/*
		 * For RX we need to kick the queue now in order to
		 * receive frames.
		 */
		queue->rt2x00dev->ops->lib->kick_queue(queue);
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(rt2x00queue_unpause_queue);

void rt2x00queue_start_queue(struct data_queue *queue)
{
	mutex_lock(&queue->status_lock);

	if (!test_bit(DEVICE_STATE_PRESENT, &queue->rt2x00dev->flags) ||
	    test_and_set_bit(QUEUE_STARTED, &queue->flags)) {
		mutex_unlock(&queue->status_lock);
		return;
	}

	set_bit(QUEUE_PAUSED, &queue->flags);

	queue->rt2x00dev->ops->lib->start_queue(queue);

	rt2x00queue_unpause_queue(queue);

	mutex_unlock(&queue->status_lock);
}
EXPORT_SYMBOL_GPL(rt2x00queue_start_queue);

void rt2x00queue_stop_queue(struct data_queue *queue)
{
	mutex_lock(&queue->status_lock);

	if (!test_and_clear_bit(QUEUE_STARTED, &queue->flags)) {
		mutex_unlock(&queue->status_lock);
		return;
	}

	rt2x00queue_pause_queue_nocheck(queue);

	queue->rt2x00dev->ops->lib->stop_queue(queue);

	mutex_unlock(&queue->status_lock);
}
EXPORT_SYMBOL_GPL(rt2x00queue_stop_queue);

void rt2x00queue_flush_queue(struct data_queue *queue, bool drop)
{
	bool tx_queue =
		(queue->qid == QID_AC_VO) ||
		(queue->qid == QID_AC_VI) ||
		(queue->qid == QID_AC_BE) ||
		(queue->qid == QID_AC_BK);

	if (rt2x00queue_empty(queue))
		return;

	/*
	 * If we are not supposed to drop any pending
	 * frames, this means we must force a start (=kick)
	 * to the queue to make sure the hardware will
	 * start transmitting.
	 */
	if (!drop && tx_queue)
		queue->rt2x00dev->ops->lib->kick_queue(queue);

	/*
	 * Check if driver supports flushing, if that is the case we can
	 * defer the flushing to the driver. Otherwise we must use the
	 * alternative which just waits for the queue to become empty.
	 */
	if (likely(queue->rt2x00dev->ops->lib->flush_queue))
		queue->rt2x00dev->ops->lib->flush_queue(queue, drop);

	/*
	 * The queue flush has failed...
	 */
	if (unlikely(!rt2x00queue_empty(queue)))
		rt2x00_warn(queue->rt2x00dev, "Queue %d failed to flush\n",
			    queue->qid);
}
EXPORT_SYMBOL_GPL(rt2x00queue_flush_queue);

void rt2x00queue_start_queues(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;

	/*
	 * rt2x00queue_start_queue will call ieee80211_wake_queue
	 * for each queue after is has been properly initialized.
	 */
	tx_queue_for_each(rt2x00dev, queue)
		rt2x00queue_start_queue(queue);

	rt2x00queue_start_queue(rt2x00dev->rx);
}
EXPORT_SYMBOL_GPL(rt2x00queue_start_queues);

void rt2x00queue_stop_queues(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;

	/*
	 * rt2x00queue_stop_queue will call ieee80211_stop_queue
	 * as well, but we are completely shutting doing everything
	 * now, so it is much safer to stop all TX queues at once,
	 * and use rt2x00queue_stop_queue for cleaning up.
	 */
	ieee80211_stop_queues(rt2x00dev->hw);

	tx_queue_for_each(rt2x00dev, queue)
		rt2x00queue_stop_queue(queue);

	rt2x00queue_stop_queue(rt2x00dev->rx);
}
EXPORT_SYMBOL_GPL(rt2x00queue_stop_queues);

void rt2x00queue_flush_queues(struct rt2x00_dev *rt2x00dev, bool drop)
{
	struct data_queue *queue;

	tx_queue_for_each(rt2x00dev, queue)
		rt2x00queue_flush_queue(queue, drop);

	rt2x00queue_flush_queue(rt2x00dev->rx, drop);
}
EXPORT_SYMBOL_GPL(rt2x00queue_flush_queues);

static void rt2x00queue_reset(struct data_queue *queue)
{
	unsigned long irqflags;
	unsigned int i;

	spin_lock_irqsave(&queue->index_lock, irqflags);

	queue->count = 0;
	queue->length = 0;

	for (i = 0; i < Q_INDEX_MAX; i++)
		queue->index[i] = 0;

	spin_unlock_irqrestore(&queue->index_lock, irqflags);
}

void rt2x00queue_init_queues(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	unsigned int i;

	queue_for_each(rt2x00dev, queue) {
		rt2x00queue_reset(queue);

		for (i = 0; i < queue->limit; i++)
			rt2x00dev->ops->lib->clear_entry(&queue->entries[i]);
	}
}

static int rt2x00queue_alloc_entries(struct data_queue *queue)
{
	struct queue_entry *entries;
	unsigned int entry_size;
	unsigned int i;

	rt2x00queue_reset(queue);

	/*
	 * Allocate all queue entries.
	 */
	entry_size = sizeof(*entries) + queue->priv_size;
	entries = kcalloc(queue->limit, entry_size, GFP_KERNEL);
	if (!entries)
		return -ENOMEM;

#define QUEUE_ENTRY_PRIV_OFFSET(__base, __index, __limit, __esize, __psize) \
	(((char *)(__base)) + ((__limit) * (__esize)) + \
	    ((__index) * (__psize)))

	for (i = 0; i < queue->limit; i++) {
		entries[i].flags = 0;
		entries[i].queue = queue;
		entries[i].skb = NULL;
		entries[i].entry_idx = i;
		entries[i].priv_data =
		    QUEUE_ENTRY_PRIV_OFFSET(entries, i, queue->limit,
					    sizeof(*entries), queue->priv_size);
	}

#undef QUEUE_ENTRY_PRIV_OFFSET

	queue->entries = entries;

	return 0;
}

static void rt2x00queue_free_skbs(struct data_queue *queue)
{
	unsigned int i;

	if (!queue->entries)
		return;

	for (i = 0; i < queue->limit; i++) {
		rt2x00queue_free_skb(&queue->entries[i]);
	}
}

static int rt2x00queue_alloc_rxskbs(struct data_queue *queue)
{
	unsigned int i;
	struct sk_buff *skb;

	for (i = 0; i < queue->limit; i++) {
		skb = rt2x00queue_alloc_rxskb(&queue->entries[i], GFP_KERNEL);
		if (!skb)
			return -ENOMEM;
		queue->entries[i].skb = skb;
	}

	return 0;
}

int rt2x00queue_initialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	int status;

	status = rt2x00queue_alloc_entries(rt2x00dev->rx);
	if (status)
		goto exit;

	tx_queue_for_each(rt2x00dev, queue) {
		status = rt2x00queue_alloc_entries(queue);
		if (status)
			goto exit;
	}

	status = rt2x00queue_alloc_entries(rt2x00dev->bcn);
	if (status)
		goto exit;

	if (rt2x00_has_cap_flag(rt2x00dev, REQUIRE_ATIM_QUEUE)) {
		status = rt2x00queue_alloc_entries(rt2x00dev->atim);
		if (status)
			goto exit;
	}

	status = rt2x00queue_alloc_rxskbs(rt2x00dev->rx);
	if (status)
		goto exit;

	return 0;

exit:
	rt2x00_err(rt2x00dev, "Queue entries allocation failed\n");

	rt2x00queue_uninitialize(rt2x00dev);

	return status;
}

void rt2x00queue_uninitialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;

	rt2x00queue_free_skbs(rt2x00dev->rx);

	queue_for_each(rt2x00dev, queue) {
		kfree(queue->entries);
		queue->entries = NULL;
	}
}

static void rt2x00queue_init(struct rt2x00_dev *rt2x00dev,
			     struct data_queue *queue, enum data_queue_qid qid)
{
	mutex_init(&queue->status_lock);
	spin_lock_init(&queue->tx_lock);
	spin_lock_init(&queue->index_lock);

	queue->rt2x00dev = rt2x00dev;
	queue->qid = qid;
	queue->txop = 0;
	queue->aifs = 2;
	queue->cw_min = 5;
	queue->cw_max = 10;

	rt2x00dev->ops->queue_init(queue);

	queue->threshold = DIV_ROUND_UP(queue->limit, 10);
}

int rt2x00queue_allocate(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	enum data_queue_qid qid;
	unsigned int req_atim =
	    rt2x00_has_cap_flag(rt2x00dev, REQUIRE_ATIM_QUEUE);

	/*
	 * We need the following queues:
	 * RX: 1
	 * TX: ops->tx_queues
	 * Beacon: 1
	 * Atim: 1 (if required)
	 */
	rt2x00dev->data_queues = 2 + rt2x00dev->ops->tx_queues + req_atim;

	queue = kcalloc(rt2x00dev->data_queues, sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return -ENOMEM;

	/*
	 * Initialize pointers
	 */
	rt2x00dev->rx = queue;
	rt2x00dev->tx = &queue[1];
	rt2x00dev->bcn = &queue[1 + rt2x00dev->ops->tx_queues];
	rt2x00dev->atim = req_atim ? &queue[2 + rt2x00dev->ops->tx_queues] : NULL;

	/*
	 * Initialize queue parameters.
	 * RX: qid = QID_RX
	 * TX: qid = QID_AC_VO + index
	 * TX: cw_min: 2^5 = 32.
	 * TX: cw_max: 2^10 = 1024.
	 * BCN: qid = QID_BEACON
	 * ATIM: qid = QID_ATIM
	 */
	rt2x00queue_init(rt2x00dev, rt2x00dev->rx, QID_RX);

	qid = QID_AC_VO;
	tx_queue_for_each(rt2x00dev, queue)
		rt2x00queue_init(rt2x00dev, queue, qid++);

	rt2x00queue_init(rt2x00dev, rt2x00dev->bcn, QID_BEACON);
	if (req_atim)
		rt2x00queue_init(rt2x00dev, rt2x00dev->atim, QID_ATIM);

	return 0;
}

void rt2x00queue_free(struct rt2x00_dev *rt2x00dev)
{
	kfree(rt2x00dev->rx);
	rt2x00dev->rx = NULL;
	rt2x00dev->tx = NULL;
	rt2x00dev->bcn = NULL;
}
