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
	return 0;
}

int
nouveau_mem_vram(struct ttm_mem_reg *reg, bool contig, u8 page)
{
	struct nouveau_mem *mem = nouveau_mem(reg);
	struct nvkm_ram *ram = nvxx_fb(&mem->cli->device)->ram;
	u64 size = ALIGN(reg->num_pages << PAGE_SHIFT, 1 << page);
	int ret;

	mem->mem.page = page;

	ret = ram->func->get(ram, size, 1 << page, contig ? 0 : 1 << page,
			     (mem->comp << 8) | mem->kind, &mem->_mem);
	if (ret)
		return ret;

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

	reg->mm_node = mem;
	return 0;
}
