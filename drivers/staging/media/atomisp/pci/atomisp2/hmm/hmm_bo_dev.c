/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/mm.h>		/* for GFP_ATOMIC */
#include <linux/slab.h>		/* for kmalloc */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/errno.h>

#ifdef CONFIG_ION
#include <linux/ion.h>
#endif

#include "atomisp_internal.h"
#include "hmm/hmm_common.h"
#include "hmm/hmm_bo_dev.h"
#include "hmm/hmm_bo.h"

/*
 * hmm_bo_device functions.
 */
int hmm_bo_device_init(struct hmm_bo_device *bdev,
		       struct isp_mmu_client *mmu_driver,
		       unsigned int vaddr_start, unsigned int size)
{
	int ret;

	check_bodev_null_return(bdev, -EINVAL);

	ret = isp_mmu_init(&bdev->mmu, mmu_driver);
	if (ret) {
		dev_err(atomisp_dev, "isp_mmu_init failed.\n");
		goto isp_mmu_init_err;
	}

	ret = hmm_vm_init(&bdev->vaddr_space, vaddr_start, size);
	if (ret) {
		dev_err(atomisp_dev, "hmm_vm_init falied. vaddr_start = 0x%x, size = %d\n",
			vaddr_start, size);
		goto vm_init_err;
	}

	INIT_LIST_HEAD(&bdev->free_bo_list);
	INIT_LIST_HEAD(&bdev->active_bo_list);

	spin_lock_init(&bdev->list_lock);
#ifdef CONFIG_ION
	/*
	 * TODO:
	 * The ion_dev should be defined by ION driver. But ION driver does
	 * not implement it yet, will fix it when it is ready.
	 */
	if (!ion_dev)
		goto vm_init_err;

	bdev->iclient = ion_client_create(ion_dev, "atomisp");
	if (IS_ERR_OR_NULL(bdev->iclient)) {
		ret = PTR_ERR(bdev->iclient);
		if (!bdev->iclient)
			ret = -EINVAL;
		goto vm_init_err;
	}
#endif
	bdev->flag = HMM_BO_DEVICE_INITED;

	return 0;

vm_init_err:
	isp_mmu_exit(&bdev->mmu);
isp_mmu_init_err:
	return ret;
}

void hmm_bo_device_exit(struct hmm_bo_device *bdev)
{
	check_bodev_null_return_void(bdev);

	/*
	 * destroy all bos in the bo list, even they are in use.
	 */
	if (!list_empty(&bdev->active_bo_list))
		dev_warn(atomisp_dev,
			     "there're still activated bo in use. "
			     "force to free them.\n");

	while (!list_empty(&bdev->active_bo_list))
		hmm_bo_unref(list_to_hmm_bo(bdev->active_bo_list.next));

	if (!list_empty(&bdev->free_bo_list))
		dev_warn(atomisp_dev,
				"there're still bo in free_bo_list. "
				"force to free them.\n");

	while (!list_empty(&bdev->free_bo_list))
		hmm_bo_unref(list_to_hmm_bo(bdev->free_bo_list.next));

	isp_mmu_exit(&bdev->mmu);
	hmm_vm_clean(&bdev->vaddr_space);
#ifdef CONFIG_ION
	if (bdev->iclient != NULL)
		ion_client_destroy(bdev->iclient);
#endif
}

int hmm_bo_device_inited(struct hmm_bo_device *bdev)
{
	check_bodev_null_return(bdev, -EINVAL);

	return bdev->flag == HMM_BO_DEVICE_INITED;
}

/*
 * find the buffer object with virtual address vaddr.
 * return NULL if no such buffer object found.
 */
struct hmm_buffer_object *hmm_bo_device_search_start(struct hmm_bo_device *bdev,
						     ia_css_ptr vaddr)
{
	struct list_head *pos;
	struct hmm_buffer_object *bo;
	unsigned long flags;

	check_bodev_null_return(bdev, NULL);

	spin_lock_irqsave(&bdev->list_lock, flags);
	list_for_each(pos, &bdev->active_bo_list) {
		bo = list_to_hmm_bo(pos);
		/* pass bo which has no vm_node allocated */
		if (!hmm_bo_vm_allocated(bo))
			continue;
		if (bo->vm_node->start == vaddr)
			goto found;
	}
	spin_unlock_irqrestore(&bdev->list_lock, flags);
	return NULL;
found:
	spin_unlock_irqrestore(&bdev->list_lock, flags);
	return bo;
}

static int in_range(unsigned int start, unsigned int size, unsigned int addr)
{
	return (start <= addr) && (start + size > addr);
}

struct hmm_buffer_object *hmm_bo_device_search_in_range(struct hmm_bo_device
							*bdev,
							unsigned int vaddr)
{
	struct list_head *pos;
	struct hmm_buffer_object *bo;
	unsigned long flags;
	int cnt = 0;

	check_bodev_null_return(bdev, NULL);

	spin_lock_irqsave(&bdev->list_lock, flags);
	list_for_each(pos, &bdev->active_bo_list) {
		cnt++;
		bo = list_to_hmm_bo(pos);
		/* pass bo which has no vm_node allocated */
		if (!hmm_bo_vm_allocated(bo))
			continue;
		if (in_range(bo->vm_node->start, bo->vm_node->size, vaddr))
			goto found;
	}
	spin_unlock_irqrestore(&bdev->list_lock, flags);
	return NULL;
found:
	if (cnt > HMM_BO_CACHE_SIZE)
		list_move(pos, &bdev->active_bo_list);
	spin_unlock_irqrestore(&bdev->list_lock, flags);
	return bo;
}

struct hmm_buffer_object *
hmm_bo_device_search_vmap_start(struct hmm_bo_device *bdev, const void *vaddr)
{
	struct list_head *pos;
	struct hmm_buffer_object *bo;
	unsigned long flags;

	check_bodev_null_return(bdev, NULL);

	spin_lock_irqsave(&bdev->list_lock, flags);
	list_for_each(pos, &bdev->active_bo_list) {
		bo = list_to_hmm_bo(pos);
		/* pass bo which has no vm_node allocated */
		if (!hmm_bo_vm_allocated(bo))
			continue;
		if (bo->vmap_addr == vaddr)
			goto found;
	}
	spin_unlock_irqrestore(&bdev->list_lock, flags);
	return NULL;
found:
	spin_unlock_irqrestore(&bdev->list_lock, flags);
	return bo;
}

/*
 * find a buffer object with pgnr pages from free_bo_list and
 * activate it (remove from free_bo_list and add to
 * active_bo_list)
 *
 * return NULL if no such buffer object found.
 */
struct hmm_buffer_object *hmm_bo_device_get_bo(struct hmm_bo_device *bdev,
					       unsigned int pgnr)
{
	struct list_head *pos;
	struct hmm_buffer_object *bo;
	unsigned long flags;

	check_bodev_null_return(bdev, NULL);

	spin_lock_irqsave(&bdev->list_lock, flags);
	list_for_each(pos, &bdev->free_bo_list) {
		bo = list_to_hmm_bo(pos);
		if (bo->pgnr == pgnr)
			goto found;
	}
	spin_unlock_irqrestore(&bdev->list_lock, flags);
	return NULL;
found:
	list_del(&bo->list);
	list_add(&bo->list, &bdev->active_bo_list);
	spin_unlock_irqrestore(&bdev->list_lock, flags);

	return bo;
}

/*
 * destroy all buffer objects in the free_bo_list.
 */
void hmm_bo_device_destroy_free_bo_list(struct hmm_bo_device *bdev)
{
	struct hmm_buffer_object *bo, *tmp;
	unsigned long flags;
	struct list_head new_head;

	check_bodev_null_return_void(bdev);

	spin_lock_irqsave(&bdev->list_lock, flags);
	list_replace_init(&bdev->free_bo_list, &new_head);
	spin_unlock_irqrestore(&bdev->list_lock, flags);

	list_for_each_entry_safe(bo, tmp, &new_head, list) {
		list_del(&bo->list);
		hmm_bo_unref(bo);
	}
}

/*
 * destroy buffer object with start virtual address vaddr.
 */
void hmm_bo_device_destroy_free_bo_addr(struct hmm_bo_device *bdev,
					unsigned int vaddr)
{
	struct list_head *pos;
	struct hmm_buffer_object *bo;
	unsigned long flags;

	check_bodev_null_return_void(bdev);

	spin_lock_irqsave(&bdev->list_lock, flags);
	list_for_each(pos, &bdev->free_bo_list) {
		bo = list_to_hmm_bo(pos);
		/* pass bo which has no vm_node allocated */
		if (!hmm_bo_vm_allocated(bo))
			continue;
		if (bo->vm_node->start == vaddr)
			goto found;
	}
	spin_unlock_irqrestore(&bdev->list_lock, flags);
	return;
found:
	list_del(&bo->list);
	spin_unlock_irqrestore(&bdev->list_lock, flags);
	hmm_bo_unref(bo);
}

/*
 * destroy all buffer objects with pgnr pages.
 */
void hmm_bo_device_destroy_free_bo_size(struct hmm_bo_device *bdev,
					unsigned int pgnr)
{
	struct list_head *pos;
	struct hmm_buffer_object *bo;
	unsigned long flags;

	check_bodev_null_return_void(bdev);

retry:
	spin_lock_irqsave(&bdev->list_lock, flags);
	list_for_each(pos, &bdev->free_bo_list) {
		bo = list_to_hmm_bo(pos);
		if (bo->pgnr == pgnr)
			goto found;
	}
	spin_unlock_irqrestore(&bdev->list_lock, flags);
	return;
found:
	list_del(&bo->list);
	spin_unlock_irqrestore(&bdev->list_lock, flags);
	hmm_bo_unref(bo);
	goto retry;
}
