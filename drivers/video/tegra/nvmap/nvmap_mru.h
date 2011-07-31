/*
 * drivers/video/tegra/nvmap_mru.c
 *
 * IOVMM virtualization support for nvmap
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#ifndef __VIDEO_TEGRA_NVMAP_MRU_H
#define __VIDEO_TEGRA_NVMAP_MRU_H

#include <linux/spinlock.h>

#include "nvmap.h"

struct tegra_iovmm_area;
struct tegra_iovmm_client;

#ifdef CONFIG_NVMAP_RECLAIM_UNPINNED_VM

static inline void nvmap_mru_lock(struct nvmap_share *share)
{
	spin_lock(&share->mru_lock);
}

static inline void nvmap_mru_unlock(struct nvmap_share *share)
{
	spin_unlock(&share->mru_lock);
}

int nvmap_mru_init(struct nvmap_share *share);

void nvmap_mru_destroy(struct nvmap_share *share);

size_t nvmap_mru_vm_size(struct tegra_iovmm_client *iovmm);

void nvmap_mru_insert_locked(struct nvmap_share *share, struct nvmap_handle *h);

void nvmap_mru_remove(struct nvmap_share *s, struct nvmap_handle *h);

struct tegra_iovmm_area *nvmap_handle_iovmm(struct nvmap_client *c,
					    struct nvmap_handle *h);

#else

#define nvmap_mru_lock(_s)	do { } while (0)
#define nvmap_mru_unlock(_s)	do { } while (0)
#define nvmap_mru_init(_s)	0
#define nvmap_mru_destroy(_s)	do { } while (0)
#define nvmap_mru_vm_size(_a)	tegra_iovmm_get_vm_size(_a)

static inline void nvmap_mru_insert_locked(struct nvmap_share *share,
                                           struct nvmap_handle *h)
{ }

static inline void nvmap_mru_remove(struct nvmap_share *s,
                                    struct nvmap_handle *h)
{ }

static inline struct tegra_iovmm_area *nvmap_handle_iovmm(struct nvmap_client *c,
							  struct nvmap_handle *h)
{
	BUG_ON(!h->pgalloc.area);
	return h->pgalloc.area;
}

#endif

#endif
