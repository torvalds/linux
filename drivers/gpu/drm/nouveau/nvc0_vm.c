/*
 * Copyright 2010 Red Hat Inc.
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
 *
 * Authors: Ben Skeggs
 */

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_vm.h"

void
nvc0_vm_map_pgt(struct nouveau_gpuobj *pgd, u32 index,
		struct nouveau_gpuobj *pgt[2])
{
	u32 pde[2] = { 0, 0 };

	if (pgt[0])
		pde[1] = 0x00000001 | (pgt[0]->vinst >> 8);
	if (pgt[1])
		pde[0] = 0x00000001 | (pgt[1]->vinst >> 8);

	nv_wo32(pgd, (index * 8) + 0, pde[0]);
	nv_wo32(pgd, (index * 8) + 4, pde[1]);
}

static inline u64
nvc0_vm_addr(struct nouveau_vma *vma, u64 phys, u32 memtype, u32 target)
{
	phys >>= 8;

	phys |= 0x00000001; /* present */
//	if (vma->access & NV_MEM_ACCESS_SYS)
//		phys |= 0x00000002;

	phys |= ((u64)target  << 32);
	phys |= ((u64)memtype << 36);

	return phys;
}

void
nvc0_vm_map(struct nouveau_vma *vma, struct nouveau_gpuobj *pgt,
	    struct nouveau_vram *mem, u32 pte, u32 cnt, u64 phys)
{
	u32 next = 1 << (vma->node->type - 8);

	phys  = nvc0_vm_addr(vma, phys, mem->memtype, 0);
	pte <<= 3;
	while (cnt--) {
		nv_wo32(pgt, pte + 0, lower_32_bits(phys));
		nv_wo32(pgt, pte + 4, upper_32_bits(phys));
		phys += next;
		pte  += 8;
	}
}

void
nvc0_vm_map_sg(struct nouveau_vma *vma, struct nouveau_gpuobj *pgt,
	       u32 pte, dma_addr_t *list, u32 cnt)
{
	pte <<= 3;
	while (cnt--) {
		u64 phys = nvc0_vm_addr(vma, *list++, 0, 5);
		nv_wo32(pgt, pte + 0, lower_32_bits(phys));
		nv_wo32(pgt, pte + 4, upper_32_bits(phys));
		pte += 8;
	}
}

void
nvc0_vm_unmap(struct nouveau_gpuobj *pgt, u32 pte, u32 cnt)
{
	pte <<= 3;
	while (cnt--) {
		nv_wo32(pgt, pte + 0, 0x00000000);
		nv_wo32(pgt, pte + 4, 0x00000000);
		pte += 8;
	}
}

void
nvc0_vm_flush(struct nouveau_vm *vm)
{
	struct drm_nouveau_private *dev_priv = vm->dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct drm_device *dev = vm->dev;
	struct nouveau_vm_pgd *vpgd;
	u32 r100c80, engine;

	pinstmem->flush(vm->dev);

	if (vm == dev_priv->chan_vm)
		engine = 1;
	else
		engine = 5;

	list_for_each_entry(vpgd, &vm->pgd_list, head) {
		r100c80 = nv_rd32(dev, 0x100c80);
		nv_wr32(dev, 0x100cb8, vpgd->obj->vinst >> 8);
		nv_wr32(dev, 0x100cbc, 0x80000000 | engine);
		if (!nv_wait(dev, 0x100c80, 0xffffffff, r100c80))
			NV_ERROR(dev, "vm flush timeout eng %d\n", engine);
	}
}
