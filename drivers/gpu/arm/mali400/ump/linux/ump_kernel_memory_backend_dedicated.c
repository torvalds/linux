/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2011, 2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/* needed to detect kernel version specific code */
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#include <linux/semaphore.h>
#else /* pre 2.6.26 the file was in the arch specific location */
#include <asm/semaphore.h>
#endif

#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/atomic.h>
#include <linux/vmalloc.h>
#include "ump_kernel_common.h"
#include "ump_kernel_memory_backend.h"



#define UMP_BLOCK_SIZE (256UL * 1024UL)  /* 256kB, remember to keep the ()s */



typedef struct block_info {
	struct block_info * next;
} block_info;



typedef struct block_allocator {
	struct semaphore mutex;
	block_info * all_blocks;
	block_info * first_free;
	u32 base;
	u32 num_blocks;
	u32 num_free;
} block_allocator;


static void block_allocator_shutdown(ump_memory_backend * backend);
static int block_allocator_allocate(void* ctx, ump_dd_mem * mem);
static void block_allocator_release(void * ctx, ump_dd_mem * handle);
static inline u32 get_phys(block_allocator * allocator, block_info * block);
static u32 block_allocator_stat(struct ump_memory_backend *backend);



/*
 * Create dedicated memory backend
 */
ump_memory_backend * ump_block_allocator_create(u32 base_address, u32 size)
{
	ump_memory_backend * backend;
	block_allocator * allocator;
	u32 usable_size;
	u32 num_blocks;

	usable_size = (size + UMP_BLOCK_SIZE - 1) & ~(UMP_BLOCK_SIZE - 1);
	num_blocks = usable_size / UMP_BLOCK_SIZE;

	if (0 == usable_size) {
		DBG_MSG(1, ("Memory block of size %u is unusable\n", size));
		return NULL;
	}

	DBG_MSG(5, ("Creating dedicated UMP memory backend. Base address: 0x%08x, size: 0x%08x\n", base_address, size));
	DBG_MSG(6, ("%u usable bytes which becomes %u blocks\n", usable_size, num_blocks));

	backend = kzalloc(sizeof(ump_memory_backend), GFP_KERNEL);
	if (NULL != backend) {
		allocator = kmalloc(sizeof(block_allocator), GFP_KERNEL);
		if (NULL != allocator) {
			allocator->all_blocks = kmalloc(sizeof(block_info) * num_blocks, GFP_KERNEL);
			if (NULL != allocator->all_blocks) {
				int i;

				allocator->first_free = NULL;
				allocator->num_blocks = num_blocks;
				allocator->num_free = num_blocks;
				allocator->base = base_address;
				sema_init(&allocator->mutex, 1);

				for (i = 0; i < num_blocks; i++) {
					allocator->all_blocks[i].next = allocator->first_free;
					allocator->first_free = &allocator->all_blocks[i];
				}

				backend->ctx = allocator;
				backend->allocate = block_allocator_allocate;
				backend->release = block_allocator_release;
				backend->shutdown = block_allocator_shutdown;
				backend->stat = block_allocator_stat;
				backend->pre_allocate_physical_check = NULL;
				backend->adjust_to_mali_phys = NULL;

				return backend;
			}
			kfree(allocator);
		}
		kfree(backend);
	}

	return NULL;
}



/*
 * Destroy specified dedicated memory backend
 */
static void block_allocator_shutdown(ump_memory_backend * backend)
{
	block_allocator * allocator;

	BUG_ON(!backend);
	BUG_ON(!backend->ctx);

	allocator = (block_allocator*)backend->ctx;

	DBG_MSG_IF(1, allocator->num_free != allocator->num_blocks, ("%u blocks still in use during shutdown\n", allocator->num_blocks - allocator->num_free));

	kfree(allocator->all_blocks);
	kfree(allocator);
	kfree(backend);
}



static int block_allocator_allocate(void* ctx, ump_dd_mem * mem)
{
	block_allocator * allocator;
	u32 left;
	block_info * last_allocated = NULL;
	int i = 0;

	BUG_ON(!ctx);
	BUG_ON(!mem);

	allocator = (block_allocator*)ctx;
	left = mem->size_bytes;

	BUG_ON(!left);
	BUG_ON(!&allocator->mutex);

	mem->nr_blocks = ((left + UMP_BLOCK_SIZE - 1) & ~(UMP_BLOCK_SIZE - 1)) / UMP_BLOCK_SIZE;
	mem->block_array = (ump_dd_physical_block*)vmalloc(sizeof(ump_dd_physical_block) * mem->nr_blocks);
	if (NULL == mem->block_array) {
		MSG_ERR(("Failed to allocate block array\n"));
		return 0;
	}

	if (down_interruptible(&allocator->mutex)) {
		MSG_ERR(("Could not get mutex to do block_allocate\n"));
		return 0;
	}

	mem->size_bytes = 0;

	while ((left > 0) && (allocator->first_free)) {
		block_info * block;

		block = allocator->first_free;
		allocator->first_free = allocator->first_free->next;
		block->next = last_allocated;
		last_allocated = block;
		allocator->num_free--;

		mem->block_array[i].addr = get_phys(allocator, block);
		mem->block_array[i].size = UMP_BLOCK_SIZE;
		mem->size_bytes += UMP_BLOCK_SIZE;

		i++;

		if (left < UMP_BLOCK_SIZE) left = 0;
		else left -= UMP_BLOCK_SIZE;
	}

	if (left) {
		block_info * block;
		/* release all memory back to the pool */
		while (last_allocated) {
			block = last_allocated->next;
			last_allocated->next = allocator->first_free;
			allocator->first_free = last_allocated;
			last_allocated = block;
			allocator->num_free++;
		}

		vfree(mem->block_array);
		mem->backend_info = NULL;
		mem->block_array = NULL;

		DBG_MSG(4, ("Could not find a mem-block for the allocation.\n"));
		up(&allocator->mutex);

		return 0;
	}

	mem->backend_info = last_allocated;

	up(&allocator->mutex);
	mem->is_cached=0;

	return 1;
}



static void block_allocator_release(void * ctx, ump_dd_mem * handle)
{
	block_allocator * allocator;
	block_info * block, * next;

	BUG_ON(!ctx);
	BUG_ON(!handle);

	allocator = (block_allocator*)ctx;
	block = (block_info*)handle->backend_info;
	BUG_ON(!block);

	if (down_interruptible(&allocator->mutex)) {
		MSG_ERR(("Allocator release: Failed to get mutex - memory leak\n"));
		return;
	}

	while (block) {
		next = block->next;

		BUG_ON( (block < allocator->all_blocks) || (block > (allocator->all_blocks + allocator->num_blocks)));

		block->next = allocator->first_free;
		allocator->first_free = block;
		allocator->num_free++;

		block = next;
	}
	DBG_MSG(3, ("%d blocks free after release call\n", allocator->num_free));
	up(&allocator->mutex);

	vfree(handle->block_array);
	handle->block_array = NULL;
}



/*
 * Helper function for calculating the physical base adderss of a memory block
 */
static inline u32 get_phys(block_allocator * allocator, block_info * block)
{
	return allocator->base + ((block - allocator->all_blocks) * UMP_BLOCK_SIZE);
}

static u32 block_allocator_stat(struct ump_memory_backend *backend)
{
	block_allocator *allocator;
	BUG_ON(!backend);
	allocator = (block_allocator*)backend->ctx;
	BUG_ON(!allocator);

	return (allocator->num_blocks - allocator->num_free)* UMP_BLOCK_SIZE;
}
