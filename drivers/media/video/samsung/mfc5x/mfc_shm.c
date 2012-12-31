/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_shm.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Shared memory interface file for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>

#include "mfc_inst.h"
#include "mfc_mem.h"
#include "mfc_buf.h"
#include "mfc_log.h"

int init_shm(struct mfc_inst_ctx *ctx)
{
#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	struct mfc_dev *dev = ctx->dev;
	struct mfc_alloc_buffer *alloc;

	if (dev->drm_playback) {
		ctx->shm = dev->drm_info.addr + MFC_SHM_OFS_DRM;
		ctx->shmofs = mfc_mem_base_ofs(dev->drm_info.base + MFC_SHM_OFS_DRM);

		memset((void *)ctx->shm, 0, MFC_SHM_SIZE);

		mfc_mem_cache_clean((void *)ctx->shm, MFC_SHM_SIZE);

		return 0;
	} else {
		alloc = _mfc_alloc_buf(ctx, MFC_SHM_SIZE, ALIGN_4B, MBT_SHM | PORT_A);

		if (alloc != NULL) {
			ctx->shm = alloc->addr;
			ctx->shmofs = mfc_mem_base_ofs(alloc->real);

			memset((void *)ctx->shm, 0, MFC_SHM_SIZE);

			mfc_mem_cache_clean((void *)ctx->shm, MFC_SHM_SIZE);

			return 0;
		}
	}
#else
	struct mfc_alloc_buffer *alloc;

	alloc = _mfc_alloc_buf(ctx, MFC_SHM_SIZE, ALIGN_4B, MBT_SHM | PORT_A);

	if (alloc != NULL) {
		ctx->shm = alloc->addr;
		ctx->shmofs = mfc_mem_base_ofs(alloc->real);

		memset((void *)ctx->shm, 0, MFC_SHM_SIZE);

		mfc_mem_cache_clean((void *)ctx->shm, MFC_SHM_SIZE);

		return 0;
	}
#endif

	mfc_err("failed alloc shared memory buffer\n");

	ctx->shm = NULL;
	ctx->shmofs = 0;

	return -1;
}

void write_shm(struct mfc_inst_ctx *ctx, unsigned int data, unsigned int offset)
{
	writel(data, (ctx->shm + offset));

	mfc_mem_cache_clean((void *)((unsigned int)(ctx->shm) + offset), 4);
}

unsigned int read_shm(struct mfc_inst_ctx *ctx, unsigned int offset)
{
	mfc_mem_cache_inv((void *)((unsigned int)(ctx->shm) + offset), 4);

	return readl(ctx->shm + offset);
}

