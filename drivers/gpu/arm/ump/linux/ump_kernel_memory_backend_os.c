/*
 * Copyright (C) 2010-2011, 2013-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/* needed to detect kernel version specific code */
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#include <linux/semaphore.h>
#else /* pre 2.6.26 the file was in the arch specific location */
#include <asm/semaphore.h>
#endif

#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/atomic.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>
#include "ump_kernel_common.h"
#include "ump_kernel_memory_backend.h"



typedef struct os_allocator {
	struct semaphore mutex;
	u32 num_pages_max;       /**< Maximum number of pages to allocate from the OS */
	u32 num_pages_allocated; /**< Number of pages allocated from the OS */
} os_allocator;



static void os_free(void *ctx, ump_dd_mem *descriptor);
static int os_allocate(void *ctx, ump_dd_mem *descriptor);
static void os_memory_backend_destroy(ump_memory_backend *backend);
static u32 os_stat(struct ump_memory_backend *backend);



/*
 * Create OS memory backend
 */
ump_memory_backend *ump_os_memory_backend_create(const int max_allocation)
{
	ump_memory_backend *backend;
	os_allocator *info;

	info = kmalloc(sizeof(os_allocator), GFP_KERNEL);
	if (NULL == info) {
		return NULL;
	}

	info->num_pages_max = max_allocation >> PAGE_SHIFT;
	info->num_pages_allocated = 0;

	sema_init(&info->mutex, 1);

	backend = kmalloc(sizeof(ump_memory_backend), GFP_KERNEL);
	if (NULL == backend) {
		kfree(info);
		return NULL;
	}

	backend->ctx = info;
	backend->allocate = os_allocate;
	backend->release = os_free;
	backend->shutdown = os_memory_backend_destroy;
	backend->stat = os_stat;
	backend->pre_allocate_physical_check = NULL;
	backend->adjust_to_mali_phys = NULL;

	return backend;
}



/*
 * Destroy specified OS memory backend
 */
static void os_memory_backend_destroy(ump_memory_backend *backend)
{
	os_allocator *info = (os_allocator *)backend->ctx;

	DBG_MSG_IF(1, 0 != info->num_pages_allocated, ("%d pages still in use during shutdown\n", info->num_pages_allocated));

	kfree(info);
	kfree(backend);
}



/*
 * Allocate UMP memory
 */
static int os_allocate(void *ctx, ump_dd_mem *descriptor)
{
	u32 left;
	os_allocator *info;
	int pages_allocated = 0;
	int is_cached;

	BUG_ON(!descriptor);
	BUG_ON(!ctx);

	info = (os_allocator *)ctx;
	left = descriptor->size_bytes;
	is_cached = descriptor->is_cached;

	if (down_interruptible(&info->mutex)) {
		DBG_MSG(1, ("Failed to get mutex in os_free\n"));
		return 0; /* failure */
	}

	descriptor->backend_info = NULL;
	descriptor->nr_blocks = ((left + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)) >> PAGE_SHIFT;

	DBG_MSG(5, ("Allocating page array. Size: %lu\n", descriptor->nr_blocks * sizeof(ump_dd_physical_block)));

	descriptor->block_array = (ump_dd_physical_block *)vmalloc(sizeof(ump_dd_physical_block) * descriptor->nr_blocks);
	if (NULL == descriptor->block_array) {
		up(&info->mutex);
		DBG_MSG(1, ("Block array could not be allocated\n"));
		return 0; /* failure */
	}

	while (left > 0 && ((info->num_pages_allocated + pages_allocated) < info->num_pages_max)) {
		struct page *new_page;

		if (is_cached) {
			new_page = alloc_page(GFP_HIGHUSER | __GFP_ZERO | __GFP_REPEAT | __GFP_NOWARN);
		} else {
			new_page = alloc_page(GFP_HIGHUSER | __GFP_ZERO | __GFP_REPEAT | __GFP_NOWARN | __GFP_COLD);
		}
		if (NULL == new_page) {
			break;
		}

		/* Ensure page caches are flushed. */
		if (is_cached) {
			descriptor->block_array[pages_allocated].addr = page_to_phys(new_page);
			descriptor->block_array[pages_allocated].size = PAGE_SIZE;
		} else {
			descriptor->block_array[pages_allocated].addr = dma_map_page(NULL, new_page, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
			descriptor->block_array[pages_allocated].size = PAGE_SIZE;
		}

		DBG_MSG(5, ("Allocated page 0x%08lx cached: %d\n", descriptor->block_array[pages_allocated].addr, is_cached));

		if (left < PAGE_SIZE) {
			left = 0;
		} else {
			left -= PAGE_SIZE;
		}

		pages_allocated++;
	}

	DBG_MSG(5, ("Alloce for ID:%2d got %d pages, cached: %d\n", descriptor->secure_id,  pages_allocated));

	if (left) {
		DBG_MSG(1, ("Failed to allocate needed pages\n"));

		while (pages_allocated) {
			pages_allocated--;
			if (!is_cached) {
				dma_unmap_page(NULL, descriptor->block_array[pages_allocated].addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
			}
			__free_page(pfn_to_page(descriptor->block_array[pages_allocated].addr >> PAGE_SHIFT));
		}

		up(&info->mutex);

		return 0; /* failure */
	}

	info->num_pages_allocated += pages_allocated;

	DBG_MSG(6, ("%d out of %d pages now allocated\n", info->num_pages_allocated, info->num_pages_max));

	up(&info->mutex);

	return 1; /* success*/
}


/*
 * Free specified UMP memory
 */
static void os_free(void *ctx, ump_dd_mem *descriptor)
{
	os_allocator *info;
	int i;

	BUG_ON(!ctx);
	BUG_ON(!descriptor);

	info = (os_allocator *)ctx;

	BUG_ON(descriptor->nr_blocks > info->num_pages_allocated);

	if (down_interruptible(&info->mutex)) {
		DBG_MSG(1, ("Failed to get mutex in os_free\n"));
		return;
	}

	DBG_MSG(5, ("Releasing %lu OS pages\n", descriptor->nr_blocks));

	info->num_pages_allocated -= descriptor->nr_blocks;

	up(&info->mutex);

	for (i = 0; i < descriptor->nr_blocks; i++) {
		DBG_MSG(6, ("Freeing physical page. Address: 0x%08lx\n", descriptor->block_array[i].addr));
		if (! descriptor->is_cached) {
			dma_unmap_page(NULL, descriptor->block_array[i].addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
		}
		__free_page(pfn_to_page(descriptor->block_array[i].addr >> PAGE_SHIFT));
	}

	vfree(descriptor->block_array);
}


static u32 os_stat(struct ump_memory_backend *backend)
{
	os_allocator *info;
	info = (os_allocator *)backend->ctx;
	return info->num_pages_allocated * _MALI_OSK_MALI_PAGE_SIZE;
}
