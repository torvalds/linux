/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_CORE_H__
#define __INTEL_DISPLAY_CORE_H__

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "intel_display.h"
#include "intel_dmc.h"
#include "intel_dpll_mgr.h"
#include "intel_gmbus.h"
#include "intel_pm_types.h"

struct drm_i915_private;
struct i915_audio_component;
struct intel_atomic_state;
struct intel_audio_funcs;
struct intel_cdclk_funcs;
struct intel_color_funcs;
struct intel_crtc;
struct intel_crtc_state;
struct intel_dpll_funcs;
struct intel_dpll_mgr;
struct intel_fbdev;
struct intel_fdi_funcs;
struct intel_hotplug_funcs;
struct intel_initial_plane_config;
struct intel_overlay;

struct intel_display_funcs {
	/*
	 * Returns the active state of the crtc, and if the crtc is active,
	 * fills out the pipe-config with the hw state.
	 */
	bool (*get_pipe_config)(struct intel_crtc *,
				struct intel_crtc_state *);
	void (*get_initial_plane_config)(struct intel_crtc *,
					 struct intel_initial_plane_config *);
	void (*crtc_enable)(struct intel_atomic_state *state,
			    struct intel_crtc *crtc);
	void (*crtc_disable)(struct intel_atomic_state *state,
			     struct intel_crtc *crtc);
	void (*commit_modeset_enables)(struct intel_atomic_state *state);
};

/* functions used for watermark calcs for display. */
struct intel_wm_funcs {
	/* update_wm is for legacy wm management */
	void (*update_wm)(struct drm_i915_private *dev_priv);
	int (*compute_pipe_wm)(struct intel_atomic_state *state,
			       struct intel_crtc *crtc);
	int (*compute_intermediate_wm)(struct intel_atomic_state *state,
				       struct intel_crtc *crtc);
	void (*initial_watermarks)(struct intel_atomic_state *state,
				   struct intel_crtc *crtc);
	void (*atomic_update_watermarks)(struct intel_atomic_state *state,
					 struct intel_crtc *crtc);
	void (*optimize_watermarks)(struct intel_atomic_state *state,
				    struct intel_crtc *crtc);
	int (*compute_global_watermarks)(struct intel_atomic_state *state);
};

struct intel_audio {
	/* hda/i915 audio component */
	struct i915_audio_component *component;
	bool component_registered;
	/* mutex for audio/video sync */
	struct mutex mutex;
	int power_refcount;
	u32 freq_cntrl;

	/* Used to save the pipe-to-encoder mapping for audio */
	struct intel_encoder *encoder_map[I915_MAX_PIPES];

	/* necessary resource sharing with HDMI LPE audio driver. */
	struct {
		struct platform_device *platdev;
		int irq;
	} lpe;
};

/*
 * dpll and cdclk state is protected by connection_mutex dpll.lock serializes
 * intel_{prepare,enable,disable}_shared_dpll.  Must be global rather than per
 * dpll, because on some platforms plls share registers.
 */
struct intel_dpll {
	struct mutex lock;

	int num_shared_dpll;
	struct intel_shared_dpll shared_dplls[I915_NUM_PLLS];
	const struct intel_dpll_mgr *mgr;

	struct {
		int nssc;
		int ssc;
	} ref_clks;
};

struct intel_hotplug {
	struct delayed_work hotplug_work;

	const u32 *hpd, *pch_hpd;

	struct {
		unsigned long last_jiffies;
		int count;
		enum {
			HPD_ENABLED = 0,
			HPD_DISABLED = 1,
			HPD_MARK_DISABLED = 2
		} state;
	} stats[HPD_NUM_PINS];
	u32 event_bits;
	u32 retry_bits;
	struct delayed_work reenable_work;

	u32 long_port_mask;
	u32 short_port_mask;
	struct work_struct dig_port_work;

	struct work_struct poll_init_work;
	bool poll_enabled;

	unsigned int hpd_storm_threshold;
	/* Whether or not to count short HPD IRQs in HPD storms */
	u8 hpd_short_storm_enabled;

	/*
	 * if we get a HPD irq from DP and a HPD irq from non-DP
	 * the non-DP HPD could block the workqueue on a mode config
	 * mutex getting, that userspace may have taken. However
	 * userspace is waiting on the DP workqueue to run which is
	 * blocked behind the non-DP one.
	 */
	struct workqueue_struct *dp_wq;
};

struct intel_wm {
	/*
	 * Raw watermark latency values:
	 * in 0.1us units for WM0,
	 * in 0.5us units for WM1+.
	 */
	/* primary */
	u16 pri_latency[5];
	/* sprite */
	u16 spr_latency[5];
	/* cursor */
	u16 cur_latency[5];
	/*
	 * Raw watermark memory latency values
	 * for SKL for all 8 levels
	 * in 1us units.
	 */
	u16 skl_latency[8];

	/* current hardware state */
	union {
		struct ilk_wm_values hw;
		struct vlv_wm_values vlv;
		struct g4x_wm_values g4x;
	};

	u8 max_level;

	/*
	 * Should be held around atomic WM register writing; also
	 * protects * intel_crtc->wm.active and
	 * crtc_state->wm.need_postvbl_update.
	 */
	struct mutex wm_mutex;
};

struct intel_display {
	/* Display functions */
	struct {
		/* Top level crtc-ish functions */
		const struct intel_display_funcs *display;

		/* Display CDCLK functions */
		const struct intel_cdclk_funcs *cdclk;

		/* Display pll funcs */
		const struct intel_dpll_funcs *dpll;

		/* irq display functions */
		const struct intel_hotplug_funcs *hotplug;

		/* pm display functions */
		const struct intel_wm_funcs *wm;

		/* fdi display functions */
		const struct intel_fdi_funcs *fdi;

		/* Display internal color functions */
		const struct intel_color_funcs *color;

		/* Display internal audio functions */
		const struct intel_audio_funcs *audio;
	} funcs;

	/* Grouping using anonymous structs. Keep sorted. */
	struct {
		/* list of fbdev register on this device */
		struct intel_fbdev *fbdev;
		struct work_struct suspend_work;
	} fbdev;

	struct {
		/*
		 * Base address of where the gmbus and gpio blocks are located
		 * (either on PCH or on SoC for platforms without PCH).
		 */
		u32 mmio_base;

		/*
		 * gmbus.mutex protects against concurrent usage of the single
		 * hw gmbus controller on different i2c buses.
		 */
		struct mutex mutex;

		struct intel_gmbus *bus[GMBUS_NUM_PINS];

		wait_queue_head_t wait_queue;
	} gmbus;

	struct {
		u32 mmio_base;

		/* protects panel power sequencer state */
		struct mutex mutex;
	} pps;

	struct {
		enum {
			I915_SAGV_UNKNOWN = 0,
			I915_SAGV_DISABLED,
			I915_SAGV_ENABLED,
			I915_SAGV_NOT_CONTROLLED
		} status;

		u32 block_time_us;
	} sagv;

	/* Grouping using named structs. Keep sorted. */
	struct intel_audio audio;
	struct intel_dmc dmc;
	struct intel_dpll dpll;
	struct intel_hotplug hotplug;
	struct intel_overlay *overlay;
	struct intel_wm wm;
};

#endif /* __INTEL_DISPLAY_CORE_H__ */
