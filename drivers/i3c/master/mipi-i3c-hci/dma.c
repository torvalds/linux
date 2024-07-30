// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 *
 * Note: The I3C HCI v2.0 spec is still in flux. The IBI support is based on
 * v1.x of the spec and v2.0 will likely be split out.
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/io.h>

#include "hci.h"
#include "cmd.h"
#include "ibi.h"


/*
 * Software Parameter Values (somewhat arb itrary for now).
 * Some of them could be determined at run time eventually.
 */

#define XFER_RINGS			1	/* max: 8 */
#define XFER_RING_ENTRIES		16	/* max: 255 */

#define IBI_RINGS			1	/* max: 8 */
#define IBI_STATUS_RING_ENTRIES		32	/* max: 255 */
#define IBI_CHUNK_CACHELINES		1	/* max: 256 bytes equivalent */
#define IBI_CHUNK_POOL_SIZE		128	/* max: 1023 */

/*
 * Ring Header Preamble
 */

#define rhs_reg_read(r)		readl(hci->RHS_regs + (RHS_##r))
#define rhs_reg_write(r, v)	writel(v, hci->RHS_regs + (RHS_##r))

#define RHS_CONTROL			0x00
#define PREAMBLE_SIZE			GENMASK(31, 24)	/* Preamble Section Size */
#define HEADER_SIZE			GENMASK(23, 16)	/* Ring Header Size */
#define MAX_HEADER_COUNT_CAP		GENMASK(7, 4) /* HC Max Header Count */
#define MAX_HEADER_COUNT		GENMASK(3, 0) /* Driver Max Header Count */

#define RHS_RHn_OFFSET(n)		(0x04 + (n)*4)

/*
 * Ring Header (Per-Ring Bundle)
 */

#define rh_reg_read(r)		readl(rh->regs + (RH_##r))
#define rh_reg_write(r, v)	writel(v, rh->regs + (RH_##r))

#define RH_CR_SETUP			0x00	/* Command/Response Ring */
#define CR_XFER_STRUCT_SIZE		GENMASK(31, 24)
#define CR_RESP_STRUCT_SIZE		GENMASK(23, 16)
#define CR_RING_SIZE			GENMASK(8, 0)

#define RH_IBI_SETUP			0x04
#define IBI_STATUS_STRUCT_SIZE		GENMASK(31, 24)
#define IBI_STATUS_RING_SIZE		GENMASK(23, 16)
#define IBI_DATA_CHUNK_SIZE		GENMASK(12, 10)
#define IBI_DATA_CHUNK_COUNT		GENMASK(9, 0)

#define RH_CHUNK_CONTROL			0x08

#define RH_INTR_STATUS			0x10
#define RH_INTR_STATUS_ENABLE		0x14
#define RH_INTR_SIGNAL_ENABLE		0x18
#define RH_INTR_FORCE			0x1c
#define INTR_IBI_READY			BIT(12)
#define INTR_TRANSFER_COMPLETION	BIT(11)
#define INTR_RING_OP			BIT(10)
#define INTR_TRANSFER_ERR		BIT(9)
#define INTR_WARN_INS_STOP_MODE		BIT(7)
#define INTR_IBI_RING_FULL		BIT(6)
#define INTR_TRANSFER_ABORT		BIT(5)

#define RH_RING_STATUS			0x20
#define RING_STATUS_LOCKED		BIT(3)
#define RING_STATUS_ABORTED		BIT(2)
#define RING_STATUS_RUNNING		BIT(1)
#define RING_STATUS_ENABLED		BIT(0)

#define RH_RING_CONTROL			0x24
#define RING_CTRL_ABORT			BIT(2)
#define RING_CTRL_RUN_STOP		BIT(1)
#define RING_CTRL_ENABLE		BIT(0)

#define RH_RING_OPERATION1		0x28
#define RING_OP1_IBI_DEQ_PTR		GENMASK(23, 16)
#define RING_OP1_CR_SW_DEQ_PTR		GENMASK(15, 8)
#define RING_OP1_CR_ENQ_PTR		GENMASK(7, 0)

#define RH_RING_OPERATION2		0x2c
#define RING_OP2_IBI_ENQ_PTR		GENMASK(23, 16)
#define RING_OP2_CR_DEQ_PTR		GENMASK(7, 0)

#define RH_CMD_RING_BASE_LO		0x30
#define RH_CMD_RING_BASE_HI		0x34
#define RH_RESP_RING_BASE_LO		0x38
#define RH_RESP_RING_BASE_HI		0x3c
#define RH_IBI_STATUS_RING_BASE_LO	0x40
#define RH_IBI_STATUS_RING_BASE_HI	0x44
#define RH_IBI_DATA_RING_BASE_LO	0x48
#define RH_IBI_DATA_RING_BASE_HI	0x4c

#define RH_CMD_RING_SG			0x50	/* Ring Scatter Gather Support */
#define RH_RESP_RING_SG			0x54
#define RH_IBI_STATUS_RING_SG		0x58
#define RH_IBI_DATA_RING_SG		0x5c
#define RING_SG_BLP			BIT(31)	/* Buffer Vs. List Pointer */
#define RING_SG_LIST_SIZE		GENMASK(15, 0)

/*
 * Data Buffer Descriptor (in memory)
 */

#define DATA_BUF_BLP			BIT(31)	/* Buffer Vs. List Pointer */
#define DATA_BUF_IOC			BIT(30)	/* Interrupt on Completion */
#define DATA_BUF_BLOCK_SIZE		GENMASK(15, 0)


struct hci_rh_data {
	void __iomem *regs;
	void *xfer, *resp, *ibi_status, *ibi_data;
	dma_addr_t xfer_dma, resp_dma, ibi_status_dma, ibi_data_dma;
	unsigned int xfer_entries, ibi_status_entries, ibi_chunks_total;
	unsigned int xfer_struct_sz, resp_struct_sz, ibi_status_sz, ibi_chunk_sz;
	unsigned int done_ptr, ibi_chunk_ptr;
	struct hci_xfer **src_xfers;
	spinlock_t lock;
	struct completion op_done;
};

struct hci_rings_data {
	unsigned int total;
	struct hci_rh_data headers[] __counted_by(total);
};

struct hci_dma_dev_ibi_data {
	struct i3c_generic_ibi_pool *pool;
	unsigned int max_len;
};

static void hci_dma_cleanup(struct i3c_hci *hci)
{
	struct hci_rings_data *rings = hci->io_data;
	struct hci_rh_data *rh;
	unsigned int i;

	if (!rings)
		return;

	for (i = 0; i < rings->total; i++) {
		rh = &rings->headers[i];

		rh_reg_write(RING_CONTROL, 0);
		rh_reg_write(CR_SETUP, 0);
		rh_reg_write(IBI_SETUP, 0);
		rh_reg_write(INTR_SIGNAL_ENABLE, 0);

		if (rh->xfer)
			dma_free_coherent(&hci->master.dev,
					  rh->xfer_struct_sz * rh->xfer_entries,
					  rh->xfer, rh->xfer_dma);
		if (rh->resp)
			dma_free_coherent(&hci->master.dev,
					  rh->resp_struct_sz * rh->xfer_entries,
					  rh->resp, rh->resp_dma);
		kfree(rh->src_xfers);
		if (rh->ibi_status)
			dma_free_coherent(&hci->master.dev,
					  rh->ibi_status_sz * rh->ibi_status_entries,
					  rh->ibi_status, rh->ibi_status_dma);
		if (rh->ibi_data_dma)
			dma_unmap_single(&hci->master.dev, rh->ibi_data_dma,
					 rh->ibi_chunk_sz * rh->ibi_chunks_total,
					 DMA_FROM_DEVICE);
		kfree(rh->ibi_data);
	}

	rhs_reg_write(CONTROL, 0);

	kfree(rings);
	hci->io_data = NULL;
}

static int hci_dma_init(struct i3c_hci *hci)
{
	struct hci_rings_data *rings;
	struct hci_rh_data *rh;
	u32 regval;
	unsigned int i, nr_rings, xfers_sz, resps_sz;
	unsigned int ibi_status_ring_sz, ibi_data_ring_sz;
	int ret;

	regval = rhs_reg_read(CONTROL);
	nr_rings = FIELD_GET(MAX_HEADER_COUNT_CAP, regval);
	dev_info(&hci->master.dev, "%d DMA rings available\n", nr_rings);
	if (unlikely(nr_rings > 8)) {
		dev_err(&hci->master.dev, "number of rings should be <= 8\n");
		nr_rings = 8;
	}
	if (nr_rings > XFER_RINGS)
		nr_rings = XFER_RINGS;
	rings = kzalloc(struct_size(rings, headers, nr_rings), GFP_KERNEL);
	if (!rings)
		return -ENOMEM;
	hci->io_data = rings;
	rings->total = nr_rings;

	regval = FIELD_PREP(MAX_HEADER_COUNT, rings->total);
	rhs_reg_write(CONTROL, regval);

	for (i = 0; i < rings->total; i++) {
		u32 offset = rhs_reg_read(RHn_OFFSET(i));

		dev_info(&hci->master.dev, "Ring %d at offset %#x\n", i, offset);
		ret = -EINVAL;
		if (!offset)
			goto err_out;
		rh = &rings->headers[i];
		rh->regs = hci->base_regs + offset;
		spin_lock_init(&rh->lock);
		init_completion(&rh->op_done);

		rh->xfer_entries = XFER_RING_ENTRIES;

		regval = rh_reg_read(CR_SETUP);
		rh->xfer_struct_sz = FIELD_GET(CR_XFER_STRUCT_SIZE, regval);
		rh->resp_struct_sz = FIELD_GET(CR_RESP_STRUCT_SIZE, regval);
		DBG("xfer_struct_sz = %d, resp_struct_sz = %d",
		    rh->xfer_struct_sz, rh->resp_struct_sz);
		xfers_sz = rh->xfer_struct_sz * rh->xfer_entries;
		resps_sz = rh->resp_struct_sz * rh->xfer_entries;

		rh->xfer = dma_alloc_coherent(&hci->master.dev, xfers_sz,
					      &rh->xfer_dma, GFP_KERNEL);
		rh->resp = dma_alloc_coherent(&hci->master.dev, resps_sz,
					      &rh->resp_dma, GFP_KERNEL);
		rh->src_xfers =
			kmalloc_array(rh->xfer_entries, sizeof(*rh->src_xfers),
				      GFP_KERNEL);
		ret = -ENOMEM;
		if (!rh->xfer || !rh->resp || !rh->src_xfers)
			goto err_out;

		rh_reg_write(CMD_RING_BASE_LO, lower_32_bits(rh->xfer_dma));
		rh_reg_write(CMD_RING_BASE_HI, upper_32_bits(rh->xfer_dma));
		rh_reg_write(RESP_RING_BASE_LO, lower_32_bits(rh->resp_dma));
		rh_reg_write(RESP_RING_BASE_HI, upper_32_bits(rh->resp_dma));

		regval = FIELD_PREP(CR_RING_SIZE, rh->xfer_entries);
		rh_reg_write(CR_SETUP, regval);

		rh_reg_write(INTR_STATUS_ENABLE, 0xffffffff);
		rh_reg_write(INTR_SIGNAL_ENABLE, INTR_IBI_READY |
						 INTR_TRANSFER_COMPLETION |
						 INTR_RING_OP |
						 INTR_TRANSFER_ERR |
						 INTR_WARN_INS_STOP_MODE |
						 INTR_IBI_RING_FULL |
						 INTR_TRANSFER_ABORT);

		/* IBIs */

		if (i >= IBI_RINGS)
			goto ring_ready;

		regval = rh_reg_read(IBI_SETUP);
		rh->ibi_status_sz = FIELD_GET(IBI_STATUS_STRUCT_SIZE, regval);
		rh->ibi_status_entries = IBI_STATUS_RING_ENTRIES;
		rh->ibi_chunks_total = IBI_CHUNK_POOL_SIZE;

		rh->ibi_chunk_sz = dma_get_cache_alignment();
		rh->ibi_chunk_sz *= IBI_CHUNK_CACHELINES;
		/*
		 * Round IBI data chunk size to number of bytes supported by
		 * the HW. Chunk size can be 2^n number of DWORDs which is the
		 * same as 2^(n+2) bytes, where n is 0..6.
		 */
		rh->ibi_chunk_sz = umax(4, rh->ibi_chunk_sz);
		rh->ibi_chunk_sz = roundup_pow_of_two(rh->ibi_chunk_sz);
		if (rh->ibi_chunk_sz > 256) {
			ret = -EINVAL;
			goto err_out;
		}

		ibi_status_ring_sz = rh->ibi_status_sz * rh->ibi_status_entries;
		ibi_data_ring_sz = rh->ibi_chunk_sz * rh->ibi_chunks_total;

		rh->ibi_status =
			dma_alloc_coherent(&hci->master.dev, ibi_status_ring_sz,
					   &rh->ibi_status_dma, GFP_KERNEL);
		rh->ibi_data = kmalloc(ibi_data_ring_sz, GFP_KERNEL);
		ret = -ENOMEM;
		if (!rh->ibi_status || !rh->ibi_data)
			goto err_out;
		rh->ibi_data_dma =
			dma_map_single(&hci->master.dev, rh->ibi_data,
				       ibi_data_ring_sz, DMA_FROM_DEVICE);
		if (dma_mapping_error(&hci->master.dev, rh->ibi_data_dma)) {
			rh->ibi_data_dma = 0;
			ret = -ENOMEM;
			goto err_out;
		}

		rh_reg_write(IBI_STATUS_RING_BASE_LO, lower_32_bits(rh->ibi_status_dma));
		rh_reg_write(IBI_STATUS_RING_BASE_HI, upper_32_bits(rh->ibi_status_dma));
		rh_reg_write(IBI_DATA_RING_BASE_LO, lower_32_bits(rh->ibi_data_dma));
		rh_reg_write(IBI_DATA_RING_BASE_HI, upper_32_bits(rh->ibi_data_dma));

		regval = FIELD_PREP(IBI_STATUS_RING_SIZE,
				    rh->ibi_status_entries) |
			 FIELD_PREP(IBI_DATA_CHUNK_SIZE,
				    ilog2(rh->ibi_chunk_sz) - 2) |
			 FIELD_PREP(IBI_DATA_CHUNK_COUNT,
				    rh->ibi_chunks_total);
		rh_reg_write(IBI_SETUP, regval);

		regval = rh_reg_read(INTR_SIGNAL_ENABLE);
		regval |= INTR_IBI_READY;
		rh_reg_write(INTR_SIGNAL_ENABLE, regval);

ring_ready:
		rh_reg_write(RING_CONTROL, RING_CTRL_ENABLE |
					   RING_CTRL_RUN_STOP);
	}

	return 0;

err_out:
	hci_dma_cleanup(hci);
	return ret;
}

static void hci_dma_unmap_xfer(struct i3c_hci *hci,
			       struct hci_xfer *xfer_list, unsigned int n)
{
	struct hci_xfer *xfer;
	unsigned int i;

	for (i = 0; i < n; i++) {
		xfer = xfer_list + i;
		if (!xfer->data)
			continue;
		dma_unmap_single(&hci->master.dev,
				 xfer->data_dma, xfer->data_len,
				 xfer->rnw ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
	}
}

static int hci_dma_queue_xfer(struct i3c_hci *hci,
			      struct hci_xfer *xfer_list, int n)
{
	struct hci_rings_data *rings = hci->io_data;
	struct hci_rh_data *rh;
	unsigned int i, ring, enqueue_ptr;
	u32 op1_val, op2_val;
	void *buf;

	/* For now we only use ring 0 */
	ring = 0;
	rh = &rings->headers[ring];

	op1_val = rh_reg_read(RING_OPERATION1);
	enqueue_ptr = FIELD_GET(RING_OP1_CR_ENQ_PTR, op1_val);
	for (i = 0; i < n; i++) {
		struct hci_xfer *xfer = xfer_list + i;
		u32 *ring_data = rh->xfer + rh->xfer_struct_sz * enqueue_ptr;

		/* store cmd descriptor */
		*ring_data++ = xfer->cmd_desc[0];
		*ring_data++ = xfer->cmd_desc[1];
		if (hci->cmd == &mipi_i3c_hci_cmd_v2) {
			*ring_data++ = xfer->cmd_desc[2];
			*ring_data++ = xfer->cmd_desc[3];
		}

		/* first word of Data Buffer Descriptor Structure */
		if (!xfer->data)
			xfer->data_len = 0;
		*ring_data++ =
			FIELD_PREP(DATA_BUF_BLOCK_SIZE, xfer->data_len) |
			((i == n - 1) ? DATA_BUF_IOC : 0);

		/* 2nd and 3rd words of Data Buffer Descriptor Structure */
		if (xfer->data) {
			buf = xfer->bounce_buf ? xfer->bounce_buf : xfer->data;
			xfer->data_dma =
				dma_map_single(&hci->master.dev,
					       buf,
					       xfer->data_len,
					       xfer->rnw ?
						  DMA_FROM_DEVICE :
						  DMA_TO_DEVICE);
			if (dma_mapping_error(&hci->master.dev,
					      xfer->data_dma)) {
				hci_dma_unmap_xfer(hci, xfer_list, i);
				return -ENOMEM;
			}
			*ring_data++ = lower_32_bits(xfer->data_dma);
			*ring_data++ = upper_32_bits(xfer->data_dma);
		} else {
			*ring_data++ = 0;
			*ring_data++ = 0;
		}

		/* remember corresponding xfer struct */
		rh->src_xfers[enqueue_ptr] = xfer;
		/* remember corresponding ring/entry for this xfer structure */
		xfer->ring_number = ring;
		xfer->ring_entry = enqueue_ptr;

		enqueue_ptr = (enqueue_ptr + 1) % rh->xfer_entries;

		/*
		 * We may update the hardware view of the enqueue pointer
		 * only if we didn't reach its dequeue pointer.
		 */
		op2_val = rh_reg_read(RING_OPERATION2);
		if (enqueue_ptr == FIELD_GET(RING_OP2_CR_DEQ_PTR, op2_val)) {
			/* the ring is full */
			hci_dma_unmap_xfer(hci, xfer_list, i + 1);
			return -EBUSY;
		}
	}

	/* take care to update the hardware enqueue pointer atomically */
	spin_lock_irq(&rh->lock);
	op1_val = rh_reg_read(RING_OPERATION1);
	op1_val &= ~RING_OP1_CR_ENQ_PTR;
	op1_val |= FIELD_PREP(RING_OP1_CR_ENQ_PTR, enqueue_ptr);
	rh_reg_write(RING_OPERATION1, op1_val);
	spin_unlock_irq(&rh->lock);

	return 0;
}

static bool hci_dma_dequeue_xfer(struct i3c_hci *hci,
				 struct hci_xfer *xfer_list, int n)
{
	struct hci_rings_data *rings = hci->io_data;
	struct hci_rh_data *rh = &rings->headers[xfer_list[0].ring_number];
	unsigned int i;
	bool did_unqueue = false;

	/* stop the ring */
	rh_reg_write(RING_CONTROL, RING_CTRL_ABORT);
	if (wait_for_completion_timeout(&rh->op_done, HZ) == 0) {
		/*
		 * We're deep in it if ever this condition is ever met.
		 * Hardware might still be writing to memory, etc.
		 */
		dev_crit(&hci->master.dev, "unable to abort the ring\n");
		WARN_ON(1);
	}

	for (i = 0; i < n; i++) {
		struct hci_xfer *xfer = xfer_list + i;
		int idx = xfer->ring_entry;

		/*
		 * At the time the abort happened, the xfer might have
		 * completed already. If not then replace corresponding
		 * descriptor entries with a no-op.
		 */
		if (idx >= 0) {
			u32 *ring_data = rh->xfer + rh->xfer_struct_sz * idx;

			/* store no-op cmd descriptor */
			*ring_data++ = FIELD_PREP(CMD_0_ATTR, 0x7);
			*ring_data++ = 0;
			if (hci->cmd == &mipi_i3c_hci_cmd_v2) {
				*ring_data++ = 0;
				*ring_data++ = 0;
			}

			/* disassociate this xfer struct */
			rh->src_xfers[idx] = NULL;

			/* and unmap it */
			hci_dma_unmap_xfer(hci, xfer, 1);

			did_unqueue = true;
		}
	}

	/* restart the ring */
	rh_reg_write(RING_CONTROL, RING_CTRL_ENABLE);

	return did_unqueue;
}

static void hci_dma_xfer_done(struct i3c_hci *hci, struct hci_rh_data *rh)
{
	u32 op1_val, op2_val, resp, *ring_resp;
	unsigned int tid, done_ptr = rh->done_ptr;
	struct hci_xfer *xfer;

	for (;;) {
		op2_val = rh_reg_read(RING_OPERATION2);
		if (done_ptr == FIELD_GET(RING_OP2_CR_DEQ_PTR, op2_val))
			break;

		ring_resp = rh->resp + rh->resp_struct_sz * done_ptr;
		resp = *ring_resp;
		tid = RESP_TID(resp);
		DBG("resp = 0x%08x", resp);

		xfer = rh->src_xfers[done_ptr];
		if (!xfer) {
			DBG("orphaned ring entry");
		} else {
			hci_dma_unmap_xfer(hci, xfer, 1);
			xfer->ring_entry = -1;
			xfer->response = resp;
			if (tid != xfer->cmd_tid) {
				dev_err(&hci->master.dev,
					"response tid=%d when expecting %d\n",
					tid, xfer->cmd_tid);
				/* TODO: do something about it? */
			}
			if (xfer->completion)
				complete(xfer->completion);
		}

		done_ptr = (done_ptr + 1) % rh->xfer_entries;
		rh->done_ptr = done_ptr;
	}

	/* take care to update the software dequeue pointer atomically */
	spin_lock(&rh->lock);
	op1_val = rh_reg_read(RING_OPERATION1);
	op1_val &= ~RING_OP1_CR_SW_DEQ_PTR;
	op1_val |= FIELD_PREP(RING_OP1_CR_SW_DEQ_PTR, done_ptr);
	rh_reg_write(RING_OPERATION1, op1_val);
	spin_unlock(&rh->lock);
}

static int hci_dma_request_ibi(struct i3c_hci *hci, struct i3c_dev_desc *dev,
			       const struct i3c_ibi_setup *req)
{
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);
	struct i3c_generic_ibi_pool *pool;
	struct hci_dma_dev_ibi_data *dev_ibi;

	dev_ibi = kmalloc(sizeof(*dev_ibi), GFP_KERNEL);
	if (!dev_ibi)
		return -ENOMEM;
	pool = i3c_generic_ibi_alloc_pool(dev, req);
	if (IS_ERR(pool)) {
		kfree(dev_ibi);
		return PTR_ERR(pool);
	}
	dev_ibi->pool = pool;
	dev_ibi->max_len = req->max_payload_len;
	dev_data->ibi_data = dev_ibi;
	return 0;
}

static void hci_dma_free_ibi(struct i3c_hci *hci, struct i3c_dev_desc *dev)
{
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);
	struct hci_dma_dev_ibi_data *dev_ibi = dev_data->ibi_data;

	dev_data->ibi_data = NULL;
	i3c_generic_ibi_free_pool(dev_ibi->pool);
	kfree(dev_ibi);
}

static void hci_dma_recycle_ibi_slot(struct i3c_hci *hci,
				     struct i3c_dev_desc *dev,
				     struct i3c_ibi_slot *slot)
{
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);
	struct hci_dma_dev_ibi_data *dev_ibi = dev_data->ibi_data;

	i3c_generic_ibi_recycle_slot(dev_ibi->pool, slot);
}

static void hci_dma_process_ibi(struct i3c_hci *hci, struct hci_rh_data *rh)
{
	struct i3c_dev_desc *dev;
	struct i3c_hci_dev_data *dev_data;
	struct hci_dma_dev_ibi_data *dev_ibi;
	struct i3c_ibi_slot *slot;
	u32 op1_val, op2_val, ibi_status_error;
	unsigned int ptr, enq_ptr, deq_ptr;
	unsigned int ibi_size, ibi_chunks, ibi_data_offset, first_part;
	int ibi_addr, last_ptr;
	void *ring_ibi_data;
	dma_addr_t ring_ibi_data_dma;

	op1_val = rh_reg_read(RING_OPERATION1);
	deq_ptr = FIELD_GET(RING_OP1_IBI_DEQ_PTR, op1_val);

	op2_val = rh_reg_read(RING_OPERATION2);
	enq_ptr = FIELD_GET(RING_OP2_IBI_ENQ_PTR, op2_val);

	ibi_status_error = 0;
	ibi_addr = -1;
	ibi_chunks = 0;
	ibi_size = 0;
	last_ptr = -1;

	/* let's find all we can about this IBI */
	for (ptr = deq_ptr; ptr != enq_ptr;
	     ptr = (ptr + 1) % rh->ibi_status_entries) {
		u32 ibi_status, *ring_ibi_status;
		unsigned int chunks;

		ring_ibi_status = rh->ibi_status + rh->ibi_status_sz * ptr;
		ibi_status = *ring_ibi_status;
		DBG("status = %#x", ibi_status);

		if (ibi_status_error) {
			/* we no longer care */
		} else if (ibi_status & IBI_ERROR) {
			ibi_status_error = ibi_status;
		} else if (ibi_addr ==  -1) {
			ibi_addr = FIELD_GET(IBI_TARGET_ADDR, ibi_status);
		} else if (ibi_addr != FIELD_GET(IBI_TARGET_ADDR, ibi_status)) {
			/* the address changed unexpectedly */
			ibi_status_error = ibi_status;
		}

		chunks = FIELD_GET(IBI_CHUNKS, ibi_status);
		ibi_chunks += chunks;
		if (!(ibi_status & IBI_LAST_STATUS)) {
			ibi_size += chunks * rh->ibi_chunk_sz;
		} else {
			ibi_size += FIELD_GET(IBI_DATA_LENGTH, ibi_status);
			last_ptr = ptr;
			break;
		}
	}

	/* validate what we've got */

	if (last_ptr == -1) {
		/* this IBI sequence is not yet complete */
		DBG("no LAST_STATUS available (e=%d d=%d)", enq_ptr, deq_ptr);
		return;
	}
	deq_ptr = last_ptr + 1;
	deq_ptr %= rh->ibi_status_entries;

	if (ibi_status_error) {
		dev_err(&hci->master.dev, "IBI error from %#x\n", ibi_addr);
		goto done;
	}

	/* determine who this is for */
	dev = i3c_hci_addr_to_dev(hci, ibi_addr);
	if (!dev) {
		dev_err(&hci->master.dev,
			"IBI for unknown device %#x\n", ibi_addr);
		goto done;
	}

	dev_data = i3c_dev_get_master_data(dev);
	dev_ibi = dev_data->ibi_data;
	if (ibi_size > dev_ibi->max_len) {
		dev_err(&hci->master.dev, "IBI payload too big (%d > %d)\n",
			ibi_size, dev_ibi->max_len);
		goto done;
	}

	/*
	 * This ring model is not suitable for zero-copy processing of IBIs.
	 * We have the data chunk ring wrap-around to deal with, meaning
	 * that the payload might span multiple chunks beginning at the
	 * end of the ring and wrap to the start of the ring. Furthermore
	 * there is no guarantee that those chunks will be released in order
	 * and in a timely manner by the upper driver. So let's just copy
	 * them to a discrete buffer. In practice they're supposed to be
	 * small anyway.
	 */
	slot = i3c_generic_ibi_get_free_slot(dev_ibi->pool);
	if (!slot) {
		dev_err(&hci->master.dev, "no free slot for IBI\n");
		goto done;
	}

	/* copy first part of the payload */
	ibi_data_offset = rh->ibi_chunk_sz * rh->ibi_chunk_ptr;
	ring_ibi_data = rh->ibi_data + ibi_data_offset;
	ring_ibi_data_dma = rh->ibi_data_dma + ibi_data_offset;
	first_part = (rh->ibi_chunks_total - rh->ibi_chunk_ptr)
			* rh->ibi_chunk_sz;
	if (first_part > ibi_size)
		first_part = ibi_size;
	dma_sync_single_for_cpu(&hci->master.dev, ring_ibi_data_dma,
				first_part, DMA_FROM_DEVICE);
	memcpy(slot->data, ring_ibi_data, first_part);

	/* copy second part if any */
	if (ibi_size > first_part) {
		/* we wrap back to the start and copy remaining data */
		ring_ibi_data = rh->ibi_data;
		ring_ibi_data_dma = rh->ibi_data_dma;
		dma_sync_single_for_cpu(&hci->master.dev, ring_ibi_data_dma,
					ibi_size - first_part, DMA_FROM_DEVICE);
		memcpy(slot->data + first_part, ring_ibi_data,
		       ibi_size - first_part);
	}

	/* submit it */
	slot->dev = dev;
	slot->len = ibi_size;
	i3c_master_queue_ibi(dev, slot);

done:
	/* take care to update the ibi dequeue pointer atomically */
	spin_lock(&rh->lock);
	op1_val = rh_reg_read(RING_OPERATION1);
	op1_val &= ~RING_OP1_IBI_DEQ_PTR;
	op1_val |= FIELD_PREP(RING_OP1_IBI_DEQ_PTR, deq_ptr);
	rh_reg_write(RING_OPERATION1, op1_val);
	spin_unlock(&rh->lock);

	/* update the chunk pointer */
	rh->ibi_chunk_ptr += ibi_chunks;
	rh->ibi_chunk_ptr %= rh->ibi_chunks_total;

	/* and tell the hardware about freed chunks */
	rh_reg_write(CHUNK_CONTROL, rh_reg_read(CHUNK_CONTROL) + ibi_chunks);
}

static bool hci_dma_irq_handler(struct i3c_hci *hci, unsigned int mask)
{
	struct hci_rings_data *rings = hci->io_data;
	unsigned int i;
	bool handled = false;

	for (i = 0; mask && i < rings->total; i++) {
		struct hci_rh_data *rh;
		u32 status;

		if (!(mask & BIT(i)))
			continue;
		mask &= ~BIT(i);

		rh = &rings->headers[i];
		status = rh_reg_read(INTR_STATUS);
		DBG("rh%d status: %#x", i, status);
		if (!status)
			continue;
		rh_reg_write(INTR_STATUS, status);

		if (status & INTR_IBI_READY)
			hci_dma_process_ibi(hci, rh);
		if (status & (INTR_TRANSFER_COMPLETION | INTR_TRANSFER_ERR))
			hci_dma_xfer_done(hci, rh);
		if (status & INTR_RING_OP)
			complete(&rh->op_done);

		if (status & INTR_TRANSFER_ABORT) {
			dev_notice_ratelimited(&hci->master.dev,
				"ring %d: Transfer Aborted\n", i);
			mipi_i3c_hci_resume(hci);
		}
		if (status & INTR_WARN_INS_STOP_MODE)
			dev_warn_ratelimited(&hci->master.dev,
				"ring %d: Inserted Stop on Mode Change\n", i);
		if (status & INTR_IBI_RING_FULL)
			dev_err_ratelimited(&hci->master.dev,
				"ring %d: IBI Ring Full Condition\n", i);

		handled = true;
	}

	return handled;
}

const struct hci_io_ops mipi_i3c_hci_dma = {
	.init			= hci_dma_init,
	.cleanup		= hci_dma_cleanup,
	.queue_xfer		= hci_dma_queue_xfer,
	.dequeue_xfer		= hci_dma_dequeue_xfer,
	.irq_handler		= hci_dma_irq_handler,
	.request_ibi		= hci_dma_request_ibi,
	.free_ibi		= hci_dma_free_ibi,
	.recycle_ibi_slot	= hci_dma_recycle_ibi_slot,
};
