// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"

#include "intel_de.h"
#include "intel_display.h"
#include "intel_dkl_phy.h"
#include "intel_dkl_phy_regs.h"

/**
 * intel_dkl_phy_init - initialize Dekel PHY
 * @i915: i915 device instance
 */
void intel_dkl_phy_init(struct drm_i915_private *i915)
{
	spin_lock_init(&i915->display.dkl.phy_lock);
}

static void
dkl_phy_set_hip_idx(struct drm_i915_private *i915, struct intel_dkl_phy_reg reg)
{
	enum tc_port tc_port = DKL_REG_TC_PORT(reg);

	drm_WARN_ON(&i915->drm, tc_port < TC_PORT_1 || tc_port >= I915_MAX_TC_PORTS);

	intel_de_write(i915,
		       HIP_INDEX_REG(tc_port),
		       HIP_INDEX_VAL(tc_port, reg.bank_idx));
}

/**
 * intel_dkl_phy_read - read a Dekel PHY register
 * @i915: i915 device instance
 * @reg: Dekel PHY register
 *
 * Read the @reg Dekel PHY register.
 *
 * Returns the read value.
 */
u32
intel_dkl_phy_read(struct drm_i915_private *i915, struct intel_dkl_phy_reg reg)
{
	u32 val;

	spin_lock(&i915->display.dkl.phy_lock);

	dkl_phy_set_hip_idx(i915, reg);
	val = intel_de_read(i915, DKL_REG_MMIO(reg));

	spin_unlock(&i915->display.dkl.phy_lock);

	return val;
}

/**
 * intel_dkl_phy_write - write a Dekel PHY register
 * @i915: i915 device instance
 * @reg: Dekel PHY register
 * @val: value to write
 *
 * Write @val to the @reg Dekel PHY register.
 */
void
intel_dkl_phy_write(struct drm_i915_private *i915, struct intel_dkl_phy_reg reg, u32 val)
{
	spin_lock(&i915->display.dkl.phy_lock);

	dkl_phy_set_hip_idx(i915, reg);
	intel_de_write(i915, DKL_REG_MMIO(reg), val);

	spin_unlock(&i915->display.dkl.phy_lock);
}

/**
 * intel_dkl_phy_rmw - read-modify-write a Dekel PHY register
 * @i915: i915 device instance
 * @reg: Dekel PHY register
 * @clear: mask to clear
 * @set: mask to set
 *
 * Read the @reg Dekel PHY register, clearing then setting the @clear/@set bits in it, and writing
 * this value back to the register if the value differs from the read one.
 */
void
intel_dkl_phy_rmw(struct drm_i915_private *i915, struct intel_dkl_phy_reg reg, u32 clear, u32 set)
{
	spin_lock(&i915->display.dkl.phy_lock);

	dkl_phy_set_hip_idx(i915, reg);
	intel_de_rmw(i915, DKL_REG_MMIO(reg), clear, set);

	spin_unlock(&i915->display.dkl.phy_lock);
}

/**
 * intel_dkl_phy_posting_read - do a posting read from a Dekel PHY register
 * @i915: i915 device instance
 * @reg: Dekel PHY register
 *
 * Read the @reg Dekel PHY register without returning the read value.
 */
void
intel_dkl_phy_posting_read(struct drm_i915_private *i915, struct intel_dkl_phy_reg reg)
{
	spin_lock(&i915->display.dkl.phy_lock);

	dkl_phy_set_hip_idx(i915, reg);
	intel_de_posting_read(i915, DKL_REG_MMIO(reg));

	spin_unlock(&i915->display.dkl.phy_lock);
}
