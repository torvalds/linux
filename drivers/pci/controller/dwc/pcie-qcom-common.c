// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pci.h>

#include "pcie-designware.h"
#include "pcie-qcom-common.h"

void qcom_pcie_common_set_16gt_equalization(struct dw_pcie *pci)
{
	u32 reg;

	/*
	 * GEN3_RELATED_OFF register is repurposed to apply equalization
	 * settings at various data transmission rates through registers namely
	 * GEN3_EQ_*. The RATE_SHADOW_SEL bit field of GEN3_RELATED_OFF
	 * determines the data rate for which these equalization settings are
	 * applied.
	 */
	reg = dw_pcie_readl_dbi(pci, GEN3_RELATED_OFF);
	reg &= ~GEN3_RELATED_OFF_GEN3_ZRXDC_NONCOMPL;
	reg &= ~GEN3_RELATED_OFF_RATE_SHADOW_SEL_MASK;
	reg |= FIELD_PREP(GEN3_RELATED_OFF_RATE_SHADOW_SEL_MASK,
			  GEN3_RELATED_OFF_RATE_SHADOW_SEL_16_0GT);
	dw_pcie_writel_dbi(pci, GEN3_RELATED_OFF, reg);

	reg = dw_pcie_readl_dbi(pci, GEN3_EQ_FB_MODE_DIR_CHANGE_OFF);
	reg &= ~(GEN3_EQ_FMDC_T_MIN_PHASE23 |
		GEN3_EQ_FMDC_N_EVALS |
		GEN3_EQ_FMDC_MAX_PRE_CUSROR_DELTA |
		GEN3_EQ_FMDC_MAX_POST_CUSROR_DELTA);
	reg |= FIELD_PREP(GEN3_EQ_FMDC_T_MIN_PHASE23, 0x1) |
		FIELD_PREP(GEN3_EQ_FMDC_N_EVALS, 0xd) |
		FIELD_PREP(GEN3_EQ_FMDC_MAX_PRE_CUSROR_DELTA, 0x5) |
		FIELD_PREP(GEN3_EQ_FMDC_MAX_POST_CUSROR_DELTA, 0x5);
	dw_pcie_writel_dbi(pci, GEN3_EQ_FB_MODE_DIR_CHANGE_OFF, reg);

	reg = dw_pcie_readl_dbi(pci, GEN3_EQ_CONTROL_OFF);
	reg &= ~(GEN3_EQ_CONTROL_OFF_FB_MODE |
		GEN3_EQ_CONTROL_OFF_PHASE23_EXIT_MODE |
		GEN3_EQ_CONTROL_OFF_FOM_INC_INITIAL_EVAL |
		GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC);
	dw_pcie_writel_dbi(pci, GEN3_EQ_CONTROL_OFF, reg);
}
EXPORT_SYMBOL_GPL(qcom_pcie_common_set_16gt_equalization);
