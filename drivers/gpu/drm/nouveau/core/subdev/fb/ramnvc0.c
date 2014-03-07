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
#include <subdev/bios/bit.h>
#include <subdev/bios/pll.h>
#include <subdev/bios/rammap.h>
#include <subdev/bios/timing.h>
#include <subdev/ltcg.h>

#include <subdev/clock.h>
#include <subdev/clock/pll.h>

#include <core/option.h>

#include "ramfuc.h"

#include "nvc0.h"

struct nvc0_ramfuc {
	struct ramfuc base;

	struct ramfuc_reg r_0x10fe20;
	struct ramfuc_reg r_0x10fe24;
	struct ramfuc_reg r_0x137320;
	struct ramfuc_reg r_0x137330;

	struct ramfuc_reg r_0x132000;
	struct ramfuc_reg r_0x132004;
	struct ramfuc_reg r_0x132100;

	struct ramfuc_reg r_0x137390;

	struct ramfuc_reg r_0x10f290;
	struct ramfuc_reg r_0x10f294;
	struct ramfuc_reg r_0x10f298;
	struct ramfuc_reg r_0x10f29c;
	struct ramfuc_reg r_0x10f2a0;

	struct ramfuc_reg r_0x10f300;
	struct ramfuc_reg r_0x10f338;
	struct ramfuc_reg r_0x10f340;
	struct ramfuc_reg r_0x10f344;
	struct ramfuc_reg r_0x10f348;

	struct ramfuc_reg r_0x10f910;
	struct ramfuc_reg r_0x10f914;

	struct ramfuc_reg r_0x100b0c;
	struct ramfuc_reg r_0x10f050;
	struct ramfuc_reg r_0x10f090;
	struct ramfuc_reg r_0x10f200;
	struct ramfuc_reg r_0x10f210;
	struct ramfuc_reg r_0x10f310;
	struct ramfuc_reg r_0x10f314;
	struct ramfuc_reg r_0x10f610;
	struct ramfuc_reg r_0x10f614;
	struct ramfuc_reg r_0x10f800;
	struct ramfuc_reg r_0x10f808;
	struct ramfuc_reg r_0x10f824;
	struct ramfuc_reg r_0x10f830;
	struct ramfuc_reg r_0x10f988;
	struct ramfuc_reg r_0x10f98c;
	struct ramfuc_reg r_0x10f990;
	struct ramfuc_reg r_0x10f998;
	struct ramfuc_reg r_0x10f9b0;
	struct ramfuc_reg r_0x10f9b4;
	struct ramfuc_reg r_0x10fb04;
	struct ramfuc_reg r_0x10fb08;
	struct ramfuc_reg r_0x137300;
	struct ramfuc_reg r_0x137310;
	struct ramfuc_reg r_0x137360;
	struct ramfuc_reg r_0x1373ec;
	struct ramfuc_reg r_0x1373f0;
	struct ramfuc_reg r_0x1373f8;

	struct ramfuc_reg r_0x61c140;
	struct ramfuc_reg r_0x611200;

	struct ramfuc_reg r_0x13d8f4;
};

struct nvc0_ram {
	struct nouveau_ram base;
	struct nvc0_ramfuc fuc;
	struct nvbios_pll refpll;
	struct nvbios_pll mempll;
};

static void
nvc0_ram_train(struct nvc0_ramfuc *fuc, u32 magic)
{
	struct nvc0_ram *ram = container_of(fuc, typeof(*ram), fuc);
	struct nouveau_fb *pfb = nouveau_fb(ram);
	u32 part = nv_rd32(pfb, 0x022438), i;
	u32 mask = nv_rd32(pfb, 0x022554);
	u32 addr = 0x110974;

	ram_wr32(fuc, 0x10f910, magic);
	ram_wr32(fuc, 0x10f914, magic);

	for (i = 0; (magic & 0x80000000) && i < part; addr += 0x1000, i++) {
		if (mask & (1 << i))
			continue;
		ram_wait(fuc, addr, 0x0000000f, 0x00000000, 500000);
	}
}

static int
nvc0_ram_calc(struct nouveau_fb *pfb, u32 freq)
{
	struct nouveau_clock *clk = nouveau_clock(pfb);
	struct nouveau_bios *bios = nouveau_bios(pfb);
	struct nvc0_ram *ram = (void *)pfb->ram;
	struct nvc0_ramfuc *fuc = &ram->fuc;
	struct bit_entry M;
	u8  ver, cnt, strap;
	u32 data;
	struct {
		u32 data;
		u8  size;
	} rammap, ramcfg, timing;
	int ref, div, out;
	int from, mode;
	int N1, M1, P;
	int ret;

	/* lookup memory config data relevant to the target frequency */
	rammap.data = nvbios_rammap_match(bios, freq / 1000, &ver, &rammap.size,
					 &cnt, &ramcfg.size);
	if (!rammap.data || ver != 0x10 || rammap.size < 0x0e) {
		nv_error(pfb, "invalid/missing rammap entry\n");
		return -EINVAL;
	}

	/* locate specific data set for the attached memory */
	if (bit_entry(bios, 'M', &M) || M.version != 2 || M.length < 3) {
		nv_error(pfb, "invalid/missing memory table\n");
		return -EINVAL;
	}

	strap = (nv_rd32(pfb, 0x101000) & 0x0000003c) >> 2;
	data = nv_ro16(bios, M.offset + 1);
	if (data)
		strap = nv_ro08(bios, data + strap);

	if (strap >= cnt) {
		nv_error(pfb, "invalid ramcfg strap\n");
		return -EINVAL;
	}

	ramcfg.data = rammap.data + rammap.size + (strap * ramcfg.size);
	if (!ramcfg.data || ver != 0x10 || ramcfg.size < 0x0e) {
		nv_error(pfb, "invalid/missing ramcfg entry\n");
		return -EINVAL;
	}

	/* lookup memory timings, if bios says they're present */
	strap = nv_ro08(bios, ramcfg.data + 0x01);
	if (strap != 0xff) {
		timing.data = nvbios_timing_entry(bios, strap, &ver,
						 &timing.size);
		if (!timing.data || ver != 0x10 || timing.size < 0x19) {
			nv_error(pfb, "invalid/missing timing entry\n");
			return -EINVAL;
		}
	} else {
		timing.data = 0;
	}

	ret = ram_init(fuc, pfb);
	if (ret)
		return ret;

	/* determine current mclk configuration */
	from = !!(ram_rd32(fuc, 0x1373f0) & 0x00000002); /*XXX: ok? */

	/* determine target mclk configuration */
	if (!(ram_rd32(fuc, 0x137300) & 0x00000100))
		ref = clk->read(clk, nv_clk_src_sppll0);
	else
		ref = clk->read(clk, nv_clk_src_sppll1);
	div = max(min((ref * 2) / freq, (u32)65), (u32)2) - 2;
	out = (ref * 2) / (div + 2);
	mode = freq != out;

	ram_mask(fuc, 0x137360, 0x00000002, 0x00000000);

	if ((ram_rd32(fuc, 0x132000) & 0x00000002) || 0 /*XXX*/) {
		ram_nuke(fuc, 0x132000);
		ram_mask(fuc, 0x132000, 0x00000002, 0x00000002);
		ram_mask(fuc, 0x132000, 0x00000002, 0x00000000);
	}

	if (mode == 1) {
		ram_nuke(fuc, 0x10fe20);
		ram_mask(fuc, 0x10fe20, 0x00000002, 0x00000002);
		ram_mask(fuc, 0x10fe20, 0x00000002, 0x00000000);
	}

// 0x00020034 // 0x0000000a
	ram_wr32(fuc, 0x132100, 0x00000001);

	if (mode == 1 && from == 0) {
		/* calculate refpll */
		ret = nva3_pll_calc(nv_subdev(pfb), &ram->refpll,
				    ram->mempll.refclk, &N1, NULL, &M1, &P);
		if (ret <= 0) {
			nv_error(pfb, "unable to calc refpll\n");
			return ret ? ret : -ERANGE;
		}

		ram_wr32(fuc, 0x10fe20, 0x20010000);
		ram_wr32(fuc, 0x137320, 0x00000003);
		ram_wr32(fuc, 0x137330, 0x81200006);
		ram_wr32(fuc, 0x10fe24, (P << 16) | (N1 << 8) | M1);
		ram_wr32(fuc, 0x10fe20, 0x20010001);
		ram_wait(fuc, 0x137390, 0x00020000, 0x00020000, 64000);

		/* calculate mempll */
		ret = nva3_pll_calc(nv_subdev(pfb), &ram->mempll, freq,
				   &N1, NULL, &M1, &P);
		if (ret <= 0) {
			nv_error(pfb, "unable to calc refpll\n");
			return ret ? ret : -ERANGE;
		}

		ram_wr32(fuc, 0x10fe20, 0x20010005);
		ram_wr32(fuc, 0x132004, (P << 16) | (N1 << 8) | M1);
		ram_wr32(fuc, 0x132000, 0x18010101);
		ram_wait(fuc, 0x137390, 0x00000002, 0x00000002, 64000);
	} else
	if (mode == 0) {
		ram_wr32(fuc, 0x137300, 0x00000003);
	}

	if (from == 0) {
		ram_nuke(fuc, 0x10fb04);
		ram_mask(fuc, 0x10fb04, 0x0000ffff, 0x00000000);
		ram_nuke(fuc, 0x10fb08);
		ram_mask(fuc, 0x10fb08, 0x0000ffff, 0x00000000);
		ram_wr32(fuc, 0x10f988, 0x2004ff00);
		ram_wr32(fuc, 0x10f98c, 0x003fc040);
		ram_wr32(fuc, 0x10f990, 0x20012001);
		ram_wr32(fuc, 0x10f998, 0x00011a00);
		ram_wr32(fuc, 0x13d8f4, 0x00000000);
	} else {
		ram_wr32(fuc, 0x10f988, 0x20010000);
		ram_wr32(fuc, 0x10f98c, 0x00000000);
		ram_wr32(fuc, 0x10f990, 0x20012001);
		ram_wr32(fuc, 0x10f998, 0x00010a00);
	}

	if (from == 0) {
// 0x00020039 // 0x000000ba
	}

// 0x0002003a // 0x00000002
	ram_wr32(fuc, 0x100b0c, 0x00080012);
// 0x00030014 // 0x00000000 // 0x02b5f070
// 0x00030014 // 0x00010000 // 0x02b5f070
	ram_wr32(fuc, 0x611200, 0x00003300);
// 0x00020034 // 0x0000000a
// 0x00030020 // 0x00000001 // 0x00000000

	ram_mask(fuc, 0x10f200, 0x00000800, 0x00000000);
	ram_wr32(fuc, 0x10f210, 0x00000000);
	ram_nsec(fuc, 1000);
	if (mode == 0)
		nvc0_ram_train(fuc, 0x000c1001);
	ram_wr32(fuc, 0x10f310, 0x00000001);
	ram_nsec(fuc, 1000);
	ram_wr32(fuc, 0x10f090, 0x00000061);
	ram_wr32(fuc, 0x10f090, 0xc000007f);
	ram_nsec(fuc, 1000);

	if (from == 0) {
		ram_wr32(fuc, 0x10f824, 0x00007fd4);
	} else {
		ram_wr32(fuc, 0x1373ec, 0x00020404);
	}

	if (mode == 0) {
		ram_mask(fuc, 0x10f808, 0x00080000, 0x00000000);
		ram_mask(fuc, 0x10f200, 0x00008000, 0x00008000);
		ram_wr32(fuc, 0x10f830, 0x41500010);
		ram_mask(fuc, 0x10f830, 0x01000000, 0x00000000);
		ram_mask(fuc, 0x132100, 0x00000100, 0x00000100);
		ram_wr32(fuc, 0x10f050, 0xff000090);
		ram_wr32(fuc, 0x1373ec, 0x00020f0f);
		ram_wr32(fuc, 0x1373f0, 0x00000003);
		ram_wr32(fuc, 0x137310, 0x81201616);
		ram_wr32(fuc, 0x132100, 0x00000001);
// 0x00020039 // 0x000000ba
		ram_wr32(fuc, 0x10f830, 0x00300017);
		ram_wr32(fuc, 0x1373f0, 0x00000001);
		ram_wr32(fuc, 0x10f824, 0x00007e77);
		ram_wr32(fuc, 0x132000, 0x18030001);
		ram_wr32(fuc, 0x10f090, 0x4000007e);
		ram_nsec(fuc, 2000);
		ram_wr32(fuc, 0x10f314, 0x00000001);
		ram_wr32(fuc, 0x10f210, 0x80000000);
		ram_wr32(fuc, 0x10f338, 0x00300220);
		ram_wr32(fuc, 0x10f300, 0x0000011d);
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f290, 0x02060505);
		ram_wr32(fuc, 0x10f294, 0x34208288);
		ram_wr32(fuc, 0x10f298, 0x44050411);
		ram_wr32(fuc, 0x10f29c, 0x0000114c);
		ram_wr32(fuc, 0x10f2a0, 0x42e10069);
		ram_wr32(fuc, 0x10f614, 0x40044f77);
		ram_wr32(fuc, 0x10f610, 0x40044f77);
		ram_wr32(fuc, 0x10f344, 0x00600009);
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f348, 0x00700008);
		ram_wr32(fuc, 0x61c140, 0x19240000);
		ram_wr32(fuc, 0x10f830, 0x00300017);
		nvc0_ram_train(fuc, 0x80021001);
		nvc0_ram_train(fuc, 0x80081001);
		ram_wr32(fuc, 0x10f340, 0x00500004);
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f830, 0x01300017);
		ram_wr32(fuc, 0x10f830, 0x00300017);
// 0x00030020 // 0x00000000 // 0x00000000
// 0x00020034 // 0x0000000b
		ram_wr32(fuc, 0x100b0c, 0x00080028);
		ram_wr32(fuc, 0x611200, 0x00003330);
	} else {
		ram_wr32(fuc, 0x10f800, 0x00001800);
		ram_wr32(fuc, 0x13d8f4, 0x00000000);
		ram_wr32(fuc, 0x1373ec, 0x00020404);
		ram_wr32(fuc, 0x1373f0, 0x00000003);
		ram_wr32(fuc, 0x10f830, 0x40700010);
		ram_wr32(fuc, 0x10f830, 0x40500010);
		ram_wr32(fuc, 0x13d8f4, 0x00000000);
		ram_wr32(fuc, 0x1373f8, 0x00000000);
		ram_wr32(fuc, 0x132100, 0x00000101);
		ram_wr32(fuc, 0x137310, 0x89201616);
		ram_wr32(fuc, 0x10f050, 0xff000090);
		ram_wr32(fuc, 0x1373ec, 0x00030404);
		ram_wr32(fuc, 0x1373f0, 0x00000002);
	// 0x00020039 // 0x00000011
		ram_wr32(fuc, 0x132100, 0x00000001);
		ram_wr32(fuc, 0x1373f8, 0x00002000);
		ram_nsec(fuc, 2000);
		ram_wr32(fuc, 0x10f808, 0x7aaa0050);
		ram_wr32(fuc, 0x10f830, 0x00500010);
		ram_wr32(fuc, 0x10f200, 0x00ce1000);
		ram_wr32(fuc, 0x10f090, 0x4000007e);
		ram_nsec(fuc, 2000);
		ram_wr32(fuc, 0x10f314, 0x00000001);
		ram_wr32(fuc, 0x10f210, 0x80000000);
		ram_wr32(fuc, 0x10f338, 0x00300200);
		ram_wr32(fuc, 0x10f300, 0x0000084d);
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f290, 0x0b343825);
		ram_wr32(fuc, 0x10f294, 0x3483028e);
		ram_wr32(fuc, 0x10f298, 0x440c0600);
		ram_wr32(fuc, 0x10f29c, 0x0000214c);
		ram_wr32(fuc, 0x10f2a0, 0x42e20069);
		ram_wr32(fuc, 0x10f200, 0x00ce0000);
		ram_wr32(fuc, 0x10f614, 0x60044e77);
		ram_wr32(fuc, 0x10f610, 0x60044e77);
		ram_wr32(fuc, 0x10f340, 0x00500000);
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f344, 0x00600228);
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f348, 0x00700000);
		ram_wr32(fuc, 0x13d8f4, 0x00000000);
		ram_wr32(fuc, 0x61c140, 0x09a40000);

		nvc0_ram_train(fuc, 0x800e1008);

		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f800, 0x00001804);
	// 0x00030020 // 0x00000000 // 0x00000000
	// 0x00020034 // 0x0000000b
		ram_wr32(fuc, 0x13d8f4, 0x00000000);
		ram_wr32(fuc, 0x100b0c, 0x00080028);
		ram_wr32(fuc, 0x611200, 0x00003330);
		ram_nsec(fuc, 100000);
		ram_wr32(fuc, 0x10f9b0, 0x05313f41);
		ram_wr32(fuc, 0x10f9b4, 0x00002f50);

		nvc0_ram_train(fuc, 0x010c1001);
	}

	ram_mask(fuc, 0x10f200, 0x00000800, 0x00000800);
// 0x00020016 // 0x00000000

	if (mode == 0)
		ram_mask(fuc, 0x132000, 0x00000001, 0x00000000);
	return 0;
}

static int
nvc0_ram_prog(struct nouveau_fb *pfb)
{
	struct nouveau_device *device = nv_device(pfb);
	struct nvc0_ram *ram = (void *)pfb->ram;
	struct nvc0_ramfuc *fuc = &ram->fuc;
	ram_exec(fuc, nouveau_boolopt(device->cfgopt, "NvMemExec", false));
	return 0;
}

static void
nvc0_ram_tidy(struct nouveau_fb *pfb)
{
	struct nvc0_ram *ram = (void *)pfb->ram;
	struct nvc0_ramfuc *fuc = &ram->fuc;
	ram_exec(fuc, false);
}

extern const u8 nvc0_pte_storage_type_map[256];

void
nvc0_ram_put(struct nouveau_fb *pfb, struct nouveau_mem **pmem)
{
	struct nouveau_ltcg *ltcg = nouveau_ltcg(pfb);
	struct nouveau_mem *mem = *pmem;

	*pmem = NULL;
	if (unlikely(mem == NULL))
		return;

	mutex_lock(&pfb->base.mutex);
	if (mem->tag)
		ltcg->tags_free(ltcg, &mem->tag);
	__nv50_ram_put(pfb, mem);
	mutex_unlock(&pfb->base.mutex);

	kfree(mem);
}

int
nvc0_ram_get(struct nouveau_fb *pfb, u64 size, u32 align, u32 ncmin,
	     u32 memtype, struct nouveau_mem **pmem)
{
	struct nouveau_mm *mm = &pfb->vram;
	struct nouveau_mm_node *r;
	struct nouveau_mem *mem;
	int type = (memtype & 0x0ff);
	int back = (memtype & 0x800);
	const bool comp = nvc0_pte_storage_type_map[type] != type;
	int ret;

	size  >>= 12;
	align >>= 12;
	ncmin >>= 12;
	if (!ncmin)
		ncmin = size;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	INIT_LIST_HEAD(&mem->regions);
	mem->size = size;

	mutex_lock(&pfb->base.mutex);
	if (comp) {
		struct nouveau_ltcg *ltcg = nouveau_ltcg(pfb);

		/* compression only works with lpages */
		if (align == (1 << (17 - 12))) {
			int n = size >> 5;
			ltcg->tags_alloc(ltcg, n, &mem->tag);
		}

		if (unlikely(!mem->tag))
			type = nvc0_pte_storage_type_map[type];
	}
	mem->memtype = type;

	do {
		if (back)
			ret = nouveau_mm_tail(mm, 1, size, ncmin, align, &r);
		else
			ret = nouveau_mm_head(mm, 1, size, ncmin, align, &r);
		if (ret) {
			mutex_unlock(&pfb->base.mutex);
			pfb->ram->put(pfb, &mem);
			return ret;
		}

		list_add_tail(&r->rl_entry, &mem->regions);
		size -= r->length;
	} while (size);
	mutex_unlock(&pfb->base.mutex);

	r = list_first_entry(&mem->regions, struct nouveau_mm_node, rl_entry);
	mem->offset = (u64)r->offset << 12;
	*pmem = mem;
	return 0;
}

int
nvc0_ram_create_(struct nouveau_object *parent, struct nouveau_object *engine,
		 struct nouveau_oclass *oclass, int size, void **pobject)
{
	struct nouveau_fb *pfb = nouveau_fb(parent);
	struct nouveau_bios *bios = nouveau_bios(pfb);
	struct nouveau_ram *ram;
	const u32 rsvd_head = ( 256 * 1024) >> 12; /* vga memory */
	const u32 rsvd_tail = (1024 * 1024) >> 12; /* vbios etc */
	u32 parts = nv_rd32(pfb, 0x022438);
	u32 pmask = nv_rd32(pfb, 0x022554);
	u32 bsize = nv_rd32(pfb, 0x10f20c);
	u32 offset, length;
	bool uniform = true;
	int ret, part;

	ret = nouveau_ram_create_(parent, engine, oclass, size, pobject);
	ram = *pobject;
	if (ret)
		return ret;

	nv_debug(pfb, "0x100800: 0x%08x\n", nv_rd32(pfb, 0x100800));
	nv_debug(pfb, "parts 0x%08x mask 0x%08x\n", parts, pmask);

	ram->type = nouveau_fb_bios_memtype(bios);
	ram->ranks = (nv_rd32(pfb, 0x10f200) & 0x00000004) ? 2 : 1;

	/* read amount of vram attached to each memory controller */
	for (part = 0; part < parts; part++) {
		if (!(pmask & (1 << part))) {
			u32 psize = nv_rd32(pfb, 0x11020c + (part * 0x1000));
			if (psize != bsize) {
				if (psize < bsize)
					bsize = psize;
				uniform = false;
			}

			nv_debug(pfb, "%d: mem_amount 0x%08x\n", part, psize);
			ram->size += (u64)psize << 20;
		}
	}

	/* if all controllers have the same amount attached, there's no holes */
	if (uniform) {
		offset = rsvd_head;
		length = (ram->size >> 12) - rsvd_head - rsvd_tail;
		ret = nouveau_mm_init(&pfb->vram, offset, length, 1);
	} else {
		/* otherwise, address lowest common amount from 0GiB */
		ret = nouveau_mm_init(&pfb->vram, rsvd_head,
				      (bsize << 8) * parts, 1);
		if (ret)
			return ret;

		/* and the rest starting from (8GiB + common_size) */
		offset = (0x0200000000ULL >> 12) + (bsize << 8);
		length = (ram->size >> 12) - (bsize << 8) - rsvd_tail;

		ret = nouveau_mm_init(&pfb->vram, offset, length, 0);
		if (ret)
			nouveau_mm_fini(&pfb->vram);
	}

	if (ret)
		return ret;

	ram->get = nvc0_ram_get;
	ram->put = nvc0_ram_put;
	return 0;
}

static int
nvc0_ram_init(struct nouveau_object *object)
{
	struct nouveau_fb *pfb = (void *)object->parent;
	struct nvc0_ram   *ram = (void *)object;
	int ret, i;

	ret = nouveau_ram_init(&ram->base);
	if (ret)
		return ret;

	/* prepare for ddr link training, and load training patterns */
	switch (ram->base.type) {
	case NV_MEM_TYPE_GDDR5: {
		static const u8  train0[] = {
			0x00, 0xff, 0x55, 0xaa, 0x33, 0xcc,
			0x00, 0xff, 0xff, 0x00, 0xff, 0x00,
		};
		static const u32 train1[] = {
			0x00000000, 0xffffffff,
			0x55555555, 0xaaaaaaaa,
			0x33333333, 0xcccccccc,
			0xf0f0f0f0, 0x0f0f0f0f,
			0x00ff00ff, 0xff00ff00,
			0x0000ffff, 0xffff0000,
		};

		for (i = 0; i < 0x30; i++) {
			nv_wr32(pfb, 0x10f968, 0x00000000 | (i << 8));
			nv_wr32(pfb, 0x10f96c, 0x00000000 | (i << 8));
			nv_wr32(pfb, 0x10f920, 0x00000100 | train0[i % 12]);
			nv_wr32(pfb, 0x10f924, 0x00000100 | train0[i % 12]);
			nv_wr32(pfb, 0x10f918,              train1[i % 12]);
			nv_wr32(pfb, 0x10f91c,              train1[i % 12]);
			nv_wr32(pfb, 0x10f920, 0x00000000 | train0[i % 12]);
			nv_wr32(pfb, 0x10f924, 0x00000000 | train0[i % 12]);
			nv_wr32(pfb, 0x10f918,              train1[i % 12]);
			nv_wr32(pfb, 0x10f91c,              train1[i % 12]);
		}
	}	break;
	default:
		break;
	}

	return 0;
}

static int
nvc0_ram_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	      struct nouveau_oclass *oclass, void *data, u32 size,
	      struct nouveau_object **pobject)
{
	struct nouveau_bios *bios = nouveau_bios(parent);
	struct nvc0_ram *ram;
	int ret;

	ret = nvc0_ram_create(parent, engine, oclass, &ram);
	*pobject = nv_object(ram);
	if (ret)
		return ret;

	ret = nvbios_pll_parse(bios, 0x0c, &ram->refpll);
	if (ret) {
		nv_error(ram, "mclk refpll data not found\n");
		return ret;
	}

	ret = nvbios_pll_parse(bios, 0x04, &ram->mempll);
	if (ret) {
		nv_error(ram, "mclk pll data not found\n");
		return ret;
	}

	switch (ram->base.type) {
	case NV_MEM_TYPE_GDDR5:
		ram->base.calc = nvc0_ram_calc;
		ram->base.prog = nvc0_ram_prog;
		ram->base.tidy = nvc0_ram_tidy;
		break;
	default:
		nv_warn(ram, "reclocking of this ram type unsupported\n");
		return 0;
	}

	ram->fuc.r_0x10fe20 = ramfuc_reg(0x10fe20);
	ram->fuc.r_0x10fe24 = ramfuc_reg(0x10fe24);
	ram->fuc.r_0x137320 = ramfuc_reg(0x137320);
	ram->fuc.r_0x137330 = ramfuc_reg(0x137330);

	ram->fuc.r_0x132000 = ramfuc_reg(0x132000);
	ram->fuc.r_0x132004 = ramfuc_reg(0x132004);
	ram->fuc.r_0x132100 = ramfuc_reg(0x132100);

	ram->fuc.r_0x137390 = ramfuc_reg(0x137390);

	ram->fuc.r_0x10f290 = ramfuc_reg(0x10f290);
	ram->fuc.r_0x10f294 = ramfuc_reg(0x10f294);
	ram->fuc.r_0x10f298 = ramfuc_reg(0x10f298);
	ram->fuc.r_0x10f29c = ramfuc_reg(0x10f29c);
	ram->fuc.r_0x10f2a0 = ramfuc_reg(0x10f2a0);

	ram->fuc.r_0x10f300 = ramfuc_reg(0x10f300);
	ram->fuc.r_0x10f338 = ramfuc_reg(0x10f338);
	ram->fuc.r_0x10f340 = ramfuc_reg(0x10f340);
	ram->fuc.r_0x10f344 = ramfuc_reg(0x10f344);
	ram->fuc.r_0x10f348 = ramfuc_reg(0x10f348);

	ram->fuc.r_0x10f910 = ramfuc_reg(0x10f910);
	ram->fuc.r_0x10f914 = ramfuc_reg(0x10f914);

	ram->fuc.r_0x100b0c = ramfuc_reg(0x100b0c);
	ram->fuc.r_0x10f050 = ramfuc_reg(0x10f050);
	ram->fuc.r_0x10f090 = ramfuc_reg(0x10f090);
	ram->fuc.r_0x10f200 = ramfuc_reg(0x10f200);
	ram->fuc.r_0x10f210 = ramfuc_reg(0x10f210);
	ram->fuc.r_0x10f310 = ramfuc_reg(0x10f310);
	ram->fuc.r_0x10f314 = ramfuc_reg(0x10f314);
	ram->fuc.r_0x10f610 = ramfuc_reg(0x10f610);
	ram->fuc.r_0x10f614 = ramfuc_reg(0x10f614);
	ram->fuc.r_0x10f800 = ramfuc_reg(0x10f800);
	ram->fuc.r_0x10f808 = ramfuc_reg(0x10f808);
	ram->fuc.r_0x10f824 = ramfuc_reg(0x10f824);
	ram->fuc.r_0x10f830 = ramfuc_reg(0x10f830);
	ram->fuc.r_0x10f988 = ramfuc_reg(0x10f988);
	ram->fuc.r_0x10f98c = ramfuc_reg(0x10f98c);
	ram->fuc.r_0x10f990 = ramfuc_reg(0x10f990);
	ram->fuc.r_0x10f998 = ramfuc_reg(0x10f998);
	ram->fuc.r_0x10f9b0 = ramfuc_reg(0x10f9b0);
	ram->fuc.r_0x10f9b4 = ramfuc_reg(0x10f9b4);
	ram->fuc.r_0x10fb04 = ramfuc_reg(0x10fb04);
	ram->fuc.r_0x10fb08 = ramfuc_reg(0x10fb08);
	ram->fuc.r_0x137310 = ramfuc_reg(0x137300);
	ram->fuc.r_0x137310 = ramfuc_reg(0x137310);
	ram->fuc.r_0x137360 = ramfuc_reg(0x137360);
	ram->fuc.r_0x1373ec = ramfuc_reg(0x1373ec);
	ram->fuc.r_0x1373f0 = ramfuc_reg(0x1373f0);
	ram->fuc.r_0x1373f8 = ramfuc_reg(0x1373f8);

	ram->fuc.r_0x61c140 = ramfuc_reg(0x61c140);
	ram->fuc.r_0x611200 = ramfuc_reg(0x611200);

	ram->fuc.r_0x13d8f4 = ramfuc_reg(0x13d8f4);
	return 0;
}

struct nouveau_oclass
nvc0_ram_oclass = {
	.handle = 0,
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_ram_ctor,
		.dtor = _nouveau_ram_dtor,
		.init = nvc0_ram_init,
		.fini = _nouveau_ram_fini,
	}
};
