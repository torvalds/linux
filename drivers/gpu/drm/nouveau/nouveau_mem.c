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
#include "nouveau_mem.h"
#include "nouveau_drv.h"
#include "nouveau_bo.h"

#include <drm/ttm/ttm_bo_driver.h>

int
nouveau_mem_map(struct nouveau_mem *mem,
		struct nvkm_vmm *vmm, struct nvkm_vma *vma)
{
	nvkm_vm_map(vma, mem->_mem);
	return 0;
}

void
nouveau_mem_fini(struct nouveau_mem *mem)
{
	if (mem->vma[1].node) {
		nvkm_vm_unmap(&mem->vma[1]);
		nvkm_vm_put(&mem->vma[1]);
	}
	if (mem->vma[0].node) {
		nvkm_vm_unmap(&mem->vma[0]);
		nvkm_vm_put(&mem->vma[0]);
	}
}

int
nouveau_mem_host(struct ttm_mem_reg *reg, struct ttm_dma_tt *tt)
{
	struct nouveau_mem *mem = nouveau_mem(reg);
	struct nouveau_cli *cli = mem->cli;

	if (mem->kind && cli->device.info.chipset == 0x50)
		mem->comp = mem->kind = 0;
	if (mem->comp) {
		if (cli->device.info.chipset >= 0xc0)
			mem->kind = gf100_pte_storage_type_map[mem->kind];
		mem->comp = 0;
	}

	mem->__mem.size = (reg->num_pages << PAGE_SHIFT) >> 12;
	mem->__mem.memtype = (mem->comp << 7) | mem->kind;
	if (tt->ttm.sg) mem->__mem.sg    = tt->ttm.sg;
	else            mem->__mem.pages = tt->dma_address;
	mem->_mem = &mem->__mem;
	mem->mem.page = 12;
	mem->_mem->memory = &mem->memory;
	return 0;
}

#include <subdev/fb/nv50.h>

struct nvkm_vram {
	struct nvkm_memory memory;
	struct nvkm_ram *ram;
	u8 page;
	struct nvkm_mm_node *mn;
};

int
nouveau_mem_vram(struct ttm_mem_reg *reg, bool contig, u8 page)
{
	struct nouveau_mem *mem = nouveau_mem(reg);
	struct nouveau_cli *cli = mem->cli;
	struct nvkm_device *device = nvxx_device(&cli->device);
	u64 size = ALIGN(reg->num_pages << PAGE_SHIFT, 1 << page);
	u8  type;
	int ret;

	mem->mem.page = page;
	mem->_mem = &mem->__mem;

	if (cli->device.info.chipset < 0xc0) {
		type = nv50_fb_memtype[mem->kind];
	} else {
		if (!mem->comp)
			mem->kind = gf100_pte_storage_type_map[mem->kind];
		mem->comp = 0;
		type = 0x01;
	}

	ret = nvkm_ram_get(device, NVKM_RAM_MM_NORMAL, type, page, size,
			   contig, false, &mem->_mem->memory);
	if (ret)
		return ret;

	mem->_mem->size = size >> NVKM_RAM_MM_SHIFT;
	mem->_mem->offset = nvkm_memory_addr(mem->_mem->memory);
	mem->_mem->mem = ((struct nvkm_vram *)mem->_mem->memory)->mn;
	mem->_mem->memtype = (mem->comp << 7) | mem->kind;

	reg->start = mem->_mem->offset >> PAGE_SHIFT;
	return ret;
}

void
nouveau_mem_del(struct ttm_mem_reg *reg)
{
	struct nouveau_mem *mem = nouveau_mem(reg);
	nouveau_mem_fini(mem);
	kfree(reg->mm_node);
	reg->mm_node = NULL;
}

static enum nvkm_memory_target
nouveau_mem_memory_target(struct nvkm_memory *memory)
{
	struct nouveau_mem *mem = container_of(memory, typeof(*mem), memory);
	if (mem->_mem->mem)
		return NVKM_MEM_TARGET_VRAM;
	return NVKM_MEM_TARGET_HOST;
};

static u8
nouveau_mem_memory_page(struct nvkm_memory *memory)
{
	struct nouveau_mem *mem = container_of(memory, typeof(*mem), memory);
	return mem->mem.page;
};

static u64
nouveau_mem_memory_size(struct nvkm_memory *memory)
{
	struct nouveau_mem *mem = container_of(memory, typeof(*mem), memory);
	return mem->_mem->size << 12;
}

static const struct nvkm_memory_func
nouveau_mem_memory = {
	.target = nouveau_mem_memory_target,
	.page = nouveau_mem_memory_page,
	.size = nouveau_mem_memory_size,
};

int
nouveau_mem_new(struct nouveau_cli *cli, u8 kind, u8 comp,
		struct ttm_mem_reg *reg)
{
	struct nouveau_mem *mem;

	if (!(mem = kzalloc(sizeof(*mem), GFP_KERNEL)))
		return -ENOMEM;
	mem->cli = cli;
	mem->kind = kind;
	mem->comp = comp;
	nvkm_memory_ctor(&nouveau_mem_memory, &mem->memory);

	reg->mm_node = mem;
	return 0;
}
