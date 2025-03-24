/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_POWER_H__
#define __INTEL_DISPLAY_POWER_H__

#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "intel_wakeref.h"

enum aux_ch;
enum port;
struct drm_i915_private;
struct i915_power_well;
struct intel_display;
struct intel_encoder;
struct seq_file;

/*
 * Keep the pipe, transcoder, port (DDI_LANES,DDI_IO,AUX) domain instances
 * consecutive, so that the pipe,transcoder,port -> power domain macros
 * work correctly.
 */
enum intel_display_power_domain {
	POWER_DOMAIN_DISPLAY_CORE,
	POWER_DOMAIN_PIPE_A,
	POWER_DOMAIN_PIPE_B,
	POWER_DOMAIN_PIPE_C,
	POWER_DOMAIN_PIPE_D,
	POWER_DOMAIN_PIPE_PANEL_FITTER_A,
	POWER_DOMAIN_PIPE_PANEL_FITTER_B,
	POWER_DOMAIN_PIPE_PANEL_FITTER_C,
	POWER_DOMAIN_PIPE_PANEL_FITTER_D,
	POWER_DOMAIN_TRANSCODER_A,
	POWER_DOMAIN_TRANSCODER_B,
	POWER_DOMAIN_TRANSCODER_C,
	POWER_DOMAIN_TRANSCODER_D,
	POWER_DOMAIN_TRANSCODER_EDP,
	POWER_DOMAIN_TRANSCODER_DSI_A,
	POWER_DOMAIN_TRANSCODER_DSI_C,

	/* VDSC/joining for eDP/DSI transcoder (ICL) or pipe A (TGL) */
	POWER_DOMAIN_TRANSCODER_VDSC_PW2,

	POWER_DOMAIN_PORT_DDI_LANES_A,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_PORT_DDI_LANES_D,
	POWER_DOMAIN_PORT_DDI_LANES_E,
	POWER_DOMAIN_PORT_DDI_LANES_F,

	POWER_DOMAIN_PORT_DDI_LANES_TC1,
	POWER_DOMAIN_PORT_DDI_LANES_TC2,
	POWER_DOMAIN_PORT_DDI_LANES_TC3,
	POWER_DOMAIN_PORT_DDI_LANES_TC4,
	POWER_DOMAIN_PORT_DDI_LANES_TC5,
	POWER_DOMAIN_PORT_DDI_LANES_TC6,

	POWER_DOMAIN_PORT_DDI_IO_A,
	POWER_DOMAIN_PORT_DDI_IO_B,
	POWER_DOMAIN_PORT_DDI_IO_C,
	POWER_DOMAIN_PORT_DDI_IO_D,
	POWER_DOMAIN_PORT_DDI_IO_E,
	POWER_DOMAIN_PORT_DDI_IO_F,

	POWER_DOMAIN_PORT_DDI_IO_TC1,
	POWER_DOMAIN_PORT_DDI_IO_TC2,
	POWER_DOMAIN_PORT_DDI_IO_TC3,
	POWER_DOMAIN_PORT_DDI_IO_TC4,
	POWER_DOMAIN_PORT_DDI_IO_TC5,
	POWER_DOMAIN_PORT_DDI_IO_TC6,

	POWER_DOMAIN_PORT_DSI,
	POWER_DOMAIN_PORT_CRT,
	POWER_DOMAIN_PORT_OTHER,
	POWER_DOMAIN_VGA,
	POWER_DOMAIN_AUDIO_MMIO,
	POWER_DOMAIN_AUDIO_PLAYBACK,

	POWER_DOMAIN_AUX_IO_A,
	POWER_DOMAIN_AUX_IO_B,
	POWER_DOMAIN_AUX_IO_C,
	POWER_DOMAIN_AUX_IO_D,
	POWER_DOMAIN_AUX_IO_E,
	POWER_DOMAIN_AUX_IO_F,

	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_AUX_D,
	POWER_DOMAIN_AUX_E,
	POWER_DOMAIN_AUX_F,

	POWER_DOMAIN_AUX_USBC1,
	POWER_DOMAIN_AUX_USBC2,
	POWER_DOMAIN_AUX_USBC3,
	POWER_DOMAIN_AUX_USBC4,
	POWER_DOMAIN_AUX_USBC5,
	POWER_DOMAIN_AUX_USBC6,

	POWER_DOMAIN_AUX_TBT1,
	POWER_DOMAIN_AUX_TBT2,
	POWER_DOMAIN_AUX_TBT3,
	POWER_DOMAIN_AUX_TBT4,
	POWER_DOMAIN_AUX_TBT5,
	POWER_DOMAIN_AUX_TBT6,

	POWER_DOMAIN_GMBUS,
	POWER_DOMAIN_GT_IRQ,
	POWER_DOMAIN_DC_OFF,
	POWER_DOMAIN_TC_COLD_OFF,
	POWER_DOMAIN_INIT,

	POWER_DOMAIN_NUM,
	POWER_DOMAIN_INVALID = POWER_DOMAIN_NUM,
};

#define POWER_DOMAIN_PIPE(pipe) ((pipe) + POWER_DOMAIN_PIPE_A)
#define POWER_DOMAIN_PIPE_PANEL_FITTER(pipe) \
		((pipe) + POWER_DOMAIN_PIPE_PANEL_FITTER_A)
#define POWER_DOMAIN_TRANSCODER(tran) \
	((tran) == TRANSCODER_EDP ? POWER_DOMAIN_TRANSCODER_EDP : \
	 (tran) + POWER_DOMAIN_TRANSCODER_A)

struct intel_power_domain_mask {
	DECLARE_BITMAP(bits, POWER_DOMAIN_NUM);
};

struct i915_power_domains {
	/*
	 * Power wells needed for initialization at driver init and suspend
	 * time are on. They are kept on until after the first modeset.
	 */
	bool initializing;
	bool display_core_suspended;
	int power_well_count;

	u32 dc_state;
	u32 target_dc_state;
	u32 allowed_dc_mask;

	intel_wakeref_t init_wakeref;
	intel_wakeref_t disable_wakeref;

	struct mutex lock;
	int domain_use_count[POWER_DOMAIN_NUM];

	struct delayed_work async_put_work;
	intel_wakeref_t async_put_wakeref;
	struct intel_power_domain_mask async_put_domains[2];
	int async_put_next_delay;

	struct i915_power_well *power_wells;
};

struct intel_display_power_domain_set {
	struct intel_power_domain_mask mask;
#ifdef CONFIG_DRM_I915_DEBUG_RUNTIME_PM
	intel_wakeref_t wakerefs[POWER_DOMAIN_NUM];
#endif
};

#define for_each_power_domain(__domain, __mask)				\
	for ((__domain) = 0; (__domain) < POWER_DOMAIN_NUM; (__domain)++)	\
		for_each_if(test_bit((__domain), (__mask)->bits))

int intel_power_domains_init(struct intel_display *display);
void intel_power_domains_cleanup(struct intel_display *display);
void intel_power_domains_init_hw(struct intel_display *display, bool resume);
void intel_power_domains_driver_remove(struct intel_display *display);
void intel_power_domains_enable(struct intel_display *display);
void intel_power_domains_disable(struct intel_display *display);
void intel_power_domains_suspend(struct intel_display *display, bool s2idle);
void intel_power_domains_resume(struct intel_display *display);
void intel_power_domains_sanitize_state(struct intel_display *display);

void intel_display_power_suspend_late(struct intel_display *display, bool s2idle);
void intel_display_power_resume_early(struct intel_display *display);
void intel_display_power_suspend(struct intel_display *display);
void intel_display_power_resume(struct intel_display *display);
void intel_display_power_set_target_dc_state(struct intel_display *display,
					     u32 state);

bool intel_display_power_is_enabled(struct drm_i915_private *dev_priv,
				    enum intel_display_power_domain domain);
intel_wakeref_t intel_display_power_get(struct drm_i915_private *dev_priv,
					enum intel_display_power_domain domain);
intel_wakeref_t
intel_display_power_get_if_enabled(struct drm_i915_private *dev_priv,
				   enum intel_display_power_domain domain);
void __intel_display_power_put_async(struct drm_i915_private *i915,
				     enum intel_display_power_domain domain,
				     intel_wakeref_t wakeref,
				     int delay_ms);
void intel_display_power_flush_work(struct drm_i915_private *i915);
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
void intel_display_power_put(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain,
			     intel_wakeref_t wakeref);
static inline void
intel_display_power_put_async(struct drm_i915_private *i915,
			      enum intel_display_power_domain domain,
			      intel_wakeref_t wakeref)
{
	__intel_display_power_put_async(i915, domain, wakeref, -1);
}

static inline void
intel_display_power_put_async_delay(struct drm_i915_private *i915,
				    enum intel_display_power_domain domain,
				    intel_wakeref_t wakeref,
				    int delay_ms)
{
	__intel_display_power_put_async(i915, domain, wakeref, delay_ms);
}
#else
void intel_display_power_put_unchecked(struct drm_i915_private *dev_priv,
				       enum intel_display_power_domain domain);

static inline void
intel_display_power_put(struct drm_i915_private *i915,
			enum intel_display_power_domain domain,
			intel_wakeref_t wakeref)
{
	intel_display_power_put_unchecked(i915, domain);
}

static inline void
intel_display_power_put_async(struct drm_i915_private *i915,
			      enum intel_display_power_domain domain,
			      intel_wakeref_t wakeref)
{
	__intel_display_power_put_async(i915, domain, INTEL_WAKEREF_DEF, -1);
}

static inline void
intel_display_power_put_async_delay(struct drm_i915_private *i915,
				    enum intel_display_power_domain domain,
				    intel_wakeref_t wakeref,
				    int delay_ms)
{
	__intel_display_power_put_async(i915, domain, INTEL_WAKEREF_DEF, delay_ms);
}
#endif

void
intel_display_power_get_in_set(struct drm_i915_private *i915,
			       struct intel_display_power_domain_set *power_domain_set,
			       enum intel_display_power_domain domain);

bool
intel_display_power_get_in_set_if_enabled(struct drm_i915_private *i915,
					  struct intel_display_power_domain_set *power_domain_set,
					  enum intel_display_power_domain domain);

void
intel_display_power_put_mask_in_set(struct drm_i915_private *i915,
				    struct intel_display_power_domain_set *power_domain_set,
				    struct intel_power_domain_mask *mask);

static inline void
intel_display_power_put_all_in_set(struct drm_i915_private *i915,
				   struct intel_display_power_domain_set *power_domain_set)
{
	intel_display_power_put_mask_in_set(i915, power_domain_set, &power_domain_set->mask);
}

void intel_display_power_debug(struct drm_i915_private *i915, struct seq_file *m);

enum intel_display_power_domain
intel_display_power_ddi_lanes_domain(struct drm_i915_private *i915, enum port port);
enum intel_display_power_domain
intel_display_power_ddi_io_domain(struct drm_i915_private *i915, enum port port);
enum intel_display_power_domain
intel_display_power_aux_io_domain(struct drm_i915_private *i915, enum aux_ch aux_ch);
enum intel_display_power_domain
intel_display_power_legacy_aux_domain(struct drm_i915_private *i915, enum aux_ch aux_ch);
enum intel_display_power_domain
intel_display_power_tbt_aux_domain(struct drm_i915_private *i915, enum aux_ch aux_ch);

/*
 * FIXME: We should probably switch this to a 0-based scheme to be consistent
 * with how we now name/number DBUF_CTL instances.
 */
enum dbuf_slice {
	DBUF_S1,
	DBUF_S2,
	DBUF_S3,
	DBUF_S4,
	I915_MAX_DBUF_SLICES
};

void gen9_dbuf_slices_update(struct drm_i915_private *dev_priv,
			     u8 req_slices);

#define with_intel_display_power(i915, domain, wf) \
	for ((wf) = intel_display_power_get((i915), (domain)); (wf); \
	     intel_display_power_put_async((i915), (domain), (wf)), (wf) = NULL)

#define with_intel_display_power_if_enabled(i915, domain, wf) \
	for ((wf) = intel_display_power_get_if_enabled((i915), (domain)); (wf); \
	     intel_display_power_put_async((i915), (domain), (wf)), (wf) = NULL)

#endif /* __INTEL_DISPLAY_POWER_H__ */
