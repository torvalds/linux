/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017 Etnaviv Project
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
};

struct etnaviv_cmdbuf_suballoc *
etnaviv_cmdbuf_suballoc_new(struct etnaviv_gpu * gpu);
void etnaviv_cmdbuf_suballoc_destroy(struct etnaviv_cmdbuf_suballoc *suballoc);


int etnaviv_cmdbuf_init(struct etnaviv_cmdbuf_suballoc *suballoc,
		struct etnaviv_cmdbuf *cmdbuf, u32 size);
void etnaviv_cmdbuf_free(struct etnaviv_cmdbuf *cmdbuf);

u32 etnaviv_cmdbuf_get_va(struct etnaviv_cmdbuf *buf);
dma_addr_t etnaviv_cmdbuf_get_pa(struct etnaviv_cmdbuf *buf);

#endif /* __ETNAVIV_CMDBUF_H__ */
