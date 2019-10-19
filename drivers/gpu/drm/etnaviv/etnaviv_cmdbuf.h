/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017 Etnaviv Project
 */

#ifndef __ETNAVIV_CMDBUF_H__
#define __ETNAVIV_CMDBUF_H__

#include <linux/types.h>

struct device;
struct etnaviv_iommu_context;
struct etnaviv_vram_mapping;
struct etnaviv_cmdbuf_suballoc;
struct etnaviv_perfmon_request;

struct etnaviv_cmdbuf {
	/* suballocator this cmdbuf is allocated from */
	struct etnaviv_cmdbuf_suballoc *suballoc;
	/* cmdbuf properties */
	int suballoc_offset;
	void *vaddr;
	u32 size;
	u32 user_size;
};

struct etnaviv_cmdbuf_suballoc *
etnaviv_cmdbuf_suballoc_new(struct device *dev);
void etnaviv_cmdbuf_suballoc_destroy(struct etnaviv_cmdbuf_suballoc *suballoc);
int etnaviv_cmdbuf_suballoc_map(struct etnaviv_cmdbuf_suballoc *suballoc,
				struct etnaviv_iommu_context *context,
				struct etnaviv_vram_mapping *mapping,
				u32 memory_base);
void etnaviv_cmdbuf_suballoc_unmap(struct etnaviv_iommu_context *context,
				   struct etnaviv_vram_mapping *mapping);


int etnaviv_cmdbuf_init(struct etnaviv_cmdbuf_suballoc *suballoc,
		struct etnaviv_cmdbuf *cmdbuf, u32 size);
void etnaviv_cmdbuf_free(struct etnaviv_cmdbuf *cmdbuf);

u32 etnaviv_cmdbuf_get_va(struct etnaviv_cmdbuf *buf,
			  struct etnaviv_vram_mapping *mapping);
dma_addr_t etnaviv_cmdbuf_get_pa(struct etnaviv_cmdbuf *buf);

#endif /* __ETNAVIV_CMDBUF_H__ */
