/**
 * \file drm_memory.c
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
#include "drmP.h"

#ifdef DEBUG_MEMORY
#include "drm_memory_debug.h"
#else

/** No-op. */
void drm_mem_init(void)
{
}

/**
 * Called when "/proc/dri/%dev%/mem" is read.
 *
 * \param buf output buffer.
 * \param start start of output data.
 * \param offset requested start offset.
 * \param len requested number of bytes.
 * \param eof whether there is no more data to return.
 * \param data private data.
 * \return number of written bytes.
 *
 * No-op.
 */
int drm_mem_info(char *buf, char **start, off_t offset,
		 int len, int *eof, void *data)
{
	return 0;
}

/** Wrapper around kmalloc() and kfree() */
void *drm_realloc(void *oldpt, size_t oldsize, size_t size, int area)
{
	void *pt;

	if (!(pt = kmalloc(size, GFP_KERNEL)))
		return NULL;
	if (oldpt && oldsize) {
		memcpy(pt, oldpt, oldsize);
		kfree(oldpt);
	}
	return pt;
}

/**
 * Allocate pages.
 *
 * \param order size order.
 * \param area memory area. (Not used.)
 * \return page address on success, or zero on failure.
 *
 * Allocate and reserve free pages.
 */
unsigned long drm_alloc_pages(int order, int area)
{
	unsigned long address;
	unsigned long bytes = PAGE_SIZE << order;
	unsigned long addr;
	unsigned int sz;

	address = __get_free_pages(GFP_KERNEL|__GFP_COMP, order);
	if (!address)
		return 0;

	/* Zero */
	memset((void *)address, 0, bytes);

	/* Reserve */
	for (addr = address, sz = bytes;
	     sz > 0; addr += PAGE_SIZE, sz -= PAGE_SIZE) {
		SetPageReserved(virt_to_page(addr));
	}

	return address;
}

/**
 * Free pages.
 *
 * \param address address of the pages to free.
 * \param order size order.
 * \param area memory area. (Not used.)
 *
 * Unreserve and free pages allocated by alloc_pages().
 */
void drm_free_pages(unsigned long address, int order, int area)
{
	unsigned long bytes = PAGE_SIZE << order;
	unsigned long addr;
	unsigned int sz;

	if (!address)
		return;

	/* Unreserve */
	for (addr = address, sz = bytes;
	     sz > 0; addr += PAGE_SIZE, sz -= PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
	}

	free_pages(address, order);
}

#if __OS_HAS_AGP
/** Wrapper around agp_allocate_memory() */
DRM_AGP_MEM *drm_alloc_agp(drm_device_t * dev, int pages, u32 type)
{
	return drm_agp_allocate_memory(dev->agp->bridge, pages, type);
}

/** Wrapper around agp_free_memory() */
int drm_free_agp(DRM_AGP_MEM * handle, int pages)
{
	return drm_agp_free_memory(handle) ? 0 : -EINVAL;
}

/** Wrapper around agp_bind_memory() */
int drm_bind_agp(DRM_AGP_MEM * handle, unsigned int start)
{
	return drm_agp_bind_memory(handle, start);
}

/** Wrapper around agp_unbind_memory() */
int drm_unbind_agp(DRM_AGP_MEM * handle)
{
	return drm_agp_unbind_memory(handle);
}
#endif				/* agp */
#endif				/* debug_memory */
