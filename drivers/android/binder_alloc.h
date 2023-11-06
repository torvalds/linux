/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 Google, Inc.
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
 * @clear_on_free:      %true if buffer must be zeroed after use
 * @allow_user_free:    %true if user is allowed to free buffer
 * @async_transaction:  %true if buffer is in use for an async txn
 * @oneway_spam_suspect: %true if total async allocate size just exceed
 * spamming detect threshold
 * @debug_id:           unique ID for debugging
 * @transaction:        pointer to associated struct binder_transaction
 * @target_node:        struct binder_node associated with this buffer
 * @data_size:          size of @transaction data
 * @offsets_size:       size of array of offsets
 * @extra_buffers_size: size of space for other objects (like sg lists)
 * @user_data:          user pointer to base of buffer space
 * @pid:                pid to attribute the buffer to (caller)
 *
 * Bookkeeping structure for binder transaction buffers
 */
struct binder_buffer {
	struct list_head entry; /* free and allocated entries by address */
	struct rb_node rb_node; /* free entry by size or allocated entry */
				/* by address */
	unsigned free:1;
	unsigned clear_on_free:1;
	unsigned allow_user_free:1;
	unsigned async_transaction:1;
	unsigned oneway_spam_suspect:1;
	unsigned debug_id:27;

	struct binder_transaction *transaction;

	struct binder_node *target_node;
	size_t data_size;
	size_t offsets_size;
	size_t extra_buffers_size;
	void __user *user_data;
	int    pid;
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
 * @mutex:              protects binder_alloc fields
 * @vma:                vm_area_struct passed to mmap_handler
 *                      (invariant after mmap)
 * @mm:                 copy of task->mm (invariant after open)
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
 * @oneway_spam_detected: %true if oneway spam detection fired, clear that
 * flag once the async buffer has returned to a healthy state
 *
 * Bookkeeping structure for per-proc address space management for binder
 * buffers. It is normally initialized during binder_init() and binder_mmap()
 * calls. The address space is used for both user-visible buffers and for
 * struct binder_buffer objects used to track the user buffers
 */
struct binder_alloc {
	struct mutex mutex;
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	void __user *buffer;
	struct list_head buffers;
	struct rb_root free_buffers;
	struct rb_root allocated_buffers;
	size_t free_async_space;
	struct binder_lru_page *pages;
	size_t buffer_size;
	int pid;
	size_t pages_high;
	bool oneway_spam_detected;
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
						  int is_async,
						  int pid);
extern void binder_alloc_init(struct binder_alloc *alloc);
extern int binder_alloc_shrinker_init(void);
extern void binder_alloc_shrinker_exit(void);
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

int binder_alloc_copy_to_buffer(struct binder_alloc *alloc,
				struct binder_buffer *buffer,
				binder_size_t buffer_offset,
				void *src,
				size_t bytes);

int binder_alloc_copy_from_buffer(struct binder_alloc *alloc,
				  void *dest,
				  struct binder_buffer *buffer,
				  binder_size_t buffer_offset,
				  size_t bytes);

#endif /* _LINUX_BINDER_ALLOC_H */

