// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/mdt_loader.h>
#include "ahb.h"
#include "ahb_wifi7.h"
#include "debug.h"
#include "hif.h"

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

	return 0;
}

static struct ath12k_ahb_driver ath12k_wifi7_ahb_driver = {
	.name = "ath12k_wifi7_ahb",
	.id_table = ath12k_wifi7_ahb_of_match,
	.ops.probe = ath12k_wifi7_ahb_probe,
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
