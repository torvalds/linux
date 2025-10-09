// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include "hw.h"
#include "hal_desc.h"
#include "../hal.h"
#include "hal.h"
#include "hal_qcn9274.h"
#include "hal_wcn7850.h"

static const struct ath12k_hw_version_map ath12k_wifi7_hw_ver_map[] = {
	[ATH12K_HW_QCN9274_HW10] = {
		.hal_ops = &hal_qcn9274_ops,
		.hal_desc_sz = sizeof(struct hal_rx_desc_qcn9274_compact),
		.tcl_to_wbm_rbm_map = ath12k_hal_tcl_to_wbm_rbm_map_qcn9274,
		.hal_params = &ath12k_hw_hal_params_qcn9274,
		.hw_regs = &qcn9274_v1_regs,
	},
	[ATH12K_HW_QCN9274_HW20] = {
		.hal_ops = &hal_qcn9274_ops,
		.hal_desc_sz = sizeof(struct hal_rx_desc_qcn9274_compact),
		.tcl_to_wbm_rbm_map = ath12k_hal_tcl_to_wbm_rbm_map_qcn9274,
		.hal_params = &ath12k_hw_hal_params_qcn9274,
		.hw_regs = &qcn9274_v2_regs,
	},
	[ATH12K_HW_WCN7850_HW20] = {
		.hal_ops = &hal_wcn7850_ops,
		.hal_desc_sz = sizeof(struct hal_rx_desc_wcn7850),
		.tcl_to_wbm_rbm_map = ath12k_hal_tcl_to_wbm_rbm_map_wcn7850,
		.hal_params = &ath12k_hw_hal_params_wcn7850,
		.hw_regs = &wcn7850_regs,
	},
	[ATH12K_HW_IPQ5332_HW10] = {
		.hal_ops = &hal_qcn9274_ops,
		.hal_desc_sz = sizeof(struct hal_rx_desc_qcn9274_compact),
		.tcl_to_wbm_rbm_map = ath12k_hal_tcl_to_wbm_rbm_map_qcn9274,
		.hal_params = &ath12k_hw_hal_params_ipq5332,
		.hw_regs = &ipq5332_regs,
	},
};

int ath12k_wifi7_hal_init(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;

	memset(hal, 0, sizeof(*hal));

	hal->hal_ops = ath12k_wifi7_hw_ver_map[ab->hw_rev].hal_ops;
	hal->hal_desc_sz = ath12k_wifi7_hw_ver_map[ab->hw_rev].hal_desc_sz;
	hal->tcl_to_wbm_rbm_map = ath12k_wifi7_hw_ver_map[ab->hw_rev].tcl_to_wbm_rbm_map;
	hal->regs = ath12k_wifi7_hw_ver_map[ab->hw_rev].hw_regs;
	hal->hal_params = ath12k_wifi7_hw_ver_map[ab->hw_rev].hal_params;

	return 0;
}
EXPORT_SYMBOL(ath12k_wifi7_hal_init);
