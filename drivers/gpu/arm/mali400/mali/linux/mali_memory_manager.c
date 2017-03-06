/*
 * Copyright (C) 2013-2017 ARM Limited. All rights reserved.
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
#include <linux/sched.h>

#include <linux/platform_device.h>
#if defined(CONFIG_DMA_SHARED_BUFFER)
#include <linux/dma-buf.h>
#endif
#include <linux/idr.h>

#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_kernel_linux.h"
#include "mali_scheduler.h"
#include "mali_memory.h"
#include "mali_memory_os_alloc.h"
#if defined(CONFIG_DMA_SHARED_BUFFER)
#include "mali_memory_dma_buf.h"
#include "mali_memory_secure.h"
#endif
#if defined(CONFIG_MALI400_UMP)
#include "mali_memory_ump.h"
#endif
#include "mali_memory_manager.h"
#include "mali_memory_virtual.h"
#include "mali_memory_util.h"
#include "mali_memory_external.h"
#include "mali_memory_cow.h"
#include "mali_memory_block_alloc.h"
#include "mali_ukk.h"
#include "mali_memory_swap_alloc.h"

/*
* New memory system interface
*/

/*inti idr for backend memory */
struct idr mali_backend_idr;
struct mutex mali_idr_mutex;

/* init allocation manager */
int mali_memory_manager_init(struct mali_allocation_manager *mgr)
{
	/* init Locks */
	rwlock_init(&mgr->vm_lock);
	mutex_init(&mgr->list_mutex);

	/* init link */
	INIT_LIST_HEAD(&mgr->head);

	/* init RB tree */
	mgr->allocation_mgr_rb = RB_ROOT;
	mgr->mali_allocation_num = 0;
	return 0;
}

/* Deinit allocation manager
* Do some check for debug
*/
void mali_memory_manager_uninit(struct mali_allocation_manager *mgr)
{
	/* check RB tree is empty */
	MALI_DEBUG_ASSERT(((void *)(mgr->allocation_mgr_rb.rb_node) == (void *)rb_last(&mgr->allocation_mgr_rb)));
	/* check allocation List */
	MALI_DEBUG_ASSERT(list_empty(&mgr->head));
}

/* Prepare memory descriptor */
static mali_mem_allocation *mali_mem_allocation_struct_create(struct mali_session_data *session)
{
	mali_mem_allocation *mali_allocation;

	/* Allocate memory */
	mali_allocation = (mali_mem_allocation *)kzalloc(sizeof(mali_mem_allocation), GFP_KERNEL);
	if (NULL == mali_allocation) {
		MALI_DEBUG_PRINT(1, ("mali_mem_allocation_struct_create: descriptor was NULL\n"));
		return NULL;
	}

	MALI_DEBUG_CODE(mali_allocation->magic = MALI_MEM_ALLOCATION_VALID_MAGIC);

	/* do init */
	mali_allocation->flags = 0;
	mali_allocation->session = session;

	INIT_LIST_HEAD(&mali_allocation->list);
	_mali_osk_atomic_init(&mali_allocation->mem_alloc_refcount, 1);

	/**
	*add to session list
	*/
	mutex_lock(&session->allocation_mgr.list_mutex);
	list_add_tail(&mali_allocation->list, &session->allocation_mgr.head);
	session->allocation_mgr.mali_allocation_num++;
	mutex_unlock(&session->allocation_mgr.list_mutex);

	return mali_allocation;
}

void  mali_mem_allocation_struct_destory(mali_mem_allocation *alloc)
{
	MALI_DEBUG_ASSERT_POINTER(alloc);
	MALI_DEBUG_ASSERT_POINTER(alloc->session);
	mutex_lock(&alloc->session->allocation_mgr.list_mutex);
	list_del(&alloc->list);
	alloc->session->allocation_mgr.mali_allocation_num--;
	mutex_unlock(&alloc->session->allocation_mgr.list_mutex);

	kfree(alloc);
}

int mali_mem_backend_struct_create(mali_mem_backend **backend, u32 psize)
{
	mali_mem_backend *mem_backend = NULL;
	s32 ret = -ENOSPC;
	s32 index = -1;
	*backend = (mali_mem_backend *)kzalloc(sizeof(mali_mem_backend), GFP_KERNEL);
	if (NULL == *backend) {
		MALI_DEBUG_PRINT(1, ("mali_mem_backend_struct_create: backend descriptor was NULL\n"));
		return -1;
	}
	mem_backend = *backend;
	mem_backend->size = psize;
	mutex_init(&mem_backend->mutex);
	INIT_LIST_HEAD(&mem_backend->list);
	mem_backend->using_count = 0;


	/* link backend with id */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
again:
	if (!idr_pre_get(&mali_backend_idr, GFP_KERNEL)) {
		kfree(mem_backend);
		return -ENOMEM;
	}
	mutex_lock(&mali_idr_mutex);
	ret = idr_get_new_above(&mali_backend_idr, mem_backend, 1, &index);
	mutex_unlock(&mali_idr_mutex);

	if (-ENOSPC == ret) {
		kfree(mem_backend);
		return -ENOSPC;
	}
	if (-EAGAIN == ret)
		goto again;
#else
	mutex_lock(&mali_idr_mutex);
	ret = idr_alloc(&mali_backend_idr, mem_backend, 1, MALI_S32_MAX, GFP_KERNEL);
	mutex_unlock(&mali_idr_mutex);
	index = ret;
	if (ret < 0) {
		MALI_DEBUG_PRINT(1, ("mali_mem_backend_struct_create: Can't allocate idr for backend! \n"));
		kfree(mem_backend);
		return -ENOSPC;
	}
#endif
	return index;
}


static void mali_mem_backend_struct_destory(mali_mem_backend **backend, s32 backend_handle)
{
	mali_mem_backend *mem_backend = *backend;

	mutex_lock(&mali_idr_mutex);
	idr_remove(&mali_backend_idr, backend_handle);
	mutex_unlock(&mali_idr_mutex);
	kfree(mem_backend);
	*backend = NULL;
}

mali_mem_backend *mali_mem_backend_struct_search(struct mali_session_data *session, u32 mali_address)
{
	struct mali_vma_node *mali_vma_node = NULL;
	mali_mem_backend *mem_bkend = NULL;
	mali_mem_allocation *mali_alloc = NULL;
	MALI_DEBUG_ASSERT_POINTER(session);
	mali_vma_node = mali_vma_offset_search(&session->allocation_mgr, mali_address, 0);
	if (NULL == mali_vma_node)  {
		MALI_DEBUG_PRINT(1, ("mali_mem_backend_struct_search:vma node was NULL\n"));
		return NULL;
	}
	mali_alloc = container_of(mali_vma_node, struct mali_mem_allocation, mali_vma_node);
	/* Get backend memory & Map on CPU */
	mutex_lock(&mali_idr_mutex);
	mem_bkend = idr_find(&mali_backend_idr, mali_alloc->backend_handle);
	mutex_unlock(&mali_idr_mutex);
	MALI_DEBUG_ASSERT(NULL != mem_bkend);
	return mem_bkend;
}

static _mali_osk_errcode_t mali_mem_resize(struct mali_session_data *session, mali_mem_backend *mem_backend, u32 physical_size)
{
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_FAULT;
	int retval = 0;
	mali_mem_allocation *mali_allocation = NULL;
	mali_mem_os_mem tmp_os_mem;
	s32 change_page_count;

	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT_POINTER(mem_backend);
	MALI_DEBUG_PRINT(4, (" mali_mem_resize_memory called! \n"));
	MALI_DEBUG_ASSERT(0 == physical_size %  MALI_MMU_PAGE_SIZE);

	mali_allocation = mem_backend->mali_allocation;
	MALI_DEBUG_ASSERT_POINTER(mali_allocation);

	MALI_DEBUG_ASSERT(MALI_MEM_FLAG_CAN_RESIZE & mali_allocation->flags);
	MALI_DEBUG_ASSERT(MALI_MEM_OS == mali_allocation->type);

	mutex_lock(&mem_backend->mutex);

	/* Do resize*/
	if (physical_size > mem_backend->size) {
		u32 add_size = physical_size - mem_backend->size;

		MALI_DEBUG_ASSERT(0 == add_size %  MALI_MMU_PAGE_SIZE);

		/* Allocate new pages from os mem */
		retval = mali_mem_os_alloc_pages(&tmp_os_mem, add_size);

		if (retval) {
			if (-ENOMEM == retval) {
				ret = _MALI_OSK_ERR_NOMEM;
			} else {
				ret = _MALI_OSK_ERR_FAULT;
			}
			MALI_DEBUG_PRINT(2, ("_mali_ukk_mem_resize: memory allocation failed !\n"));
			goto failed_alloc_memory;
		}

		MALI_DEBUG_ASSERT(tmp_os_mem.count == add_size / MALI_MMU_PAGE_SIZE);

		/* Resize the memory of the backend */
		ret = mali_mem_os_resize_pages(&tmp_os_mem, &mem_backend->os_mem, 0, tmp_os_mem.count);

		if (ret) {
			MALI_DEBUG_PRINT(2, ("_mali_ukk_mem_resize: memory	resizing failed !\n"));
			goto failed_resize_pages;
		}

		/*Resize cpu mapping */
		if (NULL != mali_allocation->cpu_mapping.vma) {
			ret = mali_mem_os_resize_cpu_map_locked(mem_backend, mali_allocation->cpu_mapping.vma, mali_allocation->cpu_mapping.vma->vm_start  + mem_backend->size, add_size);
			if (unlikely(ret != _MALI_OSK_ERR_OK)) {
				MALI_DEBUG_PRINT(2, ("_mali_ukk_mem_resize: cpu mapping failed !\n"));
				goto  failed_cpu_map;
			}
		}

		/* Resize mali mapping */
		_mali_osk_mutex_wait(session->memory_lock);
		ret = mali_mem_mali_map_resize(mali_allocation, physical_size);

		if (ret) {
			MALI_DEBUG_PRINT(1, ("_mali_ukk_mem_resize: mali map resize fail !\n"));
			goto failed_gpu_map;
		}

		ret = mali_mem_os_mali_map(&mem_backend->os_mem, session, mali_allocation->mali_vma_node.vm_node.start,
					   mali_allocation->psize / MALI_MMU_PAGE_SIZE, add_size / MALI_MMU_PAGE_SIZE, mali_allocation->mali_mapping.properties);
		if (ret) {
			MALI_DEBUG_PRINT(2, ("_mali_ukk_mem_resize: mali mapping failed !\n"));
			goto failed_gpu_map;
		}

		_mali_osk_mutex_signal(session->memory_lock);
	} else {
		u32 dec_size, page_count;
		u32 vaddr = 0;
		INIT_LIST_HEAD(&tmp_os_mem.pages);
		tmp_os_mem.count = 0;

		dec_size = mem_backend->size - physical_size;
		MALI_DEBUG_ASSERT(0 == dec_size %  MALI_MMU_PAGE_SIZE);

		page_count = dec_size / MALI_MMU_PAGE_SIZE;
		vaddr = mali_allocation->mali_vma_node.vm_node.start + physical_size;

		/* Resize the memory of the backend */
		ret = mali_mem_os_resize_pages(&mem_backend->os_mem, &tmp_os_mem, physical_size / MALI_MMU_PAGE_SIZE, page_count);

		if (ret) {
			MALI_DEBUG_PRINT(4, ("_mali_ukk_mem_resize: mali map resize failed!\n"));
			goto failed_resize_pages;
		}

		/* Resize mali map */
		_mali_osk_mutex_wait(session->memory_lock);
		mali_mem_mali_map_free(session, dec_size, vaddr, mali_allocation->flags);
		_mali_osk_mutex_signal(session->memory_lock);

		/* Zap cpu mapping */
		if (0 != mali_allocation->cpu_mapping.addr) {
			MALI_DEBUG_ASSERT(NULL != mali_allocation->cpu_mapping.vma);
			zap_vma_ptes(mali_allocation->cpu_mapping.vma, mali_allocation->cpu_mapping.vma->vm_start + physical_size, dec_size);
		}

		/* Free those extra pages */
		mali_mem_os_free(&tmp_os_mem.pages, tmp_os_mem.count, MALI_FALSE);
	}

	/* Resize memory allocation and memory backend */
	change_page_count = (s32)(physical_size - mem_backend->size) / MALI_MMU_PAGE_SIZE;
	mali_allocation->psize = physical_size;
	mem_backend->size = physical_size;
	mutex_unlock(&mem_backend->mutex);

	if (change_page_count > 0) {
		atomic_add(change_page_count, &session->mali_mem_allocated_pages);
		if (atomic_read(&session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE > session->max_mali_mem_allocated_size) {
			session->max_mali_mem_allocated_size = atomic_read(&session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE;
		}

	} else {
		atomic_sub((s32)(-change_page_count), &session->mali_mem_allocated_pages);
	}

	return _MALI_OSK_ERR_OK;

failed_gpu_map:
	_mali_osk_mutex_signal(session->memory_lock);
failed_cpu_map:
	if (physical_size > mem_backend->size) {
		mali_mem_os_resize_pages(&mem_backend->os_mem, &tmp_os_mem, mem_backend->size / MALI_MMU_PAGE_SIZE,
					 (physical_size - mem_backend->size) / MALI_MMU_PAGE_SIZE);
	} else {
		mali_mem_os_resize_pages(&tmp_os_mem, &mem_backend->os_mem, 0, tmp_os_mem.count);
	}
failed_resize_pages:
	if (0 != tmp_os_mem.count)
		mali_mem_os_free(&tmp_os_mem.pages, tmp_os_mem.count, MALI_FALSE);
failed_alloc_memory:

	mutex_unlock(&mem_backend->mutex);
	return ret;
}


/* Set GPU MMU properties */
static void _mali_memory_gpu_map_property_set(u32 *properties, u32 flags)
{
	if (_MALI_MEMORY_GPU_READ_ALLOCATE & flags) {
		*properties = MALI_MMU_FLAGS_FORCE_GP_READ_ALLOCATE;
	} else {
		*properties = MALI_MMU_FLAGS_DEFAULT;
	}
}

_mali_osk_errcode_t mali_mem_add_mem_size(struct mali_session_data *session, u32 mali_addr, u32 add_size)
{
	mali_mem_backend *mem_backend = NULL;
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_FAULT;
	mali_mem_allocation *mali_allocation = NULL;
	u32 new_physical_size;
	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT(0 == add_size %  MALI_MMU_PAGE_SIZE);

	/* Get the memory backend that need to be resize. */
	mem_backend = mali_mem_backend_struct_search(session, mali_addr);

	if (NULL == mem_backend)  {
		MALI_DEBUG_PRINT(2, ("_mali_ukk_mem_resize: memory backend = NULL!\n"));
		return ret;
	}

	mali_allocation = mem_backend->mali_allocation;

	MALI_DEBUG_ASSERT_POINTER(mali_allocation);

	new_physical_size = add_size + mem_backend->size;

	if (new_physical_size > (mali_allocation->mali_vma_node.vm_node.size))
		return ret;

	MALI_DEBUG_ASSERT(new_physical_size != mem_backend->size);

	ret = mali_mem_resize(session, mem_backend, new_physical_size);

	return ret;
}

/**
*  function@_mali_ukk_mem_allocate - allocate mali memory
*/
_mali_osk_errcode_t _mali_ukk_mem_allocate(_mali_uk_alloc_mem_s *args)
{
	struct mali_session_data *session = (struct mali_session_data *)(uintptr_t)args->ctx;
	mali_mem_backend *mem_backend = NULL;
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_FAULT;
	int retval = 0;
	mali_mem_allocation *mali_allocation = NULL;
	struct mali_vma_node *mali_vma_node = NULL;

	MALI_DEBUG_PRINT(4, (" _mali_ukk_mem_allocate, vaddr=0x%x, size =0x%x! \n", args->gpu_vaddr, args->psize));

	/* Check if the address is allocated
	*/
	mali_vma_node = mali_vma_offset_search(&session->allocation_mgr, args->gpu_vaddr, 0);

	if (unlikely(mali_vma_node)) {
		MALI_DEBUG_PRINT_ERROR(("The mali virtual address has already been used ! \n"));
		return _MALI_OSK_ERR_FAULT;
	}
	/**
	*create mali memory allocation
	*/

	mali_allocation = mali_mem_allocation_struct_create(session);

	if (mali_allocation == NULL) {
		MALI_DEBUG_PRINT(1, ("_mali_ukk_mem_allocate: Failed to create allocation struct! \n"));
		return _MALI_OSK_ERR_NOMEM;
	}
	mali_allocation->psize = args->psize;
	mali_allocation->vsize = args->vsize;

	/* MALI_MEM_OS if need to support mem resize,
	 * or MALI_MEM_BLOCK if have dedicated memory,
	 * or MALI_MEM_OS,
	 * or MALI_MEM_SWAP.
	 */
	if (args->flags & _MALI_MEMORY_ALLOCATE_SWAPPABLE) {
		mali_allocation->type = MALI_MEM_SWAP;
	} else if (args->flags & _MALI_MEMORY_ALLOCATE_RESIZEABLE) {
		mali_allocation->type = MALI_MEM_OS;
		mali_allocation->flags |= MALI_MEM_FLAG_CAN_RESIZE;
	} else if (args->flags & _MALI_MEMORY_ALLOCATE_SECURE) {
		mali_allocation->type = MALI_MEM_SECURE;
	} else if (MALI_TRUE == mali_memory_have_dedicated_memory()) {
		mali_allocation->type = MALI_MEM_BLOCK;
	} else {
		mali_allocation->type = MALI_MEM_OS;
	}

	/**
	*add allocation node to RB tree for index
	*/
	mali_allocation->mali_vma_node.vm_node.start = args->gpu_vaddr;
	mali_allocation->mali_vma_node.vm_node.size = args->vsize;

	mali_vma_offset_add(&session->allocation_mgr, &mali_allocation->mali_vma_node);

	mali_allocation->backend_handle = mali_mem_backend_struct_create(&mem_backend, args->psize);
	if (mali_allocation->backend_handle < 0) {
		ret = _MALI_OSK_ERR_NOMEM;
		MALI_DEBUG_PRINT(1, ("mali_allocation->backend_handle < 0! \n"));
		goto failed_alloc_backend;
	}


	mem_backend->mali_allocation = mali_allocation;
	mem_backend->type = mali_allocation->type;

	mali_allocation->mali_mapping.addr = args->gpu_vaddr;

	/* set gpu mmu propery */
	_mali_memory_gpu_map_property_set(&mali_allocation->mali_mapping.properties, args->flags);
	/* do prepare for MALI mapping */
	if (!(args->flags & _MALI_MEMORY_ALLOCATE_NO_BIND_GPU) && mali_allocation->psize > 0) {
		_mali_osk_mutex_wait(session->memory_lock);

		ret = mali_mem_mali_map_prepare(mali_allocation);
		if (0 != ret) {
			_mali_osk_mutex_signal(session->memory_lock);
			goto failed_prepare_map;
		}
		_mali_osk_mutex_signal(session->memory_lock);
	}

	if (mali_allocation->psize == 0) {
		mem_backend->os_mem.count = 0;
		INIT_LIST_HEAD(&mem_backend->os_mem.pages);
		goto done;
	}

	if (args->flags & _MALI_MEMORY_ALLOCATE_DEFER_BIND) {
		mali_allocation->flags |= _MALI_MEMORY_ALLOCATE_DEFER_BIND;
		mem_backend->flags |= MALI_MEM_BACKEND_FLAG_NOT_BINDED;
		/* init for defer bind backend*/
		mem_backend->os_mem.count = 0;
		INIT_LIST_HEAD(&mem_backend->os_mem.pages);

		goto done;
	}

	if (likely(mali_allocation->psize > 0)) {

		if (MALI_MEM_SECURE == mem_backend->type) {
#if defined(CONFIG_DMA_SHARED_BUFFER)
			ret = mali_mem_secure_attach_dma_buf(&mem_backend->secure_mem, mem_backend->size, args->secure_shared_fd);
			if (_MALI_OSK_ERR_OK != ret) {
				MALI_DEBUG_PRINT(1, ("Failed to attach dma buf for secure memory! \n"));
				goto failed_alloc_pages;
			}
#else
			ret = _MALI_OSK_ERR_UNSUPPORTED;
			MALI_DEBUG_PRINT(1, ("DMA not supported for mali secure memory! \n"));
			goto failed_alloc_pages;
#endif
		} else {

			/**
			*allocate physical memory
			*/
			if (mem_backend->type == MALI_MEM_OS) {
				retval = mali_mem_os_alloc_pages(&mem_backend->os_mem, mem_backend->size);
			} else if (mem_backend->type == MALI_MEM_BLOCK) {
				/* try to allocated from BLOCK memory first, then try OS memory if failed.*/
				if (mali_mem_block_alloc(&mem_backend->block_mem, mem_backend->size)) {
					retval = mali_mem_os_alloc_pages(&mem_backend->os_mem, mem_backend->size);
					mem_backend->type = MALI_MEM_OS;
					mali_allocation->type = MALI_MEM_OS;
				}
			} else if (MALI_MEM_SWAP == mem_backend->type) {
				retval = mali_mem_swap_alloc_pages(&mem_backend->swap_mem, mali_allocation->mali_vma_node.vm_node.size, &mem_backend->start_idx);
			}  else {
				/* ONLY support mem_os type */
				MALI_DEBUG_ASSERT(0);
			}

			if (retval) {
				ret = _MALI_OSK_ERR_NOMEM;
				MALI_DEBUG_PRINT(1, (" can't allocate enough pages! \n"));
				goto failed_alloc_pages;
			}
		}
	}

	/**
	*map to GPU side
	*/
	if (!(args->flags & _MALI_MEMORY_ALLOCATE_NO_BIND_GPU) && mali_allocation->psize > 0) {
		_mali_osk_mutex_wait(session->memory_lock);
		/* Map on Mali */

		if (mem_backend->type == MALI_MEM_OS) {
			ret = mali_mem_os_mali_map(&mem_backend->os_mem, session, args->gpu_vaddr, 0,
						   mem_backend->size / MALI_MMU_PAGE_SIZE, mali_allocation->mali_mapping.properties);

		} else if (mem_backend->type == MALI_MEM_BLOCK) {
			mali_mem_block_mali_map(&mem_backend->block_mem, session, args->gpu_vaddr,
						mali_allocation->mali_mapping.properties);
		} else if (mem_backend->type == MALI_MEM_SWAP) {
			ret = mali_mem_swap_mali_map(&mem_backend->swap_mem, session, args->gpu_vaddr,
						     mali_allocation->mali_mapping.properties);
		} else if (mem_backend->type == MALI_MEM_SECURE) {
#if defined(CONFIG_DMA_SHARED_BUFFER)
			ret = mali_mem_secure_mali_map(&mem_backend->secure_mem, session, args->gpu_vaddr, mali_allocation->mali_mapping.properties);
#endif
		} else { /* unsupport type */
			MALI_DEBUG_ASSERT(0);
		}

		_mali_osk_mutex_signal(session->memory_lock);
	}
done:
	if (MALI_MEM_OS == mem_backend->type) {
		atomic_add(mem_backend->os_mem.count, &session->mali_mem_allocated_pages);
	} else if (MALI_MEM_BLOCK == mem_backend->type) {
		atomic_add(mem_backend->block_mem.count, &session->mali_mem_allocated_pages);
	} else if (MALI_MEM_SECURE == mem_backend->type) {
		atomic_add(mem_backend->secure_mem.count, &session->mali_mem_allocated_pages);
	} else {
		MALI_DEBUG_ASSERT(MALI_MEM_SWAP == mem_backend->type);
		atomic_add(mem_backend->swap_mem.count, &session->mali_mem_allocated_pages);
		atomic_add(mem_backend->swap_mem.count, &session->mali_mem_array[mem_backend->type]);
	}

	if (atomic_read(&session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE > session->max_mali_mem_allocated_size) {
		session->max_mali_mem_allocated_size = atomic_read(&session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE;
	}
	return _MALI_OSK_ERR_OK;

failed_alloc_pages:
	mali_mem_mali_map_free(session, mali_allocation->psize, mali_allocation->mali_vma_node.vm_node.start, mali_allocation->flags);
failed_prepare_map:
	mali_mem_backend_struct_destory(&mem_backend, mali_allocation->backend_handle);
failed_alloc_backend:

	mali_vma_offset_remove(&session->allocation_mgr, &mali_allocation->mali_vma_node);
	mali_mem_allocation_struct_destory(mali_allocation);

	return ret;
}


_mali_osk_errcode_t _mali_ukk_mem_free(_mali_uk_free_mem_s *args)
{
	struct  mali_session_data *session = (struct mali_session_data *)(uintptr_t)args->ctx;
	u32 vaddr = args->gpu_vaddr;
	mali_mem_allocation *mali_alloc = NULL;
	struct mali_vma_node *mali_vma_node = NULL;

	/* find mali allocation structure by vaddress*/
	mali_vma_node = mali_vma_offset_search(&session->allocation_mgr, vaddr, 0);
	if (NULL == mali_vma_node) {
		MALI_DEBUG_PRINT(1, ("_mali_ukk_mem_free: invalid addr: 0x%x\n", vaddr));
		return _MALI_OSK_ERR_INVALID_ARGS;
	}
	MALI_DEBUG_ASSERT(NULL != mali_vma_node);
	mali_alloc = container_of(mali_vma_node, struct mali_mem_allocation, mali_vma_node);

	if (mali_alloc)
		/* check ref_count */
		args->free_pages_nr = mali_allocation_unref(&mali_alloc);

	return _MALI_OSK_ERR_OK;
}


/**
* Function _mali_ukk_mem_bind -- bind a external memory to a new GPU address
* It will allocate a new mem allocation and bind external memory to it.
* Supported backend type are:
* _MALI_MEMORY_BIND_BACKEND_UMP
* _MALI_MEMORY_BIND_BACKEND_DMA_BUF
* _MALI_MEMORY_BIND_BACKEND_EXTERNAL_MEMORY
* CPU access is not supported yet
*/
_mali_osk_errcode_t _mali_ukk_mem_bind(_mali_uk_bind_mem_s *args)
{
	struct  mali_session_data *session = (struct mali_session_data *)(uintptr_t)args->ctx;
	mali_mem_backend *mem_backend = NULL;
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_FAULT;
	mali_mem_allocation *mali_allocation = NULL;
	MALI_DEBUG_PRINT(5, (" _mali_ukk_mem_bind, vaddr=0x%x, size =0x%x! \n", args->vaddr, args->size));

	/**
	* allocate mali allocation.
	*/
	mali_allocation = mali_mem_allocation_struct_create(session);

	if (mali_allocation == NULL) {
		return _MALI_OSK_ERR_NOMEM;
	}
	mali_allocation->psize = args->size;
	mali_allocation->vsize = args->size;
	mali_allocation->mali_mapping.addr = args->vaddr;

	/* add allocation node to RB tree for index  */
	mali_allocation->mali_vma_node.vm_node.start = args->vaddr;
	mali_allocation->mali_vma_node.vm_node.size = args->size;
	mali_vma_offset_add(&session->allocation_mgr, &mali_allocation->mali_vma_node);

	/* allocate backend*/
	if (mali_allocation->psize > 0) {
		mali_allocation->backend_handle = mali_mem_backend_struct_create(&mem_backend, mali_allocation->psize);
		if (mali_allocation->backend_handle < 0) {
			goto Failed_alloc_backend;
		}

	} else {
		goto Failed_alloc_backend;
	}

	mem_backend->size = mali_allocation->psize;
	mem_backend->mali_allocation = mali_allocation;

	switch (args->flags & _MALI_MEMORY_BIND_BACKEND_MASK) {
	case  _MALI_MEMORY_BIND_BACKEND_UMP:
#if defined(CONFIG_MALI400_UMP)
		mali_allocation->type = MALI_MEM_UMP;
		mem_backend->type = MALI_MEM_UMP;
		ret = mali_mem_bind_ump_buf(mali_allocation, mem_backend,
					    args->mem_union.bind_ump.secure_id, args->mem_union.bind_ump.flags);
		if (_MALI_OSK_ERR_OK != ret) {
			MALI_DEBUG_PRINT(1, ("Bind ump buf failed\n"));
			goto  Failed_bind_backend;
		}
#else
		MALI_DEBUG_PRINT(1, ("UMP not supported\n"));
		goto Failed_bind_backend;
#endif
		break;
	case  _MALI_MEMORY_BIND_BACKEND_DMA_BUF:
#if defined(CONFIG_DMA_SHARED_BUFFER)
		mali_allocation->type = MALI_MEM_DMA_BUF;
		mem_backend->type = MALI_MEM_DMA_BUF;
		ret = mali_mem_bind_dma_buf(mali_allocation, mem_backend,
					    args->mem_union.bind_dma_buf.mem_fd, args->mem_union.bind_dma_buf.flags);
		if (_MALI_OSK_ERR_OK != ret) {
			MALI_DEBUG_PRINT(1, ("Bind dma buf failed\n"));
			goto Failed_bind_backend;
		}
#else
		MALI_DEBUG_PRINT(1, ("DMA not supported\n"));
		goto Failed_bind_backend;
#endif
		break;
	case _MALI_MEMORY_BIND_BACKEND_MALI_MEMORY:
		/* not allowed */
		MALI_DEBUG_PRINT_ERROR(("Mali internal memory type not supported !\n"));
		goto Failed_bind_backend;
		break;

	case _MALI_MEMORY_BIND_BACKEND_EXTERNAL_MEMORY:
		mali_allocation->type = MALI_MEM_EXTERNAL;
		mem_backend->type = MALI_MEM_EXTERNAL;
		ret = mali_mem_bind_ext_buf(mali_allocation, mem_backend, args->mem_union.bind_ext_memory.phys_addr,
					    args->mem_union.bind_ext_memory.flags);
		if (_MALI_OSK_ERR_OK != ret) {
			MALI_DEBUG_PRINT(1, ("Bind external buf failed\n"));
			goto Failed_bind_backend;
		}
		break;

	case _MALI_MEMORY_BIND_BACKEND_EXT_COW:
		/* not allowed */
		MALI_DEBUG_PRINT_ERROR(("External cow memory  type not supported !\n"));
		goto Failed_bind_backend;
		break;

	default:
		MALI_DEBUG_PRINT_ERROR(("Invalid memory type  not supported !\n"));
		goto Failed_bind_backend;
		break;
	}
	MALI_DEBUG_ASSERT(0 == mem_backend->size % MALI_MMU_PAGE_SIZE);
	atomic_add(mem_backend->size / MALI_MMU_PAGE_SIZE, &session->mali_mem_array[mem_backend->type]);
	return _MALI_OSK_ERR_OK;

Failed_bind_backend:
	mali_mem_backend_struct_destory(&mem_backend, mali_allocation->backend_handle);

Failed_alloc_backend:
	mali_vma_offset_remove(&session->allocation_mgr, &mali_allocation->mali_vma_node);
	mali_mem_allocation_struct_destory(mali_allocation);

	MALI_DEBUG_PRINT(1, (" _mali_ukk_mem_bind, return ERROR! \n"));
	return ret;
}


/*
* Function _mali_ukk_mem_unbind -- unbind a external memory to a new GPU address
* This function unbind the backend memory and free the allocation
* no ref_count for this type of memory
*/
_mali_osk_errcode_t _mali_ukk_mem_unbind(_mali_uk_unbind_mem_s *args)
{
	/**/
	struct  mali_session_data *session = (struct mali_session_data *)(uintptr_t)args->ctx;
	mali_mem_allocation *mali_allocation = NULL;
	struct mali_vma_node *mali_vma_node = NULL;
	u32 mali_addr = args->vaddr;
	MALI_DEBUG_PRINT(5, (" _mali_ukk_mem_unbind, vaddr=0x%x! \n", args->vaddr));

	/* find the allocation by vaddr */
	mali_vma_node = mali_vma_offset_search(&session->allocation_mgr, mali_addr, 0);
	if (likely(mali_vma_node)) {
		MALI_DEBUG_ASSERT(mali_addr == mali_vma_node->vm_node.start);
		mali_allocation = container_of(mali_vma_node, struct mali_mem_allocation, mali_vma_node);
	} else {
		MALI_DEBUG_ASSERT(NULL != mali_vma_node);
		return _MALI_OSK_ERR_INVALID_ARGS;
	}

	if (NULL != mali_allocation)
		/* check ref_count */
		mali_allocation_unref(&mali_allocation);
	return _MALI_OSK_ERR_OK;
}

/*
* Function _mali_ukk_mem_cow --  COW for an allocation
* This function allocate new pages for  a range (range, range+size) of allocation
*  And Map it(keep use the not in range pages from target allocation ) to an GPU vaddr
*/
_mali_osk_errcode_t _mali_ukk_mem_cow(_mali_uk_cow_mem_s *args)
{
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_FAULT;
	mali_mem_backend *target_backend = NULL;
	mali_mem_backend *mem_backend = NULL;
	struct mali_vma_node *mali_vma_node = NULL;
	mali_mem_allocation *mali_allocation = NULL;

	struct  mali_session_data *session = (struct mali_session_data *)(uintptr_t)args->ctx;
	/* Get the target backend for cow */
	target_backend = mali_mem_backend_struct_search(session, args->target_handle);

	if (NULL == target_backend || 0 == target_backend->size) {
		MALI_DEBUG_ASSERT_POINTER(target_backend);
		MALI_DEBUG_ASSERT(0 != target_backend->size);
		return ret;
	}

	/*Cow not support resized mem */
	MALI_DEBUG_ASSERT(MALI_MEM_FLAG_CAN_RESIZE != (MALI_MEM_FLAG_CAN_RESIZE & target_backend->mali_allocation->flags));

	/* Check if the new mali address is allocated */
	mali_vma_node = mali_vma_offset_search(&session->allocation_mgr, args->vaddr, 0);

	if (unlikely(mali_vma_node)) {
		MALI_DEBUG_PRINT_ERROR(("The mali virtual address has already been used ! \n"));
		return ret;
	}

	/* create new alloction for COW*/
	mali_allocation = mali_mem_allocation_struct_create(session);
	if (mali_allocation == NULL) {
		MALI_DEBUG_PRINT(1, ("_mali_ukk_mem_cow: Failed to create allocation struct!\n"));
		return _MALI_OSK_ERR_NOMEM;
	}
	mali_allocation->psize = args->target_size;
	mali_allocation->vsize = args->target_size;
	mali_allocation->type = MALI_MEM_COW;

	/*add allocation node to RB tree for index*/
	mali_allocation->mali_vma_node.vm_node.start = args->vaddr;
	mali_allocation->mali_vma_node.vm_node.size = mali_allocation->vsize;
	mali_vma_offset_add(&session->allocation_mgr, &mali_allocation->mali_vma_node);

	/* create new backend for COW memory */
	mali_allocation->backend_handle = mali_mem_backend_struct_create(&mem_backend, mali_allocation->psize);
	if (mali_allocation->backend_handle < 0) {
		ret = _MALI_OSK_ERR_NOMEM;
		MALI_DEBUG_PRINT(1, ("mali_allocation->backend_handle < 0! \n"));
		goto failed_alloc_backend;
	}
	mem_backend->mali_allocation = mali_allocation;
	mem_backend->type = mali_allocation->type;

	if (target_backend->type == MALI_MEM_SWAP ||
	    (MALI_MEM_COW == target_backend->type && (MALI_MEM_BACKEND_FLAG_SWAP_COWED & target_backend->flags))) {
		mem_backend->flags |= MALI_MEM_BACKEND_FLAG_SWAP_COWED;
		/**
		 *     CoWed swap backends couldn't be mapped as non-linear vma, because if one
		 * vma is set with flag VM_NONLINEAR, the vma->vm_private_data will be used by kernel,
		 * while in mali driver, we use this variable to store the pointer of mali_allocation, so there
		 * is a conflict.
		 *     To resolve this problem, we have to do some fake things, we reserved about 64MB
		 * space from index 0, there isn't really page's index will be set from 0 to (64MB>>PAGE_SHIFT_NUM),
		 * and all of CoWed swap memory backends' start_idx will be assigned with 0, and these
		 * backends will be mapped as linear and will add to priority tree of global swap file, while
		 * these vmas will never be found by using normal page->index, these pages in those vma
		 * also couldn't be swapped out.
		 */
		mem_backend->start_idx = 0;
	}

	/* Add the target backend's cow count, also allocate new pages for COW backend from os mem
	*for a modified range and keep the page which not in the modified range and Add ref to it
	*/
	MALI_DEBUG_PRINT(3, ("Cow mapping: target_addr: 0x%x;  cow_addr: 0x%x,  size: %u\n", target_backend->mali_allocation->mali_vma_node.vm_node.start,
			     mali_allocation->mali_vma_node.vm_node.start, mali_allocation->mali_vma_node.vm_node.size));

	ret = mali_memory_do_cow(target_backend, args->target_offset, args->target_size, mem_backend, args->range_start, args->range_size);
	if (_MALI_OSK_ERR_OK != ret) {
		MALI_DEBUG_PRINT(1, ("_mali_ukk_mem_cow: Failed to cow!\n"));
		goto failed_do_cow;
	}

	/**
	*map to GPU side
	*/
	mali_allocation->mali_mapping.addr = args->vaddr;
	/* set gpu mmu propery */
	_mali_memory_gpu_map_property_set(&mali_allocation->mali_mapping.properties, args->flags);

	_mali_osk_mutex_wait(session->memory_lock);
	/* Map on Mali */
	ret = mali_mem_mali_map_prepare(mali_allocation);
	if (0 != ret) {
		MALI_DEBUG_PRINT(1, (" prepare map fail! \n"));
		goto failed_gpu_map;
	}

	if (!(mem_backend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED)) {
		mali_mem_cow_mali_map(mem_backend, 0, mem_backend->size);
	}

	_mali_osk_mutex_signal(session->memory_lock);

	mutex_lock(&target_backend->mutex);
	target_backend->flags |= MALI_MEM_BACKEND_FLAG_COWED;
	mutex_unlock(&target_backend->mutex);

	atomic_add(args->range_size / MALI_MMU_PAGE_SIZE, &session->mali_mem_allocated_pages);
	if (atomic_read(&session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE > session->max_mali_mem_allocated_size) {
		session->max_mali_mem_allocated_size = atomic_read(&session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE;
	}
	return _MALI_OSK_ERR_OK;

failed_gpu_map:
	_mali_osk_mutex_signal(session->memory_lock);
	mali_mem_cow_release(mem_backend, MALI_FALSE);
	mem_backend->cow_mem.count = 0;
failed_do_cow:
	mali_mem_backend_struct_destory(&mem_backend, mali_allocation->backend_handle);
failed_alloc_backend:
	mali_vma_offset_remove(&session->allocation_mgr, &mali_allocation->mali_vma_node);
	mali_mem_allocation_struct_destory(mali_allocation);

	return ret;
}

_mali_osk_errcode_t _mali_ukk_mem_cow_modify_range(_mali_uk_cow_modify_range_s *args)
{
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_FAULT;
	mali_mem_backend *mem_backend = NULL;
	struct  mali_session_data *session = (struct mali_session_data *)(uintptr_t)args->ctx;

	MALI_DEBUG_PRINT(4, (" _mali_ukk_mem_cow_modify_range called! \n"));
	/* Get the backend that need to be modified. */
	mem_backend = mali_mem_backend_struct_search(session, args->vaddr);

	if (NULL == mem_backend || 0 == mem_backend->size) {
		MALI_DEBUG_ASSERT_POINTER(mem_backend);
		MALI_DEBUG_ASSERT(0 != mem_backend->size);
		return ret;
	}

	MALI_DEBUG_ASSERT(MALI_MEM_COW  == mem_backend->type);

	ret =  mali_memory_cow_modify_range(mem_backend, args->range_start, args->size);
	args->change_pages_nr = mem_backend->cow_mem.change_pages_nr;
	if (_MALI_OSK_ERR_OK != ret)
		return  ret;
	_mali_osk_mutex_wait(session->memory_lock);
	if (!(mem_backend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED)) {
		mali_mem_cow_mali_map(mem_backend, args->range_start, args->size);
	}
	_mali_osk_mutex_signal(session->memory_lock);

	atomic_add(args->change_pages_nr, &session->mali_mem_allocated_pages);
	if (atomic_read(&session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE > session->max_mali_mem_allocated_size) {
		session->max_mali_mem_allocated_size = atomic_read(&session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE;
	}

	return _MALI_OSK_ERR_OK;
}


_mali_osk_errcode_t _mali_ukk_mem_resize(_mali_uk_mem_resize_s *args)
{
	mali_mem_backend *mem_backend = NULL;
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_FAULT;

	struct  mali_session_data *session = (struct mali_session_data *)(uintptr_t)args->ctx;

	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_PRINT(4, (" mali_mem_resize_memory called! \n"));
	MALI_DEBUG_ASSERT(0 == args->psize %  MALI_MMU_PAGE_SIZE);

	/* Get the memory backend that need to be resize. */
	mem_backend = mali_mem_backend_struct_search(session, args->vaddr);

	if (NULL == mem_backend)  {
		MALI_DEBUG_PRINT(2, ("_mali_ukk_mem_resize: memory backend = NULL!\n"));
		return ret;
	}

	MALI_DEBUG_ASSERT(args->psize != mem_backend->size);

	ret = mali_mem_resize(session, mem_backend, args->psize);

	return ret;
}

_mali_osk_errcode_t _mali_ukk_mem_usage_get(_mali_uk_profiling_memory_usage_get_s *args)
{
	args->memory_usage = _mali_ukk_report_memory_usage();
	if (0 != args->vaddr) {
		mali_mem_backend *mem_backend = NULL;
		struct  mali_session_data *session = (struct mali_session_data *)(uintptr_t)args->ctx;
		/* Get the backend that need to be modified. */
		mem_backend = mali_mem_backend_struct_search(session, args->vaddr);
		if (NULL == mem_backend) {
			MALI_DEBUG_ASSERT_POINTER(mem_backend);
			return _MALI_OSK_ERR_FAULT;
		}

		if (MALI_MEM_COW == mem_backend->type)
			args->change_pages_nr = mem_backend->cow_mem.change_pages_nr;
	}
	return _MALI_OSK_ERR_OK;
}
