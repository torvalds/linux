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
 */

#ifndef _INTEL_DSI_H
#define _INTEL_DSI_H

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include "intel_drv.h"

/* Dual Link support */
#define DSI_DUAL_LINK_NONE		0
#define DSI_DUAL_LINK_FRONT_BACK	1
#define DSI_DUAL_LINK_PIXEL_ALT		2

struct intel_dsi_host;

struct intel_dsi {
	struct intel_encoder base;

	struct drm_panel *panel;
	struct intel_dsi_host *dsi_hosts[I915_MAX_PORTS];

	/* GPIO Desc for CRC based Panel control */
	struct gpio_desc *gpio_panel;

	struct intel_connector *attached_connector;

	/* bit mask of ports being driven */
	u16 ports;

	/* if true, use HS mode, otherwise LP */
	bool hs;

	/* virtual channel */
	int channel;

	/* Video mode or command mode */
	u16 operation_mode;

	/* number of DSI lanes */
	unsigned int lane_count;

	/* video mode pixel format for MIPI_DSI_FUNC_PRG register */
	u32 pixel_format;

	/* video mode format for MIPI_VIDEO_MODE_FORMAT register */
	u32 video_mode_format;

	/* eot for MIPI_EOT_DISABLE register */
	u8 eotp_pkt;
	u8 clock_stop;

	u8 escape_clk_div;
	u8 dual_link;
	u8 pixel_overlap;
	u32 port_bits;
	u32 bw_timer;
	u32 dphy_reg;
	u32 video_frmt_cfg_bits;
	u16 lp_byte_clk;

	/* timeouts in byte clocks */
	u16 lp_rx_timeout;
	u16 turn_arnd_val;
	u16 rst_timer_val;
	u16 hs_to_lp_count;
	u16 clk_lp_to_hs_count;
	u16 clk_hs_to_lp_count;

	u16 init_count;
	u32 pclk;
	u16 burst_mode_ratio;

	/* all delays in ms */
	u16 backlight_off_delay;
	u16 backlight_on_delay;
	u16 panel_on_delay;
	u16 panel_off_delay;
	u16 panel_pwr_cycle_delay;
};

struct intel_dsi_host {
	struct mipi_dsi_host base;
	struct intel_dsi *intel_dsi;
	enum port port;

	/* our little hack */
	struct mipi_dsi_device *device;
};

static inline struct intel_dsi_host *to_intel_dsi_host(struct mipi_dsi_host *h)
{
	return container_of(h, struct intel_dsi_host, base);
}

#define for_each_dsi_port(__port, __ports_mask) \
	for ((__port) = PORT_A; (__port) < I915_MAX_PORTS; (__port)++)	\
		for_each_if ((__ports_mask) & (1 << (__port)))

static inline struct intel_dsi *enc_to_intel_dsi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct intel_dsi, base.base);
}

extern void intel_enable_dsi_pll(struct intel_encoder *encoder);
extern void intel_disable_dsi_pll(struct intel_encoder *encoder);
extern u32 vlv_get_dsi_pclk(struct intel_encoder *encoder, int pipe_bpp);
extern u32 bxt_get_dsi_pclk(struct intel_encoder *encoder, int pipe_bpp);
extern void intel_dsi_reset_clocks(struct intel_encoder *encoder,
							enum port port);

struct drm_panel *vbt_panel_init(struct intel_dsi *intel_dsi, u16 panel_id);

#endif /* _INTEL_DSI_H */
