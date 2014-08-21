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

#include <subdev/clock/nva3.h>
#include <subdev/clock/pll.h>

#include <core/option.h>

#include "ramfuc.h"

#include "nv50.h"

struct nva3_ramfuc {
	struct ramfuc base;
	struct ramfuc_reg r_0x004000;
	struct ramfuc_reg r_0x004004;
	struct ramfuc_reg r_0x004018;
	struct ramfuc_reg r_0x004128;
	struct ramfuc_reg r_0x004168;
	struct ramfuc_reg r_0x100200;
	struct ramfuc_reg r_0x100210;
	struct ramfuc_reg r_0x100220[9];
	struct ramfuc_reg r_0x1002d0;
	struct ramfuc_reg r_0x1002d4;
	struct ramfuc_reg r_0x1002dc;
	struct ramfuc_reg r_0x10053c;
	struct ramfuc_reg r_0x1005a0;
	struct ramfuc_reg r_0x1005a4;
	struct ramfuc_reg r_0x100714;
	struct ramfuc_reg r_0x100718;
	struct ramfuc_reg r_0x10071c;
	struct ramfuc_reg r_0x100760;
	struct ramfuc_reg r_0x1007a0;
	struct ramfuc_reg r_0x1007e0;
	struct ramfuc_reg r_0x10f804;
	struct ramfuc_reg r_0x1110e0;
	struct ramfuc_reg r_0x111100;
	struct ramfuc_reg r_0x111104;
	struct ramfuc_reg r_0x611200;
	struct ramfuc_reg r_mr[4];
};

struct nva3_ram {
	struct nouveau_ram base;
	struct nva3_ramfuc fuc;
};

static int
nva3_ram_calc(struct nouveau_fb *pfb, u32 freq)
{
	struct nouveau_bios *bios = nouveau_bios(pfb);
	struct nva3_ram *ram = (void *)pfb->ram;
	struct nva3_ramfuc *fuc = &ram->fuc;
	struct nva3_clock_info mclk;
	u8  ver, cnt, len, strap;
	u32 data;
	struct {
		u32 data;
		u8  size;
	} rammap, ramcfg, timing;
	u32 r004018, r100760, ctrl;
	u32 unk714, unk718, unk71c;
	int ret;

	/* lookup memory config data relevant to the target frequency */
	rammap.data = nvbios_rammapEm(bios, freq / 1000, &ver, &rammap.size,
				     &cnt, &ramcfg.size);
	if (!rammap.data || ver != 0x10 || rammap.size < 0x0e) {
		nv_error(pfb, "invalid/missing rammap entry\n");
		return -EINVAL;
	}

	/* locate specific data set for the attached memory */
	strap = nvbios_ramcfg_index(nv_subdev(pfb));
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
		timing.data = nvbios_timingEe(bios, strap, &ver, &timing.size,
					     &cnt, &len);
		if (!timing.data || ver != 0x10 || timing.size < 0x19) {
			nv_error(pfb, "invalid/missing timing entry\n");
			return -EINVAL;
		}
	} else {
		timing.data = 0;
	}

	ret = nva3_pll_info(nouveau_clock(pfb), 0x12, 0x4000, freq, &mclk);
	if (ret < 0) {
		nv_error(pfb, "failed mclk calculation\n");
		return ret;
	}

	ret = ram_init(fuc, pfb);
	if (ret)
		return ret;

	/* XXX: where the fuck does 750MHz come from? */
	if (freq <= 750000) {
		r004018 = 0x10000000;
		r100760 = 0x22222222;
	} else {
		r004018 = 0x00000000;
		r100760 = 0x00000000;
	}

	ctrl = ram_rd32(fuc, 0x004000);
	if (ctrl & 0x00000008) {
		if (mclk.pll) {
			ram_mask(fuc, 0x004128, 0x00000101, 0x00000101);
			ram_wr32(fuc, 0x004004, mclk.pll);
			ram_wr32(fuc, 0x004000, (ctrl |= 0x00000001));
			ram_wr32(fuc, 0x004000, (ctrl &= 0xffffffef));
			ram_wait(fuc, 0x004000, 0x00020000, 0x00020000, 64000);
			ram_wr32(fuc, 0x004000, (ctrl |= 0x00000010));
			ram_wr32(fuc, 0x004018, 0x00005000 | r004018);
			ram_wr32(fuc, 0x004000, (ctrl |= 0x00000004));
		}
	} else {
		u32 ssel = 0x00000101;
		if (mclk.clk)
			ssel |= mclk.clk;
		else
			ssel |= 0x00080000; /* 324MHz, shouldn't matter... */
		ram_mask(fuc, 0x004168, 0x003f3141, ctrl);
	}

	if ( (nv_ro08(bios, ramcfg.data + 0x02) & 0x10)) {
		ram_mask(fuc, 0x111104, 0x00000600, 0x00000000);
	} else {
		ram_mask(fuc, 0x111100, 0x40000000, 0x40000000);
		ram_mask(fuc, 0x111104, 0x00000180, 0x00000000);
	}

	if (!(nv_ro08(bios, rammap.data + 0x04) & 0x02))
		ram_mask(fuc, 0x100200, 0x00000800, 0x00000000);
	ram_wr32(fuc, 0x611200, 0x00003300);
	if (!(nv_ro08(bios, ramcfg.data + 0x02) & 0x10))
		ram_wr32(fuc, 0x111100, 0x4c020000); /*XXX*/

	ram_wr32(fuc, 0x1002d4, 0x00000001);
	ram_wr32(fuc, 0x1002d0, 0x00000001);
	ram_wr32(fuc, 0x1002d0, 0x00000001);
	ram_wr32(fuc, 0x100210, 0x00000000);
	ram_wr32(fuc, 0x1002dc, 0x00000001);
	ram_nsec(fuc, 2000);

	ctrl = ram_rd32(fuc, 0x004000);
	if (!(ctrl & 0x00000008) && mclk.pll) {
		ram_wr32(fuc, 0x004000, (ctrl |=  0x00000008));
		ram_mask(fuc, 0x1110e0, 0x00088000, 0x00088000);
		ram_wr32(fuc, 0x004018, 0x00001000);
		ram_wr32(fuc, 0x004000, (ctrl &= ~0x00000001));
		ram_wr32(fuc, 0x004004, mclk.pll);
		ram_wr32(fuc, 0x004000, (ctrl |=  0x00000001));
		udelay(64);
		ram_wr32(fuc, 0x004018, 0x00005000 | r004018);
		udelay(20);
	} else
	if (!mclk.pll) {
		ram_mask(fuc, 0x004168, 0x003f3040, mclk.clk);
		ram_wr32(fuc, 0x004000, (ctrl |= 0x00000008));
		ram_mask(fuc, 0x1110e0, 0x00088000, 0x00088000);
		ram_wr32(fuc, 0x004018, 0x0000d000 | r004018);
	}

	if ( (nv_ro08(bios, rammap.data + 0x04) & 0x08)) {
		u32 unk5a0 = (nv_ro16(bios, ramcfg.data + 0x05) << 8) |
			      nv_ro08(bios, ramcfg.data + 0x05);
		u32 unk5a4 = (nv_ro16(bios, ramcfg.data + 0x07));
		u32 unk804 = (nv_ro08(bios, ramcfg.data + 0x09) & 0xf0) << 16 |
			     (nv_ro08(bios, ramcfg.data + 0x03) & 0x0f) << 16 |
			     (nv_ro08(bios, ramcfg.data + 0x09) & 0x0f) |
			     0x80000000;
		ram_wr32(fuc, 0x1005a0, unk5a0);
		ram_wr32(fuc, 0x1005a4, unk5a4);
		ram_wr32(fuc, 0x10f804, unk804);
		ram_mask(fuc, 0x10053c, 0x00001000, 0x00000000);
	} else {
		ram_mask(fuc, 0x10053c, 0x00001000, 0x00001000);
		ram_mask(fuc, 0x10f804, 0x80000000, 0x00000000);
		ram_mask(fuc, 0x100760, 0x22222222, r100760);
		ram_mask(fuc, 0x1007a0, 0x22222222, r100760);
		ram_mask(fuc, 0x1007e0, 0x22222222, r100760);
	}

	if (mclk.pll) {
		ram_mask(fuc, 0x1110e0, 0x00088000, 0x00011000);
		ram_wr32(fuc, 0x004000, (ctrl &= ~0x00000008));
	}

	/*XXX: LEAVE */
	ram_wr32(fuc, 0x1002dc, 0x00000000);
	ram_wr32(fuc, 0x1002d4, 0x00000001);
	ram_wr32(fuc, 0x100210, 0x80000000);
	ram_nsec(fuc, 1000);
	ram_nsec(fuc, 1000);

	ram_mask(fuc, mr[2], 0x00000000, 0x00000000);
	ram_nsec(fuc, 1000);
	ram_nuke(fuc, mr[0]);
	ram_mask(fuc, mr[0], 0x00000000, 0x00000000);
	ram_nsec(fuc, 1000);

	ram_mask(fuc, 0x100220[3], 0x00000000, 0x00000000);
	ram_mask(fuc, 0x100220[1], 0x00000000, 0x00000000);
	ram_mask(fuc, 0x100220[6], 0x00000000, 0x00000000);
	ram_mask(fuc, 0x100220[7], 0x00000000, 0x00000000);
	ram_mask(fuc, 0x100220[2], 0x00000000, 0x00000000);
	ram_mask(fuc, 0x100220[4], 0x00000000, 0x00000000);
	ram_mask(fuc, 0x100220[5], 0x00000000, 0x00000000);
	ram_mask(fuc, 0x100220[0], 0x00000000, 0x00000000);
	ram_mask(fuc, 0x100220[8], 0x00000000, 0x00000000);

	data = (nv_ro08(bios, ramcfg.data + 0x02) & 0x08) ? 0x00000000 : 0x00001000;
	ram_mask(fuc, 0x100200, 0x00001000, data);

	unk714 = ram_rd32(fuc, 0x100714) & ~0xf0000010;
	unk718 = ram_rd32(fuc, 0x100718) & ~0x00000100;
	unk71c = ram_rd32(fuc, 0x10071c) & ~0x00000100;
	if ( (nv_ro08(bios, ramcfg.data + 0x02) & 0x20))
		unk714 |= 0xf0000000;
	if (!(nv_ro08(bios, ramcfg.data + 0x02) & 0x04))
		unk714 |= 0x00000010;
	ram_wr32(fuc, 0x100714, unk714);

	if (nv_ro08(bios, ramcfg.data + 0x02) & 0x01)
		unk71c |= 0x00000100;
	ram_wr32(fuc, 0x10071c, unk71c);

	if (nv_ro08(bios, ramcfg.data + 0x02) & 0x02)
		unk718 |= 0x00000100;
	ram_wr32(fuc, 0x100718, unk718);

	if (nv_ro08(bios, ramcfg.data + 0x02) & 0x10)
		ram_wr32(fuc, 0x111100, 0x48000000); /*XXX*/

	ram_mask(fuc, mr[0], 0x100, 0x100);
	ram_nsec(fuc, 1000);
	ram_mask(fuc, mr[0], 0x100, 0x000);
	ram_nsec(fuc, 1000);

	ram_nsec(fuc, 2000);
	ram_nsec(fuc, 12000);

	ram_wr32(fuc, 0x611200, 0x00003330);
	if ( (nv_ro08(bios, rammap.data + 0x04) & 0x02))
		ram_mask(fuc, 0x100200, 0x00000800, 0x00000800);
	if ( (nv_ro08(bios, ramcfg.data + 0x02) & 0x10)) {
		ram_mask(fuc, 0x111104, 0x00000180, 0x00000180);
		ram_mask(fuc, 0x111100, 0x40000000, 0x00000000);
	} else {
		ram_mask(fuc, 0x111104, 0x00000600, 0x00000600);
	}

	if (mclk.pll) {
		ram_mask(fuc, 0x004168, 0x00000001, 0x00000000);
		ram_mask(fuc, 0x004168, 0x00000100, 0x00000000);
	} else {
		ram_mask(fuc, 0x004000, 0x00000001, 0x00000000);
		ram_mask(fuc, 0x004128, 0x00000001, 0x00000000);
		ram_mask(fuc, 0x004128, 0x00000100, 0x00000000);
	}

	return 0;
}

static int
nva3_ram_prog(struct nouveau_fb *pfb)
{
	struct nouveau_device *device = nv_device(pfb);
	struct nva3_ram *ram = (void *)pfb->ram;
	struct nva3_ramfuc *fuc = &ram->fuc;
	ram_exec(fuc, nouveau_boolopt(device->cfgopt, "NvMemExec", true));
	return 0;
}

static void
nva3_ram_tidy(struct nouveau_fb *pfb)
{
	struct nva3_ram *ram = (void *)pfb->ram;
	struct nva3_ramfuc *fuc = &ram->fuc;
	ram_exec(fuc, false);
}

static int
nva3_ram_init(struct nouveau_object *object)
{
	struct nouveau_fb *pfb = (void *)object->parent;
	struct nva3_ram   *ram = (void *)object;
	int ret, i;

	ret = nouveau_ram_init(&ram->base);
	if (ret)
		return ret;

	/* prepare for ddr link training, and load training patterns */
	switch (ram->base.type) {
	case NV_MEM_TYPE_DDR3: {
		if (nv_device(pfb)->chipset == 0xa8) {
			static const u32 pattern[16] = {
				0xaaaaaaaa, 0xcccccccc, 0xdddddddd, 0xeeeeeeee,
				0x00000000, 0x11111111, 0x44444444, 0xdddddddd,
				0x33333333, 0x55555555, 0x77777777, 0x66666666,
				0x99999999, 0x88888888, 0xeeeeeeee, 0xbbbbbbbb,
			};

			nv_wr32(pfb, 0x100538, 0x10001ff6); /*XXX*/
			nv_wr32(pfb, 0x1005a8, 0x0000ffff);
			nv_mask(pfb, 0x10f800, 0x00000001, 0x00000001);
			for (i = 0; i < 0x30; i++) {
				nv_wr32(pfb, 0x10f8c0, (i << 8) | i);
				nv_wr32(pfb, 0x10f8e0, (i << 8) | i);
				nv_wr32(pfb, 0x10f900, pattern[i % 16]);
				nv_wr32(pfb, 0x10f920, pattern[i % 16]);
			}
		}
	}
		break;
	default:
		break;
	}

	return 0;
}

static int
nva3_ram_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	      struct nouveau_oclass *oclass, void *data, u32 datasize,
	      struct nouveau_object **pobject)
{
	struct nva3_ram *ram;
	int ret, i;

	ret = nv50_ram_create(parent, engine, oclass, &ram);
	*pobject = nv_object(ram);
	if (ret)
		return ret;

	switch (ram->base.type) {
	case NV_MEM_TYPE_DDR3:
		ram->base.calc = nva3_ram_calc;
		ram->base.prog = nva3_ram_prog;
		ram->base.tidy = nva3_ram_tidy;
		break;
	default:
		nv_warn(ram, "reclocking of this ram type unsupported\n");
		return 0;
	}

	ram->fuc.r_0x004000 = ramfuc_reg(0x004000);
	ram->fuc.r_0x004004 = ramfuc_reg(0x004004);
	ram->fuc.r_0x004018 = ramfuc_reg(0x004018);
	ram->fuc.r_0x004128 = ramfuc_reg(0x004128);
	ram->fuc.r_0x004168 = ramfuc_reg(0x004168);
	ram->fuc.r_0x100200 = ramfuc_reg(0x100200);
	ram->fuc.r_0x100210 = ramfuc_reg(0x100210);
	for (i = 0; i < 9; i++)
		ram->fuc.r_0x100220[i] = ramfuc_reg(0x100220 + (i * 4));
	ram->fuc.r_0x1002d0 = ramfuc_reg(0x1002d0);
	ram->fuc.r_0x1002d4 = ramfuc_reg(0x1002d4);
	ram->fuc.r_0x1002dc = ramfuc_reg(0x1002dc);
	ram->fuc.r_0x10053c = ramfuc_reg(0x10053c);
	ram->fuc.r_0x1005a0 = ramfuc_reg(0x1005a0);
	ram->fuc.r_0x1005a4 = ramfuc_reg(0x1005a4);
	ram->fuc.r_0x100714 = ramfuc_reg(0x100714);
	ram->fuc.r_0x100718 = ramfuc_reg(0x100718);
	ram->fuc.r_0x10071c = ramfuc_reg(0x10071c);
	ram->fuc.r_0x100760 = ramfuc_reg(0x100760);
	ram->fuc.r_0x1007a0 = ramfuc_reg(0x1007a0);
	ram->fuc.r_0x1007e0 = ramfuc_reg(0x1007e0);
	ram->fuc.r_0x10f804 = ramfuc_reg(0x10f804);
	ram->fuc.r_0x1110e0 = ramfuc_reg(0x1110e0);
	ram->fuc.r_0x111100 = ramfuc_reg(0x111100);
	ram->fuc.r_0x111104 = ramfuc_reg(0x111104);
	ram->fuc.r_0x611200 = ramfuc_reg(0x611200);

	if (ram->base.ranks > 1) {
		ram->fuc.r_mr[0] = ramfuc_reg2(0x1002c0, 0x1002c8);
		ram->fuc.r_mr[1] = ramfuc_reg2(0x1002c4, 0x1002cc);
		ram->fuc.r_mr[2] = ramfuc_reg2(0x1002e0, 0x1002e8);
		ram->fuc.r_mr[3] = ramfuc_reg2(0x1002e4, 0x1002ec);
	} else {
		ram->fuc.r_mr[0] = ramfuc_reg(0x1002c0);
		ram->fuc.r_mr[1] = ramfuc_reg(0x1002c4);
		ram->fuc.r_mr[2] = ramfuc_reg(0x1002e0);
		ram->fuc.r_mr[3] = ramfuc_reg(0x1002e4);
	}

	return 0;
}

struct nouveau_oclass
nva3_ram_oclass = {
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nva3_ram_ctor,
		.dtor = _nouveau_ram_dtor,
		.init = nva3_ram_init,
		.fini = _nouveau_ram_fini,
	},
};
