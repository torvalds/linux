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
 *
 */
/*
 * This file contains functions for buffer object structure management
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/gfp.h>		/* for GFP_ATOMIC */
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/hugetlb.h>
#include <linux/highmem.h>
#include <linux/slab.h>		/* for kmalloc */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <asm/current.h>
#include <linux/sched/signal.h>
#include <linux/file.h>

#include <asm/set_memory.h>

#include "atomisp_internal.h"
#include "hmm/hmm_common.h"
#include "hmm/hmm_pool.h"
#include "hmm/hmm_bo.h"

static unsigned int order_to_nr(unsigned int order)
{
	return 1U << order;
}

static unsigned int nr_to_order_bottom(unsigned int nr)
{
	return fls(nr) - 1;
}

static struct hmm_buffer_object *__bo_alloc(struct kmem_cache *bo_cache)
{
	struct hmm_buffer_object *bo;

	bo = kmem_cache_alloc(bo_cache, GFP_KERNEL);
	if (!bo)
		dev_err(atomisp_dev, "%s: failed!\n", __func__);

	return bo;
}

static int __bo_init(struct hmm_bo_device *bdev, struct hmm_buffer_object *bo,
					unsigned int pgnr)
{
	check_bodev_null_return(bdev, -EINVAL);
	var_equal_return(hmm_bo_device_inited(bdev), 0, -EINVAL,
			"hmm_bo_device not inited yet.\n");
	/* prevent zero size buffer object */
	if (pgnr == 0) {
		dev_err(atomisp_dev, "0 size buffer is not allowed.\n");
		return -EINVAL;
	}

	memset(bo, 0, sizeof(*bo));
	mutex_init(&bo->mutex);

	/* init the bo->list HEAD as an element of entire_bo_list */
	INIT_LIST_HEAD(&bo->list);

	bo->bdev = bdev;
	bo->vmap_addr = NULL;
	bo->status = HMM_BO_FREE;
	bo->start = bdev->start;
	bo->pgnr = pgnr;
	bo->end = bo->start + pgnr_to_size(pgnr);
	bo->prev = NULL;
	bo->next = NULL;

	return 0;
}

static struct hmm_buffer_object *__bo_search_and_remove_from_free_rbtree(
				struct rb_node *node, unsigned int pgnr)
{
	struct hmm_buffer_object *this, *ret_bo, *temp_bo;

	this = rb_entry(node, struct hmm_buffer_object, node);
	if (this->pgnr == pgnr ||
		(this->pgnr > pgnr && this->node.rb_left == NULL)) {
		goto remove_bo_and_return;
	} else {
		if (this->pgnr < pgnr) {
			if (!this->node.rb_right)
				return NULL;
			ret_bo = __bo_search_and_remove_from_free_rbtree(
				this->node.rb_right, pgnr);
		} else {
			ret_bo = __bo_search_and_remove_from_free_rbtree(
				this->node.rb_left, pgnr);
		}
		if (!ret_bo) {
			if (this->pgnr > pgnr)
				goto remove_bo_and_return;
			else
				return NULL;
		}
		return ret_bo;
	}

remove_bo_and_return:
	/* NOTE: All nodes on free rbtree have a 'prev' that points to NULL.
	 * 1. check if 'this->next' is NULL:
	 *	yes: erase 'this' node and rebalance rbtree, return 'this'.
	 */
	if (this->next == NULL) {
		rb_erase(&this->node, &this->bdev->free_rbtree);
		return this;
	}
	/* NOTE: if 'this->next' is not NULL, always return 'this->next' bo.
	 * 2. check if 'this->next->next' is NULL:
	 *	yes: change the related 'next/prev' pointer,
	 *		return 'this->next' but the rbtree stays unchanged.
	 */
	temp_bo = this->next;
	this->next = temp_bo->next;
	if (temp_bo->next)
		temp_bo->next->prev = this;
	temp_bo->next = NULL;
	temp_bo->prev = NULL;
	return temp_bo;
}

static struct hmm_buffer_object *__bo_search_by_addr(struct rb_root *root,
							ia_css_ptr start)
{
	struct rb_node *n = root->rb_node;
	struct hmm_buffer_object *bo;

	do {
		bo = rb_entry(n, struct hmm_buffer_object, node);

		if (bo->start > start) {
			if (n->rb_left == NULL)
				return NULL;
			n = n->rb_left;
		} else if (bo->start < start) {
			if (n->rb_right == NULL)
				return NULL;
			n = n->rb_right;
		} else {
			return bo;
		}
	} while (n);

	return NULL;
}

static struct hmm_buffer_object *__bo_search_by_addr_in_range(
		struct rb_root *root, unsigned int start)
{
	struct rb_node *n = root->rb_node;
	struct hmm_buffer_object *bo;

	do {
		bo = rb_entry(n, struct hmm_buffer_object, node);

		if (bo->start > start) {
			if (n->rb_left == NULL)
				return NULL;
			n = n->rb_left;
		} else {
			if (bo->end > start)
				return bo;
			if (n->rb_right == NULL)
				return NULL;
			n = n->rb_right;
		}
	} while (n);

	return NULL;
}

static void __bo_insert_to_free_rbtree(struct rb_root *root,
					struct hmm_buffer_object *bo)
{
	struct rb_node **new = &(root->rb_node);
	struct rb_node *parent = NULL;
	struct hmm_buffer_object *this;
	unsigned int pgnr = bo->pgnr;

	while (*new) {
		parent = *new;
		this = container_of(*new, struct hmm_buffer_object, node);

		if (pgnr < this->pgnr) {
			new = &((*new)->rb_left);
		} else if (pgnr > this->pgnr) {
			new = &((*new)->rb_right);
		} else {
			bo->prev = this;
			bo->next = this->next;
			if (this->next)
				this->next->prev = bo;
			this->next = bo;
			bo->status = (bo->status & ~HMM_BO_MASK) | HMM_BO_FREE;
			return;
		}
	}

	bo->status = (bo->status & ~HMM_BO_MASK) | HMM_BO_FREE;

	rb_link_node(&bo->node, parent, new);
	rb_insert_color(&bo->node, root);
}

static void __bo_insert_to_alloc_rbtree(struct rb_root *root,
					struct hmm_buffer_object *bo)
{
	struct rb_node **new = &(root->rb_node);
	struct rb_node *parent = NULL;
	struct hmm_buffer_object *this;
	unsigned int start = bo->start;

	while (*new) {
		parent = *new;
		this = container_of(*new, struct hmm_buffer_object, node);

		if (start < this->start)
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	kref_init(&bo->kref);
	bo->status = (bo->status & ~HMM_BO_MASK) | HMM_BO_ALLOCED;

	rb_link_node(&bo->node, parent, new);
	rb_insert_color(&bo->node, root);
}

static struct hmm_buffer_object *__bo_break_up(struct hmm_bo_device *bdev,
					struct hmm_buffer_object *bo,
					unsigned int pgnr)
{
	struct hmm_buffer_object *new_bo;
	unsigned long flags;
	int ret;

	new_bo = __bo_alloc(bdev->bo_cache);
	if (!new_bo) {
		dev_err(atomisp_dev, "%s: __bo_alloc failed!\n", __func__);
		return NULL;
	}
	ret = __bo_init(bdev, new_bo, pgnr);
	if (ret) {
		dev_err(atomisp_dev, "%s: __bo_init failed!\n", __func__);
		kmem_cache_free(bdev->bo_cache, new_bo);
		return NULL;
	}

	new_bo->start = bo->start;
	new_bo->end = new_bo->start + pgnr_to_size(pgnr);
	bo->start = new_bo->end;
	bo->pgnr = bo->pgnr - pgnr;

	spin_lock_irqsave(&bdev->list_lock, flags);
	list_add_tail(&new_bo->list, &bo->list);
	spin_unlock_irqrestore(&bdev->list_lock, flags);

	return new_bo;
}

static void __bo_take_off_handling(struct hmm_buffer_object *bo)
{
	struct hmm_bo_device *bdev = bo->bdev;
	/* There are 4 situations when we take off a known bo from free rbtree:
	 * 1. if bo->next && bo->prev == NULL, bo is a rbtree node
	 *	and does not have a linked list after bo, to take off this bo,
	 *	we just need erase bo directly and rebalance the free rbtree
	 */
	if (bo->prev == NULL && bo->next == NULL) {
		rb_erase(&bo->node, &bdev->free_rbtree);
	/* 2. when bo->next != NULL && bo->prev == NULL, bo is a rbtree node,
	 *	and has a linked list,to take off this bo we need erase bo
	 *	first, then, insert bo->next into free rbtree and rebalance
	 *	the free rbtree
	 */
	} else if (bo->prev == NULL && bo->next != NULL) {
		bo->next->prev = NULL;
		rb_erase(&bo->node, &bdev->free_rbtree);
		__bo_insert_to_free_rbtree(&bdev->free_rbtree, bo->next);
		bo->next = NULL;
	/* 3. when bo->prev != NULL && bo->next == NULL, bo is not a rbtree
	 *	node, bo is the last element of the linked list after rbtree
	 *	node, to take off this bo, we just need set the "prev/next"
	 *	pointers to NULL, the free rbtree stays unchaged
	 */
	} else if (bo->prev != NULL && bo->next == NULL) {
		bo->prev->next = NULL;
		bo->prev = NULL;
	/* 4. when bo->prev != NULL && bo->next != NULL ,bo is not a rbtree
	 *	node, bo is in the middle of the linked list after rbtree node,
	 *	to take off this bo, we just set take the "prev/next" pointers
	 *	to NULL, the free rbtree stays unchaged
	 */
	} else {
		bo->next->prev = bo->prev;
		bo->prev->next = bo->next;
		bo->next = NULL;
		bo->prev = NULL;
	}
}

static struct hmm_buffer_object *__bo_merge(struct hmm_buffer_object *bo,
					struct hmm_buffer_object *next_bo)
{
	struct hmm_bo_device *bdev;
	unsigned long flags;

	bdev = bo->bdev;
	next_bo->start = bo->start;
	next_bo->pgnr = next_bo->pgnr + bo->pgnr;

	spin_lock_irqsave(&bdev->list_lock, flags);
	list_del(&bo->list);
	spin_unlock_irqrestore(&bdev->list_lock, flags);

	kmem_cache_free(bo->bdev->bo_cache, bo);

	return next_bo;
}

/*
 * hmm_bo_device functions.
 */
int hmm_bo_device_init(struct hmm_bo_device *bdev,
				struct isp_mmu_client *mmu_driver,
				unsigned int vaddr_start,
				unsigned int size)
{
	struct hmm_buffer_object *bo;
	unsigned long flags;
	int ret;

	check_bodev_null_return(bdev, -EINVAL);

	ret = isp_mmu_init(&bdev->mmu, mmu_driver);
	if (ret) {
		dev_err(atomisp_dev, "isp_mmu_init failed.\n");
		return ret;
	}

	bdev->start = vaddr_start;
	bdev->pgnr = size_to_pgnr_ceil(size);
	bdev->size = pgnr_to_size(bdev->pgnr);

	spin_lock_init(&bdev->list_lock);
	mutex_init(&bdev->rbtree_mutex);

	bdev->flag = HMM_BO_DEVICE_INITED;

	INIT_LIST_HEAD(&bdev->entire_bo_list);
	bdev->allocated_rbtree = RB_ROOT;
	bdev->free_rbtree = RB_ROOT;

	bdev->bo_cache = kmem_cache_create("bo_cache",
				sizeof(struct hmm_buffer_object), 0, 0, NULL);
	if (!bdev->bo_cache) {
		dev_err(atomisp_dev, "%s: create cache failed!\n", __func__);
		isp_mmu_exit(&bdev->mmu);
		return -ENOMEM;
	}

	bo = __bo_alloc(bdev->bo_cache);
	if (!bo) {
		dev_err(atomisp_dev, "%s: __bo_alloc failed!\n", __func__);
		isp_mmu_exit(&bdev->mmu);
		return -ENOMEM;
	}

	ret = __bo_init(bdev, bo, bdev->pgnr);
	if (ret) {
		dev_err(atomisp_dev, "%s: __bo_init failed!\n", __func__);
		kmem_cache_free(bdev->bo_cache, bo);
		isp_mmu_exit(&bdev->mmu);
		return -EINVAL;
	}

	spin_lock_irqsave(&bdev->list_lock, flags);
	list_add_tail(&bo->list, &bdev->entire_bo_list);
	spin_unlock_irqrestore(&bdev->list_lock, flags);

	__bo_insert_to_free_rbtree(&bdev->free_rbtree, bo);

	return 0;
}

struct hmm_buffer_object *hmm_bo_alloc(struct hmm_bo_device *bdev,
					unsigned int pgnr)
{
	struct hmm_buffer_object *bo, *new_bo;
	struct rb_root *root = &bdev->free_rbtree;

	check_bodev_null_return(bdev, NULL);
	var_equal_return(hmm_bo_device_inited(bdev), 0, NULL,
			"hmm_bo_device not inited yet.\n");

	if (pgnr == 0) {
		dev_err(atomisp_dev, "0 size buffer is not allowed.\n");
		return NULL;
	}

	mutex_lock(&bdev->rbtree_mutex);
	bo = __bo_search_and_remove_from_free_rbtree(root->rb_node, pgnr);
	if (!bo) {
		mutex_unlock(&bdev->rbtree_mutex);
		dev_err(atomisp_dev, "%s: Out of Memory! hmm_bo_alloc failed",
			__func__);
		return NULL;
	}

	if (bo->pgnr > pgnr) {
		new_bo = __bo_break_up(bdev, bo, pgnr);
		if (!new_bo) {
			mutex_unlock(&bdev->rbtree_mutex);
			dev_err(atomisp_dev, "%s: __bo_break_up failed!\n",
				__func__);
			return NULL;
		}

		__bo_insert_to_alloc_rbtree(&bdev->allocated_rbtree, new_bo);
		__bo_insert_to_free_rbtree(&bdev->free_rbtree, bo);

		mutex_unlock(&bdev->rbtree_mutex);
		return new_bo;
	}

	__bo_insert_to_alloc_rbtree(&bdev->allocated_rbtree, bo);

	mutex_unlock(&bdev->rbtree_mutex);
	return bo;
}

void hmm_bo_release(struct hmm_buffer_object *bo)
{
	struct hmm_bo_device *bdev = bo->bdev;
	struct hmm_buffer_object *next_bo, *prev_bo;

	mutex_lock(&bdev->rbtree_mutex);

	/*
	 * FIX ME:
	 *
	 * how to destroy the bo when it is stilled MMAPED?
	 *
	 * ideally, this will not happened as hmm_bo_release
	 * will only be called when kref reaches 0, and in mmap
	 * operation the hmm_bo_ref will eventually be called.
	 * so, if this happened, something goes wrong.
	 */
	if (bo->status & HMM_BO_MMAPED) {
		mutex_unlock(&bdev->rbtree_mutex);
		dev_dbg(atomisp_dev, "destroy bo which is MMAPED, do nothing\n");
		return;
	}

	if (bo->status & HMM_BO_BINDED) {
		dev_warn(atomisp_dev, "the bo is still binded, unbind it first...\n");
		hmm_bo_unbind(bo);
	}

	if (bo->status & HMM_BO_PAGE_ALLOCED) {
		dev_warn(atomisp_dev, "the pages is not freed, free pages first\n");
		hmm_bo_free_pages(bo);
	}
	if (bo->status & HMM_BO_VMAPED || bo->status & HMM_BO_VMAPED_CACHED) {
		dev_warn(atomisp_dev, "the vunmap is not done, do it...\n");
		hmm_bo_vunmap(bo);
	}

	rb_erase(&bo->node, &bdev->allocated_rbtree);

	prev_bo = list_entry(bo->list.prev, struct hmm_buffer_object, list);
	next_bo = list_entry(bo->list.next, struct hmm_buffer_object, list);

	if (bo->list.prev != &bdev->entire_bo_list &&
		prev_bo->end == bo->start &&
		(prev_bo->status & HMM_BO_MASK) == HMM_BO_FREE) {
		__bo_take_off_handling(prev_bo);
		bo = __bo_merge(prev_bo, bo);
	}

	if (bo->list.next != &bdev->entire_bo_list &&
		next_bo->start == bo->end &&
		(next_bo->status & HMM_BO_MASK) == HMM_BO_FREE) {
		__bo_take_off_handling(next_bo);
		bo = __bo_merge(bo, next_bo);
	}

	__bo_insert_to_free_rbtree(&bdev->free_rbtree, bo);

	mutex_unlock(&bdev->rbtree_mutex);
	return;
}

void hmm_bo_device_exit(struct hmm_bo_device *bdev)
{
	struct hmm_buffer_object *bo;
	unsigned long flags;

	dev_dbg(atomisp_dev, "%s: entering!\n", __func__);

	check_bodev_null_return_void(bdev);

	/*
	 * release all allocated bos even they a in use
	 * and all bos will be merged into a big bo
	 */
	while (!RB_EMPTY_ROOT(&bdev->allocated_rbtree))
		hmm_bo_release(
			rbtree_node_to_hmm_bo(bdev->allocated_rbtree.rb_node));

	dev_dbg(atomisp_dev, "%s: finished releasing all allocated bos!\n",
		__func__);

	/* free all bos to release all ISP virtual memory */
	while (!list_empty(&bdev->entire_bo_list)) {
		bo = list_to_hmm_bo(bdev->entire_bo_list.next);

		spin_lock_irqsave(&bdev->list_lock, flags);
		list_del(&bo->list);
		spin_unlock_irqrestore(&bdev->list_lock, flags);

		kmem_cache_free(bdev->bo_cache, bo);
	}

	dev_dbg(atomisp_dev, "%s: finished to free all bos!\n", __func__);

	kmem_cache_destroy(bdev->bo_cache);

	isp_mmu_exit(&bdev->mmu);
}

int hmm_bo_device_inited(struct hmm_bo_device *bdev)
{
	check_bodev_null_return(bdev, -EINVAL);

	return bdev->flag == HMM_BO_DEVICE_INITED;
}

int hmm_bo_allocated(struct hmm_buffer_object *bo)
{
	check_bo_null_return(bo, 0);

	return bo->status & HMM_BO_ALLOCED;
}

struct hmm_buffer_object *hmm_bo_device_search_start(
	struct hmm_bo_device *bdev, ia_css_ptr vaddr)
{
	struct hmm_buffer_object *bo;

	check_bodev_null_return(bdev, NULL);

	mutex_lock(&bdev->rbtree_mutex);
	bo = __bo_search_by_addr(&bdev->allocated_rbtree, vaddr);
	if (!bo) {
		mutex_unlock(&bdev->rbtree_mutex);
		dev_err(atomisp_dev, "%s can not find bo with addr: 0x%x\n",
			__func__, vaddr);
		return NULL;
	}
	mutex_unlock(&bdev->rbtree_mutex);

	return bo;
}

struct hmm_buffer_object *hmm_bo_device_search_in_range(
	struct hmm_bo_device *bdev, unsigned int vaddr)
{
	struct hmm_buffer_object *bo;

	check_bodev_null_return(bdev, NULL);

	mutex_lock(&bdev->rbtree_mutex);
	bo = __bo_search_by_addr_in_range(&bdev->allocated_rbtree, vaddr);
	if (!bo) {
		mutex_unlock(&bdev->rbtree_mutex);
		dev_err(atomisp_dev, "%s can not find bo contain addr: 0x%x\n",
			__func__, vaddr);
		return NULL;
	}
	mutex_unlock(&bdev->rbtree_mutex);

	return bo;
}

struct hmm_buffer_object *hmm_bo_device_search_vmap_start(
	struct hmm_bo_device *bdev, const void *vaddr)
{
	struct list_head *pos;
	struct hmm_buffer_object *bo;
	unsigned long flags;

	check_bodev_null_return(bdev, NULL);

	spin_lock_irqsave(&bdev->list_lock, flags);
	list_for_each(pos, &bdev->entire_bo_list) {
		bo = list_to_hmm_bo(pos);
		/* pass bo which has no vm_node allocated */
		if ((bo->status & HMM_BO_MASK) == HMM_BO_FREE)
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


static void free_private_bo_pages(struct hmm_buffer_object *bo,
				struct hmm_pool *dypool,
				struct hmm_pool *repool,
				int free_pgnr)
{
	int i, ret;

	for (i = 0; i < free_pgnr; i++) {
		switch (bo->page_obj[i].type) {
		case HMM_PAGE_TYPE_RESERVED:
			if (repool->pops
			    && repool->pops->pool_free_pages) {
				repool->pops->pool_free_pages(repool->pool_info,
							&bo->page_obj[i]);
				hmm_mem_stat.res_cnt--;
			}
			break;
		/*
		 * HMM_PAGE_TYPE_GENERAL indicates that pages are from system
		 * memory, so when free them, they should be put into dynamic
		 * pool.
		 */
		case HMM_PAGE_TYPE_DYNAMIC:
		case HMM_PAGE_TYPE_GENERAL:
			if (dypool->pops
			    && dypool->pops->pool_inited
			    && dypool->pops->pool_inited(dypool->pool_info)) {
				if (dypool->pops->pool_free_pages)
					dypool->pops->pool_free_pages(
							      dypool->pool_info,
							      &bo->page_obj[i]);
				break;
			}

			/*
			 * if dynamic memory pool doesn't exist, need to free
			 * pages to system directly.
			 */
		default:
			ret = set_pages_wb(bo->page_obj[i].page, 1);
			if (ret)
				dev_err(atomisp_dev,
						"set page to WB err ...ret = %d\n",
							ret);
			/*
			W/A: set_pages_wb seldom return value = -EFAULT
			indicate that address of page is not in valid
			range(0xffff880000000000~0xffffc7ffffffffff)
			then, _free_pages would panic; Do not know why page
			address be valid,it maybe memory corruption by lowmemory
			*/
			if (!ret) {
				__free_pages(bo->page_obj[i].page, 0);
				hmm_mem_stat.sys_size--;
			}
			break;
		}
	}

	return;
}

/*Allocate pages which will be used only by ISP*/
static int alloc_private_pages(struct hmm_buffer_object *bo,
				int from_highmem,
				bool cached,
				struct hmm_pool *dypool,
				struct hmm_pool *repool)
{
	int ret;
	unsigned int pgnr, order, blk_pgnr, alloc_pgnr;
	struct page *pages;
	gfp_t gfp = GFP_NOWAIT | __GFP_NOWARN; /* REVISIT: need __GFP_FS too? */
	int i, j;
	int failure_number = 0;
	bool reduce_order = false;
	bool lack_mem = true;

	if (from_highmem)
		gfp |= __GFP_HIGHMEM;

	pgnr = bo->pgnr;

	bo->page_obj = kmalloc_array(pgnr, sizeof(struct hmm_page_object),
				GFP_KERNEL);
	if (unlikely(!bo->page_obj))
		return -ENOMEM;

	i = 0;
	alloc_pgnr = 0;

	/*
	 * get physical pages from dynamic pages pool.
	 */
	if (dypool->pops && dypool->pops->pool_alloc_pages) {
		alloc_pgnr = dypool->pops->pool_alloc_pages(dypool->pool_info,
							bo->page_obj, pgnr,
							cached);
		hmm_mem_stat.dyc_size -= alloc_pgnr;

		if (alloc_pgnr == pgnr)
			return 0;
	}

	pgnr -= alloc_pgnr;
	i += alloc_pgnr;

	/*
	 * get physical pages from reserved pages pool for atomisp.
	 */
	if (repool->pops && repool->pops->pool_alloc_pages) {
		alloc_pgnr = repool->pops->pool_alloc_pages(repool->pool_info,
							&bo->page_obj[i], pgnr,
							cached);
		hmm_mem_stat.res_cnt += alloc_pgnr;
		if (alloc_pgnr == pgnr)
			return 0;
	}

	pgnr -= alloc_pgnr;
	i += alloc_pgnr;

	while (pgnr) {
		order = nr_to_order_bottom(pgnr);
		/*
		 * if be short of memory, we will set order to 0
		 * everytime.
		 */
		if (lack_mem)
			order = HMM_MIN_ORDER;
		else if (order > HMM_MAX_ORDER)
			order = HMM_MAX_ORDER;
retry:
		/*
		 * When order > HMM_MIN_ORDER, for performance reasons we don't
		 * want alloc_pages() to sleep. In case it fails and fallbacks
		 * to HMM_MIN_ORDER or in case the requested order is originally
		 * the minimum value, we can allow alloc_pages() to sleep for
		 * robustness purpose.
		 *
		 * REVISIT: why __GFP_FS is necessary?
		 */
		if (order == HMM_MIN_ORDER) {
			gfp &= ~GFP_NOWAIT;
			gfp |= __GFP_RECLAIM | __GFP_FS;
		}

		pages = alloc_pages(gfp, order);
		if (unlikely(!pages)) {
			/*
			 * in low memory case, if allocation page fails,
			 * we turn to try if order=0 allocation could
			 * succeed. if order=0 fails too, that means there is
			 * no memory left.
			 */
			if (order == HMM_MIN_ORDER) {
				dev_err(atomisp_dev,
					"%s: cannot allocate pages\n",
					 __func__);
				goto cleanup;
			}
			order = HMM_MIN_ORDER;
			failure_number++;
			reduce_order = true;
			/*
			 * if fail two times continuously, we think be short
			 * of memory now.
			 */
			if (failure_number == 2) {
				lack_mem = true;
				failure_number = 0;
			}
			goto retry;
		} else {
			blk_pgnr = order_to_nr(order);

			if (!cached) {
				/*
				 * set memory to uncacheable -- UC_MINUS
				 */
				ret = set_pages_uc(pages, blk_pgnr);
				if (ret) {
					dev_err(atomisp_dev,
						     "set page uncacheable"
							"failed.\n");

					__free_pages(pages, order);

					goto cleanup;
				}
			}

			for (j = 0; j < blk_pgnr; j++) {
				bo->page_obj[i].page = pages + j;
				bo->page_obj[i++].type = HMM_PAGE_TYPE_GENERAL;
			}

			pgnr -= blk_pgnr;
			hmm_mem_stat.sys_size += blk_pgnr;

			/*
			 * if order is not reduced this time, clear
			 * failure_number.
			 */
			if (reduce_order)
				reduce_order = false;
			else
				failure_number = 0;
		}
	}

	return 0;
cleanup:
	alloc_pgnr = i;
	free_private_bo_pages(bo, dypool, repool, alloc_pgnr);

	kfree(bo->page_obj);

	return -ENOMEM;
}

static void free_private_pages(struct hmm_buffer_object *bo,
				struct hmm_pool *dypool,
				struct hmm_pool *repool)
{
	free_private_bo_pages(bo, dypool, repool, bo->pgnr);

	kfree(bo->page_obj);
}

/*
 * Hacked from kernel function __get_user_pages in mm/memory.c
 *
 * Handle buffers allocated by other kernel space driver and mmaped into user
 * space, function Ignore the VM_PFNMAP and VM_IO flag in VMA structure
 *
 * Get physical pages from user space virtual address and update into page list
 */
static int __get_pfnmap_pages(struct task_struct *tsk, struct mm_struct *mm,
			      unsigned long start, int nr_pages,
			      unsigned int gup_flags, struct page **pages,
			      struct vm_area_struct **vmas)
{
	int i, ret;
	unsigned long vm_flags;

	if (nr_pages <= 0)
		return 0;

	VM_BUG_ON(!!pages != !!(gup_flags & FOLL_GET));

	/*
	 * Require read or write permissions.
	 * If FOLL_FORCE is set, we only require the "MAY" flags.
	 */
	vm_flags  = (gup_flags & FOLL_WRITE) ?
			(VM_WRITE | VM_MAYWRITE) : (VM_READ | VM_MAYREAD);
	vm_flags &= (gup_flags & FOLL_FORCE) ?
			(VM_MAYREAD | VM_MAYWRITE) : (VM_READ | VM_WRITE);
	i = 0;

	do {
		struct vm_area_struct *vma;

		vma = find_vma(mm, start);
		if (!vma) {
			dev_err(atomisp_dev, "find_vma failed\n");
			return i ? : -EFAULT;
		}

		if (is_vm_hugetlb_page(vma)) {
			/*
			i = follow_hugetlb_page(mm, vma, pages, vmas,
					&start, &nr_pages, i, gup_flags);
			*/
			continue;
		}

		do {
			struct page *page;
			unsigned long pfn;

			/*
			 * If we have a pending SIGKILL, don't keep faulting
			 * pages and potentially allocating memory.
			 */
			if (unlikely(fatal_signal_pending(current))) {
				dev_err(atomisp_dev,
					"fatal_signal_pending in %s\n",
					__func__);
				return i ? i : -ERESTARTSYS;
			}

			ret = follow_pfn(vma, start, &pfn);
			if (ret) {
				dev_err(atomisp_dev, "follow_pfn() failed\n");
				return i ? : -EFAULT;
			}

			page = pfn_to_page(pfn);
			if (IS_ERR(page))
				return i ? i : PTR_ERR(page);
			if (pages) {
				pages[i] = page;
				get_page(page);
				flush_anon_page(vma, page, start);
				flush_dcache_page(page);
			}
			if (vmas)
				vmas[i] = vma;
			i++;
			start += PAGE_SIZE;
			nr_pages--;
		} while (nr_pages && start < vma->vm_end);
	} while (nr_pages);

	return i;
}

static int get_pfnmap_pages(struct task_struct *tsk, struct mm_struct *mm,
		     unsigned long start, int nr_pages, int write, int force,
		     struct page **pages, struct vm_area_struct **vmas)
{
	int flags = FOLL_TOUCH;

	if (pages)
		flags |= FOLL_GET;
	if (write)
		flags |= FOLL_WRITE;
	if (force)
		flags |= FOLL_FORCE;

	return __get_pfnmap_pages(tsk, mm, start, nr_pages, flags, pages, vmas);
}

/*
 * Convert user space virtual address into pages list
 */
static int alloc_user_pages(struct hmm_buffer_object *bo,
			      void *userptr, bool cached)
{
	int page_nr;
	int i;
	struct vm_area_struct *vma;
	struct page **pages;

	pages = kmalloc_array(bo->pgnr, sizeof(struct page *), GFP_KERNEL);
	if (unlikely(!pages))
		return -ENOMEM;

	bo->page_obj = kmalloc_array(bo->pgnr, sizeof(struct hmm_page_object),
		GFP_KERNEL);
	if (unlikely(!bo->page_obj)) {
		kfree(pages);
		return -ENOMEM;
	}

	mutex_unlock(&bo->mutex);
	down_read(&current->mm->mmap_sem);
	vma = find_vma(current->mm, (unsigned long)userptr);
	up_read(&current->mm->mmap_sem);
	if (vma == NULL) {
		dev_err(atomisp_dev, "find_vma failed\n");
		kfree(bo->page_obj);
		kfree(pages);
		mutex_lock(&bo->mutex);
		return -EFAULT;
	}
	mutex_lock(&bo->mutex);
	/*
	 * Handle frame buffer allocated in other kerenl space driver
	 * and map to user space
	 */
	if (vma->vm_flags & (VM_IO | VM_PFNMAP)) {
		page_nr = get_pfnmap_pages(current, current->mm,
					   (unsigned long)userptr,
					   (int)(bo->pgnr), 1, 0,
					   pages, NULL);
		bo->mem_type = HMM_BO_MEM_TYPE_PFN;
	} else {
		/*Handle frame buffer allocated in user space*/
		mutex_unlock(&bo->mutex);
		down_read(&current->mm->mmap_sem);
		page_nr = get_user_pages((unsigned long)userptr,
					 (int)(bo->pgnr), 1, pages, NULL);
		up_read(&current->mm->mmap_sem);
		mutex_lock(&bo->mutex);
		bo->mem_type = HMM_BO_MEM_TYPE_USER;
	}

	/* can be written by caller, not forced */
	if (page_nr != bo->pgnr) {
		dev_err(atomisp_dev,
				"get_user_pages err: bo->pgnr = %d, "
				"pgnr actually pinned = %d.\n",
				bo->pgnr, page_nr);
		goto out_of_mem;
	}

	for (i = 0; i < bo->pgnr; i++) {
		bo->page_obj[i].page = pages[i];
		bo->page_obj[i].type = HMM_PAGE_TYPE_GENERAL;
	}
	hmm_mem_stat.usr_size += bo->pgnr;
	kfree(pages);

	return 0;

out_of_mem:
	for (i = 0; i < page_nr; i++)
		put_page(pages[i]);
	kfree(pages);
	kfree(bo->page_obj);

	return -ENOMEM;
}

static void free_user_pages(struct hmm_buffer_object *bo)
{
	int i;

	for (i = 0; i < bo->pgnr; i++)
		put_page(bo->page_obj[i].page);
	hmm_mem_stat.usr_size -= bo->pgnr;

	kfree(bo->page_obj);
}

/*
 * allocate/free physical pages for the bo.
 *
 * type indicate where are the pages from. currently we have 3 types
 * of memory: HMM_BO_PRIVATE, HMM_BO_USER, HMM_BO_SHARE.
 *
 * from_highmem is only valid when type is HMM_BO_PRIVATE, it will
 * try to alloc memory from highmem if from_highmem is set.
 *
 * userptr is only valid when type is HMM_BO_USER, it indicates
 * the start address from user space task.
 *
 * from_highmem and userptr will both be ignored when type is
 * HMM_BO_SHARE.
 */
int hmm_bo_alloc_pages(struct hmm_buffer_object *bo,
		       enum hmm_bo_type type, int from_highmem,
		       void *userptr, bool cached)
{
	int ret = -EINVAL;

	check_bo_null_return(bo, -EINVAL);

	mutex_lock(&bo->mutex);
	check_bo_status_no_goto(bo, HMM_BO_PAGE_ALLOCED, status_err);

	/*
	 * TO DO:
	 * add HMM_BO_USER type
	 */
	if (type == HMM_BO_PRIVATE) {
		ret = alloc_private_pages(bo, from_highmem,
				cached, &dynamic_pool, &reserved_pool);
	} else if (type == HMM_BO_USER) {
		ret = alloc_user_pages(bo, userptr, cached);
	} else {
		dev_err(atomisp_dev, "invalid buffer type.\n");
		ret = -EINVAL;
	}
	if (ret)
		goto alloc_err;

	bo->type = type;

	bo->status |= HMM_BO_PAGE_ALLOCED;

	mutex_unlock(&bo->mutex);

	return 0;

alloc_err:
	mutex_unlock(&bo->mutex);
	dev_err(atomisp_dev, "alloc pages err...\n");
	return ret;
status_err:
	mutex_unlock(&bo->mutex);
	dev_err(atomisp_dev,
			"buffer object has already page allocated.\n");
	return -EINVAL;
}

/*
 * free physical pages of the bo.
 */
void hmm_bo_free_pages(struct hmm_buffer_object *bo)
{
	check_bo_null_return_void(bo);

	mutex_lock(&bo->mutex);

	check_bo_status_yes_goto(bo, HMM_BO_PAGE_ALLOCED, status_err2);

	/* clear the flag anyway. */
	bo->status &= (~HMM_BO_PAGE_ALLOCED);

	if (bo->type == HMM_BO_PRIVATE)
		free_private_pages(bo, &dynamic_pool, &reserved_pool);
	else if (bo->type == HMM_BO_USER)
		free_user_pages(bo);
	else
		dev_err(atomisp_dev, "invalid buffer type.\n");
	mutex_unlock(&bo->mutex);

	return;

status_err2:
	mutex_unlock(&bo->mutex);
	dev_err(atomisp_dev,
			"buffer object not page allocated yet.\n");
}

int hmm_bo_page_allocated(struct hmm_buffer_object *bo)
{
	check_bo_null_return(bo, 0);

	return bo->status & HMM_BO_PAGE_ALLOCED;
}

/*
 * get physical page info of the bo.
 */
int hmm_bo_get_page_info(struct hmm_buffer_object *bo,
			 struct hmm_page_object **page_obj, int *pgnr)
{
	check_bo_null_return(bo, -EINVAL);

	mutex_lock(&bo->mutex);

	check_bo_status_yes_goto(bo, HMM_BO_PAGE_ALLOCED, status_err);

	*page_obj = bo->page_obj;
	*pgnr = bo->pgnr;

	mutex_unlock(&bo->mutex);

	return 0;

status_err:
	dev_err(atomisp_dev,
			"buffer object not page allocated yet.\n");
	mutex_unlock(&bo->mutex);
	return -EINVAL;
}

/*
 * bind the physical pages to a virtual address space.
 */
int hmm_bo_bind(struct hmm_buffer_object *bo)
{
	int ret;
	unsigned int virt;
	struct hmm_bo_device *bdev;
	unsigned int i;

	check_bo_null_return(bo, -EINVAL);

	mutex_lock(&bo->mutex);

	check_bo_status_yes_goto(bo,
				   HMM_BO_PAGE_ALLOCED | HMM_BO_ALLOCED,
				   status_err1);

	check_bo_status_no_goto(bo, HMM_BO_BINDED, status_err2);

	bdev = bo->bdev;

	virt = bo->start;

	for (i = 0; i < bo->pgnr; i++) {
		ret =
		    isp_mmu_map(&bdev->mmu, virt,
				page_to_phys(bo->page_obj[i].page), 1);
		if (ret)
			goto map_err;
		virt += (1 << PAGE_SHIFT);
	}

	/*
	 * flush TBL here.
	 *
	 * theoretically, we donot need to flush TLB as we didnot change
	 * any existed address mappings, but for Silicon Hive's MMU, its
	 * really a bug here. I guess when fetching PTEs (page table entity)
	 * to TLB, its MMU will fetch additional INVALID PTEs automatically
	 * for performance issue. EX, we only set up 1 page address mapping,
	 * meaning updating 1 PTE, but the MMU fetches 4 PTE at one time,
	 * so the additional 3 PTEs are invalid.
	 */
	if (bo->start != 0x0)
		isp_mmu_flush_tlb_range(&bdev->mmu, bo->start,
						(bo->pgnr << PAGE_SHIFT));

	bo->status |= HMM_BO_BINDED;

	mutex_unlock(&bo->mutex);

	return 0;

map_err:
	/* unbind the physical pages with related virtual address space */
	virt = bo->start;
	for ( ; i > 0; i--) {
		isp_mmu_unmap(&bdev->mmu, virt, 1);
		virt += pgnr_to_size(1);
	}

	mutex_unlock(&bo->mutex);
	dev_err(atomisp_dev,
			"setup MMU address mapping failed.\n");
	return ret;

status_err2:
	mutex_unlock(&bo->mutex);
	dev_err(atomisp_dev, "buffer object already binded.\n");
	return -EINVAL;
status_err1:
	mutex_unlock(&bo->mutex);
	dev_err(atomisp_dev,
		     "buffer object vm_node or page not allocated.\n");
	return -EINVAL;
}

/*
 * unbind the physical pages with related virtual address space.
 */
void hmm_bo_unbind(struct hmm_buffer_object *bo)
{
	unsigned int virt;
	struct hmm_bo_device *bdev;
	unsigned int i;

	check_bo_null_return_void(bo);

	mutex_lock(&bo->mutex);

	check_bo_status_yes_goto(bo,
				   HMM_BO_PAGE_ALLOCED |
				   HMM_BO_ALLOCED |
				   HMM_BO_BINDED, status_err);

	bdev = bo->bdev;

	virt = bo->start;

	for (i = 0; i < bo->pgnr; i++) {
		isp_mmu_unmap(&bdev->mmu, virt, 1);
		virt += pgnr_to_size(1);
	}

	/*
	 * flush TLB as the address mapping has been removed and
	 * related TLBs should be invalidated.
	 */
	isp_mmu_flush_tlb_range(&bdev->mmu, bo->start,
				(bo->pgnr << PAGE_SHIFT));

	bo->status &= (~HMM_BO_BINDED);

	mutex_unlock(&bo->mutex);

	return;

status_err:
	mutex_unlock(&bo->mutex);
	dev_err(atomisp_dev,
		     "buffer vm or page not allocated or not binded yet.\n");
}

int hmm_bo_binded(struct hmm_buffer_object *bo)
{
	int ret;

	check_bo_null_return(bo, 0);

	mutex_lock(&bo->mutex);

	ret = bo->status & HMM_BO_BINDED;

	mutex_unlock(&bo->mutex);

	return ret;
}

void *hmm_bo_vmap(struct hmm_buffer_object *bo, bool cached)
{
	struct page **pages;
	int i;

	check_bo_null_return(bo, NULL);

	mutex_lock(&bo->mutex);
	if (((bo->status & HMM_BO_VMAPED) && !cached) ||
	    ((bo->status & HMM_BO_VMAPED_CACHED) && cached)) {
		mutex_unlock(&bo->mutex);
		return bo->vmap_addr;
	}

	/* cached status need to be changed, so vunmap first */
	if (bo->status & HMM_BO_VMAPED || bo->status & HMM_BO_VMAPED_CACHED) {
		vunmap(bo->vmap_addr);
		bo->vmap_addr = NULL;
		bo->status &= ~(HMM_BO_VMAPED | HMM_BO_VMAPED_CACHED);
	}

	pages = kmalloc_array(bo->pgnr, sizeof(*pages), GFP_KERNEL);
	if (unlikely(!pages)) {
		mutex_unlock(&bo->mutex);
		return NULL;
	}

	for (i = 0; i < bo->pgnr; i++)
		pages[i] = bo->page_obj[i].page;

	bo->vmap_addr = vmap(pages, bo->pgnr, VM_MAP,
		cached ? PAGE_KERNEL : PAGE_KERNEL_NOCACHE);
	if (unlikely(!bo->vmap_addr)) {
		kfree(pages);
		mutex_unlock(&bo->mutex);
		dev_err(atomisp_dev, "vmap failed...\n");
		return NULL;
	}
	bo->status |= (cached ? HMM_BO_VMAPED_CACHED : HMM_BO_VMAPED);

	kfree(pages);

	mutex_unlock(&bo->mutex);
	return bo->vmap_addr;
}

void hmm_bo_flush_vmap(struct hmm_buffer_object *bo)
{
	check_bo_null_return_void(bo);

	mutex_lock(&bo->mutex);
	if (!(bo->status & HMM_BO_VMAPED_CACHED) || !bo->vmap_addr) {
		mutex_unlock(&bo->mutex);
		return;
	}

	clflush_cache_range(bo->vmap_addr, bo->pgnr * PAGE_SIZE);
	mutex_unlock(&bo->mutex);
}

void hmm_bo_vunmap(struct hmm_buffer_object *bo)
{
	check_bo_null_return_void(bo);

	mutex_lock(&bo->mutex);
	if (bo->status & HMM_BO_VMAPED || bo->status & HMM_BO_VMAPED_CACHED) {
		vunmap(bo->vmap_addr);
		bo->vmap_addr = NULL;
		bo->status &= ~(HMM_BO_VMAPED | HMM_BO_VMAPED_CACHED);
	}

	mutex_unlock(&bo->mutex);
	return;
}

void hmm_bo_ref(struct hmm_buffer_object *bo)
{
	check_bo_null_return_void(bo);

	kref_get(&bo->kref);
}

static void kref_hmm_bo_release(struct kref *kref)
{
	if (!kref)
		return;

	hmm_bo_release(kref_to_hmm_bo(kref));
}

void hmm_bo_unref(struct hmm_buffer_object *bo)
{
	check_bo_null_return_void(bo);

	kref_put(&bo->kref, kref_hmm_bo_release);
}

static void hmm_bo_vm_open(struct vm_area_struct *vma)
{
	struct hmm_buffer_object *bo =
	    (struct hmm_buffer_object *)vma->vm_private_data;

	check_bo_null_return_void(bo);

	hmm_bo_ref(bo);

	mutex_lock(&bo->mutex);

	bo->status |= HMM_BO_MMAPED;

	bo->mmap_count++;

	mutex_unlock(&bo->mutex);
}

static void hmm_bo_vm_close(struct vm_area_struct *vma)
{
	struct hmm_buffer_object *bo =
	    (struct hmm_buffer_object *)vma->vm_private_data;

	check_bo_null_return_void(bo);

	hmm_bo_unref(bo);

	mutex_lock(&bo->mutex);

	bo->mmap_count--;

	if (!bo->mmap_count) {
		bo->status &= (~HMM_BO_MMAPED);
		vma->vm_private_data = NULL;
	}

	mutex_unlock(&bo->mutex);
}

static const struct vm_operations_struct hmm_bo_vm_ops = {
	.open = hmm_bo_vm_open,
	.close = hmm_bo_vm_close,
};

/*
 * mmap the bo to user space.
 */
int hmm_bo_mmap(struct vm_area_struct *vma, struct hmm_buffer_object *bo)
{
	unsigned int start, end;
	unsigned int virt;
	unsigned int pgnr, i;
	unsigned int pfn;

	check_bo_null_return(bo, -EINVAL);

	check_bo_status_yes_goto(bo, HMM_BO_PAGE_ALLOCED, status_err);

	pgnr = bo->pgnr;
	start = vma->vm_start;
	end = vma->vm_end;

	/*
	 * check vma's virtual address space size and buffer object's size.
	 * must be the same.
	 */
	if ((start + pgnr_to_size(pgnr)) != end) {
		dev_warn(atomisp_dev,
			     "vma's address space size not equal"
			     " to buffer object's size");
		return -EINVAL;
	}

	virt = vma->vm_start;
	for (i = 0; i < pgnr; i++) {
		pfn = page_to_pfn(bo->page_obj[i].page);
		if (remap_pfn_range(vma, virt, pfn, PAGE_SIZE, PAGE_SHARED)) {
			dev_warn(atomisp_dev,
					"remap_pfn_range failed:"
					" virt = 0x%x, pfn = 0x%x,"
					" mapped_pgnr = %d\n", virt, pfn, 1);
			return -EINVAL;
		}
		virt += PAGE_SIZE;
	}

	vma->vm_private_data = bo;

	vma->vm_ops = &hmm_bo_vm_ops;
	vma->vm_flags |= VM_IO|VM_DONTEXPAND|VM_DONTDUMP;

	/*
	 * call hmm_bo_vm_open explictly.
	 */
	hmm_bo_vm_open(vma);

	return 0;

status_err:
	dev_err(atomisp_dev, "buffer page not allocated yet.\n");
	return -EINVAL;
}
