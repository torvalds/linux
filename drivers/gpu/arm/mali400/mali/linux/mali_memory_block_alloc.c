/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */
#include "mali_kernel_common.h"
#include "mali_memory.h"
#include "mali_memory_block_alloc.h"
#include "mali_osk.h"
#include <linux/mutex.h>
#define MALI_BLOCK_SIZE (256UL * 1024UL)  /* 256 kB, remember to keep the ()s */

struct block_info {
	struct block_info *next;
};

typedef struct block_info block_info;


typedef struct block_allocator {
	struct mutex mutex;
	block_info *all_blocks;
	block_info *first_free;
	u32 base;
	u32 cpu_usage_adjust;
	u32 num_blocks;
	u32 free_blocks;
} block_allocator;

static block_allocator *mali_mem_block_gobal_allocator = NULL;

MALI_STATIC_INLINE u32 get_phys(block_allocator *info, block_info *block)
{
	return info->base + ((block - info->all_blocks) * MALI_BLOCK_SIZE);
}

mali_mem_allocator *mali_mem_block_allocator_create(u32 base_address, u32 cpu_usage_adjust, u32 size)
{
	block_allocator *info;
	u32 usable_size;
	u32 num_blocks;

	usable_size = size & ~(MALI_BLOCK_SIZE - 1);
	MALI_DEBUG_PRINT(3, ("Mali block allocator create for region starting at 0x%08X length 0x%08X\n", base_address, size));
	MALI_DEBUG_PRINT(4, ("%d usable bytes\n", usable_size));
	num_blocks = usable_size / MALI_BLOCK_SIZE;
	MALI_DEBUG_PRINT(4, ("which becomes %d blocks\n", num_blocks));

	if (usable_size == 0) {
		MALI_DEBUG_PRINT(1, ("Memory block of size %d is unusable\n", size));
		return NULL;
	}

	info = _mali_osk_malloc(sizeof(block_allocator));
	if (NULL != info) {
		mutex_init(&info->mutex);
		info->all_blocks = _mali_osk_malloc(sizeof(block_info) * num_blocks);
		if (NULL != info->all_blocks) {
			u32 i;
			info->first_free = NULL;
			info->num_blocks = num_blocks;
			info->free_blocks = num_blocks;

			info->base = base_address;
			info->cpu_usage_adjust = cpu_usage_adjust;

			for ( i = 0; i < num_blocks; i++) {
				info->all_blocks[i].next = info->first_free;
				info->first_free = &info->all_blocks[i];
			}

			return (mali_mem_allocator *)info;
		}
		_mali_osk_free(info);
	}

	return NULL;
}

void mali_mem_block_allocator_destroy(mali_mem_allocator *allocator)
{
	block_allocator *info = (block_allocator*)allocator;

	info = mali_mem_block_gobal_allocator;
	if (NULL == info) return;

	MALI_DEBUG_ASSERT_POINTER(info);

	_mali_osk_free(info->all_blocks);
	_mali_osk_free(info);
}

static void mali_mem_block_mali_map(mali_mem_allocation *descriptor, u32 phys, u32 virt, u32 size)
{
	struct mali_page_directory *pagedir = descriptor->session->page_directory;
	u32 prop = descriptor->mali_mapping.properties;
	u32 offset = 0;

	while (size) {
		mali_mmu_pagedir_update(pagedir, virt + offset, phys + offset, MALI_MMU_PAGE_SIZE, prop);

		size -= MALI_MMU_PAGE_SIZE;
		offset += MALI_MMU_PAGE_SIZE;
	}
}

static int mali_mem_block_cpu_map(mali_mem_allocation *descriptor, struct vm_area_struct *vma, u32 mali_phys, u32 mapping_offset, u32 size, u32 cpu_usage_adjust)
{
	u32 virt = vma->vm_start + mapping_offset;
	u32 cpu_phys = mali_phys + cpu_usage_adjust;
	u32 offset = 0;
	int ret;

	while (size) {
		ret = vm_insert_pfn(vma, virt + offset, __phys_to_pfn(cpu_phys + offset));

		if (unlikely(ret)) {
			MALI_DEBUG_PRINT(1, ("Block allocator: Failed to insert pfn into vma\n"));
			return 1;
		}

		size -= MALI_MMU_PAGE_SIZE;
		offset += MALI_MMU_PAGE_SIZE;
	}

	return 0;
}

mali_mem_allocation *mali_mem_block_alloc(u32 mali_addr, u32 size, struct vm_area_struct *vma, struct mali_session_data *session)
{
	_mali_osk_errcode_t err;
	mali_mem_allocation *descriptor;
	block_allocator *info;
	u32 left;
	block_info *last_allocated = NULL;
	block_allocator_allocation *ret_allocation;
	u32 offset = 0;

	size = ALIGN(size, MALI_BLOCK_SIZE);

	info = mali_mem_block_gobal_allocator;
	if (NULL == info) return NULL;

	left = size;
	MALI_DEBUG_ASSERT(0 != left);

	descriptor = mali_mem_descriptor_create(session, MALI_MEM_BLOCK);
	if (NULL == descriptor) {
		return NULL;
	}

	descriptor->mali_mapping.addr = mali_addr;
	descriptor->size = size;
	descriptor->cpu_mapping.addr = (void __user*)vma->vm_start;
	descriptor->cpu_mapping.ref = 1;

	if (VM_SHARED == (VM_SHARED & vma->vm_flags)) {
		descriptor->mali_mapping.properties = MALI_MMU_FLAGS_DEFAULT;
	} else {
		/* Cached Mali memory mapping */
		descriptor->mali_mapping.properties = MALI_MMU_FLAGS_FORCE_GP_READ_ALLOCATE;
		vma->vm_flags |= VM_SHARED;
	}

	ret_allocation = &descriptor->block_mem.mem;

	ret_allocation->mapping_length = 0;

	_mali_osk_mutex_wait(session->memory_lock);
	mutex_lock(&info->mutex);

	if (left > (info->free_blocks * MALI_BLOCK_SIZE)) {
		MALI_DEBUG_PRINT(2, ("Mali block allocator: not enough free blocks to service allocation (%u)\n", left));
		mutex_unlock(&info->mutex);
		_mali_osk_mutex_signal(session->memory_lock);
		mali_mem_descriptor_destroy(descriptor);
		return NULL;
	}

	err = mali_mem_mali_map_prepare(descriptor);
	if (_MALI_OSK_ERR_OK != err) {
		mutex_unlock(&info->mutex);
		_mali_osk_mutex_signal(session->memory_lock);
		mali_mem_descriptor_destroy(descriptor);
		return NULL;
	}

	while ((left > 0) && (info->first_free)) {
		block_info *block;
		u32 phys_addr;
		u32 current_mapping_size;

		block = info->first_free;
		info->first_free = info->first_free->next;
		block->next = last_allocated;
		last_allocated = block;

		phys_addr = get_phys(info, block);

		if (MALI_BLOCK_SIZE < left) {
			current_mapping_size = MALI_BLOCK_SIZE;
		} else {
			current_mapping_size = left;
		}

		mali_mem_block_mali_map(descriptor, phys_addr, mali_addr + offset, current_mapping_size);
		if (mali_mem_block_cpu_map(descriptor, vma, phys_addr, offset, current_mapping_size, info->cpu_usage_adjust)) {
			/* release all memory back to the pool */
			while (last_allocated) {
				/* This relinks every block we've just allocated back into the free-list */
				block = last_allocated->next;
				last_allocated->next = info->first_free;
				info->first_free = last_allocated;
				last_allocated = block;
			}

			mutex_unlock(&info->mutex);
			_mali_osk_mutex_signal(session->memory_lock);

			mali_mem_mali_map_free(descriptor);
			mali_mem_descriptor_destroy(descriptor);

			return NULL;
		}

		left -= current_mapping_size;
		offset += current_mapping_size;
		ret_allocation->mapping_length += current_mapping_size;

		--info->free_blocks;
	}

	mutex_unlock(&info->mutex);
	_mali_osk_mutex_signal(session->memory_lock);

	MALI_DEBUG_ASSERT(0 == left);

	/* Record all the information about this allocation */
	ret_allocation->last_allocated = last_allocated;
	ret_allocation->info = info;

	return descriptor;
}

void mali_mem_block_release(mali_mem_allocation *descriptor)
{
	block_allocator *info = descriptor->block_mem.mem.info;
	block_info *block, *next;
	block_allocator_allocation *allocation = &descriptor->block_mem.mem;

	MALI_DEBUG_ASSERT(MALI_MEM_BLOCK == descriptor->type);

	block = allocation->last_allocated;

	MALI_DEBUG_ASSERT_POINTER(block);

	/* unmap */
	mali_mem_mali_map_free(descriptor);

	mutex_lock(&info->mutex);

	while (block) {
		MALI_DEBUG_ASSERT(!((block < info->all_blocks) || (block > (info->all_blocks + info->num_blocks))));

		next = block->next;

		/* relink into free-list */
		block->next = info->first_free;
		info->first_free = block;

		/* advance the loop */
		block = next;

		++info->free_blocks;
	}

	mutex_unlock(&info->mutex);
}

u32 mali_mem_block_allocator_stat(void)
{
	block_allocator *info = (block_allocator *)mali_mem_block_gobal_allocator;

	if (NULL == info) return 0;

	MALI_DEBUG_ASSERT_POINTER(info);

	return (info->num_blocks - info->free_blocks) * MALI_BLOCK_SIZE;
}

_mali_osk_errcode_t mali_memory_core_resource_dedicated_memory(u32 start, u32 size)
{
	mali_mem_allocator *allocator;

	/* Do the low level linux operation first */

	/* Request ownership of the memory */
	if (_MALI_OSK_ERR_OK != _mali_osk_mem_reqregion(start, size, "Dedicated Mali GPU memory")) {
		MALI_DEBUG_PRINT(1, ("Failed to request memory region for frame buffer (0x%08X - 0x%08X)\n", start, start + size - 1));
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create generic block allocator object to handle it */
	allocator = mali_mem_block_allocator_create(start, 0 /* cpu_usage_adjust */, size);

	if (NULL == allocator) {
		MALI_DEBUG_PRINT(1, ("Memory bank registration failed\n"));
		_mali_osk_mem_unreqregion(start, size);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	mali_mem_block_gobal_allocator = (block_allocator*)allocator;

	return _MALI_OSK_ERR_OK;
}
