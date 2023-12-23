// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) VF Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>

#include "octep_vf_config.h"
#include "octep_vf_main.h"

static void octep_vf_oq_reset_indices(struct octep_vf_oq *oq)
{
	oq->host_read_idx = 0;
	oq->host_refill_idx = 0;
	oq->refill_count = 0;
	oq->last_pkt_count = 0;
	oq->pkts_pending = 0;
}

/**
 * octep_vf_oq_fill_ring_buffers() - fill initial receive buffers for Rx ring.
 *
 * @oq: Octeon Rx queue data structure.
 *
 * Return: 0, if successfully filled receive buffers for all descriptors.
 *         -1, if failed to allocate a buffer or failed to map for DMA.
 */
static int octep_vf_oq_fill_ring_buffers(struct octep_vf_oq *oq)
{
	struct octep_vf_oq_desc_hw *desc_ring = oq->desc_ring;
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
 * octep_vf_setup_oq() - Setup a Rx queue.
 *
 * @oct: Octeon device private data structure.
 * @q_no: Rx queue number to be setup.
 *
 * Allocate resources for a Rx queue.
 */
static int octep_vf_setup_oq(struct octep_vf_device *oct, int q_no)
{
	struct octep_vf_oq *oq;
	u32 desc_ring_size;

	oq = vzalloc(sizeof(*oq));
	if (!oq)
		goto create_oq_fail;
	oct->oq[q_no] = oq;

	oq->octep_vf_dev = oct;
	oq->netdev = oct->netdev;
	oq->dev = &oct->pdev->dev;
	oq->q_no = q_no;
	oq->max_count = CFG_GET_OQ_NUM_DESC(oct->conf);
	oq->ring_size_mask = oq->max_count - 1;
	oq->buffer_size = CFG_GET_OQ_BUF_SIZE(oct->conf);
	oq->max_single_buffer_size = oq->buffer_size - OCTEP_VF_OQ_RESP_HW_SIZE;

	/* When the hardware/firmware supports additional capabilities,
	 * additional header is filled-in by Octeon after length field in
	 * Rx packets. this header contains additional packet information.
	 */
	if (oct->fw_info.rx_ol_flags)
		oq->max_single_buffer_size -= OCTEP_VF_OQ_RESP_HW_EXT_SIZE;

	oq->refill_threshold = CFG_GET_OQ_REFILL_THRESHOLD(oct->conf);

	desc_ring_size = oq->max_count * OCTEP_VF_OQ_DESC_SIZE;
	oq->desc_ring = dma_alloc_coherent(oq->dev, desc_ring_size,
					   &oq->desc_ring_dma, GFP_KERNEL);

	if (unlikely(!oq->desc_ring)) {
		dev_err(oq->dev,
			"Failed to allocate DMA memory for OQ-%d !!\n", q_no);
		goto desc_dma_alloc_err;
	}

	oq->buff_info = (struct octep_vf_rx_buffer *)
			vzalloc(oq->max_count * OCTEP_VF_OQ_RECVBUF_SIZE);
	if (unlikely(!oq->buff_info)) {
		dev_err(&oct->pdev->dev,
			"Failed to allocate buffer info for OQ-%d\n", q_no);
		goto buf_list_err;
	}

	if (octep_vf_oq_fill_ring_buffers(oq))
		goto oq_fill_buff_err;

	octep_vf_oq_reset_indices(oq);
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
 * octep_vf_oq_free_ring_buffers() - Free ring buffers.
 *
 * @oq: Octeon Rx queue data structure.
 *
 * Free receive buffers in unused Rx queue descriptors.
 */
static void octep_vf_oq_free_ring_buffers(struct octep_vf_oq *oq)
{
	struct octep_vf_oq_desc_hw *desc_ring = oq->desc_ring;
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
	octep_vf_oq_reset_indices(oq);
}

/**
 * octep_vf_free_oq() - Free Rx queue resources.
 *
 * @oq: Octeon Rx queue data structure.
 *
 * Free all resources of a Rx queue.
 */
static int octep_vf_free_oq(struct octep_vf_oq *oq)
{
	struct octep_vf_device *oct = oq->octep_vf_dev;
	int q_no = oq->q_no;

	octep_vf_oq_free_ring_buffers(oq);

	if (oq->buff_info)
		vfree(oq->buff_info);

	if (oq->desc_ring)
		dma_free_coherent(oq->dev,
				  oq->max_count * OCTEP_VF_OQ_DESC_SIZE,
				  oq->desc_ring, oq->desc_ring_dma);

	vfree(oq);
	oct->oq[q_no] = NULL;
	oct->num_oqs--;
	return 0;
}

/**
 * octep_vf_setup_oqs() - setup resources for all Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
int octep_vf_setup_oqs(struct octep_vf_device *oct)
{
	int i, retval = 0;

	oct->num_oqs = 0;
	for (i = 0; i < CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf); i++) {
		retval = octep_vf_setup_oq(oct, i);
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
		octep_vf_free_oq(oct->oq[i]);
	}
	return -1;
}

/**
 * octep_vf_oq_dbell_init() - Initialize Rx queue doorbell.
 *
 * @oct: Octeon device private data structure.
 *
 * Write number of descriptors to Rx queue doorbell register.
 */
void octep_vf_oq_dbell_init(struct octep_vf_device *oct)
{
	int i;

	for (i = 0; i < oct->num_oqs; i++)
		writel(oct->oq[i]->max_count, oct->oq[i]->pkts_credit_reg);
}

/**
 * octep_vf_free_oqs() - Free resources of all Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
void octep_vf_free_oqs(struct octep_vf_device *oct)
{
	int i;

	for (i = 0; i < CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf); i++) {
		if (!oct->oq[i])
			continue;
		octep_vf_free_oq(oct->oq[i]);
		dev_dbg(&oct->pdev->dev,
			"Successfully freed OQ(RxQ)-%d.\n", i);
	}
}
