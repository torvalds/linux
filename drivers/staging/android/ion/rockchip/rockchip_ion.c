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

static struct ion_device *idev;
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

static struct ion_heap_desc ion_heap_meta[] = {
	{
		.id		= ION_VMALLOC_HEAP_ID,
		.type	= ION_HEAP_TYPE_SYSTEM,
		.name	= ION_VMALLOC_HEAP_NAME,
	},
	{
		.id		= ION_CMA_HEAP_ID,
		.type	= ION_HEAP_TYPE_DMA,
		.name	= ION_CMA_HEAP_NAME,
	},
	{
		.id		= ION_IOMMU_HEAP_ID,
		.type	= ION_HEAP_TYPE_DMA,//ION_HEAP_TYPE_IOMMU,
		.name	= ION_IOMMU_HEAP_NAME,
	},
	{
		.id 	= ION_DRM_HEAP_ID,
		.type	= ION_HEAP_TYPE_DMA,
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

static int rockchip_ion_get_heap_base_size(struct device_node *node,
				 struct ion_platform_heap *heap)
{
	unsigned int val[2];
	int ret = 0;

	ret = of_property_read_u32_array(node,
			"reg", val, 2);
	if (!ret) {
		heap->base = val[0];
		heap->size = val[1];
	}

	pr_debug("heap: %x@%lx\n", heap->size, heap->base);

	return 0;
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
		ret = of_property_read_u32(node, "rockchip,ion_heap", &val);
		if (ret) {
			pr_err("%s: Unable to find reg key", __func__);
			goto free_heaps;
		}
		pdata->heaps[idx].id = val;

		ret = rockchip_ion_populate_heap(&pdata->heaps[idx]);
		if (ret)
			goto free_heaps;

//		rockchip_ion_get_heap_align(node, &pdata->heaps[idx]);
		ret = rockchip_ion_get_heap_base_size(node, &pdata->heaps[idx]);
		if (ret)
			goto free_heaps;

//		rockchip_ion_get_heap_adjacent(node, &pdata->heaps[idx]);
		pr_info("%d:  %d  %d  %s  0x%p\n", idx, pdata->heaps[idx].type, pdata->heaps[idx].id, pdata->heaps[idx].name, pdata->heaps[idx].priv);

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
	pr_debug("[%s %d] cmd=%X\n", __func__, __LINE__, cmd);

	switch (cmd) {
	case ION_IOC_CLEAN_CACHES:
	case ION_IOC_INV_CACHES:
	case ION_IOC_CLEAN_INV_CACHES:
		break;
	case ION_IOC_GET_PHYS:
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
		if(ret < 0)
			return ret;
		if (copy_to_user((void __user *)arg, &data, sizeof(struct ion_phys_data)))
			return -EFAULT;
		break;
	}
	case ION_IOC_GET_SHARE_ID:
	{
		struct ion_share_id_data data;
		struct dma_buf *dmabuf = NULL;

		if (copy_from_user(&data, (void __user *)arg,
					sizeof(struct ion_share_id_data)))
			return -EFAULT;

		dmabuf = dma_buf_get(data.fd);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);

		data.id = (unsigned int)dmabuf;
//		dma_buf_put(dmabuf);

		if (copy_to_user((void __user *)arg, &data, sizeof(struct ion_share_id_data)))
			return -EFAULT;

		break;
	}
	case ION_IOC_SHARE_BY_ID:
	{
		struct ion_share_id_data data;
		int fd = 0;

		if (copy_from_user(&data, (void __user *)arg,
					sizeof(struct ion_share_id_data)))
			return -EFAULT;

		fd = dma_buf_fd((struct dma_buf*)data.id, O_CLOEXEC);
		if (fd < 0)
			return fd;

		data.fd = fd;

		if (copy_to_user((void __user *)arg, &data, sizeof(struct ion_share_id_data)))
			return -EFAULT;

		break;
	}
	default:
		return -ENOTTY;
	}
	
	return 0;
}

static int rockchip_ion_probe(struct platform_device *pdev)
{
	struct ion_platform_data *pdata;
	unsigned int pdata_needs_to_be_freed;
	int err;
	int i;

	err = device_register(&rockchip_ion_cma_dev);
	if (err) {
		pr_err("Could not register %s\n", dev_name(&rockchip_ion_cma_dev));
		return err;
	}

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

	pr_info("Rockchip ion module is successfully loaded (%s)\n", ROCKCHIP_ION_VERSION);
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

int __init rockchip_ion_find_reserve_mem(unsigned long node, const char *uname,
				int depth, void *data)
{
#ifdef CONFIG_CMA
	const __be32 *prop;
	int len;
	phys_addr_t size;
	phys_addr_t base;
	u32 heap_type;

	if (!of_flat_dt_is_compatible(node, "rockchip,ion-reserve"))
		return 0;

	prop = of_get_flat_dt_prop(node, "reg", &len);
	if (!prop || (len != 2 * sizeof(unsigned long)))
		return 0;

	base = be32_to_cpu(prop[0]);
	size = be32_to_cpu(prop[1]);

	prop = of_get_flat_dt_prop(node, "rockchip,ion_heap", &len);
	if (!prop || (len != sizeof(unsigned long)))
		return 0;

	heap_type = be32_to_cpu(prop[0]);

	pr_info("%s: heap type is %x\n", __func__, heap_type);

	if (heap_type==ION_CARVEOUT_HEAP_ID) {
		pr_info("%s: reserve carveout memory: %x@%x\n", __func__, size, base);
		memblock_remove(base, size);
	} else {
		pr_info("%s: reserve cma memory: %x@%x\n", __func__, size, base);
		dma_declare_contiguous(&rockchip_ion_cma_dev, size, base, 0);
	}
#endif

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

