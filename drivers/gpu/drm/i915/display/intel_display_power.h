/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_POWER_H__
#define __INTEL_DISPLAY_POWER_H__

#include "intel_display.h"
#include "intel_runtime_pm.h"
#include "i915_reg.h"

struct drm_i915_private;
struct intel_encoder;

enum intel_display_power_domain {
	POWER_DOMAIN_DISPLAY_CORE,
	POWER_DOMAIN_PIPE_A,
	POWER_DOMAIN_PIPE_B,
	POWER_DOMAIN_PIPE_C,
	POWER_DOMAIN_PIPE_A_PANEL_FITTER,
	POWER_DOMAIN_PIPE_B_PANEL_FITTER,
	POWER_DOMAIN_PIPE_C_PANEL_FITTER,
	POWER_DOMAIN_TRANSCODER_A,
	POWER_DOMAIN_TRANSCODER_B,
	POWER_DOMAIN_TRANSCODER_C,
	POWER_DOMAIN_TRANSCODER_EDP,
	/* VDSC/joining for TRANSCODER_EDP (ICL) or TRANSCODER_A (TGL) */
	POWER_DOMAIN_TRANSCODER_VDSC_PW2,
	POWER_DOMAIN_TRANSCODER_DSI_A,
	POWER_DOMAIN_TRANSCODER_DSI_C,
	POWER_DOMAIN_PORT_DDI_A_LANES,
	POWER_DOMAIN_PORT_DDI_B_LANES,
	POWER_DOMAIN_PORT_DDI_C_LANES,
	POWER_DOMAIN_PORT_DDI_D_LANES,
	POWER_DOMAIN_PORT_DDI_TC1_LANES = POWER_DOMAIN_PORT_DDI_D_LANES,
	POWER_DOMAIN_PORT_DDI_E_LANES,
	POWER_DOMAIN_PORT_DDI_TC2_LANES = POWER_DOMAIN_PORT_DDI_E_LANES,
	POWER_DOMAIN_PORT_DDI_F_LANES,
	POWER_DOMAIN_PORT_DDI_TC3_LANES = POWER_DOMAIN_PORT_DDI_F_LANES,
	POWER_DOMAIN_PORT_DDI_TC4_LANES,
	POWER_DOMAIN_PORT_DDI_TC5_LANES,
	POWER_DOMAIN_PORT_DDI_TC6_LANES,
	POWER_DOMAIN_PORT_DDI_A_IO,
	POWER_DOMAIN_PORT_DDI_B_IO,
	POWER_DOMAIN_PORT_DDI_C_IO,
	POWER_DOMAIN_PORT_DDI_D_IO,
	POWER_DOMAIN_PORT_DDI_TC1_IO = POWER_DOMAIN_PORT_DDI_D_IO,
	POWER_DOMAIN_PORT_DDI_E_IO,
	POWER_DOMAIN_PORT_DDI_TC2_IO = POWER_DOMAIN_PORT_DDI_E_IO,
	POWER_DOMAIN_PORT_DDI_F_IO,
	POWER_DOMAIN_PORT_DDI_TC3_IO = POWER_DOMAIN_PORT_DDI_F_IO,
	POWER_DOMAIN_PORT_DDI_G_IO,
	POWER_DOMAIN_PORT_DDI_TC4_IO = POWER_DOMAIN_PORT_DDI_G_IO,
	POWER_DOMAIN_PORT_DDI_H_IO,
	POWER_DOMAIN_PORT_DDI_TC5_IO = POWER_DOMAIN_PORT_DDI_H_IO,
	POWER_DOMAIN_PORT_DDI_I_IO,
	POWER_DOMAIN_PORT_DDI_TC6_IO = POWER_DOMAIN_PORT_DDI_I_IO,
	POWER_DOMAIN_PORT_DSI,
	POWER_DOMAIN_PORT_CRT,
	POWER_DOMAIN_PORT_OTHER,
	POWER_DOMAIN_VGA,
	POWER_DOMAIN_AUDIO,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_AUX_D,
	POWER_DOMAIN_AUX_TC1 = POWER_DOMAIN_AUX_D,
	POWER_DOMAIN_AUX_E,
	POWER_DOMAIN_AUX_TC2 = POWER_DOMAIN_AUX_E,
	POWER_DOMAIN_AUX_F,
	POWER_DOMAIN_AUX_TC3 = POWER_DOMAIN_AUX_F,
	POWER_DOMAIN_AUX_TC4,
	POWER_DOMAIN_AUX_TC5,
	POWER_DOMAIN_AUX_TC6,
	POWER_DOMAIN_AUX_IO_A,
	POWER_DOMAIN_AUX_TBT1,
	POWER_DOMAIN_AUX_TBT2,
	POWER_DOMAIN_AUX_TBT3,
	POWER_DOMAIN_AUX_TBT4,
	POWER_DOMAIN_AUX_TBT5,
	POWER_DOMAIN_AUX_TBT6,
	POWER_DOMAIN_GMBUS,
	POWER_DOMAIN_MODESET,
	POWER_DOMAIN_GT_IRQ,
	POWER_DOMAIN_DPLL_DC_OFF,
	POWER_DOMAIN_INIT,

	POWER_DOMAIN_NUM,
};

#define POWER_DOMAIN_PIPE(pipe) ((pipe) + POWER_DOMAIN_PIPE_A)
#define POWER_DOMAIN_PIPE_PANEL_FITTER(pipe) \
		((pipe) + POWER_DOMAIN_PIPE_A_PANEL_FITTER)
#define POWER_DOMAIN_TRANSCODER(tran) \
	((tran) == TRANSCODER_EDP ? POWER_DOMAIN_TRANSCODER_EDP : \
	 (tran) + POWER_DOMAIN_TRANSCODER_A)

struct i915_power_well;

struct i915_power_well_ops {
	/*
	 * Synchronize the well's hw state to match the current sw state, for
	 * example enable/disable it based on the current refcount. Called
	 * during driver init and resume time, possibly after first calling
	 * the enable/disable handlers.
	 */
	void (*sync_hw)(struct drm_i915_private *dev_priv,
			struct i915_power_well *power_well);
	/*
	 * Enable the well and resources that depend on it (for example
	 * interrupts located on the well). Called after the 0->1 refcount
	 * transition.
	 */
	void (*enable)(struct drm_i915_private *dev_priv,
		       struct i915_power_well *power_well);
	/*
	 * Disable the well and resources that depend on it. Called after
	 * the 1->0 refcount transition.
	 */
	void (*disable)(struct drm_i915_private *dev_priv,
			struct i915_power_well *power_well);
	/* Returns the hw enabled state. */
	bool (*is_enabled)(struct drm_i915_private *dev_priv,
			   struct i915_power_well *power_well);
};

struct i915_power_well_regs {
	i915_reg_t bios;
	i915_reg_t driver;
	i915_reg_t kvmr;
	i915_reg_t debug;
};

/* Power well structure for haswell */
struct i915_power_well_desc {
	const char *name;
	bool always_on;
	u64 domains;
	/* unique identifier for this power well */
	enum i915_power_well_id id;
	/*
	 * Arbitraty data associated with this power well. Platform and power
	 * well specific.
	 */
	union {
		struct {
			/*
			 * request/status flag index in the PUNIT power well
			 * control/status registers.
			 */
			u8 idx;
		} vlv;
		struct {
			enum dpio_phy phy;
		} bxt;
		struct {
			const struct i915_power_well_regs *regs;
			/*
			 * request/status flag index in the power well
			 * constrol/status registers.
			 */
			u8 idx;
			/* Mask of pipes whose IRQ logic is backed by the pw */
			u8 irq_pipe_mask;
			/* The pw is backing the VGA functionality */
			bool has_vga:1;
			bool has_fuses:1;
			/*
			 * The pw is for an ICL+ TypeC PHY port in
			 * Thunderbolt mode.
			 */
			bool is_tc_tbt:1;
		} hsw;
	};
	const struct i915_power_well_ops *ops;
};

struct i915_power_well {
	const struct i915_power_well_desc *desc;
	/* power well enable/disable usage count */
	int count;
	/* cached hw enabled state */
	bool hw_enabled;
};

struct i915_power_domains {
	/*
	 * Power wells needed for initialization at driver init and suspend
	 * time are on. They are kept on until after the first modeset.
	 */
	bool initializing;
	bool display_core_suspended;
	int power_well_count;

	intel_wakeref_t wakeref;

	struct mutex lock;
	int domain_use_count[POWER_DOMAIN_NUM];

	struct delayed_work async_put_work;
	intel_wakeref_t async_put_wakeref;
	u64 async_put_domains[2];

	struct i915_power_well *power_wells;
};

#define for_each_power_domain(domain, mask)				\
	for ((domain) = 0; (domain) < POWER_DOMAIN_NUM; (domain)++)	\
		for_each_if(BIT_ULL(domain) & (mask))

#define for_each_power_well(__dev_priv, __power_well)				\
	for ((__power_well) = (__dev_priv)->power_domains.power_wells;	\
	     (__power_well) - (__dev_priv)->power_domains.power_wells <	\
		(__dev_priv)->power_domains.power_well_count;		\
	     (__power_well)++)

#define for_each_power_well_reverse(__dev_priv, __power_well)			\
	for ((__power_well) = (__dev_priv)->power_domains.power_wells +		\
			      (__dev_priv)->power_domains.power_well_count - 1;	\
	     (__power_well) - (__dev_priv)->power_domains.power_wells >= 0;	\
	     (__power_well)--)

#define for_each_power_domain_well(__dev_priv, __power_well, __domain_mask)	\
	for_each_power_well(__dev_priv, __power_well)				\
		for_each_if((__power_well)->desc->domains & (__domain_mask))

#define for_each_power_domain_well_reverse(__dev_priv, __power_well, __domain_mask) \
	for_each_power_well_reverse(__dev_priv, __power_well)		        \
		for_each_if((__power_well)->desc->domains & (__domain_mask))

void skl_enable_dc6(struct drm_i915_private *dev_priv);
void gen9_sanitize_dc_state(struct drm_i915_private *dev_priv);
void bxt_enable_dc9(struct drm_i915_private *dev_priv);
void bxt_disable_dc9(struct drm_i915_private *dev_priv);
void gen9_enable_dc5(struct drm_i915_private *dev_priv);

int intel_power_domains_init(struct drm_i915_private *dev_priv);
void intel_power_domains_cleanup(struct drm_i915_private *dev_priv);
void intel_power_domains_init_hw(struct drm_i915_private *dev_priv, bool resume);
void intel_power_domains_fini_hw(struct drm_i915_private *dev_priv);
void icl_display_core_init(struct drm_i915_private *dev_priv, bool resume);
void icl_display_core_uninit(struct drm_i915_private *dev_priv);
void intel_power_domains_enable(struct drm_i915_private *dev_priv);
void intel_power_domains_disable(struct drm_i915_private *dev_priv);
void intel_power_domains_suspend(struct drm_i915_private *dev_priv,
				 enum i915_drm_suspend_mode);
void intel_power_domains_resume(struct drm_i915_private *dev_priv);
void hsw_enable_pc8(struct drm_i915_private *dev_priv);
void hsw_disable_pc8(struct drm_i915_private *dev_priv);
void bxt_display_core_init(struct drm_i915_private *dev_priv, bool resume);
void bxt_display_core_uninit(struct drm_i915_private *dev_priv);

const char *
intel_display_power_domain_str(struct drm_i915_private *i915,
			       enum intel_display_power_domain domain);

bool intel_display_power_is_enabled(struct drm_i915_private *dev_priv,
				    enum intel_display_power_domain domain);
bool __intel_display_power_is_enabled(struct drm_i915_private *dev_priv,
				      enum intel_display_power_domain domain);
intel_wakeref_t intel_display_power_get(struct drm_i915_private *dev_priv,
					enum intel_display_power_domain domain);
intel_wakeref_t
intel_display_power_get_if_enabled(struct drm_i915_private *dev_priv,
				   enum intel_display_power_domain domain);
void intel_display_power_put_unchecked(struct drm_i915_private *dev_priv,
				       enum intel_display_power_domain domain);
void __intel_display_power_put_async(struct drm_i915_private *i915,
				     enum intel_display_power_domain domain,
				     intel_wakeref_t wakeref);
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
	__intel_display_power_put_async(i915, domain, wakeref);
}
#else
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
	__intel_display_power_put_async(i915, domain, -1);
}
#endif

#define with_intel_display_power(i915, domain, wf) \
	for ((wf) = intel_display_power_get((i915), (domain)); (wf); \
	     intel_display_power_put_async((i915), (domain), (wf)), (wf) = 0)

void icl_dbuf_slices_update(struct drm_i915_private *dev_priv,
			    u8 req_slices);

void chv_phy_powergate_lanes(struct intel_encoder *encoder,
			     bool override, unsigned int mask);
bool chv_phy_powergate_ch(struct drm_i915_private *dev_priv, enum dpio_phy phy,
			  enum dpio_channel ch, bool override);

#endif /* __INTEL_DISPLAY_POWER_H__ */
