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
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/dma-buf.h>
#include <linux/dma-contiguous.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/of_gpio.h>
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

#ifdef CONFIG_COMPAT
struct compat_ion_phys_data {
	compat_int_t handle;
	compat_ulong_t phys;
	compat_ulong_t size;
};

#define COMPAT_ION_IOC_GET_PHYS _IOWR(ION_IOC_ROCKCHIP_MAGIC, 0, \
				struct compat_ion_phys_data)

static int compat_get_ion_phys_data(
		struct compat_ion_phys_data __user *data32,
		struct ion_phys_data __user *data)
{
	compat_ulong_t l;
	compat_int_t i;
	int err;

	err = get_user(i, &data32->handle);
	err |= put_user(i, &data->handle);
	err |= get_user(l, &data32->phys);
	err |= put_user(l, &data->phys);
	err |= get_user(l, &data32->size);
	err |= put_user(l, &data->size);

	return err;
};

static int compat_put_ion_phys_data(
		struct compat_ion_phys_data __user *data32,
		struct ion_phys_data __user *data)
{
	compat_ulong_t l;
	compat_int_t i;
	int err;

	err = get_user(i, &data->handle);
	err |= put_user(i, &data32->handle);
	err |= get_user(l, &data->phys);
	err |= put_user(l, &data32->phys);
	err |= get_user(l, &data->size);
	err |= put_user(l, &data32->size);

	return err;
};
#endif

static int rk_ion_get_phys(struct ion_client *client,
			   unsigned long arg)
{
	struct ion_phys_data data;
	struct ion_handle *handle;
	int ret;

	if (copy_from_user(&data, (void __user *)arg,
			   sizeof(struct ion_phys_data)))
		return -EFAULT;

	handle = ion_handle_get_by_id(client, data.handle);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	ret = ion_phys(client, handle, &data.phys,
		       (size_t *)&data.size);
	ion_handle_put(handle);
	if (ret < 0)
		return ret;
	if (copy_to_user((void __user *)arg, &data,
			 sizeof(struct ion_phys_data)))
		return -EFAULT;

	return 0;
}

#ifdef CONFIG_COMPAT
static int compat_rk_ion_get_phys(struct ion_client *client,
				  unsigned long arg)
{
	struct compat_ion_phys_data __user *data32;
	struct ion_phys_data __user *data;
	int err;
	long ret;

	data32 = compat_ptr(arg);
	data = compat_alloc_user_space(sizeof(*data));
	if (!data)
		return -EFAULT;

	err = compat_get_ion_phys_data(data32, data);
	if (err)
		return err;

	ret = rk_ion_get_phys(client, (unsigned long)data);
	err = compat_put_ion_phys_data(data32, data);

	return ret ? ret : err;
}
#endif

static long rk_custom_ioctl(struct ion_client *client,
			    unsigned int cmd,
			    unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
#ifdef CONFIG_COMPAT
	case COMPAT_ION_IOC_GET_PHYS:
		ret = compat_rk_ion_get_phys(client, arg);
		break;
#endif
	case ION_IOC_GET_PHYS:
		ret = rk_ion_get_phys(client, arg);
		break;
	default:
		return -ENOTTY;
	}

	return ret;
}

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

	idev = ion_device_create(rk_custom_ioctl);
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
