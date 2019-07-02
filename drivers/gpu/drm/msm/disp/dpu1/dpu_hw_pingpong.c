// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/iopoll.h>

#include "dpu_hw_mdss.h"
#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_pingpong.h"
#include "dpu_kms.h"
#include "dpu_trace.h"

#define PP_TEAR_CHECK_EN                0x000
#define PP_SYNC_CONFIG_VSYNC            0x004
#define PP_SYNC_CONFIG_HEIGHT           0x008
#define PP_SYNC_WRCOUNT                 0x00C
#define PP_VSYNC_INIT_VAL               0x010
#define PP_INT_COUNT_VAL                0x014
#define PP_SYNC_THRESH                  0x018
#define PP_START_POS                    0x01C
#define PP_RD_PTR_IRQ                   0x020
#define PP_WR_PTR_IRQ                   0x024
#define PP_OUT_LINE_COUNT               0x028
#define PP_LINE_COUNT                   0x02C

#define PP_FBC_MODE                     0x034
#define PP_FBC_BUDGET_CTL               0x038
#define PP_FBC_LOSSY_MODE               0x03C

static struct dpu_pingpong_cfg *_pingpong_offset(enum dpu_pingpong pp,
		struct dpu_mdss_cfg *m,
		void __iomem *addr,
		struct dpu_hw_blk_reg_map *b)
{
	int i;

	for (i = 0; i < m->pingpong_count; i++) {
		if (pp == m->pingpong[i].id) {
			b->base_off = addr;
			b->blk_off = m->pingpong[i].base;
			b->length = m->pingpong[i].len;
			b->hwversion = m->hwversion;
			b->log_mask = DPU_DBG_MASK_PINGPONG;
			return &m->pingpong[i];
		}
	}

	return ERR_PTR(-EINVAL);
}

static int dpu_hw_pp_setup_te_config(struct dpu_hw_pingpong *pp,
		struct dpu_hw_tear_check *te)
{
	struct dpu_hw_blk_reg_map *c;
	int cfg;

	if (!pp || !te)
		return -EINVAL;
	c = &pp->hw;

	cfg = BIT(19); /*VSYNC_COUNTER_EN */
	if (te->hw_vsync_mode)
		cfg |= BIT(20);

	cfg |= te->vsync_count;

	DPU_REG_WRITE(c, PP_SYNC_CONFIG_VSYNC, cfg);
	DPU_REG_WRITE(c, PP_SYNC_CONFIG_HEIGHT, te->sync_cfg_height);
	DPU_REG_WRITE(c, PP_VSYNC_INIT_VAL, te->vsync_init_val);
	DPU_REG_WRITE(c, PP_RD_PTR_IRQ, te->rd_ptr_irq);
	DPU_REG_WRITE(c, PP_START_POS, te->start_pos);
	DPU_REG_WRITE(c, PP_SYNC_THRESH,
			((te->sync_threshold_continue << 16) |
			 te->sync_threshold_start));
	DPU_REG_WRITE(c, PP_SYNC_WRCOUNT,
			(te->start_pos + te->sync_threshold_start + 1));

	return 0;
}

static int dpu_hw_pp_poll_timeout_wr_ptr(struct dpu_hw_pingpong *pp,
		u32 timeout_us)
{
	struct dpu_hw_blk_reg_map *c;
	u32 val;
	int rc;

	if (!pp)
		return -EINVAL;

	c = &pp->hw;
	rc = readl_poll_timeout(c->base_off + c->blk_off + PP_LINE_COUNT,
			val, (val & 0xffff) >= 1, 10, timeout_us);

	return rc;
}

static int dpu_hw_pp_enable_te(struct dpu_hw_pingpong *pp, bool enable)
{
	struct dpu_hw_blk_reg_map *c;

	if (!pp)
		return -EINVAL;
	c = &pp->hw;

	DPU_REG_WRITE(c, PP_TEAR_CHECK_EN, enable);
	return 0;
}

static int dpu_hw_pp_connect_external_te(struct dpu_hw_pingpong *pp,
		bool enable_external_te)
{
	struct dpu_hw_blk_reg_map *c = &pp->hw;
	u32 cfg;
	int orig;

	if (!pp)
		return -EINVAL;

	c = &pp->hw;
	cfg = DPU_REG_READ(c, PP_SYNC_CONFIG_VSYNC);
	orig = (bool)(cfg & BIT(20));
	if (enable_external_te)
		cfg |= BIT(20);
	else
		cfg &= ~BIT(20);
	DPU_REG_WRITE(c, PP_SYNC_CONFIG_VSYNC, cfg);
	trace_dpu_pp_connect_ext_te(pp->idx - PINGPONG_0, cfg);

	return orig;
}

static int dpu_hw_pp_get_vsync_info(struct dpu_hw_pingpong *pp,
		struct dpu_hw_pp_vsync_info *info)
{
	struct dpu_hw_blk_reg_map *c;
	u32 val;

	if (!pp || !info)
		return -EINVAL;
	c = &pp->hw;

	val = DPU_REG_READ(c, PP_VSYNC_INIT_VAL);
	info->rd_ptr_init_val = val & 0xffff;

	val = DPU_REG_READ(c, PP_INT_COUNT_VAL);
	info->rd_ptr_frame_count = (val & 0xffff0000) >> 16;
	info->rd_ptr_line_count = val & 0xffff;

	val = DPU_REG_READ(c, PP_LINE_COUNT);
	info->wr_ptr_line_count = val & 0xffff;

	return 0;
}

static u32 dpu_hw_pp_get_line_count(struct dpu_hw_pingpong *pp)
{
	struct dpu_hw_blk_reg_map *c = &pp->hw;
	u32 height, init;
	u32 line = 0xFFFF;

	if (!pp)
		return 0;
	c = &pp->hw;

	init = DPU_REG_READ(c, PP_VSYNC_INIT_VAL) & 0xFFFF;
	height = DPU_REG_READ(c, PP_SYNC_CONFIG_HEIGHT) & 0xFFFF;

	if (height < init)
		return line;

	line = DPU_REG_READ(c, PP_INT_COUNT_VAL) & 0xFFFF;

	if (line < init)
		line += (0xFFFF - init);
	else
		line -= init;

	return line;
}

static void _setup_pingpong_ops(struct dpu_hw_pingpong_ops *ops,
	const struct dpu_pingpong_cfg *hw_cap)
{
	ops->setup_tearcheck = dpu_hw_pp_setup_te_config;
	ops->enable_tearcheck = dpu_hw_pp_enable_te;
	ops->connect_external_te = dpu_hw_pp_connect_external_te;
	ops->get_vsync_info = dpu_hw_pp_get_vsync_info;
	ops->poll_timeout_wr_ptr = dpu_hw_pp_poll_timeout_wr_ptr;
	ops->get_line_count = dpu_hw_pp_get_line_count;
};

static struct dpu_hw_blk_ops dpu_hw_ops;

struct dpu_hw_pingpong *dpu_hw_pingpong_init(enum dpu_pingpong idx,
		void __iomem *addr,
		struct dpu_mdss_cfg *m)
{
	struct dpu_hw_pingpong *c;
	struct dpu_pingpong_cfg *cfg;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	cfg = _pingpong_offset(idx, m, addr, &c->hw);
	if (IS_ERR_OR_NULL(cfg)) {
		kfree(c);
		return ERR_PTR(-EINVAL);
	}

	c->idx = idx;
	c->caps = cfg;
	_setup_pingpong_ops(&c->ops, c->caps);

	dpu_hw_blk_init(&c->base, DPU_HW_BLK_PINGPONG, idx, &dpu_hw_ops);

	return c;
}

void dpu_hw_pingpong_destroy(struct dpu_hw_pingpong *pp)
{
	if (pp)
		dpu_hw_blk_destroy(&pp->base);
	kfree(pp);
}
