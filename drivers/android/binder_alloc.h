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

struct binder_transaction;

/**
 * struct binder_buffer - buffer used for binder transactions
 * @entry:              entry alloc->buffers
 * @rb_node:            node for allocated_buffers/free_buffers rb trees
 * @free:               true if buffer is free
 * @allow_user_free:    describe the second member of struct blah,
 * @async_transaction:  describe the second member of struct blah,
 * @debug_id:           describe the second member of struct blah,
 * @transaction:        describe the second member of struct blah,
 * @target_node:        describe the second member of struct blah,
 * @data_size:          describe the second member of struct blah,
 * @offsets_size:       describe the second member of struct blah,
 * @extra_buffers_size: describe the second member of struct blah,
 * @data:i              describe the second member of struct blah,
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
	unsigned free_in_progress:1;
	unsigned debug_id:28;

	struct binder_transaction *transaction;

	struct binder_node *target_node;
	size_t data_size;
	size_t offsets_size;
	size_t extra_buffers_size;
	void *data;
};

/**
 * struct binder_alloc - per-binder proc state for binder allocator
 * @vma:                vm_area_struct passed to mmap_handler
 *                      (invarient after mmap)
 * @tsk:                tid for task that called init for this proc
 *                      (invariant after init)
 * @vma_vm_mm:          copy of vma->vm_mm (invarient after mmap)
 * @buffer:             base of per-proc address space mapped via mmap
 * @user_buffer_offset: offset between user and kernel VAs for buffer
 * @buffers:            list of all buffers for this proc
 * @free_buffers:       rb tree of buffers available for allocation
 *                      sorted by size
 * @allocated_buffers:  rb tree of allocated buffers sorted by address
 * @free_async_space:   VA space available for async buffers. This is
 *                      initialized at mmap time to 1/2 the full VA space
 * @pages:              array of physical page addresses for each
 *                      page of mmap'd space
 * @buffer_size:        size of address space specified via mmap
 * @pid:                pid for associated binder_proc (invariant after init)
 *
 * Bookkeeping structure for per-proc address space management for binder
 * buffers. It is normally initialized during binder_init() and binder_mmap()
 * calls. The address space is used for both user-visible buffers and for
 * struct binder_buffer objects used to track the user buffers
 */
struct binder_alloc {
	struct mutex mutex;
	struct task_struct *tsk;
	struct vm_area_struct *vma;
	struct mm_struct *vma_vm_mm;
	void *buffer;
	ptrdiff_t user_buffer_offset;
	struct list_head buffers;
	struct rb_root free_buffers;
	struct rb_root allocated_buffers;
	size_t free_async_space;
	struct page **pages;
	size_t buffer_size;
	uint32_t buffer_free;
	int pid;
};

#ifdef CONFIG_ANDROID_BINDER_IPC_SELFTEST
void binder_selftest_alloc(struct binder_alloc *alloc);
#else
static inline void binder_selftest_alloc(struct binder_alloc *alloc) {}
#endif
extern struct binder_buffer *binder_alloc_new_buf(struct binder_alloc *alloc,
						  size_t data_size,
						  size_t offsets_size,
						  size_t extra_buffers_size,
						  int is_async);
extern void binder_alloc_init(struct binder_alloc *alloc);
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

/**
 * binder_alloc_get_user_buffer_offset() - get offset between kernel/user addrs
 * @alloc:	binder_alloc for this proc
 *
 * Return:	the offset between kernel and user-space addresses to use for
 * virtual address conversion
 */
static inline ptrdiff_t
binder_alloc_get_user_buffer_offset(struct binder_alloc *alloc)
{
	/*
	 * user_buffer_offset is constant if vma is set and
	 * undefined if vma is not set. It is possible to
	 * get here with !alloc->vma if the target process
	 * is dying while a transaction is being initiated.
	 * Returning the old value is ok in this case and
	 * the transaction will fail.
	 */
	return alloc->user_buffer_offset;
}

#endif /* _LINUX_BINDER_ALLOC_H */

