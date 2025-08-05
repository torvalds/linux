// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 */

#include <drm/drm_managed.h>

#include "dpu_kms.h"
#include "dpu_hw_catalog.h"
#include "dpu_hwio.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_mdss.h"

#define LM_OP_MODE                        0x00
#define LM_OUT_SIZE                       0x04
#define LM_BORDER_COLOR_0                 0x08
#define LM_BORDER_COLOR_1                 0x010

/* These register are offset to mixer base + stage base */
#define LM_BLEND0_OP                     0x00

/* <v12 DPU with offset to mixer base + stage base */
#define LM_BLEND0_CONST_ALPHA            0x04
#define LM_FG_COLOR_FILL_COLOR_0         0x08
#define LM_FG_COLOR_FILL_COLOR_1         0x0C
#define LM_FG_COLOR_FILL_SIZE            0x10
#define LM_FG_COLOR_FILL_XY              0x14

/* >= v12 DPU */
#define LM_BG_SRC_SEL_V12                0x14
#define LM_BG_SRC_SEL_V12_RESET_VALUE    0x0000c0c0
#define LM_BORDER_COLOR_0_V12            0x1c
#define LM_BORDER_COLOR_1_V12            0x20

/* >= v12 DPU with offset to mixer base + stage base */
#define LM_BLEND0_FG_SRC_SEL_V12         0x04
#define LM_BLEND0_CONST_ALPHA_V12        0x08
#define LM_FG_COLOR_FILL_COLOR_0_V12     0x0c
#define LM_FG_COLOR_FILL_COLOR_1_V12     0x10
#define LM_FG_COLOR_FILL_SIZE_V12        0x14
#define LM_FG_COLOR_FILL_XY_V12          0x18

#define LM_BLEND0_FG_ALPHA               0x04
#define LM_BLEND0_BG_ALPHA               0x08

#define LM_MISR_CTRL                     0x310
#define LM_MISR_SIGNATURE                0x314


/**
 * _stage_offset(): returns the relative offset of the blend registers
 * for the stage to be setup
 * @ctx:     mixer ctx contains the mixer to be programmed
 * @stage: stage index to setup
 */
static inline int _stage_offset(struct dpu_hw_mixer *ctx, enum dpu_stage stage)
{
	const struct dpu_lm_sub_blks *sblk = ctx->cap->sblk;
	if (stage != DPU_STAGE_BASE && stage <= sblk->maxblendstages)
		return sblk->blendstage_base[stage - DPU_STAGE_0];

	return -EINVAL;
}

static void dpu_hw_lm_setup_out(struct dpu_hw_mixer *ctx,
		struct dpu_hw_mixer_cfg *mixer)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	u32 outsize;
	u32 op_mode;

	op_mode = DPU_REG_READ(c, LM_OP_MODE);

	outsize = mixer->out_height << 16 | mixer->out_width;
	DPU_REG_WRITE(c, LM_OUT_SIZE, outsize);

	/* SPLIT_LEFT_RIGHT */
	if (mixer->right_mixer)
		op_mode |= BIT(31);
	else
		op_mode &= ~BIT(31);
	DPU_REG_WRITE(c, LM_OP_MODE, op_mode);
}

static void dpu_hw_lm_setup_border_color(struct dpu_hw_mixer *ctx,
		struct dpu_mdss_color *color,
		u8 border_en)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;

	if (border_en) {
		DPU_REG_WRITE(c, LM_BORDER_COLOR_0,
			(color->color_0 & 0xFFF) |
			((color->color_1 & 0xFFF) << 0x10));
		DPU_REG_WRITE(c, LM_BORDER_COLOR_1,
			(color->color_2 & 0xFFF) |
			((color->color_3 & 0xFFF) << 0x10));
	}
}

static void dpu_hw_lm_setup_border_color_v12(struct dpu_hw_mixer *ctx,
					     struct dpu_mdss_color *color,
					     u8 border_en)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;

	if (border_en) {
		DPU_REG_WRITE(c, LM_BORDER_COLOR_0_V12,
			      (color->color_0 & 0x3ff) |
			      ((color->color_1 & 0x3ff) << 16));
		DPU_REG_WRITE(c, LM_BORDER_COLOR_1_V12,
			      (color->color_2 & 0x3ff) |
			      ((color->color_3 & 0x3ff) << 16));
	}
}

static void dpu_hw_lm_setup_misr(struct dpu_hw_mixer *ctx)
{
	dpu_hw_setup_misr(&ctx->hw, LM_MISR_CTRL, 0x0);
}

static int dpu_hw_lm_collect_misr(struct dpu_hw_mixer *ctx, u32 *misr_value)
{
	return dpu_hw_collect_misr(&ctx->hw, LM_MISR_CTRL, LM_MISR_SIGNATURE, misr_value);
}

static void dpu_hw_lm_setup_blend_config_combined_alpha(struct dpu_hw_mixer *ctx,
	u32 stage, u32 fg_alpha, u32 bg_alpha, u32 blend_op)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	int stage_off;
	u32 const_alpha;

	if (stage == DPU_STAGE_BASE)
		return;

	stage_off = _stage_offset(ctx, stage);
	if (WARN_ON(stage_off < 0))
		return;

	const_alpha = (bg_alpha & 0xFF) | ((fg_alpha & 0xFF) << 16);
	DPU_REG_WRITE(c, LM_BLEND0_CONST_ALPHA + stage_off, const_alpha);
	DPU_REG_WRITE(c, LM_BLEND0_OP + stage_off, blend_op);
}

static void
dpu_hw_lm_setup_blend_config_combined_alpha_v12(struct dpu_hw_mixer *ctx,
						u32 stage, u32 fg_alpha,
						u32 bg_alpha, u32 blend_op)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	int stage_off;
	u32 const_alpha;

	if (stage == DPU_STAGE_BASE)
		return;

	stage_off = _stage_offset(ctx, stage);
	if (WARN_ON(stage_off < 0))
		return;

	const_alpha = (bg_alpha & 0x3ff) | ((fg_alpha & 0x3ff) << 16);
	DPU_REG_WRITE(c, LM_BLEND0_CONST_ALPHA_V12 + stage_off, const_alpha);
	DPU_REG_WRITE(c, LM_BLEND0_OP + stage_off, blend_op);
}

static void dpu_hw_lm_setup_blend_config(struct dpu_hw_mixer *ctx,
	u32 stage, u32 fg_alpha, u32 bg_alpha, u32 blend_op)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	int stage_off;

	if (stage == DPU_STAGE_BASE)
		return;

	stage_off = _stage_offset(ctx, stage);
	if (WARN_ON(stage_off < 0))
		return;

	DPU_REG_WRITE(c, LM_BLEND0_FG_ALPHA + stage_off, fg_alpha);
	DPU_REG_WRITE(c, LM_BLEND0_BG_ALPHA + stage_off, bg_alpha);
	DPU_REG_WRITE(c, LM_BLEND0_OP + stage_off, blend_op);
}

static void dpu_hw_lm_setup_color3(struct dpu_hw_mixer *ctx,
	uint32_t mixer_op_mode)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	int op_mode;

	/* read the existing op_mode configuration */
	op_mode = DPU_REG_READ(c, LM_OP_MODE);

	op_mode = (op_mode & (BIT(31) | BIT(30))) | mixer_op_mode;

	DPU_REG_WRITE(c, LM_OP_MODE, op_mode);
}

static void dpu_hw_lm_setup_color3_v12(struct dpu_hw_mixer *ctx,
				       uint32_t mixer_op_mode)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	int op_mode, stages, stage_off, i;

	stages = ctx->cap->sblk->maxblendstages;
	if (stages <= 0)
		return;

	for (i = DPU_STAGE_0; i <= stages; i++) {
		stage_off = _stage_offset(ctx, i);
		if (WARN_ON(stage_off < 0))
			return;

		/* set color_out3 bit in blend0_op when enabled in mixer_op_mode */
		op_mode = DPU_REG_READ(c, LM_BLEND0_OP + stage_off);
		if (mixer_op_mode & BIT(i))
			op_mode |= BIT(30);
		else
			op_mode &= ~BIT(30);

		DPU_REG_WRITE(c, LM_BLEND0_OP + stage_off, op_mode);
	}
}

static int _set_staged_sspp(u32 stage, struct dpu_hw_stage_cfg *stage_cfg,
			    int pipes_per_stage, u32 *value)
{
	int i;
	u32 pipe_type = 0, pipe_id = 0, rec_id = 0;
	u32 src_sel[PIPES_PER_STAGE];

	*value = LM_BG_SRC_SEL_V12_RESET_VALUE;
	if (!stage_cfg || !pipes_per_stage)
		return 0;

	for (i = 0; i < pipes_per_stage; i++) {
		enum dpu_sspp pipe = stage_cfg->stage[stage][i];
		enum dpu_sspp_multirect_index rect_index = stage_cfg->multirect_index[stage][i];

		src_sel[i] = LM_BG_SRC_SEL_V12_RESET_VALUE;

		if (!pipe)
			continue;

		/* translate pipe data to SWI pipe_type, pipe_id */
		if (pipe >= SSPP_DMA0 && pipe <= SSPP_DMA5) {
			pipe_type = 0;
			pipe_id = pipe - SSPP_DMA0;
		} else if (pipe >= SSPP_VIG0 && pipe <= SSPP_VIG3) {
			pipe_type = 1;
			pipe_id = pipe - SSPP_VIG0;
		} else {
			DPU_ERROR("invalid rec-%d pipe:%d\n", i, pipe);
			return -EINVAL;
		}

		/* translate rec data to SWI rec_id */
		if (rect_index == DPU_SSPP_RECT_SOLO || rect_index == DPU_SSPP_RECT_0) {
			rec_id = 0;
		} else if (rect_index == DPU_SSPP_RECT_1) {
			rec_id = 1;
		} else {
			DPU_ERROR("invalid rec-%d rect_index:%d\n", i, rect_index);
			rec_id = 0;
		}

		/* calculate SWI value for rec-0 and rec-1 and store it temporary buffer */
		src_sel[i] = (((pipe_type & 0x3) << 6) | ((rec_id & 0x3) << 4) | (pipe_id & 0xf));
	}

	/* calculate final SWI register value for rec-0 and rec-1 */
	*value = 0;
	for (i = 0; i < pipes_per_stage; i++)
		*value |= src_sel[i] << (i * 8);

	return 0;
}

static int dpu_hw_lm_setup_blendstage(struct dpu_hw_mixer *ctx, enum dpu_lm lm,
				      struct dpu_hw_stage_cfg *stage_cfg)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	int i, ret, stages, stage_off, pipes_per_stage;
	u32 value;

	stages = ctx->cap->sblk->maxblendstages;
	if (stages <= 0)
		return -EINVAL;

	if (test_bit(DPU_MIXER_SOURCESPLIT, &ctx->cap->features))
		pipes_per_stage = PIPES_PER_STAGE;
	else
		pipes_per_stage = 1;

	/*
	 * When stage configuration is empty, we can enable the
	 * border color by setting the corresponding LAYER_ACTIVE bit
	 * and un-staging all the pipes from the layer mixer.
	 */
	if (!stage_cfg)
		DPU_REG_WRITE(c, LM_BG_SRC_SEL_V12, LM_BG_SRC_SEL_V12_RESET_VALUE);

	for (i = DPU_STAGE_0; i <= stages; i++) {
		stage_off = _stage_offset(ctx, i);
		if (stage_off < 0)
			return stage_off;

		ret = _set_staged_sspp(i, stage_cfg, pipes_per_stage, &value);
		if (ret)
			return ret;

		DPU_REG_WRITE(c, LM_BLEND0_FG_SRC_SEL_V12 + stage_off, value);
	}

	return 0;
}

static int dpu_hw_lm_clear_all_blendstages(struct dpu_hw_mixer *ctx)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	int i, stages, stage_off;

	stages = ctx->cap->sblk->maxblendstages;
	if (stages <= 0)
		return -EINVAL;

	DPU_REG_WRITE(c, LM_BG_SRC_SEL_V12, LM_BG_SRC_SEL_V12_RESET_VALUE);

	for (i = DPU_STAGE_0; i <= stages; i++) {
		stage_off = _stage_offset(ctx, i);
		if (stage_off < 0)
			return stage_off;

		DPU_REG_WRITE(c, LM_BLEND0_FG_SRC_SEL_V12 + stage_off,
			      LM_BG_SRC_SEL_V12_RESET_VALUE);
	}

	return 0;
}

/**
 * dpu_hw_lm_init() - Initializes the mixer hw driver object.
 * should be called once before accessing every mixer.
 * @dev:  Corresponding device for devres management
 * @cfg:  mixer catalog entry for which driver object is required
 * @addr: mapped register io address of MDP
 * @mdss_ver: DPU core's major and minor versions
 */
struct dpu_hw_mixer *dpu_hw_lm_init(struct drm_device *dev,
				    const struct dpu_lm_cfg *cfg,
				    void __iomem *addr,
				    const struct dpu_mdss_version *mdss_ver)
{
	struct dpu_hw_mixer *c;

	if (cfg->pingpong == PINGPONG_NONE) {
		DPU_DEBUG("skip mixer %d without pingpong\n", cfg->id);
		return NULL;
	}

	c = drmm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	c->hw.blk_addr = addr + cfg->base;
	c->hw.log_mask = DPU_DBG_MASK_LM;

	/* Assign ops */
	c->idx = cfg->id;
	c->cap = cfg;
	c->ops.setup_mixer_out = dpu_hw_lm_setup_out;
	if (mdss_ver->core_major_ver >= 12)
		c->ops.setup_blend_config = dpu_hw_lm_setup_blend_config_combined_alpha_v12;
	else if (mdss_ver->core_major_ver >= 4)
		c->ops.setup_blend_config = dpu_hw_lm_setup_blend_config_combined_alpha;
	else
		c->ops.setup_blend_config = dpu_hw_lm_setup_blend_config;
	if (mdss_ver->core_major_ver < 12) {
		c->ops.setup_alpha_out = dpu_hw_lm_setup_color3;
		c->ops.setup_border_color = dpu_hw_lm_setup_border_color;
	} else {
		c->ops.setup_alpha_out = dpu_hw_lm_setup_color3_v12;
		c->ops.setup_blendstage = dpu_hw_lm_setup_blendstage;
		c->ops.clear_all_blendstages = dpu_hw_lm_clear_all_blendstages;
		c->ops.setup_border_color = dpu_hw_lm_setup_border_color_v12;
	}
	c->ops.setup_misr = dpu_hw_lm_setup_misr;
	c->ops.collect_misr = dpu_hw_lm_collect_misr;

	return c;
}
