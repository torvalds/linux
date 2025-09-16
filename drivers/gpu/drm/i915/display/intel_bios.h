/*
 * Copyright © 2016-2019 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Please use intel_vbt_defs.h for VBT private data, to hide and abstract away
 * the VBT from the rest of the driver. Add the parsed, clean data to struct
 * intel_vbt_data within struct intel_display.
 */

#ifndef _INTEL_BIOS_H_
#define _INTEL_BIOS_H_

#include <linux/types.h>

struct drm_edid;
struct intel_bios_encoder_data;
struct intel_crtc_state;
struct intel_display;
struct intel_encoder;
struct intel_panel;
enum aux_ch;
enum port;

enum intel_backlight_type {
	INTEL_BACKLIGHT_PMIC,
	INTEL_BACKLIGHT_LPSS,
	INTEL_BACKLIGHT_DISPLAY_DDI,
	INTEL_BACKLIGHT_DSI_DCS,
	INTEL_BACKLIGHT_PANEL_DRIVER_INTERFACE,
	INTEL_BACKLIGHT_VESA_EDP_AUX_INTERFACE,
};

void intel_bios_init(struct intel_display *display);
void intel_bios_init_panel_early(struct intel_display *display,
				 struct intel_panel *panel,
				 const struct intel_bios_encoder_data *devdata);
void intel_bios_init_panel_late(struct intel_display *display,
				struct intel_panel *panel,
				const struct intel_bios_encoder_data *devdata,
				const struct drm_edid *drm_edid);
void intel_bios_fini_panel(struct intel_panel *panel);
void intel_bios_driver_remove(struct intel_display *display);
bool intel_bios_is_valid_vbt(struct intel_display *display,
			     const void *buf, size_t size);
bool intel_bios_is_tv_present(struct intel_display *display);
bool intel_bios_is_lvds_present(struct intel_display *display, u8 *i2c_pin);
bool intel_bios_is_port_present(struct intel_display *display, enum port port);
bool intel_bios_is_dsi_present(struct intel_display *display, enum port *port);
bool intel_bios_get_dsc_params(struct intel_encoder *encoder,
			       struct intel_crtc_state *crtc_state,
			       int dsc_max_bpc);

const struct intel_bios_encoder_data *
intel_bios_encoder_data_lookup(struct intel_display *display, enum port port);

bool intel_bios_encoder_supports_dvi(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_hdmi(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_dp(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_edp(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_typec_usb(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_tbt(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_dsi(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_dp_dual_mode(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_is_lspcon(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_lane_reversal(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_hpd_invert(const struct intel_bios_encoder_data *devdata);
enum port intel_bios_encoder_port(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_reject_edp_rate(const struct intel_bios_encoder_data *devdata,
					int rate);
enum aux_ch intel_bios_dp_aux_ch(const struct intel_bios_encoder_data *devdata);
int intel_bios_dp_boost_level(const struct intel_bios_encoder_data *devdata);
int intel_bios_dp_max_lane_count(const struct intel_bios_encoder_data *devdata);
int intel_bios_dp_max_link_rate(const struct intel_bios_encoder_data *devdata);
bool intel_bios_dp_has_shared_aux_ch(const struct intel_bios_encoder_data *devdata);
int intel_bios_hdmi_boost_level(const struct intel_bios_encoder_data *devdata);
int intel_bios_hdmi_ddc_pin(const struct intel_bios_encoder_data *devdata);
int intel_bios_hdmi_level_shift(const struct intel_bios_encoder_data *devdata);
int intel_bios_hdmi_max_tmds_clock(const struct intel_bios_encoder_data *devdata);

void intel_bios_for_each_encoder(struct intel_display *display,
				 void (*func)(struct intel_display *display,
					      const struct intel_bios_encoder_data *devdata));

void intel_bios_debugfs_register(struct intel_display *display);

#endif /* _INTEL_BIOS_H_ */
