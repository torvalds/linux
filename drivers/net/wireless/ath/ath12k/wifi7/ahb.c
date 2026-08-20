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

/*
 * Node name to UserPD ID mapping
 *
 * The io_start field is used for additional validation when the reg
 * property is present in the device tree. If io_start is 0, only
 * node_name matching is performed.
 *
 * For platforms where not all WiFi nodes have a 'reg' property, set
 * io_start to 0 for those entries. The driver will match purely by
 * node name in such cases.
 */
static const struct ath12k_ahb_userpd_map ath12k_wifi7_ahb_userpd_map[] = {
	{ .io_start = 0x0c000000, .node_name = "wifi", .upd_id = ATH12K_AHB_USERPD_ID_0 },
};

static const struct ath12k_ahb_desc ath12k_wifi7_ahb_desc[] = {
	[ATH12K_HW_IPQ5332_HW10] = {
		.hw_rev = ATH12K_HW_IPQ5332_HW10,
		.auth_enabled = true,
		.ops = &ath12k_ahb_hif_ops,
	},
	[ATH12K_HW_IPQ5424_HW10] = {
		.hw_rev = ATH12K_HW_IPQ5424_HW10,
		.auth_enabled = false,
		.ops = &ath12k_ahb_hif_ops,
	},
};

static const struct of_device_id ath12k_wifi7_ahb_of_match[] = {
	{ .compatible = "qcom,ipq5332-wifi",
	  .data = (void *)&ath12k_wifi7_ahb_desc[ATH12K_HW_IPQ5332_HW10],
	},
	{ .compatible = "qcom,ipq5424-wifi",
	  .data = (void *)&ath12k_wifi7_ahb_desc[ATH12K_HW_IPQ5424_HW10],
	},
	{ }
};

MODULE_DEVICE_TABLE(of, ath12k_wifi7_ahb_of_match);

/*
 * ath12k_wifi7_ahb_get_userpd_id - Resolve UserPD ID from DT properties
 * @ab: ath12k base structure
 *
 * Returns: UserPD ID (1-based) on success, 0 on failure
 *
 * Resolution logic:
 * 1. If reg property exist in DT, get userpd_id from io_start
 * 2. If reg property is absent, get userpd_id from DT node name
 * 3. Return 0 if no match found (probe will fail)
 */
static u32 ath12k_wifi7_ahb_get_userpd_id(struct ath12k_base *ab)
{
	const struct ath12k_ahb_userpd_map *map;
	struct resource *res;
	size_t i;

	res = platform_get_resource(ab->pdev, IORESOURCE_MEM, 0);

	for (i = 0; i < ARRAY_SIZE(ath12k_wifi7_ahb_userpd_map); i++) {
		map = &ath12k_wifi7_ahb_userpd_map[i];

		if (res) {
			if (map->io_start && map->io_start == res->start)
				return map->upd_id;
		} else if (map->node_name &&
			   of_node_name_eq(ab->dev->of_node, map->node_name)) {
			return map->upd_id;
		}
	}

	return 0;
}

static int ath12k_wifi7_ahb_probe(struct platform_device *pdev)
{
	const struct ath12k_ahb_desc *desc;
	struct ath12k_ahb *ab_ahb;
	struct ath12k_base *ab;
	int ret;

	ab = platform_get_drvdata(pdev);
	ab_ahb = ath12k_ab_to_ahb(ab);
	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EOPNOTSUPP;

	ab->target_mem_mode = ATH12K_QMI_MEMORY_MODE_DEFAULT;
	ab->hw_rev = desc->hw_rev;
	ab->hif.ops = desc->ops;
	ab_ahb->scm_auth_enabled = desc->auth_enabled;
	ab_ahb->userpd_id = ath12k_wifi7_ahb_get_userpd_id(ab);
	if (!ab_ahb->userpd_id)
		return -EOPNOTSUPP;

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
