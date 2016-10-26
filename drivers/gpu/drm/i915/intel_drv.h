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

#include <linux/async.h>
#include <linux/i2c.h>
#include <linux/hdmi.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_dp_dual_mode_helper.h>
#include <drm/drm_dp_mst_helper.h>
#include <drm/drm_rect.h>
#include <drm/drm_atomic.h>

/**
 * _wait_for - magic (register) wait macro
 *
 * Does the right thing for modeset paths when run under kdgb or similar atomic
 * contexts. Note that it's important that we check the condition again after
 * having timed out, since the timeout could be due to preemption or similar and
 * we've never had a chance to check the condition before the timeout.
 *
 * TODO: When modesetting has fully transitioned to atomic, the below
 * drm_can_sleep() can be removed and in_atomic()/!in_atomic() asserts
 * added.
 */
#define _wait_for(COND, US, W) ({ \
	unsigned long timeout__ = jiffies + usecs_to_jiffies(US) + 1;	\
	int ret__;							\
	for (;;) {							\
		bool expired__ = time_after(jiffies, timeout__);	\
		if (COND) {						\
			ret__ = 0;					\
			break;						\
		}							\
		if (expired__) {					\
			ret__ = -ETIMEDOUT;				\
			break;						\
		}							\
		if ((W) && drm_can_sleep()) {				\
			usleep_range((W), (W)*2);			\
		} else {						\
			cpu_relax();					\
		}							\
	}								\
	ret__;								\
})

#define wait_for(COND, MS)	  	_wait_for((COND), (MS) * 1000, 1000)

/* If CONFIG_PREEMPT_COUNT is disabled, in_atomic() always reports false. */
#if defined(CONFIG_DRM_I915_DEBUG) && defined(CONFIG_PREEMPT_COUNT)
# define _WAIT_FOR_ATOMIC_CHECK(ATOMIC) WARN_ON_ONCE((ATOMIC) && !in_atomic())
#else
# define _WAIT_FOR_ATOMIC_CHECK(ATOMIC) do { } while (0)
#endif

#define _wait_for_atomic(COND, US, ATOMIC) \
({ \
	int cpu, ret, timeout = (US) * 1000; \
	u64 base; \
	_WAIT_FOR_ATOMIC_CHECK(ATOMIC); \
	BUILD_BUG_ON((US) > 50000); \
	if (!(ATOMIC)) { \
		preempt_disable(); \
		cpu = smp_processor_id(); \
	} \
	base = local_clock(); \
	for (;;) { \
		u64 now = local_clock(); \
		if (!(ATOMIC)) \
			preempt_enable(); \
		if (COND) { \
			ret = 0; \
			break; \
		} \
		if (now - base >= timeout) { \
			ret = -ETIMEDOUT; \
			break; \
		} \
		cpu_relax(); \
		if (!(ATOMIC)) { \
			preempt_disable(); \
			if (unlikely(cpu != smp_processor_id())) { \
				timeout -= now - base; \
				cpu = smp_processor_id(); \
				base = local_clock(); \
			} \
		} \
	} \
	ret; \
})

#define wait_for_us(COND, US) \
({ \
	int ret__; \
	BUILD_BUG_ON(!__builtin_constant_p(US)); \
	if ((US) > 10) \
		ret__ = _wait_for((COND), (US), 10); \
	else \
		ret__ = _wait_for_atomic((COND), (US), 0); \
	ret__; \
})

#define wait_for_atomic(COND, MS)	_wait_for_atomic((COND), (MS) * 1000, 1)
#define wait_for_atomic_us(COND, US)	_wait_for_atomic((COND), (US), 1)

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
enum intel_output_type {
	INTEL_OUTPUT_UNUSED = 0,
	INTEL_OUTPUT_ANALOG = 1,
	INTEL_OUTPUT_DVO = 2,
	INTEL_OUTPUT_SDVO = 3,
	INTEL_OUTPUT_LVDS = 4,
	INTEL_OUTPUT_TVOUT = 5,
	INTEL_OUTPUT_HDMI = 6,
	INTEL_OUTPUT_DP = 7,
	INTEL_OUTPUT_EDP = 8,
	INTEL_OUTPUT_DSI = 9,
	INTEL_OUTPUT_UNKNOWN = 10,
	INTEL_OUTPUT_DP_MST = 11,
};

#define INTEL_DVO_CHIP_NONE 0
#define INTEL_DVO_CHIP_LVDS 1
#define INTEL_DVO_CHIP_TMDS 2
#define INTEL_DVO_CHIP_TVOUT 4

#define INTEL_DSI_VIDEO_MODE	0
#define INTEL_DSI_COMMAND_MODE	1

struct intel_framebuffer {
	struct drm_framebuffer base;
	struct drm_i915_gem_object *obj;
	struct intel_rotation_info rot_info;

	/* for each plane in the normal GTT view */
	struct {
		unsigned int x, y;
	} normal[2];
	/* for each plane in the rotated GTT view */
	struct {
		unsigned int x, y;
		unsigned int pitch; /* pixels */
	} rotated[2];
};

struct intel_fbdev {
	struct drm_fb_helper helper;
	struct intel_framebuffer *fb;
	struct i915_vma *vma;
	async_cookie_t cookie;
	int preferred_bpp;
};

struct intel_encoder {
	struct drm_encoder base;

	enum intel_output_type type;
	enum port port;
	unsigned int cloneable;
	void (*hot_plug)(struct intel_encoder *);
	bool (*compute_config)(struct intel_encoder *,
			       struct intel_crtc_state *,
			       struct drm_connector_state *);
	void (*pre_pll_enable)(struct intel_encoder *,
			       struct intel_crtc_state *,
			       struct drm_connector_state *);
	void (*pre_enable)(struct intel_encoder *,
			   struct intel_crtc_state *,
			   struct drm_connector_state *);
	void (*enable)(struct intel_encoder *,
		       struct intel_crtc_state *,
		       struct drm_connector_state *);
	void (*disable)(struct intel_encoder *,
			struct intel_crtc_state *,
			struct drm_connector_state *);
	void (*post_disable)(struct intel_encoder *,
			     struct intel_crtc_state *,
			     struct drm_connector_state *);
	void (*post_pll_disable)(struct intel_encoder *,
				 struct intel_crtc_state *,
				 struct drm_connector_state *);
	/* Read out the current hw state of this connector, returning true if
	 * the encoder is active. If the encoder is enabled it also set the pipe
	 * it is connected to in the pipe parameter. */
	bool (*get_hw_state)(struct intel_encoder *, enum pipe *pipe);
	/* Reconstructs the equivalent mode flags for the current hardware
	 * state. This must be called _after_ display->get_pipe_config has
	 * pre-filled the pipe config. Note that intel_encoder->base.crtc must
	 * be set correctly before calling this function. */
	void (*get_config)(struct intel_encoder *,
			   struct intel_crtc_state *pipe_config);
	/*
	 * Called during system suspend after all pending requests for the
	 * encoder are flushed (for example for DP AUX transactions) and
	 * device interrupts are disabled.
	 */
	void (*suspend)(struct intel_encoder *);
	int crtc_mask;
	enum hpd_pin hpd_pin;
	/* for communication with audio component; protected by av_mutex */
	const struct drm_connector *audio_connector;
};

struct intel_panel {
	struct drm_display_mode *fixed_mode;
	struct drm_display_mode *downclock_mode;
	int fitting_mode;

	/* backlight */
	struct {
		bool present;
		u32 level;
		u32 min;
		u32 max;
		bool enabled;
		bool combination_mode;	/* gen 2/4 only */
		bool active_low_pwm;
		bool alternate_pwm_increment;	/* lpt+ */

		/* PWM chip */
		bool util_pin_active_low;	/* bxt+ */
		u8 controller;		/* bxt+ only */
		struct pwm_device *pwm;

		struct backlight_device *device;

		/* Connector and platform specific backlight functions */
		int (*setup)(struct intel_connector *connector, enum pipe pipe);
		uint32_t (*get)(struct intel_connector *connector);
		void (*set)(struct intel_connector *connector, uint32_t level);
		void (*disable)(struct intel_connector *connector);
		void (*enable)(struct intel_connector *connector);
		uint32_t (*hz_to_pwm)(struct intel_connector *connector,
				      uint32_t hz);
		void (*power)(struct intel_connector *, bool enable);
	} backlight;
};

struct intel_connector {
	struct drm_connector base;
	/*
	 * The fixed encoder this connector is connected to.
	 */
	struct intel_encoder *encoder;

	/* Reads out the current hw, returning true if the connector is enabled
	 * and active (i.e. dpms ON state). */
	bool (*get_hw_state)(struct intel_connector *);

	/* Panel info for eDP and LVDS */
	struct intel_panel panel;

	/* Cached EDID for eDP and LVDS. May hold ERR_PTR for invalid EDID. */
	struct edid *edid;
	struct edid *detect_edid;

	/* since POLL and HPD connectors may use the same HPD line keep the native
	   state of connector->polled in case hotplug storm detection changes it */
	u8 polled;

	void *port; /* store this opaque as its illegal to dereference it */

	struct intel_dp *mst_port;
};

struct dpll {
	/* given values */
	int n;
	int m1, m2;
	int p1, p2;
	/* derived values */
	int	dot;
	int	vco;
	int	m;
	int	p;
};

struct intel_atomic_state {
	struct drm_atomic_state base;

	unsigned int cdclk;

	/*
	 * Calculated device cdclk, can be different from cdclk
	 * only when all crtc's are DPMS off.
	 */
	unsigned int dev_cdclk;

	bool dpll_set, modeset;

	/*
	 * Does this transaction change the pipes that are active?  This mask
	 * tracks which CRTC's have changed their active state at the end of
	 * the transaction (not counting the temporary disable during modesets).
	 * This mask should only be non-zero when intel_state->modeset is true,
	 * but the converse is not necessarily true; simply changing a mode may
	 * not flip the final active status of any CRTC's
	 */
	unsigned int active_pipe_changes;

	unsigned int active_crtcs;
	unsigned int min_pixclk[I915_MAX_PIPES];

	/* SKL/KBL Only */
	unsigned int cdclk_pll_vco;

	struct intel_shared_dpll_config shared_dpll[I915_NUM_PLLS];

	/*
	 * Current watermarks can't be trusted during hardware readout, so
	 * don't bother calculating intermediate watermarks.
	 */
	bool skip_intermediate_wm;

	/* Gen9+ only */
	struct skl_wm_values wm_results;
};

struct intel_plane_state {
	struct drm_plane_state base;
	struct drm_rect clip;

	struct {
		u32 offset;
		int x, y;
	} main;
	struct {
		u32 offset;
		int x, y;
	} aux;

	/*
	 * scaler_id
	 *    = -1 : not using a scaler
	 *    >=  0 : using a scalers
	 *
	 * plane requiring a scaler:
	 *   - During check_plane, its bit is set in
	 *     crtc_state->scaler_state.scaler_users by calling helper function
	 *     update_scaler_plane.
	 *   - scaler_id indicates the scaler it got assigned.
	 *
	 * plane doesn't require a scaler:
	 *   - this can happen when scaling is no more required or plane simply
	 *     got disabled.
	 *   - During check_plane, corresponding bit is reset in
	 *     crtc_state->scaler_state.scaler_users by calling helper function
	 *     update_scaler_plane.
	 */
	int scaler_id;

	struct drm_intel_sprite_colorkey ckey;

	/* async flip related structures */
	struct drm_i915_gem_request *wait_req;
};

struct intel_initial_plane_config {
	struct intel_framebuffer *fb;
	unsigned int tiling;
	int size;
	u32 base;
};

#define SKL_MIN_SRC_W 8
#define SKL_MAX_SRC_W 4096
#define SKL_MIN_SRC_H 8
#define SKL_MAX_SRC_H 4096
#define SKL_MIN_DST_W 8
#define SKL_MAX_DST_W 4096
#define SKL_MIN_DST_H 8
#define SKL_MAX_DST_H 4096

struct intel_scaler {
	int in_use;
	uint32_t mode;
};

struct intel_crtc_scaler_state {
#define SKL_NUM_SCALERS 2
	struct intel_scaler scalers[SKL_NUM_SCALERS];

	/*
	 * scaler_users: keeps track of users requesting scalers on this crtc.
	 *
	 *     If a bit is set, a user is using a scaler.
	 *     Here user can be a plane or crtc as defined below:
	 *       bits 0-30 - plane (bit position is index from drm_plane_index)
	 *       bit 31    - crtc
	 *
	 * Instead of creating a new index to cover planes and crtc, using
	 * existing drm_plane_index for planes which is well less than 31
	 * planes and bit 31 for crtc. This should be fine to cover all
	 * our platforms.
	 *
	 * intel_atomic_setup_scalers will setup available scalers to users
	 * requesting scalers. It will gracefully fail if request exceeds
	 * avilability.
	 */
#define SKL_CRTC_INDEX 31
	unsigned scaler_users;

	/* scaler used by crtc for panel fitting purpose */
	int scaler_id;
};

/* drm_mode->private_flags */
#define I915_MODE_FLAG_INHERITED 1

struct intel_pipe_wm {
	struct intel_wm_level wm[5];
	struct intel_wm_level raw_wm[5];
	uint32_t linetime;
	bool fbc_wm_enabled;
	bool pipe_enabled;
	bool sprites_enabled;
	bool sprites_scaled;
};

struct skl_plane_wm {
	struct skl_wm_level wm[8];
	struct skl_wm_level trans_wm;
};

struct skl_pipe_wm {
	struct skl_plane_wm planes[I915_MAX_PLANES];
	uint32_t linetime;
};

struct intel_crtc_wm_state {
	union {
		struct {
			/*
			 * Intermediate watermarks; these can be
			 * programmed immediately since they satisfy
			 * both the current configuration we're
			 * switching away from and the new
			 * configuration we're switching to.
			 */
			struct intel_pipe_wm intermediate;

			/*
			 * Optimal watermarks, programmed post-vblank
			 * when this state is committed.
			 */
			struct intel_pipe_wm optimal;
		} ilk;

		struct {
			/* gen9+ only needs 1-step wm programming */
			struct skl_pipe_wm optimal;
			struct skl_ddb_entry ddb;

			/* cached plane data rate */
			unsigned plane_data_rate[I915_MAX_PLANES];
			unsigned plane_y_data_rate[I915_MAX_PLANES];

			/* minimum block allocation */
			uint16_t minimum_blocks[I915_MAX_PLANES];
			uint16_t minimum_y_blocks[I915_MAX_PLANES];
		} skl;
	};

	/*
	 * Platforms with two-step watermark programming will need to
	 * update watermark programming post-vblank to switch from the
	 * safe intermediate watermarks to the optimal final
	 * watermarks.
	 */
	bool need_postvbl_update;
};

struct intel_crtc_state {
	struct drm_crtc_state base;

	/**
	 * quirks - bitfield with hw state readout quirks
	 *
	 * For various reasons the hw state readout code might not be able to
	 * completely faithfully read out the current state. These cases are
	 * tracked with quirk flags so that fastboot and state checker can act
	 * accordingly.
	 */
#define PIPE_CONFIG_QUIRK_MODE_SYNC_FLAGS	(1<<0) /* unreliable sync mode.flags */
	unsigned long quirks;

	unsigned fb_bits; /* framebuffers to flip */
	bool update_pipe; /* can a fast modeset be performed? */
	bool disable_cxsr;
	bool update_wm_pre, update_wm_post; /* watermarks are updated */
	bool fb_changed; /* fb on any of the planes is changed */

	/* Pipe source size (ie. panel fitter input size)
	 * All planes will be positioned inside this space,
	 * and get clipped at the edges. */
	int pipe_src_w, pipe_src_h;

	/* Whether to set up the PCH/FDI. Note that we never allow sharing
	 * between pch encoders and cpu encoders. */
	bool has_pch_encoder;

	/* Are we sending infoframes on the attached port */
	bool has_infoframe;

	/* CPU Transcoder for the pipe. Currently this can only differ from the
	 * pipe on Haswell and later (where we have a special eDP transcoder)
	 * and Broxton (where we have special DSI transcoders). */
	enum transcoder cpu_transcoder;

	/*
	 * Use reduced/limited/broadcast rbg range, compressing from the full
	 * range fed into the crtcs.
	 */
	bool limited_color_range;

	/* Bitmask of encoder types (enum intel_output_type)
	 * driven by the pipe.
	 */
	unsigned int output_types;

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

	/* Selected dpll when shared or NULL. */
	struct intel_shared_dpll *shared_dpll;

	/* Actual register state of the dpll, for shared dpll cross-checking. */
	struct intel_dpll_hw_state dpll_hw_state;

	/* DSI PLL registers */
	struct {
		u32 ctrl, div;
	} dsi_pll;

	int pipe_bpp;
	struct intel_link_m_n dp_m_n;

	/* m2_n2 for eDP downclock */
	struct intel_link_m_n dp_m2_n2;
	bool has_drrs;

	/*
	 * Frequence the dpll for the port should run at. Differs from the
	 * adjusted dotclock e.g. for DP or 12bpc hdmi mode. This is also
	 * already multiplied by pixel_multiplier.
	 */
	int port_clock;

	/* Used by SDVO (and if we ever fix it, HDMI). */
	unsigned pixel_multiplier;

	uint8_t lane_count;

	/*
	 * Used by platforms having DP/HDMI PHY with programmable lane
	 * latency optimization.
	 */
	uint8_t lane_lat_optim_mask;

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

	bool enable_fbc;

	bool double_wide;

	bool dp_encoder_is_mst;
	int pbn;

	struct intel_crtc_scaler_state scaler_state;

	/* w/a for waiting 2 vblanks during crtc enable */
	enum pipe hsw_workaround_pipe;

	/* IVB sprite scaling w/a (WaCxSRDisabledForSpriteScaling:ivb) */
	bool disable_lp_wm;

	struct intel_crtc_wm_state wm;

	/* Gamma mode programmed on the pipe */
	uint32_t gamma_mode;
};

struct vlv_wm_state {
	struct vlv_pipe_wm wm[3];
	struct vlv_sr_wm sr[3];
	uint8_t num_active_planes;
	uint8_t num_levels;
	uint8_t level;
	bool cxsr;
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
	bool lowfreq_avail;
	struct intel_overlay *overlay;
	struct intel_flip_work *flip_work;

	atomic_t unpin_work_count;

	/* Display surface base address adjustement for pageflips. Note that on
	 * gen4+ this only adjusts up to a tile, offsets within a tile are
	 * handled in the hw itself (with the TILEOFF register). */
	u32 dspaddr_offset;
	int adjusted_x;
	int adjusted_y;

	uint32_t cursor_addr;
	uint32_t cursor_cntl;
	uint32_t cursor_size;
	uint32_t cursor_base;

	struct intel_crtc_state *config;

	/* global reset count when the last flip was submitted */
	unsigned int reset_count;

	/* Access to these should be protected by dev_priv->irq_lock. */
	bool cpu_fifo_underrun_disabled;
	bool pch_fifo_underrun_disabled;

	/* per-pipe watermark state */
	struct {
		/* watermarks currently being used  */
		union {
			struct intel_pipe_wm ilk;
			struct skl_pipe_wm skl;
		} active;

		/* allow CxSR on this pipe */
		bool cxsr_allowed;
	} wm;

	/* gen9+: ddb allocation currently being used */
	struct skl_ddb_entry hw_ddb;

	int scanline_offset;

	struct {
		unsigned start_vbl_count;
		ktime_t start_vbl_time;
		int min_vbl, max_vbl;
		int scanline_start;
	} debug;

	/* scalers available on this crtc */
	int num_scalers;

	struct vlv_wm_state wm_state;
};

struct intel_plane_wm_parameters {
	uint32_t horiz_pixels;
	uint32_t vert_pixels;
	/*
	 *   For packed pixel formats:
	 *     bytes_per_pixel - holds bytes per pixel
	 *   For planar pixel formats:
	 *     bytes_per_pixel - holds bytes per pixel for uv-plane
	 *     y_bytes_per_pixel - holds bytes per pixel for y-plane
	 */
	uint8_t bytes_per_pixel;
	uint8_t y_bytes_per_pixel;
	bool enabled;
	bool scaled;
	u64 tiling;
	unsigned int rotation;
	uint16_t fifo_size;
};

struct intel_plane {
	struct drm_plane base;
	int plane;
	enum pipe pipe;
	bool can_scale;
	int max_downscale;
	uint32_t frontbuffer_bit;

	/* Since we need to change the watermarks before/after
	 * enabling/disabling the planes, we need to store the parameters here
	 * as the other pieces of the struct may not reflect the values we want
	 * for the watermark calculations. Currently only Haswell uses this.
	 */
	struct intel_plane_wm_parameters wm;

	/*
	 * NOTE: Do not place new plane state fields here (e.g., when adding
	 * new plane properties).  New runtime state should now be placed in
	 * the intel_plane_state structure and accessed via plane_state.
	 */

	void (*update_plane)(struct drm_plane *plane,
			     const struct intel_crtc_state *crtc_state,
			     const struct intel_plane_state *plane_state);
	void (*disable_plane)(struct drm_plane *plane,
			      struct drm_crtc *crtc);
	int (*check_plane)(struct drm_plane *plane,
			   struct intel_crtc_state *crtc_state,
			   struct intel_plane_state *state);
};

struct intel_watermark_params {
	u16 fifo_size;
	u16 max_wm;
	u8 default_wm;
	u8 guard_size;
	u8 cacheline_size;
};

struct cxsr_latency {
	bool is_desktop : 1;
	bool is_ddr3 : 1;
	u16 fsb_freq;
	u16 mem_freq;
	u16 display_sr;
	u16 display_hpll_disable;
	u16 cursor_sr;
	u16 cursor_hpll_disable;
};

#define to_intel_atomic_state(x) container_of(x, struct intel_atomic_state, base)
#define to_intel_crtc(x) container_of(x, struct intel_crtc, base)
#define to_intel_crtc_state(x) container_of(x, struct intel_crtc_state, base)
#define to_intel_connector(x) container_of(x, struct intel_connector, base)
#define to_intel_encoder(x) container_of(x, struct intel_encoder, base)
#define to_intel_framebuffer(x) container_of(x, struct intel_framebuffer, base)
#define to_intel_plane(x) container_of(x, struct intel_plane, base)
#define to_intel_plane_state(x) container_of(x, struct intel_plane_state, base)
#define intel_fb_obj(x) (x ? to_intel_framebuffer(x)->obj : NULL)

struct intel_hdmi {
	i915_reg_t hdmi_reg;
	int ddc_bus;
	struct {
		enum drm_dp_dual_mode_type type;
		int max_tmds_clock;
	} dp_dual_mode;
	bool limited_color_range;
	bool color_range_auto;
	bool has_hdmi_sink;
	bool has_audio;
	enum hdmi_force_audio force_audio;
	bool rgb_quant_range_selectable;
	enum hdmi_picture_aspect aspect_ratio;
	struct intel_connector *attached_connector;
	void (*write_infoframe)(struct drm_encoder *encoder,
				enum hdmi_infoframe_type type,
				const void *frame, ssize_t len);
	void (*set_infoframes)(struct drm_encoder *encoder,
			       bool enable,
			       const struct drm_display_mode *adjusted_mode);
	bool (*infoframe_enabled)(struct drm_encoder *encoder,
				  const struct intel_crtc_state *pipe_config);
};

struct intel_dp_mst_encoder;
#define DP_MAX_DOWNSTREAM_PORTS		0x10

/*
 * enum link_m_n_set:
 *	When platform provides two set of M_N registers for dp, we can
 *	program them and switch between them incase of DRRS.
 *	But When only one such register is provided, we have to program the
 *	required divider value on that registers itself based on the DRRS state.
 *
 * M1_N1	: Program dp_m_n on M1_N1 registers
 *			  dp_m2_n2 on M2_N2 registers (If supported)
 *
 * M2_N2	: Program dp_m2_n2 on M1_N1 registers
 *			  M2_N2 registers are not supported
 */

enum link_m_n_set {
	/* Sets the m1_n1 and m2_n2 */
	M1_N1 = 0,
	M2_N2
};

struct intel_dp_desc {
	u8 oui[3];
	u8 device_id[6];
	u8 hw_rev;
	u8 sw_major_rev;
	u8 sw_minor_rev;
} __packed;

struct intel_dp {
	i915_reg_t output_reg;
	i915_reg_t aux_ch_ctl_reg;
	i915_reg_t aux_ch_data_reg[5];
	uint32_t DP;
	int link_rate;
	uint8_t lane_count;
	uint8_t sink_count;
	bool link_mst;
	bool has_audio;
	bool detect_done;
	bool channel_eq_status;
	enum hdmi_force_audio force_audio;
	bool limited_color_range;
	bool color_range_auto;
	uint8_t dpcd[DP_RECEIVER_CAP_SIZE];
	uint8_t psr_dpcd[EDP_PSR_RECEIVER_CAP_SIZE];
	uint8_t downstream_ports[DP_MAX_DOWNSTREAM_PORTS];
	uint8_t edp_dpcd[EDP_DISPLAY_CTL_CAP_SIZE];
	/* sink rates as reported by DP_SUPPORTED_LINK_RATES */
	uint8_t num_sink_rates;
	int sink_rates[DP_MAX_SUPPORTED_RATES];
	/* sink or branch descriptor */
	struct intel_dp_desc desc;
	struct drm_dp_aux aux;
	uint8_t train_set[4];
	int panel_power_up_delay;
	int panel_power_down_delay;
	int panel_power_cycle_delay;
	int backlight_on_delay;
	int backlight_off_delay;
	struct delayed_work panel_vdd_work;
	bool want_panel_vdd;
	unsigned long last_power_on;
	unsigned long last_backlight_off;
	ktime_t panel_power_off_time;

	struct notifier_block edp_notifier;

	/*
	 * Pipe whose power sequencer is currently locked into
	 * this port. Only relevant on VLV/CHV.
	 */
	enum pipe pps_pipe;
	/*
	 * Set if the sequencer may be reset due to a power transition,
	 * requiring a reinitialization. Only relevant on BXT.
	 */
	bool pps_reset;
	struct edp_power_seq pps_delays;

	bool can_mst; /* this port supports mst */
	bool is_mst;
	int active_mst_links;
	/* connector directly attached - won't be use for modeset in mst world */
	struct intel_connector *attached_connector;

	/* mst connector list */
	struct intel_dp_mst_encoder *mst_encoders[I915_MAX_PIPES];
	struct drm_dp_mst_topology_mgr mst_mgr;

	uint32_t (*get_aux_clock_divider)(struct intel_dp *dp, int index);
	/*
	 * This function returns the value we have to program the AUX_CTL
	 * register with to kick off an AUX transaction.
	 */
	uint32_t (*get_aux_send_ctl)(struct intel_dp *dp,
				     bool has_aux_irq,
				     int send_bytes,
				     uint32_t aux_clock_divider);

	/* This is called before a link training is starterd */
	void (*prepare_link_retrain)(struct intel_dp *intel_dp);

	/* Displayport compliance testing */
	unsigned long compliance_test_type;
	unsigned long compliance_test_data;
	bool compliance_test_active;
};

struct intel_lspcon {
	bool active;
	enum drm_lspcon_mode mode;
	bool desc_valid;
};

struct intel_digital_port {
	struct intel_encoder base;
	enum port port;
	u32 saved_port_bits;
	struct intel_dp dp;
	struct intel_hdmi hdmi;
	struct intel_lspcon lspcon;
	enum irqreturn (*hpd_pulse)(struct intel_digital_port *, bool);
	bool release_cl2_override;
	uint8_t max_lanes;
};

struct intel_dp_mst_encoder {
	struct intel_encoder base;
	enum pipe pipe;
	struct intel_digital_port *primary;
	struct intel_connector *connector;
};

static inline enum dpio_channel
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

static inline enum dpio_phy
vlv_dport_to_phy(struct intel_digital_port *dport)
{
	switch (dport->port) {
	case PORT_B:
	case PORT_C:
		return DPIO_PHY0;
	case PORT_D:
		return DPIO_PHY1;
	default:
		BUG();
	}
}

static inline enum dpio_channel
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
	struct drm_i915_private *dev_priv = to_i915(dev);
	return dev_priv->pipe_to_crtc_mapping[pipe];
}

static inline struct drm_crtc *
intel_get_crtc_for_plane(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	return dev_priv->plane_to_crtc_mapping[plane];
}

struct intel_flip_work {
	struct work_struct unpin_work;
	struct work_struct mmio_work;

	struct drm_crtc *crtc;
	struct drm_framebuffer *old_fb;
	struct drm_i915_gem_object *pending_flip_obj;
	struct drm_pending_vblank_event *event;
	atomic_t pending;
	u32 flip_count;
	u32 gtt_offset;
	struct drm_i915_gem_request *flip_queued_req;
	u32 flip_queued_vblank;
	u32 flip_ready_vblank;
	unsigned int rotation;
};

struct intel_load_detect_pipe {
	struct drm_atomic_state *restore_state;
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

static inline struct intel_dp_mst_encoder *
enc_to_mst(struct drm_encoder *encoder)
{
	return container_of(encoder, struct intel_dp_mst_encoder, base.base);
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

/* intel_fifo_underrun.c */
bool intel_set_cpu_fifo_underrun_reporting(struct drm_i915_private *dev_priv,
					   enum pipe pipe, bool enable);
bool intel_set_pch_fifo_underrun_reporting(struct drm_i915_private *dev_priv,
					   enum transcoder pch_transcoder,
					   bool enable);
void intel_cpu_fifo_underrun_irq_handler(struct drm_i915_private *dev_priv,
					 enum pipe pipe);
void intel_pch_fifo_underrun_irq_handler(struct drm_i915_private *dev_priv,
					 enum transcoder pch_transcoder);
void intel_check_cpu_fifo_underruns(struct drm_i915_private *dev_priv);
void intel_check_pch_fifo_underruns(struct drm_i915_private *dev_priv);

/* i915_irq.c */
void gen5_enable_gt_irq(struct drm_i915_private *dev_priv, uint32_t mask);
void gen5_disable_gt_irq(struct drm_i915_private *dev_priv, uint32_t mask);
void gen6_reset_pm_iir(struct drm_i915_private *dev_priv, u32 mask);
void gen6_mask_pm_irq(struct drm_i915_private *dev_priv, u32 mask);
void gen6_unmask_pm_irq(struct drm_i915_private *dev_priv, u32 mask);
void gen6_enable_pm_irq(struct drm_i915_private *dev_priv, uint32_t mask);
void gen6_disable_pm_irq(struct drm_i915_private *dev_priv, uint32_t mask);
void gen6_reset_rps_interrupts(struct drm_i915_private *dev_priv);
void gen6_enable_rps_interrupts(struct drm_i915_private *dev_priv);
void gen6_disable_rps_interrupts(struct drm_i915_private *dev_priv);
u32 gen6_sanitize_rps_pm_mask(struct drm_i915_private *dev_priv, u32 mask);
void intel_runtime_pm_disable_interrupts(struct drm_i915_private *dev_priv);
void intel_runtime_pm_enable_interrupts(struct drm_i915_private *dev_priv);
static inline bool intel_irqs_enabled(struct drm_i915_private *dev_priv)
{
	/*
	 * We only use drm_irq_uninstall() at unload and VT switch, so
	 * this is the only thing we need to check.
	 */
	return dev_priv->pm.irqs_enabled;
}

int intel_get_crtc_scanline(struct intel_crtc *crtc);
void gen8_irq_power_well_post_enable(struct drm_i915_private *dev_priv,
				     unsigned int pipe_mask);
void gen8_irq_power_well_pre_disable(struct drm_i915_private *dev_priv,
				     unsigned int pipe_mask);
void gen9_reset_guc_interrupts(struct drm_i915_private *dev_priv);
void gen9_enable_guc_interrupts(struct drm_i915_private *dev_priv);
void gen9_disable_guc_interrupts(struct drm_i915_private *dev_priv);

/* intel_crt.c */
void intel_crt_init(struct drm_device *dev);
void intel_crt_reset(struct drm_encoder *encoder);

/* intel_ddi.c */
void intel_ddi_clk_select(struct intel_encoder *encoder,
			  struct intel_shared_dpll *pll);
void intel_ddi_fdi_post_disable(struct intel_encoder *intel_encoder,
				struct intel_crtc_state *old_crtc_state,
				struct drm_connector_state *old_conn_state);
void intel_prepare_dp_ddi_buffers(struct intel_encoder *encoder);
void hsw_fdi_link_train(struct drm_crtc *crtc);
void intel_ddi_init(struct drm_device *dev, enum port port);
enum port intel_ddi_get_encoder_port(struct intel_encoder *intel_encoder);
bool intel_ddi_get_hw_state(struct intel_encoder *encoder, enum pipe *pipe);
void intel_ddi_enable_transcoder_func(struct drm_crtc *crtc);
void intel_ddi_disable_transcoder_func(struct drm_i915_private *dev_priv,
				       enum transcoder cpu_transcoder);
void intel_ddi_enable_pipe_clock(struct intel_crtc *intel_crtc);
void intel_ddi_disable_pipe_clock(struct intel_crtc *intel_crtc);
bool intel_ddi_pll_select(struct intel_crtc *crtc,
			  struct intel_crtc_state *crtc_state);
void intel_ddi_set_pipe_settings(struct drm_crtc *crtc);
void intel_ddi_prepare_link_retrain(struct intel_dp *intel_dp);
bool intel_ddi_connector_get_hw_state(struct intel_connector *intel_connector);
void intel_ddi_get_config(struct intel_encoder *encoder,
			  struct intel_crtc_state *pipe_config);
struct intel_encoder *
intel_ddi_get_crtc_new_encoder(struct intel_crtc_state *crtc_state);

void intel_ddi_init_dp_buf_reg(struct intel_encoder *encoder);
void intel_ddi_clock_get(struct intel_encoder *encoder,
			 struct intel_crtc_state *pipe_config);
void intel_ddi_set_vc_payload_alloc(struct drm_crtc *crtc, bool state);
uint32_t ddi_signal_levels(struct intel_dp *intel_dp);
struct intel_shared_dpll *intel_ddi_get_link_dpll(struct intel_dp *intel_dp,
						  int clock);
unsigned int intel_fb_align_height(struct drm_device *dev,
				   unsigned int height,
				   uint32_t pixel_format,
				   uint64_t fb_format_modifier);
u32 intel_fb_stride_alignment(const struct drm_i915_private *dev_priv,
			      uint64_t fb_modifier, uint32_t pixel_format);

/* intel_audio.c */
void intel_init_audio_hooks(struct drm_i915_private *dev_priv);
void intel_audio_codec_enable(struct intel_encoder *encoder);
void intel_audio_codec_disable(struct intel_encoder *encoder);
void i915_audio_component_init(struct drm_i915_private *dev_priv);
void i915_audio_component_cleanup(struct drm_i915_private *dev_priv);

/* intel_display.c */
enum transcoder intel_crtc_pch_transcoder(struct intel_crtc *crtc);
void skl_set_preferred_cdclk_vco(struct drm_i915_private *dev_priv, int vco);
void intel_update_rawclk(struct drm_i915_private *dev_priv);
int vlv_get_cck_clock(struct drm_i915_private *dev_priv,
		      const char *name, u32 reg, int ref_freq);
void lpt_disable_pch_transcoder(struct drm_i915_private *dev_priv);
void lpt_disable_iclkip(struct drm_i915_private *dev_priv);
extern const struct drm_plane_funcs intel_plane_funcs;
void intel_init_display_hooks(struct drm_i915_private *dev_priv);
unsigned int intel_fb_xy_to_linear(int x, int y,
				   const struct intel_plane_state *state,
				   int plane);
void intel_add_fb_offsets(int *x, int *y,
			  const struct intel_plane_state *state, int plane);
unsigned int intel_rotation_info_size(const struct intel_rotation_info *rot_info);
bool intel_has_pending_fb_unpin(struct drm_device *dev);
void intel_mark_busy(struct drm_i915_private *dev_priv);
void intel_mark_idle(struct drm_i915_private *dev_priv);
void intel_crtc_restore_mode(struct drm_crtc *crtc);
int intel_display_suspend(struct drm_device *dev);
void intel_pps_unlock_regs_wa(struct drm_i915_private *dev_priv);
void intel_encoder_destroy(struct drm_encoder *encoder);
int intel_connector_init(struct intel_connector *);
struct intel_connector *intel_connector_alloc(void);
bool intel_connector_get_hw_state(struct intel_connector *connector);
void intel_connector_attach_encoder(struct intel_connector *connector,
				    struct intel_encoder *encoder);
struct drm_display_mode *intel_crtc_mode_get(struct drm_device *dev,
					     struct drm_crtc *crtc);
enum pipe intel_get_pipe_from_connector(struct intel_connector *connector);
int intel_get_pipe_from_crtc_id(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
enum transcoder intel_pipe_to_cpu_transcoder(struct drm_i915_private *dev_priv,
					     enum pipe pipe);
static inline bool
intel_crtc_has_type(const struct intel_crtc_state *crtc_state,
		    enum intel_output_type type)
{
	return crtc_state->output_types & (1 << type);
}
static inline bool
intel_crtc_has_dp_encoder(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->output_types &
		((1 << INTEL_OUTPUT_DP) |
		 (1 << INTEL_OUTPUT_DP_MST) |
		 (1 << INTEL_OUTPUT_EDP));
}
static inline void
intel_wait_for_vblank(struct drm_device *dev, int pipe)
{
	drm_wait_one_vblank(dev, pipe);
}
static inline void
intel_wait_for_vblank_if_active(struct drm_device *dev, int pipe)
{
	const struct intel_crtc *crtc =
		to_intel_crtc(intel_get_crtc_for_pipe(dev, pipe));

	if (crtc->active)
		intel_wait_for_vblank(dev, pipe);
}

u32 intel_crtc_get_vblank_counter(struct intel_crtc *crtc);

int ironlake_get_lanes_required(int target_clock, int link_bw, int bpp);
void vlv_wait_port_ready(struct drm_i915_private *dev_priv,
			 struct intel_digital_port *dport,
			 unsigned int expected_mask);
bool intel_get_load_detect_pipe(struct drm_connector *connector,
				struct drm_display_mode *mode,
				struct intel_load_detect_pipe *old,
				struct drm_modeset_acquire_ctx *ctx);
void intel_release_load_detect_pipe(struct drm_connector *connector,
				    struct intel_load_detect_pipe *old,
				    struct drm_modeset_acquire_ctx *ctx);
struct i915_vma *
intel_pin_and_fence_fb_obj(struct drm_framebuffer *fb, unsigned int rotation);
void intel_unpin_fb_obj(struct drm_framebuffer *fb, unsigned int rotation);
struct drm_framebuffer *
__intel_framebuffer_create(struct drm_device *dev,
			   struct drm_mode_fb_cmd2 *mode_cmd,
			   struct drm_i915_gem_object *obj);
void intel_finish_page_flip_cs(struct drm_i915_private *dev_priv, int pipe);
void intel_finish_page_flip_mmio(struct drm_i915_private *dev_priv, int pipe);
void intel_check_page_flip(struct drm_i915_private *dev_priv, int pipe);
int intel_prepare_plane_fb(struct drm_plane *plane,
			   struct drm_plane_state *new_state);
void intel_cleanup_plane_fb(struct drm_plane *plane,
			    struct drm_plane_state *old_state);
int intel_plane_atomic_get_property(struct drm_plane *plane,
				    const struct drm_plane_state *state,
				    struct drm_property *property,
				    uint64_t *val);
int intel_plane_atomic_set_property(struct drm_plane *plane,
				    struct drm_plane_state *state,
				    struct drm_property *property,
				    uint64_t val);
int intel_plane_atomic_calc_changes(struct drm_crtc_state *crtc_state,
				    struct drm_plane_state *plane_state);

unsigned int intel_tile_height(const struct drm_i915_private *dev_priv,
			       uint64_t fb_modifier, unsigned int cpp);

void assert_pch_transcoder_disabled(struct drm_i915_private *dev_priv,
				    enum pipe pipe);

int vlv_force_pll_on(struct drm_device *dev, enum pipe pipe,
		     const struct dpll *dpll);
void vlv_force_pll_off(struct drm_device *dev, enum pipe pipe);
int lpt_get_iclkip(struct drm_i915_private *dev_priv);

/* modesetting asserts */
void assert_panel_unlocked(struct drm_i915_private *dev_priv,
			   enum pipe pipe);
void assert_pll(struct drm_i915_private *dev_priv,
		enum pipe pipe, bool state);
#define assert_pll_enabled(d, p) assert_pll(d, p, true)
#define assert_pll_disabled(d, p) assert_pll(d, p, false)
void assert_dsi_pll(struct drm_i915_private *dev_priv, bool state);
#define assert_dsi_pll_enabled(d) assert_dsi_pll(d, true)
#define assert_dsi_pll_disabled(d) assert_dsi_pll(d, false)
void assert_fdi_rx_pll(struct drm_i915_private *dev_priv,
		       enum pipe pipe, bool state);
#define assert_fdi_rx_pll_enabled(d, p) assert_fdi_rx_pll(d, p, true)
#define assert_fdi_rx_pll_disabled(d, p) assert_fdi_rx_pll(d, p, false)
void assert_pipe(struct drm_i915_private *dev_priv, enum pipe pipe, bool state);
#define assert_pipe_enabled(d, p) assert_pipe(d, p, true)
#define assert_pipe_disabled(d, p) assert_pipe(d, p, false)
u32 intel_compute_tile_offset(int *x, int *y,
			      const struct intel_plane_state *state, int plane);
void intel_prepare_reset(struct drm_i915_private *dev_priv);
void intel_finish_reset(struct drm_i915_private *dev_priv);
void hsw_enable_pc8(struct drm_i915_private *dev_priv);
void hsw_disable_pc8(struct drm_i915_private *dev_priv);
void bxt_init_cdclk(struct drm_i915_private *dev_priv);
void bxt_uninit_cdclk(struct drm_i915_private *dev_priv);
void gen9_sanitize_dc_state(struct drm_i915_private *dev_priv);
void bxt_enable_dc9(struct drm_i915_private *dev_priv);
void bxt_disable_dc9(struct drm_i915_private *dev_priv);
void gen9_enable_dc5(struct drm_i915_private *dev_priv);
void skl_init_cdclk(struct drm_i915_private *dev_priv);
void skl_uninit_cdclk(struct drm_i915_private *dev_priv);
unsigned int skl_cdclk_get_vco(unsigned int freq);
void skl_enable_dc6(struct drm_i915_private *dev_priv);
void skl_disable_dc6(struct drm_i915_private *dev_priv);
void intel_dp_get_m_n(struct intel_crtc *crtc,
		      struct intel_crtc_state *pipe_config);
void intel_dp_set_m_n(struct intel_crtc *crtc, enum link_m_n_set m_n);
int intel_dotclock_calculate(int link_freq, const struct intel_link_m_n *m_n);
bool bxt_find_best_dpll(struct intel_crtc_state *crtc_state, int target_clock,
			struct dpll *best_clock);
int chv_calc_dpll_params(int refclk, struct dpll *pll_clock);

bool intel_crtc_active(struct drm_crtc *crtc);
void hsw_enable_ips(struct intel_crtc *crtc);
void hsw_disable_ips(struct intel_crtc *crtc);
enum intel_display_power_domain
intel_display_port_power_domain(struct intel_encoder *intel_encoder);
enum intel_display_power_domain
intel_display_port_aux_power_domain(struct intel_encoder *intel_encoder);
void intel_mode_from_pipe_config(struct drm_display_mode *mode,
				 struct intel_crtc_state *pipe_config);

int skl_update_scaler_crtc(struct intel_crtc_state *crtc_state);
int skl_max_scale(struct intel_crtc *crtc, struct intel_crtc_state *crtc_state);

u32 intel_fb_gtt_offset(struct drm_framebuffer *fb, unsigned int rotation);

u32 skl_plane_ctl_format(uint32_t pixel_format);
u32 skl_plane_ctl_tiling(uint64_t fb_modifier);
u32 skl_plane_ctl_rotation(unsigned int rotation);
u32 skl_plane_stride(const struct drm_framebuffer *fb, int plane,
		     unsigned int rotation);
int skl_check_plane_surface(struct intel_plane_state *plane_state);

/* intel_csr.c */
void intel_csr_ucode_init(struct drm_i915_private *);
void intel_csr_load_program(struct drm_i915_private *);
void intel_csr_ucode_fini(struct drm_i915_private *);
void intel_csr_ucode_suspend(struct drm_i915_private *);
void intel_csr_ucode_resume(struct drm_i915_private *);

/* intel_dp.c */
bool intel_dp_init(struct drm_device *dev, i915_reg_t output_reg, enum port port);
bool intel_dp_init_connector(struct intel_digital_port *intel_dig_port,
			     struct intel_connector *intel_connector);
void intel_dp_set_link_params(struct intel_dp *intel_dp,
			      int link_rate, uint8_t lane_count,
			      bool link_mst);
void intel_dp_start_link_train(struct intel_dp *intel_dp);
void intel_dp_stop_link_train(struct intel_dp *intel_dp);
void intel_dp_sink_dpms(struct intel_dp *intel_dp, int mode);
void intel_dp_encoder_reset(struct drm_encoder *encoder);
void intel_dp_encoder_suspend(struct intel_encoder *intel_encoder);
void intel_dp_encoder_destroy(struct drm_encoder *encoder);
int intel_dp_sink_crc(struct intel_dp *intel_dp, u8 *crc);
bool intel_dp_compute_config(struct intel_encoder *encoder,
			     struct intel_crtc_state *pipe_config,
			     struct drm_connector_state *conn_state);
bool intel_dp_is_edp(struct drm_device *dev, enum port port);
enum irqreturn intel_dp_hpd_pulse(struct intel_digital_port *intel_dig_port,
				  bool long_hpd);
void intel_edp_backlight_on(struct intel_dp *intel_dp);
void intel_edp_backlight_off(struct intel_dp *intel_dp);
void intel_edp_panel_vdd_on(struct intel_dp *intel_dp);
void intel_edp_panel_on(struct intel_dp *intel_dp);
void intel_edp_panel_off(struct intel_dp *intel_dp);
void intel_dp_add_properties(struct intel_dp *intel_dp, struct drm_connector *connector);
void intel_dp_mst_suspend(struct drm_device *dev);
void intel_dp_mst_resume(struct drm_device *dev);
int intel_dp_max_link_rate(struct intel_dp *intel_dp);
int intel_dp_rate_select(struct intel_dp *intel_dp, int rate);
void intel_dp_hot_plug(struct intel_encoder *intel_encoder);
void intel_power_sequencer_reset(struct drm_i915_private *dev_priv);
uint32_t intel_dp_pack_aux(const uint8_t *src, int src_bytes);
void intel_plane_destroy(struct drm_plane *plane);
void intel_edp_drrs_enable(struct intel_dp *intel_dp,
			   struct intel_crtc_state *crtc_state);
void intel_edp_drrs_disable(struct intel_dp *intel_dp,
			   struct intel_crtc_state *crtc_state);
void intel_edp_drrs_invalidate(struct drm_i915_private *dev_priv,
			       unsigned int frontbuffer_bits);
void intel_edp_drrs_flush(struct drm_i915_private *dev_priv,
			  unsigned int frontbuffer_bits);

void
intel_dp_program_link_training_pattern(struct intel_dp *intel_dp,
				       uint8_t dp_train_pat);
void
intel_dp_set_signal_levels(struct intel_dp *intel_dp);
void intel_dp_set_idle_link_train(struct intel_dp *intel_dp);
uint8_t
intel_dp_voltage_max(struct intel_dp *intel_dp);
uint8_t
intel_dp_pre_emphasis_max(struct intel_dp *intel_dp, uint8_t voltage_swing);
void intel_dp_compute_rate(struct intel_dp *intel_dp, int port_clock,
			   uint8_t *link_bw, uint8_t *rate_select);
bool intel_dp_source_supports_hbr2(struct intel_dp *intel_dp);
bool
intel_dp_get_link_status(struct intel_dp *intel_dp, uint8_t link_status[DP_LINK_STATUS_SIZE]);

static inline unsigned int intel_dp_unused_lane_mask(int lane_count)
{
	return ~((1 << lane_count) - 1) & 0xf;
}

bool intel_dp_read_dpcd(struct intel_dp *intel_dp);
bool __intel_dp_read_desc(struct intel_dp *intel_dp,
			  struct intel_dp_desc *desc);
bool intel_dp_read_desc(struct intel_dp *intel_dp);

/* intel_dp_aux_backlight.c */
int intel_dp_aux_init_backlight_funcs(struct intel_connector *intel_connector);

/* intel_dp_mst.c */
int intel_dp_mst_encoder_init(struct intel_digital_port *intel_dig_port, int conn_id);
void intel_dp_mst_encoder_cleanup(struct intel_digital_port *intel_dig_port);
/* intel_dsi.c */
void intel_dsi_init(struct drm_device *dev);

/* intel_dsi_dcs_backlight.c */
int intel_dsi_dcs_init_backlight_funcs(struct intel_connector *intel_connector);

/* intel_dvo.c */
void intel_dvo_init(struct drm_device *dev);
/* intel_hotplug.c */
void intel_hpd_poll_init(struct drm_i915_private *dev_priv);


/* legacy fbdev emulation in intel_fbdev.c */
#ifdef CONFIG_DRM_FBDEV_EMULATION
extern int intel_fbdev_init(struct drm_device *dev);
extern void intel_fbdev_initial_config_async(struct drm_device *dev);
extern void intel_fbdev_fini(struct drm_device *dev);
extern void intel_fbdev_set_suspend(struct drm_device *dev, int state, bool synchronous);
extern void intel_fbdev_output_poll_changed(struct drm_device *dev);
extern void intel_fbdev_restore_mode(struct drm_device *dev);
#else
static inline int intel_fbdev_init(struct drm_device *dev)
{
	return 0;
}

static inline void intel_fbdev_initial_config_async(struct drm_device *dev)
{
}

static inline void intel_fbdev_fini(struct drm_device *dev)
{
}

static inline void intel_fbdev_set_suspend(struct drm_device *dev, int state, bool synchronous)
{
}

static inline void intel_fbdev_output_poll_changed(struct drm_device *dev)
{
}

static inline void intel_fbdev_restore_mode(struct drm_device *dev)
{
}
#endif

/* intel_fbc.c */
void intel_fbc_choose_crtc(struct drm_i915_private *dev_priv,
			   struct drm_atomic_state *state);
bool intel_fbc_is_active(struct drm_i915_private *dev_priv);
void intel_fbc_pre_update(struct intel_crtc *crtc,
			  struct intel_crtc_state *crtc_state,
			  struct intel_plane_state *plane_state);
void intel_fbc_post_update(struct intel_crtc *crtc);
void intel_fbc_init(struct drm_i915_private *dev_priv);
void intel_fbc_init_pipe_state(struct drm_i915_private *dev_priv);
void intel_fbc_enable(struct intel_crtc *crtc,
		      struct intel_crtc_state *crtc_state,
		      struct intel_plane_state *plane_state);
void intel_fbc_disable(struct intel_crtc *crtc);
void intel_fbc_global_disable(struct drm_i915_private *dev_priv);
void intel_fbc_invalidate(struct drm_i915_private *dev_priv,
			  unsigned int frontbuffer_bits,
			  enum fb_op_origin origin);
void intel_fbc_flush(struct drm_i915_private *dev_priv,
		     unsigned int frontbuffer_bits, enum fb_op_origin origin);
void intel_fbc_cleanup_cfb(struct drm_i915_private *dev_priv);
void intel_fbc_handle_fifo_underrun_irq(struct drm_i915_private *dev_priv);

/* intel_hdmi.c */
void intel_hdmi_init(struct drm_device *dev, i915_reg_t hdmi_reg, enum port port);
void intel_hdmi_init_connector(struct intel_digital_port *intel_dig_port,
			       struct intel_connector *intel_connector);
struct intel_hdmi *enc_to_intel_hdmi(struct drm_encoder *encoder);
bool intel_hdmi_compute_config(struct intel_encoder *encoder,
			       struct intel_crtc_state *pipe_config,
			       struct drm_connector_state *conn_state);
void intel_dp_dual_mode_set_tmds_output(struct intel_hdmi *hdmi, bool enable);


/* intel_lvds.c */
void intel_lvds_init(struct drm_device *dev);
struct intel_encoder *intel_get_lvds_encoder(struct drm_device *dev);
bool intel_is_dual_link_lvds(struct drm_device *dev);


/* intel_modes.c */
int intel_connector_update_modes(struct drm_connector *connector,
				 struct edid *edid);
int intel_ddc_get_modes(struct drm_connector *c, struct i2c_adapter *adapter);
void intel_attach_force_audio_property(struct drm_connector *connector);
void intel_attach_broadcast_rgb_property(struct drm_connector *connector);
void intel_attach_aspect_ratio_property(struct drm_connector *connector);


/* intel_overlay.c */
void intel_setup_overlay(struct drm_i915_private *dev_priv);
void intel_cleanup_overlay(struct drm_i915_private *dev_priv);
int intel_overlay_switch_off(struct intel_overlay *overlay);
int intel_overlay_put_image_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
int intel_overlay_attrs_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
void intel_overlay_reset(struct drm_i915_private *dev_priv);


/* intel_panel.c */
int intel_panel_init(struct intel_panel *panel,
		     struct drm_display_mode *fixed_mode,
		     struct drm_display_mode *downclock_mode);
void intel_panel_fini(struct intel_panel *panel);
void intel_fixed_panel_mode(const struct drm_display_mode *fixed_mode,
			    struct drm_display_mode *adjusted_mode);
void intel_pch_panel_fitting(struct intel_crtc *crtc,
			     struct intel_crtc_state *pipe_config,
			     int fitting_mode);
void intel_gmch_panel_fitting(struct intel_crtc *crtc,
			      struct intel_crtc_state *pipe_config,
			      int fitting_mode);
void intel_panel_set_backlight_acpi(struct intel_connector *connector,
				    u32 level, u32 max);
int intel_panel_setup_backlight(struct drm_connector *connector,
				enum pipe pipe);
void intel_panel_enable_backlight(struct intel_connector *connector);
void intel_panel_disable_backlight(struct intel_connector *connector);
void intel_panel_destroy_backlight(struct drm_connector *connector);
enum drm_connector_status intel_panel_detect(struct drm_device *dev);
extern struct drm_display_mode *intel_find_panel_downclock(
				struct drm_device *dev,
				struct drm_display_mode *fixed_mode,
				struct drm_connector *connector);

#if IS_ENABLED(CONFIG_BACKLIGHT_CLASS_DEVICE)
int intel_backlight_device_register(struct intel_connector *connector);
void intel_backlight_device_unregister(struct intel_connector *connector);
#else /* CONFIG_BACKLIGHT_CLASS_DEVICE */
static int intel_backlight_device_register(struct intel_connector *connector)
{
	return 0;
}
static inline void intel_backlight_device_unregister(struct intel_connector *connector)
{
}
#endif /* CONFIG_BACKLIGHT_CLASS_DEVICE */


/* intel_psr.c */
void intel_psr_enable(struct intel_dp *intel_dp);
void intel_psr_disable(struct intel_dp *intel_dp);
void intel_psr_invalidate(struct drm_i915_private *dev_priv,
			  unsigned frontbuffer_bits);
void intel_psr_flush(struct drm_i915_private *dev_priv,
		     unsigned frontbuffer_bits,
		     enum fb_op_origin origin);
void intel_psr_init(struct drm_device *dev);
void intel_psr_single_frame_update(struct drm_i915_private *dev_priv,
				   unsigned frontbuffer_bits);

/* intel_runtime_pm.c */
int intel_power_domains_init(struct drm_i915_private *);
void intel_power_domains_fini(struct drm_i915_private *);
void intel_power_domains_init_hw(struct drm_i915_private *dev_priv, bool resume);
void intel_power_domains_suspend(struct drm_i915_private *dev_priv);
void bxt_display_core_init(struct drm_i915_private *dev_priv, bool resume);
void bxt_display_core_uninit(struct drm_i915_private *dev_priv);
void intel_runtime_pm_enable(struct drm_i915_private *dev_priv);
const char *
intel_display_power_domain_str(enum intel_display_power_domain domain);

bool intel_display_power_is_enabled(struct drm_i915_private *dev_priv,
				    enum intel_display_power_domain domain);
bool __intel_display_power_is_enabled(struct drm_i915_private *dev_priv,
				      enum intel_display_power_domain domain);
void intel_display_power_get(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain);
bool intel_display_power_get_if_enabled(struct drm_i915_private *dev_priv,
					enum intel_display_power_domain domain);
void intel_display_power_put(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain);

static inline void
assert_rpm_device_not_suspended(struct drm_i915_private *dev_priv)
{
	WARN_ONCE(dev_priv->pm.suspended,
		  "Device suspended during HW access\n");
}

static inline void
assert_rpm_wakelock_held(struct drm_i915_private *dev_priv)
{
	assert_rpm_device_not_suspended(dev_priv);
	/* FIXME: Needs to be converted back to WARN_ONCE, but currently causes
	 * too much noise. */
	if (!atomic_read(&dev_priv->pm.wakeref_count))
		DRM_DEBUG_DRIVER("RPM wakelock ref not held during HW access");
}

/**
 * disable_rpm_wakeref_asserts - disable the RPM assert checks
 * @dev_priv: i915 device instance
 *
 * This function disable asserts that check if we hold an RPM wakelock
 * reference, while keeping the device-not-suspended checks still enabled.
 * It's meant to be used only in special circumstances where our rule about
 * the wakelock refcount wrt. the device power state doesn't hold. According
 * to this rule at any point where we access the HW or want to keep the HW in
 * an active state we must hold an RPM wakelock reference acquired via one of
 * the intel_runtime_pm_get() helpers. Currently there are a few special spots
 * where this rule doesn't hold: the IRQ and suspend/resume handlers, the
 * forcewake release timer, and the GPU RPS and hangcheck works. All other
 * users should avoid using this function.
 *
 * Any calls to this function must have a symmetric call to
 * enable_rpm_wakeref_asserts().
 */
static inline void
disable_rpm_wakeref_asserts(struct drm_i915_private *dev_priv)
{
	atomic_inc(&dev_priv->pm.wakeref_count);
}

/**
 * enable_rpm_wakeref_asserts - re-enable the RPM assert checks
 * @dev_priv: i915 device instance
 *
 * This function re-enables the RPM assert checks after disabling them with
 * disable_rpm_wakeref_asserts. It's meant to be used only in special
 * circumstances otherwise its use should be avoided.
 *
 * Any calls to this function must have a symmetric call to
 * disable_rpm_wakeref_asserts().
 */
static inline void
enable_rpm_wakeref_asserts(struct drm_i915_private *dev_priv)
{
	atomic_dec(&dev_priv->pm.wakeref_count);
}

void intel_runtime_pm_get(struct drm_i915_private *dev_priv);
bool intel_runtime_pm_get_if_in_use(struct drm_i915_private *dev_priv);
void intel_runtime_pm_get_noresume(struct drm_i915_private *dev_priv);
void intel_runtime_pm_put(struct drm_i915_private *dev_priv);

void intel_display_set_init_power(struct drm_i915_private *dev, bool enable);

void chv_phy_powergate_lanes(struct intel_encoder *encoder,
			     bool override, unsigned int mask);
bool chv_phy_powergate_ch(struct drm_i915_private *dev_priv, enum dpio_phy phy,
			  enum dpio_channel ch, bool override);


/* intel_pm.c */
void intel_init_clock_gating(struct drm_device *dev);
void intel_suspend_hw(struct drm_device *dev);
int ilk_wm_max_level(const struct drm_i915_private *dev_priv);
void intel_update_watermarks(struct drm_crtc *crtc);
void intel_init_pm(struct drm_device *dev);
void intel_init_clock_gating_hooks(struct drm_i915_private *dev_priv);
void intel_pm_setup(struct drm_device *dev);
void intel_gpu_ips_init(struct drm_i915_private *dev_priv);
void intel_gpu_ips_teardown(void);
void intel_init_gt_powersave(struct drm_i915_private *dev_priv);
void intel_cleanup_gt_powersave(struct drm_i915_private *dev_priv);
void intel_sanitize_gt_powersave(struct drm_i915_private *dev_priv);
void intel_enable_gt_powersave(struct drm_i915_private *dev_priv);
void intel_autoenable_gt_powersave(struct drm_i915_private *dev_priv);
void intel_disable_gt_powersave(struct drm_i915_private *dev_priv);
void intel_suspend_gt_powersave(struct drm_i915_private *dev_priv);
void gen6_rps_busy(struct drm_i915_private *dev_priv);
void gen6_rps_reset_ei(struct drm_i915_private *dev_priv);
void gen6_rps_idle(struct drm_i915_private *dev_priv);
void gen6_rps_boost(struct drm_i915_private *dev_priv,
		    struct intel_rps_client *rps,
		    unsigned long submitted);
void intel_queue_rps_boost_for_request(struct drm_i915_gem_request *req);
void vlv_wm_get_hw_state(struct drm_device *dev);
void ilk_wm_get_hw_state(struct drm_device *dev);
void skl_wm_get_hw_state(struct drm_device *dev);
void skl_ddb_get_hw_state(struct drm_i915_private *dev_priv,
			  struct skl_ddb_allocation *ddb /* out */);
void skl_pipe_wm_get_hw_state(struct drm_crtc *crtc,
			      struct skl_pipe_wm *out);
bool intel_can_enable_sagv(struct drm_atomic_state *state);
int intel_enable_sagv(struct drm_i915_private *dev_priv);
int intel_disable_sagv(struct drm_i915_private *dev_priv);
bool skl_wm_level_equals(const struct skl_wm_level *l1,
			 const struct skl_wm_level *l2);
bool skl_ddb_allocation_equals(const struct skl_ddb_allocation *old,
			       const struct skl_ddb_allocation *new,
			       enum pipe pipe);
bool skl_ddb_allocation_overlaps(struct drm_atomic_state *state,
				 struct intel_crtc *intel_crtc);
void skl_write_cursor_wm(struct intel_crtc *intel_crtc,
			 const struct skl_plane_wm *wm,
			 const struct skl_ddb_allocation *ddb);
void skl_write_plane_wm(struct intel_crtc *intel_crtc,
			const struct skl_plane_wm *wm,
			const struct skl_ddb_allocation *ddb,
			int plane);
uint32_t ilk_pipe_pixel_rate(const struct intel_crtc_state *pipe_config);
bool ilk_disable_lp_wm(struct drm_device *dev);
int sanitize_rc6_option(struct drm_i915_private *dev_priv, int enable_rc6);
static inline int intel_enable_rc6(void)
{
	return i915.enable_rc6;
}

/* intel_sdvo.c */
bool intel_sdvo_init(struct drm_device *dev,
		     i915_reg_t reg, enum port port);


/* intel_sprite.c */
int intel_usecs_to_scanlines(const struct drm_display_mode *adjusted_mode,
			     int usecs);
int intel_plane_init(struct drm_device *dev, enum pipe pipe, int plane);
int intel_sprite_set_colorkey(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
void intel_pipe_update_start(struct intel_crtc *crtc);
void intel_pipe_update_end(struct intel_crtc *crtc, struct intel_flip_work *work);

/* intel_tv.c */
void intel_tv_init(struct drm_device *dev);

/* intel_atomic.c */
int intel_connector_atomic_get_property(struct drm_connector *connector,
					const struct drm_connector_state *state,
					struct drm_property *property,
					uint64_t *val);
struct drm_crtc_state *intel_crtc_duplicate_state(struct drm_crtc *crtc);
void intel_crtc_destroy_state(struct drm_crtc *crtc,
			       struct drm_crtc_state *state);
struct drm_atomic_state *intel_atomic_state_alloc(struct drm_device *dev);
void intel_atomic_state_clear(struct drm_atomic_state *);
struct intel_shared_dpll_config *
intel_atomic_get_shared_dpll_state(struct drm_atomic_state *s);

static inline struct intel_crtc_state *
intel_atomic_get_crtc_state(struct drm_atomic_state *state,
			    struct intel_crtc *crtc)
{
	struct drm_crtc_state *crtc_state;
	crtc_state = drm_atomic_get_crtc_state(state, &crtc->base);
	if (IS_ERR(crtc_state))
		return ERR_CAST(crtc_state);

	return to_intel_crtc_state(crtc_state);
}

static inline struct intel_plane_state *
intel_atomic_get_existing_plane_state(struct drm_atomic_state *state,
				      struct intel_plane *plane)
{
	struct drm_plane_state *plane_state;

	plane_state = drm_atomic_get_existing_plane_state(state, &plane->base);

	return to_intel_plane_state(plane_state);
}

int intel_atomic_setup_scalers(struct drm_device *dev,
	struct intel_crtc *intel_crtc,
	struct intel_crtc_state *crtc_state);

/* intel_atomic_plane.c */
struct intel_plane_state *intel_create_plane_state(struct drm_plane *plane);
struct drm_plane_state *intel_plane_duplicate_state(struct drm_plane *plane);
void intel_plane_destroy_state(struct drm_plane *plane,
			       struct drm_plane_state *state);
extern const struct drm_plane_helper_funcs intel_plane_helper_funcs;

/* intel_color.c */
void intel_color_init(struct drm_crtc *crtc);
int intel_color_check(struct drm_crtc *crtc, struct drm_crtc_state *state);
void intel_color_set_csc(struct drm_crtc_state *crtc_state);
void intel_color_load_luts(struct drm_crtc_state *crtc_state);

/* intel_lspcon.c */
bool lspcon_init(struct intel_digital_port *intel_dig_port);
void lspcon_resume(struct intel_lspcon *lspcon);
#endif /* __INTEL_DRV_H__ */
