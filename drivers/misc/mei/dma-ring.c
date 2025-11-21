// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2016-2018 Intel Corporation. All rights reserved.
 */
#include <linux/dma-mapping.h>
#include <linux/mei.h>

#include "mei_dev.h"

/**
 * mei_dmam_dscr_alloc() - allocate a managed coherent buffer
 *     for the dma descriptor
 * @dev: mei_device
 * @dscr: dma descriptor
 *
 * Return:
 * * 0       - on success or zero allocation request
 * * -EINVAL - if size is not power of 2
 * * -ENOMEM - of allocation has failed
 */
static int mei_dmam_dscr_alloc(struct mei_device *dev,
			       struct mei_dma_dscr *dscr)
{
	if (!dscr->size)
		return 0;

	if (WARN_ON(!is_power_of_2(dscr->size)))
		return -EINVAL;

	if (dscr->vaddr)
		return 0;

	dscr->vaddr = dmam_alloc_coherent(dev->parent, dscr->size, &dscr->daddr,
					  GFP_KERNEL);
	if (!dscr->vaddr)
		return -ENOMEM;

	return 0;
}

/**
 * mei_dmam_dscr_free() - free a managed coherent buffer
 *     from the dma descriptor
 * @dev: mei_device
 * @dscr: dma descriptor
 */
static void mei_dmam_dscr_free(struct mei_device *dev,
			       struct mei_dma_dscr *dscr)
{
	if (!dscr->vaddr)
		return;

	dmam_free_coherent(dev->parent, dscr->size, dscr->vaddr, dscr->daddr);
	dscr->vaddr = NULL;
}

/**
 * mei_dmam_ring_free() - free dma ring buffers
 * @dev: mei device
 */
void mei_dmam_ring_free(struct mei_device *dev)
{
	int i;

	for (i = 0; i < DMA_DSCR_NUM; i++)
		mei_dmam_dscr_free(dev, &dev->dr_dscr[i]);
}

/**
 * mei_dmam_ring_alloc() - allocate dma ring buffers
 * @dev: mei device
 *
 * Return: -ENOMEM on allocation failure 0 otherwise
 */
int mei_dmam_ring_alloc(struct mei_device *dev)
{
	int i;

	for (i = 0; i < DMA_DSCR_NUM; i++)
		if (mei_dmam_dscr_alloc(dev, &dev->dr_dscr[i]))
			goto err;

	return 0;

err:
	mei_dmam_ring_free(dev);
	return -ENOMEM;
}

/**
 * mei_dma_ring_is_allocated() - check if dma ring is allocated
 * @dev: mei device
 *
 * Return: true if dma ring is allocated
 */
bool mei_dma_ring_is_allocated(struct mei_device *dev)
{
	return !!dev->dr_dscr[DMA_DSCR_HOST].vaddr;
}

static inline
struct hbm_dma_ring_ctrl *mei_dma_ring_ctrl(struct mei_device *dev)
{
	return (struct hbm_dma_ring_ctrl *)dev->dr_dscr[DMA_DSCR_CTRL].vaddr;
}

/**
 * mei_dma_ring_reset() - reset the dma control block
 * @dev: mei device
 */
void mei_dma_ring_reset(struct mei_device *dev)
{
	struct hbm_dma_ring_ctrl *ctrl = mei_dma_ring_ctrl(dev);

	if (!ctrl)
		return;

	memset(ctrl, 0, sizeof(*ctrl));
}

/**
 * mei_dma_copy_from() - copy from dma ring into buffer
 * @dev: mei device
 * @buf: data buffer
 * @offset: offset in slots.
 * @n: number of slots to copy.
 *
 * Return: number of bytes copied
 */
static size_t mei_dma_copy_from(struct mei_device *dev, unsigned char *buf,
				u32 offset, u32 n)
{
	unsigned char *dbuf = dev->dr_dscr[DMA_DSCR_DEVICE].vaddr;

	size_t b_offset = offset << 2;
	size_t b_n = n << 2;

	memcpy(buf, dbuf + b_offset, b_n);

	return b_n;
}

/**
 * mei_dma_copy_to() - copy to a buffer to the dma ring
 * @dev: mei device
 * @buf: data buffer
 * @offset: offset in slots.
 * @n: number of slots to copy.
 *
 * Return: number of bytes copied
 */
static size_t mei_dma_copy_to(struct mei_device *dev, unsigned char *buf,
			      u32 offset, u32 n)
{
	unsigned char *hbuf = dev->dr_dscr[DMA_DSCR_HOST].vaddr;

	size_t b_offset = offset << 2;
	size_t b_n = n << 2;

	memcpy(hbuf + b_offset, buf, b_n);

	return b_n;
}

/**
 * mei_dma_ring_read() - read data from the ring
 * @dev: mei device
 * @buf: buffer to read into: may be NULL in case of dropping the data.
 * @len: length to read.
 */
void mei_dma_ring_read(struct mei_device *dev, unsigned char *buf, u32 len)
{
	struct hbm_dma_ring_ctrl *ctrl = mei_dma_ring_ctrl(dev);
	u32 dbuf_depth;
	u32 rd_idx, rem, slots;

	if (WARN_ON(!ctrl))
		return;

	dev_dbg(&dev->dev, "reading from dma %u bytes\n", len);

	if (!len)
		return;

	dbuf_depth = dev->dr_dscr[DMA_DSCR_DEVICE].size >> 2;
	rd_idx = READ_ONCE(ctrl->dbuf_rd_idx) & (dbuf_depth - 1);
	slots = mei_data2slots(len);

	/* if buf is NULL we drop the packet by advancing the pointer.*/
	if (!buf)
		goto out;

	if (rd_idx + slots > dbuf_depth) {
		buf += mei_dma_copy_from(dev, buf, rd_idx, dbuf_depth - rd_idx);
		rem = slots - (dbuf_depth - rd_idx);
		rd_idx = 0;
	} else {
		rem = slots;
	}

	mei_dma_copy_from(dev, buf, rd_idx, rem);
out:
	WRITE_ONCE(ctrl->dbuf_rd_idx, ctrl->dbuf_rd_idx + slots);
}

static inline u32 mei_dma_ring_hbuf_depth(struct mei_device *dev)
{
	return dev->dr_dscr[DMA_DSCR_HOST].size >> 2;
}

/**
 * mei_dma_ring_empty_slots() - calaculate number of empty slots in dma ring
 * @dev: mei_device
 *
 * Return: number of empty slots
 */
u32 mei_dma_ring_empty_slots(struct mei_device *dev)
{
	struct hbm_dma_ring_ctrl *ctrl = mei_dma_ring_ctrl(dev);
	u32 wr_idx, rd_idx, hbuf_depth, empty;

	if (!mei_dma_ring_is_allocated(dev))
		return 0;

	if (WARN_ON(!ctrl))
		return 0;

	/* easier to work in slots */
	hbuf_depth = mei_dma_ring_hbuf_depth(dev);
	rd_idx = READ_ONCE(ctrl->hbuf_rd_idx);
	wr_idx = READ_ONCE(ctrl->hbuf_wr_idx);

	if (rd_idx > wr_idx)
		empty = rd_idx - wr_idx;
	else
		empty = hbuf_depth - (wr_idx - rd_idx);

	return empty;
}

/**
 * mei_dma_ring_write - write data to dma ring host buffer
 *
 * @dev: mei_device
 * @buf: data will be written
 * @len: data length
 */
void mei_dma_ring_write(struct mei_device *dev, unsigned char *buf, u32 len)
{
	struct hbm_dma_ring_ctrl *ctrl = mei_dma_ring_ctrl(dev);
	u32 hbuf_depth;
	u32 wr_idx, rem, slots;

	if (WARN_ON(!ctrl))
		return;

	dev_dbg(&dev->dev, "writing to dma %u bytes\n", len);
	hbuf_depth = mei_dma_ring_hbuf_depth(dev);
	wr_idx = READ_ONCE(ctrl->hbuf_wr_idx) & (hbuf_depth - 1);
	slots = mei_data2slots(len);

	if (wr_idx + slots > hbuf_depth) {
		buf += mei_dma_copy_to(dev, buf, wr_idx, hbuf_depth - wr_idx);
		rem = slots - (hbuf_depth - wr_idx);
		wr_idx = 0;
	} else {
		rem = slots;
	}

	mei_dma_copy_to(dev, buf, wr_idx, rem);

	WRITE_ONCE(ctrl->hbuf_wr_idx, ctrl->hbuf_wr_idx + slots);
}
