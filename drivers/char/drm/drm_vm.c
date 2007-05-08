/**
 * \file drm_vm.c
 * Memory mapping for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Mon Jan  4 08:58:31 1999 by faith@valinux.com
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

#include "drmP.h"
#if defined(__ia64__)
#include <linux/efi.h>
#endif

static void drm_vm_open(struct vm_area_struct *vma);
static void drm_vm_close(struct vm_area_struct *vma);

static pgprot_t drm_io_prot(uint32_t map_type, struct vm_area_struct *vma)
{
	pgprot_t tmp = vm_get_page_prot(vma->vm_flags);

#if defined(__i386__) || defined(__x86_64__)
	if (boot_cpu_data.x86 > 3 && map_type != _DRM_AGP) {
		pgprot_val(tmp) |= _PAGE_PCD;
		pgprot_val(tmp) &= ~_PAGE_PWT;
	}
#elif defined(__powerpc__)
	pgprot_val(tmp) |= _PAGE_NO_CACHE;
	if (map_type == _DRM_REGISTERS)
		pgprot_val(tmp) |= _PAGE_GUARDED;
#endif
#if defined(__ia64__)
	if (efi_range_is_wc(vma->vm_start, vma->vm_end -
				    vma->vm_start))
		tmp = pgprot_writecombine(tmp);
	else
		tmp = pgprot_noncached(tmp);
#endif
	return tmp;
}

/**
 * \c nopage method for AGP virtual memory.
 *
 * \param vma virtual memory area.
 * \param address access address.
 * \return pointer to the page structure.
 *
 * Find the right map and if it's AGP memory find the real physical page to
 * map, get the page, increment the use count and return it.
 */
#if __OS_HAS_AGP
static __inline__ struct page *drm_do_vm_nopage(struct vm_area_struct *vma,
						unsigned long address)
{
	drm_file_t *priv = vma->vm_file->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_map_t *map = NULL;
	drm_map_list_t *r_list;
	drm_hash_item_t *hash;

	/*
	 * Find the right map
	 */
	if (!drm_core_has_AGP(dev))
		goto vm_nopage_error;

	if (!dev->agp || !dev->agp->cant_use_aperture)
		goto vm_nopage_error;

	if (drm_ht_find_item(&dev->map_hash, vma->vm_pgoff, &hash))
		goto vm_nopage_error;

	r_list = drm_hash_entry(hash, drm_map_list_t, hash);
	map = r_list->map;

	if (map && map->type == _DRM_AGP) {
		unsigned long offset = address - vma->vm_start;
		unsigned long baddr = map->offset + offset;
		struct drm_agp_mem *agpmem;
		struct page *page;

#ifdef __alpha__
		/*
		 * Adjust to a bus-relative address
		 */
		baddr -= dev->hose->mem_space->start;
#endif

		/*
		 * It's AGP memory - find the real physical page to map
		 */
		for (agpmem = dev->agp->memory; agpmem; agpmem = agpmem->next) {
			if (agpmem->bound <= baddr &&
			    agpmem->bound + agpmem->pages * PAGE_SIZE > baddr)
				break;
		}

		if (!agpmem)
			goto vm_nopage_error;

		/*
		 * Get the page, inc the use count, and return it
		 */
		offset = (baddr - agpmem->bound) >> PAGE_SHIFT;
		page = virt_to_page(__va(agpmem->memory->memory[offset]));
		get_page(page);

		DRM_DEBUG
		    ("baddr = 0x%lx page = 0x%p, offset = 0x%lx, count=%d\n",
		     baddr, __va(agpmem->memory->memory[offset]), offset,
		     page_count(page));

		return page;
	}
      vm_nopage_error:
	return NOPAGE_SIGBUS;	/* Disallow mremap */
}
#else				/* __OS_HAS_AGP */
static __inline__ struct page *drm_do_vm_nopage(struct vm_area_struct *vma,
						unsigned long address)
{
	return NOPAGE_SIGBUS;
}
#endif				/* __OS_HAS_AGP */

/**
 * \c nopage method for shared virtual memory.
 *
 * \param vma virtual memory area.
 * \param address access address.
 * \return pointer to the page structure.
 *
 * Get the the mapping, find the real physical page to map, get the page, and
 * return it.
 */
static __inline__ struct page *drm_do_vm_shm_nopage(struct vm_area_struct *vma,
						    unsigned long address)
{
	drm_map_t *map = (drm_map_t *) vma->vm_private_data;
	unsigned long offset;
	unsigned long i;
	struct page *page;

	if (address > vma->vm_end)
		return NOPAGE_SIGBUS;	/* Disallow mremap */
	if (!map)
		return NOPAGE_SIGBUS;	/* Nothing allocated */

	offset = address - vma->vm_start;
	i = (unsigned long)map->handle + offset;
	page = vmalloc_to_page((void *)i);
	if (!page)
		return NOPAGE_SIGBUS;
	get_page(page);

	DRM_DEBUG("shm_nopage 0x%lx\n", address);
	return page;
}

/**
 * \c close method for shared virtual memory.
 *
 * \param vma virtual memory area.
 *
 * Deletes map information if we are the last
 * person to close a mapping and it's not in the global maplist.
 */
static void drm_vm_shm_close(struct vm_area_struct *vma)
{
	drm_file_t *priv = vma->vm_file->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_vma_entry_t *pt, *prev, *next;
	drm_map_t *map;
	drm_map_list_t *r_list;
	struct list_head *list;
	int found_maps = 0;

	DRM_DEBUG("0x%08lx,0x%08lx\n",
		  vma->vm_start, vma->vm_end - vma->vm_start);
	atomic_dec(&dev->vma_count);

	map = vma->vm_private_data;

	mutex_lock(&dev->struct_mutex);
	for (pt = dev->vmalist, prev = NULL; pt; pt = next) {
		next = pt->next;
		if (pt->vma->vm_private_data == map)
			found_maps++;
		if (pt->vma == vma) {
			if (prev) {
				prev->next = pt->next;
			} else {
				dev->vmalist = pt->next;
			}
			drm_free(pt, sizeof(*pt), DRM_MEM_VMAS);
		} else {
			prev = pt;
		}
	}
	/* We were the only map that was found */
	if (found_maps == 1 && map->flags & _DRM_REMOVABLE) {
		/* Check to see if we are in the maplist, if we are not, then
		 * we delete this mappings information.
		 */
		found_maps = 0;
		list = &dev->maplist->head;
		list_for_each(list, &dev->maplist->head) {
			r_list = list_entry(list, drm_map_list_t, head);
			if (r_list->map == map)
				found_maps++;
		}

		if (!found_maps) {
			drm_dma_handle_t dmah;

			switch (map->type) {
			case _DRM_REGISTERS:
			case _DRM_FRAME_BUFFER:
				if (drm_core_has_MTRR(dev) && map->mtrr >= 0) {
					int retcode;
					retcode = mtrr_del(map->mtrr,
							   map->offset,
							   map->size);
					DRM_DEBUG("mtrr_del = %d\n", retcode);
				}
				iounmap(map->handle);
				break;
			case _DRM_SHM:
				vfree(map->handle);
				break;
			case _DRM_AGP:
			case _DRM_SCATTER_GATHER:
				break;
			case _DRM_CONSISTENT:
				dmah.vaddr = map->handle;
				dmah.busaddr = map->offset;
				dmah.size = map->size;
				__drm_pci_free(dev, &dmah);
				break;
			}
			drm_free(map, sizeof(*map), DRM_MEM_MAPS);
		}
	}
	mutex_unlock(&dev->struct_mutex);
}

/**
 * \c nopage method for DMA virtual memory.
 *
 * \param vma virtual memory area.
 * \param address access address.
 * \return pointer to the page structure.
 *
 * Determine the page number from the page offset and get it from drm_device_dma::pagelist.
 */
static __inline__ struct page *drm_do_vm_dma_nopage(struct vm_area_struct *vma,
						    unsigned long address)
{
	drm_file_t *priv = vma->vm_file->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_device_dma_t *dma = dev->dma;
	unsigned long offset;
	unsigned long page_nr;
	struct page *page;

	if (!dma)
		return NOPAGE_SIGBUS;	/* Error */
	if (address > vma->vm_end)
		return NOPAGE_SIGBUS;	/* Disallow mremap */
	if (!dma->pagelist)
		return NOPAGE_SIGBUS;	/* Nothing allocated */

	offset = address - vma->vm_start;	/* vm_[pg]off[set] should be 0 */
	page_nr = offset >> PAGE_SHIFT;
	page = virt_to_page((dma->pagelist[page_nr] + (offset & (~PAGE_MASK))));

	get_page(page);

	DRM_DEBUG("dma_nopage 0x%lx (page %lu)\n", address, page_nr);
	return page;
}

/**
 * \c nopage method for scatter-gather virtual memory.
 *
 * \param vma virtual memory area.
 * \param address access address.
 * \return pointer to the page structure.
 *
 * Determine the map offset from the page offset and get it from drm_sg_mem::pagelist.
 */
static __inline__ struct page *drm_do_vm_sg_nopage(struct vm_area_struct *vma,
						   unsigned long address)
{
	drm_map_t *map = (drm_map_t *) vma->vm_private_data;
	drm_file_t *priv = vma->vm_file->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_sg_mem_t *entry = dev->sg;
	unsigned long offset;
	unsigned long map_offset;
	unsigned long page_offset;
	struct page *page;

	if (!entry)
		return NOPAGE_SIGBUS;	/* Error */
	if (address > vma->vm_end)
		return NOPAGE_SIGBUS;	/* Disallow mremap */
	if (!entry->pagelist)
		return NOPAGE_SIGBUS;	/* Nothing allocated */

	offset = address - vma->vm_start;
	map_offset = map->offset - (unsigned long)dev->sg->virtual;
	page_offset = (offset >> PAGE_SHIFT) + (map_offset >> PAGE_SHIFT);
	page = entry->pagelist[page_offset];
	get_page(page);

	return page;
}

static struct page *drm_vm_nopage(struct vm_area_struct *vma,
				  unsigned long address, int *type)
{
	if (type)
		*type = VM_FAULT_MINOR;
	return drm_do_vm_nopage(vma, address);
}

static struct page *drm_vm_shm_nopage(struct vm_area_struct *vma,
				      unsigned long address, int *type)
{
	if (type)
		*type = VM_FAULT_MINOR;
	return drm_do_vm_shm_nopage(vma, address);
}

static struct page *drm_vm_dma_nopage(struct vm_area_struct *vma,
				      unsigned long address, int *type)
{
	if (type)
		*type = VM_FAULT_MINOR;
	return drm_do_vm_dma_nopage(vma, address);
}

static struct page *drm_vm_sg_nopage(struct vm_area_struct *vma,
				     unsigned long address, int *type)
{
	if (type)
		*type = VM_FAULT_MINOR;
	return drm_do_vm_sg_nopage(vma, address);
}

/** AGP virtual memory operations */
static struct vm_operations_struct drm_vm_ops = {
	.nopage = drm_vm_nopage,
	.open = drm_vm_open,
	.close = drm_vm_close,
};

/** Shared virtual memory operations */
static struct vm_operations_struct drm_vm_shm_ops = {
	.nopage = drm_vm_shm_nopage,
	.open = drm_vm_open,
	.close = drm_vm_shm_close,
};

/** DMA virtual memory operations */
static struct vm_operations_struct drm_vm_dma_ops = {
	.nopage = drm_vm_dma_nopage,
	.open = drm_vm_open,
	.close = drm_vm_close,
};

/** Scatter-gather virtual memory operations */
static struct vm_operations_struct drm_vm_sg_ops = {
	.nopage = drm_vm_sg_nopage,
	.open = drm_vm_open,
	.close = drm_vm_close,
};

/**
 * \c open method for shared virtual memory.
 *
 * \param vma virtual memory area.
 *
 * Create a new drm_vma_entry structure as the \p vma private data entry and
 * add it to drm_device::vmalist.
 */
static void drm_vm_open_locked(struct vm_area_struct *vma)
{
	drm_file_t *priv = vma->vm_file->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_vma_entry_t *vma_entry;

	DRM_DEBUG("0x%08lx,0x%08lx\n",
		  vma->vm_start, vma->vm_end - vma->vm_start);
	atomic_inc(&dev->vma_count);

	vma_entry = drm_alloc(sizeof(*vma_entry), DRM_MEM_VMAS);
	if (vma_entry) {
		vma_entry->vma = vma;
		vma_entry->next = dev->vmalist;
		vma_entry->pid = current->pid;
		dev->vmalist = vma_entry;
	}
}

static void drm_vm_open(struct vm_area_struct *vma)
{
	drm_file_t *priv = vma->vm_file->private_data;
	drm_device_t *dev = priv->head->dev;

	mutex_lock(&dev->struct_mutex);
	drm_vm_open_locked(vma);
	mutex_unlock(&dev->struct_mutex);
}

/**
 * \c close method for all virtual memory types.
 *
 * \param vma virtual memory area.
 *
 * Search the \p vma private data entry in drm_device::vmalist, unlink it, and
 * free it.
 */
static void drm_vm_close(struct vm_area_struct *vma)
{
	drm_file_t *priv = vma->vm_file->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_vma_entry_t *pt, *prev;

	DRM_DEBUG("0x%08lx,0x%08lx\n",
		  vma->vm_start, vma->vm_end - vma->vm_start);
	atomic_dec(&dev->vma_count);

	mutex_lock(&dev->struct_mutex);
	for (pt = dev->vmalist, prev = NULL; pt; prev = pt, pt = pt->next) {
		if (pt->vma == vma) {
			if (prev) {
				prev->next = pt->next;
			} else {
				dev->vmalist = pt->next;
			}
			drm_free(pt, sizeof(*pt), DRM_MEM_VMAS);
			break;
		}
	}
	mutex_unlock(&dev->struct_mutex);
}

/**
 * mmap DMA memory.
 *
 * \param filp file pointer.
 * \param vma virtual memory area.
 * \return zero on success or a negative number on failure.
 *
 * Sets the virtual memory area operations structure to vm_dma_ops, the file
 * pointer, and calls vm_open().
 */
static int drm_mmap_dma(struct file *filp, struct vm_area_struct *vma)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev;
	drm_device_dma_t *dma;
	unsigned long length = vma->vm_end - vma->vm_start;

	dev = priv->head->dev;
	dma = dev->dma;
	DRM_DEBUG("start = 0x%lx, end = 0x%lx, page offset = 0x%lx\n",
		  vma->vm_start, vma->vm_end, vma->vm_pgoff);

	/* Length must match exact page count */
	if (!dma || (length >> PAGE_SHIFT) != dma->page_count) {
		return -EINVAL;
	}

	if (!capable(CAP_SYS_ADMIN) &&
	    (dma->flags & _DRM_DMA_USE_PCI_RO)) {
		vma->vm_flags &= ~(VM_WRITE | VM_MAYWRITE);
#if defined(__i386__) || defined(__x86_64__)
		pgprot_val(vma->vm_page_prot) &= ~_PAGE_RW;
#else
		/* Ye gads this is ugly.  With more thought
		   we could move this up higher and use
		   `protection_map' instead.  */
		vma->vm_page_prot =
		    __pgprot(pte_val
			     (pte_wrprotect
			      (__pte(pgprot_val(vma->vm_page_prot)))));
#endif
	}

	vma->vm_ops = &drm_vm_dma_ops;

	vma->vm_flags |= VM_RESERVED;	/* Don't swap */

	vma->vm_file = filp;	/* Needed for drm_vm_open() */
	drm_vm_open_locked(vma);
	return 0;
}

unsigned long drm_core_get_map_ofs(drm_map_t * map)
{
	return map->offset;
}

EXPORT_SYMBOL(drm_core_get_map_ofs);

unsigned long drm_core_get_reg_ofs(struct drm_device *dev)
{
#ifdef __alpha__
	return dev->hose->dense_mem_base - dev->hose->mem_space->start;
#else
	return 0;
#endif
}

EXPORT_SYMBOL(drm_core_get_reg_ofs);

/**
 * mmap DMA memory.
 *
 * \param filp file pointer.
 * \param vma virtual memory area.
 * \return zero on success or a negative number on failure.
 *
 * If the virtual memory area has no offset associated with it then it's a DMA
 * area, so calls mmap_dma(). Otherwise searches the map in drm_device::maplist,
 * checks that the restricted flag is not set, sets the virtual memory operations
 * according to the mapping type and remaps the pages. Finally sets the file
 * pointer and calls vm_open().
 */
static int drm_mmap_locked(struct file *filp, struct vm_area_struct *vma)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_map_t *map = NULL;
	unsigned long offset = 0;
	drm_hash_item_t *hash;

	DRM_DEBUG("start = 0x%lx, end = 0x%lx, page offset = 0x%lx\n",
		  vma->vm_start, vma->vm_end, vma->vm_pgoff);

	if (!priv->authenticated)
		return -EACCES;

	/* We check for "dma". On Apple's UniNorth, it's valid to have
	 * the AGP mapped at physical address 0
	 * --BenH.
	 */
	if (!vma->vm_pgoff
#if __OS_HAS_AGP
	    && (!dev->agp
		|| dev->agp->agp_info.device->vendor != PCI_VENDOR_ID_APPLE)
#endif
	    )
		return drm_mmap_dma(filp, vma);

	if (drm_ht_find_item(&dev->map_hash, vma->vm_pgoff, &hash)) {
		DRM_ERROR("Could not find map\n");
		return -EINVAL;
	}

	map = drm_hash_entry(hash, drm_map_list_t, hash)->map;
	if (!map || ((map->flags & _DRM_RESTRICTED) && !capable(CAP_SYS_ADMIN)))
		return -EPERM;

	/* Check for valid size. */
	if (map->size < vma->vm_end - vma->vm_start)
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN) && (map->flags & _DRM_READ_ONLY)) {
		vma->vm_flags &= ~(VM_WRITE | VM_MAYWRITE);
#if defined(__i386__) || defined(__x86_64__)
		pgprot_val(vma->vm_page_prot) &= ~_PAGE_RW;
#else
		/* Ye gads this is ugly.  With more thought
		   we could move this up higher and use
		   `protection_map' instead.  */
		vma->vm_page_prot =
		    __pgprot(pte_val
			     (pte_wrprotect
			      (__pte(pgprot_val(vma->vm_page_prot)))));
#endif
	}

	switch (map->type) {
	case _DRM_AGP:
		if (drm_core_has_AGP(dev) && dev->agp->cant_use_aperture) {
			/*
			 * On some platforms we can't talk to bus dma address from the CPU, so for
			 * memory of type DRM_AGP, we'll deal with sorting out the real physical
			 * pages and mappings in nopage()
			 */
#if defined(__powerpc__)
			pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE;
#endif
			vma->vm_ops = &drm_vm_ops;
			break;
		}
		/* fall through to _DRM_FRAME_BUFFER... */
	case _DRM_FRAME_BUFFER:
	case _DRM_REGISTERS:
		offset = dev->driver->get_reg_ofs(dev);
		vma->vm_flags |= VM_IO;	/* not in core dump */
		vma->vm_page_prot = drm_io_prot(map->type, vma);
#ifdef __sparc__
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#endif
		if (io_remap_pfn_range(vma, vma->vm_start,
				       (map->offset + offset) >> PAGE_SHIFT,
				       vma->vm_end - vma->vm_start,
				       vma->vm_page_prot))
			return -EAGAIN;
		DRM_DEBUG("   Type = %d; start = 0x%lx, end = 0x%lx,"
			  " offset = 0x%lx\n",
			  map->type,
			  vma->vm_start, vma->vm_end, map->offset + offset);
		vma->vm_ops = &drm_vm_ops;
		break;
	case _DRM_CONSISTENT:
		/* Consistent memory is really like shared memory. But
		 * it's allocated in a different way, so avoid nopage */
		if (remap_pfn_range(vma, vma->vm_start,
		    page_to_pfn(virt_to_page(map->handle)),
		    vma->vm_end - vma->vm_start, vma->vm_page_prot))
			return -EAGAIN;
	/* fall through to _DRM_SHM */
	case _DRM_SHM:
		vma->vm_ops = &drm_vm_shm_ops;
		vma->vm_private_data = (void *)map;
		/* Don't let this area swap.  Change when
		   DRM_KERNEL advisory is supported. */
		vma->vm_flags |= VM_RESERVED;
		break;
	case _DRM_SCATTER_GATHER:
		vma->vm_ops = &drm_vm_sg_ops;
		vma->vm_private_data = (void *)map;
		vma->vm_flags |= VM_RESERVED;
		break;
	default:
		return -EINVAL;	/* This should never happen. */
	}
	vma->vm_flags |= VM_RESERVED;	/* Don't swap */

	vma->vm_file = filp;	/* Needed for drm_vm_open() */
	drm_vm_open_locked(vma);
	return 0;
}

int drm_mmap(struct file *filp, struct vm_area_struct *vma)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm_mmap_locked(filp, vma);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}
EXPORT_SYMBOL(drm_mmap);
