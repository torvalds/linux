// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_intf.h"
#include "dpu_kms.h"
#include "dpu_trace.h"

#include <linux/iopoll.h>

#define INTF_TIMING_ENGINE_EN           0x000
#define INTF_CONFIG                     0x004
#define INTF_HSYNC_CTL                  0x008
#define INTF_VSYNC_PERIOD_F0            0x00C
#define INTF_VSYNC_PERIOD_F1            0x010
#define INTF_VSYNC_PULSE_WIDTH_F0       0x014
#define INTF_VSYNC_PULSE_WIDTH_F1       0x018
#define INTF_DISPLAY_V_START_F0         0x01C
#define INTF_DISPLAY_V_START_F1         0x020
#define INTF_DISPLAY_V_END_F0           0x024
#define INTF_DISPLAY_V_END_F1           0x028
#define INTF_ACTIVE_V_START_F0          0x02C
#define INTF_ACTIVE_V_START_F1          0x030
#define INTF_ACTIVE_V_END_F0            0x034
#define INTF_ACTIVE_V_END_F1            0x038
#define INTF_DISPLAY_HCTL               0x03C
#define INTF_ACTIVE_HCTL                0x040
#define INTF_BORDER_COLOR               0x044
#define INTF_UNDERFLOW_COLOR            0x048
#define INTF_HSYNC_SKEW                 0x04C
#define INTF_POLARITY_CTL               0x050
#define INTF_TEST_CTL                   0x054
#define INTF_TP_COLOR0                  0x058
#define INTF_TP_COLOR1                  0x05C
#define INTF_CONFIG2                    0x060
#define INTF_DISPLAY_DATA_HCTL          0x064
#define INTF_ACTIVE_DATA_HCTL           0x068

#define INTF_DSI_CMD_MODE_TRIGGER_EN    0x084
#define INTF_PANEL_FORMAT               0x090

#define INTF_FRAME_LINE_COUNT_EN        0x0A8
#define INTF_FRAME_COUNT                0x0AC
#define INTF_LINE_COUNT                 0x0B0

#define INTF_DEFLICKER_CONFIG           0x0F0
#define INTF_DEFLICKER_STRNG_COEFF      0x0F4
#define INTF_DEFLICKER_WEAK_COEFF       0x0F8

#define INTF_TPG_ENABLE                 0x100
#define INTF_TPG_MAIN_CONTROL           0x104
#define INTF_TPG_VIDEO_CONFIG           0x108
#define INTF_TPG_COMPONENT_LIMITS       0x10C
#define INTF_TPG_RECTANGLE              0x110
#define INTF_TPG_INITIAL_VALUE          0x114
#define INTF_TPG_BLK_WHITE_PATTERN_FRAMES 0x118
#define INTF_TPG_RGB_MAPPING            0x11C
#define INTF_PROG_FETCH_START           0x170
#define INTF_PROG_ROT_START             0x174

#define INTF_MISR_CTRL                  0x180
#define INTF_MISR_SIGNATURE             0x184

#define INTF_MUX                        0x25C
#define INTF_STATUS                     0x26C
#define INTF_AVR_CONTROL                0x270
#define INTF_AVR_MODE                   0x274
#define INTF_AVR_TRIGGER                0x278
#define INTF_AVR_VTOTAL                 0x27C
#define INTF_TEAR_MDP_VSYNC_SEL         0x280
#define INTF_TEAR_TEAR_CHECK_EN         0x284
#define INTF_TEAR_SYNC_CONFIG_VSYNC     0x288
#define INTF_TEAR_SYNC_CONFIG_HEIGHT    0x28C
#define INTF_TEAR_SYNC_WRCOUNT          0x290
#define INTF_TEAR_VSYNC_INIT_VAL        0x294
#define INTF_TEAR_INT_COUNT_VAL         0x298
#define INTF_TEAR_SYNC_THRESH           0x29C
#define INTF_TEAR_START_POS             0x2A0
#define INTF_TEAR_RD_PTR_IRQ            0x2A4
#define INTF_TEAR_WR_PTR_IRQ            0x2A8
#define INTF_TEAR_OUT_LINE_COUNT        0x2AC
#define INTF_TEAR_LINE_COUNT            0x2B0
#define INTF_TEAR_AUTOREFRESH_CONFIG    0x2B4

#define INTF_CFG_ACTIVE_H_EN	BIT(29)
#define INTF_CFG_ACTIVE_V_EN	BIT(30)

#define INTF_CFG2_DATABUS_WIDEN	BIT(0)
#define INTF_CFG2_DATA_HCTL_EN	BIT(4)
#define INTF_CFG2_DCE_DATA_COMPRESS     BIT(12)


static void dpu_hw_intf_setup_timing_engine(struct dpu_hw_intf *ctx,
		const struct intf_timing_params *p,
		const struct dpu_format *fmt)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	u32 hsync_period, vsync_period;
	u32 display_v_start, display_v_end;
	u32 hsync_start_x, hsync_end_x;
	u32 hsync_data_start_x, hsync_data_end_x;
	u32 active_h_start, active_h_end;
	u32 active_v_start, active_v_end;
	u32 active_hctl, display_hctl, hsync_ctl;
	u32 polarity_ctl, den_polarity;
	u32 panel_format;
	u32 intf_cfg, intf_cfg2 = 0;
	u32 display_data_hctl = 0, active_data_hctl = 0;
	u32 data_width;
	bool dp_intf = false;

	/* read interface_cfg */
	intf_cfg = DPU_REG_READ(c, INTF_CONFIG);

	if (ctx->cap->type == INTF_DP)
		dp_intf = true;

	hsync_period = p->hsync_pulse_width + p->h_back_porch + p->width +
	p->h_front_porch;
	vsync_period = p->vsync_pulse_width + p->v_back_porch + p->height +
	p->v_front_porch;

	display_v_start = ((p->vsync_pulse_width + p->v_back_porch) *
	hsync_period) + p->hsync_skew;
	display_v_end = ((vsync_period - p->v_front_porch) * hsync_period) +
	p->hsync_skew - 1;

	hsync_start_x = p->h_back_porch + p->hsync_pulse_width;
	hsync_end_x = hsync_period - p->h_front_porch - 1;

	if (p->width != p->xres) { /* border fill added */
		active_h_start = hsync_start_x;
		active_h_end = active_h_start + p->xres - 1;
	} else {
		active_h_start = 0;
		active_h_end = 0;
	}

	if (p->height != p->yres) { /* border fill added */
		active_v_start = display_v_start;
		active_v_end = active_v_start + (p->yres * hsync_period) - 1;
	} else {
		active_v_start = 0;
		active_v_end = 0;
	}

	if (active_h_end) {
		active_hctl = (active_h_end << 16) | active_h_start;
		intf_cfg |= INTF_CFG_ACTIVE_H_EN;
	} else {
		active_hctl = 0;
	}

	if (active_v_end)
		intf_cfg |= INTF_CFG_ACTIVE_V_EN;

	hsync_ctl = (hsync_period << 16) | p->hsync_pulse_width;
	display_hctl = (hsync_end_x << 16) | hsync_start_x;

	/*
	 * DATA_HCTL_EN controls data timing which can be different from
	 * video timing. It is recommended to enable it for all cases, except
	 * if compression is enabled in 1 pixel per clock mode
	 */
	if (p->wide_bus_en)
		intf_cfg2 |= INTF_CFG2_DATABUS_WIDEN | INTF_CFG2_DATA_HCTL_EN;

	data_width = p->width;

	hsync_data_start_x = hsync_start_x;
	hsync_data_end_x =  hsync_start_x + data_width - 1;

	display_data_hctl = (hsync_data_end_x << 16) | hsync_data_start_x;

	if (dp_intf) {
		/* DP timing adjustment */
		display_v_start += p->hsync_pulse_width + p->h_back_porch;
		display_v_end   -= p->h_front_porch;

		active_h_start = hsync_start_x;
		active_h_end = active_h_start + p->xres - 1;
		active_v_start = display_v_start;
		active_v_end = active_v_start + (p->yres * hsync_period) - 1;

		active_hctl = (active_h_end << 16) | active_h_start;
		display_hctl = active_hctl;

		intf_cfg |= INTF_CFG_ACTIVE_H_EN | INTF_CFG_ACTIVE_V_EN;
	}

	den_polarity = 0;
	polarity_ctl = (den_polarity << 2) | /*  DEN Polarity  */
		(p->vsync_polarity << 1) | /* VSYNC Polarity */
		(p->hsync_polarity << 0);  /* HSYNC Polarity */

	if (!DPU_FORMAT_IS_YUV(fmt))
		panel_format = (fmt->bits[C0_G_Y] |
				(fmt->bits[C1_B_Cb] << 2) |
				(fmt->bits[C2_R_Cr] << 4) |
				(0x21 << 8));
	else
		/* Interface treats all the pixel data in RGB888 format */
		panel_format = (COLOR_8BIT |
				(COLOR_8BIT << 2) |
				(COLOR_8BIT << 4) |
				(0x21 << 8));

	DPU_REG_WRITE(c, INTF_HSYNC_CTL, hsync_ctl);
	DPU_REG_WRITE(c, INTF_VSYNC_PERIOD_F0, vsync_period * hsync_period);
	DPU_REG_WRITE(c, INTF_VSYNC_PULSE_WIDTH_F0,
			p->vsync_pulse_width * hsync_period);
	DPU_REG_WRITE(c, INTF_DISPLAY_HCTL, display_hctl);
	DPU_REG_WRITE(c, INTF_DISPLAY_V_START_F0, display_v_start);
	DPU_REG_WRITE(c, INTF_DISPLAY_V_END_F0, display_v_end);
	DPU_REG_WRITE(c, INTF_ACTIVE_HCTL,  active_hctl);
	DPU_REG_WRITE(c, INTF_ACTIVE_V_START_F0, active_v_start);
	DPU_REG_WRITE(c, INTF_ACTIVE_V_END_F0, active_v_end);
	DPU_REG_WRITE(c, INTF_BORDER_COLOR, p->border_clr);
	DPU_REG_WRITE(c, INTF_UNDERFLOW_COLOR, p->underflow_clr);
	DPU_REG_WRITE(c, INTF_HSYNC_SKEW, p->hsync_skew);
	DPU_REG_WRITE(c, INTF_POLARITY_CTL, polarity_ctl);
	DPU_REG_WRITE(c, INTF_FRAME_LINE_COUNT_EN, 0x3);
	DPU_REG_WRITE(c, INTF_CONFIG, intf_cfg);
	DPU_REG_WRITE(c, INTF_PANEL_FORMAT, panel_format);
	if (ctx->cap->features & BIT(DPU_DATA_HCTL_EN)) {
		DPU_REG_WRITE(c, INTF_CONFIG2, intf_cfg2);
		DPU_REG_WRITE(c, INTF_DISPLAY_DATA_HCTL, display_data_hctl);
		DPU_REG_WRITE(c, INTF_ACTIVE_DATA_HCTL, active_data_hctl);
	}
}

static void dpu_hw_intf_enable_timing_engine(
		struct dpu_hw_intf *intf,
		u8 enable)
{
	struct dpu_hw_blk_reg_map *c = &intf->hw;
	/* Note: Display interface select is handled in top block hw layer */
	DPU_REG_WRITE(c, INTF_TIMING_ENGINE_EN, enable != 0);
}

static void dpu_hw_intf_setup_prg_fetch(
		struct dpu_hw_intf *intf,
		const struct intf_prog_fetch *fetch)
{
	struct dpu_hw_blk_reg_map *c = &intf->hw;
	int fetch_enable;

	/*
	 * Fetch should always be outside the active lines. If the fetching
	 * is programmed within active region, hardware behavior is unknown.
	 */

	fetch_enable = DPU_REG_READ(c, INTF_CONFIG);
	if (fetch->enable) {
		fetch_enable |= BIT(31);
		DPU_REG_WRITE(c, INTF_PROG_FETCH_START,
				fetch->fetch_start);
	} else {
		fetch_enable &= ~BIT(31);
	}

	DPU_REG_WRITE(c, INTF_CONFIG, fetch_enable);
}

static void dpu_hw_intf_bind_pingpong_blk(
		struct dpu_hw_intf *intf,
		const enum dpu_pingpong pp)
{
	struct dpu_hw_blk_reg_map *c = &intf->hw;
	u32 mux_cfg;

	mux_cfg = DPU_REG_READ(c, INTF_MUX);
	mux_cfg &= ~0xf;

	if (pp)
		mux_cfg |= (pp - PINGPONG_0) & 0x7;
	else
		mux_cfg |= 0xf;

	DPU_REG_WRITE(c, INTF_MUX, mux_cfg);
}

static void dpu_hw_intf_get_status(
		struct dpu_hw_intf *intf,
		struct intf_status *s)
{
	struct dpu_hw_blk_reg_map *c = &intf->hw;
	unsigned long cap = intf->cap->features;

	if (cap & BIT(DPU_INTF_STATUS_SUPPORTED))
		s->is_en = DPU_REG_READ(c, INTF_STATUS) & BIT(0);
	else
		s->is_en = DPU_REG_READ(c, INTF_TIMING_ENGINE_EN);

	s->is_prog_fetch_en = !!(DPU_REG_READ(c, INTF_CONFIG) & BIT(31));
	if (s->is_en) {
		s->frame_count = DPU_REG_READ(c, INTF_FRAME_COUNT);
		s->line_count = DPU_REG_READ(c, INTF_LINE_COUNT);
	} else {
		s->line_count = 0;
		s->frame_count = 0;
	}
}

static u32 dpu_hw_intf_get_line_count(struct dpu_hw_intf *intf)
{
	struct dpu_hw_blk_reg_map *c;

	if (!intf)
		return 0;

	c = &intf->hw;

	return DPU_REG_READ(c, INTF_LINE_COUNT);
}

static void dpu_hw_intf_setup_misr(struct dpu_hw_intf *intf, bool enable, u32 frame_count)
{
	dpu_hw_setup_misr(&intf->hw, INTF_MISR_CTRL, enable, frame_count);
}

static int dpu_hw_intf_collect_misr(struct dpu_hw_intf *intf, u32 *misr_value)
{
	return dpu_hw_collect_misr(&intf->hw, INTF_MISR_CTRL, INTF_MISR_SIGNATURE, misr_value);
}

static int dpu_hw_intf_enable_te(struct dpu_hw_intf *intf,
		struct dpu_hw_tear_check *te)
{
	struct dpu_hw_blk_reg_map *c;
	int cfg;

	if (!intf)
		return -EINVAL;

	c = &intf->hw;

	cfg = BIT(19); /* VSYNC_COUNTER_EN */
	if (te->hw_vsync_mode)
		cfg |= BIT(20);

	cfg |= te->vsync_count;

	DPU_REG_WRITE(c, INTF_TEAR_SYNC_CONFIG_VSYNC, cfg);
	DPU_REG_WRITE(c, INTF_TEAR_SYNC_CONFIG_HEIGHT, te->sync_cfg_height);
	DPU_REG_WRITE(c, INTF_TEAR_VSYNC_INIT_VAL, te->vsync_init_val);
	DPU_REG_WRITE(c, INTF_TEAR_RD_PTR_IRQ, te->rd_ptr_irq);
	DPU_REG_WRITE(c, INTF_TEAR_START_POS, te->start_pos);
	DPU_REG_WRITE(c, INTF_TEAR_SYNC_THRESH,
			((te->sync_threshold_continue << 16) |
			 te->sync_threshold_start));
	DPU_REG_WRITE(c, INTF_TEAR_SYNC_WRCOUNT,
			(te->start_pos + te->sync_threshold_start + 1));

	DPU_REG_WRITE(c, INTF_TEAR_TEAR_CHECK_EN, 1);

	return 0;
}

static void dpu_hw_intf_setup_autorefresh_config(struct dpu_hw_intf *intf,
		u32 frame_count, bool enable)
{
	struct dpu_hw_blk_reg_map *c;
	u32 refresh_cfg;

	c = &intf->hw;
	refresh_cfg = DPU_REG_READ(c, INTF_TEAR_AUTOREFRESH_CONFIG);
	if (enable)
		refresh_cfg = BIT(31) | frame_count;
	else
		refresh_cfg &= ~BIT(31);

	DPU_REG_WRITE(c, INTF_TEAR_AUTOREFRESH_CONFIG, refresh_cfg);
}

/*
 * dpu_hw_intf_get_autorefresh_config - Get autorefresh config from HW
 * @intf:        DPU intf structure
 * @frame_count: Used to return the current frame count from hw
 *
 * Returns: True if autorefresh enabled, false if disabled.
 */
static bool dpu_hw_intf_get_autorefresh_config(struct dpu_hw_intf *intf,
		u32 *frame_count)
{
	u32 val = DPU_REG_READ(&intf->hw, INTF_TEAR_AUTOREFRESH_CONFIG);

	if (frame_count != NULL)
		*frame_count = val & 0xffff;
	return !!((val & BIT(31)) >> 31);
}

static int dpu_hw_intf_disable_te(struct dpu_hw_intf *intf)
{
	struct dpu_hw_blk_reg_map *c;

	if (!intf)
		return -EINVAL;

	c = &intf->hw;
	DPU_REG_WRITE(c, INTF_TEAR_TEAR_CHECK_EN, 0);
	return 0;
}

static int dpu_hw_intf_connect_external_te(struct dpu_hw_intf *intf,
		bool enable_external_te)
{
	struct dpu_hw_blk_reg_map *c = &intf->hw;
	u32 cfg;
	int orig;

	if (!intf)
		return -EINVAL;

	c = &intf->hw;
	cfg = DPU_REG_READ(c, INTF_TEAR_SYNC_CONFIG_VSYNC);
	orig = (bool)(cfg & BIT(20));
	if (enable_external_te)
		cfg |= BIT(20);
	else
		cfg &= ~BIT(20);
	DPU_REG_WRITE(c, INTF_TEAR_SYNC_CONFIG_VSYNC, cfg);
	trace_dpu_intf_connect_ext_te(intf->idx - INTF_0, cfg);

	return orig;
}

static int dpu_hw_intf_get_vsync_info(struct dpu_hw_intf *intf,
		struct dpu_hw_pp_vsync_info *info)
{
	struct dpu_hw_blk_reg_map *c = &intf->hw;
	u32 val;

	if (!intf || !info)
		return -EINVAL;

	c = &intf->hw;

	val = DPU_REG_READ(c, INTF_TEAR_VSYNC_INIT_VAL);
	info->rd_ptr_init_val = val & 0xffff;

	val = DPU_REG_READ(c, INTF_TEAR_INT_COUNT_VAL);
	info->rd_ptr_frame_count = (val & 0xffff0000) >> 16;
	info->rd_ptr_line_count = val & 0xffff;

	val = DPU_REG_READ(c, INTF_TEAR_LINE_COUNT);
	info->wr_ptr_line_count = val & 0xffff;

	val = DPU_REG_READ(c, INTF_FRAME_COUNT);
	info->intf_frame_count = val;

	return 0;
}

static void dpu_hw_intf_vsync_sel(struct dpu_hw_intf *intf,
		u32 vsync_source)
{
	struct dpu_hw_blk_reg_map *c;

	if (!intf)
		return;

	c = &intf->hw;

	DPU_REG_WRITE(c, INTF_TEAR_MDP_VSYNC_SEL, (vsync_source & 0xf));
}

static void dpu_hw_intf_disable_autorefresh(struct dpu_hw_intf *intf,
					    uint32_t encoder_id, u16 vdisplay)
{
	struct dpu_hw_pp_vsync_info info;
	int trial = 0;

	/* If autorefresh is already disabled, we have nothing to do */
	if (!dpu_hw_intf_get_autorefresh_config(intf, NULL))
		return;

	/*
	 * If autorefresh is enabled, disable it and make sure it is safe to
	 * proceed with current frame commit/push. Sequence followed is,
	 * 1. Disable TE
	 * 2. Disable autorefresh config
	 * 4. Poll for frame transfer ongoing to be false
	 * 5. Enable TE back
	 */

	dpu_hw_intf_connect_external_te(intf, false);
	dpu_hw_intf_setup_autorefresh_config(intf, 0, false);

	do {
		udelay(DPU_ENC_MAX_POLL_TIMEOUT_US);
		if ((trial * DPU_ENC_MAX_POLL_TIMEOUT_US)
				> (KICKOFF_TIMEOUT_MS * USEC_PER_MSEC)) {
			DPU_ERROR("enc%d intf%d disable autorefresh failed\n",
				  encoder_id, intf->idx - INTF_0);
			break;
		}

		trial++;

		dpu_hw_intf_get_vsync_info(intf, &info);
	} while (info.wr_ptr_line_count > 0 &&
		 info.wr_ptr_line_count < vdisplay);

	dpu_hw_intf_connect_external_te(intf, true);

	DPU_DEBUG("enc%d intf%d disabled autorefresh\n",
		  encoder_id, intf->idx - INTF_0);

}

static void dpu_hw_intf_enable_compression(struct dpu_hw_intf *ctx)
{
	u32 intf_cfg2 = DPU_REG_READ(&ctx->hw, INTF_CONFIG2);

	intf_cfg2 |= INTF_CFG2_DCE_DATA_COMPRESS;

	DPU_REG_WRITE(&ctx->hw, INTF_CONFIG2, intf_cfg2);
}

static void _setup_intf_ops(struct dpu_hw_intf_ops *ops,
		unsigned long cap)
{
	ops->setup_timing_gen = dpu_hw_intf_setup_timing_engine;
	ops->setup_prg_fetch  = dpu_hw_intf_setup_prg_fetch;
	ops->get_status = dpu_hw_intf_get_status;
	ops->enable_timing = dpu_hw_intf_enable_timing_engine;
	ops->get_line_count = dpu_hw_intf_get_line_count;
	if (cap & BIT(DPU_INTF_INPUT_CTRL))
		ops->bind_pingpong_blk = dpu_hw_intf_bind_pingpong_blk;
	ops->setup_misr = dpu_hw_intf_setup_misr;
	ops->collect_misr = dpu_hw_intf_collect_misr;

	if (cap & BIT(DPU_INTF_TE)) {
		ops->enable_tearcheck = dpu_hw_intf_enable_te;
		ops->disable_tearcheck = dpu_hw_intf_disable_te;
		ops->connect_external_te = dpu_hw_intf_connect_external_te;
		ops->vsync_sel = dpu_hw_intf_vsync_sel;
		ops->disable_autorefresh = dpu_hw_intf_disable_autorefresh;
	}

	if (cap & BIT(DPU_INTF_DATA_COMPRESS))
		ops->enable_compression = dpu_hw_intf_enable_compression;
}

struct dpu_hw_intf *dpu_hw_intf_init(const struct dpu_intf_cfg *cfg,
		void __iomem *addr)
{
	struct dpu_hw_intf *c;

	if (cfg->type == INTF_NONE) {
		DPU_DEBUG("Skip intf %d with type NONE\n", cfg->id - INTF_0);
		return NULL;
	}

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	c->hw.blk_addr = addr + cfg->base;
	c->hw.log_mask = DPU_DBG_MASK_INTF;

	/*
	 * Assign ops
	 */
	c->idx = cfg->id;
	c->cap = cfg;
	_setup_intf_ops(&c->ops, c->cap->features);

	return c;
}

void dpu_hw_intf_destroy(struct dpu_hw_intf *intf)
{
	kfree(intf);
}

