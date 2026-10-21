// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023-2026 Intel Corporation */
#include <linux/dev_printk.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timekeeping.h>

#include "issei_dev.h"
#include "hw_msg.h"

static inline size_t __issei_dma_size(const struct issei_dma *dma)
{
	return dma->length.h2f + dma->length.f2h + dma->length.ctl;
}

/**
 * issei_dmam_setup - setup DMA buffer and clean it
 * @idev: issei device object
 *
 * Return: 0 on success, <0 on failures
 */
int issei_dmam_setup(struct issei_device *idev)
{
	struct issei_dma *dma = &idev->dma;
	size_t size;

	size = __issei_dma_size(dma);
	if (!size)
		return -EINVAL;

	if (!dma->vaddr)
		dma->vaddr = dmam_alloc_coherent(idev->parent, size, &dma->daddr,
						 GFP_KERNEL | __GFP_ZERO);
	if (dma->vaddr)
		memset(dma->vaddr, 0, size);
	return dma->vaddr ? 0 : -ENOMEM;
}

static inline struct control_buffer *__dma_get_ctl_buf(struct issei_dma *dma)
{
	return dma->vaddr + dma->length.h2f + dma->length.f2h;
}

static bool __issei_dma_is_read_busy(struct issei_dma *dma)
{
	struct control_buffer *ctl = __dma_get_ctl_buf(dma);

	return ctl->f2h_counter_wr != ctl->f2h_counter_rd;
}

static bool __issei_dma_is_write_busy(struct issei_dma *dma)
{
	struct control_buffer *ctl = __dma_get_ctl_buf(dma);

	return ctl->h2f_counter_wr != ctl->h2f_counter_rd;
}

static void __issei_dma_read_finalize(struct issei_device *idev)
{
	struct control_buffer *ctl = __dma_get_ctl_buf(&idev->dma);

	dev_dbg(&idev->dev, "ctl->f2h_counter_rd %u\n", ctl->f2h_counter_rd);
	/* No need to check overflow - the firmware counters overflow the same way */
	ctl->f2h_counter_rd++;
}

static void __issei_dma_write_finalize(struct issei_device *idev)
{
	struct control_buffer *ctl = __dma_get_ctl_buf(&idev->dma);

	dev_dbg(&idev->dev, "ctl->h2f_counter_wr %u\n", ctl->h2f_counter_wr);
	/* No need to check overflow - the firmware counters overflow the same way */
	ctl->h2f_counter_wr++;
}

/**
 * issei_dma_write - write data package to DMA
 * @idev: issei device object
 * @data: data atructure
 *
 * Return: 0 on success, <0 on failures
 */
int issei_dma_write(struct issei_device *idev, const struct issei_dma_data *data)
{
	u8 *write_buf = idev->dma.vaddr;
	struct ham_message_header *hdr = (struct ham_message_header *)write_buf;

	if (data->length > idev->dma.length.h2f - sizeof(*hdr)) {
		dev_err(&idev->dev, "Message is too big\n");
		return -EMSGSIZE;
	}

	if (__issei_dma_is_write_busy(&idev->dma)) {
		if (ktime_ms_delta(ktime_get(), idev->last_write_ts) > ISSEI_WRITE_TIMEOUT_MSEC) {
			dev_err(&idev->dev, "Write stuck in queue\n");
			return -EIO;
		}
		dev_info(&idev->dev, "Write is busy\n");
		return -EBUSY;
	}

	hdr->length = data->length;
	hdr->fw_id = data->fw_id;
	hdr->host_id = data->host_id;
	hdr->flags = data->flags;
	hdr->status = data->status;
	hdr->reserved = 0;

	memcpy(write_buf + sizeof(*hdr), data->buf, data->length);

	__issei_dma_write_finalize(idev);
	idev->last_write_ts = ktime_get();
	return 0;
}

/**
 * issei_dma_read - read data package from DMA
 * @idev: issei device object
 * @data: data atructure
 *
 * Return: %0 on success, <0 on failures
 */
int issei_dma_read(struct issei_device *idev, struct issei_dma_data *data)
{
	u8 *read_buf = idev->dma.vaddr + idev->dma.length.h2f;
	struct ham_message_header *hdr = (struct ham_message_header *)read_buf;

	if (!__issei_dma_is_read_busy(&idev->dma)) {
		dev_dbg(&idev->dev, "Nothing to read\n");
		return -ENODATA;
	}

	dev_dbg(&idev->dev, "Reading header\n");
	data->length = hdr->length;
	data->fw_id = hdr->fw_id;
	data->host_id = hdr->host_id;
	data->flags = hdr->flags;
	data->status = hdr->status;

	if (data->length > idev->dma.length.f2h - sizeof(*hdr)) {
		dev_err(&idev->dev, "Message length %u is bigger than buffer %zu\n",
			data->length, idev->dma.length.f2h - sizeof(*hdr));
		return -EIO;
	}

	dev_dbg(&idev->dev, "Reading data (size %u)\n", data->length);
	data->buf = kmemdup(read_buf + sizeof(*hdr), data->length, GFP_KERNEL);
	if (!data->buf)
		return -ENOMEM;
	__issei_dma_read_finalize(idev);
	return 0;
}
