/*
 * Atheros AR9170 driver
 *
 * mac80211 interaction code
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2009, Christian Lamparter <chunkeey@web.de>
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
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *    Copyright (c) 2007-2008 Atheros Communications, Inc.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>
#include "ar9170.h"
#include "hw.h"
#include "cmd.h"

static int modparam_nohwcrypt;
module_param_named(nohwcrypt, modparam_nohwcrypt, bool, S_IRUGO);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption.");

#define RATE(_bitrate, _hw_rate, _txpidx, _flags) {	\
	.bitrate	= (_bitrate),			\
	.flags		= (_flags),			\
	.hw_value	= (_hw_rate) | (_txpidx) << 4,	\
}

static struct ieee80211_rate __ar9170_ratetable[] = {
	RATE(10, 0, 0, 0),
	RATE(20, 1, 1, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(55, 2, 2, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(110, 3, 3, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(60, 0xb, 0, 0),
	RATE(90, 0xf, 0, 0),
	RATE(120, 0xa, 0, 0),
	RATE(180, 0xe, 0, 0),
	RATE(240, 0x9, 0, 0),
	RATE(360, 0xd, 1, 0),
	RATE(480, 0x8, 2, 0),
	RATE(540, 0xc, 3, 0),
};
#undef RATE

#define ar9170_g_ratetable	(__ar9170_ratetable + 0)
#define ar9170_g_ratetable_size	12
#define ar9170_a_ratetable	(__ar9170_ratetable + 4)
#define ar9170_a_ratetable_size	8

/*
 * NB: The hw_value is used as an index into the ar9170_phy_freq_params
 *     array in phy.c so that we don't have to do frequency lookups!
 */
#define CHAN(_freq, _idx) {		\
	.center_freq	= (_freq),	\
	.hw_value	= (_idx),	\
	.max_power	= 18, /* XXX */	\
}

static struct ieee80211_channel ar9170_2ghz_chantable[] = {
	CHAN(2412,  0),
	CHAN(2417,  1),
	CHAN(2422,  2),
	CHAN(2427,  3),
	CHAN(2432,  4),
	CHAN(2437,  5),
	CHAN(2442,  6),
	CHAN(2447,  7),
	CHAN(2452,  8),
	CHAN(2457,  9),
	CHAN(2462, 10),
	CHAN(2467, 11),
	CHAN(2472, 12),
	CHAN(2484, 13),
};

static struct ieee80211_channel ar9170_5ghz_chantable[] = {
	CHAN(4920, 14),
	CHAN(4940, 15),
	CHAN(4960, 16),
	CHAN(4980, 17),
	CHAN(5040, 18),
	CHAN(5060, 19),
	CHAN(5080, 20),
	CHAN(5180, 21),
	CHAN(5200, 22),
	CHAN(5220, 23),
	CHAN(5240, 24),
	CHAN(5260, 25),
	CHAN(5280, 26),
	CHAN(5300, 27),
	CHAN(5320, 28),
	CHAN(5500, 29),
	CHAN(5520, 30),
	CHAN(5540, 31),
	CHAN(5560, 32),
	CHAN(5580, 33),
	CHAN(5600, 34),
	CHAN(5620, 35),
	CHAN(5640, 36),
	CHAN(5660, 37),
	CHAN(5680, 38),
	CHAN(5700, 39),
	CHAN(5745, 40),
	CHAN(5765, 41),
	CHAN(5785, 42),
	CHAN(5805, 43),
	CHAN(5825, 44),
	CHAN(5170, 45),
	CHAN(5190, 46),
	CHAN(5210, 47),
	CHAN(5230, 48),
};
#undef CHAN

#define AR9170_HT_CAP							\
{									\
	.ht_supported	= true,						\
	.cap		= IEEE80211_HT_CAP_MAX_AMSDU |			\
			  IEEE80211_HT_CAP_SUP_WIDTH_20_40 |		\
			  IEEE80211_HT_CAP_SGI_40 |			\
			  IEEE80211_HT_CAP_DSSSCCK40 |			\
			  IEEE80211_HT_CAP_SM_PS,			\
	.ampdu_factor	= 3,						\
	.ampdu_density	= 6,						\
	.mcs		= {						\
		.rx_mask = { 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, },	\
	},								\
}

static struct ieee80211_supported_band ar9170_band_2GHz = {
	.channels	= ar9170_2ghz_chantable,
	.n_channels	= ARRAY_SIZE(ar9170_2ghz_chantable),
	.bitrates	= ar9170_g_ratetable,
	.n_bitrates	= ar9170_g_ratetable_size,
	.ht_cap		= AR9170_HT_CAP,
};

static struct ieee80211_supported_band ar9170_band_5GHz = {
	.channels	= ar9170_5ghz_chantable,
	.n_channels	= ARRAY_SIZE(ar9170_5ghz_chantable),
	.bitrates	= ar9170_a_ratetable,
	.n_bitrates	= ar9170_a_ratetable_size,
	.ht_cap		= AR9170_HT_CAP,
};

static void ar9170_tx(struct ar9170 *ar);

#ifdef AR9170_QUEUE_DEBUG
static void ar9170_print_txheader(struct ar9170 *ar, struct sk_buff *skb)
{
	struct ar9170_tx_control *txc = (void *) skb->data;
	struct ieee80211_tx_info *txinfo = IEEE80211_SKB_CB(skb);
	struct ar9170_tx_info *arinfo = (void *) txinfo->rate_driver_data;
	struct ieee80211_hdr *hdr = (void *) txc->frame_data;

	printk(KERN_DEBUG "%s: => FRAME [skb:%p, q:%d, DA:[%pM] flags:%x "
			  "mac_ctrl:%04x, phy_ctrl:%08x, timeout:[%d ms]]\n",
	       wiphy_name(ar->hw->wiphy), skb, skb_get_queue_mapping(skb),
	       ieee80211_get_DA(hdr), arinfo->flags,
	       le16_to_cpu(txc->mac_control), le32_to_cpu(txc->phy_control),
	       jiffies_to_msecs(arinfo->timeout - jiffies));
}

static void __ar9170_dump_txqueue(struct ar9170 *ar,
				struct sk_buff_head *queue)
{
	struct sk_buff *skb;
	int i = 0;

	printk(KERN_DEBUG "---[ cut here ]---\n");
	printk(KERN_DEBUG "%s: %d entries in queue.\n",
	       wiphy_name(ar->hw->wiphy), skb_queue_len(queue));

	skb_queue_walk(queue, skb) {
		printk(KERN_DEBUG "index:%d => \n", i++);
		ar9170_print_txheader(ar, skb);
	}
	if (i != skb_queue_len(queue))
		printk(KERN_DEBUG "WARNING: queue frame counter "
		       "mismatch %d != %d\n", skb_queue_len(queue), i);
	printk(KERN_DEBUG "---[ end ]---\n");
}

static void ar9170_dump_txqueue(struct ar9170 *ar,
				struct sk_buff_head *queue)
{
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	__ar9170_dump_txqueue(ar, queue);
	spin_unlock_irqrestore(&queue->lock, flags);
}

static void __ar9170_dump_txstats(struct ar9170 *ar)
{
	int i;

	printk(KERN_DEBUG "%s: QoS queue stats\n",
	       wiphy_name(ar->hw->wiphy));

	for (i = 0; i < __AR9170_NUM_TXQ; i++)
		printk(KERN_DEBUG "%s: queue:%d limit:%d len:%d waitack:%d\n",
		       wiphy_name(ar->hw->wiphy), i, ar->tx_stats[i].limit,
		       ar->tx_stats[i].len, skb_queue_len(&ar->tx_status[i]));
}

static void ar9170_dump_txstats(struct ar9170 *ar)
{
	unsigned long flags;

	spin_lock_irqsave(&ar->tx_stats_lock, flags);
	__ar9170_dump_txstats(ar);
	spin_unlock_irqrestore(&ar->tx_stats_lock, flags);
}
#endif /* AR9170_QUEUE_DEBUG */

/* caller must guarantee exclusive access for _bin_ queue. */
static void ar9170_recycle_expired(struct ar9170 *ar,
				   struct sk_buff_head *queue,
				   struct sk_buff_head *bin)
{
	struct sk_buff *skb, *old = NULL;
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	while ((skb = skb_peek(queue))) {
		struct ieee80211_tx_info *txinfo;
		struct ar9170_tx_info *arinfo;

		txinfo = IEEE80211_SKB_CB(skb);
		arinfo = (void *) txinfo->rate_driver_data;

		if (time_is_before_jiffies(arinfo->timeout)) {
#ifdef AR9170_QUEUE_DEBUG
			printk(KERN_DEBUG "%s: [%ld > %ld] frame expired => "
			       "recycle \n", wiphy_name(ar->hw->wiphy),
			       jiffies, arinfo->timeout);
			ar9170_print_txheader(ar, skb);
#endif /* AR9170_QUEUE_DEBUG */
			__skb_unlink(skb, queue);
			__skb_queue_tail(bin, skb);
		} else {
			break;
		}

		if (unlikely(old == skb)) {
			/* bail out - queue is shot. */

			WARN_ON(1);
			break;
		}
		old = skb;
	}
	spin_unlock_irqrestore(&queue->lock, flags);
}

static void ar9170_tx_status(struct ar9170 *ar, struct sk_buff *skb,
				    u16 tx_status)
{
	struct ieee80211_tx_info *txinfo;
	unsigned int retries = 0;

	txinfo = IEEE80211_SKB_CB(skb);
	ieee80211_tx_info_clear_status(txinfo);

	switch (tx_status) {
	case AR9170_TX_STATUS_RETRY:
		retries = 2;
	case AR9170_TX_STATUS_COMPLETE:
		txinfo->flags |= IEEE80211_TX_STAT_ACK;
		break;

	case AR9170_TX_STATUS_FAILED:
		retries = ar->hw->conf.long_frame_max_tx_count;
		break;

	default:
		printk(KERN_ERR "%s: invalid tx_status response (%x).\n",
		       wiphy_name(ar->hw->wiphy), tx_status);
		break;
	}

	txinfo->status.rates[0].count = retries + 1;
	skb_pull(skb, sizeof(struct ar9170_tx_control));
	ieee80211_tx_status_irqsafe(ar->hw, skb);
}

void ar9170_tx_callback(struct ar9170 *ar, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ar9170_tx_info *arinfo = (void *) info->rate_driver_data;
	unsigned int queue = skb_get_queue_mapping(skb);
	unsigned long flags;

	spin_lock_irqsave(&ar->tx_stats_lock, flags);
	ar->tx_stats[queue].len--;

	if (skb_queue_empty(&ar->tx_pending[queue])) {
#ifdef AR9170_QUEUE_STOP_DEBUG
		printk(KERN_DEBUG "%s: wake queue %d\n",
		       wiphy_name(ar->hw->wiphy), queue);
		__ar9170_dump_txstats(ar);
#endif /* AR9170_QUEUE_STOP_DEBUG */
		ieee80211_wake_queue(ar->hw, queue);
	}
	spin_unlock_irqrestore(&ar->tx_stats_lock, flags);

	if (arinfo->flags & AR9170_TX_FLAG_BLOCK_ACK) {
		dev_kfree_skb_any(skb);
	} else if (arinfo->flags & AR9170_TX_FLAG_WAIT_FOR_ACK) {
		arinfo->timeout = jiffies +
				  msecs_to_jiffies(AR9170_TX_TIMEOUT);

		skb_queue_tail(&ar->tx_status[queue], skb);
	} else if (arinfo->flags & AR9170_TX_FLAG_NO_ACK) {
		ar9170_tx_status(ar, skb, AR9170_TX_STATUS_FAILED);
	} else {
#ifdef AR9170_QUEUE_DEBUG
		printk(KERN_DEBUG "%s: unsupported frame flags!\n",
		       wiphy_name(ar->hw->wiphy));
		ar9170_print_txheader(ar, skb);
#endif /* AR9170_QUEUE_DEBUG */
		dev_kfree_skb_any(skb);
	}

	if (!ar->tx_stats[queue].len &&
	    !skb_queue_empty(&ar->tx_pending[queue])) {
		ar9170_tx(ar);
	}
}

static struct sk_buff *ar9170_get_queued_skb(struct ar9170 *ar,
					     const u8 *mac,
					     struct sk_buff_head *queue,
					     const u32 rate)
{
	unsigned long flags;
	struct sk_buff *skb;

	/*
	 * Unfortunately, the firmware does not tell to which (queued) frame
	 * this transmission status report belongs to.
	 *
	 * So we have to make risky guesses - with the scarce information
	 * the firmware provided (-> destination MAC, and phy_control) -
	 * and hope that we picked the right one...
	 */

	spin_lock_irqsave(&queue->lock, flags);
	skb_queue_walk(queue, skb) {
		struct ar9170_tx_control *txc = (void *) skb->data;
		struct ieee80211_hdr *hdr = (void *) txc->frame_data;
		u32 r;

		if (mac && compare_ether_addr(ieee80211_get_DA(hdr), mac)) {
#ifdef AR9170_QUEUE_DEBUG
			printk(KERN_DEBUG "%s: skip frame => DA %pM != %pM\n",
			       wiphy_name(ar->hw->wiphy), mac,
			       ieee80211_get_DA(hdr));
			ar9170_print_txheader(ar, skb);
#endif /* AR9170_QUEUE_DEBUG */
			continue;
		}

		r = (le32_to_cpu(txc->phy_control) & AR9170_TX_PHY_MCS_MASK) >>
		    AR9170_TX_PHY_MCS_SHIFT;

		if ((rate != AR9170_TX_INVALID_RATE) && (r != rate)) {
#ifdef AR9170_QUEUE_DEBUG
			printk(KERN_DEBUG "%s: skip frame => rate %d != %d\n",
			       wiphy_name(ar->hw->wiphy), rate, r);
			ar9170_print_txheader(ar, skb);
#endif /* AR9170_QUEUE_DEBUG */
			continue;
		}

		__skb_unlink(skb, queue);
		spin_unlock_irqrestore(&queue->lock, flags);
		return skb;
	}

#ifdef AR9170_QUEUE_DEBUG
	printk(KERN_ERR "%s: ESS:[%pM] does not have any "
			"outstanding frames in queue.\n",
			wiphy_name(ar->hw->wiphy), mac);
	__ar9170_dump_txqueue(ar, queue);
#endif /* AR9170_QUEUE_DEBUG */
	spin_unlock_irqrestore(&queue->lock, flags);

	return NULL;
}

/*
 * This worker tries to keeps an maintain tx_status queues.
 * So we can guarantee that incoming tx_status reports are
 * actually for a pending frame.
 */

static void ar9170_tx_janitor(struct work_struct *work)
{
	struct ar9170 *ar = container_of(work, struct ar9170,
					 tx_janitor.work);
	struct sk_buff_head waste;
	unsigned int i;
	bool resched = false;

	if (unlikely(!IS_STARTED(ar)))
		return ;

	skb_queue_head_init(&waste);

	for (i = 0; i < __AR9170_NUM_TXQ; i++) {
#ifdef AR9170_QUEUE_DEBUG
		printk(KERN_DEBUG "%s: garbage collector scans queue:%d\n",
		       wiphy_name(ar->hw->wiphy), i);
		ar9170_dump_txqueue(ar, &ar->tx_pending[i]);
		ar9170_dump_txqueue(ar, &ar->tx_status[i]);
#endif /* AR9170_QUEUE_DEBUG */

		ar9170_recycle_expired(ar, &ar->tx_status[i], &waste);
		ar9170_recycle_expired(ar, &ar->tx_pending[i], &waste);
		skb_queue_purge(&waste);

		if (!skb_queue_empty(&ar->tx_status[i]) ||
		    !skb_queue_empty(&ar->tx_pending[i]))
			resched = true;
	}

	if (resched)
		queue_delayed_work(ar->hw->workqueue,
				   &ar->tx_janitor,
				   msecs_to_jiffies(AR9170_JANITOR_DELAY));
}

void ar9170_handle_command_response(struct ar9170 *ar, void *buf, u32 len)
{
	struct ar9170_cmd_response *cmd = (void *) buf;

	if ((cmd->type & 0xc0) != 0xc0) {
		ar->callback_cmd(ar, len, buf);
		return;
	}

	/* hardware event handlers */
	switch (cmd->type) {
	case 0xc1: {
		/*
		 * TX status notification:
		 * bytes: 0c c1 XX YY M1 M2 M3 M4 M5 M6 R4 R3 R2 R1 S2 S1
		 *
		 * XX always 81
		 * YY always 00
		 * M1-M6 is the MAC address
		 * R1-R4 is the transmit rate
		 * S1-S2 is the transmit status
		 */

		struct sk_buff *skb;
		u32 phy = le32_to_cpu(cmd->tx_status.rate);
		u32 q = (phy & AR9170_TX_PHY_QOS_MASK) >>
			AR9170_TX_PHY_QOS_SHIFT;
#ifdef AR9170_QUEUE_DEBUG
		printk(KERN_DEBUG "%s: recv tx_status for %pM, p:%08x, q:%d\n",
		       wiphy_name(ar->hw->wiphy), cmd->tx_status.dst, phy, q);
#endif /* AR9170_QUEUE_DEBUG */

		skb = ar9170_get_queued_skb(ar, cmd->tx_status.dst,
					    &ar->tx_status[q],
					    AR9170_TX_INVALID_RATE);
		if (unlikely(!skb))
			return ;

		ar9170_tx_status(ar, skb, le16_to_cpu(cmd->tx_status.status));
		break;
		}

	case 0xc0:
		/*
		 * pre-TBTT event
		 */
		if (ar->vif && ar->vif->type == NL80211_IFTYPE_AP)
			queue_work(ar->hw->workqueue, &ar->beacon_work);
		break;

	case 0xc2:
		/*
		 * (IBSS) beacon send notification
		 * bytes: 04 c2 XX YY B4 B3 B2 B1
		 *
		 * XX always 80
		 * YY always 00
		 * B1-B4 "should" be the number of send out beacons.
		 */
		break;

	case 0xc3:
		/* End of Atim Window */
		break;

	case 0xc4:
	case 0xc5:
		/* BlockACK events */
		break;

	case 0xc6:
		/* Watchdog Interrupt */
		break;

	case 0xc9:
		/* retransmission issue / SIFS/EIFS collision ?! */
		break;

	/* firmware debug */
	case 0xca:
		printk(KERN_DEBUG "ar9170 FW: %.*s\n", len - 4, (char *)buf + 4);
		break;
	case 0xcb:
		len -= 4;

		switch (len) {
		case 1:
			printk(KERN_DEBUG "ar9170 FW: u8: %#.2x\n",
				*((char *)buf + 4));
			break;
		case 2:
			printk(KERN_DEBUG "ar9170 FW: u8: %#.4x\n",
				le16_to_cpup((__le16 *)((char *)buf + 4)));
			break;
		case 4:
			printk(KERN_DEBUG "ar9170 FW: u8: %#.8x\n",
				le32_to_cpup((__le32 *)((char *)buf + 4)));
			break;
		case 8:
			printk(KERN_DEBUG "ar9170 FW: u8: %#.16lx\n",
				(unsigned long)le64_to_cpup(
						(__le64 *)((char *)buf + 4)));
			break;
		}
		break;
	case 0xcc:
		print_hex_dump_bytes("ar9170 FW:", DUMP_PREFIX_NONE,
				     (char *)buf + 4, len - 4);
		break;

	default:
		printk(KERN_INFO "received unhandled event %x\n", cmd->type);
		print_hex_dump_bytes("dump:", DUMP_PREFIX_NONE, buf, len);
		break;
	}
}

static void ar9170_rx_reset_rx_mpdu(struct ar9170 *ar)
{
	memset(&ar->rx_mpdu.plcp, 0, sizeof(struct ar9170_rx_head));
	ar->rx_mpdu.has_plcp = false;
}

int ar9170_nag_limiter(struct ar9170 *ar)
{
	bool print_message;

	/*
	 * we expect all sorts of errors in promiscuous mode.
	 * don't bother with it, it's OK!
	 */
	if (ar->sniffer_enabled)
		return false;

	/*
	 * only go for frequent errors! The hardware tends to
	 * do some stupid thing once in a while under load, in
	 * noisy environments or just for fun!
	 */
	if (time_before(jiffies, ar->bad_hw_nagger) && net_ratelimit())
		print_message = true;
	else
		print_message = false;

	/* reset threshold for "once in a while" */
	ar->bad_hw_nagger = jiffies + HZ / 4;
	return print_message;
}

static int ar9170_rx_mac_status(struct ar9170 *ar,
				struct ar9170_rx_head *head,
				struct ar9170_rx_macstatus *mac,
				struct ieee80211_rx_status *status)
{
	u8 error, decrypt;

	BUILD_BUG_ON(sizeof(struct ar9170_rx_head) != 12);
	BUILD_BUG_ON(sizeof(struct ar9170_rx_macstatus) != 4);

	error = mac->error;
	if (error & AR9170_RX_ERROR_MMIC) {
		status->flag |= RX_FLAG_MMIC_ERROR;
		error &= ~AR9170_RX_ERROR_MMIC;
	}

	if (error & AR9170_RX_ERROR_PLCP) {
		status->flag |= RX_FLAG_FAILED_PLCP_CRC;
		error &= ~AR9170_RX_ERROR_PLCP;

		if (!(ar->filter_state & FIF_PLCPFAIL))
			return -EINVAL;
	}

	if (error & AR9170_RX_ERROR_FCS) {
		status->flag |= RX_FLAG_FAILED_FCS_CRC;
		error &= ~AR9170_RX_ERROR_FCS;

		if (!(ar->filter_state & FIF_FCSFAIL))
			return -EINVAL;
	}

	decrypt = ar9170_get_decrypt_type(mac);
	if (!(decrypt & AR9170_RX_ENC_SOFTWARE) &&
	    decrypt != AR9170_ENC_ALG_NONE)
		status->flag |= RX_FLAG_DECRYPTED;

	/* ignore wrong RA errors */
	error &= ~AR9170_RX_ERROR_WRONG_RA;

	if (error & AR9170_RX_ERROR_DECRYPT) {
		error &= ~AR9170_RX_ERROR_DECRYPT;
		/*
		 * Rx decryption is done in place,
		 * the original data is lost anyway.
		 */

		return -EINVAL;
	}

	/* drop any other error frames */
	if (unlikely(error)) {
		/* TODO: update netdevice's RX dropped/errors statistics */

		if (ar9170_nag_limiter(ar))
			printk(KERN_DEBUG "%s: received frame with "
			       "suspicious error code (%#x).\n",
			       wiphy_name(ar->hw->wiphy), error);

		return -EINVAL;
	}

	status->band = ar->channel->band;
	status->freq = ar->channel->center_freq;

	switch (mac->status & AR9170_RX_STATUS_MODULATION_MASK) {
	case AR9170_RX_STATUS_MODULATION_CCK:
		if (mac->status & AR9170_RX_STATUS_SHORT_PREAMBLE)
			status->flag |= RX_FLAG_SHORTPRE;
		switch (head->plcp[0]) {
		case 0x0a:
			status->rate_idx = 0;
			break;
		case 0x14:
			status->rate_idx = 1;
			break;
		case 0x37:
			status->rate_idx = 2;
			break;
		case 0x6e:
			status->rate_idx = 3;
			break;
		default:
			if (ar9170_nag_limiter(ar))
				printk(KERN_ERR "%s: invalid plcp cck rate "
				       "(%x).\n", wiphy_name(ar->hw->wiphy),
				       head->plcp[0]);
			return -EINVAL;
		}
		break;

	case AR9170_RX_STATUS_MODULATION_OFDM:
		switch (head->plcp[0] & 0xf) {
		case 0xb:
			status->rate_idx = 0;
			break;
		case 0xf:
			status->rate_idx = 1;
			break;
		case 0xa:
			status->rate_idx = 2;
			break;
		case 0xe:
			status->rate_idx = 3;
			break;
		case 0x9:
			status->rate_idx = 4;
			break;
		case 0xd:
			status->rate_idx = 5;
			break;
		case 0x8:
			status->rate_idx = 6;
			break;
		case 0xc:
			status->rate_idx = 7;
			break;
		default:
			if (ar9170_nag_limiter(ar))
				printk(KERN_ERR "%s: invalid plcp ofdm rate "
				       "(%x).\n", wiphy_name(ar->hw->wiphy),
				       head->plcp[0]);
			return -EINVAL;
		}
		if (status->band == IEEE80211_BAND_2GHZ)
			status->rate_idx += 4;
		break;

	case AR9170_RX_STATUS_MODULATION_HT:
		if (head->plcp[3] & 0x80)
			status->flag |= RX_FLAG_40MHZ;
		if (head->plcp[6] & 0x80)
			status->flag |= RX_FLAG_SHORT_GI;

		status->rate_idx = clamp(0, 75, head->plcp[6] & 0x7f);
		status->flag |= RX_FLAG_HT;
		break;

	case AR9170_RX_STATUS_MODULATION_DUPOFDM:
		/* XXX */
		if (ar9170_nag_limiter(ar))
			printk(KERN_ERR "%s: invalid modulation\n",
			       wiphy_name(ar->hw->wiphy));
		return -EINVAL;
	}

	return 0;
}

static void ar9170_rx_phy_status(struct ar9170 *ar,
				 struct ar9170_rx_phystatus *phy,
				 struct ieee80211_rx_status *status)
{
	int i;

	BUILD_BUG_ON(sizeof(struct ar9170_rx_phystatus) != 20);

	for (i = 0; i < 3; i++)
		if (phy->rssi[i] != 0x80)
			status->antenna |= BIT(i);

	/* post-process RSSI */
	for (i = 0; i < 7; i++)
		if (phy->rssi[i] & 0x80)
			phy->rssi[i] = ((phy->rssi[i] & 0x7f) + 1) & 0x7f;

	/* TODO: we could do something with phy_errors */
	status->signal = ar->noise[0] + phy->rssi_combined;
	status->noise = ar->noise[0];
}

static struct sk_buff *ar9170_rx_copy_data(u8 *buf, int len)
{
	struct sk_buff *skb;
	int reserved = 0;
	struct ieee80211_hdr *hdr = (void *) buf;

	if (ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		reserved += NET_IP_ALIGN;

		if (*qc & IEEE80211_QOS_CONTROL_A_MSDU_PRESENT)
			reserved += NET_IP_ALIGN;
	}

	if (ieee80211_has_a4(hdr->frame_control))
		reserved += NET_IP_ALIGN;

	reserved = 32 + (reserved & NET_IP_ALIGN);

	skb = dev_alloc_skb(len + reserved);
	if (likely(skb)) {
		skb_reserve(skb, reserved);
		memcpy(skb_put(skb, len), buf, len);
	}

	return skb;
}

/*
 * If the frame alignment is right (or the kernel has
 * CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS), and there
 * is only a single MPDU in the USB frame, then we could
 * submit to mac80211 the SKB directly. However, since
 * there may be multiple packets in one SKB in stream
 * mode, and we need to observe the proper ordering,
 * this is non-trivial.
 */

static void ar9170_handle_mpdu(struct ar9170 *ar, u8 *buf, int len)
{
	struct ar9170_rx_head *head;
	struct ar9170_rx_macstatus *mac;
	struct ar9170_rx_phystatus *phy = NULL;
	struct ieee80211_rx_status status;
	struct sk_buff *skb;
	int mpdu_len;

	if (unlikely(!IS_STARTED(ar) || len < (sizeof(*mac))))
		return ;

	/* Received MPDU */
	mpdu_len = len - sizeof(*mac);

	mac = (void *)(buf + mpdu_len);
	if (unlikely(mac->error & AR9170_RX_ERROR_FATAL)) {
		/* this frame is too damaged and can't be used - drop it */

		return ;
	}

	switch (mac->status & AR9170_RX_STATUS_MPDU_MASK) {
	case AR9170_RX_STATUS_MPDU_FIRST:
		/* first mpdu packet has the plcp header */
		if (likely(mpdu_len >= sizeof(struct ar9170_rx_head))) {
			head = (void *) buf;
			memcpy(&ar->rx_mpdu.plcp, (void *) buf,
			       sizeof(struct ar9170_rx_head));

			mpdu_len -= sizeof(struct ar9170_rx_head);
			buf += sizeof(struct ar9170_rx_head);
			ar->rx_mpdu.has_plcp = true;
		} else {
			if (ar9170_nag_limiter(ar))
				printk(KERN_ERR "%s: plcp info is clipped.\n",
				       wiphy_name(ar->hw->wiphy));
			return ;
		}
		break;

	case AR9170_RX_STATUS_MPDU_LAST:
		/* last mpdu has a extra tail with phy status information */

		if (likely(mpdu_len >= sizeof(struct ar9170_rx_phystatus))) {
			mpdu_len -= sizeof(struct ar9170_rx_phystatus);
			phy = (void *)(buf + mpdu_len);
		} else {
			if (ar9170_nag_limiter(ar))
				printk(KERN_ERR "%s: frame tail is clipped.\n",
				       wiphy_name(ar->hw->wiphy));
			return ;
		}

	case AR9170_RX_STATUS_MPDU_MIDDLE:
		/* middle mpdus are just data */
		if (unlikely(!ar->rx_mpdu.has_plcp)) {
			if (!ar9170_nag_limiter(ar))
				return ;

			printk(KERN_ERR "%s: rx stream did not start "
					"with a first_mpdu frame tag.\n",
			       wiphy_name(ar->hw->wiphy));

			return ;
		}

		head = &ar->rx_mpdu.plcp;
		break;

	case AR9170_RX_STATUS_MPDU_SINGLE:
		/* single mpdu - has plcp (head) and phy status (tail) */
		head = (void *) buf;

		mpdu_len -= sizeof(struct ar9170_rx_head);
		mpdu_len -= sizeof(struct ar9170_rx_phystatus);

		buf += sizeof(struct ar9170_rx_head);
		phy = (void *)(buf + mpdu_len);
		break;

	default:
		BUG_ON(1);
		break;
	}

	if (unlikely(mpdu_len < FCS_LEN))
		return ;

	memset(&status, 0, sizeof(status));
	if (unlikely(ar9170_rx_mac_status(ar, head, mac, &status)))
		return ;

	if (phy)
		ar9170_rx_phy_status(ar, phy, &status);

	skb = ar9170_rx_copy_data(buf, mpdu_len);
	if (likely(skb))
		ieee80211_rx_irqsafe(ar->hw, skb, &status);
}

void ar9170_rx(struct ar9170 *ar, struct sk_buff *skb)
{
	unsigned int i, tlen, resplen, wlen = 0, clen = 0;
	u8 *tbuf, *respbuf;

	tbuf = skb->data;
	tlen = skb->len;

	while (tlen >= 4) {
		clen = tbuf[1] << 8 | tbuf[0];
		wlen = ALIGN(clen, 4);

		/* check if this is stream has a valid tag.*/
		if (tbuf[2] != 0 || tbuf[3] != 0x4e) {
			/*
			 * TODO: handle the highly unlikely event that the
			 * corrupted stream has the TAG at the right position.
			 */

			/* check if the frame can be repaired. */
			if (!ar->rx_failover_missing) {
				/* this is no "short read". */
				if (ar9170_nag_limiter(ar)) {
					printk(KERN_ERR "%s: missing tag!\n",
					       wiphy_name(ar->hw->wiphy));
					goto err_telluser;
				} else
					goto err_silent;
			}

			if (ar->rx_failover_missing > tlen) {
				if (ar9170_nag_limiter(ar)) {
					printk(KERN_ERR "%s: possible multi "
					       "stream corruption!\n",
					       wiphy_name(ar->hw->wiphy));
					goto err_telluser;
				} else
					goto err_silent;
			}

			memcpy(skb_put(ar->rx_failover, tlen), tbuf, tlen);
			ar->rx_failover_missing -= tlen;

			if (ar->rx_failover_missing <= 0) {
				/*
				 * nested ar9170_rx call!
				 * termination is guranteed, even when the
				 * combined frame also have a element with
				 * a bad tag.
				 */

				ar->rx_failover_missing = 0;
				ar9170_rx(ar, ar->rx_failover);

				skb_reset_tail_pointer(ar->rx_failover);
				skb_trim(ar->rx_failover, 0);
			}

			return ;
		}

		/* check if stream is clipped */
		if (wlen > tlen - 4) {
			if (ar->rx_failover_missing) {
				/* TODO: handle double stream corruption. */
				if (ar9170_nag_limiter(ar)) {
					printk(KERN_ERR "%s: double rx stream "
					       "corruption!\n",
						wiphy_name(ar->hw->wiphy));
					goto err_telluser;
				} else
					goto err_silent;
			}

			/*
			 * save incomplete data set.
			 * the firmware will resend the missing bits when
			 * the rx - descriptor comes round again.
			 */

			memcpy(skb_put(ar->rx_failover, tlen), tbuf, tlen);
			ar->rx_failover_missing = clen - tlen;
			return ;
		}
		resplen = clen;
		respbuf = tbuf + 4;
		tbuf += wlen + 4;
		tlen -= wlen + 4;

		i = 0;

		/* weird thing, but this is the same in the original driver */
		while (resplen > 2 && i < 12 &&
		       respbuf[0] == 0xff && respbuf[1] == 0xff) {
			i += 2;
			resplen -= 2;
			respbuf += 2;
		}

		if (resplen < 4)
			continue;

		/* found the 6 * 0xffff marker? */
		if (i == 12)
			ar9170_handle_command_response(ar, respbuf, resplen);
		else
			ar9170_handle_mpdu(ar, respbuf, clen);
	}

	if (tlen) {
		if (net_ratelimit())
			printk(KERN_ERR "%s: %d bytes of unprocessed "
					"data left in rx stream!\n",
			       wiphy_name(ar->hw->wiphy), tlen);

		goto err_telluser;
	}

	return ;

err_telluser:
	printk(KERN_ERR "%s: damaged RX stream data [want:%d, "
			"data:%d, rx:%d, pending:%d ]\n",
	       wiphy_name(ar->hw->wiphy), clen, wlen, tlen,
	       ar->rx_failover_missing);

	if (ar->rx_failover_missing)
		print_hex_dump_bytes("rxbuf:", DUMP_PREFIX_OFFSET,
				     ar->rx_failover->data,
				     ar->rx_failover->len);

	print_hex_dump_bytes("stream:", DUMP_PREFIX_OFFSET,
			     skb->data, skb->len);

	printk(KERN_ERR "%s: please check your hardware and cables, if "
			"you see this message frequently.\n",
	       wiphy_name(ar->hw->wiphy));

err_silent:
	if (ar->rx_failover_missing) {
		skb_reset_tail_pointer(ar->rx_failover);
		skb_trim(ar->rx_failover, 0);
		ar->rx_failover_missing = 0;
	}
}

#define AR9170_FILL_QUEUE(queue, ai_fs, cwmin, cwmax, _txop)		\
do {									\
	queue.aifs = ai_fs;						\
	queue.cw_min = cwmin;						\
	queue.cw_max = cwmax;						\
	queue.txop = _txop;						\
} while (0)

static int ar9170_op_start(struct ieee80211_hw *hw)
{
	struct ar9170 *ar = hw->priv;
	int err, i;

	mutex_lock(&ar->mutex);

	ar->filter_changed = 0;

	/* reinitialize queues statistics */
	memset(&ar->tx_stats, 0, sizeof(ar->tx_stats));
	for (i = 0; i < __AR9170_NUM_TXQ; i++)
		ar->tx_stats[i].limit = AR9170_TXQ_DEPTH;

	/* reset QoS defaults */
	AR9170_FILL_QUEUE(ar->edcf[0], 3, 15, 1023,  0); /* BEST EFFORT*/
	AR9170_FILL_QUEUE(ar->edcf[1], 7, 15, 1023,  0); /* BACKGROUND */
	AR9170_FILL_QUEUE(ar->edcf[2], 2, 7,    15, 94); /* VIDEO */
	AR9170_FILL_QUEUE(ar->edcf[3], 2, 3,     7, 47); /* VOICE */
	AR9170_FILL_QUEUE(ar->edcf[4], 2, 3,     7,  0); /* SPECIAL */

	ar->bad_hw_nagger = jiffies;

	err = ar->open(ar);
	if (err)
		goto out;

	err = ar9170_init_mac(ar);
	if (err)
		goto out;

	err = ar9170_set_qos(ar);
	if (err)
		goto out;

	err = ar9170_init_phy(ar, IEEE80211_BAND_2GHZ);
	if (err)
		goto out;

	err = ar9170_init_rf(ar);
	if (err)
		goto out;

	/* start DMA */
	err = ar9170_write_reg(ar, 0x1c3d30, 0x100);
	if (err)
		goto out;

	ar->state = AR9170_STARTED;

out:
	mutex_unlock(&ar->mutex);
	return err;
}

static void ar9170_op_stop(struct ieee80211_hw *hw)
{
	struct ar9170 *ar = hw->priv;
	unsigned int i;

	if (IS_STARTED(ar))
		ar->state = AR9170_IDLE;

	flush_workqueue(ar->hw->workqueue);

	cancel_delayed_work_sync(&ar->tx_janitor);
	cancel_work_sync(&ar->filter_config_work);
	cancel_work_sync(&ar->beacon_work);
	mutex_lock(&ar->mutex);

	if (IS_ACCEPTING_CMD(ar)) {
		ar9170_set_leds_state(ar, 0);

		/* stop DMA */
		ar9170_write_reg(ar, 0x1c3d30, 0);
		ar->stop(ar);
	}

	for (i = 0; i < __AR9170_NUM_TXQ; i++) {
		skb_queue_purge(&ar->tx_pending[i]);
		skb_queue_purge(&ar->tx_status[i]);
	}
	mutex_unlock(&ar->mutex);
}

static int ar9170_tx_prepare(struct ar9170 *ar, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;
	struct ar9170_tx_control *txc;
	struct ieee80211_tx_info *info;
	struct ieee80211_tx_rate *txrate;
	struct ar9170_tx_info *arinfo;
	unsigned int queue = skb_get_queue_mapping(skb);
	u16 keytype = 0;
	u16 len, icv = 0;

	BUILD_BUG_ON(sizeof(*arinfo) > sizeof(info->rate_driver_data));

	hdr = (void *)skb->data;
	info = IEEE80211_SKB_CB(skb);
	len = skb->len;

	txc = (void *)skb_push(skb, sizeof(*txc));

	if (info->control.hw_key) {
		icv = info->control.hw_key->icv_len;

		switch (info->control.hw_key->alg) {
		case ALG_WEP:
			keytype = AR9170_TX_MAC_ENCR_RC4;
			break;
		case ALG_TKIP:
			keytype = AR9170_TX_MAC_ENCR_RC4;
			break;
		case ALG_CCMP:
			keytype = AR9170_TX_MAC_ENCR_AES;
			break;
		default:
			WARN_ON(1);
			goto err_out;
		}
	}

	/* Length */
	txc->length = cpu_to_le16(len + icv + 4);

	txc->mac_control = cpu_to_le16(AR9170_TX_MAC_HW_DURATION |
				       AR9170_TX_MAC_BACKOFF);
	txc->mac_control |= cpu_to_le16(ar9170_qos_hwmap[queue] <<
					AR9170_TX_MAC_QOS_SHIFT);
	txc->mac_control |= cpu_to_le16(keytype);
	txc->phy_control = cpu_to_le32(0);

	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		txc->mac_control |= cpu_to_le16(AR9170_TX_MAC_NO_ACK);

	txrate = &info->control.rates[0];
	if (txrate->flags & IEEE80211_TX_RC_USE_CTS_PROTECT)
		txc->mac_control |= cpu_to_le16(AR9170_TX_MAC_PROT_CTS);
	else if (txrate->flags & IEEE80211_TX_RC_USE_RTS_CTS)
		txc->mac_control |= cpu_to_le16(AR9170_TX_MAC_PROT_RTS);

	arinfo = (void *)info->rate_driver_data;
	arinfo->timeout = jiffies + msecs_to_jiffies(AR9170_QUEUE_TIMEOUT);

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK) &&
	     (is_valid_ether_addr(ieee80211_get_DA(hdr)))) {
		if (info->flags & IEEE80211_TX_CTL_AMPDU) {
			if (unlikely(!info->control.sta))
				goto err_out;

			txc->mac_control |= cpu_to_le16(AR9170_TX_MAC_AGGR);
			arinfo->flags = AR9170_TX_FLAG_BLOCK_ACK;
			goto out;
		}

		txc->mac_control |= cpu_to_le16(AR9170_TX_MAC_RATE_PROBE);
		/*
		 * WARNING:
		 * Putting the QoS queue bits into an unexplored territory is
		 * certainly not elegant.
		 *
		 * In my defense: This idea provides a reasonable way to
		 * smuggle valuable information to the tx_status callback.
		 * Also, the idea behind this bit-abuse came straight from
		 * the original driver code.
		 */

		txc->phy_control |=
			cpu_to_le32(queue << AR9170_TX_PHY_QOS_SHIFT);
		arinfo->flags = AR9170_TX_FLAG_WAIT_FOR_ACK;
	} else {
		arinfo->flags = AR9170_TX_FLAG_NO_ACK;
	}

out:
	return 0;

err_out:
	skb_pull(skb, sizeof(*txc));
	return -EINVAL;
}

static void ar9170_tx_prepare_phy(struct ar9170 *ar, struct sk_buff *skb)
{
	struct ar9170_tx_control *txc;
	struct ieee80211_tx_info *info;
	struct ieee80211_rate *rate = NULL;
	struct ieee80211_tx_rate *txrate;
	u32 power, chains;

	txc = (void *) skb->data;
	info = IEEE80211_SKB_CB(skb);
	txrate = &info->control.rates[0];

	if (txrate->flags & IEEE80211_TX_RC_GREEN_FIELD)
		txc->phy_control |= cpu_to_le32(AR9170_TX_PHY_GREENFIELD);

	if (txrate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
		txc->phy_control |= cpu_to_le32(AR9170_TX_PHY_SHORT_PREAMBLE);

	if (txrate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		txc->phy_control |= cpu_to_le32(AR9170_TX_PHY_BW_40MHZ);
	/* this works because 40 MHz is 2 and dup is 3 */
	if (txrate->flags & IEEE80211_TX_RC_DUP_DATA)
		txc->phy_control |= cpu_to_le32(AR9170_TX_PHY_BW_40MHZ_DUP);

	if (txrate->flags & IEEE80211_TX_RC_SHORT_GI)
		txc->phy_control |= cpu_to_le32(AR9170_TX_PHY_SHORT_GI);

	if (txrate->flags & IEEE80211_TX_RC_MCS) {
		u32 r = txrate->idx;
		u8 *txpower;

		/* heavy clip control */
		txc->phy_control |= cpu_to_le32((r & 0x7) << 7);

		r <<= AR9170_TX_PHY_MCS_SHIFT;
		BUG_ON(r & ~AR9170_TX_PHY_MCS_MASK);

		txc->phy_control |= cpu_to_le32(r & AR9170_TX_PHY_MCS_MASK);
		txc->phy_control |= cpu_to_le32(AR9170_TX_PHY_MOD_HT);

		if (txrate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH) {
			if (info->band == IEEE80211_BAND_5GHZ)
				txpower = ar->power_5G_ht40;
			else
				txpower = ar->power_2G_ht40;
		} else {
			if (info->band == IEEE80211_BAND_5GHZ)
				txpower = ar->power_5G_ht20;
			else
				txpower = ar->power_2G_ht20;
		}

		power = txpower[(txrate->idx) & 7];
	} else {
		u8 *txpower;
		u32 mod;
		u32 phyrate;
		u8 idx = txrate->idx;

		if (info->band != IEEE80211_BAND_2GHZ) {
			idx += 4;
			txpower = ar->power_5G_leg;
			mod = AR9170_TX_PHY_MOD_OFDM;
		} else {
			if (idx < 4) {
				txpower = ar->power_2G_cck;
				mod = AR9170_TX_PHY_MOD_CCK;
			} else {
				mod = AR9170_TX_PHY_MOD_OFDM;
				txpower = ar->power_2G_ofdm;
			}
		}

		rate = &__ar9170_ratetable[idx];

		phyrate = rate->hw_value & 0xF;
		power = txpower[(rate->hw_value & 0x30) >> 4];
		phyrate <<= AR9170_TX_PHY_MCS_SHIFT;

		txc->phy_control |= cpu_to_le32(mod);
		txc->phy_control |= cpu_to_le32(phyrate);
	}

	power <<= AR9170_TX_PHY_TX_PWR_SHIFT;
	power &= AR9170_TX_PHY_TX_PWR_MASK;
	txc->phy_control |= cpu_to_le32(power);

	/* set TX chains */
	if (ar->eeprom.tx_mask == 1) {
		chains = AR9170_TX_PHY_TXCHAIN_1;
	} else {
		chains = AR9170_TX_PHY_TXCHAIN_2;

		/* >= 36M legacy OFDM - use only one chain */
		if (rate && rate->bitrate >= 360)
			chains = AR9170_TX_PHY_TXCHAIN_1;
	}
	txc->phy_control |= cpu_to_le32(chains << AR9170_TX_PHY_TXCHAIN_SHIFT);
}

static void ar9170_tx(struct ar9170 *ar)
{
	struct sk_buff *skb;
	unsigned long flags;
	struct ieee80211_tx_info *info;
	struct ar9170_tx_info *arinfo;
	unsigned int i, frames, frames_failed, remaining_space;
	int err;
	bool schedule_garbagecollector = false;

	BUILD_BUG_ON(sizeof(*arinfo) > sizeof(info->rate_driver_data));

	if (unlikely(!IS_STARTED(ar)))
		return ;

	remaining_space = AR9170_TX_MAX_PENDING;

	for (i = 0; i < __AR9170_NUM_TXQ; i++) {
		spin_lock_irqsave(&ar->tx_stats_lock, flags);
		if (ar->tx_stats[i].len >= ar->tx_stats[i].limit) {
#ifdef AR9170_QUEUE_DEBUG
			printk(KERN_DEBUG "%s: queue %d full\n",
			       wiphy_name(ar->hw->wiphy), i);

			__ar9170_dump_txstats(ar);
			printk(KERN_DEBUG "stuck frames: ===> \n");
			ar9170_dump_txqueue(ar, &ar->tx_pending[i]);
			ar9170_dump_txqueue(ar, &ar->tx_status[i]);
#endif /* AR9170_QUEUE_DEBUG */
			ieee80211_stop_queue(ar->hw, i);
			spin_unlock_irqrestore(&ar->tx_stats_lock, flags);
			continue;
		}

		frames = min(ar->tx_stats[i].limit - ar->tx_stats[i].len,
			     skb_queue_len(&ar->tx_pending[i]));

		if (remaining_space < frames) {
#ifdef AR9170_QUEUE_DEBUG
			printk(KERN_DEBUG "%s: tx quota reached queue:%d, "
			       "remaining slots:%d, needed:%d\n",
			       wiphy_name(ar->hw->wiphy), i, remaining_space,
			       frames);

			ar9170_dump_txstats(ar);
#endif /* AR9170_QUEUE_DEBUG */
			frames = remaining_space;
		}

		ar->tx_stats[i].len += frames;
		ar->tx_stats[i].count += frames;
		spin_unlock_irqrestore(&ar->tx_stats_lock, flags);

		if (!frames)
			continue;

		frames_failed = 0;
		while (frames) {
			skb = skb_dequeue(&ar->tx_pending[i]);
			if (unlikely(!skb)) {
				frames_failed += frames;
				frames = 0;
				break;
			}

			info = IEEE80211_SKB_CB(skb);
			arinfo = (void *) info->rate_driver_data;

			/* TODO: cancel stuck frames */
			arinfo->timeout = jiffies +
					  msecs_to_jiffies(AR9170_TX_TIMEOUT);

#ifdef AR9170_QUEUE_DEBUG
			printk(KERN_DEBUG "%s: send frame q:%d =>\n",
			       wiphy_name(ar->hw->wiphy), i);
			ar9170_print_txheader(ar, skb);
#endif /* AR9170_QUEUE_DEBUG */

			err = ar->tx(ar, skb);
			if (unlikely(err)) {
				frames_failed++;
				dev_kfree_skb_any(skb);
			} else {
				remaining_space--;
				schedule_garbagecollector = true;
			}

			frames--;
		}

#ifdef AR9170_QUEUE_DEBUG
		printk(KERN_DEBUG "%s: ar9170_tx report for queue %d\n",
		       wiphy_name(ar->hw->wiphy), i);

		printk(KERN_DEBUG "%s: unprocessed pending frames left:\n",
		       wiphy_name(ar->hw->wiphy));
		ar9170_dump_txqueue(ar, &ar->tx_pending[i]);
#endif /* AR9170_QUEUE_DEBUG */

		if (unlikely(frames_failed)) {
#ifdef AR9170_QUEUE_DEBUG
			printk(KERN_DEBUG "%s: frames failed =>\n",
			       wiphy_name(ar->hw->wiphy), frames_failed);
#endif /* AR9170_QUEUE_DEBUG */

			spin_lock_irqsave(&ar->tx_stats_lock, flags);
			ar->tx_stats[i].len -= frames_failed;
			ar->tx_stats[i].count -= frames_failed;
			ieee80211_wake_queue(ar->hw, i);
			spin_unlock_irqrestore(&ar->tx_stats_lock, flags);
		}
	}

	if (schedule_garbagecollector)
		queue_delayed_work(ar->hw->workqueue,
				   &ar->tx_janitor,
				   msecs_to_jiffies(AR9170_JANITOR_DELAY));
}

int ar9170_op_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ar9170 *ar = hw->priv;
	struct ieee80211_tx_info *info;

	if (unlikely(!IS_STARTED(ar)))
		goto err_free;

	if (unlikely(ar9170_tx_prepare(ar, skb)))
		goto err_free;

	info = IEEE80211_SKB_CB(skb);
	if (info->flags & IEEE80211_TX_CTL_AMPDU) {
		/* drop frame, we do not allow TX A-MPDU aggregation yet. */
		goto err_free;
	} else {
		unsigned int queue = skb_get_queue_mapping(skb);

		ar9170_tx_prepare_phy(ar, skb);
		skb_queue_tail(&ar->tx_pending[queue], skb);
	}

	ar9170_tx(ar);
	return NETDEV_TX_OK;

err_free:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static int ar9170_op_add_interface(struct ieee80211_hw *hw,
				   struct ieee80211_if_init_conf *conf)
{
	struct ar9170 *ar = hw->priv;
	int err = 0;

	mutex_lock(&ar->mutex);

	if (ar->vif) {
		err = -EBUSY;
		goto unlock;
	}

	ar->vif = conf->vif;
	memcpy(ar->mac_addr, conf->mac_addr, ETH_ALEN);

	if (modparam_nohwcrypt || (ar->vif->type != NL80211_IFTYPE_STATION)) {
		ar->rx_software_decryption = true;
		ar->disable_offload = true;
	}

	ar->cur_filter = 0;
	ar->want_filter = AR9170_MAC_REG_FTF_DEFAULTS;
	err = ar9170_update_frame_filter(ar);
	if (err)
		goto unlock;

	err = ar9170_set_operating_mode(ar);

unlock:
	mutex_unlock(&ar->mutex);
	return err;
}

static void ar9170_op_remove_interface(struct ieee80211_hw *hw,
				       struct ieee80211_if_init_conf *conf)
{
	struct ar9170 *ar = hw->priv;

	mutex_lock(&ar->mutex);
	ar->vif = NULL;
	ar->want_filter = 0;
	ar9170_update_frame_filter(ar);
	ar9170_set_beacon_timers(ar);
	dev_kfree_skb(ar->beacon);
	ar->beacon = NULL;
	ar->sniffer_enabled = false;
	ar->rx_software_decryption = false;
	ar9170_set_operating_mode(ar);
	mutex_unlock(&ar->mutex);
}

static int ar9170_op_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ar9170 *ar = hw->priv;
	int err = 0;

	mutex_lock(&ar->mutex);

	if (changed & IEEE80211_CONF_CHANGE_LISTEN_INTERVAL) {
		/* TODO */
		err = 0;
	}

	if (changed & IEEE80211_CONF_CHANGE_PS) {
		/* TODO */
		err = 0;
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		/* TODO */
		err = 0;
	}

	if (changed & IEEE80211_CONF_CHANGE_RETRY_LIMITS) {
		/*
		 * is it long_frame_max_tx_count or short_frame_max_tx_count?
		 */

		err = ar9170_set_hwretry_limit(ar,
			ar->hw->conf.long_frame_max_tx_count);
		if (err)
			goto out;
	}

	if (changed & BSS_CHANGED_BEACON_INT) {
		err = ar9170_set_beacon_timers(ar);
		if (err)
			goto out;
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {

		/* adjust slot time for 5 GHz */
		err = ar9170_set_slot_time(ar);
		if (err)
			goto out;

		err = ar9170_set_dyn_sifs_ack(ar);
		if (err)
			goto out;

		err = ar9170_set_channel(ar, hw->conf.channel,
				AR9170_RFI_NONE,
				nl80211_to_ar9170(hw->conf.channel_type));
		if (err)
			goto out;
	}

out:
	mutex_unlock(&ar->mutex);
	return err;
}

static void ar9170_set_filters(struct work_struct *work)
{
	struct ar9170 *ar = container_of(work, struct ar9170,
					 filter_config_work);
	int err;

	if (unlikely(!IS_STARTED(ar)))
		return ;

	mutex_lock(&ar->mutex);
	if (test_and_clear_bit(AR9170_FILTER_CHANGED_MODE,
			       &ar->filter_changed)) {
		err = ar9170_set_operating_mode(ar);
		if (err)
			goto unlock;
	}

	if (test_and_clear_bit(AR9170_FILTER_CHANGED_MULTICAST,
			       &ar->filter_changed)) {
		err = ar9170_update_multicast(ar);
		if (err)
			goto unlock;
	}

	if (test_and_clear_bit(AR9170_FILTER_CHANGED_FRAMEFILTER,
			       &ar->filter_changed)) {
		err = ar9170_update_frame_filter(ar);
		if (err)
			goto unlock;
	}

unlock:
	mutex_unlock(&ar->mutex);
}

static void ar9170_op_configure_filter(struct ieee80211_hw *hw,
				       unsigned int changed_flags,
				       unsigned int *new_flags,
				       int mc_count, struct dev_mc_list *mclist)
{
	struct ar9170 *ar = hw->priv;

	/* mask supported flags */
	*new_flags &= FIF_ALLMULTI | FIF_CONTROL | FIF_BCN_PRBRESP_PROMISC |
		      FIF_PROMISC_IN_BSS | FIF_FCSFAIL | FIF_PLCPFAIL;
	ar->filter_state = *new_flags;
	/*
	 * We can support more by setting the sniffer bit and
	 * then checking the error flags, later.
	 */

	if (changed_flags & FIF_ALLMULTI) {
		if (*new_flags & FIF_ALLMULTI) {
			ar->want_mc_hash = ~0ULL;
		} else {
			u64 mchash;
			int i;

			/* always get broadcast frames */
			mchash = 1ULL << (0xff >> 2);

			for (i = 0; i < mc_count; i++) {
				if (WARN_ON(!mclist))
					break;
				mchash |= 1ULL << (mclist->dmi_addr[5] >> 2);
				mclist = mclist->next;
			}
		ar->want_mc_hash = mchash;
		}
		set_bit(AR9170_FILTER_CHANGED_MULTICAST, &ar->filter_changed);
	}

	if (changed_flags & FIF_CONTROL) {
		u32 filter = AR9170_MAC_REG_FTF_PSPOLL |
			     AR9170_MAC_REG_FTF_RTS |
			     AR9170_MAC_REG_FTF_CTS |
			     AR9170_MAC_REG_FTF_ACK |
			     AR9170_MAC_REG_FTF_CFE |
			     AR9170_MAC_REG_FTF_CFE_ACK;

		if (*new_flags & FIF_CONTROL)
			ar->want_filter = ar->cur_filter | filter;
		else
			ar->want_filter = ar->cur_filter & ~filter;

		set_bit(AR9170_FILTER_CHANGED_FRAMEFILTER,
			&ar->filter_changed);
	}

	if (changed_flags & FIF_PROMISC_IN_BSS) {
		ar->sniffer_enabled = ((*new_flags) & FIF_PROMISC_IN_BSS) != 0;
		set_bit(AR9170_FILTER_CHANGED_MODE,
			&ar->filter_changed);
	}

	if (likely(IS_STARTED(ar)))
		queue_work(ar->hw->workqueue, &ar->filter_config_work);
}

static void ar9170_op_bss_info_changed(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u32 changed)
{
	struct ar9170 *ar = hw->priv;
	int err = 0;

	mutex_lock(&ar->mutex);

	if (changed & BSS_CHANGED_BSSID) {
		memcpy(ar->bssid, bss_conf->bssid, ETH_ALEN);
		err = ar9170_set_operating_mode(ar);
		if (err)
			goto out;
	}

	if (changed & (BSS_CHANGED_BEACON | BSS_CHANGED_BEACON_ENABLED)) {
		err = ar9170_update_beacon(ar);
		if (err)
			goto out;

		err = ar9170_set_beacon_timers(ar);
		if (err)
			goto out;
	}

	if (changed & BSS_CHANGED_ASSOC) {
#ifndef CONFIG_AR9170_LEDS
		/* enable assoc LED. */
		err = ar9170_set_leds_state(ar, bss_conf->assoc ? 2 : 0);
#endif /* CONFIG_AR9170_LEDS */
	}

	if (changed & BSS_CHANGED_BEACON_INT) {
		err = ar9170_set_beacon_timers(ar);
		if (err)
			goto out;
	}

	if (changed & BSS_CHANGED_HT) {
		/* TODO */
		err = 0;
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		err = ar9170_set_slot_time(ar);
		if (err)
			goto out;
	}

	if (changed & BSS_CHANGED_BASIC_RATES) {
		err = ar9170_set_basic_rates(ar);
		if (err)
			goto out;
	}

out:
	mutex_unlock(&ar->mutex);
}

static u64 ar9170_op_get_tsf(struct ieee80211_hw *hw)
{
	struct ar9170 *ar = hw->priv;
	int err;
	u32 tsf_low;
	u32 tsf_high;
	u64 tsf;

	mutex_lock(&ar->mutex);
	err = ar9170_read_reg(ar, AR9170_MAC_REG_TSF_L, &tsf_low);
	if (!err)
		err = ar9170_read_reg(ar, AR9170_MAC_REG_TSF_H, &tsf_high);
	mutex_unlock(&ar->mutex);

	if (WARN_ON(err))
		return 0;

	tsf = tsf_high;
	tsf = (tsf << 32) | tsf_low;
	return tsf;
}

static int ar9170_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct ar9170 *ar = hw->priv;
	int err = 0, i;
	u8 ktype;

	if ((!ar->vif) || (ar->disable_offload))
		return -EOPNOTSUPP;

	switch (key->alg) {
	case ALG_WEP:
		if (key->keylen == WLAN_KEY_LEN_WEP40)
			ktype = AR9170_ENC_ALG_WEP64;
		else
			ktype = AR9170_ENC_ALG_WEP128;
		break;
	case ALG_TKIP:
		ktype = AR9170_ENC_ALG_TKIP;
		break;
	case ALG_CCMP:
		ktype = AR9170_ENC_ALG_AESCCMP;
		break;
	default:
		return -EOPNOTSUPP;
	}

	mutex_lock(&ar->mutex);
	if (cmd == SET_KEY) {
		if (unlikely(!IS_STARTED(ar))) {
			err = -EOPNOTSUPP;
			goto out;
		}

		/* group keys need all-zeroes address */
		if (!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
			sta = NULL;

		if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
			for (i = 0; i < 64; i++)
				if (!(ar->usedkeys & BIT(i)))
					break;
			if (i == 64) {
				ar->rx_software_decryption = true;
				ar9170_set_operating_mode(ar);
				err = -ENOSPC;
				goto out;
			}
		} else {
			i = 64 + key->keyidx;
		}

		key->hw_key_idx = i;

		err = ar9170_upload_key(ar, i, sta ? sta->addr : NULL, ktype, 0,
					key->key, min_t(u8, 16, key->keylen));
		if (err)
			goto out;

		if (key->alg == ALG_TKIP) {
			err = ar9170_upload_key(ar, i, sta ? sta->addr : NULL,
						ktype, 1, key->key + 16, 16);
			if (err)
				goto out;

			/*
			 * hardware is not capable generating the MMIC
			 * for fragmented frames!
			 */
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		}

		if (i < 64)
			ar->usedkeys |= BIT(i);

		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
	} else {
		if (unlikely(!IS_STARTED(ar))) {
			/* The device is gone... together with the key ;-) */
			err = 0;
			goto out;
		}

		err = ar9170_disable_key(ar, key->hw_key_idx);
		if (err)
			goto out;

		if (key->hw_key_idx < 64) {
			ar->usedkeys &= ~BIT(key->hw_key_idx);
		} else {
			err = ar9170_upload_key(ar, key->hw_key_idx, NULL,
						AR9170_ENC_ALG_NONE, 0,
						NULL, 0);
			if (err)
				goto out;

			if (key->alg == ALG_TKIP) {
				err = ar9170_upload_key(ar, key->hw_key_idx,
							NULL,
							AR9170_ENC_ALG_NONE, 1,
							NULL, 0);
				if (err)
					goto out;
			}

		}
	}

	ar9170_regwrite_begin(ar);
	ar9170_regwrite(AR9170_MAC_REG_ROLL_CALL_TBL_L, ar->usedkeys);
	ar9170_regwrite(AR9170_MAC_REG_ROLL_CALL_TBL_H, ar->usedkeys >> 32);
	ar9170_regwrite_finish();
	err = ar9170_regwrite_result();

out:
	mutex_unlock(&ar->mutex);

	return err;
}

static void ar9170_sta_notify(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      enum sta_notify_cmd cmd,
			      struct ieee80211_sta *sta)
{
}

static int ar9170_get_stats(struct ieee80211_hw *hw,
			    struct ieee80211_low_level_stats *stats)
{
	struct ar9170 *ar = hw->priv;
	u32 val;
	int err;

	mutex_lock(&ar->mutex);
	err = ar9170_read_reg(ar, AR9170_MAC_REG_TX_RETRY, &val);
	ar->stats.dot11ACKFailureCount += val;

	memcpy(stats, &ar->stats, sizeof(*stats));
	mutex_unlock(&ar->mutex);

	return 0;
}

static int ar9170_get_tx_stats(struct ieee80211_hw *hw,
			       struct ieee80211_tx_queue_stats *tx_stats)
{
	struct ar9170 *ar = hw->priv;

	spin_lock_bh(&ar->tx_stats_lock);
	memcpy(tx_stats, ar->tx_stats, sizeof(tx_stats[0]) * hw->queues);
	spin_unlock_bh(&ar->tx_stats_lock);

	return 0;
}

static int ar9170_conf_tx(struct ieee80211_hw *hw, u16 queue,
			  const struct ieee80211_tx_queue_params *param)
{
	struct ar9170 *ar = hw->priv;
	int ret;

	mutex_lock(&ar->mutex);
	if ((param) && !(queue > __AR9170_NUM_TXQ)) {
		memcpy(&ar->edcf[ar9170_qos_hwmap[queue]],
		       param, sizeof(*param));

		ret = ar9170_set_qos(ar);
	} else
		ret = -EINVAL;

	mutex_unlock(&ar->mutex);
	return ret;
}

static int ar9170_ampdu_action(struct ieee80211_hw *hw,
			       enum ieee80211_ampdu_mlme_action action,
			       struct ieee80211_sta *sta, u16 tid, u16 *ssn)
{
	switch (action) {
	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
		/*
		 * Something goes wrong -- RX locks up
		 * after a while of receiving aggregated
		 * frames -- not enabling for now.
		 */
		return -EOPNOTSUPP;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct ieee80211_ops ar9170_ops = {
	.start			= ar9170_op_start,
	.stop			= ar9170_op_stop,
	.tx			= ar9170_op_tx,
	.add_interface		= ar9170_op_add_interface,
	.remove_interface	= ar9170_op_remove_interface,
	.config			= ar9170_op_config,
	.configure_filter	= ar9170_op_configure_filter,
	.conf_tx		= ar9170_conf_tx,
	.bss_info_changed	= ar9170_op_bss_info_changed,
	.get_tsf		= ar9170_op_get_tsf,
	.set_key		= ar9170_set_key,
	.sta_notify		= ar9170_sta_notify,
	.get_stats		= ar9170_get_stats,
	.get_tx_stats		= ar9170_get_tx_stats,
	.ampdu_action		= ar9170_ampdu_action,
};

void *ar9170_alloc(size_t priv_size)
{
	struct ieee80211_hw *hw;
	struct ar9170 *ar;
	struct sk_buff *skb;
	int i;

	/*
	 * this buffer is used for rx stream reconstruction.
	 * Under heavy load this device (or the transport layer?)
	 * tends to split the streams into seperate rx descriptors.
	 */

	skb = __dev_alloc_skb(AR9170_MAX_RX_BUFFER_SIZE, GFP_KERNEL);
	if (!skb)
		goto err_nomem;

	hw = ieee80211_alloc_hw(priv_size, &ar9170_ops);
	if (!hw)
		goto err_nomem;

	ar = hw->priv;
	ar->hw = hw;
	ar->rx_failover = skb;

	mutex_init(&ar->mutex);
	spin_lock_init(&ar->cmdlock);
	spin_lock_init(&ar->tx_stats_lock);
	for (i = 0; i < __AR9170_NUM_TXQ; i++) {
		skb_queue_head_init(&ar->tx_status[i]);
		skb_queue_head_init(&ar->tx_pending[i]);
	}
	ar9170_rx_reset_rx_mpdu(ar);
	INIT_WORK(&ar->filter_config_work, ar9170_set_filters);
	INIT_WORK(&ar->beacon_work, ar9170_new_beacon);
	INIT_DELAYED_WORK(&ar->tx_janitor, ar9170_tx_janitor);

	/* all hw supports 2.4 GHz, so set channel to 1 by default */
	ar->channel = &ar9170_2ghz_chantable[0];

	/* first part of wiphy init */
	ar->hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
					 BIT(NL80211_IFTYPE_WDS) |
					 BIT(NL80211_IFTYPE_ADHOC);
	ar->hw->flags |= IEEE80211_HW_RX_INCLUDES_FCS |
			 IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING |
			 IEEE80211_HW_SIGNAL_DBM |
			 IEEE80211_HW_NOISE_DBM;

	ar->hw->queues = __AR9170_NUM_TXQ;
	ar->hw->extra_tx_headroom = 8;
	ar->hw->sta_data_size = sizeof(struct ar9170_sta_info);

	ar->hw->max_rates = 1;
	ar->hw->max_rate_tries = 3;

	for (i = 0; i < ARRAY_SIZE(ar->noise); i++)
		ar->noise[i] = -95; /* ATH_DEFAULT_NOISE_FLOOR */

	return ar;

err_nomem:
	kfree_skb(skb);
	return ERR_PTR(-ENOMEM);
}

static int ar9170_read_eeprom(struct ar9170 *ar)
{
#define RW	8	/* number of words to read at once */
#define RB	(sizeof(u32) * RW)
	DECLARE_MAC_BUF(mbuf);
	u8 *eeprom = (void *)&ar->eeprom;
	u8 *addr = ar->eeprom.mac_address;
	__le32 offsets[RW];
	int i, j, err, bands = 0;

	BUILD_BUG_ON(sizeof(ar->eeprom) & 3);

	BUILD_BUG_ON(RB > AR9170_MAX_CMD_LEN - 4);
#ifndef __CHECKER__
	/* don't want to handle trailing remains */
	BUILD_BUG_ON(sizeof(ar->eeprom) % RB);
#endif

	for (i = 0; i < sizeof(ar->eeprom)/RB; i++) {
		for (j = 0; j < RW; j++)
			offsets[j] = cpu_to_le32(AR9170_EEPROM_START +
						 RB * i + 4 * j);

		err = ar->exec_cmd(ar, AR9170_CMD_RREG,
				   RB, (u8 *) &offsets,
				   RB, eeprom + RB * i);
		if (err)
			return err;
	}

#undef RW
#undef RB

	if (ar->eeprom.length == cpu_to_le16(0xFFFF))
		return -ENODATA;

	if (ar->eeprom.operating_flags & AR9170_OPFLAG_2GHZ) {
		ar->hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &ar9170_band_2GHz;
		bands++;
	}
	if (ar->eeprom.operating_flags & AR9170_OPFLAG_5GHZ) {
		ar->hw->wiphy->bands[IEEE80211_BAND_5GHZ] = &ar9170_band_5GHz;
		bands++;
	}
	/*
	 * I measured this, a bandswitch takes roughly
	 * 135 ms and a frequency switch about 80.
	 *
	 * FIXME: measure these values again once EEPROM settings
	 *	  are used, that will influence them!
	 */
	if (bands == 2)
		ar->hw->channel_change_time = 135 * 1000;
	else
		ar->hw->channel_change_time = 80 * 1000;

	ar->regulatory.current_rd = le16_to_cpu(ar->eeprom.reg_domain[0]);
	ar->regulatory.current_rd_ext = le16_to_cpu(ar->eeprom.reg_domain[1]);

	/* second part of wiphy init */
	SET_IEEE80211_PERM_ADDR(ar->hw, addr);

	return bands ? 0 : -EINVAL;
}

static int ar9170_reg_notifier(struct wiphy *wiphy,
			struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ar9170 *ar = hw->priv;

	return ath_reg_notifier_apply(wiphy, request, &ar->regulatory);
}

int ar9170_register(struct ar9170 *ar, struct device *pdev)
{
	int err;

	/* try to read EEPROM, init MAC addr */
	err = ar9170_read_eeprom(ar);
	if (err)
		goto err_out;

	err = ath_regd_init(&ar->regulatory, ar->hw->wiphy,
			    ar9170_reg_notifier);
	if (err)
		goto err_out;

	err = ieee80211_register_hw(ar->hw);
	if (err)
		goto err_out;

	if (!ath_is_world_regd(&ar->regulatory))
		regulatory_hint(ar->hw->wiphy, ar->regulatory.alpha2);

	err = ar9170_init_leds(ar);
	if (err)
		goto err_unreg;

#ifdef CONFIG_AR9170_LEDS
	err = ar9170_register_leds(ar);
	if (err)
		goto err_unreg;
#endif /* CONFIG_AR9170_LEDS */

	dev_info(pdev, "Atheros AR9170 is registered as '%s'\n",
		 wiphy_name(ar->hw->wiphy));

	return err;

err_unreg:
	ieee80211_unregister_hw(ar->hw);

err_out:
	return err;
}

void ar9170_unregister(struct ar9170 *ar)
{
#ifdef CONFIG_AR9170_LEDS
	ar9170_unregister_leds(ar);
#endif /* CONFIG_AR9170_LEDS */

	kfree_skb(ar->rx_failover);
	ieee80211_unregister_hw(ar->hw);
	mutex_destroy(&ar->mutex);
}
