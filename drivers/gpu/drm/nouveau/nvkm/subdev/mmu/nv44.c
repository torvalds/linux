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
#include "nv04.h"

#include <core/gpuobj.h>
#include <core/option.h>
#include <subdev/timer.h>

#define NV44_GART_SIZE (512 * 1024 * 1024)
#define NV44_GART_PAGE (  4 * 1024)

/*******************************************************************************
 * VM map/unmap callbacks
 ******************************************************************************/

static void
nv44_vm_fill(struct nvkm_gpuobj *pgt, dma_addr_t null,
	     dma_addr_t *list, u32 pte, u32 cnt)
{
	u32 base = (pte << 2) & ~0x0000000f;
	u32 tmp[4];

	tmp[0] = nvkm_ro32(pgt, base + 0x0);
	tmp[1] = nvkm_ro32(pgt, base + 0x4);
	tmp[2] = nvkm_ro32(pgt, base + 0x8);
	tmp[3] = nvkm_ro32(pgt, base + 0xc);

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

	nvkm_wo32(pgt, base + 0x0, tmp[0]);
	nvkm_wo32(pgt, base + 0x4, tmp[1]);
	nvkm_wo32(pgt, base + 0x8, tmp[2]);
	nvkm_wo32(pgt, base + 0xc, tmp[3] | 0x40000000);
}

static void
nv44_vm_map_sg(struct nvkm_vma *vma, struct nvkm_gpuobj *pgt,
	       struct nvkm_mem *mem, u32 pte, u32 cnt, dma_addr_t *list)
{
	struct nv04_mmu *mmu = (void *)vma->vm->mmu;
	u32 tmp[4];
	int i;

	nvkm_kmap(pgt);
	if (pte & 3) {
		u32  max = 4 - (pte & 3);
		u32 part = (cnt > max) ? max : cnt;
		nv44_vm_fill(pgt, mmu->null, list, pte, part);
		pte  += part;
		list += part;
		cnt  -= part;
	}

	while (cnt >= 4) {
		for (i = 0; i < 4; i++)
			tmp[i] = *list++ >> 12;
		nvkm_wo32(pgt, pte++ * 4, tmp[0] >>  0 | tmp[1] << 27);
		nvkm_wo32(pgt, pte++ * 4, tmp[1] >>  5 | tmp[2] << 22);
		nvkm_wo32(pgt, pte++ * 4, tmp[2] >> 10 | tmp[3] << 17);
		nvkm_wo32(pgt, pte++ * 4, tmp[3] >> 15 | 0x40000000);
		cnt -= 4;
	}

	if (cnt)
		nv44_vm_fill(pgt, mmu->null, list, pte, cnt);
	nvkm_done(pgt);
}

static void
nv44_vm_unmap(struct nvkm_gpuobj *pgt, u32 pte, u32 cnt)
{
	struct nv04_mmu *mmu = (void *)nvkm_mmu(pgt);

	nvkm_kmap(pgt);
	if (pte & 3) {
		u32  max = 4 - (pte & 3);
		u32 part = (cnt > max) ? max : cnt;
		nv44_vm_fill(pgt, mmu->null, NULL, pte, part);
		pte  += part;
		cnt  -= part;
	}

	while (cnt >= 4) {
		nvkm_wo32(pgt, pte++ * 4, 0x00000000);
		nvkm_wo32(pgt, pte++ * 4, 0x00000000);
		nvkm_wo32(pgt, pte++ * 4, 0x00000000);
		nvkm_wo32(pgt, pte++ * 4, 0x00000000);
		cnt -= 4;
	}

	if (cnt)
		nv44_vm_fill(pgt, mmu->null, NULL, pte, cnt);
	nvkm_done(pgt);
}

static void
nv44_vm_flush(struct nvkm_vm *vm)
{
	struct nv04_mmu *mmu = (void *)vm->mmu;
	struct nvkm_device *device = mmu->base.subdev.device;
	nvkm_wr32(device, 0x100814, mmu->base.limit - NV44_GART_PAGE);
	nvkm_wr32(device, 0x100808, 0x00000020);
	nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x100808) & 0x00000001)
			break;
	);
	nvkm_wr32(device, 0x100808, 0x00000000);
}

/*******************************************************************************
 * MMU subdev
 ******************************************************************************/

static int
nv44_mmu_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	struct nvkm_device *device = nv_device(parent);
	struct nv04_mmu *mmu;
	int ret;

	if (pci_find_capability(device->pdev, PCI_CAP_ID_AGP) ||
	    !nvkm_boolopt(device->cfgopt, "NvPCIE", true)) {
		return nvkm_object_old(parent, engine, &nv04_mmu_oclass,
					data, size, pobject);
	}

	ret = nvkm_mmu_create(parent, engine, oclass, "PCIEGART",
			      "mmu", &mmu);
	*pobject = nv_object(mmu);
	if (ret)
		return ret;

	mmu->base.create = nv04_vm_create;
	mmu->base.limit = NV44_GART_SIZE;
	mmu->base.dma_bits = 39;
	mmu->base.pgt_bits = 32 - 12;
	mmu->base.spg_shift = 12;
	mmu->base.lpg_shift = 12;
	mmu->base.map_sg = nv44_vm_map_sg;
	mmu->base.unmap = nv44_vm_unmap;
	mmu->base.flush = nv44_vm_flush;

	mmu->nullp = pci_alloc_consistent(device->pdev, 16 * 1024, &mmu->null);
	if (!mmu->nullp) {
		nvkm_warn(&mmu->base.subdev, "unable to allocate dummy pages\n");
		mmu->null = 0;
	}

	ret = nvkm_vm_create(&mmu->base, 0, NV44_GART_SIZE, 0, 4096,
			     &mmu->vm);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(mmu), NULL,
			      (NV44_GART_SIZE / NV44_GART_PAGE) * 4,
			      512 * 1024, NVOBJ_FLAG_ZERO_ALLOC,
			      &mmu->vm->pgt[0].obj[0]);
	mmu->vm->pgt[0].refcount[0] = 1;
	if (ret)
		return ret;

	return 0;
}

static int
nv44_mmu_init(struct nvkm_object *object)
{
	struct nv04_mmu *mmu = (void *)object;
	struct nvkm_device *device = mmu->base.subdev.device;
	struct nvkm_gpuobj *gart = mmu->vm->pgt[0].obj[0];
	u32 addr;
	int ret;

	ret = nvkm_mmu_init(&mmu->base);
	if (ret)
		return ret;

	/* calculate vram address of this PRAMIN block, object must be
	 * allocated on 512KiB alignment, and not exceed a total size
	 * of 512KiB for this to work correctly
	 */
	addr  = nvkm_rd32(device, 0x10020c);
	addr -= ((gart->addr >> 19) + 1) << 19;

	nvkm_wr32(device, 0x100850, 0x80000000);
	nvkm_wr32(device, 0x100818, mmu->null);
	nvkm_wr32(device, 0x100804, NV44_GART_SIZE);
	nvkm_wr32(device, 0x100850, 0x00008000);
	nvkm_mask(device, 0x10008c, 0x00000200, 0x00000200);
	nvkm_wr32(device, 0x100820, 0x00000000);
	nvkm_wr32(device, 0x10082c, 0x00000001);
	nvkm_wr32(device, 0x100800, addr | 0x00000010);
	return 0;
}

struct nvkm_oclass
nv44_mmu_oclass = {
	.handle = NV_SUBDEV(MMU, 0x44),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv44_mmu_ctor,
		.dtor = nv04_mmu_dtor,
		.init = nv44_mmu_init,
		.fini = _nvkm_mmu_fini,
	},
};
