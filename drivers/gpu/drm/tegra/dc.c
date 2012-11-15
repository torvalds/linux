/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <mach/clk.h>

#include "drm.h"
#include "dc.h"

struct tegra_dc_window {
	fixed20_12 x;
	fixed20_12 y;
	fixed20_12 w;
	fixed20_12 h;
	unsigned int outx;
	unsigned int outy;
	unsigned int outw;
	unsigned int outh;
	unsigned int stride;
	unsigned int fmt;
};

static const struct drm_crtc_funcs tegra_crtc_funcs = {
	.set_config = drm_crtc_helper_set_config,
	.destroy = drm_crtc_cleanup,
};

static void tegra_crtc_dpms(struct drm_crtc *crtc, int mode)
{
}

static bool tegra_crtc_mode_fixup(struct drm_crtc *crtc,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted)
{
	return true;
}

static inline u32 compute_dda_inc(fixed20_12 inf, unsigned int out, bool v,
				  unsigned int bpp)
{
	fixed20_12 outf = dfixed_init(out);
	u32 dda_inc;
	int max;

	if (v)
		max = 15;
	else {
		switch (bpp) {
		case 2:
			max = 8;
			break;

		default:
			WARN_ON_ONCE(1);
			/* fallthrough */
		case 4:
			max = 4;
			break;
		}
	}

	outf.full = max_t(u32, outf.full - dfixed_const(1), dfixed_const(1));
	inf.full -= dfixed_const(1);

	dda_inc = dfixed_div(inf, outf);
	dda_inc = min_t(u32, dda_inc, dfixed_const(max));

	return dda_inc;
}

static inline u32 compute_initial_dda(fixed20_12 in)
{
	return dfixed_frac(in);
}

static int tegra_dc_set_timings(struct tegra_dc *dc,
				struct drm_display_mode *mode)
{
	/* TODO: For HDMI compliance, h & v ref_to_sync should be set to 1 */
	unsigned int h_ref_to_sync = 0;
	unsigned int v_ref_to_sync = 0;
	unsigned long value;

	tegra_dc_writel(dc, 0x0, DC_DISP_DISP_TIMING_OPTIONS);

	value = (v_ref_to_sync << 16) | h_ref_to_sync;
	tegra_dc_writel(dc, value, DC_DISP_REF_TO_SYNC);

	value = ((mode->vsync_end - mode->vsync_start) << 16) |
		((mode->hsync_end - mode->hsync_start) <<  0);
	tegra_dc_writel(dc, value, DC_DISP_SYNC_WIDTH);

	value = ((mode->vsync_start - mode->vdisplay) << 16) |
		((mode->hsync_start - mode->hdisplay) <<  0);
	tegra_dc_writel(dc, value, DC_DISP_BACK_PORCH);

	value = ((mode->vtotal - mode->vsync_end) << 16) |
		((mode->htotal - mode->hsync_end) <<  0);
	tegra_dc_writel(dc, value, DC_DISP_FRONT_PORCH);

	value = (mode->vdisplay << 16) | mode->hdisplay;
	tegra_dc_writel(dc, value, DC_DISP_ACTIVE);

	return 0;
}

static int tegra_crtc_setup_clk(struct drm_crtc *crtc,
				struct drm_display_mode *mode,
				unsigned long *div)
{
	unsigned long pclk = mode->clock * 1000, rate;
	struct tegra_dc *dc = to_tegra_dc(crtc);
	struct tegra_output *output = NULL;
	struct drm_encoder *encoder;
	long err;

	list_for_each_entry(encoder, &crtc->dev->mode_config.encoder_list, head)
		if (encoder->crtc == crtc) {
			output = encoder_to_output(encoder);
			break;
		}

	if (!output)
		return -ENODEV;

	/*
	 * This assumes that the display controller will divide its parent
	 * clock by 2 to generate the pixel clock.
	 */
	err = tegra_output_setup_clock(output, dc->clk, pclk * 2);
	if (err < 0) {
		dev_err(dc->dev, "failed to setup clock: %ld\n", err);
		return err;
	}

	rate = clk_get_rate(dc->clk);
	*div = (rate * 2 / pclk) - 2;

	DRM_DEBUG_KMS("rate: %lu, div: %lu\n", rate, *div);

	return 0;
}

static int tegra_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted,
			       int x, int y, struct drm_framebuffer *old_fb)
{
	struct tegra_framebuffer *fb = to_tegra_fb(crtc->fb);
	struct tegra_dc *dc = to_tegra_dc(crtc);
	unsigned int h_dda, v_dda, bpp;
	struct tegra_dc_window win;
	unsigned long div, value;
	int err;

	err = tegra_crtc_setup_clk(crtc, mode, &div);
	if (err) {
		dev_err(dc->dev, "failed to setup clock for CRTC: %d\n", err);
		return err;
	}

	/* program display mode */
	tegra_dc_set_timings(dc, mode);

	value = DE_SELECT_ACTIVE | DE_CONTROL_NORMAL;
	tegra_dc_writel(dc, value, DC_DISP_DATA_ENABLE_OPTIONS);

	value = tegra_dc_readl(dc, DC_COM_PIN_OUTPUT_POLARITY(1));
	value &= ~LVS_OUTPUT_POLARITY_LOW;
	value &= ~LHS_OUTPUT_POLARITY_LOW;
	tegra_dc_writel(dc, value, DC_COM_PIN_OUTPUT_POLARITY(1));

	value = DISP_DATA_FORMAT_DF1P1C | DISP_ALIGNMENT_MSB |
		DISP_ORDER_RED_BLUE;
	tegra_dc_writel(dc, value, DC_DISP_DISP_INTERFACE_CONTROL);

	tegra_dc_writel(dc, 0x00010001, DC_DISP_SHIFT_CLOCK_OPTIONS);

	value = SHIFT_CLK_DIVIDER(div) | PIXEL_CLK_DIVIDER_PCD1;
	tegra_dc_writel(dc, value, DC_DISP_DISP_CLOCK_CONTROL);

	/* setup window parameters */
	memset(&win, 0, sizeof(win));
	win.x.full = dfixed_const(0);
	win.y.full = dfixed_const(0);
	win.w.full = dfixed_const(mode->hdisplay);
	win.h.full = dfixed_const(mode->vdisplay);
	win.outx = 0;
	win.outy = 0;
	win.outw = mode->hdisplay;
	win.outh = mode->vdisplay;

	switch (crtc->fb->pixel_format) {
	case DRM_FORMAT_XRGB8888:
		win.fmt = WIN_COLOR_DEPTH_B8G8R8A8;
		break;

	case DRM_FORMAT_RGB565:
		win.fmt = WIN_COLOR_DEPTH_B5G6R5;
		break;

	default:
		win.fmt = WIN_COLOR_DEPTH_B8G8R8A8;
		WARN_ON(1);
		break;
	}

	bpp = crtc->fb->bits_per_pixel / 8;
	win.stride = win.outw * bpp;

	/* program window registers */
	value = tegra_dc_readl(dc, DC_CMD_DISPLAY_WINDOW_HEADER);
	value |= WINDOW_A_SELECT;
	tegra_dc_writel(dc, value, DC_CMD_DISPLAY_WINDOW_HEADER);

	tegra_dc_writel(dc, win.fmt, DC_WIN_COLOR_DEPTH);
	tegra_dc_writel(dc, 0, DC_WIN_BYTE_SWAP);

	value = V_POSITION(win.outy) | H_POSITION(win.outx);
	tegra_dc_writel(dc, value, DC_WIN_POSITION);

	value = V_SIZE(win.outh) | H_SIZE(win.outw);
	tegra_dc_writel(dc, value, DC_WIN_SIZE);

	value = V_PRESCALED_SIZE(dfixed_trunc(win.h)) |
		H_PRESCALED_SIZE(dfixed_trunc(win.w) * bpp);
	tegra_dc_writel(dc, value, DC_WIN_PRESCALED_SIZE);

	h_dda = compute_dda_inc(win.w, win.outw, false, bpp);
	v_dda = compute_dda_inc(win.h, win.outh, true, bpp);

	value = V_DDA_INC(v_dda) | H_DDA_INC(h_dda);
	tegra_dc_writel(dc, value, DC_WIN_DDA_INC);

	h_dda = compute_initial_dda(win.x);
	v_dda = compute_initial_dda(win.y);

	tegra_dc_writel(dc, h_dda, DC_WIN_H_INITIAL_DDA);
	tegra_dc_writel(dc, v_dda, DC_WIN_V_INITIAL_DDA);

	tegra_dc_writel(dc, 0, DC_WIN_UV_BUF_STRIDE);
	tegra_dc_writel(dc, 0, DC_WIN_BUF_STRIDE);

	tegra_dc_writel(dc, fb->obj->paddr, DC_WINBUF_START_ADDR);
	tegra_dc_writel(dc, win.stride, DC_WIN_LINE_STRIDE);
	tegra_dc_writel(dc, dfixed_trunc(win.x) * bpp,
			DC_WINBUF_ADDR_H_OFFSET);
	tegra_dc_writel(dc, dfixed_trunc(win.y), DC_WINBUF_ADDR_V_OFFSET);

	value = WIN_ENABLE;

	if (bpp < 24)
		value |= COLOR_EXPAND;

	tegra_dc_writel(dc, value, DC_WIN_WIN_OPTIONS);

	tegra_dc_writel(dc, 0xff00, DC_WIN_BLEND_NOKEY);
	tegra_dc_writel(dc, 0xff00, DC_WIN_BLEND_1WIN);

	return 0;
}

static void tegra_crtc_prepare(struct drm_crtc *crtc)
{
	struct tegra_dc *dc = to_tegra_dc(crtc);
	unsigned int syncpt;
	unsigned long value;

	/* hardware initialization */
	tegra_periph_reset_deassert(dc->clk);
	usleep_range(10000, 20000);

	if (dc->pipe)
		syncpt = SYNCPT_VBLANK1;
	else
		syncpt = SYNCPT_VBLANK0;

	/* initialize display controller */
	tegra_dc_writel(dc, 0x00000100, DC_CMD_GENERAL_INCR_SYNCPT_CNTRL);
	tegra_dc_writel(dc, 0x100 | syncpt, DC_CMD_CONT_SYNCPT_VSYNC);

	value = WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT | WIN_A_OF_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_TYPE);

	value = WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT |
		WIN_A_OF_INT | WIN_B_OF_INT | WIN_C_OF_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_POLARITY);

	value = PW0_ENABLE | PW1_ENABLE | PW2_ENABLE | PW3_ENABLE |
		PW4_ENABLE | PM0_ENABLE | PM1_ENABLE;
	tegra_dc_writel(dc, value, DC_CMD_DISPLAY_POWER_CONTROL);

	value = tegra_dc_readl(dc, DC_CMD_DISPLAY_COMMAND);
	value |= DISP_CTRL_MODE_C_DISPLAY;
	tegra_dc_writel(dc, value, DC_CMD_DISPLAY_COMMAND);

	/* initialize timer */
	value = CURSOR_THRESHOLD(0) | WINDOW_A_THRESHOLD(0x20) |
		WINDOW_B_THRESHOLD(0x20) | WINDOW_C_THRESHOLD(0x20);
	tegra_dc_writel(dc, value, DC_DISP_DISP_MEM_HIGH_PRIORITY);

	value = CURSOR_THRESHOLD(0) | WINDOW_A_THRESHOLD(1) |
		WINDOW_B_THRESHOLD(1) | WINDOW_C_THRESHOLD(1);
	tegra_dc_writel(dc, value, DC_DISP_DISP_MEM_HIGH_PRIORITY_TIMER);

	value = VBLANK_INT | WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_MASK);

	value = VBLANK_INT | WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_ENABLE);
}

static void tegra_crtc_commit(struct drm_crtc *crtc)
{
	struct tegra_dc *dc = to_tegra_dc(crtc);
	unsigned long update_mask;
	unsigned long value;

	update_mask = GENERAL_ACT_REQ | WIN_A_ACT_REQ;

	tegra_dc_writel(dc, update_mask << 8, DC_CMD_STATE_CONTROL);

	value = tegra_dc_readl(dc, DC_CMD_INT_ENABLE);
	value |= FRAME_END_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_ENABLE);

	value = tegra_dc_readl(dc, DC_CMD_INT_MASK);
	value |= FRAME_END_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_MASK);

	tegra_dc_writel(dc, update_mask, DC_CMD_STATE_CONTROL);
}

static void tegra_crtc_load_lut(struct drm_crtc *crtc)
{
}

static const struct drm_crtc_helper_funcs tegra_crtc_helper_funcs = {
	.dpms = tegra_crtc_dpms,
	.mode_fixup = tegra_crtc_mode_fixup,
	.mode_set = tegra_crtc_mode_set,
	.prepare = tegra_crtc_prepare,
	.commit = tegra_crtc_commit,
	.load_lut = tegra_crtc_load_lut,
};

static irqreturn_t tegra_drm_irq(int irq, void *data)
{
	struct tegra_dc *dc = data;
	unsigned long status;

	status = tegra_dc_readl(dc, DC_CMD_INT_STATUS);
	tegra_dc_writel(dc, status, DC_CMD_INT_STATUS);

	if (status & FRAME_END_INT) {
		/*
		dev_dbg(dc->dev, "%s(): frame end\n", __func__);
		*/
	}

	if (status & VBLANK_INT) {
		/*
		dev_dbg(dc->dev, "%s(): vertical blank\n", __func__);
		*/
		drm_handle_vblank(dc->base.dev, dc->pipe);
	}

	if (status & (WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT)) {
		/*
		dev_dbg(dc->dev, "%s(): underflow\n", __func__);
		*/
	}

	return IRQ_HANDLED;
}

static int tegra_dc_show_regs(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct tegra_dc *dc = node->info_ent->data;

#define DUMP_REG(name)						\
	seq_printf(s, "%-40s %#05x %08lx\n", #name, name,	\
		   tegra_dc_readl(dc, name))

	DUMP_REG(DC_CMD_GENERAL_INCR_SYNCPT);
	DUMP_REG(DC_CMD_GENERAL_INCR_SYNCPT_CNTRL);
	DUMP_REG(DC_CMD_GENERAL_INCR_SYNCPT_ERROR);
	DUMP_REG(DC_CMD_WIN_A_INCR_SYNCPT);
	DUMP_REG(DC_CMD_WIN_A_INCR_SYNCPT_CNTRL);
	DUMP_REG(DC_CMD_WIN_A_INCR_SYNCPT_ERROR);
	DUMP_REG(DC_CMD_WIN_B_INCR_SYNCPT);
	DUMP_REG(DC_CMD_WIN_B_INCR_SYNCPT_CNTRL);
	DUMP_REG(DC_CMD_WIN_B_INCR_SYNCPT_ERROR);
	DUMP_REG(DC_CMD_WIN_C_INCR_SYNCPT);
	DUMP_REG(DC_CMD_WIN_C_INCR_SYNCPT_CNTRL);
	DUMP_REG(DC_CMD_WIN_C_INCR_SYNCPT_ERROR);
	DUMP_REG(DC_CMD_CONT_SYNCPT_VSYNC);
	DUMP_REG(DC_CMD_DISPLAY_COMMAND_OPTION0);
	DUMP_REG(DC_CMD_DISPLAY_COMMAND);
	DUMP_REG(DC_CMD_SIGNAL_RAISE);
	DUMP_REG(DC_CMD_DISPLAY_POWER_CONTROL);
	DUMP_REG(DC_CMD_INT_STATUS);
	DUMP_REG(DC_CMD_INT_MASK);
	DUMP_REG(DC_CMD_INT_ENABLE);
	DUMP_REG(DC_CMD_INT_TYPE);
	DUMP_REG(DC_CMD_INT_POLARITY);
	DUMP_REG(DC_CMD_SIGNAL_RAISE1);
	DUMP_REG(DC_CMD_SIGNAL_RAISE2);
	DUMP_REG(DC_CMD_SIGNAL_RAISE3);
	DUMP_REG(DC_CMD_STATE_ACCESS);
	DUMP_REG(DC_CMD_STATE_CONTROL);
	DUMP_REG(DC_CMD_DISPLAY_WINDOW_HEADER);
	DUMP_REG(DC_CMD_REG_ACT_CONTROL);
	DUMP_REG(DC_COM_CRC_CONTROL);
	DUMP_REG(DC_COM_CRC_CHECKSUM);
	DUMP_REG(DC_COM_PIN_OUTPUT_ENABLE(0));
	DUMP_REG(DC_COM_PIN_OUTPUT_ENABLE(1));
	DUMP_REG(DC_COM_PIN_OUTPUT_ENABLE(2));
	DUMP_REG(DC_COM_PIN_OUTPUT_ENABLE(3));
	DUMP_REG(DC_COM_PIN_OUTPUT_POLARITY(0));
	DUMP_REG(DC_COM_PIN_OUTPUT_POLARITY(1));
	DUMP_REG(DC_COM_PIN_OUTPUT_POLARITY(2));
	DUMP_REG(DC_COM_PIN_OUTPUT_POLARITY(3));
	DUMP_REG(DC_COM_PIN_OUTPUT_DATA(0));
	DUMP_REG(DC_COM_PIN_OUTPUT_DATA(1));
	DUMP_REG(DC_COM_PIN_OUTPUT_DATA(2));
	DUMP_REG(DC_COM_PIN_OUTPUT_DATA(3));
	DUMP_REG(DC_COM_PIN_INPUT_ENABLE(0));
	DUMP_REG(DC_COM_PIN_INPUT_ENABLE(1));
	DUMP_REG(DC_COM_PIN_INPUT_ENABLE(2));
	DUMP_REG(DC_COM_PIN_INPUT_ENABLE(3));
	DUMP_REG(DC_COM_PIN_INPUT_DATA(0));
	DUMP_REG(DC_COM_PIN_INPUT_DATA(1));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(0));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(1));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(2));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(3));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(4));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(5));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(6));
	DUMP_REG(DC_COM_PIN_MISC_CONTROL);
	DUMP_REG(DC_COM_PIN_PM0_CONTROL);
	DUMP_REG(DC_COM_PIN_PM0_DUTY_CYCLE);
	DUMP_REG(DC_COM_PIN_PM1_CONTROL);
	DUMP_REG(DC_COM_PIN_PM1_DUTY_CYCLE);
	DUMP_REG(DC_COM_SPI_CONTROL);
	DUMP_REG(DC_COM_SPI_START_BYTE);
	DUMP_REG(DC_COM_HSPI_WRITE_DATA_AB);
	DUMP_REG(DC_COM_HSPI_WRITE_DATA_CD);
	DUMP_REG(DC_COM_HSPI_CS_DC);
	DUMP_REG(DC_COM_SCRATCH_REGISTER_A);
	DUMP_REG(DC_COM_SCRATCH_REGISTER_B);
	DUMP_REG(DC_COM_GPIO_CTRL);
	DUMP_REG(DC_COM_GPIO_DEBOUNCE_COUNTER);
	DUMP_REG(DC_COM_CRC_CHECKSUM_LATCHED);
	DUMP_REG(DC_DISP_DISP_SIGNAL_OPTIONS0);
	DUMP_REG(DC_DISP_DISP_SIGNAL_OPTIONS1);
	DUMP_REG(DC_DISP_DISP_WIN_OPTIONS);
	DUMP_REG(DC_DISP_DISP_MEM_HIGH_PRIORITY);
	DUMP_REG(DC_DISP_DISP_MEM_HIGH_PRIORITY_TIMER);
	DUMP_REG(DC_DISP_DISP_TIMING_OPTIONS);
	DUMP_REG(DC_DISP_REF_TO_SYNC);
	DUMP_REG(DC_DISP_SYNC_WIDTH);
	DUMP_REG(DC_DISP_BACK_PORCH);
	DUMP_REG(DC_DISP_ACTIVE);
	DUMP_REG(DC_DISP_FRONT_PORCH);
	DUMP_REG(DC_DISP_H_PULSE0_CONTROL);
	DUMP_REG(DC_DISP_H_PULSE0_POSITION_A);
	DUMP_REG(DC_DISP_H_PULSE0_POSITION_B);
	DUMP_REG(DC_DISP_H_PULSE0_POSITION_C);
	DUMP_REG(DC_DISP_H_PULSE0_POSITION_D);
	DUMP_REG(DC_DISP_H_PULSE1_CONTROL);
	DUMP_REG(DC_DISP_H_PULSE1_POSITION_A);
	DUMP_REG(DC_DISP_H_PULSE1_POSITION_B);
	DUMP_REG(DC_DISP_H_PULSE1_POSITION_C);
	DUMP_REG(DC_DISP_H_PULSE1_POSITION_D);
	DUMP_REG(DC_DISP_H_PULSE2_CONTROL);
	DUMP_REG(DC_DISP_H_PULSE2_POSITION_A);
	DUMP_REG(DC_DISP_H_PULSE2_POSITION_B);
	DUMP_REG(DC_DISP_H_PULSE2_POSITION_C);
	DUMP_REG(DC_DISP_H_PULSE2_POSITION_D);
	DUMP_REG(DC_DISP_V_PULSE0_CONTROL);
	DUMP_REG(DC_DISP_V_PULSE0_POSITION_A);
	DUMP_REG(DC_DISP_V_PULSE0_POSITION_B);
	DUMP_REG(DC_DISP_V_PULSE0_POSITION_C);
	DUMP_REG(DC_DISP_V_PULSE1_CONTROL);
	DUMP_REG(DC_DISP_V_PULSE1_POSITION_A);
	DUMP_REG(DC_DISP_V_PULSE1_POSITION_B);
	DUMP_REG(DC_DISP_V_PULSE1_POSITION_C);
	DUMP_REG(DC_DISP_V_PULSE2_CONTROL);
	DUMP_REG(DC_DISP_V_PULSE2_POSITION_A);
	DUMP_REG(DC_DISP_V_PULSE3_CONTROL);
	DUMP_REG(DC_DISP_V_PULSE3_POSITION_A);
	DUMP_REG(DC_DISP_M0_CONTROL);
	DUMP_REG(DC_DISP_M1_CONTROL);
	DUMP_REG(DC_DISP_DI_CONTROL);
	DUMP_REG(DC_DISP_PP_CONTROL);
	DUMP_REG(DC_DISP_PP_SELECT_A);
	DUMP_REG(DC_DISP_PP_SELECT_B);
	DUMP_REG(DC_DISP_PP_SELECT_C);
	DUMP_REG(DC_DISP_PP_SELECT_D);
	DUMP_REG(DC_DISP_DISP_CLOCK_CONTROL);
	DUMP_REG(DC_DISP_DISP_INTERFACE_CONTROL);
	DUMP_REG(DC_DISP_DISP_COLOR_CONTROL);
	DUMP_REG(DC_DISP_SHIFT_CLOCK_OPTIONS);
	DUMP_REG(DC_DISP_DATA_ENABLE_OPTIONS);
	DUMP_REG(DC_DISP_SERIAL_INTERFACE_OPTIONS);
	DUMP_REG(DC_DISP_LCD_SPI_OPTIONS);
	DUMP_REG(DC_DISP_BORDER_COLOR);
	DUMP_REG(DC_DISP_COLOR_KEY0_LOWER);
	DUMP_REG(DC_DISP_COLOR_KEY0_UPPER);
	DUMP_REG(DC_DISP_COLOR_KEY1_LOWER);
	DUMP_REG(DC_DISP_COLOR_KEY1_UPPER);
	DUMP_REG(DC_DISP_CURSOR_FOREGROUND);
	DUMP_REG(DC_DISP_CURSOR_BACKGROUND);
	DUMP_REG(DC_DISP_CURSOR_START_ADDR);
	DUMP_REG(DC_DISP_CURSOR_START_ADDR_NS);
	DUMP_REG(DC_DISP_CURSOR_POSITION);
	DUMP_REG(DC_DISP_CURSOR_POSITION_NS);
	DUMP_REG(DC_DISP_INIT_SEQ_CONTROL);
	DUMP_REG(DC_DISP_SPI_INIT_SEQ_DATA_A);
	DUMP_REG(DC_DISP_SPI_INIT_SEQ_DATA_B);
	DUMP_REG(DC_DISP_SPI_INIT_SEQ_DATA_C);
	DUMP_REG(DC_DISP_SPI_INIT_SEQ_DATA_D);
	DUMP_REG(DC_DISP_DC_MCCIF_FIFOCTRL);
	DUMP_REG(DC_DISP_MCCIF_DISPLAY0A_HYST);
	DUMP_REG(DC_DISP_MCCIF_DISPLAY0B_HYST);
	DUMP_REG(DC_DISP_MCCIF_DISPLAY1A_HYST);
	DUMP_REG(DC_DISP_MCCIF_DISPLAY1B_HYST);
	DUMP_REG(DC_DISP_DAC_CRT_CTRL);
	DUMP_REG(DC_DISP_DISP_MISC_CONTROL);
	DUMP_REG(DC_DISP_SD_CONTROL);
	DUMP_REG(DC_DISP_SD_CSC_COEFF);
	DUMP_REG(DC_DISP_SD_LUT(0));
	DUMP_REG(DC_DISP_SD_LUT(1));
	DUMP_REG(DC_DISP_SD_LUT(2));
	DUMP_REG(DC_DISP_SD_LUT(3));
	DUMP_REG(DC_DISP_SD_LUT(4));
	DUMP_REG(DC_DISP_SD_LUT(5));
	DUMP_REG(DC_DISP_SD_LUT(6));
	DUMP_REG(DC_DISP_SD_LUT(7));
	DUMP_REG(DC_DISP_SD_LUT(8));
	DUMP_REG(DC_DISP_SD_FLICKER_CONTROL);
	DUMP_REG(DC_DISP_DC_PIXEL_COUNT);
	DUMP_REG(DC_DISP_SD_HISTOGRAM(0));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(1));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(2));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(3));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(4));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(5));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(6));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(7));
	DUMP_REG(DC_DISP_SD_BL_TF(0));
	DUMP_REG(DC_DISP_SD_BL_TF(1));
	DUMP_REG(DC_DISP_SD_BL_TF(2));
	DUMP_REG(DC_DISP_SD_BL_TF(3));
	DUMP_REG(DC_DISP_SD_BL_CONTROL);
	DUMP_REG(DC_DISP_SD_HW_K_VALUES);
	DUMP_REG(DC_DISP_SD_MAN_K_VALUES);
	DUMP_REG(DC_WIN_WIN_OPTIONS);
	DUMP_REG(DC_WIN_BYTE_SWAP);
	DUMP_REG(DC_WIN_BUFFER_CONTROL);
	DUMP_REG(DC_WIN_COLOR_DEPTH);
	DUMP_REG(DC_WIN_POSITION);
	DUMP_REG(DC_WIN_SIZE);
	DUMP_REG(DC_WIN_PRESCALED_SIZE);
	DUMP_REG(DC_WIN_H_INITIAL_DDA);
	DUMP_REG(DC_WIN_V_INITIAL_DDA);
	DUMP_REG(DC_WIN_DDA_INC);
	DUMP_REG(DC_WIN_LINE_STRIDE);
	DUMP_REG(DC_WIN_BUF_STRIDE);
	DUMP_REG(DC_WIN_UV_BUF_STRIDE);
	DUMP_REG(DC_WIN_BUFFER_ADDR_MODE);
	DUMP_REG(DC_WIN_DV_CONTROL);
	DUMP_REG(DC_WIN_BLEND_NOKEY);
	DUMP_REG(DC_WIN_BLEND_1WIN);
	DUMP_REG(DC_WIN_BLEND_2WIN_X);
	DUMP_REG(DC_WIN_BLEND_2WIN_Y);
	DUMP_REG(DC_WIN_BLEND32WIN_XY);
	DUMP_REG(DC_WIN_HP_FETCH_CONTROL);
	DUMP_REG(DC_WINBUF_START_ADDR);
	DUMP_REG(DC_WINBUF_START_ADDR_NS);
	DUMP_REG(DC_WINBUF_START_ADDR_U);
	DUMP_REG(DC_WINBUF_START_ADDR_U_NS);
	DUMP_REG(DC_WINBUF_START_ADDR_V);
	DUMP_REG(DC_WINBUF_START_ADDR_V_NS);
	DUMP_REG(DC_WINBUF_ADDR_H_OFFSET);
	DUMP_REG(DC_WINBUF_ADDR_H_OFFSET_NS);
	DUMP_REG(DC_WINBUF_ADDR_V_OFFSET);
	DUMP_REG(DC_WINBUF_ADDR_V_OFFSET_NS);
	DUMP_REG(DC_WINBUF_UFLOW_STATUS);
	DUMP_REG(DC_WINBUF_AD_UFLOW_STATUS);
	DUMP_REG(DC_WINBUF_BD_UFLOW_STATUS);
	DUMP_REG(DC_WINBUF_CD_UFLOW_STATUS);

#undef DUMP_REG

	return 0;
}

static struct drm_info_list debugfs_files[] = {
	{ "regs", tegra_dc_show_regs, 0, NULL },
};

static int tegra_dc_debugfs_init(struct tegra_dc *dc, struct drm_minor *minor)
{
	unsigned int i;
	char *name;
	int err;

	name = kasprintf(GFP_KERNEL, "dc.%d", dc->pipe);
	dc->debugfs = debugfs_create_dir(name, minor->debugfs_root);
	kfree(name);

	if (!dc->debugfs)
		return -ENOMEM;

	dc->debugfs_files = kmemdup(debugfs_files, sizeof(debugfs_files),
				    GFP_KERNEL);
	if (!dc->debugfs_files) {
		err = -ENOMEM;
		goto remove;
	}

	for (i = 0; i < ARRAY_SIZE(debugfs_files); i++)
		dc->debugfs_files[i].data = dc;

	err = drm_debugfs_create_files(dc->debugfs_files,
				       ARRAY_SIZE(debugfs_files),
				       dc->debugfs, minor);
	if (err < 0)
		goto free;

	dc->minor = minor;

	return 0;

free:
	kfree(dc->debugfs_files);
	dc->debugfs_files = NULL;
remove:
	debugfs_remove(dc->debugfs);
	dc->debugfs = NULL;

	return err;
}

static int tegra_dc_debugfs_exit(struct tegra_dc *dc)
{
	drm_debugfs_remove_files(dc->debugfs_files, ARRAY_SIZE(debugfs_files),
				 dc->minor);
	dc->minor = NULL;

	kfree(dc->debugfs_files);
	dc->debugfs_files = NULL;

	debugfs_remove(dc->debugfs);
	dc->debugfs = NULL;

	return 0;
}

static int tegra_dc_drm_init(struct host1x_client *client,
			     struct drm_device *drm)
{
	struct tegra_dc *dc = host1x_client_to_dc(client);
	int err;

	dc->pipe = drm->mode_config.num_crtc;

	drm_crtc_init(drm, &dc->base, &tegra_crtc_funcs);
	drm_mode_crtc_set_gamma_size(&dc->base, 256);
	drm_crtc_helper_add(&dc->base, &tegra_crtc_helper_funcs);

	err = tegra_dc_rgb_init(drm, dc);
	if (err < 0 && err != -ENODEV) {
		dev_err(dc->dev, "failed to initialize RGB output: %d\n", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		err = tegra_dc_debugfs_init(dc, drm->primary);
		if (err < 0)
			dev_err(dc->dev, "debugfs setup failed: %d\n", err);
	}

	err = devm_request_irq(dc->dev, dc->irq, tegra_drm_irq, 0,
			       dev_name(dc->dev), dc);
	if (err < 0) {
		dev_err(dc->dev, "failed to request IRQ#%u: %d\n", dc->irq,
			err);
		return err;
	}

	return 0;
}

static int tegra_dc_drm_exit(struct host1x_client *client)
{
	struct tegra_dc *dc = host1x_client_to_dc(client);
	int err;

	devm_free_irq(dc->dev, dc->irq, dc);

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		err = tegra_dc_debugfs_exit(dc);
		if (err < 0)
			dev_err(dc->dev, "debugfs cleanup failed: %d\n", err);
	}

	err = tegra_dc_rgb_exit(dc);
	if (err) {
		dev_err(dc->dev, "failed to shutdown RGB output: %d\n", err);
		return err;
	}

	return 0;
}

static const struct host1x_client_ops dc_client_ops = {
	.drm_init = tegra_dc_drm_init,
	.drm_exit = tegra_dc_drm_exit,
};

static int tegra_dc_probe(struct platform_device *pdev)
{
	struct host1x *host1x = dev_get_drvdata(pdev->dev.parent);
	struct resource *regs;
	struct tegra_dc *dc;
	int err;

	dc = devm_kzalloc(&pdev->dev, sizeof(*dc), GFP_KERNEL);
	if (!dc)
		return -ENOMEM;

	INIT_LIST_HEAD(&dc->list);
	dc->dev = &pdev->dev;

	dc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dc->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(dc->clk);
	}

	err = clk_prepare_enable(dc->clk);
	if (err < 0)
		return err;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "failed to get registers\n");
		return -ENXIO;
	}

	dc->regs = devm_request_and_ioremap(&pdev->dev, regs);
	if (!dc->regs) {
		dev_err(&pdev->dev, "failed to remap registers\n");
		return -ENXIO;
	}

	dc->irq = platform_get_irq(pdev, 0);
	if (dc->irq < 0) {
		dev_err(&pdev->dev, "failed to get IRQ\n");
		return -ENXIO;
	}

	INIT_LIST_HEAD(&dc->client.list);
	dc->client.ops = &dc_client_ops;
	dc->client.dev = &pdev->dev;

	err = tegra_dc_rgb_probe(dc);
	if (err < 0 && err != -ENODEV) {
		dev_err(&pdev->dev, "failed to probe RGB output: %d\n", err);
		return err;
	}

	err = host1x_register_client(host1x, &dc->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register host1x client: %d\n",
			err);
		return err;
	}

	platform_set_drvdata(pdev, dc);

	return 0;
}

static int tegra_dc_remove(struct platform_device *pdev)
{
	struct host1x *host1x = dev_get_drvdata(pdev->dev.parent);
	struct tegra_dc *dc = platform_get_drvdata(pdev);
	int err;

	err = host1x_unregister_client(host1x, &dc->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister host1x client: %d\n",
			err);
		return err;
	}

	clk_disable_unprepare(dc->clk);

	return 0;
}

static struct of_device_id tegra_dc_of_match[] = {
	{ .compatible = "nvidia,tegra20-dc", },
	{ },
};

struct platform_driver tegra_dc_driver = {
	.driver = {
		.name = "tegra-dc",
		.owner = THIS_MODULE,
		.of_match_table = tegra_dc_of_match,
	},
	.probe = tegra_dc_probe,
	.remove = tegra_dc_remove,
};
