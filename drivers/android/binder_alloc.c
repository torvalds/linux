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

struct list_lru binder_freelist;

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
		return alloc->vm_start + alloc->buffer_size - buffer->user_data;
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
		unsigned long user_ptr)
{
	struct rb_node *n = alloc->allocated_buffers.rb_node;
	struct binder_buffer *buffer;

	while (n) {
		buffer = rb_entry(n, struct binder_buffer, rb_node);
		BUG_ON(buffer->free);

		if (user_ptr < buffer->user_data) {
			n = n->rb_left;
		} else if (user_ptr > buffer->user_data) {
			n = n->rb_right;
		} else {
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
						   unsigned long user_ptr)
{
	struct binder_buffer *buffer;

	mutex_lock(&alloc->mutex);
	buffer = binder_alloc_prepare_to_free_locked(alloc, user_ptr);
	mutex_unlock(&alloc->mutex);
	return buffer;
}

static inline void
binder_set_installed_page(struct binder_alloc *alloc,
			  unsigned long index,
			  struct page *page)
{
	/* Pairs with acquire in binder_get_installed_page() */
	smp_store_release(&alloc->pages[index], page);
}

static inline struct page *
binder_get_installed_page(struct binder_alloc *alloc, unsigned long index)
{
	/* Pairs with release in binder_set_installed_page() */
	return smp_load_acquire(&alloc->pages[index]);
}

static void binder_lru_freelist_add(struct binder_alloc *alloc,
				    unsigned long start, unsigned long end)
{
	unsigned long page_addr;
	struct page *page;

	trace_binder_update_page_range(alloc, false, start, end);

	for (page_addr = start; page_addr < end; page_addr += PAGE_SIZE) {
		size_t index;
		int ret;

		index = (page_addr - alloc->vm_start) / PAGE_SIZE;
		page = binder_get_installed_page(alloc, index);
		if (!page)
			continue;

		trace_binder_free_lru_start(alloc, index);

		ret = list_lru_add(&binder_freelist,
				   page_to_lru(page),
				   page_to_nid(page),
				   NULL);
		WARN_ON(!ret);

		trace_binder_free_lru_end(alloc, index);
	}
}

static inline
void binder_alloc_set_mapped(struct binder_alloc *alloc, bool state)
{
	/* pairs with smp_load_acquire in binder_alloc_is_mapped() */
	smp_store_release(&alloc->mapped, state);
}

static inline bool binder_alloc_is_mapped(struct binder_alloc *alloc)
{
	/* pairs with smp_store_release in binder_alloc_set_mapped() */
	return smp_load_acquire(&alloc->mapped);
}

static struct page *binder_page_lookup(struct binder_alloc *alloc,
				       unsigned long addr)
{
	struct mm_struct *mm = alloc->mm;
	struct page *page;
	long npages = 0;

	/*
	 * Find an existing page in the remote mm. If missing,
	 * don't attempt to fault-in just propagate an error.
	 */
	mmap_read_lock(mm);
	if (binder_alloc_is_mapped(alloc))
		npages = get_user_pages_remote(mm, addr, 1, FOLL_NOFAULT,
					       &page, NULL);
	mmap_read_unlock(mm);

	return npages > 0 ? page : NULL;
}

static int binder_page_insert(struct binder_alloc *alloc,
			      unsigned long addr,
			      struct page *page)
{
	struct mm_struct *mm = alloc->mm;
	struct vm_area_struct *vma;
	int ret = -ESRCH;

	/* attempt per-vma lock first */
	vma = lock_vma_under_rcu(mm, addr);
	if (vma) {
		if (binder_alloc_is_mapped(alloc))
			ret = vm_insert_page(vma, addr, page);
		vma_end_read(vma);
		return ret;
	}

	/* fall back to mmap_lock */
	mmap_read_lock(mm);
	vma = vma_lookup(mm, addr);
	if (vma && binder_alloc_is_mapped(alloc))
		ret = vm_insert_page(vma, addr, page);
	mmap_read_unlock(mm);

	return ret;
}

static struct page *binder_page_alloc(struct binder_alloc *alloc,
				      unsigned long index)
{
	struct binder_shrinker_mdata *mdata;
	struct page *page;

	page = alloc_page(GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO);
	if (!page)
		return NULL;

	/* allocate and install shrinker metadata under page->private */
	mdata = kzalloc(sizeof(*mdata), GFP_KERNEL);
	if (!mdata) {
		__free_page(page);
		return NULL;
	}

	mdata->alloc = alloc;
	mdata->page_index = index;
	INIT_LIST_HEAD(&mdata->lru);
	set_page_private(page, (unsigned long)mdata);

	return page;
}

static void binder_free_page(struct page *page)
{
	kfree((struct binder_shrinker_mdata *)page_private(page));
	__free_page(page);
}

static int binder_install_single_page(struct binder_alloc *alloc,
				      unsigned long index,
				      unsigned long addr)
{
	struct page *page;
	int ret;

	if (!mmget_not_zero(alloc->mm))
		return -ESRCH;

	page = binder_page_alloc(alloc, index);
	if (!page) {
		ret = -ENOMEM;
		goto out;
	}

	ret = binder_page_insert(alloc, addr, page);
	switch (ret) {
	case -EBUSY:
		/*
		 * EBUSY is ok. Someone installed the pte first but the
		 * alloc->pages[index] has not been updated yet. Discard
		 * our page and look up the one already installed.
		 */
		ret = 0;
		binder_free_page(page);
		page = binder_page_lookup(alloc, addr);
		if (!page) {
			pr_err("%d: failed to find page at offset %lx\n",
			       alloc->pid, addr - alloc->vm_start);
			ret = -ESRCH;
			break;
		}
		fallthrough;
	case 0:
		/* Mark page installation complete and safe to use */
		binder_set_installed_page(alloc, index, page);
		break;
	default:
		binder_free_page(page);
		pr_err("%d: %s failed to insert page at offset %lx with %d\n",
		       alloc->pid, __func__, addr - alloc->vm_start, ret);
		break;
	}
out:
	mmput_async(alloc->mm);
	return ret;
}

static int binder_install_buffer_pages(struct binder_alloc *alloc,
				       struct binder_buffer *buffer,
				       size_t size)
{
	unsigned long start, final;
	unsigned long page_addr;

	start = buffer->user_data & PAGE_MASK;
	final = PAGE_ALIGN(buffer->user_data + size);

	for (page_addr = start; page_addr < final; page_addr += PAGE_SIZE) {
		unsigned long index;
		int ret;

		index = (page_addr - alloc->vm_start) / PAGE_SIZE;
		if (binder_get_installed_page(alloc, index))
			continue;

		trace_binder_alloc_page_start(alloc, index);

		ret = binder_install_single_page(alloc, index, page_addr);
		if (ret)
			return ret;

		trace_binder_alloc_page_end(alloc, index);
	}

	return 0;
}

/* The range of pages should exclude those shared with other buffers */
static void binder_lru_freelist_del(struct binder_alloc *alloc,
				    unsigned long start, unsigned long end)
{
	unsigned long page_addr;
	struct page *page;

	trace_binder_update_page_range(alloc, true, start, end);

	for (page_addr = start; page_addr < end; page_addr += PAGE_SIZE) {
		unsigned long index;
		bool on_lru;

		index = (page_addr - alloc->vm_start) / PAGE_SIZE;
		page = binder_get_installed_page(alloc, index);

		if (page) {
			trace_binder_alloc_lru_start(alloc, index);

			on_lru = list_lru_del(&binder_freelist,
					      page_to_lru(page),
					      page_to_nid(page),
					      NULL);
			WARN_ON(!on_lru);

			trace_binder_alloc_lru_end(alloc, index);
			continue;
		}

		if (index + 1 > alloc->pages_high)
			alloc->pages_high = index + 1;
	}
}

static void debug_no_space_locked(struct binder_alloc *alloc)
{
	size_t largest_alloc_size = 0;
	struct binder_buffer *buffer;
	size_t allocated_buffers = 0;
	size_t largest_free_size = 0;
	size_t total_alloc_size = 0;
	size_t total_free_size = 0;
	size_t free_buffers = 0;
	size_t buffer_size;
	struct rb_node *n;

	for (n = rb_first(&alloc->allocated_buffers); n; n = rb_next(n)) {
		buffer = rb_entry(n, struct binder_buffer, rb_node);
		buffer_size = binder_alloc_buffer_size(alloc, buffer);
		allocated_buffers++;
		total_alloc_size += buffer_size;
		if (buffer_size > largest_alloc_size)
			largest_alloc_size = buffer_size;
	}

	for (n = rb_first(&alloc->free_buffers); n; n = rb_next(n)) {
		buffer = rb_entry(n, struct binder_buffer, rb_node);
		buffer_size = binder_alloc_buffer_size(alloc, buffer);
		free_buffers++;
		total_free_size += buffer_size;
		if (buffer_size > largest_free_size)
			largest_free_size = buffer_size;
	}

	binder_alloc_debug(BINDER_DEBUG_USER_ERROR,
			   "allocated: %zd (num: %zd largest: %zd), free: %zd (num: %zd largest: %zd)\n",
			   total_alloc_size, allocated_buffers,
			   largest_alloc_size, total_free_size,
			   free_buffers, largest_free_size);
}

static bool debug_low_async_space_locked(struct binder_alloc *alloc)
{
	/*
	 * Find the amount and size of buffers allocated by the current caller;
	 * The idea is that once we cross the threshold, whoever is responsible
	 * for the low async space is likely to try to send another async txn,
	 * and at some point we'll catch them in the act. This is more efficient
	 * than keeping a map per pid.
	 */
	struct binder_buffer *buffer;
	size_t total_alloc_size = 0;
	int pid = current->tgid;
	size_t num_buffers = 0;
	struct rb_node *n;

	/*
	 * Only start detecting spammers once we have less than 20% of async
	 * space left (which is less than 10% of total buffer size).
	 */
	if (alloc->free_async_space >= alloc->buffer_size / 10) {
		alloc->oneway_spam_detected = false;
		return false;
	}

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

/* Callers preallocate @new_buffer, it is freed by this function if unused */
static struct binder_buffer *binder_alloc_new_buf_locked(
				struct binder_alloc *alloc,
				struct binder_buffer *new_buffer,
				size_t size,
				int is_async)
{
	struct rb_node *n = alloc->free_buffers.rb_node;
	struct rb_node *best_fit = NULL;
	struct binder_buffer *buffer;
	unsigned long next_used_page;
	unsigned long curr_last_page;
	size_t buffer_size;

	if (is_async && alloc->free_async_space < size) {
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
			     "%d: binder_alloc_buf size %zd failed, no async space left\n",
			      alloc->pid, size);
		buffer = ERR_PTR(-ENOSPC);
		goto out;
	}

	while (n) {
		buffer = rb_entry(n, struct binder_buffer, rb_node);
		BUG_ON(!buffer->free);
		buffer_size = binder_alloc_buffer_size(alloc, buffer);

		if (size < buffer_size) {
			best_fit = n;
			n = n->rb_left;
		} else if (size > buffer_size) {
			n = n->rb_right;
		} else {
			best_fit = n;
			break;
		}
	}

	if (unlikely(!best_fit)) {
		binder_alloc_debug(BINDER_DEBUG_USER_ERROR,
				   "%d: binder_alloc_buf size %zd failed, no address space\n",
				   alloc->pid, size);
		debug_no_space_locked(alloc);
		buffer = ERR_PTR(-ENOSPC);
		goto out;
	}

	if (buffer_size != size) {
		/* Found an oversized buffer and needs to be split */
		buffer = rb_entry(best_fit, struct binder_buffer, rb_node);
		buffer_size = binder_alloc_buffer_size(alloc, buffer);

		WARN_ON(n || buffer_size == size);
		new_buffer->user_data = buffer->user_data + size;
		list_add(&new_buffer->entry, &buffer->entry);
		new_buffer->free = 1;
		binder_insert_free_buffer(alloc, new_buffer);
		new_buffer = NULL;
	}

	binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
		     "%d: binder_alloc_buf size %zd got buffer %pK size %zd\n",
		      alloc->pid, size, buffer, buffer_size);

	/*
	 * Now we remove the pages from the freelist. A clever calculation
	 * with buffer_size determines if the last page is shared with an
	 * adjacent in-use buffer. In such case, the page has been already
	 * removed from the freelist so we trim our range short.
	 */
	next_used_page = (buffer->user_data + buffer_size) & PAGE_MASK;
	curr_last_page = PAGE_ALIGN(buffer->user_data + size);
	binder_lru_freelist_del(alloc, PAGE_ALIGN(buffer->user_data),
				min(next_used_page, curr_last_page));

	rb_erase(&buffer->rb_node, &alloc->free_buffers);
	buffer->free = 0;
	buffer->allow_user_free = 0;
	binder_insert_allocated_buffer_locked(alloc, buffer);
	buffer->async_transaction = is_async;
	buffer->oneway_spam_suspect = false;
	if (is_async) {
		alloc->free_async_space -= size;
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC_ASYNC,
			     "%d: binder_alloc_buf size %zd async free %zd\n",
			      alloc->pid, size, alloc->free_async_space);
		if (debug_low_async_space_locked(alloc))
			buffer->oneway_spam_suspect = true;
	}

out:
	/* Discard possibly unused new_buffer */
	kfree(new_buffer);
	return buffer;
}

/* Calculate the sanitized total size, returns 0 for invalid request */
static inline size_t sanitized_size(size_t data_size,
				    size_t offsets_size,
				    size_t extra_buffers_size)
{
	size_t total, tmp;

	/* Align to pointer size and check for overflows */
	tmp = ALIGN(data_size, sizeof(void *)) +
		ALIGN(offsets_size, sizeof(void *));
	if (tmp < data_size || tmp < offsets_size)
		return 0;
	total = tmp + ALIGN(extra_buffers_size, sizeof(void *));
	if (total < tmp || total < extra_buffers_size)
		return 0;

	/* Pad 0-sized buffers so they get a unique address */
	total = max(total, sizeof(void *));

	return total;
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
 * Return:	The allocated buffer or %ERR_PTR(-errno) if error
 */
struct binder_buffer *binder_alloc_new_buf(struct binder_alloc *alloc,
					   size_t data_size,
					   size_t offsets_size,
					   size_t extra_buffers_size,
					   int is_async)
{
	struct binder_buffer *buffer, *next;
	size_t size;
	int ret;

	/* Check binder_alloc is fully initialized */
	if (!binder_alloc_is_mapped(alloc)) {
		binder_alloc_debug(BINDER_DEBUG_USER_ERROR,
				   "%d: binder_alloc_buf, no vma\n",
				   alloc->pid);
		return ERR_PTR(-ESRCH);
	}

	size = sanitized_size(data_size, offsets_size, extra_buffers_size);
	if (unlikely(!size)) {
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
				   "%d: got transaction with invalid size %zd-%zd-%zd\n",
				   alloc->pid, data_size, offsets_size,
				   extra_buffers_size);
		return ERR_PTR(-EINVAL);
	}

	/* Preallocate the next buffer */
	next = kzalloc(sizeof(*next), GFP_KERNEL);
	if (!next)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&alloc->mutex);
	buffer = binder_alloc_new_buf_locked(alloc, next, size, is_async);
	if (IS_ERR(buffer)) {
		mutex_unlock(&alloc->mutex);
		goto out;
	}

	buffer->data_size = data_size;
	buffer->offsets_size = offsets_size;
	buffer->extra_buffers_size = extra_buffers_size;
	buffer->pid = current->tgid;
	mutex_unlock(&alloc->mutex);

	ret = binder_install_buffer_pages(alloc, buffer, size);
	if (ret) {
		binder_alloc_free_buf(alloc, buffer);
		buffer = ERR_PTR(ret);
	}
out:
	return buffer;
}

static unsigned long buffer_start_page(struct binder_buffer *buffer)
{
	return buffer->user_data & PAGE_MASK;
}

static unsigned long prev_buffer_end_page(struct binder_buffer *buffer)
{
	return (buffer->user_data - 1) & PAGE_MASK;
}

static void binder_delete_free_buffer(struct binder_alloc *alloc,
				      struct binder_buffer *buffer)
{
	struct binder_buffer *prev, *next;

	if (PAGE_ALIGNED(buffer->user_data))
		goto skip_freelist;

	BUG_ON(alloc->buffers.next == &buffer->entry);
	prev = binder_buffer_prev(buffer);
	BUG_ON(!prev->free);
	if (prev_buffer_end_page(prev) == buffer_start_page(buffer))
		goto skip_freelist;

	if (!list_is_last(&buffer->entry, &alloc->buffers)) {
		next = binder_buffer_next(buffer);
		if (buffer_start_page(next) == buffer_start_page(buffer))
			goto skip_freelist;
	}

	binder_lru_freelist_add(alloc, buffer_start_page(buffer),
				buffer_start_page(buffer) + PAGE_SIZE);
skip_freelist:
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
	BUG_ON(buffer->user_data < alloc->vm_start);
	BUG_ON(buffer->user_data > alloc->vm_start + alloc->buffer_size);

	if (buffer->async_transaction) {
		alloc->free_async_space += buffer_size;
		binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC_ASYNC,
			     "%d: binder_free_buf size %zd async free %zd\n",
			      alloc->pid, size, alloc->free_async_space);
	}

	binder_lru_freelist_add(alloc, PAGE_ALIGN(buffer->user_data),
				(buffer->user_data + buffer_size) & PAGE_MASK);

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
		(buffer->user_data - alloc->vm_start);
	pgoff_t pgoff = buffer_space_offset & ~PAGE_MASK;
	size_t index = buffer_space_offset >> PAGE_SHIFT;

	*pgoffp = pgoff;

	return alloc->pages[index];
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
	struct binder_buffer *buffer;
	const char *failure_string;
	int ret;

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

	alloc->vm_start = vma->vm_start;

	alloc->pages = kvcalloc(alloc->buffer_size / PAGE_SIZE,
				sizeof(alloc->pages[0]),
				GFP_KERNEL);
	if (!alloc->pages) {
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

	buffer->user_data = alloc->vm_start;
	list_add(&buffer->entry, &alloc->buffers);
	buffer->free = 1;
	binder_insert_free_buffer(alloc, buffer);
	alloc->free_async_space = alloc->buffer_size / 2;

	/* Signal binder_alloc is fully initialized */
	binder_alloc_set_mapped(alloc, true);

	return 0;

err_alloc_buf_struct_failed:
	kvfree(alloc->pages);
	alloc->pages = NULL;
err_alloc_pages_failed:
	alloc->vm_start = 0;
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
	BUG_ON(alloc->mapped);

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
			struct page *page;
			bool on_lru;

			page = binder_get_installed_page(alloc, i);
			if (!page)
				continue;

			on_lru = list_lru_del(&binder_freelist,
					      page_to_lru(page),
					      page_to_nid(page),
					      NULL);
			binder_alloc_debug(BINDER_DEBUG_BUFFER_ALLOC,
				     "%s: %d: page %d %s\n",
				     __func__, alloc->pid, i,
				     on_lru ? "on lru" : "active");
			binder_free_page(page);
			page_count++;
		}
	}
	mutex_unlock(&alloc->mutex);
	kvfree(alloc->pages);
	if (alloc->mm)
		mmdrop(alloc->mm);

	binder_alloc_debug(BINDER_DEBUG_OPEN_CLOSE,
		     "%s: %d buffers %d, pages %d\n",
		     __func__, alloc->pid, buffers, page_count);
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
	struct binder_buffer *buffer;
	struct rb_node *n;

	mutex_lock(&alloc->mutex);
	for (n = rb_first(&alloc->allocated_buffers); n; n = rb_next(n)) {
		buffer = rb_entry(n, struct binder_buffer, rb_node);
		seq_printf(m, "  buffer %d: %lx size %zd:%zd:%zd %s\n",
			   buffer->debug_id,
			   buffer->user_data - alloc->vm_start,
			   buffer->data_size, buffer->offsets_size,
			   buffer->extra_buffers_size,
			   buffer->transaction ? "active" : "delivered");
	}
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
	struct page *page;
	int i;
	int active = 0;
	int lru = 0;
	int free = 0;

	mutex_lock(&alloc->mutex);
	/*
	 * Make sure the binder_alloc is fully initialized, otherwise we might
	 * read inconsistent state.
	 */
	if (binder_alloc_is_mapped(alloc)) {
		for (i = 0; i < alloc->buffer_size / PAGE_SIZE; i++) {
			page = binder_get_installed_page(alloc, i);
			if (!page)
				free++;
			else if (list_empty(page_to_lru(page)))
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
 * Clears alloc->mapped to prevent new incoming transactions from
 * allocating more buffers.
 */
void binder_alloc_vma_close(struct binder_alloc *alloc)
{
	binder_alloc_set_mapped(alloc, false);
}

/**
 * binder_alloc_free_page() - shrinker callback to free pages
 * @item:   item to free
 * @lru:    list_lru instance of the item
 * @cb_arg: callback argument
 *
 * Called from list_lru_walk() in binder_shrink_scan() to free
 * up pages when the system is under memory pressure.
 */
enum lru_status binder_alloc_free_page(struct list_head *item,
				       struct list_lru_one *lru,
				       void *cb_arg)
	__must_hold(&lru->lock)
{
	struct binder_shrinker_mdata *mdata = container_of(item, typeof(*mdata), lru);
	struct binder_alloc *alloc = mdata->alloc;
	struct mm_struct *mm = alloc->mm;
	struct vm_area_struct *vma;
	struct page *page_to_free;
	unsigned long page_addr;
	int mm_locked = 0;
	size_t index;

	if (!mmget_not_zero(mm))
		goto err_mmget;

	index = mdata->page_index;
	page_addr = alloc->vm_start + index * PAGE_SIZE;

	/* attempt per-vma lock first */
	vma = lock_vma_under_rcu(mm, page_addr);
	if (!vma) {
		/* fall back to mmap_lock */
		if (!mmap_read_trylock(mm))
			goto err_mmap_read_lock_failed;
		mm_locked = 1;
		vma = vma_lookup(mm, page_addr);
	}

	if (!mutex_trylock(&alloc->mutex))
		goto err_get_alloc_mutex_failed;

	/*
	 * Since a binder_alloc can only be mapped once, we ensure
	 * the vma corresponds to this mapping by checking whether
	 * the binder_alloc is still mapped.
	 */
	if (vma && !binder_alloc_is_mapped(alloc))
		goto err_invalid_vma;

	trace_binder_unmap_kernel_start(alloc, index);

	page_to_free = alloc->pages[index];
	binder_set_installed_page(alloc, index, NULL);

	trace_binder_unmap_kernel_end(alloc, index);

	list_lru_isolate(lru, item);
	spin_unlock(&lru->lock);

	if (vma) {
		trace_binder_unmap_user_start(alloc, index);

		zap_page_range_single(vma, page_addr, PAGE_SIZE, NULL);

		trace_binder_unmap_user_end(alloc, index);
	}

	mutex_unlock(&alloc->mutex);
	if (mm_locked)
		mmap_read_unlock(mm);
	else
		vma_end_read(vma);
	mmput_async(mm);
	binder_free_page(page_to_free);

	return LRU_REMOVED_RETRY;

err_invalid_vma:
	mutex_unlock(&alloc->mutex);
err_get_alloc_mutex_failed:
	if (mm_locked)
		mmap_read_unlock(mm);
	else
		vma_end_read(vma);
err_mmap_read_lock_failed:
	mmput_async(mm);
err_mmget:
	return LRU_SKIP;
}

static unsigned long
binder_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	return list_lru_count(&binder_freelist);
}

static unsigned long
binder_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	return list_lru_walk(&binder_freelist, binder_alloc_free_page,
			    NULL, sc->nr_to_scan);
}

static struct shrinker *binder_shrinker;

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
	int ret;

	ret = list_lru_init(&binder_freelist);
	if (ret)
		return ret;

	binder_shrinker = shrinker_alloc(0, "android-binder");
	if (!binder_shrinker) {
		list_lru_destroy(&binder_freelist);
		return -ENOMEM;
	}

	binder_shrinker->count_objects = binder_shrink_count;
	binder_shrinker->scan_objects = binder_shrink_scan;

	shrinker_register(binder_shrinker);

	return 0;
}

void binder_alloc_shrinker_exit(void)
{
	shrinker_free(binder_shrinker);
	list_lru_destroy(&binder_freelist);
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
