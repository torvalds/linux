/**
 * \file drm_memory.h
 * Memory management wrappers for DRM.
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
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
#include "drmP.h"

typedef struct drm_mem_stats {
	const char *name;
	int succeed_count;
	int free_count;
	int fail_count;
	unsigned long bytes_allocated;
	unsigned long bytes_freed;
} drm_mem_stats_t;

static DEFINE_SPINLOCK(DRM(mem_lock));
static unsigned long DRM(ram_available) = 0;	/* In pages */
static unsigned long DRM(ram_used) = 0;
static drm_mem_stats_t DRM(mem_stats)[] =
{
	[DRM_MEM_DMA] = {
	"dmabufs"},[DRM_MEM_SAREA] = {
	"sareas"},[DRM_MEM_DRIVER] = {
	"driver"},[DRM_MEM_MAGIC] = {
	"magic"},[DRM_MEM_IOCTLS] = {
	"ioctltab"},[DRM_MEM_MAPS] = {
	"maplist"},[DRM_MEM_VMAS] = {
	"vmalist"},[DRM_MEM_BUFS] = {
	"buflist"},[DRM_MEM_SEGS] = {
	"seglist"},[DRM_MEM_PAGES] = {
	"pagelist"},[DRM_MEM_FILES] = {
	"files"},[DRM_MEM_QUEUES] = {
	"queues"},[DRM_MEM_CMDS] = {
	"commands"},[DRM_MEM_MAPPINGS] = {
	"mappings"},[DRM_MEM_BUFLISTS] = {
	"buflists"},[DRM_MEM_AGPLISTS] = {
	"agplist"},[DRM_MEM_SGLISTS] = {
	"sglist"},[DRM_MEM_TOTALAGP] = {
	"totalagp"},[DRM_MEM_BOUNDAGP] = {
	"boundagp"},[DRM_MEM_CTXBITMAP] = {
	"ctxbitmap"},[DRM_MEM_CTXLIST] = {
	"ctxlist"},[DRM_MEM_STUB] = {
	"stub"}, {
	NULL, 0,}		/* Last entry must be null */
};

void DRM(mem_init) (void) {
	drm_mem_stats_t *mem;
	struct sysinfo si;

	for (mem = DRM(mem_stats); mem->name; ++mem) {
		mem->succeed_count = 0;
		mem->free_count = 0;
		mem->fail_count = 0;
		mem->bytes_allocated = 0;
		mem->bytes_freed = 0;
	}

	si_meminfo(&si);
	DRM(ram_available) = si.totalram;
	DRM(ram_used) = 0;
}

/* drm_mem_info is called whenever a process reads /dev/drm/mem. */

static int DRM(_mem_info) (char *buf, char **start, off_t offset,
			   int request, int *eof, void *data) {
	drm_mem_stats_t *pt;
	int len = 0;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*eof = 0;
	*start = &buf[offset];

	DRM_PROC_PRINT("		  total counts			"
		       " |    outstanding  \n");
	DRM_PROC_PRINT("type	   alloc freed fail	bytes	   freed"
		       " | allocs      bytes\n\n");
	DRM_PROC_PRINT("%-9.9s %5d %5d %4d %10lu kB         |\n",
		       "system", 0, 0, 0,
		       DRM(ram_available) << (PAGE_SHIFT - 10));
	DRM_PROC_PRINT("%-9.9s %5d %5d %4d %10lu kB         |\n",
		       "locked", 0, 0, 0, DRM(ram_used) >> 10);
	DRM_PROC_PRINT("\n");
	for (pt = DRM(mem_stats); pt->name; pt++) {
		DRM_PROC_PRINT("%-9.9s %5d %5d %4d %10lu %10lu | %6d %10ld\n",
			       pt->name,
			       pt->succeed_count,
			       pt->free_count,
			       pt->fail_count,
			       pt->bytes_allocated,
			       pt->bytes_freed,
			       pt->succeed_count - pt->free_count,
			       (long)pt->bytes_allocated
			       - (long)pt->bytes_freed);
	}

	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

int DRM(mem_info) (char *buf, char **start, off_t offset,
		   int len, int *eof, void *data) {
	int ret;

	spin_lock(&DRM(mem_lock));
	ret = DRM(_mem_info) (buf, start, offset, len, eof, data);
	spin_unlock(&DRM(mem_lock));
	return ret;
}

void *DRM(alloc) (size_t size, int area) {
	void *pt;

	if (!size) {
		DRM_MEM_ERROR(area, "Allocating 0 bytes\n");
		return NULL;
	}

	if (!(pt = kmalloc(size, GFP_KERNEL))) {
		spin_lock(&DRM(mem_lock));
		++DRM(mem_stats)[area].fail_count;
		spin_unlock(&DRM(mem_lock));
		return NULL;
	}
	spin_lock(&DRM(mem_lock));
	++DRM(mem_stats)[area].succeed_count;
	DRM(mem_stats)[area].bytes_allocated += size;
	spin_unlock(&DRM(mem_lock));
	return pt;
}

void *DRM(calloc) (size_t nmemb, size_t size, int area) {
	void *addr;

	addr = DRM(alloc) (nmemb * size, area);
	if (addr != NULL)
		memset((void *)addr, 0, size * nmemb);

	return addr;
}

void *DRM(realloc) (void *oldpt, size_t oldsize, size_t size, int area) {
	void *pt;

	if (!(pt = DRM(alloc) (size, area)))
		return NULL;
	if (oldpt && oldsize) {
		memcpy(pt, oldpt, oldsize);
		DRM(free) (oldpt, oldsize, area);
	}
	return pt;
}

void DRM(free) (void *pt, size_t size, int area) {
	int alloc_count;
	int free_count;

	if (!pt)
		DRM_MEM_ERROR(area, "Attempt to free NULL pointer\n");
	else
		kfree(pt);
	spin_lock(&DRM(mem_lock));
	DRM(mem_stats)[area].bytes_freed += size;
	free_count = ++DRM(mem_stats)[area].free_count;
	alloc_count = DRM(mem_stats)[area].succeed_count;
	spin_unlock(&DRM(mem_lock));
	if (free_count > alloc_count) {
		DRM_MEM_ERROR(area, "Excess frees: %d frees, %d allocs\n",
			      free_count, alloc_count);
	}
}

unsigned long DRM(alloc_pages) (int order, int area) {
	unsigned long address;
	unsigned long bytes = PAGE_SIZE << order;
	unsigned long addr;
	unsigned int sz;

	spin_lock(&DRM(mem_lock));
	if ((DRM(ram_used) >> PAGE_SHIFT)
	    > (DRM_RAM_PERCENT * DRM(ram_available)) / 100) {
		spin_unlock(&DRM(mem_lock));
		return 0;
	}
	spin_unlock(&DRM(mem_lock));

	address = __get_free_pages(GFP_KERNEL, order);
	if (!address) {
		spin_lock(&DRM(mem_lock));
		++DRM(mem_stats)[area].fail_count;
		spin_unlock(&DRM(mem_lock));
		return 0;
	}
	spin_lock(&DRM(mem_lock));
	++DRM(mem_stats)[area].succeed_count;
	DRM(mem_stats)[area].bytes_allocated += bytes;
	DRM(ram_used) += bytes;
	spin_unlock(&DRM(mem_lock));

	/* Zero outside the lock */
	memset((void *)address, 0, bytes);

	/* Reserve */
	for (addr = address, sz = bytes;
	     sz > 0; addr += PAGE_SIZE, sz -= PAGE_SIZE) {
		SetPageReserved(virt_to_page(addr));
	}

	return address;
}

void DRM(free_pages) (unsigned long address, int order, int area) {
	unsigned long bytes = PAGE_SIZE << order;
	int alloc_count;
	int free_count;
	unsigned long addr;
	unsigned int sz;

	if (!address) {
		DRM_MEM_ERROR(area, "Attempt to free address 0\n");
	} else {
		/* Unreserve */
		for (addr = address, sz = bytes;
		     sz > 0; addr += PAGE_SIZE, sz -= PAGE_SIZE) {
			ClearPageReserved(virt_to_page(addr));
		}
		free_pages(address, order);
	}

	spin_lock(&DRM(mem_lock));
	free_count = ++DRM(mem_stats)[area].free_count;
	alloc_count = DRM(mem_stats)[area].succeed_count;
	DRM(mem_stats)[area].bytes_freed += bytes;
	DRM(ram_used) -= bytes;
	spin_unlock(&DRM(mem_lock));
	if (free_count > alloc_count) {
		DRM_MEM_ERROR(area,
			      "Excess frees: %d frees, %d allocs\n",
			      free_count, alloc_count);
	}
}

void *DRM(ioremap) (unsigned long offset, unsigned long size,
		    drm_device_t * dev) {
	void *pt;

	if (!size) {
		DRM_MEM_ERROR(DRM_MEM_MAPPINGS,
			      "Mapping 0 bytes at 0x%08lx\n", offset);
		return NULL;
	}

	if (!(pt = drm_ioremap(offset, size, dev))) {
		spin_lock(&DRM(mem_lock));
		++DRM(mem_stats)[DRM_MEM_MAPPINGS].fail_count;
		spin_unlock(&DRM(mem_lock));
		return NULL;
	}
	spin_lock(&DRM(mem_lock));
	++DRM(mem_stats)[DRM_MEM_MAPPINGS].succeed_count;
	DRM(mem_stats)[DRM_MEM_MAPPINGS].bytes_allocated += size;
	spin_unlock(&DRM(mem_lock));
	return pt;
}

void *DRM(ioremap_nocache) (unsigned long offset, unsigned long size,
			    drm_device_t * dev) {
	void *pt;

	if (!size) {
		DRM_MEM_ERROR(DRM_MEM_MAPPINGS,
			      "Mapping 0 bytes at 0x%08lx\n", offset);
		return NULL;
	}

	if (!(pt = drm_ioremap_nocache(offset, size, dev))) {
		spin_lock(&DRM(mem_lock));
		++DRM(mem_stats)[DRM_MEM_MAPPINGS].fail_count;
		spin_unlock(&DRM(mem_lock));
		return NULL;
	}
	spin_lock(&DRM(mem_lock));
	++DRM(mem_stats)[DRM_MEM_MAPPINGS].succeed_count;
	DRM(mem_stats)[DRM_MEM_MAPPINGS].bytes_allocated += size;
	spin_unlock(&DRM(mem_lock));
	return pt;
}

void DRM(ioremapfree) (void *pt, unsigned long size, drm_device_t * dev) {
	int alloc_count;
	int free_count;

	if (!pt)
		DRM_MEM_ERROR(DRM_MEM_MAPPINGS,
			      "Attempt to free NULL pointer\n");
	else
		drm_ioremapfree(pt, size, dev);

	spin_lock(&DRM(mem_lock));
	DRM(mem_stats)[DRM_MEM_MAPPINGS].bytes_freed += size;
	free_count = ++DRM(mem_stats)[DRM_MEM_MAPPINGS].free_count;
	alloc_count = DRM(mem_stats)[DRM_MEM_MAPPINGS].succeed_count;
	spin_unlock(&DRM(mem_lock));
	if (free_count > alloc_count) {
		DRM_MEM_ERROR(DRM_MEM_MAPPINGS,
			      "Excess frees: %d frees, %d allocs\n",
			      free_count, alloc_count);
	}
}

#if __OS_HAS_AGP

DRM_AGP_MEM *DRM(alloc_agp) (int pages, u32 type) {
	DRM_AGP_MEM *handle;

	if (!pages) {
		DRM_MEM_ERROR(DRM_MEM_TOTALAGP, "Allocating 0 pages\n");
		return NULL;
	}

	if ((handle = DRM(agp_allocate_memory) (pages, type))) {
		spin_lock(&DRM(mem_lock));
		++DRM(mem_stats)[DRM_MEM_TOTALAGP].succeed_count;
		DRM(mem_stats)[DRM_MEM_TOTALAGP].bytes_allocated
		    += pages << PAGE_SHIFT;
		spin_unlock(&DRM(mem_lock));
		return handle;
	}
	spin_lock(&DRM(mem_lock));
	++DRM(mem_stats)[DRM_MEM_TOTALAGP].fail_count;
	spin_unlock(&DRM(mem_lock));
	return NULL;
}

int DRM(free_agp) (DRM_AGP_MEM * handle, int pages) {
	int alloc_count;
	int free_count;
	int retval = -EINVAL;

	if (!handle) {
		DRM_MEM_ERROR(DRM_MEM_TOTALAGP,
			      "Attempt to free NULL AGP handle\n");
		return retval;
	}

	if (DRM(agp_free_memory) (handle)) {
		spin_lock(&DRM(mem_lock));
		free_count = ++DRM(mem_stats)[DRM_MEM_TOTALAGP].free_count;
		alloc_count = DRM(mem_stats)[DRM_MEM_TOTALAGP].succeed_count;
		DRM(mem_stats)[DRM_MEM_TOTALAGP].bytes_freed
		    += pages << PAGE_SHIFT;
		spin_unlock(&DRM(mem_lock));
		if (free_count > alloc_count) {
			DRM_MEM_ERROR(DRM_MEM_TOTALAGP,
				      "Excess frees: %d frees, %d allocs\n",
				      free_count, alloc_count);
		}
		return 0;
	}
	return retval;
}

int DRM(bind_agp) (DRM_AGP_MEM * handle, unsigned int start) {
	int retcode = -EINVAL;

	if (!handle) {
		DRM_MEM_ERROR(DRM_MEM_BOUNDAGP,
			      "Attempt to bind NULL AGP handle\n");
		return retcode;
	}

	if (!(retcode = DRM(agp_bind_memory) (handle, start))) {
		spin_lock(&DRM(mem_lock));
		++DRM(mem_stats)[DRM_MEM_BOUNDAGP].succeed_count;
		DRM(mem_stats)[DRM_MEM_BOUNDAGP].bytes_allocated
		    += handle->page_count << PAGE_SHIFT;
		spin_unlock(&DRM(mem_lock));
		return retcode;
	}
	spin_lock(&DRM(mem_lock));
	++DRM(mem_stats)[DRM_MEM_BOUNDAGP].fail_count;
	spin_unlock(&DRM(mem_lock));
	return retcode;
}

int DRM(unbind_agp) (DRM_AGP_MEM * handle) {
	int alloc_count;
	int free_count;
	int retcode = -EINVAL;

	if (!handle) {
		DRM_MEM_ERROR(DRM_MEM_BOUNDAGP,
			      "Attempt to unbind NULL AGP handle\n");
		return retcode;
	}

	if ((retcode = DRM(agp_unbind_memory) (handle)))
		return retcode;
	spin_lock(&DRM(mem_lock));
	free_count = ++DRM(mem_stats)[DRM_MEM_BOUNDAGP].free_count;
	alloc_count = DRM(mem_stats)[DRM_MEM_BOUNDAGP].succeed_count;
	DRM(mem_stats)[DRM_MEM_BOUNDAGP].bytes_freed
	    += handle->page_count << PAGE_SHIFT;
	spin_unlock(&DRM(mem_lock));
	if (free_count > alloc_count) {
		DRM_MEM_ERROR(DRM_MEM_BOUNDAGP,
			      "Excess frees: %d frees, %d allocs\n",
			      free_count, alloc_count);
	}
	return retcode;
}
#endif
