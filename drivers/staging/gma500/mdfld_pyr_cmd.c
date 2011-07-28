/*
 * Copyright (c)  2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicensen
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Thomas Eaton <thomas.g.eaton@intel.com>
 * Scott Rowe <scott.m.rowe@intel.com>
*/

#include "mdfld_dsi_dbi.h"
#include "mdfld_dsi_dpi.h"
#include "mdfld_dsi_output.h"
#include "mdfld_output.h"
#include "mdfld_dsi_dbi_dpu.h"
#include "mdfld_dsi_pkg_sender.h"

#include "displays/pyr_cmd.h"

static struct drm_display_mode *pyr_cmd_get_config_mode(struct drm_device *dev)
{
	struct drm_display_mode *mode;

	mode = kzalloc(sizeof(*mode), GFP_KERNEL);
	if (!mode) {
		dev_err(dev->dev, "Out of memory\n");
		return NULL;
	}

	dev_dbg(dev->dev, "hdisplay is %d\n", mode->hdisplay);
	dev_dbg(dev->dev, "vdisplay is %d\n", mode->vdisplay);
	dev_dbg(dev->dev, "HSS is %d\n", mode->hsync_start);
	dev_dbg(dev->dev, "HSE is %d\n", mode->hsync_end);
	dev_dbg(dev->dev, "htotal is %d\n", mode->htotal);
	dev_dbg(dev->dev, "VSS is %d\n", mode->vsync_start);
	dev_dbg(dev->dev, "VSE is %d\n", mode->vsync_end);
	dev_dbg(dev->dev, "vtotal is %d\n", mode->vtotal);
	dev_dbg(dev->dev, "clock is %d\n", mode->clock);

	mode->hdisplay = 480;
	mode->vdisplay = 864;
	mode->hsync_start = 487;
	mode->hsync_end = 490;
	mode->htotal = 499;
	mode->vsync_start = 874;
	mode->vsync_end = 878;
	mode->vtotal = 886;
	mode->clock = 25777;

	drm_mode_set_name(mode);
	drm_mode_set_crtcinfo(mode, 0);

	mode->type |= DRM_MODE_TYPE_PREFERRED;

	return mode;
}

static bool pyr_dsi_dbi_mode_fixup(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_display_mode *fixed_mode = pyr_cmd_get_config_mode(dev);

	if (fixed_mode) {
		adjusted_mode->hdisplay = fixed_mode->hdisplay;
		adjusted_mode->hsync_start = fixed_mode->hsync_start;
		adjusted_mode->hsync_end = fixed_mode->hsync_end;
		adjusted_mode->htotal = fixed_mode->htotal;
		adjusted_mode->vdisplay = fixed_mode->vdisplay;
		adjusted_mode->vsync_start = fixed_mode->vsync_start;
		adjusted_mode->vsync_end = fixed_mode->vsync_end;
		adjusted_mode->vtotal = fixed_mode->vtotal;
		adjusted_mode->clock = fixed_mode->clock;
		drm_mode_set_crtcinfo(adjusted_mode, CRTC_INTERLACE_HALVE_V);
		kfree(fixed_mode);
	}
	return true;
}

static void pyr_dsi_dbi_set_power(struct drm_encoder *encoder, bool on)
{
	int ret = 0;
	struct mdfld_dsi_encoder *dsi_encoder = MDFLD_DSI_ENCODER(encoder);
	struct mdfld_dsi_dbi_output *dbi_output =
				MDFLD_DSI_DBI_OUTPUT(dsi_encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 reg_offset = 0;
	int pipe = (dbi_output->channel_num == 0) ? 0 : 2;

	dev_dbg(dev->dev, "pipe %d : %s, panel on: %s\n", pipe,
			on ? "On" : "Off",
			dbi_output->dbi_panel_on ? "True" : "False");

	if (pipe == 2) {
		if (on)
			dev_priv->dual_mipi = true;
		else
			dev_priv->dual_mipi = false;

		reg_offset = MIPIC_REG_OFFSET;
	} else {
		if (!on)
			dev_priv->dual_mipi = false;
	}

	if (!gma_power_begin(dev, true)) {
		dev_err(dev->dev, "hw begin failed\n");
		return;
	}


	if (on) {
		if (dbi_output->dbi_panel_on)
			goto out_err;

		ret = mdfld_dsi_dbi_update_power(dbi_output, DRM_MODE_DPMS_ON);
		if (ret) {
			dev_err(dev->dev, "power on error\n");
			goto out_err;
		}

		dbi_output->dbi_panel_on = true;

		if (pipe == 2) {
			dev_priv->dbi_panel_on2 = true;
		} else {
			dev_priv->dbi_panel_on = true;
			mdfld_enable_te(dev, 0);
		}
	} else {
		if (!dbi_output->dbi_panel_on && !dbi_output->first_boot)
			goto out_err;

		dbi_output->dbi_panel_on = false;
		dbi_output->first_boot = false;

		if (pipe == 2) {
			dev_priv->dbi_panel_on2 = false;
			mdfld_disable_te(dev, 2);
		} else {
			dev_priv->dbi_panel_on = false;
			mdfld_disable_te(dev, 0);

			if (dev_priv->dbi_panel_on2)
				mdfld_enable_te(dev, 2);
		}

		ret = mdfld_dsi_dbi_update_power(dbi_output, DRM_MODE_DPMS_OFF);
		if (ret) {
			dev_err(dev->dev, "power on error\n");
			goto out_err;
		}
	}

out_err:
	gma_power_end(dev);

	if (ret)
		dev_err(dev->dev, "failed\n");
}

static void pyr_dsi_controller_dbi_init(struct mdfld_dsi_config *dsi_config,
								int pipe)
{
	struct drm_device *dev = dsi_config->dev;
	u32 reg_offset = pipe ? MIPIC_REG_OFFSET : 0;
	int lane_count = dsi_config->lane_count;
	u32 val = 0;

	dev_dbg(dev->dev, "Init DBI interface on pipe %d...\n", pipe);

	/* Un-ready device */
	REG_WRITE((MIPIA_DEVICE_READY_REG + reg_offset), 0x00000000);

	/* Init dsi adapter before kicking off */
	REG_WRITE((MIPIA_CONTROL_REG + reg_offset), 0x00000018);

	/* TODO: figure out how to setup these registers */
	REG_WRITE((MIPIA_DPHY_PARAM_REG + reg_offset), 0x150c600F);
	REG_WRITE((MIPIA_CLK_LANE_SWITCH_TIME_CNT_REG + reg_offset),
								0x000a0014);
	REG_WRITE((MIPIA_DBI_BW_CTRL_REG + reg_offset), 0x00000400);
	REG_WRITE((MIPIA_HS_LS_DBI_ENABLE_REG + reg_offset), 0x00000000);

	/* Enable all interrupts */
	REG_WRITE((MIPIA_INTR_EN_REG + reg_offset), 0xffffffff);
	/* Max value: 20 clock cycles of txclkesc */
	REG_WRITE((MIPIA_TURN_AROUND_TIMEOUT_REG + reg_offset), 0x0000001f);
	/* Min 21 txclkesc, max: ffffh */
	REG_WRITE((MIPIA_DEVICE_RESET_TIMER_REG + reg_offset), 0x0000ffff);
	/* Min: 7d0 max: 4e20 */
	REG_WRITE((MIPIA_INIT_COUNT_REG + reg_offset), 0x00000fa0);

	/* Set up func_prg */
	val |= lane_count;
	val |= (dsi_config->channel_num << DSI_DBI_VIRT_CHANNEL_OFFSET);
	val |= DSI_DBI_COLOR_FORMAT_OPTION2;
	REG_WRITE((MIPIA_DSI_FUNC_PRG_REG + reg_offset), val);

	REG_WRITE((MIPIA_HS_TX_TIMEOUT_REG + reg_offset), 0x3fffff);
	REG_WRITE((MIPIA_LP_RX_TIMEOUT_REG + reg_offset), 0xffff);

	/* De-assert dbi_stall when half of DBI FIFO is empty */
	/* REG_WRITE((MIPIA_DBI_FIFO_THROTTLE_REG + reg_offset), 0x00000000); */

	REG_WRITE((MIPIA_HIGH_LOW_SWITCH_COUNT_REG + reg_offset), 0x46);
	REG_WRITE((MIPIA_EOT_DISABLE_REG + reg_offset), 0x00000002);
	REG_WRITE((MIPIA_LP_BYTECLK_REG + reg_offset), 0x00000004);
	REG_WRITE((MIPIA_DEVICE_READY_REG + reg_offset), 0x00000001);
}

static void pyr_dsi_dbi_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	int ret = 0;
	struct drm_device *dev = encoder->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mdfld_dsi_encoder *dsi_encoder = MDFLD_DSI_ENCODER(encoder);
	struct mdfld_dsi_dbi_output *dsi_output =
					MDFLD_DSI_DBI_OUTPUT(dsi_encoder);
	struct mdfld_dsi_config *dsi_config =
				mdfld_dsi_encoder_get_config(dsi_encoder);
	struct mdfld_dsi_connector *dsi_connector = dsi_config->connector;
	int pipe = dsi_connector->pipe;
	u8 param = 0;

	/* Regs */
	u32 mipi_reg = MIPI;
	u32 dspcntr_reg = DSPACNTR;
	u32 pipeconf_reg = PIPEACONF;
	u32 reg_offset = 0;

	/* Values */
	u32 dspcntr_val = dev_priv->dspcntr;
	u32 pipeconf_val = dev_priv->pipeconf;
	u32 h_active_area = mode->hdisplay;
	u32 v_active_area = mode->vdisplay;
	u32 mipi_val = (PASS_FROM_SPHY_TO_AFE | SEL_FLOPPED_HSTX |
							TE_TRIGGER_GPIO_PIN);

	dev_dbg(dev->dev, "mipi_val =0x%x\n", mipi_val);

	dev_dbg(dev->dev, "type %s\n", (pipe == 2) ? "MIPI2" : "MIPI");
	dev_dbg(dev->dev, "h %d v %d\n", mode->hdisplay, mode->vdisplay);

	if (pipe == 2) {
		mipi_reg = MIPI_C;
		dspcntr_reg = DSPCCNTR;
		pipeconf_reg = PIPECCONF;

		reg_offset = MIPIC_REG_OFFSET;

		dspcntr_val = dev_priv->dspcntr2;
		pipeconf_val = dev_priv->pipeconf2;
	} else {
		mipi_val |= 0x2; /* Two lanes for port A and C respectively */
	}

	if (!gma_power_begin(dev, true)) {
		dev_err(dev->dev, "hw begin failed\n");
		return;
	}

	/* Set up pipe related registers */
	REG_WRITE(mipi_reg, mipi_val);
	REG_READ(mipi_reg);

	pyr_dsi_controller_dbi_init(dsi_config, pipe);

	msleep(20);

	REG_WRITE(dspcntr_reg, dspcntr_val);
	REG_READ(dspcntr_reg);

	/* 20ms delay before sending exit_sleep_mode */
	msleep(20);

	/* Send exit_sleep_mode DCS */
	ret = mdfld_dsi_dbi_send_dcs(dsi_output, exit_sleep_mode, NULL,
						0, CMD_DATA_SRC_SYSTEM_MEM);
	if (ret) {
		dev_err(dev->dev, "sent exit_sleep_mode faild\n");
		goto out_err;
	}

	/*send set_tear_on DCS*/
	ret = mdfld_dsi_dbi_send_dcs(dsi_output, set_tear_on,
					&param, 1, CMD_DATA_SRC_SYSTEM_MEM);
	if (ret) {
		dev_err(dev->dev, "%s - sent set_tear_on faild\n", __func__);
		goto out_err;
	}

	/* Do some init stuff */
	mdfld_dsi_brightness_init(dsi_config, pipe);
	mdfld_dsi_gen_fifo_ready(dev, (MIPIA_GEN_FIFO_STAT_REG + reg_offset),
				HS_CTRL_FIFO_EMPTY | HS_DATA_FIFO_EMPTY);

	REG_WRITE(pipeconf_reg, pipeconf_val | PIPEACONF_DSR);
	REG_READ(pipeconf_reg);

	/* TODO: this looks ugly, try to move it to CRTC mode setting */
	if (pipe == 2)
		dev_priv->pipeconf2 |= PIPEACONF_DSR;
	else
		dev_priv->pipeconf |= PIPEACONF_DSR;

	dev_dbg(dev->dev, "pipeconf %x\n",  REG_READ(pipeconf_reg));

	ret = mdfld_dsi_dbi_update_area(dsi_output, 0, 0,
				h_active_area - 1, v_active_area - 1);
	if (ret) {
		dev_err(dev->dev, "update area failed\n");
		goto out_err;
	}

out_err:
	gma_power_end(dev);

	if (ret)
		dev_err(dev->dev, "mode set failed\n");
	else
		dev_dbg(dev->dev, "mode set done successfully\n");
}

static void pyr_dsi_dbi_prepare(struct drm_encoder *encoder)
{
	struct mdfld_dsi_encoder *dsi_encoder = MDFLD_DSI_ENCODER(encoder);
	struct mdfld_dsi_dbi_output *dbi_output =
					MDFLD_DSI_DBI_OUTPUT(dsi_encoder);

	dbi_output->mode_flags |= MODE_SETTING_IN_ENCODER;
	dbi_output->mode_flags &= ~MODE_SETTING_ENCODER_DONE;

	pyr_dsi_dbi_set_power(encoder, false);
}

static void pyr_dsi_dbi_commit(struct drm_encoder *encoder)
{
	struct mdfld_dsi_encoder *dsi_encoder = MDFLD_DSI_ENCODER(encoder);
	struct mdfld_dsi_dbi_output *dbi_output =
					MDFLD_DSI_DBI_OUTPUT(dsi_encoder);
	struct drm_device *dev = dbi_output->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_drm_dpu_rect rect;

	pyr_dsi_dbi_set_power(encoder, true);

	dbi_output->mode_flags &= ~MODE_SETTING_IN_ENCODER;

	rect.x = rect.y = 0;
	rect.width = 864;
	rect.height = 480;

	if (dbi_output->channel_num == 1) {
		dev_priv->dsr_fb_update |= MDFLD_DSR_2D_3D_2;
		/* If DPU enabled report a fullscreen damage */
		mdfld_dbi_dpu_report_damage(dev, MDFLD_PLANEC, &rect);
	} else {
		dev_priv->dsr_fb_update |= MDFLD_DSR_2D_3D_0;
		mdfld_dbi_dpu_report_damage(dev, MDFLD_PLANEA, &rect);
	}
	dbi_output->mode_flags |= MODE_SETTING_ENCODER_DONE;
}

static void pyr_dsi_dbi_dpms(struct drm_encoder *encoder, int mode)
{
	struct mdfld_dsi_encoder *dsi_encoder = MDFLD_DSI_ENCODER(encoder);
	struct mdfld_dsi_dbi_output *dbi_output =
					MDFLD_DSI_DBI_OUTPUT(dsi_encoder);
	struct drm_device *dev = dbi_output->dev;

	dev_dbg(dev->dev, "%s\n",  (mode == DRM_MODE_DPMS_ON ? "on" : "off"));

	if (mode == DRM_MODE_DPMS_ON)
		pyr_dsi_dbi_set_power(encoder, true);
	else
		pyr_dsi_dbi_set_power(encoder, false);
}

/*
 * Update the DBI MIPI Panel Frame Buffer.
 */
static void pyr_dsi_dbi_update_fb(struct mdfld_dsi_dbi_output *dbi_output,
								int pipe)
{
	struct mdfld_dsi_pkg_sender *sender =
		mdfld_dsi_encoder_get_pkg_sender(&dbi_output->base);
	struct drm_device *dev = dbi_output->dev;
	struct drm_crtc *crtc = dbi_output->base.base.crtc;
	struct psb_intel_crtc *psb_crtc = (crtc) ?
				to_psb_intel_crtc(crtc) : NULL;

	u32 dpll_reg = MRST_DPLL_A;
	u32 dspcntr_reg = DSPACNTR;
	u32 pipeconf_reg = PIPEACONF;
	u32 dsplinoff_reg = DSPALINOFF;
	u32 dspsurf_reg = DSPASURF;
	u32 hs_gen_ctrl_reg = HS_GEN_CTRL_REG;
	u32 gen_fifo_stat_reg = GEN_FIFO_STAT_REG;
	u32 reg_offset = 0;

	u32 intr_status;
	u32 fifo_stat_reg_val;
	u32 dpll_reg_val;
	u32 dspcntr_reg_val;
	u32 pipeconf_reg_val;

	/* If mode setting on-going, back off */
	if ((dbi_output->mode_flags & MODE_SETTING_ON_GOING) ||
		(psb_crtc && psb_crtc->mode_flags & MODE_SETTING_ON_GOING) ||
		!(dbi_output->mode_flags & MODE_SETTING_ENCODER_DONE))
		return;

	/*
	 * Look for errors here.  In particular we're checking for whatever
	 * error status might have appeared during the last frame transmit
	 * (memory write).
	 *
	 * Normally, the bits we're testing here would be set infrequently,
	 * if at all.  However, one panel (at least) returns at least one
	 * error bit on most frames.  So we've disabled the kernel message
	 * for now.
	 *
	 * Still clear whatever error bits are set, except don't clear the
	 * ones that would make the Penwell DSI controller reset if we
	 * cleared them.
	 */
	intr_status = REG_READ(INTR_STAT_REG);
	if ((intr_status & 0x26FFFFFF) != 0) {
		/* dev_err(dev->dev, "DSI status: 0x%08X\n", intr_status); */
		intr_status &= 0x26F3FFFF;
		REG_WRITE(INTR_STAT_REG, intr_status);
	}

	if (pipe == 2) {
		dspcntr_reg = DSPCCNTR;
		pipeconf_reg = PIPECCONF;
		dsplinoff_reg = DSPCLINOFF;
		dspsurf_reg = DSPCSURF;

		hs_gen_ctrl_reg = HS_GEN_CTRL_REG + MIPIC_REG_OFFSET;
		gen_fifo_stat_reg = GEN_FIFO_STAT_REG + MIPIC_REG_OFFSET,

		reg_offset = MIPIC_REG_OFFSET;
	}

	if (!gma_power_begin(dev, true)) {
		dev_err(dev->dev, "hw begin failed\n");
		return;
	}

	fifo_stat_reg_val = REG_READ(MIPIA_GEN_FIFO_STAT_REG + reg_offset);
	dpll_reg_val = REG_READ(dpll_reg);
	dspcntr_reg_val = REG_READ(dspcntr_reg);
	pipeconf_reg_val = REG_READ(pipeconf_reg);

	if (!(fifo_stat_reg_val & (1 << 27)) ||
		(dpll_reg_val & DPLL_VCO_ENABLE) ||
		!(dspcntr_reg_val & DISPLAY_PLANE_ENABLE) ||
		!(pipeconf_reg_val & DISPLAY_PLANE_ENABLE)) {
		goto update_fb_out0;
	}

	/* Refresh plane changes */
	REG_WRITE(dsplinoff_reg, REG_READ(dsplinoff_reg));
	REG_WRITE(dspsurf_reg, REG_READ(dspsurf_reg));
	REG_READ(dspsurf_reg);

	mdfld_dsi_send_dcs(sender,
			   write_mem_start,
			   NULL,
			   0,
			   CMD_DATA_SRC_PIPE,
			   MDFLD_DSI_SEND_PACKAGE);

	/*
	 * The idea here is to transmit a Generic Read command after the
	 * Write Memory Start/Continue commands finish.  This asks for
	 * the panel to return an "ACK No Errors," or (if it has errors
	 * to report) an Error Report.  This allows us to monitor the
	 * panel's perception of the health of the DSI.
	 */
	mdfld_dsi_gen_fifo_ready(dev, gen_fifo_stat_reg,
				HS_CTRL_FIFO_EMPTY | HS_DATA_FIFO_EMPTY);
	REG_WRITE(hs_gen_ctrl_reg, (1 << WORD_COUNTS_POS) | GEN_READ_0);

	dbi_output->dsr_fb_update_done = true;
update_fb_out0:
	gma_power_end(dev);
}

/*
 * TODO: will be removed later, should work out display interfaces for power
 */
void pyr_dsi_adapter_init(struct mdfld_dsi_config *dsi_config, int pipe)
{
	if (!dsi_config || (pipe != 0 && pipe != 2)) {
		WARN_ON(1);
		return;
	}
	pyr_dsi_controller_dbi_init(dsi_config, pipe);
}

static int pyr_cmd_get_panel_info(struct drm_device *dev, int pipe,
							struct panel_info *pi)
{
	if (!dev || !pi)
		return -EINVAL;

	pi->width_mm = PYR_PANEL_WIDTH;
	pi->height_mm = PYR_PANEL_HEIGHT;

	return 0;
}

/* PYR DBI encoder helper funcs */
static const struct drm_encoder_helper_funcs pyr_dsi_dbi_helper_funcs = {
	.dpms = pyr_dsi_dbi_dpms,
	.mode_fixup = pyr_dsi_dbi_mode_fixup,
	.prepare = pyr_dsi_dbi_prepare,
	.mode_set = pyr_dsi_dbi_mode_set,
	.commit = pyr_dsi_dbi_commit,
};

/* PYR DBI encoder funcs */
static const struct drm_encoder_funcs mdfld_dsi_dbi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

void pyr_cmd_init(struct drm_device *dev, struct panel_funcs *p_funcs)
{
	p_funcs->encoder_funcs = &mdfld_dsi_dbi_encoder_funcs;
	p_funcs->encoder_helper_funcs = &pyr_dsi_dbi_helper_funcs;
	p_funcs->get_config_mode = &pyr_cmd_get_config_mode;
	p_funcs->update_fb = pyr_dsi_dbi_update_fb;
	p_funcs->get_panel_info = pyr_cmd_get_panel_info;
}
