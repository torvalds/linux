/*
 * drivers/gpu/rockchip/rockchip_ion.c
 *
 * Copyright (C) 2011 Google, Inc.
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

#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "../ion.h"
#include "../ion_priv.h"

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <video/of_display_timing.h>
#endif

static struct ion_device *idev;
static int num_heaps;
static struct ion_heap **heaps;
/*
static struct ion_platform_heap s_heap_data = {
        .type = ION_HEAP_TYPE_DMA,
        .id = 1,
        .name = "norheap",
        .size = 256*SZ_1M,
        .align = SZ_1M,
};
*/
struct ion_heap_desc {
	unsigned int id;
	enum ion_heap_type type;
	const char *name;
};

static struct ion_heap_desc ion_heap_meta[] = {
	{
		.id	= ION_SYSTEM_HEAP_ID,
		.type	= ION_HEAP_TYPE_SYSTEM,
		.name	= ION_VMALLOC_HEAP_NAME,
	},
	{
		.id	= ION_VIDEO_HEAP_ID,
		.type	= ION_HEAP_TYPE_DMA,
		.name	= ION_VIDEO_HEAP_NAME,
	},
	{
		.id	= ION_AUDIO_HEAP_ID,
		.type	= ION_HEAP_TYPE_DMA,
		.name	= ION_AUDIO_HEAP_NAME,
	},
	{
		.id	= ION_IOMMU_HEAP_ID,
		.type	= ION_HEAP_TYPE_DMA,//ION_HEAP_TYPE_IOMMU,
		.name	= ION_IOMMU_HEAP_NAME,
	},
	{
		.id	= ION_CAMERA_HEAP_ID,
		.type	= ION_HEAP_TYPE_DMA,
		.name	= ION_CAMERA_HEAP_NAME,
	},
};

static int rockchip_ion_populate_heap(struct ion_platform_heap *heap)
{
	unsigned int i;
	int ret = -EINVAL;
	unsigned int len = ARRAY_SIZE(ion_heap_meta);
	for (i = 0; i < len; ++i) {
		if (ion_heap_meta[i].id == heap->id) {
			heap->name = ion_heap_meta[i].name;
			heap->type = ion_heap_meta[i].type;
			ret = 0;
			break;
		}
	}
	if (ret)
		pr_err("%s: Unable to populate heap, error: %d", __func__, ret);
	return ret;
}

static int rockchip_ion_get_heap_size(struct device_node *node,
				 struct ion_platform_heap *heap)
{
	unsigned int val;
	int ret = 0;

	ret = of_property_read_u32(node, "rockchip,memory-reservation-size", &val);
	if (!ret) {
		heap->size = val;
	} else {
		ret = 0;
	}

	return ret;
}

static struct ion_platform_data *rockchip_ion_parse_dt(
					struct device *dev)
{
        struct device_node *dt_node = dev->of_node;
	struct ion_platform_data *pdata = 0;
	struct device_node *node;
	uint32_t val = 0;
	int ret = 0;
	uint32_t num_heaps = 0;
	int idx = 0;

	for_each_child_of_node(dt_node, node)
		num_heaps++;

        pr_info("%s: num_heaps = %d\n", __func__, num_heaps);
        
	if (!num_heaps)
		return ERR_PTR(-EINVAL);

	pdata = kzalloc(sizeof(struct ion_platform_data) +
			num_heaps*sizeof(struct ion_platform_heap), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);
        pdata->heaps = (struct ion_platform_heap*)((void*)pdata+sizeof(struct ion_platform_data));
	pdata->nr = num_heaps;
        
	for_each_child_of_node(dt_node, node) {
		ret = of_property_read_u32(node, "reg", &val);
		if (ret) {
			pr_err("%s: Unable to find reg key", __func__);
			goto free_heaps;
		}
		pdata->heaps[idx].id = val;

		ret = rockchip_ion_populate_heap(&pdata->heaps[idx]);
		if (ret)
			goto free_heaps;

//		msm_ion_get_heap_align(node, &pdata->heaps[idx]);
		ret = rockchip_ion_get_heap_size(node, &pdata->heaps[idx]);
		if (ret)
			goto free_heaps;

//		msm_ion_get_heap_adjacent(node, &pdata->heaps[idx]);
                pdata->heaps[idx].priv = dev;
                pr_info("%d:  %d  %d  %s  0x%08X\n", idx, pdata->heaps[idx].type, pdata->heaps[idx].id, pdata->heaps[idx].name, pdata->heaps[idx].size);

		++idx;
	}

	return pdata;

free_heaps:
	kfree(pdata);
	return ERR_PTR(ret);
}

struct ion_client *rockchip_ion_client_create(const char *name)
{
	return ion_client_create(idev, name);
}
EXPORT_SYMBOL(rockchip_ion_client_create);

static long rockchip_custom_ioctl (struct ion_client *client, unsigned int cmd,
			      unsigned long arg)
{
	return 0;
}

static int rockchip_ion_probe(struct platform_device *pdev)
{
	struct ion_platform_data *pdata;
        unsigned int pdata_needs_to_be_freed;
	int err;
	int i;

        printk("%s\n", __func__);

	if (pdev->dev.of_node) {
		pdata = rockchip_ion_parse_dt(&pdev->dev);
		if (IS_ERR(pdata)) {
			return PTR_ERR(pdata);
		}
		pdata_needs_to_be_freed = 1;
	} else {
		pdata = pdev->dev.platform_data;
		pdata_needs_to_be_freed = 0;
	}

	num_heaps = pdata->nr;
	heaps = kzalloc(sizeof(struct ion_heap *) * num_heaps, GFP_KERNEL);

	idev = ion_device_create(rockchip_custom_ioctl);
	if (IS_ERR_OR_NULL(idev)) {
		kfree(heaps);
		return PTR_ERR(idev);
	}
	/* create the heaps as specified in the board file */
	for (i = 0; i < num_heaps; i++) {
		struct ion_platform_heap *heap_data = &pdata->heaps[i];

		heaps[i] = ion_heap_create(heap_data);
		if (IS_ERR_OR_NULL(heaps[i])) {
			err = PTR_ERR(heaps[i]);
			goto err;
		}
		ion_device_add_heap(idev, heaps[i]);
	}
	platform_set_drvdata(pdev, idev);
	if (pdata_needs_to_be_freed)
		kfree(pdata);

        pr_info("Rockchip ion module is successfully loaded\n");
	return 0;
err:
	for (i = 0; i < num_heaps; i++) {
        	if (heaps[i])
			ion_heap_destroy(heaps[i]);
	}
	if (pdata_needs_to_be_freed)
		kfree(pdata);
	kfree(heaps);
	return err;
}

static int rockchip_ion_remove(struct platform_device *pdev)
{
        struct ion_device *idev = platform_get_drvdata(pdev);
        int i;

        ion_device_destroy(idev);
        for (i = 0; i < num_heaps; i++)
                ion_heap_destroy(heaps[i]);
        kfree(heaps);
        return 0;
}

static const struct of_device_id rockchip_ion_dt_ids[] = {
        { .compatible = "rockchip,ion", },
        {}
};

static struct platform_driver ion_driver = {
        .probe = rockchip_ion_probe,
        .remove = rockchip_ion_remove,
        .driver = {
                .name = "ion-rockchip",
                .owner	= THIS_MODULE,
                .of_match_table = of_match_ptr(rockchip_ion_dt_ids),
        },
};

static int __init ion_init(void)
{
        return platform_driver_register(&ion_driver);
}

static void __exit ion_exit(void)
{
        platform_driver_unregister(&ion_driver);
}

subsys_initcall(ion_init);
module_exit(ion_exit);

