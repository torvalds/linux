// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include "dpu_hwio.h"
#include "dpu_hw_ctl.h"
#include "dpu_kms.h"
#include "dpu_trace.h"

#define   CTL_LAYER(lm)                 \
	(((lm) == LM_5) ? (0x024) : (((lm) - LM_0) * 0x004))
#define   CTL_LAYER_EXT(lm)             \
	(0x40 + (((lm) - LM_0) * 0x004))
#define   CTL_LAYER_EXT2(lm)             \
	(0x70 + (((lm) - LM_0) * 0x004))
#define   CTL_LAYER_EXT3(lm)             \
	(0xA0 + (((lm) - LM_0) * 0x004))
#define   CTL_TOP                       0x014
#define   CTL_FLUSH                     0x018
#define   CTL_START                     0x01C
#define   CTL_PREPARE                   0x0d0
#define   CTL_SW_RESET                  0x030
#define   CTL_LAYER_EXTN_OFFSET         0x40
#define   CTL_INTF_ACTIVE               0x0F4
#define   CTL_INTF_FLUSH                0x110
#define   CTL_INTF_MASTER               0x134

#define CTL_MIXER_BORDER_OUT            BIT(24)
#define CTL_FLUSH_MASK_CTL              BIT(17)

#define DPU_REG_RESET_TIMEOUT_US        2000
#define  INTF_IDX       31

static const struct dpu_ctl_cfg *_ctl_offset(enum dpu_ctl ctl,
		const struct dpu_mdss_cfg *m,
		void __iomem *addr,
		struct dpu_hw_blk_reg_map *b)
{
	int i;

	for (i = 0; i < m->ctl_count; i++) {
		if (ctl == m->ctl[i].id) {
			b->base_off = addr;
			b->blk_off = m->ctl[i].base;
			b->length = m->ctl[i].len;
			b->hwversion = m->hwversion;
			b->log_mask = DPU_DBG_MASK_CTL;
			return &m->ctl[i];
		}
	}
	return ERR_PTR(-ENOMEM);
}

static int _mixer_stages(const struct dpu_lm_cfg *mixer, int count,
		enum dpu_lm lm)
{
	int i;
	int stages = -EINVAL;

	for (i = 0; i < count; i++) {
		if (lm == mixer[i].id) {
			stages = mixer[i].sblk->maxblendstages;
			break;
		}
	}

	return stages;
}

static inline u32 dpu_hw_ctl_get_flush_register(struct dpu_hw_ctl *ctx)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;

	return DPU_REG_READ(c, CTL_FLUSH);
}

static inline void dpu_hw_ctl_trigger_start(struct dpu_hw_ctl *ctx)
{
	trace_dpu_hw_ctl_trigger_start(ctx->pending_flush_mask,
				       dpu_hw_ctl_get_flush_register(ctx));
	DPU_REG_WRITE(&ctx->hw, CTL_START, 0x1);
}

static inline void dpu_hw_ctl_trigger_pending(struct dpu_hw_ctl *ctx)
{
	trace_dpu_hw_ctl_trigger_prepare(ctx->pending_flush_mask,
					 dpu_hw_ctl_get_flush_register(ctx));
	DPU_REG_WRITE(&ctx->hw, CTL_PREPARE, 0x1);
}

static inline void dpu_hw_ctl_clear_pending_flush(struct dpu_hw_ctl *ctx)
{
	trace_dpu_hw_ctl_clear_pending_flush(ctx->pending_flush_mask,
				     dpu_hw_ctl_get_flush_register(ctx));
	ctx->pending_flush_mask = 0x0;
}

static inline void dpu_hw_ctl_update_pending_flush(struct dpu_hw_ctl *ctx,
		u32 flushbits)
{
	trace_dpu_hw_ctl_update_pending_flush(flushbits,
					      ctx->pending_flush_mask);
	ctx->pending_flush_mask |= flushbits;
}

static inline void dpu_hw_ctl_update_pending_intf_flush(struct dpu_hw_ctl *ctx,
		u32 flushbits)
{
	ctx->pending_intf_flush_mask |= flushbits;
}

static u32 dpu_hw_ctl_get_pending_flush(struct dpu_hw_ctl *ctx)
{
	return ctx->pending_flush_mask;
}

static inline void dpu_hw_ctl_trigger_flush_v1(struct dpu_hw_ctl *ctx)
{

	if (ctx->pending_flush_mask & BIT(INTF_IDX))
		DPU_REG_WRITE(&ctx->hw, CTL_INTF_FLUSH,
				ctx->pending_intf_flush_mask);

	DPU_REG_WRITE(&ctx->hw, CTL_FLUSH, ctx->pending_flush_mask);
}

static inline void dpu_hw_ctl_trigger_flush(struct dpu_hw_ctl *ctx)
{
	trace_dpu_hw_ctl_trigger_pending_flush(ctx->pending_flush_mask,
				     dpu_hw_ctl_get_flush_register(ctx));
	DPU_REG_WRITE(&ctx->hw, CTL_FLUSH, ctx->pending_flush_mask);
}

static uint32_t dpu_hw_ctl_get_bitmask_sspp(struct dpu_hw_ctl *ctx,
	enum dpu_sspp sspp)
{
	uint32_t flushbits = 0;

	switch (sspp) {
	case SSPP_VIG0:
		flushbits =  BIT(0);
		break;
	case SSPP_VIG1:
		flushbits = BIT(1);
		break;
	case SSPP_VIG2:
		flushbits = BIT(2);
		break;
	case SSPP_VIG3:
		flushbits = BIT(18);
		break;
	case SSPP_RGB0:
		flushbits = BIT(3);
		break;
	case SSPP_RGB1:
		flushbits = BIT(4);
		break;
	case SSPP_RGB2:
		flushbits = BIT(5);
		break;
	case SSPP_RGB3:
		flushbits = BIT(19);
		break;
	case SSPP_DMA0:
		flushbits = BIT(11);
		break;
	case SSPP_DMA1:
		flushbits = BIT(12);
		break;
	case SSPP_DMA2:
		flushbits = BIT(24);
		break;
	case SSPP_DMA3:
		flushbits = BIT(25);
		break;
	case SSPP_CURSOR0:
		flushbits = BIT(22);
		break;
	case SSPP_CURSOR1:
		flushbits = BIT(23);
		break;
	default:
		break;
	}

	return flushbits;
}

static uint32_t dpu_hw_ctl_get_bitmask_mixer(struct dpu_hw_ctl *ctx,
	enum dpu_lm lm)
{
	uint32_t flushbits = 0;

	switch (lm) {
	case LM_0:
		flushbits = BIT(6);
		break;
	case LM_1:
		flushbits = BIT(7);
		break;
	case LM_2:
		flushbits = BIT(8);
		break;
	case LM_3:
		flushbits = BIT(9);
		break;
	case LM_4:
		flushbits = BIT(10);
		break;
	case LM_5:
		flushbits = BIT(20);
		break;
	default:
		return -EINVAL;
	}

	flushbits |= CTL_FLUSH_MASK_CTL;

	return flushbits;
}

static int dpu_hw_ctl_get_bitmask_intf(struct dpu_hw_ctl *ctx,
		u32 *flushbits, enum dpu_intf intf)
{
	switch (intf) {
	case INTF_0:
		*flushbits |= BIT(31);
		break;
	case INTF_1:
		*flushbits |= BIT(30);
		break;
	case INTF_2:
		*flushbits |= BIT(29);
		break;
	case INTF_3:
		*flushbits |= BIT(28);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int dpu_hw_ctl_get_bitmask_intf_v1(struct dpu_hw_ctl *ctx,
		u32 *flushbits, enum dpu_intf intf)
{
	*flushbits |= BIT(31);
	return 0;
}

static int dpu_hw_ctl_active_get_bitmask_intf(struct dpu_hw_ctl *ctx,
		u32 *flushbits, enum dpu_intf intf)
{
	*flushbits |= BIT(intf - INTF_0);
	return 0;
}

static uint32_t dpu_hw_ctl_get_bitmask_dspp(struct dpu_hw_ctl *ctx,
	enum dpu_dspp dspp)
{
	uint32_t flushbits = 0;

	switch (dspp) {
	case DSPP_0:
		flushbits = BIT(13);
		break;
	case DSPP_1:
		flushbits = BIT(14);
		break;
	case DSPP_2:
		flushbits = BIT(15);
		break;
	case DSPP_3:
		flushbits = BIT(21);
		break;
	default:
		return 0;
	}

	return flushbits;
}

static u32 dpu_hw_ctl_poll_reset_status(struct dpu_hw_ctl *ctx, u32 timeout_us)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	ktime_t timeout;
	u32 status;

	timeout = ktime_add_us(ktime_get(), timeout_us);

	/*
	 * it takes around 30us to have mdp finish resetting its ctl path
	 * poll every 50us so that reset should be completed at 1st poll
	 */
	do {
		status = DPU_REG_READ(c, CTL_SW_RESET);
		status &= 0x1;
		if (status)
			usleep_range(20, 50);
	} while (status && ktime_compare_safe(ktime_get(), timeout) < 0);

	return status;
}

static int dpu_hw_ctl_reset_control(struct dpu_hw_ctl *ctx)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;

	pr_debug("issuing hw ctl reset for ctl:%d\n", ctx->idx);
	DPU_REG_WRITE(c, CTL_SW_RESET, 0x1);
	if (dpu_hw_ctl_poll_reset_status(ctx, DPU_REG_RESET_TIMEOUT_US))
		return -EINVAL;

	return 0;
}

static int dpu_hw_ctl_wait_reset_status(struct dpu_hw_ctl *ctx)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	u32 status;

	status = DPU_REG_READ(c, CTL_SW_RESET);
	status &= 0x01;
	if (!status)
		return 0;

	pr_debug("hw ctl reset is set for ctl:%d\n", ctx->idx);
	if (dpu_hw_ctl_poll_reset_status(ctx, DPU_REG_RESET_TIMEOUT_US)) {
		pr_err("hw recovery is not complete for ctl:%d\n", ctx->idx);
		return -EINVAL;
	}

	return 0;
}

static void dpu_hw_ctl_clear_all_blendstages(struct dpu_hw_ctl *ctx)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	int i;

	for (i = 0; i < ctx->mixer_count; i++) {
		DPU_REG_WRITE(c, CTL_LAYER(LM_0 + i), 0);
		DPU_REG_WRITE(c, CTL_LAYER_EXT(LM_0 + i), 0);
		DPU_REG_WRITE(c, CTL_LAYER_EXT2(LM_0 + i), 0);
		DPU_REG_WRITE(c, CTL_LAYER_EXT3(LM_0 + i), 0);
	}
}

static void dpu_hw_ctl_setup_blendstage(struct dpu_hw_ctl *ctx,
	enum dpu_lm lm, struct dpu_hw_stage_cfg *stage_cfg)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	u32 mixercfg = 0, mixercfg_ext = 0, mix, ext;
	u32 mixercfg_ext2 = 0, mixercfg_ext3 = 0;
	int i, j;
	int stages;
	int pipes_per_stage;

	stages = _mixer_stages(ctx->mixer_hw_caps, ctx->mixer_count, lm);
	if (stages < 0)
		return;

	if (test_bit(DPU_MIXER_SOURCESPLIT,
		&ctx->mixer_hw_caps->features))
		pipes_per_stage = PIPES_PER_STAGE;
	else
		pipes_per_stage = 1;

	mixercfg = CTL_MIXER_BORDER_OUT; /* always set BORDER_OUT */

	if (!stage_cfg)
		goto exit;

	for (i = 0; i <= stages; i++) {
		/* overflow to ext register if 'i + 1 > 7' */
		mix = (i + 1) & 0x7;
		ext = i >= 7;

		for (j = 0 ; j < pipes_per_stage; j++) {
			enum dpu_sspp_multirect_index rect_index =
				stage_cfg->multirect_index[i][j];

			switch (stage_cfg->stage[i][j]) {
			case SSPP_VIG0:
				if (rect_index == DPU_SSPP_RECT_1) {
					mixercfg_ext3 |= ((i + 1) & 0xF) << 0;
				} else {
					mixercfg |= mix << 0;
					mixercfg_ext |= ext << 0;
				}
				break;
			case SSPP_VIG1:
				if (rect_index == DPU_SSPP_RECT_1) {
					mixercfg_ext3 |= ((i + 1) & 0xF) << 4;
				} else {
					mixercfg |= mix << 3;
					mixercfg_ext |= ext << 2;
				}
				break;
			case SSPP_VIG2:
				if (rect_index == DPU_SSPP_RECT_1) {
					mixercfg_ext3 |= ((i + 1) & 0xF) << 8;
				} else {
					mixercfg |= mix << 6;
					mixercfg_ext |= ext << 4;
				}
				break;
			case SSPP_VIG3:
				if (rect_index == DPU_SSPP_RECT_1) {
					mixercfg_ext3 |= ((i + 1) & 0xF) << 12;
				} else {
					mixercfg |= mix << 26;
					mixercfg_ext |= ext << 6;
				}
				break;
			case SSPP_RGB0:
				mixercfg |= mix << 9;
				mixercfg_ext |= ext << 8;
				break;
			case SSPP_RGB1:
				mixercfg |= mix << 12;
				mixercfg_ext |= ext << 10;
				break;
			case SSPP_RGB2:
				mixercfg |= mix << 15;
				mixercfg_ext |= ext << 12;
				break;
			case SSPP_RGB3:
				mixercfg |= mix << 29;
				mixercfg_ext |= ext << 14;
				break;
			case SSPP_DMA0:
				if (rect_index == DPU_SSPP_RECT_1) {
					mixercfg_ext2 |= ((i + 1) & 0xF) << 8;
				} else {
					mixercfg |= mix << 18;
					mixercfg_ext |= ext << 16;
				}
				break;
			case SSPP_DMA1:
				if (rect_index == DPU_SSPP_RECT_1) {
					mixercfg_ext2 |= ((i + 1) & 0xF) << 12;
				} else {
					mixercfg |= mix << 21;
					mixercfg_ext |= ext << 18;
				}
				break;
			case SSPP_DMA2:
				if (rect_index == DPU_SSPP_RECT_1) {
					mixercfg_ext2 |= ((i + 1) & 0xF) << 16;
				} else {
					mix |= (i + 1) & 0xF;
					mixercfg_ext2 |= mix << 0;
				}
				break;
			case SSPP_DMA3:
				if (rect_index == DPU_SSPP_RECT_1) {
					mixercfg_ext2 |= ((i + 1) & 0xF) << 20;
				} else {
					mix |= (i + 1) & 0xF;
					mixercfg_ext2 |= mix << 4;
				}
				break;
			case SSPP_CURSOR0:
				mixercfg_ext |= ((i + 1) & 0xF) << 20;
				break;
			case SSPP_CURSOR1:
				mixercfg_ext |= ((i + 1) & 0xF) << 26;
				break;
			default:
				break;
			}
		}
	}

exit:
	DPU_REG_WRITE(c, CTL_LAYER(lm), mixercfg);
	DPU_REG_WRITE(c, CTL_LAYER_EXT(lm), mixercfg_ext);
	DPU_REG_WRITE(c, CTL_LAYER_EXT2(lm), mixercfg_ext2);
	DPU_REG_WRITE(c, CTL_LAYER_EXT3(lm), mixercfg_ext3);
}


static void dpu_hw_ctl_intf_cfg_v1(struct dpu_hw_ctl *ctx,
		struct dpu_hw_intf_cfg *cfg)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	u32 intf_active = 0;
	u32 mode_sel = 0;

	if (cfg->intf_mode_sel == DPU_CTL_MODE_SEL_CMD)
		mode_sel |= BIT(17);

	intf_active = DPU_REG_READ(c, CTL_INTF_ACTIVE);
	intf_active |= BIT(cfg->intf - INTF_0);

	DPU_REG_WRITE(c, CTL_TOP, mode_sel);
	DPU_REG_WRITE(c, CTL_INTF_ACTIVE, intf_active);
}

static void dpu_hw_ctl_intf_cfg(struct dpu_hw_ctl *ctx,
		struct dpu_hw_intf_cfg *cfg)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	u32 intf_cfg = 0;

	intf_cfg |= (cfg->intf & 0xF) << 4;

	if (cfg->mode_3d) {
		intf_cfg |= BIT(19);
		intf_cfg |= (cfg->mode_3d - 0x1) << 20;
	}

	switch (cfg->intf_mode_sel) {
	case DPU_CTL_MODE_SEL_VID:
		intf_cfg &= ~BIT(17);
		intf_cfg &= ~(0x3 << 15);
		break;
	case DPU_CTL_MODE_SEL_CMD:
		intf_cfg |= BIT(17);
		intf_cfg |= ((cfg->stream_sel & 0x3) << 15);
		break;
	default:
		pr_err("unknown interface type %d\n", cfg->intf_mode_sel);
		return;
	}

	DPU_REG_WRITE(c, CTL_TOP, intf_cfg);
}

static void _setup_ctl_ops(struct dpu_hw_ctl_ops *ops,
		unsigned long cap)
{
	if (cap & BIT(DPU_CTL_ACTIVE_CFG)) {
		ops->trigger_flush = dpu_hw_ctl_trigger_flush_v1;
		ops->setup_intf_cfg = dpu_hw_ctl_intf_cfg_v1;
		ops->get_bitmask_intf = dpu_hw_ctl_get_bitmask_intf_v1;
		ops->get_bitmask_active_intf =
			dpu_hw_ctl_active_get_bitmask_intf;
		ops->update_pending_intf_flush =
			dpu_hw_ctl_update_pending_intf_flush;
	} else {
		ops->trigger_flush = dpu_hw_ctl_trigger_flush;
		ops->setup_intf_cfg = dpu_hw_ctl_intf_cfg;
		ops->get_bitmask_intf = dpu_hw_ctl_get_bitmask_intf;
	}
	ops->clear_pending_flush = dpu_hw_ctl_clear_pending_flush;
	ops->update_pending_flush = dpu_hw_ctl_update_pending_flush;
	ops->get_pending_flush = dpu_hw_ctl_get_pending_flush;
	ops->get_flush_register = dpu_hw_ctl_get_flush_register;
	ops->trigger_start = dpu_hw_ctl_trigger_start;
	ops->trigger_pending = dpu_hw_ctl_trigger_pending;
	ops->reset = dpu_hw_ctl_reset_control;
	ops->wait_reset_status = dpu_hw_ctl_wait_reset_status;
	ops->clear_all_blendstages = dpu_hw_ctl_clear_all_blendstages;
	ops->setup_blendstage = dpu_hw_ctl_setup_blendstage;
	ops->get_bitmask_sspp = dpu_hw_ctl_get_bitmask_sspp;
	ops->get_bitmask_mixer = dpu_hw_ctl_get_bitmask_mixer;
	ops->get_bitmask_dspp = dpu_hw_ctl_get_bitmask_dspp;
};

static struct dpu_hw_blk_ops dpu_hw_ops;

struct dpu_hw_ctl *dpu_hw_ctl_init(enum dpu_ctl idx,
		void __iomem *addr,
		const struct dpu_mdss_cfg *m)
{
	struct dpu_hw_ctl *c;
	const struct dpu_ctl_cfg *cfg;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	cfg = _ctl_offset(idx, m, addr, &c->hw);
	if (IS_ERR_OR_NULL(cfg)) {
		kfree(c);
		pr_err("failed to create dpu_hw_ctl %d\n", idx);
		return ERR_PTR(-EINVAL);
	}

	c->caps = cfg;
	_setup_ctl_ops(&c->ops, c->caps->features);
	c->idx = idx;
	c->mixer_count = m->mixer_count;
	c->mixer_hw_caps = m->mixer;

	dpu_hw_blk_init(&c->base, DPU_HW_BLK_CTL, idx, &dpu_hw_ops);

	return c;
}

void dpu_hw_ctl_destroy(struct dpu_hw_ctl *ctx)
{
	if (ctx)
		dpu_hw_blk_destroy(&ctx->base);
	kfree(ctx);
}
