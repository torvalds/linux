// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU HEVC codec driver
 *
 * Copyright (C) 2020 Safran Passenger Innovations LLC
 */

#include <linux/types.h>
#include <media/v4l2-mem2mem.h>

#include "hantro.h"
#include "hantro_hw.h"

#define VERT_FILTER_RAM_SIZE 8 /* bytes per pixel row */
/*
 * BSD control data of current picture at tile border
 * 128 bits per 4x4 tile = 128/(8*4) bytes per row
 */
#define BSD_CTRL_RAM_SIZE 4 /* bytes per pixel row */
/* tile border coefficients of filter */
#define VERT_SAO_RAM_SIZE 48 /* bytes per pixel */

#define SCALING_LIST_SIZE (16 * 64)

#define MAX_TILE_COLS 20
#define MAX_TILE_ROWS 22

void hantro_hevc_ref_init(struct hantro_ctx *ctx)
{
	struct hantro_hevc_dec_hw_ctx *hevc_dec = &ctx->hevc_dec;

	hevc_dec->ref_bufs_used = 0;
}

dma_addr_t hantro_hevc_get_ref_buf(struct hantro_ctx *ctx,
				   int poc)
{
	struct hantro_hevc_dec_hw_ctx *hevc_dec = &ctx->hevc_dec;
	int i;

	/* Find the reference buffer in already known ones */
	for (i = 0;  i < NUM_REF_PICTURES; i++) {
		if (hevc_dec->ref_bufs_poc[i] == poc) {
			hevc_dec->ref_bufs_used |= 1 << i;
			return hevc_dec->ref_bufs[i].dma;
		}
	}

	return 0;
}

int hantro_hevc_add_ref_buf(struct hantro_ctx *ctx, int poc, dma_addr_t addr)
{
	struct hantro_hevc_dec_hw_ctx *hevc_dec = &ctx->hevc_dec;
	int i;

	/* Add a new reference buffer */
	for (i = 0; i < NUM_REF_PICTURES; i++) {
		if (!(hevc_dec->ref_bufs_used & 1 << i)) {
			hevc_dec->ref_bufs_used |= 1 << i;
			hevc_dec->ref_bufs_poc[i] = poc;
			hevc_dec->ref_bufs[i].dma = addr;
			return 0;
		}
	}

	return -EINVAL;
}

static int tile_buffer_reallocate(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	struct hantro_hevc_dec_hw_ctx *hevc_dec = &ctx->hevc_dec;
	const struct hantro_hevc_dec_ctrls *ctrls = &ctx->hevc_dec.ctrls;
	const struct v4l2_ctrl_hevc_pps *pps = ctrls->pps;
	const struct v4l2_ctrl_hevc_sps *sps = ctrls->sps;
	unsigned int num_tile_cols = pps->num_tile_columns_minus1 + 1;
	unsigned int height64 = (sps->pic_height_in_luma_samples + 63) & ~63;
	unsigned int size;

	if (num_tile_cols <= 1 ||
	    num_tile_cols <= hevc_dec->num_tile_cols_allocated)
		return 0;

	/* Need to reallocate due to tiles passed via PPS */
	if (hevc_dec->tile_filter.cpu) {
		dma_free_coherent(vpu->dev, hevc_dec->tile_filter.size,
				  hevc_dec->tile_filter.cpu,
				  hevc_dec->tile_filter.dma);
		hevc_dec->tile_filter.cpu = NULL;
	}

	if (hevc_dec->tile_sao.cpu) {
		dma_free_coherent(vpu->dev, hevc_dec->tile_sao.size,
				  hevc_dec->tile_sao.cpu,
				  hevc_dec->tile_sao.dma);
		hevc_dec->tile_sao.cpu = NULL;
	}

	if (hevc_dec->tile_bsd.cpu) {
		dma_free_coherent(vpu->dev, hevc_dec->tile_bsd.size,
				  hevc_dec->tile_bsd.cpu,
				  hevc_dec->tile_bsd.dma);
		hevc_dec->tile_bsd.cpu = NULL;
	}

	size = VERT_FILTER_RAM_SIZE * height64 * (num_tile_cols - 1);
	hevc_dec->tile_filter.cpu = dma_alloc_coherent(vpu->dev, size,
						       &hevc_dec->tile_filter.dma,
						       GFP_KERNEL);
	if (!hevc_dec->tile_filter.cpu)
		goto err_free_tile_buffers;
	hevc_dec->tile_filter.size = size;

	size = VERT_SAO_RAM_SIZE * height64 * (num_tile_cols - 1);
	hevc_dec->tile_sao.cpu = dma_alloc_coherent(vpu->dev, size,
						    &hevc_dec->tile_sao.dma,
						    GFP_KERNEL);
	if (!hevc_dec->tile_sao.cpu)
		goto err_free_tile_buffers;
	hevc_dec->tile_sao.size = size;

	size = BSD_CTRL_RAM_SIZE * height64 * (num_tile_cols - 1);
	hevc_dec->tile_bsd.cpu = dma_alloc_coherent(vpu->dev, size,
						    &hevc_dec->tile_bsd.dma,
						    GFP_KERNEL);
	if (!hevc_dec->tile_bsd.cpu)
		goto err_free_tile_buffers;
	hevc_dec->tile_bsd.size = size;

	hevc_dec->num_tile_cols_allocated = num_tile_cols;

	return 0;

err_free_tile_buffers:
	if (hevc_dec->tile_filter.cpu)
		dma_free_coherent(vpu->dev, hevc_dec->tile_filter.size,
				  hevc_dec->tile_filter.cpu,
				  hevc_dec->tile_filter.dma);
	hevc_dec->tile_filter.cpu = NULL;

	if (hevc_dec->tile_sao.cpu)
		dma_free_coherent(vpu->dev, hevc_dec->tile_sao.size,
				  hevc_dec->tile_sao.cpu,
				  hevc_dec->tile_sao.dma);
	hevc_dec->tile_sao.cpu = NULL;

	if (hevc_dec->tile_bsd.cpu)
		dma_free_coherent(vpu->dev, hevc_dec->tile_bsd.size,
				  hevc_dec->tile_bsd.cpu,
				  hevc_dec->tile_bsd.dma);
	hevc_dec->tile_bsd.cpu = NULL;

	return -ENOMEM;
}

int hantro_hevc_dec_prepare_run(struct hantro_ctx *ctx)
{
	struct hantro_hevc_dec_hw_ctx *hevc_ctx = &ctx->hevc_dec;
	struct hantro_hevc_dec_ctrls *ctrls = &hevc_ctx->ctrls;
	int ret;

	hantro_start_prepare_run(ctx);

	ctrls->decode_params =
		hantro_get_ctrl(ctx, V4L2_CID_MPEG_VIDEO_HEVC_DECODE_PARAMS);
	if (WARN_ON(!ctrls->decode_params))
		return -EINVAL;

	ctrls->scaling =
		hantro_get_ctrl(ctx, V4L2_CID_MPEG_VIDEO_HEVC_SCALING_MATRIX);
	if (WARN_ON(!ctrls->scaling))
		return -EINVAL;

	ctrls->sps =
		hantro_get_ctrl(ctx, V4L2_CID_MPEG_VIDEO_HEVC_SPS);
	if (WARN_ON(!ctrls->sps))
		return -EINVAL;

	ctrls->pps =
		hantro_get_ctrl(ctx, V4L2_CID_MPEG_VIDEO_HEVC_PPS);
	if (WARN_ON(!ctrls->pps))
		return -EINVAL;

	ret = tile_buffer_reallocate(ctx);
	if (ret)
		return ret;

	return 0;
}

void hantro_hevc_dec_exit(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	struct hantro_hevc_dec_hw_ctx *hevc_dec = &ctx->hevc_dec;

	if (hevc_dec->tile_sizes.cpu)
		dma_free_coherent(vpu->dev, hevc_dec->tile_sizes.size,
				  hevc_dec->tile_sizes.cpu,
				  hevc_dec->tile_sizes.dma);
	hevc_dec->tile_sizes.cpu = NULL;

	if (hevc_dec->scaling_lists.cpu)
		dma_free_coherent(vpu->dev, hevc_dec->scaling_lists.size,
				  hevc_dec->scaling_lists.cpu,
				  hevc_dec->scaling_lists.dma);
	hevc_dec->scaling_lists.cpu = NULL;

	if (hevc_dec->tile_filter.cpu)
		dma_free_coherent(vpu->dev, hevc_dec->tile_filter.size,
				  hevc_dec->tile_filter.cpu,
				  hevc_dec->tile_filter.dma);
	hevc_dec->tile_filter.cpu = NULL;

	if (hevc_dec->tile_sao.cpu)
		dma_free_coherent(vpu->dev, hevc_dec->tile_sao.size,
				  hevc_dec->tile_sao.cpu,
				  hevc_dec->tile_sao.dma);
	hevc_dec->tile_sao.cpu = NULL;

	if (hevc_dec->tile_bsd.cpu)
		dma_free_coherent(vpu->dev, hevc_dec->tile_bsd.size,
				  hevc_dec->tile_bsd.cpu,
				  hevc_dec->tile_bsd.dma);
	hevc_dec->tile_bsd.cpu = NULL;
}

int hantro_hevc_dec_init(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	struct hantro_hevc_dec_hw_ctx *hevc_dec = &ctx->hevc_dec;
	unsigned int size;

	memset(hevc_dec, 0, sizeof(*hevc_dec));

	/*
	 * Maximum number of tiles times width and height (2 bytes each),
	 * rounding up to next 16 bytes boundary + one extra 16 byte
	 * chunk (HW guys wanted to have this).
	 */
	size = round_up(MAX_TILE_COLS * MAX_TILE_ROWS * 4 * sizeof(u16) + 16, 16);
	hevc_dec->tile_sizes.cpu = dma_alloc_coherent(vpu->dev, size,
						      &hevc_dec->tile_sizes.dma,
						      GFP_KERNEL);
	if (!hevc_dec->tile_sizes.cpu)
		return -ENOMEM;

	hevc_dec->tile_sizes.size = size;

	hevc_dec->scaling_lists.cpu = dma_alloc_coherent(vpu->dev, SCALING_LIST_SIZE,
							 &hevc_dec->scaling_lists.dma,
							 GFP_KERNEL);
	if (!hevc_dec->scaling_lists.cpu)
		return -ENOMEM;

	hevc_dec->scaling_lists.size = SCALING_LIST_SIZE;

	hantro_hevc_ref_init(ctx);

	return 0;
}
