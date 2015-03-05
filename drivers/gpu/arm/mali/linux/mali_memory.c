/*
 * Copyright (C) 2013-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/platform_device.h>

#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_kernel_linux.h"
#include "mali_scheduler.h"
#include "mali_executor.h"
#include "mali_kernel_descriptor_mapping.h"

#include "mali_memory.h"
#include "mali_memory_dma_buf.h"
#include "mali_memory_os_alloc.h"
#include "mali_memory_block_alloc.h"

extern unsigned int mali_dedicated_mem_size;
extern unsigned int mali_shared_mem_size;

/* session->memory_lock must be held when calling this function */
static void mali_mem_release(mali_mem_allocation *descriptor)
{
	MALI_DEBUG_ASSERT_POINTER(descriptor);
	MALI_DEBUG_ASSERT_LOCK_HELD(descriptor->session->memory_lock);

	MALI_DEBUG_ASSERT(MALI_MEM_ALLOCATION_VALID_MAGIC == descriptor->magic);

	switch (descriptor->type) {
	case MALI_MEM_OS:
		mali_mem_os_release(descriptor);
		break;
	case MALI_MEM_DMA_BUF:
#if defined(CONFIG_DMA_SHARED_BUFFER)
		mali_mem_dma_buf_release(descriptor);
#endif
		break;
	case MALI_MEM_UMP:
#if defined(CONFIG_MALI400_UMP)
		mali_mem_ump_release(descriptor);
#endif
		break;
	case MALI_MEM_EXTERNAL:
		mali_mem_external_release(descriptor);
		break;
	case MALI_MEM_BLOCK:
		mali_mem_block_release(descriptor);
		break;
	default:
		MALI_DEBUG_PRINT(1, ("mem type %d is not in the mali_mem_type enum.\n", descriptor->type));
		break;
	}
}

static void mali_mem_vma_open(struct vm_area_struct *vma)
{
	mali_mem_allocation *descriptor = (mali_mem_allocation *)vma->vm_private_data;
	MALI_DEBUG_PRINT(4, ("Open called on vma %p\n", vma));

	descriptor->cpu_mapping.ref++;

	return;
}

static void mali_mem_vma_close(struct vm_area_struct *vma)
{
	mali_mem_allocation *descriptor;
	struct mali_session_data *session;
	mali_mem_virt_cpu_mapping *mapping;

	MALI_DEBUG_PRINT(3, ("Close called on vma %p\n", vma));

	descriptor = (mali_mem_allocation *)vma->vm_private_data;
	BUG_ON(!descriptor);

	MALI_DEBUG_ASSERT(MALI_MEM_ALLOCATION_VALID_MAGIC == descriptor->magic);

	mapping = &descriptor->cpu_mapping;
	BUG_ON(0 == mapping->ref);

	mapping->ref--;
	if (0 != mapping->ref) {
		MALI_DEBUG_PRINT(3, ("Ignoring this close, %d references still exists\n", mapping->ref));
		return;
	}

	session = descriptor->session;

	mali_descriptor_mapping_free(session->descriptor_mapping, descriptor->id);

	_mali_osk_mutex_wait(session->memory_lock);
	mali_mem_release(descriptor);
	_mali_osk_mutex_signal(session->memory_lock);

	mali_mem_descriptor_destroy(descriptor);
}

static int mali_kernel_memory_cpu_page_fault_handler(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	void __user *address;
	mali_mem_allocation *descriptor;

	address = vmf->virtual_address;
	descriptor = (mali_mem_allocation *)vma->vm_private_data;

	MALI_DEBUG_ASSERT(MALI_MEM_ALLOCATION_VALID_MAGIC == descriptor->magic);

	/*
	 * We always fail the call since all memory is pre-faulted when assigned to the process.
	 * Only the Mali cores can use page faults to extend buffers.
	*/

	MALI_DEBUG_PRINT(1, ("Page-fault in Mali memory region caused by the CPU.\n"));
	MALI_DEBUG_PRINT(1, ("Tried to access %p (process local virtual address) which is not currently mapped to any Mali memory.\n", (void *)address));

	MALI_IGNORE(address);
	MALI_IGNORE(descriptor);

	return VM_FAULT_SIGBUS;
}

static struct vm_operations_struct mali_kernel_vm_ops = {
	.open = mali_mem_vma_open,
	.close = mali_mem_vma_close,
	.fault = mali_kernel_memory_cpu_page_fault_handler
};

/** @note munmap handler is done by vma close handler */
int mali_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct mali_session_data *session;
	mali_mem_allocation *descriptor;
	u32 size = vma->vm_end - vma->vm_start;
	u32 mali_addr = vma->vm_pgoff << PAGE_SHIFT;

	session = (struct mali_session_data *)filp->private_data;
	if (NULL == session) {
		MALI_PRINT_ERROR(("mmap called without any session data available\n"));
		return -EFAULT;
	}

	MALI_DEBUG_PRINT(4, ("MMap() handler: start=0x%08X, phys=0x%08X, size=0x%08X vma->flags 0x%08x\n",
			     (unsigned int)vma->vm_start, (unsigned int)(vma->vm_pgoff << PAGE_SHIFT),
			     (unsigned int)(vma->vm_end - vma->vm_start), vma->vm_flags));

	/* Set some bits which indicate that, the memory is IO memory, meaning
	 * that no paging is to be performed and the memory should not be
	 * included in crash dumps. And that the memory is reserved, meaning
	 * that it's present and can never be paged out (see also previous
	 * entry)
	 */
	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_DONTCOPY;
	vma->vm_flags |= VM_PFNMAP;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
	vma->vm_flags |= VM_RESERVED;
#else
	vma->vm_flags |= VM_DONTDUMP;
	vma->vm_flags |= VM_DONTEXPAND;
#endif

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vma->vm_ops = &mali_kernel_vm_ops; /* Operations used on any memory system */

	descriptor = mali_mem_block_alloc(mali_addr, size, vma, session);
	if (NULL == descriptor) {
		descriptor = mali_mem_os_alloc(mali_addr, size, vma, session);
		if (NULL == descriptor) {
			MALI_DEBUG_PRINT(3, ("MMAP failed\n"));
			return -ENOMEM;
		}
	}

	MALI_DEBUG_ASSERT(MALI_MEM_ALLOCATION_VALID_MAGIC == descriptor->magic);

	vma->vm_private_data = (void *)descriptor;

	/* Put on descriptor map */
	if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_allocate_mapping(session->descriptor_mapping, descriptor, &descriptor->id)) {
		_mali_osk_mutex_wait(session->memory_lock);
		if (MALI_MEM_OS == descriptor->type) {
			mali_mem_os_release(descriptor);
		} else if (MALI_MEM_BLOCK == descriptor->type) {
			mali_mem_block_release(descriptor);
		}
		_mali_osk_mutex_signal(session->memory_lock);
		return -EFAULT;
	}

	return 0;
}


/* Prepare memory descriptor */
mali_mem_allocation *mali_mem_descriptor_create(struct mali_session_data *session, mali_mem_type type)
{
	mali_mem_allocation *descriptor;

	descriptor = (mali_mem_allocation *)kzalloc(sizeof(mali_mem_allocation), GFP_KERNEL);
	if (NULL == descriptor) {
		MALI_DEBUG_PRINT(3, ("mali_ukk_mem_mmap: descriptor was NULL\n"));
		return NULL;
	}

	MALI_DEBUG_CODE(descriptor->magic = MALI_MEM_ALLOCATION_VALID_MAGIC);

	descriptor->flags = 0;
	descriptor->type = type;
	descriptor->session = session;

	return descriptor;
}

void mali_mem_descriptor_destroy(mali_mem_allocation *descriptor)
{
	MALI_DEBUG_ASSERT(MALI_MEM_ALLOCATION_VALID_MAGIC == descriptor->magic);
	MALI_DEBUG_CODE(descriptor->magic = MALI_MEM_ALLOCATION_FREED_MAGIC);

	kfree(descriptor);
}

_mali_osk_errcode_t mali_mem_mali_map_prepare(mali_mem_allocation *descriptor)
{
	u32 size = descriptor->size;
	struct mali_session_data *session = descriptor->session;

	MALI_DEBUG_ASSERT(MALI_MEM_ALLOCATION_VALID_MAGIC == descriptor->magic);

	/* Map dma-buf into this session's page tables */

	if (descriptor->flags & MALI_MEM_FLAG_MALI_GUARD_PAGE) {
		size += MALI_MMU_PAGE_SIZE;
	}

	return mali_mmu_pagedir_map(session->page_directory, descriptor->mali_mapping.addr, size);
}

void mali_mem_mali_map_free(mali_mem_allocation *descriptor)
{
	u32 size = descriptor->size;
	struct mali_session_data *session = descriptor->session;

	MALI_DEBUG_ASSERT(MALI_MEM_ALLOCATION_VALID_MAGIC == descriptor->magic);

	if (descriptor->flags & MALI_MEM_FLAG_MALI_GUARD_PAGE) {
		size += MALI_MMU_PAGE_SIZE;
	}

	/* Umap and flush L2 */
	mali_mmu_pagedir_unmap(session->page_directory, descriptor->mali_mapping.addr, descriptor->size);

	mali_executor_zap_all_active(session);
}

u32 _mali_ukk_report_memory_usage(void)
{
	u32 sum = 0;

	sum += mali_mem_block_allocator_stat();
	sum += mali_mem_os_stat();

	return sum;
}

u32 _mali_ukk_report_total_memory_size(void)
{
	return mali_dedicated_mem_size + mali_shared_mem_size;
}


/**
 * Per-session memory descriptor mapping table sizes
 */
#define MALI_MEM_DESCRIPTORS_INIT 64
#define MALI_MEM_DESCRIPTORS_MAX 65536

_mali_osk_errcode_t mali_memory_session_begin(struct mali_session_data *session_data)
{
	MALI_DEBUG_PRINT(5, ("Memory session begin\n"));

	/* Create descriptor mapping table */
	session_data->descriptor_mapping = mali_descriptor_mapping_create(MALI_MEM_DESCRIPTORS_INIT, MALI_MEM_DESCRIPTORS_MAX);

	if (NULL == session_data->descriptor_mapping) {
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	session_data->memory_lock = _mali_osk_mutex_init(_MALI_OSK_LOCKFLAG_ORDERED,
				    _MALI_OSK_LOCK_ORDER_MEM_SESSION);

	if (NULL == session_data->memory_lock) {
		mali_descriptor_mapping_destroy(session_data->descriptor_mapping);
		_mali_osk_free(session_data);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	MALI_DEBUG_PRINT(5, ("MMU session begin: success\n"));
	MALI_SUCCESS;
}

/** @brief Callback function that releases memory
 *
 * session->memory_lock must be held when calling this function.
 */
static void descriptor_table_cleanup_callback(int descriptor_id, void *map_target)
{
	mali_mem_allocation *descriptor;

	descriptor = (mali_mem_allocation *)map_target;

	MALI_DEBUG_ASSERT_LOCK_HELD(descriptor->session->memory_lock);

	MALI_DEBUG_PRINT(3, ("Cleanup of descriptor %d mapping to 0x%x in descriptor table\n", descriptor_id, map_target));
	MALI_DEBUG_ASSERT(descriptor);

	mali_mem_release(descriptor);
	mali_mem_descriptor_destroy(descriptor);
}

void mali_memory_session_end(struct mali_session_data *session)
{
	MALI_DEBUG_PRINT(3, ("MMU session end\n"));

	if (NULL == session) {
		MALI_DEBUG_PRINT(1, ("No session data found during session end\n"));
		return;
	}

	/* Lock the session so we can modify the memory list */
	_mali_osk_mutex_wait(session->memory_lock);

	/* Free all allocations still in the descriptor map, and terminate the map */
	if (NULL != session->descriptor_mapping) {
		mali_descriptor_mapping_call_for_each(session->descriptor_mapping, descriptor_table_cleanup_callback);
		mali_descriptor_mapping_destroy(session->descriptor_mapping);
		session->descriptor_mapping = NULL;
	}

	_mali_osk_mutex_signal(session->memory_lock);

	/* Free the lock */
	_mali_osk_mutex_term(session->memory_lock);

	return;
}

_mali_osk_errcode_t mali_memory_initialize(void)
{
	return mali_mem_os_init();
}

void mali_memory_terminate(void)
{
	mali_mem_os_term();
	mali_mem_block_allocator_destroy(NULL);
}
