/*
 * Copyright 2013 Red Hat Inc.
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

#include <subdev/bios.h>
#include <core/mm.h>
#include "priv.h"

void
__nv50_ram_put(struct nouveau_fb *pfb, struct nouveau_mem *mem)
{
	struct nouveau_mm_node *this;

	while (!list_empty(&mem->regions)) {
		this = list_first_entry(&mem->regions, typeof(*this), rl_entry);

		list_del(&this->rl_entry);
		nouveau_mm_free(&pfb->vram, &this);
	}

	nouveau_mm_free(&pfb->tags, &mem->tag);
}

void
nv50_ram_put(struct nouveau_fb *pfb, struct nouveau_mem **pmem)
{
	struct nouveau_mem *mem = *pmem;

	*pmem = NULL;
	if (unlikely(mem == NULL))
		return;

	mutex_lock(&pfb->base.mutex);
	__nv50_ram_put(pfb, mem);
	mutex_unlock(&pfb->base.mutex);

	kfree(mem);
}

static int
nv50_ram_get(struct nouveau_fb *pfb, u64 size, u32 align, u32 ncmin,
	     u32 memtype, struct nouveau_mem **pmem)
{
	struct nouveau_mm *heap = &pfb->vram;
	struct nouveau_mm *tags = &pfb->tags;
	struct nouveau_mm_node *r;
	struct nouveau_mem *mem;
	int comp = (memtype & 0x300) >> 8;
	int type = (memtype & 0x07f);
	int back = (memtype & 0x800);
	int min, max, ret;

	max = (size >> 12);
	min = ncmin ? (ncmin >> 12) : max;
	align >>= 12;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	mutex_lock(&pfb->base.mutex);
	if (comp) {
		if (align == 16) {
			int n = (max >> 4) * comp;

			ret = nouveau_mm_head(tags, 1, n, n, 1, &mem->tag);
			if (ret)
				mem->tag = NULL;
		}

		if (unlikely(!mem->tag))
			comp = 0;
	}

	INIT_LIST_HEAD(&mem->regions);
	mem->memtype = (comp << 7) | type;
	mem->size = max;

	type = nv50_fb_memtype[type];
	do {
		if (back)
			ret = nouveau_mm_tail(heap, type, max, min, align, &r);
		else
			ret = nouveau_mm_head(heap, type, max, min, align, &r);
		if (ret) {
			mutex_unlock(&pfb->base.mutex);
			pfb->ram->put(pfb, &mem);
			return ret;
		}

		list_add_tail(&r->rl_entry, &mem->regions);
		max -= r->length;
	} while (max);
	mutex_unlock(&pfb->base.mutex);

	r = list_first_entry(&mem->regions, struct nouveau_mm_node, rl_entry);
	mem->offset = (u64)r->offset << 12;
	*pmem = mem;
	return 0;
}

static u32
nv50_fb_vram_rblock(struct nouveau_fb *pfb, struct nouveau_ram *ram)
{
	int i, parts, colbits, rowbitsa, rowbitsb, banks;
	u64 rowsize, predicted;
	u32 r0, r4, rt, ru, rblock_size;

	r0 = nv_rd32(pfb, 0x100200);
	r4 = nv_rd32(pfb, 0x100204);
	rt = nv_rd32(pfb, 0x100250);
	ru = nv_rd32(pfb, 0x001540);
	nv_debug(pfb, "memcfg 0x%08x 0x%08x 0x%08x 0x%08x\n", r0, r4, rt, ru);

	for (i = 0, parts = 0; i < 8; i++) {
		if (ru & (0x00010000 << i))
			parts++;
	}

	colbits  =  (r4 & 0x0000f000) >> 12;
	rowbitsa = ((r4 & 0x000f0000) >> 16) + 8;
	rowbitsb = ((r4 & 0x00f00000) >> 20) + 8;
	banks    = 1 << (((r4 & 0x03000000) >> 24) + 2);

	rowsize = parts * banks * (1 << colbits) * 8;
	predicted = rowsize << rowbitsa;
	if (r0 & 0x00000004)
		predicted += rowsize << rowbitsb;

	if (predicted != ram->size) {
		nv_warn(pfb, "memory controller reports %d MiB VRAM\n",
			(u32)(ram->size >> 20));
	}

	rblock_size = rowsize;
	if (rt & 1)
		rblock_size *= 3;

	nv_debug(pfb, "rblock %d bytes\n", rblock_size);
	return rblock_size;
}

static int
nv50_ram_create(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 datasize,
		struct nouveau_object **pobject)
{
	struct nouveau_fb *pfb = nouveau_fb(parent);
	struct nouveau_device *device = nv_device(pfb);
	struct nouveau_bios *bios = nouveau_bios(device);
	struct nouveau_ram *ram;
	const u32 rsvd_head = ( 256 * 1024) >> 12; /* vga memory */
	const u32 rsvd_tail = (1024 * 1024) >> 12; /* vbios etc */
	u32 size;
	int ret;

	ret = nouveau_ram_create(parent, engine, oclass, &ram);
	*pobject = nv_object(ram);
	if (ret)
		return ret;

	ram->size = nv_rd32(pfb, 0x10020c);
	ram->size = (ram->size & 0xffffff00) |
		       ((ram->size & 0x000000ff) << 32);

	size = (ram->size >> 12) - rsvd_head - rsvd_tail;
	switch (device->chipset) {
	case 0xaa:
	case 0xac:
	case 0xaf: /* IGPs, no reordering, no real VRAM */
		ret = nouveau_mm_init(&pfb->vram, rsvd_head, size, 1);
		if (ret)
			return ret;

		ram->type   = NV_MEM_TYPE_STOLEN;
		ram->stolen = (u64)nv_rd32(pfb, 0x100e10) << 12;
		break;
	default:
		switch (nv_rd32(pfb, 0x100714) & 0x00000007) {
		case 0: ram->type = NV_MEM_TYPE_DDR1; break;
		case 1:
			if (nouveau_fb_bios_memtype(bios) == NV_MEM_TYPE_DDR3)
				ram->type = NV_MEM_TYPE_DDR3;
			else
				ram->type = NV_MEM_TYPE_DDR2;
			break;
		case 2: ram->type = NV_MEM_TYPE_GDDR3; break;
		case 3: ram->type = NV_MEM_TYPE_GDDR4; break;
		case 4: ram->type = NV_MEM_TYPE_GDDR5; break;
		default:
			break;
		}

		ret = nouveau_mm_init(&pfb->vram, rsvd_head, size,
				      nv50_fb_vram_rblock(pfb, ram) >> 12);
		if (ret)
			return ret;

		ram->ranks = (nv_rd32(pfb, 0x100200) & 0x4) ? 2 : 1;
		ram->tags  =  nv_rd32(pfb, 0x100320);
		break;
	}

	ram->get = nv50_ram_get;
	ram->put = nv50_ram_put;
	return 0;
}

struct nouveau_oclass
nv50_ram_oclass = {
	.handle = 0,
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_ram_create,
		.dtor = _nouveau_ram_dtor,
		.init = _nouveau_ram_init,
		.fini = _nouveau_ram_fini,
	}
};
