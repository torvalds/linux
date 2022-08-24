/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_CORE_H__
#define __INTEL_DISPLAY_CORE_H__

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "intel_display.h"
#include "intel_dmc.h"
#include "intel_gmbus.h"

struct drm_i915_private;
struct i915_audio_component;
struct intel_atomic_state;
struct intel_audio_funcs;
struct intel_cdclk_funcs;
struct intel_color_funcs;
struct intel_crtc;
struct intel_crtc_state;
struct intel_dpll_funcs;
struct intel_fdi_funcs;
struct intel_hotplug_funcs;
struct intel_initial_plane_config;

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

	/* Grouping using named structs. Keep sorted. */
	struct intel_audio audio;
	struct intel_dmc dmc;
};

#endif /* __INTEL_DISPLAY_CORE_H__ */
