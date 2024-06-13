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
 *  Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "t7xx_dpmaif.h"
#include "t7xx_hif_dpmaif.h"
#include "t7xx_hif_dpmaif_rx.h"
#include "t7xx_pci.h"

#define DPMAIF_BAT_COUNT		8192
#define DPMAIF_FRG_COUNT		4814
#define DPMAIF_PIT_COUNT		(DPMAIF_BAT_COUNT * 2)

#define DPMAIF_BAT_CNT_THRESHOLD	30
#define DPMAIF_PIT_CNT_THRESHOLD	60
#define DPMAIF_RX_PUSH_THRESHOLD_MASK	GENMASK(2, 0)
#define DPMAIF_NOTIFY_RELEASE_COUNT	128
#define DPMAIF_POLL_PIT_TIME_US		20
#define DPMAIF_POLL_PIT_MAX_TIME_US	2000
#define DPMAIF_WQ_TIME_LIMIT_MS		2
#define DPMAIF_CS_RESULT_PASS		0

/* Packet type */
#define DES_PT_PD			0
#define DES_PT_MSG			1
/* Buffer type */
#define PKT_BUF_FRAG			1

static unsigned int t7xx_normal_pit_bid(const struct dpmaif_pit *pit_info)
{
	u32 value;

	value = FIELD_GET(PD_PIT_H_BID, le32_to_cpu(pit_info->pd.footer));
	value <<= 13;
	value += FIELD_GET(PD_PIT_BUFFER_ID, le32_to_cpu(pit_info->header));
	return value;
}

static int t7xx_dpmaif_net_rx_push_thread(void *arg)
{
	struct dpmaif_rx_queue *q = arg;
	struct dpmaif_ctrl *hif_ctrl;
	struct dpmaif_callbacks *cb;

	hif_ctrl = q->dpmaif_ctrl;
	cb = hif_ctrl->callbacks;

	while (!kthread_should_stop()) {
		struct sk_buff *skb;
		unsigned long flags;

		if (skb_queue_empty(&q->skb_list)) {
			if (wait_event_interruptible(q->rx_wq,
						     !skb_queue_empty(&q->skb_list) ||
						     kthread_should_stop()))
				continue;

			if (kthread_should_stop())
				break;
		}

		spin_lock_irqsave(&q->skb_list.lock, flags);
		skb = __skb_dequeue(&q->skb_list);
		spin_unlock_irqrestore(&q->skb_list.lock, flags);

		if (!skb)
			continue;

		cb->recv_skb(hif_ctrl->t7xx_dev, skb);
		cond_resched();
	}

	return 0;
}

static int t7xx_dpmaif_update_bat_wr_idx(struct dpmaif_ctrl *dpmaif_ctrl,
					 const unsigned int q_num, const unsigned int bat_cnt)
{
	struct dpmaif_rx_queue *rxq = &dpmaif_ctrl->rxq[q_num];
	struct dpmaif_bat_request *bat_req = rxq->bat_req;
	unsigned int old_rl_idx, new_wr_idx, old_wr_idx;

	if (!rxq->que_started) {
		dev_err(dpmaif_ctrl->dev, "RX queue %d has not been started\n", rxq->index);
		return -EINVAL;
	}

	old_rl_idx = bat_req->bat_release_rd_idx;
	old_wr_idx = bat_req->bat_wr_idx;
	new_wr_idx = old_wr_idx + bat_cnt;

	if (old_rl_idx > old_wr_idx && new_wr_idx >= old_rl_idx)
		goto err_flow;

	if (new_wr_idx >= bat_req->bat_size_cnt) {
		new_wr_idx -= bat_req->bat_size_cnt;
		if (new_wr_idx >= old_rl_idx)
			goto err_flow;
	}

	bat_req->bat_wr_idx = new_wr_idx;
	return 0;

err_flow:
	dev_err(dpmaif_ctrl->dev, "RX BAT flow check fail\n");
	return -EINVAL;
}

static bool t7xx_alloc_and_map_skb_info(const struct dpmaif_ctrl *dpmaif_ctrl,
					const unsigned int size, struct dpmaif_bat_skb *cur_skb)
{
	dma_addr_t data_bus_addr;
	struct sk_buff *skb;

	skb = __dev_alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return false;

	data_bus_addr = dma_map_single(dpmaif_ctrl->dev, skb->data, size, DMA_FROM_DEVICE);
	if (dma_mapping_error(dpmaif_ctrl->dev, data_bus_addr)) {
		dev_err_ratelimited(dpmaif_ctrl->dev, "DMA mapping error\n");
		dev_kfree_skb_any(skb);
		return false;
	}

	cur_skb->skb = skb;
	cur_skb->data_bus_addr = data_bus_addr;
	cur_skb->data_len = size;

	return true;
}

static void t7xx_unmap_bat_skb(struct device *dev, struct dpmaif_bat_skb *bat_skb_base,
			       unsigned int index)
{
	struct dpmaif_bat_skb *bat_skb = bat_skb_base + index;

	if (bat_skb->skb) {
		dma_unmap_single(dev, bat_skb->data_bus_addr, bat_skb->data_len, DMA_FROM_DEVICE);
		dev_kfree_skb(bat_skb->skb);
		bat_skb->skb = NULL;
	}
}

/**
 * t7xx_dpmaif_rx_buf_alloc() - Allocate buffers for the BAT ring.
 * @dpmaif_ctrl: Pointer to DPMAIF context structure.
 * @bat_req: Pointer to BAT request structure.
 * @q_num: Queue number.
 * @buf_cnt: Number of buffers to allocate.
 * @initial: Indicates if the ring is being populated for the first time.
 *
 * Allocate skb and store the start address of the data buffer into the BAT ring.
 * If this is not the initial call, notify the HW about the new entries.
 *
 * Return:
 * * 0		- Success.
 * * -ERROR	- Error code.
 */
int t7xx_dpmaif_rx_buf_alloc(struct dpmaif_ctrl *dpmaif_ctrl,
			     const struct dpmaif_bat_request *bat_req,
			     const unsigned int q_num, const unsigned int buf_cnt,
			     const bool initial)
{
	unsigned int i, bat_cnt, bat_max_cnt, bat_start_idx;
	int ret;

	if (!buf_cnt || buf_cnt > bat_req->bat_size_cnt)
		return -EINVAL;

	/* Check BAT buffer space */
	bat_max_cnt = bat_req->bat_size_cnt;

	bat_cnt = t7xx_ring_buf_rd_wr_count(bat_max_cnt, bat_req->bat_release_rd_idx,
					    bat_req->bat_wr_idx, DPMAIF_WRITE);
	if (buf_cnt > bat_cnt)
		return -ENOMEM;

	bat_start_idx = bat_req->bat_wr_idx;

	for (i = 0; i < buf_cnt; i++) {
		unsigned int cur_bat_idx = bat_start_idx + i;
		struct dpmaif_bat_skb *cur_skb;
		struct dpmaif_bat *cur_bat;

		if (cur_bat_idx >= bat_max_cnt)
			cur_bat_idx -= bat_max_cnt;

		cur_skb = (struct dpmaif_bat_skb *)bat_req->bat_skb + cur_bat_idx;
		if (!cur_skb->skb &&
		    !t7xx_alloc_and_map_skb_info(dpmaif_ctrl, bat_req->pkt_buf_sz, cur_skb))
			break;

		cur_bat = (struct dpmaif_bat *)bat_req->bat_base + cur_bat_idx;
		cur_bat->buffer_addr_ext = upper_32_bits(cur_skb->data_bus_addr);
		cur_bat->p_buffer_addr = lower_32_bits(cur_skb->data_bus_addr);
	}

	if (!i)
		return -ENOMEM;

	ret = t7xx_dpmaif_update_bat_wr_idx(dpmaif_ctrl, q_num, i);
	if (ret)
		goto err_unmap_skbs;

	if (!initial) {
		unsigned int hw_wr_idx;

		ret = t7xx_dpmaif_dl_snd_hw_bat_cnt(&dpmaif_ctrl->hw_info, i);
		if (ret)
			goto err_unmap_skbs;

		hw_wr_idx = t7xx_dpmaif_dl_get_bat_wr_idx(&dpmaif_ctrl->hw_info,
							  DPF_RX_QNO_DFT);
		if (hw_wr_idx != bat_req->bat_wr_idx) {
			ret = -EFAULT;
			dev_err(dpmaif_ctrl->dev, "Write index mismatch in RX ring\n");
			goto err_unmap_skbs;
		}
	}

	return 0;

err_unmap_skbs:
	while (--i > 0)
		t7xx_unmap_bat_skb(dpmaif_ctrl->dev, bat_req->bat_skb, i);

	return ret;
}

static int t7xx_dpmaifq_release_pit_entry(struct dpmaif_rx_queue *rxq,
					  const unsigned int rel_entry_num)
{
	struct dpmaif_hw_info *hw_info = &rxq->dpmaif_ctrl->hw_info;
	unsigned int old_rel_idx, new_rel_idx, hw_wr_idx;
	int ret;

	if (!rxq->que_started)
		return 0;

	if (rel_entry_num >= rxq->pit_size_cnt) {
		dev_err(rxq->dpmaif_ctrl->dev, "Invalid PIT release index\n");
		return -EINVAL;
	}

	old_rel_idx = rxq->pit_release_rd_idx;
	new_rel_idx = old_rel_idx + rel_entry_num;
	hw_wr_idx = rxq->pit_wr_idx;
	if (hw_wr_idx < old_rel_idx && new_rel_idx >= rxq->pit_size_cnt)
		new_rel_idx -= rxq->pit_size_cnt;

	ret = t7xx_dpmaif_dlq_add_pit_remain_cnt(hw_info, rxq->index, rel_entry_num);
	if (ret) {
		dev_err(rxq->dpmaif_ctrl->dev, "PIT release failure: %d\n", ret);
		return ret;
	}

	rxq->pit_release_rd_idx = new_rel_idx;
	return 0;
}

static void t7xx_dpmaif_set_bat_mask(struct dpmaif_bat_request *bat_req, unsigned int idx)
{
	unsigned long flags;

	spin_lock_irqsave(&bat_req->mask_lock, flags);
	set_bit(idx, bat_req->bat_bitmap);
	spin_unlock_irqrestore(&bat_req->mask_lock, flags);
}

static int t7xx_frag_bat_cur_bid_check(struct dpmaif_rx_queue *rxq,
				       const unsigned int cur_bid)
{
	struct dpmaif_bat_request *bat_frag = rxq->bat_frag;
	struct dpmaif_bat_page *bat_page;

	if (cur_bid >= DPMAIF_FRG_COUNT)
		return -EINVAL;

	bat_page = bat_frag->bat_skb + cur_bid;
	if (!bat_page->page)
		return -EINVAL;

	return 0;
}

static void t7xx_unmap_bat_page(struct device *dev, struct dpmaif_bat_page *bat_page_base,
				unsigned int index)
{
	struct dpmaif_bat_page *bat_page = bat_page_base + index;

	if (bat_page->page) {
		dma_unmap_page(dev, bat_page->data_bus_addr, bat_page->data_len, DMA_FROM_DEVICE);
		put_page(bat_page->page);
		bat_page->page = NULL;
	}
}

/**
 * t7xx_dpmaif_rx_frag_alloc() - Allocates buffers for the Fragment BAT ring.
 * @dpmaif_ctrl: Pointer to DPMAIF context structure.
 * @bat_req: Pointer to BAT request structure.
 * @buf_cnt: Number of buffers to allocate.
 * @initial: Indicates if the ring is being populated for the first time.
 *
 * Fragment BAT is used when the received packet does not fit in a normal BAT entry.
 * This function allocates a page fragment and stores the start address of the page
 * into the Fragment BAT ring.
 * If this is not the initial call, notify the HW about the new entries.
 *
 * Return:
 * * 0		- Success.
 * * -ERROR	- Error code.
 */
int t7xx_dpmaif_rx_frag_alloc(struct dpmaif_ctrl *dpmaif_ctrl, struct dpmaif_bat_request *bat_req,
			      const unsigned int buf_cnt, const bool initial)
{
	unsigned int buf_space, cur_bat_idx = bat_req->bat_wr_idx;
	struct dpmaif_bat_page *bat_skb = bat_req->bat_skb;
	int ret = 0, i;

	if (!buf_cnt || buf_cnt > bat_req->bat_size_cnt)
		return -EINVAL;

	buf_space = t7xx_ring_buf_rd_wr_count(bat_req->bat_size_cnt,
					      bat_req->bat_release_rd_idx, bat_req->bat_wr_idx,
					      DPMAIF_WRITE);
	if (buf_cnt > buf_space) {
		dev_err(dpmaif_ctrl->dev,
			"Requested more buffers than the space available in RX frag ring\n");
		return -EINVAL;
	}

	for (i = 0; i < buf_cnt; i++) {
		struct dpmaif_bat_page *cur_page = bat_skb + cur_bat_idx;
		struct dpmaif_bat *cur_bat;
		dma_addr_t data_base_addr;

		if (!cur_page->page) {
			unsigned long offset;
			struct page *page;
			void *data;

			data = netdev_alloc_frag(bat_req->pkt_buf_sz);
			if (!data)
				break;

			page = virt_to_head_page(data);
			offset = data - page_address(page);

			data_base_addr = dma_map_page(dpmaif_ctrl->dev, page, offset,
						      bat_req->pkt_buf_sz, DMA_FROM_DEVICE);
			if (dma_mapping_error(dpmaif_ctrl->dev, data_base_addr)) {
				put_page(virt_to_head_page(data));
				dev_err(dpmaif_ctrl->dev, "DMA mapping fail\n");
				break;
			}

			cur_page->page = page;
			cur_page->data_bus_addr = data_base_addr;
			cur_page->offset = offset;
			cur_page->data_len = bat_req->pkt_buf_sz;
		}

		data_base_addr = cur_page->data_bus_addr;
		cur_bat = (struct dpmaif_bat *)bat_req->bat_base + cur_bat_idx;
		cur_bat->buffer_addr_ext = upper_32_bits(data_base_addr);
		cur_bat->p_buffer_addr = lower_32_bits(data_base_addr);
		cur_bat_idx = t7xx_ring_buf_get_next_wr_idx(bat_req->bat_size_cnt, cur_bat_idx);
	}

	bat_req->bat_wr_idx = cur_bat_idx;

	if (!initial)
		t7xx_dpmaif_dl_snd_hw_frg_cnt(&dpmaif_ctrl->hw_info, i);

	if (i < buf_cnt) {
		ret = -ENOMEM;
		if (initial) {
			while (--i > 0)
				t7xx_unmap_bat_page(dpmaif_ctrl->dev, bat_req->bat_skb, i);
		}
	}

	return ret;
}

static int t7xx_dpmaif_set_frag_to_skb(const struct dpmaif_rx_queue *rxq,
				       const struct dpmaif_pit *pkt_info,
				       struct sk_buff *skb)
{
	unsigned long long data_bus_addr, data_base_addr;
	struct device *dev = rxq->dpmaif_ctrl->dev;
	struct dpmaif_bat_page *page_info;
	unsigned int data_len;
	int data_offset;

	page_info = rxq->bat_frag->bat_skb;
	page_info += t7xx_normal_pit_bid(pkt_info);
	dma_unmap_page(dev, page_info->data_bus_addr, page_info->data_len, DMA_FROM_DEVICE);

	if (!page_info->page)
		return -EINVAL;

	data_bus_addr = le32_to_cpu(pkt_info->pd.data_addr_h);
	data_bus_addr = (data_bus_addr << 32) + le32_to_cpu(pkt_info->pd.data_addr_l);
	data_base_addr = page_info->data_bus_addr;
	data_offset = data_bus_addr - data_base_addr;
	data_offset += page_info->offset;
	data_len = FIELD_GET(PD_PIT_DATA_LEN, le32_to_cpu(pkt_info->header));
	skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page_info->page,
			data_offset, data_len, page_info->data_len);

	page_info->page = NULL;
	page_info->offset = 0;
	page_info->data_len = 0;
	return 0;
}

static int t7xx_dpmaif_get_frag(struct dpmaif_rx_queue *rxq,
				const struct dpmaif_pit *pkt_info,
				const struct dpmaif_cur_rx_skb_info *skb_info)
{
	unsigned int cur_bid = t7xx_normal_pit_bid(pkt_info);
	int ret;

	ret = t7xx_frag_bat_cur_bid_check(rxq, cur_bid);
	if (ret < 0)
		return ret;

	ret = t7xx_dpmaif_set_frag_to_skb(rxq, pkt_info, skb_info->cur_skb);
	if (ret < 0) {
		dev_err(rxq->dpmaif_ctrl->dev, "Failed to set frag data to skb: %d\n", ret);
		return ret;
	}

	t7xx_dpmaif_set_bat_mask(rxq->bat_frag, cur_bid);
	return 0;
}

static int t7xx_bat_cur_bid_check(struct dpmaif_rx_queue *rxq, const unsigned int cur_bid)
{
	struct dpmaif_bat_skb *bat_skb = rxq->bat_req->bat_skb;

	bat_skb += cur_bid;
	if (cur_bid >= DPMAIF_BAT_COUNT || !bat_skb->skb)
		return -EINVAL;

	return 0;
}

static int t7xx_dpmaif_read_pit_seq(const struct dpmaif_pit *pit)
{
	return FIELD_GET(PD_PIT_PIT_SEQ, le32_to_cpu(pit->pd.footer));
}

static int t7xx_dpmaif_check_pit_seq(struct dpmaif_rx_queue *rxq,
				     const struct dpmaif_pit *pit)
{
	unsigned int cur_pit_seq, expect_pit_seq = rxq->expect_pit_seq;

	if (read_poll_timeout_atomic(t7xx_dpmaif_read_pit_seq, cur_pit_seq,
				     cur_pit_seq == expect_pit_seq, DPMAIF_POLL_PIT_TIME_US,
				     DPMAIF_POLL_PIT_MAX_TIME_US, false, pit))
		return -EFAULT;

	rxq->expect_pit_seq++;
	if (rxq->expect_pit_seq >= DPMAIF_DL_PIT_SEQ_VALUE)
		rxq->expect_pit_seq = 0;

	return 0;
}

static unsigned int t7xx_dpmaif_avail_pkt_bat_cnt(struct dpmaif_bat_request *bat_req)
{
	unsigned int zero_index;
	unsigned long flags;

	spin_lock_irqsave(&bat_req->mask_lock, flags);

	zero_index = find_next_zero_bit(bat_req->bat_bitmap, bat_req->bat_size_cnt,
					bat_req->bat_release_rd_idx);

	if (zero_index < bat_req->bat_size_cnt) {
		spin_unlock_irqrestore(&bat_req->mask_lock, flags);
		return zero_index - bat_req->bat_release_rd_idx;
	}

	/* limiting the search till bat_release_rd_idx */
	zero_index = find_first_zero_bit(bat_req->bat_bitmap, bat_req->bat_release_rd_idx);
	spin_unlock_irqrestore(&bat_req->mask_lock, flags);
	return bat_req->bat_size_cnt - bat_req->bat_release_rd_idx + zero_index;
}

static int t7xx_dpmaif_release_bat_entry(const struct dpmaif_rx_queue *rxq,
					 const unsigned int rel_entry_num,
					 const enum bat_type buf_type)
{
	struct dpmaif_hw_info *hw_info = &rxq->dpmaif_ctrl->hw_info;
	unsigned int old_rel_idx, new_rel_idx, hw_rd_idx, i;
	struct dpmaif_bat_request *bat;
	unsigned long flags;

	if (!rxq->que_started || !rel_entry_num)
		return -EINVAL;

	if (buf_type == BAT_TYPE_FRAG) {
		bat = rxq->bat_frag;
		hw_rd_idx = t7xx_dpmaif_dl_get_frg_rd_idx(hw_info, rxq->index);
	} else {
		bat = rxq->bat_req;
		hw_rd_idx = t7xx_dpmaif_dl_get_bat_rd_idx(hw_info, rxq->index);
	}

	if (rel_entry_num >= bat->bat_size_cnt)
		return -EINVAL;

	old_rel_idx = bat->bat_release_rd_idx;
	new_rel_idx = old_rel_idx + rel_entry_num;

	/* Do not need to release if the queue is empty */
	if (bat->bat_wr_idx == old_rel_idx)
		return 0;

	if (hw_rd_idx >= old_rel_idx) {
		if (new_rel_idx > hw_rd_idx)
			return -EINVAL;
	}

	if (new_rel_idx >= bat->bat_size_cnt) {
		new_rel_idx -= bat->bat_size_cnt;
		if (new_rel_idx > hw_rd_idx)
			return -EINVAL;
	}

	spin_lock_irqsave(&bat->mask_lock, flags);
	for (i = 0; i < rel_entry_num; i++) {
		unsigned int index = bat->bat_release_rd_idx + i;

		if (index >= bat->bat_size_cnt)
			index -= bat->bat_size_cnt;

		clear_bit(index, bat->bat_bitmap);
	}
	spin_unlock_irqrestore(&bat->mask_lock, flags);

	bat->bat_release_rd_idx = new_rel_idx;
	return rel_entry_num;
}

static int t7xx_dpmaif_pit_release_and_add(struct dpmaif_rx_queue *rxq)
{
	int ret;

	if (rxq->pit_remain_release_cnt < DPMAIF_PIT_CNT_THRESHOLD)
		return 0;

	ret = t7xx_dpmaifq_release_pit_entry(rxq, rxq->pit_remain_release_cnt);
	if (ret)
		return ret;

	rxq->pit_remain_release_cnt = 0;
	return 0;
}

static int t7xx_dpmaif_bat_release_and_add(const struct dpmaif_rx_queue *rxq)
{
	unsigned int bid_cnt;
	int ret;

	bid_cnt = t7xx_dpmaif_avail_pkt_bat_cnt(rxq->bat_req);
	if (bid_cnt < DPMAIF_BAT_CNT_THRESHOLD)
		return 0;

	ret = t7xx_dpmaif_release_bat_entry(rxq, bid_cnt, BAT_TYPE_NORMAL);
	if (ret <= 0) {
		dev_err(rxq->dpmaif_ctrl->dev, "Release PKT BAT failed: %d\n", ret);
		return ret;
	}

	ret = t7xx_dpmaif_rx_buf_alloc(rxq->dpmaif_ctrl, rxq->bat_req, rxq->index, bid_cnt, false);
	if (ret < 0)
		dev_err(rxq->dpmaif_ctrl->dev, "Allocate new RX buffer failed: %d\n", ret);

	return ret;
}

static int t7xx_dpmaif_frag_bat_release_and_add(const struct dpmaif_rx_queue *rxq)
{
	unsigned int bid_cnt;
	int ret;

	bid_cnt = t7xx_dpmaif_avail_pkt_bat_cnt(rxq->bat_frag);
	if (bid_cnt < DPMAIF_BAT_CNT_THRESHOLD)
		return 0;

	ret = t7xx_dpmaif_release_bat_entry(rxq, bid_cnt, BAT_TYPE_FRAG);
	if (ret <= 0) {
		dev_err(rxq->dpmaif_ctrl->dev, "Release BAT entry failed: %d\n", ret);
		return ret;
	}

	return t7xx_dpmaif_rx_frag_alloc(rxq->dpmaif_ctrl, rxq->bat_frag, bid_cnt, false);
}

static void t7xx_dpmaif_parse_msg_pit(const struct dpmaif_rx_queue *rxq,
				      const struct dpmaif_pit *msg_pit,
				      struct dpmaif_cur_rx_skb_info *skb_info)
{
	int header = le32_to_cpu(msg_pit->header);

	skb_info->cur_chn_idx = FIELD_GET(MSG_PIT_CHANNEL_ID, header);
	skb_info->check_sum = FIELD_GET(MSG_PIT_CHECKSUM, header);
	skb_info->pit_dp = FIELD_GET(MSG_PIT_DP, header);
	skb_info->pkt_type = FIELD_GET(MSG_PIT_IP, le32_to_cpu(msg_pit->msg.params_3));
}

static int t7xx_dpmaif_set_data_to_skb(const struct dpmaif_rx_queue *rxq,
				       const struct dpmaif_pit *pkt_info,
				       struct dpmaif_cur_rx_skb_info *skb_info)
{
	unsigned long long data_bus_addr, data_base_addr;
	struct device *dev = rxq->dpmaif_ctrl->dev;
	struct dpmaif_bat_skb *bat_skb;
	unsigned int data_len;
	struct sk_buff *skb;
	int data_offset;

	bat_skb = rxq->bat_req->bat_skb;
	bat_skb += t7xx_normal_pit_bid(pkt_info);
	dma_unmap_single(dev, bat_skb->data_bus_addr, bat_skb->data_len, DMA_FROM_DEVICE);

	data_bus_addr = le32_to_cpu(pkt_info->pd.data_addr_h);
	data_bus_addr = (data_bus_addr << 32) + le32_to_cpu(pkt_info->pd.data_addr_l);
	data_base_addr = bat_skb->data_bus_addr;
	data_offset = data_bus_addr - data_base_addr;
	data_len = FIELD_GET(PD_PIT_DATA_LEN, le32_to_cpu(pkt_info->header));
	skb = bat_skb->skb;
	skb->len = 0;
	skb_reset_tail_pointer(skb);
	skb_reserve(skb, data_offset);

	if (skb->tail + data_len > skb->end) {
		dev_err(dev, "No buffer space available\n");
		return -ENOBUFS;
	}

	skb_put(skb, data_len);
	skb_info->cur_skb = skb;
	bat_skb->skb = NULL;
	return 0;
}

static int t7xx_dpmaif_get_rx_pkt(struct dpmaif_rx_queue *rxq,
				  const struct dpmaif_pit *pkt_info,
				  struct dpmaif_cur_rx_skb_info *skb_info)
{
	unsigned int cur_bid = t7xx_normal_pit_bid(pkt_info);
	int ret;

	ret = t7xx_bat_cur_bid_check(rxq, cur_bid);
	if (ret < 0)
		return ret;

	ret = t7xx_dpmaif_set_data_to_skb(rxq, pkt_info, skb_info);
	if (ret < 0) {
		dev_err(rxq->dpmaif_ctrl->dev, "RX set data to skb failed: %d\n", ret);
		return ret;
	}

	t7xx_dpmaif_set_bat_mask(rxq->bat_req, cur_bid);
	return 0;
}

static int t7xx_dpmaifq_rx_notify_hw(struct dpmaif_rx_queue *rxq)
{
	struct dpmaif_ctrl *dpmaif_ctrl = rxq->dpmaif_ctrl;
	int ret;

	queue_work(dpmaif_ctrl->bat_release_wq, &dpmaif_ctrl->bat_release_work);

	ret = t7xx_dpmaif_pit_release_and_add(rxq);
	if (ret < 0)
		dev_err(dpmaif_ctrl->dev, "RXQ%u update PIT failed: %d\n", rxq->index, ret);

	return ret;
}

static void t7xx_dpmaif_rx_skb_enqueue(struct dpmaif_rx_queue *rxq, struct sk_buff *skb)
{
	unsigned long flags;

	spin_lock_irqsave(&rxq->skb_list.lock, flags);
	if (rxq->skb_list.qlen < rxq->skb_list_max_len)
		__skb_queue_tail(&rxq->skb_list, skb);
	else
		dev_kfree_skb_any(skb);
	spin_unlock_irqrestore(&rxq->skb_list.lock, flags);
}

static void t7xx_dpmaif_rx_skb(struct dpmaif_rx_queue *rxq,
			       struct dpmaif_cur_rx_skb_info *skb_info)
{
	struct sk_buff *skb = skb_info->cur_skb;
	struct t7xx_skb_cb *skb_cb;
	u8 netif_id;

	skb_info->cur_skb = NULL;

	if (skb_info->pit_dp) {
		dev_kfree_skb_any(skb);
		return;
	}

	skb->ip_summed = skb_info->check_sum == DPMAIF_CS_RESULT_PASS ? CHECKSUM_UNNECESSARY :
									CHECKSUM_NONE;
	netif_id = FIELD_GET(NETIF_MASK, skb_info->cur_chn_idx);
	skb_cb = T7XX_SKB_CB(skb);
	skb_cb->netif_idx = netif_id;
	skb_cb->rx_pkt_type = skb_info->pkt_type;
	t7xx_dpmaif_rx_skb_enqueue(rxq, skb);
}

static int t7xx_dpmaif_rx_start(struct dpmaif_rx_queue *rxq, const unsigned int pit_cnt,
				const unsigned long timeout)
{
	unsigned int cur_pit, pit_len, rx_cnt, recv_skb_cnt = 0;
	struct device *dev = rxq->dpmaif_ctrl->dev;
	struct dpmaif_cur_rx_skb_info *skb_info;
	int ret = 0;

	pit_len = rxq->pit_size_cnt;
	skb_info = &rxq->rx_data_info;
	cur_pit = rxq->pit_rd_idx;

	for (rx_cnt = 0; rx_cnt < pit_cnt; rx_cnt++) {
		struct dpmaif_pit *pkt_info;
		u32 val;

		if (!skb_info->msg_pit_received && time_after_eq(jiffies, timeout))
			break;

		pkt_info = (struct dpmaif_pit *)rxq->pit_base + cur_pit;
		if (t7xx_dpmaif_check_pit_seq(rxq, pkt_info)) {
			dev_err_ratelimited(dev, "RXQ%u checks PIT SEQ fail\n", rxq->index);
			return -EAGAIN;
		}

		val = FIELD_GET(PD_PIT_PACKET_TYPE, le32_to_cpu(pkt_info->header));
		if (val == DES_PT_MSG) {
			if (skb_info->msg_pit_received)
				dev_err(dev, "RXQ%u received repeated PIT\n", rxq->index);

			skb_info->msg_pit_received = true;
			t7xx_dpmaif_parse_msg_pit(rxq, pkt_info, skb_info);
		} else { /* DES_PT_PD */
			val = FIELD_GET(PD_PIT_BUFFER_TYPE, le32_to_cpu(pkt_info->header));
			if (val != PKT_BUF_FRAG)
				ret = t7xx_dpmaif_get_rx_pkt(rxq, pkt_info, skb_info);
			else if (!skb_info->cur_skb)
				ret = -EINVAL;
			else
				ret = t7xx_dpmaif_get_frag(rxq, pkt_info, skb_info);

			if (ret < 0) {
				skb_info->err_payload = 1;
				dev_err_ratelimited(dev, "RXQ%u error payload\n", rxq->index);
			}

			val = FIELD_GET(PD_PIT_CONT, le32_to_cpu(pkt_info->header));
			if (!val) {
				if (!skb_info->err_payload) {
					t7xx_dpmaif_rx_skb(rxq, skb_info);
				} else if (skb_info->cur_skb) {
					dev_kfree_skb_any(skb_info->cur_skb);
					skb_info->cur_skb = NULL;
				}

				memset(skb_info, 0, sizeof(*skb_info));

				recv_skb_cnt++;
				if (!(recv_skb_cnt & DPMAIF_RX_PUSH_THRESHOLD_MASK)) {
					wake_up_all(&rxq->rx_wq);
					recv_skb_cnt = 0;
				}
			}
		}

		cur_pit = t7xx_ring_buf_get_next_wr_idx(pit_len, cur_pit);
		rxq->pit_rd_idx = cur_pit;
		rxq->pit_remain_release_cnt++;

		if (rx_cnt > 0 && !(rx_cnt % DPMAIF_NOTIFY_RELEASE_COUNT)) {
			ret = t7xx_dpmaifq_rx_notify_hw(rxq);
			if (ret < 0)
				break;
		}
	}

	if (recv_skb_cnt)
		wake_up_all(&rxq->rx_wq);

	if (!ret)
		ret = t7xx_dpmaifq_rx_notify_hw(rxq);

	if (ret)
		return ret;

	return rx_cnt;
}

static unsigned int t7xx_dpmaifq_poll_pit(struct dpmaif_rx_queue *rxq)
{
	unsigned int hw_wr_idx, pit_cnt;

	if (!rxq->que_started)
		return 0;

	hw_wr_idx = t7xx_dpmaif_dl_dlq_pit_get_wr_idx(&rxq->dpmaif_ctrl->hw_info, rxq->index);
	pit_cnt = t7xx_ring_buf_rd_wr_count(rxq->pit_size_cnt, rxq->pit_rd_idx, hw_wr_idx,
					    DPMAIF_READ);
	rxq->pit_wr_idx = hw_wr_idx;
	return pit_cnt;
}

static int t7xx_dpmaif_rx_data_collect(struct dpmaif_ctrl *dpmaif_ctrl,
				       const unsigned int q_num, const unsigned int budget)
{
	struct dpmaif_rx_queue *rxq = &dpmaif_ctrl->rxq[q_num];
	unsigned long time_limit;
	unsigned int cnt;

	time_limit = jiffies + msecs_to_jiffies(DPMAIF_WQ_TIME_LIMIT_MS);

	while ((cnt = t7xx_dpmaifq_poll_pit(rxq))) {
		unsigned int rd_cnt;
		int real_cnt;

		rd_cnt = min(cnt, budget);

		real_cnt = t7xx_dpmaif_rx_start(rxq, rd_cnt, time_limit);
		if (real_cnt < 0)
			return real_cnt;

		if (real_cnt < cnt)
			return -EAGAIN;
	}

	return 0;
}

static void t7xx_dpmaif_do_rx(struct dpmaif_ctrl *dpmaif_ctrl, struct dpmaif_rx_queue *rxq)
{
	struct dpmaif_hw_info *hw_info = &dpmaif_ctrl->hw_info;
	int ret;

	ret = t7xx_dpmaif_rx_data_collect(dpmaif_ctrl, rxq->index, rxq->budget);
	if (ret < 0) {
		/* Try one more time */
		queue_work(rxq->worker, &rxq->dpmaif_rxq_work);
		t7xx_dpmaif_clr_ip_busy_sts(hw_info);
	} else {
		t7xx_dpmaif_clr_ip_busy_sts(hw_info);
		t7xx_dpmaif_dlq_unmask_rx_done(hw_info, rxq->index);
	}
}

static void t7xx_dpmaif_rxq_work(struct work_struct *work)
{
	struct dpmaif_rx_queue *rxq = container_of(work, struct dpmaif_rx_queue, dpmaif_rxq_work);
	struct dpmaif_ctrl *dpmaif_ctrl = rxq->dpmaif_ctrl;
	int ret;

	atomic_set(&rxq->rx_processing, 1);
	/* Ensure rx_processing is changed to 1 before actually begin RX flow */
	smp_mb();

	if (!rxq->que_started) {
		atomic_set(&rxq->rx_processing, 0);
		dev_err(dpmaif_ctrl->dev, "Work RXQ: %d has not been started\n", rxq->index);
		return;
	}

	ret = pm_runtime_resume_and_get(dpmaif_ctrl->dev);
	if (ret < 0 && ret != -EACCES)
		return;

	t7xx_pci_disable_sleep(dpmaif_ctrl->t7xx_dev);
	if (t7xx_pci_sleep_disable_complete(dpmaif_ctrl->t7xx_dev))
		t7xx_dpmaif_do_rx(dpmaif_ctrl, rxq);

	t7xx_pci_enable_sleep(dpmaif_ctrl->t7xx_dev);
	pm_runtime_mark_last_busy(dpmaif_ctrl->dev);
	pm_runtime_put_autosuspend(dpmaif_ctrl->dev);
	atomic_set(&rxq->rx_processing, 0);
}

void t7xx_dpmaif_irq_rx_done(struct dpmaif_ctrl *dpmaif_ctrl, const unsigned int que_mask)
{
	struct dpmaif_rx_queue *rxq;
	int qno;

	qno = ffs(que_mask) - 1;
	if (qno < 0 || qno > DPMAIF_RXQ_NUM - 1) {
		dev_err(dpmaif_ctrl->dev, "Invalid RXQ number: %u\n", qno);
		return;
	}

	rxq = &dpmaif_ctrl->rxq[qno];
	queue_work(rxq->worker, &rxq->dpmaif_rxq_work);
}

static void t7xx_dpmaif_base_free(const struct dpmaif_ctrl *dpmaif_ctrl,
				  const struct dpmaif_bat_request *bat_req)
{
	if (bat_req->bat_base)
		dma_free_coherent(dpmaif_ctrl->dev,
				  bat_req->bat_size_cnt * sizeof(struct dpmaif_bat),
				  bat_req->bat_base, bat_req->bat_bus_addr);
}

/**
 * t7xx_dpmaif_bat_alloc() - Allocate the BAT ring buffer.
 * @dpmaif_ctrl: Pointer to DPMAIF context structure.
 * @bat_req: Pointer to BAT request structure.
 * @buf_type: BAT ring type.
 *
 * This function allocates the BAT ring buffer shared with the HW device, also allocates
 * a buffer used to store information about the BAT skbs for further release.
 *
 * Return:
 * * 0		- Success.
 * * -ERROR	- Error code.
 */
int t7xx_dpmaif_bat_alloc(const struct dpmaif_ctrl *dpmaif_ctrl, struct dpmaif_bat_request *bat_req,
			  const enum bat_type buf_type)
{
	int sw_buf_size;

	if (buf_type == BAT_TYPE_FRAG) {
		sw_buf_size = sizeof(struct dpmaif_bat_page);
		bat_req->bat_size_cnt = DPMAIF_FRG_COUNT;
		bat_req->pkt_buf_sz = DPMAIF_HW_FRG_PKTBUF;
	} else {
		sw_buf_size = sizeof(struct dpmaif_bat_skb);
		bat_req->bat_size_cnt = DPMAIF_BAT_COUNT;
		bat_req->pkt_buf_sz = DPMAIF_HW_BAT_PKTBUF;
	}

	bat_req->type = buf_type;
	bat_req->bat_wr_idx = 0;
	bat_req->bat_release_rd_idx = 0;

	bat_req->bat_base = dma_alloc_coherent(dpmaif_ctrl->dev,
					       bat_req->bat_size_cnt * sizeof(struct dpmaif_bat),
					       &bat_req->bat_bus_addr, GFP_KERNEL | __GFP_ZERO);
	if (!bat_req->bat_base)
		return -ENOMEM;

	/* For AP SW to record skb information */
	bat_req->bat_skb = devm_kzalloc(dpmaif_ctrl->dev, bat_req->bat_size_cnt * sw_buf_size,
					GFP_KERNEL);
	if (!bat_req->bat_skb)
		goto err_free_dma_mem;

	bat_req->bat_bitmap = bitmap_zalloc(bat_req->bat_size_cnt, GFP_KERNEL);
	if (!bat_req->bat_bitmap)
		goto err_free_dma_mem;

	spin_lock_init(&bat_req->mask_lock);
	atomic_set(&bat_req->refcnt, 0);
	return 0;

err_free_dma_mem:
	t7xx_dpmaif_base_free(dpmaif_ctrl, bat_req);

	return -ENOMEM;
}

void t7xx_dpmaif_bat_free(const struct dpmaif_ctrl *dpmaif_ctrl, struct dpmaif_bat_request *bat_req)
{
	if (!bat_req || !atomic_dec_and_test(&bat_req->refcnt))
		return;

	bitmap_free(bat_req->bat_bitmap);
	bat_req->bat_bitmap = NULL;

	if (bat_req->bat_skb) {
		unsigned int i;

		for (i = 0; i < bat_req->bat_size_cnt; i++) {
			if (bat_req->type == BAT_TYPE_FRAG)
				t7xx_unmap_bat_page(dpmaif_ctrl->dev, bat_req->bat_skb, i);
			else
				t7xx_unmap_bat_skb(dpmaif_ctrl->dev, bat_req->bat_skb, i);
		}
	}

	t7xx_dpmaif_base_free(dpmaif_ctrl, bat_req);
}

static int t7xx_dpmaif_rx_alloc(struct dpmaif_rx_queue *rxq)
{
	rxq->pit_size_cnt = DPMAIF_PIT_COUNT;
	rxq->pit_rd_idx = 0;
	rxq->pit_wr_idx = 0;
	rxq->pit_release_rd_idx = 0;
	rxq->expect_pit_seq = 0;
	rxq->pit_remain_release_cnt = 0;
	memset(&rxq->rx_data_info, 0, sizeof(rxq->rx_data_info));

	rxq->pit_base = dma_alloc_coherent(rxq->dpmaif_ctrl->dev,
					   rxq->pit_size_cnt * sizeof(struct dpmaif_pit),
					   &rxq->pit_bus_addr, GFP_KERNEL | __GFP_ZERO);
	if (!rxq->pit_base)
		return -ENOMEM;

	rxq->bat_req = &rxq->dpmaif_ctrl->bat_req;
	atomic_inc(&rxq->bat_req->refcnt);

	rxq->bat_frag = &rxq->dpmaif_ctrl->bat_frag;
	atomic_inc(&rxq->bat_frag->refcnt);
	return 0;
}

static void t7xx_dpmaif_rx_buf_free(const struct dpmaif_rx_queue *rxq)
{
	if (!rxq->dpmaif_ctrl)
		return;

	t7xx_dpmaif_bat_free(rxq->dpmaif_ctrl, rxq->bat_req);
	t7xx_dpmaif_bat_free(rxq->dpmaif_ctrl, rxq->bat_frag);

	if (rxq->pit_base)
		dma_free_coherent(rxq->dpmaif_ctrl->dev,
				  rxq->pit_size_cnt * sizeof(struct dpmaif_pit),
				  rxq->pit_base, rxq->pit_bus_addr);
}

int t7xx_dpmaif_rxq_init(struct dpmaif_rx_queue *queue)
{
	int ret;

	ret = t7xx_dpmaif_rx_alloc(queue);
	if (ret < 0) {
		dev_err(queue->dpmaif_ctrl->dev, "Failed to allocate RX buffers: %d\n", ret);
		return ret;
	}

	INIT_WORK(&queue->dpmaif_rxq_work, t7xx_dpmaif_rxq_work);

	queue->worker = alloc_workqueue("dpmaif_rx%d_worker",
					WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_HIGHPRI, 1, queue->index);
	if (!queue->worker) {
		ret = -ENOMEM;
		goto err_free_rx_buffer;
	}

	init_waitqueue_head(&queue->rx_wq);
	skb_queue_head_init(&queue->skb_list);
	queue->skb_list_max_len = queue->bat_req->pkt_buf_sz;
	queue->rx_thread = kthread_run(t7xx_dpmaif_net_rx_push_thread,
				       queue, "dpmaif_rx%d_push", queue->index);

	ret = PTR_ERR_OR_ZERO(queue->rx_thread);
	if (ret)
		goto err_free_workqueue;

	return 0;

err_free_workqueue:
	destroy_workqueue(queue->worker);

err_free_rx_buffer:
	t7xx_dpmaif_rx_buf_free(queue);

	return ret;
}

void t7xx_dpmaif_rxq_free(struct dpmaif_rx_queue *queue)
{
	if (queue->worker)
		destroy_workqueue(queue->worker);

	if (queue->rx_thread)
		kthread_stop(queue->rx_thread);

	skb_queue_purge(&queue->skb_list);
	t7xx_dpmaif_rx_buf_free(queue);
}

static void t7xx_dpmaif_bat_release_work(struct work_struct *work)
{
	struct dpmaif_ctrl *dpmaif_ctrl = container_of(work, struct dpmaif_ctrl, bat_release_work);
	struct dpmaif_rx_queue *rxq;
	int ret;

	ret = pm_runtime_resume_and_get(dpmaif_ctrl->dev);
	if (ret < 0 && ret != -EACCES)
		return;

	t7xx_pci_disable_sleep(dpmaif_ctrl->t7xx_dev);

	/* ALL RXQ use one BAT table, so choose DPF_RX_QNO_DFT */
	rxq = &dpmaif_ctrl->rxq[DPF_RX_QNO_DFT];
	if (t7xx_pci_sleep_disable_complete(dpmaif_ctrl->t7xx_dev)) {
		t7xx_dpmaif_bat_release_and_add(rxq);
		t7xx_dpmaif_frag_bat_release_and_add(rxq);
	}

	t7xx_pci_enable_sleep(dpmaif_ctrl->t7xx_dev);
	pm_runtime_mark_last_busy(dpmaif_ctrl->dev);
	pm_runtime_put_autosuspend(dpmaif_ctrl->dev);
}

int t7xx_dpmaif_bat_rel_wq_alloc(struct dpmaif_ctrl *dpmaif_ctrl)
{
	dpmaif_ctrl->bat_release_wq = alloc_workqueue("dpmaif_bat_release_work_queue",
						      WQ_MEM_RECLAIM, 1);
	if (!dpmaif_ctrl->bat_release_wq)
		return -ENOMEM;

	INIT_WORK(&dpmaif_ctrl->bat_release_work, t7xx_dpmaif_bat_release_work);
	return 0;
}

void t7xx_dpmaif_bat_wq_rel(struct dpmaif_ctrl *dpmaif_ctrl)
{
	flush_work(&dpmaif_ctrl->bat_release_work);

	if (dpmaif_ctrl->bat_release_wq) {
		destroy_workqueue(dpmaif_ctrl->bat_release_wq);
		dpmaif_ctrl->bat_release_wq = NULL;
	}
}

/**
 * t7xx_dpmaif_rx_stop() - Suspend RX flow.
 * @dpmaif_ctrl: Pointer to data path control struct dpmaif_ctrl.
 *
 * Wait for all the RX work to finish executing and mark the RX queue as paused.
 */
void t7xx_dpmaif_rx_stop(struct dpmaif_ctrl *dpmaif_ctrl)
{
	unsigned int i;

	for (i = 0; i < DPMAIF_RXQ_NUM; i++) {
		struct dpmaif_rx_queue *rxq = &dpmaif_ctrl->rxq[i];
		int timeout, value;

		flush_work(&rxq->dpmaif_rxq_work);

		timeout = readx_poll_timeout_atomic(atomic_read, &rxq->rx_processing, value,
						    !value, 0, DPMAIF_CHECK_INIT_TIMEOUT_US);
		if (timeout)
			dev_err(dpmaif_ctrl->dev, "Stop RX SW failed\n");

		/* Ensure RX processing has stopped before we set rxq->que_started to false */
		smp_mb();
		rxq->que_started = false;
	}
}

static void t7xx_dpmaif_stop_rxq(struct dpmaif_rx_queue *rxq)
{
	int cnt, j = 0;

	flush_work(&rxq->dpmaif_rxq_work);
	rxq->que_started = false;

	do {
		cnt = t7xx_ring_buf_rd_wr_count(rxq->pit_size_cnt, rxq->pit_rd_idx,
						rxq->pit_wr_idx, DPMAIF_READ);

		if (++j >= DPMAIF_MAX_CHECK_COUNT) {
			dev_err(rxq->dpmaif_ctrl->dev, "Stop RX SW failed, %d\n", cnt);
			break;
		}
	} while (cnt);

	memset(rxq->pit_base, 0, rxq->pit_size_cnt * sizeof(struct dpmaif_pit));
	memset(rxq->bat_req->bat_base, 0, rxq->bat_req->bat_size_cnt * sizeof(struct dpmaif_bat));
	bitmap_zero(rxq->bat_req->bat_bitmap, rxq->bat_req->bat_size_cnt);
	memset(&rxq->rx_data_info, 0, sizeof(rxq->rx_data_info));

	rxq->pit_rd_idx = 0;
	rxq->pit_wr_idx = 0;
	rxq->pit_release_rd_idx = 0;
	rxq->expect_pit_seq = 0;
	rxq->pit_remain_release_cnt = 0;
	rxq->bat_req->bat_release_rd_idx = 0;
	rxq->bat_req->bat_wr_idx = 0;
	rxq->bat_frag->bat_release_rd_idx = 0;
	rxq->bat_frag->bat_wr_idx = 0;
}

void t7xx_dpmaif_rx_clear(struct dpmaif_ctrl *dpmaif_ctrl)
{
	int i;

	for (i = 0; i < DPMAIF_RXQ_NUM; i++)
		t7xx_dpmaif_stop_rxq(&dpmaif_ctrl->rxq[i]);
}
