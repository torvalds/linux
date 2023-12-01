// SPDX-License-Identifier: GPL-2.0-only
/* binder_alloc.c
 *
 * Android IPC Subsystem
 *
 * Copyright (C) 2007-2017 Google, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/list.h>
#include <linux/sched/mm.h>
#include <linux/module.h>
#include <linux/rtmutex.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/list_lru.h>
#include <linux/ratelimit.h>
#include <asm/cacheflush.h>
#include <linux/uaccess.h>
#include <linux/highmem.h>
#include <linux/sizes.h>
#include "binder_alloc.h"
#include "binder_trace.h"

struct list_lru binder_alloc_lru;

static DEFINE_MUTEX(binder_alloc_mmap_lock);

enum {
	BINDER_DEBUG_USER_ERROR             = 1U << 0,
	BINDER_DEBUG_OPEN_CLOSE             = 1U << 1,
	BINDER_DEBUG_BUFFER_ALLOC           = 1U << 2,
	BINDER_DEBUG_BUFFER_ALLOC_ASYNC     = 1U << 3,
};
static uint32_t binder_alloc_debug_mask = BINDER_DEBUG_USER_ERROR;

module_param_named(debug_mask, binder_alloc_debug_mask,
		   uint, 0644);

#define binder_alloc_debug(mask, x...) \
	do { \
		if (binder_alloc_debug_mask & mask) \
			pr_info_ratelimited(x); \
	} while (0)

static struct binder_buffer *binder_buffer_next(struct binder_buffer *buffer)
{
	return list_entry(buffer->entry.next, struct binder_buffer, entry);
}

static struct binder_buffer *binder_buffer_prev(struct binder_buffer *buffer)
{
	return list_entry(buffer->entry.prev, struct binder_buffer, entry);
}

static size_t binder_alloc_buffer_size(struct binder_alloc *alloc,
				       struct binder_buffer *buffer)
{
	if (list_is_last(&buffer->entry, &alloc->buffers))
		return alloc->buffer + alloc->buffer_size - buffer->user_data;
	return binder_buffer_next(buffer)->user_data - buffer->user_data;
}

static void binder_insert_free_buffer(struct binder_alloc *alloc,
				      struct binder_buffer *new_buffer)
{
	struct rb_node **p = &alloc->free_buffers.rb_node;
	struct rb_node *parent = NULL;
	struct binder_buffer *buffer;
	size_t buffer_size;
	size_t new_buffer_size;

	BUG_ON(!new_buffer->free);

	new_buffer_size = binder_alloc_buffer_size(alloc, new_buffer);

	binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
		     "%d: add free buffer, size %zd, at %pK\n",
		      alloc->pid, new_buffer_size, new_buffer);

	while (*p) {
		parent = *p;
		buffer = rb_entry(parent, struct binder_buffer, rb_node);
		BUG_ON(!buffer->free);

		buffer_size = binder_alloc_buffer_size(alloc, buffer);

		if (new_buffer_size < buffer_size)
			p = &parent->rb_left;
		else
			p = &parent->rb_right;
	}
	rb_link_node(&new_buffer->rb_node, parent, p);
	rb_insert_color(&new_buffer->rb_node, &alloc->free_buffers);
}

static void binder_insert_allocated_buffer_locked(
		struct binder_alloc *alloc, struct binder_buffer *new_buffer)
{
	struct rb_node **p = &alloc->allocated_buffers.rb_node;
	struct rb_node *parent = NULL;
	struct binder_buffer *buffer;

	BUG_ON(new_buffer->free);

	while (*p) {
		parent = *p;
		buffer = rb_entry(parent, struct binder_buffer, rb_node);
		BUG_ON(buffer->free);

		if (new_buffer->user_data < buffer->user_data)
			p = &parent->rb_left;
		else if (new_buffer->user_data > buffer->user_data)
			p = &parent->rb_right;
		else
			BUG();
	}
	rb_link_node(&new_buffer->rb_node, parent, p);
	rb_insert_color(&new_buffer->rb_node, &alloc->allocated_buffers);
}

static struct binder_buffer *binder_alloc_prepare_to_free_locked(
		struct binder_alloc *alloc,
		uintptr_t user_ptr)
{
	struct rb_node *n = alloc->allocated_buffers.rb_node;
	struct binder_buffer *buffer;
	void __user *uptr;

	uptr = (void __user *)user_ptr;

	while (n) {
		buffer = rb_entry(n, struct binder_buffer, rb_node);
		BUG_ON(buffer->free);

		if (uptr < buffer->user_data)
			n = n->rb_left;
		else if (uptr > buffer->user_data)
			n = n->rb_right;
		else {
			/*
			 * Guard against user threads attempting to
			 * free the buffer when in use by kernel or
			 * after it's already been freed.
			 */
			if (!buffer->allow_user_free)
				return ERR_PTR(-EPERM);
			buffer->allow_user_free = 0;
			return buffer;
		}
	}
	return NULL;
}

/**
 * binder_alloc_prepare_to_free() - get buffer given user ptr
 * @alloc:	binder_alloc for this proc
 * @user_ptr:	User pointer to buffer data
 *
 * Validate userspace pointer to buffer data and return buffer corresponding to
 * that user pointer. Search the rb tree for buffer that matches user data
 * pointer.
 *
 * Return:	Pointer to buffer or NULL
 */
struct binder_buffer *binder_alloc_prepare_to_free(struct binder_alloc *alloc,
						   uintptr_t user_ptr)
{
	struct binder_buffer *buffer;

	mutex_lock(&alloc->mutex);
	buffer = binder_alloc_prepare_to_free_locked(alloc, user_ptr);
	mutex_unlock(&alloc->mutex);
	return buffer;
}

static int binder_update_page_range(struct binder_alloc *alloc, int allocate,
				    void __user *start, void __user *end)
{
	void __user *page_addr;
	unsigned long user_page_addr;
	struct binder_lru_page *page;
	struct vm_area_struct *vma = NULL;
	struct mm_struct *mm = NULL;
	bool need_mm = false;

	binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
		     "%d: %s pages %pK-%pK\n", alloc->pid,
		     allocate ? "allocate" : "free", start, end);

	if (end <= start)
		return 0;

	trace_binder_update_page_range(alloc, allocate, start, end);

	if (allocate == 0)
		goto free_range;

	for (page_addr = start; page_addr < end; page_addr += PAGE_SIZE) {
		page = &alloc->pages[(page_addr - alloc->buffer) / PAGE_SIZE];
		if (!page->page_ptr) {
			need_mm = true;
			break;
		}
	}

	if (need_mm && mmget_not_zero(alloc->mm))
		mm = alloc->mm;

	if (mm) {
		mmap_write_lock(mm);
		vma = alloc->vma;
	}

	if (!vma && need_mm) {
		binder_alloc_debug(BINDER_DEBUG_USER_ERROR,
				   "%d: binder_alloc_buf failed to map pages in userspace, no vma\n",
				   alloc->pid);
		goto err_no_vma;
	}

	for (page_addr = start; page_addr < end; page_addr += PAGE_SIZE) {
		int ret;
		bool on_lru;
		size_t index;

		index = (page_addr - alloc->buffer) / PAGE_SIZE;
		page = &alloc->pages[index];

		if (page->page_ptr) {
			trace_binder_alloc_lru_start(alloc, index);

			on_lru = list_lru_del(&binder_alloc_lru, &page->lru);
			WARN_ON(!on_lru);

			trace_binder_alloc_lru_end(alloc, index);
			continue;
		}

		if (WARN_ON(!vma))
			goto err_page_ptr_cleared;

		trace_binder_alloc_page_start(alloc, index);
		page->page_ptr = alloc_page(GFP_KERNEL |
					    __GFP_HIGHMEM |
					    __GFP_ZERO);
		if (!page->page_ptr) {
			pr_err("%d: binder_alloc_buf failed for page at %pK\n",
				alloc->pid, page_addr);
			goto err_alloc_page_failed;
		}
		page->alloc = alloc;
		INIT_LIST_HEAD(&page->lru);

		user_page_addr = (uintptr_t)page_addr;
		ret = vm_insert_page(vma, user_page_addr, page[0].page_ptr);
		if (ret) {
			pr_err("%d: binder_alloc_buf failed to map page at %lx in userspace\n",
			       alloc->pid, user_page_addr);
			goto err_vm_insert_page_failed;
		}

		if (index + 1 > alloc->pages_high)
			alloc->pages_high = index + 1;

		trace_binder_alloc_page_end(alloc, index);
	}
	if (mm) {
		mmap_write_unlock(mm);
		mmput_async(mm);
	}
	return 0;

free_range:
	for (page_addr = end - PAGE_SIZE; 1; page_addr -= PAGE_SIZE) {
		bool ret;
		size_t index;

		index = (page_addr - alloc->buffer) / PAGE_SIZE;
		page = &alloc->pages[index];

		trace_binder_free_lru_start(alloc, index);

		ret = list_lru_add(&binder_alloc_lru, &page->lru);
		WARN_ON(!ret);

		trace_binder_free_lru_end(alloc, index);
		if (page_addr == start)
			break;
		continue;

err_vm_insert_page_failed:
		__free_page(page->page_ptr);
		page->page_ptr = NULL;
err_alloc_page_failed:
err_page_ptr_cleared:
		if (page_addr == start)
			break;
	}
err_no_vma:
	if (mm) {
		mmap_write_unlock(mm);
		mmput_async(mm);
	}
	return vma ? -ENOMEM : -ESRCH;
}

static inline void binder_alloc_set_vma(struct binder_alloc *alloc,
		struct vm_area_struct *vma)
{
	/* pairs with smp_load_acquire in binder_alloc_get_vma() */
	smp_store_release(&alloc->vma, vma);
}

static inline struct vm_area_struct *binder_alloc_get_vma(
		struct binder_alloc *alloc)
{
	/* pairs with smp_store_release in binder_alloc_set_vma() */
	return smp_load_acquire(&alloc->vma);
}

static bool debug_low_async_space_locked(struct binder_alloc *alloc, int pid)
{
	/*
	 * Find the amount and size of buffers allocated by the current caller;
	 * The idea is that once we cross the threshold, whoever is responsible
	 * for the low async space is likely to try to send another async txn,
	 * and at some point we'll catch them in the act. This is more efficient
	 * than keeping a map per pid.
	 */
	struct rb_node *n;
	struct binder_buffer *buffer;
	size_t total_alloc_size = 0;
	size_t num_buffers = 0;

	for (n = rb_first(&alloc->allocated_buffers); n != NULL;
		 n = rb_next(n)) {
		buffer = rb_entry(n, struct binder_buffer, rb_node);
		if (buffer->pid != pid)
			continue;
		if (!buffer->async_transaction)
			continue;
		total_alloc_size += binder_alloc_buffer_size(alloc, buffer);
		num_buffers++;
	}

	/*
	 * Warn if this pid has more than 50 transactions, or more than 50% of
	 * async space (which is 25% of total buffer size). Oneway spam is only
	 * detected when the threshold is exceeded.
	 */
	if (num_buffers > 50 || total_alloc_size > alloc->buffer_size / 4) {
		binder_alloc_debug(BINDER_DEBUG_USER_ERROR,
			     "%d: pid %d spamming oneway? %zd buffers allocated for a total size of %zd\n",
			      alloc->pid, pid, num_buffers, total_alloc_size);
		if (!alloc->oneway_spam_detected) {
			alloc->oneway_spam_detected = true;
			return true;
		}
	}
	return false;
}

static struct binder_buffer *binder_alloc_new_buf_locked(
				struct binder_alloc *alloc,
				size_t data_size,
				size_t offsets_size,
				size_t extra_buffers_size,
				int is_async,
				int pid)
{
	struct rb_node *n = alloc->free_buffers.rb_node;
	struct binder_buffer *buffer;
	size_t buffer_size;
	struct rb_node *best_fit = NULL;
	void __user *has_page_addr;
	void __user *end_page_addr;
	size_t size, data_offsets_size;
	int ret;

	/* Check binder_alloc is fully initialized */
	if (!binder_alloc_get_vma(alloc)) {
		binder_alloc_debug(BINDER_DEBUG_USER_ERROR,
				   "%d: binder_alloc_buf, no vma\n",
				   alloc->pid);
		return ERR_PTR(-ESRCH);
	}

	data_offsets_size = ALIGN(data_size, sizeof(void *)) +
		ALIGN(offsets_size, sizeof(void *));

	if (data_offsets_size < data_size || data_offsets_size < offsets_size) {
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
				"%d: got transaction with invalid size %zd-%zd\n",
				alloc->pid, data_size, offsets_size);
		return ERR_PTR(-EINVAL);
	}
	size = data_offsets_size + ALIGN(extra_buffers_size, sizeof(void *));
	if (size < data_offsets_size || size < extra_buffers_size) {
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
				"%d: got transaction with invalid extra_buffers_size %zd\n",
				alloc->pid, extra_buffers_size);
		return ERR_PTR(-EINVAL);
	}

	/* Pad 0-size buffers so they get assigned unique addresses */
	size = max(size, sizeof(void *));

	if (is_async && alloc->free_async_space < size) {
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
			     "%d: binder_alloc_buf size %zd failed, no async space left\n",
			      alloc->pid, size);
		return ERR_PTR(-ENOSPC);
	}

	while (n) {
		buffer = rb_entry(n, struct binder_buffer, rb_node);
		BUG_ON(!buffer->free);
		buffer_size = binder_alloc_buffer_size(alloc, buffer);

		if (size < buffer_size) {
			best_fit = n;
			n = n->rb_left;
		} else if (size > buffer_size)
			n = n->rb_right;
		else {
			best_fit = n;
			break;
		}
	}
	if (best_fit == NULL) {
		size_t allocated_buffers = 0;
		size_t largest_alloc_size = 0;
		size_t total_alloc_size = 0;
		size_t free_buffers = 0;
		size_t largest_free_size = 0;
		size_t total_free_size = 0;

		for (n = rb_first(&alloc->allocated_buffers); n != NULL;
		     n = rb_next(n)) {
			buffer = rb_entry(n, struct binder_buffer, rb_node);
			buffer_size = binder_alloc_buffer_size(alloc, buffer);
			allocated_buffers++;
			total_alloc_size += buffer_size;
			if (buffer_size > largest_alloc_size)
				largest_alloc_size = buffer_size;
		}
		for (n = rb_first(&alloc->free_buffers); n != NULL;
		     n = rb_next(n)) {
			buffer = rb_entry(n, struct binder_buffer, rb_node);
			buffer_size = binder_alloc_buffer_size(alloc, buffer);
			free_buffers++;
			total_free_size += buffer_size;
			if (buffer_size > largest_free_size)
				largest_free_size = buffer_size;
		}
		binder_alloc_debug(BINDER_DEBUG_USER_ERROR,
				   "%d: binder_alloc_buf size %zd failed, no address space\n",
				   alloc->pid, size);
		binder_alloc_debug(BINDER_DEBUG_USER_ERROR,
				   "allocated: %zd (num: %zd largest: %zd), free: %zd (num: %zd largest: %zd)\n",
				   total_alloc_size, allocated_buffers,
				   largest_alloc_size, total_free_size,
				   free_buffers, largest_free_size);
		return ERR_PTR(-ENOSPC);
	}
	if (n == NULL) {
		buffer = rb_entry(best_fit, struct binder_buffer, rb_node);
		buffer_size = binder_alloc_buffer_size(alloc, buffer);
	}

	binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
		     "%d: binder_alloc_buf size %zd got buffer %pK size %zd\n",
		      alloc->pid, size, buffer, buffer_size);

	has_page_addr = (void __user *)
		(((uintptr_t)buffer->user_data + buffer_size) & PAGE_MASK);
	WARN_ON(n && buffer_size != size);
	end_page_addr =
		(void __user *)PAGE_ALIGN((uintptr_t)buffer->user_data + size);
	if (end_page_addr > has_page_addr)
		end_page_addr = has_page_addr;
	ret = binder_update_page_range(alloc, 1, (void __user *)
		PAGE_ALIGN((uintptr_t)buffer->user_data), end_page_addr);
	if (ret)
		return ERR_PTR(ret);

	if (buffer_size != size) {
		struct binder_buffer *new_buffer;

		new_buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
		if (!new_buffer) {
			pr_err("%s: %d failed to alloc new buffer struct\n",
			       __func__, alloc->pid);
			goto err_alloc_buf_struct_failed;
		}
		new_buffer->user_data = (u8 __user *)buffer->user_data + size;
		list_add(&new_buffer->entry, &buffer->entry);
		new_buffer->free = 1;
		binder_insert_free_buffer(alloc, new_buffer);
	}

	rb_erase(best_fit, &alloc->free_buffers);
	buffer->free = 0;
	buffer->allow_user_free = 0;
	binder_insert_allocated_buffer_locked(alloc, buffer);
	binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
		     "%d: binder_alloc_buf size %zd got %pK\n",
		      alloc->pid, size, buffer);
	buffer->data_size = data_size;
	buffer->offsets_size = offsets_size;
	buffer->async_transaction = is_async;
	buffer->extra_buffers_size = extra_buffers_size;
	buffer->pid = pid;
	buffer->oneway_spam_suspect = false;
	if (is_async) {
		alloc->free_async_space -= size;
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC_ASYNC,
			     "%d: binder_alloc_buf size %zd async free %zd\n",
			      alloc->pid, size, alloc->free_async_space);
		if (alloc->free_async_space < alloc->buffer_size / 10) {
			/*
			 * Start detecting spammers once we have less than 20%
			 * of async space left (which is less than 10% of total
			 * buffer size).
			 */
			buffer->oneway_spam_suspect = debug_low_async_space_locked(alloc, pid);
		} else {
			alloc->oneway_spam_detected = false;
		}
	}
	return buffer;

err_alloc_buf_struct_failed:
	binder_update_page_range(alloc, 0, (void __user *)
				 PAGE_ALIGN((uintptr_t)buffer->user_data),
				 end_page_addr);
	return ERR_PTR(-ENOMEM);
}

/**
 * binder_alloc_new_buf() - Allocate a new binder buffer
 * @alloc:              binder_alloc for this proc
 * @data_size:          size of user data buffer
 * @offsets_size:       user specified buffer offset
 * @extra_buffers_size: size of extra space for meta-data (eg, security context)
 * @is_async:           buffer for async transaction
 * @pid:				pid to attribute allocation to (used for debugging)
 *
 * Allocate a new buffer given the requested sizes. Returns
 * the kernel version of the buffer pointer. The size allocated
 * is the sum of the three given sizes (each rounded up to
 * pointer-sized boundary)
 *
 * Return:	The allocated buffer or %ERR_PTR(-errno) if error
 */
struct binder_buffer *binder_alloc_new_buf(struct binder_alloc *alloc,
					   size_t data_size,
					   size_t offsets_size,
					   size_t extra_buffers_size,
					   int is_async,
					   int pid)
{
	struct binder_buffer *buffer;

	mutex_lock(&alloc->mutex);
	buffer = binder_alloc_new_buf_locked(alloc, data_size, offsets_size,
					     extra_buffers_size, is_async, pid);
	mutex_unlock(&alloc->mutex);
	return buffer;
}

static void __user *buffer_start_page(struct binder_buffer *buffer)
{
	return (void __user *)((uintptr_t)buffer->user_data & PAGE_MASK);
}

static void __user *prev_buffer_end_page(struct binder_buffer *buffer)
{
	return (void __user *)
		(((uintptr_t)(buffer->user_data) - 1) & PAGE_MASK);
}

static void binder_delete_free_buffer(struct binder_alloc *alloc,
				      struct binder_buffer *buffer)
{
	struct binder_buffer *prev, *next = NULL;
	bool to_free = true;

	BUG_ON(alloc->buffers.next == &buffer->entry);
	prev = binder_buffer_prev(buffer);
	BUG_ON(!prev->free);
	if (prev_buffer_end_page(prev) == buffer_start_page(buffer)) {
		to_free = false;
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
				   "%d: merge free, buffer %pK share page with %pK\n",
				   alloc->pid, buffer->user_data,
				   prev->user_data);
	}

	if (!list_is_last(&buffer->entry, &alloc->buffers)) {
		next = binder_buffer_next(buffer);
		if (buffer_start_page(next) == buffer_start_page(buffer)) {
			to_free = false;
			binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
					   "%d: merge free, buffer %pK share page with %pK\n",
					   alloc->pid,
					   buffer->user_data,
					   next->user_data);
		}
	}

	if (PAGE_ALIGNED(buffer->user_data)) {
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
				   "%d: merge free, buffer start %pK is page aligned\n",
				   alloc->pid, buffer->user_data);
		to_free = false;
	}

	if (to_free) {
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
				   "%d: merge free, buffer %pK do not share page with %pK or %pK\n",
				   alloc->pid, buffer->user_data,
				   prev->user_data,
				   next ? next->user_data : NULL);
		binder_update_page_range(alloc, 0, buffer_start_page(buffer),
					 buffer_start_page(buffer) + PAGE_SIZE);
	}
	list_del(&buffer->entry);
	kfree(buffer);
}

static void binder_free_buf_locked(struct binder_alloc *alloc,
				   struct binder_buffer *buffer)
{
	size_t size, buffer_size;

	buffer_size = binder_alloc_buffer_size(alloc, buffer);

	size = ALIGN(buffer->data_size, sizeof(void *)) +
		ALIGN(buffer->offsets_size, sizeof(void *)) +
		ALIGN(buffer->extra_buffers_size, sizeof(void *));

	binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
		     "%d: binder_free_buf %pK size %zd buffer_size %zd\n",
		      alloc->pid, buffer, size, buffer_size);

	BUG_ON(buffer->free);
	BUG_ON(size > buffer_size);
	BUG_ON(buffer->transaction != NULL);
	BUG_ON(buffer->user_data < alloc->buffer);
	BUG_ON(buffer->user_data > alloc->buffer + alloc->buffer_size);

	if (buffer->async_transaction) {
		alloc->free_async_space += buffer_size;
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC_ASYNC,
			     "%d: binder_free_buf size %zd async free %zd\n",
			      alloc->pid, size, alloc->free_async_space);
	}

	binder_update_page_range(alloc, 0,
		(void __user *)PAGE_ALIGN((uintptr_t)buffer->user_data),
		(void __user *)(((uintptr_t)
			  buffer->user_data + buffer_size) & PAGE_MASK));

	rb_erase(&buffer->rb_node, &alloc->allocated_buffers);
	buffer->free = 1;
	if (!list_is_last(&buffer->entry, &alloc->buffers)) {
		struct binder_buffer *next = binder_buffer_next(buffer);

		if (next->free) {
			rb_erase(&next->rb_node, &alloc->free_buffers);
			binder_delete_free_buffer(alloc, next);
		}
	}
	if (alloc->buffers.next != &buffer->entry) {
		struct binder_buffer *prev = binder_buffer_prev(buffer);

		if (prev->free) {
			binder_delete_free_buffer(alloc, buffer);
			rb_erase(&prev->rb_node, &alloc->free_buffers);
			buffer = prev;
		}
	}
	binder_insert_free_buffer(alloc, buffer);
}

static void binder_alloc_clear_buf(struct binder_alloc *alloc,
				   struct binder_buffer *buffer);
/**
 * binder_alloc_free_buf() - free a binder buffer
 * @alloc:	binder_alloc for this proc
 * @buffer:	kernel pointer to buffer
 *
 * Free the buffer allocated via binder_alloc_new_buf()
 */
void binder_alloc_free_buf(struct binder_alloc *alloc,
			    struct binder_buffer *buffer)
{
	/*
	 * We could eliminate the call to binder_alloc_clear_buf()
	 * from binder_alloc_deferred_release() by moving this to
	 * binder_free_buf_locked(). However, that could
	 * increase contention for the alloc mutex if clear_on_free
	 * is used frequently for large buffers. The mutex is not
	 * needed for correctness here.
	 */
	if (buffer->clear_on_free) {
		binder_alloc_clear_buf(alloc, buffer);
		buffer->clear_on_free = false;
	}
	mutex_lock(&alloc->mutex);
	binder_free_buf_locked(alloc, buffer);
	mutex_unlock(&alloc->mutex);
}

/**
 * binder_alloc_mmap_handler() - map virtual address space for proc
 * @alloc:	alloc structure for this proc
 * @vma:	vma passed to mmap()
 *
 * Called by binder_mmap() to initialize the space specified in
 * vma for allocating binder buffers
 *
 * Return:
 *      0 = success
 *      -EBUSY = address space already mapped
 *      -ENOMEM = failed to map memory to given address space
 */
int binder_alloc_mmap_handler(struct binder_alloc *alloc,
			      struct vm_area_struct *vma)
{
	int ret;
	const char *failure_string;
	struct binder_buffer *buffer;

	if (unlikely(vma->vm_mm != alloc->mm)) {
		ret = -EINVAL;
		failure_string = "invalid vma->vm_mm";
		goto err_invalid_mm;
	}

	mutex_lock(&binder_alloc_mmap_lock);
	if (alloc->buffer_size) {
		ret = -EBUSY;
		failure_string = "already mapped";
		goto err_already_mapped;
	}
	alloc->buffer_size = min_t(unsigned long, vma->vm_end - vma->vm_start,
				   SZ_4M);
	mutex_unlock(&binder_alloc_mmap_lock);

	alloc->buffer = (void __user *)vma->vm_start;

	alloc->pages = kcalloc(alloc->buffer_size / PAGE_SIZE,
			       sizeof(alloc->pages[0]),
			       GFP_KERNEL);
	if (alloc->pages == NULL) {
		ret = -ENOMEM;
		failure_string = "alloc page array";
		goto err_alloc_pages_failed;
	}

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		failure_string = "alloc buffer struct";
		goto err_alloc_buf_struct_failed;
	}

	buffer->user_data = alloc->buffer;
	list_add(&buffer->entry, &alloc->buffers);
	buffer->free = 1;
	binder_insert_free_buffer(alloc, buffer);
	alloc->free_async_space = alloc->buffer_size / 2;

	/* Signal binder_alloc is fully initialized */
	binder_alloc_set_vma(alloc, vma);

	return 0;

err_alloc_buf_struct_failed:
	kfree(alloc->pages);
	alloc->pages = NULL;
err_alloc_pages_failed:
	alloc->buffer = NULL;
	mutex_lock(&binder_alloc_mmap_lock);
	alloc->buffer_size = 0;
err_already_mapped:
	mutex_unlock(&binder_alloc_mmap_lock);
err_invalid_mm:
	binder_alloc_debug(BINDER_DEBUG_USER_ERROR,
			   "%s: %d %lx-%lx %s failed %d\n", __func__,
			   alloc->pid, vma->vm_start, vma->vm_end,
			   failure_string, ret);
	return ret;
}


void binder_alloc_deferred_release(struct binder_alloc *alloc)
{
	struct rb_node *n;
	int buffers, page_count;
	struct binder_buffer *buffer;

	buffers = 0;
	mutex_lock(&alloc->mutex);
	BUG_ON(alloc->vma);

	while ((n = rb_first(&alloc->allocated_buffers))) {
		buffer = rb_entry(n, struct binder_buffer, rb_node);

		/* Transaction should already have been freed */
		BUG_ON(buffer->transaction);

		if (buffer->clear_on_free) {
			binder_alloc_clear_buf(alloc, buffer);
			buffer->clear_on_free = false;
		}
		binder_free_buf_locked(alloc, buffer);
		buffers++;
	}

	while (!list_empty(&alloc->buffers)) {
		buffer = list_first_entry(&alloc->buffers,
					  struct binder_buffer, entry);
		WARN_ON(!buffer->free);

		list_del(&buffer->entry);
		WARN_ON_ONCE(!list_empty(&alloc->buffers));
		kfree(buffer);
	}

	page_count = 0;
	if (alloc->pages) {
		int i;

		for (i = 0; i < alloc->buffer_size / PAGE_SIZE; i++) {
			void __user *page_addr;
			bool on_lru;

			if (!alloc->pages[i].page_ptr)
				continue;

			on_lru = list_lru_del(&binder_alloc_lru,
					      &alloc->pages[i].lru);
			page_addr = alloc->buffer + i * PAGE_SIZE;
			binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
				     "%s: %d: page %d at %pK %s\n",
				     __func__, alloc->pid, i, page_addr,
				     on_lru ? "on lru" : "active");
			__free_page(alloc->pages[i].page_ptr);
			page_count++;
		}
		kfree(alloc->pages);
	}
	mutex_unlock(&alloc->mutex);
	if (alloc->mm)
		mmdrop(alloc->mm);

	binder_alloc_debug(BINDER_DEBUG_OPEN_CLOSE,
		     "%s: %d buffers %d, pages %d\n",
		     __func__, alloc->pid, buffers, page_count);
}

static void print_binder_buffer(struct seq_file *m, const char *prefix,
				struct binder_buffer *buffer)
{
	seq_printf(m, "%s %d: %pK size %zd:%zd:%zd %s\n",
		   prefix, buffer->debug_id, buffer->user_data,
		   buffer->data_size, buffer->offsets_size,
		   buffer->extra_buffers_size,
		   buffer->transaction ? "active" : "delivered");
}

/**
 * binder_alloc_print_allocated() - print buffer info
 * @m:     seq_file for output via seq_printf()
 * @alloc: binder_alloc for this proc
 *
 * Prints information about every buffer associated with
 * the binder_alloc state to the given seq_file
 */
void binder_alloc_print_allocated(struct seq_file *m,
				  struct binder_alloc *alloc)
{
	struct rb_node *n;

	mutex_lock(&alloc->mutex);
	for (n = rb_first(&alloc->allocated_buffers); n != NULL; n = rb_next(n))
		print_binder_buffer(m, "  buffer",
				    rb_entry(n, struct binder_buffer, rb_node));
	mutex_unlock(&alloc->mutex);
}

/**
 * binder_alloc_print_pages() - print page usage
 * @m:     seq_file for output via seq_printf()
 * @alloc: binder_alloc for this proc
 */
void binder_alloc_print_pages(struct seq_file *m,
			      struct binder_alloc *alloc)
{
	struct binder_lru_page *page;
	int i;
	int active = 0;
	int lru = 0;
	int free = 0;

	mutex_lock(&alloc->mutex);
	/*
	 * Make sure the binder_alloc is fully initialized, otherwise we might
	 * read inconsistent state.
	 */
	if (binder_alloc_get_vma(alloc) != NULL) {
		for (i = 0; i < alloc->buffer_size / PAGE_SIZE; i++) {
			page = &alloc->pages[i];
			if (!page->page_ptr)
				free++;
			else if (list_empty(&page->lru))
				active++;
			else
				lru++;
		}
	}
	mutex_unlock(&alloc->mutex);
	seq_printf(m, "  pages: %d:%d:%d\n", active, lru, free);
	seq_printf(m, "  pages high watermark: %zu\n", alloc->pages_high);
}

/**
 * binder_alloc_get_allocated_count() - return count of buffers
 * @alloc: binder_alloc for this proc
 *
 * Return: count of allocated buffers
 */
int binder_alloc_get_allocated_count(struct binder_alloc *alloc)
{
	struct rb_node *n;
	int count = 0;

	mutex_lock(&alloc->mutex);
	for (n = rb_first(&alloc->allocated_buffers); n != NULL; n = rb_next(n))
		count++;
	mutex_unlock(&alloc->mutex);
	return count;
}


/**
 * binder_alloc_vma_close() - invalidate address space
 * @alloc: binder_alloc for this proc
 *
 * Called from binder_vma_close() when releasing address space.
 * Clears alloc->vma to prevent new incoming transactions from
 * allocating more buffers.
 */
void binder_alloc_vma_close(struct binder_alloc *alloc)
{
	binder_alloc_set_vma(alloc, NULL);
}

/**
 * binder_alloc_free_page() - shrinker callback to free pages
 * @item:   item to free
 * @lock:   lock protecting the item
 * @cb_arg: callback argument
 *
 * Called from list_lru_walk() in binder_shrink_scan() to free
 * up pages when the system is under memory pressure.
 */
enum lru_status binder_alloc_free_page(struct list_head *item,
				       struct list_lru_one *lru,
				       spinlock_t *lock,
				       void *cb_arg)
	__must_hold(lock)
{
	struct mm_struct *mm = NULL;
	struct binder_lru_page *page = container_of(item,
						    struct binder_lru_page,
						    lru);
	struct binder_alloc *alloc;
	uintptr_t page_addr;
	size_t index;
	struct vm_area_struct *vma;

	alloc = page->alloc;
	if (!mutex_trylock(&alloc->mutex))
		goto err_get_alloc_mutex_failed;

	if (!page->page_ptr)
		goto err_page_already_freed;

	index = page - alloc->pages;
	page_addr = (uintptr_t)alloc->buffer + index * PAGE_SIZE;

	mm = alloc->mm;
	if (!mmget_not_zero(mm))
		goto err_mmget;
	if (!mmap_read_trylock(mm))
		goto err_mmap_read_lock_failed;
	vma = vma_lookup(mm, page_addr);
	if (vma && vma != binder_alloc_get_vma(alloc))
		goto err_invalid_vma;

	list_lru_isolate(lru, item);
	spin_unlock(lock);

	if (vma) {
		trace_binder_unmap_user_start(alloc, index);

		zap_page_range(vma, page_addr, PAGE_SIZE);

		trace_binder_unmap_user_end(alloc, index);
	}
	mmap_read_unlock(mm);
	mmput_async(mm);

	trace_binder_unmap_kernel_start(alloc, index);

	__free_page(page->page_ptr);
	page->page_ptr = NULL;

	trace_binder_unmap_kernel_end(alloc, index);

	spin_lock(lock);
	mutex_unlock(&alloc->mutex);
	return LRU_REMOVED_RETRY;

err_invalid_vma:
	mmap_read_unlock(mm);
err_mmap_read_lock_failed:
	mmput_async(mm);
err_mmget:
err_page_already_freed:
	mutex_unlock(&alloc->mutex);
err_get_alloc_mutex_failed:
	return LRU_SKIP;
}

static unsigned long
binder_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	return list_lru_count(&binder_alloc_lru);
}

static unsigned long
binder_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	return list_lru_walk(&binder_alloc_lru, binder_alloc_free_page,
			    NULL, sc->nr_to_scan);
}

static struct shrinker binder_shrinker = {
	.count_objects = binder_shrink_count,
	.scan_objects = binder_shrink_scan,
	.seeks = DEFAULT_SEEKS,
};

/**
 * binder_alloc_init() - called by binder_open() for per-proc initialization
 * @alloc: binder_alloc for this proc
 *
 * Called from binder_open() to initialize binder_alloc fields for
 * new binder proc
 */
void binder_alloc_init(struct binder_alloc *alloc)
{
	alloc->pid = current->group_leader->pid;
	alloc->mm = current->mm;
	mmgrab(alloc->mm);
	mutex_init(&alloc->mutex);
	INIT_LIST_HEAD(&alloc->buffers);
}

int binder_alloc_shrinker_init(void)
{
	int ret = list_lru_init(&binder_alloc_lru);

	if (ret == 0) {
		ret = register_shrinker(&binder_shrinker, "android-binder");
		if (ret)
			list_lru_destroy(&binder_alloc_lru);
	}
	return ret;
}

void binder_alloc_shrinker_exit(void)
{
	unregister_shrinker(&binder_shrinker);
	list_lru_destroy(&binder_alloc_lru);
}

/**
 * check_buffer() - verify that buffer/offset is safe to access
 * @alloc: binder_alloc for this proc
 * @buffer: binder buffer to be accessed
 * @offset: offset into @buffer data
 * @bytes: bytes to access from offset
 *
 * Check that the @offset/@bytes are within the size of the given
 * @buffer and that the buffer is currently active and not freeable.
 * Offsets must also be multiples of sizeof(u32). The kernel is
 * allowed to touch the buffer in two cases:
 *
 * 1) when the buffer is being created:
 *     (buffer->free == 0 && buffer->allow_user_free == 0)
 * 2) when the buffer is being torn down:
 *     (buffer->free == 0 && buffer->transaction == NULL).
 *
 * Return: true if the buffer is safe to access
 */
static inline bool check_buffer(struct binder_alloc *alloc,
				struct binder_buffer *buffer,
				binder_size_t offset, size_t bytes)
{
	size_t buffer_size = binder_alloc_buffer_size(alloc, buffer);

	return buffer_size >= bytes &&
		offset <= buffer_size - bytes &&
		IS_ALIGNED(offset, sizeof(u32)) &&
		!buffer->free &&
		(!buffer->allow_user_free || !buffer->transaction);
}

/**
 * binder_alloc_get_page() - get kernel pointer for given buffer offset
 * @alloc: binder_alloc for this proc
 * @buffer: binder buffer to be accessed
 * @buffer_offset: offset into @buffer data
 * @pgoffp: address to copy final page offset to
 *
 * Lookup the struct page corresponding to the address
 * at @buffer_offset into @buffer->user_data. If @pgoffp is not
 * NULL, the byte-offset into the page is written there.
 *
 * The caller is responsible to ensure that the offset points
 * to a valid address within the @buffer and that @buffer is
 * not freeable by the user. Since it can't be freed, we are
 * guaranteed that the corresponding elements of @alloc->pages[]
 * cannot change.
 *
 * Return: struct page
 */
static struct page *binder_alloc_get_page(struct binder_alloc *alloc,
					  struct binder_buffer *buffer,
					  binder_size_t buffer_offset,
					  pgoff_t *pgoffp)
{
	binder_size_t buffer_space_offset = buffer_offset +
		(buffer->user_data - alloc->buffer);
	pgoff_t pgoff = buffer_space_offset & ~PAGE_MASK;
	size_t index = buffer_space_offset >> PAGE_SHIFT;
	struct binder_lru_page *lru_page;

	lru_page = &alloc->pages[index];
	*pgoffp = pgoff;
	return lru_page->page_ptr;
}

/**
 * binder_alloc_clear_buf() - zero out buffer
 * @alloc: binder_alloc for this proc
 * @buffer: binder buffer to be cleared
 *
 * memset the given buffer to 0
 */
static void binder_alloc_clear_buf(struct binder_alloc *alloc,
				   struct binder_buffer *buffer)
{
	size_t bytes = binder_alloc_buffer_size(alloc, buffer);
	binder_size_t buffer_offset = 0;

	while (bytes) {
		unsigned long size;
		struct page *page;
		pgoff_t pgoff;

		page = binder_alloc_get_page(alloc, buffer,
					     buffer_offset, &pgoff);
		size = min_t(size_t, bytes, PAGE_SIZE - pgoff);
		memset_page(page, pgoff, 0, size);
		bytes -= size;
		buffer_offset += size;
	}
}

/**
 * binder_alloc_copy_user_to_buffer() - copy src user to tgt user
 * @alloc: binder_alloc for this proc
 * @buffer: binder buffer to be accessed
 * @buffer_offset: offset into @buffer data
 * @from: userspace pointer to source buffer
 * @bytes: bytes to copy
 *
 * Copy bytes from source userspace to target buffer.
 *
 * Return: bytes remaining to be copied
 */
unsigned long
binder_alloc_copy_user_to_buffer(struct binder_alloc *alloc,
				 struct binder_buffer *buffer,
				 binder_size_t buffer_offset,
				 const void __user *from,
				 size_t bytes)
{
	if (!check_buffer(alloc, buffer, buffer_offset, bytes))
		return bytes;

	while (bytes) {
		unsigned long size;
		unsigned long ret;
		struct page *page;
		pgoff_t pgoff;
		void *kptr;

		page = binder_alloc_get_page(alloc, buffer,
					     buffer_offset, &pgoff);
		size = min_t(size_t, bytes, PAGE_SIZE - pgoff);
		kptr = kmap_local_page(page) + pgoff;
		ret = copy_from_user(kptr, from, size);
		kunmap_local(kptr);
		if (ret)
			return bytes - size + ret;
		bytes -= size;
		from += size;
		buffer_offset += size;
	}
	return 0;
}

static int binder_alloc_do_buffer_copy(struct binder_alloc *alloc,
				       bool to_buffer,
				       struct binder_buffer *buffer,
				       binder_size_t buffer_offset,
				       void *ptr,
				       size_t bytes)
{
	/* All copies must be 32-bit aligned and 32-bit size */
	if (!check_buffer(alloc, buffer, buffer_offset, bytes))
		return -EINVAL;

	while (bytes) {
		unsigned long size;
		struct page *page;
		pgoff_t pgoff;

		page = binder_alloc_get_page(alloc, buffer,
					     buffer_offset, &pgoff);
		size = min_t(size_t, bytes, PAGE_SIZE - pgoff);
		if (to_buffer)
			memcpy_to_page(page, pgoff, ptr, size);
		else
			memcpy_from_page(ptr, page, pgoff, size);
		bytes -= size;
		pgoff = 0;
		ptr = ptr + size;
		buffer_offset += size;
	}
	return 0;
}

int binder_alloc_copy_to_buffer(struct binder_alloc *alloc,
				struct binder_buffer *buffer,
				binder_size_t buffer_offset,
				void *src,
				size_t bytes)
{
	return binder_alloc_do_buffer_copy(alloc, true, buffer, buffer_offset,
					   src, bytes);
}

int binder_alloc_copy_from_buffer(struct binder_alloc *alloc,
				  void *dest,
				  struct binder_buffer *buffer,
				  binder_size_t buffer_offset,
				  size_t bytes)
{
	return binder_alloc_do_buffer_copy(alloc, false, buffer, buffer_offset,
					   dest, bytes);
}

