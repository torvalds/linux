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
#include <linux/hdmi.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_dp_helper.h>

/**
 * _wait_for - magic (register) wait macro
 *
 * Does the right thing for modeset paths when run under kdgb or similar atomic
 * contexts. Note that it's important that we check the condition again after
 * having timed out, since the timeout could be due to preemption or similar and
 * we've never had a chance to check the condition before the timeout.
 */
#define _wait_for(COND, MS, W) ({ \
	unsigned long timeout__ = jiffies + msecs_to_jiffies(MS) + 1;	\
	int ret__ = 0;							\
	while (!(COND)) {						\
		if (time_after(jiffies, timeout__)) {			\
			if (!(COND))					\
				ret__ = -ETIMEDOUT;			\
			break;						\
		}							\
		if (W && drm_can_sleep())  {				\
			msleep(W);					\
		} else {						\
			cpu_relax();					\
		}							\
	}								\
	ret__;								\
})

#define wait_for(COND, MS) _wait_for(COND, MS, 1)
#define wait_for_atomic(COND, MS) _wait_for(COND, MS, 0)
#define wait_for_atomic_us(COND, US) _wait_for((COND), \
					       DIV_ROUND_UP((US), 1000), 0)

#define KHz(x) (1000 * (x))
#define MHz(x) KHz(1000 * (x))

/*
 * Display related stuff
 */

/* store information about an Ixxx DVO */
/* The i830->i865 use multiple DVOs with multiple i2cs */
/* the i915, i945 have a single sDVO i2c bus - which is different */
#define MAX_OUTPUTS 6
/* maximum connectors per crtcs in the mode set */

/* Maximum cursor sizes */
#define GEN2_CURSOR_WIDTH 64
#define GEN2_CURSOR_HEIGHT 64
#define MAX_CURSOR_WIDTH 256
#define MAX_CURSOR_HEIGHT 256

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
#define INTEL_OUTPUT_DSI 9
#define INTEL_OUTPUT_UNKNOWN 10

#define INTEL_DVO_CHIP_NONE 0
#define INTEL_DVO_CHIP_LVDS 1
#define INTEL_DVO_CHIP_TMDS 2
#define INTEL_DVO_CHIP_TVOUT 4

#define INTEL_DSI_VIDEO_MODE	0
#define INTEL_DSI_COMMAND_MODE	1

struct intel_framebuffer {
	struct drm_framebuffer base;
	struct drm_i915_gem_object *obj;
};

struct intel_fbdev {
	struct drm_fb_helper helper;
	struct intel_framebuffer *fb;
	struct list_head fbdev_list;
	struct drm_display_mode *our_mode;
	int preferred_bpp;
};

struct intel_encoder {
	struct drm_encoder base;
	/*
	 * The new crtc this encoder will be driven from. Only differs from
	 * base->crtc while a modeset is in progress.
	 */
	struct intel_crtc *new_crtc;

	int type;
	unsigned int cloneable;
	bool connectors_active;
	void (*hot_plug)(struct intel_encoder *);
	bool (*compute_config)(struct intel_encoder *,
			       struct intel_crtc_config *);
	void (*pre_pll_enable)(struct intel_encoder *);
	void (*pre_enable)(struct intel_encoder *);
	void (*enable)(struct intel_encoder *);
	void (*mode_set)(struct intel_encoder *intel_encoder);
	void (*disable)(struct intel_encoder *);
	void (*post_disable)(struct intel_encoder *);
	/* Read out the current hw state of this connector, returning true if
	 * the encoder is active. If the encoder is enabled it also set the pipe
	 * it is connected to in the pipe parameter. */
	bool (*get_hw_state)(struct intel_encoder *, enum pipe *pipe);
	/* Reconstructs the equivalent mode flags for the current hardware
	 * state. This must be called _after_ display->get_pipe_config has
	 * pre-filled the pipe config. Note that intel_encoder->base.crtc must
	 * be set correctly before calling this function. */
	void (*get_config)(struct intel_encoder *,
			   struct intel_crtc_config *pipe_config);
	int crtc_mask;
	enum hpd_pin hpd_pin;
};

struct intel_panel {
	struct drm_display_mode *fixed_mode;
	struct drm_display_mode *downclock_mode;
	int fitting_mode;

	/* backlight */
	struct {
		bool present;
		u32 level;
		u32 max;
		bool enabled;
		bool combination_mode;	/* gen 2/4 only */
		bool active_low_pwm;
		struct backlight_device *device;
	} backlight;
};

struct intel_connector {
	struct drm_connector base;
	/*
	 * The fixed encoder this connector is connected to.
	 */
	struct intel_encoder *encoder;

	/*
	 * The new encoder this connector will be driven. Only differs from
	 * encoder while a modeset is in progress.
	 */
	struct intel_encoder *new_encoder;

	/* Reads out the current hw, returning true if the connector is enabled
	 * and active (i.e. dpms ON state). */
	bool (*get_hw_state)(struct intel_connector *);

	/*
	 * Removes all interfaces through which the connector is accessible
	 * - like sysfs, debugfs entries -, so that no new operations can be
	 * started on the connector. Also makes sure all currently pending
	 * operations finish before returing.
	 */
	void (*unregister)(struct intel_connector *);

	/* Panel info for eDP and LVDS */
	struct intel_panel panel;

	/* Cached EDID for eDP and LVDS. May hold ERR_PTR for invalid EDID. */
	struct edid *edid;

	/* since POLL and HPD connectors may use the same HPD line keep the native
	   state of connector->polled in case hotplug storm detection changes it */
	u8 polled;
};

typedef struct dpll {
	/* given values */
	int n;
	int m1, m2;
	int p1, p2;
	/* derived values */
	int	dot;
	int	vco;
	int	m;
	int	p;
} intel_clock_t;

struct intel_plane_config {
	bool tiled;
	int size;
	u32 base;
};

struct intel_crtc_config {
	/**
	 * quirks - bitfield with hw state readout quirks
	 *
	 * For various reasons the hw state readout code might not be able to
	 * completely faithfully read out the current state. These cases are
	 * tracked with quirk flags so that fastboot and state checker can act
	 * accordingly.
	 */
#define PIPE_CONFIG_QUIRK_MODE_SYNC_FLAGS	(1<<0) /* unreliable sync mode.flags */
#define PIPE_CONFIG_QUIRK_INHERITED_MODE	(1<<1) /* mode inherited from firmware */
	unsigned long quirks;

	/* User requested mode, only valid as a starting point to
	 * compute adjusted_mode, except in the case of (S)DVO where
	 * it's also for the output timings of the (S)DVO chip.
	 * adjusted_mode will then correspond to the S(DVO) chip's
	 * preferred input timings. */
	struct drm_display_mode requested_mode;
	/* Actual pipe timings ie. what we program into the pipe timing
	 * registers. adjusted_mode.crtc_clock is the pipe pixel clock. */
	struct drm_display_mode adjusted_mode;

	/* Pipe source size (ie. panel fitter input size)
	 * All planes will be positioned inside this space,
	 * and get clipped at the edges. */
	int pipe_src_w, pipe_src_h;

	/* Whether to set up the PCH/FDI. Note that we never allow sharing
	 * between pch encoders and cpu encoders. */
	bool has_pch_encoder;

	/* CPU Transcoder for the pipe. Currently this can only differ from the
	 * pipe on Haswell (where we have a special eDP transcoder). */
	enum transcoder cpu_transcoder;

	/*
	 * Use reduced/limited/broadcast rbg range, compressing from the full
	 * range fed into the crtcs.
	 */
	bool limited_color_range;

	/* DP has a bunch of special case unfortunately, so mark the pipe
	 * accordingly. */
	bool has_dp_encoder;

	/* Whether we should send NULL infoframes. Required for audio. */
	bool has_hdmi_sink;

	/* Audio enabled on this pipe. Only valid if either has_hdmi_sink or
	 * has_dp_encoder is set. */
	bool has_audio;

	/*
	 * Enable dithering, used when the selected pipe bpp doesn't match the
	 * plane bpp.
	 */
	bool dither;

	/* Controls for the clock computation, to override various stages. */
	bool clock_set;

	/* SDVO TV has a bunch of special case. To make multifunction encoders
	 * work correctly, we need to track this at runtime.*/
	bool sdvo_tv_clock;

	/*
	 * crtc bandwidth limit, don't increase pipe bpp or clock if not really
	 * required. This is set in the 2nd loop of calling encoder's
	 * ->compute_config if the first pick doesn't work out.
	 */
	bool bw_constrained;

	/* Settings for the intel dpll used on pretty much everything but
	 * haswell. */
	struct dpll dpll;

	/* Selected dpll when shared or DPLL_ID_PRIVATE. */
	enum intel_dpll_id shared_dpll;

	/* Actual register state of the dpll, for shared dpll cross-checking. */
	struct intel_dpll_hw_state dpll_hw_state;

	int pipe_bpp;
	struct intel_link_m_n dp_m_n;

	/* m2_n2 for eDP downclock */
	struct intel_link_m_n dp_m2_n2;

	/*
	 * Frequence the dpll for the port should run at. Differs from the
	 * adjusted dotclock e.g. for DP or 12bpc hdmi mode. This is also
	 * already multiplied by pixel_multiplier.
	 */
	int port_clock;

	/* Used by SDVO (and if we ever fix it, HDMI). */
	unsigned pixel_multiplier;

	/* Panel fitter controls for gen2-gen4 + VLV */
	struct {
		u32 control;
		u32 pgm_ratios;
		u32 lvds_border_bits;
	} gmch_pfit;

	/* Panel fitter placement and size for Ironlake+ */
	struct {
		u32 pos;
		u32 size;
		bool enabled;
		bool force_thru;
	} pch_pfit;

	/* FDI configuration, only valid if has_pch_encoder is set. */
	int fdi_lanes;
	struct intel_link_m_n fdi_m_n;

	bool ips_enabled;

	bool double_wide;
};

struct intel_pipe_wm {
	struct intel_wm_level wm[5];
	uint32_t linetime;
	bool fbc_wm_enabled;
	bool pipe_enabled;
	bool sprites_enabled;
	bool sprites_scaled;
};

struct intel_mmio_flip {
	u32 seqno;
	u32 ring_id;
};

struct intel_crtc {
	struct drm_crtc base;
	enum pipe pipe;
	enum plane plane;
	u8 lut_r[256], lut_g[256], lut_b[256];
	/*
	 * Whether the crtc and the connected output pipeline is active. Implies
	 * that crtc->enabled is set, i.e. the current mode configuration has
	 * some outputs connected to this crtc.
	 */
	bool active;
	unsigned long enabled_power_domains;
	bool primary_enabled; /* is the primary plane (partially) visible? */
	bool lowfreq_avail;
	struct intel_overlay *overlay;
	struct intel_unpin_work *unpin_work;

	atomic_t unpin_work_count;

	/* Display surface base address adjustement for pageflips. Note that on
	 * gen4+ this only adjusts up to a tile, offsets within a tile are
	 * handled in the hw itself (with the TILEOFF register). */
	unsigned long dspaddr_offset;

	struct drm_i915_gem_object *cursor_bo;
	uint32_t cursor_addr;
	int16_t cursor_width, cursor_height;
	uint32_t cursor_cntl;
	uint32_t cursor_base;

	struct intel_plane_config plane_config;
	struct intel_crtc_config config;
	struct intel_crtc_config *new_config;
	bool new_enabled;

	uint32_t ddi_pll_sel;

	/* reset counter value when the last flip was submitted */
	unsigned int reset_counter;

	/* Access to these should be protected by dev_priv->irq_lock. */
	bool cpu_fifo_underrun_disabled;
	bool pch_fifo_underrun_disabled;

	/* per-pipe watermark state */
	struct {
		/* watermarks currently being used  */
		struct intel_pipe_wm active;
	} wm;

	wait_queue_head_t vbl_wait;

	int scanline_offset;
	struct intel_mmio_flip mmio_flip;
};

struct intel_plane_wm_parameters {
	uint32_t horiz_pixels;
	uint8_t bytes_per_pixel;
	bool enabled;
	bool scaled;
};

struct intel_plane {
	struct drm_plane base;
	int plane;
	enum pipe pipe;
	struct drm_i915_gem_object *obj;
	bool can_scale;
	int max_downscale;
	int crtc_x, crtc_y;
	unsigned int crtc_w, crtc_h;
	uint32_t src_x, src_y;
	uint32_t src_w, src_h;

	/* Since we need to change the watermarks before/after
	 * enabling/disabling the planes, we need to store the parameters here
	 * as the other pieces of the struct may not reflect the values we want
	 * for the watermark calculations. Currently only Haswell uses this.
	 */
	struct intel_plane_wm_parameters wm;

	void (*update_plane)(struct drm_plane *plane,
			     struct drm_crtc *crtc,
			     struct drm_framebuffer *fb,
			     struct drm_i915_gem_object *obj,
			     int crtc_x, int crtc_y,
			     unsigned int crtc_w, unsigned int crtc_h,
			     uint32_t x, uint32_t y,
			     uint32_t src_w, uint32_t src_h);
	void (*disable_plane)(struct drm_plane *plane,
			      struct drm_crtc *crtc);
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
#define intel_fb_obj(x) (x ? to_intel_framebuffer(x)->obj : NULL)

struct intel_hdmi {
	u32 hdmi_reg;
	int ddc_bus;
	uint32_t color_range;
	bool color_range_auto;
	bool has_hdmi_sink;
	bool has_audio;
	enum hdmi_force_audio force_audio;
	bool rgb_quant_range_selectable;
	void (*write_infoframe)(struct drm_encoder *encoder,
				enum hdmi_infoframe_type type,
				const void *frame, ssize_t len);
	void (*set_infoframes)(struct drm_encoder *encoder,
			       bool enable,
			       struct drm_display_mode *adjusted_mode);
};

#define DP_MAX_DOWNSTREAM_PORTS		0x10

/**
 * HIGH_RR is the highest eDP panel refresh rate read from EDID
 * LOW_RR is the lowest eDP panel refresh rate found from EDID
 * parsing for same resolution.
 */
enum edp_drrs_refresh_rate_type {
	DRRS_HIGH_RR,
	DRRS_LOW_RR,
	DRRS_MAX_RR, /* RR count */
};

struct intel_dp {
	uint32_t output_reg;
	uint32_t aux_ch_ctl_reg;
	uint32_t DP;
	bool has_audio;
	enum hdmi_force_audio force_audio;
	uint32_t color_range;
	bool color_range_auto;
	uint8_t link_bw;
	uint8_t lane_count;
	uint8_t dpcd[DP_RECEIVER_CAP_SIZE];
	uint8_t psr_dpcd[EDP_PSR_RECEIVER_CAP_SIZE];
	uint8_t downstream_ports[DP_MAX_DOWNSTREAM_PORTS];
	struct drm_dp_aux aux;
	uint8_t train_set[4];
	int panel_power_up_delay;
	int panel_power_down_delay;
	int panel_power_cycle_delay;
	int backlight_on_delay;
	int backlight_off_delay;
	struct delayed_work panel_vdd_work;
	bool want_panel_vdd;
	unsigned long last_power_cycle;
	unsigned long last_power_on;
	unsigned long last_backlight_off;
	bool use_tps3;
	struct intel_connector *attached_connector;

	uint32_t (*get_aux_clock_divider)(struct intel_dp *dp, int index);
	/*
	 * This function returns the value we have to program the AUX_CTL
	 * register with to kick off an AUX transaction.
	 */
	uint32_t (*get_aux_send_ctl)(struct intel_dp *dp,
				     bool has_aux_irq,
				     int send_bytes,
				     uint32_t aux_clock_divider);
	struct {
		enum drrs_support_type type;
		enum edp_drrs_refresh_rate_type refresh_rate_type;
		struct mutex mutex;
	} drrs_state;

};

struct intel_digital_port {
	struct intel_encoder base;
	enum port port;
	u32 saved_port_bits;
	struct intel_dp dp;
	struct intel_hdmi hdmi;
	bool (*hpd_pulse)(struct intel_digital_port *, bool);
};

static inline int
vlv_dport_to_channel(struct intel_digital_port *dport)
{
	switch (dport->port) {
	case PORT_B:
	case PORT_D:
		return DPIO_CH0;
	case PORT_C:
		return DPIO_CH1;
	default:
		BUG();
	}
}

static inline int
vlv_pipe_to_channel(enum pipe pipe)
{
	switch (pipe) {
	case PIPE_A:
	case PIPE_C:
		return DPIO_CH0;
	case PIPE_B:
		return DPIO_CH1;
	default:
		BUG();
	}
}

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
	struct drm_crtc *crtc;
	struct drm_i915_gem_object *old_fb_obj;
	struct drm_i915_gem_object *pending_flip_obj;
	struct drm_pending_vblank_event *event;
	atomic_t pending;
#define INTEL_FLIP_INACTIVE	0
#define INTEL_FLIP_PENDING	1
#define INTEL_FLIP_COMPLETE	2
	u32 flip_count;
	u32 gtt_offset;
	bool enable_stall_check;
};

struct intel_set_config {
	struct drm_encoder **save_connector_encoders;
	struct drm_crtc **save_encoder_crtcs;
	bool *save_crtc_enabled;

	bool fb_changed;
	bool mode_changed;
};

struct intel_load_detect_pipe {
	struct drm_framebuffer *release_fb;
	bool load_detect_temp;
	int dpms_mode;
};

static inline struct intel_encoder *
intel_attached_encoder(struct drm_connector *connector)
{
	return to_intel_connector(connector)->encoder;
}

static inline struct intel_digital_port *
enc_to_dig_port(struct drm_encoder *encoder)
{
	return container_of(encoder, struct intel_digital_port, base.base);
}

static inline struct intel_dp *enc_to_intel_dp(struct drm_encoder *encoder)
{
	return &enc_to_dig_port(encoder)->dp;
}

static inline struct intel_digital_port *
dp_to_dig_port(struct intel_dp *intel_dp)
{
	return container_of(intel_dp, struct intel_digital_port, dp);
}

static inline struct intel_digital_port *
hdmi_to_dig_port(struct intel_hdmi *intel_hdmi)
{
	return container_of(intel_hdmi, struct intel_digital_port, hdmi);
}


/* i915_irq.c */
bool intel_set_cpu_fifo_underrun_reporting(struct drm_device *dev,
					   enum pipe pipe, bool enable);
bool intel_set_pch_fifo_underrun_reporting(struct drm_device *dev,
					   enum transcoder pch_transcoder,
					   bool enable);
void ilk_enable_gt_irq(struct drm_i915_private *dev_priv, uint32_t mask);
void ilk_disable_gt_irq(struct drm_i915_private *dev_priv, uint32_t mask);
void snb_enable_pm_irq(struct drm_i915_private *dev_priv, uint32_t mask);
void snb_disable_pm_irq(struct drm_i915_private *dev_priv, uint32_t mask);
void bdw_enable_pm_irq(struct drm_i915_private *dev_priv, uint32_t mask);
void bdw_disable_pm_irq(struct drm_i915_private *dev_priv, uint32_t mask);
void intel_runtime_pm_disable_interrupts(struct drm_device *dev);
void intel_runtime_pm_restore_interrupts(struct drm_device *dev);
int intel_get_crtc_scanline(struct intel_crtc *crtc);
void i9xx_check_fifo_underruns(struct drm_device *dev);


/* intel_crt.c */
void intel_crt_init(struct drm_device *dev);


/* intel_ddi.c */
void intel_prepare_ddi(struct drm_device *dev);
void hsw_fdi_link_train(struct drm_crtc *crtc);
void intel_ddi_init(struct drm_device *dev, enum port port);
enum port intel_ddi_get_encoder_port(struct intel_encoder *intel_encoder);
bool intel_ddi_get_hw_state(struct intel_encoder *encoder, enum pipe *pipe);
int intel_ddi_get_cdclk_freq(struct drm_i915_private *dev_priv);
void intel_ddi_pll_init(struct drm_device *dev);
void intel_ddi_enable_transcoder_func(struct drm_crtc *crtc);
void intel_ddi_disable_transcoder_func(struct drm_i915_private *dev_priv,
				       enum transcoder cpu_transcoder);
void intel_ddi_enable_pipe_clock(struct intel_crtc *intel_crtc);
void intel_ddi_disable_pipe_clock(struct intel_crtc *intel_crtc);
void intel_ddi_setup_hw_pll_state(struct drm_device *dev);
bool intel_ddi_pll_select(struct intel_crtc *crtc);
void intel_ddi_pll_enable(struct intel_crtc *crtc);
void intel_ddi_put_crtc_pll(struct drm_crtc *crtc);
void intel_ddi_set_pipe_settings(struct drm_crtc *crtc);
void intel_ddi_prepare_link_retrain(struct drm_encoder *encoder);
bool intel_ddi_connector_get_hw_state(struct intel_connector *intel_connector);
void intel_ddi_fdi_disable(struct drm_crtc *crtc);
void intel_ddi_get_config(struct intel_encoder *encoder,
			  struct intel_crtc_config *pipe_config);


/* intel_display.c */
const char *intel_output_name(int output);
bool intel_has_pending_fb_unpin(struct drm_device *dev);
int intel_pch_rawclk(struct drm_device *dev);
void intel_mark_busy(struct drm_device *dev);
void intel_fb_obj_invalidate(struct drm_i915_gem_object *obj,
			     struct intel_engine_cs *ring);
void intel_frontbuffer_flip_prepare(struct drm_device *dev,
				    unsigned frontbuffer_bits);
void intel_frontbuffer_flip_complete(struct drm_device *dev,
				     unsigned frontbuffer_bits);
void intel_frontbuffer_flush(struct drm_device *dev,
			     unsigned frontbuffer_bits);
/**
 * intel_frontbuffer_flip - prepare frontbuffer flip
 * @dev: DRM device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called after scheduling a flip on @obj. This is for
 * synchronous plane updates which will happen on the next vblank and which will
 * not get delayed by pending gpu rendering.
 *
 * Can be called without any locks held.
 */
static inline
void intel_frontbuffer_flip(struct drm_device *dev,
			    unsigned frontbuffer_bits)
{
	intel_frontbuffer_flush(dev, frontbuffer_bits);
}

void intel_fb_obj_flush(struct drm_i915_gem_object *obj, bool retire);
void intel_mark_idle(struct drm_device *dev);
void intel_crtc_restore_mode(struct drm_crtc *crtc);
void intel_crtc_update_dpms(struct drm_crtc *crtc);
void intel_encoder_destroy(struct drm_encoder *encoder);
void intel_connector_dpms(struct drm_connector *, int mode);
bool intel_connector_get_hw_state(struct intel_connector *connector);
void intel_modeset_check_state(struct drm_device *dev);
bool ibx_digital_port_connected(struct drm_i915_private *dev_priv,
				struct intel_digital_port *port);
void intel_connector_attach_encoder(struct intel_connector *connector,
				    struct intel_encoder *encoder);
struct drm_encoder *intel_best_encoder(struct drm_connector *connector);
struct drm_display_mode *intel_crtc_mode_get(struct drm_device *dev,
					     struct drm_crtc *crtc);
enum pipe intel_get_pipe_from_connector(struct intel_connector *connector);
int intel_get_pipe_from_crtc_id(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
enum transcoder intel_pipe_to_cpu_transcoder(struct drm_i915_private *dev_priv,
					     enum pipe pipe);
void intel_wait_for_vblank(struct drm_device *dev, int pipe);
void intel_wait_for_pipe_off(struct drm_device *dev, int pipe);
int ironlake_get_lanes_required(int target_clock, int link_bw, int bpp);
void vlv_wait_port_ready(struct drm_i915_private *dev_priv,
			 struct intel_digital_port *dport);
bool intel_get_load_detect_pipe(struct drm_connector *connector,
				struct drm_display_mode *mode,
				struct intel_load_detect_pipe *old,
				struct drm_modeset_acquire_ctx *ctx);
void intel_release_load_detect_pipe(struct drm_connector *connector,
				    struct intel_load_detect_pipe *old,
				    struct drm_modeset_acquire_ctx *ctx);
int intel_pin_and_fence_fb_obj(struct drm_device *dev,
			       struct drm_i915_gem_object *obj,
			       struct intel_engine_cs *pipelined);
void intel_unpin_fb_obj(struct drm_i915_gem_object *obj);
struct drm_framebuffer *
__intel_framebuffer_create(struct drm_device *dev,
			   struct drm_mode_fb_cmd2 *mode_cmd,
			   struct drm_i915_gem_object *obj);
void intel_prepare_page_flip(struct drm_device *dev, int plane);
void intel_finish_page_flip(struct drm_device *dev, int pipe);
void intel_finish_page_flip_plane(struct drm_device *dev, int plane);
struct intel_shared_dpll *intel_crtc_to_shared_dpll(struct intel_crtc *crtc);
void assert_shared_dpll(struct drm_i915_private *dev_priv,
			struct intel_shared_dpll *pll,
			bool state);
#define assert_shared_dpll_enabled(d, p) assert_shared_dpll(d, p, true)
#define assert_shared_dpll_disabled(d, p) assert_shared_dpll(d, p, false)
void assert_pll(struct drm_i915_private *dev_priv,
		enum pipe pipe, bool state);
#define assert_pll_enabled(d, p) assert_pll(d, p, true)
#define assert_pll_disabled(d, p) assert_pll(d, p, false)
void assert_fdi_rx_pll(struct drm_i915_private *dev_priv,
		       enum pipe pipe, bool state);
#define assert_fdi_rx_pll_enabled(d, p) assert_fdi_rx_pll(d, p, true)
#define assert_fdi_rx_pll_disabled(d, p) assert_fdi_rx_pll(d, p, false)
void assert_pipe(struct drm_i915_private *dev_priv, enum pipe pipe, bool state);
#define assert_pipe_enabled(d, p) assert_pipe(d, p, true)
#define assert_pipe_disabled(d, p) assert_pipe(d, p, false)
void intel_write_eld(struct drm_encoder *encoder,
		     struct drm_display_mode *mode);
unsigned long intel_gen4_compute_page_offset(int *x, int *y,
					     unsigned int tiling_mode,
					     unsigned int bpp,
					     unsigned int pitch);
void intel_display_handle_reset(struct drm_device *dev);
void hsw_enable_pc8(struct drm_i915_private *dev_priv);
void hsw_disable_pc8(struct drm_i915_private *dev_priv);
void intel_dp_get_m_n(struct intel_crtc *crtc,
		      struct intel_crtc_config *pipe_config);
int intel_dotclock_calculate(int link_freq, const struct intel_link_m_n *m_n);
void
ironlake_check_encoder_dotclock(const struct intel_crtc_config *pipe_config,
				int dotclock);
bool intel_crtc_active(struct drm_crtc *crtc);
void hsw_enable_ips(struct intel_crtc *crtc);
void hsw_disable_ips(struct intel_crtc *crtc);
void intel_display_set_init_power(struct drm_i915_private *dev, bool enable);
enum intel_display_power_domain
intel_display_port_power_domain(struct intel_encoder *intel_encoder);
void intel_mode_from_pipe_config(struct drm_display_mode *mode,
				 struct intel_crtc_config *pipe_config);
int intel_format_to_fourcc(int format);
void intel_crtc_wait_for_pending_flips(struct drm_crtc *crtc);


/* intel_dp.c */
void intel_dp_init(struct drm_device *dev, int output_reg, enum port port);
bool intel_dp_init_connector(struct intel_digital_port *intel_dig_port,
			     struct intel_connector *intel_connector);
void intel_dp_start_link_train(struct intel_dp *intel_dp);
void intel_dp_complete_link_train(struct intel_dp *intel_dp);
void intel_dp_stop_link_train(struct intel_dp *intel_dp);
void intel_dp_sink_dpms(struct intel_dp *intel_dp, int mode);
void intel_dp_encoder_destroy(struct drm_encoder *encoder);
void intel_dp_check_link_status(struct intel_dp *intel_dp);
int intel_dp_sink_crc(struct intel_dp *intel_dp, u8 *crc);
bool intel_dp_compute_config(struct intel_encoder *encoder,
			     struct intel_crtc_config *pipe_config);
bool intel_dp_is_edp(struct drm_device *dev, enum port port);
bool intel_dp_hpd_pulse(struct intel_digital_port *intel_dig_port,
			bool long_hpd);
void intel_edp_backlight_on(struct intel_dp *intel_dp);
void intel_edp_backlight_off(struct intel_dp *intel_dp);
void intel_edp_panel_vdd_on(struct intel_dp *intel_dp);
void intel_edp_panel_on(struct intel_dp *intel_dp);
void intel_edp_panel_off(struct intel_dp *intel_dp);
void intel_edp_psr_enable(struct intel_dp *intel_dp);
void intel_edp_psr_disable(struct intel_dp *intel_dp);
void intel_dp_set_drrs_state(struct drm_device *dev, int refresh_rate);
void intel_edp_psr_exit(struct drm_device *dev);
void intel_edp_psr_init(struct drm_device *dev);

/* intel_dsi.c */
void intel_dsi_init(struct drm_device *dev);


/* intel_dvo.c */
void intel_dvo_init(struct drm_device *dev);


/* legacy fbdev emulation in intel_fbdev.c */
#ifdef CONFIG_DRM_I915_FBDEV
extern int intel_fbdev_init(struct drm_device *dev);
extern void intel_fbdev_initial_config(struct drm_device *dev);
extern void intel_fbdev_fini(struct drm_device *dev);
extern void intel_fbdev_set_suspend(struct drm_device *dev, int state);
extern void intel_fbdev_output_poll_changed(struct drm_device *dev);
extern void intel_fbdev_restore_mode(struct drm_device *dev);
#else
static inline int intel_fbdev_init(struct drm_device *dev)
{
	return 0;
}

static inline void intel_fbdev_initial_config(struct drm_device *dev)
{
}

static inline void intel_fbdev_fini(struct drm_device *dev)
{
}

static inline void intel_fbdev_set_suspend(struct drm_device *dev, int state)
{
}

static inline void intel_fbdev_restore_mode(struct drm_device *dev)
{
}
#endif

/* intel_hdmi.c */
void intel_hdmi_init(struct drm_device *dev, int hdmi_reg, enum port port);
void intel_hdmi_init_connector(struct intel_digital_port *intel_dig_port,
			       struct intel_connector *intel_connector);
struct intel_hdmi *enc_to_intel_hdmi(struct drm_encoder *encoder);
bool intel_hdmi_compute_config(struct intel_encoder *encoder,
			       struct intel_crtc_config *pipe_config);


/* intel_lvds.c */
void intel_lvds_init(struct drm_device *dev);
bool intel_is_dual_link_lvds(struct drm_device *dev);


/* intel_modes.c */
int intel_connector_update_modes(struct drm_connector *connector,
				 struct edid *edid);
int intel_ddc_get_modes(struct drm_connector *c, struct i2c_adapter *adapter);
void intel_attach_force_audio_property(struct drm_connector *connector);
void intel_attach_broadcast_rgb_property(struct drm_connector *connector);


/* intel_overlay.c */
void intel_setup_overlay(struct drm_device *dev);
void intel_cleanup_overlay(struct drm_device *dev);
int intel_overlay_switch_off(struct intel_overlay *overlay);
int intel_overlay_put_image(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
int intel_overlay_attrs(struct drm_device *dev, void *data,
			struct drm_file *file_priv);


/* intel_panel.c */
int intel_panel_init(struct intel_panel *panel,
		     struct drm_display_mode *fixed_mode,
		     struct drm_display_mode *downclock_mode);
void intel_panel_fini(struct intel_panel *panel);
void intel_fixed_panel_mode(const struct drm_display_mode *fixed_mode,
			    struct drm_display_mode *adjusted_mode);
void intel_pch_panel_fitting(struct intel_crtc *crtc,
			     struct intel_crtc_config *pipe_config,
			     int fitting_mode);
void intel_gmch_panel_fitting(struct intel_crtc *crtc,
			      struct intel_crtc_config *pipe_config,
			      int fitting_mode);
void intel_panel_set_backlight(struct intel_connector *connector, u32 level,
			       u32 max);
int intel_panel_setup_backlight(struct drm_connector *connector);
void intel_panel_enable_backlight(struct intel_connector *connector);
void intel_panel_disable_backlight(struct intel_connector *connector);
void intel_panel_destroy_backlight(struct drm_connector *connector);
void intel_panel_init_backlight_funcs(struct drm_device *dev);
enum drm_connector_status intel_panel_detect(struct drm_device *dev);
extern struct drm_display_mode *intel_find_panel_downclock(
				struct drm_device *dev,
				struct drm_display_mode *fixed_mode,
				struct drm_connector *connector);

/* intel_pm.c */
void intel_init_clock_gating(struct drm_device *dev);
void intel_suspend_hw(struct drm_device *dev);
int ilk_wm_max_level(const struct drm_device *dev);
void intel_update_watermarks(struct drm_crtc *crtc);
void intel_update_sprite_watermarks(struct drm_plane *plane,
				    struct drm_crtc *crtc,
				    uint32_t sprite_width, int pixel_size,
				    bool enabled, bool scaled);
void intel_init_pm(struct drm_device *dev);
void intel_pm_setup(struct drm_device *dev);
bool intel_fbc_enabled(struct drm_device *dev);
void intel_update_fbc(struct drm_device *dev);
void intel_gpu_ips_init(struct drm_i915_private *dev_priv);
void intel_gpu_ips_teardown(void);
int intel_power_domains_init(struct drm_i915_private *);
void intel_power_domains_remove(struct drm_i915_private *);
bool intel_display_power_enabled(struct drm_i915_private *dev_priv,
				 enum intel_display_power_domain domain);
bool intel_display_power_enabled_unlocked(struct drm_i915_private *dev_priv,
					  enum intel_display_power_domain domain);
void intel_display_power_get(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain);
void intel_display_power_put(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain);
void intel_power_domains_init_hw(struct drm_i915_private *dev_priv);
void intel_init_gt_powersave(struct drm_device *dev);
void intel_cleanup_gt_powersave(struct drm_device *dev);
void intel_enable_gt_powersave(struct drm_device *dev);
void intel_disable_gt_powersave(struct drm_device *dev);
void intel_suspend_gt_powersave(struct drm_device *dev);
void intel_reset_gt_powersave(struct drm_device *dev);
void ironlake_teardown_rc6(struct drm_device *dev);
void gen6_update_ring_freq(struct drm_device *dev);
void gen6_rps_idle(struct drm_i915_private *dev_priv);
void gen6_rps_boost(struct drm_i915_private *dev_priv);
void intel_aux_display_runtime_get(struct drm_i915_private *dev_priv);
void intel_aux_display_runtime_put(struct drm_i915_private *dev_priv);
void intel_runtime_pm_get(struct drm_i915_private *dev_priv);
void intel_runtime_pm_get_noresume(struct drm_i915_private *dev_priv);
void intel_runtime_pm_put(struct drm_i915_private *dev_priv);
void intel_init_runtime_pm(struct drm_i915_private *dev_priv);
void intel_fini_runtime_pm(struct drm_i915_private *dev_priv);
void ilk_wm_get_hw_state(struct drm_device *dev);


/* intel_sdvo.c */
bool intel_sdvo_init(struct drm_device *dev, uint32_t sdvo_reg, bool is_sdvob);


/* intel_sprite.c */
int intel_plane_init(struct drm_device *dev, enum pipe pipe, int plane);
void intel_flush_primary_plane(struct drm_i915_private *dev_priv,
			       enum plane plane);
void intel_plane_restore(struct drm_plane *plane);
void intel_plane_disable(struct drm_plane *plane);
int intel_sprite_set_colorkey(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
int intel_sprite_get_colorkey(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);


/* intel_tv.c */
void intel_tv_init(struct drm_device *dev);

#endif /* __INTEL_DRV_H__ */
