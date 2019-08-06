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

	/*
	 * video mode pixel format
	 *
	 * XXX: consolidate on .format in struct mipi_dsi_device.
	 */
	enum mipi_dsi_pixel_format pixel_format;

	/* video mode format for MIPI_VIDEO_MODE_FORMAT register */
	u32 video_mode_format;

	/* eot for MIPI_EOT_DISABLE register */
	u8 eotp_pkt;
	u8 clock_stop;

	u8 escape_clk_div;
	u8 dual_link;

	u16 dcs_backlight_ports;
	u16 dcs_cabc_ports;

	/* RGB or BGR */
	bool bgr_enabled;

	u8 pixel_overlap;
	u32 port_bits;
	u32 bw_timer;
	u32 dphy_reg;

	/* data lanes dphy timing */
	u32 dphy_data_lane_reg;
	u32 video_frmt_cfg_bits;
	u16 lp_byte_clk;

	/* timeouts in byte clocks */
	u16 hs_tx_timeout;
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

#define for_each_dsi_port(__port, __ports_mask) for_each_port_masked(__port, __ports_mask)

static inline struct intel_dsi *enc_to_intel_dsi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct intel_dsi, base.base);
}

static inline bool is_vid_mode(struct intel_dsi *intel_dsi)
{
	return intel_dsi->operation_mode == INTEL_DSI_VIDEO_MODE;
}

static inline bool is_cmd_mode(struct intel_dsi *intel_dsi)
{
	return intel_dsi->operation_mode == INTEL_DSI_COMMAND_MODE;
}

static inline u16 intel_dsi_encoder_ports(struct intel_encoder *encoder)
{
	return enc_to_intel_dsi(&encoder->base)->ports;
}

/* intel_dsi.c */
int intel_dsi_bitrate(const struct intel_dsi *intel_dsi);
int intel_dsi_tlpx_ns(const struct intel_dsi *intel_dsi);
enum drm_panel_orientation
intel_dsi_get_panel_orientation(struct intel_connector *connector);

/* vlv_dsi.c */
void vlv_dsi_wait_for_fifo_empty(struct intel_dsi *intel_dsi, enum port port);
enum mipi_dsi_pixel_format pixel_format_from_register_bits(u32 fmt);
int intel_dsi_get_modes(struct drm_connector *connector);
enum drm_mode_status intel_dsi_mode_valid(struct drm_connector *connector,
					  struct drm_display_mode *mode);
struct intel_dsi_host *intel_dsi_host_init(struct intel_dsi *intel_dsi,
					   const struct mipi_dsi_host_ops *funcs,
					   enum port port);

/* vlv_dsi_pll.c */
int vlv_dsi_pll_compute(struct intel_encoder *encoder,
			struct intel_crtc_state *config);
void vlv_dsi_pll_enable(struct intel_encoder *encoder,
			const struct intel_crtc_state *config);
void vlv_dsi_pll_disable(struct intel_encoder *encoder);
u32 vlv_dsi_get_pclk(struct intel_encoder *encoder, int pipe_bpp,
		     struct intel_crtc_state *config);
void vlv_dsi_reset_clocks(struct intel_encoder *encoder, enum port port);

bool bxt_dsi_pll_is_enabled(struct drm_i915_private *dev_priv);
int bxt_dsi_pll_compute(struct intel_encoder *encoder,
			struct intel_crtc_state *config);
void bxt_dsi_pll_enable(struct intel_encoder *encoder,
			const struct intel_crtc_state *config);
void bxt_dsi_pll_disable(struct intel_encoder *encoder);
u32 bxt_dsi_get_pclk(struct intel_encoder *encoder, int pipe_bpp,
		     struct intel_crtc_state *config);
void bxt_dsi_reset_clocks(struct intel_encoder *encoder, enum port port);

/* intel_dsi_vbt.c */
bool intel_dsi_vbt_init(struct intel_dsi *intel_dsi, u16 panel_id);
int intel_dsi_vbt_get_modes(struct intel_dsi *intel_dsi);
void intel_dsi_vbt_exec_sequence(struct intel_dsi *intel_dsi,
				 enum mipi_seq seq_id);
void intel_dsi_msleep(struct intel_dsi *intel_dsi, int msec);

#endif /* _INTEL_DSI_H */
