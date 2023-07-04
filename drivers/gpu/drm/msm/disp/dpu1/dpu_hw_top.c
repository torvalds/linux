// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_top.h"
#include "dpu_kms.h"

#define FLD_SPLIT_DISPLAY_CMD             BIT(1)
#define FLD_SMART_PANEL_FREE_RUN          BIT(2)
#define FLD_INTF_1_SW_TRG_MUX             BIT(4)
#define FLD_INTF_2_SW_TRG_MUX             BIT(8)
#define FLD_TE_LINE_INTER_WATERLEVEL_MASK 0xFFFF

#define TRAFFIC_SHAPER_EN                 BIT(31)
#define TRAFFIC_SHAPER_RD_CLIENT(num)     (0x030 + (num * 4))
#define TRAFFIC_SHAPER_WR_CLIENT(num)     (0x060 + (num * 4))
#define TRAFFIC_SHAPER_FIXPOINT_FACTOR    4

#define MDP_TICK_COUNT                    16
#define XO_CLK_RATE                       19200
#define MS_TICKS_IN_SEC                   1000

#define CALCULATE_WD_LOAD_VALUE(fps) \
	((uint32_t)((MS_TICKS_IN_SEC * XO_CLK_RATE)/(MDP_TICK_COUNT * fps)))

static void dpu_hw_setup_split_pipe(struct dpu_hw_mdp *mdp,
		struct split_pipe_cfg *cfg)
{
	struct dpu_hw_blk_reg_map *c;
	u32 upper_pipe = 0;
	u32 lower_pipe = 0;

	if (!mdp || !cfg)
		return;

	c = &mdp->hw;

	if (cfg->en) {
		if (cfg->mode == INTF_MODE_CMD) {
			lower_pipe = FLD_SPLIT_DISPLAY_CMD;
			/* interface controlling sw trigger */
			if (cfg->intf == INTF_2)
				lower_pipe |= FLD_INTF_1_SW_TRG_MUX;
			else
				lower_pipe |= FLD_INTF_2_SW_TRG_MUX;
			upper_pipe = lower_pipe;
		} else {
			if (cfg->intf == INTF_2) {
				lower_pipe = FLD_INTF_1_SW_TRG_MUX;
				upper_pipe = FLD_INTF_2_SW_TRG_MUX;
			} else {
				lower_pipe = FLD_INTF_2_SW_TRG_MUX;
				upper_pipe = FLD_INTF_1_SW_TRG_MUX;
			}
		}
	}

	DPU_REG_WRITE(c, SSPP_SPARE, cfg->split_flush_en ? 0x1 : 0x0);
	DPU_REG_WRITE(c, SPLIT_DISPLAY_LOWER_PIPE_CTRL, lower_pipe);
	DPU_REG_WRITE(c, SPLIT_DISPLAY_UPPER_PIPE_CTRL, upper_pipe);
	DPU_REG_WRITE(c, SPLIT_DISPLAY_EN, cfg->en & 0x1);
}

static bool dpu_hw_setup_clk_force_ctrl(struct dpu_hw_mdp *mdp,
		enum dpu_clk_ctrl_type clk_ctrl, bool enable)
{
	struct dpu_hw_blk_reg_map *c;
	u32 reg_off, bit_off;
	u32 reg_val, new_val;
	bool clk_forced_on;

	if (!mdp)
		return false;

	c = &mdp->hw;

	if (clk_ctrl <= DPU_CLK_CTRL_NONE || clk_ctrl >= DPU_CLK_CTRL_MAX)
		return false;

	reg_off = mdp->caps->clk_ctrls[clk_ctrl].reg_off;
	bit_off = mdp->caps->clk_ctrls[clk_ctrl].bit_off;

	reg_val = DPU_REG_READ(c, reg_off);

	if (enable)
		new_val = reg_val | BIT(bit_off);
	else
		new_val = reg_val & ~BIT(bit_off);

	DPU_REG_WRITE(c, reg_off, new_val);

	clk_forced_on = !(reg_val & BIT(bit_off));

	return clk_forced_on;
}


static void dpu_hw_get_danger_status(struct dpu_hw_mdp *mdp,
		struct dpu_danger_safe_status *status)
{
	struct dpu_hw_blk_reg_map *c;
	u32 value;

	if (!mdp || !status)
		return;

	c = &mdp->hw;

	value = DPU_REG_READ(c, DANGER_STATUS);
	status->mdp = (value >> 0) & 0x3;
	status->sspp[SSPP_VIG0] = (value >> 4) & 0x3;
	status->sspp[SSPP_VIG1] = (value >> 6) & 0x3;
	status->sspp[SSPP_VIG2] = (value >> 8) & 0x3;
	status->sspp[SSPP_VIG3] = (value >> 10) & 0x3;
	status->sspp[SSPP_RGB0] = (value >> 12) & 0x3;
	status->sspp[SSPP_RGB1] = (value >> 14) & 0x3;
	status->sspp[SSPP_RGB2] = (value >> 16) & 0x3;
	status->sspp[SSPP_RGB3] = (value >> 18) & 0x3;
	status->sspp[SSPP_DMA0] = (value >> 20) & 0x3;
	status->sspp[SSPP_DMA1] = (value >> 22) & 0x3;
	status->sspp[SSPP_DMA2] = (value >> 28) & 0x3;
	status->sspp[SSPP_DMA3] = (value >> 30) & 0x3;
	status->sspp[SSPP_CURSOR0] = (value >> 24) & 0x3;
	status->sspp[SSPP_CURSOR1] = (value >> 26) & 0x3;
}

static void dpu_hw_setup_vsync_source(struct dpu_hw_mdp *mdp,
		struct dpu_vsync_source_cfg *cfg)
{
	struct dpu_hw_blk_reg_map *c;
	u32 reg, wd_load_value, wd_ctl, wd_ctl2;

	if (!mdp || !cfg)
		return;

	c = &mdp->hw;

	if (cfg->vsync_source >= DPU_VSYNC_SOURCE_WD_TIMER_4 &&
			cfg->vsync_source <= DPU_VSYNC_SOURCE_WD_TIMER_0) {
		switch (cfg->vsync_source) {
		case DPU_VSYNC_SOURCE_WD_TIMER_4:
			wd_load_value = MDP_WD_TIMER_4_LOAD_VALUE;
			wd_ctl = MDP_WD_TIMER_4_CTL;
			wd_ctl2 = MDP_WD_TIMER_4_CTL2;
			break;
		case DPU_VSYNC_SOURCE_WD_TIMER_3:
			wd_load_value = MDP_WD_TIMER_3_LOAD_VALUE;
			wd_ctl = MDP_WD_TIMER_3_CTL;
			wd_ctl2 = MDP_WD_TIMER_3_CTL2;
			break;
		case DPU_VSYNC_SOURCE_WD_TIMER_2:
			wd_load_value = MDP_WD_TIMER_2_LOAD_VALUE;
			wd_ctl = MDP_WD_TIMER_2_CTL;
			wd_ctl2 = MDP_WD_TIMER_2_CTL2;
			break;
		case DPU_VSYNC_SOURCE_WD_TIMER_1:
			wd_load_value = MDP_WD_TIMER_1_LOAD_VALUE;
			wd_ctl = MDP_WD_TIMER_1_CTL;
			wd_ctl2 = MDP_WD_TIMER_1_CTL2;
			break;
		case DPU_VSYNC_SOURCE_WD_TIMER_0:
		default:
			wd_load_value = MDP_WD_TIMER_0_LOAD_VALUE;
			wd_ctl = MDP_WD_TIMER_0_CTL;
			wd_ctl2 = MDP_WD_TIMER_0_CTL2;
			break;
		}

		DPU_REG_WRITE(c, wd_load_value,
			CALCULATE_WD_LOAD_VALUE(cfg->frame_rate));

		DPU_REG_WRITE(c, wd_ctl, BIT(0)); /* clear timer */
		reg = DPU_REG_READ(c, wd_ctl2);
		reg |= BIT(8);		/* enable heartbeat timer */
		reg |= BIT(0);		/* enable WD timer */
		DPU_REG_WRITE(c, wd_ctl2, reg);

		/* make sure that timers are enabled/disabled for vsync state */
		wmb();
	}
}

static void dpu_hw_setup_vsync_source_and_vsync_sel(struct dpu_hw_mdp *mdp,
		struct dpu_vsync_source_cfg *cfg)
{
	struct dpu_hw_blk_reg_map *c;
	u32 reg, i;
	static const u32 pp_offset[PINGPONG_MAX] = {0xC, 0x8, 0x4, 0x13, 0x18};

	if (!mdp || !cfg || (cfg->pp_count > ARRAY_SIZE(cfg->ppnumber)))
		return;

	c = &mdp->hw;

	reg = DPU_REG_READ(c, MDP_VSYNC_SEL);
	for (i = 0; i < cfg->pp_count; i++) {
		int pp_idx = cfg->ppnumber[i] - PINGPONG_0;

		if (pp_idx >= ARRAY_SIZE(pp_offset))
			continue;

		reg &= ~(0xf << pp_offset[pp_idx]);
		reg |= (cfg->vsync_source & 0xf) << pp_offset[pp_idx];
	}
	DPU_REG_WRITE(c, MDP_VSYNC_SEL, reg);

	dpu_hw_setup_vsync_source(mdp, cfg);
}

static void dpu_hw_get_safe_status(struct dpu_hw_mdp *mdp,
		struct dpu_danger_safe_status *status)
{
	struct dpu_hw_blk_reg_map *c;
	u32 value;

	if (!mdp || !status)
		return;

	c = &mdp->hw;

	value = DPU_REG_READ(c, SAFE_STATUS);
	status->mdp = (value >> 0) & 0x1;
	status->sspp[SSPP_VIG0] = (value >> 4) & 0x1;
	status->sspp[SSPP_VIG1] = (value >> 6) & 0x1;
	status->sspp[SSPP_VIG2] = (value >> 8) & 0x1;
	status->sspp[SSPP_VIG3] = (value >> 10) & 0x1;
	status->sspp[SSPP_RGB0] = (value >> 12) & 0x1;
	status->sspp[SSPP_RGB1] = (value >> 14) & 0x1;
	status->sspp[SSPP_RGB2] = (value >> 16) & 0x1;
	status->sspp[SSPP_RGB3] = (value >> 18) & 0x1;
	status->sspp[SSPP_DMA0] = (value >> 20) & 0x1;
	status->sspp[SSPP_DMA1] = (value >> 22) & 0x1;
	status->sspp[SSPP_DMA2] = (value >> 28) & 0x1;
	status->sspp[SSPP_DMA3] = (value >> 30) & 0x1;
	status->sspp[SSPP_CURSOR0] = (value >> 24) & 0x1;
	status->sspp[SSPP_CURSOR1] = (value >> 26) & 0x1;
}

static void dpu_hw_intf_audio_select(struct dpu_hw_mdp *mdp)
{
	struct dpu_hw_blk_reg_map *c;

	if (!mdp)
		return;

	c = &mdp->hw;

	DPU_REG_WRITE(c, HDMI_DP_CORE_SELECT, 0x1);
}

static void _setup_mdp_ops(struct dpu_hw_mdp_ops *ops,
		unsigned long cap)
{
	ops->setup_split_pipe = dpu_hw_setup_split_pipe;
	ops->setup_clk_force_ctrl = dpu_hw_setup_clk_force_ctrl;
	ops->get_danger_status = dpu_hw_get_danger_status;

	if (cap & BIT(DPU_MDP_VSYNC_SEL))
		ops->setup_vsync_source = dpu_hw_setup_vsync_source_and_vsync_sel;
	else
		ops->setup_vsync_source = dpu_hw_setup_vsync_source;

	ops->get_safe_status = dpu_hw_get_safe_status;

	if (cap & BIT(DPU_MDP_AUDIO_SELECT))
		ops->intf_audio_select = dpu_hw_intf_audio_select;
}

struct dpu_hw_mdp *dpu_hw_mdptop_init(const struct dpu_mdp_cfg *cfg,
		void __iomem *addr,
		const struct dpu_mdss_cfg *m)
{
	struct dpu_hw_mdp *mdp;

	if (!addr)
		return ERR_PTR(-EINVAL);

	mdp = kzalloc(sizeof(*mdp), GFP_KERNEL);
	if (!mdp)
		return ERR_PTR(-ENOMEM);

	mdp->hw.blk_addr = addr + cfg->base;
	mdp->hw.log_mask = DPU_DBG_MASK_TOP;

	/*
	 * Assign ops
	 */
	mdp->idx = cfg->id;
	mdp->caps = cfg;
	_setup_mdp_ops(&mdp->ops, mdp->caps->features);

	return mdp;
}

void dpu_hw_mdp_destroy(struct dpu_hw_mdp *mdp)
{
	kfree(mdp);
}

