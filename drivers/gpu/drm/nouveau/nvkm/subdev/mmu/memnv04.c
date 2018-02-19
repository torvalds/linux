/*
 * Copyright 2017 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "mem.h"

#include <core/memory.h>
#include <subdev/fb.h>

#include <nvif/if000b.h>
#include <nvif/unpack.h>

int
nv04_mem_map(struct nvkm_mmu *mmu, struct nvkm_memory *memory, void *argv,
	     u32 argc, u64 *paddr, u64 *psize, struct nvkm_vma **pvma)
{
	union {
		struct nv04_mem_map_vn vn;
	} *args = argv;
	struct nvkm_device *device = mmu->subdev.device;
	const u64 addr = nvkm_memory_addr(memory);
	int ret = -ENOSYS;

	if ((ret = nvif_unvers(ret, &argv, &argc, args->vn)))
		return ret;

	*paddr = device->func->resource_addr(device, 1) + addr;
	*psize = nvkm_memory_size(memory);
	*pvma = ERR_PTR(-ENODEV);
	return 0;
}

int
nv04_mem_new(struct nvkm_mmu *mmu, int type, u8 page, u64 size,
	     void *argv, u32 argc, struct nvkm_memory **pmemory)
{
	union {
		struct nv04_mem_vn vn;
	} *args = argv;
	int ret = -ENOSYS;

	if ((ret = nvif_unvers(ret, &argv, &argc, args->vn)))
		return ret;

	if (mmu->type[type].type & NVKM_MEM_MAPPABLE)
		type = NVKM_RAM_MM_NORMAL;
	else
		type = NVKM_RAM_MM_NOMAP;

	return nvkm_ram_get(mmu->subdev.device, type, 0x01, page,
			    size, true, false, pmemory);
}
