/*
 * Copyright (C) 2017 Google, Inc.
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

#ifndef _LINUX_BINDER_ALLOC_H
#define _LINUX_BINDER_ALLOC_H

#include <linux/rbtree.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/rtmutex.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/list_lru.h>
#include <uapi/linux/android/binder.h>

extern struct list_lru binder_alloc_lru;
struct binder_transaction;

/**
 * struct binder_buffer - buffer used for binder transactions
 * @entry:              entry alloc->buffers
 * @rb_node:            node for allocated_buffers/free_buffers rb trees
 * @free:               %true if buffer is free
 * @allow_user_free:    %true if user is allowed to free buffer
 * @async_transaction:  %true if buffer is in use for an async txn
 * @debug_id:           unique ID for debugging
 * @transaction:        pointer to associated struct binder_transaction
 * @target_node:        struct binder_node associated with this buffer
 * @data_size:          size of @transaction data
 * @offsets_size:       size of array of offsets
 * @extra_buffers_size: size of space for other objects (like sg lists)
 * @data:               pointer to base of buffer space
 *
 * Bookkeeping structure for binder transaction buffers
 */
struct binder_buffer {
	struct list_head entry; /* free and allocated entries by address */
	struct rb_node rb_node; /* free entry by size or allocated entry */
				/* by address */
	unsigned free:1;
	unsigned allow_user_free:1;
	unsigned async_transaction:1;
	unsigned debug_id:29;

	struct binder_transaction *transaction;

	struct binder_node *target_node;
	size_t data_size;
	size_t offsets_size;
	size_t extra_buffers_size;
	void *data;
};

/**
 * struct binder_lru_page - page object used for binder shrinker
 * @page_ptr: pointer to physical page in mmap'd space
 * @lru:      entry in binder_alloc_lru
 * @alloc:    binder_alloc for a proc
 */
struct binder_lru_page {
	struct list_head lru;
	struct page *page_ptr;
	struct binder_alloc *alloc;
};

/**
 * struct binder_alloc - per-binder proc state for binder allocator
 * @vma:                vm_area_struct passed to mmap_handler
 *                      (invarient after mmap)
 * @tsk:                tid for task that called init for this proc
 *                      (invariant after init)
 * @vma_vm_mm:          copy of vma->vm_mm (invarient after mmap)
 * @buffer:             base of per-proc address space mapped via mmap
 * @buffers:            list of all buffers for this proc
 * @free_buffers:       rb tree of buffers available for allocation
 *                      sorted by size
 * @allocated_buffers:  rb tree of allocated buffers sorted by address
 * @free_async_space:   VA space available for async buffers. This is
 *                      initialized at mmap time to 1/2 the full VA space
 * @pages:              array of binder_lru_page
 * @buffer_size:        size of address space specified via mmap
 * @pid:                pid for associated binder_proc (invariant after init)
 * @pages_high:         high watermark of offset in @pages
 *
 * Bookkeeping structure for per-proc address space management for binder
 * buffers. It is normally initialized during binder_init() and binder_mmap()
 * calls. The address space is used for both user-visible buffers and for
 * struct binder_buffer objects used to track the user buffers
 */
struct binder_alloc {
	struct mutex mutex;
	struct vm_area_struct *vma;
	struct mm_struct *vma_vm_mm;
	void *buffer;
	struct list_head buffers;
	struct rb_root free_buffers;
	struct rb_root allocated_buffers;
	size_t free_async_space;
	struct binder_lru_page *pages;
	size_t buffer_size;
	uint32_t buffer_free;
	int pid;
	size_t pages_high;
};

#ifdef CONFIG_ANDROID_BINDER_IPC_SELFTEST
void binder_selftest_alloc(struct binder_alloc *alloc);
#else
static inline void binder_selftest_alloc(struct binder_alloc *alloc) {}
#endif
enum lru_status binder_alloc_free_page(struct list_head *item,
				       struct list_lru_one *lru,
				       spinlock_t *lock, void *cb_arg);
extern struct binder_buffer *binder_alloc_new_buf(struct binder_alloc *alloc,
						  size_t data_size,
						  size_t offsets_size,
						  size_t extra_buffers_size,
						  int is_async);
extern void binder_alloc_init(struct binder_alloc *alloc);
extern int binder_alloc_shrinker_init(void);
extern void binder_alloc_vma_close(struct binder_alloc *alloc);
extern struct binder_buffer *
binder_alloc_prepare_to_free(struct binder_alloc *alloc,
			     uintptr_t user_ptr);
extern void binder_alloc_free_buf(struct binder_alloc *alloc,
				  struct binder_buffer *buffer);
extern int binder_alloc_mmap_handler(struct binder_alloc *alloc,
				     struct vm_area_struct *vma);
extern void binder_alloc_deferred_release(struct binder_alloc *alloc);
extern int binder_alloc_get_allocated_count(struct binder_alloc *alloc);
extern void binder_alloc_print_allocated(struct seq_file *m,
					 struct binder_alloc *alloc);
void binder_alloc_print_pages(struct seq_file *m,
			      struct binder_alloc *alloc);

/**
 * binder_alloc_get_free_async_space() - get free space available for async
 * @alloc:	binder_alloc for this proc
 *
 * Return:	the bytes remaining in the address-space for async transactions
 */
static inline size_t
binder_alloc_get_free_async_space(struct binder_alloc *alloc)
{
	size_t free_async_space;

	mutex_lock(&alloc->mutex);
	free_async_space = alloc->free_async_space;
	mutex_unlock(&alloc->mutex);
	return free_async_space;
}

unsigned long
binder_alloc_copy_user_to_buffer(struct binder_alloc *alloc,
				 struct binder_buffer *buffer,
				 binder_size_t buffer_offset,
				 const void __user *from,
				 size_t bytes);

void binder_alloc_copy_to_buffer(struct binder_alloc *alloc,
				 struct binder_buffer *buffer,
				 binder_size_t buffer_offset,
				 void *src,
				 size_t bytes);

void binder_alloc_copy_from_buffer(struct binder_alloc *alloc,
				   void *dest,
				   struct binder_buffer *buffer,
				   binder_size_t buffer_offset,
				   size_t bytes);

#endif /* _LINUX_BINDER_ALLOC_H */

