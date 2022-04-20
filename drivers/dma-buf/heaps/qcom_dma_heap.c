// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/err.h>

#include <linux/qcom_dma_heap.h>
#include "qcom_cma_heap.h"
#include "qcom_dt_parser.h"
#include "qcom_system_heap.h"
#include "qcom_carveout_heap.h"
#include "qcom_secure_system_heap.h"

static int qcom_dma_heap_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i;
	struct platform_data *heaps;

	qcom_system_heap_create("qcom,system", "system", false);
#ifdef CONFIG_QCOM_DMABUF_HEAPS_SYSTEM_UNCACHED
	qcom_system_heap_create("qcom,system-uncached", NULL, true);
#endif
	qcom_secure_system_heap_create("qcom,secure-pixel", NULL,
				       QCOM_DMA_HEAP_FLAG_CP_PIXEL);
	qcom_secure_system_heap_create("qcom,secure-non-pixel", NULL,
				       QCOM_DMA_HEAP_FLAG_CP_NON_PIXEL);

	heaps = parse_heap_dt(pdev);
	if (IS_ERR_OR_NULL(heaps))
		return PTR_ERR(heaps);

	for (i = 0; i < heaps->nr; i++) {
		struct platform_heap *heap_data = &heaps->heaps[i];

		switch (heap_data->type) {
		case HEAP_TYPE_SECURE_CARVEOUT:
			ret = qcom_secure_carveout_heap_create(heap_data);
			if (ret < 0)
				pr_err("%s: DMA-BUF Heap: Failed to create %s, error is %d\n",
				       __func__, heap_data->name, ret);
			else if (!ret)
				pr_info("%s: DMA-BUF Heap: Created %s\n", __func__,
					heap_data->name);
			break;
		case HEAP_TYPE_CARVEOUT:
			ret = qcom_carveout_heap_create(heap_data);
			if (ret < 0)
				pr_err("%s: DMA-BUF Heap: Failed to create %s, error is %d\n",
				       __func__, heap_data->name, ret);
			else if (!ret)
				pr_info("%s: DMA-BUF Heap: Created %s\n", __func__,
					heap_data->name);
			break;
		case HEAP_TYPE_CMA:
			ret = qcom_add_cma_heap(heap_data);
			if (ret < 0)
				pr_err("%s: DMA-BUF Heap: Failed to create %s, error is %d\n",
				       __func__, heap_data->name, ret);
			else if (!ret)
				pr_info("%s: DMA-BUF Heap: Created %s\n", __func__,
					heap_data->name);
			break;
		default:
			pr_err("%s: Unknown heap type %u\n", __func__, heap_data->type);
			break;
		}
	}

	free_pdata(heaps);

	return ret;
}

static const struct of_device_id qcom_dma_heap_match_table[] = {
	{.compatible = "qcom,dma-heaps"},
	{},
};

static struct platform_driver qcom_dma_heap_driver = {
	.probe = qcom_dma_heap_probe,
	.driver = {
		.name = "qcom-dma-heap",
		.of_match_table = qcom_dma_heap_match_table,
	},
};

static int __init init_heap_driver(void)
{
	return platform_driver_register(&qcom_dma_heap_driver);
}
module_init(init_heap_driver);

MODULE_LICENSE("GPL v2");
