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

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/i915_drm.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include "intel_dsi.h"

static const struct {
	u16 panel_id;
	struct drm_panel * (*init)(struct intel_dsi *intel_dsi, u16 panel_id);
} intel_dsi_drivers[] = {
	{
		.panel_id = MIPI_DSI_GENERIC_PANEL_ID,
		.init = vbt_panel_init,
	},
};

/* return pixels in terms of txbyteclkhs */
static u16 txbyteclkhs(u16 pixels, int bpp, int lane_count,
		       u16 burst_mode_ratio)
{
	return DIV_ROUND_UP(DIV_ROUND_UP(pixels * bpp * burst_mode_ratio,
					 8 * 100), lane_count);
}

/* return pixels equvalent to txbyteclkhs */
static u16 pixels_from_txbyteclkhs(u16 clk_hs, int bpp, int lane_count,
			u16 burst_mode_ratio)
{
	return DIV_ROUND_UP((clk_hs * lane_count * 8 * 100),
						(bpp * burst_mode_ratio));
}

enum mipi_dsi_pixel_format pixel_format_from_register_bits(u32 fmt)
{
	/* It just so happens the VBT matches register contents. */
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

static void wait_for_dsi_fifo_empty(struct intel_dsi *intel_dsi, enum port port)
{
	struct drm_encoder *encoder = &intel_dsi->base.base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 mask;

	mask = LP_CTRL_FIFO_EMPTY | HS_CTRL_FIFO_EMPTY |
		LP_DATA_FIFO_EMPTY | HS_DATA_FIFO_EMPTY;

	if (intel_wait_for_register(dev_priv,
				    MIPI_GEN_FIFO_STAT(port), mask, mask,
				    100))
		DRM_ERROR("DPI FIFOs are not empty\n");
}

static void write_data(struct drm_i915_private *dev_priv,
		       i915_reg_t reg,
		       const u8 *data, u32 len)
{
	u32 i, j;

	for (i = 0; i < len; i += 4) {
		u32 val = 0;

		for (j = 0; j < min_t(u32, len - i, 4); j++)
			val |= *data++ << 8 * j;

		I915_WRITE(reg, val);
	}
}

static void read_data(struct drm_i915_private *dev_priv,
		      i915_reg_t reg,
		      u8 *data, u32 len)
{
	u32 i, j;

	for (i = 0; i < len; i += 4) {
		u32 val = I915_READ(reg);

		for (j = 0; j < min_t(u32, len - i, 4); j++)
			*data++ = val >> 8 * j;
	}
}

static ssize_t intel_dsi_host_transfer(struct mipi_dsi_host *host,
				       const struct mipi_dsi_msg *msg)
{
	struct intel_dsi_host *intel_dsi_host = to_intel_dsi_host(host);
	struct drm_device *dev = intel_dsi_host->intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum port port = intel_dsi_host->port;
	struct mipi_dsi_packet packet;
	ssize_t ret;
	const u8 *header, *data;
	i915_reg_t data_reg, ctrl_reg;
	u32 data_mask, ctrl_mask;

	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret < 0)
		return ret;

	header = packet.header;
	data = packet.payload;

	if (msg->flags & MIPI_DSI_MSG_USE_LPM) {
		data_reg = MIPI_LP_GEN_DATA(port);
		data_mask = LP_DATA_FIFO_FULL;
		ctrl_reg = MIPI_LP_GEN_CTRL(port);
		ctrl_mask = LP_CTRL_FIFO_FULL;
	} else {
		data_reg = MIPI_HS_GEN_DATA(port);
		data_mask = HS_DATA_FIFO_FULL;
		ctrl_reg = MIPI_HS_GEN_CTRL(port);
		ctrl_mask = HS_CTRL_FIFO_FULL;
	}

	/* note: this is never true for reads */
	if (packet.payload_length) {
		if (intel_wait_for_register(dev_priv,
					    MIPI_GEN_FIFO_STAT(port),
					    data_mask, 0,
					    50))
			DRM_ERROR("Timeout waiting for HS/LP DATA FIFO !full\n");

		write_data(dev_priv, data_reg, packet.payload,
			   packet.payload_length);
	}

	if (msg->rx_len) {
		I915_WRITE(MIPI_INTR_STAT(port), GEN_READ_DATA_AVAIL);
	}

	if (intel_wait_for_register(dev_priv,
				    MIPI_GEN_FIFO_STAT(port),
				    ctrl_mask, 0,
				    50)) {
		DRM_ERROR("Timeout waiting for HS/LP CTRL FIFO !full\n");
	}

	I915_WRITE(ctrl_reg, header[2] << 16 | header[1] << 8 | header[0]);

	/* ->rx_len is set only for reads */
	if (msg->rx_len) {
		data_mask = GEN_READ_DATA_AVAIL;
		if (intel_wait_for_register(dev_priv,
					    MIPI_INTR_STAT(port),
					    data_mask, data_mask,
					    50))
			DRM_ERROR("Timeout waiting for read data.\n");

		read_data(dev_priv, data_reg, msg->rx_buf, msg->rx_len);
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

static struct intel_dsi_host *intel_dsi_host_init(struct intel_dsi *intel_dsi,
						  enum port port)
{
	struct intel_dsi_host *host;
	struct mipi_dsi_device *device;

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		return NULL;

	host->base.ops = &intel_dsi_host_ops;
	host->intel_dsi = intel_dsi;
	host->port = port;

	/*
	 * We should call mipi_dsi_host_register(&host->base) here, but we don't
	 * have a host->dev, and we don't have OF stuff either. So just use the
	 * dsi framework as a library and hope for the best. Create the dsi
	 * devices by ourselves here too. Need to be careful though, because we
	 * don't initialize any of the driver model devices here.
	 */
	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device) {
		kfree(host);
		return NULL;
	}

	device->host = &host->base;
	host->device = device;

	return host;
}

/*
 * send a video mode command
 *
 * XXX: commands with data in MIPI_DPI_DATA?
 */
static int dpi_send_cmd(struct intel_dsi *intel_dsi, u32 cmd, bool hs,
			enum port port)
{
	struct drm_encoder *encoder = &intel_dsi->base.base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 mask;

	/* XXX: pipe, hs */
	if (hs)
		cmd &= ~DPI_LP_MODE;
	else
		cmd |= DPI_LP_MODE;

	/* clear bit */
	I915_WRITE(MIPI_INTR_STAT(port), SPL_PKT_SENT_INTERRUPT);

	/* XXX: old code skips write if control unchanged */
	if (cmd == I915_READ(MIPI_DPI_CONTROL(port)))
		DRM_ERROR("Same special packet %02x twice in a row.\n", cmd);

	I915_WRITE(MIPI_DPI_CONTROL(port), cmd);

	mask = SPL_PKT_SENT_INTERRUPT;
	if (intel_wait_for_register(dev_priv,
				    MIPI_INTR_STAT(port), mask, mask,
				    100))
		DRM_ERROR("Video mode command 0x%08x send failed.\n", cmd);

	return 0;
}

static void band_gap_reset(struct drm_i915_private *dev_priv)
{
	mutex_lock(&dev_priv->sb_lock);

	vlv_flisdsi_write(dev_priv, 0x08, 0x0001);
	vlv_flisdsi_write(dev_priv, 0x0F, 0x0005);
	vlv_flisdsi_write(dev_priv, 0x0F, 0x0025);
	udelay(150);
	vlv_flisdsi_write(dev_priv, 0x0F, 0x0000);
	vlv_flisdsi_write(dev_priv, 0x08, 0x0000);

	mutex_unlock(&dev_priv->sb_lock);
}

static inline bool is_vid_mode(struct intel_dsi *intel_dsi)
{
	return intel_dsi->operation_mode == INTEL_DSI_VIDEO_MODE;
}

static inline bool is_cmd_mode(struct intel_dsi *intel_dsi)
{
	return intel_dsi->operation_mode == INTEL_DSI_COMMAND_MODE;
}

static bool intel_dsi_compute_config(struct intel_encoder *encoder,
				     struct intel_crtc_state *pipe_config,
				     struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = container_of(encoder, struct intel_dsi,
						   base);
	struct intel_connector *intel_connector = intel_dsi->attached_connector;
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->base.crtc);
	const struct drm_display_mode *fixed_mode = intel_connector->panel.fixed_mode;
	struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	int ret;

	DRM_DEBUG_KMS("\n");

	if (fixed_mode) {
		intel_fixed_panel_mode(fixed_mode, adjusted_mode);

		if (HAS_GMCH_DISPLAY(dev_priv))
			intel_gmch_panel_fitting(crtc, pipe_config,
						 intel_connector->panel.fitting_mode);
		else
			intel_pch_panel_fitting(crtc, pipe_config,
						intel_connector->panel.fitting_mode);
	}

	/* DSI uses short packets for sync events, so clear mode flags for DSI */
	adjusted_mode->flags = 0;

	if (IS_GEN9_LP(dev_priv)) {
		/* Dual link goes to DSI transcoder A. */
		if (intel_dsi->ports == BIT(PORT_C))
			pipe_config->cpu_transcoder = TRANSCODER_DSI_C;
		else
			pipe_config->cpu_transcoder = TRANSCODER_DSI_A;
	}

	ret = intel_compute_dsi_pll(encoder, pipe_config);
	if (ret)
		return false;

	pipe_config->clock_set = true;

	return true;
}

static void bxt_dsi_device_ready(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 val;

	DRM_DEBUG_KMS("\n");

	/* Exit Low power state in 4 steps*/
	for_each_dsi_port(port, intel_dsi->ports) {

		/* 1. Enable MIPI PHY transparent latch */
		val = I915_READ(BXT_MIPI_PORT_CTRL(port));
		I915_WRITE(BXT_MIPI_PORT_CTRL(port), val | LP_OUTPUT_HOLD);
		usleep_range(2000, 2500);

		/* 2. Enter ULPS */
		val = I915_READ(MIPI_DEVICE_READY(port));
		val &= ~ULPS_STATE_MASK;
		val |= (ULPS_STATE_ENTER | DEVICE_READY);
		I915_WRITE(MIPI_DEVICE_READY(port), val);
		/* at least 2us - relaxed for hrtimer subsystem optimization */
		usleep_range(10, 50);

		/* 3. Exit ULPS */
		val = I915_READ(MIPI_DEVICE_READY(port));
		val &= ~ULPS_STATE_MASK;
		val |= (ULPS_STATE_EXIT | DEVICE_READY);
		I915_WRITE(MIPI_DEVICE_READY(port), val);
		usleep_range(1000, 1500);

		/* Clear ULPS and set device ready */
		val = I915_READ(MIPI_DEVICE_READY(port));
		val &= ~ULPS_STATE_MASK;
		val |= DEVICE_READY;
		I915_WRITE(MIPI_DEVICE_READY(port), val);
	}
}

static void vlv_dsi_device_ready(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 val;

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->sb_lock);
	/* program rcomp for compliance, reduce from 50 ohms to 45 ohms
	 * needed everytime after power gate */
	vlv_flisdsi_write(dev_priv, 0x04, 0x0004);
	mutex_unlock(&dev_priv->sb_lock);

	/* bandgap reset is needed after everytime we do power gate */
	band_gap_reset(dev_priv);

	for_each_dsi_port(port, intel_dsi->ports) {

		I915_WRITE(MIPI_DEVICE_READY(port), ULPS_STATE_ENTER);
		usleep_range(2500, 3000);

		/* Enable MIPI PHY transparent latch
		 * Common bit for both MIPI Port A & MIPI Port C
		 * No similar bit in MIPI Port C reg
		 */
		val = I915_READ(MIPI_PORT_CTRL(PORT_A));
		I915_WRITE(MIPI_PORT_CTRL(PORT_A), val | LP_OUTPUT_HOLD);
		usleep_range(1000, 1500);

		I915_WRITE(MIPI_DEVICE_READY(port), ULPS_STATE_EXIT);
		usleep_range(2500, 3000);

		I915_WRITE(MIPI_DEVICE_READY(port), DEVICE_READY);
		usleep_range(2500, 3000);
	}
}

static void intel_dsi_device_ready(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		vlv_dsi_device_ready(encoder);
	else if (IS_GEN9_LP(dev_priv))
		bxt_dsi_device_ready(encoder);
}

static void intel_dsi_port_enable(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;

	if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK) {
		u32 temp;

		temp = I915_READ(VLV_CHICKEN_3);
		temp &= ~PIXEL_OVERLAP_CNT_MASK |
					intel_dsi->pixel_overlap <<
					PIXEL_OVERLAP_CNT_SHIFT;
		I915_WRITE(VLV_CHICKEN_3, temp);
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		i915_reg_t port_ctrl = IS_GEN9_LP(dev_priv) ?
			BXT_MIPI_PORT_CTRL(port) : MIPI_PORT_CTRL(port);
		u32 temp;

		temp = I915_READ(port_ctrl);

		temp &= ~LANE_CONFIGURATION_MASK;
		temp &= ~DUAL_LINK_MODE_MASK;

		if (intel_dsi->ports == (BIT(PORT_A) | BIT(PORT_C))) {
			temp |= (intel_dsi->dual_link - 1)
						<< DUAL_LINK_MODE_SHIFT;
			if (IS_BROXTON(dev_priv))
				temp |= LANE_CONFIGURATION_DUAL_LINK_A;
			else
				temp |= intel_crtc->pipe ?
					LANE_CONFIGURATION_DUAL_LINK_B :
					LANE_CONFIGURATION_DUAL_LINK_A;
		}
		/* assert ip_tg_enable signal */
		I915_WRITE(port_ctrl, temp | DPI_ENABLE);
		POSTING_READ(port_ctrl);
	}
}

static void intel_dsi_port_disable(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;

	for_each_dsi_port(port, intel_dsi->ports) {
		i915_reg_t port_ctrl = IS_GEN9_LP(dev_priv) ?
			BXT_MIPI_PORT_CTRL(port) : MIPI_PORT_CTRL(port);
		u32 temp;

		/* de-assert ip_tg_enable signal */
		temp = I915_READ(port_ctrl);
		I915_WRITE(port_ctrl, temp & ~DPI_ENABLE);
		POSTING_READ(port_ctrl);
	}
}

static void intel_dsi_enable(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;

	DRM_DEBUG_KMS("\n");

	if (is_cmd_mode(intel_dsi)) {
		for_each_dsi_port(port, intel_dsi->ports)
			I915_WRITE(MIPI_MAX_RETURN_PKT_SIZE(port), 8 * 4);
	} else {
		msleep(20); /* XXX */
		for_each_dsi_port(port, intel_dsi->ports)
			dpi_send_cmd(intel_dsi, TURN_ON, false, port);
		msleep(100);

		drm_panel_enable(intel_dsi->panel);

		for_each_dsi_port(port, intel_dsi->ports)
			wait_for_dsi_fifo_empty(intel_dsi, port);

		intel_dsi_port_enable(encoder);
	}

	intel_panel_enable_backlight(intel_dsi->attached_connector);
}

static void intel_dsi_prepare(struct intel_encoder *intel_encoder,
			      struct intel_crtc_state *pipe_config);

static void intel_dsi_pre_enable(struct intel_encoder *encoder,
				 struct intel_crtc_state *pipe_config,
				 struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;

	DRM_DEBUG_KMS("\n");

	/*
	 * The BIOS may leave the PLL in a wonky state where it doesn't
	 * lock. It needs to be fully powered down to fix it.
	 */
	intel_disable_dsi_pll(encoder);
	intel_enable_dsi_pll(encoder, pipe_config);

	intel_dsi_prepare(encoder, pipe_config);

	/* Panel Enable over CRC PMIC */
	if (intel_dsi->gpio_panel)
		gpiod_set_value_cansleep(intel_dsi->gpio_panel, 1);

	msleep(intel_dsi->panel_on_delay);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		u32 val;

		/* Disable DPOunit clock gating, can stall pipe */
		val = I915_READ(DSPCLK_GATE_D);
		val |= DPOUNIT_CLOCK_GATE_DISABLE;
		I915_WRITE(DSPCLK_GATE_D, val);
	}

	/* put device in ready state */
	intel_dsi_device_ready(encoder);

	drm_panel_prepare(intel_dsi->panel);

	for_each_dsi_port(port, intel_dsi->ports)
		wait_for_dsi_fifo_empty(intel_dsi, port);

	/* Enable port in pre-enable phase itself because as per hw team
	 * recommendation, port should be enabled befor plane & pipe */
	intel_dsi_enable(encoder);
}

static void intel_dsi_enable_nop(struct intel_encoder *encoder,
				 struct intel_crtc_state *pipe_config,
				 struct drm_connector_state *conn_state)
{
	DRM_DEBUG_KMS("\n");

	/* for DSI port enable has to be done before pipe
	 * and plane enable, so port enable is done in
	 * pre_enable phase itself unlike other encoders
	 */
}

static void intel_dsi_pre_disable(struct intel_encoder *encoder,
				  struct intel_crtc_state *old_crtc_state,
				  struct drm_connector_state *old_conn_state)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;

	DRM_DEBUG_KMS("\n");

	intel_panel_disable_backlight(intel_dsi->attached_connector);

	if (is_vid_mode(intel_dsi)) {
		/* Send Shutdown command to the panel in LP mode */
		for_each_dsi_port(port, intel_dsi->ports)
			dpi_send_cmd(intel_dsi, SHUTDOWN, false, port);
		msleep(10);
	}
}

static void intel_dsi_disable(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 temp;

	DRM_DEBUG_KMS("\n");

	if (is_vid_mode(intel_dsi)) {
		for_each_dsi_port(port, intel_dsi->ports)
			wait_for_dsi_fifo_empty(intel_dsi, port);

		intel_dsi_port_disable(encoder);
		msleep(2);
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		/* Panel commands can be sent when clock is in LP11 */
		I915_WRITE(MIPI_DEVICE_READY(port), 0x0);

		intel_dsi_reset_clocks(encoder, port);
		I915_WRITE(MIPI_EOT_DISABLE(port), CLOCKSTOP);

		temp = I915_READ(MIPI_DSI_FUNC_PRG(port));
		temp &= ~VID_MODE_FORMAT_MASK;
		I915_WRITE(MIPI_DSI_FUNC_PRG(port), temp);

		I915_WRITE(MIPI_DEVICE_READY(port), 0x1);
	}
	/* if disable packets are sent before sending shutdown packet then in
	 * some next enable sequence send turn on packet error is observed */
	drm_panel_disable(intel_dsi->panel);

	for_each_dsi_port(port, intel_dsi->ports)
		wait_for_dsi_fifo_empty(intel_dsi, port);
}

static void intel_dsi_clear_device_ready(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;

	DRM_DEBUG_KMS("\n");
	for_each_dsi_port(port, intel_dsi->ports) {
		/* Common bit for both MIPI Port A & MIPI Port C on VLV/CHV */
		i915_reg_t port_ctrl = IS_GEN9_LP(dev_priv) ?
			BXT_MIPI_PORT_CTRL(port) : MIPI_PORT_CTRL(PORT_A);
		u32 val;

		I915_WRITE(MIPI_DEVICE_READY(port), DEVICE_READY |
							ULPS_STATE_ENTER);
		usleep_range(2000, 2500);

		I915_WRITE(MIPI_DEVICE_READY(port), DEVICE_READY |
							ULPS_STATE_EXIT);
		usleep_range(2000, 2500);

		I915_WRITE(MIPI_DEVICE_READY(port), DEVICE_READY |
							ULPS_STATE_ENTER);
		usleep_range(2000, 2500);

		/* Wait till Clock lanes are in LP-00 state for MIPI Port A
		 * only. MIPI Port C has no similar bit for checking
		 */
		if (intel_wait_for_register(dev_priv,
					    port_ctrl, AFE_LATCHOUT, 0,
					    30))
			DRM_ERROR("DSI LP not going Low\n");

		/* Disable MIPI PHY transparent latch */
		val = I915_READ(port_ctrl);
		I915_WRITE(port_ctrl, val & ~LP_OUTPUT_HOLD);
		usleep_range(1000, 1500);

		I915_WRITE(MIPI_DEVICE_READY(port), 0x00);
		usleep_range(2000, 2500);
	}
}

static void intel_dsi_post_disable(struct intel_encoder *encoder,
				   struct intel_crtc_state *pipe_config,
				   struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);

	DRM_DEBUG_KMS("\n");

	intel_dsi_disable(encoder);

	intel_dsi_clear_device_ready(encoder);

	intel_disable_dsi_pll(encoder);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		u32 val;

		val = I915_READ(DSPCLK_GATE_D);
		val &= ~DPOUNIT_CLOCK_GATE_DISABLE;
		I915_WRITE(DSPCLK_GATE_D, val);
	}

	drm_panel_unprepare(intel_dsi->panel);

	msleep(intel_dsi->panel_off_delay);

	/* Panel Disable over CRC PMIC */
	if (intel_dsi->gpio_panel)
		gpiod_set_value_cansleep(intel_dsi->gpio_panel, 0);

	/*
	 * FIXME As we do with eDP, just make a note of the time here
	 * and perform the wait before the next panel power on.
	 */
	msleep(intel_dsi->panel_pwr_cycle_delay);
}

static bool intel_dsi_get_hw_state(struct intel_encoder *encoder,
				   enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum intel_display_power_domain power_domain;
	enum port port;
	bool active = false;

	DRM_DEBUG_KMS("\n");

	power_domain = intel_display_port_power_domain(encoder);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
		return false;

	/*
	 * On Broxton the PLL needs to be enabled with a valid divider
	 * configuration, otherwise accessing DSI registers will hang the
	 * machine. See BSpec North Display Engine registers/MIPI[BXT].
	 */
	if (IS_GEN9_LP(dev_priv) && !intel_dsi_pll_is_enabled(dev_priv))
		goto out_put_power;

	/* XXX: this only works for one DSI output */
	for_each_dsi_port(port, intel_dsi->ports) {
		i915_reg_t ctrl_reg = IS_GEN9_LP(dev_priv) ?
			BXT_MIPI_PORT_CTRL(port) : MIPI_PORT_CTRL(port);
		bool enabled = I915_READ(ctrl_reg) & DPI_ENABLE;

		/*
		 * Due to some hardware limitations on VLV/CHV, the DPI enable
		 * bit in port C control register does not get set. As a
		 * workaround, check pipe B conf instead.
		 */
		if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
		    port == PORT_C)
			enabled = I915_READ(PIPECONF(PIPE_B)) & PIPECONF_ENABLE;

		/* Try command mode if video mode not enabled */
		if (!enabled) {
			u32 tmp = I915_READ(MIPI_DSI_FUNC_PRG(port));
			enabled = tmp & CMD_MODE_DATA_WIDTH_MASK;
		}

		if (!enabled)
			continue;

		if (!(I915_READ(MIPI_DEVICE_READY(port)) & DEVICE_READY))
			continue;

		if (IS_GEN9_LP(dev_priv)) {
			u32 tmp = I915_READ(MIPI_CTRL(port));
			tmp &= BXT_PIPE_SELECT_MASK;
			tmp >>= BXT_PIPE_SELECT_SHIFT;

			if (WARN_ON(tmp > PIPE_C))
				continue;

			*pipe = tmp;
		} else {
			*pipe = port == PORT_A ? PIPE_A : PIPE_B;
		}

		active = true;
		break;
	}

out_put_power:
	intel_display_power_put(dev_priv, power_domain);

	return active;
}

static void bxt_dsi_get_pipe_config(struct intel_encoder *encoder,
				 struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_display_mode *adjusted_mode =
					&pipe_config->base.adjusted_mode;
	struct drm_display_mode *adjusted_mode_sw;
	struct intel_crtc *intel_crtc;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	unsigned int lane_count = intel_dsi->lane_count;
	unsigned int bpp, fmt;
	enum port port;
	u16 hactive, hfp, hsync, hbp, vfp, vsync, vbp;
	u16 hfp_sw, hsync_sw, hbp_sw;
	u16 crtc_htotal_sw, crtc_hsync_start_sw, crtc_hsync_end_sw,
				crtc_hblank_start_sw, crtc_hblank_end_sw;

	/* FIXME: hw readout should not depend on SW state */
	intel_crtc = to_intel_crtc(encoder->base.crtc);
	adjusted_mode_sw = &intel_crtc->config->base.adjusted_mode;

	/*
	 * Atleast one port is active as encoder->get_config called only if
	 * encoder->get_hw_state() returns true.
	 */
	for_each_dsi_port(port, intel_dsi->ports) {
		if (I915_READ(BXT_MIPI_PORT_CTRL(port)) & DPI_ENABLE)
			break;
	}

	fmt = I915_READ(MIPI_DSI_FUNC_PRG(port)) & VID_MODE_FORMAT_MASK;
	pipe_config->pipe_bpp =
			mipi_dsi_pixel_format_to_bpp(
				pixel_format_from_register_bits(fmt));
	bpp = pipe_config->pipe_bpp;

	/* In terms of pixels */
	adjusted_mode->crtc_hdisplay =
				I915_READ(BXT_MIPI_TRANS_HACTIVE(port));
	adjusted_mode->crtc_vdisplay =
				I915_READ(BXT_MIPI_TRANS_VACTIVE(port));
	adjusted_mode->crtc_vtotal =
				I915_READ(BXT_MIPI_TRANS_VTOTAL(port));

	hactive = adjusted_mode->crtc_hdisplay;
	hfp = I915_READ(MIPI_HFP_COUNT(port));

	/*
	 * Meaningful for video mode non-burst sync pulse mode only,
	 * can be zero for non-burst sync events and burst modes
	 */
	hsync = I915_READ(MIPI_HSYNC_PADDING_COUNT(port));
	hbp = I915_READ(MIPI_HBP_COUNT(port));

	/* harizontal values are in terms of high speed byte clock */
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
	vfp = I915_READ(MIPI_VFP_COUNT(port));
	vsync = I915_READ(MIPI_VSYNC_PADDING_COUNT(port));
	vbp = I915_READ(MIPI_VBP_COUNT(port));

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
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 pclk;
	DRM_DEBUG_KMS("\n");

	if (IS_GEN9_LP(dev_priv))
		bxt_dsi_get_pipe_config(encoder, pipe_config);

	pclk = intel_dsi_get_pclk(encoder, pipe_config->pipe_bpp,
				  pipe_config);
	if (!pclk)
		return;

	pipe_config->base.adjusted_mode.crtc_clock = pclk;
	pipe_config->port_clock = pclk;
}

static enum drm_mode_status
intel_dsi_mode_valid(struct drm_connector *connector,
		     struct drm_display_mode *mode)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	const struct drm_display_mode *fixed_mode = intel_connector->panel.fixed_mode;
	int max_dotclk = to_i915(connector->dev)->max_dotclk_freq;

	DRM_DEBUG_KMS("\n");

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN) {
		DRM_DEBUG_KMS("MODE_NO_DBLESCAN\n");
		return MODE_NO_DBLESCAN;
	}

	if (fixed_mode) {
		if (mode->hdisplay > fixed_mode->hdisplay)
			return MODE_PANEL;
		if (mode->vdisplay > fixed_mode->vdisplay)
			return MODE_PANEL;
		if (fixed_mode->clock > max_dotclk)
			return MODE_CLOCK_HIGH;
	}

	return MODE_OK;
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

static void set_dsi_timings(struct drm_encoder *encoder,
			    const struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
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
		if (IS_GEN9_LP(dev_priv)) {
			/*
			 * Program hdisplay and vdisplay on MIPI transcoder.
			 * This is different from calculated hactive and
			 * vactive, as they are calculated per channel basis,
			 * whereas these values should be based on resolution.
			 */
			I915_WRITE(BXT_MIPI_TRANS_HACTIVE(port),
				   adjusted_mode->crtc_hdisplay);
			I915_WRITE(BXT_MIPI_TRANS_VACTIVE(port),
				   adjusted_mode->crtc_vdisplay);
			I915_WRITE(BXT_MIPI_TRANS_VTOTAL(port),
				   adjusted_mode->crtc_vtotal);
		}

		I915_WRITE(MIPI_HACTIVE_AREA_COUNT(port), hactive);
		I915_WRITE(MIPI_HFP_COUNT(port), hfp);

		/* meaningful for video mode non-burst sync pulse mode only,
		 * can be zero for non-burst sync events and burst modes */
		I915_WRITE(MIPI_HSYNC_PADDING_COUNT(port), hsync);
		I915_WRITE(MIPI_HBP_COUNT(port), hbp);

		/* vertical values are in terms of lines */
		I915_WRITE(MIPI_VFP_COUNT(port), vfp);
		I915_WRITE(MIPI_VSYNC_PADDING_COUNT(port), vsync);
		I915_WRITE(MIPI_VBP_COUNT(port), vbp);
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

static void intel_dsi_prepare(struct intel_encoder *intel_encoder,
			      struct intel_crtc_state *pipe_config)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(pipe_config->base.crtc);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	const struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	enum port port;
	unsigned int bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);
	u32 val, tmp;
	u16 mode_hdisplay;

	DRM_DEBUG_KMS("pipe %c\n", pipe_name(intel_crtc->pipe));

	mode_hdisplay = adjusted_mode->crtc_hdisplay;

	if (intel_dsi->dual_link) {
		mode_hdisplay /= 2;
		if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK)
			mode_hdisplay += intel_dsi->pixel_overlap;
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
			/*
			 * escape clock divider, 20MHz, shared for A and C.
			 * device ready must be off when doing this! txclkesc?
			 */
			tmp = I915_READ(MIPI_CTRL(PORT_A));
			tmp &= ~ESCAPE_CLOCK_DIVIDER_MASK;
			I915_WRITE(MIPI_CTRL(PORT_A), tmp |
					ESCAPE_CLOCK_DIVIDER_1);

			/* read request priority is per pipe */
			tmp = I915_READ(MIPI_CTRL(port));
			tmp &= ~READ_REQUEST_PRIORITY_MASK;
			I915_WRITE(MIPI_CTRL(port), tmp |
					READ_REQUEST_PRIORITY_HIGH);
		} else if (IS_GEN9_LP(dev_priv)) {
			enum pipe pipe = intel_crtc->pipe;

			tmp = I915_READ(MIPI_CTRL(port));
			tmp &= ~BXT_PIPE_SELECT_MASK;

			tmp |= BXT_PIPE_SELECT(pipe);
			I915_WRITE(MIPI_CTRL(port), tmp);
		}

		/* XXX: why here, why like this? handling in irq handler?! */
		I915_WRITE(MIPI_INTR_STAT(port), 0xffffffff);
		I915_WRITE(MIPI_INTR_EN(port), 0xffffffff);

		I915_WRITE(MIPI_DPHY_PARAM(port), intel_dsi->dphy_reg);

		I915_WRITE(MIPI_DPI_RESOLUTION(port),
			adjusted_mode->crtc_vdisplay << VERTICAL_ADDRESS_SHIFT |
			mode_hdisplay << HORIZONTAL_ADDRESS_SHIFT);
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

	if (IS_GEN9_LP(dev_priv)) {
		tmp |= BXT_DPHY_DEFEATURE_EN;
		if (!is_cmd_mode(intel_dsi))
			tmp |= BXT_DEFEATURE_DPI_FIFO_CTR;
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		I915_WRITE(MIPI_DSI_FUNC_PRG(port), val);

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
			intel_dsi->video_mode_format == VIDEO_MODE_BURST) {
			I915_WRITE(MIPI_HS_TX_TIMEOUT(port),
				txbyteclkhs(adjusted_mode->crtc_htotal, bpp,
					    intel_dsi->lane_count,
					    intel_dsi->burst_mode_ratio) + 1);
		} else {
			I915_WRITE(MIPI_HS_TX_TIMEOUT(port),
				txbyteclkhs(adjusted_mode->crtc_vtotal *
					    adjusted_mode->crtc_htotal,
					    bpp, intel_dsi->lane_count,
					    intel_dsi->burst_mode_ratio) + 1);
		}
		I915_WRITE(MIPI_LP_RX_TIMEOUT(port), intel_dsi->lp_rx_timeout);
		I915_WRITE(MIPI_TURN_AROUND_TIMEOUT(port),
						intel_dsi->turn_arnd_val);
		I915_WRITE(MIPI_DEVICE_RESET_TIMER(port),
						intel_dsi->rst_timer_val);

		/* dphy stuff */

		/* in terms of low power clock */
		I915_WRITE(MIPI_INIT_COUNT(port),
				txclkesc(intel_dsi->escape_clk_div, 100));

		if (IS_GEN9_LP(dev_priv) && (!intel_dsi->dual_link)) {
			/*
			 * BXT spec says write MIPI_INIT_COUNT for
			 * both the ports, even if only one is
			 * getting used. So write the other port
			 * if not in dual link mode.
			 */
			I915_WRITE(MIPI_INIT_COUNT(port ==
						PORT_A ? PORT_C : PORT_A),
					intel_dsi->init_count);
		}

		/* recovery disables */
		I915_WRITE(MIPI_EOT_DISABLE(port), tmp);

		/* in terms of low power clock */
		I915_WRITE(MIPI_INIT_COUNT(port), intel_dsi->init_count);

		/* in terms of txbyteclkhs. actual high to low switch +
		 * MIPI_STOP_STATE_STALL * MIPI_LP_BYTECLK.
		 *
		 * XXX: write MIPI_STOP_STATE_STALL?
		 */
		I915_WRITE(MIPI_HIGH_LOW_SWITCH_COUNT(port),
						intel_dsi->hs_to_lp_count);

		/* XXX: low power clock equivalence in terms of byte clock.
		 * the number of byte clocks occupied in one low power clock.
		 * based on txbyteclkhs and txclkesc.
		 * txclkesc time / txbyteclk time * (105 + MIPI_STOP_STATE_STALL
		 * ) / 105.???
		 */
		I915_WRITE(MIPI_LP_BYTECLK(port), intel_dsi->lp_byte_clk);

		/* the bw essential for transmitting 16 long packets containing
		 * 252 bytes meant for dcs write memory command is programmed in
		 * this register in terms of byte clocks. based on dsi transfer
		 * rate and the number of lanes configured the time taken to
		 * transmit 16 long packets in a dsi stream varies. */
		I915_WRITE(MIPI_DBI_BW_CTRL(port), intel_dsi->bw_timer);

		I915_WRITE(MIPI_CLK_LANE_SWITCH_TIME_CNT(port),
		intel_dsi->clk_lp_to_hs_count << LP_HS_SSW_CNT_SHIFT |
		intel_dsi->clk_hs_to_lp_count << HS_LP_PWR_SW_CNT_SHIFT);

		if (is_vid_mode(intel_dsi))
			/* Some panels might have resolution which is not a
			 * multiple of 64 like 1366 x 768. Enable RANDOM
			 * resolution support for such panels by default */
			I915_WRITE(MIPI_VIDEO_MODE_FORMAT(port),
				intel_dsi->video_frmt_cfg_bits |
				intel_dsi->video_mode_format |
				IP_TG_CONFIG |
				RANDOM_DPI_DISPLAY_RESOLUTION);
	}
}

static int intel_dsi_get_modes(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_display_mode *mode;

	DRM_DEBUG_KMS("\n");

	if (!intel_connector->panel.fixed_mode) {
		DRM_DEBUG_KMS("no fixed mode\n");
		return 0;
	}

	mode = drm_mode_duplicate(connector->dev,
				  intel_connector->panel.fixed_mode);
	if (!mode) {
		DRM_DEBUG_KMS("drm_mode_duplicate failed\n");
		return 0;
	}

	drm_mode_probed_add(connector, mode);
	return 1;
}

static int intel_dsi_set_property(struct drm_connector *connector,
				  struct drm_property *property,
				  uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_crtc *crtc;
	int ret;

	ret = drm_object_property_set_value(&connector->base, property, val);
	if (ret)
		return ret;

	if (property == dev->mode_config.scaling_mode_property) {
		if (val == DRM_MODE_SCALE_NONE) {
			DRM_DEBUG_KMS("no scaling not supported\n");
			return -EINVAL;
		}
		if (HAS_GMCH_DISPLAY(to_i915(dev)) &&
		    val == DRM_MODE_SCALE_CENTER) {
			DRM_DEBUG_KMS("centering not supported\n");
			return -EINVAL;
		}

		if (intel_connector->panel.fitting_mode == val)
			return 0;

		intel_connector->panel.fitting_mode = val;
	}

	crtc = connector->state->crtc;
	if (crtc && crtc->state->enable) {
		/*
		 * If the CRTC is enabled, the display will be changed
		 * according to the new panel fitting mode.
		 */
		intel_crtc_restore_mode(crtc);
	}

	return 0;
}

static void intel_dsi_connector_destroy(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	DRM_DEBUG_KMS("\n");
	intel_panel_fini(&intel_connector->panel);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static void intel_dsi_encoder_destroy(struct drm_encoder *encoder)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);

	if (intel_dsi->panel) {
		drm_panel_detach(intel_dsi->panel);
		/* XXX: Logically this call belongs in the panel driver. */
		drm_panel_remove(intel_dsi->panel);
	}

	/* dispose of the gpios */
	if (intel_dsi->gpio_panel)
		gpiod_put(intel_dsi->gpio_panel);

	intel_encoder_destroy(encoder);
}

static const struct drm_encoder_funcs intel_dsi_funcs = {
	.destroy = intel_dsi_encoder_destroy,
};

static const struct drm_connector_helper_funcs intel_dsi_connector_helper_funcs = {
	.get_modes = intel_dsi_get_modes,
	.mode_valid = intel_dsi_mode_valid,
};

static const struct drm_connector_funcs intel_dsi_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.late_register = intel_connector_register,
	.early_unregister = intel_connector_unregister,
	.destroy = intel_dsi_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = intel_dsi_set_property,
	.atomic_get_property = intel_connector_atomic_get_property,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
};

static void intel_dsi_add_properties(struct intel_connector *connector)
{
	struct drm_device *dev = connector->base.dev;

	if (connector->panel.fixed_mode) {
		drm_mode_create_scaling_mode_property(dev);
		drm_object_attach_property(&connector->base.base,
					   dev->mode_config.scaling_mode_property,
					   DRM_MODE_SCALE_ASPECT);
		connector->panel.fitting_mode = DRM_MODE_SCALE_ASPECT;
	}
}

void intel_dsi_init(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_dsi *intel_dsi;
	struct intel_encoder *intel_encoder;
	struct drm_encoder *encoder;
	struct intel_connector *intel_connector;
	struct drm_connector *connector;
	struct drm_display_mode *scan, *fixed_mode = NULL;
	enum port port;
	unsigned int i;

	DRM_DEBUG_KMS("\n");

	/* There is no detection method for MIPI so rely on VBT */
	if (!intel_bios_is_dsi_present(dev_priv, &port))
		return;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		dev_priv->mipi_mmio_base = VLV_MIPI_BASE;
	} else if (IS_GEN9_LP(dev_priv)) {
		dev_priv->mipi_mmio_base = BXT_MIPI_BASE;
	} else {
		DRM_ERROR("Unsupported Mipi device to reg base");
		return;
	}

	intel_dsi = kzalloc(sizeof(*intel_dsi), GFP_KERNEL);
	if (!intel_dsi)
		return;

	intel_connector = intel_connector_alloc();
	if (!intel_connector) {
		kfree(intel_dsi);
		return;
	}

	intel_encoder = &intel_dsi->base;
	encoder = &intel_encoder->base;
	intel_dsi->attached_connector = intel_connector;

	connector = &intel_connector->base;

	drm_encoder_init(dev, encoder, &intel_dsi_funcs, DRM_MODE_ENCODER_DSI,
			 "DSI %c", port_name(port));

	intel_encoder->compute_config = intel_dsi_compute_config;
	intel_encoder->pre_enable = intel_dsi_pre_enable;
	intel_encoder->enable = intel_dsi_enable_nop;
	intel_encoder->disable = intel_dsi_pre_disable;
	intel_encoder->post_disable = intel_dsi_post_disable;
	intel_encoder->get_hw_state = intel_dsi_get_hw_state;
	intel_encoder->get_config = intel_dsi_get_config;

	intel_connector->get_hw_state = intel_connector_get_hw_state;

	intel_encoder->port = port;
	/*
	 * On BYT/CHV, pipe A maps to MIPI DSI port A, pipe B maps to MIPI DSI
	 * port C. BXT isn't limited like this.
	 */
	if (IS_GEN9_LP(dev_priv))
		intel_encoder->crtc_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C);
	else if (port == PORT_A)
		intel_encoder->crtc_mask = BIT(PIPE_A);
	else
		intel_encoder->crtc_mask = BIT(PIPE_B);

	if (dev_priv->vbt.dsi.config->dual_link) {
		intel_dsi->ports = BIT(PORT_A) | BIT(PORT_C);

		switch (dev_priv->vbt.dsi.config->dl_dcs_backlight_ports) {
		case DL_DCS_PORT_A:
			intel_dsi->dcs_backlight_ports = BIT(PORT_A);
			break;
		case DL_DCS_PORT_C:
			intel_dsi->dcs_backlight_ports = BIT(PORT_C);
			break;
		default:
		case DL_DCS_PORT_A_AND_C:
			intel_dsi->dcs_backlight_ports = BIT(PORT_A) | BIT(PORT_C);
			break;
		}

		switch (dev_priv->vbt.dsi.config->dl_dcs_cabc_ports) {
		case DL_DCS_PORT_A:
			intel_dsi->dcs_cabc_ports = BIT(PORT_A);
			break;
		case DL_DCS_PORT_C:
			intel_dsi->dcs_cabc_ports = BIT(PORT_C);
			break;
		default:
		case DL_DCS_PORT_A_AND_C:
			intel_dsi->dcs_cabc_ports = BIT(PORT_A) | BIT(PORT_C);
			break;
		}
	} else {
		intel_dsi->ports = BIT(port);
		intel_dsi->dcs_backlight_ports = BIT(port);
		intel_dsi->dcs_cabc_ports = BIT(port);
	}

	if (!dev_priv->vbt.dsi.config->cabc_supported)
		intel_dsi->dcs_cabc_ports = 0;

	/* Create a DSI host (and a device) for each port. */
	for_each_dsi_port(port, intel_dsi->ports) {
		struct intel_dsi_host *host;

		host = intel_dsi_host_init(intel_dsi, port);
		if (!host)
			goto err;

		intel_dsi->dsi_hosts[port] = host;
	}

	for (i = 0; i < ARRAY_SIZE(intel_dsi_drivers); i++) {
		intel_dsi->panel = intel_dsi_drivers[i].init(intel_dsi,
							     intel_dsi_drivers[i].panel_id);
		if (intel_dsi->panel)
			break;
	}

	if (!intel_dsi->panel) {
		DRM_DEBUG_KMS("no device found\n");
		goto err;
	}

	/*
	 * In case of BYT with CRC PMIC, we need to use GPIO for
	 * Panel control.
	 */
	if (dev_priv->vbt.dsi.config->pwm_blc == PPS_BLC_PMIC) {
		intel_dsi->gpio_panel =
			gpiod_get(dev->dev, "panel", GPIOD_OUT_HIGH);

		if (IS_ERR(intel_dsi->gpio_panel)) {
			DRM_ERROR("Failed to own gpio for panel control\n");
			intel_dsi->gpio_panel = NULL;
		}
	}

	intel_encoder->type = INTEL_OUTPUT_DSI;
	intel_encoder->cloneable = 0;
	drm_connector_init(dev, connector, &intel_dsi_connector_funcs,
			   DRM_MODE_CONNECTOR_DSI);

	drm_connector_helper_add(connector, &intel_dsi_connector_helper_funcs);

	connector->display_info.subpixel_order = SubPixelHorizontalRGB; /*XXX*/
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	intel_connector_attach_encoder(intel_connector, intel_encoder);

	drm_panel_attach(intel_dsi->panel, connector);

	mutex_lock(&dev->mode_config.mutex);
	drm_panel_get_modes(intel_dsi->panel);
	list_for_each_entry(scan, &connector->probed_modes, head) {
		if ((scan->type & DRM_MODE_TYPE_PREFERRED)) {
			fixed_mode = drm_mode_duplicate(dev, scan);
			break;
		}
	}
	mutex_unlock(&dev->mode_config.mutex);

	if (!fixed_mode) {
		DRM_DEBUG_KMS("no fixed mode\n");
		goto err;
	}

	connector->display_info.width_mm = fixed_mode->width_mm;
	connector->display_info.height_mm = fixed_mode->height_mm;

	intel_panel_init(&intel_connector->panel, fixed_mode, NULL);
	intel_panel_setup_backlight(connector, INVALID_PIPE);

	intel_dsi_add_properties(intel_connector);

	return;

err:
	drm_encoder_cleanup(&intel_encoder->base);
	kfree(intel_dsi);
	kfree(intel_connector);
}
