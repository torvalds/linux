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

#include <core/device.h>
#include <core/gpuobj.h>

#include <subdev/timer.h>
#include <subdev/fb.h>
#include <subdev/vm.h>
#include <subdev/ltcg.h>

struct nvc0_vmmgr_priv {
	struct nouveau_vmmgr base;
};


/* Map from compressed to corresponding uncompressed storage type.
 * The value 0xff represents an invalid storage type.
 */
const u8 nvc0_pte_storage_type_map[256] =
{
	0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0xff, 0x01, /* 0x00 */
	0x01, 0x01, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x11, 0xff, 0xff, 0xff, 0xff, 0xff, 0x11, /* 0x10 */
	0x11, 0x11, 0x11, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x26, 0x27, /* 0x20 */
	0x28, 0x29, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0x30 */
	0xff, 0xff, 0x26, 0x27, 0x28, 0x29, 0x26, 0x27,
	0x28, 0x29, 0xff, 0xff, 0xff, 0xff, 0x46, 0xff, /* 0x40 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x46, 0x46, 0x46, 0x46, 0xff, 0xff, 0xff, /* 0x50 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0x60 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0x70 */
	0xff, 0xff, 0xff, 0x7b, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7b, 0x7b, /* 0x80 */
	0x7b, 0x7b, 0xff, 0x8b, 0x8c, 0x8d, 0x8e, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0x90 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x8b, 0x8c, 0x8d, 0x8e, 0xa7, /* 0xa0 */
	0xa8, 0xa9, 0xaa, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0xb0 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xa7,
	0xa8, 0xa9, 0xaa, 0xc3, 0xff, 0xff, 0xff, 0xff, /* 0xc0 */
	0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xc3, 0xc3,
	0xc3, 0xc3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0xd0 */
	0xfe, 0xff, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
	0xfe, 0xff, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xff, /* 0xe0 */
	0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xfe, 0xff,
	0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 0xf0 */
	0xfe, 0xfe, 0xfe, 0xfe, 0xff, 0xfd, 0xfe, 0xff
};


static void
nvc0_vm_map_pgt(struct nouveau_gpuobj *pgd, u32 index,
		struct nouveau_gpuobj *pgt[2])
{
	u32 pde[2] = { 0, 0 };

	if (pgt[0])
		pde[1] = 0x00000001 | (pgt[0]->addr >> 8);
	if (pgt[1])
		pde[0] = 0x00000001 | (pgt[1]->addr >> 8);

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

static void
nvc0_vm_map(struct nouveau_vma *vma, struct nouveau_gpuobj *pgt,
	    struct nouveau_mem *mem, u32 pte, u32 cnt, u64 phys, u64 delta)
{
	u64 next = 1 << (vma->node->type - 8);

	phys  = nvc0_vm_addr(vma, phys, mem->memtype, 0);
	pte <<= 3;

	if (mem->tag) {
		struct nouveau_ltcg *ltcg =
			nouveau_ltcg(vma->vm->vmm->base.base.parent);
		u32 tag = mem->tag->offset + (delta >> 17);
		phys |= (u64)tag << (32 + 12);
		next |= (u64)1   << (32 + 12);
		ltcg->tags_clear(ltcg, tag, cnt);
	}

	while (cnt--) {
		nv_wo32(pgt, pte + 0, lower_32_bits(phys));
		nv_wo32(pgt, pte + 4, upper_32_bits(phys));
		phys += next;
		pte  += 8;
	}
}

static void
nvc0_vm_map_sg(struct nouveau_vma *vma, struct nouveau_gpuobj *pgt,
	       struct nouveau_mem *mem, u32 pte, u32 cnt, dma_addr_t *list)
{
	u32 target = (vma->access & NV_MEM_ACCESS_NOSNOOP) ? 7 : 5;
	/* compressed storage types are invalid for system memory */
	u32 memtype = nvc0_pte_storage_type_map[mem->memtype & 0xff];

	pte <<= 3;
	while (cnt--) {
		u64 phys = nvc0_vm_addr(vma, *list++, memtype, target);
		nv_wo32(pgt, pte + 0, lower_32_bits(phys));
		nv_wo32(pgt, pte + 4, upper_32_bits(phys));
		pte += 8;
	}
}

static void
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
nvc0_vm_flush_engine(struct nouveau_subdev *subdev, u64 addr, int type)
{
	struct nvc0_vmmgr_priv *priv = (void *)nouveau_vmmgr(subdev);

	/* looks like maybe a "free flush slots" counter, the
	 * faster you write to 0x100cbc to more it decreases
	 */
	mutex_lock(&nv_subdev(priv)->mutex);
	if (!nv_wait_ne(subdev, 0x100c80, 0x00ff0000, 0x00000000)) {
		nv_error(subdev, "vm timeout 0: 0x%08x %d\n",
			 nv_rd32(subdev, 0x100c80), type);
	}

	nv_wr32(subdev, 0x100cb8, addr >> 8);
	nv_wr32(subdev, 0x100cbc, 0x80000000 | type);

	/* wait for flush to be queued? */
	if (!nv_wait(subdev, 0x100c80, 0x00008000, 0x00008000)) {
		nv_error(subdev, "vm timeout 1: 0x%08x %d\n",
			 nv_rd32(subdev, 0x100c80), type);
	}
	mutex_unlock(&nv_subdev(priv)->mutex);
}

static void
nvc0_vm_flush(struct nouveau_vm *vm)
{
	struct nouveau_vm_pgd *vpgd;

	list_for_each_entry(vpgd, &vm->pgd_list, head) {
		nvc0_vm_flush_engine(nv_subdev(vm->vmm), vpgd->obj->addr, 1);
	}
}

static int
nvc0_vm_create(struct nouveau_vmmgr *vmm, u64 offset, u64 length,
	       u64 mm_offset, struct nouveau_vm **pvm)
{
	return nouveau_vm_create(vmm, offset, length, mm_offset, 4096, pvm);
}

static int
nvc0_vmmgr_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nvc0_vmmgr_priv *priv;
	int ret;

	ret = nouveau_vmmgr_create(parent, engine, oclass, "VM", "vm", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.limit = 1ULL << 40;
	priv->base.dma_bits = 40;
	priv->base.pgt_bits  = 27 - 12;
	priv->base.spg_shift = 12;
	priv->base.lpg_shift = 17;
	priv->base.create = nvc0_vm_create;
	priv->base.map_pgt = nvc0_vm_map_pgt;
	priv->base.map = nvc0_vm_map;
	priv->base.map_sg = nvc0_vm_map_sg;
	priv->base.unmap = nvc0_vm_unmap;
	priv->base.flush = nvc0_vm_flush;
	return 0;
}

struct nouveau_oclass
nvc0_vmmgr_oclass = {
	.handle = NV_SUBDEV(VM, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_vmmgr_ctor,
		.dtor = _nouveau_vmmgr_dtor,
		.init = _nouveau_vmmgr_init,
		.fini = _nouveau_vmmgr_fini,
	},
};
