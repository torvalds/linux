// SPDX-License-Identifier: GPL-2.0-only
/*
 *  PS3 system bus driver.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/dma-map-ops.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <asm/udbg.h>
#include <asm/lv1call.h>
#include <asm/firmware.h>
#include <asm/cell-regs.h>

#include "platform.h"

static struct device ps3_system_bus = {
	.init_name = "ps3_system",
};

/* FIXME: need device usage counters! */
static struct {
	struct mutex mutex;
	int sb_11; /* usb 0 */
	int sb_12; /* usb 0 */
	int gpu;
} usage_hack;

static int ps3_is_device(struct ps3_system_bus_device *dev, u64 bus_id,
			 u64 dev_id)
{
	return dev->bus_id == bus_id && dev->dev_id == dev_id;
}

static int ps3_open_hv_device_sb(struct ps3_system_bus_device *dev)
{
	int result;

	BUG_ON(!dev->bus_id);
	mutex_lock(&usage_hack.mutex);

	if (ps3_is_device(dev, 1, 1)) {
		usage_hack.sb_11++;
		if (usage_hack.sb_11 > 1) {
			result = 0;
			goto done;
		}
	}

	if (ps3_is_device(dev, 1, 2)) {
		usage_hack.sb_12++;
		if (usage_hack.sb_12 > 1) {
			result = 0;
			goto done;
		}
	}

	result = lv1_open_device(dev->bus_id, dev->dev_id, 0);

	if (result) {
		pr_warn("%s:%d: lv1_open_device dev=%u.%u(%s) failed: %s\n",
			__func__, __LINE__, dev->match_id, dev->match_sub_id,
			dev_name(&dev->core), ps3_result(result));
		result = -EPERM;
	}

done:
	mutex_unlock(&usage_hack.mutex);
	return result;
}

static int ps3_close_hv_device_sb(struct ps3_system_bus_device *dev)
{
	int result;

	BUG_ON(!dev->bus_id);
	mutex_lock(&usage_hack.mutex);

	if (ps3_is_device(dev, 1, 1)) {
		usage_hack.sb_11--;
		if (usage_hack.sb_11) {
			result = 0;
			goto done;
		}
	}

	if (ps3_is_device(dev, 1, 2)) {
		usage_hack.sb_12--;
		if (usage_hack.sb_12) {
			result = 0;
			goto done;
		}
	}

	result = lv1_close_device(dev->bus_id, dev->dev_id);
	BUG_ON(result);

done:
	mutex_unlock(&usage_hack.mutex);
	return result;
}

static int ps3_open_hv_device_gpu(struct ps3_system_bus_device *dev)
{
	int result;

	mutex_lock(&usage_hack.mutex);

	usage_hack.gpu++;
	if (usage_hack.gpu > 1) {
		result = 0;
		goto done;
	}

	result = lv1_gpu_open(0);

	if (result) {
		pr_warn("%s:%d: lv1_gpu_open failed: %s\n", __func__,
			__LINE__, ps3_result(result));
			result = -EPERM;
	}

done:
	mutex_unlock(&usage_hack.mutex);
	return result;
}

static int ps3_close_hv_device_gpu(struct ps3_system_bus_device *dev)
{
	int result;

	mutex_lock(&usage_hack.mutex);

	usage_hack.gpu--;
	if (usage_hack.gpu) {
		result = 0;
		goto done;
	}

	result = lv1_gpu_close();
	BUG_ON(result);

done:
	mutex_unlock(&usage_hack.mutex);
	return result;
}

int ps3_open_hv_device(struct ps3_system_bus_device *dev)
{
	BUG_ON(!dev);
	pr_debug("%s:%d: match_id: %u\n", __func__, __LINE__, dev->match_id);

	switch (dev->match_id) {
	case PS3_MATCH_ID_EHCI:
	case PS3_MATCH_ID_OHCI:
	case PS3_MATCH_ID_GELIC:
	case PS3_MATCH_ID_STOR_DISK:
	case PS3_MATCH_ID_STOR_ROM:
	case PS3_MATCH_ID_STOR_FLASH:
		return ps3_open_hv_device_sb(dev);

	case PS3_MATCH_ID_SOUND:
	case PS3_MATCH_ID_GPU:
		return ps3_open_hv_device_gpu(dev);

	case PS3_MATCH_ID_AV_SETTINGS:
	case PS3_MATCH_ID_SYSTEM_MANAGER:
		pr_debug("%s:%d: unsupported match_id: %u\n", __func__,
			__LINE__, dev->match_id);
		pr_debug("%s:%d: bus_id: %llu\n", __func__, __LINE__,
			dev->bus_id);
		BUG();
		return -EINVAL;

	default:
		break;
	}

	pr_debug("%s:%d: unknown match_id: %u\n", __func__, __LINE__,
		dev->match_id);
	BUG();
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(ps3_open_hv_device);

int ps3_close_hv_device(struct ps3_system_bus_device *dev)
{
	BUG_ON(!dev);
	pr_debug("%s:%d: match_id: %u\n", __func__, __LINE__, dev->match_id);

	switch (dev->match_id) {
	case PS3_MATCH_ID_EHCI:
	case PS3_MATCH_ID_OHCI:
	case PS3_MATCH_ID_GELIC:
	case PS3_MATCH_ID_STOR_DISK:
	case PS3_MATCH_ID_STOR_ROM:
	case PS3_MATCH_ID_STOR_FLASH:
		return ps3_close_hv_device_sb(dev);

	case PS3_MATCH_ID_SOUND:
	case PS3_MATCH_ID_GPU:
		return ps3_close_hv_device_gpu(dev);

	case PS3_MATCH_ID_AV_SETTINGS:
	case PS3_MATCH_ID_SYSTEM_MANAGER:
		pr_debug("%s:%d: unsupported match_id: %u\n", __func__,
			__LINE__, dev->match_id);
		pr_debug("%s:%d: bus_id: %llu\n", __func__, __LINE__,
			dev->bus_id);
		BUG();
		return -EINVAL;

	default:
		break;
	}

	pr_debug("%s:%d: unknown match_id: %u\n", __func__, __LINE__,
		dev->match_id);
	BUG();
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(ps3_close_hv_device);

#define dump_mmio_region(_a) _dump_mmio_region(_a, __func__, __LINE__)
static void _dump_mmio_region(const struct ps3_mmio_region* r,
	const char* func, int line)
{
	pr_debug("%s:%d: dev       %llu:%llu\n", func, line, r->dev->bus_id,
		r->dev->dev_id);
	pr_debug("%s:%d: bus_addr  %lxh\n", func, line, r->bus_addr);
	pr_debug("%s:%d: len       %lxh\n", func, line, r->len);
	pr_debug("%s:%d: lpar_addr %lxh\n", func, line, r->lpar_addr);
}

static int ps3_sb_mmio_region_create(struct ps3_mmio_region *r)
{
	int result;
	u64 lpar_addr;

	result = lv1_map_device_mmio_region(r->dev->bus_id, r->dev->dev_id,
		r->bus_addr, r->len, r->page_size, &lpar_addr);
	r->lpar_addr = lpar_addr;

	if (result) {
		pr_debug("%s:%d: lv1_map_device_mmio_region failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		r->lpar_addr = 0;
	}

	dump_mmio_region(r);
	return result;
}

static int ps3_ioc0_mmio_region_create(struct ps3_mmio_region *r)
{
	/* device specific; do nothing currently */
	return 0;
}

int ps3_mmio_region_create(struct ps3_mmio_region *r)
{
	return r->mmio_ops->create(r);
}
EXPORT_SYMBOL_GPL(ps3_mmio_region_create);

static int ps3_sb_free_mmio_region(struct ps3_mmio_region *r)
{
	int result;

	dump_mmio_region(r);
	result = lv1_unmap_device_mmio_region(r->dev->bus_id, r->dev->dev_id,
		r->lpar_addr);

	if (result)
		pr_debug("%s:%d: lv1_unmap_device_mmio_region failed: %s\n",
			__func__, __LINE__, ps3_result(result));

	r->lpar_addr = 0;
	return result;
}

static int ps3_ioc0_free_mmio_region(struct ps3_mmio_region *r)
{
	/* device specific; do nothing currently */
	return 0;
}


int ps3_free_mmio_region(struct ps3_mmio_region *r)
{
	return r->mmio_ops->free(r);
}

EXPORT_SYMBOL_GPL(ps3_free_mmio_region);

static const struct ps3_mmio_region_ops ps3_mmio_sb_region_ops = {
	.create = ps3_sb_mmio_region_create,
	.free = ps3_sb_free_mmio_region
};

static const struct ps3_mmio_region_ops ps3_mmio_ioc0_region_ops = {
	.create = ps3_ioc0_mmio_region_create,
	.free = ps3_ioc0_free_mmio_region
};

int ps3_mmio_region_init(struct ps3_system_bus_device *dev,
	struct ps3_mmio_region *r, unsigned long bus_addr, unsigned long len,
	enum ps3_mmio_page_size page_size)
{
	r->dev = dev;
	r->bus_addr = bus_addr;
	r->len = len;
	r->page_size = page_size;
	switch (dev->dev_type) {
	case PS3_DEVICE_TYPE_SB:
		r->mmio_ops = &ps3_mmio_sb_region_ops;
		break;
	case PS3_DEVICE_TYPE_IOC0:
		r->mmio_ops = &ps3_mmio_ioc0_region_ops;
		break;
	default:
		BUG();
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ps3_mmio_region_init);

static int ps3_system_bus_match(struct device *_dev,
	struct device_driver *_drv)
{
	int result;
	struct ps3_system_bus_driver *drv = ps3_drv_to_system_bus_drv(_drv);
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);

	if (!dev->match_sub_id)
		result = dev->match_id == drv->match_id;
	else
		result = dev->match_sub_id == drv->match_sub_id &&
			dev->match_id == drv->match_id;

	if (result)
		pr_info("%s:%d: dev=%u.%u(%s), drv=%u.%u(%s): match\n",
			__func__, __LINE__,
			dev->match_id, dev->match_sub_id, dev_name(&dev->core),
			drv->match_id, drv->match_sub_id, drv->core.name);
	else
		pr_debug("%s:%d: dev=%u.%u(%s), drv=%u.%u(%s): miss\n",
			__func__, __LINE__,
			dev->match_id, dev->match_sub_id, dev_name(&dev->core),
			drv->match_id, drv->match_sub_id, drv->core.name);

	return result;
}

static int ps3_system_bus_probe(struct device *_dev)
{
	int result = 0;
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);
	struct ps3_system_bus_driver *drv;

	BUG_ON(!dev);
	dev_dbg(_dev, "%s:%d\n", __func__, __LINE__);

	drv = ps3_system_bus_dev_to_system_bus_drv(dev);
	BUG_ON(!drv);

	if (drv->probe)
		result = drv->probe(dev);
	else
		pr_debug("%s:%d: %s no probe method\n", __func__, __LINE__,
			dev_name(&dev->core));

	pr_debug(" <- %s:%d: %s\n", __func__, __LINE__, dev_name(&dev->core));
	return result;
}

static void ps3_system_bus_remove(struct device *_dev)
{
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);
	struct ps3_system_bus_driver *drv;

	BUG_ON(!dev);
	dev_dbg(_dev, "%s:%d\n", __func__, __LINE__);

	drv = ps3_system_bus_dev_to_system_bus_drv(dev);
	BUG_ON(!drv);

	if (drv->remove)
		drv->remove(dev);
	else
		dev_dbg(&dev->core, "%s:%d %s: no remove method\n",
			__func__, __LINE__, drv->core.name);

	pr_debug(" <- %s:%d: %s\n", __func__, __LINE__, dev_name(&dev->core));
}

static void ps3_system_bus_shutdown(struct device *_dev)
{
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);
	struct ps3_system_bus_driver *drv;

	BUG_ON(!dev);

	dev_dbg(&dev->core, " -> %s:%d: match_id %d\n", __func__, __LINE__,
		dev->match_id);

	if (!dev->core.driver) {
		dev_dbg(&dev->core, "%s:%d: no driver bound\n", __func__,
			__LINE__);
		return;
	}

	drv = ps3_system_bus_dev_to_system_bus_drv(dev);

	BUG_ON(!drv);

	dev_dbg(&dev->core, "%s:%d: %s -> %s\n", __func__, __LINE__,
		dev_name(&dev->core), drv->core.name);

	if (drv->shutdown)
		drv->shutdown(dev);
	else if (drv->remove) {
		dev_dbg(&dev->core, "%s:%d %s: no shutdown, calling remove\n",
			__func__, __LINE__, drv->core.name);
		drv->remove(dev);
	} else {
		dev_dbg(&dev->core, "%s:%d %s: no shutdown method\n",
			__func__, __LINE__, drv->core.name);
		BUG();
	}

	dev_dbg(&dev->core, " <- %s:%d\n", __func__, __LINE__);
}

static int ps3_system_bus_uevent(struct device *_dev, struct kobj_uevent_env *env)
{
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);

	if (add_uevent_var(env, "MODALIAS=ps3:%d:%d", dev->match_id,
			   dev->match_sub_id))
		return -ENOMEM;
	return 0;
}

static ssize_t modalias_show(struct device *_dev, struct device_attribute *a,
	char *buf)
{
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);
	int len = snprintf(buf, PAGE_SIZE, "ps3:%d:%d\n", dev->match_id,
			   dev->match_sub_id);

	return (len >= PAGE_SIZE) ? (PAGE_SIZE - 1) : len;
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *ps3_system_bus_dev_attrs[] = {
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ps3_system_bus_dev);

struct bus_type ps3_system_bus_type = {
	.name = "ps3_system_bus",
	.match = ps3_system_bus_match,
	.uevent = ps3_system_bus_uevent,
	.probe = ps3_system_bus_probe,
	.remove = ps3_system_bus_remove,
	.shutdown = ps3_system_bus_shutdown,
	.dev_groups = ps3_system_bus_dev_groups,
};

static int __init ps3_system_bus_init(void)
{
	int result;

	if (!firmware_has_feature(FW_FEATURE_PS3_LV1))
		return -ENODEV;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	mutex_init(&usage_hack.mutex);

	result = device_register(&ps3_system_bus);
	BUG_ON(result);

	result = bus_register(&ps3_system_bus_type);
	BUG_ON(result);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

core_initcall(ps3_system_bus_init);

/* Allocates a contiguous real buffer and creates mappings over it.
 * Returns the virtual address of the buffer and sets dma_handle
 * to the dma address (mapping) of the first page.
 */
static void * ps3_alloc_coherent(struct device *_dev, size_t size,
				 dma_addr_t *dma_handle, gfp_t flag,
				 unsigned long attrs)
{
	int result;
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);
	unsigned long virt_addr;

	flag &= ~(__GFP_DMA | __GFP_HIGHMEM);
	flag |= __GFP_ZERO;

	virt_addr = __get_free_pages(flag, get_order(size));

	if (!virt_addr) {
		pr_debug("%s:%d: get_free_pages failed\n", __func__, __LINE__);
		goto clean_none;
	}

	result = ps3_dma_map(dev->d_region, virt_addr, size, dma_handle,
			     CBE_IOPTE_PP_W | CBE_IOPTE_PP_R |
			     CBE_IOPTE_SO_RW | CBE_IOPTE_M);

	if (result) {
		pr_debug("%s:%d: ps3_dma_map failed (%d)\n",
			__func__, __LINE__, result);
		BUG_ON("check region type");
		goto clean_alloc;
	}

	return (void*)virt_addr;

clean_alloc:
	free_pages(virt_addr, get_order(size));
clean_none:
	dma_handle = NULL;
	return NULL;
}

static void ps3_free_coherent(struct device *_dev, size_t size, void *vaddr,
			      dma_addr_t dma_handle, unsigned long attrs)
{
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);

	ps3_dma_unmap(dev->d_region, dma_handle, size);
	free_pages((unsigned long)vaddr, get_order(size));
}

/* Creates TCEs for a user provided buffer.  The user buffer must be
 * contiguous real kernel storage (not vmalloc).  The address passed here
 * comprises a page address and offset into that page. The dma_addr_t
 * returned will point to the same byte within the page as was passed in.
 */

static dma_addr_t ps3_sb_map_page(struct device *_dev, struct page *page,
	unsigned long offset, size_t size, enum dma_data_direction direction,
	unsigned long attrs)
{
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);
	int result;
	dma_addr_t bus_addr;
	void *ptr = page_address(page) + offset;

	result = ps3_dma_map(dev->d_region, (unsigned long)ptr, size,
			     &bus_addr,
			     CBE_IOPTE_PP_R | CBE_IOPTE_PP_W |
			     CBE_IOPTE_SO_RW | CBE_IOPTE_M);

	if (result) {
		pr_debug("%s:%d: ps3_dma_map failed (%d)\n",
			__func__, __LINE__, result);
	}

	return bus_addr;
}

static dma_addr_t ps3_ioc0_map_page(struct device *_dev, struct page *page,
				    unsigned long offset, size_t size,
				    enum dma_data_direction direction,
				    unsigned long attrs)
{
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);
	int result;
	dma_addr_t bus_addr;
	u64 iopte_flag;
	void *ptr = page_address(page) + offset;

	iopte_flag = CBE_IOPTE_M;
	switch (direction) {
	case DMA_BIDIRECTIONAL:
		iopte_flag |= CBE_IOPTE_PP_R | CBE_IOPTE_PP_W | CBE_IOPTE_SO_RW;
		break;
	case DMA_TO_DEVICE:
		iopte_flag |= CBE_IOPTE_PP_R | CBE_IOPTE_SO_R;
		break;
	case DMA_FROM_DEVICE:
		iopte_flag |= CBE_IOPTE_PP_W | CBE_IOPTE_SO_RW;
		break;
	default:
		/* not happned */
		BUG();
	}
	result = ps3_dma_map(dev->d_region, (unsigned long)ptr, size,
			     &bus_addr, iopte_flag);

	if (result) {
		pr_debug("%s:%d: ps3_dma_map failed (%d)\n",
			__func__, __LINE__, result);
	}
	return bus_addr;
}

static void ps3_unmap_page(struct device *_dev, dma_addr_t dma_addr,
	size_t size, enum dma_data_direction direction, unsigned long attrs)
{
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);
	int result;

	result = ps3_dma_unmap(dev->d_region, dma_addr, size);

	if (result) {
		pr_debug("%s:%d: ps3_dma_unmap failed (%d)\n",
			__func__, __LINE__, result);
	}
}

static int ps3_sb_map_sg(struct device *_dev, struct scatterlist *sgl,
	int nents, enum dma_data_direction direction, unsigned long attrs)
{
#if defined(CONFIG_PS3_DYNAMIC_DMA)
	BUG_ON("do");
	return -EPERM;
#else
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		int result = ps3_dma_map(dev->d_region, sg_phys(sg),
					sg->length, &sg->dma_address, 0);

		if (result) {
			pr_debug("%s:%d: ps3_dma_map failed (%d)\n",
				__func__, __LINE__, result);
			return -EINVAL;
		}

		sg->dma_length = sg->length;
	}

	return nents;
#endif
}

static int ps3_ioc0_map_sg(struct device *_dev, struct scatterlist *sg,
			   int nents,
			   enum dma_data_direction direction,
			   unsigned long attrs)
{
	BUG();
	return -EINVAL;
}

static void ps3_sb_unmap_sg(struct device *_dev, struct scatterlist *sg,
	int nents, enum dma_data_direction direction, unsigned long attrs)
{
#if defined(CONFIG_PS3_DYNAMIC_DMA)
	BUG_ON("do");
#endif
}

static void ps3_ioc0_unmap_sg(struct device *_dev, struct scatterlist *sg,
			    int nents, enum dma_data_direction direction,
			    unsigned long attrs)
{
	BUG();
}

static int ps3_dma_supported(struct device *_dev, u64 mask)
{
	return mask >= DMA_BIT_MASK(32);
}

static const struct dma_map_ops ps3_sb_dma_ops = {
	.alloc = ps3_alloc_coherent,
	.free = ps3_free_coherent,
	.map_sg = ps3_sb_map_sg,
	.unmap_sg = ps3_sb_unmap_sg,
	.dma_supported = ps3_dma_supported,
	.map_page = ps3_sb_map_page,
	.unmap_page = ps3_unmap_page,
	.mmap = dma_common_mmap,
	.get_sgtable = dma_common_get_sgtable,
	.alloc_pages = dma_common_alloc_pages,
	.free_pages = dma_common_free_pages,
};

static const struct dma_map_ops ps3_ioc0_dma_ops = {
	.alloc = ps3_alloc_coherent,
	.free = ps3_free_coherent,
	.map_sg = ps3_ioc0_map_sg,
	.unmap_sg = ps3_ioc0_unmap_sg,
	.dma_supported = ps3_dma_supported,
	.map_page = ps3_ioc0_map_page,
	.unmap_page = ps3_unmap_page,
	.mmap = dma_common_mmap,
	.get_sgtable = dma_common_get_sgtable,
	.alloc_pages = dma_common_alloc_pages,
	.free_pages = dma_common_free_pages,
};

/**
 * ps3_system_bus_release_device - remove a device from the system bus
 */

static void ps3_system_bus_release_device(struct device *_dev)
{
	struct ps3_system_bus_device *dev = ps3_dev_to_system_bus_dev(_dev);
	kfree(dev);
}

/**
 * ps3_system_bus_device_register - add a device to the system bus
 *
 * ps3_system_bus_device_register() expects the dev object to be allocated
 * dynamically by the caller.  The system bus takes ownership of the dev
 * object and frees the object in ps3_system_bus_release_device().
 */

int ps3_system_bus_device_register(struct ps3_system_bus_device *dev)
{
	int result;
	static unsigned int dev_ioc0_count;
	static unsigned int dev_sb_count;
	static unsigned int dev_vuart_count;
	static unsigned int dev_lpm_count;

	if (!dev->core.parent)
		dev->core.parent = &ps3_system_bus;
	dev->core.bus = &ps3_system_bus_type;
	dev->core.release = ps3_system_bus_release_device;

	switch (dev->dev_type) {
	case PS3_DEVICE_TYPE_IOC0:
		dev->core.dma_ops = &ps3_ioc0_dma_ops;
		dev_set_name(&dev->core, "ioc0_%02x", ++dev_ioc0_count);
		break;
	case PS3_DEVICE_TYPE_SB:
		dev->core.dma_ops = &ps3_sb_dma_ops;
		dev_set_name(&dev->core, "sb_%02x", ++dev_sb_count);

		break;
	case PS3_DEVICE_TYPE_VUART:
		dev_set_name(&dev->core, "vuart_%02x", ++dev_vuart_count);
		break;
	case PS3_DEVICE_TYPE_LPM:
		dev_set_name(&dev->core, "lpm_%02x", ++dev_lpm_count);
		break;
	default:
		BUG();
	}

	dev->core.of_node = NULL;
	set_dev_node(&dev->core, 0);

	pr_debug("%s:%d add %s\n", __func__, __LINE__, dev_name(&dev->core));

	result = device_register(&dev->core);
	return result;
}

EXPORT_SYMBOL_GPL(ps3_system_bus_device_register);

int ps3_system_bus_driver_register(struct ps3_system_bus_driver *drv)
{
	int result;

	pr_debug(" -> %s:%d: %s\n", __func__, __LINE__, drv->core.name);

	if (!firmware_has_feature(FW_FEATURE_PS3_LV1))
		return -ENODEV;

	drv->core.bus = &ps3_system_bus_type;

	result = driver_register(&drv->core);
	pr_debug(" <- %s:%d: %s\n", __func__, __LINE__, drv->core.name);
	return result;
}

EXPORT_SYMBOL_GPL(ps3_system_bus_driver_register);

void ps3_system_bus_driver_unregister(struct ps3_system_bus_driver *drv)
{
	pr_debug(" -> %s:%d: %s\n", __func__, __LINE__, drv->core.name);
	driver_unregister(&drv->core);
	pr_debug(" <- %s:%d: %s\n", __func__, __LINE__, drv->core.name);
}

EXPORT_SYMBOL_GPL(ps3_system_bus_driver_unregister);
