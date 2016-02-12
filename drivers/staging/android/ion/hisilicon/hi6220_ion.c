/*
 * Hisilicon Hi6220 ION Driver
 *
 * Copyright (c) 2015 Hisilicon Limited.
 *
 * Author: Chen Feng <puck.chen@hisilicon.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "Ion: " fmt

#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/mm.h>
#include "../ion_priv.h"
#include "../ion.h"

struct hi6220_ion_type_table {
	const char *name;
	enum ion_heap_type type;
};

static struct hi6220_ion_type_table ion_type_table[] = {
	{"ion_system", ION_HEAP_TYPE_SYSTEM},
	{"ion_system_contig", ION_HEAP_TYPE_SYSTEM_CONTIG},
	{"ion_carveout", ION_HEAP_TYPE_CARVEOUT},
	{"ion_chunk", ION_HEAP_TYPE_CHUNK},
	{"ion_dma", ION_HEAP_TYPE_DMA},
	{"ion_custom", ION_HEAP_TYPE_CUSTOM},
};

static struct ion_device *idev;
static int num_heaps;
static struct ion_heap **heaps;
static struct ion_platform_heap **heaps_data;

static int get_type_by_name(const char *name, enum ion_heap_type *type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ion_type_table); i++) {
		if (strncmp(name, ion_type_table[i].name, strlen(name)))
			continue;

		*type = ion_type_table[i].type;
		return 0;
	}

	return -EINVAL;
}

static int hi6220_set_platform_data(struct platform_device *pdev)
{
	unsigned int base;
	unsigned int size;
	unsigned int id;
	const char *heap_name;
	const char *type_name;
	enum ion_heap_type type;
	int ret;
	struct device_node *np;
	struct ion_platform_heap *p_data;
	const struct device_node *dt_node = pdev->dev.of_node;
	int index = 0;

	for_each_child_of_node(dt_node, np)
		num_heaps++;

	heaps_data = devm_kzalloc(&pdev->dev,
				  sizeof(struct ion_platform_heap *) *
				  num_heaps,
				  GFP_KERNEL);
	if (!heaps_data)
		return -ENOMEM;

	for_each_child_of_node(dt_node, np) {
		ret = of_property_read_string(np, "heap-name", &heap_name);
		if (ret < 0) {
			pr_err("check the name of node %s\n", np->name);
			continue;
		}

		ret = of_property_read_u32(np, "heap-id", &id);
		if (ret < 0) {
			pr_err("check the id %s\n", np->name);
			continue;
		}

		ret = of_property_read_u32(np, "heap-base", &base);
		if (ret < 0) {
			pr_err("check the base of node %s\n", np->name);
			continue;
		}

		ret = of_property_read_u32(np, "heap-size", &size);
		if (ret < 0) {
			pr_err("check the size of node %s\n", np->name);
			continue;
		}

		ret = of_property_read_string(np, "heap-type", &type_name);
		if (ret < 0) {
			pr_err("check the type of node %s\n", np->name);
			continue;
		}

		ret = get_type_by_name(type_name, &type);
		if (ret < 0) {
			pr_err("type name error %s!\n", type_name);
			continue;
		}
		pr_info("heap index %d : name %s base 0x%x size 0x%x id %d type %d\n",
			index, heap_name, base, size, id, type);

		p_data = devm_kzalloc(&pdev->dev,
				      sizeof(struct ion_platform_heap),
				      GFP_KERNEL);
		if (!p_data)
			return -ENOMEM;

		p_data->name = heap_name;
		p_data->base = base;
		p_data->size = size;
		p_data->id = id;
		p_data->type = type;

		heaps_data[index] = p_data;
		index++;
	}
	return 0;
}

static int hi6220_ion_probe(struct platform_device *pdev)
{
	int i;
	int err;
	static struct ion_platform_heap *p_heap;

	idev = ion_device_create(NULL);
	err = hi6220_set_platform_data(pdev);
	if (err) {
		pr_err("ion set platform data error!\n");
		goto err_free_idev;
	}
	heaps = devm_kzalloc(&pdev->dev,
			     sizeof(struct ion_heap *) * num_heaps,
			     GFP_KERNEL);
	if (!heaps) {
		err = -ENOMEM;
		goto err_free_idev;
	}

	/*
	 * create the heaps as specified in the dts file
	 */
	for (i = 0; i < num_heaps; i++) {
		p_heap = heaps_data[i];
		heaps[i] = ion_heap_create(p_heap);
		if (IS_ERR_OR_NULL(heaps[i])) {
			err = PTR_ERR(heaps[i]);
			goto err_free_heaps;
		}

		ion_device_add_heap(idev, heaps[i]);

		pr_info("%s: adding heap %s of type %d with %lx@%lx\n",
			__func__, p_heap->name, p_heap->type,
			p_heap->base, (unsigned long)p_heap->size);
	}
	return err;

err_free_heaps:
	for (i = 0; i < num_heaps; ++i) {
		ion_heap_destroy(heaps[i]);
		heaps[i] = NULL;
	}
err_free_idev:
	ion_device_destroy(idev);

	return err;
}

static int hi6220_ion_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < num_heaps; i++) {
		ion_heap_destroy(heaps[i]);
		heaps[i] = NULL;
	}
	ion_device_destroy(idev);

	return 0;
}

static const struct of_device_id hi6220_ion_match_table[] = {
	{.compatible = "hisilicon,hi6220-ion"},
	{},
};

static struct platform_driver hi6220_ion_driver = {
	.probe = hi6220_ion_probe,
	.remove = hi6220_ion_remove,
	.driver = {
		.name = "ion-hi6220",
		.of_match_table = hi6220_ion_match_table,
	},
};

static int __init hi6220_ion_init(void)
{
	int ret;

	ret = platform_driver_register(&hi6220_ion_driver);
	return ret;
}

subsys_initcall(hi6220_ion_init);
