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
	},
	[ATH12K_HW_QCN9274_HW20] = {
		.hal_ops = &hal_qcn9274_ops,
		.hal_desc_sz = sizeof(struct hal_rx_desc_qcn9274_compact),
	},
	[ATH12K_HW_WCN7850_HW20] = {
		.hal_ops = &hal_wcn7850_ops,
		.hal_desc_sz = sizeof(struct hal_rx_desc_wcn7850),
	},
	[ATH12K_HW_IPQ5332_HW10] = {
		.hal_ops = &hal_qcn9274_ops,
		.hal_desc_sz = sizeof(struct hal_rx_desc_qcn9274_compact),
	},
};

int ath12k_wifi7_hal_init(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;

	memset(hal, 0, sizeof(*hal));

	hal->hal_ops = ath12k_wifi7_hw_ver_map[ab->hw_rev].hal_ops;
	hal->hal_desc_sz = ath12k_wifi7_hw_ver_map[ab->hw_rev].hal_desc_sz;

	return 0;
}
EXPORT_SYMBOL(ath12k_wifi7_hal_init);
