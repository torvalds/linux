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

#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_dp_mst_helper.h>
#include <drm/drm_fixed.h>
#include <drm/drm_crtc_helper.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

struct radeon_bo;
struct radeon_device;

#define to_radeon_crtc(x) container_of(x, struct radeon_crtc, base)
#define to_radeon_connector(x) container_of(x, struct radeon_connector, base)
#define to_radeon_encoder(x) container_of(x, struct radeon_encoder, base)

#define RADEON_MAX_HPD_PINS 7
#define RADEON_MAX_CRTCS 6
#define RADEON_MAX_AFMT_BLOCKS 7

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

enum radeon_output_csc {
	RADEON_OUTPUT_CSC_BYPASS = 0,
	RADEON_OUTPUT_CSC_TVRGB = 1,
	RADEON_OUTPUT_CSC_YCBCR601 = 2,
	RADEON_OUTPUT_CSC_YCBCR709 = 3,
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
	struct i2c_algo_bit_data bit;
	struct radeon_i2c_bus_rec rec;
	struct drm_dp_aux aux;
	bool has_aux;
	struct mutex mutex;
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
	CT_SAM440EP,
	CT_MAC_G4_SILVER
};

enum radeon_dvo_chip {
	DVO_SIL164,
	DVO_SIL1178,
};

struct radeon_fbdev;

struct radeon_afmt {
	bool enabled;
	int offset;
	bool last_buffer_filled_status;
	int id;
};

struct radeon_mode_info {
	struct atom_context *atom_context;
	struct card_info *atom_card_info;
	enum radeon_connector_table connector_table;
	bool mode_config_initialized;
	struct radeon_crtc *crtcs[RADEON_MAX_CRTCS];
	struct radeon_afmt *afmt[RADEON_MAX_AFMT_BLOCKS];
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
	/* audio */
	struct drm_property *audio_property;
	/* FMT dithering */
	struct drm_property *dither_property;
	/* Output CSC */
	struct drm_property *output_csc_property;
	/* hardcoded DFP edid from BIOS */
	struct edid *bios_hardcoded_edid;
	int bios_hardcoded_edid_size;

	/* pointer to fbdev info structure */
	struct radeon_fbdev *rfbdev;
	/* firmware flags */
	u16 firmware_flags;
	/* pointer to backlight encoder */
	struct radeon_encoder *bl_encoder;

	/* bitmask for active encoder frontends */
	uint32_t active_encoders;
};

#define RADEON_MAX_BL_LEVEL 0xFF

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) || defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

struct radeon_backlight_privdata {
	struct radeon_encoder *encoder;
	uint8_t negative;
};

#endif

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

struct radeon_atom_ss {
	uint16_t percentage;
	uint16_t percentage_divider;
	uint8_t type;
	uint16_t step;
	uint8_t delay;
	uint8_t range;
	uint8_t refdiv;
	/* asic_ss */
	uint16_t rate;
	uint16_t amount;
};

enum radeon_flip_status {
	RADEON_FLIP_NONE,
	RADEON_FLIP_PENDING,
	RADEON_FLIP_SUBMITTED
};

struct radeon_crtc {
	struct drm_crtc base;
	int crtc_id;
	bool enabled;
	bool can_tile;
	bool cursor_out_of_bounds;
	uint32_t crtc_offset;
	struct drm_gem_object *cursor_bo;
	uint64_t cursor_addr;
	int cursor_x;
	int cursor_y;
	int cursor_hot_x;
	int cursor_hot_y;
	int cursor_width;
	int cursor_height;
	int max_cursor_width;
	int max_cursor_height;
	uint32_t legacy_display_base_addr;
	enum radeon_rmx_type rmx_type;
	u8 h_border;
	u8 v_border;
	fixed20_12 vsc;
	fixed20_12 hsc;
	struct drm_display_mode native_mode;
	int pll_id;
	/* page flipping */
	struct workqueue_struct *flip_queue;
	struct radeon_flip_work *flip_work;
	enum radeon_flip_status flip_status;
	/* pll sharing */
	struct radeon_atom_ss ss;
	bool ss_enabled;
	u32 adjusted_clock;
	int bpc;
	u32 pll_reference_div;
	u32 pll_post_div;
	u32 pll_flags;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	/* for dpm */
	u32 line_time;
	u32 wm_low;
	u32 wm_high;
	u32 lb_vblank_lead_lines;
	struct drm_display_mode hw_mode;
	enum radeon_output_csc output_csc;
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
	int panel_mode;
	struct radeon_afmt *afmt;
	struct r600_audio_pin *pin;
	int active_mst_links;
};

struct radeon_encoder_atom_dac {
	enum radeon_tv_std tv_std;
};

struct radeon_encoder_mst {
	int crtc;
	struct radeon_encoder *primary;
	struct radeon_connector *connector;
	struct drm_dp_mst_port *port;
	int pbn;
	int fe;
	bool fe_from_be;
	bool enc_active;
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
	bool is_ext_encoder;
	u16 caps;
	struct radeon_audio_funcs *audio;
	enum radeon_output_csc output_csc;
	bool can_mst;
	uint32_t offset;
	bool is_mst_encoder;
	/* front end for this mst encoder */
};

struct radeon_connector_atom_dig {
	uint32_t igp_lane_info;
	/* displayport */
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	u8 dp_sink_type;
	int dp_clock;
	int dp_lane_count;
	bool edp_on;
	bool is_mst;
};

struct radeon_gpio_rec {
	bool valid;
	u8 id;
	u32 reg;
	u32 mask;
	u32 shift;
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

enum radeon_connector_audio {
	RADEON_AUDIO_DISABLE = 0,
	RADEON_AUDIO_ENABLE = 1,
	RADEON_AUDIO_AUTO = 2
};

enum radeon_connector_dither {
	RADEON_FMT_DITHER_DISABLE = 0,
	RADEON_FMT_DITHER_ENABLE = 1,
};

struct stream_attribs {
	uint16_t fe;
	uint16_t slots;
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
	bool detected_hpd_without_ddc; /* if an HPD signal was detected on DVI, but ddc probing failed */
	uint16_t connector_object_id;
	struct radeon_hpd hpd;
	struct radeon_router router;
	struct radeon_i2c_chan *router_bus;
	enum radeon_connector_audio audio;
	enum radeon_connector_dither dither;
	int pixelclock_for_modeset;
	bool is_mst_connector;
	struct radeon_connector *mst_port;
	struct drm_dp_mst_port *port;
	struct drm_dp_mst_topology_mgr mst_mgr;

	struct radeon_encoder *mst_encoder;
	struct stream_attribs cur_stream_attribs[6];
	int enabled_attribs;
};

#define ENCODER_MODE_IS_DP(em) (((em) == ATOM_ENCODER_MODE_DP) || \
				((em) == ATOM_ENCODER_MODE_DP_MST))

struct atom_clock_dividers {
	u32 post_div;
	union {
		struct {
#ifdef __BIG_ENDIAN
			u32 reserved : 6;
			u32 whole_fb_div : 12;
			u32 frac_fb_div : 14;
#else
			u32 frac_fb_div : 14;
			u32 whole_fb_div : 12;
			u32 reserved : 6;
#endif
		};
		u32 fb_div;
	};
	u32 ref_div;
	bool enable_post_div;
	bool enable_dithen;
	u32 vco_mode;
	u32 real_clock;
	/* added for CI */
	u32 post_divider;
	u32 flags;
};

struct atom_mpll_param {
	union {
		struct {
#ifdef __BIG_ENDIAN
			u32 reserved : 8;
			u32 clkfrac : 12;
			u32 clkf : 12;
#else
			u32 clkf : 12;
			u32 clkfrac : 12;
			u32 reserved : 8;
#endif
		};
		u32 fb_div;
	};
	u32 post_div;
	u32 bwcntl;
	u32 dll_speed;
	u32 vco_mode;
	u32 yclk_sel;
	u32 qdr;
	u32 half_rate;
};

#define MEM_TYPE_GDDR5  0x50
#define MEM_TYPE_GDDR4  0x40
#define MEM_TYPE_GDDR3  0x30
#define MEM_TYPE_DDR2   0x20
#define MEM_TYPE_GDDR1  0x10
#define MEM_TYPE_DDR3   0xb0
#define MEM_TYPE_MASK   0xf0

struct atom_memory_info {
	u8 mem_vendor;
	u8 mem_type;
};

#define MAX_AC_TIMING_ENTRIES 16

struct atom_memory_clock_range_table
{
	u8 num_entries;
	u8 rsv[3];
	u32 mclk[MAX_AC_TIMING_ENTRIES];
};

#define VBIOS_MC_REGISTER_ARRAY_SIZE 32
#define VBIOS_MAX_AC_TIMING_ENTRIES 20

struct atom_mc_reg_entry {
	u32 mclk_max;
	u32 mc_data[VBIOS_MC_REGISTER_ARRAY_SIZE];
};

struct atom_mc_register_address {
	u16 s1;
	u8 pre_reg_data;
};

struct atom_mc_reg_table {
	u8 last;
	u8 num_entries;
	struct atom_mc_reg_entry mc_reg_table_entry[VBIOS_MAX_AC_TIMING_ENTRIES];
	struct atom_mc_register_address mc_reg_address[VBIOS_MC_REGISTER_ARRAY_SIZE];
};

#define MAX_VOLTAGE_ENTRIES 32

struct atom_voltage_table_entry
{
	u16 value;
	u32 smio_low;
};

struct atom_voltage_table
{
	u32 count;
	u32 mask_low;
	u32 phase_delay;
	struct atom_voltage_table_entry entries[MAX_VOLTAGE_ENTRIES];
};

/* Driver internal use only flags of radeon_get_crtc_scanoutpos() */
#define DRM_SCANOUTPOS_VALID        (1 << 0)
#define DRM_SCANOUTPOS_IN_VBLANK    (1 << 1)
#define DRM_SCANOUTPOS_ACCURATE     (1 << 2)
#define USE_REAL_VBLANKSTART 		(1 << 30)
#define GET_DISTANCE_TO_VBLANKSTART	(1 << 31)

extern void
radeon_add_atom_connector(struct drm_device *dev,
			  uint32_t connector_id,
			  uint32_t supported_device,
			  int connector_type,
			  struct radeon_i2c_bus_rec *i2c_bus,
			  uint32_t igp_lane_info,
			  uint16_t connector_object_id,
			  struct radeon_hpd *hpd,
			  struct radeon_router *router);
extern void
radeon_add_legacy_connector(struct drm_device *dev,
			    uint32_t connector_id,
			    uint32_t supported_device,
			    int connector_type,
			    struct radeon_i2c_bus_rec *i2c_bus,
			    uint16_t connector_object_id,
			    struct radeon_hpd *hpd);
extern uint32_t
radeon_get_encoder_enum(struct drm_device *dev, uint32_t supported_device,
			uint8_t dac);
extern void radeon_link_encoder_connector(struct drm_device *dev);

extern enum radeon_tv_std
radeon_combios_get_tv_info(struct radeon_device *rdev);
extern enum radeon_tv_std
radeon_atombios_get_tv_info(struct radeon_device *rdev);
extern void radeon_atombios_get_default_voltages(struct radeon_device *rdev,
						 u16 *vddc, u16 *vddci, u16 *mvdd);

extern void
radeon_combios_connected_scratch_regs(struct drm_connector *connector,
				      struct drm_encoder *encoder,
				      bool connected);
extern void
radeon_atombios_connected_scratch_regs(struct drm_connector *connector,
				       struct drm_encoder *encoder,
				       bool connected);

extern struct drm_connector *
radeon_get_connector_for_encoder(struct drm_encoder *encoder);
extern struct drm_connector *
radeon_get_connector_for_encoder_init(struct drm_encoder *encoder);
extern bool radeon_dig_monitor_is_duallink(struct drm_encoder *encoder,
				    u32 pixel_clock);

extern u16 radeon_encoder_get_dp_bridge_encoder_id(struct drm_encoder *encoder);
extern u16 radeon_connector_encoder_get_dp_bridge_encoder_id(struct drm_connector *connector);
extern bool radeon_connector_is_dp12_capable(struct drm_connector *connector);
extern int radeon_get_monitor_bpc(struct drm_connector *connector);

extern struct edid *radeon_connector_edid(struct drm_connector *connector);

extern void radeon_connector_hotplug(struct drm_connector *connector);
extern int radeon_dp_mode_valid_helper(struct drm_connector *connector,
				       struct drm_display_mode *mode);
extern void radeon_dp_set_link_config(struct drm_connector *connector,
				      const struct drm_display_mode *mode);
extern void radeon_dp_link_train(struct drm_encoder *encoder,
				 struct drm_connector *connector);
extern bool radeon_dp_needs_link_train(struct radeon_connector *radeon_connector);
extern u8 radeon_dp_getsinktype(struct radeon_connector *radeon_connector);
extern bool radeon_dp_getdpcd(struct radeon_connector *radeon_connector);
extern int radeon_dp_get_panel_mode(struct drm_encoder *encoder,
				    struct drm_connector *connector);
extern void radeon_dp_set_rx_power_state(struct drm_connector *connector,
					 u8 power_state);
extern void radeon_dp_aux_init(struct radeon_connector *radeon_connector);
extern ssize_t
radeon_dp_aux_transfer_native(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg);

extern void atombios_dig_encoder_setup(struct drm_encoder *encoder, int action, int panel_mode);
extern void atombios_dig_encoder_setup2(struct drm_encoder *encoder, int action, int panel_mode, int enc_override);
extern void radeon_atom_encoder_init(struct radeon_device *rdev);
extern void radeon_atom_disp_eng_pll_init(struct radeon_device *rdev);
extern void atombios_dig_transmitter_setup(struct drm_encoder *encoder,
					   int action, uint8_t lane_num,
					   uint8_t lane_set);
extern void atombios_dig_transmitter_setup2(struct drm_encoder *encoder,
					    int action, uint8_t lane_num,
					    uint8_t lane_set, int fe);
extern void atombios_set_mst_encoder_crtc_source(struct drm_encoder *encoder,
						 int fe);
extern void radeon_atom_ext_encoder_setup_ddc(struct drm_encoder *encoder);
extern struct drm_encoder *radeon_get_external_encoder(struct drm_encoder *encoder);
void radeon_atom_copy_swap(u8 *dst, u8 *src, u8 num_bytes, bool to_le);

extern void radeon_i2c_init(struct radeon_device *rdev);
extern void radeon_i2c_fini(struct radeon_device *rdev);
extern void radeon_combios_i2c_init(struct radeon_device *rdev);
extern void radeon_atombios_i2c_init(struct radeon_device *rdev);
extern void radeon_i2c_add(struct radeon_device *rdev,
			   struct radeon_i2c_bus_rec *rec,
			   const char *name);
extern struct radeon_i2c_chan *radeon_i2c_lookup(struct radeon_device *rdev,
						 struct radeon_i2c_bus_rec *i2c_bus);
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
extern bool radeon_ddc_probe(struct radeon_connector *radeon_connector, bool use_aux);

extern bool radeon_atombios_get_ppll_ss_info(struct radeon_device *rdev,
					     struct radeon_atom_ss *ss,
					     int id);
extern bool radeon_atombios_get_asic_ss_info(struct radeon_device *rdev,
					     struct radeon_atom_ss *ss,
					     int id, u32 clock);
extern struct radeon_gpio_rec radeon_atombios_lookup_gpio(struct radeon_device *rdev,
							  u8 id);

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
extern bool radeon_encoder_is_digital(struct drm_encoder *encoder);

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
extern int radeon_crtc_cursor_set2(struct drm_crtc *crtc,
				   struct drm_file *file_priv,
				   uint32_t handle,
				   uint32_t width,
				   uint32_t height,
				   int32_t hot_x,
				   int32_t hot_y);
extern int radeon_crtc_cursor_move(struct drm_crtc *crtc,
				   int x, int y);
extern void radeon_cursor_reset(struct drm_crtc *crtc);

extern int radeon_get_crtc_scanoutpos(struct drm_device *dev, unsigned int pipe,
				      unsigned int flags, int *vpos, int *hpos,
				      ktime_t *stime, ktime_t *etime,
				      const struct drm_display_mode *mode);

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
int radeon_framebuffer_init(struct drm_device *dev,
			     struct drm_framebuffer *rfb,
			     const struct drm_mode_fb_cmd2 *mode_cmd,
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
					const struct drm_display_mode *mode,
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

/* fmt blocks */
void avivo_program_fmt(struct drm_encoder *encoder);
void dce3_program_fmt(struct drm_encoder *encoder);
void dce4_program_fmt(struct drm_encoder *encoder);
void dce8_program_fmt(struct drm_encoder *encoder);

/* fbdev layer */
int radeon_fbdev_init(struct radeon_device *rdev);
void radeon_fbdev_fini(struct radeon_device *rdev);
void radeon_fbdev_set_suspend(struct radeon_device *rdev, int state);
bool radeon_fbdev_robj_is_fb(struct radeon_device *rdev, struct radeon_bo *robj);

void radeon_crtc_handle_vblank(struct radeon_device *rdev, int crtc_id);

void radeon_fb_add_connector(struct radeon_device *rdev, struct drm_connector *connector);
void radeon_fb_remove_connector(struct radeon_device *rdev, struct drm_connector *connector);

void radeon_crtc_handle_flip(struct radeon_device *rdev, int crtc_id);

int radeon_align_pitch(struct radeon_device *rdev, int width, int bpp, bool tiled);

/* mst */
int radeon_dp_mst_init(struct radeon_connector *radeon_connector);
int radeon_dp_mst_probe(struct radeon_connector *radeon_connector);
int radeon_dp_mst_check_status(struct radeon_connector *radeon_connector);
int radeon_mst_debugfs_init(struct radeon_device *rdev);
void radeon_dp_mst_prepare_pll(struct drm_crtc *crtc, struct drm_display_mode *mode);

void radeon_setup_mst_connector(struct drm_device *dev);

int radeon_atom_pick_dig_encoder(struct drm_encoder *encoder, int fe_idx);
void radeon_atom_release_dig_encoder(struct radeon_device *rdev, int enc_idx);
#endif
