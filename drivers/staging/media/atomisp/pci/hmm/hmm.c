// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010-2017 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */
/*
 * This file contains entry functions for memory management of ISP driver
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/highmem.h>	/* for kmap */
#include <linux/io.h>		/* for page_to_phys */
#include <linux/sysfs.h>

#include "hmm/hmm.h"
#include "hmm/hmm_bo.h"

#include "atomisp_internal.h"
#include "asm/cacheflush.h"
#include "mmu/isp_mmu.h"
#include "mmu/sh_mmu_mrfld.h"

struct hmm_bo_device bo_device;
static ia_css_ptr dummy_ptr = mmgr_EXCEPTION;
static bool hmm_initialized;

/*
 * p: private
 * v: vmalloc
 */
static const char hmm_bo_type_string[] = "pv";

static ssize_t bo_show(struct device *dev, struct device_attribute *attr,
		       char *buf, struct list_head *bo_list, bool active)
{
	ssize_t ret = 0;
	struct hmm_buffer_object *bo;
	unsigned long flags;
	int i;
	long total[HMM_BO_LAST] = { 0 };
	long count[HMM_BO_LAST] = { 0 };
	int index1 = 0;
	int index2 = 0;

	ret = scnprintf(buf, PAGE_SIZE, "type pgnr\n");
	if (ret <= 0)
		return 0;

	index1 += ret;

	spin_lock_irqsave(&bo_device.list_lock, flags);
	list_for_each_entry(bo, bo_list, list) {
		if ((active && (bo->status & HMM_BO_ALLOCED)) ||
		    (!active && !(bo->status & HMM_BO_ALLOCED))) {
			ret = scnprintf(buf + index1, PAGE_SIZE - index1,
					"%c %d\n",
					hmm_bo_type_string[bo->type], bo->pgnr);

			total[bo->type] += bo->pgnr;
			count[bo->type]++;
			if (ret > 0)
				index1 += ret;
		}
	}
	spin_unlock_irqrestore(&bo_device.list_lock, flags);

	for (i = 0; i < HMM_BO_LAST; i++) {
		if (count[i]) {
			ret = scnprintf(buf + index1 + index2,
					PAGE_SIZE - index1 - index2,
					"%ld %c buffer objects: %ld KB\n",
					count[i], hmm_bo_type_string[i],
					total[i] * 4);
			if (ret > 0)
				index2 += ret;
		}
	}

	/* Add trailing zero, not included by scnprintf */
	return index1 + index2 + 1;
}

static ssize_t active_bo_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return bo_show(dev, attr, buf, &bo_device.entire_bo_list, true);
}

static ssize_t free_bo_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return bo_show(dev, attr, buf, &bo_device.entire_bo_list, false);
}


static DEVICE_ATTR_RO(active_bo);
static DEVICE_ATTR_RO(free_bo);

static struct attribute *sysfs_attrs_ctrl[] = {
	&dev_attr_active_bo.attr,
	&dev_attr_free_bo.attr,
	NULL
};

static struct attribute_group atomisp_attribute_group[] = {
	{.attrs = sysfs_attrs_ctrl },
};

int hmm_init(void)
{
	int ret;

	ret = hmm_bo_device_init(&bo_device, &sh_mmu_mrfld,
				 ISP_VM_START, ISP_VM_SIZE);
	if (ret)
		dev_err(atomisp_dev, "hmm_bo_device_init failed.\n");

	hmm_initialized = true;

	/*
	 * As hmm use NULL to indicate invalid ISP virtual address,
	 * and ISP_VM_START is defined to 0 too, so we allocate
	 * one piece of dummy memory, which should return value 0,
	 * at the beginning, to avoid hmm_alloc return 0 in the
	 * further allocation.
	 */
	dummy_ptr = hmm_alloc(1);

	if (!ret) {
		ret = sysfs_create_group(&atomisp_dev->kobj,
					 atomisp_attribute_group);
		if (ret)
			dev_err(atomisp_dev,
				"%s Failed to create sysfs\n", __func__);
	}

	return ret;
}

void hmm_cleanup(void)
{
	if (dummy_ptr == mmgr_EXCEPTION)
		return;
	sysfs_remove_group(&atomisp_dev->kobj, atomisp_attribute_group);

	/* free dummy memory first */
	hmm_free(dummy_ptr);
	dummy_ptr = 0;

	hmm_bo_device_exit(&bo_device);
	hmm_initialized = false;
}

static ia_css_ptr __hmm_alloc(size_t bytes, enum hmm_bo_type type,
			      void *vmalloc_addr)
{
	unsigned int pgnr;
	struct hmm_buffer_object *bo;
	int ret;

	/*
	 * Check if we are initialized. In the ideal world we wouldn't need
	 * this but we can tackle it once the driver is a lot cleaner
	 */

	if (!hmm_initialized)
		hmm_init();
	/* Get page number from size */
	pgnr = size_to_pgnr_ceil(bytes);

	/* Buffer object structure init */
	bo = hmm_bo_alloc(&bo_device, pgnr);
	if (!bo) {
		dev_err(atomisp_dev, "hmm_bo_create failed.\n");
		goto create_bo_err;
	}

	/* Allocate pages for memory */
	ret = hmm_bo_alloc_pages(bo, type, vmalloc_addr);
	if (ret) {
		dev_err(atomisp_dev, "hmm_bo_alloc_pages failed.\n");
		goto alloc_page_err;
	}

	/* Combine the virtual address and pages together */
	ret = hmm_bo_bind(bo);
	if (ret) {
		dev_err(atomisp_dev, "hmm_bo_bind failed.\n");
		goto bind_err;
	}

	dev_dbg(atomisp_dev, "pages: 0x%08x (%zu bytes), type: %d, vmalloc %p\n",
		bo->start, bytes, type, vmalloc_noprof);

	return bo->start;

bind_err:
	hmm_bo_free_pages(bo);
alloc_page_err:
	hmm_bo_unref(bo);
create_bo_err:
	return 0;
}

ia_css_ptr hmm_alloc(size_t bytes)
{
	return __hmm_alloc(bytes, HMM_BO_PRIVATE, NULL);
}

ia_css_ptr hmm_create_from_vmalloc_buf(size_t bytes, void *vmalloc_addr)
{
	return __hmm_alloc(bytes, HMM_BO_VMALLOC, vmalloc_addr);
}

void hmm_free(ia_css_ptr virt)
{
	struct hmm_buffer_object *bo;

	dev_dbg(atomisp_dev, "%s: free 0x%08x\n", __func__, virt);

	if (WARN_ON(virt == mmgr_EXCEPTION))
		return;

	bo = hmm_bo_device_search_start(&bo_device, (unsigned int)virt);

	if (!bo) {
		dev_err(atomisp_dev,
			"can not find buffer object start with address 0x%x\n",
			(unsigned int)virt);
		return;
	}

	hmm_bo_unbind(bo);
	hmm_bo_free_pages(bo);
	hmm_bo_unref(bo);
}

static inline int hmm_check_bo(struct hmm_buffer_object *bo, unsigned int ptr)
{
	if (!bo) {
		dev_err(atomisp_dev,
			"can not find buffer object contains address 0x%x\n",
			ptr);
		return -EINVAL;
	}

	if (!hmm_bo_page_allocated(bo)) {
		dev_err(atomisp_dev,
			"buffer object has no page allocated.\n");
		return -EINVAL;
	}

	if (!hmm_bo_allocated(bo)) {
		dev_err(atomisp_dev,
			"buffer object has no virtual address space allocated.\n");
		return -EINVAL;
	}

	return 0;
}

/* Read function in ISP memory management */
static int load_and_flush_by_kmap(ia_css_ptr virt, void *data,
				  unsigned int bytes)
{
	struct hmm_buffer_object *bo;
	unsigned int idx, offset, len;
	char *src, *des;
	int ret;

	bo = hmm_bo_device_search_in_range(&bo_device, virt);
	ret = hmm_check_bo(bo, virt);
	if (ret)
		return ret;

	des = (char *)data;
	while (bytes) {
		idx = (virt - bo->start) >> PAGE_SHIFT;
		offset = (virt - bo->start) - (idx << PAGE_SHIFT);

		src = (char *)kmap_local_page(bo->pages[idx]) + offset;

		if ((bytes + offset) >= PAGE_SIZE) {
			len = PAGE_SIZE - offset;
			bytes -= len;
		} else {
			len = bytes;
			bytes = 0;
		}

		virt += len;	/* update virt for next loop */

		if (des) {
			memcpy(des, src, len);
			des += len;
		}

		clflush_cache_range(src, len);

		kunmap_local(src);
	}

	return 0;
}

/* Read function in ISP memory management */
static int load_and_flush(ia_css_ptr virt, void *data, unsigned int bytes)
{
	struct hmm_buffer_object *bo;
	int ret;

	bo = hmm_bo_device_search_in_range(&bo_device, virt);
	ret = hmm_check_bo(bo, virt);
	if (ret)
		return ret;

	if (bo->status & HMM_BO_VMAPED || bo->status & HMM_BO_VMAPED_CACHED) {
		void *src = bo->vmap_addr;

		src += (virt - bo->start);
		memcpy(data, src, bytes);
		if (bo->status & HMM_BO_VMAPED_CACHED)
			clflush_cache_range(src, bytes);
	} else {
		void *vptr;

		vptr = hmm_bo_vmap(bo, true);
		if (!vptr)
			return load_and_flush_by_kmap(virt, data, bytes);
		else
			vptr = vptr + (virt - bo->start);

		memcpy(data, vptr, bytes);
		clflush_cache_range(vptr, bytes);
		hmm_bo_vunmap(bo);
	}

	return 0;
}

/* Read function in ISP memory management */
int hmm_load(ia_css_ptr virt, void *data, unsigned int bytes)
{
	if (!virt) {
		dev_warn(atomisp_dev,
			"hmm_store: address is NULL\n");
		return -EINVAL;
	}
	if (!data) {
		dev_err(atomisp_dev,
			"hmm_store: data is a NULL argument\n");
		return -EINVAL;
	}
	return load_and_flush(virt, data, bytes);
}

/* Flush hmm data from the data cache */
int hmm_flush(ia_css_ptr virt, unsigned int bytes)
{
	return load_and_flush(virt, NULL, bytes);
}

/* Write function in ISP memory management */
int hmm_store(ia_css_ptr virt, const void *data, unsigned int bytes)
{
	struct hmm_buffer_object *bo;
	unsigned int idx, offset, len;
	char *src, *des;
	int ret;

	if (!virt) {
		dev_warn(atomisp_dev,
			"hmm_store: address is NULL\n");
		return -EINVAL;
	}
	if (!data) {
		dev_err(atomisp_dev,
			"hmm_store: data is a NULL argument\n");
		return -EINVAL;
	}

	bo = hmm_bo_device_search_in_range(&bo_device, virt);
	ret = hmm_check_bo(bo, virt);
	if (ret)
		return ret;

	if (bo->status & HMM_BO_VMAPED || bo->status & HMM_BO_VMAPED_CACHED) {
		void *dst = bo->vmap_addr;

		dst += (virt - bo->start);
		memcpy(dst, data, bytes);
		if (bo->status & HMM_BO_VMAPED_CACHED)
			clflush_cache_range(dst, bytes);
	} else {
		void *vptr;

		vptr = hmm_bo_vmap(bo, true);
		if (vptr) {
			vptr = vptr + (virt - bo->start);

			memcpy(vptr, data, bytes);
			clflush_cache_range(vptr, bytes);
			hmm_bo_vunmap(bo);
			return 0;
		}
	}

	src = (char *)data;
	while (bytes) {
		idx = (virt - bo->start) >> PAGE_SHIFT;
		offset = (virt - bo->start) - (idx << PAGE_SHIFT);

		des = (char *)kmap_local_page(bo->pages[idx]);

		if (!des) {
			dev_err(atomisp_dev,
				"kmap buffer object page failed: pg_idx = %d\n",
				idx);
			return -EINVAL;
		}

		des += offset;

		if ((bytes + offset) >= PAGE_SIZE) {
			len = PAGE_SIZE - offset;
			bytes -= len;
		} else {
			len = bytes;
			bytes = 0;
		}

		virt += len;

		memcpy(des, src, len);

		src += len;

		clflush_cache_range(des, len);

		kunmap_local(des);
	}

	return 0;
}

/* memset function in ISP memory management */
int hmm_set(ia_css_ptr virt, int c, unsigned int bytes)
{
	struct hmm_buffer_object *bo;
	unsigned int idx, offset, len;
	char *des;
	int ret;

	bo = hmm_bo_device_search_in_range(&bo_device, virt);
	ret = hmm_check_bo(bo, virt);
	if (ret)
		return ret;

	if (bo->status & HMM_BO_VMAPED || bo->status & HMM_BO_VMAPED_CACHED) {
		void *dst = bo->vmap_addr;

		dst += (virt - bo->start);
		memset(dst, c, bytes);

		if (bo->status & HMM_BO_VMAPED_CACHED)
			clflush_cache_range(dst, bytes);
	} else {
		void *vptr;

		vptr = hmm_bo_vmap(bo, true);
		if (vptr) {
			vptr = vptr + (virt - bo->start);
			memset(vptr, c, bytes);
			clflush_cache_range(vptr, bytes);
			hmm_bo_vunmap(bo);
			return 0;
		}
	}

	while (bytes) {
		idx = (virt - bo->start) >> PAGE_SHIFT;
		offset = (virt - bo->start) - (idx << PAGE_SHIFT);

		des = (char *)kmap_local_page(bo->pages[idx]) + offset;

		if ((bytes + offset) >= PAGE_SIZE) {
			len = PAGE_SIZE - offset;
			bytes -= len;
		} else {
			len = bytes;
			bytes = 0;
		}

		virt += len;

		memset(des, c, len);

		clflush_cache_range(des, len);

		kunmap_local(des);
	}

	return 0;
}

/* Virtual address to physical address convert */
phys_addr_t hmm_virt_to_phys(ia_css_ptr virt)
{
	unsigned int idx, offset;
	struct hmm_buffer_object *bo;

	bo = hmm_bo_device_search_in_range(&bo_device, virt);
	if (!bo) {
		dev_err(atomisp_dev,
			"can not find buffer object contains address 0x%x\n",
			virt);
		return -1;
	}

	idx = (virt - bo->start) >> PAGE_SHIFT;
	offset = (virt - bo->start) - (idx << PAGE_SHIFT);

	return page_to_phys(bo->pages[idx]) + offset;
}

int hmm_mmap(struct vm_area_struct *vma, ia_css_ptr virt)
{
	struct hmm_buffer_object *bo;

	bo = hmm_bo_device_search_start(&bo_device, virt);
	if (!bo) {
		dev_err(atomisp_dev,
			"can not find buffer object start with address 0x%x\n",
			virt);
		return -EINVAL;
	}

	return hmm_bo_mmap(vma, bo);
}

/* Map ISP virtual address into IA virtual address */
void *hmm_vmap(ia_css_ptr virt, bool cached)
{
	struct hmm_buffer_object *bo;
	void *ptr;

	bo = hmm_bo_device_search_in_range(&bo_device, virt);
	if (!bo) {
		dev_err(atomisp_dev,
			"can not find buffer object contains address 0x%x\n",
			virt);
		return NULL;
	}

	ptr = hmm_bo_vmap(bo, cached);
	if (ptr)
		return ptr + (virt - bo->start);
	else
		return NULL;
}

/* Flush the memory which is mapped as cached memory through hmm_vmap */
void hmm_flush_vmap(ia_css_ptr virt)
{
	struct hmm_buffer_object *bo;

	bo = hmm_bo_device_search_in_range(&bo_device, virt);
	if (!bo) {
		dev_warn(atomisp_dev,
			 "can not find buffer object contains address 0x%x\n",
			 virt);
		return;
	}

	hmm_bo_flush_vmap(bo);
}

void hmm_vunmap(ia_css_ptr virt)
{
	struct hmm_buffer_object *bo;

	bo = hmm_bo_device_search_in_range(&bo_device, virt);
	if (!bo) {
		dev_warn(atomisp_dev,
			 "can not find buffer object contains address 0x%x\n",
			 virt);
		return;
	}

	hmm_bo_vunmap(bo);
}
