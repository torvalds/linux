/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/swap.h>
#include "drm_cache.h"
#include "ttm/ttm_module.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_placement.h"

static int ttm_tt_swapin(struct ttm_tt *ttm);

/**
 * Allocates storage for pointers to the pages that back the ttm.
 *
 * Uses kmalloc if possible. Otherwise falls back to vmalloc.
 */
static void ttm_tt_alloc_page_directory(struct ttm_tt *ttm)
{
	unsigned long size = ttm->num_pages * sizeof(*ttm->pages);
	ttm->pages = NULL;

	if (size <= PAGE_SIZE)
		ttm->pages = kzalloc(size, GFP_KERNEL);

	if (!ttm->pages) {
		ttm->pages = vmalloc_user(size);
		if (ttm->pages)
			ttm->page_flags |= TTM_PAGE_FLAG_VMALLOC;
	}
}

static void ttm_tt_free_page_directory(struct ttm_tt *ttm)
{
	if (ttm->page_flags & TTM_PAGE_FLAG_VMALLOC) {
		vfree(ttm->pages);
		ttm->page_flags &= ~TTM_PAGE_FLAG_VMALLOC;
	} else {
		kfree(ttm->pages);
	}
	ttm->pages = NULL;
}

static struct page *ttm_tt_alloc_page(unsigned page_flags)
{
	gfp_t gfp_flags = GFP_USER;

	if (page_flags & TTM_PAGE_FLAG_ZERO_ALLOC)
		gfp_flags |= __GFP_ZERO;

	if (page_flags & TTM_PAGE_FLAG_DMA32)
		gfp_flags |= __GFP_DMA32;
	else
		gfp_flags |= __GFP_HIGHMEM;

	return alloc_page(gfp_flags);
}

static void ttm_tt_free_user_pages(struct ttm_tt *ttm)
{
	int write;
	int dirty;
	struct page *page;
	int i;
	struct ttm_backend *be = ttm->be;

	BUG_ON(!(ttm->page_flags & TTM_PAGE_FLAG_USER));
	write = ((ttm->page_flags & TTM_PAGE_FLAG_WRITE) != 0);
	dirty = ((ttm->page_flags & TTM_PAGE_FLAG_USER_DIRTY) != 0);

	if (be)
		be->func->clear(be);

	for (i = 0; i < ttm->num_pages; ++i) {
		page = ttm->pages[i];
		if (page == NULL)
			continue;

		if (page == ttm->dummy_read_page) {
			BUG_ON(write);
			continue;
		}

		if (write && dirty && !PageReserved(page))
			set_page_dirty_lock(page);

		ttm->pages[i] = NULL;
		ttm_mem_global_free(ttm->glob->mem_glob, PAGE_SIZE);
		put_page(page);
	}
	ttm->state = tt_unpopulated;
	ttm->first_himem_page = ttm->num_pages;
	ttm->last_lomem_page = -1;
}

static struct page *__ttm_tt_get_page(struct ttm_tt *ttm, int index)
{
	struct page *p;
	struct ttm_mem_global *mem_glob = ttm->glob->mem_glob;
	int ret;

	while (NULL == (p = ttm->pages[index])) {
		p = ttm_tt_alloc_page(ttm->page_flags);

		if (!p)
			return NULL;

		ret = ttm_mem_global_alloc_page(mem_glob, p, false, false);
		if (unlikely(ret != 0))
			goto out_err;

		if (PageHighMem(p))
			ttm->pages[--ttm->first_himem_page] = p;
		else
			ttm->pages[++ttm->last_lomem_page] = p;
	}
	return p;
out_err:
	put_page(p);
	return NULL;
}

struct page *ttm_tt_get_page(struct ttm_tt *ttm, int index)
{
	int ret;

	if (unlikely(ttm->page_flags & TTM_PAGE_FLAG_SWAPPED)) {
		ret = ttm_tt_swapin(ttm);
		if (unlikely(ret != 0))
			return NULL;
	}
	return __ttm_tt_get_page(ttm, index);
}

int ttm_tt_populate(struct ttm_tt *ttm)
{
	struct page *page;
	unsigned long i;
	struct ttm_backend *be;
	int ret;

	if (ttm->state != tt_unpopulated)
		return 0;

	if (unlikely(ttm->page_flags & TTM_PAGE_FLAG_SWAPPED)) {
		ret = ttm_tt_swapin(ttm);
		if (unlikely(ret != 0))
			return ret;
	}

	be = ttm->be;

	for (i = 0; i < ttm->num_pages; ++i) {
		page = __ttm_tt_get_page(ttm, i);
		if (!page)
			return -ENOMEM;
	}

	be->func->populate(be, ttm->num_pages, ttm->pages,
			   ttm->dummy_read_page);
	ttm->state = tt_unbound;
	return 0;
}

#ifdef CONFIG_X86
static inline int ttm_tt_set_page_caching(struct page *p,
					  enum ttm_caching_state c_state)
{
	if (PageHighMem(p))
		return 0;

	switch (c_state) {
	case tt_cached:
		return set_pages_wb(p, 1);
	case tt_wc:
	    return set_memory_wc((unsigned long) page_address(p), 1);
	default:
		return set_pages_uc(p, 1);
	}
}
#else /* CONFIG_X86 */
static inline int ttm_tt_set_page_caching(struct page *p,
					  enum ttm_caching_state c_state)
{
	return 0;
}
#endif /* CONFIG_X86 */

/*
 * Change caching policy for the linear kernel map
 * for range of pages in a ttm.
 */

static int ttm_tt_set_caching(struct ttm_tt *ttm,
			      enum ttm_caching_state c_state)
{
	int i, j;
	struct page *cur_page;
	int ret;

	if (ttm->caching_state == c_state)
		return 0;

	if (c_state != tt_cached) {
		ret = ttm_tt_populate(ttm);
		if (unlikely(ret != 0))
			return ret;
	}

	if (ttm->caching_state == tt_cached)
		drm_clflush_pages(ttm->pages, ttm->num_pages);

	for (i = 0; i < ttm->num_pages; ++i) {
		cur_page = ttm->pages[i];
		if (likely(cur_page != NULL)) {
			ret = ttm_tt_set_page_caching(cur_page, c_state);
			if (unlikely(ret != 0))
				goto out_err;
		}
	}

	ttm->caching_state = c_state;

	return 0;

out_err:
	for (j = 0; j < i; ++j) {
		cur_page = ttm->pages[j];
		if (likely(cur_page != NULL)) {
			(void)ttm_tt_set_page_caching(cur_page,
						      ttm->caching_state);
		}
	}

	return ret;
}

int ttm_tt_set_placement_caching(struct ttm_tt *ttm, uint32_t placement)
{
	enum ttm_caching_state state;

	if (placement & TTM_PL_FLAG_WC)
		state = tt_wc;
	else if (placement & TTM_PL_FLAG_UNCACHED)
		state = tt_uncached;
	else
		state = tt_cached;

	return ttm_tt_set_caching(ttm, state);
}

static void ttm_tt_free_alloced_pages(struct ttm_tt *ttm)
{
	int i;
	struct page *cur_page;
	struct ttm_backend *be = ttm->be;

	if (be)
		be->func->clear(be);
	(void)ttm_tt_set_caching(ttm, tt_cached);
	for (i = 0; i < ttm->num_pages; ++i) {
		cur_page = ttm->pages[i];
		ttm->pages[i] = NULL;
		if (cur_page) {
			if (page_count(cur_page) != 1)
				printk(KERN_ERR TTM_PFX
				       "Erroneous page count. "
				       "Leaking pages.\n");
			ttm_mem_global_free_page(ttm->glob->mem_glob,
						 cur_page);
			__free_page(cur_page);
		}
	}
	ttm->state = tt_unpopulated;
	ttm->first_himem_page = ttm->num_pages;
	ttm->last_lomem_page = -1;
}

void ttm_tt_destroy(struct ttm_tt *ttm)
{
	struct ttm_backend *be;

	if (unlikely(ttm == NULL))
		return;

	be = ttm->be;
	if (likely(be != NULL)) {
		be->func->destroy(be);
		ttm->be = NULL;
	}

	if (likely(ttm->pages != NULL)) {
		if (ttm->page_flags & TTM_PAGE_FLAG_USER)
			ttm_tt_free_user_pages(ttm);
		else
			ttm_tt_free_alloced_pages(ttm);

		ttm_tt_free_page_directory(ttm);
	}

	if (!(ttm->page_flags & TTM_PAGE_FLAG_PERSISTANT_SWAP) &&
	    ttm->swap_storage)
		fput(ttm->swap_storage);

	kfree(ttm);
}

int ttm_tt_set_user(struct ttm_tt *ttm,
		    struct task_struct *tsk,
		    unsigned long start, unsigned long num_pages)
{
	struct mm_struct *mm = tsk->mm;
	int ret;
	int write = (ttm->page_flags & TTM_PAGE_FLAG_WRITE) != 0;
	struct ttm_mem_global *mem_glob = ttm->glob->mem_glob;

	BUG_ON(num_pages != ttm->num_pages);
	BUG_ON((ttm->page_flags & TTM_PAGE_FLAG_USER) == 0);

	/**
	 * Account user pages as lowmem pages for now.
	 */

	ret = ttm_mem_global_alloc(mem_glob, num_pages * PAGE_SIZE,
				   false, false);
	if (unlikely(ret != 0))
		return ret;

	down_read(&mm->mmap_sem);
	ret = get_user_pages(tsk, mm, start, num_pages,
			     write, 0, ttm->pages, NULL);
	up_read(&mm->mmap_sem);

	if (ret != num_pages && write) {
		ttm_tt_free_user_pages(ttm);
		ttm_mem_global_free(mem_glob, num_pages * PAGE_SIZE);
		return -ENOMEM;
	}

	ttm->tsk = tsk;
	ttm->start = start;
	ttm->state = tt_unbound;

	return 0;
}

struct ttm_tt *ttm_tt_create(struct ttm_bo_device *bdev, unsigned long size,
			     uint32_t page_flags, struct page *dummy_read_page)
{
	struct ttm_bo_driver *bo_driver = bdev->driver;
	struct ttm_tt *ttm;

	if (!bo_driver)
		return NULL;

	ttm = kzalloc(sizeof(*ttm), GFP_KERNEL);
	if (!ttm)
		return NULL;

	ttm->glob = bdev->glob;
	ttm->num_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	ttm->first_himem_page = ttm->num_pages;
	ttm->last_lomem_page = -1;
	ttm->caching_state = tt_cached;
	ttm->page_flags = page_flags;

	ttm->dummy_read_page = dummy_read_page;

	ttm_tt_alloc_page_directory(ttm);
	if (!ttm->pages) {
		ttm_tt_destroy(ttm);
		printk(KERN_ERR TTM_PFX "Failed allocating page table\n");
		return NULL;
	}
	ttm->be = bo_driver->create_ttm_backend_entry(bdev);
	if (!ttm->be) {
		ttm_tt_destroy(ttm);
		printk(KERN_ERR TTM_PFX "Failed creating ttm backend entry\n");
		return NULL;
	}
	ttm->state = tt_unpopulated;
	return ttm;
}

void ttm_tt_unbind(struct ttm_tt *ttm)
{
	int ret;
	struct ttm_backend *be = ttm->be;

	if (ttm->state == tt_bound) {
		ret = be->func->unbind(be);
		BUG_ON(ret);
		ttm->state = tt_unbound;
	}
}

int ttm_tt_bind(struct ttm_tt *ttm, struct ttm_mem_reg *bo_mem)
{
	int ret = 0;
	struct ttm_backend *be;

	if (!ttm)
		return -EINVAL;

	if (ttm->state == tt_bound)
		return 0;

	be = ttm->be;

	ret = ttm_tt_populate(ttm);
	if (ret)
		return ret;

	ret = be->func->bind(be, bo_mem);
	if (ret) {
		printk(KERN_ERR TTM_PFX "Couldn't bind backend.\n");
		return ret;
	}

	ttm->state = tt_bound;

	if (ttm->page_flags & TTM_PAGE_FLAG_USER)
		ttm->page_flags |= TTM_PAGE_FLAG_USER_DIRTY;
	return 0;
}
EXPORT_SYMBOL(ttm_tt_bind);

static int ttm_tt_swapin(struct ttm_tt *ttm)
{
	struct address_space *swap_space;
	struct file *swap_storage;
	struct page *from_page;
	struct page *to_page;
	void *from_virtual;
	void *to_virtual;
	int i;
	int ret;

	if (ttm->page_flags & TTM_PAGE_FLAG_USER) {
		ret = ttm_tt_set_user(ttm, ttm->tsk, ttm->start,
				      ttm->num_pages);
		if (unlikely(ret != 0))
			return ret;

		ttm->page_flags &= ~TTM_PAGE_FLAG_SWAPPED;
		return 0;
	}

	swap_storage = ttm->swap_storage;
	BUG_ON(swap_storage == NULL);

	swap_space = swap_storage->f_path.dentry->d_inode->i_mapping;

	for (i = 0; i < ttm->num_pages; ++i) {
		from_page = read_mapping_page(swap_space, i, NULL);
		if (IS_ERR(from_page))
			goto out_err;
		to_page = __ttm_tt_get_page(ttm, i);
		if (unlikely(to_page == NULL))
			goto out_err;

		preempt_disable();
		from_virtual = kmap_atomic(from_page, KM_USER0);
		to_virtual = kmap_atomic(to_page, KM_USER1);
		memcpy(to_virtual, from_virtual, PAGE_SIZE);
		kunmap_atomic(to_virtual, KM_USER1);
		kunmap_atomic(from_virtual, KM_USER0);
		preempt_enable();
		page_cache_release(from_page);
	}

	if (!(ttm->page_flags & TTM_PAGE_FLAG_PERSISTANT_SWAP))
		fput(swap_storage);
	ttm->swap_storage = NULL;
	ttm->page_flags &= ~TTM_PAGE_FLAG_SWAPPED;

	return 0;
out_err:
	ttm_tt_free_alloced_pages(ttm);
	return -ENOMEM;
}

int ttm_tt_swapout(struct ttm_tt *ttm, struct file *persistant_swap_storage)
{
	struct address_space *swap_space;
	struct file *swap_storage;
	struct page *from_page;
	struct page *to_page;
	void *from_virtual;
	void *to_virtual;
	int i;

	BUG_ON(ttm->state != tt_unbound && ttm->state != tt_unpopulated);
	BUG_ON(ttm->caching_state != tt_cached);

	/*
	 * For user buffers, just unpin the pages, as there should be
	 * vma references.
	 */

	if (ttm->page_flags & TTM_PAGE_FLAG_USER) {
		ttm_tt_free_user_pages(ttm);
		ttm->page_flags |= TTM_PAGE_FLAG_SWAPPED;
		ttm->swap_storage = NULL;
		return 0;
	}

	if (!persistant_swap_storage) {
		swap_storage = shmem_file_setup("ttm swap",
						ttm->num_pages << PAGE_SHIFT,
						0);
		if (unlikely(IS_ERR(swap_storage))) {
			printk(KERN_ERR "Failed allocating swap storage.\n");
			return -ENOMEM;
		}
	} else
		swap_storage = persistant_swap_storage;

	swap_space = swap_storage->f_path.dentry->d_inode->i_mapping;

	for (i = 0; i < ttm->num_pages; ++i) {
		from_page = ttm->pages[i];
		if (unlikely(from_page == NULL))
			continue;
		to_page = read_mapping_page(swap_space, i, NULL);
		if (unlikely(to_page == NULL))
			goto out_err;

		preempt_disable();
		from_virtual = kmap_atomic(from_page, KM_USER0);
		to_virtual = kmap_atomic(to_page, KM_USER1);
		memcpy(to_virtual, from_virtual, PAGE_SIZE);
		kunmap_atomic(to_virtual, KM_USER1);
		kunmap_atomic(from_virtual, KM_USER0);
		preempt_enable();
		set_page_dirty(to_page);
		mark_page_accessed(to_page);
		page_cache_release(to_page);
	}

	ttm_tt_free_alloced_pages(ttm);
	ttm->swap_storage = swap_storage;
	ttm->page_flags |= TTM_PAGE_FLAG_SWAPPED;
	if (persistant_swap_storage)
		ttm->page_flags |= TTM_PAGE_FLAG_PERSISTANT_SWAP;

	return 0;
out_err:
	if (!persistant_swap_storage)
		fput(swap_storage);

	return -ENOMEM;
}
