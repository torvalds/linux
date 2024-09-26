// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DMA driver for AMD Queue-based DMA Subsystem
 *
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/dma-map-ops.h>
#include <linux/platform_device.h>
#include <linux/platform_data/amd_qdma.h>
#include <linux/regmap.h>

#include "qdma.h"

#define CHAN_STR(q)		(((q)->dir == DMA_MEM_TO_DEV) ? "H2C" : "C2H")
#define QDMA_REG_OFF(d, r)	((d)->roffs[r].off)

/* MMIO regmap config for all QDMA registers */
static const struct regmap_config qdma_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static inline struct qdma_queue *to_qdma_queue(struct dma_chan *chan)
{
	return container_of(chan, struct qdma_queue, vchan.chan);
}

static inline struct qdma_mm_vdesc *to_qdma_vdesc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct qdma_mm_vdesc, vdesc);
}

static inline u32 qdma_get_intr_ring_idx(struct qdma_device *qdev)
{
	u32 idx;

	idx = qdev->qintr_rings[qdev->qintr_ring_idx++].ridx;
	qdev->qintr_ring_idx %= qdev->qintr_ring_num;

	return idx;
}

static u64 qdma_get_field(const struct qdma_device *qdev, const u32 *data,
			  enum qdma_reg_fields field)
{
	const struct qdma_reg_field *f = &qdev->rfields[field];
	u16 low_pos, hi_pos, low_bit, hi_bit;
	u64 value = 0, mask;

	low_pos = f->lsb / BITS_PER_TYPE(*data);
	hi_pos = f->msb / BITS_PER_TYPE(*data);

	if (low_pos == hi_pos) {
		low_bit = f->lsb % BITS_PER_TYPE(*data);
		hi_bit = f->msb % BITS_PER_TYPE(*data);
		mask = GENMASK(hi_bit, low_bit);
		value = (data[low_pos] & mask) >> low_bit;
	} else if (hi_pos == low_pos + 1) {
		low_bit = f->lsb % BITS_PER_TYPE(*data);
		hi_bit = low_bit + (f->msb - f->lsb);
		value = ((u64)data[hi_pos] << BITS_PER_TYPE(*data)) |
			data[low_pos];
		mask = GENMASK_ULL(hi_bit, low_bit);
		value = (value & mask) >> low_bit;
	} else {
		hi_bit = f->msb % BITS_PER_TYPE(*data);
		mask = GENMASK(hi_bit, 0);
		value = data[hi_pos] & mask;
		low_bit = f->msb - f->lsb - hi_bit;
		value <<= low_bit;
		low_bit -= 32;
		value |= (u64)data[hi_pos - 1] << low_bit;
		mask = GENMASK(31, 32 - low_bit);
		value |= (data[hi_pos - 2] & mask) >> low_bit;
	}

	return value;
}

static void qdma_set_field(const struct qdma_device *qdev, u32 *data,
			   enum qdma_reg_fields field, u64 value)
{
	const struct qdma_reg_field *f = &qdev->rfields[field];
	u16 low_pos, hi_pos, low_bit;

	low_pos = f->lsb / BITS_PER_TYPE(*data);
	hi_pos = f->msb / BITS_PER_TYPE(*data);
	low_bit = f->lsb % BITS_PER_TYPE(*data);

	data[low_pos++] |= value << low_bit;
	if (low_pos <= hi_pos)
		data[low_pos++] |= (u32)(value >> (32 - low_bit));
	if (low_pos <= hi_pos)
		data[low_pos] |= (u32)(value >> (64 - low_bit));
}

static inline int qdma_reg_write(const struct qdma_device *qdev,
				 const u32 *data, enum qdma_regs reg)
{
	const struct qdma_reg *r = &qdev->roffs[reg];
	int ret;

	if (r->count > 1)
		ret = regmap_bulk_write(qdev->regmap, r->off, data, r->count);
	else
		ret = regmap_write(qdev->regmap, r->off, *data);

	return ret;
}

static inline int qdma_reg_read(const struct qdma_device *qdev, u32 *data,
				enum qdma_regs reg)
{
	const struct qdma_reg *r = &qdev->roffs[reg];
	int ret;

	if (r->count > 1)
		ret = regmap_bulk_read(qdev->regmap, r->off, data, r->count);
	else
		ret = regmap_read(qdev->regmap, r->off, data);

	return ret;
}

static int qdma_context_cmd_execute(const struct qdma_device *qdev,
				    enum qdma_ctxt_type type,
				    enum qdma_ctxt_cmd cmd, u16 index)
{
	u32 value = 0;
	int ret;

	qdma_set_field(qdev, &value, QDMA_REGF_CMD_INDX, index);
	qdma_set_field(qdev, &value, QDMA_REGF_CMD_CMD, cmd);
	qdma_set_field(qdev, &value, QDMA_REGF_CMD_TYPE, type);

	ret = qdma_reg_write(qdev, &value, QDMA_REGO_CTXT_CMD);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(qdev->regmap,
				       QDMA_REG_OFF(qdev, QDMA_REGO_CTXT_CMD),
				       value,
				       !qdma_get_field(qdev, &value,
						       QDMA_REGF_CMD_BUSY),
				       QDMA_POLL_INTRVL_US,
				       QDMA_POLL_TIMEOUT_US);
	if (ret) {
		qdma_err(qdev, "Context command execution timed out");
		return ret;
	}

	return 0;
}

static int qdma_context_write_data(const struct qdma_device *qdev,
				   const u32 *data)
{
	u32 mask[QDMA_CTXT_REGMAP_LEN];
	int ret;

	memset(mask, ~0, sizeof(mask));

	ret = qdma_reg_write(qdev, mask, QDMA_REGO_CTXT_MASK);
	if (ret)
		return ret;

	ret = qdma_reg_write(qdev, data, QDMA_REGO_CTXT_DATA);
	if (ret)
		return ret;

	return 0;
}

static void qdma_prep_sw_desc_context(const struct qdma_device *qdev,
				      const struct qdma_ctxt_sw_desc *ctxt,
				      u32 *data)
{
	memset(data, 0, QDMA_CTXT_REGMAP_LEN * sizeof(*data));
	qdma_set_field(qdev, data, QDMA_REGF_DESC_BASE, ctxt->desc_base);
	qdma_set_field(qdev, data, QDMA_REGF_IRQ_VEC, ctxt->vec);
	qdma_set_field(qdev, data, QDMA_REGF_FUNCTION_ID, qdev->fid);

	qdma_set_field(qdev, data, QDMA_REGF_DESC_SIZE, QDMA_DESC_SIZE_32B);
	qdma_set_field(qdev, data, QDMA_REGF_RING_ID, QDMA_DEFAULT_RING_ID);
	qdma_set_field(qdev, data, QDMA_REGF_QUEUE_MODE, QDMA_QUEUE_OP_MM);
	qdma_set_field(qdev, data, QDMA_REGF_IRQ_ENABLE, 1);
	qdma_set_field(qdev, data, QDMA_REGF_WBK_ENABLE, 1);
	qdma_set_field(qdev, data, QDMA_REGF_WBI_CHECK, 1);
	qdma_set_field(qdev, data, QDMA_REGF_IRQ_ARM, 1);
	qdma_set_field(qdev, data, QDMA_REGF_IRQ_AGG, 1);
	qdma_set_field(qdev, data, QDMA_REGF_WBI_INTVL_ENABLE, 1);
	qdma_set_field(qdev, data, QDMA_REGF_QUEUE_ENABLE, 1);
	qdma_set_field(qdev, data, QDMA_REGF_MRKR_DISABLE, 1);
}

static void qdma_prep_intr_context(const struct qdma_device *qdev,
				   const struct qdma_ctxt_intr *ctxt,
				   u32 *data)
{
	memset(data, 0, QDMA_CTXT_REGMAP_LEN * sizeof(*data));
	qdma_set_field(qdev, data, QDMA_REGF_INTR_AGG_BASE, ctxt->agg_base);
	qdma_set_field(qdev, data, QDMA_REGF_INTR_VECTOR, ctxt->vec);
	qdma_set_field(qdev, data, QDMA_REGF_INTR_SIZE, ctxt->size);
	qdma_set_field(qdev, data, QDMA_REGF_INTR_VALID, ctxt->valid);
	qdma_set_field(qdev, data, QDMA_REGF_INTR_COLOR, ctxt->color);
	qdma_set_field(qdev, data, QDMA_REGF_INTR_FUNCTION_ID, qdev->fid);
}

static void qdma_prep_fmap_context(const struct qdma_device *qdev,
				   const struct qdma_ctxt_fmap *ctxt,
				   u32 *data)
{
	memset(data, 0, QDMA_CTXT_REGMAP_LEN * sizeof(*data));
	qdma_set_field(qdev, data, QDMA_REGF_QUEUE_BASE, ctxt->qbase);
	qdma_set_field(qdev, data, QDMA_REGF_QUEUE_MAX, ctxt->qmax);
}

/*
 * Program the indirect context register space
 *
 * Once the queue is enabled, context is dynamically updated by hardware. Any
 * modification of the context through this API when the queue is enabled can
 * result in unexpected behavior. Reading the context when the queue is enabled
 * is not recommended as it can result in reduced performance.
 */
static int qdma_prog_context(struct qdma_device *qdev, enum qdma_ctxt_type type,
			     enum qdma_ctxt_cmd cmd, u16 index, u32 *ctxt)
{
	int ret;

	mutex_lock(&qdev->ctxt_lock);
	if (cmd == QDMA_CTXT_WRITE) {
		ret = qdma_context_write_data(qdev, ctxt);
		if (ret)
			goto failed;
	}

	ret = qdma_context_cmd_execute(qdev, type, cmd, index);
	if (ret)
		goto failed;

	if (cmd == QDMA_CTXT_READ) {
		ret = qdma_reg_read(qdev, ctxt, QDMA_REGO_CTXT_DATA);
		if (ret)
			goto failed;
	}

failed:
	mutex_unlock(&qdev->ctxt_lock);

	return ret;
}

static int qdma_check_queue_status(struct qdma_device *qdev,
				   enum dma_transfer_direction dir, u16 qid)
{
	u32 status, data[QDMA_CTXT_REGMAP_LEN] = {0};
	enum qdma_ctxt_type type;
	int ret;

	if (dir == DMA_MEM_TO_DEV)
		type = QDMA_CTXT_DESC_SW_H2C;
	else
		type = QDMA_CTXT_DESC_SW_C2H;
	ret = qdma_prog_context(qdev, type, QDMA_CTXT_READ, qid, data);
	if (ret)
		return ret;

	status = qdma_get_field(qdev, data, QDMA_REGF_QUEUE_ENABLE);
	if (status) {
		qdma_err(qdev, "queue %d already in use", qid);
		return -EBUSY;
	}

	return 0;
}

static int qdma_clear_queue_context(const struct qdma_queue *queue)
{
	enum qdma_ctxt_type h2c_types[] = { QDMA_CTXT_DESC_SW_H2C,
					    QDMA_CTXT_DESC_HW_H2C,
					    QDMA_CTXT_DESC_CR_H2C,
					    QDMA_CTXT_PFTCH, };
	enum qdma_ctxt_type c2h_types[] = { QDMA_CTXT_DESC_SW_C2H,
					    QDMA_CTXT_DESC_HW_C2H,
					    QDMA_CTXT_DESC_CR_C2H,
					    QDMA_CTXT_PFTCH, };
	struct qdma_device *qdev = queue->qdev;
	enum qdma_ctxt_type *type;
	int ret, num, i;

	if (queue->dir == DMA_MEM_TO_DEV) {
		type = h2c_types;
		num = ARRAY_SIZE(h2c_types);
	} else {
		type = c2h_types;
		num = ARRAY_SIZE(c2h_types);
	}
	for (i = 0; i < num; i++) {
		ret = qdma_prog_context(qdev, type[i], QDMA_CTXT_CLEAR,
					queue->qid, NULL);
		if (ret) {
			qdma_err(qdev, "Failed to clear ctxt %d", type[i]);
			return ret;
		}
	}

	return 0;
}

static int qdma_setup_fmap_context(struct qdma_device *qdev)
{
	u32 ctxt[QDMA_CTXT_REGMAP_LEN];
	struct qdma_ctxt_fmap fmap;
	int ret;

	ret = qdma_prog_context(qdev, QDMA_CTXT_FMAP, QDMA_CTXT_CLEAR,
				qdev->fid, NULL);
	if (ret) {
		qdma_err(qdev, "Failed clearing context");
		return ret;
	}

	fmap.qbase = 0;
	fmap.qmax = qdev->chan_num * 2;
	qdma_prep_fmap_context(qdev, &fmap, ctxt);
	ret = qdma_prog_context(qdev, QDMA_CTXT_FMAP, QDMA_CTXT_WRITE,
				qdev->fid, ctxt);
	if (ret)
		qdma_err(qdev, "Failed setup fmap, ret %d", ret);

	return ret;
}

static int qdma_setup_queue_context(struct qdma_device *qdev,
				    const struct qdma_ctxt_sw_desc *sw_desc,
				    enum dma_transfer_direction dir, u16 qid)
{
	u32 ctxt[QDMA_CTXT_REGMAP_LEN];
	enum qdma_ctxt_type type;
	int ret;

	if (dir == DMA_MEM_TO_DEV)
		type = QDMA_CTXT_DESC_SW_H2C;
	else
		type = QDMA_CTXT_DESC_SW_C2H;

	qdma_prep_sw_desc_context(qdev, sw_desc, ctxt);
	/* Setup SW descriptor context */
	ret = qdma_prog_context(qdev, type, QDMA_CTXT_WRITE, qid, ctxt);
	if (ret)
		qdma_err(qdev, "Failed setup SW desc ctxt for queue: %d", qid);

	return ret;
}

/*
 * Enable or disable memory-mapped DMA engines
 * 1: enable, 0: disable
 */
static int qdma_sgdma_control(struct qdma_device *qdev, u32 ctrl)
{
	int ret;

	ret = qdma_reg_write(qdev, &ctrl, QDMA_REGO_MM_H2C_CTRL);
	ret |= qdma_reg_write(qdev, &ctrl, QDMA_REGO_MM_C2H_CTRL);

	return ret;
}

static int qdma_get_hw_info(struct qdma_device *qdev)
{
	struct qdma_platdata *pdata = dev_get_platdata(&qdev->pdev->dev);
	u32 value = 0;
	int ret;

	ret = qdma_reg_read(qdev, &value, QDMA_REGO_QUEUE_COUNT);
	if (ret)
		return ret;

	value = qdma_get_field(qdev, &value, QDMA_REGF_QUEUE_COUNT) + 1;
	if (pdata->max_mm_channels * 2 > value) {
		qdma_err(qdev, "not enough hw queues %d", value);
		return -EINVAL;
	}
	qdev->chan_num = pdata->max_mm_channels;

	ret = qdma_reg_read(qdev, &qdev->fid, QDMA_REGO_FUNC_ID);
	if (ret)
		return ret;

	qdma_info(qdev, "max channel %d, function id %d",
		  qdev->chan_num, qdev->fid);

	return 0;
}

static inline int qdma_update_pidx(const struct qdma_queue *queue, u16 pidx)
{
	struct qdma_device *qdev = queue->qdev;

	return regmap_write(qdev->regmap, queue->pidx_reg,
			    pidx | QDMA_QUEUE_ARM_BIT);
}

static inline int qdma_update_cidx(const struct qdma_queue *queue,
				   u16 ridx, u16 cidx)
{
	struct qdma_device *qdev = queue->qdev;

	return regmap_write(qdev->regmap, queue->cidx_reg,
			    ((u32)ridx << 16) | cidx);
}

/**
 * qdma_free_vdesc - Free descriptor
 * @vdesc: Virtual DMA descriptor
 */
static void qdma_free_vdesc(struct virt_dma_desc *vdesc)
{
	struct qdma_mm_vdesc *vd = to_qdma_vdesc(vdesc);

	kfree(vd);
}

static int qdma_alloc_queues(struct qdma_device *qdev,
			     enum dma_transfer_direction dir)
{
	struct qdma_queue *q, **queues;
	u32 i, pidx_base;
	int ret;

	if (dir == DMA_MEM_TO_DEV) {
		queues = &qdev->h2c_queues;
		pidx_base = QDMA_REG_OFF(qdev, QDMA_REGO_H2C_PIDX);
	} else {
		queues = &qdev->c2h_queues;
		pidx_base = QDMA_REG_OFF(qdev, QDMA_REGO_C2H_PIDX);
	}

	*queues = devm_kcalloc(&qdev->pdev->dev, qdev->chan_num, sizeof(*q),
			       GFP_KERNEL);
	if (!*queues)
		return -ENOMEM;

	for (i = 0; i < qdev->chan_num; i++) {
		ret = qdma_check_queue_status(qdev, dir, i);
		if (ret)
			return ret;

		q = &(*queues)[i];
		q->ring_size = QDMA_DEFAULT_RING_SIZE;
		q->idx_mask = q->ring_size - 2;
		q->qdev = qdev;
		q->dir = dir;
		q->qid = i;
		q->pidx_reg = pidx_base + i * QDMA_DMAP_REG_STRIDE;
		q->cidx_reg = QDMA_REG_OFF(qdev, QDMA_REGO_INTR_CIDX) +
				i * QDMA_DMAP_REG_STRIDE;
		q->vchan.desc_free = qdma_free_vdesc;
		vchan_init(&q->vchan, &qdev->dma_dev);
	}

	return 0;
}

static int qdma_device_verify(struct qdma_device *qdev)
{
	u32 value;
	int ret;

	ret = regmap_read(qdev->regmap, QDMA_IDENTIFIER_REGOFF, &value);
	if (ret)
		return ret;

	value = FIELD_GET(QDMA_IDENTIFIER_MASK, value);
	if (value != QDMA_IDENTIFIER) {
		qdma_err(qdev, "Invalid identifier");
		return -ENODEV;
	}
	qdev->rfields = qdma_regfs_default;
	qdev->roffs = qdma_regos_default;

	return 0;
}

static int qdma_device_setup(struct qdma_device *qdev)
{
	struct device *dev = &qdev->pdev->dev;
	u32 ring_sz = QDMA_DEFAULT_RING_SIZE;
	int ret = 0;

	while (dev && get_dma_ops(dev))
		dev = dev->parent;
	if (!dev) {
		qdma_err(qdev, "dma device not found");
		return -EINVAL;
	}
	set_dma_ops(&qdev->pdev->dev, get_dma_ops(dev));

	ret = qdma_setup_fmap_context(qdev);
	if (ret) {
		qdma_err(qdev, "Failed setup fmap context");
		return ret;
	}

	/* Setup global ring buffer size at QDMA_DEFAULT_RING_ID index */
	ret = qdma_reg_write(qdev, &ring_sz, QDMA_REGO_RING_SIZE);
	if (ret) {
		qdma_err(qdev, "Failed to setup ring %d of size %ld",
			 QDMA_DEFAULT_RING_ID, QDMA_DEFAULT_RING_SIZE);
		return ret;
	}

	/* Enable memory-mapped DMA engine in both directions */
	ret = qdma_sgdma_control(qdev, 1);
	if (ret) {
		qdma_err(qdev, "Failed to SGDMA with error %d", ret);
		return ret;
	}

	ret = qdma_alloc_queues(qdev, DMA_MEM_TO_DEV);
	if (ret) {
		qdma_err(qdev, "Failed to alloc H2C queues, ret %d", ret);
		return ret;
	}

	ret = qdma_alloc_queues(qdev, DMA_DEV_TO_MEM);
	if (ret) {
		qdma_err(qdev, "Failed to alloc C2H queues, ret %d", ret);
		return ret;
	}

	return 0;
}

/**
 * qdma_free_queue_resources() - Free queue resources
 * @chan: DMA channel
 */
static void qdma_free_queue_resources(struct dma_chan *chan)
{
	struct qdma_queue *queue = to_qdma_queue(chan);
	struct qdma_device *qdev = queue->qdev;
	struct device *dev = qdev->dma_dev.dev;

	qdma_clear_queue_context(queue);
	vchan_free_chan_resources(&queue->vchan);
	dma_free_coherent(dev, queue->ring_size * QDMA_MM_DESC_SIZE,
			  queue->desc_base, queue->dma_desc_base);
}

/**
 * qdma_alloc_queue_resources() - Allocate queue resources
 * @chan: DMA channel
 */
static int qdma_alloc_queue_resources(struct dma_chan *chan)
{
	struct qdma_queue *queue = to_qdma_queue(chan);
	struct qdma_device *qdev = queue->qdev;
	struct qdma_ctxt_sw_desc desc;
	size_t size;
	int ret;

	ret = qdma_clear_queue_context(queue);
	if (ret)
		return ret;

	size = queue->ring_size * QDMA_MM_DESC_SIZE;
	queue->desc_base = dma_alloc_coherent(qdev->dma_dev.dev, size,
					      &queue->dma_desc_base,
					      GFP_KERNEL);
	if (!queue->desc_base) {
		qdma_err(qdev, "Failed to allocate descriptor ring");
		return -ENOMEM;
	}

	/* Setup SW descriptor queue context for DMA memory map */
	desc.vec = qdma_get_intr_ring_idx(qdev);
	desc.desc_base = queue->dma_desc_base;
	ret = qdma_setup_queue_context(qdev, &desc, queue->dir, queue->qid);
	if (ret) {
		qdma_err(qdev, "Failed to setup SW desc ctxt for %s",
			 chan->name);
		dma_free_coherent(qdev->dma_dev.dev, size, queue->desc_base,
				  queue->dma_desc_base);
		return ret;
	}

	queue->pidx = 0;
	queue->cidx = 0;

	return 0;
}

static bool qdma_filter_fn(struct dma_chan *chan, void *param)
{
	struct qdma_queue *queue = to_qdma_queue(chan);
	struct qdma_queue_info *info = param;

	return info->dir == queue->dir;
}

static int qdma_xfer_start(struct qdma_queue *queue)
{
	struct qdma_device *qdev = queue->qdev;
	int ret;

	if (!vchan_next_desc(&queue->vchan))
		return 0;

	qdma_dbg(qdev, "Tnx kickoff with P: %d for %s%d",
		 queue->issued_vdesc->pidx, CHAN_STR(queue), queue->qid);

	ret = qdma_update_pidx(queue, queue->issued_vdesc->pidx);
	if (ret) {
		qdma_err(qdev, "Failed to update PIDX to %d for %s queue: %d",
			 queue->pidx, CHAN_STR(queue), queue->qid);
	}

	return ret;
}

static void qdma_issue_pending(struct dma_chan *chan)
{
	struct qdma_queue *queue = to_qdma_queue(chan);
	unsigned long flags;

	spin_lock_irqsave(&queue->vchan.lock, flags);
	if (vchan_issue_pending(&queue->vchan)) {
		if (queue->submitted_vdesc) {
			queue->issued_vdesc = queue->submitted_vdesc;
			queue->submitted_vdesc = NULL;
		}
		qdma_xfer_start(queue);
	}

	spin_unlock_irqrestore(&queue->vchan.lock, flags);
}

static struct qdma_mm_desc *qdma_get_desc(struct qdma_queue *q)
{
	struct qdma_mm_desc *desc;

	if (((q->pidx + 1) & q->idx_mask) == q->cidx)
		return NULL;

	desc = q->desc_base + q->pidx;
	q->pidx = (q->pidx + 1) & q->idx_mask;

	return desc;
}

static int qdma_hw_enqueue(struct qdma_queue *q, struct qdma_mm_vdesc *vdesc)
{
	struct qdma_mm_desc *desc;
	struct scatterlist *sg;
	u64 addr, *src, *dst;
	u32 rest, len;
	int ret = 0;
	u32 i;

	if (!vdesc->sg_len)
		return 0;

	if (q->dir == DMA_MEM_TO_DEV) {
		dst = &vdesc->dev_addr;
		src = &addr;
	} else {
		dst = &addr;
		src = &vdesc->dev_addr;
	}

	for_each_sg(vdesc->sgl, sg, vdesc->sg_len, i) {
		addr = sg_dma_address(sg) + vdesc->sg_off;
		rest = sg_dma_len(sg) - vdesc->sg_off;
		while (rest) {
			len = min_t(u32, rest, QDMA_MM_DESC_MAX_LEN);
			desc = qdma_get_desc(q);
			if (!desc) {
				ret = -EBUSY;
				goto out;
			}

			desc->src_addr = cpu_to_le64(*src);
			desc->dst_addr = cpu_to_le64(*dst);
			desc->len = cpu_to_le32(len);

			vdesc->dev_addr += len;
			vdesc->sg_off += len;
			vdesc->pending_descs++;
			addr += len;
			rest -= len;
		}
		vdesc->sg_off = 0;
	}
out:
	vdesc->sg_len -= i;
	vdesc->pidx = q->pidx;
	return ret;
}

static void qdma_fill_pending_vdesc(struct qdma_queue *q)
{
	struct virt_dma_chan *vc = &q->vchan;
	struct qdma_mm_vdesc *vdesc = NULL;
	struct virt_dma_desc *vd;
	int ret;

	if (!list_empty(&vc->desc_issued)) {
		vd = &q->issued_vdesc->vdesc;
		list_for_each_entry_from(vd, &vc->desc_issued, node) {
			vdesc = to_qdma_vdesc(vd);
			ret = qdma_hw_enqueue(q, vdesc);
			if (ret) {
				q->issued_vdesc = vdesc;
				return;
			}
		}
		q->issued_vdesc = vdesc;
	}

	if (list_empty(&vc->desc_submitted))
		return;

	if (q->submitted_vdesc)
		vd = &q->submitted_vdesc->vdesc;
	else
		vd = list_first_entry(&vc->desc_submitted, typeof(*vd), node);

	list_for_each_entry_from(vd, &vc->desc_submitted, node) {
		vdesc = to_qdma_vdesc(vd);
		ret = qdma_hw_enqueue(q, vdesc);
		if (ret)
			break;
	}
	q->submitted_vdesc = vdesc;
}

static dma_cookie_t qdma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct virt_dma_chan *vc = to_virt_chan(tx->chan);
	struct qdma_queue *q = to_qdma_queue(&vc->chan);
	struct virt_dma_desc *vd;
	unsigned long flags;
	dma_cookie_t cookie;

	vd = container_of(tx, struct virt_dma_desc, tx);
	spin_lock_irqsave(&vc->lock, flags);
	cookie = dma_cookie_assign(tx);

	list_move_tail(&vd->node, &vc->desc_submitted);
	qdma_fill_pending_vdesc(q);
	spin_unlock_irqrestore(&vc->lock, flags);

	return cookie;
}

static struct dma_async_tx_descriptor *
qdma_prep_device_sg(struct dma_chan *chan, struct scatterlist *sgl,
		    unsigned int sg_len, enum dma_transfer_direction dir,
		    unsigned long flags, void *context)
{
	struct qdma_queue *q = to_qdma_queue(chan);
	struct dma_async_tx_descriptor *tx;
	struct qdma_mm_vdesc *vdesc;

	vdesc = kzalloc(sizeof(*vdesc), GFP_NOWAIT);
	if (!vdesc)
		return NULL;
	vdesc->sgl = sgl;
	vdesc->sg_len = sg_len;
	if (dir == DMA_MEM_TO_DEV)
		vdesc->dev_addr = q->cfg.dst_addr;
	else
		vdesc->dev_addr = q->cfg.src_addr;

	tx = vchan_tx_prep(&q->vchan, &vdesc->vdesc, flags);
	tx->tx_submit = qdma_tx_submit;

	return tx;
}

static int qdma_device_config(struct dma_chan *chan,
			      struct dma_slave_config *cfg)
{
	struct qdma_queue *q = to_qdma_queue(chan);

	memcpy(&q->cfg, cfg, sizeof(*cfg));

	return 0;
}

static int qdma_arm_err_intr(const struct qdma_device *qdev)
{
	u32 value = 0;

	qdma_set_field(qdev, &value, QDMA_REGF_ERR_INT_FUNC, qdev->fid);
	qdma_set_field(qdev, &value, QDMA_REGF_ERR_INT_VEC, qdev->err_irq_idx);
	qdma_set_field(qdev, &value, QDMA_REGF_ERR_INT_ARM, 1);

	return qdma_reg_write(qdev, &value, QDMA_REGO_ERR_INT);
}

static irqreturn_t qdma_error_isr(int irq, void *data)
{
	struct qdma_device *qdev = data;
	u32 err_stat = 0;
	int ret;

	ret = qdma_reg_read(qdev, &err_stat, QDMA_REGO_ERR_STAT);
	if (ret) {
		qdma_err(qdev, "read error state failed, ret %d", ret);
		goto out;
	}

	qdma_err(qdev, "global error %d", err_stat);
	ret = qdma_reg_write(qdev, &err_stat, QDMA_REGO_ERR_STAT);
	if (ret)
		qdma_err(qdev, "clear error state failed, ret %d", ret);

out:
	qdma_arm_err_intr(qdev);
	return IRQ_HANDLED;
}

static irqreturn_t qdma_queue_isr(int irq, void *data)
{
	struct qdma_intr_ring *intr = data;
	struct qdma_queue *q = NULL;
	struct qdma_device *qdev;
	u32 index, comp_desc;
	u64 intr_ent;
	u8 color;
	int ret;
	u16 qid;

	qdev = intr->qdev;
	index = intr->cidx;
	while (1) {
		struct virt_dma_desc *vd;
		struct qdma_mm_vdesc *vdesc;
		unsigned long flags;
		u32 cidx;

		intr_ent = le64_to_cpu(intr->base[index]);
		color = FIELD_GET(QDMA_INTR_MASK_COLOR, intr_ent);
		if (color != intr->color)
			break;

		qid = FIELD_GET(QDMA_INTR_MASK_QID, intr_ent);
		if (FIELD_GET(QDMA_INTR_MASK_TYPE, intr_ent))
			q = qdev->c2h_queues;
		else
			q = qdev->h2c_queues;
		q += qid;

		cidx = FIELD_GET(QDMA_INTR_MASK_CIDX, intr_ent);

		spin_lock_irqsave(&q->vchan.lock, flags);
		comp_desc = (cidx - q->cidx) & q->idx_mask;

		vd = vchan_next_desc(&q->vchan);
		if (!vd)
			goto skip;

		vdesc = to_qdma_vdesc(vd);
		while (comp_desc > vdesc->pending_descs) {
			list_del(&vd->node);
			vchan_cookie_complete(vd);
			comp_desc -= vdesc->pending_descs;
			vd = vchan_next_desc(&q->vchan);
			vdesc = to_qdma_vdesc(vd);
		}
		vdesc->pending_descs -= comp_desc;
		if (!vdesc->pending_descs && QDMA_VDESC_QUEUED(vdesc)) {
			list_del(&vd->node);
			vchan_cookie_complete(vd);
		}
		q->cidx = cidx;

		qdma_fill_pending_vdesc(q);
		qdma_xfer_start(q);

skip:
		spin_unlock_irqrestore(&q->vchan.lock, flags);

		/*
		 * Wrap the index value and flip the expected color value if
		 * interrupt aggregation PIDX has wrapped around.
		 */
		index++;
		index &= QDMA_INTR_RING_IDX_MASK;
		if (!index)
			intr->color = !intr->color;
	}

	/*
	 * Update the software interrupt aggregation ring CIDX if a valid entry
	 * was found.
	 */
	if (q) {
		qdma_dbg(qdev, "update intr ring%d %d", intr->ridx, index);

		/*
		 * Record the last read index of status descriptor from the
		 * interrupt aggregation ring.
		 */
		intr->cidx = index;

		ret = qdma_update_cidx(q, intr->ridx, index);
		if (ret) {
			qdma_err(qdev, "Failed to update IRQ CIDX");
			return IRQ_NONE;
		}
	}

	return IRQ_HANDLED;
}

static int qdma_init_error_irq(struct qdma_device *qdev)
{
	struct device *dev = &qdev->pdev->dev;
	int ret;
	u32 vec;

	vec = qdev->queue_irq_start - 1;

	ret = devm_request_threaded_irq(dev, vec, NULL, qdma_error_isr,
					IRQF_ONESHOT, "amd-qdma-error", qdev);
	if (ret) {
		qdma_err(qdev, "Failed to request error IRQ vector: %d", vec);
		return ret;
	}

	ret = qdma_arm_err_intr(qdev);
	if (ret)
		qdma_err(qdev, "Failed to arm err interrupt, ret %d", ret);

	return ret;
}

static int qdmam_alloc_qintr_rings(struct qdma_device *qdev)
{
	u32 ctxt[QDMA_CTXT_REGMAP_LEN];
	struct device *dev = &qdev->pdev->dev;
	struct qdma_intr_ring *ring;
	struct qdma_ctxt_intr intr_ctxt;
	u32 vector;
	int ret, i;

	qdev->qintr_ring_num = qdev->queue_irq_num;
	qdev->qintr_rings = devm_kcalloc(dev, qdev->qintr_ring_num,
					 sizeof(*qdev->qintr_rings),
					 GFP_KERNEL);
	if (!qdev->qintr_rings)
		return -ENOMEM;

	vector = qdev->queue_irq_start;
	for (i = 0; i < qdev->qintr_ring_num; i++, vector++) {
		ring = &qdev->qintr_rings[i];
		ring->qdev = qdev;
		ring->msix_id = qdev->err_irq_idx + i + 1;
		ring->ridx = i;
		ring->color = 1;
		ring->base = dmam_alloc_coherent(dev, QDMA_INTR_RING_SIZE,
						 &ring->dev_base, GFP_KERNEL);
		if (!ring->base) {
			qdma_err(qdev, "Failed to alloc intr ring %d", i);
			return -ENOMEM;
		}
		intr_ctxt.agg_base = QDMA_INTR_RING_BASE(ring->dev_base);
		intr_ctxt.size = (QDMA_INTR_RING_SIZE - 1) / 4096;
		intr_ctxt.vec = ring->msix_id;
		intr_ctxt.valid = true;
		intr_ctxt.color = true;
		ret = qdma_prog_context(qdev, QDMA_CTXT_INTR_COAL,
					QDMA_CTXT_CLEAR, ring->ridx, NULL);
		if (ret) {
			qdma_err(qdev, "Failed clear intr ctx, ret %d", ret);
			return ret;
		}

		qdma_prep_intr_context(qdev, &intr_ctxt, ctxt);
		ret = qdma_prog_context(qdev, QDMA_CTXT_INTR_COAL,
					QDMA_CTXT_WRITE, ring->ridx, ctxt);
		if (ret) {
			qdma_err(qdev, "Failed setup intr ctx, ret %d", ret);
			return ret;
		}

		ret = devm_request_threaded_irq(dev, vector, NULL,
						qdma_queue_isr, IRQF_ONESHOT,
						"amd-qdma-queue", ring);
		if (ret) {
			qdma_err(qdev, "Failed to request irq %d", vector);
			return ret;
		}
	}

	return 0;
}

static int qdma_intr_init(struct qdma_device *qdev)
{
	int ret;

	ret = qdma_init_error_irq(qdev);
	if (ret) {
		qdma_err(qdev, "Failed to init error IRQs, ret %d", ret);
		return ret;
	}

	ret = qdmam_alloc_qintr_rings(qdev);
	if (ret) {
		qdma_err(qdev, "Failed to init queue IRQs, ret %d", ret);
		return ret;
	}

	return 0;
}

static void amd_qdma_remove(struct platform_device *pdev)
{
	struct qdma_device *qdev = platform_get_drvdata(pdev);

	qdma_sgdma_control(qdev, 0);
	dma_async_device_unregister(&qdev->dma_dev);

	mutex_destroy(&qdev->ctxt_lock);
}

static int amd_qdma_probe(struct platform_device *pdev)
{
	struct qdma_platdata *pdata = dev_get_platdata(&pdev->dev);
	struct qdma_device *qdev;
	struct resource *res;
	void __iomem *regs;
	int ret;

	qdev = devm_kzalloc(&pdev->dev, sizeof(*qdev), GFP_KERNEL);
	if (!qdev)
		return -ENOMEM;

	platform_set_drvdata(pdev, qdev);
	qdev->pdev = pdev;
	mutex_init(&qdev->ctxt_lock);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		qdma_err(qdev, "Failed to get IRQ resource");
		ret = -ENODEV;
		goto failed;
	}
	qdev->err_irq_idx = pdata->irq_index;
	qdev->queue_irq_start = res->start + 1;
	qdev->queue_irq_num = resource_size(res) - 1;

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		qdma_err(qdev, "Failed to map IO resource, err %d", ret);
		goto failed;
	}

	qdev->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					     &qdma_regmap_config);
	if (IS_ERR(qdev->regmap)) {
		ret = PTR_ERR(qdev->regmap);
		qdma_err(qdev, "Regmap init failed, err %d", ret);
		goto failed;
	}

	ret = qdma_device_verify(qdev);
	if (ret)
		goto failed;

	ret = qdma_get_hw_info(qdev);
	if (ret)
		goto failed;

	INIT_LIST_HEAD(&qdev->dma_dev.channels);

	ret = qdma_device_setup(qdev);
	if (ret)
		goto failed;

	ret = qdma_intr_init(qdev);
	if (ret) {
		qdma_err(qdev, "Failed to initialize IRQs %d", ret);
		goto failed_disable_engine;
	}

	dma_cap_set(DMA_SLAVE, qdev->dma_dev.cap_mask);
	dma_cap_set(DMA_PRIVATE, qdev->dma_dev.cap_mask);

	qdev->dma_dev.dev = &pdev->dev;
	qdev->dma_dev.filter.map = pdata->device_map;
	qdev->dma_dev.filter.mapcnt = qdev->chan_num * 2;
	qdev->dma_dev.filter.fn = qdma_filter_fn;
	qdev->dma_dev.device_alloc_chan_resources = qdma_alloc_queue_resources;
	qdev->dma_dev.device_free_chan_resources = qdma_free_queue_resources;
	qdev->dma_dev.device_prep_slave_sg = qdma_prep_device_sg;
	qdev->dma_dev.device_config = qdma_device_config;
	qdev->dma_dev.device_issue_pending = qdma_issue_pending;
	qdev->dma_dev.device_tx_status = dma_cookie_status;
	qdev->dma_dev.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);

	ret = dma_async_device_register(&qdev->dma_dev);
	if (ret) {
		qdma_err(qdev, "Failed to register AMD QDMA: %d", ret);
		goto failed_disable_engine;
	}

	return 0;

failed_disable_engine:
	qdma_sgdma_control(qdev, 0);
failed:
	mutex_destroy(&qdev->ctxt_lock);
	qdma_err(qdev, "Failed to probe AMD QDMA driver");
	return ret;
}

static struct platform_driver amd_qdma_driver = {
	.driver		= {
		.name = "amd-qdma",
	},
	.probe		= amd_qdma_probe,
	.remove_new	= amd_qdma_remove,
};

module_platform_driver(amd_qdma_driver);

MODULE_DESCRIPTION("AMD QDMA driver");
MODULE_AUTHOR("XRT Team <runtimeca39d@amd.com>");
MODULE_LICENSE("GPL");
