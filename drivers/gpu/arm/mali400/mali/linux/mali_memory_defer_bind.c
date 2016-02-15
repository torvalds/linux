/*
 * Copyright (C) 2013-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/mm_types.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <asm/cacheflush.h>
#include <linux/sched.h>
#ifdef CONFIG_ARM
#include <asm/outercache.h>
#endif
#include <asm/dma-mapping.h>

#include "mali_memory.h"
#include "mali_kernel_common.h"
#include "mali_uk_types.h"
#include "mali_osk.h"
#include "mali_kernel_linux.h"
#include "mali_memory_defer_bind.h"
#include "mali_executor.h"
#include "mali_osk.h"
#include "mali_scheduler.h"
#include "mali_gp_job.h"

mali_defer_bind_manager *mali_dmem_man = NULL;

static u32 mali_dmem_get_gp_varying_size(struct mali_gp_job *gp_job)
{
	return gp_job->required_varying_memsize / _MALI_OSK_MALI_PAGE_SIZE;
}

_mali_osk_errcode_t mali_mem_defer_bind_manager_init(void)
{
	mali_dmem_man = _mali_osk_calloc(1, sizeof(struct mali_defer_bind_manager));
	if (!mali_dmem_man)
		return _MALI_OSK_ERR_NOMEM;

	atomic_set(&mali_dmem_man->num_used_pages, 0);
	atomic_set(&mali_dmem_man->num_dmem, 0);

	return _MALI_OSK_ERR_OK;
}


void mali_mem_defer_bind_manager_destory(void)
{
	if (mali_dmem_man) {
		MALI_DEBUG_ASSERT(0 == atomic_read(&mali_dmem_man->num_dmem));
		kfree(mali_dmem_man);
	}
	mali_dmem_man = NULL;
}


/*allocate pages from OS memory*/
_mali_osk_errcode_t mali_mem_defer_alloc_mem(u32 require, struct mali_session_data *session, mali_defer_mem_block *dblock)
{
	int retval = 0;
	u32 num_pages = require;
	mali_mem_os_mem os_mem;

	retval = mali_mem_os_alloc_pages(&os_mem, num_pages * _MALI_OSK_MALI_PAGE_SIZE);

	/* add to free pages list */
	if (0 == retval) {
		MALI_DEBUG_PRINT(4, ("mali_mem_defer_alloc_mem ,,*** pages allocate = 0x%x \n", num_pages));
		list_splice(&os_mem.pages, &dblock->free_pages);
		atomic_add(os_mem.count, &dblock->num_free_pages);
		atomic_add(os_mem.count, &session->mali_mem_allocated_pages);
		if (atomic_read(&session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE > session->max_mali_mem_allocated_size) {
			session->max_mali_mem_allocated_size = atomic_read(&session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE;
		}
		return _MALI_OSK_ERR_OK;
	} else
		return _MALI_OSK_ERR_FAULT;
}

_mali_osk_errcode_t mali_mem_prepare_mem_for_job(struct mali_gp_job *next_gp_job, mali_defer_mem_block *dblock)
{
	u32 require_page;

	if (!next_gp_job)
		return _MALI_OSK_ERR_FAULT;

	require_page = mali_dmem_get_gp_varying_size(next_gp_job);

	MALI_DEBUG_PRINT(4, ("mali_mem_defer_prepare_mem_work, require alloc page 0x%x\n",
			     require_page));
	/* allocate more pages from OS */
	if (_MALI_OSK_ERR_OK != mali_mem_defer_alloc_mem(require_page, next_gp_job->session, dblock)) {
		MALI_DEBUG_PRINT(1, ("ERROR##mali_mem_defer_prepare_mem_work, allocate page failed!!"));
		return _MALI_OSK_ERR_NOMEM;
	}

	next_gp_job->bind_flag = MALI_DEFER_BIND_MEMORY_PREPARED;

	return _MALI_OSK_ERR_OK;
}


/* do preparetion for allocation before defer bind */
_mali_osk_errcode_t mali_mem_defer_bind_allocation_prepare(mali_mem_allocation *alloc, struct list_head *list, u32 *required_varying_memsize)
{
	mali_mem_backend *mem_bkend = NULL;
	struct mali_backend_bind_list *bk_list = _mali_osk_calloc(1, sizeof(struct mali_backend_bind_list));
	if (NULL == bk_list)
		return _MALI_OSK_ERR_FAULT;

	INIT_LIST_HEAD(&bk_list->node);
	/* Get backend memory */
	mutex_lock(&mali_idr_mutex);
	if (!(mem_bkend = idr_find(&mali_backend_idr, alloc->backend_handle))) {
		MALI_DEBUG_PRINT(1, ("Can't find memory backend in defer bind!\n"));
		mutex_unlock(&mali_idr_mutex);
		_mali_osk_free(bk_list);
		return _MALI_OSK_ERR_FAULT;
	}
	mutex_unlock(&mali_idr_mutex);

	/* If the mem backend has already been bound, no need to bind again.*/
	if (mem_bkend->os_mem.count > 0) {
		_mali_osk_free(bk_list);
		return _MALI_OSK_ERR_OK;
	}

	MALI_DEBUG_PRINT(4, ("bind_allocation_prepare:: allocation =%x vaddr=0x%x!\n", alloc, alloc->mali_vma_node.vm_node.start));

	INIT_LIST_HEAD(&mem_bkend->os_mem.pages);

	bk_list->bkend = mem_bkend;
	bk_list->vaddr = alloc->mali_vma_node.vm_node.start;
	bk_list->session = alloc->session;
	bk_list->page_num = mem_bkend->size / _MALI_OSK_MALI_PAGE_SIZE;
	*required_varying_memsize +=  mem_bkend->size;
	MALI_DEBUG_ASSERT(mem_bkend->type == MALI_MEM_OS);

	/* add to job to do list */
	list_add(&bk_list->node, list);

	return _MALI_OSK_ERR_OK;
}



/* bind phyiscal memory to allocation
This function will be called in IRQ handler*/
static _mali_osk_errcode_t mali_mem_defer_bind_allocation(struct mali_backend_bind_list *bk_node,
		struct list_head *pages)
{
	struct mali_session_data *session = bk_node->session;
	mali_mem_backend *mem_bkend = bk_node->bkend;
	MALI_DEBUG_PRINT(4, ("mali_mem_defer_bind_allocation, bind bkend = %x page num=0x%x vaddr=%x session=%x\n", mem_bkend, bk_node->page_num, bk_node->vaddr, session));

	MALI_DEBUG_ASSERT(mem_bkend->type == MALI_MEM_OS);
	list_splice(pages, &mem_bkend->os_mem.pages);
	mem_bkend->os_mem.count = bk_node->page_num;

	if (mem_bkend->type == MALI_MEM_OS) {
		mali_mem_os_mali_map(&mem_bkend->os_mem, session, bk_node->vaddr, 0,
				     mem_bkend->os_mem.count, MALI_MMU_FLAGS_DEFAULT);
	}
	smp_wmb();
	bk_node->flag = MALI_DEFER_BIND_MEMORY_BINDED;
	mem_bkend->flags &= ~MALI_MEM_BACKEND_FLAG_NOT_BINDED;
	mem_bkend->flags |= MALI_MEM_BACKEND_FLAG_BINDED;
	return _MALI_OSK_ERR_OK;
}


static struct list_head *mali_mem_defer_get_free_page_list(u32 count, struct list_head *pages, mali_defer_mem_block *dblock)
{
	int i = 0;
	struct mali_page_node *m_page, *m_tmp;

	if (atomic_read(&dblock->num_free_pages) < count) {
		return NULL;
	} else {
		list_for_each_entry_safe(m_page, m_tmp, &dblock->free_pages, list) {
			if (i < count) {
				list_move_tail(&m_page->list, pages);
			} else {
				break;
			}
			i++;
		}
		MALI_DEBUG_ASSERT(i == count);
		atomic_sub(count, &dblock->num_free_pages);
		return pages;
	}
}


/* called in job start IOCTL to bind physical memory for each allocations
@ bk_list backend list to do defer bind
@ pages page list to do this bind
@ count number of pages
*/
_mali_osk_errcode_t mali_mem_defer_bind(struct mali_gp_job *gp,
					struct mali_defer_mem_block *dmem_block)
{
	struct mali_defer_mem *dmem = NULL;
	struct mali_backend_bind_list *bkn, *bkn_tmp;
	LIST_HEAD(pages);

	if (gp->required_varying_memsize != (atomic_read(&dmem_block->num_free_pages) * _MALI_OSK_MALI_PAGE_SIZE)) {
		MALI_DEBUG_PRINT_ERROR(("#BIND:  The memsize of varying buffer not match to the pagesize of the dmem_block!!## \n"));
		return _MALI_OSK_ERR_FAULT;
	}

	MALI_DEBUG_PRINT(4, ("#BIND: GP job=%x## \n", gp));
	dmem = (mali_defer_mem *)_mali_osk_calloc(1, sizeof(struct mali_defer_mem));
	if (dmem) {
		INIT_LIST_HEAD(&dmem->node);
		gp->dmem = dmem;
	} else {
		return _MALI_OSK_ERR_NOMEM;
	}

	atomic_add(1, &mali_dmem_man->num_dmem);
	/* for each bk_list backend, do bind */
	list_for_each_entry_safe(bkn, bkn_tmp , &gp->vary_todo, node) {
		INIT_LIST_HEAD(&pages);
		if (likely(mali_mem_defer_get_free_page_list(bkn->page_num, &pages, dmem_block))) {
			list_del(&bkn->node);
			mali_mem_defer_bind_allocation(bkn, &pages);
			_mali_osk_free(bkn);
		} else {
			/* not enough memory will not happen */
			MALI_DEBUG_PRINT_ERROR(("#BIND: NOT enough memory when binded !!## \n"));
			_mali_osk_free(gp->dmem);
			return _MALI_OSK_ERR_NOMEM;
		}
	}

	if (!list_empty(&gp->vary_todo)) {
		MALI_DEBUG_PRINT_ERROR(("#BIND:  The deferbind backend list isn't empty !!## \n"));
		_mali_osk_free(gp->dmem);
		return _MALI_OSK_ERR_FAULT;
	}

	dmem->flag = MALI_DEFER_BIND_MEMORY_BINDED;

	return _MALI_OSK_ERR_OK;
}

void mali_mem_defer_dmem_free(struct mali_gp_job *gp)
{
	if (gp->dmem) {
		atomic_dec(&mali_dmem_man->num_dmem);
		_mali_osk_free(gp->dmem);
	}
}

