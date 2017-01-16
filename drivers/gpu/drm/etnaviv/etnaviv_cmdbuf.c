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

#include "etnaviv_cmdbuf.h"
#include "etnaviv_gpu.h"
#include "etnaviv_mmu.h"

struct etnaviv_cmdbuf *etnaviv_cmdbuf_new(struct etnaviv_gpu *gpu, u32 size,
	size_t nr_bos)
{
	struct etnaviv_cmdbuf *cmdbuf;
	size_t sz = size_vstruct(nr_bos, sizeof(cmdbuf->bo_map[0]),
				 sizeof(*cmdbuf));

	cmdbuf = kzalloc(sz, GFP_KERNEL);
	if (!cmdbuf)
		return NULL;

	if (gpu->mmu->version == ETNAVIV_IOMMU_V2)
		size = ALIGN(size, SZ_4K);

	cmdbuf->vaddr = dma_alloc_wc(gpu->dev, size, &cmdbuf->paddr,
				     GFP_KERNEL);
	if (!cmdbuf->vaddr) {
		kfree(cmdbuf);
		return NULL;
	}

	cmdbuf->gpu = gpu;
	cmdbuf->size = size;

	return cmdbuf;
}

void etnaviv_cmdbuf_free(struct etnaviv_cmdbuf *cmdbuf)
{
	etnaviv_iommu_put_cmdbuf_va(cmdbuf->gpu, cmdbuf);
	dma_free_wc(cmdbuf->gpu->dev, cmdbuf->size, cmdbuf->vaddr,
		    cmdbuf->paddr);
	kfree(cmdbuf);
}
