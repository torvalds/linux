// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/err.h>

#include <linux/qcom_dma_heap.h>
#include <linux/qcom_tui_heap.h>
#include "qcom_cma_heap.h"
#include "qcom_dt_parser.h"
#include "qcom_system_heap.h"
#include "qcom_carveout_heap.h"
#include "qcom_secure_system_heap.h"
#include "qcom_dma_heap_priv.h"
#include "qcom_system_movable_heap.h"

/*
 * We cache the file ops used by DMA-BUFs so that a user with a struct file
 * pointer can determine if that file is for a DMA-BUF. We can't initialize
 * the pointer here at compile time as ERR_PTR() evaluates to a function.
 */
#define FOPS_INIT_VAL ERR_PTR(-EINVAL)
static const struct file_operations *dma_buf_cached_fops;

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
	qcom_sys_movable_heap_create();

	heaps = parse_heap_dt(pdev);
	if (IS_ERR_OR_NULL(heaps))
		return PTR_ERR(heaps);

	for (i = 0; i < heaps->nr; i++) {
		struct platform_heap *heap_data = &heaps->heaps[i];

		switch (heap_data->type) {
		case HEAP_TYPE_SECURE_CARVEOUT:
			ret = qcom_secure_carveout_heap_create(heap_data);
			break;
		case HEAP_TYPE_CARVEOUT:
			ret = qcom_carveout_heap_create(heap_data);
			break;
		case HEAP_TYPE_CMA:
			ret = qcom_add_cma_heap(heap_data);
			break;
		case HEAP_TYPE_TUI_CARVEOUT:
			ret = qcom_tui_carveout_heap_create(heap_data);
			break;
		default:
			pr_err("%s: Unknown heap type %u\n", __func__, heap_data->type);
			break;
		}

		if (ret)
			pr_err("%s: DMA-BUF Heap: Failed to create %s, error is %d\n",
			       __func__, heap_data->name, ret);
		else
			pr_info("%s: DMA-BUF Heap: Created %s\n", __func__,
				heap_data->name);
	}

	free_pdata(heaps);

	dma_buf_cached_fops = FOPS_INIT_VAL;

	return ret;
}

void qcom_store_dma_buf_fops(struct file *file)
{
	if (dma_buf_cached_fops == FOPS_INIT_VAL)
		dma_buf_cached_fops = file->f_op;
}
EXPORT_SYMBOL(qcom_store_dma_buf_fops);

bool qcom_is_dma_buf_file(struct file *file)
{
	return file->f_op == dma_buf_cached_fops;
}
EXPORT_SYMBOL(qcom_is_dma_buf_file);

static int qcom_dma_heaps_freeze(struct device *dev)
{
	int ret;

	ret = qcom_secure_carveout_heap_freeze();
	if (ret) {
		pr_err("Failed to freeze secure carveout heap: %d\n", ret);
		return ret;
	}

	ret = qcom_secure_system_heap_freeze();
	if (ret) {
		pr_err("Failed to freeze secure system heap: %d\n", ret);
		goto err;
	}

	return 0;
err:
	ret = qcom_secure_carveout_heap_restore();
	if (ret) {
		pr_err("Failed to restore secure carveout heap: %d\n", ret);
		return ret;
	}
	return -EBUSY;
}

static int qcom_dma_heaps_restore(struct device *dev)
{
	int ret;

	ret = qcom_secure_carveout_heap_restore();
	if (ret)
		pr_err("Failed to restore secure carveout heap: %d\n", ret);

	ret = qcom_secure_system_heap_restore();
	if (ret)
		pr_err("Failed to restore secure system heap: %d\n", ret);

	return ret;
}

static const struct dev_pm_ops qcom_dma_heaps_pm_ops = {
	.freeze_late = qcom_dma_heaps_freeze,
	.restore_early = qcom_dma_heaps_restore,
};

static const struct of_device_id qcom_dma_heap_match_table[] = {
	{.compatible = "qcom,dma-heaps"},
	{},
};

static struct platform_driver qcom_dma_heap_driver = {
	.probe = qcom_dma_heap_probe,
	.driver = {
		.name = "qcom-dma-heap",
		.of_match_table = qcom_dma_heap_match_table,
		.pm = &qcom_dma_heaps_pm_ops,
	},
};

static int __init init_heap_driver(void)
{
	return platform_driver_register(&qcom_dma_heap_driver);
}
module_init(init_heap_driver);

MODULE_LICENSE("GPL v2");
