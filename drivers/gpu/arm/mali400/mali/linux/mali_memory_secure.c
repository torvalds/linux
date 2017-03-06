/*
 * Copyright (C) 2010-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_memory.h"
#include "mali_memory_secure.h"
#include "mali_osk.h"
#include <linux/mutex.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>

_mali_osk_errcode_t mali_mem_secure_attach_dma_buf(mali_mem_secure *secure_mem, u32 size, int mem_fd)
{
	struct dma_buf *buf;
	MALI_DEBUG_ASSERT_POINTER(secure_mem);

	/* get dma buffer */
	buf = dma_buf_get(mem_fd);
	if (IS_ERR_OR_NULL(buf)) {
		MALI_DEBUG_PRINT_ERROR(("Failed to get dma buf!\n"));
		return _MALI_OSK_ERR_FAULT;
	}

	if (size != buf->size) {
		MALI_DEBUG_PRINT_ERROR(("The secure mem size not match to the dma buf size!\n"));
		goto failed_alloc_mem;
	}

	secure_mem->buf =  buf;
	secure_mem->attachment = dma_buf_attach(secure_mem->buf, &mali_platform_device->dev);
	if (NULL == secure_mem->attachment) {
		MALI_DEBUG_PRINT_ERROR(("Failed to get dma buf attachment!\n"));
		goto failed_dma_attach;
	}

	secure_mem->sgt = dma_buf_map_attachment(secure_mem->attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(secure_mem->sgt)) {
		MALI_DEBUG_PRINT_ERROR(("Failed to map dma buf attachment\n"));
		goto  failed_dma_map;
	}

	secure_mem->count = size / MALI_MMU_PAGE_SIZE;

	return _MALI_OSK_ERR_OK;

failed_dma_map:
	dma_buf_detach(secure_mem->buf, secure_mem->attachment);
failed_dma_attach:
failed_alloc_mem:
	dma_buf_put(buf);
	return _MALI_OSK_ERR_FAULT;
}

_mali_osk_errcode_t mali_mem_secure_mali_map(mali_mem_secure *secure_mem, struct mali_session_data *session, u32 vaddr, u32 props)
{
	struct mali_page_directory *pagedir;
	struct scatterlist *sg;
	u32 virt = vaddr;
	u32 prop = props;
	int i;

	MALI_DEBUG_ASSERT_POINTER(secure_mem);
	MALI_DEBUG_ASSERT_POINTER(secure_mem->sgt);
	MALI_DEBUG_ASSERT_POINTER(session);

	pagedir = session->page_directory;

	for_each_sg(secure_mem->sgt->sgl, sg, secure_mem->sgt->nents, i) {
		u32 size = sg_dma_len(sg);
		dma_addr_t phys = sg_dma_address(sg);

		/* sg must be page aligned. */
		MALI_DEBUG_ASSERT(0 == size % MALI_MMU_PAGE_SIZE);
		MALI_DEBUG_ASSERT(0 == (phys & ~(uintptr_t)0xFFFFFFFF));

		mali_mmu_pagedir_update(pagedir, virt, phys, size, prop);

		MALI_DEBUG_PRINT(3, ("The secure mem physical address: 0x%x gpu virtual address: 0x%x! \n", phys, virt));
		virt += size;
	}

	return _MALI_OSK_ERR_OK;
}

void mali_mem_secure_mali_unmap(mali_mem_allocation *alloc)
{
	struct mali_session_data *session;
	MALI_DEBUG_ASSERT_POINTER(alloc);
	session = alloc->session;
	MALI_DEBUG_ASSERT_POINTER(session);

	mali_session_memory_lock(session);
	mali_mem_mali_map_free(session, alloc->psize, alloc->mali_vma_node.vm_node.start,
			       alloc->flags);
	mali_session_memory_unlock(session);
}


int mali_mem_secure_cpu_map(mali_mem_backend *mem_bkend, struct vm_area_struct *vma)
{

	int ret = 0;
	struct scatterlist *sg;
	mali_mem_secure *secure_mem = &mem_bkend->secure_mem;
	unsigned long addr = vma->vm_start;
	int i;

	MALI_DEBUG_ASSERT(mem_bkend->type == MALI_MEM_SECURE);

	for_each_sg(secure_mem->sgt->sgl, sg, secure_mem->sgt->nents, i) {
		phys_addr_t phys;
		dma_addr_t dev_addr;
		u32 size, j;
		dev_addr = sg_dma_address(sg);
#if defined(CONFIG_ARM64) ||LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
		phys =  dma_to_phys(&mali_platform_device->dev, dev_addr);
#else
		phys = page_to_phys(pfn_to_page(dma_to_pfn(&mali_platform_device->dev, dev_addr)));
#endif
		size = sg_dma_len(sg);
		MALI_DEBUG_ASSERT(0 == size % _MALI_OSK_MALI_PAGE_SIZE);

		for (j = 0; j < size / _MALI_OSK_MALI_PAGE_SIZE; j++) {
			ret = vm_insert_pfn(vma, addr, PFN_DOWN(phys));

			if (unlikely(0 != ret)) {
				return -EFAULT;
			}
			addr += _MALI_OSK_MALI_PAGE_SIZE;
			phys += _MALI_OSK_MALI_PAGE_SIZE;

			MALI_DEBUG_PRINT(3, ("The secure mem physical address: 0x%x , cpu virtual address: 0x%x! \n", phys, addr));
		}
	}
	return ret;
}

u32 mali_mem_secure_release(mali_mem_backend *mem_bkend)
{
	struct mali_mem_secure *mem;
	mali_mem_allocation *alloc = mem_bkend->mali_allocation;
	u32 free_pages_nr = 0;
	MALI_DEBUG_ASSERT(mem_bkend->type == MALI_MEM_SECURE);

	mem = &mem_bkend->secure_mem;
	MALI_DEBUG_ASSERT_POINTER(mem->attachment);
	MALI_DEBUG_ASSERT_POINTER(mem->buf);
	MALI_DEBUG_ASSERT_POINTER(mem->sgt);
	/* Unmap the memory from the mali virtual address space. */
	mali_mem_secure_mali_unmap(alloc);
	mutex_lock(&mem_bkend->mutex);
	dma_buf_unmap_attachment(mem->attachment, mem->sgt, DMA_BIDIRECTIONAL);
	dma_buf_detach(mem->buf, mem->attachment);
	dma_buf_put(mem->buf);
	mutex_unlock(&mem_bkend->mutex);

	free_pages_nr = mem->count;

	return free_pages_nr;
}


