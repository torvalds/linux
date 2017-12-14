/* binder_alloc.c
 *
 * Android IPC Subsystem
 *
 * Copyright (C) 2007-2017 Google, Inc.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/cacheflush.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/rtmutex.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/list_lru.h>
#include "binder_alloc.h"
#include "binder_trace.h"

struct list_lru binder_alloc_lru;

static DEFINE_MUTEX(binder_alloc_mmap_lock);

enum {
	BINDER_DEBUG_OPEN_CLOSE             = 1U << 1,
	BINDER_DEBUG_BUFFER_ALLOC           = 1U << 2,
	BINDER_DEBUG_BUFFER_ALLOC_ASYNC     = 1U << 3,
};
static uint32_t binder_alloc_debug_mask;

module_param_named(debug_mask, binder_alloc_debug_mask,
		   uint, S_IWUSR | S_IRUGO);

#define binder_alloc_debug(mask, x...) \
	do { \
		if (binder_alloc_debug_mask & mask) \
			pr_info(x); \
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
		return (u8 *)alloc->buffer +
			alloc->buffer_size - (u8 *)buffer->data;
	return (u8 *)binder_buffer_next(buffer)->data - (u8 *)buffer->data;
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

		if (new_buffer->data < buffer->data)
			p = &parent->rb_left;
		else if (new_buffer->data > buffer->data)
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
	void *kern_ptr;

	kern_ptr = (void *)(user_ptr - alloc->user_buffer_offset);

	while (n) {
		buffer = rb_entry(n, struct binder_buffer, rb_node);
		BUG_ON(buffer->free);

		if (kern_ptr < buffer->data)
			n = n->rb_left;
		else if (kern_ptr > buffer->data)
			n = n->rb_right;
		else {
			/*
			 * Guard against user threads attempting to
			 * free the buffer twice
			 */
			if (buffer->free_in_progress) {
				pr_err("%d:%d FREE_BUFFER u%016llx user freed buffer twice\n",
				       alloc->pid, current->pid, (u64)user_ptr);
				return NULL;
			}
			buffer->free_in_progress = 1;
			return buffer;
		}
	}
	return NULL;
}

/**
 * binder_alloc_buffer_lookup() - get buffer given user ptr
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
				    void *start, void *end)
{
	void *page_addr;
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

	/* Same as mmget_not_zero() in later kernel versions */
	if (need_mm && atomic_inc_not_zero(&alloc->vma_vm_mm->mm_users))
		mm = alloc->vma_vm_mm;

	if (mm) {
		down_write(&mm->mmap_sem);
		vma = alloc->vma;
	}

	if (!vma && need_mm) {
		pr_err("%d: binder_alloc_buf failed to map pages in userspace, no vma\n",
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

		ret = map_kernel_range_noflush((unsigned long)page_addr,
					       PAGE_SIZE, PAGE_KERNEL,
					       &page->page_ptr);
		flush_cache_vmap((unsigned long)page_addr,
				(unsigned long)page_addr + PAGE_SIZE);
		if (ret != 1) {
			pr_err("%d: binder_alloc_buf failed to map page at %pK in kernel\n",
			       alloc->pid, page_addr);
			goto err_map_kernel_failed;
		}
		user_page_addr =
			(uintptr_t)page_addr + alloc->user_buffer_offset;
		ret = vm_insert_page(vma, user_page_addr, page[0].page_ptr);
		if (ret) {
			pr_err("%d: binder_alloc_buf failed to map page at %lx in userspace\n",
			       alloc->pid, user_page_addr);
			goto err_vm_insert_page_failed;
		}

		if (index + 1 > alloc->pages_high)
			alloc->pages_high = index + 1;

		trace_binder_alloc_page_end(alloc, index);
		/* vm_insert_page does not seem to increment the refcount */
	}
	if (mm) {
		up_write(&mm->mmap_sem);
		mmput(mm);
	}
	return 0;

free_range:
	for (page_addr = end - PAGE_SIZE; page_addr >= start;
	     page_addr -= PAGE_SIZE) {
		bool ret;
		size_t index;

		index = (page_addr - alloc->buffer) / PAGE_SIZE;
		page = &alloc->pages[index];

		trace_binder_free_lru_start(alloc, index);

		ret = list_lru_add(&binder_alloc_lru, &page->lru);
		WARN_ON(!ret);

		trace_binder_free_lru_end(alloc, index);
		continue;

err_vm_insert_page_failed:
		unmap_kernel_range((unsigned long)page_addr, PAGE_SIZE);
err_map_kernel_failed:
		__free_page(page->page_ptr);
		page->page_ptr = NULL;
err_alloc_page_failed:
err_page_ptr_cleared:
		;
	}
err_no_vma:
	if (mm) {
		up_write(&mm->mmap_sem);
		mmput(mm);
	}
	return vma ? -ENOMEM : -ESRCH;
}

static struct binder_buffer *binder_alloc_new_buf_locked(
				struct binder_alloc *alloc,
				size_t data_size,
				size_t offsets_size,
				size_t extra_buffers_size,
				int is_async)
{
	struct rb_node *n = alloc->free_buffers.rb_node;
	struct binder_buffer *buffer;
	size_t buffer_size;
	struct rb_node *best_fit = NULL;
	void *has_page_addr;
	void *end_page_addr;
	size_t size, data_offsets_size;
	int ret;

	if (alloc->vma == NULL) {
		pr_err("%d: binder_alloc_buf, no vma\n",
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
	if (is_async &&
	    alloc->free_async_space < size + sizeof(struct binder_buffer)) {
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
			     "%d: binder_alloc_buf size %zd failed, no async space left\n",
			      alloc->pid, size);
		return ERR_PTR(-ENOSPC);
	}

	/* Pad 0-size buffers so they get assigned unique addresses */
	size = max(size, sizeof(void *));

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
		pr_err("%d: binder_alloc_buf size %zd failed, no address space\n",
			alloc->pid, size);
		pr_err("allocated: %zd (num: %zd largest: %zd), free: %zd (num: %zd largest: %zd)\n",
		       total_alloc_size, allocated_buffers, largest_alloc_size,
		       total_free_size, free_buffers, largest_free_size);
		return ERR_PTR(-ENOSPC);
	}
	if (n == NULL) {
		buffer = rb_entry(best_fit, struct binder_buffer, rb_node);
		buffer_size = binder_alloc_buffer_size(alloc, buffer);
	}

	binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
		     "%d: binder_alloc_buf size %zd got buffer %pK size %zd\n",
		      alloc->pid, size, buffer, buffer_size);

	has_page_addr =
		(void *)(((uintptr_t)buffer->data + buffer_size) & PAGE_MASK);
	WARN_ON(n && buffer_size != size);
	end_page_addr =
		(void *)PAGE_ALIGN((uintptr_t)buffer->data + size);
	if (end_page_addr > has_page_addr)
		end_page_addr = has_page_addr;
	ret = binder_update_page_range(alloc, 1,
	    (void *)PAGE_ALIGN((uintptr_t)buffer->data), end_page_addr);
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
		new_buffer->data = (u8 *)buffer->data + size;
		list_add(&new_buffer->entry, &buffer->entry);
		new_buffer->free = 1;
		binder_insert_free_buffer(alloc, new_buffer);
	}

	rb_erase(best_fit, &alloc->free_buffers);
	buffer->free = 0;
	buffer->free_in_progress = 0;
	binder_insert_allocated_buffer_locked(alloc, buffer);
	binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
		     "%d: binder_alloc_buf size %zd got %pK\n",
		      alloc->pid, size, buffer);
	buffer->data_size = data_size;
	buffer->offsets_size = offsets_size;
	buffer->async_transaction = is_async;
	buffer->extra_buffers_size = extra_buffers_size;
	if (is_async) {
		alloc->free_async_space -= size + sizeof(struct binder_buffer);
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC_ASYNC,
			     "%d: binder_alloc_buf size %zd async free %zd\n",
			      alloc->pid, size, alloc->free_async_space);
	}
	return buffer;

err_alloc_buf_struct_failed:
	binder_update_page_range(alloc, 0,
				 (void *)PAGE_ALIGN((uintptr_t)buffer->data),
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
 *
 * Allocate a new buffer given the requested sizes. Returns
 * the kernel version of the buffer pointer. The size allocated
 * is the sum of the three given sizes (each rounded up to
 * pointer-sized boundary)
 *
 * Return:	The allocated buffer or %NULL if error
 */
struct binder_buffer *binder_alloc_new_buf(struct binder_alloc *alloc,
					   size_t data_size,
					   size_t offsets_size,
					   size_t extra_buffers_size,
					   int is_async)
{
	struct binder_buffer *buffer;

	mutex_lock(&alloc->mutex);
	buffer = binder_alloc_new_buf_locked(alloc, data_size, offsets_size,
					     extra_buffers_size, is_async);
	mutex_unlock(&alloc->mutex);
	return buffer;
}

static void *buffer_start_page(struct binder_buffer *buffer)
{
	return (void *)((uintptr_t)buffer->data & PAGE_MASK);
}

static void *prev_buffer_end_page(struct binder_buffer *buffer)
{
	return (void *)(((uintptr_t)(buffer->data) - 1) & PAGE_MASK);
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
				   alloc->pid, buffer->data, prev->data);
	}

	if (!list_is_last(&buffer->entry, &alloc->buffers)) {
		next = binder_buffer_next(buffer);
		if (buffer_start_page(next) == buffer_start_page(buffer)) {
			to_free = false;
			binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
					   "%d: merge free, buffer %pK share page with %pK\n",
					   alloc->pid,
					   buffer->data,
					   next->data);
		}
	}

	if (PAGE_ALIGNED(buffer->data)) {
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
				   "%d: merge free, buffer start %pK is page aligned\n",
				   alloc->pid, buffer->data);
		to_free = false;
	}

	if (to_free) {
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
				   "%d: merge free, buffer %pK do not share page with %pK or %pK\n",
				   alloc->pid, buffer->data,
				   prev->data, next ? next->data : NULL);
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
	BUG_ON(buffer->data < alloc->buffer);
	BUG_ON(buffer->data > alloc->buffer + alloc->buffer_size);

	if (buffer->async_transaction) {
		alloc->free_async_space += size + sizeof(struct binder_buffer);

		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC_ASYNC,
			     "%d: binder_free_buf size %zd async free %zd\n",
			      alloc->pid, size, alloc->free_async_space);
	}

	binder_update_page_range(alloc, 0,
		(void *)PAGE_ALIGN((uintptr_t)buffer->data),
		(void *)(((uintptr_t)buffer->data + buffer_size) & PAGE_MASK));

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

/**
 * binder_alloc_free_buf() - free a binder buffer
 * @alloc:	binder_alloc for this proc
 * @buffer:	kernel pointer to buffer
 *
 * Free the buffer allocated via binder_alloc_new_buffer()
 */
void binder_alloc_free_buf(struct binder_alloc *alloc,
			    struct binder_buffer *buffer)
{
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
	struct vm_struct *area;
	const char *failure_string;
	struct binder_buffer *buffer;

	mutex_lock(&binder_alloc_mmap_lock);
	if (alloc->buffer) {
		ret = -EBUSY;
		failure_string = "already mapped";
		goto err_already_mapped;
	}

	area = get_vm_area(vma->vm_end - vma->vm_start, VM_IOREMAP);
	if (area == NULL) {
		ret = -ENOMEM;
		failure_string = "get_vm_area";
		goto err_get_vm_area_failed;
	}
	alloc->buffer = area->addr;
	alloc->user_buffer_offset =
		vma->vm_start - (uintptr_t)alloc->buffer;
	mutex_unlock(&binder_alloc_mmap_lock);

#ifdef CONFIG_CPU_CACHE_VIPT
	if (cache_is_vipt_aliasing()) {
		while (CACHE_COLOUR(
				(vma->vm_start ^ (uint32_t)alloc->buffer))) {
			pr_info("binder_mmap: %d %lx-%lx maps %pK bad alignment\n",
				alloc->pid, vma->vm_start, vma->vm_end,
				alloc->buffer);
			vma->vm_start += PAGE_SIZE;
		}
	}
#endif
	alloc->pages = kzalloc(sizeof(alloc->pages[0]) *
				   ((vma->vm_end - vma->vm_start) / PAGE_SIZE),
			       GFP_KERNEL);
	if (alloc->pages == NULL) {
		ret = -ENOMEM;
		failure_string = "alloc page array";
		goto err_alloc_pages_failed;
	}
	alloc->buffer_size = vma->vm_end - vma->vm_start;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		failure_string = "alloc buffer struct";
		goto err_alloc_buf_struct_failed;
	}

	buffer->data = alloc->buffer;
	list_add(&buffer->entry, &alloc->buffers);
	buffer->free = 1;
	binder_insert_free_buffer(alloc, buffer);
	alloc->free_async_space = alloc->buffer_size / 2;
	barrier();
	alloc->vma = vma;
	alloc->vma_vm_mm = vma->vm_mm;
	/* Same as mmgrab() in later kernel versions */
	atomic_inc(&alloc->vma_vm_mm->mm_count);

	return 0;

err_alloc_buf_struct_failed:
	kfree(alloc->pages);
	alloc->pages = NULL;
err_alloc_pages_failed:
	mutex_lock(&binder_alloc_mmap_lock);
	vfree(alloc->buffer);
	alloc->buffer = NULL;
err_get_vm_area_failed:
err_already_mapped:
	mutex_unlock(&binder_alloc_mmap_lock);
	pr_err("%s: %d %lx-%lx %s failed %d\n", __func__,
	       alloc->pid, vma->vm_start, vma->vm_end, failure_string, ret);
	return ret;
}


void binder_alloc_deferred_release(struct binder_alloc *alloc)
{
	struct rb_node *n;
	int buffers, page_count;
	struct binder_buffer *buffer;

	BUG_ON(alloc->vma);

	buffers = 0;
	mutex_lock(&alloc->mutex);
	while ((n = rb_first(&alloc->allocated_buffers))) {
		buffer = rb_entry(n, struct binder_buffer, rb_node);

		/* Transaction should already have been freed */
		BUG_ON(buffer->transaction);

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
			void *page_addr;
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
			unmap_kernel_range((unsigned long)page_addr, PAGE_SIZE);
			__free_page(alloc->pages[i].page_ptr);
			page_count++;
		}
		kfree(alloc->pages);
		vfree(alloc->buffer);
	}
	mutex_unlock(&alloc->mutex);
	if (alloc->vma_vm_mm)
		mmdrop(alloc->vma_vm_mm);

	binder_alloc_debug(BINDER_DEBUG_OPEN_CLOSE,
		     "%s: %d buffers %d, pages %d\n",
		     __func__, alloc->pid, buffers, page_count);
}

static void print_binder_buffer(struct seq_file *m, const char *prefix,
				struct binder_buffer *buffer)
{
	seq_printf(m, "%s %d: %pK size %zd:%zd:%zd %s\n",
		   prefix, buffer->debug_id, buffer->data,
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
	for (i = 0; i < alloc->buffer_size / PAGE_SIZE; i++) {
		page = &alloc->pages[i];
		if (!page->page_ptr)
			free++;
		else if (list_empty(&page->lru))
			active++;
		else
			lru++;
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
	WRITE_ONCE(alloc->vma, NULL);
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
	vma = alloc->vma;
	if (vma) {
		/* Same as mmget_not_zero() in later kernel versions */
		if (!atomic_inc_not_zero(&alloc->vma_vm_mm->mm_users))
			goto err_mmget;
		mm = alloc->vma_vm_mm;
		if (!down_write_trylock(&mm->mmap_sem))
			goto err_down_write_mmap_sem_failed;
	}

	list_lru_isolate(lru, item);
	spin_unlock(lock);

	if (vma) {
		trace_binder_unmap_user_start(alloc, index);

		zap_page_range(vma,
			       page_addr +
			       alloc->user_buffer_offset,
			       PAGE_SIZE, NULL);

		trace_binder_unmap_user_end(alloc, index);

		up_write(&mm->mmap_sem);
		mmput(mm);
	}

	trace_binder_unmap_kernel_start(alloc, index);

	unmap_kernel_range(page_addr, PAGE_SIZE);
	__free_page(page->page_ptr);
	page->page_ptr = NULL;

	trace_binder_unmap_kernel_end(alloc, index);

	spin_lock(lock);
	mutex_unlock(&alloc->mutex);
	return LRU_REMOVED_RETRY;

err_down_write_mmap_sem_failed:
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
	unsigned long ret = list_lru_count(&binder_alloc_lru);
	return ret;
}

static unsigned long
binder_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	unsigned long ret;

	ret = list_lru_walk(&binder_alloc_lru, binder_alloc_free_page,
			    NULL, sc->nr_to_scan);
	return ret;
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
	mutex_init(&alloc->mutex);
	INIT_LIST_HEAD(&alloc->buffers);
}

int binder_alloc_shrinker_init(void)
{
	int ret = list_lru_init(&binder_alloc_lru);

	if (ret == 0) {
		ret = register_shrinker(&binder_shrinker);
		if (ret)
			list_lru_destroy(&binder_alloc_lru);
	}
	return ret;
}
