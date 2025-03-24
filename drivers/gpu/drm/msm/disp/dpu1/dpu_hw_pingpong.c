// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/iopoll.h>

#include <drm/drm_managed.h>

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
#define PP_AUTOREFRESH_CONFIG           0x030

#define PP_FBC_MODE                     0x034
#define PP_FBC_BUDGET_CTL               0x038
#define PP_FBC_LOSSY_MODE               0x03C
#define PP_DSC_MODE                     0x0a0
#define PP_DCE_DATA_IN_SWAP             0x0ac
#define PP_DCE_DATA_OUT_SWAP            0x0c8

#define PP_DITHER_EN			0x000
#define PP_DITHER_BITDEPTH		0x004
#define PP_DITHER_MATRIX		0x008

#define DITHER_DEPTH_MAP_INDEX 9

static u32 dither_depth_map[DITHER_DEPTH_MAP_INDEX] = {
	0, 0, 0, 0, 0, 0, 0, 1, 2
};

static void dpu_hw_pp_setup_dither(struct dpu_hw_pingpong *pp,
				    struct dpu_hw_dither_cfg *cfg)
{
	struct dpu_hw_blk_reg_map *c;
	u32 i, base, data = 0;

	c = &pp->hw;
	base = pp->caps->sblk->dither.base;
	if (!cfg) {
		DPU_REG_WRITE(c, base + PP_DITHER_EN, 0);
		return;
	}

	data = dither_depth_map[cfg->c0_bitdepth] & REG_MASK(2);
	data |= (dither_depth_map[cfg->c1_bitdepth] & REG_MASK(2)) << 2;
	data |= (dither_depth_map[cfg->c2_bitdepth] & REG_MASK(2)) << 4;
	data |= (dither_depth_map[cfg->c3_bitdepth] & REG_MASK(2)) << 6;
	data |= (cfg->temporal_en) ? (1 << 8) : 0;

	DPU_REG_WRITE(c, base + PP_DITHER_BITDEPTH, data);

	for (i = 0; i < DITHER_MATRIX_SZ - 3; i += 4) {
		data = (cfg->matrix[i] & REG_MASK(4)) |
			((cfg->matrix[i + 1] & REG_MASK(4)) << 4) |
			((cfg->matrix[i + 2] & REG_MASK(4)) << 8) |
			((cfg->matrix[i + 3] & REG_MASK(4)) << 12);
		DPU_REG_WRITE(c, base + PP_DITHER_MATRIX + i, data);
	}
	DPU_REG_WRITE(c, base + PP_DITHER_EN, 1);
}

static int dpu_hw_pp_enable_te(struct dpu_hw_pingpong *pp,
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

	DPU_REG_WRITE(c, PP_TEAR_CHECK_EN, 1);

	return 0;
}

static void dpu_hw_pp_setup_autorefresh_config(struct dpu_hw_pingpong *pp,
					       u32 frame_count, bool enable)
{
	DPU_REG_WRITE(&pp->hw, PP_AUTOREFRESH_CONFIG,
		      enable ? (BIT(31) | frame_count) : 0);
}

/*
 * dpu_hw_pp_get_autorefresh_config - Get autorefresh config from HW
 * @pp:          DPU pingpong structure
 * @frame_count: Used to return the current frame count from hw
 *
 * Returns: True if autorefresh enabled, false if disabled.
 */
static bool dpu_hw_pp_get_autorefresh_config(struct dpu_hw_pingpong *pp,
					     u32 *frame_count)
{
	u32 val = DPU_REG_READ(&pp->hw, PP_AUTOREFRESH_CONFIG);
	if (frame_count != NULL)
		*frame_count = val & 0xffff;
	return !!((val & BIT(31)) >> 31);
}

static int dpu_hw_pp_disable_te(struct dpu_hw_pingpong *pp)
{
	struct dpu_hw_blk_reg_map *c;

	if (!pp)
		return -EINVAL;
	c = &pp->hw;

	DPU_REG_WRITE(c, PP_TEAR_CHECK_EN, 0);
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

static void dpu_hw_pp_disable_autorefresh(struct dpu_hw_pingpong *pp,
					  uint32_t encoder_id, u16 vdisplay)
{
	struct dpu_hw_pp_vsync_info info;
	int trial = 0;

	/* If autorefresh is already disabled, we have nothing to do */
	if (!dpu_hw_pp_get_autorefresh_config(pp, NULL))
		return;

	/*
	 * If autorefresh is enabled, disable it and make sure it is safe to
	 * proceed with current frame commit/push. Sequence followed is,
	 * 1. Disable TE
	 * 2. Disable autorefresh config
	 * 4. Poll for frame transfer ongoing to be false
	 * 5. Enable TE back
	 */

	dpu_hw_pp_connect_external_te(pp, false);
	dpu_hw_pp_setup_autorefresh_config(pp, 0, false);

	do {
		udelay(DPU_ENC_MAX_POLL_TIMEOUT_US);
		if ((trial * DPU_ENC_MAX_POLL_TIMEOUT_US)
				> (KICKOFF_TIMEOUT_MS * USEC_PER_MSEC)) {
			DPU_ERROR("enc%d pp%d disable autorefresh failed\n",
				  encoder_id, pp->idx - PINGPONG_0);
			break;
		}

		trial++;

		dpu_hw_pp_get_vsync_info(pp, &info);
	} while (info.wr_ptr_line_count > 0 &&
		 info.wr_ptr_line_count < vdisplay);

	dpu_hw_pp_connect_external_te(pp, true);

	DPU_DEBUG("enc%d pp%d disabled autorefresh\n",
		  encoder_id, pp->idx - PINGPONG_0);
}

static int dpu_hw_pp_dsc_enable(struct dpu_hw_pingpong *pp)
{
	struct dpu_hw_blk_reg_map *c = &pp->hw;

	DPU_REG_WRITE(c, PP_DSC_MODE, 1);
	return 0;
}

static void dpu_hw_pp_dsc_disable(struct dpu_hw_pingpong *pp)
{
	struct dpu_hw_blk_reg_map *c = &pp->hw;

	DPU_REG_WRITE(c, PP_DSC_MODE, 0);
}

static int dpu_hw_pp_setup_dsc(struct dpu_hw_pingpong *pp)
{
	struct dpu_hw_blk_reg_map *pp_c = &pp->hw;
	int data;

	data = DPU_REG_READ(pp_c, PP_DCE_DATA_OUT_SWAP);
	data |= BIT(18); /* endian flip */
	DPU_REG_WRITE(pp_c, PP_DCE_DATA_OUT_SWAP, data);
	return 0;
}

/**
 * dpu_hw_pingpong_init() - initializes the pingpong driver for the passed
 * pingpong catalog entry.
 * @dev:  Corresponding device for devres management
 * @cfg:  Pingpong catalog entry for which driver object is required
 * @addr: Mapped register io address of MDP
 * @mdss_rev: dpu core's major and minor versions
 * Return: Error code or allocated dpu_hw_pingpong context
 */
struct dpu_hw_pingpong *dpu_hw_pingpong_init(struct drm_device *dev,
					     const struct dpu_pingpong_cfg *cfg,
					     void __iomem *addr,
					     const struct dpu_mdss_version *mdss_rev)
{
	struct dpu_hw_pingpong *c;

	c = drmm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	c->hw.blk_addr = addr + cfg->base;
	c->hw.log_mask = DPU_DBG_MASK_PINGPONG;

	c->idx = cfg->id;
	c->caps = cfg;

	if (mdss_rev->core_major_ver < 5) {
		WARN_ON(!cfg->intr_rdptr);

		c->ops.enable_tearcheck = dpu_hw_pp_enable_te;
		c->ops.disable_tearcheck = dpu_hw_pp_disable_te;
		c->ops.connect_external_te = dpu_hw_pp_connect_external_te;
		c->ops.get_line_count = dpu_hw_pp_get_line_count;
		c->ops.disable_autorefresh = dpu_hw_pp_disable_autorefresh;
	}

	if (test_bit(DPU_PINGPONG_DSC, &cfg->features)) {
		c->ops.setup_dsc = dpu_hw_pp_setup_dsc;
		c->ops.enable_dsc = dpu_hw_pp_dsc_enable;
		c->ops.disable_dsc = dpu_hw_pp_dsc_disable;
	}

	if (test_bit(DPU_PINGPONG_DITHER, &cfg->features))
		c->ops.setup_dither = dpu_hw_pp_setup_dither;

	return c;
}
