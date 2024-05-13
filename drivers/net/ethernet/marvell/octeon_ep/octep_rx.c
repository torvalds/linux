// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>

#include "octep_config.h"
#include "octep_main.h"

static void octep_oq_reset_indices(struct octep_oq *oq)
{
	oq->host_read_idx = 0;
	oq->host_refill_idx = 0;
	oq->refill_count = 0;
	oq->last_pkt_count = 0;
	oq->pkts_pending = 0;
}

/**
 * octep_oq_fill_ring_buffers() - fill initial receive buffers for Rx ring.
 *
 * @oq: Octeon Rx queue data structure.
 *
 * Return: 0, if successfully filled receive buffers for all descriptors.
 *         -1, if failed to allocate a buffer or failed to map for DMA.
 */
static int octep_oq_fill_ring_buffers(struct octep_oq *oq)
{
	struct octep_oq_desc_hw *desc_ring = oq->desc_ring;
	struct page *page;
	u32 i;

	for (i = 0; i < oq->max_count; i++) {
		page = dev_alloc_page();
		if (unlikely(!page)) {
			dev_err(oq->dev, "Rx buffer alloc failed\n");
			goto rx_buf_alloc_err;
		}
		desc_ring[i].buffer_ptr = dma_map_page(oq->dev, page, 0,
						       PAGE_SIZE,
						       DMA_FROM_DEVICE);
		if (dma_mapping_error(oq->dev, desc_ring[i].buffer_ptr)) {
			dev_err(oq->dev,
				"OQ-%d buffer alloc: DMA mapping error!\n",
				oq->q_no);
			put_page(page);
			goto dma_map_err;
		}
		oq->buff_info[i].page = page;
	}

	return 0;

dma_map_err:
rx_buf_alloc_err:
	while (i) {
		i--;
		dma_unmap_page(oq->dev, desc_ring[i].buffer_ptr, PAGE_SIZE, DMA_FROM_DEVICE);
		put_page(oq->buff_info[i].page);
		oq->buff_info[i].page = NULL;
	}

	return -1;
}

/**
 * octep_oq_refill() - refill buffers for used Rx ring descriptors.
 *
 * @oct: Octeon device private data structure.
 * @oq: Octeon Rx queue data structure.
 *
 * Return: number of descriptors successfully refilled with receive buffers.
 */
static int octep_oq_refill(struct octep_device *oct, struct octep_oq *oq)
{
	struct octep_oq_desc_hw *desc_ring = oq->desc_ring;
	struct page *page;
	u32 refill_idx, i;

	refill_idx = oq->host_refill_idx;
	for (i = 0; i < oq->refill_count; i++) {
		page = dev_alloc_page();
		if (unlikely(!page)) {
			dev_err(oq->dev, "refill: rx buffer alloc failed\n");
			oq->stats.alloc_failures++;
			break;
		}

		desc_ring[refill_idx].buffer_ptr = dma_map_page(oq->dev, page, 0,
								PAGE_SIZE, DMA_FROM_DEVICE);
		if (dma_mapping_error(oq->dev, desc_ring[refill_idx].buffer_ptr)) {
			dev_err(oq->dev,
				"OQ-%d buffer refill: DMA mapping error!\n",
				oq->q_no);
			put_page(page);
			oq->stats.alloc_failures++;
			break;
		}
		oq->buff_info[refill_idx].page = page;
		refill_idx++;
		if (refill_idx == oq->max_count)
			refill_idx = 0;
	}
	oq->host_refill_idx = refill_idx;
	oq->refill_count -= i;

	return i;
}

/**
 * octep_setup_oq() - Setup a Rx queue.
 *
 * @oct: Octeon device private data structure.
 * @q_no: Rx queue number to be setup.
 *
 * Allocate resources for a Rx queue.
 */
static int octep_setup_oq(struct octep_device *oct, int q_no)
{
	struct octep_oq *oq;
	u32 desc_ring_size;

	oq = vzalloc(sizeof(*oq));
	if (!oq)
		goto create_oq_fail;
	oct->oq[q_no] = oq;

	oq->octep_dev = oct;
	oq->netdev = oct->netdev;
	oq->dev = &oct->pdev->dev;
	oq->q_no = q_no;
	oq->max_count = CFG_GET_OQ_NUM_DESC(oct->conf);
	oq->ring_size_mask = oq->max_count - 1;
	oq->buffer_size = CFG_GET_OQ_BUF_SIZE(oct->conf);
	oq->max_single_buffer_size = oq->buffer_size - OCTEP_OQ_RESP_HW_SIZE;

	/* When the hardware/firmware supports additional capabilities,
	 * additional header is filled-in by Octeon after length field in
	 * Rx packets. this header contains additional packet information.
	 */
	if (oct->caps_enabled)
		oq->max_single_buffer_size -= OCTEP_OQ_RESP_HW_EXT_SIZE;

	oq->refill_threshold = CFG_GET_OQ_REFILL_THRESHOLD(oct->conf);

	desc_ring_size = oq->max_count * OCTEP_OQ_DESC_SIZE;
	oq->desc_ring = dma_alloc_coherent(oq->dev, desc_ring_size,
					   &oq->desc_ring_dma, GFP_KERNEL);

	if (unlikely(!oq->desc_ring)) {
		dev_err(oq->dev,
			"Failed to allocate DMA memory for OQ-%d !!\n", q_no);
		goto desc_dma_alloc_err;
	}

	oq->buff_info = vcalloc(oq->max_count, OCTEP_OQ_RECVBUF_SIZE);
	if (unlikely(!oq->buff_info)) {
		dev_err(&oct->pdev->dev,
			"Failed to allocate buffer info for OQ-%d\n", q_no);
		goto buf_list_err;
	}

	if (octep_oq_fill_ring_buffers(oq))
		goto oq_fill_buff_err;

	octep_oq_reset_indices(oq);
	oct->hw_ops.setup_oq_regs(oct, q_no);
	oct->num_oqs++;

	return 0;

oq_fill_buff_err:
	vfree(oq->buff_info);
	oq->buff_info = NULL;
buf_list_err:
	dma_free_coherent(oq->dev, desc_ring_size,
			  oq->desc_ring, oq->desc_ring_dma);
	oq->desc_ring = NULL;
desc_dma_alloc_err:
	vfree(oq);
	oct->oq[q_no] = NULL;
create_oq_fail:
	return -1;
}

/**
 * octep_oq_free_ring_buffers() - Free ring buffers.
 *
 * @oq: Octeon Rx queue data structure.
 *
 * Free receive buffers in unused Rx queue descriptors.
 */
static void octep_oq_free_ring_buffers(struct octep_oq *oq)
{
	struct octep_oq_desc_hw *desc_ring = oq->desc_ring;
	int  i;

	if (!oq->desc_ring || !oq->buff_info)
		return;

	for (i = 0; i < oq->max_count; i++)  {
		if (oq->buff_info[i].page) {
			dma_unmap_page(oq->dev, desc_ring[i].buffer_ptr,
				       PAGE_SIZE, DMA_FROM_DEVICE);
			put_page(oq->buff_info[i].page);
			oq->buff_info[i].page = NULL;
			desc_ring[i].buffer_ptr = 0;
		}
	}
	octep_oq_reset_indices(oq);
}

/**
 * octep_free_oq() - Free Rx queue resources.
 *
 * @oq: Octeon Rx queue data structure.
 *
 * Free all resources of a Rx queue.
 */
static int octep_free_oq(struct octep_oq *oq)
{
	struct octep_device *oct = oq->octep_dev;
	int q_no = oq->q_no;

	octep_oq_free_ring_buffers(oq);

	vfree(oq->buff_info);

	if (oq->desc_ring)
		dma_free_coherent(oq->dev,
				  oq->max_count * OCTEP_OQ_DESC_SIZE,
				  oq->desc_ring, oq->desc_ring_dma);

	vfree(oq);
	oct->oq[q_no] = NULL;
	oct->num_oqs--;
	return 0;
}

/**
 * octep_setup_oqs() - setup resources for all Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
int octep_setup_oqs(struct octep_device *oct)
{
	int i, retval = 0;

	oct->num_oqs = 0;
	for (i = 0; i < CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf); i++) {
		retval = octep_setup_oq(oct, i);
		if (retval) {
			dev_err(&oct->pdev->dev,
				"Failed to setup OQ(RxQ)-%d.\n", i);
			goto oq_setup_err;
		}
		dev_dbg(&oct->pdev->dev, "Successfully setup OQ(RxQ)-%d.\n", i);
	}

	return 0;

oq_setup_err:
	while (i) {
		i--;
		octep_free_oq(oct->oq[i]);
	}
	return -1;
}

/**
 * octep_oq_dbell_init() - Initialize Rx queue doorbell.
 *
 * @oct: Octeon device private data structure.
 *
 * Write number of descriptors to Rx queue doorbell register.
 */
void octep_oq_dbell_init(struct octep_device *oct)
{
	int i;

	for (i = 0; i < oct->num_oqs; i++)
		writel(oct->oq[i]->max_count, oct->oq[i]->pkts_credit_reg);
}

/**
 * octep_free_oqs() - Free resources of all Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
void octep_free_oqs(struct octep_device *oct)
{
	int i;

	for (i = 0; i < CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf); i++) {
		if (!oct->oq[i])
			continue;
		octep_free_oq(oct->oq[i]);
		dev_dbg(&oct->pdev->dev,
			"Successfully freed OQ(RxQ)-%d.\n", i);
	}
}

/**
 * octep_oq_check_hw_for_pkts() - Check for new Rx packets.
 *
 * @oct: Octeon device private data structure.
 * @oq: Octeon Rx queue data structure.
 *
 * Return: packets received after previous check.
 */
static int octep_oq_check_hw_for_pkts(struct octep_device *oct,
				      struct octep_oq *oq)
{
	u32 pkt_count, new_pkts;

	pkt_count = readl(oq->pkts_sent_reg);
	new_pkts = pkt_count - oq->last_pkt_count;

	/* Clear the hardware packets counter register if the rx queue is
	 * being processed continuously with-in a single interrupt and
	 * reached half its max value.
	 * this counter is not cleared every time read, to save write cycles.
	 */
	if (unlikely(pkt_count > 0xF0000000U)) {
		writel(pkt_count, oq->pkts_sent_reg);
		pkt_count = readl(oq->pkts_sent_reg);
		new_pkts += pkt_count;
	}
	oq->last_pkt_count = pkt_count;
	oq->pkts_pending += new_pkts;
	return new_pkts;
}

/**
 * __octep_oq_process_rx() - Process hardware Rx queue and push to stack.
 *
 * @oct: Octeon device private data structure.
 * @oq: Octeon Rx queue data structure.
 * @pkts_to_process: number of packets to be processed.
 *
 * Process the new packets in Rx queue.
 * Packets larger than single Rx buffer arrive in consecutive descriptors.
 * But, count returned by the API only accounts full packets, not fragments.
 *
 * Return: number of packets processed and pushed to stack.
 */
static int __octep_oq_process_rx(struct octep_device *oct,
				 struct octep_oq *oq, u16 pkts_to_process)
{
	struct octep_oq_resp_hw_ext *resp_hw_ext = NULL;
	struct octep_rx_buffer *buff_info;
	struct octep_oq_resp_hw *resp_hw;
	u32 pkt, rx_bytes, desc_used;
	struct sk_buff *skb;
	u16 data_offset;
	u32 read_idx;

	read_idx = oq->host_read_idx;
	rx_bytes = 0;
	desc_used = 0;
	for (pkt = 0; pkt < pkts_to_process; pkt++) {
		buff_info = (struct octep_rx_buffer *)&oq->buff_info[read_idx];
		dma_unmap_page(oq->dev, oq->desc_ring[read_idx].buffer_ptr,
			       PAGE_SIZE, DMA_FROM_DEVICE);
		resp_hw = page_address(buff_info->page);
		buff_info->page = NULL;

		/* Swap the length field that is in Big-Endian to CPU */
		buff_info->len = be64_to_cpu(resp_hw->length);
		if (oct->caps_enabled & OCTEP_CAP_RX_CHECKSUM) {
			/* Extended response header is immediately after
			 * response header (resp_hw)
			 */
			resp_hw_ext = (struct octep_oq_resp_hw_ext *)
				      (resp_hw + 1);
			buff_info->len -= OCTEP_OQ_RESP_HW_EXT_SIZE;
			/* Packet Data is immediately after
			 * extended response header.
			 */
			data_offset = OCTEP_OQ_RESP_HW_SIZE +
				      OCTEP_OQ_RESP_HW_EXT_SIZE;
		} else {
			/* Data is immediately after
			 * Hardware Rx response header.
			 */
			data_offset = OCTEP_OQ_RESP_HW_SIZE;
		}
		rx_bytes += buff_info->len;

		if (buff_info->len <= oq->max_single_buffer_size) {
			skb = build_skb((void *)resp_hw, PAGE_SIZE);
			skb_reserve(skb, data_offset);
			skb_put(skb, buff_info->len);
			read_idx++;
			desc_used++;
			if (read_idx == oq->max_count)
				read_idx = 0;
		} else {
			struct skb_shared_info *shinfo;
			u16 data_len;

			skb = build_skb((void *)resp_hw, PAGE_SIZE);
			skb_reserve(skb, data_offset);
			/* Head fragment includes response header(s);
			 * subsequent fragments contains only data.
			 */
			skb_put(skb, oq->max_single_buffer_size);
			read_idx++;
			desc_used++;
			if (read_idx == oq->max_count)
				read_idx = 0;

			shinfo = skb_shinfo(skb);
			data_len = buff_info->len - oq->max_single_buffer_size;
			while (data_len) {
				dma_unmap_page(oq->dev, oq->desc_ring[read_idx].buffer_ptr,
					       PAGE_SIZE, DMA_FROM_DEVICE);
				buff_info = (struct octep_rx_buffer *)
					    &oq->buff_info[read_idx];
				if (data_len < oq->buffer_size) {
					buff_info->len = data_len;
					data_len = 0;
				} else {
					buff_info->len = oq->buffer_size;
					data_len -= oq->buffer_size;
				}

				skb_add_rx_frag(skb, shinfo->nr_frags,
						buff_info->page, 0,
						buff_info->len,
						buff_info->len);
				buff_info->page = NULL;
				read_idx++;
				desc_used++;
				if (read_idx == oq->max_count)
					read_idx = 0;
			}
		}

		skb->dev = oq->netdev;
		skb->protocol =  eth_type_trans(skb, skb->dev);
		if (resp_hw_ext &&
		    resp_hw_ext->csum_verified == OCTEP_CSUM_VERIFIED)
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;
		napi_gro_receive(oq->napi, skb);
	}

	oq->host_read_idx = read_idx;
	oq->refill_count += desc_used;
	oq->stats.packets += pkt;
	oq->stats.bytes += rx_bytes;

	return pkt;
}

/**
 * octep_oq_process_rx() - Process Rx queue.
 *
 * @oq: Octeon Rx queue data structure.
 * @budget: max number of packets can be processed in one invocation.
 *
 * Check for newly received packets and process them.
 * Keeps checking for new packets until budget is used or no new packets seen.
 *
 * Return: number of packets processed.
 */
int octep_oq_process_rx(struct octep_oq *oq, int budget)
{
	u32 pkts_available, pkts_processed, total_pkts_processed;
	struct octep_device *oct = oq->octep_dev;

	pkts_available = 0;
	pkts_processed = 0;
	total_pkts_processed = 0;
	while (total_pkts_processed < budget) {
		 /* update pending count only when current one exhausted */
		if (oq->pkts_pending == 0)
			octep_oq_check_hw_for_pkts(oct, oq);
		pkts_available = min(budget - total_pkts_processed,
				     oq->pkts_pending);
		if (!pkts_available)
			break;

		pkts_processed = __octep_oq_process_rx(oct, oq,
						       pkts_available);
		oq->pkts_pending -= pkts_processed;
		total_pkts_processed += pkts_processed;
	}

	if (oq->refill_count >= oq->refill_threshold) {
		u32 desc_refilled = octep_oq_refill(oct, oq);

		/* flush pending writes before updating credits */
		wmb();
		writel(desc_refilled, oq->pkts_credit_reg);
	}

	return total_pkts_processed;
}
