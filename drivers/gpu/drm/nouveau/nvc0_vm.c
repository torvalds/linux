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
	if (vma->access & NV_MEM_ACCESS_SYS)
		phys |= 0x00000002;

	phys |= ((u64)target  << 32);
	phys |= ((u64)memtype << 36);

	return phys;
}

void
nvc0_vm_map(struct nouveau_vma *vma, struct nouveau_gpuobj *pgt,
	    struct nouveau_mem *mem, u32 pte, u32 cnt, u64 phys, u64 delta)
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
	       struct nouveau_mem *mem, u32 pte, u32 cnt, dma_addr_t *list)
{
	pte <<= 3;
	while (cnt--) {
		u64 phys = nvc0_vm_addr(vma, *list++, mem->memtype, 5);
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
	unsigned long flags;
	u32 engine;

	engine = 1;
	if (vm == dev_priv->bar1_vm || vm == dev_priv->bar3_vm)
		engine |= 4;

	pinstmem->flush(vm->dev);

	spin_lock_irqsave(&dev_priv->vm_lock, flags);
	list_for_each_entry(vpgd, &vm->pgd_list, head) {
		/* looks like maybe a "free flush slots" counter, the
		 * faster you write to 0x100cbc to more it decreases
		 */
		if (!nv_wait_ne(dev, 0x100c80, 0x00ff0000, 0x00000000)) {
			NV_ERROR(dev, "vm timeout 0: 0x%08x %d\n",
				 nv_rd32(dev, 0x100c80), engine);
		}
		nv_wr32(dev, 0x100cb8, vpgd->obj->vinst >> 8);
		nv_wr32(dev, 0x100cbc, 0x80000000 | engine);
		/* wait for flush to be queued? */
		if (!nv_wait(dev, 0x100c80, 0x00008000, 0x00008000)) {
			NV_ERROR(dev, "vm timeout 1: 0x%08x %d\n",
				 nv_rd32(dev, 0x100c80), engine);
		}
	}
	spin_unlock_irqrestore(&dev_priv->vm_lock, flags);
}
