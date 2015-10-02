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
 *          Roy Spliet
 */
#define gt215_clk(p) container_of((p), struct gt215_clk, base)
#include "gt215.h"
#include "pll.h"

#include <engine/fifo.h>
#include <subdev/bios.h>
#include <subdev/bios/pll.h>
#include <subdev/timer.h>

struct gt215_clk {
	struct nvkm_clk base;
	struct gt215_clk_info eng[nv_clk_src_max];
};

static u32 read_clk(struct gt215_clk *, int, bool);
static u32 read_pll(struct gt215_clk *, int, u32);

static u32
read_vco(struct gt215_clk *clk, int idx)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 sctl = nvkm_rd32(device, 0x4120 + (idx * 4));

	switch (sctl & 0x00000030) {
	case 0x00000000:
		return device->crystal;
	case 0x00000020:
		return read_pll(clk, 0x41, 0x00e820);
	case 0x00000030:
		return read_pll(clk, 0x42, 0x00e8a0);
	default:
		return 0;
	}
}

static u32
read_clk(struct gt215_clk *clk, int idx, bool ignore_en)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 sctl, sdiv, sclk;

	/* refclk for the 0xe8xx plls is a fixed frequency */
	if (idx >= 0x40) {
		if (device->chipset == 0xaf) {
			/* no joke.. seriously.. sigh.. */
			return nvkm_rd32(device, 0x00471c) * 1000;
		}

		return device->crystal;
	}

	sctl = nvkm_rd32(device, 0x4120 + (idx * 4));
	if (!ignore_en && !(sctl & 0x00000100))
		return 0;

	/* out_alt */
	if (sctl & 0x00000400)
		return 108000;

	/* vco_out */
	switch (sctl & 0x00003000) {
	case 0x00000000:
		if (!(sctl & 0x00000200))
			return device->crystal;
		return 0;
	case 0x00002000:
		if (sctl & 0x00000040)
			return 108000;
		return 100000;
	case 0x00003000:
		/* vco_enable */
		if (!(sctl & 0x00000001))
			return 0;

		sclk = read_vco(clk, idx);
		sdiv = ((sctl & 0x003f0000) >> 16) + 2;
		return (sclk * 2) / sdiv;
	default:
		return 0;
	}
}

static u32
read_pll(struct gt215_clk *clk, int idx, u32 pll)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 ctrl = nvkm_rd32(device, pll + 0);
	u32 sclk = 0, P = 1, N = 1, M = 1;

	if (!(ctrl & 0x00000008)) {
		if (ctrl & 0x00000001) {
			u32 coef = nvkm_rd32(device, pll + 4);
			M = (coef & 0x000000ff) >> 0;
			N = (coef & 0x0000ff00) >> 8;
			P = (coef & 0x003f0000) >> 16;

			/* no post-divider on these..
			 * XXX: it looks more like two post-"dividers" that
			 * cross each other out in the default RPLL config */
			if ((pll & 0x00ff00) == 0x00e800)
				P = 1;

			sclk = read_clk(clk, 0x00 + idx, false);
		}
	} else {
		sclk = read_clk(clk, 0x10 + idx, false);
	}

	if (M * P)
		return sclk * N / (M * P);

	return 0;
}

static int
gt215_clk_read(struct nvkm_clk *base, enum nv_clk_src src)
{
	struct gt215_clk *clk = gt215_clk(base);
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;
	u32 hsrc;

	switch (src) {
	case nv_clk_src_crystal:
		return device->crystal;
	case nv_clk_src_core:
	case nv_clk_src_core_intm:
		return read_pll(clk, 0x00, 0x4200);
	case nv_clk_src_shader:
		return read_pll(clk, 0x01, 0x4220);
	case nv_clk_src_mem:
		return read_pll(clk, 0x02, 0x4000);
	case nv_clk_src_disp:
		return read_clk(clk, 0x20, false);
	case nv_clk_src_vdec:
		return read_clk(clk, 0x21, false);
	case nv_clk_src_daemon:
		return read_clk(clk, 0x25, false);
	case nv_clk_src_host:
		hsrc = (nvkm_rd32(device, 0xc040) & 0x30000000) >> 28;
		switch (hsrc) {
		case 0:
			return read_clk(clk, 0x1d, false);
		case 2:
		case 3:
			return 277000;
		default:
			nvkm_error(subdev, "unknown HOST clock source %d\n", hsrc);
			return -EINVAL;
		}
	default:
		nvkm_error(subdev, "invalid clock source %d\n", src);
		return -EINVAL;
	}

	return 0;
}

int
gt215_clk_info(struct nvkm_clk *base, int idx, u32 khz,
	       struct gt215_clk_info *info)
{
	struct gt215_clk *clk = gt215_clk(base);
	u32 oclk, sclk, sdiv;
	s32 diff;

	info->clk = 0;

	switch (khz) {
	case 27000:
		info->clk = 0x00000100;
		return khz;
	case 100000:
		info->clk = 0x00002100;
		return khz;
	case 108000:
		info->clk = 0x00002140;
		return khz;
	default:
		sclk = read_vco(clk, idx);
		sdiv = min((sclk * 2) / khz, (u32)65);
		oclk = (sclk * 2) / sdiv;
		diff = ((khz + 3000) - oclk);

		/* When imprecise, play it safe and aim for a clock lower than
		 * desired rather than higher */
		if (diff < 0) {
			sdiv++;
			oclk = (sclk * 2) / sdiv;
		}

		/* divider can go as low as 2, limited here because NVIDIA
		 * and the VBIOS on my NVA8 seem to prefer using the PLL
		 * for 810MHz - is there a good reason?
		 * XXX: PLLs with refclk 810MHz?  */
		if (sdiv > 4) {
			info->clk = (((sdiv - 2) << 16) | 0x00003100);
			return oclk;
		}

		break;
	}

	return -ERANGE;
}

int
gt215_pll_info(struct nvkm_clk *base, int idx, u32 pll, u32 khz,
	       struct gt215_clk_info *info)
{
	struct gt215_clk *clk = gt215_clk(base);
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvbios_pll limits;
	int P, N, M, diff;
	int ret;

	info->pll = 0;

	/* If we can get a within [-2, 3) MHz of a divider, we'll disable the
	 * PLL and use the divider instead. */
	ret = gt215_clk_info(&clk->base, idx, khz, info);
	diff = khz - ret;
	if (!pll || (diff >= -2000 && diff < 3000)) {
		goto out;
	}

	/* Try with PLL */
	ret = nvbios_pll_parse(subdev->device->bios, pll, &limits);
	if (ret)
		return ret;

	ret = gt215_clk_info(&clk->base, idx - 0x10, limits.refclk, info);
	if (ret != limits.refclk)
		return -EINVAL;

	ret = gt215_pll_calc(subdev, &limits, khz, &N, NULL, &M, &P);
	if (ret >= 0) {
		info->pll = (P << 16) | (N << 8) | M;
	}

out:
	info->fb_delay = max(((khz + 7566) / 15133), (u32) 18);
	return ret ? ret : -ERANGE;
}

static int
calc_clk(struct gt215_clk *clk, struct nvkm_cstate *cstate,
	 int idx, u32 pll, int dom)
{
	int ret = gt215_pll_info(&clk->base, idx, pll, cstate->domain[dom],
				 &clk->eng[dom]);
	if (ret >= 0)
		return 0;
	return ret;
}

static int
calc_host(struct gt215_clk *clk, struct nvkm_cstate *cstate)
{
	int ret = 0;
	u32 kHz = cstate->domain[nv_clk_src_host];
	struct gt215_clk_info *info = &clk->eng[nv_clk_src_host];

	if (kHz == 277000) {
		info->clk = 0;
		info->host_out = NVA3_HOST_277;
		return 0;
	}

	info->host_out = NVA3_HOST_CLK;

	ret = gt215_clk_info(&clk->base, 0x1d, kHz, info);
	if (ret >= 0)
		return 0;

	return ret;
}

int
gt215_clk_pre(struct nvkm_clk *clk, unsigned long *flags)
{
	struct nvkm_device *device = clk->subdev.device;
	struct nvkm_fifo *fifo = device->fifo;

	/* halt and idle execution engines */
	nvkm_mask(device, 0x020060, 0x00070000, 0x00000000);
	nvkm_mask(device, 0x002504, 0x00000001, 0x00000001);
	/* Wait until the interrupt handler is finished */
	if (nvkm_msec(device, 2000,
		if (!nvkm_rd32(device, 0x000100))
			break;
	) < 0)
		return -EBUSY;

	if (fifo)
		nvkm_fifo_pause(fifo, flags);

	if (nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x002504) & 0x00000010)
			break;
	) < 0)
		return -EIO;

	if (nvkm_msec(device, 2000,
		u32 tmp = nvkm_rd32(device, 0x00251c) & 0x0000003f;
		if (tmp == 0x0000003f)
			break;
	) < 0)
		return -EIO;

	return 0;
}

void
gt215_clk_post(struct nvkm_clk *clk, unsigned long *flags)
{
	struct nvkm_device *device = clk->subdev.device;
	struct nvkm_fifo *fifo = device->fifo;

	if (fifo && flags)
		nvkm_fifo_start(fifo, flags);

	nvkm_mask(device, 0x002504, 0x00000001, 0x00000000);
	nvkm_mask(device, 0x020060, 0x00070000, 0x00040000);
}

static void
disable_clk_src(struct gt215_clk *clk, u32 src)
{
	struct nvkm_device *device = clk->base.subdev.device;
	nvkm_mask(device, src, 0x00000100, 0x00000000);
	nvkm_mask(device, src, 0x00000001, 0x00000000);
}

static void
prog_pll(struct gt215_clk *clk, int idx, u32 pll, int dom)
{
	struct gt215_clk_info *info = &clk->eng[dom];
	struct nvkm_device *device = clk->base.subdev.device;
	const u32 src0 = 0x004120 + (idx * 4);
	const u32 src1 = 0x004160 + (idx * 4);
	const u32 ctrl = pll + 0;
	const u32 coef = pll + 4;
	u32 bypass;

	if (info->pll) {
		/* Always start from a non-PLL clock */
		bypass = nvkm_rd32(device, ctrl)  & 0x00000008;
		if (!bypass) {
			nvkm_mask(device, src1, 0x00000101, 0x00000101);
			nvkm_mask(device, ctrl, 0x00000008, 0x00000008);
			udelay(20);
		}

		nvkm_mask(device, src0, 0x003f3141, 0x00000101 | info->clk);
		nvkm_wr32(device, coef, info->pll);
		nvkm_mask(device, ctrl, 0x00000015, 0x00000015);
		nvkm_mask(device, ctrl, 0x00000010, 0x00000000);
		if (nvkm_msec(device, 2000,
			if (nvkm_rd32(device, ctrl) & 0x00020000)
				break;
		) < 0) {
			nvkm_mask(device, ctrl, 0x00000010, 0x00000010);
			nvkm_mask(device, src0, 0x00000101, 0x00000000);
			return;
		}
		nvkm_mask(device, ctrl, 0x00000010, 0x00000010);
		nvkm_mask(device, ctrl, 0x00000008, 0x00000000);
		disable_clk_src(clk, src1);
	} else {
		nvkm_mask(device, src1, 0x003f3141, 0x00000101 | info->clk);
		nvkm_mask(device, ctrl, 0x00000018, 0x00000018);
		udelay(20);
		nvkm_mask(device, ctrl, 0x00000001, 0x00000000);
		disable_clk_src(clk, src0);
	}
}

static void
prog_clk(struct gt215_clk *clk, int idx, int dom)
{
	struct gt215_clk_info *info = &clk->eng[dom];
	struct nvkm_device *device = clk->base.subdev.device;
	nvkm_mask(device, 0x004120 + (idx * 4), 0x003f3141, 0x00000101 | info->clk);
}

static void
prog_host(struct gt215_clk *clk)
{
	struct gt215_clk_info *info = &clk->eng[nv_clk_src_host];
	struct nvkm_device *device = clk->base.subdev.device;
	u32 hsrc = (nvkm_rd32(device, 0xc040));

	switch (info->host_out) {
	case NVA3_HOST_277:
		if ((hsrc & 0x30000000) == 0) {
			nvkm_wr32(device, 0xc040, hsrc | 0x20000000);
			disable_clk_src(clk, 0x4194);
		}
		break;
	case NVA3_HOST_CLK:
		prog_clk(clk, 0x1d, nv_clk_src_host);
		if ((hsrc & 0x30000000) >= 0x20000000) {
			nvkm_wr32(device, 0xc040, hsrc & ~0x30000000);
		}
		break;
	default:
		break;
	}

	/* This seems to be a clock gating factor on idle, always set to 64 */
	nvkm_wr32(device, 0xc044, 0x3e);
}

static void
prog_core(struct gt215_clk *clk, int dom)
{
	struct gt215_clk_info *info = &clk->eng[dom];
	struct nvkm_device *device = clk->base.subdev.device;
	u32 fb_delay = nvkm_rd32(device, 0x10002c);

	if (fb_delay < info->fb_delay)
		nvkm_wr32(device, 0x10002c, info->fb_delay);

	prog_pll(clk, 0x00, 0x004200, dom);

	if (fb_delay > info->fb_delay)
		nvkm_wr32(device, 0x10002c, info->fb_delay);
}

static int
gt215_clk_calc(struct nvkm_clk *base, struct nvkm_cstate *cstate)
{
	struct gt215_clk *clk = gt215_clk(base);
	struct gt215_clk_info *core = &clk->eng[nv_clk_src_core];
	int ret;

	if ((ret = calc_clk(clk, cstate, 0x10, 0x4200, nv_clk_src_core)) ||
	    (ret = calc_clk(clk, cstate, 0x11, 0x4220, nv_clk_src_shader)) ||
	    (ret = calc_clk(clk, cstate, 0x20, 0x0000, nv_clk_src_disp)) ||
	    (ret = calc_clk(clk, cstate, 0x21, 0x0000, nv_clk_src_vdec)) ||
	    (ret = calc_host(clk, cstate)))
		return ret;

	/* XXX: Should be reading the highest bit in the VBIOS clock to decide
	 * whether to use a PLL or not... but using a PLL defeats the purpose */
	if (core->pll) {
		ret = gt215_clk_info(&clk->base, 0x10,
				     cstate->domain[nv_clk_src_core_intm],
				     &clk->eng[nv_clk_src_core_intm]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int
gt215_clk_prog(struct nvkm_clk *base)
{
	struct gt215_clk *clk = gt215_clk(base);
	struct gt215_clk_info *core = &clk->eng[nv_clk_src_core];
	int ret = 0;
	unsigned long flags;
	unsigned long *f = &flags;

	ret = gt215_clk_pre(&clk->base, f);
	if (ret)
		goto out;

	if (core->pll)
		prog_core(clk, nv_clk_src_core_intm);

	prog_core(clk,  nv_clk_src_core);
	prog_pll(clk, 0x01, 0x004220, nv_clk_src_shader);
	prog_clk(clk, 0x20, nv_clk_src_disp);
	prog_clk(clk, 0x21, nv_clk_src_vdec);
	prog_host(clk);

out:
	if (ret == -EBUSY)
		f = NULL;

	gt215_clk_post(&clk->base, f);
	return ret;
}

static void
gt215_clk_tidy(struct nvkm_clk *base)
{
}

static const struct nvkm_clk_func
gt215_clk = {
	.read = gt215_clk_read,
	.calc = gt215_clk_calc,
	.prog = gt215_clk_prog,
	.tidy = gt215_clk_tidy,
	.domains = {
		{ nv_clk_src_crystal  , 0xff },
		{ nv_clk_src_core     , 0x00, 0, "core", 1000 },
		{ nv_clk_src_shader   , 0x01, 0, "shader", 1000 },
		{ nv_clk_src_mem      , 0x02, 0, "memory", 1000 },
		{ nv_clk_src_vdec     , 0x03 },
		{ nv_clk_src_disp     , 0x04 },
		{ nv_clk_src_host     , 0x05 },
		{ nv_clk_src_core_intm, 0x06 },
		{ nv_clk_src_max }
	}
};

int
gt215_clk_new(struct nvkm_device *device, int index, struct nvkm_clk **pclk)
{
	struct gt215_clk *clk;

	if (!(clk = kzalloc(sizeof(*clk), GFP_KERNEL)))
		return -ENOMEM;
	*pclk = &clk->base;

	return nvkm_clk_ctor(&gt215_clk, device, index, true, &clk->base);
}
