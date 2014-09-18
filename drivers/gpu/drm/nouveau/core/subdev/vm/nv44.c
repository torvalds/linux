/*
 * Copyright 2012 Red Hat Inc.
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

#include <core/gpuobj.h>
#include <core/option.h>

#include <subdev/timer.h>
#include <subdev/vm.h>

#include "nv04.h"

#define NV44_GART_SIZE (512 * 1024 * 1024)
#define NV44_GART_PAGE (  4 * 1024)

/*******************************************************************************
 * VM map/unmap callbacks
 ******************************************************************************/

static void
nv44_vm_fill(struct nouveau_gpuobj *pgt, dma_addr_t null,
	     dma_addr_t *list, u32 pte, u32 cnt)
{
	u32 base = (pte << 2) & ~0x0000000f;
	u32 tmp[4];

	tmp[0] = nv_ro32(pgt, base + 0x0);
	tmp[1] = nv_ro32(pgt, base + 0x4);
	tmp[2] = nv_ro32(pgt, base + 0x8);
	tmp[3] = nv_ro32(pgt, base + 0xc);

	while (cnt--) {
		u32 addr = list ? (*list++ >> 12) : (null >> 12);
		switch (pte++ & 0x3) {
		case 0:
			tmp[0] &= ~0x07ffffff;
			tmp[0] |= addr;
			break;
		case 1:
			tmp[0] &= ~0xf8000000;
			tmp[0] |= addr << 27;
			tmp[1] &= ~0x003fffff;
			tmp[1] |= addr >> 5;
			break;
		case 2:
			tmp[1] &= ~0xffc00000;
			tmp[1] |= addr << 22;
			tmp[2] &= ~0x0001ffff;
			tmp[2] |= addr >> 10;
			break;
		case 3:
			tmp[2] &= ~0xfffe0000;
			tmp[2] |= addr << 17;
			tmp[3] &= ~0x00000fff;
			tmp[3] |= addr >> 15;
			break;
		}
	}

	nv_wo32(pgt, base + 0x0, tmp[0]);
	nv_wo32(pgt, base + 0x4, tmp[1]);
	nv_wo32(pgt, base + 0x8, tmp[2]);
	nv_wo32(pgt, base + 0xc, tmp[3] | 0x40000000);
}

static void
nv44_vm_map_sg(struct nouveau_vma *vma, struct nouveau_gpuobj *pgt,
	       struct nouveau_mem *mem, u32 pte, u32 cnt, dma_addr_t *list)
{
	struct nv04_vmmgr_priv *priv = (void *)vma->vm->vmm;
	u32 tmp[4];
	int i;

	if (pte & 3) {
		u32  max = 4 - (pte & 3);
		u32 part = (cnt > max) ? max : cnt;
		nv44_vm_fill(pgt, priv->null, list, pte, part);
		pte  += part;
		list += part;
		cnt  -= part;
	}

	while (cnt >= 4) {
		for (i = 0; i < 4; i++)
			tmp[i] = *list++ >> 12;
		nv_wo32(pgt, pte++ * 4, tmp[0] >>  0 | tmp[1] << 27);
		nv_wo32(pgt, pte++ * 4, tmp[1] >>  5 | tmp[2] << 22);
		nv_wo32(pgt, pte++ * 4, tmp[2] >> 10 | tmp[3] << 17);
		nv_wo32(pgt, pte++ * 4, tmp[3] >> 15 | 0x40000000);
		cnt -= 4;
	}

	if (cnt)
		nv44_vm_fill(pgt, priv->null, list, pte, cnt);
}

static void
nv44_vm_unmap(struct nouveau_gpuobj *pgt, u32 pte, u32 cnt)
{
	struct nv04_vmmgr_priv *priv = (void *)nouveau_vmmgr(pgt);

	if (pte & 3) {
		u32  max = 4 - (pte & 3);
		u32 part = (cnt > max) ? max : cnt;
		nv44_vm_fill(pgt, priv->null, NULL, pte, part);
		pte  += part;
		cnt  -= part;
	}

	while (cnt >= 4) {
		nv_wo32(pgt, pte++ * 4, 0x00000000);
		nv_wo32(pgt, pte++ * 4, 0x00000000);
		nv_wo32(pgt, pte++ * 4, 0x00000000);
		nv_wo32(pgt, pte++ * 4, 0x00000000);
		cnt -= 4;
	}

	if (cnt)
		nv44_vm_fill(pgt, priv->null, NULL, pte, cnt);
}

static void
nv44_vm_flush(struct nouveau_vm *vm)
{
	struct nv04_vmmgr_priv *priv = (void *)vm->vmm;
	nv_wr32(priv, 0x100814, priv->base.limit - NV44_GART_PAGE);
	nv_wr32(priv, 0x100808, 0x00000020);
	if (!nv_wait(priv, 0x100808, 0x00000001, 0x00000001))
		nv_error(priv, "timeout: 0x%08x\n", nv_rd32(priv, 0x100808));
	nv_wr32(priv, 0x100808, 0x00000000);
}

/*******************************************************************************
 * VMMGR subdev
 ******************************************************************************/

static int
nv44_vmmgr_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct nv04_vmmgr_priv *priv;
	int ret;

	if (pci_find_capability(device->pdev, PCI_CAP_ID_AGP) ||
	    !nouveau_boolopt(device->cfgopt, "NvPCIE", true)) {
		return nouveau_object_ctor(parent, engine, &nv04_vmmgr_oclass,
					   data, size, pobject);
	}

	ret = nouveau_vmmgr_create(parent, engine, oclass, "PCIEGART",
				   "pciegart", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.create = nv04_vm_create;
	priv->base.limit = NV44_GART_SIZE;
	priv->base.dma_bits = 39;
	priv->base.pgt_bits = 32 - 12;
	priv->base.spg_shift = 12;
	priv->base.lpg_shift = 12;
	priv->base.map_sg = nv44_vm_map_sg;
	priv->base.unmap = nv44_vm_unmap;
	priv->base.flush = nv44_vm_flush;

	priv->nullp = pci_alloc_consistent(device->pdev, 16 * 1024, &priv->null);
	if (!priv->nullp) {
		nv_error(priv, "unable to allocate dummy pages\n");
		return -ENOMEM;
	}

	ret = nouveau_vm_create(&priv->base, 0, NV44_GART_SIZE, 0, 4096,
				&priv->vm);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), NULL,
				(NV44_GART_SIZE / NV44_GART_PAGE) * 4,
				 512 * 1024, NVOBJ_FLAG_ZERO_ALLOC,
				 &priv->vm->pgt[0].obj[0]);
	priv->vm->pgt[0].refcount[0] = 1;
	if (ret)
		return ret;

	return 0;
}

static int
nv44_vmmgr_init(struct nouveau_object *object)
{
	struct nv04_vmmgr_priv *priv = (void *)object;
	struct nouveau_gpuobj *gart = priv->vm->pgt[0].obj[0];
	u32 addr;
	int ret;

	ret = nouveau_vmmgr_init(&priv->base);
	if (ret)
		return ret;

	/* calculate vram address of this PRAMIN block, object must be
	 * allocated on 512KiB alignment, and not exceed a total size
	 * of 512KiB for this to work correctly
	 */
	addr  = nv_rd32(priv, 0x10020c);
	addr -= ((gart->addr >> 19) + 1) << 19;

	nv_wr32(priv, 0x100850, 0x80000000);
	nv_wr32(priv, 0x100818, priv->null);
	nv_wr32(priv, 0x100804, NV44_GART_SIZE);
	nv_wr32(priv, 0x100850, 0x00008000);
	nv_mask(priv, 0x10008c, 0x00000200, 0x00000200);
	nv_wr32(priv, 0x100820, 0x00000000);
	nv_wr32(priv, 0x10082c, 0x00000001);
	nv_wr32(priv, 0x100800, addr | 0x00000010);
	return 0;
}

struct nouveau_oclass
nv44_vmmgr_oclass = {
	.handle = NV_SUBDEV(VM, 0x44),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv44_vmmgr_ctor,
		.dtor = nv04_vmmgr_dtor,
		.init = nv44_vmmgr_init,
		.fini = _nouveau_vmmgr_fini,
	},
};
