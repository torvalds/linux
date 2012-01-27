/*
 * Copyright 2000 ATI Technologies Inc., Markham, Ontario, and
 *                VA Linux Systems Inc., Fremont, California.
 * Copyright 2008 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Original Authors:
 *   Kevin E. Martin, Rickard E. Faith, Alan Hourihane
 *
 * Kernel port Author: Dave Airlie
 */

#ifndef RADEON_MODE_H
#define RADEON_MODE_H

#include <drm_crtc.h>
#include <drm_mode.h>
#include <drm_edid.h>
#include <drm_dp_helper.h>
#include <drm_fixed.h>
#include <drm_crtc_helper.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

struct radeon_bo;
struct radeon_device;

#define to_radeon_crtc(x) container_of(x, struct radeon_crtc, base)
#define to_radeon_connector(x) container_of(x, struct radeon_connector, base)
#define to_radeon_encoder(x) container_of(x, struct radeon_encoder, base)
#define to_radeon_framebuffer(x) container_of(x, struct radeon_framebuffer, base)

enum radeon_rmx_type {
	RMX_OFF,
	RMX_FULL,
	RMX_CENTER,
	RMX_ASPECT
};

enum radeon_tv_std {
	TV_STD_NTSC,
	TV_STD_PAL,
	TV_STD_PAL_M,
	TV_STD_PAL_60,
	TV_STD_NTSC_J,
	TV_STD_SCART_PAL,
	TV_STD_SECAM,
	TV_STD_PAL_CN,
	TV_STD_PAL_N,
};

enum radeon_underscan_type {
	UNDERSCAN_OFF,
	UNDERSCAN_ON,
	UNDERSCAN_AUTO,
};

enum radeon_hpd_id {
	RADEON_HPD_1 = 0,
	RADEON_HPD_2,
	RADEON_HPD_3,
	RADEON_HPD_4,
	RADEON_HPD_5,
	RADEON_HPD_6,
	RADEON_HPD_NONE = 0xff,
};

#define RADEON_MAX_I2C_BUS 16

/* radeon gpio-based i2c
 * 1. "mask" reg and bits
 *    grabs the gpio pins for software use
 *    0=not held  1=held
 * 2. "a" reg and bits
 *    output pin value
 *    0=low 1=high
 * 3. "en" reg and bits
 *    sets the pin direction
 *    0=input 1=output
 * 4. "y" reg and bits
 *    input pin value
 *    0=low 1=high
 */
struct radeon_i2c_bus_rec {
	bool valid;
	/* id used by atom */
	uint8_t i2c_id;
	/* id used by atom */
	enum radeon_hpd_id hpd;
	/* can be used with hw i2c engine */
	bool hw_capable;
	/* uses multi-media i2c engine */
	bool mm_i2c;
	/* regs and bits */
	uint32_t mask_clk_reg;
	uint32_t mask_data_reg;
	uint32_t a_clk_reg;
	uint32_t a_data_reg;
	uint32_t en_clk_reg;
	uint32_t en_data_reg;
	uint32_t y_clk_reg;
	uint32_t y_data_reg;
	uint32_t mask_clk_mask;
	uint32_t mask_data_mask;
	uint32_t a_clk_mask;
	uint32_t a_data_mask;
	uint32_t en_clk_mask;
	uint32_t en_data_mask;
	uint32_t y_clk_mask;
	uint32_t y_data_mask;
};

struct radeon_tmds_pll {
    uint32_t freq;
    uint32_t value;
};

#define RADEON_MAX_BIOS_CONNECTOR 16

/* pll flags */
#define RADEON_PLL_USE_BIOS_DIVS        (1 << 0)
#define RADEON_PLL_NO_ODD_POST_DIV      (1 << 1)
#define RADEON_PLL_USE_REF_DIV          (1 << 2)
#define RADEON_PLL_LEGACY               (1 << 3)
#define RADEON_PLL_PREFER_LOW_REF_DIV   (1 << 4)
#define RADEON_PLL_PREFER_HIGH_REF_DIV  (1 << 5)
#define RADEON_PLL_PREFER_LOW_FB_DIV    (1 << 6)
#define RADEON_PLL_PREFER_HIGH_FB_DIV   (1 << 7)
#define RADEON_PLL_PREFER_LOW_POST_DIV  (1 << 8)
#define RADEON_PLL_PREFER_HIGH_POST_DIV (1 << 9)
#define RADEON_PLL_USE_FRAC_FB_DIV      (1 << 10)
#define RADEON_PLL_PREFER_CLOSEST_LOWER (1 << 11)
#define RADEON_PLL_USE_POST_DIV         (1 << 12)
#define RADEON_PLL_IS_LCD               (1 << 13)
#define RADEON_PLL_PREFER_MINM_OVER_MAXP (1 << 14)

struct radeon_pll {
	/* reference frequency */
	uint32_t reference_freq;

	/* fixed dividers */
	uint32_t reference_div;
	uint32_t post_div;

	/* pll in/out limits */
	uint32_t pll_in_min;
	uint32_t pll_in_max;
	uint32_t pll_out_min;
	uint32_t pll_out_max;
	uint32_t lcd_pll_out_min;
	uint32_t lcd_pll_out_max;
	uint32_t best_vco;

	/* divider limits */
	uint32_t min_ref_div;
	uint32_t max_ref_div;
	uint32_t min_post_div;
	uint32_t max_post_div;
	uint32_t min_feedback_div;
	uint32_t max_feedback_div;
	uint32_t min_frac_feedback_div;
	uint32_t max_frac_feedback_div;

	/* flags for the current clock */
	uint32_t flags;

	/* pll id */
	uint32_t id;
};

struct radeon_i2c_chan {
	struct i2c_adapter adapter;
	struct drm_device *dev;
	union {
		struct i2c_algo_bit_data bit;
		struct i2c_algo_dp_aux_data dp;
	} algo;
	struct radeon_i2c_bus_rec rec;
};

/* mostly for macs, but really any system without connector tables */
enum radeon_connector_table {
	CT_NONE = 0,
	CT_GENERIC,
	CT_IBOOK,
	CT_POWERBOOK_EXTERNAL,
	CT_POWERBOOK_INTERNAL,
	CT_POWERBOOK_VGA,
	CT_MINI_EXTERNAL,
	CT_MINI_INTERNAL,
	CT_IMAC_G5_ISIGHT,
	CT_EMAC,
	CT_RN50_POWER,
	CT_MAC_X800,
	CT_MAC_G5_9600,
};

enum radeon_dvo_chip {
	DVO_SIL164,
	DVO_SIL1178,
};

struct radeon_fbdev;

struct radeon_mode_info {
	struct atom_context *atom_context;
	struct card_info *atom_card_info;
	enum radeon_connector_table connector_table;
	bool mode_config_initialized;
	struct radeon_crtc *crtcs[6];
	/* DVI-I properties */
	struct drm_property *coherent_mode_property;
	/* DAC enable load detect */
	struct drm_property *load_detect_property;
	/* TV standard */
	struct drm_property *tv_std_property;
	/* legacy TMDS PLL detect */
	struct drm_property *tmds_pll_property;
	/* underscan */
	struct drm_property *underscan_property;
	struct drm_property *underscan_hborder_property;
	struct drm_property *underscan_vborder_property;
	/* hardcoded DFP edid from BIOS */
	struct edid *bios_hardcoded_edid;
	int bios_hardcoded_edid_size;

	/* pointer to fbdev info structure */
	struct radeon_fbdev *rfbdev;
};

#define MAX_H_CODE_TIMING_LEN 32
#define MAX_V_CODE_TIMING_LEN 32

/* need to store these as reading
   back code tables is excessive */
struct radeon_tv_regs {
	uint32_t tv_uv_adr;
	uint32_t timing_cntl;
	uint32_t hrestart;
	uint32_t vrestart;
	uint32_t frestart;
	uint16_t h_code_timing[MAX_H_CODE_TIMING_LEN];
	uint16_t v_code_timing[MAX_V_CODE_TIMING_LEN];
};

struct radeon_crtc {
	struct drm_crtc base;
	int crtc_id;
	u16 lut_r[256], lut_g[256], lut_b[256];
	bool enabled;
	bool can_tile;
	uint32_t crtc_offset;
	struct drm_gem_object *cursor_bo;
	uint64_t cursor_addr;
	int cursor_width;
	int cursor_height;
	uint32_t legacy_display_base_addr;
	uint32_t legacy_cursor_offset;
	enum radeon_rmx_type rmx_type;
	u8 h_border;
	u8 v_border;
	fixed20_12 vsc;
	fixed20_12 hsc;
	struct drm_display_mode native_mode;
	int pll_id;
	/* page flipping */
	struct radeon_unpin_work *unpin_work;
	int deferred_flip_completion;
};

struct radeon_encoder_primary_dac {
	/* legacy primary dac */
	uint32_t ps2_pdac_adj;
};

struct radeon_encoder_lvds {
	/* legacy lvds */
	uint16_t panel_vcc_delay;
	uint8_t  panel_pwr_delay;
	uint8_t  panel_digon_delay;
	uint8_t  panel_blon_delay;
	uint16_t panel_ref_divider;
	uint8_t  panel_post_divider;
	uint16_t panel_fb_divider;
	bool     use_bios_dividers;
	uint32_t lvds_gen_cntl;
	/* panel mode */
	struct drm_display_mode native_mode;
	struct backlight_device *bl_dev;
	int      dpms_mode;
	uint8_t  backlight_level;
};

struct radeon_encoder_tv_dac {
	/* legacy tv dac */
	uint32_t ps2_tvdac_adj;
	uint32_t ntsc_tvdac_adj;
	uint32_t pal_tvdac_adj;

	int               h_pos;
	int               v_pos;
	int               h_size;
	int               supported_tv_stds;
	bool              tv_on;
	enum radeon_tv_std tv_std;
	struct radeon_tv_regs tv;
};

struct radeon_encoder_int_tmds {
	/* legacy int tmds */
	struct radeon_tmds_pll tmds_pll[4];
};

struct radeon_encoder_ext_tmds {
	/* tmds over dvo */
	struct radeon_i2c_chan *i2c_bus;
	uint8_t slave_addr;
	enum radeon_dvo_chip dvo_chip;
};

/* spread spectrum */
struct radeon_atom_ss {
	uint16_t percentage;
	uint8_t type;
	uint16_t step;
	uint8_t delay;
	uint8_t range;
	uint8_t refdiv;
	/* asic_ss */
	uint16_t rate;
	uint16_t amount;
};

struct radeon_encoder_atom_dig {
	bool linkb;
	/* atom dig */
	bool coherent_mode;
	int dig_encoder; /* -1 disabled, 0 DIGA, 1 DIGB, etc. */
	/* atom lvds/edp */
	uint32_t lcd_misc;
	uint16_t panel_pwr_delay;
	uint32_t lcd_ss_id;
	/* panel mode */
	struct drm_display_mode native_mode;
	struct backlight_device *bl_dev;
	int dpms_mode;
	uint8_t backlight_level;
};

struct radeon_encoder_atom_dac {
	enum radeon_tv_std tv_std;
};

struct radeon_encoder {
	struct drm_encoder base;
	uint32_t encoder_enum;
	uint32_t encoder_id;
	uint32_t devices;
	uint32_t active_device;
	uint32_t flags;
	uint32_t pixel_clock;
	enum radeon_rmx_type rmx_type;
	enum radeon_underscan_type underscan_type;
	uint32_t underscan_hborder;
	uint32_t underscan_vborder;
	struct drm_display_mode native_mode;
	void *enc_priv;
	int audio_polling_active;
	int hdmi_offset;
	int hdmi_config_offset;
	int hdmi_audio_workaround;
	int hdmi_buffer_status;
	bool is_ext_encoder;
	u16 caps;
};

struct radeon_connector_atom_dig {
	uint32_t igp_lane_info;
	/* displayport */
	struct radeon_i2c_chan *dp_i2c_bus;
	u8 dpcd[8];
	u8 dp_sink_type;
	int dp_clock;
	int dp_lane_count;
	bool edp_on;
};

struct radeon_gpio_rec {
	bool valid;
	u8 id;
	u32 reg;
	u32 mask;
};

struct radeon_hpd {
	enum radeon_hpd_id hpd;
	u8 plugged_state;
	struct radeon_gpio_rec gpio;
};

struct radeon_router {
	u32 router_id;
	struct radeon_i2c_bus_rec i2c_info;
	u8 i2c_addr;
	/* i2c mux */
	bool ddc_valid;
	u8 ddc_mux_type;
	u8 ddc_mux_control_pin;
	u8 ddc_mux_state;
	/* clock/data mux */
	bool cd_valid;
	u8 cd_mux_type;
	u8 cd_mux_control_pin;
	u8 cd_mux_state;
};

struct radeon_connector {
	struct drm_connector base;
	uint32_t connector_id;
	uint32_t devices;
	struct radeon_i2c_chan *ddc_bus;
	/* some systems have an hdmi and vga port with a shared ddc line */
	bool shared_ddc;
	bool use_digital;
	/* we need to mind the EDID between detect
	   and get modes due to analog/digital/tvencoder */
	struct edid *edid;
	void *con_priv;
	bool dac_load_detect;
	bool detected_by_load; /* if the connection status was determined by load */
	uint16_t connector_object_id;
	struct radeon_hpd hpd;
	struct radeon_router router;
	struct radeon_i2c_chan *router_bus;
};

struct radeon_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
};

#define ENCODER_MODE_IS_DP(em) (((em) == ATOM_ENCODER_MODE_DP) || \
				((em) == ATOM_ENCODER_MODE_DP_MST))

extern enum radeon_tv_std
radeon_combios_get_tv_info(struct radeon_device *rdev);
extern enum radeon_tv_std
radeon_atombios_get_tv_info(struct radeon_device *rdev);

extern struct drm_connector *
radeon_get_connector_for_encoder(struct drm_encoder *encoder);

extern u16 radeon_encoder_get_dp_bridge_encoder_id(struct drm_encoder *encoder);
extern u16 radeon_connector_encoder_get_dp_bridge_encoder_id(struct drm_connector *connector);
extern bool radeon_connector_encoder_is_hbr2(struct drm_connector *connector);
extern bool radeon_connector_is_dp12_capable(struct drm_connector *connector);

extern void radeon_connector_hotplug(struct drm_connector *connector);
extern int radeon_dp_mode_valid_helper(struct drm_connector *connector,
				       struct drm_display_mode *mode);
extern void radeon_dp_set_link_config(struct drm_connector *connector,
				      struct drm_display_mode *mode);
extern void radeon_dp_link_train(struct drm_encoder *encoder,
				 struct drm_connector *connector);
extern bool radeon_dp_needs_link_train(struct radeon_connector *radeon_connector);
extern u8 radeon_dp_getsinktype(struct radeon_connector *radeon_connector);
extern bool radeon_dp_getdpcd(struct radeon_connector *radeon_connector);
extern void atombios_dig_encoder_setup(struct drm_encoder *encoder, int action, int panel_mode);
extern void radeon_atom_encoder_init(struct radeon_device *rdev);
extern void atombios_dig_transmitter_setup(struct drm_encoder *encoder,
					   int action, uint8_t lane_num,
					   uint8_t lane_set);
extern void radeon_atom_ext_encoder_setup_ddc(struct drm_encoder *encoder);
extern struct drm_encoder *radeon_get_external_encoder(struct drm_encoder *encoder);
extern int radeon_dp_i2c_aux_ch(struct i2c_adapter *adapter, int mode,
				u8 write_byte, u8 *read_byte);

extern void radeon_i2c_init(struct radeon_device *rdev);
extern void radeon_i2c_fini(struct radeon_device *rdev);
extern void radeon_combios_i2c_init(struct radeon_device *rdev);
extern void radeon_atombios_i2c_init(struct radeon_device *rdev);
extern void radeon_i2c_add(struct radeon_device *rdev,
			   struct radeon_i2c_bus_rec *rec,
			   const char *name);
extern struct radeon_i2c_chan *radeon_i2c_lookup(struct radeon_device *rdev,
						 struct radeon_i2c_bus_rec *i2c_bus);
extern struct radeon_i2c_chan *radeon_i2c_create_dp(struct drm_device *dev,
						    struct radeon_i2c_bus_rec *rec,
						    const char *name);
extern struct radeon_i2c_chan *radeon_i2c_create(struct drm_device *dev,
						 struct radeon_i2c_bus_rec *rec,
						 const char *name);
extern void radeon_i2c_destroy(struct radeon_i2c_chan *i2c);
extern void radeon_i2c_get_byte(struct radeon_i2c_chan *i2c_bus,
				u8 slave_addr,
				u8 addr,
				u8 *val);
extern void radeon_i2c_put_byte(struct radeon_i2c_chan *i2c,
				u8 slave_addr,
				u8 addr,
				u8 val);
extern void radeon_router_select_ddc_port(struct radeon_connector *radeon_connector);
extern void radeon_router_select_cd_port(struct radeon_connector *radeon_connector);
extern bool radeon_ddc_probe(struct radeon_connector *radeon_connector);
extern int radeon_ddc_get_modes(struct radeon_connector *radeon_connector);

extern struct drm_encoder *radeon_best_encoder(struct drm_connector *connector);

extern bool radeon_atombios_get_ppll_ss_info(struct radeon_device *rdev,
					     struct radeon_atom_ss *ss,
					     int id);
extern bool radeon_atombios_get_asic_ss_info(struct radeon_device *rdev,
					     struct radeon_atom_ss *ss,
					     int id, u32 clock);

extern void radeon_compute_pll_legacy(struct radeon_pll *pll,
				      uint64_t freq,
				      uint32_t *dot_clock_p,
				      uint32_t *fb_div_p,
				      uint32_t *frac_fb_div_p,
				      uint32_t *ref_div_p,
				      uint32_t *post_div_p);

extern void radeon_compute_pll_avivo(struct radeon_pll *pll,
				     u32 freq,
				     u32 *dot_clock_p,
				     u32 *fb_div_p,
				     u32 *frac_fb_div_p,
				     u32 *ref_div_p,
				     u32 *post_div_p);

extern void radeon_setup_encoder_clones(struct drm_device *dev);

struct drm_encoder *radeon_encoder_legacy_lvds_add(struct drm_device *dev, int bios_index);
struct drm_encoder *radeon_encoder_legacy_primary_dac_add(struct drm_device *dev, int bios_index, int with_tv);
struct drm_encoder *radeon_encoder_legacy_tv_dac_add(struct drm_device *dev, int bios_index, int with_tv);
struct drm_encoder *radeon_encoder_legacy_tmds_int_add(struct drm_device *dev, int bios_index);
struct drm_encoder *radeon_encoder_legacy_tmds_ext_add(struct drm_device *dev, int bios_index);
extern void atombios_dvo_setup(struct drm_encoder *encoder, int action);
extern void atombios_digital_setup(struct drm_encoder *encoder, int action);
extern int atombios_get_encoder_mode(struct drm_encoder *encoder);
extern bool atombios_set_edp_panel_power(struct drm_connector *connector, int action);
extern void radeon_encoder_set_active_device(struct drm_encoder *encoder);

extern void radeon_crtc_load_lut(struct drm_crtc *crtc);
extern int atombios_crtc_set_base(struct drm_crtc *crtc, int x, int y,
				   struct drm_framebuffer *old_fb);
extern int atombios_crtc_set_base_atomic(struct drm_crtc *crtc,
					 struct drm_framebuffer *fb,
					 int x, int y,
					 enum mode_set_atomic state);
extern int atombios_crtc_mode_set(struct drm_crtc *crtc,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode,
				   int x, int y,
				   struct drm_framebuffer *old_fb);
extern void atombios_crtc_dpms(struct drm_crtc *crtc, int mode);

extern int radeon_crtc_set_base(struct drm_crtc *crtc, int x, int y,
				 struct drm_framebuffer *old_fb);
extern int radeon_crtc_set_base_atomic(struct drm_crtc *crtc,
				       struct drm_framebuffer *fb,
				       int x, int y,
				       enum mode_set_atomic state);
extern int radeon_crtc_do_set_base(struct drm_crtc *crtc,
				   struct drm_framebuffer *fb,
				   int x, int y, int atomic);
extern int radeon_crtc_cursor_set(struct drm_crtc *crtc,
				  struct drm_file *file_priv,
				  uint32_t handle,
				  uint32_t width,
				  uint32_t height);
extern int radeon_crtc_cursor_move(struct drm_crtc *crtc,
				   int x, int y);

extern int radeon_get_crtc_scanoutpos(struct drm_device *dev, int crtc,
				      int *vpos, int *hpos);

extern bool radeon_combios_check_hardcoded_edid(struct radeon_device *rdev);
extern struct edid *
radeon_bios_get_hardcoded_edid(struct radeon_device *rdev);
extern bool radeon_atom_get_clock_info(struct drm_device *dev);
extern bool radeon_combios_get_clock_info(struct drm_device *dev);
extern struct radeon_encoder_atom_dig *
radeon_atombios_get_lvds_info(struct radeon_encoder *encoder);
extern bool radeon_atombios_get_tmds_info(struct radeon_encoder *encoder,
					  struct radeon_encoder_int_tmds *tmds);
extern bool radeon_legacy_get_tmds_info_from_combios(struct radeon_encoder *encoder,
						     struct radeon_encoder_int_tmds *tmds);
extern bool radeon_legacy_get_tmds_info_from_table(struct radeon_encoder *encoder,
						   struct radeon_encoder_int_tmds *tmds);
extern bool radeon_legacy_get_ext_tmds_info_from_combios(struct radeon_encoder *encoder,
							 struct radeon_encoder_ext_tmds *tmds);
extern bool radeon_legacy_get_ext_tmds_info_from_table(struct radeon_encoder *encoder,
						       struct radeon_encoder_ext_tmds *tmds);
extern struct radeon_encoder_primary_dac *
radeon_atombios_get_primary_dac_info(struct radeon_encoder *encoder);
extern struct radeon_encoder_tv_dac *
radeon_atombios_get_tv_dac_info(struct radeon_encoder *encoder);
extern struct radeon_encoder_lvds *
radeon_combios_get_lvds_info(struct radeon_encoder *encoder);
extern void radeon_combios_get_ext_tmds_info(struct radeon_encoder *encoder);
extern struct radeon_encoder_tv_dac *
radeon_combios_get_tv_dac_info(struct radeon_encoder *encoder);
extern struct radeon_encoder_primary_dac *
radeon_combios_get_primary_dac_info(struct radeon_encoder *encoder);
extern bool radeon_combios_external_tmds_setup(struct drm_encoder *encoder);
extern void radeon_external_tmds_setup(struct drm_encoder *encoder);
extern void radeon_combios_output_lock(struct drm_encoder *encoder, bool lock);
extern void radeon_combios_initialize_bios_scratch_regs(struct drm_device *dev);
extern void radeon_atom_output_lock(struct drm_encoder *encoder, bool lock);
extern void radeon_atom_initialize_bios_scratch_regs(struct drm_device *dev);
extern void radeon_save_bios_scratch_regs(struct radeon_device *rdev);
extern void radeon_restore_bios_scratch_regs(struct radeon_device *rdev);
extern void
radeon_atombios_encoder_crtc_scratch_regs(struct drm_encoder *encoder, int crtc);
extern void
radeon_atombios_encoder_dpms_scratch_regs(struct drm_encoder *encoder, bool on);
extern void
radeon_combios_encoder_crtc_scratch_regs(struct drm_encoder *encoder, int crtc);
extern void
radeon_combios_encoder_dpms_scratch_regs(struct drm_encoder *encoder, bool on);
extern void radeon_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
				     u16 blue, int regno);
extern void radeon_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
				     u16 *blue, int regno);
void radeon_framebuffer_init(struct drm_device *dev,
			     struct radeon_framebuffer *rfb,
			     struct drm_mode_fb_cmd2 *mode_cmd,
			     struct drm_gem_object *obj);

int radeonfb_remove(struct drm_device *dev, struct drm_framebuffer *fb);
bool radeon_get_legacy_connector_info_from_bios(struct drm_device *dev);
bool radeon_get_legacy_connector_info_from_table(struct drm_device *dev);
void radeon_atombios_init_crtc(struct drm_device *dev,
			       struct radeon_crtc *radeon_crtc);
void radeon_legacy_init_crtc(struct drm_device *dev,
			     struct radeon_crtc *radeon_crtc);

void radeon_get_clock_info(struct drm_device *dev);

extern bool radeon_get_atom_connector_info_from_object_table(struct drm_device *dev);
extern bool radeon_get_atom_connector_info_from_supported_devices_table(struct drm_device *dev);

void radeon_enc_destroy(struct drm_encoder *encoder);
void radeon_copy_fb(struct drm_device *dev, struct drm_gem_object *dst_obj);
void radeon_combios_asic_init(struct drm_device *dev);
bool radeon_crtc_scaling_mode_fixup(struct drm_crtc *crtc,
					struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode);
void radeon_panel_mode_fixup(struct drm_encoder *encoder,
			     struct drm_display_mode *adjusted_mode);
void atom_rv515_force_tv_scaler(struct radeon_device *rdev, struct radeon_crtc *radeon_crtc);

/* legacy tv */
void radeon_legacy_tv_adjust_crtc_reg(struct drm_encoder *encoder,
				      uint32_t *h_total_disp, uint32_t *h_sync_strt_wid,
				      uint32_t *v_total_disp, uint32_t *v_sync_strt_wid);
void radeon_legacy_tv_adjust_pll1(struct drm_encoder *encoder,
				  uint32_t *htotal_cntl, uint32_t *ppll_ref_div,
				  uint32_t *ppll_div_3, uint32_t *pixclks_cntl);
void radeon_legacy_tv_adjust_pll2(struct drm_encoder *encoder,
				  uint32_t *htotal2_cntl, uint32_t *p2pll_ref_div,
				  uint32_t *p2pll_div_0, uint32_t *pixclks_cntl);
void radeon_legacy_tv_mode_set(struct drm_encoder *encoder,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode);

/* fbdev layer */
int radeon_fbdev_init(struct radeon_device *rdev);
void radeon_fbdev_fini(struct radeon_device *rdev);
void radeon_fbdev_set_suspend(struct radeon_device *rdev, int state);
int radeon_fbdev_total_size(struct radeon_device *rdev);
bool radeon_fbdev_robj_is_fb(struct radeon_device *rdev, struct radeon_bo *robj);

void radeon_fb_output_poll_changed(struct radeon_device *rdev);

void radeon_crtc_handle_flip(struct radeon_device *rdev, int crtc_id);

int radeon_align_pitch(struct radeon_device *rdev, int width, int bpp, bool tiled);
#endif
