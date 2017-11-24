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

#include <linux/types.h>

struct etnaviv_gpu;
struct etnaviv_cmdbuf_suballoc;
struct etnaviv_perfmon_request;

struct etnaviv_cmdbuf {
	/* suballocator this cmdbuf is allocated from */
	struct etnaviv_cmdbuf_suballoc *suballoc;
	/* user context key, must be unique between all active users */
	struct etnaviv_file_private *ctx;
	/* cmdbuf properties */
	int suballoc_offset;
	void *vaddr;
	u32 size;
	u32 user_size;
	/* fence after which this buffer is to be disposed */
	struct dma_fence *fence;
	/* per GPU in-flight list */
	struct list_head node;
	/* BOs attached to this command buffer */
	unsigned int nr_bos;
	struct etnaviv_vram_mapping *bo_map[0];
};

struct etnaviv_cmdbuf_suballoc *
etnaviv_cmdbuf_suballoc_new(struct etnaviv_gpu * gpu);
void etnaviv_cmdbuf_suballoc_destroy(struct etnaviv_cmdbuf_suballoc *suballoc);

struct etnaviv_cmdbuf *
etnaviv_cmdbuf_new(struct etnaviv_cmdbuf_suballoc *suballoc, u32 size,
		   size_t nr_bos);
void etnaviv_cmdbuf_free(struct etnaviv_cmdbuf *cmdbuf);

u32 etnaviv_cmdbuf_get_va(struct etnaviv_cmdbuf *buf);
dma_addr_t etnaviv_cmdbuf_get_pa(struct etnaviv_cmdbuf *buf);

#endif /* __ETNAVIV_CMDBUF_H__ */
