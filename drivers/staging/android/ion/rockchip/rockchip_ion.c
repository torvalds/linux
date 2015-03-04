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
#include <linux/rockchip_ion.h>
#include <linux/uaccess.h>
#include "../ion_priv.h"
#include <linux/dma-buf.h>
#include <linux/dma-contiguous.h>
#include <linux/memblock.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <video/of_display_timing.h>
#include <linux/of_fdt.h>
#endif

#include <linux/compat.h>

struct ion_device *rockchip_ion_dev;
EXPORT_SYMBOL(rockchip_ion_dev);

static int num_heaps;
static struct ion_heap **heaps;

struct ion_heap_desc {
	unsigned int id;
	enum ion_heap_type type;
	const char *name;
};

extern struct ion_handle *ion_handle_get_by_id(struct ion_client *client,
 						int id);
extern int ion_handle_put(struct ion_handle *handle);

#define ION_CMA_HEAP_NAME		"cma"
#define ION_IOMMU_HEAP_NAME		"iommu"
#define ION_VMALLOC_HEAP_NAME		"vmalloc"
#define ION_DRM_HEAP_NAME		"drm"
#define ION_CARVEOUT_HEAP_NAME		"carveout"

#define MAX_ION_HEAP		10

static struct ion_platform_heap ion_plat_heap[MAX_ION_HEAP];
struct ion_platform_data ion_pdata = {
	.nr = 0,
	.heaps = ion_plat_heap,
};

static struct ion_heap_desc ion_heap_meta[] = {
	{
		.id	= ION_VMALLOC_HEAP_ID,
		.type	= ION_HEAP_TYPE_SYSTEM,
		.name	= ION_VMALLOC_HEAP_NAME,
	},
	{
		.id	= ION_CMA_HEAP_ID,
		.type	= ION_HEAP_TYPE_DMA,
		.name	= ION_CMA_HEAP_NAME,
	},
	{
		.id 	= ION_DRM_HEAP_ID,
		.type	= ION_HEAP_TYPE_DRM,
		.name	= ION_DRM_HEAP_NAME,
	},
	{
		.id 	= ION_CARVEOUT_HEAP_ID,
		.type	= ION_HEAP_TYPE_CARVEOUT,
		.name	= ION_CARVEOUT_HEAP_NAME,
	},
};

struct device rockchip_ion_cma_dev = {
	.coherent_dma_mask = DMA_BIT_MASK(32),
	.init_name = "rockchip_ion_cma",
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
			if(heap->id == ION_CMA_HEAP_ID)
				heap->priv = &rockchip_ion_cma_dev;
			ret = 0;
			break;
		}
	}
	if (ret)
		pr_err("%s: Unable to populate heap, error: %d", __func__, ret);
	return ret;
}

struct ion_client *rockchip_ion_client_create(const char *name)
{
	return ion_client_create(rockchip_ion_dev, name);
}
EXPORT_SYMBOL(rockchip_ion_client_create);

#ifdef CONFIG_COMPAT
struct compat_ion_phys_data {
	compat_int_t handle;
	compat_ulong_t phys;
	compat_ulong_t size;
};

#define COMPAT_ION_IOC_GET_PHYS	_IOWR(ION_IOC_ROCKCHIP_MAGIC, 0, \
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

static int rockchip_ion_get_phys(struct ion_client *client, unsigned long arg)
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

	ret = ion_phys(client, handle, &data.phys, (size_t *)&data.size);
	pr_debug("ret=%d, phys=0x%lX\n", ret, data.phys);
	ion_handle_put(handle);
	if (ret < 0)
		return ret;
	if (copy_to_user((void __user *)arg, &data,
			 sizeof(struct ion_phys_data)))
		return -EFAULT;

	return 0;
}

static long rockchip_custom_ioctl (struct ion_client *client, unsigned int cmd,
			      unsigned long arg)
{
	pr_debug("%s(%d): cmd=%x\n", __func__, __LINE__, cmd);

	if (is_compat_task()) {
#ifdef CONFIG_COMPAT
		long ret;
		switch (cmd) {
		case COMPAT_ION_IOC_GET_PHYS: {
			struct compat_ion_phys_data __user *data32;
			struct ion_phys_data __user *data;
			int err;

			data32 = compat_ptr(arg);
			data = compat_alloc_user_space(sizeof(*data));
			if (data == NULL)
				return -EFAULT;

			err = compat_get_ion_phys_data(data32, data);
			if (err)
				return err;

			ret = rockchip_ion_get_phys(client,
						    (unsigned long)data);

			err = compat_put_ion_phys_data(data32, data);
			return ret ? ret : err;
		}
		default:
			return -ENOTTY;
		}
#endif
	} else {
		switch (cmd) {
		case ION_IOC_GET_PHYS:
			return rockchip_ion_get_phys(client, arg);
		default:
			return -ENOTTY;
		}
	}
	
	return 0;
}

static int rockchip_ion_probe(struct platform_device *pdev)
{
	struct ion_platform_data *pdata;
	struct ion_device *idev;
	int err;
	int i;

	err = device_register(&rockchip_ion_cma_dev);
	if (err) {
		pr_err("Could not register %s\n", dev_name(&rockchip_ion_cma_dev));
		return err;
	}

	if (pdev->dev.of_node) {
		pdata = &ion_pdata;
		if (IS_ERR(pdata)) {
			return PTR_ERR(pdata);
		}
	} else {
		pdata = pdev->dev.platform_data;
	}

	num_heaps = pdata->nr;
	heaps = kzalloc(sizeof(struct ion_heap *) * num_heaps, GFP_KERNEL);

	idev = ion_device_create(rockchip_custom_ioctl);
	if (IS_ERR_OR_NULL(idev)) {
		kfree(heaps);
		return PTR_ERR(idev);
	}
	rockchip_ion_dev = idev;
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

	pr_info("Rockchip ion module is successfully loaded (%s)\n", ROCKCHIP_ION_VERSION);
	return 0;
err:
	for (i = 0; i < num_heaps; i++) {
		if (heaps[i])
		ion_heap_destroy(heaps[i]);
	}
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

int __init rockchip_ion_find_heap(unsigned long node, const char *uname,
				int depth, void *data)
{
	const __be32 *prop;
	int len;
	struct ion_platform_heap* heap;
	struct ion_platform_data* pdata = (struct ion_platform_data*)data;

	if (pdata==NULL || pdata->nr >= MAX_ION_HEAP) {
		// break now
		pr_err("ion heap is too much!\n");
		return 1;
	}

	if (!of_flat_dt_is_compatible(node, "rockchip,ion-heap"))
		return 0;

	prop = of_get_flat_dt_prop(node, "rockchip,ion_heap", &len);
	if (!prop || (len != sizeof(__be32)))
		return 0;

	heap = &pdata->heaps[pdata->nr++];
	heap->base = heap->size = heap->align = 0;
	heap->id = be32_to_cpu(prop[0]);
	rockchip_ion_populate_heap(heap);

	prop = of_get_flat_dt_prop(node, "reg", &len);
	if (prop && (len >= 2*sizeof(__be32))) {
		heap->base = be32_to_cpu(prop[0]);
		heap->size = be32_to_cpu(prop[1]);
		if (len==3*sizeof(__be32))
			heap->align = be32_to_cpu(prop[2]);
	}

	pr_info("ion heap(%s): base(%lx) size(%zx) align(%lx)\n", heap->name,
			heap->base, heap->size, heap->align);
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

