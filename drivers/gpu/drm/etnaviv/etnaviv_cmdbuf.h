/*
 * Copyright (C) 2017 Etnaviv Project
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ETNAVIV_CMDBUF_H__
#define __ETNAVIV_CMDBUF_H__

#include <drm/drm_mm.h>
#include <linux/types.h>

struct etnaviv_cmdbuf {
	/* device this cmdbuf is allocated for */
	struct etnaviv_gpu *gpu;
	/* user context key, must be unique between all active users */
	struct etnaviv_file_private *ctx;
	/* cmdbuf properties */
	void *vaddr;
	dma_addr_t paddr;
	u32 size;
	u32 user_size;
	/* vram node used if the cmdbuf is mapped through the MMUv2 */
	struct drm_mm_node vram_node;
	/* fence after which this buffer is to be disposed */
	struct dma_fence *fence;
	/* target exec state */
	u32 exec_state;
	/* per GPU in-flight list */
	struct list_head node;
	/* BOs attached to this command buffer */
	unsigned int nr_bos;
	struct etnaviv_vram_mapping *bo_map[0];
};

u32 etnaviv_cmdbuf_get_va(struct etnaviv_cmdbuf *buf);

#endif /* __ETNAVIV_CMDBUF_H__ */
