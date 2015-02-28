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

#include "ramfuc.h"
#include "nv50.h"

#include <core/device.h>
#include <core/option.h>
#include <subdev/bios.h>
#include <subdev/bios/M0205.h>
#include <subdev/bios/rammap.h>
#include <subdev/bios/timing.h>
#include <subdev/clk/gt215.h>
#include <subdev/gpio.h>

/* XXX: Remove when memx gains GPIO support */
extern int nv50_gpio_location(int line, u32 *reg, u32 *shift);

struct gt215_ramfuc {
	struct ramfuc base;
	struct ramfuc_reg r_0x001610;
	struct ramfuc_reg r_0x001700;
	struct ramfuc_reg r_0x002504;
	struct ramfuc_reg r_0x004000;
	struct ramfuc_reg r_0x004004;
	struct ramfuc_reg r_0x004018;
	struct ramfuc_reg r_0x004128;
	struct ramfuc_reg r_0x004168;
	struct ramfuc_reg r_0x100080;
	struct ramfuc_reg r_0x100200;
	struct ramfuc_reg r_0x100210;
	struct ramfuc_reg r_0x100220[9];
	struct ramfuc_reg r_0x100264;
	struct ramfuc_reg r_0x1002d0;
	struct ramfuc_reg r_0x1002d4;
	struct ramfuc_reg r_0x1002dc;
	struct ramfuc_reg r_0x10053c;
	struct ramfuc_reg r_0x1005a0;
	struct ramfuc_reg r_0x1005a4;
	struct ramfuc_reg r_0x100700;
	struct ramfuc_reg r_0x100714;
	struct ramfuc_reg r_0x100718;
	struct ramfuc_reg r_0x10071c;
	struct ramfuc_reg r_0x100720;
	struct ramfuc_reg r_0x100760;
	struct ramfuc_reg r_0x1007a0;
	struct ramfuc_reg r_0x1007e0;
	struct ramfuc_reg r_0x100da0;
	struct ramfuc_reg r_0x10f804;
	struct ramfuc_reg r_0x1110e0;
	struct ramfuc_reg r_0x111100;
	struct ramfuc_reg r_0x111104;
	struct ramfuc_reg r_0x1111e0;
	struct ramfuc_reg r_0x111400;
	struct ramfuc_reg r_0x611200;
	struct ramfuc_reg r_mr[4];
	struct ramfuc_reg r_gpioFBVREF;
};

struct gt215_ltrain {
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
	struct nvkm_mem *mem;
};

struct gt215_ram {
	struct nvkm_ram base;
	struct gt215_ramfuc fuc;
	struct gt215_ltrain ltrain;
};

void
gt215_link_train_calc(u32 *vals, struct gt215_ltrain *train)
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
gt215_link_train(struct nvkm_fb *pfb)
{
	struct nvkm_bios *bios = nvkm_bios(pfb);
	struct gt215_ram *ram = (void *)pfb->ram;
	struct nvkm_clk *clk = nvkm_clk(pfb);
	struct gt215_ltrain *train = &ram->ltrain;
	struct nvkm_device *device = nv_device(pfb);
	struct gt215_ramfuc *fuc = &ram->fuc;
	u32 *result, r1700;
	int ret, i;
	struct nvbios_M0205T M0205T = { 0 };
	u8 ver, hdr, cnt, len, snr, ssz;
	unsigned int clk_current;
	unsigned long flags;
	unsigned long *f = &flags;

	if (nvkm_boolopt(device->cfgopt, "NvMemExec", true) != true)
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

	ret = gt215_clk_pre(clk, f);
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

	gt215_clk_post(clk, f);

	ram_train_result(pfb, result, 64);
	for (i = 0; i < 64; i++)
		nv_debug(pfb, "Train: %08x", result[i]);
	gt215_link_train_calc(result, train);

	nv_debug(pfb, "Train: %08x %08x %08x", train->r_100720,
			train->r_1111e0, train->r_111400);

	kfree(result);

	train->state = NVA3_TRAIN_DONE;

	return ret;

out:
	if(ret == -EBUSY)
		f = NULL;

	train->state = NVA3_TRAIN_UNSUPPORTED;

	gt215_clk_post(clk, f);
	return ret;
}

int
gt215_link_train_init(struct nvkm_fb *pfb)
{
	static const u32 pattern[16] = {
		0xaaaaaaaa, 0xcccccccc, 0xdddddddd, 0xeeeeeeee,
		0x00000000, 0x11111111, 0x44444444, 0xdddddddd,
		0x33333333, 0x55555555, 0x77777777, 0x66666666,
		0x99999999, 0x88888888, 0xeeeeeeee, 0xbbbbbbbb,
	};
	struct nvkm_bios *bios = nvkm_bios(pfb);
	struct gt215_ram *ram = (void *)pfb->ram;
	struct gt215_ltrain *train = &ram->ltrain;
	struct nvkm_mem *mem;
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
gt215_link_train_fini(struct nvkm_fb *pfb)
{
	struct gt215_ram *ram = (void *)pfb->ram;

	if (ram->ltrain.mem)
		pfb->ram->put(pfb, &ram->ltrain.mem);
}

/*
 * RAM reclocking
 */
#define T(t) cfg->timing_10_##t
static int
gt215_ram_timing_calc(struct nvkm_fb *pfb, u32 *timing)
{
	struct gt215_ram *ram = (void *)pfb->ram;
	struct nvbios_ramcfg *cfg = &ram->base.target.bios;
	int tUNK_base, tUNK_40_0, prevCL;
	u32 cur2, cur3, cur7, cur8;

	cur2 = nv_rd32(pfb, 0x100228);
	cur3 = nv_rd32(pfb, 0x10022c);
	cur7 = nv_rd32(pfb, 0x10023c);
	cur8 = nv_rd32(pfb, 0x100240);


	switch ((!T(CWL)) * ram->base.type) {
	case NV_MEM_TYPE_DDR2:
		T(CWL) = T(CL) - 1;
		break;
	case NV_MEM_TYPE_GDDR3:
		T(CWL) = ((cur2 & 0xff000000) >> 24) + 1;
		break;
	}

	prevCL = (cur3 & 0x000000ff) + 1;
	tUNK_base = ((cur7 & 0x00ff0000) >> 16) - prevCL;

	timing[0] = (T(RP) << 24 | T(RAS) << 16 | T(RFC) << 8 | T(RC));
	timing[1] = (T(WR) + 1 + T(CWL)) << 24 |
		    max_t(u8,T(18), 1) << 16 |
		    (T(WTR) + 1 + T(CWL)) << 8 |
		    (5 + T(CL) - T(CWL));
	timing[2] = (T(CWL) - 1) << 24 |
		    (T(RRD) << 16) |
		    (T(RCDWR) << 8) |
		    T(RCDRD);
	timing[3] = (cur3 & 0x00ff0000) |
		    (0x30 + T(CL)) << 24 |
		    (0xb + T(CL)) << 8 |
		    (T(CL) - 1);
	timing[4] = T(20) << 24 |
		    T(21) << 16 |
		    T(13) << 8 |
		    T(13);
	timing[5] = T(RFC) << 24 |
		    max_t(u8,T(RCDRD), T(RCDWR)) << 16 |
		    max_t(u8, (T(CWL) + 6), (T(CL) + 2)) << 8 |
		    T(RP);
	timing[6] = (0x5a + T(CL)) << 16 |
		    max_t(u8, 1, (6 - T(CL) + T(CWL))) << 8 |
		    (0x50 + T(CL) - T(CWL));
	timing[7] = (cur7 & 0xff000000) |
		    ((tUNK_base + T(CL)) << 16) |
		    0x202;
	timing[8] = cur8 & 0xffffff00;

	switch (ram->base.type) {
	case NV_MEM_TYPE_DDR2:
	case NV_MEM_TYPE_GDDR3:
		tUNK_40_0 = prevCL - (cur8 & 0xff);
		if (tUNK_40_0 > 0)
			timing[8] |= T(CL);
		break;
	default:
		break;
	}

	nv_debug(pfb, "Entry: 220: %08x %08x %08x %08x\n",
			timing[0], timing[1], timing[2], timing[3]);
	nv_debug(pfb, "  230: %08x %08x %08x %08x\n",
			timing[4], timing[5], timing[6], timing[7]);
	nv_debug(pfb, "  240: %08x\n", timing[8]);
	return 0;
}
#undef T

static void
nvkm_sddr2_dll_reset(struct gt215_ramfuc *fuc)
{
	ram_mask(fuc, mr[0], 0x100, 0x100);
	ram_nsec(fuc, 1000);
	ram_mask(fuc, mr[0], 0x100, 0x000);
	ram_nsec(fuc, 1000);
}

static void
nvkm_sddr3_dll_disable(struct gt215_ramfuc *fuc, u32 *mr)
{
	u32 mr1_old = ram_rd32(fuc, mr[1]);

	if (!(mr1_old & 0x1)) {
		ram_wr32(fuc, 0x1002d4, 0x00000001);
		ram_wr32(fuc, mr[1], mr[1]);
		ram_nsec(fuc, 1000);
	}
}

static void
nvkm_gddr3_dll_disable(struct gt215_ramfuc *fuc, u32 *mr)
{
	u32 mr1_old = ram_rd32(fuc, mr[1]);

	if (!(mr1_old & 0x40)) {
		ram_wr32(fuc, mr[1], mr[1]);
		ram_nsec(fuc, 1000);
	}
}

static void
gt215_ram_lock_pll(struct gt215_ramfuc *fuc, struct gt215_clk_info *mclk)
{
	ram_wr32(fuc, 0x004004, mclk->pll);
	ram_mask(fuc, 0x004000, 0x00000001, 0x00000001);
	ram_mask(fuc, 0x004000, 0x00000010, 0x00000000);
	ram_wait(fuc, 0x004000, 0x00020000, 0x00020000, 64000);
	ram_mask(fuc, 0x004000, 0x00000010, 0x00000010);
}

static void
gt215_ram_fbvref(struct gt215_ramfuc *fuc, u32 val)
{
	struct nvkm_gpio *gpio = nvkm_gpio(fuc->base.pfb);
	struct dcb_gpio_func func;
	u32 reg, sh, gpio_val;
	int ret;

	if (gpio->get(gpio, 0, 0x2e, DCB_GPIO_UNUSED) != val) {
		ret = gpio->find(gpio, 0, 0x2e, DCB_GPIO_UNUSED, &func);
		if (ret)
			return;

		nv50_gpio_location(func.line, &reg, &sh);
		gpio_val = ram_rd32(fuc, gpioFBVREF);
		if (gpio_val & (8 << sh))
			val = !val;

		ram_mask(fuc, gpioFBVREF, (0x3 << sh), ((val | 0x2) << sh));
		ram_nsec(fuc, 20000);
	}
}

static int
gt215_ram_calc(struct nvkm_fb *pfb, u32 freq)
{
	struct nvkm_bios *bios = nvkm_bios(pfb);
	struct gt215_ram *ram = (void *)pfb->ram;
	struct gt215_ramfuc *fuc = &ram->fuc;
	struct gt215_ltrain *train = &ram->ltrain;
	struct gt215_clk_info mclk;
	struct nvkm_ram_data *next;
	u8  ver, hdr, cnt, len, strap;
	u32 data;
	u32 r004018, r100760, r100da0, r111100, ctrl;
	u32 unk714, unk718, unk71c;
	int ret, i;
	u32 timing[9];
	bool pll2pll;

	next = &ram->base.target;
	next->freq = freq;
	ram->base.next = next;

	if (ram->ltrain.state == NVA3_TRAIN_ONCE)
		gt215_link_train(pfb);

	/* lookup memory config data relevant to the target frequency */
	i = 0;
	data = nvbios_rammapEm(bios, freq / 1000, &ver, &hdr, &cnt, &len,
			       &next->bios);
	if (!data || ver != 0x10 || hdr < 0x05) {
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
	if (!data || ver != 0x10 || hdr < 0x09) {
		nv_error(pfb, "invalid/missing ramcfg entry\n");
		return -EINVAL;
	}

	/* lookup memory timings, if bios says they're present */
	if (next->bios.ramcfg_timing != 0xff) {
		data = nvbios_timingEp(bios, next->bios.ramcfg_timing,
				       &ver, &hdr, &cnt, &len,
				       &next->bios);
		if (!data || ver != 0x10 || hdr < 0x17) {
			nv_error(pfb, "invalid/missing timing entry\n");
			return -EINVAL;
		}
	}

	ret = gt215_pll_info(nvkm_clk(pfb), 0x12, 0x4000, freq, &mclk);
	if (ret < 0) {
		nv_error(pfb, "failed mclk calculation\n");
		return ret;
	}

	gt215_ram_timing_calc(pfb, timing);

	ret = ram_init(fuc, pfb);
	if (ret)
		return ret;

	/* Determine ram-specific MR values */
	ram->base.mr[0] = ram_rd32(fuc, mr[0]);
	ram->base.mr[1] = ram_rd32(fuc, mr[1]);
	ram->base.mr[2] = ram_rd32(fuc, mr[2]);

	switch (ram->base.type) {
	case NV_MEM_TYPE_DDR2:
		ret = nvkm_sddr2_calc(&ram->base);
		break;
	case NV_MEM_TYPE_DDR3:
		ret = nvkm_sddr3_calc(&ram->base);
		break;
	case NV_MEM_TYPE_GDDR3:
		ret = nvkm_gddr3_calc(&ram->base);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	if (ret)
		return ret;

	/* XXX: where the fuck does 750MHz come from? */
	if (freq <= 750000) {
		r004018 = 0x10000000;
		r100760 = 0x22222222;
		r100da0 = 0x00000010;
	} else {
		r004018 = 0x00000000;
		r100760 = 0x00000000;
		r100da0 = 0x00000000;
	}

	if (!next->bios.ramcfg_10_DLLoff)
		r004018 |= 0x00004000;

	/* pll2pll requires to switch to a safe clock first */
	ctrl = ram_rd32(fuc, 0x004000);
	pll2pll = (!(ctrl & 0x00000008)) && mclk.pll;

	/* Pre, NVIDIA does this outside the script */
	if (next->bios.ramcfg_10_02_10) {
		ram_mask(fuc, 0x111104, 0x00000600, 0x00000000);
	} else {
		ram_mask(fuc, 0x111100, 0x40000000, 0x40000000);
		ram_mask(fuc, 0x111104, 0x00000180, 0x00000000);
	}
	/* Always disable this bit during reclock */
	ram_mask(fuc, 0x100200, 0x00000800, 0x00000000);

	/* If switching from non-pll to pll, lock before disabling FB */
	if (mclk.pll && !pll2pll) {
		ram_mask(fuc, 0x004128, 0x003f3141, mclk.clk | 0x00000101);
		gt215_ram_lock_pll(fuc, &mclk);
	}

	/* Start with disabling some CRTCs and PFIFO? */
	ram_wait_vblank(fuc);
	ram_wr32(fuc, 0x611200, 0x3300);
	ram_mask(fuc, 0x002504, 0x1, 0x1);
	ram_nsec(fuc, 10000);
	ram_wait(fuc, 0x002504, 0x10, 0x10, 20000); /* XXX: or longer? */
	ram_block(fuc);
	ram_nsec(fuc, 2000);

	if (!next->bios.ramcfg_10_02_10) {
		if (ram->base.type == NV_MEM_TYPE_GDDR3)
			ram_mask(fuc, 0x111100, 0x04020000, 0x00020000);
		else
			ram_mask(fuc, 0x111100, 0x04020000, 0x04020000);
	}

	/* If we're disabling the DLL, do it now */
	switch (next->bios.ramcfg_10_DLLoff * ram->base.type) {
	case NV_MEM_TYPE_DDR3:
		nvkm_sddr3_dll_disable(fuc, ram->base.mr);
		break;
	case NV_MEM_TYPE_GDDR3:
		nvkm_gddr3_dll_disable(fuc, ram->base.mr);
		break;
	}

	if (fuc->r_gpioFBVREF.addr && next->bios.timing_10_ODT)
		gt215_ram_fbvref(fuc, 0);

	/* Brace RAM for impact */
	ram_wr32(fuc, 0x1002d4, 0x00000001);
	ram_wr32(fuc, 0x1002d0, 0x00000001);
	ram_wr32(fuc, 0x1002d0, 0x00000001);
	ram_wr32(fuc, 0x100210, 0x00000000);
	ram_wr32(fuc, 0x1002dc, 0x00000001);
	ram_nsec(fuc, 2000);

	if (nv_device(pfb)->chipset == 0xa3 && freq <= 500000)
		ram_mask(fuc, 0x100700, 0x00000006, 0x00000006);

	/* Fiddle with clocks */
	/* There's 4 scenario's
	 * pll->pll: first switch to a 324MHz clock, set up new PLL, switch
	 * clk->pll: Set up new PLL, switch
	 * pll->clk: Set up clock, switch
	 * clk->clk: Overwrite ctrl and other bits, switch */

	/* Switch to regular clock - 324MHz */
	if (pll2pll) {
		ram_mask(fuc, 0x004000, 0x00000004, 0x00000004);
		ram_mask(fuc, 0x004168, 0x003f3141, 0x00083101);
		ram_mask(fuc, 0x004000, 0x00000008, 0x00000008);
		ram_mask(fuc, 0x1110e0, 0x00088000, 0x00088000);
		ram_wr32(fuc, 0x004018, 0x00001000);
		gt215_ram_lock_pll(fuc, &mclk);
	}

	if (mclk.pll) {
		ram_mask(fuc, 0x004000, 0x00000105, 0x00000105);
		ram_wr32(fuc, 0x004018, 0x00001000 | r004018);
		ram_wr32(fuc, 0x100da0, r100da0);
	} else {
		ram_mask(fuc, 0x004168, 0x003f3141, mclk.clk | 0x00000101);
		ram_mask(fuc, 0x004000, 0x00000108, 0x00000008);
		ram_mask(fuc, 0x1110e0, 0x00088000, 0x00088000);
		ram_wr32(fuc, 0x004018, 0x00009000 | r004018);
		ram_wr32(fuc, 0x100da0, r100da0);
	}
	ram_nsec(fuc, 20000);

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
		if (train->state == NVA3_TRAIN_DONE) {
			ram_wr32(fuc, 0x100080, 0x1020);
			ram_mask(fuc, 0x111400, 0xffffffff, train->r_111400);
			ram_mask(fuc, 0x1111e0, 0xffffffff, train->r_1111e0);
			ram_mask(fuc, 0x100720, 0xffffffff, train->r_100720);
		}
		ram_mask(fuc, 0x10053c, 0x00001000, 0x00001000);
		ram_mask(fuc, 0x10f804, 0x80000000, 0x00000000);
		ram_mask(fuc, 0x100760, 0x22222222, r100760);
		ram_mask(fuc, 0x1007a0, 0x22222222, r100760);
		ram_mask(fuc, 0x1007e0, 0x22222222, r100760);
	}

	if (nv_device(pfb)->chipset == 0xa3 && freq > 500000) {
		ram_mask(fuc, 0x100700, 0x00000006, 0x00000000);
	}

	/* Final switch */
	if (mclk.pll) {
		ram_mask(fuc, 0x1110e0, 0x00088000, 0x00011000);
		ram_mask(fuc, 0x004000, 0x00000008, 0x00000000);
	}

	ram_wr32(fuc, 0x1002dc, 0x00000000);
	ram_wr32(fuc, 0x1002d4, 0x00000001);
	ram_wr32(fuc, 0x100210, 0x80000000);
	ram_nsec(fuc, 2000);

	/* Set RAM MR parameters and timings */
	for (i = 2; i >= 0; i--) {
		if (ram_rd32(fuc, mr[i]) != ram->base.mr[i]) {
			ram_wr32(fuc, mr[i], ram->base.mr[i]);
			ram_nsec(fuc, 1000);
		}
	}

	ram_wr32(fuc, 0x100220[3], timing[3]);
	ram_wr32(fuc, 0x100220[1], timing[1]);
	ram_wr32(fuc, 0x100220[6], timing[6]);
	ram_wr32(fuc, 0x100220[7], timing[7]);
	ram_wr32(fuc, 0x100220[2], timing[2]);
	ram_wr32(fuc, 0x100220[4], timing[4]);
	ram_wr32(fuc, 0x100220[5], timing[5]);
	ram_wr32(fuc, 0x100220[0], timing[0]);
	ram_wr32(fuc, 0x100220[8], timing[8]);

	/* Misc */
	ram_mask(fuc, 0x100200, 0x00001000, !next->bios.ramcfg_10_02_08 << 12);

	/* XXX: A lot of "chipset"/"ram type" specific stuff...? */
	unk714  = ram_rd32(fuc, 0x100714) & ~0xf0000130;
	unk718  = ram_rd32(fuc, 0x100718) & ~0x00000100;
	unk71c  = ram_rd32(fuc, 0x10071c) & ~0x00000100;
	r111100 = ram_rd32(fuc, 0x111100) & ~0x3a800000;

	if (next->bios.ramcfg_10_02_04) {
		switch (ram->base.type) {
		case NV_MEM_TYPE_DDR3:
			if (nv_device(pfb)->chipset != 0xa8)
				r111100 |= 0x00000004;
			/* no break */
		case NV_MEM_TYPE_DDR2:
			r111100 |= 0x08000000;
			break;
		default:
			break;
		}
	} else {
		switch (ram->base.type) {
		case NV_MEM_TYPE_DDR2:
			r111100 |= 0x1a800000;
			unk714  |= 0x00000010;
			break;
		case NV_MEM_TYPE_DDR3:
			if (nv_device(pfb)->chipset == 0xa8) {
				r111100 |=  0x08000000;
			} else {
				r111100 &= ~0x00000004;
				r111100 |=  0x12800000;
			}
			unk714  |= 0x00000010;
			break;
		case NV_MEM_TYPE_GDDR3:
			r111100 |= 0x30000000;
			unk714  |= 0x00000020;
			break;
		default:
			break;
		}
	}

	unk714 |= (next->bios.ramcfg_10_04_01) << 8;

	if (next->bios.ramcfg_10_02_20)
		unk714 |= 0xf0000000;
	if (next->bios.ramcfg_10_02_02)
		unk718 |= 0x00000100;
	if (next->bios.ramcfg_10_02_01)
		unk71c |= 0x00000100;
	if (next->bios.timing_10_24 != 0xff) {
		unk718 &= ~0xf0000000;
		unk718 |= next->bios.timing_10_24 << 28;
	}
	if (next->bios.ramcfg_10_02_10)
		r111100 &= ~0x04020000;

	ram_mask(fuc, 0x100714, 0xffffffff, unk714);
	ram_mask(fuc, 0x10071c, 0xffffffff, unk71c);
	ram_mask(fuc, 0x100718, 0xffffffff, unk718);
	ram_mask(fuc, 0x111100, 0xffffffff, r111100);

	if (fuc->r_gpioFBVREF.addr && !next->bios.timing_10_ODT)
		gt215_ram_fbvref(fuc, 1);

	/* Reset DLL */
	if (!next->bios.ramcfg_10_DLLoff)
		nvkm_sddr2_dll_reset(fuc);

	if (ram->base.type == NV_MEM_TYPE_GDDR3) {
		ram_nsec(fuc, 31000);
	} else {
		ram_nsec(fuc, 14000);
	}

	if (ram->base.type == NV_MEM_TYPE_DDR3) {
		ram_wr32(fuc, 0x100264, 0x1);
		ram_nsec(fuc, 2000);
	}

	ram_nuke(fuc, 0x100700);
	ram_mask(fuc, 0x100700, 0x01000000, 0x01000000);
	ram_mask(fuc, 0x100700, 0x01000000, 0x00000000);

	/* Re-enable FB */
	ram_unblock(fuc);
	ram_wr32(fuc, 0x611200, 0x3330);

	/* Post fiddlings */
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
gt215_ram_prog(struct nvkm_fb *pfb)
{
	struct nvkm_device *device = nv_device(pfb);
	struct gt215_ram *ram = (void *)pfb->ram;
	struct gt215_ramfuc *fuc = &ram->fuc;
	bool exec = nvkm_boolopt(device->cfgopt, "NvMemExec", true);

	if (exec) {
		nv_mask(pfb, 0x001534, 0x2, 0x2);

		ram_exec(fuc, true);

		/* Post-processing, avoids flicker */
		nv_mask(pfb, 0x002504, 0x1, 0x0);
		nv_mask(pfb, 0x001534, 0x2, 0x0);

		nv_mask(pfb, 0x616308, 0x10, 0x10);
		nv_mask(pfb, 0x616b08, 0x10, 0x10);
	} else {
		ram_exec(fuc, false);
	}
	return 0;
}

static void
gt215_ram_tidy(struct nvkm_fb *pfb)
{
	struct gt215_ram *ram = (void *)pfb->ram;
	struct gt215_ramfuc *fuc = &ram->fuc;
	ram_exec(fuc, false);
}

static int
gt215_ram_init(struct nvkm_object *object)
{
	struct nvkm_fb *pfb = (void *)object->parent;
	struct gt215_ram   *ram = (void *)object;
	int ret;

	ret = nvkm_ram_init(&ram->base);
	if (ret)
		return ret;

	gt215_link_train_init(pfb);
	return 0;
}

static int
gt215_ram_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_fb *pfb = (void *)object->parent;

	if (!suspend)
		gt215_link_train_fini(pfb);

	return 0;
}

static int
gt215_ram_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 datasize,
	       struct nvkm_object **pobject)
{
	struct nvkm_fb *pfb = nvkm_fb(parent);
	struct nvkm_gpio *gpio = nvkm_gpio(pfb);
	struct dcb_gpio_func func;
	struct gt215_ram *ram;
	int ret, i;
	u32 reg, shift;

	ret = nv50_ram_create(parent, engine, oclass, &ram);
	*pobject = nv_object(ram);
	if (ret)
		return ret;

	switch (ram->base.type) {
	case NV_MEM_TYPE_DDR2:
	case NV_MEM_TYPE_DDR3:
	case NV_MEM_TYPE_GDDR3:
		ram->base.calc = gt215_ram_calc;
		ram->base.prog = gt215_ram_prog;
		ram->base.tidy = gt215_ram_tidy;
		break;
	default:
		nv_warn(ram, "reclocking of this ram type unsupported\n");
		return 0;
	}

	ram->fuc.r_0x001610 = ramfuc_reg(0x001610);
	ram->fuc.r_0x001700 = ramfuc_reg(0x001700);
	ram->fuc.r_0x002504 = ramfuc_reg(0x002504);
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
	ram->fuc.r_0x100264 = ramfuc_reg(0x100264);
	ram->fuc.r_0x1002d0 = ramfuc_reg(0x1002d0);
	ram->fuc.r_0x1002d4 = ramfuc_reg(0x1002d4);
	ram->fuc.r_0x1002dc = ramfuc_reg(0x1002dc);
	ram->fuc.r_0x10053c = ramfuc_reg(0x10053c);
	ram->fuc.r_0x1005a0 = ramfuc_reg(0x1005a0);
	ram->fuc.r_0x1005a4 = ramfuc_reg(0x1005a4);
	ram->fuc.r_0x100700 = ramfuc_reg(0x100700);
	ram->fuc.r_0x100714 = ramfuc_reg(0x100714);
	ram->fuc.r_0x100718 = ramfuc_reg(0x100718);
	ram->fuc.r_0x10071c = ramfuc_reg(0x10071c);
	ram->fuc.r_0x100720 = ramfuc_reg(0x100720);
	ram->fuc.r_0x100760 = ramfuc_stride(0x100760, 4, ram->base.part_mask);
	ram->fuc.r_0x1007a0 = ramfuc_stride(0x1007a0, 4, ram->base.part_mask);
	ram->fuc.r_0x1007e0 = ramfuc_stride(0x1007e0, 4, ram->base.part_mask);
	ram->fuc.r_0x100da0 = ramfuc_stride(0x100da0, 4, ram->base.part_mask);
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

	ret = gpio->find(gpio, 0, 0x2e, DCB_GPIO_UNUSED, &func);
	if (ret == 0) {
		nv50_gpio_location(func.line, &reg, &shift);
		ram->fuc.r_gpioFBVREF = ramfuc_reg(reg);
	}

	return 0;
}

struct nvkm_oclass
gt215_ram_oclass = {
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gt215_ram_ctor,
		.dtor = _nvkm_ram_dtor,
		.init = gt215_ram_init,
		.fini = gt215_ram_fini,
	},
};
