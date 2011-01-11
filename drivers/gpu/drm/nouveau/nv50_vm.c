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
nv50_vm_map_pgt(struct nouveau_gpuobj *pgd, u32 pde,
		struct nouveau_gpuobj *pgt[2])
{
	struct drm_nouveau_private *dev_priv = pgd->dev->dev_private;
	u64 phys = 0xdeadcafe00000000ULL;
	u32 coverage = 0;

	if (pgt[0]) {
		phys = 0x00000003 | pgt[0]->vinst; /* present, 4KiB pages */
		coverage = (pgt[0]->size >> 3) << 12;
	} else
	if (pgt[1]) {
		phys = 0x00000001 | pgt[1]->vinst; /* present */
		coverage = (pgt[1]->size >> 3) << 16;
	}

	if (phys & 1) {
		if (dev_priv->vram_sys_base) {
			phys += dev_priv->vram_sys_base;
			phys |= 0x30;
		}

		if (coverage <= 32 * 1024 * 1024)
			phys |= 0x60;
		else if (coverage <= 64 * 1024 * 1024)
			phys |= 0x40;
		else if (coverage < 128 * 1024 * 1024)
			phys |= 0x20;
	}

	nv_wo32(pgd, (pde * 8) + 0, lower_32_bits(phys));
	nv_wo32(pgd, (pde * 8) + 4, upper_32_bits(phys));
}

static inline u64
nv50_vm_addr(struct nouveau_vma *vma, struct nouveau_gpuobj *pgt,
	     u64 phys, u32 memtype, u32 target)
{
	struct drm_nouveau_private *dev_priv = pgt->dev->dev_private;

	phys |= 1; /* present */
	phys |= (u64)memtype << 40;

	/* IGPs don't have real VRAM, re-target to stolen system memory */
	if (target == 0 && dev_priv->vram_sys_base) {
		phys  += dev_priv->vram_sys_base;
		target = 3;
	}

	phys |= target << 4;

	if (vma->access & NV_MEM_ACCESS_SYS)
		phys |= (1 << 6);

	if (!(vma->access & NV_MEM_ACCESS_WO))
		phys |= (1 << 3);

	return phys;
}

void
nv50_vm_map(struct nouveau_vma *vma, struct nouveau_gpuobj *pgt,
	    struct nouveau_vram *mem, u32 pte, u32 cnt, u64 phys)
{
	u32 block;
	int i;

	phys  = nv50_vm_addr(vma, pgt, phys, mem->memtype, 0);
	pte <<= 3;
	cnt <<= 3;

	while (cnt) {
		u32 offset_h = upper_32_bits(phys);
		u32 offset_l = lower_32_bits(phys);

		for (i = 7; i >= 0; i--) {
			block = 1 << (i + 3);
			if (cnt >= block && !(pte & (block - 1)))
				break;
		}
		offset_l |= (i << 7);

		phys += block << (vma->node->type - 3);
		cnt  -= block;

		while (block) {
			nv_wo32(pgt, pte + 0, offset_l);
			nv_wo32(pgt, pte + 4, offset_h);
			pte += 8;
			block -= 8;
		}
	}
}

void
nv50_vm_map_sg(struct nouveau_vma *vma, struct nouveau_gpuobj *pgt,
	       u32 pte, dma_addr_t *list, u32 cnt)
{
	pte <<= 3;
	while (cnt--) {
		u64 phys = nv50_vm_addr(vma, pgt, (u64)*list++, 0, 2);
		nv_wo32(pgt, pte + 0, lower_32_bits(phys));
		nv_wo32(pgt, pte + 4, upper_32_bits(phys));
		pte += 8;
	}
}

void
nv50_vm_unmap(struct nouveau_gpuobj *pgt, u32 pte, u32 cnt)
{
	pte <<= 3;
	while (cnt--) {
		nv_wo32(pgt, pte + 0, 0x00000000);
		nv_wo32(pgt, pte + 4, 0x00000000);
		pte += 8;
	}
}

void
nv50_vm_flush(struct nouveau_vm *vm)
{
	struct drm_nouveau_private *dev_priv = vm->dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_crypt_engine *pcrypt = &dev_priv->engine.crypt;

	pinstmem->flush(vm->dev);

	/* BAR */
	if (vm != dev_priv->chan_vm) {
		nv50_vm_flush_engine(vm->dev, 6);
		return;
	}

	pfifo->tlb_flush(vm->dev);

	if (atomic_read(&vm->pgraph_refs))
		pgraph->tlb_flush(vm->dev);
	if (atomic_read(&vm->pcrypt_refs))
		pcrypt->tlb_flush(vm->dev);
}

void
nv50_vm_flush_engine(struct drm_device *dev, int engine)
{
	nv_wr32(dev, 0x100c80, (engine << 16) | 1);
	if (!nv_wait(dev, 0x100c80, 0x00000001, 0x00000000))
		NV_ERROR(dev, "vm flush timeout: engine %d\n", engine);
}
