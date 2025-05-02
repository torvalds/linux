/* SPDX-License-Identifier: GPL-2.0 OR MIT */
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

#define pr_fmt(fmt) "[TTM] " fmt

#include <linux/cc_platform.h>
#include <linux/debugfs.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/shmem_fs.h>
#include <drm/drm_cache.h>
#include <drm/drm_device.h>
#include <drm/drm_util.h>
#include <drm/ttm/ttm_backup.h>
#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_tt.h>

#include "ttm_module.h"

static unsigned long ttm_pages_limit;

MODULE_PARM_DESC(pages_limit, "Limit for the allocated pages");
module_param_named(pages_limit, ttm_pages_limit, ulong, 0644);

static unsigned long ttm_dma32_pages_limit;

MODULE_PARM_DESC(dma32_pages_limit, "Limit for the allocated DMA32 pages");
module_param_named(dma32_pages_limit, ttm_dma32_pages_limit, ulong, 0644);

static atomic_long_t ttm_pages_allocated;
static atomic_long_t ttm_dma32_pages_allocated;

/*
 * Allocates a ttm structure for the given BO.
 */
int ttm_tt_create(struct ttm_buffer_object *bo, bool zero_alloc)
{
	struct ttm_device *bdev = bo->bdev;
	struct drm_device *ddev = bo->base.dev;
	uint32_t page_flags = 0;

	dma_resv_assert_held(bo->base.resv);

	if (bo->ttm)
		return 0;

	switch (bo->type) {
	case ttm_bo_type_device:
		if (zero_alloc)
			page_flags |= TTM_TT_FLAG_ZERO_ALLOC;
		break;
	case ttm_bo_type_kernel:
		break;
	case ttm_bo_type_sg:
		page_flags |= TTM_TT_FLAG_EXTERNAL;
		break;
	default:
		pr_err("Illegal buffer object type\n");
		return -EINVAL;
	}
	/*
	 * When using dma_alloc_coherent with memory encryption the
	 * mapped TT pages need to be decrypted or otherwise the drivers
	 * will end up sending encrypted mem to the gpu.
	 */
	if (bdev->pool.use_dma_alloc && cc_platform_has(CC_ATTR_GUEST_MEM_ENCRYPT)) {
		page_flags |= TTM_TT_FLAG_DECRYPTED;
		drm_info_once(ddev, "TT memory decryption enabled.");
	}

	bo->ttm = bdev->funcs->ttm_tt_create(bo, page_flags);
	if (unlikely(bo->ttm == NULL))
		return -ENOMEM;

	WARN_ON(bo->ttm->page_flags & TTM_TT_FLAG_EXTERNAL_MAPPABLE &&
		!(bo->ttm->page_flags & TTM_TT_FLAG_EXTERNAL));

	return 0;
}
EXPORT_SYMBOL_FOR_TESTS_ONLY(ttm_tt_create);

/*
 * Allocates storage for pointers to the pages that back the ttm.
 */
static int ttm_tt_alloc_page_directory(struct ttm_tt *ttm)
{
	ttm->pages = kvcalloc(ttm->num_pages, sizeof(void*), GFP_KERNEL);
	if (!ttm->pages)
		return -ENOMEM;

	return 0;
}

static int ttm_dma_tt_alloc_page_directory(struct ttm_tt *ttm)
{
	ttm->pages = kvcalloc(ttm->num_pages, sizeof(*ttm->pages) +
			      sizeof(*ttm->dma_address), GFP_KERNEL);
	if (!ttm->pages)
		return -ENOMEM;

	ttm->dma_address = (void *)(ttm->pages + ttm->num_pages);
	return 0;
}

static int ttm_sg_tt_alloc_page_directory(struct ttm_tt *ttm)
{
	ttm->dma_address = kvcalloc(ttm->num_pages, sizeof(*ttm->dma_address),
				    GFP_KERNEL);
	if (!ttm->dma_address)
		return -ENOMEM;

	return 0;
}

void ttm_tt_destroy(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	bdev->funcs->ttm_tt_destroy(bdev, ttm);
}
EXPORT_SYMBOL_FOR_TESTS_ONLY(ttm_tt_destroy);

static void ttm_tt_init_fields(struct ttm_tt *ttm,
			       struct ttm_buffer_object *bo,
			       uint32_t page_flags,
			       enum ttm_caching caching,
			       unsigned long extra_pages)
{
	ttm->num_pages = (PAGE_ALIGN(bo->base.size) >> PAGE_SHIFT) + extra_pages;
	ttm->page_flags = page_flags;
	ttm->dma_address = NULL;
	ttm->swap_storage = NULL;
	ttm->sg = bo->sg;
	ttm->caching = caching;
	ttm->restore = NULL;
	ttm->backup = NULL;
}

int ttm_tt_init(struct ttm_tt *ttm, struct ttm_buffer_object *bo,
		uint32_t page_flags, enum ttm_caching caching,
		unsigned long extra_pages)
{
	ttm_tt_init_fields(ttm, bo, page_flags, caching, extra_pages);

	if (ttm_tt_alloc_page_directory(ttm)) {
		pr_err("Failed allocating page table\n");
		return -ENOMEM;
	}
	return 0;
}
EXPORT_SYMBOL(ttm_tt_init);

void ttm_tt_fini(struct ttm_tt *ttm)
{
	WARN_ON(ttm->page_flags & TTM_TT_FLAG_PRIV_POPULATED);

	if (ttm->swap_storage)
		fput(ttm->swap_storage);
	ttm->swap_storage = NULL;

	if (ttm_tt_is_backed_up(ttm))
		ttm_pool_drop_backed_up(ttm);
	if (ttm->backup) {
		ttm_backup_fini(ttm->backup);
		ttm->backup = NULL;
	}

	if (ttm->pages)
		kvfree(ttm->pages);
	else
		kvfree(ttm->dma_address);
	ttm->pages = NULL;
	ttm->dma_address = NULL;
}
EXPORT_SYMBOL(ttm_tt_fini);

int ttm_sg_tt_init(struct ttm_tt *ttm, struct ttm_buffer_object *bo,
		   uint32_t page_flags, enum ttm_caching caching)
{
	int ret;

	ttm_tt_init_fields(ttm, bo, page_flags, caching, 0);

	if (page_flags & TTM_TT_FLAG_EXTERNAL)
		ret = ttm_sg_tt_alloc_page_directory(ttm);
	else
		ret = ttm_dma_tt_alloc_page_directory(ttm);
	if (ret) {
		pr_err("Failed allocating page table\n");
		return -ENOMEM;
	}
	return 0;
}
EXPORT_SYMBOL(ttm_sg_tt_init);

int ttm_tt_swapin(struct ttm_tt *ttm)
{
	struct address_space *swap_space;
	struct file *swap_storage;
	struct page *from_page;
	struct page *to_page;
	gfp_t gfp_mask;
	int i, ret;

	swap_storage = ttm->swap_storage;
	BUG_ON(swap_storage == NULL);

	swap_space = swap_storage->f_mapping;
	gfp_mask = mapping_gfp_mask(swap_space);

	for (i = 0; i < ttm->num_pages; ++i) {
		from_page = shmem_read_mapping_page_gfp(swap_space, i,
							gfp_mask);
		if (IS_ERR(from_page)) {
			ret = PTR_ERR(from_page);
			goto out_err;
		}
		to_page = ttm->pages[i];
		if (unlikely(to_page == NULL)) {
			ret = -ENOMEM;
			goto out_err;
		}

		copy_highpage(to_page, from_page);
		put_page(from_page);
	}

	fput(swap_storage);
	ttm->swap_storage = NULL;
	ttm->page_flags &= ~TTM_TT_FLAG_SWAPPED;

	return 0;

out_err:
	return ret;
}
EXPORT_SYMBOL_FOR_TESTS_ONLY(ttm_tt_swapin);

/**
 * ttm_tt_backup() - Helper to back up a struct ttm_tt.
 * @bdev: The TTM device.
 * @tt: The struct ttm_tt.
 * @flags: Flags that govern the backup behaviour.
 *
 * Update the page accounting and call ttm_pool_shrink_tt to free pages
 * or back them up.
 *
 * Return: Number of pages freed or swapped out, or negative error code on
 * error.
 */
long ttm_tt_backup(struct ttm_device *bdev, struct ttm_tt *tt,
		   const struct ttm_backup_flags flags)
{
	long ret;

	if (WARN_ON(IS_ERR_OR_NULL(tt->backup)))
		return 0;

	ret = ttm_pool_backup(&bdev->pool, tt, &flags);
	if (ret > 0) {
		tt->page_flags &= ~TTM_TT_FLAG_PRIV_POPULATED;
		tt->page_flags |= TTM_TT_FLAG_BACKED_UP;
	}

	return ret;
}

int ttm_tt_restore(struct ttm_device *bdev, struct ttm_tt *tt,
		   const struct ttm_operation_ctx *ctx)
{
	int ret = ttm_pool_restore_and_alloc(&bdev->pool, tt, ctx);

	if (ret)
		return ret;

	tt->page_flags &= ~TTM_TT_FLAG_BACKED_UP;

	return 0;
}
EXPORT_SYMBOL(ttm_tt_restore);

/**
 * ttm_tt_swapout - swap out tt object
 *
 * @bdev: TTM device structure.
 * @ttm: The struct ttm_tt.
 * @gfp_flags: Flags to use for memory allocation.
 *
 * Swapout a TT object to a shmem_file, return number of pages swapped out or
 * negative error code.
 */
int ttm_tt_swapout(struct ttm_device *bdev, struct ttm_tt *ttm,
		   gfp_t gfp_flags)
{
	loff_t size = (loff_t)ttm->num_pages << PAGE_SHIFT;
	struct address_space *swap_space;
	struct file *swap_storage;
	struct page *from_page;
	struct page *to_page;
	int i, ret;

	swap_storage = shmem_file_setup("ttm swap", size, 0);
	if (IS_ERR(swap_storage)) {
		pr_err("Failed allocating swap storage\n");
		return PTR_ERR(swap_storage);
	}

	swap_space = swap_storage->f_mapping;
	gfp_flags &= mapping_gfp_mask(swap_space);

	for (i = 0; i < ttm->num_pages; ++i) {
		from_page = ttm->pages[i];
		if (unlikely(from_page == NULL))
			continue;

		to_page = shmem_read_mapping_page_gfp(swap_space, i, gfp_flags);
		if (IS_ERR(to_page)) {
			ret = PTR_ERR(to_page);
			goto out_err;
		}
		copy_highpage(to_page, from_page);
		set_page_dirty(to_page);
		mark_page_accessed(to_page);
		put_page(to_page);
	}

	ttm_tt_unpopulate(bdev, ttm);
	ttm->swap_storage = swap_storage;
	ttm->page_flags |= TTM_TT_FLAG_SWAPPED;

	return ttm->num_pages;

out_err:
	fput(swap_storage);

	return ret;
}
EXPORT_SYMBOL_FOR_TESTS_ONLY(ttm_tt_swapout);

int ttm_tt_populate(struct ttm_device *bdev,
		    struct ttm_tt *ttm, struct ttm_operation_ctx *ctx)
{
	int ret;

	if (!ttm)
		return -EINVAL;

	if (ttm_tt_is_populated(ttm))
		return 0;

	if (!(ttm->page_flags & TTM_TT_FLAG_EXTERNAL)) {
		atomic_long_add(ttm->num_pages, &ttm_pages_allocated);
		if (bdev->pool.use_dma32)
			atomic_long_add(ttm->num_pages,
					&ttm_dma32_pages_allocated);
	}

	while (atomic_long_read(&ttm_pages_allocated) > ttm_pages_limit ||
	       atomic_long_read(&ttm_dma32_pages_allocated) >
	       ttm_dma32_pages_limit) {

		ret = ttm_global_swapout(ctx, GFP_KERNEL);
		if (ret == 0)
			break;
		if (ret < 0)
			goto error;
	}

	if (bdev->funcs->ttm_tt_populate)
		ret = bdev->funcs->ttm_tt_populate(bdev, ttm, ctx);
	else
		ret = ttm_pool_alloc(&bdev->pool, ttm, ctx);
	if (ret)
		goto error;

	ttm->page_flags |= TTM_TT_FLAG_PRIV_POPULATED;
	ttm->page_flags &= ~TTM_TT_FLAG_BACKED_UP;
	if (unlikely(ttm->page_flags & TTM_TT_FLAG_SWAPPED)) {
		ret = ttm_tt_swapin(ttm);
		if (unlikely(ret != 0)) {
			ttm_tt_unpopulate(bdev, ttm);
			return ret;
		}
	}

	return 0;

error:
	if (!(ttm->page_flags & TTM_TT_FLAG_EXTERNAL)) {
		atomic_long_sub(ttm->num_pages, &ttm_pages_allocated);
		if (bdev->pool.use_dma32)
			atomic_long_sub(ttm->num_pages,
					&ttm_dma32_pages_allocated);
	}
	return ret;
}

#if IS_ENABLED(CONFIG_DRM_TTM_KUNIT_TEST)
EXPORT_SYMBOL(ttm_tt_populate);
#endif

void ttm_tt_unpopulate(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	if (!ttm_tt_is_populated(ttm))
		return;

	if (bdev->funcs->ttm_tt_unpopulate)
		bdev->funcs->ttm_tt_unpopulate(bdev, ttm);
	else
		ttm_pool_free(&bdev->pool, ttm);

	if (!(ttm->page_flags & TTM_TT_FLAG_EXTERNAL)) {
		atomic_long_sub(ttm->num_pages, &ttm_pages_allocated);
		if (bdev->pool.use_dma32)
			atomic_long_sub(ttm->num_pages,
					&ttm_dma32_pages_allocated);
	}

	ttm->page_flags &= ~TTM_TT_FLAG_PRIV_POPULATED;
}
EXPORT_SYMBOL_FOR_TESTS_ONLY(ttm_tt_unpopulate);

#ifdef CONFIG_DEBUG_FS

/* Test the shrinker functions and dump the result */
static int ttm_tt_debugfs_shrink_show(struct seq_file *m, void *data)
{
	struct ttm_operation_ctx ctx = { false, false };

	seq_printf(m, "%d\n", ttm_global_swapout(&ctx, GFP_KERNEL));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ttm_tt_debugfs_shrink);

#endif


/*
 * ttm_tt_mgr_init - register with the MM shrinker
 *
 * Register with the MM shrinker for swapping out BOs.
 */
void ttm_tt_mgr_init(unsigned long num_pages, unsigned long num_dma32_pages)
{
#ifdef CONFIG_DEBUG_FS
	debugfs_create_file("tt_shrink", 0400, ttm_debugfs_root, NULL,
			    &ttm_tt_debugfs_shrink_fops);
#endif

	if (!ttm_pages_limit)
		ttm_pages_limit = num_pages;

	if (!ttm_dma32_pages_limit)
		ttm_dma32_pages_limit = num_dma32_pages;
}

static void ttm_kmap_iter_tt_map_local(struct ttm_kmap_iter *iter,
				       struct iosys_map *dmap,
				       pgoff_t i)
{
	struct ttm_kmap_iter_tt *iter_tt =
		container_of(iter, typeof(*iter_tt), base);

	iosys_map_set_vaddr(dmap, kmap_local_page_prot(iter_tt->tt->pages[i],
						       iter_tt->prot));
}

static void ttm_kmap_iter_tt_unmap_local(struct ttm_kmap_iter *iter,
					 struct iosys_map *map)
{
	kunmap_local(map->vaddr);
}

static const struct ttm_kmap_iter_ops ttm_kmap_iter_tt_ops = {
	.map_local = ttm_kmap_iter_tt_map_local,
	.unmap_local = ttm_kmap_iter_tt_unmap_local,
	.maps_tt = true,
};

/**
 * ttm_kmap_iter_tt_init - Initialize a struct ttm_kmap_iter_tt
 * @iter_tt: The struct ttm_kmap_iter_tt to initialize.
 * @tt: Struct ttm_tt holding page pointers of the struct ttm_resource.
 *
 * Return: Pointer to the embedded struct ttm_kmap_iter.
 */
struct ttm_kmap_iter *
ttm_kmap_iter_tt_init(struct ttm_kmap_iter_tt *iter_tt,
		      struct ttm_tt *tt)
{
	iter_tt->base.ops = &ttm_kmap_iter_tt_ops;
	iter_tt->tt = tt;
	if (tt)
		iter_tt->prot = ttm_prot_from_caching(tt->caching, PAGE_KERNEL);
	else
		iter_tt->prot = PAGE_KERNEL;

	return &iter_tt->base;
}
EXPORT_SYMBOL(ttm_kmap_iter_tt_init);

unsigned long ttm_tt_pages_limit(void)
{
	return ttm_pages_limit;
}
EXPORT_SYMBOL(ttm_tt_pages_limit);

/**
 * ttm_tt_setup_backup() - Allocate and assign a backup structure for a ttm_tt
 * @tt: The ttm_tt for wich to allocate and assign a backup structure.
 *
 * Assign a backup structure to be used for tt backup. This should
 * typically be done at bo creation, to avoid allocations at shrinking
 * time.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ttm_tt_setup_backup(struct ttm_tt *tt)
{
	struct ttm_backup *backup =
		ttm_backup_shmem_create(((loff_t)tt->num_pages) << PAGE_SHIFT);

	if (WARN_ON_ONCE(!(tt->page_flags & TTM_TT_FLAG_EXTERNAL_MAPPABLE)))
		return -EINVAL;

	if (IS_ERR(backup))
		return PTR_ERR(backup);

	if (tt->backup)
		ttm_backup_fini(tt->backup);

	tt->backup = backup;
	return 0;
}
EXPORT_SYMBOL(ttm_tt_setup_backup);
