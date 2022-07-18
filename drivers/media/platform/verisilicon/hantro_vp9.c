// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VP9 codec driver
 *
 * Copyright (C) 2021 Collabora Ltd.
 */

#include <linux/types.h>
#include <media/v4l2-mem2mem.h>

#include "hantro.h"
#include "hantro_hw.h"
#include "hantro_vp9.h"

#define POW2(x) (1 << (x))

#define MAX_LOG2_TILE_COLUMNS 6
#define MAX_NUM_TILE_COLS POW2(MAX_LOG2_TILE_COLUMNS)
#define MAX_TILE_COLS 20
#define MAX_TILE_ROWS 22

static size_t hantro_vp9_tile_filter_size(unsigned int height)
{
	u32 h, height32, size;

	h = roundup(height, 8);

	height32 = roundup(h, 64);
	size = 24 * height32 * (MAX_NUM_TILE_COLS - 1); /* luma: 8, chroma: 8 + 8 */

	return size;
}

static size_t hantro_vp9_bsd_control_size(unsigned int height)
{
	u32 h, height32;

	h = roundup(height, 8);
	height32 = roundup(h, 64);

	return 16 * (height32 / 4) * (MAX_NUM_TILE_COLS - 1);
}

static size_t hantro_vp9_segment_map_size(unsigned int width, unsigned int height)
{
	u32 w, h;
	int num_ctbs;

	w = roundup(width, 8);
	h = roundup(height, 8);
	num_ctbs = ((w + 63) / 64) * ((h + 63) / 64);

	return num_ctbs * 32;
}

static inline size_t hantro_vp9_prob_tab_size(void)
{
	return roundup(sizeof(struct hantro_g2_all_probs), 16);
}

static inline size_t hantro_vp9_count_tab_size(void)
{
	return roundup(sizeof(struct symbol_counts), 16);
}

static inline size_t hantro_vp9_tile_info_size(void)
{
	return roundup((MAX_TILE_COLS * MAX_TILE_ROWS * 4 * sizeof(u16) + 15 + 16) & ~0xf, 16);
}

static void *get_coeffs_arr(struct symbol_counts *cnts, int i, int j, int k, int l, int m)
{
	if (i == 0)
		return &cnts->count_coeffs[j][k][l][m];

	if (i == 1)
		return &cnts->count_coeffs8x8[j][k][l][m];

	if (i == 2)
		return &cnts->count_coeffs16x16[j][k][l][m];

	if (i == 3)
		return &cnts->count_coeffs32x32[j][k][l][m];

	return NULL;
}

static void *get_eobs1(struct symbol_counts *cnts, int i, int j, int k, int l, int m)
{
	if (i == 0)
		return &cnts->count_coeffs[j][k][l][m][3];

	if (i == 1)
		return &cnts->count_coeffs8x8[j][k][l][m][3];

	if (i == 2)
		return &cnts->count_coeffs16x16[j][k][l][m][3];

	if (i == 3)
		return &cnts->count_coeffs32x32[j][k][l][m][3];

	return NULL;
}

#define INNER_LOOP \
	do {										\
		for (m = 0; m < ARRAY_SIZE(vp9_ctx->cnts.coeff[i][0][0][0]); ++m) {	\
			vp9_ctx->cnts.coeff[i][j][k][l][m] =				\
				get_coeffs_arr(cnts, i, j, k, l, m);			\
			vp9_ctx->cnts.eob[i][j][k][l][m][0] =				\
				&cnts->count_eobs[i][j][k][l][m];			\
			vp9_ctx->cnts.eob[i][j][k][l][m][1] =				\
				get_eobs1(cnts, i, j, k, l, m);				\
		}									\
	} while (0)

static void init_v4l2_vp9_count_tbl(struct hantro_ctx *ctx)
{
	struct hantro_vp9_dec_hw_ctx *vp9_ctx = &ctx->vp9_dec;
	struct symbol_counts *cnts = vp9_ctx->misc.cpu + vp9_ctx->ctx_counters_offset;
	int i, j, k, l, m;

	vp9_ctx->cnts.partition = &cnts->partition_counts;
	vp9_ctx->cnts.skip = &cnts->mbskip_count;
	vp9_ctx->cnts.intra_inter = &cnts->intra_inter_count;
	vp9_ctx->cnts.tx32p = &cnts->tx32x32_count;
	/*
	 * g2 hardware uses tx16x16_count[2][3], while the api
	 * expects tx16p[2][4], so this must be explicitly copied
	 * into vp9_ctx->cnts.tx16p when passing the data to the
	 * vp9 library function
	 */
	vp9_ctx->cnts.tx8p = &cnts->tx8x8_count;

	vp9_ctx->cnts.y_mode = &cnts->sb_ymode_counts;
	vp9_ctx->cnts.uv_mode = &cnts->uv_mode_counts;
	vp9_ctx->cnts.comp = &cnts->comp_inter_count;
	vp9_ctx->cnts.comp_ref = &cnts->comp_ref_count;
	vp9_ctx->cnts.single_ref = &cnts->single_ref_count;
	vp9_ctx->cnts.filter = &cnts->switchable_interp_counts;
	vp9_ctx->cnts.mv_joint = &cnts->mv_counts.joints;
	vp9_ctx->cnts.sign = &cnts->mv_counts.sign;
	vp9_ctx->cnts.classes = &cnts->mv_counts.classes;
	vp9_ctx->cnts.class0 = &cnts->mv_counts.class0;
	vp9_ctx->cnts.bits = &cnts->mv_counts.bits;
	vp9_ctx->cnts.class0_fp = &cnts->mv_counts.class0_fp;
	vp9_ctx->cnts.fp = &cnts->mv_counts.fp;
	vp9_ctx->cnts.class0_hp = &cnts->mv_counts.class0_hp;
	vp9_ctx->cnts.hp = &cnts->mv_counts.hp;

	for (i = 0; i < ARRAY_SIZE(vp9_ctx->cnts.coeff); ++i)
		for (j = 0; j < ARRAY_SIZE(vp9_ctx->cnts.coeff[i]); ++j)
			for (k = 0; k < ARRAY_SIZE(vp9_ctx->cnts.coeff[i][0]); ++k)
				for (l = 0; l < ARRAY_SIZE(vp9_ctx->cnts.coeff[i][0][0]); ++l)
					INNER_LOOP;
}

int hantro_vp9_dec_init(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	const struct hantro_variant *variant = vpu->variant;
	struct hantro_vp9_dec_hw_ctx *vp9_dec = &ctx->vp9_dec;
	struct hantro_aux_buf *tile_edge = &vp9_dec->tile_edge;
	struct hantro_aux_buf *segment_map = &vp9_dec->segment_map;
	struct hantro_aux_buf *misc = &vp9_dec->misc;
	u32 i, max_width, max_height, size;

	if (variant->num_dec_fmts < 1)
		return -EINVAL;

	for (i = 0; i < variant->num_dec_fmts; ++i)
		if (variant->dec_fmts[i].fourcc == V4L2_PIX_FMT_VP9_FRAME)
			break;

	if (i == variant->num_dec_fmts)
		return -EINVAL;

	max_width = vpu->variant->dec_fmts[i].frmsize.max_width;
	max_height = vpu->variant->dec_fmts[i].frmsize.max_height;

	size = hantro_vp9_tile_filter_size(max_height);
	vp9_dec->bsd_ctrl_offset = size;
	size += hantro_vp9_bsd_control_size(max_height);

	tile_edge->cpu = dma_alloc_coherent(vpu->dev, size, &tile_edge->dma, GFP_KERNEL);
	if (!tile_edge->cpu)
		return -ENOMEM;

	tile_edge->size = size;
	memset(tile_edge->cpu, 0, size);

	size = hantro_vp9_segment_map_size(max_width, max_height);
	vp9_dec->segment_map_size = size;
	size *= 2; /* we need two areas of this size, used alternately */

	segment_map->cpu = dma_alloc_coherent(vpu->dev, size, &segment_map->dma, GFP_KERNEL);
	if (!segment_map->cpu)
		goto err_segment_map;

	segment_map->size = size;
	memset(segment_map->cpu, 0, size);

	size = hantro_vp9_prob_tab_size();
	vp9_dec->ctx_counters_offset = size;
	size += hantro_vp9_count_tab_size();
	vp9_dec->tile_info_offset = size;
	size += hantro_vp9_tile_info_size();

	misc->cpu = dma_alloc_coherent(vpu->dev, size, &misc->dma, GFP_KERNEL);
	if (!misc->cpu)
		goto err_misc;

	misc->size = size;
	memset(misc->cpu, 0, size);

	init_v4l2_vp9_count_tbl(ctx);

	return 0;

err_misc:
	dma_free_coherent(vpu->dev, segment_map->size, segment_map->cpu, segment_map->dma);

err_segment_map:
	dma_free_coherent(vpu->dev, tile_edge->size, tile_edge->cpu, tile_edge->dma);

	return -ENOMEM;
}

void hantro_vp9_dec_exit(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	struct hantro_vp9_dec_hw_ctx *vp9_dec = &ctx->vp9_dec;
	struct hantro_aux_buf *tile_edge = &vp9_dec->tile_edge;
	struct hantro_aux_buf *segment_map = &vp9_dec->segment_map;
	struct hantro_aux_buf *misc = &vp9_dec->misc;

	dma_free_coherent(vpu->dev, misc->size, misc->cpu, misc->dma);
	dma_free_coherent(vpu->dev, segment_map->size, segment_map->cpu, segment_map->dma);
	dma_free_coherent(vpu->dev, tile_edge->size, tile_edge->cpu, tile_edge->dma);
}
