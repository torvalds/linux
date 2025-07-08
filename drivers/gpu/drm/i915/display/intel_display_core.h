/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_CORE_H__
#define __INTEL_DISPLAY_CORE_H__

#include <linux/list.h>
#include <linux/llist.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <drm/drm_connector.h>
#include <drm/drm_modeset_lock.h>

#include "intel_cdclk.h"
#include "intel_display_device.h"
#include "intel_display_limits.h"
#include "intel_display_params.h"
#include "intel_display_power.h"
#include "intel_dmc_wl.h"
#include "intel_dpll_mgr.h"
#include "intel_fbc.h"
#include "intel_global_state.h"
#include "intel_gmbus.h"
#include "intel_opregion.h"
#include "intel_pch.h"
#include "intel_wm_types.h"

struct drm_property;
struct drm_property_blob;
struct i915_audio_component;
struct i915_hdcp_arbiter;
struct intel_atomic_state;
struct intel_audio_funcs;
struct intel_cdclk_funcs;
struct intel_cdclk_vals;
struct intel_color_funcs;
struct intel_crtc;
struct intel_crtc_state;
struct intel_dmc;
struct intel_dpll_global_funcs;
struct intel_dpll_mgr;
struct intel_fbdev;
struct intel_fdi_funcs;
struct intel_hotplug_funcs;
struct intel_initial_plane_config;
struct intel_opregion;
struct intel_overlay;
struct task_struct;

/* Amount of SAGV/QGV points, BSpec precisely defines this */
#define I915_NUM_QGV_POINTS 8

/* Amount of PSF GV points, BSpec precisely defines this */
#define I915_NUM_PSF_GV_POINTS 3

struct intel_display_funcs {
	/*
	 * Returns the active state of the crtc, and if the crtc is active,
	 * fills out the pipe-config with the hw state.
	 */
	bool (*get_pipe_config)(struct intel_crtc *,
				struct intel_crtc_state *);
	void (*get_initial_plane_config)(struct intel_crtc *,
					 struct intel_initial_plane_config *);
	bool (*fixup_initial_plane_config)(struct intel_crtc *crtc,
					   const struct intel_initial_plane_config *plane_config);
	void (*crtc_enable)(struct intel_atomic_state *state,
			    struct intel_crtc *crtc);
	void (*crtc_disable)(struct intel_atomic_state *state,
			     struct intel_crtc *crtc);
	void (*commit_modeset_enables)(struct intel_atomic_state *state);
};

/* functions used for watermark calcs for display. */
struct intel_wm_funcs {
	/* update_wm is for legacy wm management */
	void (*update_wm)(struct intel_display *display);
	int (*compute_watermarks)(struct intel_atomic_state *state,
				  struct intel_crtc *crtc);
	void (*initial_watermarks)(struct intel_atomic_state *state,
				   struct intel_crtc *crtc);
	void (*atomic_update_watermarks)(struct intel_atomic_state *state,
					 struct intel_crtc *crtc);
	void (*optimize_watermarks)(struct intel_atomic_state *state,
				    struct intel_crtc *crtc);
	int (*compute_global_watermarks)(struct intel_atomic_state *state);
	void (*get_hw_state)(struct intel_display *display);
	void (*sanitize)(struct intel_display *display);
};

struct intel_audio_state {
	struct intel_encoder *encoder;
	u8 eld[MAX_ELD_BYTES];
};

struct intel_audio {
	/* hda/i915 audio component */
	struct i915_audio_component *component;
	bool component_registered;
	/* mutex for audio/video sync */
	struct mutex mutex;
	int power_refcount;
	u32 freq_cntrl;

	/* current audio state for the audio component hooks */
	struct intel_audio_state state[I915_MAX_TRANSCODERS];

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
struct intel_dpll_global {
	struct mutex lock;

	int num_dpll;
	struct intel_dpll dplls[I915_NUM_PLLS];
	const struct intel_dpll_mgr *mgr;

	struct {
		int nssc;
		int ssc;
	} ref_clks;

	/*
	 * Bitmask of PLLs using the PCH SSC, indexed using enum intel_dpll_id.
	 */
	u8 pch_ssc_use;
};

struct intel_frontbuffer_tracking {
	spinlock_t lock;

	/*
	 * Tracking bits for delayed frontbuffer flushing du to gpu activity or
	 * scheduled flips.
	 */
	unsigned busy_bits;
	unsigned flip_bits;
};

struct intel_hotplug {
	struct delayed_work hotplug_work;

	const u32 *hpd, *pch_hpd;

	struct {
		unsigned long last_jiffies;
		int count;
		int blocked_count;
		enum {
			HPD_ENABLED = 0,
			HPD_DISABLED = 1,
			HPD_MARK_DISABLED = 2
		} state;
	} stats[HPD_NUM_PINS];
	u32 event_bits;
	u32 retry_bits;
	struct delayed_work reenable_work;

	u32 long_hpd_pin_mask;
	u32 short_hpd_pin_mask;
	struct work_struct dig_port_work;

	struct work_struct poll_init_work;
	bool poll_enabled;

	/*
	 * Queuing of hotplug_work, reenable_work and poll_init_work is
	 * enabled. Protected by intel_display::irq::lock.
	 */
	bool detection_work_enabled;

	unsigned int hpd_storm_threshold;
	/* Whether or not to count short HPD IRQs in HPD storms */
	u8 hpd_short_storm_enabled;

	/* Last state reported by oob_hotplug_event for each encoder */
	unsigned long oob_hotplug_last_state;

	/*
	 * if we get a HPD irq from DP and a HPD irq from non-DP
	 * the non-DP HPD could block the workqueue on a mode config
	 * mutex getting, that userspace may have taken. However
	 * userspace is waiting on the DP workqueue to run which is
	 * blocked behind the non-DP one.
	 */
	struct workqueue_struct *dp_wq;

	/*
	 * Flag to track if long HPDs need not to be processed
	 *
	 * Some panels generate long HPDs while keep connected to the port.
	 * This can cause issues with CI tests results. In CI systems we
	 * don't expect to disconnect the panels and could ignore the long
	 * HPDs generated from the faulty panels. This flag can be used as
	 * cue to ignore the long HPDs and can be set / unset using debugfs.
	 */
	bool ignore_long_hpd;
};

struct intel_vbt_data {
	/* bdb version */
	u16 version;

	/* Feature bits */
	unsigned int int_tv_support:1;
	unsigned int int_crt_support:1;
	unsigned int lvds_use_ssc:1;
	unsigned int int_lvds_support:1;
	unsigned int display_clock_mode:1;
	unsigned int fdi_rx_polarity_inverted:1;
	int lvds_ssc_freq;
	enum drm_panel_orientation orientation;

	bool override_afc_startup;
	u8 override_afc_startup_val;

	int crt_ddc_pin;

	struct list_head display_devices;
	struct list_head bdb_blocks;

	struct sdvo_device_mapping {
		u8 initialized;
		u8 dvo_port;
		u8 target_addr;
		u8 dvo_wiring;
		u8 i2c_pin;
		u8 ddc_pin;
	} sdvo_mappings[2];
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

	u8 num_levels;

	/*
	 * Should be held around atomic WM register writing; also
	 * protects * intel_crtc->wm.active and
	 * crtc_state->wm.need_postvbl_update.
	 */
	struct mutex wm_mutex;

	bool ipc_enabled;
};

struct intel_display {
	/* drm device backpointer */
	struct drm_device *drm;

	/* Platform (and subplatform, if any) identification */
	struct intel_display_platforms platform;

	/* Intel PCH: where the south display engine lives */
	enum intel_pch pch_type;

	/* Display functions */
	struct {
		/* Top level crtc-ish functions */
		const struct intel_display_funcs *display;

		/* Display CDCLK functions */
		const struct intel_cdclk_funcs *cdclk;

		/* Display pll funcs */
		const struct intel_dpll_global_funcs *dpll;

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

	struct {
		bool any_task_allowed;
		struct task_struct *allowed_task;
	} access;

	struct {
		/* backlight registers and fields in struct intel_panel */
		struct mutex lock;
	} backlight;

	struct {
		struct intel_global_obj obj;

		struct intel_bw_info {
			/* for each QGV point */
			unsigned int deratedbw[I915_NUM_QGV_POINTS];
			/* for each PSF GV point */
			unsigned int psf_bw[I915_NUM_PSF_GV_POINTS];
			/* Peak BW for each QGV point */
			unsigned int peakbw[I915_NUM_QGV_POINTS];
			u8 num_qgv_points;
			u8 num_psf_gv_points;
			u8 num_planes;
		} max[6];
	} bw;

	struct {
		/* The current hardware cdclk configuration */
		struct intel_cdclk_config hw;

		/* cdclk, divider, and ratio table from bspec */
		const struct intel_cdclk_vals *table;

		struct intel_global_obj obj;

		unsigned int max_cdclk_freq;
		unsigned int max_dotclk_freq;
		unsigned int skl_preferred_vco_freq;
	} cdclk;

	struct {
		struct drm_property_blob *glk_linear_degamma_lut;
	} color;

	struct {
		/* The current hardware dbuf configuration */
		u8 enabled_slices;

		struct intel_global_obj obj;
	} dbuf;

	struct {
		/*
		 * dkl.phy_lock protects against concurrent access of the
		 * Dekel TypeC PHYs.
		 */
		spinlock_t phy_lock;
	} dkl;

	struct {
		struct intel_dmc *dmc;
		intel_wakeref_t wakeref;
	} dmc;

	struct {
		/* VLV/CHV/BXT/GLK DSI MMIO register base address */
		u32 mmio_base;
	} dsi;

	struct {
		/* list of fbdev register on this device */
		struct intel_fbdev *fbdev;
	} fbdev;

	struct {
		unsigned int pll_freq;
		u32 rx_config;
	} fdi;

	struct {
		struct list_head obj_list;
	} global;

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
		struct i915_hdcp_arbiter *arbiter;
		bool comp_added;

		/*
		 * HDCP message struct for allocation of memory which can be
		 * reused when sending message to gsc cs.
		 * this is only populated post Meteorlake
		 */
		struct intel_hdcp_gsc_context *gsc_context;
		/* Mutex to protect the above hdcp related values. */
		struct mutex hdcp_mutex;
	} hdcp;

	struct {
		/*
		 * HTI (aka HDPORT) state read during initial hw readout. Most
		 * platforms don't have HTI, so this will just stay 0. Those
		 * that do will use this later to figure out which PLLs and PHYs
		 * are unavailable for driver usage.
		 */
		u32 state;
	} hti;

	struct {
		/* Access with DISPLAY_INFO() */
		const struct intel_display_device_info *__device_info;

		/* Access with DISPLAY_RUNTIME_INFO() */
		struct intel_display_runtime_info __runtime_info;
	} info;

	struct {
		bool false_color;
	} ips;

	struct {
		/* protects the irq masks */
		spinlock_t lock;

		/*
		 * Most platforms treat the display irq block as an always-on
		 * power domain. vlv/chv can disable it at runtime and need
		 * special care to avoid writing any of the display block
		 * registers outside of the power domain. We defer setting up
		 * the display irqs in this case to the runtime pm.
		 */
		bool vlv_display_irqs_enabled;

		/* For i915gm/i945gm vblank irq workaround */
		u8 vblank_enabled;

		int vblank_enable_count;

		struct work_struct vblank_notify_work;

		u32 de_irq_mask[I915_MAX_PIPES];
		u32 pipestat_irq_mask[I915_MAX_PIPES];
	} irq;

	struct {
		/* protected by wm.wm_mutex */
		u16 linetime[I915_MAX_PIPES];
		bool disable[I915_MAX_PIPES];
	} pkgc;

	struct {
		wait_queue_head_t waitqueue;

		/* mutex to protect pmdemand programming sequence */
		struct mutex lock;

		struct intel_global_obj obj;
	} pmdemand;

	struct {
		struct i915_power_domains domains;

		/* Shadow for DISPLAY_PHY_CONTROL which can't be safely read */
		u32 chv_phy_control;

		/* perform PHY state sanity checks? */
		bool chv_phy_assert[2];
	} power;

	struct {
		u32 mmio_base;

		/* protects panel power sequencer state */
		struct mutex mutex;
	} pps;

	struct {
		struct drm_property *broadcast_rgb;
		struct drm_property *force_audio;
	} properties;

	struct {
		unsigned long mask;
	} quirks;

	struct {
		/* restore state for suspend/resume and display reset */
		struct drm_atomic_state *modeset_state;
		struct drm_modeset_acquire_ctx reset_ctx;
		/* modeset stuck tracking for reset */
		atomic_t pending_fb_pin;
		u32 saveDSPARB;
		u32 saveSWF0[16];
		u32 saveSWF1[16];
		u32 saveSWF3[3];
		u16 saveGCDGMBUS;
	} restore;

	struct {
		enum {
			I915_SAGV_UNKNOWN = 0,
			I915_SAGV_DISABLED,
			I915_SAGV_ENABLED,
			I915_SAGV_NOT_CONTROLLED
		} status;

		u32 block_time_us;
	} sagv;

	struct {
		/* LPT/WPT IOSF sideband protection */
		struct mutex lock;
	} sbi;

	struct {
		/*
		 * DG2: Mask of PHYs that were not calibrated by the firmware
		 * and should not be used.
		 */
		u8 phy_failed_calibration;
	} snps;

	struct {
		/*
		 * Shadows for CHV DPLL_MD regs to keep the state
		 * checker somewhat working in the presence hardware
		 * crappiness (can't read out DPLL_MD for pipes B & C).
		 */
		u32 chv_dpll_md[I915_MAX_PIPES];
		u32 bxt_phy_grc;
	} state;

	struct {
		/* ordered wq for modesets */
		struct workqueue_struct *modeset;

		/* unbound hipri wq for page flips/plane updates */
		struct workqueue_struct *flip;

		/* hipri wq for commit cleanups */
		struct workqueue_struct *cleanup;

		/* unordered workqueue for all display unordered work */
		struct workqueue_struct *unordered;
	} wq;

	/* Grouping using named structs. Keep sorted. */
	struct drm_dp_tunnel_mgr *dp_tunnel_mgr;
	struct intel_audio audio;
	struct intel_dpll_global dpll;
	struct intel_fbc *fbc[I915_MAX_FBCS];
	struct intel_frontbuffer_tracking fb_tracking;
	struct intel_hotplug hotplug;
	struct intel_opregion *opregion;
	struct intel_overlay *overlay;
	struct intel_display_params params;
	struct intel_vbt_data vbt;
	struct intel_dmc_wl wl;
	struct intel_wm wm;

	struct work_struct psr_dc5_dc6_wa_work;
};

#endif /* __INTEL_DISPLAY_CORE_H__ */
