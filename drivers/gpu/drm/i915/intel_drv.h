/*
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef __INTEL_DRV_H__
#define __INTEL_DRV_H__

#include <linux/i2c.h>
#include "i915_drm.h"
#include "i915_drv.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "drm_fb_helper.h"

#define _wait_for(COND, MS, W) ({ \
	unsigned long timeout__ = jiffies + msecs_to_jiffies(MS);	\
	int ret__ = 0;							\
	while (!(COND)) {						\
		if (time_after(jiffies, timeout__)) {			\
			ret__ = -ETIMEDOUT;				\
			break;						\
		}							\
		if (W && drm_can_sleep()) msleep(W);	\
	}								\
	ret__;								\
})

#define wait_for_atomic_us(COND, US) ({ \
	int i, ret__ = -ETIMEDOUT;	\
	for (i = 0; i < (US); i++) {	\
		if ((COND)) {		\
			ret__ = 0;	\
			break;		\
		}			\
		udelay(1);		\
	}				\
	ret__;				\
})

#define wait_for(COND, MS) _wait_for(COND, MS, 1)
#define wait_for_atomic(COND, MS) _wait_for(COND, MS, 0)

#define KHz(x) (1000*x)
#define MHz(x) KHz(1000*x)

/*
 * Display related stuff
 */

/* store information about an Ixxx DVO */
/* The i830->i865 use multiple DVOs with multiple i2cs */
/* the i915, i945 have a single sDVO i2c bus - which is different */
#define MAX_OUTPUTS 6
/* maximum connectors per crtcs in the mode set */
#define INTELFB_CONN_LIMIT 4

#define INTEL_I2C_BUS_DVO 1
#define INTEL_I2C_BUS_SDVO 2

/* these are outputs from the chip - integrated only
   external chips are via DVO or SDVO output */
#define INTEL_OUTPUT_UNUSED 0
#define INTEL_OUTPUT_ANALOG 1
#define INTEL_OUTPUT_DVO 2
#define INTEL_OUTPUT_SDVO 3
#define INTEL_OUTPUT_LVDS 4
#define INTEL_OUTPUT_TVOUT 5
#define INTEL_OUTPUT_HDMI 6
#define INTEL_OUTPUT_DISPLAYPORT 7
#define INTEL_OUTPUT_EDP 8

/* Intel Pipe Clone Bit */
#define INTEL_HDMIB_CLONE_BIT 1
#define INTEL_HDMIC_CLONE_BIT 2
#define INTEL_HDMID_CLONE_BIT 3
#define INTEL_HDMIE_CLONE_BIT 4
#define INTEL_HDMIF_CLONE_BIT 5
#define INTEL_SDVO_NON_TV_CLONE_BIT 6
#define INTEL_SDVO_TV_CLONE_BIT 7
#define INTEL_SDVO_LVDS_CLONE_BIT 8
#define INTEL_ANALOG_CLONE_BIT 9
#define INTEL_TV_CLONE_BIT 10
#define INTEL_DP_B_CLONE_BIT 11
#define INTEL_DP_C_CLONE_BIT 12
#define INTEL_DP_D_CLONE_BIT 13
#define INTEL_LVDS_CLONE_BIT 14
#define INTEL_DVO_TMDS_CLONE_BIT 15
#define INTEL_DVO_LVDS_CLONE_BIT 16
#define INTEL_EDP_CLONE_BIT 17

#define INTEL_DVO_CHIP_NONE 0
#define INTEL_DVO_CHIP_LVDS 1
#define INTEL_DVO_CHIP_TMDS 2
#define INTEL_DVO_CHIP_TVOUT 4

/* drm_display_mode->private_flags */
#define INTEL_MODE_PIXEL_MULTIPLIER_SHIFT (0x0)
#define INTEL_MODE_PIXEL_MULTIPLIER_MASK (0xf << INTEL_MODE_PIXEL_MULTIPLIER_SHIFT)
#define INTEL_MODE_DP_FORCE_6BPC (0x10)
/* This flag must be set by the encoder's mode_fixup if it changes the crtc
 * timings in the mode to prevent the crtc fixup from overwriting them.
 * Currently only lvds needs that. */
#define INTEL_MODE_CRTC_TIMINGS_SET (0x20)

static inline void
intel_mode_set_pixel_multiplier(struct drm_display_mode *mode,
				int multiplier)
{
	mode->clock *= multiplier;
	mode->private_flags |= multiplier;
}

static inline int
intel_mode_get_pixel_multiplier(const struct drm_display_mode *mode)
{
	return (mode->private_flags & INTEL_MODE_PIXEL_MULTIPLIER_MASK) >> INTEL_MODE_PIXEL_MULTIPLIER_SHIFT;
}

struct intel_framebuffer {
	struct drm_framebuffer base;
	struct drm_i915_gem_object *obj;
};

struct intel_fbdev {
	struct drm_fb_helper helper;
	struct intel_framebuffer ifb;
	struct list_head fbdev_list;
	struct drm_display_mode *our_mode;
};

struct intel_encoder {
	struct drm_encoder base;
	int type;
	bool needs_tv_clock;
	void (*hot_plug)(struct intel_encoder *);
	int crtc_mask;
	int clone_mask;
};

struct intel_connector {
	struct drm_connector base;
	struct intel_encoder *encoder;
};

struct intel_crtc {
	struct drm_crtc base;
	enum pipe pipe;
	enum plane plane;
	u8 lut_r[256], lut_g[256], lut_b[256];
	int dpms_mode;
	bool active; /* is the crtc on? independent of the dpms mode */
	bool busy; /* is scanout buffer being updated frequently? */
	struct timer_list idle_timer;
	bool lowfreq_avail;
	struct intel_overlay *overlay;
	struct intel_unpin_work *unpin_work;
	int fdi_lanes;

	struct drm_i915_gem_object *cursor_bo;
	uint32_t cursor_addr;
	int16_t cursor_x, cursor_y;
	int16_t cursor_width, cursor_height;
	bool cursor_visible;
	unsigned int bpp;

	/* We can share PLLs across outputs if the timings match */
	struct intel_pch_pll *pch_pll;
};

struct intel_plane {
	struct drm_plane base;
	enum pipe pipe;
	struct drm_i915_gem_object *obj;
	bool primary_disabled;
	int max_downscale;
	u32 lut_r[1024], lut_g[1024], lut_b[1024];
	void (*update_plane)(struct drm_plane *plane,
			     struct drm_framebuffer *fb,
			     struct drm_i915_gem_object *obj,
			     int crtc_x, int crtc_y,
			     unsigned int crtc_w, unsigned int crtc_h,
			     uint32_t x, uint32_t y,
			     uint32_t src_w, uint32_t src_h);
	void (*disable_plane)(struct drm_plane *plane);
	int (*update_colorkey)(struct drm_plane *plane,
			       struct drm_intel_sprite_colorkey *key);
	void (*get_colorkey)(struct drm_plane *plane,
			     struct drm_intel_sprite_colorkey *key);
};

struct intel_watermark_params {
	unsigned long fifo_size;
	unsigned long max_wm;
	unsigned long default_wm;
	unsigned long guard_size;
	unsigned long cacheline_size;
};

struct cxsr_latency {
	int is_desktop;
	int is_ddr3;
	unsigned long fsb_freq;
	unsigned long mem_freq;
	unsigned long display_sr;
	unsigned long display_hpll_disable;
	unsigned long cursor_sr;
	unsigned long cursor_hpll_disable;
};

#define to_intel_crtc(x) container_of(x, struct intel_crtc, base)
#define to_intel_connector(x) container_of(x, struct intel_connector, base)
#define to_intel_encoder(x) container_of(x, struct intel_encoder, base)
#define to_intel_framebuffer(x) container_of(x, struct intel_framebuffer, base)
#define to_intel_plane(x) container_of(x, struct intel_plane, base)

#define DIP_HEADER_SIZE	5

#define DIP_TYPE_AVI    0x82
#define DIP_VERSION_AVI 0x2
#define DIP_LEN_AVI     13
#define DIP_AVI_PR_1    0
#define DIP_AVI_PR_2    1

#define DIP_TYPE_SPD	0x83
#define DIP_VERSION_SPD	0x1
#define DIP_LEN_SPD	25
#define DIP_SPD_UNKNOWN	0
#define DIP_SPD_DSTB	0x1
#define DIP_SPD_DVDP	0x2
#define DIP_SPD_DVHS	0x3
#define DIP_SPD_HDDVR	0x4
#define DIP_SPD_DVC	0x5
#define DIP_SPD_DSC	0x6
#define DIP_SPD_VCD	0x7
#define DIP_SPD_GAME	0x8
#define DIP_SPD_PC	0x9
#define DIP_SPD_BD	0xa
#define DIP_SPD_SCD	0xb

struct dip_infoframe {
	uint8_t type;		/* HB0 */
	uint8_t ver;		/* HB1 */
	uint8_t len;		/* HB2 - body len, not including checksum */
	uint8_t ecc;		/* Header ECC */
	uint8_t checksum;	/* PB0 */
	union {
		struct {
			/* PB1 - Y 6:5, A 4:4, B 3:2, S 1:0 */
			uint8_t Y_A_B_S;
			/* PB2 - C 7:6, M 5:4, R 3:0 */
			uint8_t C_M_R;
			/* PB3 - ITC 7:7, EC 6:4, Q 3:2, SC 1:0 */
			uint8_t ITC_EC_Q_SC;
			/* PB4 - VIC 6:0 */
			uint8_t VIC;
			/* PB5 - YQ 7:6, CN 5:4, PR 3:0 */
			uint8_t YQ_CN_PR;
			/* PB6 to PB13 */
			uint16_t top_bar_end;
			uint16_t bottom_bar_start;
			uint16_t left_bar_end;
			uint16_t right_bar_start;
		} avi;
		struct {
			uint8_t vn[8];
			uint8_t pd[16];
			uint8_t sdi;
		} spd;
		uint8_t payload[27];
	} __attribute__ ((packed)) body;
} __attribute__((packed));

struct intel_hdmi {
	struct intel_encoder base;
	u32 sdvox_reg;
	int ddc_bus;
	int ddi_port;
	uint32_t color_range;
	bool has_hdmi_sink;
	bool has_audio;
	enum hdmi_force_audio force_audio;
	void (*write_infoframe)(struct drm_encoder *encoder,
				struct dip_infoframe *frame);
};

static inline struct drm_crtc *
intel_get_crtc_for_pipe(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	return dev_priv->pipe_to_crtc_mapping[pipe];
}

static inline struct drm_crtc *
intel_get_crtc_for_plane(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	return dev_priv->plane_to_crtc_mapping[plane];
}

struct intel_unpin_work {
	struct work_struct work;
	struct drm_device *dev;
	struct drm_i915_gem_object *old_fb_obj;
	struct drm_i915_gem_object *pending_flip_obj;
	struct drm_pending_vblank_event *event;
	int pending;
	bool enable_stall_check;
};

struct intel_fbc_work {
	struct delayed_work work;
	struct drm_crtc *crtc;
	struct drm_framebuffer *fb;
	int interval;
};

int intel_ddc_get_modes(struct drm_connector *c, struct i2c_adapter *adapter);
extern bool intel_ddc_probe(struct intel_encoder *intel_encoder, int ddc_bus);

extern void intel_attach_force_audio_property(struct drm_connector *connector);
extern void intel_attach_broadcast_rgb_property(struct drm_connector *connector);

extern void intel_crt_init(struct drm_device *dev);
extern void intel_hdmi_init(struct drm_device *dev, int sdvox_reg);
extern struct intel_hdmi *enc_to_intel_hdmi(struct drm_encoder *encoder);
extern void intel_hdmi_set_avi_infoframe(struct drm_encoder *encoder,
			    struct drm_display_mode *adjusted_mode);
extern void intel_hdmi_set_spd_infoframe(struct drm_encoder *encoder);
extern void intel_dip_infoframe_csum(struct dip_infoframe *avi_if);
extern bool intel_sdvo_init(struct drm_device *dev, uint32_t sdvo_reg,
			    bool is_sdvob);
extern void intel_dvo_init(struct drm_device *dev);
extern void intel_tv_init(struct drm_device *dev);
extern void intel_mark_busy(struct drm_device *dev,
			    struct drm_i915_gem_object *obj);
extern bool intel_lvds_init(struct drm_device *dev);
extern void intel_dp_init(struct drm_device *dev, int dp_reg);
void
intel_dp_set_m_n(struct drm_crtc *crtc, struct drm_display_mode *mode,
		 struct drm_display_mode *adjusted_mode);
extern bool intel_dpd_is_edp(struct drm_device *dev);
extern void intel_edp_link_config(struct intel_encoder *, int *, int *);
extern bool intel_encoder_is_pch_edp(struct drm_encoder *encoder);
extern int intel_plane_init(struct drm_device *dev, enum pipe pipe);
extern void intel_flush_display_plane(struct drm_i915_private *dev_priv,
				      enum plane plane);

void intel_sanitize_pm(struct drm_device *dev);

/* intel_panel.c */
extern void intel_fixed_panel_mode(struct drm_display_mode *fixed_mode,
				   struct drm_display_mode *adjusted_mode);
extern void intel_pch_panel_fitting(struct drm_device *dev,
				    int fitting_mode,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode);
extern u32 intel_panel_get_max_backlight(struct drm_device *dev);
extern u32 intel_panel_get_backlight(struct drm_device *dev);
extern void intel_panel_set_backlight(struct drm_device *dev, u32 level);
extern int intel_panel_setup_backlight(struct drm_device *dev);
extern void intel_panel_enable_backlight(struct drm_device *dev);
extern void intel_panel_disable_backlight(struct drm_device *dev);
extern void intel_panel_destroy_backlight(struct drm_device *dev);
extern enum drm_connector_status intel_panel_detect(struct drm_device *dev);

extern void intel_crtc_load_lut(struct drm_crtc *crtc);
extern void intel_encoder_prepare(struct drm_encoder *encoder);
extern void intel_encoder_commit(struct drm_encoder *encoder);
extern void intel_encoder_destroy(struct drm_encoder *encoder);

static inline struct intel_encoder *intel_attached_encoder(struct drm_connector *connector)
{
	return to_intel_connector(connector)->encoder;
}

extern void intel_connector_attach_encoder(struct intel_connector *connector,
					   struct intel_encoder *encoder);
extern struct drm_encoder *intel_best_encoder(struct drm_connector *connector);

extern struct drm_display_mode *intel_crtc_mode_get(struct drm_device *dev,
						    struct drm_crtc *crtc);
int intel_get_pipe_from_crtc_id(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
extern void intel_wait_for_vblank(struct drm_device *dev, int pipe);
extern void intel_wait_for_pipe_off(struct drm_device *dev, int pipe);

struct intel_load_detect_pipe {
	struct drm_framebuffer *release_fb;
	bool load_detect_temp;
	int dpms_mode;
};
extern bool intel_get_load_detect_pipe(struct intel_encoder *intel_encoder,
				       struct drm_connector *connector,
				       struct drm_display_mode *mode,
				       struct intel_load_detect_pipe *old);
extern void intel_release_load_detect_pipe(struct intel_encoder *intel_encoder,
					   struct drm_connector *connector,
					   struct intel_load_detect_pipe *old);

extern void intelfb_restore(void);
extern void intel_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
				    u16 blue, int regno);
extern void intel_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
				    u16 *blue, int regno);
extern void intel_enable_clock_gating(struct drm_device *dev);
extern void ironlake_disable_rc6(struct drm_device *dev);
extern void ironlake_enable_drps(struct drm_device *dev);
extern void ironlake_disable_drps(struct drm_device *dev);

extern int intel_pin_and_fence_fb_obj(struct drm_device *dev,
				      struct drm_i915_gem_object *obj,
				      struct intel_ring_buffer *pipelined);
extern void intel_unpin_fb_obj(struct drm_i915_gem_object *obj);

extern int intel_framebuffer_init(struct drm_device *dev,
				  struct intel_framebuffer *ifb,
				  struct drm_mode_fb_cmd2 *mode_cmd,
				  struct drm_i915_gem_object *obj);
extern int intel_fbdev_init(struct drm_device *dev);
extern void intel_fbdev_fini(struct drm_device *dev);
extern void intel_fbdev_set_suspend(struct drm_device *dev, int state);
extern void intel_prepare_page_flip(struct drm_device *dev, int plane);
extern void intel_finish_page_flip(struct drm_device *dev, int pipe);
extern void intel_finish_page_flip_plane(struct drm_device *dev, int plane);

extern void intel_setup_overlay(struct drm_device *dev);
extern void intel_cleanup_overlay(struct drm_device *dev);
extern int intel_overlay_switch_off(struct intel_overlay *overlay);
extern int intel_overlay_put_image(struct drm_device *dev, void *data,
				   struct drm_file *file_priv);
extern int intel_overlay_attrs(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);

extern void intel_fb_output_poll_changed(struct drm_device *dev);
extern void intel_fb_restore_mode(struct drm_device *dev);

extern void assert_pipe(struct drm_i915_private *dev_priv, enum pipe pipe,
			bool state);
#define assert_pipe_enabled(d, p) assert_pipe(d, p, true)
#define assert_pipe_disabled(d, p) assert_pipe(d, p, false)

extern void intel_init_clock_gating(struct drm_device *dev);
extern void intel_write_eld(struct drm_encoder *encoder,
			    struct drm_display_mode *mode);
extern void intel_cpt_verify_modeset(struct drm_device *dev, int pipe);
extern void intel_prepare_ddi(struct drm_device *dev);
extern void hsw_fdi_link_train(struct drm_crtc *crtc);
extern void intel_ddi_init(struct drm_device *dev, enum port port);

/* For use by IVB LP watermark workaround in intel_sprite.c */
extern void intel_update_watermarks(struct drm_device *dev);
extern void intel_update_sprite_watermarks(struct drm_device *dev, int pipe,
					   uint32_t sprite_width,
					   int pixel_size);
extern void intel_update_linetime_watermarks(struct drm_device *dev, int pipe,
			 struct drm_display_mode *mode);

extern int intel_sprite_set_colorkey(struct drm_device *dev, void *data,
				     struct drm_file *file_priv);
extern int intel_sprite_get_colorkey(struct drm_device *dev, void *data,
				     struct drm_file *file_priv);

extern u32 intel_dpio_read(struct drm_i915_private *dev_priv, int reg);

/* Power-related functions, located in intel_pm.c */
extern void intel_init_pm(struct drm_device *dev);
/* FBC */
extern bool intel_fbc_enabled(struct drm_device *dev);
extern void intel_enable_fbc(struct drm_crtc *crtc, unsigned long interval);
extern void intel_update_fbc(struct drm_device *dev);
/* IPS */
extern void intel_gpu_ips_init(struct drm_i915_private *dev_priv);
extern void intel_gpu_ips_teardown(void);

extern void gen6_enable_rps(struct drm_i915_private *dev_priv);
extern void gen6_update_ring_freq(struct drm_i915_private *dev_priv);
extern void gen6_disable_rps(struct drm_device *dev);
extern void intel_init_emon(struct drm_device *dev);

extern void intel_ddi_dpms(struct drm_encoder *encoder, int mode);
extern void intel_ddi_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode);

#endif /* __INTEL_DRV_H__ */
