// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/mdt_loader.h>
#include "../ahb.h"
#include "ahb.h"
#include "../debug.h"
#include "../hif.h"
#include "hw.h"
#include "dp.h"
#include "core.h"

static const struct of_device_id ath12k_wifi7_ahb_of_match[] = {
	{ .compatible = "qcom,ipq5332-wifi",
	  .data = (void *)ATH12K_HW_IPQ5332_HW10,
	},
	{ }
};

MODULE_DEVICE_TABLE(of, ath12k_wifi7_ahb_of_match);

static int ath12k_wifi7_ahb_probe(struct platform_device *pdev)
{
	struct ath12k_ahb *ab_ahb;
	enum ath12k_hw_rev hw_rev;
	struct ath12k_base *ab;
	int ret;

	ab = platform_get_drvdata(pdev);
	ab_ahb = ath12k_ab_to_ahb(ab);

	hw_rev = (enum ath12k_hw_rev)(kernel_ulong_t)of_device_get_match_data(&pdev->dev);
	switch (hw_rev) {
	case ATH12K_HW_IPQ5332_HW10:
		ab_ahb->userpd_id = ATH12K_IPQ5332_USERPD_ID;
		break;
	default:
		return -EOPNOTSUPP;
	}

	ab->target_mem_mode = ATH12K_QMI_MEMORY_MODE_DEFAULT;
	ab->hw_rev = hw_rev;

	ret = ath12k_wifi7_hw_init(ab);
	if (ret) {
		ath12k_err(ab, "WiFi-7 hw_init for AHB failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct ath12k_ahb_driver ath12k_wifi7_ahb_driver = {
	.name = "ath12k_wifi7_ahb",
	.id_table = ath12k_wifi7_ahb_of_match,
	.ops.probe = ath12k_wifi7_ahb_probe,
	.ops.arch_init = ath12k_wifi7_arch_init,
	.ops.arch_deinit = ath12k_wifi7_arch_deinit,
};

int ath12k_wifi7_ahb_init(void)
{
	return ath12k_ahb_register_driver(ATH12K_DEVICE_FAMILY_WIFI7,
					  &ath12k_wifi7_ahb_driver);
}

void ath12k_wifi7_ahb_exit(void)
{
	ath12k_ahb_unregister_driver(ATH12K_DEVICE_FAMILY_WIFI7);
}
