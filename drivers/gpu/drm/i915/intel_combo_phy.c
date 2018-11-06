// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

#include "intel_drv.h"

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
static void cnl_set_procmon_ref_values(struct drm_i915_private *dev_priv,
				       enum port port)
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

	val = I915_READ(ICL_PORT_COMP_DW1(port));
	val &= ~((0xff << 16) | 0xff);
	val |= procmon->dw1;
	I915_WRITE(ICL_PORT_COMP_DW1(port), val);

	I915_WRITE(ICL_PORT_COMP_DW9(port), procmon->dw9);
	I915_WRITE(ICL_PORT_COMP_DW10(port), procmon->dw10);
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

	val = I915_READ(CHICKEN_MISC_2);
	val |= CNL_COMP_PWR_DOWN;
	I915_WRITE(CHICKEN_MISC_2, val);
}

void icl_combo_phys_init(struct drm_i915_private *dev_priv)
{
	enum port port;

	for (port = PORT_A; port <= PORT_B; port++) {
		u32 val;

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

	for (port = PORT_A; port <= PORT_B; port++) {
		u32 val;

		val = I915_READ(ICL_PHY_MISC(port));
		val |= ICL_PHY_MISC_DE_IO_COMP_PWR_DOWN;
		I915_WRITE(ICL_PHY_MISC(port), val);

		val = I915_READ(ICL_PORT_COMP_DW0(port));
		val &= ~COMP_INIT;
		I915_WRITE(ICL_PORT_COMP_DW0(port), val);
	}
}
