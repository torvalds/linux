/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/platform_device.h>

#define MEM		1
#define STANDBY		0

extern int register_qcom_clks_pm(struct platform_device *pdev,
				bool runtime, struct qcom_cc_desc *desc);
