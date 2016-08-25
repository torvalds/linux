/*
 * Copyright (C) 2011-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_MEMORY_DMA_BUF_H__
#define __MALI_MEMORY_DMA_BUF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "mali_uk_types.h"
#include "mali_osk.h"
#include "mali_memory.h"

struct mali_pp_job;

struct mali_dma_buf_attachment;
struct mali_dma_buf_attachment {
	struct dma_buf *buf;
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;
	struct mali_session_data *session;
	int map_ref;
	struct mutex map_lock;
	mali_bool is_mapped;
	wait_queue_head_t wait_queue;
};

int mali_dma_buf_get_size(struct mali_session_data *session, _mali_uk_dma_buf_get_size_s __user *arg);

void mali_mem_unbind_dma_buf(mali_mem_backend *mem_backend);

_mali_osk_errcode_t mali_mem_bind_dma_buf(mali_mem_allocation *alloc,
		mali_mem_backend *mem_backend,
		int fd, u32 flags);

#if !defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
int mali_dma_buf_map_job(struct mali_pp_job *job);
void mali_dma_buf_unmap_job(struct mali_pp_job *job);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __MALI_MEMORY_DMA_BUF_H__ */
