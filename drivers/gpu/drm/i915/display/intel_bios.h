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
 * intel_vbt_data within struct drm_i915_private.
 */

#ifndef _INTEL_BIOS_H_
#define _INTEL_BIOS_H_

#include <linux/types.h>

struct drm_edid;
struct drm_i915_private;
struct intel_bios_encoder_data;
struct intel_crtc_state;
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

struct edp_power_seq {
	u16 t1_t3;
	u16 t8;
	u16 t9;
	u16 t10;
	u16 t11_t12;
} __packed;

/*
 * MIPI Sequence Block definitions
 *
 * Note the VBT spec has AssertReset / DeassertReset swapped from their
 * usual naming, we use the proper names here to avoid confusion when
 * reading the code.
 */
enum mipi_seq {
	MIPI_SEQ_END = 0,
	MIPI_SEQ_DEASSERT_RESET,	/* Spec says MipiAssertResetPin */
	MIPI_SEQ_INIT_OTP,
	MIPI_SEQ_DISPLAY_ON,
	MIPI_SEQ_DISPLAY_OFF,
	MIPI_SEQ_ASSERT_RESET,		/* Spec says MipiDeassertResetPin */
	MIPI_SEQ_BACKLIGHT_ON,		/* sequence block v2+ */
	MIPI_SEQ_BACKLIGHT_OFF,		/* sequence block v2+ */
	MIPI_SEQ_TEAR_ON,		/* sequence block v2+ */
	MIPI_SEQ_TEAR_OFF,		/* sequence block v3+ */
	MIPI_SEQ_POWER_ON,		/* sequence block v3+ */
	MIPI_SEQ_POWER_OFF,		/* sequence block v3+ */
	MIPI_SEQ_MAX
};

enum mipi_seq_element {
	MIPI_SEQ_ELEM_END = 0,
	MIPI_SEQ_ELEM_SEND_PKT,
	MIPI_SEQ_ELEM_DELAY,
	MIPI_SEQ_ELEM_GPIO,
	MIPI_SEQ_ELEM_I2C,		/* sequence block v2+ */
	MIPI_SEQ_ELEM_SPI,		/* sequence block v3+ */
	MIPI_SEQ_ELEM_PMIC,		/* sequence block v3+ */
	MIPI_SEQ_ELEM_MAX
};

#define MIPI_DSI_UNDEFINED_PANEL_ID	0
#define MIPI_DSI_GENERIC_PANEL_ID	1

struct mipi_config {
	u16 panel_id;

	/* General Params */
	u32 enable_dithering:1;
	u32 rsvd1:1;
	u32 is_bridge:1;

	u32 panel_arch_type:2;
	u32 is_cmd_mode:1;

#define NON_BURST_SYNC_PULSE	0x1
#define NON_BURST_SYNC_EVENTS	0x2
#define BURST_MODE		0x3
	u32 video_transfer_mode:2;

	u32 cabc_supported:1;
#define PPS_BLC_PMIC   0
#define PPS_BLC_SOC    1
	u32 pwm_blc:1;

	/* Bit 13:10 */
#define PIXEL_FORMAT_RGB565			0x1
#define PIXEL_FORMAT_RGB666			0x2
#define PIXEL_FORMAT_RGB666_LOOSELY_PACKED	0x3
#define PIXEL_FORMAT_RGB888			0x4
	u32 videomode_color_format:4;

	/* Bit 15:14 */
#define ENABLE_ROTATION_0	0x0
#define ENABLE_ROTATION_90	0x1
#define ENABLE_ROTATION_180	0x2
#define ENABLE_ROTATION_270	0x3
	u32 rotation:2;
	u32 bta_enabled:1;
	u32 rsvd2:15;

	/* 2 byte Port Description */
#define DUAL_LINK_NOT_SUPPORTED	0
#define DUAL_LINK_FRONT_BACK	1
#define DUAL_LINK_PIXEL_ALT	2
	u16 dual_link:2;
	u16 lane_cnt:2;
	u16 pixel_overlap:3;
	u16 rgb_flip:1;
#define DL_DCS_PORT_A			0x00
#define DL_DCS_PORT_C			0x01
#define DL_DCS_PORT_A_AND_C		0x02
	u16 dl_dcs_cabc_ports:2;
	u16 dl_dcs_backlight_ports:2;
	u16 rsvd3:4;

	u16 rsvd4;

	u8 rsvd5;
	u32 target_burst_mode_freq;
	u32 dsi_ddr_clk;
	u32 bridge_ref_clk;

#define  BYTE_CLK_SEL_20MHZ		0
#define  BYTE_CLK_SEL_10MHZ		1
#define  BYTE_CLK_SEL_5MHZ		2
	u8 byte_clk_sel:2;

	u8 rsvd6:6;

	/* DPHY Flags */
	u16 dphy_param_valid:1;
	u16 eot_pkt_disabled:1;
	u16 enable_clk_stop:1;
	u16 rsvd7:13;

	u32 hs_tx_timeout;
	u32 lp_rx_timeout;
	u32 turn_around_timeout;
	u32 device_reset_timer;
	u32 master_init_timer;
	u32 dbi_bw_timer;
	u32 lp_byte_clk_val;

	/*  4 byte Dphy Params */
	u32 prepare_cnt:6;
	u32 rsvd8:2;
	u32 clk_zero_cnt:8;
	u32 trail_cnt:5;
	u32 rsvd9:3;
	u32 exit_zero_cnt:6;
	u32 rsvd10:2;

	u32 clk_lane_switch_cnt;
	u32 hl_switch_cnt;

	u32 rsvd11[6];

	/* timings based on dphy spec */
	u8 tclk_miss;
	u8 tclk_post;
	u8 rsvd12;
	u8 tclk_pre;
	u8 tclk_prepare;
	u8 tclk_settle;
	u8 tclk_term_enable;
	u8 tclk_trail;
	u16 tclk_prepare_clkzero;
	u8 rsvd13;
	u8 td_term_enable;
	u8 teot;
	u8 ths_exit;
	u8 ths_prepare;
	u16 ths_prepare_hszero;
	u8 rsvd14;
	u8 ths_settle;
	u8 ths_skip;
	u8 ths_trail;
	u8 tinit;
	u8 tlpx;
	u8 rsvd15[3];

	/* GPIOs */
	u8 panel_enable;
	u8 bl_enable;
	u8 pwm_enable;
	u8 reset_r_n;
	u8 pwr_down_r;
	u8 stdby_r_n;

} __packed;

/* all delays have a unit of 100us */
struct mipi_pps_data {
	u16 panel_on_delay;
	u16 bl_enable_delay;
	u16 bl_disable_delay;
	u16 panel_off_delay;
	u16 panel_power_cycle_delay;
} __packed;

void intel_bios_init(struct drm_i915_private *dev_priv);
void intel_bios_init_panel_early(struct drm_i915_private *dev_priv,
				 struct intel_panel *panel,
				 const struct intel_bios_encoder_data *devdata);
void intel_bios_init_panel_late(struct drm_i915_private *dev_priv,
				struct intel_panel *panel,
				const struct intel_bios_encoder_data *devdata,
				const struct drm_edid *drm_edid);
void intel_bios_fini_panel(struct intel_panel *panel);
void intel_bios_driver_remove(struct drm_i915_private *dev_priv);
bool intel_bios_is_valid_vbt(const void *buf, size_t size);
bool intel_bios_is_tv_present(struct drm_i915_private *dev_priv);
bool intel_bios_is_lvds_present(struct drm_i915_private *dev_priv, u8 *i2c_pin);
bool intel_bios_is_port_present(struct drm_i915_private *dev_priv, enum port port);
bool intel_bios_is_port_edp(struct drm_i915_private *dev_priv, enum port port);
bool intel_bios_is_port_dp_dual_mode(struct drm_i915_private *dev_priv, enum port port);
bool intel_bios_is_dsi_present(struct drm_i915_private *dev_priv, enum port *port);
bool intel_bios_get_dsc_params(struct intel_encoder *encoder,
			       struct intel_crtc_state *crtc_state,
			       int dsc_max_bpc);
bool intel_bios_port_supports_typec_usb(struct drm_i915_private *i915, enum port port);
bool intel_bios_port_supports_tbt(struct drm_i915_private *i915, enum port port);

const struct intel_bios_encoder_data *
intel_bios_encoder_data_lookup(struct drm_i915_private *i915, enum port port);

bool intel_bios_encoder_supports_dvi(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_hdmi(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_dp(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_edp(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_typec_usb(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_tbt(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_is_lspcon(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_lane_reversal(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_hpd_invert(const struct intel_bios_encoder_data *devdata);
enum aux_ch intel_bios_dp_aux_ch(const struct intel_bios_encoder_data *devdata);
int intel_bios_dp_boost_level(const struct intel_bios_encoder_data *devdata);
int intel_bios_dp_max_lane_count(const struct intel_bios_encoder_data *devdata);
int intel_bios_dp_max_link_rate(const struct intel_bios_encoder_data *devdata);
int intel_bios_hdmi_boost_level(const struct intel_bios_encoder_data *devdata);
int intel_bios_hdmi_ddc_pin(const struct intel_bios_encoder_data *devdata);
int intel_bios_hdmi_level_shift(const struct intel_bios_encoder_data *devdata);
int intel_bios_hdmi_max_tmds_clock(const struct intel_bios_encoder_data *devdata);

#endif /* _INTEL_BIOS_H_ */
