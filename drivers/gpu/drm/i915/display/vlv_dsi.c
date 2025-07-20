/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
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
 * Author: Jani Nikula <jani.nikula@intel.com>
 */

#include <linux/dmi.h>
#include <linux/slab.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_probe_helper.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_atomic.h"
#include "intel_backlight.h"
#include "intel_connector.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dsi.h"
#include "intel_dsi_vbt.h"
#include "intel_fifo_underrun.h"
#include "intel_panel.h"
#include "intel_pfit.h"
#include "skl_scaler.h"
#include "vlv_dsi.h"
#include "vlv_dsi_pll.h"
#include "vlv_dsi_regs.h"
#include "vlv_sideband.h"

/* return pixels in terms of txbyteclkhs */
static u16 txbyteclkhs(u16 pixels, int bpp, int lane_count,
		       u16 burst_mode_ratio)
{
	return DIV_ROUND_UP(DIV_ROUND_UP(pixels * bpp * burst_mode_ratio,
					 8 * 100), lane_count);
}

/* return pixels equivalent to txbyteclkhs */
static u16 pixels_from_txbyteclkhs(u16 clk_hs, int bpp, int lane_count,
			u16 burst_mode_ratio)
{
	return DIV_ROUND_UP((clk_hs * lane_count * 8 * 100),
						(bpp * burst_mode_ratio));
}

static enum mipi_dsi_pixel_format pixel_format_from_register_bits(u32 fmt)
{
	switch (fmt) {
	case VID_MODE_FORMAT_RGB888:
		return MIPI_DSI_FMT_RGB888;
	case VID_MODE_FORMAT_RGB666:
		return MIPI_DSI_FMT_RGB666;
	case VID_MODE_FORMAT_RGB666_PACKED:
		return MIPI_DSI_FMT_RGB666_PACKED;
	case VID_MODE_FORMAT_RGB565:
		return MIPI_DSI_FMT_RGB565;
	default:
		MISSING_CASE(fmt);
		return MIPI_DSI_FMT_RGB666;
	}
}

void vlv_dsi_wait_for_fifo_empty(struct intel_dsi *intel_dsi, enum port port)
{
	struct intel_display *display = to_intel_display(&intel_dsi->base);
	u32 mask;

	mask = LP_CTRL_FIFO_EMPTY | HS_CTRL_FIFO_EMPTY |
		LP_DATA_FIFO_EMPTY | HS_DATA_FIFO_EMPTY;

	if (intel_de_wait_for_set(display, MIPI_GEN_FIFO_STAT(display, port),
				  mask, 100))
		drm_err(display->drm, "DPI FIFOs are not empty\n");
}

static void write_data(struct intel_display *display,
		       i915_reg_t reg,
		       const u8 *data, u32 len)
{
	u32 i, j;

	for (i = 0; i < len; i += 4) {
		u32 val = 0;

		for (j = 0; j < min_t(u32, len - i, 4); j++)
			val |= *data++ << 8 * j;

		intel_de_write(display, reg, val);
	}
}

static void read_data(struct intel_display *display,
		      i915_reg_t reg,
		      u8 *data, u32 len)
{
	u32 i, j;

	for (i = 0; i < len; i += 4) {
		u32 val = intel_de_read(display, reg);

		for (j = 0; j < min_t(u32, len - i, 4); j++)
			*data++ = val >> 8 * j;
	}
}

static ssize_t intel_dsi_host_transfer(struct mipi_dsi_host *host,
				       const struct mipi_dsi_msg *msg)
{
	struct intel_dsi_host *intel_dsi_host = to_intel_dsi_host(host);
	struct intel_dsi *intel_dsi = intel_dsi_host->intel_dsi;
	struct intel_display *display = to_intel_display(&intel_dsi->base);
	enum port port = intel_dsi_host->port;
	struct mipi_dsi_packet packet;
	ssize_t ret;
	const u8 *header;
	i915_reg_t data_reg, ctrl_reg;
	u32 data_mask, ctrl_mask;

	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret < 0)
		return ret;

	header = packet.header;

	if (msg->flags & MIPI_DSI_MSG_USE_LPM) {
		data_reg = MIPI_LP_GEN_DATA(display, port);
		data_mask = LP_DATA_FIFO_FULL;
		ctrl_reg = MIPI_LP_GEN_CTRL(display, port);
		ctrl_mask = LP_CTRL_FIFO_FULL;
	} else {
		data_reg = MIPI_HS_GEN_DATA(display, port);
		data_mask = HS_DATA_FIFO_FULL;
		ctrl_reg = MIPI_HS_GEN_CTRL(display, port);
		ctrl_mask = HS_CTRL_FIFO_FULL;
	}

	/* note: this is never true for reads */
	if (packet.payload_length) {
		if (intel_de_wait_for_clear(display, MIPI_GEN_FIFO_STAT(display, port),
					    data_mask, 50))
			drm_err(display->drm,
				"Timeout waiting for HS/LP DATA FIFO !full\n");

		write_data(display, data_reg, packet.payload,
			   packet.payload_length);
	}

	if (msg->rx_len) {
		intel_de_write(display, MIPI_INTR_STAT(display, port),
			       GEN_READ_DATA_AVAIL);
	}

	if (intel_de_wait_for_clear(display, MIPI_GEN_FIFO_STAT(display, port),
				    ctrl_mask, 50)) {
		drm_err(display->drm,
			"Timeout waiting for HS/LP CTRL FIFO !full\n");
	}

	intel_de_write(display, ctrl_reg,
		       header[2] << 16 | header[1] << 8 | header[0]);

	/* ->rx_len is set only for reads */
	if (msg->rx_len) {
		data_mask = GEN_READ_DATA_AVAIL;
		if (intel_de_wait_for_set(display, MIPI_INTR_STAT(display, port),
					  data_mask, 50))
			drm_err(display->drm,
				"Timeout waiting for read data.\n");

		read_data(display, data_reg, msg->rx_buf, msg->rx_len);
	}

	/* XXX: fix for reads and writes */
	return 4 + packet.payload_length;
}

static int intel_dsi_host_attach(struct mipi_dsi_host *host,
				 struct mipi_dsi_device *dsi)
{
	return 0;
}

static int intel_dsi_host_detach(struct mipi_dsi_host *host,
				 struct mipi_dsi_device *dsi)
{
	return 0;
}

static const struct mipi_dsi_host_ops intel_dsi_host_ops = {
	.attach = intel_dsi_host_attach,
	.detach = intel_dsi_host_detach,
	.transfer = intel_dsi_host_transfer,
};

/*
 * send a video mode command
 *
 * XXX: commands with data in MIPI_DPI_DATA?
 */
static int dpi_send_cmd(struct intel_dsi *intel_dsi, u32 cmd, bool hs,
			enum port port)
{
	struct intel_display *display = to_intel_display(&intel_dsi->base);
	u32 mask;

	/* XXX: pipe, hs */
	if (hs)
		cmd &= ~DPI_LP_MODE;
	else
		cmd |= DPI_LP_MODE;

	/* clear bit */
	intel_de_write(display, MIPI_INTR_STAT(display, port), SPL_PKT_SENT_INTERRUPT);

	/* XXX: old code skips write if control unchanged */
	if (cmd == intel_de_read(display, MIPI_DPI_CONTROL(display, port)))
		drm_dbg_kms(display->drm,
			    "Same special packet %02x twice in a row.\n", cmd);

	intel_de_write(display, MIPI_DPI_CONTROL(display, port), cmd);

	mask = SPL_PKT_SENT_INTERRUPT;
	if (intel_de_wait_for_set(display, MIPI_INTR_STAT(display, port), mask, 100))
		drm_err(display->drm,
			"Video mode command 0x%08x send failed.\n", cmd);

	return 0;
}

static void band_gap_reset(struct intel_display *display)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);

	vlv_flisdsi_get(dev_priv);

	vlv_flisdsi_write(dev_priv, 0x08, 0x0001);
	vlv_flisdsi_write(dev_priv, 0x0F, 0x0005);
	vlv_flisdsi_write(dev_priv, 0x0F, 0x0025);
	udelay(150);
	vlv_flisdsi_write(dev_priv, 0x0F, 0x0000);
	vlv_flisdsi_write(dev_priv, 0x08, 0x0000);

	vlv_flisdsi_put(dev_priv);
}

static int intel_dsi_compute_config(struct intel_encoder *encoder,
				    struct intel_crtc_state *pipe_config,
				    struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	struct intel_connector *intel_connector = intel_dsi->attached_connector;
	struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	int ret;

	drm_dbg_kms(display->drm, "\n");
	pipe_config->sink_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;

	ret = intel_panel_compute_config(intel_connector, adjusted_mode);
	if (ret)
		return ret;

	ret = intel_pfit_compute_config(pipe_config, conn_state);
	if (ret)
		return ret;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	/* DSI uses short packets for sync events, so clear mode flags for DSI */
	adjusted_mode->flags = 0;

	if (intel_dsi->pixel_format == MIPI_DSI_FMT_RGB888)
		pipe_config->pipe_bpp = 24;
	else
		pipe_config->pipe_bpp = 18;

	if (display->platform.geminilake || display->platform.broxton) {
		/* Enable Frame time stamp based scanline reporting */
		pipe_config->mode_flags |=
			I915_MODE_FLAG_GET_SCANLINE_FROM_TIMESTAMP;

		/* Dual link goes to DSI transcoder A. */
		if (intel_dsi->ports == BIT(PORT_C))
			pipe_config->cpu_transcoder = TRANSCODER_DSI_C;
		else
			pipe_config->cpu_transcoder = TRANSCODER_DSI_A;

		ret = bxt_dsi_pll_compute(encoder, pipe_config);
		if (ret)
			return -EINVAL;
	} else {
		ret = vlv_dsi_pll_compute(encoder, pipe_config);
		if (ret)
			return -EINVAL;
	}

	pipe_config->clock_set = true;

	return 0;
}

static bool glk_dsi_enable_io(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;
	bool cold_boot = false;

	/* Set the MIPI mode
	 * If MIPI_Mode is off, then writing to LP_Wake bit is not reflecting.
	 * Power ON MIPI IO first and then write into IO reset and LP wake bits
	 */
	for_each_dsi_port(port, intel_dsi->ports)
		intel_de_rmw(display, MIPI_CTRL(display, port), 0, GLK_MIPIIO_ENABLE);

	/* Put the IO into reset */
	intel_de_rmw(display, MIPI_CTRL(display, PORT_A), GLK_MIPIIO_RESET_RELEASED, 0);

	/* Program LP Wake */
	for_each_dsi_port(port, intel_dsi->ports) {
		u32 tmp = intel_de_read(display, MIPI_DEVICE_READY(display, port));

		intel_de_rmw(display, MIPI_CTRL(display, port),
			     GLK_LP_WAKE, (tmp & DEVICE_READY) ? GLK_LP_WAKE : 0);
	}

	/* Wait for Pwr ACK */
	for_each_dsi_port(port, intel_dsi->ports) {
		if (intel_de_wait_for_set(display, MIPI_CTRL(display, port),
					  GLK_MIPIIO_PORT_POWERED, 20))
			drm_err(display->drm, "MIPIO port is powergated\n");
	}

	/* Check for cold boot scenario */
	for_each_dsi_port(port, intel_dsi->ports) {
		cold_boot |=
			!(intel_de_read(display, MIPI_DEVICE_READY(display, port)) & DEVICE_READY);
	}

	return cold_boot;
}

static void glk_dsi_device_ready(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	/* Wait for MIPI PHY status bit to set */
	for_each_dsi_port(port, intel_dsi->ports) {
		if (intel_de_wait_for_set(display, MIPI_CTRL(display, port),
					  GLK_PHY_STATUS_PORT_READY, 20))
			drm_err(display->drm, "PHY is not ON\n");
	}

	/* Get IO out of reset */
	intel_de_rmw(display, MIPI_CTRL(display, PORT_A), 0, GLK_MIPIIO_RESET_RELEASED);

	/* Get IO out of Low power state*/
	for_each_dsi_port(port, intel_dsi->ports) {
		if (!(intel_de_read(display, MIPI_DEVICE_READY(display, port)) & DEVICE_READY)) {
			intel_de_rmw(display, MIPI_DEVICE_READY(display, port),
				     ULPS_STATE_MASK, DEVICE_READY);
			usleep_range(10, 15);
		} else {
			/* Enter ULPS */
			intel_de_rmw(display, MIPI_DEVICE_READY(display, port),
				     ULPS_STATE_MASK, ULPS_STATE_ENTER | DEVICE_READY);

			/* Wait for ULPS active */
			if (intel_de_wait_for_clear(display, MIPI_CTRL(display, port),
						    GLK_ULPS_NOT_ACTIVE, 20))
				drm_err(display->drm, "ULPS not active\n");

			/* Exit ULPS */
			intel_de_rmw(display, MIPI_DEVICE_READY(display, port),
				     ULPS_STATE_MASK, ULPS_STATE_EXIT | DEVICE_READY);

			/* Enter Normal Mode */
			intel_de_rmw(display, MIPI_DEVICE_READY(display, port),
				     ULPS_STATE_MASK,
				     ULPS_STATE_NORMAL_OPERATION | DEVICE_READY);

			intel_de_rmw(display, MIPI_CTRL(display, port), GLK_LP_WAKE, 0);
		}
	}

	/* Wait for Stop state */
	for_each_dsi_port(port, intel_dsi->ports) {
		if (intel_de_wait_for_set(display, MIPI_CTRL(display, port),
					  GLK_DATA_LANE_STOP_STATE, 20))
			drm_err(display->drm,
				"Date lane not in STOP state\n");
	}

	/* Wait for AFE LATCH */
	for_each_dsi_port(port, intel_dsi->ports) {
		if (intel_de_wait_for_set(display, BXT_MIPI_PORT_CTRL(port),
					  AFE_LATCHOUT, 20))
			drm_err(display->drm,
				"D-PHY not entering LP-11 state\n");
	}
}

static void bxt_dsi_device_ready(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;
	u32 val;

	drm_dbg_kms(display->drm, "\n");

	/* Enable MIPI PHY transparent latch */
	for_each_dsi_port(port, intel_dsi->ports) {
		intel_de_rmw(display, BXT_MIPI_PORT_CTRL(port), 0, LP_OUTPUT_HOLD);
		usleep_range(2000, 2500);
	}

	/* Clear ULPS and set device ready */
	for_each_dsi_port(port, intel_dsi->ports) {
		val = intel_de_read(display, MIPI_DEVICE_READY(display, port));
		val &= ~ULPS_STATE_MASK;
		intel_de_write(display, MIPI_DEVICE_READY(display, port), val);
		usleep_range(2000, 2500);
		val |= DEVICE_READY;
		intel_de_write(display, MIPI_DEVICE_READY(display, port), val);
	}
}

static void vlv_dsi_device_ready(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	drm_dbg_kms(display->drm, "\n");

	vlv_flisdsi_get(dev_priv);
	/* program rcomp for compliance, reduce from 50 ohms to 45 ohms
	 * needed everytime after power gate */
	vlv_flisdsi_write(dev_priv, 0x04, 0x0004);
	vlv_flisdsi_put(dev_priv);

	/* bandgap reset is needed after everytime we do power gate */
	band_gap_reset(display);

	for_each_dsi_port(port, intel_dsi->ports) {

		intel_de_write(display, MIPI_DEVICE_READY(display, port),
			       ULPS_STATE_ENTER);
		usleep_range(2500, 3000);

		/* Enable MIPI PHY transparent latch
		 * Common bit for both MIPI Port A & MIPI Port C
		 * No similar bit in MIPI Port C reg
		 */
		intel_de_rmw(display, VLV_MIPI_PORT_CTRL(PORT_A), 0, LP_OUTPUT_HOLD);
		usleep_range(1000, 1500);

		intel_de_write(display, MIPI_DEVICE_READY(display, port),
			       ULPS_STATE_EXIT);
		usleep_range(2500, 3000);

		intel_de_write(display, MIPI_DEVICE_READY(display, port),
			       DEVICE_READY);
		usleep_range(2500, 3000);
	}
}

static void intel_dsi_device_ready(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);

	if (display->platform.geminilake)
		glk_dsi_device_ready(encoder);
	else if (display->platform.geminilake || display->platform.broxton)
		bxt_dsi_device_ready(encoder);
	else
		vlv_dsi_device_ready(encoder);
}

static void glk_dsi_enter_low_power_mode(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	/* Enter ULPS */
	for_each_dsi_port(port, intel_dsi->ports)
		intel_de_rmw(display, MIPI_DEVICE_READY(display, port),
			     ULPS_STATE_MASK, ULPS_STATE_ENTER | DEVICE_READY);

	/* Wait for MIPI PHY status bit to unset */
	for_each_dsi_port(port, intel_dsi->ports) {
		if (intel_de_wait_for_clear(display, MIPI_CTRL(display, port),
					    GLK_PHY_STATUS_PORT_READY, 20))
			drm_err(display->drm, "PHY is not turning OFF\n");
	}

	/* Wait for Pwr ACK bit to unset */
	for_each_dsi_port(port, intel_dsi->ports) {
		if (intel_de_wait_for_clear(display, MIPI_CTRL(display, port),
					    GLK_MIPIIO_PORT_POWERED, 20))
			drm_err(display->drm,
				"MIPI IO Port is not powergated\n");
	}
}

static void glk_dsi_disable_mipi_io(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	/* Put the IO into reset */
	intel_de_rmw(display, MIPI_CTRL(display, PORT_A), GLK_MIPIIO_RESET_RELEASED, 0);

	/* Wait for MIPI PHY status bit to unset */
	for_each_dsi_port(port, intel_dsi->ports) {
		if (intel_de_wait_for_clear(display, MIPI_CTRL(display, port),
					    GLK_PHY_STATUS_PORT_READY, 20))
			drm_err(display->drm, "PHY is not turning OFF\n");
	}

	/* Clear MIPI mode */
	for_each_dsi_port(port, intel_dsi->ports)
		intel_de_rmw(display, MIPI_CTRL(display, port), GLK_MIPIIO_ENABLE, 0);
}

static void glk_dsi_clear_device_ready(struct intel_encoder *encoder)
{
	glk_dsi_enter_low_power_mode(encoder);
	glk_dsi_disable_mipi_io(encoder);
}

static i915_reg_t port_ctrl_reg(struct intel_display *display, enum port port)
{
	return display->platform.geminilake || display->platform.broxton ?
		BXT_MIPI_PORT_CTRL(port) : VLV_MIPI_PORT_CTRL(port);
}

static void vlv_dsi_clear_device_ready(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	drm_dbg_kms(display->drm, "\n");
	for_each_dsi_port(port, intel_dsi->ports) {
		/* Common bit for both MIPI Port A & MIPI Port C on VLV/CHV */
		i915_reg_t port_ctrl = display->platform.broxton ?
			BXT_MIPI_PORT_CTRL(port) : VLV_MIPI_PORT_CTRL(PORT_A);

		intel_de_write(display, MIPI_DEVICE_READY(display, port),
			       DEVICE_READY | ULPS_STATE_ENTER);
		usleep_range(2000, 2500);

		intel_de_write(display, MIPI_DEVICE_READY(display, port),
			       DEVICE_READY | ULPS_STATE_EXIT);
		usleep_range(2000, 2500);

		intel_de_write(display, MIPI_DEVICE_READY(display, port),
			       DEVICE_READY | ULPS_STATE_ENTER);
		usleep_range(2000, 2500);

		/*
		 * On VLV/CHV, wait till Clock lanes are in LP-00 state for MIPI
		 * Port A only. MIPI Port C has no similar bit for checking.
		 */
		if ((display->platform.broxton || port == PORT_A) &&
		    intel_de_wait_for_clear(display, port_ctrl,
					    AFE_LATCHOUT, 30))
			drm_err(display->drm, "DSI LP not going Low\n");

		/* Disable MIPI PHY transparent latch */
		intel_de_rmw(display, port_ctrl, LP_OUTPUT_HOLD, 0);
		usleep_range(1000, 1500);

		intel_de_write(display, MIPI_DEVICE_READY(display, port), 0x00);
		usleep_range(2000, 2500);
	}
}

static void intel_dsi_port_enable(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK) {
		u32 temp = intel_dsi->pixel_overlap;

		if (display->platform.geminilake || display->platform.broxton) {
			for_each_dsi_port(port, intel_dsi->ports)
				intel_de_rmw(display, MIPI_CTRL(display, port),
					     BXT_PIXEL_OVERLAP_CNT_MASK,
					     temp << BXT_PIXEL_OVERLAP_CNT_SHIFT);
		} else {
			intel_de_rmw(display, VLV_CHICKEN_3,
				     PIXEL_OVERLAP_CNT_MASK,
				     temp << PIXEL_OVERLAP_CNT_SHIFT);
		}
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		i915_reg_t port_ctrl = port_ctrl_reg(display, port);
		u32 temp;

		temp = intel_de_read(display, port_ctrl);

		temp &= ~LANE_CONFIGURATION_MASK;
		temp &= ~DUAL_LINK_MODE_MASK;

		if (intel_dsi->ports == (BIT(PORT_A) | BIT(PORT_C))) {
			temp |= (intel_dsi->dual_link - 1)
						<< DUAL_LINK_MODE_SHIFT;
			if (display->platform.broxton)
				temp |= LANE_CONFIGURATION_DUAL_LINK_A;
			else
				temp |= crtc->pipe ?
					LANE_CONFIGURATION_DUAL_LINK_B :
					LANE_CONFIGURATION_DUAL_LINK_A;
		}

		if (intel_dsi->pixel_format != MIPI_DSI_FMT_RGB888)
			temp |= DITHERING_ENABLE;

		/* assert ip_tg_enable signal */
		intel_de_write(display, port_ctrl, temp | DPI_ENABLE);
		intel_de_posting_read(display, port_ctrl);
	}
}

static void intel_dsi_port_disable(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	for_each_dsi_port(port, intel_dsi->ports) {
		i915_reg_t port_ctrl = port_ctrl_reg(display, port);

		/* de-assert ip_tg_enable signal */
		intel_de_rmw(display, port_ctrl, DPI_ENABLE, 0);
		intel_de_posting_read(display, port_ctrl);
	}
}

static void intel_dsi_prepare(struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config);
static void intel_dsi_unprepare(struct intel_encoder *encoder);

/*
 * Panel enable/disable sequences from the VBT spec.
 *
 * Note the spec has AssertReset / DeassertReset swapped from their
 * usual naming. We use the normal names to avoid confusion (so below
 * they are swapped compared to the spec).
 *
 * Steps starting with MIPI refer to VBT sequences, note that for v2
 * VBTs several steps which have a VBT in v2 are expected to be handled
 * directly by the driver, by directly driving gpios for example.
 *
 * v2 video mode seq         v3 video mode seq         command mode seq
 * - power on                - MIPIPanelPowerOn        - power on
 * - wait t1+t2                                        - wait t1+t2
 * - MIPIDeassertResetPin    - MIPIDeassertResetPin    - MIPIDeassertResetPin
 * - io lines to lp-11       - io lines to lp-11       - io lines to lp-11
 * - MIPISendInitialDcsCmds  - MIPISendInitialDcsCmds  - MIPISendInitialDcsCmds
 *                                                     - MIPITearOn
 *                                                     - MIPIDisplayOn
 * - turn on DPI             - turn on DPI             - set pipe to dsr mode
 * - MIPIDisplayOn           - MIPIDisplayOn
 * - wait t5                                           - wait t5
 * - backlight on            - MIPIBacklightOn         - backlight on
 * ...                       ...                       ... issue mem cmds ...
 * - backlight off           - MIPIBacklightOff        - backlight off
 * - wait t6                                           - wait t6
 * - MIPIDisplayOff
 * - turn off DPI            - turn off DPI            - disable pipe dsr mode
 *                                                     - MIPITearOff
 *                           - MIPIDisplayOff          - MIPIDisplayOff
 * - io lines to lp-00       - io lines to lp-00       - io lines to lp-00
 * - MIPIAssertResetPin      - MIPIAssertResetPin      - MIPIAssertResetPin
 * - wait t3                                           - wait t3
 * - power off               - MIPIPanelPowerOff       - power off
 * - wait t4                                           - wait t4
 */

/*
 * DSI port enable has to be done before pipe and plane enable, so we do it in
 * the pre_enable hook instead of the enable hook.
 */
static void intel_dsi_pre_enable(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *pipe_config,
				 const struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	enum pipe pipe = crtc->pipe;
	enum port port;
	bool glk_cold_boot = false;

	drm_dbg_kms(display->drm, "\n");

	intel_dsi_wait_panel_power_cycle(intel_dsi);

	intel_set_cpu_fifo_underrun_reporting(display, pipe, true);

	/*
	 * The BIOS may leave the PLL in a wonky state where it doesn't
	 * lock. It needs to be fully powered down to fix it.
	 */
	if (display->platform.geminilake || display->platform.broxton) {
		bxt_dsi_pll_disable(encoder);
		bxt_dsi_pll_enable(encoder, pipe_config);
	} else {
		vlv_dsi_pll_disable(encoder);
		vlv_dsi_pll_enable(encoder, pipe_config);
	}

	if (display->platform.broxton) {
		/* Add MIPI IO reset programming for modeset */
		intel_de_rmw(display, BXT_P_CR_GT_DISP_PWRON, 0, MIPIO_RST_CTRL);

		/* Power up DSI regulator */
		intel_de_write(display, BXT_P_DSI_REGULATOR_CFG, STAP_SELECT);
		intel_de_write(display, BXT_P_DSI_REGULATOR_TX_CTRL, 0);
	}

	if (display->platform.valleyview || display->platform.cherryview) {
		/* Disable DPOunit clock gating, can stall pipe */
		intel_de_rmw(display, DSPCLK_GATE_D(display),
			     0, DPOUNIT_CLOCK_GATE_DISABLE);
	}

	if (!display->platform.geminilake)
		intel_dsi_prepare(encoder, pipe_config);

	/* Give the panel time to power-on and then deassert its reset */
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_POWER_ON);
	msleep(intel_dsi->panel_on_delay);
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_DEASSERT_RESET);

	if (display->platform.geminilake) {
		glk_cold_boot = glk_dsi_enable_io(encoder);

		/* Prepare port in cold boot(s3/s4) scenario */
		if (glk_cold_boot)
			intel_dsi_prepare(encoder, pipe_config);
	}

	/* Put device in ready state (LP-11) */
	intel_dsi_device_ready(encoder);

	/* Prepare port in normal boot scenario */
	if (display->platform.geminilake && !glk_cold_boot)
		intel_dsi_prepare(encoder, pipe_config);

	/* Send initialization commands in LP mode */
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_INIT_OTP);

	/*
	 * Enable port in pre-enable phase itself because as per hw team
	 * recommendation, port should be enabled before plane & pipe
	 */
	if (is_cmd_mode(intel_dsi)) {
		for_each_dsi_port(port, intel_dsi->ports)
			intel_de_write(display,
				       MIPI_MAX_RETURN_PKT_SIZE(display, port), 8 * 4);
		intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_TEAR_ON);
		intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_DISPLAY_ON);
	} else {
		msleep(20); /* XXX */
		for_each_dsi_port(port, intel_dsi->ports)
			dpi_send_cmd(intel_dsi, TURN_ON, false, port);
		msleep(100);

		intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_DISPLAY_ON);

		intel_dsi_port_enable(encoder, pipe_config);
	}

	intel_backlight_enable(pipe_config, conn_state);
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_BACKLIGHT_ON);
}

static void bxt_dsi_enable(struct intel_atomic_state *state,
			   struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state,
			   const struct drm_connector_state *conn_state)
{
	intel_crtc_vblank_on(crtc_state);
}

/*
 * DSI port disable has to be done after pipe and plane disable, so we do it in
 * the post_disable hook.
 */
static void intel_dsi_disable(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state,
			      const struct drm_connector_state *old_conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	drm_dbg_kms(display->drm, "\n");

	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_BACKLIGHT_OFF);
	intel_backlight_disable(old_conn_state);

	/*
	 * According to the spec we should send SHUTDOWN before
	 * MIPI_SEQ_DISPLAY_OFF only for v3+ VBTs, but field testing
	 * has shown that the v3 sequence works for v2 VBTs too
	 */
	if (is_vid_mode(intel_dsi)) {
		/* Send Shutdown command to the panel in LP mode */
		for_each_dsi_port(port, intel_dsi->ports)
			dpi_send_cmd(intel_dsi, SHUTDOWN, false, port);
		msleep(10);
	}
}

static void intel_dsi_clear_device_ready(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);

	if (display->platform.geminilake)
		glk_dsi_clear_device_ready(encoder);
	else
		vlv_dsi_clear_device_ready(encoder);
}

static void intel_dsi_post_disable(struct intel_atomic_state *state,
				   struct intel_encoder *encoder,
				   const struct intel_crtc_state *old_crtc_state,
				   const struct drm_connector_state *old_conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	drm_dbg_kms(display->drm, "\n");

	if (display->platform.geminilake || display->platform.broxton) {
		intel_crtc_vblank_off(old_crtc_state);

		skl_scaler_disable(old_crtc_state);
	}

	if (is_vid_mode(intel_dsi)) {
		for_each_dsi_port(port, intel_dsi->ports)
			vlv_dsi_wait_for_fifo_empty(intel_dsi, port);

		intel_dsi_port_disable(encoder);
		usleep_range(2000, 5000);
	}

	intel_dsi_unprepare(encoder);

	/*
	 * if disable packets are sent before sending shutdown packet then in
	 * some next enable sequence send turn on packet error is observed
	 */
	if (is_cmd_mode(intel_dsi))
		intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_TEAR_OFF);
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_DISPLAY_OFF);

	/* Transition to LP-00 */
	intel_dsi_clear_device_ready(encoder);

	if (display->platform.broxton) {
		/* Power down DSI regulator to save power */
		intel_de_write(display, BXT_P_DSI_REGULATOR_CFG, STAP_SELECT);
		intel_de_write(display, BXT_P_DSI_REGULATOR_TX_CTRL,
			       HS_IO_CTRL_SELECT);

		/* Add MIPI IO reset programming for modeset */
		intel_de_rmw(display, BXT_P_CR_GT_DISP_PWRON, MIPIO_RST_CTRL, 0);
	}

	if (display->platform.geminilake || display->platform.broxton) {
		bxt_dsi_pll_disable(encoder);
	} else {
		vlv_dsi_pll_disable(encoder);

		intel_de_rmw(display, DSPCLK_GATE_D(display),
			     DPOUNIT_CLOCK_GATE_DISABLE, 0);
	}

	/* Assert reset */
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_ASSERT_RESET);

	msleep(intel_dsi->panel_off_delay);
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_POWER_OFF);

	intel_dsi->panel_power_off_time = ktime_get_boottime();
}

static bool intel_dsi_get_hw_state(struct intel_encoder *encoder,
				   enum pipe *pipe)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	intel_wakeref_t wakeref;
	enum port port;
	bool active = false;

	drm_dbg_kms(display->drm, "\n");

	wakeref = intel_display_power_get_if_enabled(display,
						     encoder->power_domain);
	if (!wakeref)
		return false;

	/*
	 * On Broxton the PLL needs to be enabled with a valid divider
	 * configuration, otherwise accessing DSI registers will hang the
	 * machine. See BSpec North Display Engine registers/MIPI[BXT].
	 */
	if ((display->platform.geminilake || display->platform.broxton) &&
	    !bxt_dsi_pll_is_enabled(display))
		goto out_put_power;

	/* XXX: this only works for one DSI output */
	for_each_dsi_port(port, intel_dsi->ports) {
		i915_reg_t port_ctrl = port_ctrl_reg(display, port);
		bool enabled = intel_de_read(display, port_ctrl) & DPI_ENABLE;

		/*
		 * Due to some hardware limitations on VLV/CHV, the DPI enable
		 * bit in port C control register does not get set. As a
		 * workaround, check pipe B conf instead.
		 */
		if ((display->platform.valleyview || display->platform.cherryview) &&
		    port == PORT_C)
			enabled = intel_de_read(display,
						TRANSCONF(display, PIPE_B)) & TRANSCONF_ENABLE;

		/* Try command mode if video mode not enabled */
		if (!enabled) {
			u32 tmp = intel_de_read(display,
						MIPI_DSI_FUNC_PRG(display, port));
			enabled = tmp & CMD_MODE_DATA_WIDTH_MASK;
		}

		if (!enabled)
			continue;

		if (!(intel_de_read(display, MIPI_DEVICE_READY(display, port)) & DEVICE_READY))
			continue;

		if (display->platform.geminilake || display->platform.broxton) {
			u32 tmp = intel_de_read(display, MIPI_CTRL(display, port));
			tmp &= BXT_PIPE_SELECT_MASK;
			tmp >>= BXT_PIPE_SELECT_SHIFT;

			if (drm_WARN_ON(display->drm, tmp > PIPE_C))
				continue;

			*pipe = tmp;
		} else {
			*pipe = port == PORT_A ? PIPE_A : PIPE_B;
		}

		active = true;
		break;
	}

out_put_power:
	intel_display_power_put(display, encoder->power_domain, wakeref);

	return active;
}

static void bxt_dsi_get_pipe_config(struct intel_encoder *encoder,
				    struct intel_crtc_state *pipe_config)
{
	struct intel_display *display = to_intel_display(encoder);
	struct drm_display_mode *adjusted_mode =
					&pipe_config->hw.adjusted_mode;
	struct drm_display_mode *adjusted_mode_sw;
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	unsigned int lane_count = intel_dsi->lane_count;
	unsigned int bpp, fmt;
	enum port port;
	u16 hactive, hfp, hsync, hbp, vfp, vsync;
	u16 hfp_sw, hsync_sw, hbp_sw;
	u16 crtc_htotal_sw, crtc_hsync_start_sw, crtc_hsync_end_sw,
				crtc_hblank_start_sw, crtc_hblank_end_sw;

	/* FIXME: hw readout should not depend on SW state */
	adjusted_mode_sw = &crtc->config->hw.adjusted_mode;

	/*
	 * Atleast one port is active as encoder->get_config called only if
	 * encoder->get_hw_state() returns true.
	 */
	for_each_dsi_port(port, intel_dsi->ports) {
		if (intel_de_read(display, BXT_MIPI_PORT_CTRL(port)) & DPI_ENABLE)
			break;
	}

	fmt = intel_de_read(display, MIPI_DSI_FUNC_PRG(display, port)) & VID_MODE_FORMAT_MASK;
	bpp = mipi_dsi_pixel_format_to_bpp(
			pixel_format_from_register_bits(fmt));

	pipe_config->pipe_bpp = bdw_get_pipe_misc_bpp(crtc);

	/* Enable Frame time stamo based scanline reporting */
	pipe_config->mode_flags |=
		I915_MODE_FLAG_GET_SCANLINE_FROM_TIMESTAMP;

	/* In terms of pixels */
	adjusted_mode->crtc_hdisplay =
				intel_de_read(display,
				              BXT_MIPI_TRANS_HACTIVE(port));
	adjusted_mode->crtc_vdisplay =
				intel_de_read(display,
				              BXT_MIPI_TRANS_VACTIVE(port));
	adjusted_mode->crtc_vtotal =
				intel_de_read(display,
				              BXT_MIPI_TRANS_VTOTAL(port)) + 1;

	hactive = adjusted_mode->crtc_hdisplay;
	hfp = intel_de_read(display, MIPI_HFP_COUNT(display, port));

	/*
	 * Meaningful for video mode non-burst sync pulse mode only,
	 * can be zero for non-burst sync events and burst modes
	 */
	hsync = intel_de_read(display, MIPI_HSYNC_PADDING_COUNT(display, port));
	hbp = intel_de_read(display, MIPI_HBP_COUNT(display, port));

	/* horizontal values are in terms of high speed byte clock */
	hfp = pixels_from_txbyteclkhs(hfp, bpp, lane_count,
						intel_dsi->burst_mode_ratio);
	hsync = pixels_from_txbyteclkhs(hsync, bpp, lane_count,
						intel_dsi->burst_mode_ratio);
	hbp = pixels_from_txbyteclkhs(hbp, bpp, lane_count,
						intel_dsi->burst_mode_ratio);

	if (intel_dsi->dual_link) {
		hfp *= 2;
		hsync *= 2;
		hbp *= 2;
	}

	/* vertical values are in terms of lines */
	vfp = intel_de_read(display, MIPI_VFP_COUNT(display, port));
	vsync = intel_de_read(display, MIPI_VSYNC_PADDING_COUNT(display, port));

	adjusted_mode->crtc_htotal = hactive + hfp + hsync + hbp;
	adjusted_mode->crtc_hsync_start = hfp + adjusted_mode->crtc_hdisplay;
	adjusted_mode->crtc_hsync_end = hsync + adjusted_mode->crtc_hsync_start;
	adjusted_mode->crtc_hblank_start = adjusted_mode->crtc_hdisplay;
	adjusted_mode->crtc_hblank_end = adjusted_mode->crtc_htotal;

	adjusted_mode->crtc_vsync_start = vfp + adjusted_mode->crtc_vdisplay;
	adjusted_mode->crtc_vsync_end = vsync + adjusted_mode->crtc_vsync_start;
	adjusted_mode->crtc_vblank_start = adjusted_mode->crtc_vdisplay;
	adjusted_mode->crtc_vblank_end = adjusted_mode->crtc_vtotal;

	/*
	 * In BXT DSI there is no regs programmed with few horizontal timings
	 * in Pixels but txbyteclkhs.. So retrieval process adds some
	 * ROUND_UP ERRORS in the process of PIXELS<==>txbyteclkhs.
	 * Actually here for the given adjusted_mode, we are calculating the
	 * value programmed to the port and then back to the horizontal timing
	 * param in pixels. This is the expected value, including roundup errors
	 * And if that is same as retrieved value from port, then
	 * (HW state) adjusted_mode's horizontal timings are corrected to
	 * match with SW state to nullify the errors.
	 */
	/* Calculating the value programmed to the Port register */
	hfp_sw = adjusted_mode_sw->crtc_hsync_start -
					adjusted_mode_sw->crtc_hdisplay;
	hsync_sw = adjusted_mode_sw->crtc_hsync_end -
					adjusted_mode_sw->crtc_hsync_start;
	hbp_sw = adjusted_mode_sw->crtc_htotal -
					adjusted_mode_sw->crtc_hsync_end;

	if (intel_dsi->dual_link) {
		hfp_sw /= 2;
		hsync_sw /= 2;
		hbp_sw /= 2;
	}

	hfp_sw = txbyteclkhs(hfp_sw, bpp, lane_count,
						intel_dsi->burst_mode_ratio);
	hsync_sw = txbyteclkhs(hsync_sw, bpp, lane_count,
			    intel_dsi->burst_mode_ratio);
	hbp_sw = txbyteclkhs(hbp_sw, bpp, lane_count,
						intel_dsi->burst_mode_ratio);

	/* Reverse calculating the adjusted mode parameters from port reg vals*/
	hfp_sw = pixels_from_txbyteclkhs(hfp_sw, bpp, lane_count,
						intel_dsi->burst_mode_ratio);
	hsync_sw = pixels_from_txbyteclkhs(hsync_sw, bpp, lane_count,
						intel_dsi->burst_mode_ratio);
	hbp_sw = pixels_from_txbyteclkhs(hbp_sw, bpp, lane_count,
						intel_dsi->burst_mode_ratio);

	if (intel_dsi->dual_link) {
		hfp_sw *= 2;
		hsync_sw *= 2;
		hbp_sw *= 2;
	}

	crtc_htotal_sw = adjusted_mode_sw->crtc_hdisplay + hfp_sw +
							hsync_sw + hbp_sw;
	crtc_hsync_start_sw = hfp_sw + adjusted_mode_sw->crtc_hdisplay;
	crtc_hsync_end_sw = hsync_sw + crtc_hsync_start_sw;
	crtc_hblank_start_sw = adjusted_mode_sw->crtc_hdisplay;
	crtc_hblank_end_sw = crtc_htotal_sw;

	if (adjusted_mode->crtc_htotal == crtc_htotal_sw)
		adjusted_mode->crtc_htotal = adjusted_mode_sw->crtc_htotal;

	if (adjusted_mode->crtc_hsync_start == crtc_hsync_start_sw)
		adjusted_mode->crtc_hsync_start =
					adjusted_mode_sw->crtc_hsync_start;

	if (adjusted_mode->crtc_hsync_end == crtc_hsync_end_sw)
		adjusted_mode->crtc_hsync_end =
					adjusted_mode_sw->crtc_hsync_end;

	if (adjusted_mode->crtc_hblank_start == crtc_hblank_start_sw)
		adjusted_mode->crtc_hblank_start =
					adjusted_mode_sw->crtc_hblank_start;

	if (adjusted_mode->crtc_hblank_end == crtc_hblank_end_sw)
		adjusted_mode->crtc_hblank_end =
					adjusted_mode_sw->crtc_hblank_end;
}

static void intel_dsi_get_config(struct intel_encoder *encoder,
				 struct intel_crtc_state *pipe_config)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	u32 pclk;

	drm_dbg_kms(display->drm, "\n");

	pipe_config->output_types |= BIT(INTEL_OUTPUT_DSI);

	if (display->platform.geminilake || display->platform.broxton) {
		bxt_dsi_get_pipe_config(encoder, pipe_config);
		pclk = bxt_dsi_get_pclk(encoder, pipe_config);
	} else {
		pclk = vlv_dsi_get_pclk(encoder, pipe_config);
	}

	pipe_config->port_clock = pclk;

	/* FIXME definitely not right for burst/cmd mode/pixel overlap */
	pipe_config->hw.adjusted_mode.crtc_clock = pclk;
	if (intel_dsi->dual_link)
		pipe_config->hw.adjusted_mode.crtc_clock *= 2;
}

/* return txclkesc cycles in terms of divider and duration in us */
static u16 txclkesc(u32 divider, unsigned int us)
{
	switch (divider) {
	case ESCAPE_CLOCK_DIVIDER_1:
	default:
		return 20 * us;
	case ESCAPE_CLOCK_DIVIDER_2:
		return 10 * us;
	case ESCAPE_CLOCK_DIVIDER_4:
		return 5 * us;
	}
}

static void set_dsi_timings(struct intel_encoder *encoder,
			    const struct drm_display_mode *adjusted_mode)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;
	unsigned int bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);
	unsigned int lane_count = intel_dsi->lane_count;

	u16 hactive, hfp, hsync, hbp, vfp, vsync, vbp;

	hactive = adjusted_mode->crtc_hdisplay;
	hfp = adjusted_mode->crtc_hsync_start - adjusted_mode->crtc_hdisplay;
	hsync = adjusted_mode->crtc_hsync_end - adjusted_mode->crtc_hsync_start;
	hbp = adjusted_mode->crtc_htotal - adjusted_mode->crtc_hsync_end;

	if (intel_dsi->dual_link) {
		hactive /= 2;
		if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK)
			hactive += intel_dsi->pixel_overlap;
		hfp /= 2;
		hsync /= 2;
		hbp /= 2;
	}

	vfp = adjusted_mode->crtc_vsync_start - adjusted_mode->crtc_vdisplay;
	vsync = adjusted_mode->crtc_vsync_end - adjusted_mode->crtc_vsync_start;
	vbp = adjusted_mode->crtc_vtotal - adjusted_mode->crtc_vsync_end;

	/* horizontal values are in terms of high speed byte clock */
	hactive = txbyteclkhs(hactive, bpp, lane_count,
			      intel_dsi->burst_mode_ratio);
	hfp = txbyteclkhs(hfp, bpp, lane_count, intel_dsi->burst_mode_ratio);
	hsync = txbyteclkhs(hsync, bpp, lane_count,
			    intel_dsi->burst_mode_ratio);
	hbp = txbyteclkhs(hbp, bpp, lane_count, intel_dsi->burst_mode_ratio);

	for_each_dsi_port(port, intel_dsi->ports) {
		if (display->platform.geminilake || display->platform.broxton) {
			/*
			 * Program hdisplay and vdisplay on MIPI transcoder.
			 * This is different from calculated hactive and
			 * vactive, as they are calculated per channel basis,
			 * whereas these values should be based on resolution.
			 */
			intel_de_write(display, BXT_MIPI_TRANS_HACTIVE(port),
				       adjusted_mode->crtc_hdisplay);
			intel_de_write(display, BXT_MIPI_TRANS_VACTIVE(port),
				       adjusted_mode->crtc_vdisplay);
			intel_de_write(display, BXT_MIPI_TRANS_VTOTAL(port),
				       adjusted_mode->crtc_vtotal - 1);
		}

		intel_de_write(display, MIPI_HACTIVE_AREA_COUNT(display, port),
			       hactive);
		intel_de_write(display, MIPI_HFP_COUNT(display, port), hfp);

		/* meaningful for video mode non-burst sync pulse mode only,
		 * can be zero for non-burst sync events and burst modes */
		intel_de_write(display, MIPI_HSYNC_PADDING_COUNT(display, port),
			       hsync);
		intel_de_write(display, MIPI_HBP_COUNT(display, port), hbp);

		/* vertical values are in terms of lines */
		intel_de_write(display, MIPI_VFP_COUNT(display, port), vfp);
		intel_de_write(display, MIPI_VSYNC_PADDING_COUNT(display, port),
			       vsync);
		intel_de_write(display, MIPI_VBP_COUNT(display, port), vbp);
	}
}

static u32 pixel_format_to_reg(enum mipi_dsi_pixel_format fmt)
{
	switch (fmt) {
	case MIPI_DSI_FMT_RGB888:
		return VID_MODE_FORMAT_RGB888;
	case MIPI_DSI_FMT_RGB666:
		return VID_MODE_FORMAT_RGB666;
	case MIPI_DSI_FMT_RGB666_PACKED:
		return VID_MODE_FORMAT_RGB666_PACKED;
	case MIPI_DSI_FMT_RGB565:
		return VID_MODE_FORMAT_RGB565;
	default:
		MISSING_CASE(fmt);
		return VID_MODE_FORMAT_RGB666;
	}
}

static void intel_dsi_prepare(struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	const struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	enum port port;
	unsigned int bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);
	u32 val, tmp;
	u16 mode_hdisplay;

	drm_dbg_kms(display->drm, "pipe %c\n", pipe_name(crtc->pipe));

	mode_hdisplay = adjusted_mode->crtc_hdisplay;

	if (intel_dsi->dual_link) {
		mode_hdisplay /= 2;
		if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK)
			mode_hdisplay += intel_dsi->pixel_overlap;
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		if (display->platform.valleyview || display->platform.cherryview) {
			/*
			 * escape clock divider, 20MHz, shared for A and C.
			 * device ready must be off when doing this! txclkesc?
			 */
			tmp = intel_de_read(display, MIPI_CTRL(display, PORT_A));
			tmp &= ~ESCAPE_CLOCK_DIVIDER_MASK;
			intel_de_write(display, MIPI_CTRL(display, PORT_A),
				       tmp | ESCAPE_CLOCK_DIVIDER_1);

			/* read request priority is per pipe */
			tmp = intel_de_read(display, MIPI_CTRL(display, port));
			tmp &= ~READ_REQUEST_PRIORITY_MASK;
			intel_de_write(display, MIPI_CTRL(display, port),
				       tmp | READ_REQUEST_PRIORITY_HIGH);
		} else if (display->platform.geminilake || display->platform.broxton) {
			enum pipe pipe = crtc->pipe;

			intel_de_rmw(display, MIPI_CTRL(display, port),
				     BXT_PIPE_SELECT_MASK, BXT_PIPE_SELECT(pipe));
		}

		/* XXX: why here, why like this? handling in irq handler?! */
		intel_de_write(display, MIPI_INTR_STAT(display, port), 0xffffffff);
		intel_de_write(display, MIPI_INTR_EN(display, port), 0xffffffff);

		intel_de_write(display, MIPI_DPHY_PARAM(display, port),
			       intel_dsi->dphy_reg);

		intel_de_write(display, MIPI_DPI_RESOLUTION(display, port),
			       adjusted_mode->crtc_vdisplay << VERTICAL_ADDRESS_SHIFT | mode_hdisplay << HORIZONTAL_ADDRESS_SHIFT);
	}

	set_dsi_timings(encoder, adjusted_mode);

	val = intel_dsi->lane_count << DATA_LANES_PRG_REG_SHIFT;
	if (is_cmd_mode(intel_dsi)) {
		val |= intel_dsi->channel << CMD_MODE_CHANNEL_NUMBER_SHIFT;
		val |= CMD_MODE_DATA_WIDTH_8_BIT; /* XXX */
	} else {
		val |= intel_dsi->channel << VID_MODE_CHANNEL_NUMBER_SHIFT;
		val |= pixel_format_to_reg(intel_dsi->pixel_format);
	}

	tmp = 0;
	if (intel_dsi->eotp_pkt == 0)
		tmp |= EOT_DISABLE;
	if (intel_dsi->clock_stop)
		tmp |= CLOCKSTOP;

	if (display->platform.geminilake || display->platform.broxton) {
		tmp |= BXT_DPHY_DEFEATURE_EN;
		if (!is_cmd_mode(intel_dsi))
			tmp |= BXT_DEFEATURE_DPI_FIFO_CTR;
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		intel_de_write(display, MIPI_DSI_FUNC_PRG(display, port), val);

		/* timeouts for recovery. one frame IIUC. if counter expires,
		 * EOT and stop state. */

		/*
		 * In burst mode, value greater than one DPI line Time in byte
		 * clock (txbyteclkhs) To timeout this timer 1+ of the above
		 * said value is recommended.
		 *
		 * In non-burst mode, Value greater than one DPI frame time in
		 * byte clock(txbyteclkhs) To timeout this timer 1+ of the above
		 * said value is recommended.
		 *
		 * In DBI only mode, value greater than one DBI frame time in
		 * byte clock(txbyteclkhs) To timeout this timer 1+ of the above
		 * said value is recommended.
		 */

		if (is_vid_mode(intel_dsi) &&
			intel_dsi->video_mode == BURST_MODE) {
			intel_de_write(display, MIPI_HS_TX_TIMEOUT(display, port),
				       txbyteclkhs(adjusted_mode->crtc_htotal, bpp, intel_dsi->lane_count, intel_dsi->burst_mode_ratio) + 1);
		} else {
			intel_de_write(display, MIPI_HS_TX_TIMEOUT(display, port),
				       txbyteclkhs(adjusted_mode->crtc_vtotal * adjusted_mode->crtc_htotal, bpp, intel_dsi->lane_count, intel_dsi->burst_mode_ratio) + 1);
		}
		intel_de_write(display, MIPI_LP_RX_TIMEOUT(display, port),
			       intel_dsi->lp_rx_timeout);
		intel_de_write(display, MIPI_TURN_AROUND_TIMEOUT(display, port),
			       intel_dsi->turn_arnd_val);
		intel_de_write(display, MIPI_DEVICE_RESET_TIMER(display, port),
			       intel_dsi->rst_timer_val);

		/* dphy stuff */

		/* in terms of low power clock */
		intel_de_write(display, MIPI_INIT_COUNT(display, port),
			       txclkesc(intel_dsi->escape_clk_div, 100));

		if ((display->platform.geminilake || display->platform.broxton) &&
		    !intel_dsi->dual_link) {
			/*
			 * BXT spec says write MIPI_INIT_COUNT for
			 * both the ports, even if only one is
			 * getting used. So write the other port
			 * if not in dual link mode.
			 */
			intel_de_write(display,
				       MIPI_INIT_COUNT(display, port == PORT_A ? PORT_C : PORT_A),
				       intel_dsi->init_count);
		}

		/* recovery disables */
		intel_de_write(display, MIPI_EOT_DISABLE(display, port), tmp);

		/* in terms of low power clock */
		intel_de_write(display, MIPI_INIT_COUNT(display, port),
			       intel_dsi->init_count);

		/* in terms of txbyteclkhs. actual high to low switch +
		 * MIPI_STOP_STATE_STALL * MIPI_LP_BYTECLK.
		 *
		 * XXX: write MIPI_STOP_STATE_STALL?
		 */
		intel_de_write(display, MIPI_HIGH_LOW_SWITCH_COUNT(display, port),
			       intel_dsi->hs_to_lp_count);

		/* XXX: low power clock equivalence in terms of byte clock.
		 * the number of byte clocks occupied in one low power clock.
		 * based on txbyteclkhs and txclkesc.
		 * txclkesc time / txbyteclk time * (105 + MIPI_STOP_STATE_STALL
		 * ) / 105.???
		 */
		intel_de_write(display, MIPI_LP_BYTECLK(display, port),
			       intel_dsi->lp_byte_clk);

		if (display->platform.geminilake) {
			intel_de_write(display, MIPI_TLPX_TIME_COUNT(display, port),
				       intel_dsi->lp_byte_clk);
			/* Shadow of DPHY reg */
			intel_de_write(display, MIPI_CLK_LANE_TIMING(display, port),
				       intel_dsi->dphy_reg);
		}

		/* the bw essential for transmitting 16 long packets containing
		 * 252 bytes meant for dcs write memory command is programmed in
		 * this register in terms of byte clocks. based on dsi transfer
		 * rate and the number of lanes configured the time taken to
		 * transmit 16 long packets in a dsi stream varies. */
		intel_de_write(display, MIPI_DBI_BW_CTRL(display, port),
			       intel_dsi->bw_timer);

		intel_de_write(display, MIPI_CLK_LANE_SWITCH_TIME_CNT(display, port),
			       intel_dsi->clk_lp_to_hs_count << LP_HS_SSW_CNT_SHIFT | intel_dsi->clk_hs_to_lp_count << HS_LP_PWR_SW_CNT_SHIFT);

		if (is_vid_mode(intel_dsi)) {
			u32 fmt = intel_dsi->video_frmt_cfg_bits | IP_TG_CONFIG;

			/*
			 * Some panels might have resolution which is not a
			 * multiple of 64 like 1366 x 768. Enable RANDOM
			 * resolution support for such panels by default.
			 */
			fmt |= RANDOM_DPI_DISPLAY_RESOLUTION;

			switch (intel_dsi->video_mode) {
			default:
				MISSING_CASE(intel_dsi->video_mode);
				fallthrough;
			case NON_BURST_SYNC_EVENTS:
				fmt |= VIDEO_MODE_NON_BURST_WITH_SYNC_EVENTS;
				break;
			case NON_BURST_SYNC_PULSE:
				fmt |= VIDEO_MODE_NON_BURST_WITH_SYNC_PULSE;
				break;
			case BURST_MODE:
				fmt |= VIDEO_MODE_BURST;
				break;
			}

			intel_de_write(display, MIPI_VIDEO_MODE_FORMAT(display, port), fmt);
		}
	}
}

static void intel_dsi_unprepare(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	if (display->platform.geminilake)
		return;

	for_each_dsi_port(port, intel_dsi->ports) {
		/* Panel commands can be sent when clock is in LP11 */
		intel_de_write(display, MIPI_DEVICE_READY(display, port), 0x0);

		if (display->platform.geminilake || display->platform.broxton)
			bxt_dsi_reset_clocks(encoder, port);
		else
			vlv_dsi_reset_clocks(encoder, port);
		intel_de_write(display, MIPI_EOT_DISABLE(display, port), CLOCKSTOP);

		intel_de_rmw(display, MIPI_DSI_FUNC_PRG(display, port), VID_MODE_FORMAT_MASK, 0);

		intel_de_write(display, MIPI_DEVICE_READY(display, port), 0x1);
	}
}

static const struct drm_encoder_funcs intel_dsi_funcs = {
	.destroy = intel_encoder_destroy,
};

static enum drm_mode_status vlv_dsi_mode_valid(struct drm_connector *connector,
					       const struct drm_display_mode *mode)
{
	struct intel_display *display = to_intel_display(connector->dev);

	if (display->platform.valleyview || display->platform.cherryview) {
		enum drm_mode_status status;

		status = intel_cpu_transcoder_mode_valid(display, mode);
		if (status != MODE_OK)
			return status;
	}

	return intel_dsi_mode_valid(connector, mode);
}

static const struct drm_connector_helper_funcs intel_dsi_connector_helper_funcs = {
	.get_modes = intel_dsi_get_modes,
	.mode_valid = vlv_dsi_mode_valid,
	.atomic_check = intel_digital_connector_atomic_check,
};

static const struct drm_connector_funcs intel_dsi_connector_funcs = {
	.detect = intel_panel_detect,
	.late_register = intel_connector_register,
	.early_unregister = intel_connector_unregister,
	.destroy = intel_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_get_property = intel_digital_connector_atomic_get_property,
	.atomic_set_property = intel_digital_connector_atomic_set_property,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = intel_digital_connector_duplicate_state,
};

static void vlv_dsi_add_properties(struct intel_connector *connector)
{
	const struct drm_display_mode *fixed_mode =
		intel_panel_preferred_fixed_mode(connector);

	intel_attach_scaling_mode_property(&connector->base);

	drm_connector_set_panel_orientation_with_quirk(&connector->base,
						       intel_dsi_get_panel_orientation(connector),
						       fixed_mode->hdisplay,
						       fixed_mode->vdisplay);
}

#define NS_KHZ_RATIO		1000000

#define PREPARE_CNT_MAX		0x3F
#define EXIT_ZERO_CNT_MAX	0x3F
#define CLK_ZERO_CNT_MAX	0xFF
#define TRAIL_CNT_MAX		0x1F

static void vlv_dphy_param_init(struct intel_dsi *intel_dsi)
{
	struct intel_display *display = to_intel_display(&intel_dsi->base);
	struct intel_connector *connector = intel_dsi->attached_connector;
	struct mipi_config *mipi_config = connector->panel.vbt.dsi.config;
	u32 tlpx_ns, extra_byte_count, tlpx_ui;
	u32 ui_num, ui_den;
	u32 prepare_cnt, exit_zero_cnt, clk_zero_cnt, trail_cnt;
	u32 ths_prepare_ns, tclk_trail_ns;
	u32 tclk_prepare_clkzero, ths_prepare_hszero;
	u32 lp_to_hs_switch, hs_to_lp_switch;
	u32 mul;

	tlpx_ns = intel_dsi_tlpx_ns(intel_dsi);

	switch (intel_dsi->lane_count) {
	case 1:
	case 2:
		extra_byte_count = 2;
		break;
	case 3:
		extra_byte_count = 4;
		break;
	case 4:
	default:
		extra_byte_count = 3;
		break;
	}

	/* in Kbps */
	ui_num = NS_KHZ_RATIO;
	ui_den = intel_dsi_bitrate(intel_dsi);

	tclk_prepare_clkzero = mipi_config->tclk_prepare_clkzero;
	ths_prepare_hszero = mipi_config->ths_prepare_hszero;

	/*
	 * B060
	 * LP byte clock = TLPX/ (8UI)
	 */
	intel_dsi->lp_byte_clk = DIV_ROUND_UP(tlpx_ns * ui_den, 8 * ui_num);

	/* DDR clock period = 2 * UI
	 * UI(sec) = 1/(bitrate * 10^3) (bitrate is in KHZ)
	 * UI(nsec) = 10^6 / bitrate
	 * DDR clock period (nsec) = 2 * UI = (2 * 10^6)/ bitrate
	 * DDR clock count  = ns_value / DDR clock period
	 *
	 * For GEMINILAKE dphy_param_reg will be programmed in terms of
	 * HS byte clock count for other platform in HS ddr clock count
	 */
	mul = display->platform.geminilake ? 8 : 2;
	ths_prepare_ns = max(mipi_config->ths_prepare,
			     mipi_config->tclk_prepare);

	/* prepare count */
	prepare_cnt = DIV_ROUND_UP(ths_prepare_ns * ui_den, ui_num * mul);

	if (prepare_cnt > PREPARE_CNT_MAX) {
		drm_dbg_kms(display->drm, "prepare count too high %u\n",
			    prepare_cnt);
		prepare_cnt = PREPARE_CNT_MAX;
	}

	/* exit zero count */
	exit_zero_cnt = DIV_ROUND_UP(
				(ths_prepare_hszero - ths_prepare_ns) * ui_den,
				ui_num * mul
				);

	/*
	 * Exit zero is unified val ths_zero and ths_exit
	 * minimum value for ths_exit = 110ns
	 * min (exit_zero_cnt * 2) = 110/UI
	 * exit_zero_cnt = 55/UI
	 */
	if (exit_zero_cnt < (55 * ui_den / ui_num) && (55 * ui_den) % ui_num)
		exit_zero_cnt += 1;

	if (exit_zero_cnt > EXIT_ZERO_CNT_MAX) {
		drm_dbg_kms(display->drm, "exit zero count too high %u\n",
			    exit_zero_cnt);
		exit_zero_cnt = EXIT_ZERO_CNT_MAX;
	}

	/* clk zero count */
	clk_zero_cnt = DIV_ROUND_UP(
				(tclk_prepare_clkzero -	ths_prepare_ns)
				* ui_den, ui_num * mul);

	if (clk_zero_cnt > CLK_ZERO_CNT_MAX) {
		drm_dbg_kms(display->drm, "clock zero count too high %u\n",
			    clk_zero_cnt);
		clk_zero_cnt = CLK_ZERO_CNT_MAX;
	}

	/* trail count */
	tclk_trail_ns = max(mipi_config->tclk_trail, mipi_config->ths_trail);
	trail_cnt = DIV_ROUND_UP(tclk_trail_ns * ui_den, ui_num * mul);

	if (trail_cnt > TRAIL_CNT_MAX) {
		drm_dbg_kms(display->drm, "trail count too high %u\n",
			    trail_cnt);
		trail_cnt = TRAIL_CNT_MAX;
	}

	/* B080 */
	intel_dsi->dphy_reg = exit_zero_cnt << 24 | trail_cnt << 16 |
						clk_zero_cnt << 8 | prepare_cnt;

	/*
	 * LP to HS switch count = 4TLPX + PREP_COUNT * mul + EXIT_ZERO_COUNT *
	 *					mul + 10UI + Extra Byte Count
	 *
	 * HS to LP switch count = THS-TRAIL + 2TLPX + Extra Byte Count
	 * Extra Byte Count is calculated according to number of lanes.
	 * High Low Switch Count is the Max of LP to HS and
	 * HS to LP switch count
	 *
	 */
	tlpx_ui = DIV_ROUND_UP(tlpx_ns * ui_den, ui_num);

	/* B044 */
	/* FIXME:
	 * The comment above does not match with the code */
	lp_to_hs_switch = DIV_ROUND_UP(4 * tlpx_ui + prepare_cnt * mul +
						exit_zero_cnt * mul + 10, 8);

	hs_to_lp_switch = DIV_ROUND_UP(mipi_config->ths_trail + 2 * tlpx_ui, 8);

	intel_dsi->hs_to_lp_count = max(lp_to_hs_switch, hs_to_lp_switch);
	intel_dsi->hs_to_lp_count += extra_byte_count;

	/* B088 */
	/* LP -> HS for clock lanes
	 * LP clk sync + LP11 + LP01 + tclk_prepare + tclk_zero +
	 *						extra byte count
	 * 2TPLX + 1TLPX + 1 TPLX(in ns) + prepare_cnt * 2 + clk_zero_cnt *
	 *					2(in UI) + extra byte count
	 * In byteclks = (4TLPX + prepare_cnt * 2 + clk_zero_cnt *2 (in UI)) /
	 *					8 + extra byte count
	 */
	intel_dsi->clk_lp_to_hs_count =
		DIV_ROUND_UP(
			4 * tlpx_ui + prepare_cnt * 2 +
			clk_zero_cnt * 2,
			8);

	intel_dsi->clk_lp_to_hs_count += extra_byte_count;

	/* HS->LP for Clock Lanes
	 * Low Power clock synchronisations + 1Tx byteclk + tclk_trail +
	 *						Extra byte count
	 * 2TLPX + 8UI + (trail_count*2)(in UI) + Extra byte count
	 * In byteclks = (2*TLpx(in UI) + trail_count*2 +8)(in UI)/8 +
	 *						Extra byte count
	 */
	intel_dsi->clk_hs_to_lp_count =
		DIV_ROUND_UP(2 * tlpx_ui + trail_cnt * 2 + 8,
			8);
	intel_dsi->clk_hs_to_lp_count += extra_byte_count;

	intel_dsi_log_params(intel_dsi);
}

int vlv_dsi_min_cdclk(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI))
		return 0;

	/*
	 * On Valleyview some DSI panels lose (v|h)sync when the clock is lower
	 * than 320000KHz.
	 */
	if (display->platform.valleyview)
		return 320000;

	/*
	 * On Geminilake once the CDCLK gets as low as 79200
	 * picture gets unstable, despite that values are
	 * correct for DSI PLL and DE PLL.
	 */
	if (display->platform.geminilake)
		return 158400;

	return 0;
}

typedef void (*vlv_dsi_dmi_quirk_func)(struct intel_dsi *intel_dsi);

/*
 * Vtotal is wrong on the Asus TF103C leading to the last line of the display
 * being shown as the first line. The factory installed Android has a hardcoded
 * modeline, causing it to not suffer from this BIOS bug.
 *
 * Original mode: "1280x800": 60 67700 1280 1312 1328 1376 800 808 812 820 0x8 0xa
 * Fixed    mode: "1280x800": 60 67700 1280 1312 1328 1376 800 808 812 816 0x8 0xa
 *
 * https://gitlab.freedesktop.org/drm/intel/-/issues/9381
 */
static void vlv_dsi_asus_tf103c_mode_fixup(struct intel_dsi *intel_dsi)
{
	/* Cast away the const as we want to fixup the mode */
	struct drm_display_mode *fixed_mode = (struct drm_display_mode *)
		intel_panel_preferred_fixed_mode(intel_dsi->attached_connector);

	if (fixed_mode->vtotal == 820)
		fixed_mode->vtotal -= 4;
}

/*
 * On the Lenovo Yoga Tablet 2 830 / 1050 there are 2 problems:
 * 1. The I2C MIPI sequence elements reference bus 3. ACPI has I2C1 - I2C7
 *    which under Linux become bus 0 - 6. And the MIPI sequence reference
 *    to bus 3 is indented for I2C3 which is bus 2 under Linux.
 *
 *    Note mipi_exec_i2c() cannot just subtract 1 from the bus
 *    given in the I2C MIPI sequence element. Since on other
 *    devices the I2C bus-numbers used in the MIPI sequences do
 *    actually start at 0.
 *
 * 2. width_/height_mm contain a bogus 192mm x 120mm size. This is
 *    especially a problem on the 8" 830 version which uses a 10:16
 *    portrait screen where as the bogus size is 16:10.
 *
 * https://gitlab.freedesktop.org/drm/intel/-/issues/9379
 */
static void vlv_dsi_lenovo_yoga_tab2_size_fixup(struct intel_dsi *intel_dsi)
{
	const struct drm_display_mode *fixed_mode =
		intel_panel_preferred_fixed_mode(intel_dsi->attached_connector);
	struct drm_display_info *info = &intel_dsi->attached_connector->base.display_info;

	intel_dsi->i2c_bus_num = 2;

	/*
	 * The 10" 1050 uses a 1920x1200 landscape screen, where as the 8" 830
	 * uses a 1200x1920 portrait screen.
	 */
	if (fixed_mode->hdisplay == 1920) {
		info->width_mm = 216;
		info->height_mm = 135;
	} else {
		info->width_mm = 107;
		info->height_mm = 171;
	}
}

/*
 * On the Lenovo Yoga Tab 3 Pro YT3-X90F there are 2 problems:
 * 1. i2c_acpi_find_adapter() picks the wrong adapter causing mipi_exec_i2c()
 *    to not work. Fix this by setting i2c_bus_num.
 * 2. There is no backlight off MIPI sequence, causing the backlight to stay on.
 *    Add a backlight off sequence mirroring the existing backlight on sequence.
 *
 * https://gitlab.freedesktop.org/drm/intel/-/issues/9380
 */
static void vlv_dsi_lenovo_yoga_tab3_backlight_fixup(struct intel_dsi *intel_dsi)
{
	static const u8 backlight_off_sequence[16] = {
		/* Header Seq-id 7, length after header 11 bytes */
		0x07, 0x0b, 0x00, 0x00, 0x00,
		/* MIPI_SEQ_ELEM_I2C bus 0 addr 0x2c reg 0x00 data-len 1 data 0x00 */
		0x04, 0x08, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x01, 0x00,
		/* MIPI_SEQ_ELEM_END */
		0x00
	};
	struct intel_connector *connector = intel_dsi->attached_connector;

	intel_dsi->i2c_bus_num = 0;
	connector->panel.vbt.dsi.sequence[MIPI_SEQ_BACKLIGHT_OFF] = backlight_off_sequence;
}

static const struct dmi_system_id vlv_dsi_dmi_quirk_table[] = {
	{
		/* Asus Transformer Pad TF103C */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "TF103C"),
		},
		.driver_data = (void *)vlv_dsi_asus_tf103c_mode_fixup,
	},
	{
		/*
		 * Lenovo Yoga Tablet 2 830F/L or 1050F/L (The 8" and 10"
		 * Lenovo Yoga Tablet 2 use the same mainboard)
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corp."),
			DMI_MATCH(DMI_PRODUCT_NAME, "VALLEYVIEW C0 PLATFORM"),
			DMI_MATCH(DMI_BOARD_NAME, "BYT-T FFD8"),
			/* Partial match on beginning of BIOS version */
			DMI_MATCH(DMI_BIOS_VERSION, "BLADE_21"),
		},
		.driver_data = (void *)vlv_dsi_lenovo_yoga_tab2_size_fixup,
	},
	{
		/* Lenovo Yoga Tab 3 Pro YT3-X90F */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Blade3-10A-001"),
		},
		.driver_data = (void *)vlv_dsi_lenovo_yoga_tab3_backlight_fixup,
	},
	{ }
};

void vlv_dsi_init(struct intel_display *display)
{
	struct intel_dsi *intel_dsi;
	struct intel_encoder *encoder;
	struct intel_connector *connector;
	struct drm_display_mode *current_mode;
	const struct dmi_system_id *dmi_id;
	enum port port;
	enum pipe pipe;

	drm_dbg_kms(display->drm, "\n");

	/* There is no detection method for MIPI so rely on VBT */
	if (!intel_bios_is_dsi_present(display, &port))
		return;

	if (display->platform.geminilake || display->platform.broxton)
		display->dsi.mmio_base = BXT_MIPI_BASE;
	else
		display->dsi.mmio_base = VLV_MIPI_BASE;

	intel_dsi = kzalloc(sizeof(*intel_dsi), GFP_KERNEL);
	if (!intel_dsi)
		return;

	connector = intel_connector_alloc();
	if (!connector) {
		kfree(intel_dsi);
		return;
	}

	encoder = &intel_dsi->base;
	intel_dsi->attached_connector = connector;

	drm_encoder_init(display->drm, &encoder->base, &intel_dsi_funcs,
			 DRM_MODE_ENCODER_DSI, "DSI %c", port_name(port));

	encoder->compute_config = intel_dsi_compute_config;
	encoder->pre_enable = intel_dsi_pre_enable;
	if (display->platform.geminilake || display->platform.broxton)
		encoder->enable = bxt_dsi_enable;
	encoder->disable = intel_dsi_disable;
	encoder->post_disable = intel_dsi_post_disable;
	encoder->get_hw_state = intel_dsi_get_hw_state;
	encoder->get_config = intel_dsi_get_config;
	encoder->update_pipe = intel_backlight_update;
	encoder->shutdown = intel_dsi_shutdown;

	connector->get_hw_state = intel_connector_get_hw_state;

	encoder->port = port;
	encoder->type = INTEL_OUTPUT_DSI;
	encoder->power_domain = POWER_DOMAIN_PORT_DSI;
	encoder->cloneable = 0;

	/*
	 * On BYT/CHV, pipe A maps to MIPI DSI port A, pipe B maps to MIPI DSI
	 * port C. BXT isn't limited like this.
	 */
	if (display->platform.geminilake || display->platform.broxton)
		encoder->pipe_mask = ~0;
	else if (port == PORT_A)
		encoder->pipe_mask = BIT(PIPE_A);
	else
		encoder->pipe_mask = BIT(PIPE_B);

	intel_dsi->panel_power_off_time = ktime_get_boottime();

	intel_bios_init_panel_late(display, &connector->panel, NULL, NULL);

	if (connector->panel.vbt.dsi.config->dual_link)
		intel_dsi->ports = BIT(PORT_A) | BIT(PORT_C);
	else
		intel_dsi->ports = BIT(port);

	if (drm_WARN_ON(display->drm, connector->panel.vbt.dsi.bl_ports & ~intel_dsi->ports))
		connector->panel.vbt.dsi.bl_ports &= intel_dsi->ports;

	if (drm_WARN_ON(display->drm, connector->panel.vbt.dsi.cabc_ports & ~intel_dsi->ports))
		connector->panel.vbt.dsi.cabc_ports &= intel_dsi->ports;

	/* Create a DSI host (and a device) for each port. */
	for_each_dsi_port(port, intel_dsi->ports) {
		struct intel_dsi_host *host;

		host = intel_dsi_host_init(intel_dsi, &intel_dsi_host_ops,
					   port);
		if (!host)
			goto err;

		intel_dsi->dsi_hosts[port] = host;
	}

	if (!intel_dsi_vbt_init(intel_dsi, MIPI_DSI_GENERIC_PANEL_ID)) {
		drm_dbg_kms(display->drm, "no device found\n");
		goto err;
	}

	/* Use clock read-back from current hw-state for fastboot */
	current_mode = intel_encoder_current_mode(encoder);
	if (current_mode) {
		drm_dbg_kms(display->drm, "Calculated pclk %d GOP %d\n",
			    intel_dsi->pclk, current_mode->clock);
		if (intel_fuzzy_clock_check(intel_dsi->pclk,
					    current_mode->clock)) {
			drm_dbg_kms(display->drm, "Using GOP pclk\n");
			intel_dsi->pclk = current_mode->clock;
		}

		kfree(current_mode);
	}

	vlv_dphy_param_init(intel_dsi);

	intel_dsi_vbt_gpio_init(intel_dsi,
				intel_dsi_get_hw_state(encoder, &pipe));

	drm_connector_init(display->drm, &connector->base, &intel_dsi_connector_funcs,
			   DRM_MODE_CONNECTOR_DSI);

	drm_connector_helper_add(&connector->base, &intel_dsi_connector_helper_funcs);

	connector->base.display_info.subpixel_order = SubPixelHorizontalRGB; /*XXX*/

	intel_connector_attach_encoder(connector, encoder);

	mutex_lock(&display->drm->mode_config.mutex);
	intel_panel_add_vbt_lfp_fixed_mode(connector);
	mutex_unlock(&display->drm->mode_config.mutex);

	if (!intel_panel_preferred_fixed_mode(connector)) {
		drm_dbg_kms(display->drm, "no fixed mode\n");
		goto err_cleanup_connector;
	}

	dmi_id = dmi_first_match(vlv_dsi_dmi_quirk_table);
	if (dmi_id) {
		vlv_dsi_dmi_quirk_func quirk_func =
			(vlv_dsi_dmi_quirk_func)dmi_id->driver_data;

		quirk_func(intel_dsi);
	}

	intel_panel_init(connector, NULL);

	intel_backlight_setup(connector, INVALID_PIPE);

	vlv_dsi_add_properties(connector);

	return;

err_cleanup_connector:
	drm_connector_cleanup(&connector->base);
err:
	drm_encoder_cleanup(&encoder->base);
	kfree(intel_dsi);
	kfree(connector);
}
