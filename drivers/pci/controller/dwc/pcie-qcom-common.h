/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _PCIE_QCOM_COMMON_H
#define _PCIE_QCOM_COMMON_H

struct dw_pcie;

void qcom_pcie_common_set_16gt_equalization(struct dw_pcie *pci);
void qcom_pcie_common_set_16gt_lane_margining(struct dw_pcie *pci);

#endif
