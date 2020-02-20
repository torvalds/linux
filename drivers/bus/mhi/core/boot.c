// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mhi.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include "internal.h"

void mhi_free_bhie_table(struct mhi_controller *mhi_cntrl,
			 struct image_info *image_info)
{
	int i;
	struct mhi_buf *mhi_buf = image_info->mhi_buf;

	for (i = 0; i < image_info->entries; i++, mhi_buf++)
		mhi_free_coherent(mhi_cntrl, mhi_buf->len, mhi_buf->buf,
				  mhi_buf->dma_addr);

	kfree(image_info->mhi_buf);
	kfree(image_info);
}

int mhi_alloc_bhie_table(struct mhi_controller *mhi_cntrl,
			 struct image_info **image_info,
			 size_t alloc_size)
{
	size_t seg_size = mhi_cntrl->seg_len;
	int segments = DIV_ROUND_UP(alloc_size, seg_size) + 1;
	int i;
	struct image_info *img_info;
	struct mhi_buf *mhi_buf;

	img_info = kzalloc(sizeof(*img_info), GFP_KERNEL);
	if (!img_info)
		return -ENOMEM;

	/* Allocate memory for entries */
	img_info->mhi_buf = kcalloc(segments, sizeof(*img_info->mhi_buf),
				    GFP_KERNEL);
	if (!img_info->mhi_buf)
		goto error_alloc_mhi_buf;

	/* Allocate and populate vector table */
	mhi_buf = img_info->mhi_buf;
	for (i = 0; i < segments; i++, mhi_buf++) {
		size_t vec_size = seg_size;

		/* Vector table is the last entry */
		if (i == segments - 1)
			vec_size = sizeof(struct bhi_vec_entry) * i;

		mhi_buf->len = vec_size;
		mhi_buf->buf = mhi_alloc_coherent(mhi_cntrl, vec_size,
						  &mhi_buf->dma_addr,
						  GFP_KERNEL);
		if (!mhi_buf->buf)
			goto error_alloc_segment;
	}

	img_info->bhi_vec = img_info->mhi_buf[segments - 1].buf;
	img_info->entries = segments;
	*image_info = img_info;

	return 0;

error_alloc_segment:
	for (--i, --mhi_buf; i >= 0; i--, mhi_buf--)
		mhi_free_coherent(mhi_cntrl, mhi_buf->len, mhi_buf->buf,
				  mhi_buf->dma_addr);

error_alloc_mhi_buf:
	kfree(img_info);

	return -ENOMEM;
}
