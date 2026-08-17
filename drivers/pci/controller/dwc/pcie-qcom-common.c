// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pci.h>

#include "pcie-designware.h"
#include "pcie-qcom-common.h"

void qcom_pcie_common_set_equalization(struct dw_pcie *pci)
{
	struct device *dev = pci->dev;
	u32 reg;
	u16 speed;

	/*
	 * GEN3_RELATED_OFF register is repurposed to apply equalization
	 * settings at various data transmission rates through registers namely
	 * GEN3_EQ_*. The RATE_SHADOW_SEL bit field of GEN3_RELATED_OFF
	 * determines the data rate for which these equalization settings are
	 * applied.
	 */

	for (speed = PCIE_SPEED_8_0GT; speed <= pcie_get_link_speed(pci->max_link_speed); speed++) {
		if (speed > PCIE_SPEED_32_0GT) {
			dev_warn(dev, "Skipped equalization settings for unsupported data rate\n");
			break;
		}

		reg = dw_pcie_readl_dbi(pci, GEN3_RELATED_OFF);
		reg &= ~GEN3_RELATED_OFF_GEN3_ZRXDC_NONCOMPL;
		FIELD_MODIFY(GEN3_RELATED_OFF_RATE_SHADOW_SEL_MASK, &reg,
			     speed - PCIE_SPEED_8_0GT);
		dw_pcie_writel_dbi(pci, GEN3_RELATED_OFF, reg);

		reg = dw_pcie_readl_dbi(pci, GEN3_EQ_FB_MODE_DIR_CHANGE_OFF);
		FIELD_MODIFY(GEN3_EQ_FMDC_T_MIN_PHASE23, &reg, 0x1);
		FIELD_MODIFY(GEN3_EQ_FMDC_N_EVALS, &reg, 0xd);
		FIELD_MODIFY(GEN3_EQ_FMDC_MAX_PRE_CURSOR_DELTA, &reg, 0x5);
		FIELD_MODIFY(GEN3_EQ_FMDC_MAX_POST_CURSOR_DELTA, &reg, 0x5);
		dw_pcie_writel_dbi(pci, GEN3_EQ_FB_MODE_DIR_CHANGE_OFF, reg);

		reg = dw_pcie_readl_dbi(pci, GEN3_EQ_CONTROL_OFF);
		reg &= ~(GEN3_EQ_CONTROL_OFF_FB_MODE |
			GEN3_EQ_CONTROL_OFF_PHASE23_EXIT_MODE |
			GEN3_EQ_CONTROL_OFF_FOM_INC_INITIAL_EVAL |
			GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC);
		dw_pcie_writel_dbi(pci, GEN3_EQ_CONTROL_OFF, reg);
	}
}
EXPORT_SYMBOL_GPL(qcom_pcie_common_set_equalization);

void qcom_pcie_common_set_16gt_lane_margining(struct dw_pcie *pci)
{
	u32 reg;

	reg = dw_pcie_readl_dbi(pci, GEN4_LANE_MARGINING_1_OFF);
	FIELD_MODIFY(MARGINING_MAX_VOLTAGE_OFFSET, &reg, 0x24);
	FIELD_MODIFY(MARGINING_NUM_VOLTAGE_STEPS, &reg, 0x78);
	FIELD_MODIFY(MARGINING_MAX_TIMING_OFFSET, &reg, 0x32);
	FIELD_MODIFY(MARGINING_NUM_TIMING_STEPS, &reg, 0x10);
	dw_pcie_writel_dbi(pci, GEN4_LANE_MARGINING_1_OFF, reg);

	reg = dw_pcie_readl_dbi(pci, GEN4_LANE_MARGINING_2_OFF);
	reg |= MARGINING_IND_ERROR_SAMPLER |
		MARGINING_SAMPLE_REPORTING_METHOD |
		MARGINING_IND_LEFT_RIGHT_TIMING |
		MARGINING_VOLTAGE_SUPPORTED;
	reg &= ~MARGINING_IND_UP_DOWN_VOLTAGE;
	FIELD_MODIFY(MARGINING_MAXLANES, &reg, pci->num_lanes);
	FIELD_MODIFY(MARGINING_SAMPLE_RATE_TIMING, &reg, 0x3f);
	FIELD_MODIFY(MARGINING_SAMPLE_RATE_VOLTAGE, &reg, 0x3f);
	dw_pcie_writel_dbi(pci, GEN4_LANE_MARGINING_2_OFF, reg);
}
EXPORT_SYMBOL_GPL(qcom_pcie_common_set_16gt_lane_margining);
