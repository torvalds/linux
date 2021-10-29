// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 */

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
#define LM_BLEND0_CONST_ALPHA            0x04
#define LM_FG_COLOR_FILL_COLOR_0         0x08
#define LM_FG_COLOR_FILL_COLOR_1         0x0C
#define LM_FG_COLOR_FILL_SIZE            0x10
#define LM_FG_COLOR_FILL_XY              0x14

#define LM_BLEND0_FG_ALPHA               0x04
#define LM_BLEND0_BG_ALPHA               0x08

#define LM_MISR_CTRL                     0x310
#define LM_MISR_SIGNATURE                0x314
#define LM_MISR_FRAME_COUNT_MASK         0xFF
#define LM_MISR_CTRL_ENABLE              BIT(8)
#define LM_MISR_CTRL_STATUS              BIT(9)
#define LM_MISR_CTRL_STATUS_CLEAR        BIT(10)
#define LM_MISR_CTRL_FREE_RUN_MASK     BIT(31)


static const struct dpu_lm_cfg *_lm_offset(enum dpu_lm mixer,
		const struct dpu_mdss_cfg *m,
		void __iomem *addr,
		struct dpu_hw_blk_reg_map *b)
{
	int i;

	for (i = 0; i < m->mixer_count; i++) {
		if (mixer == m->mixer[i].id) {
			b->base_off = addr;
			b->blk_off = m->mixer[i].base;
			b->length = m->mixer[i].len;
			b->hwversion = m->hwversion;
			b->log_mask = DPU_DBG_MASK_LM;
			return &m->mixer[i];
		}
	}

	return ERR_PTR(-ENOMEM);
}

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

static void dpu_hw_lm_setup_misr(struct dpu_hw_mixer *ctx, bool enable, u32 frame_count)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	u32 config = 0;

	DPU_REG_WRITE(c, LM_MISR_CTRL, LM_MISR_CTRL_STATUS_CLEAR);

	/* Clear old MISR value (in case it's read before a new value is calculated)*/
	wmb();

	if (enable) {
		config = (frame_count & LM_MISR_FRAME_COUNT_MASK) |
			LM_MISR_CTRL_ENABLE | LM_MISR_CTRL_FREE_RUN_MASK;

		DPU_REG_WRITE(c, LM_MISR_CTRL, config);
	} else {
		DPU_REG_WRITE(c, LM_MISR_CTRL, 0);
	}

}

static int dpu_hw_lm_collect_misr(struct dpu_hw_mixer *ctx, u32 *misr_value)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	u32 ctrl = 0;

	if (!misr_value)
		return -EINVAL;

	ctrl = DPU_REG_READ(c, LM_MISR_CTRL);

	if (!(ctrl & LM_MISR_CTRL_ENABLE))
		return -EINVAL;

	if (!(ctrl & LM_MISR_CTRL_STATUS))
		return -EINVAL;

	*misr_value = DPU_REG_READ(c, LM_MISR_SIGNATURE);

	return 0;
}

static void dpu_hw_lm_setup_blend_config_sdm845(struct dpu_hw_mixer *ctx,
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

static void _setup_mixer_ops(const struct dpu_mdss_cfg *m,
		struct dpu_hw_lm_ops *ops,
		unsigned long features)
{
	ops->setup_mixer_out = dpu_hw_lm_setup_out;
	if (m->hwversion >= DPU_HW_VER_400)
		ops->setup_blend_config = dpu_hw_lm_setup_blend_config_sdm845;
	else
		ops->setup_blend_config = dpu_hw_lm_setup_blend_config;
	ops->setup_alpha_out = dpu_hw_lm_setup_color3;
	ops->setup_border_color = dpu_hw_lm_setup_border_color;
	ops->setup_misr = dpu_hw_lm_setup_misr;
	ops->collect_misr = dpu_hw_lm_collect_misr;
}

struct dpu_hw_mixer *dpu_hw_lm_init(enum dpu_lm idx,
		void __iomem *addr,
		const struct dpu_mdss_cfg *m)
{
	struct dpu_hw_mixer *c;
	const struct dpu_lm_cfg *cfg;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	cfg = _lm_offset(idx, m, addr, &c->hw);
	if (IS_ERR_OR_NULL(cfg)) {
		kfree(c);
		return ERR_PTR(-EINVAL);
	}

	/* Assign ops */
	c->idx = idx;
	c->cap = cfg;
	_setup_mixer_ops(m, &c->ops, c->cap->features);

	return c;
}

void dpu_hw_lm_destroy(struct dpu_hw_mixer *lm)
{
	kfree(lm);
}
