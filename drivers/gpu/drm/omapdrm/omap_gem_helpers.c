/*
 * drivers/gpu/drm/omapdrm/omap_gem_helpers.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* temporary copy of drm_gem_{get,put}_pages() until the
 * "drm/gem: add functions to get/put pages" patch is merged..
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/shmem_fs.h>

#include <drm/drmP.h>

/**
 * drm_gem_get_pages - helper to allocate backing pages for a GEM object
 * @obj: obj in question
 * @gfpmask: gfp mask of requested pages
 */
struct page **_drm_gem_get_pages(struct drm_gem_object *obj, gfp_t gfpmask)
{
	struct inode *inode;
	struct address_space *mapping;
	struct page *p, **pages;
	int i, npages;

	/* This is the shared memory object that backs the GEM resource */
	inode = file_inode(obj->filp);
	mapping = inode->i_mapping;

	npages = obj->size >> PAGE_SHIFT;

	pages = drm_malloc_ab(npages, sizeof(struct page *));
	if (pages == NULL)
		return ERR_PTR(-ENOMEM);

	gfpmask |= mapping_gfp_mask(mapping);

	for (i = 0; i < npages; i++) {
		p = shmem_read_mapping_page_gfp(mapping, i, gfpmask);
		if (IS_ERR(p))
			goto fail;
		pages[i] = p;

		/* There is a hypothetical issue w/ drivers that require
		 * buffer memory in the low 4GB.. if the pages are un-
		 * pinned, and swapped out, they can end up swapped back
		 * in above 4GB.  If pages are already in memory, then
		 * shmem_read_mapping_page_gfp will ignore the gfpmask,
		 * even if the already in-memory page disobeys the mask.
		 *
		 * It is only a theoretical issue today, because none of
		 * the devices with this limitation can be populated with
		 * enough memory to trigger the issue.  But this BUG_ON()
		 * is here as a reminder in case the problem with
		 * shmem_read_mapping_page_gfp() isn't solved by the time
		 * it does become a real issue.
		 *
		 * See this thread: http://lkml.org/lkml/2011/7/11/238
		 */
		BUG_ON((gfpmask & __GFP_DMA32) &&
				(page_to_pfn(p) >= 0x00100000UL));
	}

	return pages;

fail:
	while (i--)
		page_cache_release(pages[i]);

	drm_free_large(pages);
	return ERR_CAST(p);
}

/**
 * drm_gem_put_pages - helper to free backing pages for a GEM object
 * @obj: obj in question
 * @pages: pages to free
 */
void _drm_gem_put_pages(struct drm_gem_object *obj, struct page **pages,
		bool dirty, bool accessed)
{
	int i, npages;

	npages = obj->size >> PAGE_SHIFT;

	for (i = 0; i < npages; i++) {
		if (dirty)
			set_page_dirty(pages[i]);

		if (accessed)
			mark_page_accessed(pages[i]);

		/* Undo the reference we took when populating the table */
		page_cache_release(pages[i]);
	}

	drm_free_large(pages);
}

int
_drm_gem_create_mmap_offset_size(struct drm_gem_object *obj, size_t size)
{
	struct drm_device *dev = obj->dev;
	struct drm_gem_mm *mm = dev->mm_private;

	return drm_vma_offset_add(&mm->vma_manager, &obj->vma_node,
				  size / PAGE_SIZE);
}
