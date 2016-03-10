/*
 * Copyright (C) 2012-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/fs.h>      /* file system operations */
#include <asm/uaccess.h>        /* user space access */
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/rbtree.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/mutex.h>

#include "mali_ukk.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_session.h"
#include "mali_kernel_linux.h"

#include "mali_memory.h"
#include "mali_memory_dma_buf.h"
#include "mali_memory_virtual.h"
#include "mali_pp_job.h"

/*
 * Map DMA buf attachment \a mem into \a session at virtual address \a virt.
 */
static int mali_dma_buf_map(mali_mem_backend *mem_backend)
{
	mali_mem_allocation *alloc;
	struct mali_dma_buf_attachment *mem;
	struct  mali_session_data *session;
	struct mali_page_directory *pagedir;
	_mali_osk_errcode_t err;
	struct scatterlist *sg;
	u32 virt, flags;
	int i;

	MALI_DEBUG_ASSERT_POINTER(mem_backend);

	alloc = mem_backend->mali_allocation;
	MALI_DEBUG_ASSERT_POINTER(alloc);

	mem = mem_backend->dma_buf.attachment;
	MALI_DEBUG_ASSERT_POINTER(mem);

	session = alloc->session;
	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT(mem->session == session);

	virt = alloc->mali_vma_node.vm_node.start;
	flags = alloc->flags;

	mali_session_memory_lock(session);
	mem->map_ref++;

	MALI_DEBUG_PRINT(5, ("Mali DMA-buf: map attachment %p, new map_ref = %d\n", mem, mem->map_ref));

	if (1 == mem->map_ref) {

		/* First reference taken, so we need to map the dma buf */
		MALI_DEBUG_ASSERT(!mem->is_mapped);

		mem->sgt = dma_buf_map_attachment(mem->attachment, DMA_BIDIRECTIONAL);
		if (IS_ERR_OR_NULL(mem->sgt)) {
			MALI_DEBUG_PRINT_ERROR(("Failed to map dma-buf attachment\n"));
			mem->map_ref--;
			mali_session_memory_unlock(session);
			return -EFAULT;
		}

		err = mali_mem_mali_map_prepare(alloc);
		if (_MALI_OSK_ERR_OK != err) {
			MALI_DEBUG_PRINT(1, ("Mapping of DMA memory failed\n"));
			mem->map_ref--;
			mali_session_memory_unlock(session);
			return -ENOMEM;
		}

		pagedir = mali_session_get_page_directory(session);
		MALI_DEBUG_ASSERT_POINTER(pagedir);

		for_each_sg(mem->sgt->sgl, sg, mem->sgt->nents, i) {
			u32 size = sg_dma_len(sg);
			dma_addr_t phys = sg_dma_address(sg);

			/* sg must be page aligned. */
			MALI_DEBUG_ASSERT(0 == size % MALI_MMU_PAGE_SIZE);
			MALI_DEBUG_ASSERT(0 == (phys & ~(uintptr_t)0xFFFFFFFF));

			mali_mmu_pagedir_update(pagedir, virt, phys, size, MALI_MMU_FLAGS_DEFAULT);

			virt += size;
		}

		if (flags & MALI_MEM_FLAG_MALI_GUARD_PAGE) {
			u32 guard_phys;
			MALI_DEBUG_PRINT(7, ("Mapping in extra guard page\n"));

			guard_phys = sg_dma_address(mem->sgt->sgl);
			mali_mmu_pagedir_update(pagedir, virt, guard_phys, MALI_MMU_PAGE_SIZE, MALI_MMU_FLAGS_DEFAULT);
		}

		mem->is_mapped = MALI_TRUE;
		mali_session_memory_unlock(session);
		/* Wake up any thread waiting for buffer to become mapped */
		wake_up_all(&mem->wait_queue);
	} else {
		MALI_DEBUG_ASSERT(mem->is_mapped);
		mali_session_memory_unlock(session);
	}

	return 0;
}

static void mali_dma_buf_unmap(mali_mem_allocation *alloc, struct mali_dma_buf_attachment *mem)
{
	MALI_DEBUG_ASSERT_POINTER(alloc);
	MALI_DEBUG_ASSERT_POINTER(mem);
	MALI_DEBUG_ASSERT_POINTER(mem->attachment);
	MALI_DEBUG_ASSERT_POINTER(mem->buf);
	MALI_DEBUG_ASSERT_POINTER(alloc->session);

	mali_session_memory_lock(alloc->session);
	mem->map_ref--;

	MALI_DEBUG_PRINT(5, ("Mali DMA-buf: unmap attachment %p, new map_ref = %d\n", mem, mem->map_ref));

	if (0 == mem->map_ref) {
		dma_buf_unmap_attachment(mem->attachment, mem->sgt, DMA_BIDIRECTIONAL);
		if (MALI_TRUE == mem->is_mapped) {
			mali_mem_mali_map_free(alloc->session, alloc->psize, alloc->mali_vma_node.vm_node.start,
					       alloc->flags);
		}
		mem->is_mapped = MALI_FALSE;
	}
	mali_session_memory_unlock(alloc->session);
	/* Wake up any thread waiting for buffer to become unmapped */
	wake_up_all(&mem->wait_queue);
}

#if !defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
int mali_dma_buf_map_job(struct mali_pp_job *job)
{
	struct mali_dma_buf_attachment *mem;
	_mali_osk_errcode_t err;
	int i;
	int ret = 0;
	u32 num_memory_cookies;
	struct mali_session_data *session;
	struct mali_vma_node *mali_vma_node = NULL;
	mali_mem_allocation *mali_alloc = NULL;
	mali_mem_backend *mem_bkend = NULL;

	MALI_DEBUG_ASSERT_POINTER(job);

	num_memory_cookies = mali_pp_job_num_memory_cookies(job);

	session = mali_pp_job_get_session(job);

	MALI_DEBUG_ASSERT_POINTER(session);

	for (i = 0; i < num_memory_cookies; i++) {
		u32 mali_addr  = mali_pp_job_get_memory_cookie(job, i);
		mali_vma_node = mali_vma_offset_search(&session->allocation_mgr, mali_addr, 0);
		MALI_DEBUG_ASSERT(NULL != mali_vma_node);
		mali_alloc = container_of(mali_vma_node, struct mali_mem_allocation, mali_vma_node);
		MALI_DEBUG_ASSERT(NULL != mali_alloc);
		if (MALI_MEM_DMA_BUF != mali_alloc->type) {
			continue;
		}

		/* Get backend memory & Map on CPU */
		mutex_lock(&mali_idr_mutex);
		mem_bkend = idr_find(&mali_backend_idr, mali_alloc->backend_handle);
		mutex_unlock(&mali_idr_mutex);
		MALI_DEBUG_ASSERT(NULL != mem_bkend);

		mem = mem_bkend->dma_buf.attachment;

		MALI_DEBUG_ASSERT_POINTER(mem);
		MALI_DEBUG_ASSERT(mem->session == mali_pp_job_get_session(job));

		err = mali_dma_buf_map(mem_bkend);
		if (0 != err) {
			MALI_DEBUG_PRINT_ERROR(("Mali DMA-buf: Failed to map dma-buf for mali address %x\n", mali_addr));
			ret = -EFAULT;
			continue;
		}
	}
	return ret;
}

void mali_dma_buf_unmap_job(struct mali_pp_job *job)
{
	struct mali_dma_buf_attachment *mem;
	int i;
	u32 num_memory_cookies;
	struct mali_session_data *session;
	struct mali_vma_node *mali_vma_node = NULL;
	mali_mem_allocation *mali_alloc = NULL;
	mali_mem_backend *mem_bkend = NULL;

	MALI_DEBUG_ASSERT_POINTER(job);

	num_memory_cookies = mali_pp_job_num_memory_cookies(job);

	session = mali_pp_job_get_session(job);

	MALI_DEBUG_ASSERT_POINTER(session);

	for (i = 0; i < num_memory_cookies; i++) {
		u32 mali_addr  = mali_pp_job_get_memory_cookie(job, i);
		mali_vma_node = mali_vma_offset_search(&session->allocation_mgr, mali_addr, 0);
		MALI_DEBUG_ASSERT(NULL != mali_vma_node);
		mali_alloc = container_of(mali_vma_node, struct mali_mem_allocation, mali_vma_node);
		MALI_DEBUG_ASSERT(NULL != mali_alloc);
		if (MALI_MEM_DMA_BUF != mali_alloc->type) {
			continue;
		}

		/* Get backend memory & Map on CPU */
		mutex_lock(&mali_idr_mutex);
		mem_bkend = idr_find(&mali_backend_idr, mali_alloc->backend_handle);
		mutex_unlock(&mali_idr_mutex);
		MALI_DEBUG_ASSERT(NULL != mem_bkend);

		mem = mem_bkend->dma_buf.attachment;

		MALI_DEBUG_ASSERT_POINTER(mem);
		MALI_DEBUG_ASSERT(mem->session == mali_pp_job_get_session(job));
		mali_dma_buf_unmap(mem_bkend->mali_allocation, mem);
	}
}
#endif /* !CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH */

int mali_dma_buf_get_size(struct mali_session_data *session, _mali_uk_dma_buf_get_size_s __user *user_arg)
{
	_mali_uk_dma_buf_get_size_s args;
	int fd;
	struct dma_buf *buf;

	/* get call arguments from user space. copy_from_user returns how many bytes which where NOT copied */
	if (0 != copy_from_user(&args, (void __user *)user_arg, sizeof(_mali_uk_dma_buf_get_size_s))) {
		return -EFAULT;
	}

	/* Do DMA-BUF stuff */
	fd = args.mem_fd;

	buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(buf)) {
		MALI_DEBUG_PRINT_ERROR(("Failed to get dma-buf from fd: %d\n", fd));
		return PTR_RET(buf);
	}

	if (0 != put_user(buf->size, &user_arg->size)) {
		dma_buf_put(buf);
		return -EFAULT;
	}

	dma_buf_put(buf);

	return 0;
}

_mali_osk_errcode_t mali_mem_bind_dma_buf(mali_mem_allocation *alloc,
		mali_mem_backend *mem_backend,
		int fd, u32 flags)
{
	struct dma_buf *buf;
	struct mali_dma_buf_attachment *dma_mem;
	struct  mali_session_data *session = alloc->session;

	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT_POINTER(mem_backend);
	MALI_DEBUG_ASSERT_POINTER(alloc);

	/* get dma buffer */
	buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(buf)) {
		return _MALI_OSK_ERR_FAULT;
	}

	/* Currently, mapping of the full buffer are supported. */
	if (alloc->psize != buf->size) {
		goto failed_alloc_mem;
	}

	dma_mem = _mali_osk_calloc(1, sizeof(struct mali_dma_buf_attachment));
	if (NULL == dma_mem) {
		goto failed_alloc_mem;
	}

	dma_mem->buf = buf;
	dma_mem->session = session;
	dma_mem->map_ref = 0;
	init_waitqueue_head(&dma_mem->wait_queue);

	dma_mem->attachment = dma_buf_attach(dma_mem->buf, &mali_platform_device->dev);
	if (NULL == dma_mem->attachment) {
		goto failed_dma_attach;
	}

	mem_backend->dma_buf.attachment = dma_mem;

	alloc->flags |= MALI_MEM_FLAG_DONT_CPU_MAP;
	if (flags & _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE) {
		alloc->flags |= MALI_MEM_FLAG_MALI_GUARD_PAGE;
	}


#if defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
	/* Map memory into session's Mali virtual address space. */
	if (0 != mali_dma_buf_map(mem_backend)) {
		goto Failed_dma_map;
	}
#endif

	return _MALI_OSK_ERR_OK;

#if defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
Failed_dma_map:
	mali_dma_buf_unmap(alloc, dma_mem);
#endif
	/* Wait for buffer to become unmapped */
	wait_event(dma_mem->wait_queue, !dma_mem->is_mapped);
	MALI_DEBUG_ASSERT(!dma_mem->is_mapped);
	dma_buf_detach(dma_mem->buf, dma_mem->attachment);
failed_dma_attach:
	_mali_osk_free(dma_mem);
failed_alloc_mem:
	dma_buf_put(buf);
	return _MALI_OSK_ERR_FAULT;
}

void mali_mem_unbind_dma_buf(mali_mem_backend *mem_backend)
{
	struct mali_dma_buf_attachment *mem;
	MALI_DEBUG_ASSERT_POINTER(mem_backend);
	MALI_DEBUG_ASSERT(MALI_MEM_DMA_BUF == mem_backend->type);

	mem = mem_backend->dma_buf.attachment;
	MALI_DEBUG_ASSERT_POINTER(mem);
	MALI_DEBUG_ASSERT_POINTER(mem->attachment);
	MALI_DEBUG_ASSERT_POINTER(mem->buf);
	MALI_DEBUG_PRINT(3, ("Mali DMA-buf: release attachment %p\n", mem));

#if defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
	MALI_DEBUG_ASSERT_POINTER(mem_backend->mali_allocation);
	/* We mapped implicitly on attach, so we need to unmap on release */
	mali_dma_buf_unmap(mem_backend->mali_allocation, mem);
#endif
	/* Wait for buffer to become unmapped */
	wait_event(mem->wait_queue, !mem->is_mapped);
	MALI_DEBUG_ASSERT(!mem->is_mapped);

	dma_buf_detach(mem->buf, mem->attachment);
	dma_buf_put(mem->buf);

	_mali_osk_free(mem);
}
