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
#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/i2c-algo-bit.h>
#include "radeon_fixed.h"

struct radeon_device;

#define to_radeon_crtc(x) container_of(x, struct radeon_crtc, base)
#define to_radeon_connector(x) container_of(x, struct radeon_connector, base)
#define to_radeon_encoder(x) container_of(x, struct radeon_encoder, base)
#define to_radeon_framebuffer(x) container_of(x, struct radeon_framebuffer, base)

enum radeon_connector_type {
	CONNECTOR_NONE,
	CONNECTOR_VGA,
	CONNECTOR_DVI_I,
	CONNECTOR_DVI_D,
	CONNECTOR_DVI_A,
	CONNECTOR_STV,
	CONNECTOR_CTV,
	CONNECTOR_LVDS,
	CONNECTOR_DIGITAL,
	CONNECTOR_SCART,
	CONNECTOR_HDMI_TYPE_A,
	CONNECTOR_HDMI_TYPE_B,
	CONNECTOR_0XC,
	CONNECTOR_0XD,
	CONNECTOR_DIN,
	CONNECTOR_DISPLAY_PORT,
	CONNECTOR_UNSUPPORTED
};

enum radeon_dvi_type {
	DVI_AUTO,
	DVI_DIGITAL,
	DVI_ANALOG
};

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
};

struct radeon_i2c_bus_rec {
	bool valid;
	uint32_t mask_clk_reg;
	uint32_t mask_data_reg;
	uint32_t a_clk_reg;
	uint32_t a_data_reg;
	uint32_t put_clk_reg;
	uint32_t put_data_reg;
	uint32_t get_clk_reg;
	uint32_t get_data_reg;
	uint32_t mask_clk_mask;
	uint32_t mask_data_mask;
	uint32_t put_clk_mask;
	uint32_t put_data_mask;
	uint32_t get_clk_mask;
	uint32_t get_data_mask;
	uint32_t a_clk_mask;
	uint32_t a_data_mask;
};

struct radeon_tmds_pll {
    uint32_t freq;
    uint32_t value;
};

#define RADEON_MAX_BIOS_CONNECTOR 16

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

struct radeon_pll {
	uint16_t reference_freq;
	uint16_t reference_div;
	uint32_t pll_in_min;
	uint32_t pll_in_max;
	uint32_t pll_out_min;
	uint32_t pll_out_max;
	uint16_t xclk;

	uint32_t min_ref_div;
	uint32_t max_ref_div;
	uint32_t min_post_div;
	uint32_t max_post_div;
	uint32_t min_feedback_div;
	uint32_t max_feedback_div;
	uint32_t min_frac_feedback_div;
	uint32_t max_frac_feedback_div;
	uint32_t best_vco;
};

struct radeon_i2c_chan {
	struct drm_device *dev;
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo;
	struct radeon_i2c_bus_rec rec;
};

/* mostly for macs, but really any system without connector tables */
enum radeon_connector_table {
	CT_NONE,
	CT_GENERIC,
	CT_IBOOK,
	CT_POWERBOOK_EXTERNAL,
	CT_POWERBOOK_INTERNAL,
	CT_POWERBOOK_VGA,
	CT_MINI_EXTERNAL,
	CT_MINI_INTERNAL,
	CT_IMAC_G5_ISIGHT,
	CT_EMAC,
};

struct radeon_mode_info {
	struct atom_context *atom_context;
	struct card_info *atom_card_info;
	enum radeon_connector_table connector_table;
	bool mode_config_initialized;
	struct radeon_crtc *crtcs[2];
	/* DVI-I properties */
	struct drm_property *coherent_mode_property;
	/* DAC enable load detect */
	struct drm_property *load_detect_property;
	/* TV standard load detect */
	struct drm_property *tv_std_property;
	/* legacy TMDS PLL detect */
	struct drm_property *tmds_pll_property;

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
	fixed20_12 vsc;
	fixed20_12 hsc;
	struct drm_display_mode native_mode;
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

/* spread spectrum */
struct radeon_atom_ss {
	uint16_t percentage;
	uint8_t type;
	uint8_t step;
	uint8_t delay;
	uint8_t range;
	uint8_t refdiv;
};

struct radeon_encoder_atom_dig {
	/* atom dig */
	bool coherent_mode;
	int dig_block;
	/* atom lvds */
	uint32_t lvds_misc;
	uint16_t panel_pwr_delay;
	struct radeon_atom_ss *ss;
	/* panel mode */
	struct drm_display_mode native_mode;
};

struct radeon_encoder_atom_dac {
	enum radeon_tv_std tv_std;
};

struct radeon_encoder {
	struct drm_encoder base;
	uint32_t encoder_id;
	uint32_t devices;
	uint32_t active_device;
	uint32_t flags;
	uint32_t pixel_clock;
	enum radeon_rmx_type rmx_type;
	struct drm_display_mode native_mode;
	void *enc_priv;
};

struct radeon_connector_atom_dig {
	uint32_t igp_lane_info;
	bool linkb;
};

struct radeon_connector {
	struct drm_connector base;
	uint32_t connector_id;
	uint32_t devices;
	struct radeon_i2c_chan *ddc_bus;
	/* some systems have a an hdmi and vga port with a shared ddc line */
	bool shared_ddc;
	bool use_digital;
	/* we need to mind the EDID between detect
	   and get modes due to analog/digital/tvencoder */
	struct edid *edid;
	void *con_priv;
	bool dac_load_detect;
	uint16_t connector_object_id;
};

struct radeon_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
};

extern struct radeon_i2c_chan *radeon_i2c_create(struct drm_device *dev,
						 struct radeon_i2c_bus_rec *rec,
						 const char *name);
extern void radeon_i2c_destroy(struct radeon_i2c_chan *i2c);
extern bool radeon_ddc_probe(struct radeon_connector *radeon_connector);
extern int radeon_ddc_get_modes(struct radeon_connector *radeon_connector);

extern struct drm_encoder *radeon_best_encoder(struct drm_connector *connector);

extern void radeon_compute_pll(struct radeon_pll *pll,
			       uint64_t freq,
			       uint32_t *dot_clock_p,
			       uint32_t *fb_div_p,
			       uint32_t *frac_fb_div_p,
			       uint32_t *ref_div_p,
			       uint32_t *post_div_p,
			       int flags);

struct drm_encoder *radeon_encoder_legacy_lvds_add(struct drm_device *dev, int bios_index);
struct drm_encoder *radeon_encoder_legacy_primary_dac_add(struct drm_device *dev, int bios_index, int with_tv);
struct drm_encoder *radeon_encoder_legacy_tv_dac_add(struct drm_device *dev, int bios_index, int with_tv);
struct drm_encoder *radeon_encoder_legacy_tmds_int_add(struct drm_device *dev, int bios_index);
struct drm_encoder *radeon_encoder_legacy_tmds_ext_add(struct drm_device *dev, int bios_index);
extern void atombios_external_tmds_setup(struct drm_encoder *encoder, int action);
extern int atombios_get_encoder_mode(struct drm_encoder *encoder);
extern void radeon_encoder_set_active_device(struct drm_encoder *encoder);

extern void radeon_crtc_load_lut(struct drm_crtc *crtc);
extern int atombios_crtc_set_base(struct drm_crtc *crtc, int x, int y,
				   struct drm_framebuffer *old_fb);
extern int atombios_crtc_mode_set(struct drm_crtc *crtc,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode,
				   int x, int y,
				   struct drm_framebuffer *old_fb);
extern void atombios_crtc_dpms(struct drm_crtc *crtc, int mode);

extern int radeon_crtc_set_base(struct drm_crtc *crtc, int x, int y,
				 struct drm_framebuffer *old_fb);
extern void radeon_legacy_atom_set_surface(struct drm_crtc *crtc);

extern int radeon_crtc_cursor_set(struct drm_crtc *crtc,
				  struct drm_file *file_priv,
				  uint32_t handle,
				  uint32_t width,
				  uint32_t height);
extern int radeon_crtc_cursor_move(struct drm_crtc *crtc,
				   int x, int y);

extern bool radeon_atom_get_clock_info(struct drm_device *dev);
extern bool radeon_combios_get_clock_info(struct drm_device *dev);
extern struct radeon_encoder_atom_dig *
radeon_atombios_get_lvds_info(struct radeon_encoder *encoder);
bool radeon_atombios_get_tmds_info(struct radeon_encoder *encoder,
				   struct radeon_encoder_int_tmds *tmds);
bool radeon_legacy_get_tmds_info_from_combios(struct radeon_encoder *encoder,
					   struct radeon_encoder_int_tmds *tmds);
bool radeon_legacy_get_tmds_info_from_table(struct radeon_encoder *encoder,
					    struct radeon_encoder_int_tmds *tmds);
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
struct drm_framebuffer *radeon_framebuffer_create(struct drm_device *dev,
						  struct drm_mode_fb_cmd *mode_cmd,
						  struct drm_gem_object *obj);

int radeonfb_probe(struct drm_device *dev);

int radeonfb_remove(struct drm_device *dev, struct drm_framebuffer *fb);
bool radeon_get_legacy_connector_info_from_bios(struct drm_device *dev);
bool radeon_get_legacy_connector_info_from_table(struct drm_device *dev);
void radeon_atombios_init_crtc(struct drm_device *dev,
			       struct radeon_crtc *radeon_crtc);
void radeon_legacy_init_crtc(struct drm_device *dev,
			     struct radeon_crtc *radeon_crtc);
void radeon_i2c_do_lock(struct radeon_connector *radeon_connector, int lock_state);

void radeon_get_clock_info(struct drm_device *dev);

extern bool radeon_get_atom_connector_info_from_object_table(struct drm_device *dev);
extern bool radeon_get_atom_connector_info_from_supported_devices_table(struct drm_device *dev);

void radeon_rmx_mode_fixup(struct drm_encoder *encoder,
			   struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);
void radeon_enc_destroy(struct drm_encoder *encoder);
void radeon_copy_fb(struct drm_device *dev, struct drm_gem_object *dst_obj);
void radeon_combios_asic_init(struct drm_device *dev);
extern int radeon_static_clocks_init(struct drm_device *dev);
bool radeon_crtc_scaling_mode_fixup(struct drm_crtc *crtc,
					struct drm_display_mode *mode,
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
#endif
