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
#include <linux/sched/mm.h>
#include <linux/module.h>
#include <linux/rtmutex.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include "binder_alloc.h"
#include "binder_trace.h"

static DEFINE_MUTEX(binder_alloc_mmap_lock);

enum {
	BINDER_DEBUG_OPEN_CLOSE             = 1U << 1,
	BINDER_DEBUG_BUFFER_ALLOC           = 1U << 2,
	BINDER_DEBUG_BUFFER_ALLOC_ASYNC     = 1U << 3,
};
static uint32_t binder_alloc_debug_mask;

module_param_named(debug_mask, binder_alloc_debug_mask,
		   uint, 0644);

#define binder_alloc_debug(mask, x...) \
	do { \
		if (binder_alloc_debug_mask & mask) \
			pr_info(x); \
	} while (0)

static size_t binder_alloc_buffer_size(struct binder_alloc *alloc,
				       struct binder_buffer *buffer)
{
	if (list_is_last(&buffer->entry, &alloc->buffers))
		return alloc->buffer +
		       alloc->buffer_size - (void *)buffer->data;
	return (size_t)list_entry(buffer->entry.next,
			  struct binder_buffer, entry) - (size_t)buffer->data;
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

		if (new_buffer < buffer)
			p = &parent->rb_left;
		else if (new_buffer > buffer)
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
	struct binder_buffer *kern_ptr;

	kern_ptr = (struct binder_buffer *)(user_ptr - alloc->user_buffer_offset
		- offsetof(struct binder_buffer, data));

	while (n) {
		buffer = rb_entry(n, struct binder_buffer, rb_node);
		BUG_ON(buffer->free);

		if (kern_ptr < buffer)
			n = n->rb_left;
		else if (kern_ptr > buffer)
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
				    void *start, void *end,
				    struct vm_area_struct *vma)
{
	void *page_addr;
	unsigned long user_page_addr;
	struct page **page;
	struct mm_struct *mm;

	binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
		     "%d: %s pages %pK-%pK\n", alloc->pid,
		     allocate ? "allocate" : "free", start, end);

	if (end <= start)
		return 0;

	trace_binder_update_page_range(alloc, allocate, start, end);

	if (vma)
		mm = NULL;
	else
		mm = get_task_mm(alloc->tsk);

	if (mm) {
		down_write(&mm->mmap_sem);
		vma = alloc->vma;
		if (vma && mm != alloc->vma_vm_mm) {
			pr_err("%d: vma mm and task mm mismatch\n",
				alloc->pid);
			vma = NULL;
		}
	}

	if (allocate == 0)
		goto free_range;

	if (vma == NULL) {
		pr_err("%d: binder_alloc_buf failed to map pages in userspace, no vma\n",
			alloc->pid);
		goto err_no_vma;
	}

	for (page_addr = start; page_addr < end; page_addr += PAGE_SIZE) {
		int ret;

		page = &alloc->pages[(page_addr - alloc->buffer) / PAGE_SIZE];

		BUG_ON(*page);
		*page = alloc_page(GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO);
		if (*page == NULL) {
			pr_err("%d: binder_alloc_buf failed for page at %pK\n",
				alloc->pid, page_addr);
			goto err_alloc_page_failed;
		}
		ret = map_kernel_range_noflush((unsigned long)page_addr,
					PAGE_SIZE, PAGE_KERNEL, page);
		flush_cache_vmap((unsigned long)page_addr,
				(unsigned long)page_addr + PAGE_SIZE);
		if (ret != 1) {
			pr_err("%d: binder_alloc_buf failed to map page at %pK in kernel\n",
			       alloc->pid, page_addr);
			goto err_map_kernel_failed;
		}
		user_page_addr =
			(uintptr_t)page_addr + alloc->user_buffer_offset;
		ret = vm_insert_page(vma, user_page_addr, page[0]);
		if (ret) {
			pr_err("%d: binder_alloc_buf failed to map page at %lx in userspace\n",
			       alloc->pid, user_page_addr);
			goto err_vm_insert_page_failed;
		}
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
		page = &alloc->pages[(page_addr - alloc->buffer) / PAGE_SIZE];
		if (vma)
			zap_page_range(vma, (uintptr_t)page_addr +
				alloc->user_buffer_offset, PAGE_SIZE);
err_vm_insert_page_failed:
		unmap_kernel_range((unsigned long)page_addr, PAGE_SIZE);
err_map_kernel_failed:
		__free_page(*page);
		*page = NULL;
err_alloc_page_failed:
		;
	}
err_no_vma:
	if (mm) {
		up_write(&mm->mmap_sem);
		mmput(mm);
	}
	return vma ? -ENOMEM : -ESRCH;
}

struct binder_buffer *binder_alloc_new_buf_locked(struct binder_alloc *alloc,
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
	if (n == NULL) {
		if (size + sizeof(struct binder_buffer) + 4 >= buffer_size)
			buffer_size = size; /* no room for other buffers */
		else
			buffer_size = size + sizeof(struct binder_buffer);
	}
	end_page_addr =
		(void *)PAGE_ALIGN((uintptr_t)buffer->data + buffer_size);
	if (end_page_addr > has_page_addr)
		end_page_addr = has_page_addr;
	ret = binder_update_page_range(alloc, 1,
	    (void *)PAGE_ALIGN((uintptr_t)buffer->data), end_page_addr, NULL);
	if (ret)
		return ERR_PTR(ret);

	rb_erase(best_fit, &alloc->free_buffers);
	buffer->free = 0;
	buffer->free_in_progress = 0;
	binder_insert_allocated_buffer_locked(alloc, buffer);
	if (buffer_size != size) {
		struct binder_buffer *new_buffer = (void *)buffer->data + size;

		list_add(&new_buffer->entry, &buffer->entry);
		new_buffer->free = 1;
		binder_insert_free_buffer(alloc, new_buffer);
	}
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
	return (void *)((uintptr_t)buffer & PAGE_MASK);
}

static void *buffer_end_page(struct binder_buffer *buffer)
{
	return (void *)(((uintptr_t)(buffer + 1) - 1) & PAGE_MASK);
}

static void binder_delete_free_buffer(struct binder_alloc *alloc,
				      struct binder_buffer *buffer)
{
	struct binder_buffer *prev, *next = NULL;
	int free_page_end = 1;
	int free_page_start = 1;

	BUG_ON(alloc->buffers.next == &buffer->entry);
	prev = list_entry(buffer->entry.prev, struct binder_buffer, entry);
	BUG_ON(!prev->free);
	if (buffer_end_page(prev) == buffer_start_page(buffer)) {
		free_page_start = 0;
		if (buffer_end_page(prev) == buffer_end_page(buffer))
			free_page_end = 0;
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
			     "%d: merge free, buffer %pK share page with %pK\n",
			      alloc->pid, buffer, prev);
	}

	if (!list_is_last(&buffer->entry, &alloc->buffers)) {
		next = list_entry(buffer->entry.next,
				  struct binder_buffer, entry);
		if (buffer_start_page(next) == buffer_end_page(buffer)) {
			free_page_end = 0;
			if (buffer_start_page(next) ==
			    buffer_start_page(buffer))
				free_page_start = 0;
			binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
				     "%d: merge free, buffer %pK share page with %pK\n",
				      alloc->pid, buffer, prev);
		}
	}
	list_del(&buffer->entry);
	if (free_page_start || free_page_end) {
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
			     "%d: merge free, buffer %pK do not share page%s%s with %pK or %pK\n",
			     alloc->pid, buffer, free_page_start ? "" : " end",
			     free_page_end ? "" : " start", prev, next);
		binder_update_page_range(alloc, 0, free_page_start ?
			buffer_start_page(buffer) : buffer_end_page(buffer),
			(free_page_end ? buffer_end_page(buffer) :
			buffer_start_page(buffer)) + PAGE_SIZE, NULL);
	}
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
	BUG_ON((void *)buffer < alloc->buffer);
	BUG_ON((void *)buffer > alloc->buffer + alloc->buffer_size);

	if (buffer->async_transaction) {
		alloc->free_async_space += size + sizeof(struct binder_buffer);

		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC_ASYNC,
			     "%d: binder_free_buf size %zd async free %zd\n",
			      alloc->pid, size, alloc->free_async_space);
	}

	binder_update_page_range(alloc, 0,
		(void *)PAGE_ALIGN((uintptr_t)buffer->data),
		(void *)(((uintptr_t)buffer->data + buffer_size) & PAGE_MASK),
		NULL);

	rb_erase(&buffer->rb_node, &alloc->allocated_buffers);
	buffer->free = 1;
	if (!list_is_last(&buffer->entry, &alloc->buffers)) {
		struct binder_buffer *next = list_entry(buffer->entry.next,
						struct binder_buffer, entry);

		if (next->free) {
			rb_erase(&next->rb_node, &alloc->free_buffers);
			binder_delete_free_buffer(alloc, next);
		}
	}
	if (alloc->buffers.next != &buffer->entry) {
		struct binder_buffer *prev = list_entry(buffer->entry.prev,
						struct binder_buffer, entry);

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
			pr_info("%s: %d %lx-%lx maps %pK bad alignment\n",
				__func__, alloc->pid, vma->vm_start,
				vma->vm_end, alloc->buffer);
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

	if (binder_update_page_range(alloc, 1, alloc->buffer,
				     alloc->buffer + PAGE_SIZE, vma)) {
		ret = -ENOMEM;
		failure_string = "alloc small buf";
		goto err_alloc_small_buf_failed;
	}
	buffer = alloc->buffer;
	INIT_LIST_HEAD(&alloc->buffers);
	list_add(&buffer->entry, &alloc->buffers);
	buffer->free = 1;
	binder_insert_free_buffer(alloc, buffer);
	alloc->free_async_space = alloc->buffer_size / 2;
	barrier();
	alloc->vma = vma;
	alloc->vma_vm_mm = vma->vm_mm;

	return 0;

err_alloc_small_buf_failed:
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

	BUG_ON(alloc->vma);

	buffers = 0;
	mutex_lock(&alloc->mutex);
	while ((n = rb_first(&alloc->allocated_buffers))) {
		struct binder_buffer *buffer;

		buffer = rb_entry(n, struct binder_buffer, rb_node);

		/* Transaction should already have been freed */
		BUG_ON(buffer->transaction);

		binder_free_buf_locked(alloc, buffer);
		buffers++;
	}

	page_count = 0;
	if (alloc->pages) {
		int i;

		for (i = 0; i < alloc->buffer_size / PAGE_SIZE; i++) {
			void *page_addr;

			if (!alloc->pages[i])
				continue;

			page_addr = alloc->buffer + i * PAGE_SIZE;
			binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
				     "%s: %d: page %d at %pK not freed\n",
				     __func__, alloc->pid, i, page_addr);
			unmap_kernel_range((unsigned long)page_addr, PAGE_SIZE);
			__free_page(alloc->pages[i]);
			page_count++;
		}
		kfree(alloc->pages);
		vfree(alloc->buffer);
	}
	mutex_unlock(&alloc->mutex);

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
	WRITE_ONCE(alloc->vma_vm_mm, NULL);
}

/**
 * binder_alloc_init() - called by binder_open() for per-proc initialization
 * @alloc: binder_alloc for this proc
 *
 * Called from binder_open() to initialize binder_alloc fields for
 * new binder proc
 */
void binder_alloc_init(struct binder_alloc *alloc)
{
	alloc->tsk = current->group_leader;
	alloc->pid = current->group_leader->pid;
	mutex_init(&alloc->mutex);
}

