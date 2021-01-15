/*
 * Atheros CARL9170 driver
 *
 * 802.11 xmit & status routines
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2009, 2010, Christian Lamparter <chunkeey@googlemail.com>
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

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>
#include "carl9170.h"
#include "hw.h"
#include "cmd.h"

static inline unsigned int __carl9170_get_queue(struct ar9170 *ar,
						unsigned int queue)
{
	if (unlikely(modparam_noht)) {
		return queue;
	} else {
		/*
		 * This is just another workaround, until
		 * someone figures out how to get QoS and
		 * AMPDU to play nicely together.
		 */

		return 2;		/* AC_BE */
	}
}

static inline unsigned int carl9170_get_queue(struct ar9170 *ar,
					      struct sk_buff *skb)
{
	return __carl9170_get_queue(ar, skb_get_queue_mapping(skb));
}

static bool is_mem_full(struct ar9170 *ar)
{
	return (DIV_ROUND_UP(IEEE80211_MAX_FRAME_LEN, ar->fw.mem_block_size) >
		atomic_read(&ar->mem_free_blocks));
}

static void carl9170_tx_accounting(struct ar9170 *ar, struct sk_buff *skb)
{
	int queue, i;
	bool mem_full;

	atomic_inc(&ar->tx_total_queued);

	queue = skb_get_queue_mapping(skb);
	spin_lock_bh(&ar->tx_stats_lock);

	/*
	 * The driver has to accept the frame, regardless if the queue is
	 * full to the brim, or not. We have to do the queuing internally,
	 * since mac80211 assumes that a driver which can operate with
	 * aggregated frames does not reject frames for this reason.
	 */
	ar->tx_stats[queue].len++;
	ar->tx_stats[queue].count++;

	mem_full = is_mem_full(ar);
	for (i = 0; i < ar->hw->queues; i++) {
		if (mem_full || ar->tx_stats[i].len >= ar->tx_stats[i].limit) {
			ieee80211_stop_queue(ar->hw, i);
			ar->queue_stop_timeout[i] = jiffies;
		}
	}

	spin_unlock_bh(&ar->tx_stats_lock);
}

/* needs rcu_read_lock */
static struct ieee80211_sta *__carl9170_get_tx_sta(struct ar9170 *ar,
						   struct sk_buff *skb)
{
	struct _carl9170_tx_superframe *super = (void *) skb->data;
	struct ieee80211_hdr *hdr = (void *) super->frame_data;
	struct ieee80211_vif *vif;
	unsigned int vif_id;

	vif_id = (super->s.misc & CARL9170_TX_SUPER_MISC_VIF_ID) >>
		 CARL9170_TX_SUPER_MISC_VIF_ID_S;

	if (WARN_ON_ONCE(vif_id >= AR9170_MAX_VIRTUAL_MAC))
		return NULL;

	vif = rcu_dereference(ar->vif_priv[vif_id].vif);
	if (unlikely(!vif))
		return NULL;

	/*
	 * Normally we should use wrappers like ieee80211_get_DA to get
	 * the correct peer ieee80211_sta.
	 *
	 * But there is a problem with indirect traffic (broadcasts, or
	 * data which is designated for other stations) in station mode.
	 * The frame will be directed to the AP for distribution and not
	 * to the actual destination.
	 */

	return ieee80211_find_sta(vif, hdr->addr1);
}

static void carl9170_tx_ps_unblock(struct ar9170 *ar, struct sk_buff *skb)
{
	struct ieee80211_sta *sta;
	struct carl9170_sta_info *sta_info;

	rcu_read_lock();
	sta = __carl9170_get_tx_sta(ar, skb);
	if (unlikely(!sta))
		goto out_rcu;

	sta_info = (struct carl9170_sta_info *) sta->drv_priv;
	if (atomic_dec_return(&sta_info->pending_frames) == 0)
		ieee80211_sta_block_awake(ar->hw, sta, false);

out_rcu:
	rcu_read_unlock();
}

static void carl9170_tx_accounting_free(struct ar9170 *ar, struct sk_buff *skb)
{
	int queue;

	queue = skb_get_queue_mapping(skb);

	spin_lock_bh(&ar->tx_stats_lock);

	ar->tx_stats[queue].len--;

	if (!is_mem_full(ar)) {
		unsigned int i;
		for (i = 0; i < ar->hw->queues; i++) {
			if (ar->tx_stats[i].len >= CARL9170_NUM_TX_LIMIT_SOFT)
				continue;

			if (ieee80211_queue_stopped(ar->hw, i)) {
				unsigned long tmp;

				tmp = jiffies - ar->queue_stop_timeout[i];
				if (tmp > ar->max_queue_stop_timeout[i])
					ar->max_queue_stop_timeout[i] = tmp;
			}

			ieee80211_wake_queue(ar->hw, i);
		}
	}

	spin_unlock_bh(&ar->tx_stats_lock);

	if (atomic_dec_and_test(&ar->tx_total_queued))
		complete(&ar->tx_flush);
}

static int carl9170_alloc_dev_space(struct ar9170 *ar, struct sk_buff *skb)
{
	struct _carl9170_tx_superframe *super = (void *) skb->data;
	unsigned int chunks;
	int cookie = -1;

	atomic_inc(&ar->mem_allocs);

	chunks = DIV_ROUND_UP(skb->len, ar->fw.mem_block_size);
	if (unlikely(atomic_sub_return(chunks, &ar->mem_free_blocks) < 0)) {
		atomic_add(chunks, &ar->mem_free_blocks);
		return -ENOSPC;
	}

	spin_lock_bh(&ar->mem_lock);
	cookie = bitmap_find_free_region(ar->mem_bitmap, ar->fw.mem_blocks, 0);
	spin_unlock_bh(&ar->mem_lock);

	if (unlikely(cookie < 0)) {
		atomic_add(chunks, &ar->mem_free_blocks);
		return -ENOSPC;
	}

	super = (void *) skb->data;

	/*
	 * Cookie #0 serves two special purposes:
	 *  1. The firmware might use it generate BlockACK frames
	 *     in responds of an incoming BlockAckReqs.
	 *
	 *  2. Prevent double-free bugs.
	 */
	super->s.cookie = (u8) cookie + 1;
	return 0;
}

static void carl9170_release_dev_space(struct ar9170 *ar, struct sk_buff *skb)
{
	struct _carl9170_tx_superframe *super = (void *) skb->data;
	int cookie;

	/* make a local copy of the cookie */
	cookie = super->s.cookie;
	/* invalidate cookie */
	super->s.cookie = 0;

	/*
	 * Do a out-of-bounds check on the cookie:
	 *
	 *  * cookie "0" is reserved and won't be assigned to any
	 *    out-going frame. Internally however, it is used to
	 *    mark no longer/un-accounted frames and serves as a
	 *    cheap way of preventing frames from being freed
	 *    twice by _accident_. NB: There is a tiny race...
	 *
	 *  * obviously, cookie number is limited by the amount
	 *    of available memory blocks, so the number can
	 *    never execeed the mem_blocks count.
	 */
	if (WARN_ON_ONCE(cookie == 0) ||
	    WARN_ON_ONCE(cookie > ar->fw.mem_blocks))
		return;

	atomic_add(DIV_ROUND_UP(skb->len, ar->fw.mem_block_size),
		   &ar->mem_free_blocks);

	spin_lock_bh(&ar->mem_lock);
	bitmap_release_region(ar->mem_bitmap, cookie - 1, 0);
	spin_unlock_bh(&ar->mem_lock);
}

/* Called from any context */
static void carl9170_tx_release(struct kref *ref)
{
	struct ar9170 *ar;
	struct carl9170_tx_info *arinfo;
	struct ieee80211_tx_info *txinfo;
	struct sk_buff *skb;

	arinfo = container_of(ref, struct carl9170_tx_info, ref);
	txinfo = container_of((void *) arinfo, struct ieee80211_tx_info,
			      rate_driver_data);
	skb = container_of((void *) txinfo, struct sk_buff, cb);

	ar = arinfo->ar;
	if (WARN_ON_ONCE(!ar))
		return;

	BUILD_BUG_ON(
	    offsetof(struct ieee80211_tx_info, status.ack_signal) != 20);

	memset(&txinfo->status.ack_signal, 0,
	       sizeof(struct ieee80211_tx_info) -
	       offsetof(struct ieee80211_tx_info, status.ack_signal));

	if (atomic_read(&ar->tx_total_queued))
		ar->tx_schedule = true;

	if (txinfo->flags & IEEE80211_TX_CTL_AMPDU) {
		if (!atomic_read(&ar->tx_ampdu_upload))
			ar->tx_ampdu_schedule = true;

		if (txinfo->flags & IEEE80211_TX_STAT_AMPDU) {
			struct _carl9170_tx_superframe *super;

			super = (void *)skb->data;
			txinfo->status.ampdu_len = super->s.rix;
			txinfo->status.ampdu_ack_len = super->s.cnt;
		} else if ((txinfo->flags & IEEE80211_TX_STAT_ACK) &&
			   !(txinfo->flags & IEEE80211_TX_CTL_REQ_TX_STATUS)) {
			/*
			 * drop redundant tx_status reports:
			 *
			 * 1. ampdu_ack_len of the final tx_status does
			 *    include the feedback of this particular frame.
			 *
			 * 2. tx_status_irqsafe only queues up to 128
			 *    tx feedback reports and discards the rest.
			 *
			 * 3. minstrel_ht is picky, it only accepts
			 *    reports of frames with the TX_STATUS_AMPDU flag.
			 *
			 * 4. mac80211 is not particularly interested in
			 *    feedback either [CTL_REQ_TX_STATUS not set]
			 */

			ieee80211_free_txskb(ar->hw, skb);
			return;
		} else {
			/*
			 * Either the frame transmission has failed or
			 * mac80211 requested tx status.
			 */
		}
	}

	skb_pull(skb, sizeof(struct _carl9170_tx_superframe));
	ieee80211_tx_status_irqsafe(ar->hw, skb);
}

void carl9170_tx_get_skb(struct sk_buff *skb)
{
	struct carl9170_tx_info *arinfo = (void *)
		(IEEE80211_SKB_CB(skb))->rate_driver_data;
	kref_get(&arinfo->ref);
}

int carl9170_tx_put_skb(struct sk_buff *skb)
{
	struct carl9170_tx_info *arinfo = (void *)
		(IEEE80211_SKB_CB(skb))->rate_driver_data;

	return kref_put(&arinfo->ref, carl9170_tx_release);
}

/* Caller must hold the tid_info->lock & rcu_read_lock */
static void carl9170_tx_shift_bm(struct ar9170 *ar,
	struct carl9170_sta_tid *tid_info, u16 seq)
{
	u16 off;

	off = SEQ_DIFF(seq, tid_info->bsn);

	if (WARN_ON_ONCE(off >= CARL9170_BAW_BITS))
		return;

	/*
	 * Sanity check. For each MPDU we set the bit in bitmap and
	 * clear it once we received the tx_status.
	 * But if the bit is already cleared then we've been bitten
	 * by a bug.
	 */
	WARN_ON_ONCE(!test_and_clear_bit(off, tid_info->bitmap));

	off = SEQ_DIFF(tid_info->snx, tid_info->bsn);
	if (WARN_ON_ONCE(off >= CARL9170_BAW_BITS))
		return;

	if (!bitmap_empty(tid_info->bitmap, off))
		off = find_first_bit(tid_info->bitmap, off);

	tid_info->bsn += off;
	tid_info->bsn &= 0x0fff;

	bitmap_shift_right(tid_info->bitmap, tid_info->bitmap,
			   off, CARL9170_BAW_BITS);
}

static void carl9170_tx_status_process_ampdu(struct ar9170 *ar,
	struct sk_buff *skb, struct ieee80211_tx_info *txinfo)
{
	struct _carl9170_tx_superframe *super = (void *) skb->data;
	struct ieee80211_hdr *hdr = (void *) super->frame_data;
	struct ieee80211_sta *sta;
	struct carl9170_sta_info *sta_info;
	struct carl9170_sta_tid *tid_info;
	u8 tid;

	if (!(txinfo->flags & IEEE80211_TX_CTL_AMPDU) ||
	    txinfo->flags & IEEE80211_TX_CTL_INJECTED)
		return;

	rcu_read_lock();
	sta = __carl9170_get_tx_sta(ar, skb);
	if (unlikely(!sta))
		goto out_rcu;

	tid = get_tid_h(hdr);

	sta_info = (void *) sta->drv_priv;
	tid_info = rcu_dereference(sta_info->agg[tid]);
	if (!tid_info)
		goto out_rcu;

	spin_lock_bh(&tid_info->lock);
	if (likely(tid_info->state >= CARL9170_TID_STATE_IDLE))
		carl9170_tx_shift_bm(ar, tid_info, get_seq_h(hdr));

	if (sta_info->stats[tid].clear) {
		sta_info->stats[tid].clear = false;
		sta_info->stats[tid].req = false;
		sta_info->stats[tid].ampdu_len = 0;
		sta_info->stats[tid].ampdu_ack_len = 0;
	}

	sta_info->stats[tid].ampdu_len++;
	if (txinfo->status.rates[0].count == 1)
		sta_info->stats[tid].ampdu_ack_len++;

	if (!(txinfo->flags & IEEE80211_TX_STAT_ACK))
		sta_info->stats[tid].req = true;

	if (super->f.mac_control & cpu_to_le16(AR9170_TX_MAC_IMM_BA)) {
		super->s.rix = sta_info->stats[tid].ampdu_len;
		super->s.cnt = sta_info->stats[tid].ampdu_ack_len;
		txinfo->flags |= IEEE80211_TX_STAT_AMPDU;
		if (sta_info->stats[tid].req)
			txinfo->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;

		sta_info->stats[tid].clear = true;
	}
	spin_unlock_bh(&tid_info->lock);

out_rcu:
	rcu_read_unlock();
}

static void carl9170_tx_bar_status(struct ar9170 *ar, struct sk_buff *skb,
	struct ieee80211_tx_info *tx_info)
{
	struct _carl9170_tx_superframe *super = (void *) skb->data;
	struct ieee80211_bar *bar = (void *) super->frame_data;

	/*
	 * Unlike all other frames, the status report for BARs does
	 * not directly come from the hardware as it is incapable of
	 * matching a BA to a previously send BAR.
	 * Instead the RX-path will scan for incoming BAs and set the
	 * IEEE80211_TX_STAT_ACK if it sees one that was likely
	 * caused by a BAR from us.
	 */

	if (unlikely(ieee80211_is_back_req(bar->frame_control)) &&
	   !(tx_info->flags & IEEE80211_TX_STAT_ACK)) {
		struct carl9170_bar_list_entry *entry;
		int queue = skb_get_queue_mapping(skb);

		rcu_read_lock();
		list_for_each_entry_rcu(entry, &ar->bar_list[queue], list) {
			if (entry->skb == skb) {
				spin_lock_bh(&ar->bar_list_lock[queue]);
				list_del_rcu(&entry->list);
				spin_unlock_bh(&ar->bar_list_lock[queue]);
				kfree_rcu(entry, head);
				goto out;
			}
		}

		WARN(1, "bar not found in %d - ra:%pM ta:%pM c:%x ssn:%x\n",
		       queue, bar->ra, bar->ta, bar->control,
			bar->start_seq_num);
out:
		rcu_read_unlock();
	}
}

void carl9170_tx_status(struct ar9170 *ar, struct sk_buff *skb,
			const bool success)
{
	struct ieee80211_tx_info *txinfo;

	carl9170_tx_accounting_free(ar, skb);

	txinfo = IEEE80211_SKB_CB(skb);

	carl9170_tx_bar_status(ar, skb, txinfo);

	if (success)
		txinfo->flags |= IEEE80211_TX_STAT_ACK;
	else
		ar->tx_ack_failures++;

	if (txinfo->flags & IEEE80211_TX_CTL_AMPDU)
		carl9170_tx_status_process_ampdu(ar, skb, txinfo);

	carl9170_tx_ps_unblock(ar, skb);
	carl9170_tx_put_skb(skb);
}

/* This function may be called form any context */
void carl9170_tx_callback(struct ar9170 *ar, struct sk_buff *skb)
{
	struct ieee80211_tx_info *txinfo = IEEE80211_SKB_CB(skb);

	atomic_dec(&ar->tx_total_pending);

	if (txinfo->flags & IEEE80211_TX_CTL_AMPDU)
		atomic_dec(&ar->tx_ampdu_upload);

	if (carl9170_tx_put_skb(skb))
		tasklet_hi_schedule(&ar->usb_tasklet);
}

static struct sk_buff *carl9170_get_queued_skb(struct ar9170 *ar, u8 cookie,
					       struct sk_buff_head *queue)
{
	struct sk_buff *skb;

	spin_lock_bh(&queue->lock);
	skb_queue_walk(queue, skb) {
		struct _carl9170_tx_superframe *txc = (void *) skb->data;

		if (txc->s.cookie != cookie)
			continue;

		__skb_unlink(skb, queue);
		spin_unlock_bh(&queue->lock);

		carl9170_release_dev_space(ar, skb);
		return skb;
	}
	spin_unlock_bh(&queue->lock);

	return NULL;
}

static void carl9170_tx_fill_rateinfo(struct ar9170 *ar, unsigned int rix,
	unsigned int tries, struct ieee80211_tx_info *txinfo)
{
	unsigned int i;

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		if (txinfo->status.rates[i].idx < 0)
			break;

		if (i == rix) {
			txinfo->status.rates[i].count = tries;
			i++;
			break;
		}
	}

	for (; i < IEEE80211_TX_MAX_RATES; i++) {
		txinfo->status.rates[i].idx = -1;
		txinfo->status.rates[i].count = 0;
	}
}

static void carl9170_check_queue_stop_timeout(struct ar9170 *ar)
{
	int i;
	struct sk_buff *skb;
	struct ieee80211_tx_info *txinfo;
	struct carl9170_tx_info *arinfo;
	bool restart = false;

	for (i = 0; i < ar->hw->queues; i++) {
		spin_lock_bh(&ar->tx_status[i].lock);

		skb = skb_peek(&ar->tx_status[i]);

		if (!skb)
			goto next;

		txinfo = IEEE80211_SKB_CB(skb);
		arinfo = (void *) txinfo->rate_driver_data;

		if (time_is_before_jiffies(arinfo->timeout +
		    msecs_to_jiffies(CARL9170_QUEUE_STUCK_TIMEOUT)) == true)
			restart = true;

next:
		spin_unlock_bh(&ar->tx_status[i].lock);
	}

	if (restart) {
		/*
		 * At least one queue has been stuck for long enough.
		 * Give the device a kick and hope it gets back to
		 * work.
		 *
		 * possible reasons may include:
		 *  - frames got lost/corrupted (bad connection to the device)
		 *  - stalled rx processing/usb controller hiccups
		 *  - firmware errors/bugs
		 *  - every bug you can think of.
		 *  - all bugs you can't...
		 *  - ...
		 */
		carl9170_restart(ar, CARL9170_RR_STUCK_TX);
	}
}

static void carl9170_tx_ampdu_timeout(struct ar9170 *ar)
{
	struct carl9170_sta_tid *iter;
	struct sk_buff *skb;
	struct ieee80211_tx_info *txinfo;
	struct carl9170_tx_info *arinfo;
	struct ieee80211_sta *sta;

	rcu_read_lock();
	list_for_each_entry_rcu(iter, &ar->tx_ampdu_list, list) {
		if (iter->state < CARL9170_TID_STATE_IDLE)
			continue;

		spin_lock_bh(&iter->lock);
		skb = skb_peek(&iter->queue);
		if (!skb)
			goto unlock;

		txinfo = IEEE80211_SKB_CB(skb);
		arinfo = (void *)txinfo->rate_driver_data;
		if (time_is_after_jiffies(arinfo->timeout +
		    msecs_to_jiffies(CARL9170_QUEUE_TIMEOUT)))
			goto unlock;

		sta = iter->sta;
		if (WARN_ON(!sta))
			goto unlock;

		ieee80211_stop_tx_ba_session(sta, iter->tid);
unlock:
		spin_unlock_bh(&iter->lock);

	}
	rcu_read_unlock();
}

void carl9170_tx_janitor(struct work_struct *work)
{
	struct ar9170 *ar = container_of(work, struct ar9170,
					 tx_janitor.work);
	if (!IS_STARTED(ar))
		return;

	ar->tx_janitor_last_run = jiffies;

	carl9170_check_queue_stop_timeout(ar);
	carl9170_tx_ampdu_timeout(ar);

	if (!atomic_read(&ar->tx_total_queued))
		return;

	ieee80211_queue_delayed_work(ar->hw, &ar->tx_janitor,
		msecs_to_jiffies(CARL9170_TX_TIMEOUT));
}

static void __carl9170_tx_process_status(struct ar9170 *ar,
	const uint8_t cookie, const uint8_t info)
{
	struct sk_buff *skb;
	struct ieee80211_tx_info *txinfo;
	unsigned int r, t, q;
	bool success = true;

	q = ar9170_qmap(info & CARL9170_TX_STATUS_QUEUE);

	skb = carl9170_get_queued_skb(ar, cookie, &ar->tx_status[q]);
	if (!skb) {
		/*
		 * We have lost the race to another thread.
		 */

		return ;
	}

	txinfo = IEEE80211_SKB_CB(skb);

	if (!(info & CARL9170_TX_STATUS_SUCCESS))
		success = false;

	r = (info & CARL9170_TX_STATUS_RIX) >> CARL9170_TX_STATUS_RIX_S;
	t = (info & CARL9170_TX_STATUS_TRIES) >> CARL9170_TX_STATUS_TRIES_S;

	carl9170_tx_fill_rateinfo(ar, r, t, txinfo);
	carl9170_tx_status(ar, skb, success);
}

void carl9170_tx_process_status(struct ar9170 *ar,
				const struct carl9170_rsp *cmd)
{
	unsigned int i;

	for (i = 0;  i < cmd->hdr.ext; i++) {
		if (WARN_ON(i > ((cmd->hdr.len / 2) + 1))) {
			print_hex_dump_bytes("UU:", DUMP_PREFIX_NONE,
					     (void *) cmd, cmd->hdr.len + 4);
			break;
		}

		__carl9170_tx_process_status(ar, cmd->_tx_status[i].cookie,
					     cmd->_tx_status[i].info);
	}
}

static void carl9170_tx_rate_tpc_chains(struct ar9170 *ar,
	struct ieee80211_tx_info *info,	struct ieee80211_tx_rate *txrate,
	unsigned int *phyrate, unsigned int *tpc, unsigned int *chains)
{
	struct ieee80211_rate *rate = NULL;
	u8 *txpower;
	unsigned int idx;

	idx = txrate->idx;
	*tpc = 0;
	*phyrate = 0;

	if (txrate->flags & IEEE80211_TX_RC_MCS) {
		if (txrate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH) {
			/* +1 dBm for HT40 */
			*tpc += 2;

			if (info->band == NL80211_BAND_2GHZ)
				txpower = ar->power_2G_ht40;
			else
				txpower = ar->power_5G_ht40;
		} else {
			if (info->band == NL80211_BAND_2GHZ)
				txpower = ar->power_2G_ht20;
			else
				txpower = ar->power_5G_ht20;
		}

		*phyrate = txrate->idx;
		*tpc += txpower[idx & 7];
	} else {
		if (info->band == NL80211_BAND_2GHZ) {
			if (idx < 4)
				txpower = ar->power_2G_cck;
			else
				txpower = ar->power_2G_ofdm;
		} else {
			txpower = ar->power_5G_leg;
			idx += 4;
		}

		rate = &__carl9170_ratetable[idx];
		*tpc += txpower[(rate->hw_value & 0x30) >> 4];
		*phyrate = rate->hw_value & 0xf;
	}

	if (ar->eeprom.tx_mask == 1) {
		*chains = AR9170_TX_PHY_TXCHAIN_1;
	} else {
		if (!(txrate->flags & IEEE80211_TX_RC_MCS) &&
		    rate && rate->bitrate >= 360)
			*chains = AR9170_TX_PHY_TXCHAIN_1;
		else
			*chains = AR9170_TX_PHY_TXCHAIN_2;
	}

	*tpc = min_t(unsigned int, *tpc, ar->hw->conf.power_level * 2);
}

static __le32 carl9170_tx_physet(struct ar9170 *ar,
	struct ieee80211_tx_info *info, struct ieee80211_tx_rate *txrate)
{
	unsigned int power = 0, chains = 0, phyrate = 0;
	__le32 tmp;

	tmp = cpu_to_le32(0);

	if (txrate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		tmp |= cpu_to_le32(AR9170_TX_PHY_BW_40MHZ <<
			AR9170_TX_PHY_BW_S);
	/* this works because 40 MHz is 2 and dup is 3 */
	if (txrate->flags & IEEE80211_TX_RC_DUP_DATA)
		tmp |= cpu_to_le32(AR9170_TX_PHY_BW_40MHZ_DUP <<
			AR9170_TX_PHY_BW_S);

	if (txrate->flags & IEEE80211_TX_RC_SHORT_GI)
		tmp |= cpu_to_le32(AR9170_TX_PHY_SHORT_GI);

	if (txrate->flags & IEEE80211_TX_RC_MCS) {
		SET_VAL(AR9170_TX_PHY_MCS, phyrate, txrate->idx);

		/* heavy clip control */
		tmp |= cpu_to_le32((txrate->idx & 0x7) <<
			AR9170_TX_PHY_TX_HEAVY_CLIP_S);

		tmp |= cpu_to_le32(AR9170_TX_PHY_MOD_HT);

		/*
		 * green field preamble does not work.
		 *
		 * if (txrate->flags & IEEE80211_TX_RC_GREEN_FIELD)
		 * tmp |= cpu_to_le32(AR9170_TX_PHY_GREENFIELD);
		 */
	} else {
		if (info->band == NL80211_BAND_2GHZ) {
			if (txrate->idx <= AR9170_TX_PHY_RATE_CCK_11M)
				tmp |= cpu_to_le32(AR9170_TX_PHY_MOD_CCK);
			else
				tmp |= cpu_to_le32(AR9170_TX_PHY_MOD_OFDM);
		} else {
			tmp |= cpu_to_le32(AR9170_TX_PHY_MOD_OFDM);
		}

		/*
		 * short preamble seems to be broken too.
		 *
		 * if (txrate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
		 *	tmp |= cpu_to_le32(AR9170_TX_PHY_SHORT_PREAMBLE);
		 */
	}
	carl9170_tx_rate_tpc_chains(ar, info, txrate,
				    &phyrate, &power, &chains);

	tmp |= cpu_to_le32(SET_CONSTVAL(AR9170_TX_PHY_MCS, phyrate));
	tmp |= cpu_to_le32(SET_CONSTVAL(AR9170_TX_PHY_TX_PWR, power));
	tmp |= cpu_to_le32(SET_CONSTVAL(AR9170_TX_PHY_TXCHAIN, chains));
	return tmp;
}

static bool carl9170_tx_rts_check(struct ar9170 *ar,
				  struct ieee80211_tx_rate *rate,
				  bool ampdu, bool multi)
{
	switch (ar->erp_mode) {
	case CARL9170_ERP_AUTO:
		if (ampdu)
			break;
		fallthrough;

	case CARL9170_ERP_MAC80211:
		if (!(rate->flags & IEEE80211_TX_RC_USE_RTS_CTS))
			break;
		fallthrough;

	case CARL9170_ERP_RTS:
		if (likely(!multi))
			return true;
		break;

	default:
		break;
	}

	return false;
}

static bool carl9170_tx_cts_check(struct ar9170 *ar,
				  struct ieee80211_tx_rate *rate)
{
	switch (ar->erp_mode) {
	case CARL9170_ERP_AUTO:
	case CARL9170_ERP_MAC80211:
		if (!(rate->flags & IEEE80211_TX_RC_USE_CTS_PROTECT))
			break;
		fallthrough;

	case CARL9170_ERP_CTS:
		return true;

	default:
		break;
	}

	return false;
}

static void carl9170_tx_get_rates(struct ar9170 *ar,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  struct sk_buff *skb)
{
	struct ieee80211_tx_info *info;

	BUILD_BUG_ON(IEEE80211_TX_MAX_RATES < CARL9170_TX_MAX_RATES);
	BUILD_BUG_ON(IEEE80211_TX_MAX_RATES > IEEE80211_TX_RATE_TABLE_SIZE);

	info = IEEE80211_SKB_CB(skb);

	ieee80211_get_tx_rates(vif, sta, skb,
			       info->control.rates,
			       IEEE80211_TX_MAX_RATES);
}

static void carl9170_tx_apply_rateset(struct ar9170 *ar,
				      struct ieee80211_tx_info *sinfo,
				      struct sk_buff *skb)
{
	struct ieee80211_tx_rate *txrate;
	struct ieee80211_tx_info *info;
	struct _carl9170_tx_superframe *txc = (void *) skb->data;
	int i;
	bool ampdu;
	bool no_ack;

	info = IEEE80211_SKB_CB(skb);
	ampdu = !!(info->flags & IEEE80211_TX_CTL_AMPDU);
	no_ack = !!(info->flags & IEEE80211_TX_CTL_NO_ACK);

	/* Set the rate control probe flag for all (sub-) frames.
	 * This is because the TX_STATS_AMPDU flag is only set on
	 * the last frame, so it has to be inherited.
	 */
	info->flags |= (sinfo->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE);

	/* NOTE: For the first rate, the ERP & AMPDU flags are directly
	 * taken from mac_control. For all fallback rate, the firmware
	 * updates the mac_control flags from the rate info field.
	 */
	for (i = 0; i < CARL9170_TX_MAX_RATES; i++) {
		__le32 phy_set;

		txrate = &sinfo->control.rates[i];
		if (txrate->idx < 0)
			break;

		phy_set = carl9170_tx_physet(ar, info, txrate);
		if (i == 0) {
			__le16 mac_tmp = cpu_to_le16(0);

			/* first rate - part of the hw's frame header */
			txc->f.phy_control = phy_set;

			if (ampdu && txrate->flags & IEEE80211_TX_RC_MCS)
				mac_tmp |= cpu_to_le16(AR9170_TX_MAC_AGGR);

			if (carl9170_tx_rts_check(ar, txrate, ampdu, no_ack))
				mac_tmp |= cpu_to_le16(AR9170_TX_MAC_PROT_RTS);
			else if (carl9170_tx_cts_check(ar, txrate))
				mac_tmp |= cpu_to_le16(AR9170_TX_MAC_PROT_CTS);

			txc->f.mac_control |= mac_tmp;
		} else {
			/* fallback rates are stored in the firmware's
			 * retry rate set array.
			 */
			txc->s.rr[i - 1] = phy_set;
		}

		SET_VAL(CARL9170_TX_SUPER_RI_TRIES, txc->s.ri[i],
			txrate->count);

		if (carl9170_tx_rts_check(ar, txrate, ampdu, no_ack))
			txc->s.ri[i] |= (AR9170_TX_MAC_PROT_RTS <<
				CARL9170_TX_SUPER_RI_ERP_PROT_S);
		else if (carl9170_tx_cts_check(ar, txrate))
			txc->s.ri[i] |= (AR9170_TX_MAC_PROT_CTS <<
				CARL9170_TX_SUPER_RI_ERP_PROT_S);

		if (ampdu && (txrate->flags & IEEE80211_TX_RC_MCS))
			txc->s.ri[i] |= CARL9170_TX_SUPER_RI_AMPDU;
	}
}

static int carl9170_tx_prepare(struct ar9170 *ar,
			       struct ieee80211_sta *sta,
			       struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;
	struct _carl9170_tx_superframe *txc;
	struct carl9170_vif_info *cvif;
	struct ieee80211_tx_info *info;
	struct carl9170_tx_info *arinfo;
	unsigned int hw_queue;
	__le16 mac_tmp;
	u16 len;

	BUILD_BUG_ON(sizeof(*arinfo) > sizeof(info->rate_driver_data));
	BUILD_BUG_ON(sizeof(struct _carl9170_tx_superdesc) !=
		     CARL9170_TX_SUPERDESC_LEN);

	BUILD_BUG_ON(sizeof(struct _ar9170_tx_hwdesc) !=
		     AR9170_TX_HWDESC_LEN);

	BUILD_BUG_ON(AR9170_MAX_VIRTUAL_MAC >
		((CARL9170_TX_SUPER_MISC_VIF_ID >>
		 CARL9170_TX_SUPER_MISC_VIF_ID_S) + 1));

	hw_queue = ar9170_qmap(carl9170_get_queue(ar, skb));

	hdr = (void *)skb->data;
	info = IEEE80211_SKB_CB(skb);
	len = skb->len;

	/*
	 * Note: If the frame was sent through a monitor interface,
	 * the ieee80211_vif pointer can be NULL.
	 */
	if (likely(info->control.vif))
		cvif = (void *) info->control.vif->drv_priv;
	else
		cvif = NULL;

	txc = skb_push(skb, sizeof(*txc));
	memset(txc, 0, sizeof(*txc));

	SET_VAL(CARL9170_TX_SUPER_MISC_QUEUE, txc->s.misc, hw_queue);

	if (likely(cvif))
		SET_VAL(CARL9170_TX_SUPER_MISC_VIF_ID, txc->s.misc, cvif->id);

	if (unlikely(info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM))
		txc->s.misc |= CARL9170_TX_SUPER_MISC_CAB;

	if (unlikely(info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ))
		txc->s.misc |= CARL9170_TX_SUPER_MISC_ASSIGN_SEQ;

	if (unlikely(ieee80211_is_probe_resp(hdr->frame_control)))
		txc->s.misc |= CARL9170_TX_SUPER_MISC_FILL_IN_TSF;

	mac_tmp = cpu_to_le16(AR9170_TX_MAC_HW_DURATION |
			      AR9170_TX_MAC_BACKOFF);
	mac_tmp |= cpu_to_le16((hw_queue << AR9170_TX_MAC_QOS_S) &
			       AR9170_TX_MAC_QOS);

	if (unlikely(info->flags & IEEE80211_TX_CTL_NO_ACK))
		mac_tmp |= cpu_to_le16(AR9170_TX_MAC_NO_ACK);

	if (info->control.hw_key) {
		len += info->control.hw_key->icv_len;

		switch (info->control.hw_key->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
		case WLAN_CIPHER_SUITE_TKIP:
			mac_tmp |= cpu_to_le16(AR9170_TX_MAC_ENCR_RC4);
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			mac_tmp |= cpu_to_le16(AR9170_TX_MAC_ENCR_AES);
			break;
		default:
			WARN_ON(1);
			goto err_out;
		}
	}

	if (info->flags & IEEE80211_TX_CTL_AMPDU) {
		unsigned int density, factor;

		if (unlikely(!sta || !cvif))
			goto err_out;

		factor = min_t(unsigned int, 1u, sta->ht_cap.ampdu_factor);
		density = sta->ht_cap.ampdu_density;

		if (density) {
			/*
			 * Watch out!
			 *
			 * Otus uses slightly different density values than
			 * those from the 802.11n spec.
			 */

			density = max_t(unsigned int, density + 1, 7u);
		}

		SET_VAL(CARL9170_TX_SUPER_AMPDU_DENSITY,
			txc->s.ampdu_settings, density);

		SET_VAL(CARL9170_TX_SUPER_AMPDU_FACTOR,
			txc->s.ampdu_settings, factor);
	}

	txc->s.len = cpu_to_le16(skb->len);
	txc->f.length = cpu_to_le16(len + FCS_LEN);
	txc->f.mac_control = mac_tmp;

	arinfo = (void *)info->rate_driver_data;
	arinfo->timeout = jiffies;
	arinfo->ar = ar;
	kref_init(&arinfo->ref);
	return 0;

err_out:
	skb_pull(skb, sizeof(*txc));
	return -EINVAL;
}

static void carl9170_set_immba(struct ar9170 *ar, struct sk_buff *skb)
{
	struct _carl9170_tx_superframe *super;

	super = (void *) skb->data;
	super->f.mac_control |= cpu_to_le16(AR9170_TX_MAC_IMM_BA);
}

static void carl9170_set_ampdu_params(struct ar9170 *ar, struct sk_buff *skb)
{
	struct _carl9170_tx_superframe *super;
	int tmp;

	super = (void *) skb->data;

	tmp = (super->s.ampdu_settings & CARL9170_TX_SUPER_AMPDU_DENSITY) <<
		CARL9170_TX_SUPER_AMPDU_DENSITY_S;

	/*
	 * If you haven't noticed carl9170_tx_prepare has already filled
	 * in all ampdu spacing & factor parameters.
	 * Now it's the time to check whenever the settings have to be
	 * updated by the firmware, or if everything is still the same.
	 *
	 * There's no sane way to handle different density values with
	 * this hardware, so we may as well just do the compare in the
	 * driver.
	 */

	if (tmp != ar->current_density) {
		ar->current_density = tmp;
		super->s.ampdu_settings |=
			CARL9170_TX_SUPER_AMPDU_COMMIT_DENSITY;
	}

	tmp = (super->s.ampdu_settings & CARL9170_TX_SUPER_AMPDU_FACTOR) <<
		CARL9170_TX_SUPER_AMPDU_FACTOR_S;

	if (tmp != ar->current_factor) {
		ar->current_factor = tmp;
		super->s.ampdu_settings |=
			CARL9170_TX_SUPER_AMPDU_COMMIT_FACTOR;
	}
}

static void carl9170_tx_ampdu(struct ar9170 *ar)
{
	struct sk_buff_head agg;
	struct carl9170_sta_tid *tid_info;
	struct sk_buff *skb, *first;
	struct ieee80211_tx_info *tx_info_first;
	unsigned int i = 0, done_ampdus = 0;
	u16 seq, queue, tmpssn;

	atomic_inc(&ar->tx_ampdu_scheduler);
	ar->tx_ampdu_schedule = false;

	if (atomic_read(&ar->tx_ampdu_upload))
		return;

	if (!ar->tx_ampdu_list_len)
		return;

	__skb_queue_head_init(&agg);

	rcu_read_lock();
	tid_info = rcu_dereference(ar->tx_ampdu_iter);
	if (WARN_ON_ONCE(!tid_info)) {
		rcu_read_unlock();
		return;
	}

retry:
	list_for_each_entry_continue_rcu(tid_info, &ar->tx_ampdu_list, list) {
		i++;

		if (tid_info->state < CARL9170_TID_STATE_PROGRESS)
			continue;

		queue = TID_TO_WME_AC(tid_info->tid);

		spin_lock_bh(&tid_info->lock);
		if (tid_info->state != CARL9170_TID_STATE_XMIT)
			goto processed;

		tid_info->counter++;
		first = skb_peek(&tid_info->queue);
		tmpssn = carl9170_get_seq(first);
		seq = tid_info->snx;

		if (unlikely(tmpssn != seq)) {
			tid_info->state = CARL9170_TID_STATE_IDLE;

			goto processed;
		}

		tx_info_first = NULL;
		while ((skb = skb_peek(&tid_info->queue))) {
			/* strict 0, 1, ..., n - 1, n frame sequence order */
			if (unlikely(carl9170_get_seq(skb) != seq))
				break;

			/* don't upload more than AMPDU FACTOR allows. */
			if (unlikely(SEQ_DIFF(tid_info->snx, tid_info->bsn) >=
			    (tid_info->max - 1)))
				break;

			if (!tx_info_first) {
				carl9170_tx_get_rates(ar, tid_info->vif,
						      tid_info->sta, first);
				tx_info_first = IEEE80211_SKB_CB(first);
			}

			carl9170_tx_apply_rateset(ar, tx_info_first, skb);

			atomic_inc(&ar->tx_ampdu_upload);
			tid_info->snx = seq = SEQ_NEXT(seq);
			__skb_unlink(skb, &tid_info->queue);

			__skb_queue_tail(&agg, skb);

			if (skb_queue_len(&agg) >= CARL9170_NUM_TX_AGG_MAX)
				break;
		}

		if (skb_queue_empty(&tid_info->queue) ||
		    carl9170_get_seq(skb_peek(&tid_info->queue)) !=
		    tid_info->snx) {
			/* stop TID, if A-MPDU frames are still missing,
			 * or whenever the queue is empty.
			 */

			tid_info->state = CARL9170_TID_STATE_IDLE;
		}
		done_ampdus++;

processed:
		spin_unlock_bh(&tid_info->lock);

		if (skb_queue_empty(&agg))
			continue;

		/* apply ampdu spacing & factor settings */
		carl9170_set_ampdu_params(ar, skb_peek(&agg));

		/* set aggregation push bit */
		carl9170_set_immba(ar, skb_peek_tail(&agg));

		spin_lock_bh(&ar->tx_pending[queue].lock);
		skb_queue_splice_tail_init(&agg, &ar->tx_pending[queue]);
		spin_unlock_bh(&ar->tx_pending[queue].lock);
		ar->tx_schedule = true;
	}
	if ((done_ampdus++ == 0) && (i++ == 0))
		goto retry;

	rcu_assign_pointer(ar->tx_ampdu_iter, tid_info);
	rcu_read_unlock();
}

static struct sk_buff *carl9170_tx_pick_skb(struct ar9170 *ar,
					    struct sk_buff_head *queue)
{
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;
	struct carl9170_tx_info *arinfo;

	BUILD_BUG_ON(sizeof(*arinfo) > sizeof(info->rate_driver_data));

	spin_lock_bh(&queue->lock);
	skb = skb_peek(queue);
	if (unlikely(!skb))
		goto err_unlock;

	if (carl9170_alloc_dev_space(ar, skb))
		goto err_unlock;

	__skb_unlink(skb, queue);
	spin_unlock_bh(&queue->lock);

	info = IEEE80211_SKB_CB(skb);
	arinfo = (void *) info->rate_driver_data;

	arinfo->timeout = jiffies;
	return skb;

err_unlock:
	spin_unlock_bh(&queue->lock);
	return NULL;
}

void carl9170_tx_drop(struct ar9170 *ar, struct sk_buff *skb)
{
	struct _carl9170_tx_superframe *super;
	uint8_t q = 0;

	ar->tx_dropped++;

	super = (void *)skb->data;
	SET_VAL(CARL9170_TX_SUPER_MISC_QUEUE, q,
		ar9170_qmap(carl9170_get_queue(ar, skb)));
	__carl9170_tx_process_status(ar, super->s.cookie, q);
}

static bool carl9170_tx_ps_drop(struct ar9170 *ar, struct sk_buff *skb)
{
	struct ieee80211_sta *sta;
	struct carl9170_sta_info *sta_info;
	struct ieee80211_tx_info *tx_info;

	rcu_read_lock();
	sta = __carl9170_get_tx_sta(ar, skb);
	if (!sta)
		goto out_rcu;

	sta_info = (void *) sta->drv_priv;
	tx_info = IEEE80211_SKB_CB(skb);

	if (unlikely(sta_info->sleeping) &&
	    !(tx_info->flags & (IEEE80211_TX_CTL_NO_PS_BUFFER |
				IEEE80211_TX_CTL_CLEAR_PS_FILT))) {
		rcu_read_unlock();

		if (tx_info->flags & IEEE80211_TX_CTL_AMPDU)
			atomic_dec(&ar->tx_ampdu_upload);

		tx_info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
		carl9170_release_dev_space(ar, skb);
		carl9170_tx_status(ar, skb, false);
		return true;
	}

out_rcu:
	rcu_read_unlock();
	return false;
}

static void carl9170_bar_check(struct ar9170 *ar, struct sk_buff *skb)
{
	struct _carl9170_tx_superframe *super = (void *) skb->data;
	struct ieee80211_bar *bar = (void *) super->frame_data;

	if (unlikely(ieee80211_is_back_req(bar->frame_control)) &&
	    skb->len >= sizeof(struct ieee80211_bar)) {
		struct carl9170_bar_list_entry *entry;
		unsigned int queue = skb_get_queue_mapping(skb);

		entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
		if (!WARN_ON_ONCE(!entry)) {
			entry->skb = skb;
			spin_lock_bh(&ar->bar_list_lock[queue]);
			list_add_tail_rcu(&entry->list, &ar->bar_list[queue]);
			spin_unlock_bh(&ar->bar_list_lock[queue]);
		}
	}
}

static void carl9170_tx(struct ar9170 *ar)
{
	struct sk_buff *skb;
	unsigned int i, q;
	bool schedule_garbagecollector = false;

	ar->tx_schedule = false;

	if (unlikely(!IS_STARTED(ar)))
		return;

	carl9170_usb_handle_tx_err(ar);

	for (i = 0; i < ar->hw->queues; i++) {
		while (!skb_queue_empty(&ar->tx_pending[i])) {
			skb = carl9170_tx_pick_skb(ar, &ar->tx_pending[i]);
			if (unlikely(!skb))
				break;

			if (unlikely(carl9170_tx_ps_drop(ar, skb)))
				continue;

			carl9170_bar_check(ar, skb);

			atomic_inc(&ar->tx_total_pending);

			q = __carl9170_get_queue(ar, i);
			/*
			 * NB: tx_status[i] vs. tx_status[q],
			 * TODO: Move into pick_skb or alloc_dev_space.
			 */
			skb_queue_tail(&ar->tx_status[q], skb);

			/*
			 * increase ref count to "2".
			 * Ref counting is the easiest way to solve the
			 * race between the urb's completion routine:
			 *	carl9170_tx_callback
			 * and wlan tx status functions:
			 *	carl9170_tx_status/janitor.
			 */
			carl9170_tx_get_skb(skb);

			carl9170_usb_tx(ar, skb);
			schedule_garbagecollector = true;
		}
	}

	if (!schedule_garbagecollector)
		return;

	ieee80211_queue_delayed_work(ar->hw, &ar->tx_janitor,
		msecs_to_jiffies(CARL9170_TX_TIMEOUT));
}

static bool carl9170_tx_ampdu_queue(struct ar9170 *ar,
	struct ieee80211_sta *sta, struct sk_buff *skb,
	struct ieee80211_tx_info *txinfo)
{
	struct carl9170_sta_info *sta_info;
	struct carl9170_sta_tid *agg;
	struct sk_buff *iter;
	u16 tid, seq, qseq, off;
	bool run = false;

	tid = carl9170_get_tid(skb);
	seq = carl9170_get_seq(skb);
	sta_info = (void *) sta->drv_priv;

	rcu_read_lock();
	agg = rcu_dereference(sta_info->agg[tid]);

	if (!agg)
		goto err_unlock_rcu;

	spin_lock_bh(&agg->lock);
	if (unlikely(agg->state < CARL9170_TID_STATE_IDLE))
		goto err_unlock;

	/* check if sequence is within the BA window */
	if (unlikely(!BAW_WITHIN(agg->bsn, CARL9170_BAW_BITS, seq)))
		goto err_unlock;

	if (WARN_ON_ONCE(!BAW_WITHIN(agg->snx, CARL9170_BAW_BITS, seq)))
		goto err_unlock;

	off = SEQ_DIFF(seq, agg->bsn);
	if (WARN_ON_ONCE(test_and_set_bit(off, agg->bitmap)))
		goto err_unlock;

	if (likely(BAW_WITHIN(agg->hsn, CARL9170_BAW_BITS, seq))) {
		__skb_queue_tail(&agg->queue, skb);
		agg->hsn = seq;
		goto queued;
	}

	skb_queue_reverse_walk(&agg->queue, iter) {
		qseq = carl9170_get_seq(iter);

		if (BAW_WITHIN(qseq, CARL9170_BAW_BITS, seq)) {
			__skb_queue_after(&agg->queue, iter, skb);
			goto queued;
		}
	}

	__skb_queue_head(&agg->queue, skb);
queued:

	if (unlikely(agg->state != CARL9170_TID_STATE_XMIT)) {
		if (agg->snx == carl9170_get_seq(skb_peek(&agg->queue))) {
			agg->state = CARL9170_TID_STATE_XMIT;
			run = true;
		}
	}

	spin_unlock_bh(&agg->lock);
	rcu_read_unlock();

	return run;

err_unlock:
	spin_unlock_bh(&agg->lock);

err_unlock_rcu:
	rcu_read_unlock();
	txinfo->flags &= ~IEEE80211_TX_CTL_AMPDU;
	carl9170_tx_status(ar, skb, false);
	ar->tx_dropped++;
	return false;
}

void carl9170_op_tx(struct ieee80211_hw *hw,
		    struct ieee80211_tx_control *control,
		    struct sk_buff *skb)
{
	struct ar9170 *ar = hw->priv;
	struct ieee80211_tx_info *info;
	struct ieee80211_sta *sta = control->sta;
	struct ieee80211_vif *vif;
	bool run;

	if (unlikely(!IS_STARTED(ar)))
		goto err_free;

	info = IEEE80211_SKB_CB(skb);
	vif = info->control.vif;

	if (unlikely(carl9170_tx_prepare(ar, sta, skb)))
		goto err_free;

	carl9170_tx_accounting(ar, skb);
	/*
	 * from now on, one has to use carl9170_tx_status to free
	 * all ressouces which are associated with the frame.
	 */

	if (sta) {
		struct carl9170_sta_info *stai = (void *) sta->drv_priv;
		atomic_inc(&stai->pending_frames);
	}

	if (info->flags & IEEE80211_TX_CTL_AMPDU) {
		/* to static code analyzers and reviewers:
		 * mac80211 guarantees that a valid "sta"
		 * reference is present, if a frame is to
		 * be part of an ampdu. Hence any extra
		 * sta == NULL checks are redundant in this
		 * special case.
		 */
		run = carl9170_tx_ampdu_queue(ar, sta, skb, info);
		if (run)
			carl9170_tx_ampdu(ar);

	} else {
		unsigned int queue = skb_get_queue_mapping(skb);

		carl9170_tx_get_rates(ar, vif, sta, skb);
		carl9170_tx_apply_rateset(ar, info, skb);
		skb_queue_tail(&ar->tx_pending[queue], skb);
	}

	carl9170_tx(ar);
	return;

err_free:
	ar->tx_dropped++;
	ieee80211_free_txskb(ar->hw, skb);
}

void carl9170_tx_scheduler(struct ar9170 *ar)
{

	if (ar->tx_ampdu_schedule)
		carl9170_tx_ampdu(ar);

	if (ar->tx_schedule)
		carl9170_tx(ar);
}

/* caller has to take rcu_read_lock */
static struct carl9170_vif_info *carl9170_pick_beaconing_vif(struct ar9170 *ar)
{
	struct carl9170_vif_info *cvif;
	int i = 1;

	/* The AR9170 hardware has no fancy beacon queue or some
	 * other scheduling mechanism. So, the driver has to make
	 * due by setting the two beacon timers (pretbtt and tbtt)
	 * once and then swapping the beacon address in the HW's
	 * register file each time the pretbtt fires.
	 */

	cvif = rcu_dereference(ar->beacon_iter);
	if (ar->vifs > 0 && cvif) {
		do {
			list_for_each_entry_continue_rcu(cvif, &ar->vif_list,
							 list) {
				if (cvif->active && cvif->enable_beacon)
					goto out;
			}
		} while (ar->beacon_enabled && i--);
	}

out:
	RCU_INIT_POINTER(ar->beacon_iter, cvif);
	return cvif;
}

static bool carl9170_tx_beacon_physet(struct ar9170 *ar, struct sk_buff *skb,
				      u32 *ht1, u32 *plcp)
{
	struct ieee80211_tx_info *txinfo;
	struct ieee80211_tx_rate *rate;
	unsigned int power, chains;
	bool ht_rate;

	txinfo = IEEE80211_SKB_CB(skb);
	rate = &txinfo->control.rates[0];
	ht_rate = !!(txinfo->control.rates[0].flags & IEEE80211_TX_RC_MCS);
	carl9170_tx_rate_tpc_chains(ar, txinfo, rate, plcp, &power, &chains);

	*ht1 = AR9170_MAC_BCN_HT1_TX_ANT0;
	if (chains == AR9170_TX_PHY_TXCHAIN_2)
		*ht1 |= AR9170_MAC_BCN_HT1_TX_ANT1;
	SET_VAL(AR9170_MAC_BCN_HT1_PWR_CTRL, *ht1, 7);
	SET_VAL(AR9170_MAC_BCN_HT1_TPC, *ht1, power);
	SET_VAL(AR9170_MAC_BCN_HT1_CHAIN_MASK, *ht1, chains);

	if (ht_rate) {
		*ht1 |= AR9170_MAC_BCN_HT1_HT_EN;
		if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
			*plcp |= AR9170_MAC_BCN_HT2_SGI;

		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH) {
			*ht1 |= AR9170_MAC_BCN_HT1_BWC_40M_SHARED;
			*plcp |= AR9170_MAC_BCN_HT2_BW40;
		} else if (rate->flags & IEEE80211_TX_RC_DUP_DATA) {
			*ht1 |= AR9170_MAC_BCN_HT1_BWC_40M_DUP;
			*plcp |= AR9170_MAC_BCN_HT2_BW40;
		}

		SET_VAL(AR9170_MAC_BCN_HT2_LEN, *plcp, skb->len + FCS_LEN);
	} else {
		if (*plcp <= AR9170_TX_PHY_RATE_CCK_11M)
			*plcp |= ((skb->len + FCS_LEN) << (3 + 16)) + 0x0400;
		else
			*plcp |= ((skb->len + FCS_LEN) << 16) + 0x0010;
	}

	return ht_rate;
}

int carl9170_update_beacon(struct ar9170 *ar, const bool submit)
{
	struct sk_buff *skb = NULL;
	struct carl9170_vif_info *cvif;
	__le32 *data, *old = NULL;
	u32 word, ht1, plcp, off, addr, len;
	int i = 0, err = 0;
	bool ht_rate;

	rcu_read_lock();
	cvif = carl9170_pick_beaconing_vif(ar);
	if (!cvif)
		goto out_unlock;

	skb = ieee80211_beacon_get_tim(ar->hw, carl9170_get_vif(cvif),
		NULL, NULL);

	if (!skb) {
		err = -ENOMEM;
		goto err_free;
	}

	spin_lock_bh(&ar->beacon_lock);
	data = (__le32 *)skb->data;
	if (cvif->beacon)
		old = (__le32 *)cvif->beacon->data;

	off = cvif->id * AR9170_MAC_BCN_LENGTH_MAX;
	addr = ar->fw.beacon_addr + off;
	len = roundup(skb->len + FCS_LEN, 4);

	if ((off + len) > ar->fw.beacon_max_len) {
		if (net_ratelimit()) {
			wiphy_err(ar->hw->wiphy, "beacon does not "
				  "fit into device memory!\n");
		}
		err = -EINVAL;
		goto err_unlock;
	}

	if (len > AR9170_MAC_BCN_LENGTH_MAX) {
		if (net_ratelimit()) {
			wiphy_err(ar->hw->wiphy, "no support for beacons "
				"bigger than %d (yours:%d).\n",
				 AR9170_MAC_BCN_LENGTH_MAX, len);
		}

		err = -EMSGSIZE;
		goto err_unlock;
	}

	ht_rate = carl9170_tx_beacon_physet(ar, skb, &ht1, &plcp);

	carl9170_async_regwrite_begin(ar);
	carl9170_async_regwrite(AR9170_MAC_REG_BCN_HT1, ht1);
	if (ht_rate)
		carl9170_async_regwrite(AR9170_MAC_REG_BCN_HT2, plcp);
	else
		carl9170_async_regwrite(AR9170_MAC_REG_BCN_PLCP, plcp);

	for (i = 0; i < DIV_ROUND_UP(skb->len, 4); i++) {
		/*
		 * XXX: This accesses beyond skb data for up
		 *	to the last 3 bytes!!
		 */

		if (old && (data[i] == old[i]))
			continue;

		word = le32_to_cpu(data[i]);
		carl9170_async_regwrite(addr + 4 * i, word);
	}
	carl9170_async_regwrite_finish();

	dev_kfree_skb_any(cvif->beacon);
	cvif->beacon = NULL;

	err = carl9170_async_regwrite_result();
	if (!err)
		cvif->beacon = skb;
	spin_unlock_bh(&ar->beacon_lock);
	if (err)
		goto err_free;

	if (submit) {
		err = carl9170_bcn_ctrl(ar, cvif->id,
					CARL9170_BCN_CTRL_CAB_TRIGGER,
					addr, skb->len + FCS_LEN);

		if (err)
			goto err_free;
	}
out_unlock:
	rcu_read_unlock();
	return 0;

err_unlock:
	spin_unlock_bh(&ar->beacon_lock);

err_free:
	rcu_read_unlock();
	dev_kfree_skb_any(skb);
	return err;
}
