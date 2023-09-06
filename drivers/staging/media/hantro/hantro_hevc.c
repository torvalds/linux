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

#define MAX_TILE_COLS 20
#define MAX_TILE_ROWS 22

#define UNUSED_REF	-1

#define G2_ALIGN		16

size_t hantro_hevc_chroma_offset(const struct v4l2_ctrl_hevc_sps *sps)
{
	int bytes_per_pixel = sps->bit_depth_luma_minus8 == 0 ? 1 : 2;

	return sps->pic_width_in_luma_samples *
	       sps->pic_height_in_luma_samples * bytes_per_pixel;
}

size_t hantro_hevc_motion_vectors_offset(const struct v4l2_ctrl_hevc_sps *sps)
{
	size_t cr_offset = hantro_hevc_chroma_offset(sps);

	return ALIGN((cr_offset * 3) / 2, G2_ALIGN);
}

static size_t hantro_hevc_mv_size(const struct v4l2_ctrl_hevc_sps *sps)
{
	u32 min_cb_log2_size_y = sps->log2_min_luma_coding_block_size_minus3 + 3;
	u32 ctb_log2_size_y = min_cb_log2_size_y + sps->log2_diff_max_min_luma_coding_block_size;
	u32 pic_width_in_ctbs_y = (sps->pic_width_in_luma_samples + (1 << ctb_log2_size_y) - 1)
				  >> ctb_log2_size_y;
	u32 pic_height_in_ctbs_y = (sps->pic_height_in_luma_samples + (1 << ctb_log2_size_y) - 1)
				   >> ctb_log2_size_y;
	size_t mv_size;

	mv_size = pic_width_in_ctbs_y * pic_height_in_ctbs_y *
		  (1 << (2 * (ctb_log2_size_y - 4))) * 16;

	vpu_debug(4, "%dx%d (CTBs) %zu MV bytes\n",
		  pic_width_in_ctbs_y, pic_height_in_ctbs_y, mv_size);

	return mv_size;
}

static size_t hantro_hevc_ref_size(struct hantro_ctx *ctx)
{
	const struct hantro_hevc_dec_ctrls *ctrls = &ctx->hevc_dec.ctrls;
	const struct v4l2_ctrl_hevc_sps *sps = ctrls->sps;

	return hantro_hevc_motion_vectors_offset(sps) + hantro_hevc_mv_size(sps);
}

static void hantro_hevc_ref_free(struct hantro_ctx *ctx)
{
	struct hantro_hevc_dec_hw_ctx *hevc_dec = &ctx->hevc_dec;
	struct hantro_dev *vpu = ctx->dev;
	int i;

	for (i = 0;  i < NUM_REF_PICTURES; i++) {
		if (hevc_dec->ref_bufs[i].cpu)
			dma_free_coherent(vpu->dev, hevc_dec->ref_bufs[i].size,
					  hevc_dec->ref_bufs[i].cpu,
					  hevc_dec->ref_bufs[i].dma);
	}
}

static void hantro_hevc_ref_init(struct hantro_ctx *ctx)
{
	struct hantro_hevc_dec_hw_ctx *hevc_dec = &ctx->hevc_dec;
	int i;

	for (i = 0;  i < NUM_REF_PICTURES; i++)
		hevc_dec->ref_bufs_poc[i] = UNUSED_REF;
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

	/* Allocate a new reference buffer */
	for (i = 0; i < NUM_REF_PICTURES; i++) {
		if (hevc_dec->ref_bufs_poc[i] == UNUSED_REF) {
			if (!hevc_dec->ref_bufs[i].cpu) {
				struct hantro_dev *vpu = ctx->dev;

				/*
				 * Allocate the space needed for the raw data +
				 * motion vector data. Optimizations could be to
				 * allocate raw data in non coherent memory and only
				 * clear the motion vector data.
				 */
				hevc_dec->ref_bufs[i].cpu =
					dma_alloc_coherent(vpu->dev,
							   hantro_hevc_ref_size(ctx),
							   &hevc_dec->ref_bufs[i].dma,
							   GFP_KERNEL);
				if (!hevc_dec->ref_bufs[i].cpu)
					return 0;

				hevc_dec->ref_bufs[i].size = hantro_hevc_ref_size(ctx);
			}
			hevc_dec->ref_bufs_used |= 1 << i;
			memset(hevc_dec->ref_bufs[i].cpu, 0, hantro_hevc_ref_size(ctx));
			hevc_dec->ref_bufs_poc[i] = poc;

			return hevc_dec->ref_bufs[i].dma;
		}
	}

	return 0;
}

void hantro_hevc_ref_remove_unused(struct hantro_ctx *ctx)
{
	struct hantro_hevc_dec_hw_ctx *hevc_dec = &ctx->hevc_dec;
	int i;

	/* Just tag buffer as unused, do not free them */
	for (i = 0;  i < NUM_REF_PICTURES; i++) {
		if (hevc_dec->ref_bufs_poc[i] == UNUSED_REF)
			continue;

		if (hevc_dec->ref_bufs_used & (1 << i))
			continue;

		hevc_dec->ref_bufs_poc[i] = UNUSED_REF;
	}
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

	hantro_hevc_ref_free(ctx);
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

	hantro_hevc_ref_init(ctx);

	return 0;
}
