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
#define mcp77_clk(p) container_of((p), struct mcp77_clk, base)
#include "gt215.h"
#include "pll.h"

#include <subdev/bios.h>
#include <subdev/bios/pll.h>
#include <subdev/timer.h>

struct mcp77_clk {
	struct nvkm_clk base;
	enum nv_clk_src csrc, ssrc, vsrc;
	u32 cctrl, sctrl;
	u32 ccoef, scoef;
	u32 cpost, spost;
	u32 vdiv;
};

static u32
read_div(struct mcp77_clk *clk)
{
	struct nvkm_device *device = clk->base.subdev.device;
	return nvkm_rd32(device, 0x004600);
}

static u32
read_pll(struct mcp77_clk *clk, u32 base)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 ctrl = nvkm_rd32(device, base + 0);
	u32 coef = nvkm_rd32(device, base + 4);
	u32 ref = nvkm_clk_read(&clk->base, nv_clk_src_href);
	u32 post_div = 0;
	u32 clock = 0;
	int N1, M1;

	switch (base){
	case 0x4020:
		post_div = 1 << ((nvkm_rd32(device, 0x4070) & 0x000f0000) >> 16);
		break;
	case 0x4028:
		post_div = (nvkm_rd32(device, 0x4040) & 0x000f0000) >> 16;
		break;
	default:
		break;
	}

	N1 = (coef & 0x0000ff00) >> 8;
	M1 = (coef & 0x000000ff);
	if ((ctrl & 0x80000000) && M1) {
		clock = ref * N1 / M1;
		clock = clock / post_div;
	}

	return clock;
}

static int
mcp77_clk_read(struct nvkm_clk *base, enum nv_clk_src src)
{
	struct mcp77_clk *clk = mcp77_clk(base);
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mast = nvkm_rd32(device, 0x00c054);
	u32 P = 0;

	switch (src) {
	case nv_clk_src_crystal:
		return device->crystal;
	case nv_clk_src_href:
		return 100000; /* PCIE reference clock */
	case nv_clk_src_hclkm4:
		return nvkm_clk_read(&clk->base, nv_clk_src_href) * 4;
	case nv_clk_src_hclkm2d3:
		return nvkm_clk_read(&clk->base, nv_clk_src_href) * 2 / 3;
	case nv_clk_src_host:
		switch (mast & 0x000c0000) {
		case 0x00000000: return nvkm_clk_read(&clk->base, nv_clk_src_hclkm2d3);
		case 0x00040000: break;
		case 0x00080000: return nvkm_clk_read(&clk->base, nv_clk_src_hclkm4);
		case 0x000c0000: return nvkm_clk_read(&clk->base, nv_clk_src_cclk);
		}
		break;
	case nv_clk_src_core:
		P = (nvkm_rd32(device, 0x004028) & 0x00070000) >> 16;

		switch (mast & 0x00000003) {
		case 0x00000000: return nvkm_clk_read(&clk->base, nv_clk_src_crystal) >> P;
		case 0x00000001: return 0;
		case 0x00000002: return nvkm_clk_read(&clk->base, nv_clk_src_hclkm4) >> P;
		case 0x00000003: return read_pll(clk, 0x004028) >> P;
		}
		break;
	case nv_clk_src_cclk:
		if ((mast & 0x03000000) != 0x03000000)
			return nvkm_clk_read(&clk->base, nv_clk_src_core);

		if ((mast & 0x00000200) == 0x00000000)
			return nvkm_clk_read(&clk->base, nv_clk_src_core);

		switch (mast & 0x00000c00) {
		case 0x00000000: return nvkm_clk_read(&clk->base, nv_clk_src_href);
		case 0x00000400: return nvkm_clk_read(&clk->base, nv_clk_src_hclkm4);
		case 0x00000800: return nvkm_clk_read(&clk->base, nv_clk_src_hclkm2d3);
		default: return 0;
		}
	case nv_clk_src_shader:
		P = (nvkm_rd32(device, 0x004020) & 0x00070000) >> 16;
		switch (mast & 0x00000030) {
		case 0x00000000:
			if (mast & 0x00000040)
				return nvkm_clk_read(&clk->base, nv_clk_src_href) >> P;
			return nvkm_clk_read(&clk->base, nv_clk_src_crystal) >> P;
		case 0x00000010: break;
		case 0x00000020: return read_pll(clk, 0x004028) >> P;
		case 0x00000030: return read_pll(clk, 0x004020) >> P;
		}
		break;
	case nv_clk_src_mem:
		return 0;
		break;
	case nv_clk_src_vdec:
		P = (read_div(clk) & 0x00000700) >> 8;

		switch (mast & 0x00400000) {
		case 0x00400000:
			return nvkm_clk_read(&clk->base, nv_clk_src_core) >> P;
			break;
		default:
			return 500000 >> P;
			break;
		}
		break;
	default:
		break;
	}

	nvkm_debug(subdev, "unknown clock source %d %08x\n", src, mast);
	return 0;
}

static u32
calc_pll(struct mcp77_clk *clk, u32 reg,
	 u32 clock, int *N, int *M, int *P)
{
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvbios_pll pll;
	int ret;

	ret = nvbios_pll_parse(subdev->device->bios, reg, &pll);
	if (ret)
		return 0;

	pll.vco2.max_freq = 0;
	pll.refclk = nvkm_clk_read(&clk->base, nv_clk_src_href);
	if (!pll.refclk)
		return 0;

	return nv04_pll_calc(subdev, &pll, clock, N, M, NULL, NULL, P);
}

static inline u32
calc_P(u32 src, u32 target, int *div)
{
	u32 clk0 = src, clk1 = src;
	for (*div = 0; *div <= 7; (*div)++) {
		if (clk0 <= target) {
			clk1 = clk0 << (*div ? 1 : 0);
			break;
		}
		clk0 >>= 1;
	}

	if (target - clk0 <= clk1 - target)
		return clk0;
	(*div)--;
	return clk1;
}

static int
mcp77_clk_calc(struct nvkm_clk *base, struct nvkm_cstate *cstate)
{
	struct mcp77_clk *clk = mcp77_clk(base);
	const int shader = cstate->domain[nv_clk_src_shader];
	const int core = cstate->domain[nv_clk_src_core];
	const int vdec = cstate->domain[nv_clk_src_vdec];
	struct nvkm_subdev *subdev = &clk->base.subdev;
	u32 out = 0, clock = 0;
	int N, M, P1, P2 = 0;
	int divs = 0;

	/* cclk: find suitable source, disable PLL if we can */
	if (core < nvkm_clk_read(&clk->base, nv_clk_src_hclkm4))
		out = calc_P(nvkm_clk_read(&clk->base, nv_clk_src_hclkm4), core, &divs);

	/* Calculate clock * 2, so shader clock can use it too */
	clock = calc_pll(clk, 0x4028, (core << 1), &N, &M, &P1);

	if (abs(core - out) <= abs(core - (clock >> 1))) {
		clk->csrc = nv_clk_src_hclkm4;
		clk->cctrl = divs << 16;
	} else {
		/* NVCTRL is actually used _after_ NVPOST, and after what we
		 * call NVPLL. To make matters worse, NVPOST is an integer
		 * divider instead of a right-shift number. */
		if(P1 > 2) {
			P2 = P1 - 2;
			P1 = 2;
		}

		clk->csrc = nv_clk_src_core;
		clk->ccoef = (N << 8) | M;

		clk->cctrl = (P2 + 1) << 16;
		clk->cpost = (1 << P1) << 16;
	}

	/* sclk: nvpll + divisor, href or spll */
	out = 0;
	if (shader == nvkm_clk_read(&clk->base, nv_clk_src_href)) {
		clk->ssrc = nv_clk_src_href;
	} else {
		clock = calc_pll(clk, 0x4020, shader, &N, &M, &P1);
		if (clk->csrc == nv_clk_src_core)
			out = calc_P((core << 1), shader, &divs);

		if (abs(shader - out) <=
		    abs(shader - clock) &&
		   (divs + P2) <= 7) {
			clk->ssrc = nv_clk_src_core;
			clk->sctrl = (divs + P2) << 16;
		} else {
			clk->ssrc = nv_clk_src_shader;
			clk->scoef = (N << 8) | M;
			clk->sctrl = P1 << 16;
		}
	}

	/* vclk */
	out = calc_P(core, vdec, &divs);
	clock = calc_P(500000, vdec, &P1);
	if(abs(vdec - out) <= abs(vdec - clock)) {
		clk->vsrc = nv_clk_src_cclk;
		clk->vdiv = divs << 16;
	} else {
		clk->vsrc = nv_clk_src_vdec;
		clk->vdiv = P1 << 16;
	}

	/* Print strategy! */
	nvkm_debug(subdev, "nvpll: %08x %08x %08x\n",
		   clk->ccoef, clk->cpost, clk->cctrl);
	nvkm_debug(subdev, " spll: %08x %08x %08x\n",
		   clk->scoef, clk->spost, clk->sctrl);
	nvkm_debug(subdev, " vdiv: %08x\n", clk->vdiv);
	if (clk->csrc == nv_clk_src_hclkm4)
		nvkm_debug(subdev, "core: hrefm4\n");
	else
		nvkm_debug(subdev, "core: nvpll\n");

	if (clk->ssrc == nv_clk_src_hclkm4)
		nvkm_debug(subdev, "shader: hrefm4\n");
	else if (clk->ssrc == nv_clk_src_core)
		nvkm_debug(subdev, "shader: nvpll\n");
	else
		nvkm_debug(subdev, "shader: spll\n");

	if (clk->vsrc == nv_clk_src_hclkm4)
		nvkm_debug(subdev, "vdec: 500MHz\n");
	else
		nvkm_debug(subdev, "vdec: core\n");

	return 0;
}

static int
mcp77_clk_prog(struct nvkm_clk *base)
{
	struct mcp77_clk *clk = mcp77_clk(base);
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;
	u32 pllmask = 0, mast;
	unsigned long flags;
	unsigned long *f = &flags;
	int ret = 0;

	ret = gt215_clk_pre(&clk->base, f);
	if (ret)
		goto out;

	/* First switch to safe clocks: href */
	mast = nvkm_mask(device, 0xc054, 0x03400e70, 0x03400640);
	mast &= ~0x00400e73;
	mast |= 0x03000000;

	switch (clk->csrc) {
	case nv_clk_src_hclkm4:
		nvkm_mask(device, 0x4028, 0x00070000, clk->cctrl);
		mast |= 0x00000002;
		break;
	case nv_clk_src_core:
		nvkm_wr32(device, 0x402c, clk->ccoef);
		nvkm_wr32(device, 0x4028, 0x80000000 | clk->cctrl);
		nvkm_wr32(device, 0x4040, clk->cpost);
		pllmask |= (0x3 << 8);
		mast |= 0x00000003;
		break;
	default:
		nvkm_warn(subdev, "Reclocking failed: unknown core clock\n");
		goto resume;
	}

	switch (clk->ssrc) {
	case nv_clk_src_href:
		nvkm_mask(device, 0x4020, 0x00070000, 0x00000000);
		/* mast |= 0x00000000; */
		break;
	case nv_clk_src_core:
		nvkm_mask(device, 0x4020, 0x00070000, clk->sctrl);
		mast |= 0x00000020;
		break;
	case nv_clk_src_shader:
		nvkm_wr32(device, 0x4024, clk->scoef);
		nvkm_wr32(device, 0x4020, 0x80000000 | clk->sctrl);
		nvkm_wr32(device, 0x4070, clk->spost);
		pllmask |= (0x3 << 12);
		mast |= 0x00000030;
		break;
	default:
		nvkm_warn(subdev, "Reclocking failed: unknown sclk clock\n");
		goto resume;
	}

	if (nvkm_msec(device, 2000,
		u32 tmp = nvkm_rd32(device, 0x004080) & pllmask;
		if (tmp == pllmask)
			break;
	) < 0)
		goto resume;

	switch (clk->vsrc) {
	case nv_clk_src_cclk:
		mast |= 0x00400000;
		/* fall through */
	default:
		nvkm_wr32(device, 0x4600, clk->vdiv);
	}

	nvkm_wr32(device, 0xc054, mast);

resume:
	/* Disable some PLLs and dividers when unused */
	if (clk->csrc != nv_clk_src_core) {
		nvkm_wr32(device, 0x4040, 0x00000000);
		nvkm_mask(device, 0x4028, 0x80000000, 0x00000000);
	}

	if (clk->ssrc != nv_clk_src_shader) {
		nvkm_wr32(device, 0x4070, 0x00000000);
		nvkm_mask(device, 0x4020, 0x80000000, 0x00000000);
	}

out:
	if (ret == -EBUSY)
		f = NULL;

	gt215_clk_post(&clk->base, f);
	return ret;
}

static void
mcp77_clk_tidy(struct nvkm_clk *base)
{
}

static const struct nvkm_clk_func
mcp77_clk = {
	.read = mcp77_clk_read,
	.calc = mcp77_clk_calc,
	.prog = mcp77_clk_prog,
	.tidy = mcp77_clk_tidy,
	.domains = {
		{ nv_clk_src_crystal, 0xff },
		{ nv_clk_src_href   , 0xff },
		{ nv_clk_src_core   , 0xff, 0, "core", 1000 },
		{ nv_clk_src_shader , 0xff, 0, "shader", 1000 },
		{ nv_clk_src_vdec   , 0xff, 0, "vdec", 1000 },
		{ nv_clk_src_max }
	}
};

int
mcp77_clk_new(struct nvkm_device *device, int index, struct nvkm_clk **pclk)
{
	struct mcp77_clk *clk;

	if (!(clk = kzalloc(sizeof(*clk), GFP_KERNEL)))
		return -ENOMEM;
	*pclk = &clk->base;

	return nvkm_clk_ctor(&mcp77_clk, device, index, true, &clk->base);
}
