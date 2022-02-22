/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */
#ifndef __INTEL_DISPLAY_POWER_WELL_H__
#define __INTEL_DISPLAY_POWER_WELL_H__

#include <linux/types.h>

#include "intel_display.h"
#include "intel_display_power.h"

struct drm_i915_private;

struct i915_power_well_regs {
	i915_reg_t bios;
	i915_reg_t driver;
	i915_reg_t kvmr;
	i915_reg_t debug;
};

struct i915_power_well_ops {
	const struct i915_power_well_regs *regs;
	/*
	 * Synchronize the well's hw state to match the current sw state, for
	 * example enable/disable it based on the current refcount. Called
	 * during driver init and resume time, possibly after first calling
	 * the enable/disable handlers.
	 */
	void (*sync_hw)(struct drm_i915_private *i915,
			struct i915_power_well *power_well);
	/*
	 * Enable the well and resources that depend on it (for example
	 * interrupts located on the well). Called after the 0->1 refcount
	 * transition.
	 */
	void (*enable)(struct drm_i915_private *i915,
		       struct i915_power_well *power_well);
	/*
	 * Disable the well and resources that depend on it. Called after
	 * the 1->0 refcount transition.
	 */
	void (*disable)(struct drm_i915_private *i915,
			struct i915_power_well *power_well);
	/* Returns the hw enabled state. */
	bool (*is_enabled)(struct drm_i915_private *i915,
			   struct i915_power_well *power_well);
};

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
			/*
			 * request/status flag index in the power well
			 * constrol/status registers.
			 */
			u8 idx;
			/* Mask of pipes whose IRQ logic is backed by the pw */
			u8 irq_pipe_mask;
			/*
			 * Instead of waiting for the status bit to ack enables,
			 * just wait a specific amount of time and then consider
			 * the well enabled.
			 */
			u16 fixed_enable_delay;
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

void intel_power_well_enable(struct drm_i915_private *i915,
			     struct i915_power_well *power_well);
void intel_power_well_disable(struct drm_i915_private *i915,
			      struct i915_power_well *power_well);
void intel_power_well_sync_hw(struct drm_i915_private *i915,
			      struct i915_power_well *power_well);
void intel_power_well_get(struct drm_i915_private *i915,
			  struct i915_power_well *power_well);
void intel_power_well_put(struct drm_i915_private *i915,
			  struct i915_power_well *power_well);
bool intel_power_well_is_enabled(struct drm_i915_private *i915,
				 struct i915_power_well *power_well);
bool intel_power_well_is_enabled_cached(struct i915_power_well *power_well);
bool intel_power_well_is_always_on(struct i915_power_well *power_well);
const char *intel_power_well_name(struct i915_power_well *power_well);
u64 intel_power_well_domains(struct i915_power_well *power_well);
int intel_power_well_refcount(struct i915_power_well *power_well);

#endif
