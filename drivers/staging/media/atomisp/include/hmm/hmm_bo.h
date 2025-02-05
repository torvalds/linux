/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 */

#ifndef	__HMM_BO_H__
#define	__HMM_BO_H__

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include "mmu/isp_mmu.h"
#include "hmm/hmm_common.h"
#include "ia_css_types.h"

#define	check_bodev_null_return(bdev, exp)	\
		check_null_return(bdev, exp, \
			"NULL hmm_bo_device.\n")

#define	check_bodev_null_return_void(bdev)	\
		check_null_return_void(bdev, \
			"NULL hmm_bo_device.\n")

#define	check_bo_status_yes_goto(bo, _status, label) \
	var_not_equal_goto((bo->status & (_status)), (_status), \
			label, \
			"HMM buffer status not contain %s.\n", \
			#_status)

#define	check_bo_status_no_goto(bo, _status, label) \
	var_equal_goto((bo->status & (_status)), (_status), \
			label, \
			"HMM buffer status contains %s.\n", \
			#_status)

#define rbtree_node_to_hmm_bo(root_node)	\
	container_of((root_node), struct hmm_buffer_object, node)

#define	list_to_hmm_bo(list_ptr)	\
	list_entry((list_ptr), struct hmm_buffer_object, list)

#define	kref_to_hmm_bo(kref_ptr)	\
	list_entry((kref_ptr), struct hmm_buffer_object, kref)

#define	check_bo_null_return(bo, exp)	\
	check_null_return(bo, exp, "NULL hmm buffer object.\n")

#define	check_bo_null_return_void(bo)	\
	check_null_return_void(bo, "NULL hmm buffer object.\n")

#define	ISP_VM_START	0x0
#define	ISP_VM_SIZE	(0x7FFFFFFF)	/* 2G address space */
#define	ISP_PTR_NULL	NULL

#define	HMM_BO_DEVICE_INITED	0x1

enum hmm_bo_type {
	HMM_BO_PRIVATE,
	HMM_BO_VMALLOC,
	HMM_BO_LAST,
};

#define	HMM_BO_MASK		0x1
#define	HMM_BO_FREE		0x0
#define	HMM_BO_ALLOCED	0x1
#define	HMM_BO_PAGE_ALLOCED	0x2
#define	HMM_BO_BINDED		0x4
#define	HMM_BO_MMAPED		0x8
#define	HMM_BO_VMAPED		0x10
#define	HMM_BO_VMAPED_CACHED	0x20
#define	HMM_BO_ACTIVE		0x1000

struct hmm_bo_device {
	struct isp_mmu		mmu;

	/* start/pgnr/size is used to record the virtual memory of this bo */
	unsigned int start;
	unsigned int pgnr;
	unsigned int size;

	/* list lock is used to protect the entire_bo_list */
	spinlock_t	list_lock;
	int flag;

	/* linked list for entire buffer object */
	struct list_head entire_bo_list;
	/* rbtree for maintain entire allocated vm */
	struct rb_root allocated_rbtree;
	/* rbtree for maintain entire free vm */
	struct rb_root free_rbtree;
	struct mutex rbtree_mutex;
	struct kmem_cache *bo_cache;
};

struct hmm_buffer_object {
	struct hmm_bo_device	*bdev;
	struct list_head	list;
	struct kref	kref;

	struct page **pages;

	/* mutex protecting this BO */
	struct mutex		mutex;
	enum hmm_bo_type	type;
	int		mmap_count;
	int		status;
	void		*vmap_addr; /* kernel virtual address by vmap */

	struct rb_node	node;
	unsigned int	start;
	unsigned int	end;
	unsigned int	pgnr;
	/*
	 * When insert a bo which has the same pgnr with an existed
	 * bo node in the free_rbtree, using "prev & next" pointer
	 * to maintain a bo linked list instead of insert this bo
	 * into free_rbtree directly, it will make sure each node
	 * in free_rbtree has different pgnr.
	 * "prev & next" default is NULL.
	 */
	struct hmm_buffer_object	*prev;
	struct hmm_buffer_object	*next;
};

struct hmm_buffer_object *hmm_bo_alloc(struct hmm_bo_device *bdev,
				       unsigned int pgnr);

void hmm_bo_release(struct hmm_buffer_object *bo);

int hmm_bo_device_init(struct hmm_bo_device *bdev,
		       struct isp_mmu_client *mmu_driver,
		       unsigned int vaddr_start, unsigned int size);

/*
 * clean up all hmm_bo_device related things.
 */
void hmm_bo_device_exit(struct hmm_bo_device *bdev);

/*
 * whether the bo device is inited or not.
 */
int hmm_bo_device_inited(struct hmm_bo_device *bdev);

/*
 * increase buffer object reference.
 */
void hmm_bo_ref(struct hmm_buffer_object *bo);

/*
 * decrease buffer object reference. if reference reaches 0,
 * release function of the buffer object will be called.
 *
 * this call is also used to release hmm_buffer_object or its
 * upper level object with it embedded in. you need to call
 * this function when it is no longer used.
 *
 * Note:
 *
 * user dont need to care about internal resource release of
 * the buffer object in the release callback, it will be
 * handled internally.
 *
 * this call will only release internal resource of the buffer
 * object but will not free the buffer object itself, as the
 * buffer object can be both pre-allocated statically or
 * dynamically allocated. so user need to deal with the release
 * of the buffer object itself manually. below example shows
 * the normal case of using the buffer object.
 *
 *	struct hmm_buffer_object *bo = hmm_bo_create(bdev, pgnr);
 *	......
 *	hmm_bo_unref(bo);
 *
 * or:
 *
 *	struct hmm_buffer_object bo;
 *
 *	hmm_bo_init(bdev, &bo, pgnr, NULL);
 *	...
 *	hmm_bo_unref(&bo);
 */
void hmm_bo_unref(struct hmm_buffer_object *bo);

int hmm_bo_allocated(struct hmm_buffer_object *bo);

/*
 * Allocate/Free physical pages for the bo. Type indicates if the
 * pages will be allocated by using video driver (for share buffer)
 * or by ISP driver itself.
 */
int hmm_bo_alloc_pages(struct hmm_buffer_object *bo,
		       enum hmm_bo_type type,
		       void *vmalloc_addr);
void hmm_bo_free_pages(struct hmm_buffer_object *bo);
int hmm_bo_page_allocated(struct hmm_buffer_object *bo);

/*
 * bind/unbind the physical pages to a virtual address space.
 */
int hmm_bo_bind(struct hmm_buffer_object *bo);
void hmm_bo_unbind(struct hmm_buffer_object *bo);
int hmm_bo_binded(struct hmm_buffer_object *bo);

/*
 * vmap buffer object's pages to contiguous kernel virtual address.
 * if the buffer has been vmaped, return the virtual address directly.
 */
void *hmm_bo_vmap(struct hmm_buffer_object *bo, bool cached);

/*
 * flush the cache for the vmapped buffer object's pages,
 * if the buffer has not been vmapped, return directly.
 */
void hmm_bo_flush_vmap(struct hmm_buffer_object *bo);

/*
 * vunmap buffer object's kernel virtual address.
 */
void hmm_bo_vunmap(struct hmm_buffer_object *bo);

/*
 * mmap the bo's physical pages to specific vma.
 *
 * vma's address space size must be the same as bo's size,
 * otherwise it will return -EINVAL.
 *
 * vma->vm_flags will be set to (VM_RESERVED | VM_IO).
 */
int hmm_bo_mmap(struct vm_area_struct *vma,
		struct hmm_buffer_object *bo);

/*
 * find the buffer object by its virtual address vaddr.
 * return NULL if no such buffer object found.
 */
struct hmm_buffer_object *hmm_bo_device_search_start(
    struct hmm_bo_device *bdev, ia_css_ptr vaddr);

/*
 * find the buffer object by its virtual address.
 * it does not need to be the start address of one bo,
 * it can be an address within the range of one bo.
 * return NULL if no such buffer object found.
 */
struct hmm_buffer_object *hmm_bo_device_search_in_range(
    struct hmm_bo_device *bdev, ia_css_ptr vaddr);

/*
 * find the buffer object with kernel virtual address vaddr.
 * return NULL if no such buffer object found.
 */
struct hmm_buffer_object *hmm_bo_device_search_vmap_start(
    struct hmm_bo_device *bdev, const void *vaddr);

#endif
