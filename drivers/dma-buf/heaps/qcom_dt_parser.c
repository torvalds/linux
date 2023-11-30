// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>

#include <linux/qcom_dma_heap.h>
#include "qcom_dt_parser.h"

#ifdef CONFIG_PANIC_ON_QCOM_DMA_HEAPS_FAILURE
#define QCOM_DMA_HEAP_WARN(fmt...) panic(fmt)
#else /* CONFIG_PANIC_ON_QCOM_DMA_HEAPS_FAILURE */
#define QCOM_DMA_HEAP_WARN(fmt...) WARN(1, fmt)
#endif /* CONFIG_PANIC_ON_QCOM_DMA_HEAPS_FAILURE */

static int populate_heap(struct device_node *node,
			 struct platform_heap *heap)
{
	int ret;

	/* Mandatory properties */
	ret = of_property_read_string(node, "qcom,dma-heap-name", &heap->name);
	if (ret)
		goto err;

	ret = of_property_read_u32(node, "qcom,dma-heap-type", &heap->type);
	if (ret) {
		pr_err("Reading %s property in node %s failed with err %d.\n",
		       "qcom,dma-heap-type", of_node_full_name(node), ret);
		goto err;
	}

	/* Optional properties */
	heap->is_uncached = of_property_read_bool(node, "qcom,uncached-heap");

	ret = of_property_read_u32(node, "qcom,token", &heap->token);
	if (ret && ret != -EINVAL)
		goto err;

	ret = of_property_read_u32(node, "qcom,max-align", &heap->max_align);
	if (ret && ret != -EINVAL)
		goto err;

	return 0;

err:
	if (ret)
		QCOM_DMA_HEAP_WARN("%s: Unable to populate heap %s, err: %d\n",
				   __func__, of_node_full_name(node), ret);
	return ret;
}

void free_pdata(const struct platform_data *pdata)
{
	kfree(pdata->heaps);
	kfree(pdata);
}

static int heap_dt_init(struct device_node *mem_node,
			struct platform_heap *heap)
{
	struct device *dev = heap->dev;
	struct reserved_mem *rmem;
	int ret = 0;


	rmem = of_reserved_mem_lookup(mem_node);

	if (!rmem) {
		dev_err(dev, "Failed to find reserved memory region\n");
		return -EINVAL;
	}

	/*
	 * We only need to call this when the memory-region is managed by
	 * a reserved memory region driver (e.g. CMA, coherent, etc). In that
	 * case, they will have ops for device specific initialization for
	 * the memory region. Otherwise, we have a pure carveout, which needs
	 * not be initialized.
	 */
	if (rmem->ops) {
		ret = of_reserved_mem_device_init_by_idx(dev, dev->of_node, 0);
		if (ret) {
			dev_err(dev,
				"Failed to initialize memory region rc: %d\n",
				ret);
			return ret;
		}
	}

	heap->base = rmem->base;
	heap->size = rmem->size;
	heap->is_nomap =  of_property_read_bool(mem_node, "no-map");

	return ret;
}

static void release_reserved_memory_regions(struct platform_heap *heaps,
					    int idx)
{
	struct device *dev;
	struct device_node *node, *mem_node;

	for (idx = idx - 1; idx >= 0; idx--) {
		dev = heaps[idx].dev;
		node = dev->of_node;
		mem_node = of_parse_phandle(node, "memory-region", 0);

		if (mem_node)
			of_reserved_mem_device_release(dev);

		of_node_put(mem_node);
	}
}

struct platform_data *parse_heap_dt(struct platform_device *pdev)
{
	struct platform_data *pdata = NULL;
	struct device_node *node;
	struct device_node *mem_node;
	struct platform_device *new_dev = NULL;
	const struct device_node *dt_node = pdev->dev.of_node;
	int ret;
	int idx = 0;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	for_each_available_child_of_node(dt_node, node)
		pdata->nr++;

	/*
	 * No heaps defined in the devicetree. However, there may be other
	 * heaps (e.g. system heaps) that do not need to be defined in the
	 * devicetree.
	 */
	if (!pdata->nr)
		goto out;

	pdata->heaps = kcalloc(pdata->nr, sizeof(*pdata->heaps), GFP_KERNEL);
	if (!pdata->heaps) {
		kfree(pdata);
		return ERR_PTR(-ENOMEM);
	}

	for_each_available_child_of_node(dt_node, node) {
		new_dev = of_platform_device_create(node, NULL, &pdev->dev);
		if (!new_dev) {
			pr_err("Failed to create device %s\n", node->name);
			ret = -EINVAL;
			goto free_heaps;
		}
		of_dma_configure(&new_dev->dev, node, true);

		pdata->heaps[idx].dev = &new_dev->dev;

		ret = populate_heap(node, &pdata->heaps[idx]);
		if (ret)
			goto free_heaps;

		mem_node = of_parse_phandle(node, "memory-region", 0);
		if (mem_node) {
			ret = heap_dt_init(mem_node, &pdata->heaps[idx]);
			if (ret)
				goto free_heaps;

			of_node_put(mem_node);
		}

		++idx;
	}

out:
	return pdata;

free_heaps:
	of_node_put(node);
	release_reserved_memory_regions(pdata->heaps, idx);
	free_pdata(pdata);
	return ERR_PTR(ret);
}
