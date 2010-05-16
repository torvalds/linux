/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 */

/*
 * iwm Tx theory of operation:
 *
 * 1) We receive a 802.3 frame from the stack
 * 2) We convert it to a 802.11 frame [iwm_xmit_frame]
 * 3) We queue it to its corresponding tx queue [iwm_xmit_frame]
 * 4) We schedule the tx worker. There is one worker per tx
 *    queue. [iwm_xmit_frame]
 * 5) The tx worker is scheduled
 * 6) We go through every queued skb on the tx queue, and for each
 *    and every one of them: [iwm_tx_worker]
 *    a) We check if we have enough Tx credits (see below for a Tx
 *       credits description) for the frame length. [iwm_tx_worker]
 *    b) If we do, we aggregate the Tx frame into a UDMA one, by
 *       concatenating one REPLY_TX command per Tx frame. [iwm_tx_worker]
 *    c) When we run out of credits, or when we reach the maximum
 *       concatenation size, we actually send the concatenated UDMA
 *       frame. [iwm_tx_worker]
 *
 * When we run out of Tx credits, the skbs are filling the tx queue,
 * and eventually we will stop the netdev queue. [iwm_tx_worker]
 * The tx queue is emptied as we're getting new tx credits, by
 * scheduling the tx_worker. [iwm_tx_credit_inc]
 * The netdev queue is started again when we have enough tx credits,
 * and when our tx queue has some reasonable amout of space available
 * (i.e. half of the max size). [iwm_tx_worker]
 */

#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ieee80211.h>

#include "iwm.h"
#include "debug.h"
#include "commands.h"
#include "hal.h"
#include "umac.h"
#include "bus.h"

#define IWM_UMAC_PAGE_ALLOC_WRAP 0xffff

#define BYTES_TO_PAGES(n)	 (1 + ((n) >> ilog2(IWM_UMAC_PAGE_SIZE)) - \
				 (((n) & (IWM_UMAC_PAGE_SIZE - 1)) == 0))

#define pool_id_to_queue(id)	 ((id < IWM_TX_CMD_QUEUE) ? id : id - 1)
#define queue_to_pool_id(q)	 ((q < IWM_TX_CMD_QUEUE) ? q : q + 1)

/* require to hold tx_credit lock */
static int iwm_tx_credit_get(struct iwm_tx_credit *tx_credit, int id)
{
	struct pool_entry *pool = &tx_credit->pools[id];
	struct spool_entry *spool = &tx_credit->spools[pool->sid];
	int spool_pages;

	/* number of pages can be taken from spool by this pool */
	spool_pages = spool->max_pages - spool->alloc_pages +
		      max(pool->min_pages - pool->alloc_pages, 0);

	return min(pool->max_pages - pool->alloc_pages, spool_pages);
}

static bool iwm_tx_credit_ok(struct iwm_priv *iwm, int id, int nb)
{
	u32 npages = BYTES_TO_PAGES(nb);

	if (npages <= iwm_tx_credit_get(&iwm->tx_credit, id))
		return 1;

	set_bit(id, &iwm->tx_credit.full_pools_map);

	IWM_DBG_TX(iwm, DBG, "LINK: stop txq[%d], available credit: %d\n",
		   pool_id_to_queue(id),
		   iwm_tx_credit_get(&iwm->tx_credit, id));

	return 0;
}

void iwm_tx_credit_inc(struct iwm_priv *iwm, int id, int total_freed_pages)
{
	struct pool_entry *pool;
	struct spool_entry *spool;
	int freed_pages;
	int queue;

	BUG_ON(id >= IWM_MACS_OUT_GROUPS);

	pool = &iwm->tx_credit.pools[id];
	spool = &iwm->tx_credit.spools[pool->sid];

	freed_pages = total_freed_pages - pool->total_freed_pages;
	IWM_DBG_TX(iwm, DBG, "Free %d pages for pool[%d]\n", freed_pages, id);

	if (!freed_pages) {
		IWM_DBG_TX(iwm, DBG, "No pages are freed by UMAC\n");
		return;
	} else if (freed_pages < 0)
		freed_pages += IWM_UMAC_PAGE_ALLOC_WRAP + 1;

	if (pool->alloc_pages > pool->min_pages) {
		int spool_pages = pool->alloc_pages - pool->min_pages;
		spool_pages = min(spool_pages, freed_pages);
		spool->alloc_pages -= spool_pages;
	}

	pool->alloc_pages -= freed_pages;
	pool->total_freed_pages = total_freed_pages;

	IWM_DBG_TX(iwm, DBG, "Pool[%d] pages alloc: %d, total_freed: %d, "
		   "Spool[%d] pages alloc: %d\n", id, pool->alloc_pages,
		   pool->total_freed_pages, pool->sid, spool->alloc_pages);

	if (test_bit(id, &iwm->tx_credit.full_pools_map) &&
	    (pool->alloc_pages < pool->max_pages / 2)) {
		clear_bit(id, &iwm->tx_credit.full_pools_map);

		queue = pool_id_to_queue(id);

		IWM_DBG_TX(iwm, DBG, "LINK: start txq[%d], available "
			   "credit: %d\n", queue,
			   iwm_tx_credit_get(&iwm->tx_credit, id));
		queue_work(iwm->txq[queue].wq, &iwm->txq[queue].worker);
	}
}

static void iwm_tx_credit_dec(struct iwm_priv *iwm, int id, int alloc_pages)
{
	struct pool_entry *pool;
	struct spool_entry *spool;
	int spool_pages;

	IWM_DBG_TX(iwm, DBG, "Allocate %d pages for pool[%d]\n",
		   alloc_pages, id);

	BUG_ON(id >= IWM_MACS_OUT_GROUPS);

	pool = &iwm->tx_credit.pools[id];
	spool = &iwm->tx_credit.spools[pool->sid];

	spool_pages = pool->alloc_pages + alloc_pages - pool->min_pages;

	if (pool->alloc_pages >= pool->min_pages)
		spool->alloc_pages += alloc_pages;
	else if (spool_pages > 0)
		spool->alloc_pages += spool_pages;

	pool->alloc_pages += alloc_pages;

	IWM_DBG_TX(iwm, DBG, "Pool[%d] pages alloc: %d, total_freed: %d, "
		   "Spool[%d] pages alloc: %d\n", id, pool->alloc_pages,
		   pool->total_freed_pages, pool->sid, spool->alloc_pages);
}

int iwm_tx_credit_alloc(struct iwm_priv *iwm, int id, int nb)
{
	u32 npages = BYTES_TO_PAGES(nb);
	int ret = 0;

	spin_lock(&iwm->tx_credit.lock);

	if (!iwm_tx_credit_ok(iwm, id, nb)) {
		IWM_DBG_TX(iwm, DBG, "No credit avaliable for pool[%d]\n", id);
		ret = -ENOSPC;
		goto out;
	}

	iwm_tx_credit_dec(iwm, id, npages);

 out:
	spin_unlock(&iwm->tx_credit.lock);
	return ret;
}

/*
 * Since we're on an SDIO or USB bus, we are not sharing memory
 * for storing to be transmitted frames. The host needs to push
 * them upstream. As a consequence there needs to be a way for
 * the target to let us know if it can actually take more TX frames
 * or not. This is what Tx credits are for.
 *
 * For each Tx HW queue, we have a Tx pool, and then we have one
 * unique super pool (spool), which is actually a global pool of
 * all the UMAC pages.
 * For each Tx pool we have a min_pages, a max_pages fields, and a
 * alloc_pages fields. The alloc_pages tracks the number of pages
 * currently allocated from the tx pool.
 * Here are the rules to check if given a tx frame we have enough
 * tx credits for it:
 * 1) We translate the frame length into a number of UMAC pages.
 *    Let's call them n_pages.
 * 2) For the corresponding tx pool, we check if n_pages +
 *    pool->alloc_pages is higher than pool->min_pages. min_pages
 *    represent a set of pre-allocated pages on the tx pool. If
 *    that's the case, then we need to allocate those pages from
 *    the spool. We can do so until we reach spool->max_pages.
 * 3) Each tx pool is not allowed to allocate more than pool->max_pages
 *    from the spool, so once we're over min_pages, we can allocate
 *    pages from the spool, but not more than max_pages.
 *
 * When the tx code path needs to send a tx frame, it checks first
 * if it has enough tx credits, following those rules. [iwm_tx_credit_get]
 * If it does, it then updates the pool and spool counters and
 * then send the frame. [iwm_tx_credit_alloc and iwm_tx_credit_dec]
 * On the other side, when the UMAC is done transmitting frames, it
 * will send a credit update notification to the host. This is when
 * the pool and spool counters gets to be decreased. [iwm_tx_credit_inc,
 * called from rx.c:iwm_ntf_tx_credit_update]
 *
 */
void iwm_tx_credit_init_pools(struct iwm_priv *iwm,
			      struct iwm_umac_notif_alive *alive)
{
	int i, sid, pool_pages;

	spin_lock(&iwm->tx_credit.lock);

	iwm->tx_credit.pool_nr = le16_to_cpu(alive->page_grp_count);
	iwm->tx_credit.full_pools_map = 0;
	memset(&iwm->tx_credit.spools[0], 0, sizeof(struct spool_entry));

	IWM_DBG_TX(iwm, DBG, "Pools number is %d\n", iwm->tx_credit.pool_nr);

	for (i = 0; i < iwm->tx_credit.pool_nr; i++) {
		__le32 page_grp_state = alive->page_grp_state[i];

		iwm->tx_credit.pools[i].id = GET_VAL32(page_grp_state,
				UMAC_ALIVE_PAGE_STS_GRP_NUM);
		iwm->tx_credit.pools[i].sid = GET_VAL32(page_grp_state,
				UMAC_ALIVE_PAGE_STS_SGRP_NUM);
		iwm->tx_credit.pools[i].min_pages = GET_VAL32(page_grp_state,
				UMAC_ALIVE_PAGE_STS_GRP_MIN_SIZE);
		iwm->tx_credit.pools[i].max_pages = GET_VAL32(page_grp_state,
				UMAC_ALIVE_PAGE_STS_GRP_MAX_SIZE);
		iwm->tx_credit.pools[i].alloc_pages = 0;
		iwm->tx_credit.pools[i].total_freed_pages = 0;

		sid = iwm->tx_credit.pools[i].sid;
		pool_pages = iwm->tx_credit.pools[i].min_pages;

		if (iwm->tx_credit.spools[sid].max_pages == 0) {
			iwm->tx_credit.spools[sid].id = sid;
			iwm->tx_credit.spools[sid].max_pages =
				GET_VAL32(page_grp_state,
					  UMAC_ALIVE_PAGE_STS_SGRP_MAX_SIZE);
			iwm->tx_credit.spools[sid].alloc_pages = 0;
		}

		iwm->tx_credit.spools[sid].alloc_pages += pool_pages;

		IWM_DBG_TX(iwm, DBG, "Pool idx: %d, id: %d, sid: %d, capacity "
			   "min: %d, max: %d, pool alloc: %d, total_free: %d, "
			   "super poll alloc: %d\n",
			   i, iwm->tx_credit.pools[i].id,
			   iwm->tx_credit.pools[i].sid,
			   iwm->tx_credit.pools[i].min_pages,
			   iwm->tx_credit.pools[i].max_pages,
			   iwm->tx_credit.pools[i].alloc_pages,
			   iwm->tx_credit.pools[i].total_freed_pages,
			   iwm->tx_credit.spools[sid].alloc_pages);
	}

	spin_unlock(&iwm->tx_credit.lock);
}

#define IWM_UDMA_HDR_LEN	sizeof(struct iwm_umac_wifi_out_hdr)

static int iwm_tx_build_packet(struct iwm_priv *iwm, struct sk_buff *skb,
			       int pool_id, u8 *buf)
{
	struct iwm_umac_wifi_out_hdr *hdr = (struct iwm_umac_wifi_out_hdr *)buf;
	struct iwm_udma_wifi_cmd udma_cmd;
	struct iwm_umac_cmd umac_cmd;
	struct iwm_tx_info *tx_info = skb_to_tx_info(skb);

	udma_cmd.count = cpu_to_le16(skb->len +
				     sizeof(struct iwm_umac_fw_cmd_hdr));
	/* set EOP to 0 here. iwm_udma_wifi_hdr_set_eop() will be
	 * called later to set EOP for the last packet. */
	udma_cmd.eop = 0;
	udma_cmd.credit_group = pool_id;
	udma_cmd.ra_tid = tx_info->sta << 4 | tx_info->tid;
	udma_cmd.lmac_offset = 0;

	umac_cmd.id = REPLY_TX;
	umac_cmd.count = cpu_to_le16(skb->len);
	umac_cmd.color = tx_info->color;
	umac_cmd.resp = 0;
	umac_cmd.seq_num = cpu_to_le16(iwm_alloc_wifi_cmd_seq(iwm));

	iwm_build_udma_wifi_hdr(iwm, &hdr->hw_hdr, &udma_cmd);
	iwm_build_umac_hdr(iwm, &hdr->sw_hdr, &umac_cmd);

	memcpy(buf + sizeof(*hdr), skb->data, skb->len);

	return umac_cmd.seq_num;
}

static int iwm_tx_send_concat_packets(struct iwm_priv *iwm,
				      struct iwm_tx_queue *txq)
{
	int ret;

	if (!txq->concat_count)
		return 0;

	IWM_DBG_TX(iwm, DBG, "Send concatenated Tx: queue %d, %d bytes\n",
		   txq->id, txq->concat_count);

	/* mark EOP for the last packet */
	iwm_udma_wifi_hdr_set_eop(iwm, txq->concat_ptr, 1);

	ret = iwm_bus_send_chunk(iwm, txq->concat_buf, txq->concat_count);

	txq->concat_count = 0;
	txq->concat_ptr = txq->concat_buf;

	return ret;
}

void iwm_tx_worker(struct work_struct *work)
{
	struct iwm_priv *iwm;
	struct iwm_tx_info *tx_info = NULL;
	struct sk_buff *skb;
	struct iwm_tx_queue *txq;
	struct iwm_sta_info *sta_info;
	struct iwm_tid_info *tid_info;
	int cmdlen, ret, pool_id;

	txq = container_of(work, struct iwm_tx_queue, worker);
	iwm = container_of(txq, struct iwm_priv, txq[txq->id]);

	pool_id = queue_to_pool_id(txq->id);

	while (!test_bit(pool_id, &iwm->tx_credit.full_pools_map) &&
	       !skb_queue_empty(&txq->queue)) {

		spin_lock_bh(&txq->lock);
		skb = skb_dequeue(&txq->queue);
		spin_unlock_bh(&txq->lock);

		tx_info = skb_to_tx_info(skb);
		sta_info = &iwm->sta_table[tx_info->sta];
		if (!sta_info->valid) {
			IWM_ERR(iwm, "Trying to send a frame to unknown STA\n");
			kfree_skb(skb);
			continue;
		}

		tid_info = &sta_info->tid_info[tx_info->tid];

		mutex_lock(&tid_info->mutex);

		/*
		 * If the RAxTID is stopped, we queue the skb to the stopped
		 * queue.
		 * Whenever we'll get a UMAC notification to resume the tx flow
		 * for this RAxTID, we'll merge back the stopped queue into the
		 * regular queue. See iwm_ntf_stop_resume_tx() from rx.c.
		 */
		if (tid_info->stopped) {
			IWM_DBG_TX(iwm, DBG, "%dx%d stopped\n",
				   tx_info->sta, tx_info->tid);
			spin_lock_bh(&txq->lock);
			skb_queue_tail(&txq->stopped_queue, skb);
			spin_unlock_bh(&txq->lock);

			mutex_unlock(&tid_info->mutex);
			continue;
		}

		cmdlen = IWM_UDMA_HDR_LEN + skb->len;

		IWM_DBG_TX(iwm, DBG, "Tx frame on queue %d: skb: 0x%p, sta: "
			   "%d, color: %d\n", txq->id, skb, tx_info->sta,
			   tx_info->color);

		if (txq->concat_count + cmdlen > IWM_HAL_CONCATENATE_BUF_SIZE)
			iwm_tx_send_concat_packets(iwm, txq);

		ret = iwm_tx_credit_alloc(iwm, pool_id, cmdlen);
		if (ret) {
			IWM_DBG_TX(iwm, DBG, "not enough tx_credit for queue "
				   "%d, Tx worker stopped\n", txq->id);
			spin_lock_bh(&txq->lock);
			skb_queue_head(&txq->queue, skb);
			spin_unlock_bh(&txq->lock);

			mutex_unlock(&tid_info->mutex);
			break;
		}

		txq->concat_ptr = txq->concat_buf + txq->concat_count;
		tid_info->last_seq_num =
			iwm_tx_build_packet(iwm, skb, pool_id, txq->concat_ptr);
		txq->concat_count += ALIGN(cmdlen, 16);

		mutex_unlock(&tid_info->mutex);

		kfree_skb(skb);
	}

	iwm_tx_send_concat_packets(iwm, txq);

	if (__netif_subqueue_stopped(iwm_to_ndev(iwm), txq->id) &&
	    !test_bit(pool_id, &iwm->tx_credit.full_pools_map) &&
	    (skb_queue_len(&txq->queue) < IWM_TX_LIST_SIZE / 2)) {
		IWM_DBG_TX(iwm, DBG, "LINK: start netif_subqueue[%d]", txq->id);
		netif_wake_subqueue(iwm_to_ndev(iwm), txq->id);
	}
}

int iwm_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct iwm_priv *iwm = ndev_to_iwm(netdev);
	struct net_device *ndev = iwm_to_ndev(iwm);
	struct wireless_dev *wdev = iwm_to_wdev(iwm);
	struct iwm_tx_info *tx_info;
	struct iwm_tx_queue *txq;
	struct iwm_sta_info *sta_info;
	u8 *dst_addr, sta_id;
	u16 queue;
	int ret;


	if (!test_bit(IWM_STATUS_ASSOCIATED, &iwm->status)) {
		IWM_DBG_TX(iwm, DBG, "LINK: stop netif_all_queues: "
			   "not associated\n");
		netif_tx_stop_all_queues(netdev);
		goto drop;
	}

	queue = skb_get_queue_mapping(skb);
	BUG_ON(queue >= IWM_TX_DATA_QUEUES); /* no iPAN yet */

	txq = &iwm->txq[queue];

	/* No free space for Tx, tx_worker is too slow */
	if ((skb_queue_len(&txq->queue) > IWM_TX_LIST_SIZE) ||
	    (skb_queue_len(&txq->stopped_queue) > IWM_TX_LIST_SIZE)) {
		IWM_DBG_TX(iwm, DBG, "LINK: stop netif_subqueue[%d]\n", queue);
		netif_stop_subqueue(netdev, queue);
		return NETDEV_TX_BUSY;
	}

	ret = ieee80211_data_from_8023(skb, netdev->dev_addr, wdev->iftype,
				       iwm->bssid, 0);
	if (ret) {
		IWM_ERR(iwm, "build wifi header failed\n");
		goto drop;
	}

	dst_addr = ((struct ieee80211_hdr *)(skb->data))->addr1;

	for (sta_id = 0; sta_id < IWM_STA_TABLE_NUM; sta_id++) {
		sta_info = &iwm->sta_table[sta_id];
		if (sta_info->valid &&
		    !memcmp(dst_addr, sta_info->addr, ETH_ALEN))
			break;
	}

	if (sta_id == IWM_STA_TABLE_NUM) {
		IWM_ERR(iwm, "STA %pM not found in sta_table, Tx ignored\n",
			dst_addr);
		goto drop;
	}

	tx_info = skb_to_tx_info(skb);
	tx_info->sta = sta_id;
	tx_info->color = sta_info->color;
	/* UMAC uses TID 8 (vs. 0) for non QoS packets */
	if (sta_info->qos)
		tx_info->tid = skb->priority;
	else
		tx_info->tid = IWM_UMAC_MGMT_TID;

	spin_lock_bh(&iwm->txq[queue].lock);
	skb_queue_tail(&iwm->txq[queue].queue, skb);
	spin_unlock_bh(&iwm->txq[queue].lock);

	queue_work(iwm->txq[queue].wq, &iwm->txq[queue].worker);

	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += skb->len;
	return NETDEV_TX_OK;

 drop:
	ndev->stats.tx_dropped++;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}
