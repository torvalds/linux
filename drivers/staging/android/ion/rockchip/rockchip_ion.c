/*
 * drivers/staging/android/ion/rockchip/rockchip_ion.c
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/dma-contiguous.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/rockchip_ion.h>

#include "../ion_priv.h"

struct ion_device *rockchip_ion_dev;
static struct ion_heap **heaps;

struct ion_heap_desc {
	unsigned int id;
	enum ion_heap_type type;
	const char *name;
};

static struct ion_heap_desc ion_heap_meta[] = {
	{
		.id	= ION_HEAP_TYPE_SYSTEM,
		.type	= ION_HEAP_TYPE_SYSTEM,
		.name	= "system-heap",
	}, {
		.id	= ION_HEAP_TYPE_CARVEOUT,
		.type	= ION_HEAP_TYPE_CARVEOUT,
		.name	= "carveout-heap",
	}, {
		.id	= ION_HEAP_TYPE_DMA,
		.type	= ION_HEAP_TYPE_DMA,
		.name	= "cma-heap",
	},
};

/* Return result of step for heap array. */
static int rk_ion_of_heap(struct ion_platform_heap *myheap,
			  struct device_node *node)
{
	unsigned int reg[2] = {0,};
	int itype;

	for (itype = 0; itype < ARRAY_SIZE(ion_heap_meta); itype++) {
		if (strcmp(ion_heap_meta[itype].name, node->name))
			continue;

		myheap->name = node->name;
		myheap->align = SZ_1M;
		myheap->id = ion_heap_meta[itype].id;
		if (!strcmp("cma-heap", node->name)) {
			myheap->type = ION_HEAP_TYPE_DMA;
			if (!of_property_read_u32_array(node, "reg", reg, 2)) {
				myheap->base = reg[0];
				myheap->size = reg[1];
			}
			return 1;
		}

		if (!strcmp("system-heap", node->name)) {
			myheap->type = ION_HEAP_TYPE_SYSTEM;
			return 1;
		}
	}

	return 0;
}

static struct ion_platform_data *rk_ion_of(struct device_node *node)
{
	struct ion_platform_data *pdata;
	int iheap = 0;
	struct device_node *child;
	struct ion_platform_heap *myheap;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	pdata->nr = of_get_child_count(node);
again:
	pdata->heaps = kcalloc(pdata->nr, sizeof(*myheap), GFP_KERNEL);
	for_each_child_of_node(node, child) {
		iheap += rk_ion_of_heap(&pdata->heaps[iheap], child);
	}

	if (pdata->nr != iheap) {
		pdata->nr = iheap;
		iheap = 0;
		kfree(pdata->heaps);
		pr_err("%s: mismatch, repeating\n", __func__);
		goto again;
	}

	return pdata;
}

static int rk_ion_probe(struct platform_device *pdev)
{
	int err;
	int i;
	struct ion_platform_data *pdata = pdev->dev.platform_data;
	struct ion_device *idev;

	err = of_reserved_mem_device_init(&pdev->dev);
	if (err)
		pr_debug("No reserved memory region assign to ion\n");

	if (!pdata) {
		pdata = rk_ion_of(pdev->dev.of_node);
		pdev->dev.platform_data = pdata;
	}

	heaps = kcalloc(pdata->nr, sizeof(*heaps), GFP_KERNEL);

	idev = ion_device_create(NULL);
	if (IS_ERR_OR_NULL(idev)) {
		kfree(heaps);
		return PTR_ERR(idev);
	}

	ion_device_set_platform(idev, &pdev->dev);
	rockchip_ion_dev = idev;

	/* create the heaps as specified in the board file */
	for (i = 0; i < pdata->nr; i++) {
		struct ion_platform_heap *heap_data = &pdata->heaps[i];

		heap_data->priv = &pdev->dev;
		heaps[i] = ion_heap_create(heap_data);
		if (IS_ERR_OR_NULL(heaps[i])) {
			err = PTR_ERR(heaps[i]);
			goto err;
		}
		pr_info("rockchip ion: success to create - %s\n",
			heaps[i]->name);
		ion_device_add_heap(idev, heaps[i]);
	}
	platform_set_drvdata(pdev, idev);

	return 0;
err:
	for (i = 0; i < pdata->nr; i++) {
		if (heaps[i])
			ion_heap_destroy(heaps[i]);
	}

	kfree(heaps);
	return err;
}

static int rk_ion_remove(struct platform_device *pdev)
{
	struct ion_platform_data *pdata = pdev->dev.platform_data;
	struct ion_device *idev = platform_get_drvdata(pdev);
	int i;

	ion_device_destroy(idev);
	for (i = 0; i < pdata->nr; i++)
		ion_heap_destroy(heaps[i]);

	kfree(heaps);
	return 0;
}

struct ion_client *rockchip_ion_client_create(const char *name)
{
	if (!rockchip_ion_dev) {
		pr_err("rockchip ion idev is NULL\n");
		return NULL;
	}

	return ion_client_create(rockchip_ion_dev, name);
}
EXPORT_SYMBOL_GPL(rockchip_ion_client_create);

static const struct of_device_id rk_ion_match[] = {
	{ .compatible = "rockchip,ion", },
	{}
};

static struct platform_driver ion_driver = {
	.probe = rk_ion_probe,
	.remove = rk_ion_remove,
	.driver = {
		.name = "ion-rk",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rk_ion_match),
	},
};

static int __init rk_ion_init(void)
{
	return platform_driver_register(&ion_driver);
}

static void __exit rk_ion_exit(void)
{
	platform_driver_unregister(&ion_driver);
}

subsys_initcall(rk_ion_init);
module_exit(rk_ion_exit);

MODULE_AUTHOR("Meiyou.chen <cmy@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP Ion driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, rk_ion_match);
