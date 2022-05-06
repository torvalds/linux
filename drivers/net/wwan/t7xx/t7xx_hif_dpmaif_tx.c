// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *
 * Contributors:
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/minmax.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "t7xx_dpmaif.h"
#include "t7xx_hif_dpmaif.h"
#include "t7xx_hif_dpmaif_tx.h"
#include "t7xx_pci.h"

#define DPMAIF_SKB_TX_BURST_CNT	5
#define DPMAIF_DRB_LIST_LEN	6144

/* DRB dtype */
#define DES_DTYP_PD		0
#define DES_DTYP_MSG		1

static unsigned int t7xx_dpmaif_update_drb_rd_idx(struct dpmaif_ctrl *dpmaif_ctrl,
						  unsigned int q_num)
{
	struct dpmaif_tx_queue *txq = &dpmaif_ctrl->txq[q_num];
	unsigned int old_sw_rd_idx, new_hw_rd_idx, drb_cnt;
	unsigned long flags;

	if (!txq->que_started)
		return 0;

	old_sw_rd_idx = txq->drb_rd_idx;
	new_hw_rd_idx = t7xx_dpmaif_ul_get_rd_idx(&dpmaif_ctrl->hw_info, q_num);
	if (new_hw_rd_idx >= DPMAIF_DRB_LIST_LEN) {
		dev_err(dpmaif_ctrl->dev, "Out of range read index: %u\n", new_hw_rd_idx);
		return 0;
	}

	if (old_sw_rd_idx <= new_hw_rd_idx)
		drb_cnt = new_hw_rd_idx - old_sw_rd_idx;
	else
		drb_cnt = txq->drb_size_cnt - old_sw_rd_idx + new_hw_rd_idx;

	spin_lock_irqsave(&txq->tx_lock, flags);
	txq->drb_rd_idx = new_hw_rd_idx;
	spin_unlock_irqrestore(&txq->tx_lock, flags);

	return drb_cnt;
}

static unsigned int t7xx_dpmaif_release_tx_buffer(struct dpmaif_ctrl *dpmaif_ctrl,
						  unsigned int q_num, unsigned int release_cnt)
{
	struct dpmaif_tx_queue *txq = &dpmaif_ctrl->txq[q_num];
	struct dpmaif_callbacks *cb = dpmaif_ctrl->callbacks;
	struct dpmaif_drb_skb *cur_drb_skb, *drb_skb_base;
	struct dpmaif_drb *cur_drb, *drb_base;
	unsigned int drb_cnt, i, cur_idx;
	unsigned long flags;

	drb_skb_base = txq->drb_skb_base;
	drb_base = txq->drb_base;

	spin_lock_irqsave(&txq->tx_lock, flags);
	drb_cnt = txq->drb_size_cnt;
	cur_idx = txq->drb_release_rd_idx;
	spin_unlock_irqrestore(&txq->tx_lock, flags);

	for (i = 0; i < release_cnt; i++) {
		cur_drb = drb_base + cur_idx;
		if (FIELD_GET(DRB_HDR_DTYP, le32_to_cpu(cur_drb->header)) == DES_DTYP_PD) {
			cur_drb_skb = drb_skb_base + cur_idx;
			if (!cur_drb_skb->is_msg)
				dma_unmap_single(dpmaif_ctrl->dev, cur_drb_skb->bus_addr,
						 cur_drb_skb->data_len, DMA_TO_DEVICE);

			if (!FIELD_GET(DRB_HDR_CONT, le32_to_cpu(cur_drb->header))) {
				if (!cur_drb_skb->skb) {
					dev_err(dpmaif_ctrl->dev,
						"txq%u: DRB check fail, invalid skb\n", q_num);
					continue;
				}

				dev_kfree_skb_any(cur_drb_skb->skb);
			}

			cur_drb_skb->skb = NULL;
		}

		spin_lock_irqsave(&txq->tx_lock, flags);
		cur_idx = t7xx_ring_buf_get_next_wr_idx(drb_cnt, cur_idx);
		txq->drb_release_rd_idx = cur_idx;
		spin_unlock_irqrestore(&txq->tx_lock, flags);

		if (atomic_inc_return(&txq->tx_budget) > txq->drb_size_cnt / 8)
			cb->state_notify(dpmaif_ctrl->t7xx_dev, DMPAIF_TXQ_STATE_IRQ, txq->index);
	}

	if (FIELD_GET(DRB_HDR_CONT, le32_to_cpu(cur_drb->header)))
		dev_err(dpmaif_ctrl->dev, "txq%u: DRB not marked as the last one\n", q_num);

	return i;
}

static int t7xx_dpmaif_tx_release(struct dpmaif_ctrl *dpmaif_ctrl,
				  unsigned int q_num, unsigned int budget)
{
	struct dpmaif_tx_queue *txq = &dpmaif_ctrl->txq[q_num];
	unsigned int rel_cnt, real_rel_cnt;

	/* Update read index from HW */
	t7xx_dpmaif_update_drb_rd_idx(dpmaif_ctrl, q_num);

	rel_cnt = t7xx_ring_buf_rd_wr_count(txq->drb_size_cnt, txq->drb_release_rd_idx,
					    txq->drb_rd_idx, DPMAIF_READ);

	real_rel_cnt = min_not_zero(budget, rel_cnt);
	if (real_rel_cnt)
		real_rel_cnt = t7xx_dpmaif_release_tx_buffer(dpmaif_ctrl, q_num, real_rel_cnt);

	return real_rel_cnt < rel_cnt ? -EAGAIN : 0;
}

static bool t7xx_dpmaif_drb_ring_not_empty(struct dpmaif_tx_queue *txq)
{
	return !!t7xx_dpmaif_update_drb_rd_idx(txq->dpmaif_ctrl, txq->index);
}

static void t7xx_dpmaif_tx_done(struct work_struct *work)
{
	struct dpmaif_tx_queue *txq = container_of(work, struct dpmaif_tx_queue, dpmaif_tx_work);
	struct dpmaif_ctrl *dpmaif_ctrl = txq->dpmaif_ctrl;
	struct dpmaif_hw_info *hw_info;
	int ret;

	hw_info = &dpmaif_ctrl->hw_info;
	ret = t7xx_dpmaif_tx_release(dpmaif_ctrl, txq->index, txq->drb_size_cnt);
	if (ret == -EAGAIN ||
	    (t7xx_dpmaif_ul_clr_done(hw_info, txq->index) &&
	     t7xx_dpmaif_drb_ring_not_empty(txq))) {
		queue_work(dpmaif_ctrl->txq[txq->index].worker,
			   &dpmaif_ctrl->txq[txq->index].dpmaif_tx_work);
		/* Give the device time to enter the low power state */
		t7xx_dpmaif_clr_ip_busy_sts(hw_info);
	} else {
		t7xx_dpmaif_clr_ip_busy_sts(hw_info);
		t7xx_dpmaif_unmask_ulq_intr(hw_info, txq->index);
	}
}

static void t7xx_setup_msg_drb(struct dpmaif_ctrl *dpmaif_ctrl, unsigned int q_num,
			       unsigned int cur_idx, unsigned int pkt_len, unsigned int count_l,
			       unsigned int channel_id)
{
	struct dpmaif_drb *drb_base = dpmaif_ctrl->txq[q_num].drb_base;
	struct dpmaif_drb *drb = drb_base + cur_idx;

	drb->header = cpu_to_le32(FIELD_PREP(DRB_HDR_DTYP, DES_DTYP_MSG) |
				  FIELD_PREP(DRB_HDR_CONT, 1) |
				  FIELD_PREP(DRB_HDR_DATA_LEN, pkt_len));

	drb->msg.msg_hdr = cpu_to_le32(FIELD_PREP(DRB_MSG_COUNT_L, count_l) |
				       FIELD_PREP(DRB_MSG_CHANNEL_ID, channel_id) |
				       FIELD_PREP(DRB_MSG_L4_CHK, 1));
}

static void t7xx_setup_payload_drb(struct dpmaif_ctrl *dpmaif_ctrl, unsigned int q_num,
				   unsigned int cur_idx, dma_addr_t data_addr,
				   unsigned int pkt_size, bool last_one)
{
	struct dpmaif_drb *drb_base = dpmaif_ctrl->txq[q_num].drb_base;
	struct dpmaif_drb *drb = drb_base + cur_idx;
	u32 header;

	header = FIELD_PREP(DRB_HDR_DTYP, DES_DTYP_PD) | FIELD_PREP(DRB_HDR_DATA_LEN, pkt_size);
	if (!last_one)
		header |= FIELD_PREP(DRB_HDR_CONT, 1);

	drb->header = cpu_to_le32(header);
	drb->pd.data_addr_l = cpu_to_le32(lower_32_bits(data_addr));
	drb->pd.data_addr_h = cpu_to_le32(upper_32_bits(data_addr));
}

static void t7xx_record_drb_skb(struct dpmaif_ctrl *dpmaif_ctrl, unsigned int q_num,
				unsigned int cur_idx, struct sk_buff *skb, bool is_msg,
				bool is_frag, bool is_last_one, dma_addr_t bus_addr,
				unsigned int data_len)
{
	struct dpmaif_drb_skb *drb_skb_base = dpmaif_ctrl->txq[q_num].drb_skb_base;
	struct dpmaif_drb_skb *drb_skb = drb_skb_base + cur_idx;

	drb_skb->skb = skb;
	drb_skb->bus_addr = bus_addr;
	drb_skb->data_len = data_len;
	drb_skb->index = cur_idx;
	drb_skb->is_msg = is_msg;
	drb_skb->is_frag = is_frag;
	drb_skb->is_last = is_last_one;
}

static int t7xx_dpmaif_add_skb_to_ring(struct dpmaif_ctrl *dpmaif_ctrl, struct sk_buff *skb)
{
	struct dpmaif_callbacks *cb = dpmaif_ctrl->callbacks;
	unsigned int wr_cnt, send_cnt, payload_cnt;
	unsigned int cur_idx, drb_wr_idx_backup;
	struct skb_shared_info *shinfo;
	struct dpmaif_tx_queue *txq;
	struct t7xx_skb_cb *skb_cb;
	unsigned long flags;

	skb_cb = T7XX_SKB_CB(skb);
	txq = &dpmaif_ctrl->txq[skb_cb->txq_number];
	if (!txq->que_started || dpmaif_ctrl->state != DPMAIF_STATE_PWRON)
		return -ENODEV;

	atomic_set(&txq->tx_processing, 1);
	 /* Ensure tx_processing is changed to 1 before actually begin TX flow */
	smp_mb();

	shinfo = skb_shinfo(skb);
	if (shinfo->frag_list)
		dev_warn_ratelimited(dpmaif_ctrl->dev, "frag_list not supported\n");

	payload_cnt = shinfo->nr_frags + 1;
	/* nr_frags: frag cnt, 1: skb->data, 1: msg DRB */
	send_cnt = payload_cnt + 1;

	spin_lock_irqsave(&txq->tx_lock, flags);
	cur_idx = txq->drb_wr_idx;
	drb_wr_idx_backup = cur_idx;
	txq->drb_wr_idx += send_cnt;
	if (txq->drb_wr_idx >= txq->drb_size_cnt)
		txq->drb_wr_idx -= txq->drb_size_cnt;
	t7xx_setup_msg_drb(dpmaif_ctrl, txq->index, cur_idx, skb->len, 0, skb_cb->netif_idx);
	t7xx_record_drb_skb(dpmaif_ctrl, txq->index, cur_idx, skb, true, 0, 0, 0, 0);
	spin_unlock_irqrestore(&txq->tx_lock, flags);

	for (wr_cnt = 0; wr_cnt < payload_cnt; wr_cnt++) {
		bool is_frag, is_last_one = wr_cnt == payload_cnt - 1;
		unsigned int data_len;
		dma_addr_t bus_addr;
		void *data_addr;

		if (!wr_cnt) {
			data_len = skb_headlen(skb);
			data_addr = skb->data;
			is_frag = false;
		} else {
			skb_frag_t *frag = shinfo->frags + wr_cnt - 1;

			data_len = skb_frag_size(frag);
			data_addr = skb_frag_address(frag);
			is_frag = true;
		}

		bus_addr = dma_map_single(dpmaif_ctrl->dev, data_addr, data_len, DMA_TO_DEVICE);
		if (dma_mapping_error(dpmaif_ctrl->dev, bus_addr))
			goto unmap_buffers;

		cur_idx = t7xx_ring_buf_get_next_wr_idx(txq->drb_size_cnt, cur_idx);

		spin_lock_irqsave(&txq->tx_lock, flags);
		t7xx_setup_payload_drb(dpmaif_ctrl, txq->index, cur_idx, bus_addr, data_len,
				       is_last_one);
		t7xx_record_drb_skb(dpmaif_ctrl, txq->index, cur_idx, skb, false, is_frag,
				    is_last_one, bus_addr, data_len);
		spin_unlock_irqrestore(&txq->tx_lock, flags);
	}

	if (atomic_sub_return(send_cnt, &txq->tx_budget) <= (MAX_SKB_FRAGS + 2))
		cb->state_notify(dpmaif_ctrl->t7xx_dev, DMPAIF_TXQ_STATE_FULL, txq->index);

	atomic_set(&txq->tx_processing, 0);

	return 0;

unmap_buffers:
	while (wr_cnt--) {
		struct dpmaif_drb_skb *drb_skb = txq->drb_skb_base;

		cur_idx = cur_idx ? cur_idx - 1 : txq->drb_size_cnt - 1;
		drb_skb += cur_idx;
		dma_unmap_single(dpmaif_ctrl->dev, drb_skb->bus_addr,
				 drb_skb->data_len, DMA_TO_DEVICE);
	}

	txq->drb_wr_idx = drb_wr_idx_backup;
	atomic_set(&txq->tx_processing, 0);

	return -ENOMEM;
}

static bool t7xx_tx_lists_are_all_empty(const struct dpmaif_ctrl *dpmaif_ctrl)
{
	int i;

	for (i = 0; i < DPMAIF_TXQ_NUM; i++) {
		if (!skb_queue_empty(&dpmaif_ctrl->txq[i].tx_skb_head))
			return false;
	}

	return true;
}

/* Currently, only the default TX queue is used */
static struct dpmaif_tx_queue *t7xx_select_tx_queue(struct dpmaif_ctrl *dpmaif_ctrl)
{
	struct dpmaif_tx_queue *txq;

	txq = &dpmaif_ctrl->txq[DPMAIF_TX_DEFAULT_QUEUE];
	if (!txq->que_started)
		return NULL;

	return txq;
}

static unsigned int t7xx_txq_drb_wr_available(struct dpmaif_tx_queue *txq)
{
	return t7xx_ring_buf_rd_wr_count(txq->drb_size_cnt, txq->drb_release_rd_idx,
					 txq->drb_wr_idx, DPMAIF_WRITE);
}

static unsigned int t7xx_skb_drb_cnt(struct sk_buff *skb)
{
	/* Normal DRB (frags data + skb linear data) + msg DRB */
	return skb_shinfo(skb)->nr_frags + 2;
}

static int t7xx_txq_burst_send_skb(struct dpmaif_tx_queue *txq)
{
	unsigned int drb_remain_cnt, i;
	unsigned int send_drb_cnt;
	int drb_cnt = 0;
	int ret = 0;

	drb_remain_cnt = t7xx_txq_drb_wr_available(txq);

	for (i = 0; i < DPMAIF_SKB_TX_BURST_CNT; i++) {
		struct sk_buff *skb;

		skb = skb_peek(&txq->tx_skb_head);
		if (!skb)
			break;

		send_drb_cnt = t7xx_skb_drb_cnt(skb);
		if (drb_remain_cnt < send_drb_cnt) {
			drb_remain_cnt = t7xx_txq_drb_wr_available(txq);
			continue;
		}

		drb_remain_cnt -= send_drb_cnt;

		ret = t7xx_dpmaif_add_skb_to_ring(txq->dpmaif_ctrl, skb);
		if (ret < 0) {
			dev_err(txq->dpmaif_ctrl->dev,
				"Failed to add skb to device's ring: %d\n", ret);
			break;
		}

		drb_cnt += send_drb_cnt;
		skb_unlink(skb, &txq->tx_skb_head);
	}

	if (drb_cnt > 0)
		return drb_cnt;

	return ret;
}

static void t7xx_do_tx_hw_push(struct dpmaif_ctrl *dpmaif_ctrl)
{
	do {
		struct dpmaif_tx_queue *txq;
		int drb_send_cnt;

		txq = t7xx_select_tx_queue(dpmaif_ctrl);
		if (!txq)
			return;

		drb_send_cnt = t7xx_txq_burst_send_skb(txq);
		if (drb_send_cnt <= 0) {
			usleep_range(10, 20);
			cond_resched();
			continue;
		}

		t7xx_dpmaif_ul_update_hw_drb_cnt(&dpmaif_ctrl->hw_info, txq->index,
						 drb_send_cnt * DPMAIF_UL_DRB_SIZE_WORD);

		cond_resched();
	} while (!t7xx_tx_lists_are_all_empty(dpmaif_ctrl) && !kthread_should_stop() &&
		 (dpmaif_ctrl->state == DPMAIF_STATE_PWRON));
}

static int t7xx_dpmaif_tx_hw_push_thread(void *arg)
{
	struct dpmaif_ctrl *dpmaif_ctrl = arg;

	while (!kthread_should_stop()) {
		if (t7xx_tx_lists_are_all_empty(dpmaif_ctrl) ||
		    dpmaif_ctrl->state != DPMAIF_STATE_PWRON) {
			if (wait_event_interruptible(dpmaif_ctrl->tx_wq,
						     (!t7xx_tx_lists_are_all_empty(dpmaif_ctrl) &&
						     dpmaif_ctrl->state == DPMAIF_STATE_PWRON) ||
						     kthread_should_stop()))
				continue;

			if (kthread_should_stop())
				break;
		}

		t7xx_do_tx_hw_push(dpmaif_ctrl);
	}

	return 0;
}

int t7xx_dpmaif_tx_thread_init(struct dpmaif_ctrl *dpmaif_ctrl)
{
	init_waitqueue_head(&dpmaif_ctrl->tx_wq);
	dpmaif_ctrl->tx_thread = kthread_run(t7xx_dpmaif_tx_hw_push_thread,
					     dpmaif_ctrl, "dpmaif_tx_hw_push");
	return PTR_ERR_OR_ZERO(dpmaif_ctrl->tx_thread);
}

void t7xx_dpmaif_tx_thread_rel(struct dpmaif_ctrl *dpmaif_ctrl)
{
	if (dpmaif_ctrl->tx_thread)
		kthread_stop(dpmaif_ctrl->tx_thread);
}

/**
 * t7xx_dpmaif_tx_send_skb() - Add skb to the transmit queue.
 * @dpmaif_ctrl: Pointer to struct dpmaif_ctrl.
 * @txq_number: Queue number to xmit on.
 * @skb: Pointer to the skb to transmit.
 *
 * Add the skb to the queue of the skbs to be transmit.
 * Wake up the thread that push the skbs from the queue to the HW.
 *
 * Return:
 * * 0		- Success.
 * * -EBUSY	- Tx budget exhausted.
 *		  In normal circumstances t7xx_dpmaif_add_skb_to_ring() must report the txq full
 *		  state to prevent this error condition.
 */
int t7xx_dpmaif_tx_send_skb(struct dpmaif_ctrl *dpmaif_ctrl, unsigned int txq_number,
			    struct sk_buff *skb)
{
	struct dpmaif_tx_queue *txq = &dpmaif_ctrl->txq[txq_number];
	struct dpmaif_callbacks *cb = dpmaif_ctrl->callbacks;
	struct t7xx_skb_cb *skb_cb;

	if (atomic_read(&txq->tx_budget) <= t7xx_skb_drb_cnt(skb)) {
		cb->state_notify(dpmaif_ctrl->t7xx_dev, DMPAIF_TXQ_STATE_FULL, txq_number);
		return -EBUSY;
	}

	skb_cb = T7XX_SKB_CB(skb);
	skb_cb->txq_number = txq_number;
	skb_queue_tail(&txq->tx_skb_head, skb);
	wake_up(&dpmaif_ctrl->tx_wq);

	return 0;
}

void t7xx_dpmaif_irq_tx_done(struct dpmaif_ctrl *dpmaif_ctrl, unsigned int que_mask)
{
	int i;

	for (i = 0; i < DPMAIF_TXQ_NUM; i++) {
		if (que_mask & BIT(i))
			queue_work(dpmaif_ctrl->txq[i].worker, &dpmaif_ctrl->txq[i].dpmaif_tx_work);
	}
}

static int t7xx_dpmaif_tx_drb_buf_init(struct dpmaif_tx_queue *txq)
{
	size_t brb_skb_size, brb_pd_size;

	brb_pd_size = DPMAIF_DRB_LIST_LEN * sizeof(struct dpmaif_drb);
	brb_skb_size = DPMAIF_DRB_LIST_LEN * sizeof(struct dpmaif_drb_skb);

	txq->drb_size_cnt = DPMAIF_DRB_LIST_LEN;

	/* For HW && AP SW */
	txq->drb_base = dma_alloc_coherent(txq->dpmaif_ctrl->dev, brb_pd_size,
					   &txq->drb_bus_addr, GFP_KERNEL | __GFP_ZERO);
	if (!txq->drb_base)
		return -ENOMEM;

	/* For AP SW to record the skb information */
	txq->drb_skb_base = devm_kzalloc(txq->dpmaif_ctrl->dev, brb_skb_size, GFP_KERNEL);
	if (!txq->drb_skb_base) {
		dma_free_coherent(txq->dpmaif_ctrl->dev, brb_pd_size,
				  txq->drb_base, txq->drb_bus_addr);
		return -ENOMEM;
	}

	return 0;
}

static void t7xx_dpmaif_tx_free_drb_skb(struct dpmaif_tx_queue *txq)
{
	struct dpmaif_drb_skb *drb_skb, *drb_skb_base = txq->drb_skb_base;
	unsigned int i;

	if (!drb_skb_base)
		return;

	for (i = 0; i < txq->drb_size_cnt; i++) {
		drb_skb = drb_skb_base + i;
		if (!drb_skb->skb)
			continue;

		if (!drb_skb->is_msg)
			dma_unmap_single(txq->dpmaif_ctrl->dev, drb_skb->bus_addr,
					 drb_skb->data_len, DMA_TO_DEVICE);

		if (drb_skb->is_last) {
			dev_kfree_skb(drb_skb->skb);
			drb_skb->skb = NULL;
		}
	}
}

static void t7xx_dpmaif_tx_drb_buf_rel(struct dpmaif_tx_queue *txq)
{
	if (txq->drb_base)
		dma_free_coherent(txq->dpmaif_ctrl->dev,
				  txq->drb_size_cnt * sizeof(struct dpmaif_drb),
				  txq->drb_base, txq->drb_bus_addr);

	t7xx_dpmaif_tx_free_drb_skb(txq);
}

/**
 * t7xx_dpmaif_txq_init() - Initialize TX queue.
 * @txq: Pointer to struct dpmaif_tx_queue.
 *
 * Initialize the TX queue data structure and allocate memory for it to use.
 *
 * Return:
 * * 0		- Success.
 * * -ERROR	- Error code from failure sub-initializations.
 */
int t7xx_dpmaif_txq_init(struct dpmaif_tx_queue *txq)
{
	int ret;

	skb_queue_head_init(&txq->tx_skb_head);
	init_waitqueue_head(&txq->req_wq);
	atomic_set(&txq->tx_budget, DPMAIF_DRB_LIST_LEN);

	ret = t7xx_dpmaif_tx_drb_buf_init(txq);
	if (ret) {
		dev_err(txq->dpmaif_ctrl->dev, "Failed to initialize DRB buffers: %d\n", ret);
		return ret;
	}

	txq->worker = alloc_workqueue("md_dpmaif_tx%d_worker", WQ_UNBOUND | WQ_MEM_RECLAIM |
				      (txq->index ? 0 : WQ_HIGHPRI), 1, txq->index);
	if (!txq->worker)
		return -ENOMEM;

	INIT_WORK(&txq->dpmaif_tx_work, t7xx_dpmaif_tx_done);
	spin_lock_init(&txq->tx_lock);

	return 0;
}

void t7xx_dpmaif_txq_free(struct dpmaif_tx_queue *txq)
{
	if (txq->worker)
		destroy_workqueue(txq->worker);

	skb_queue_purge(&txq->tx_skb_head);
	t7xx_dpmaif_tx_drb_buf_rel(txq);
}

void t7xx_dpmaif_tx_stop(struct dpmaif_ctrl *dpmaif_ctrl)
{
	int i;

	for (i = 0; i < DPMAIF_TXQ_NUM; i++) {
		struct dpmaif_tx_queue *txq;
		int count = 0;

		txq = &dpmaif_ctrl->txq[i];
		txq->que_started = false;
		/* Make sure TXQ is disabled */
		smp_mb();

		/* Wait for active Tx to be done */
		while (atomic_read(&txq->tx_processing)) {
			if (++count >= DPMAIF_MAX_CHECK_COUNT) {
				dev_err(dpmaif_ctrl->dev, "TX queue stop failed\n");
				break;
			}
		}
	}
}

static void t7xx_dpmaif_txq_flush_rel(struct dpmaif_tx_queue *txq)
{
	txq->que_started = false;

	cancel_work_sync(&txq->dpmaif_tx_work);
	flush_work(&txq->dpmaif_tx_work);
	t7xx_dpmaif_tx_free_drb_skb(txq);

	txq->drb_rd_idx = 0;
	txq->drb_wr_idx = 0;
	txq->drb_release_rd_idx = 0;
}

void t7xx_dpmaif_tx_clear(struct dpmaif_ctrl *dpmaif_ctrl)
{
	int i;

	for (i = 0; i < DPMAIF_TXQ_NUM; i++)
		t7xx_dpmaif_txq_flush_rel(&dpmaif_ctrl->txq[i]);
}
