// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <drm/drm_device.h>

#include "intel_de.h"
#include "intel_display.h"
#include "intel_dkl_phy.h"
#include "intel_dkl_phy_regs.h"

/**
 * intel_dkl_phy_init - initialize Dekel PHY
 * @display: display device instance
 */
void intel_dkl_phy_init(struct intel_display *display)
{
	spin_lock_init(&display->dkl.phy_lock);
}

static void
dkl_phy_set_hip_idx(struct intel_display *display, struct intel_dkl_phy_reg reg)
{
	enum tc_port tc_port = DKL_REG_TC_PORT(reg);

	drm_WARN_ON(display->drm, tc_port < TC_PORT_1 || tc_port >= I915_MAX_TC_PORTS);

	intel_de_write(display,
		       HIP_INDEX_REG(tc_port),
		       HIP_INDEX_VAL(tc_port, reg.bank_idx));
}

/**
 * intel_dkl_phy_read - read a Dekel PHY register
 * @display: intel_display device instance
 * @reg: Dekel PHY register
 *
 * Read the @reg Dekel PHY register.
 *
 * Returns the read value.
 */
u32
intel_dkl_phy_read(struct intel_display *display, struct intel_dkl_phy_reg reg)
{
	u32 val;

	spin_lock(&display->dkl.phy_lock);

	dkl_phy_set_hip_idx(display, reg);
	val = intel_de_read(display, DKL_REG_MMIO(reg));

	spin_unlock(&display->dkl.phy_lock);

	return val;
}

/**
 * intel_dkl_phy_write - write a Dekel PHY register
 * @display: intel_display device instance
 * @reg: Dekel PHY register
 * @val: value to write
 *
 * Write @val to the @reg Dekel PHY register.
 */
void
intel_dkl_phy_write(struct intel_display *display, struct intel_dkl_phy_reg reg, u32 val)
{
	spin_lock(&display->dkl.phy_lock);

	dkl_phy_set_hip_idx(display, reg);
	intel_de_write(display, DKL_REG_MMIO(reg), val);

	spin_unlock(&display->dkl.phy_lock);
}

/**
 * intel_dkl_phy_rmw - read-modify-write a Dekel PHY register
 * @display: display device instance
 * @reg: Dekel PHY register
 * @clear: mask to clear
 * @set: mask to set
 *
 * Read the @reg Dekel PHY register, clearing then setting the @clear/@set bits in it, and writing
 * this value back to the register if the value differs from the read one.
 */
void
intel_dkl_phy_rmw(struct intel_display *display, struct intel_dkl_phy_reg reg, u32 clear, u32 set)
{
	spin_lock(&display->dkl.phy_lock);

	dkl_phy_set_hip_idx(display, reg);
	intel_de_rmw(display, DKL_REG_MMIO(reg), clear, set);

	spin_unlock(&display->dkl.phy_lock);
}

/**
 * intel_dkl_phy_posting_read - do a posting read from a Dekel PHY register
 * @display: display device instance
 * @reg: Dekel PHY register
 *
 * Read the @reg Dekel PHY register without returning the read value.
 */
void
intel_dkl_phy_posting_read(struct intel_display *display, struct intel_dkl_phy_reg reg)
{
	spin_lock(&display->dkl.phy_lock);

	dkl_phy_set_hip_idx(display, reg);
	intel_de_posting_read(display, DKL_REG_MMIO(reg));

	spin_unlock(&display->dkl.phy_lock);
}
