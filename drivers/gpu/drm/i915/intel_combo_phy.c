// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

#include "intel_drv.h"

#define for_each_combo_port(__dev_priv, __port) \
	for ((__port) = PORT_A; (__port) < I915_MAX_PORTS; (__port)++)	\
		for_each_if(intel_port_is_combophy(__dev_priv, __port))

#define for_each_combo_port_reverse(__dev_priv, __port) \
	for ((__port) = I915_MAX_PORTS; (__port)-- > PORT_A;) \
		for_each_if(intel_port_is_combophy(__dev_priv, __port))

enum {
	PROCMON_0_85V_DOT_0,
	PROCMON_0_95V_DOT_0,
	PROCMON_0_95V_DOT_1,
	PROCMON_1_05V_DOT_0,
	PROCMON_1_05V_DOT_1,
};

static const struct cnl_procmon {
	u32 dw1, dw9, dw10;
} cnl_procmon_values[] = {
	[PROCMON_0_85V_DOT_0] =
		{ .dw1 = 0x00000000, .dw9 = 0x62AB67BB, .dw10 = 0x51914F96, },
	[PROCMON_0_95V_DOT_0] =
		{ .dw1 = 0x00000000, .dw9 = 0x86E172C7, .dw10 = 0x77CA5EAB, },
	[PROCMON_0_95V_DOT_1] =
		{ .dw1 = 0x00000000, .dw9 = 0x93F87FE1, .dw10 = 0x8AE871C5, },
	[PROCMON_1_05V_DOT_0] =
		{ .dw1 = 0x00000000, .dw9 = 0x98FA82DD, .dw10 = 0x89E46DC1, },
	[PROCMON_1_05V_DOT_1] =
		{ .dw1 = 0x00440000, .dw9 = 0x9A00AB25, .dw10 = 0x8AE38FF1, },
};

/*
 * CNL has just one set of registers, while ICL has two sets: one for port A and
 * the other for port B. The CNL registers are equivalent to the ICL port A
 * registers, that's why we call the ICL macros even though the function has CNL
 * on its name.
 */
static const struct cnl_procmon *
cnl_get_procmon_ref_values(struct drm_i915_private *dev_priv, enum port port)
{
	const struct cnl_procmon *procmon;
	u32 val;

	val = I915_READ(ICL_PORT_COMP_DW3(port));
	switch (val & (PROCESS_INFO_MASK | VOLTAGE_INFO_MASK)) {
	default:
		MISSING_CASE(val);
		/* fall through */
	case VOLTAGE_INFO_0_85V | PROCESS_INFO_DOT_0:
		procmon = &cnl_procmon_values[PROCMON_0_85V_DOT_0];
		break;
	case VOLTAGE_INFO_0_95V | PROCESS_INFO_DOT_0:
		procmon = &cnl_procmon_values[PROCMON_0_95V_DOT_0];
		break;
	case VOLTAGE_INFO_0_95V | PROCESS_INFO_DOT_1:
		procmon = &cnl_procmon_values[PROCMON_0_95V_DOT_1];
		break;
	case VOLTAGE_INFO_1_05V | PROCESS_INFO_DOT_0:
		procmon = &cnl_procmon_values[PROCMON_1_05V_DOT_0];
		break;
	case VOLTAGE_INFO_1_05V | PROCESS_INFO_DOT_1:
		procmon = &cnl_procmon_values[PROCMON_1_05V_DOT_1];
		break;
	}

	return procmon;
}

static void cnl_set_procmon_ref_values(struct drm_i915_private *dev_priv,
				       enum port port)
{
	const struct cnl_procmon *procmon;
	u32 val;

	procmon = cnl_get_procmon_ref_values(dev_priv, port);

	val = I915_READ(ICL_PORT_COMP_DW1(port));
	val &= ~((0xff << 16) | 0xff);
	val |= procmon->dw1;
	I915_WRITE(ICL_PORT_COMP_DW1(port), val);

	I915_WRITE(ICL_PORT_COMP_DW9(port), procmon->dw9);
	I915_WRITE(ICL_PORT_COMP_DW10(port), procmon->dw10);
}

static bool check_phy_reg(struct drm_i915_private *dev_priv,
			  enum port port, i915_reg_t reg, u32 mask,
			  u32 expected_val)
{
	u32 val = I915_READ(reg);

	if ((val & mask) != expected_val) {
		DRM_DEBUG_DRIVER("Port %c combo PHY reg %08x state mismatch: "
				 "current %08x mask %08x expected %08x\n",
				 port_name(port),
				 reg.reg, val, mask, expected_val);
		return false;
	}

	return true;
}

static bool cnl_verify_procmon_ref_values(struct drm_i915_private *dev_priv,
					  enum port port)
{
	const struct cnl_procmon *procmon;
	bool ret;

	procmon = cnl_get_procmon_ref_values(dev_priv, port);

	ret = check_phy_reg(dev_priv, port, ICL_PORT_COMP_DW1(port),
			    (0xff << 16) | 0xff, procmon->dw1);
	ret &= check_phy_reg(dev_priv, port, ICL_PORT_COMP_DW9(port),
			     -1U, procmon->dw9);
	ret &= check_phy_reg(dev_priv, port, ICL_PORT_COMP_DW10(port),
			     -1U, procmon->dw10);

	return ret;
}

static bool cnl_combo_phy_enabled(struct drm_i915_private *dev_priv)
{
	return !(I915_READ(CHICKEN_MISC_2) & CNL_COMP_PWR_DOWN) &&
		(I915_READ(CNL_PORT_COMP_DW0) & COMP_INIT);
}

static bool cnl_combo_phy_verify_state(struct drm_i915_private *dev_priv)
{
	enum port port = PORT_A;
	bool ret;

	if (!cnl_combo_phy_enabled(dev_priv))
		return false;

	ret = cnl_verify_procmon_ref_values(dev_priv, port);

	ret &= check_phy_reg(dev_priv, port, CNL_PORT_CL1CM_DW5,
			     CL_POWER_DOWN_ENABLE, CL_POWER_DOWN_ENABLE);

	return ret;
}

void cnl_combo_phys_init(struct drm_i915_private *dev_priv)
{
	u32 val;

	val = I915_READ(CHICKEN_MISC_2);
	val &= ~CNL_COMP_PWR_DOWN;
	I915_WRITE(CHICKEN_MISC_2, val);

	/* Dummy PORT_A to get the correct CNL register from the ICL macro */
	cnl_set_procmon_ref_values(dev_priv, PORT_A);

	val = I915_READ(CNL_PORT_COMP_DW0);
	val |= COMP_INIT;
	I915_WRITE(CNL_PORT_COMP_DW0, val);

	val = I915_READ(CNL_PORT_CL1CM_DW5);
	val |= CL_POWER_DOWN_ENABLE;
	I915_WRITE(CNL_PORT_CL1CM_DW5, val);
}

void cnl_combo_phys_uninit(struct drm_i915_private *dev_priv)
{
	u32 val;

	if (!cnl_combo_phy_verify_state(dev_priv))
		DRM_WARN("Combo PHY HW state changed unexpectedly.\n");

	val = I915_READ(CHICKEN_MISC_2);
	val |= CNL_COMP_PWR_DOWN;
	I915_WRITE(CHICKEN_MISC_2, val);
}

static bool icl_combo_phy_enabled(struct drm_i915_private *dev_priv,
				  enum port port)
{
	return !(I915_READ(ICL_PHY_MISC(port)) &
		 ICL_PHY_MISC_DE_IO_COMP_PWR_DOWN) &&
		(I915_READ(ICL_PORT_COMP_DW0(port)) & COMP_INIT);
}

static bool icl_combo_phy_verify_state(struct drm_i915_private *dev_priv,
				       enum port port)
{
	bool ret;

	if (!icl_combo_phy_enabled(dev_priv, port))
		return false;

	ret = cnl_verify_procmon_ref_values(dev_priv, port);

	ret &= check_phy_reg(dev_priv, port, ICL_PORT_CL_DW5(port),
			     CL_POWER_DOWN_ENABLE, CL_POWER_DOWN_ENABLE);

	return ret;
}

void icl_combo_phys_init(struct drm_i915_private *dev_priv)
{
	enum port port;

	for_each_combo_port(dev_priv, port) {
		u32 val;

		if (icl_combo_phy_verify_state(dev_priv, port)) {
			DRM_DEBUG_DRIVER("Port %c combo PHY already enabled, won't reprogram it.\n",
					 port_name(port));
			continue;
		}

		val = I915_READ(ICL_PHY_MISC(port));
		val &= ~ICL_PHY_MISC_DE_IO_COMP_PWR_DOWN;
		I915_WRITE(ICL_PHY_MISC(port), val);

		cnl_set_procmon_ref_values(dev_priv, port);

		val = I915_READ(ICL_PORT_COMP_DW0(port));
		val |= COMP_INIT;
		I915_WRITE(ICL_PORT_COMP_DW0(port), val);

		val = I915_READ(ICL_PORT_CL_DW5(port));
		val |= CL_POWER_DOWN_ENABLE;
		I915_WRITE(ICL_PORT_CL_DW5(port), val);
	}
}

void icl_combo_phys_uninit(struct drm_i915_private *dev_priv)
{
	enum port port;

	for_each_combo_port_reverse(dev_priv, port) {
		u32 val;

		if (!icl_combo_phy_verify_state(dev_priv, port))
			DRM_WARN("Port %c combo PHY HW state changed unexpectedly\n",
				 port_name(port));

		val = I915_READ(ICL_PHY_MISC(port));
		val |= ICL_PHY_MISC_DE_IO_COMP_PWR_DOWN;
		I915_WRITE(ICL_PHY_MISC(port), val);

		val = I915_READ(ICL_PORT_COMP_DW0(port));
		val &= ~COMP_INIT;
		I915_WRITE(ICL_PORT_COMP_DW0(port), val);
	}
}
