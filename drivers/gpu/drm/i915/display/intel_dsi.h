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

#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>

#include "intel_display_types.h"

#define INTEL_DSI_VIDEO_MODE	0
#define INTEL_DSI_COMMAND_MODE	1

/* Dual Link support */
#define DSI_DUAL_LINK_NONE		0
#define DSI_DUAL_LINK_FRONT_BACK	1
#define DSI_DUAL_LINK_PIXEL_ALT		2

struct intel_dsi_host;

struct intel_dsi {
	struct intel_encoder base;

	struct intel_dsi_host *dsi_hosts[I915_MAX_PORTS];
	intel_wakeref_t io_wakeref[I915_MAX_PORTS];

	/* GPIO Desc for panel and backlight control */
	struct gpio_desc *gpio_panel;
	struct gpio_desc *gpio_backlight;

	struct intel_connector *attached_connector;

	/* bit mask of ports (vlv dsi) or phys (icl dsi) being driven */
	union {
		u16 ports;	/* VLV DSI */
		u16 phys;	/* ICL DSI */
	};

	/* if true, use HS mode, otherwise LP */
	bool hs;

	/* virtual channel */
	int channel;

	/* Video mode or command mode */
	u16 operation_mode;

	/* number of DSI lanes */
	unsigned int lane_count;

	/* i2c bus associated with the slave device */
	int i2c_bus_num;

	/*
	 * video mode pixel format
	 *
	 * XXX: consolidate on .format in struct mipi_dsi_device.
	 */
	enum mipi_dsi_pixel_format pixel_format;

	/* NON_BURST_SYNC_PULSE, NON_BURST_SYNC_EVENTS, or BURST_MODE */
	int video_mode;

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
	ktime_t panel_power_off_time;
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
	for_each_port_masked(__port, __ports_mask)
#define for_each_dsi_phy(__phy, __phys_mask) \
	for_each_phy_masked(__phy, __phys_mask)

static inline struct intel_dsi *enc_to_intel_dsi(struct intel_encoder *encoder)
{
	return container_of(&encoder->base, struct intel_dsi, base.base);
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
	return enc_to_intel_dsi(encoder)->ports;
}

int intel_dsi_bitrate(const struct intel_dsi *intel_dsi);
int intel_dsi_tlpx_ns(const struct intel_dsi *intel_dsi);
enum drm_panel_orientation
intel_dsi_get_panel_orientation(struct intel_connector *connector);
int intel_dsi_get_modes(struct drm_connector *connector);
enum drm_mode_status intel_dsi_mode_valid(struct drm_connector *connector,
					  struct drm_display_mode *mode);
struct intel_dsi_host *intel_dsi_host_init(struct intel_dsi *intel_dsi,
					   const struct mipi_dsi_host_ops *funcs,
					   enum port port);

#endif /* _INTEL_DSI_H */
