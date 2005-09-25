/**
 * \file drm_memory.h
 * Memory management wrappers for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Thu Feb  4 14:00:34 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/config.h>
#include <linux/highmem.h>
#include <linux/vmalloc.h>
#include "drmP.h"

/**
 * Cut down version of drm_memory_debug.h, which used to be called
 * drm_memory.h.
 */

#if __OS_HAS_AGP

#include <linux/vmalloc.h>

#ifdef HAVE_PAGE_AGP
#include <asm/agp.h>
#else
# ifdef __powerpc__
#  define PAGE_AGP	__pgprot(_PAGE_KERNEL | _PAGE_NO_CACHE)
# else
#  define PAGE_AGP	PAGE_KERNEL
# endif
#endif

/*
 * Find the drm_map that covers the range [offset, offset+size).
 */
static inline drm_map_t *drm_lookup_map(unsigned long offset,
					unsigned long size, drm_device_t * dev)
{
	struct list_head *list;
	drm_map_list_t *r_list;
	drm_map_t *map;

	list_for_each(list, &dev->maplist->head) {
		r_list = (drm_map_list_t *) list;
		map = r_list->map;
		if (!map)
			continue;
		if (map->offset <= offset
		    && (offset + size) <= (map->offset + map->size))
			return map;
	}
	return NULL;
}

static inline void *agp_remap(unsigned long offset, unsigned long size,
			      drm_device_t * dev)
{
	unsigned long *phys_addr_map, i, num_pages =
	    PAGE_ALIGN(size) / PAGE_SIZE;
	struct drm_agp_mem *agpmem;
	struct page **page_map;
	void *addr;

	size = PAGE_ALIGN(size);

#ifdef __alpha__
	offset -= dev->hose->mem_space->start;
#endif

	for (agpmem = dev->agp->memory; agpmem; agpmem = agpmem->next)
		if (agpmem->bound <= offset
		    && (agpmem->bound + (agpmem->pages << PAGE_SHIFT)) >=
		    (offset + size))
			break;
	if (!agpmem)
		return NULL;

	/*
	 * OK, we're mapping AGP space on a chipset/platform on which memory accesses by
	 * the CPU do not get remapped by the GART.  We fix this by using the kernel's
	 * page-table instead (that's probably faster anyhow...).
	 */
	/* note: use vmalloc() because num_pages could be large... */
	page_map = vmalloc(num_pages * sizeof(struct page *));
	if (!page_map)
		return NULL;

	phys_addr_map =
	    agpmem->memory->memory + (offset - agpmem->bound) / PAGE_SIZE;
	for (i = 0; i < num_pages; ++i)
		page_map[i] = pfn_to_page(phys_addr_map[i] >> PAGE_SHIFT);
	addr = vmap(page_map, num_pages, VM_IOREMAP, PAGE_AGP);
	vfree(page_map);

	return addr;
}

static inline unsigned long drm_follow_page(void *vaddr)
{
	pgd_t *pgd = pgd_offset_k((unsigned long)vaddr);
	pud_t *pud = pud_offset(pgd, (unsigned long)vaddr);
	pmd_t *pmd = pmd_offset(pud, (unsigned long)vaddr);
	pte_t *ptep = pte_offset_kernel(pmd, (unsigned long)vaddr);
	return pte_pfn(*ptep) << PAGE_SHIFT;
}

#else				/* __OS_HAS_AGP */

static inline drm_map_t *drm_lookup_map(unsigned long offset,
					unsigned long size, drm_device_t * dev)
{
	return NULL;
}

static inline void *agp_remap(unsigned long offset, unsigned long size,
			      drm_device_t * dev)
{
	return NULL;
}

static inline unsigned long drm_follow_page(void *vaddr)
{
	return 0;
}

#endif

static inline void *drm_ioremap(unsigned long offset, unsigned long size,
				drm_device_t * dev)
{
	if (drm_core_has_AGP(dev) && dev->agp && dev->agp->cant_use_aperture) {
		drm_map_t *map = drm_lookup_map(offset, size, dev);

		if (map && map->type == _DRM_AGP)
			return agp_remap(offset, size, dev);
	}
	return ioremap(offset, size);
}

static inline void *drm_ioremap_nocache(unsigned long offset,
					unsigned long size, drm_device_t * dev)
{
	if (drm_core_has_AGP(dev) && dev->agp && dev->agp->cant_use_aperture) {
		drm_map_t *map = drm_lookup_map(offset, size, dev);

		if (map && map->type == _DRM_AGP)
			return agp_remap(offset, size, dev);
	}
	return ioremap_nocache(offset, size);
}

static inline void drm_ioremapfree(void *pt, unsigned long size,
				   drm_device_t * dev)
{
	/*
	 * This is a bit ugly.  It would be much cleaner if the DRM API would use separate
	 * routines for handling mappings in the AGP space.  Hopefully this can be done in
	 * a future revision of the interface...
	 */
	if (drm_core_has_AGP(dev) && dev->agp && dev->agp->cant_use_aperture
	    && ((unsigned long)pt >= VMALLOC_START
		&& (unsigned long)pt < VMALLOC_END)) {
		unsigned long offset;
		drm_map_t *map;

		offset = drm_follow_page(pt) | ((unsigned long)pt & ~PAGE_MASK);
		map = drm_lookup_map(offset, size, dev);
		if (map && map->type == _DRM_AGP) {
			vunmap(pt);
			return;
		}
	}

	iounmap(pt);
}
