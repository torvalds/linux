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
 * 	    Roy Spliet <rspliet@eclipso.eu>
 */

#include <subdev/bios.h>
#include <subdev/bios/bit.h>
#include <subdev/bios/pll.h>
#include <subdev/bios/rammap.h>
#include <subdev/bios/M0205.h>
#include <subdev/bios/timing.h>

#include <subdev/clock/nva3.h>
#include <subdev/clock/pll.h>

#include <subdev/timer.h>

#include <engine/fifo.h>

#include <core/option.h>

#include "ramfuc.h"

#include "nv50.h"

struct nva3_ramfuc {
	struct ramfuc base;
	struct ramfuc_reg r_0x001610;
	struct ramfuc_reg r_0x001700;
	struct ramfuc_reg r_0x004000;
	struct ramfuc_reg r_0x004004;
	struct ramfuc_reg r_0x004018;
	struct ramfuc_reg r_0x004128;
	struct ramfuc_reg r_0x004168;
	struct ramfuc_reg r_0x100080;
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
	struct ramfuc_reg r_0x100720;
	struct ramfuc_reg r_0x100760;
	struct ramfuc_reg r_0x1007a0;
	struct ramfuc_reg r_0x1007e0;
	struct ramfuc_reg r_0x10f804;
	struct ramfuc_reg r_0x1110e0;
	struct ramfuc_reg r_0x111100;
	struct ramfuc_reg r_0x111104;
	struct ramfuc_reg r_0x1111e0;
	struct ramfuc_reg r_0x111400;
	struct ramfuc_reg r_0x611200;
	struct ramfuc_reg r_mr[4];
};

struct nva3_ltrain {
	enum {
		NVA3_TRAIN_UNKNOWN,
		NVA3_TRAIN_UNSUPPORTED,
		NVA3_TRAIN_ONCE,
		NVA3_TRAIN_EXEC,
		NVA3_TRAIN_DONE
	} state;
	u32 r_100720;
	u32 r_1111e0;
	u32 r_111400;
	struct nouveau_mem *mem;
};

struct nva3_ram {
	struct nouveau_ram base;
	struct nva3_ramfuc fuc;
	struct nva3_ltrain ltrain;
};

void
nva3_link_train_calc(u32 *vals, struct nva3_ltrain *train)
{
	int i, lo, hi;
	u8 median[8], bins[4] = {0, 0, 0, 0}, bin = 0, qty = 0;

	for (i = 0; i < 8; i++) {
		for (lo = 0; lo < 0x40; lo++) {
			if (!(vals[lo] & 0x80000000))
				continue;
			if (vals[lo] & (0x101 << i))
				break;
		}

		if (lo == 0x40)
			return;

		for (hi = lo + 1; hi < 0x40; hi++) {
			if (!(vals[lo] & 0x80000000))
				continue;
			if (!(vals[hi] & (0x101 << i))) {
				hi--;
				break;
			}
		}

		median[i] = ((hi - lo) >> 1) + lo;
		bins[(median[i] & 0xf0) >> 4]++;
		median[i] += 0x30;
	}

	/* Find the best value for 0x1111e0 */
	for (i = 0; i < 4; i++) {
		if (bins[i] > qty) {
			bin = i + 3;
			qty = bins[i];
		}
	}

	train->r_100720 = 0;
	for (i = 0; i < 8; i++) {
		median[i] = max(median[i], (u8) (bin << 4));
		median[i] = min(median[i], (u8) ((bin << 4) | 0xf));

		train->r_100720 |= ((median[i] & 0x0f) << (i << 2));
	}

	train->r_1111e0 = 0x02000000 | (bin * 0x101);
	train->r_111400 = 0x0;
}

/*
 * Link training for (at least) DDR3
 */
int
nva3_link_train(struct nouveau_fb *pfb)
{
	struct nouveau_bios *bios = nouveau_bios(pfb);
	struct nva3_ram *ram = (void *)pfb->ram;
	struct nouveau_clock *clk = nouveau_clock(pfb);
	struct nva3_ltrain *train = &ram->ltrain;
	struct nouveau_device *device = nv_device(pfb);
	struct nva3_ramfuc *fuc = &ram->fuc;
	u32 *result, r1700;
	int ret, i;
	struct nvbios_M0205T M0205T = { 0 };
	u8 ver, hdr, cnt, len, snr, ssz;
	unsigned int clk_current;
	unsigned long flags;
	unsigned long *f = &flags;

	if (nouveau_boolopt(device->cfgopt, "NvMemExec", true) != true)
		return -ENOSYS;

	/* XXX: Multiple partitions? */
	result = kmalloc(64 * sizeof(u32), GFP_KERNEL);
	if (!result)
		return -ENOMEM;

	train->state = NVA3_TRAIN_EXEC;

	/* Clock speeds for training and back */
	nvbios_M0205Tp(bios, &ver, &hdr, &cnt, &len, &snr, &ssz, &M0205T);
	if (M0205T.freq == 0)
		return -ENOENT;

	clk_current = clk->read(clk, nv_clk_src_mem);

	ret = nva3_clock_pre(clk, f);
	if (ret)
		goto out;

	/* First: clock up/down */
	ret = ram->base.calc(pfb, (u32) M0205T.freq * 1000);
	if (ret)
		goto out;

	/* Do this *after* calc, eliminates write in script */
	nv_wr32(pfb, 0x111400, 0x00000000);
	/* XXX: Magic writes that improve train reliability? */
	nv_mask(pfb, 0x100674, 0x0000ffff, 0x00000000);
	nv_mask(pfb, 0x1005e4, 0x0000ffff, 0x00000000);
	nv_mask(pfb, 0x100b0c, 0x000000ff, 0x00000000);
	nv_wr32(pfb, 0x100c04, 0x00000400);

	/* Now the training script */
	r1700 = ram_rd32(fuc, 0x001700);

	ram_mask(fuc, 0x100200, 0x00000800, 0x00000000);
	ram_wr32(fuc, 0x611200, 0x3300);
	ram_wait_vblank(fuc);
	ram_wait(fuc, 0x611200, 0x00000003, 0x00000000, 500000);
	ram_mask(fuc, 0x001610, 0x00000083, 0x00000003);
	ram_mask(fuc, 0x100080, 0x00000020, 0x00000000);
	ram_mask(fuc, 0x10f804, 0x80000000, 0x00000000);
	ram_wr32(fuc, 0x001700, 0x00000000);

	ram_train(fuc);

	/* Reset */
	ram_mask(fuc, 0x10f804, 0x80000000, 0x80000000);
	ram_wr32(fuc, 0x10053c, 0x0);
	ram_wr32(fuc, 0x100720, train->r_100720);
	ram_wr32(fuc, 0x1111e0, train->r_1111e0);
	ram_wr32(fuc, 0x111400, train->r_111400);
	ram_nuke(fuc, 0x100080);
	ram_mask(fuc, 0x100080, 0x00000020, 0x00000020);
	ram_nsec(fuc, 1000);

	ram_wr32(fuc, 0x001700, r1700);
	ram_mask(fuc, 0x001610, 0x00000083, 0x00000080);
	ram_wr32(fuc, 0x611200, 0x3330);
	ram_mask(fuc, 0x100200, 0x00000800, 0x00000800);

	ram_exec(fuc, true);

	ram->base.calc(pfb, clk_current);
	ram_exec(fuc, true);

	/* Post-processing, avoids flicker */
	nv_mask(pfb, 0x616308, 0x10, 0x10);
	nv_mask(pfb, 0x616b08, 0x10, 0x10);

	nva3_clock_post(clk, f);

	ram_train_result(pfb, result, 64);
	for (i = 0; i < 64; i++)
		nv_debug(pfb, "Train: %08x", result[i]);
	nva3_link_train_calc(result, train);

	nv_debug(pfb, "Train: %08x %08x %08x", train->r_100720,
			train->r_1111e0, train->r_111400);

	kfree(result);

	train->state = NVA3_TRAIN_DONE;

	return ret;

out:
	if(ret == -EBUSY)
		f = NULL;

	train->state = NVA3_TRAIN_UNSUPPORTED;

	nva3_clock_post(clk, f);
	return ret;
}

int
nva3_link_train_init(struct nouveau_fb *pfb)
{
	static const u32 pattern[16] = {
		0xaaaaaaaa, 0xcccccccc, 0xdddddddd, 0xeeeeeeee,
		0x00000000, 0x11111111, 0x44444444, 0xdddddddd,
		0x33333333, 0x55555555, 0x77777777, 0x66666666,
		0x99999999, 0x88888888, 0xeeeeeeee, 0xbbbbbbbb,
	};
	struct nouveau_bios *bios = nouveau_bios(pfb);
	struct nva3_ram *ram = (void *)pfb->ram;
	struct nva3_ltrain *train = &ram->ltrain;
	struct nouveau_mem *mem;
	struct nvbios_M0205E M0205E;
	u8 ver, hdr, cnt, len;
	u32 r001700;
	int ret, i = 0;

	train->state = NVA3_TRAIN_UNSUPPORTED;

	/* We support type "5"
	 * XXX: training pattern table appears to be unused for this routine */
	if (!nvbios_M0205Ep(bios, i, &ver, &hdr, &cnt, &len, &M0205E))
		return -ENOENT;

	if (M0205E.type != 5)
		return 0;

	train->state = NVA3_TRAIN_ONCE;

	ret = pfb->ram->get(pfb, 0x8000, 0x10000, 0, 0x800, &ram->ltrain.mem);
	if (ret)
		return ret;

	mem = ram->ltrain.mem;

	nv_wr32(pfb, 0x100538, 0x10000000 | (mem->offset >> 16));
	nv_wr32(pfb, 0x1005a8, 0x0000ffff);
	nv_mask(pfb, 0x10f800, 0x00000001, 0x00000001);

	for (i = 0; i < 0x30; i++) {
		nv_wr32(pfb, 0x10f8c0, (i << 8) | i);
		nv_wr32(pfb, 0x10f900, pattern[i % 16]);
	}

	for (i = 0; i < 0x30; i++) {
		nv_wr32(pfb, 0x10f8e0, (i << 8) | i);
		nv_wr32(pfb, 0x10f920, pattern[i % 16]);
	}

	/* And upload the pattern */
	r001700 = nv_rd32(pfb, 0x1700);
	nv_wr32(pfb, 0x1700, mem->offset >> 16);
	for (i = 0; i < 16; i++)
		nv_wr32(pfb, 0x700000 + (i << 2), pattern[i]);
	for (i = 0; i < 16; i++)
		nv_wr32(pfb, 0x700100 + (i << 2), pattern[i]);
	nv_wr32(pfb, 0x1700, r001700);

	train->r_100720 = nv_rd32(pfb, 0x100720);
	train->r_1111e0 = nv_rd32(pfb, 0x1111e0);
	train->r_111400 = nv_rd32(pfb, 0x111400);

	return 0;
}

void
nva3_link_train_fini(struct nouveau_fb *pfb)
{
	struct nva3_ram *ram = (void *)pfb->ram;

	if (ram->ltrain.mem)
		pfb->ram->put(pfb, &ram->ltrain.mem);
}

static int
nva3_ram_calc(struct nouveau_fb *pfb, u32 freq)
{
	struct nouveau_bios *bios = nouveau_bios(pfb);
	struct nva3_ram *ram = (void *)pfb->ram;
	struct nva3_ramfuc *fuc = &ram->fuc;
	struct nva3_clock_info mclk;
	struct nouveau_ram_data *next;
	u8  ver, hdr, cnt, len, strap;
	u32 data;
	u32 r004018, r100760, ctrl;
	u32 unk714, unk718, unk71c;
	int ret, i;

	next = &ram->base.target;
	next->freq = freq;
	ram->base.next = next;

	if (ram->ltrain.state == NVA3_TRAIN_ONCE)
		nva3_link_train(pfb);

	/* lookup memory config data relevant to the target frequency */
	i = 0;
	while ((data = nvbios_rammapEp(bios, i++, &ver, &hdr, &cnt, &len,
				      &next->bios))) {
		if (freq / 1000 >= next->bios.rammap_min &&
		    freq / 1000 <= next->bios.rammap_max)
			break;
	}

	if (!data || ver != 0x10 || hdr < 0x0e) {
		nv_error(pfb, "invalid/missing rammap entry\n");
		return -EINVAL;
	}

	/* locate specific data set for the attached memory */
	strap = nvbios_ramcfg_index(nv_subdev(pfb));
	if (strap >= cnt) {
		nv_error(pfb, "invalid ramcfg strap\n");
		return -EINVAL;
	}

	data = nvbios_rammapSp(bios, data, ver, hdr, cnt, len, strap,
			       &ver, &hdr, &next->bios);
	if (!data || ver != 0x10 || hdr < 0x0e) {
		nv_error(pfb, "invalid/missing ramcfg entry\n");
		return -EINVAL;
	}

	/* lookup memory timings, if bios says they're present */
	if (next->bios.ramcfg_timing != 0xff) {
		data = nvbios_timingEp(bios, next->bios.ramcfg_timing,
				       &ver, &hdr, &cnt, &len,
				       &next->bios);
		if (!data || ver != 0x10 || hdr < 0x19) {
			nv_error(pfb, "invalid/missing timing entry\n");
			return -EINVAL;
		}
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

	if (next->bios.ramcfg_10_02_10) {
		ram_mask(fuc, 0x111104, 0x00000600, 0x00000000);
	} else {
		ram_mask(fuc, 0x111100, 0x40000000, 0x40000000);
		ram_mask(fuc, 0x111104, 0x00000180, 0x00000000);
	}

	if (!next->bios.rammap_10_04_02)
		ram_mask(fuc, 0x100200, 0x00000800, 0x00000000);
	ram_wr32(fuc, 0x611200, 0x00003300);
	if (!next->bios.ramcfg_10_02_10)
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

	if (next->bios.rammap_10_04_08) {
		ram_wr32(fuc, 0x1005a0, next->bios.ramcfg_10_06 << 16 |
					next->bios.ramcfg_10_05 << 8 |
					next->bios.ramcfg_10_05);
		ram_wr32(fuc, 0x1005a4, next->bios.ramcfg_10_08 << 8 |
					next->bios.ramcfg_10_07);
		ram_wr32(fuc, 0x10f804, next->bios.ramcfg_10_09_f0 << 20 |
					next->bios.ramcfg_10_03_0f << 16 |
					next->bios.ramcfg_10_09_0f |
					0x80000000);
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

	ram_mask(fuc, 0x100200, 0x00001000, !next->bios.ramcfg_10_02_08 << 12);

	unk714 = ram_rd32(fuc, 0x100714) & ~0xf0000010;
	unk718 = ram_rd32(fuc, 0x100718) & ~0x00000100;
	unk71c = ram_rd32(fuc, 0x10071c) & ~0x00000100;
	if (next->bios.ramcfg_10_02_20)
		unk714 |= 0xf0000000;
	if (!next->bios.ramcfg_10_02_04)
		unk714 |= 0x00000010;
	ram_wr32(fuc, 0x100714, unk714);

	if (next->bios.ramcfg_10_02_01)
		unk71c |= 0x00000100;
	ram_wr32(fuc, 0x10071c, unk71c);

	if (next->bios.ramcfg_10_02_02)
		unk718 |= 0x00000100;
	ram_wr32(fuc, 0x100718, unk718);

	if (next->bios.ramcfg_10_02_10)
		ram_wr32(fuc, 0x111100, 0x48000000); /*XXX*/

	ram_mask(fuc, mr[0], 0x100, 0x100);
	ram_nsec(fuc, 1000);
	ram_mask(fuc, mr[0], 0x100, 0x000);
	ram_nsec(fuc, 1000);

	ram_nsec(fuc, 2000);
	ram_nsec(fuc, 12000);

	ram_wr32(fuc, 0x611200, 0x00003330);
	if (next->bios.rammap_10_04_02)
		ram_mask(fuc, 0x100200, 0x00000800, 0x00000800);
	if (next->bios.ramcfg_10_02_10) {
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
	int ret;

	ret = nouveau_ram_init(&ram->base);
	if (ret)
		return ret;

	nva3_link_train_init(pfb);

	return 0;
}

static int
nva3_ram_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_fb *pfb = (void *)object->parent;

	if (!suspend)
		nva3_link_train_fini(pfb);

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

	ram->fuc.r_0x001610 = ramfuc_reg(0x001610);
	ram->fuc.r_0x001700 = ramfuc_reg(0x001700);
	ram->fuc.r_0x004000 = ramfuc_reg(0x004000);
	ram->fuc.r_0x004004 = ramfuc_reg(0x004004);
	ram->fuc.r_0x004018 = ramfuc_reg(0x004018);
	ram->fuc.r_0x004128 = ramfuc_reg(0x004128);
	ram->fuc.r_0x004168 = ramfuc_reg(0x004168);
	ram->fuc.r_0x100080 = ramfuc_reg(0x100080);
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
	ram->fuc.r_0x100720 = ramfuc_reg(0x100720);
	ram->fuc.r_0x100760 = ramfuc_stride(0x100760, 4, ram->base.part_mask);
	ram->fuc.r_0x1007a0 = ramfuc_stride(0x1007a0, 4, ram->base.part_mask);
	ram->fuc.r_0x1007e0 = ramfuc_stride(0x1007e0, 4, ram->base.part_mask);
	ram->fuc.r_0x10f804 = ramfuc_reg(0x10f804);
	ram->fuc.r_0x1110e0 = ramfuc_stride(0x1110e0, 4, ram->base.part_mask);
	ram->fuc.r_0x111100 = ramfuc_reg(0x111100);
	ram->fuc.r_0x111104 = ramfuc_reg(0x111104);
	ram->fuc.r_0x1111e0 = ramfuc_reg(0x1111e0);
	ram->fuc.r_0x111400 = ramfuc_reg(0x111400);
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
		.fini = nva3_ram_fini,
	},
};
