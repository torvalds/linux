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

#include <drm/drmP.h>
#include "nouveau_drm.h"
#include "nouveau_bios.h"
#include "nouveau_hw.h"
#include "nouveau_pm.h"
#include "nouveau_hwsq.h"

#include "nv50_display.h"

#include <subdev/bios/pll.h>
#include <subdev/clock.h>
#include <subdev/timer.h>
#include <subdev/fb.h>

enum clk_src {
	clk_src_crystal,
	clk_src_href,
	clk_src_hclk,
	clk_src_hclkm3,
	clk_src_hclkm3d2,
	clk_src_host,
	clk_src_nvclk,
	clk_src_sclk,
	clk_src_mclk,
	clk_src_vdec,
	clk_src_dom6
};

static u32 read_clk(struct drm_device *, enum clk_src);

static u32
read_div(struct drm_device *dev)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);

	switch (nv_device(drm->device)->chipset) {
	case 0x50: /* it exists, but only has bit 31, not the dividers.. */
	case 0x84:
	case 0x86:
	case 0x98:
	case 0xa0:
		return nv_rd32(device, 0x004700);
	case 0x92:
	case 0x94:
	case 0x96:
		return nv_rd32(device, 0x004800);
	default:
		return 0x00000000;
	}
}

static u32
read_pll_src(struct drm_device *dev, u32 base)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	u32 coef, ref = read_clk(dev, clk_src_crystal);
	u32 rsel = nv_rd32(device, 0x00e18c);
	int P, N, M, id;

	switch (nv_device(drm->device)->chipset) {
	case 0x50:
	case 0xa0:
		switch (base) {
		case 0x4020:
		case 0x4028: id = !!(rsel & 0x00000004); break;
		case 0x4008: id = !!(rsel & 0x00000008); break;
		case 0x4030: id = 0; break;
		default:
			NV_ERROR(drm, "ref: bad pll 0x%06x\n", base);
			return 0;
		}

		coef = nv_rd32(device, 0x00e81c + (id * 0x0c));
		ref *=  (coef & 0x01000000) ? 2 : 4;
		P    =  (coef & 0x00070000) >> 16;
		N    = ((coef & 0x0000ff00) >> 8) + 1;
		M    = ((coef & 0x000000ff) >> 0) + 1;
		break;
	case 0x84:
	case 0x86:
	case 0x92:
		coef = nv_rd32(device, 0x00e81c);
		P    = (coef & 0x00070000) >> 16;
		N    = (coef & 0x0000ff00) >> 8;
		M    = (coef & 0x000000ff) >> 0;
		break;
	case 0x94:
	case 0x96:
	case 0x98:
		rsel = nv_rd32(device, 0x00c050);
		switch (base) {
		case 0x4020: rsel = (rsel & 0x00000003) >> 0; break;
		case 0x4008: rsel = (rsel & 0x0000000c) >> 2; break;
		case 0x4028: rsel = (rsel & 0x00001800) >> 11; break;
		case 0x4030: rsel = 3; break;
		default:
			NV_ERROR(drm, "ref: bad pll 0x%06x\n", base);
			return 0;
		}

		switch (rsel) {
		case 0: id = 1; break;
		case 1: return read_clk(dev, clk_src_crystal);
		case 2: return read_clk(dev, clk_src_href);
		case 3: id = 0; break;
		}

		coef =  nv_rd32(device, 0x00e81c + (id * 0x28));
		P    = (nv_rd32(device, 0x00e824 + (id * 0x28)) >> 16) & 7;
		P   += (coef & 0x00070000) >> 16;
		N    = (coef & 0x0000ff00) >> 8;
		M    = (coef & 0x000000ff) >> 0;
		break;
	default:
		BUG_ON(1);
	}

	if (M)
		return (ref * N / M) >> P;
	return 0;
}

static u32
read_pll_ref(struct drm_device *dev, u32 base)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	u32 src, mast = nv_rd32(device, 0x00c040);

	switch (base) {
	case 0x004028:
		src = !!(mast & 0x00200000);
		break;
	case 0x004020:
		src = !!(mast & 0x00400000);
		break;
	case 0x004008:
		src = !!(mast & 0x00010000);
		break;
	case 0x004030:
		src = !!(mast & 0x02000000);
		break;
	case 0x00e810:
		return read_clk(dev, clk_src_crystal);
	default:
		NV_ERROR(drm, "bad pll 0x%06x\n", base);
		return 0;
	}

	if (src)
		return read_clk(dev, clk_src_href);
	return read_pll_src(dev, base);
}

static u32
read_pll(struct drm_device *dev, u32 base)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	u32 mast = nv_rd32(device, 0x00c040);
	u32 ctrl = nv_rd32(device, base + 0);
	u32 coef = nv_rd32(device, base + 4);
	u32 ref = read_pll_ref(dev, base);
	u32 clk = 0;
	int N1, N2, M1, M2;

	if (base == 0x004028 && (mast & 0x00100000)) {
		/* wtf, appears to only disable post-divider on nva0 */
		if (nv_device(drm->device)->chipset != 0xa0)
			return read_clk(dev, clk_src_dom6);
	}

	N2 = (coef & 0xff000000) >> 24;
	M2 = (coef & 0x00ff0000) >> 16;
	N1 = (coef & 0x0000ff00) >> 8;
	M1 = (coef & 0x000000ff);
	if ((ctrl & 0x80000000) && M1) {
		clk = ref * N1 / M1;
		if ((ctrl & 0x40000100) == 0x40000000) {
			if (M2)
				clk = clk * N2 / M2;
			else
				clk = 0;
		}
	}

	return clk;
}

static u32
read_clk(struct drm_device *dev, enum clk_src src)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	u32 mast = nv_rd32(device, 0x00c040);
	u32 P = 0;

	switch (src) {
	case clk_src_crystal:
		return device->crystal;
	case clk_src_href:
		return 100000; /* PCIE reference clock */
	case clk_src_hclk:
		return read_clk(dev, clk_src_href) * 27778 / 10000;
	case clk_src_hclkm3:
		return read_clk(dev, clk_src_hclk) * 3;
	case clk_src_hclkm3d2:
		return read_clk(dev, clk_src_hclk) * 3 / 2;
	case clk_src_host:
		switch (mast & 0x30000000) {
		case 0x00000000: return read_clk(dev, clk_src_href);
		case 0x10000000: break;
		case 0x20000000: /* !0x50 */
		case 0x30000000: return read_clk(dev, clk_src_hclk);
		}
		break;
	case clk_src_nvclk:
		if (!(mast & 0x00100000))
			P = (nv_rd32(device, 0x004028) & 0x00070000) >> 16;
		switch (mast & 0x00000003) {
		case 0x00000000: return read_clk(dev, clk_src_crystal) >> P;
		case 0x00000001: return read_clk(dev, clk_src_dom6);
		case 0x00000002: return read_pll(dev, 0x004020) >> P;
		case 0x00000003: return read_pll(dev, 0x004028) >> P;
		}
		break;
	case clk_src_sclk:
		P = (nv_rd32(device, 0x004020) & 0x00070000) >> 16;
		switch (mast & 0x00000030) {
		case 0x00000000:
			if (mast & 0x00000080)
				return read_clk(dev, clk_src_host) >> P;
			return read_clk(dev, clk_src_crystal) >> P;
		case 0x00000010: break;
		case 0x00000020: return read_pll(dev, 0x004028) >> P;
		case 0x00000030: return read_pll(dev, 0x004020) >> P;
		}
		break;
	case clk_src_mclk:
		P = (nv_rd32(device, 0x004008) & 0x00070000) >> 16;
		if (nv_rd32(device, 0x004008) & 0x00000200) {
			switch (mast & 0x0000c000) {
			case 0x00000000:
				return read_clk(dev, clk_src_crystal) >> P;
			case 0x00008000:
			case 0x0000c000:
				return read_clk(dev, clk_src_href) >> P;
			}
		} else {
			return read_pll(dev, 0x004008) >> P;
		}
		break;
	case clk_src_vdec:
		P = (read_div(dev) & 0x00000700) >> 8;
		switch (nv_device(drm->device)->chipset) {
		case 0x84:
		case 0x86:
		case 0x92:
		case 0x94:
		case 0x96:
		case 0xa0:
			switch (mast & 0x00000c00) {
			case 0x00000000:
				if (nv_device(drm->device)->chipset == 0xa0) /* wtf?? */
					return read_clk(dev, clk_src_nvclk) >> P;
				return read_clk(dev, clk_src_crystal) >> P;
			case 0x00000400:
				return 0;
			case 0x00000800:
				if (mast & 0x01000000)
					return read_pll(dev, 0x004028) >> P;
				return read_pll(dev, 0x004030) >> P;
			case 0x00000c00:
				return read_clk(dev, clk_src_nvclk) >> P;
			}
			break;
		case 0x98:
			switch (mast & 0x00000c00) {
			case 0x00000000:
				return read_clk(dev, clk_src_nvclk) >> P;
			case 0x00000400:
				return 0;
			case 0x00000800:
				return read_clk(dev, clk_src_hclkm3d2) >> P;
			case 0x00000c00:
				return read_clk(dev, clk_src_mclk) >> P;
			}
			break;
		}
		break;
	case clk_src_dom6:
		switch (nv_device(drm->device)->chipset) {
		case 0x50:
		case 0xa0:
			return read_pll(dev, 0x00e810) >> 2;
		case 0x84:
		case 0x86:
		case 0x92:
		case 0x94:
		case 0x96:
		case 0x98:
			P = (read_div(dev) & 0x00000007) >> 0;
			switch (mast & 0x0c000000) {
			case 0x00000000: return read_clk(dev, clk_src_href);
			case 0x04000000: break;
			case 0x08000000: return read_clk(dev, clk_src_hclk);
			case 0x0c000000:
				return read_clk(dev, clk_src_hclkm3) >> P;
			}
			break;
		default:
			break;
		}
	default:
		break;
	}

	NV_DEBUG(drm, "unknown clock source %d 0x%08x\n", src, mast);
	return 0;
}

int
nv50_pm_clocks_get(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	if (nv_device(drm->device)->chipset == 0xaa ||
	    nv_device(drm->device)->chipset == 0xac)
		return 0;

	perflvl->core   = read_clk(dev, clk_src_nvclk);
	perflvl->shader = read_clk(dev, clk_src_sclk);
	perflvl->memory = read_clk(dev, clk_src_mclk);
	if (nv_device(drm->device)->chipset != 0x50) {
		perflvl->vdec = read_clk(dev, clk_src_vdec);
		perflvl->dom6 = read_clk(dev, clk_src_dom6);
	}

	return 0;
}

struct nv50_pm_state {
	struct nouveau_pm_level *perflvl;
	struct hwsq_ucode eclk_hwsq;
	struct hwsq_ucode mclk_hwsq;
	u32 mscript;
	u32 mmast;
	u32 mctrl;
	u32 mcoef;
};

static u32
calc_pll(struct drm_device *dev, u32 reg, struct nvbios_pll *pll,
	 u32 clk, int *N1, int *M1, int *log2P)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_bios *bios = nouveau_bios(device);
	struct nouveau_clock *pclk = nouveau_clock(device);
	struct nouveau_pll_vals coef;
	int ret;

	ret = nvbios_pll_parse(bios, reg, pll);
	if (ret)
		return 0;

	pll->vco2.max_freq = 0;
	pll->refclk = read_pll_ref(dev, reg);
	if (!pll->refclk)
		return 0;

	ret = pclk->pll_calc(pclk, pll, clk, &coef);
	if (ret == 0)
		return 0;

	*N1 = coef.N1;
	*M1 = coef.M1;
	*log2P = coef.log2P;
	return ret;
}

static inline u32
calc_div(u32 src, u32 target, int *div)
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

static inline u32
clk_same(u32 a, u32 b)
{
	return ((a / 1000) == (b / 1000));
}

static void
mclk_precharge(struct nouveau_mem_exec_func *exec)
{
	struct nv50_pm_state *info = exec->priv;
	struct hwsq_ucode *hwsq = &info->mclk_hwsq;

	hwsq_wr32(hwsq, 0x1002d4, 0x00000001);
}

static void
mclk_refresh(struct nouveau_mem_exec_func *exec)
{
	struct nv50_pm_state *info = exec->priv;
	struct hwsq_ucode *hwsq = &info->mclk_hwsq;

	hwsq_wr32(hwsq, 0x1002d0, 0x00000001);
}

static void
mclk_refresh_auto(struct nouveau_mem_exec_func *exec, bool enable)
{
	struct nv50_pm_state *info = exec->priv;
	struct hwsq_ucode *hwsq = &info->mclk_hwsq;

	hwsq_wr32(hwsq, 0x100210, enable ? 0x80000000 : 0x00000000);
}

static void
mclk_refresh_self(struct nouveau_mem_exec_func *exec, bool enable)
{
	struct nv50_pm_state *info = exec->priv;
	struct hwsq_ucode *hwsq = &info->mclk_hwsq;

	hwsq_wr32(hwsq, 0x1002dc, enable ? 0x00000001 : 0x00000000);
}

static void
mclk_wait(struct nouveau_mem_exec_func *exec, u32 nsec)
{
	struct nv50_pm_state *info = exec->priv;
	struct hwsq_ucode *hwsq = &info->mclk_hwsq;

	if (nsec > 1000)
		hwsq_usec(hwsq, (nsec + 500) / 1000);
}

static u32
mclk_mrg(struct nouveau_mem_exec_func *exec, int mr)
{
	struct nouveau_device *device = nouveau_dev(exec->dev);
	if (mr <= 1)
		return nv_rd32(device, 0x1002c0 + ((mr - 0) * 4));
	if (mr <= 3)
		return nv_rd32(device, 0x1002e0 + ((mr - 2) * 4));
	return 0;
}

static void
mclk_mrs(struct nouveau_mem_exec_func *exec, int mr, u32 data)
{
	struct nouveau_device *device = nouveau_dev(exec->dev);
	struct nouveau_fb *pfb = nouveau_fb(device);
	struct nv50_pm_state *info = exec->priv;
	struct hwsq_ucode *hwsq = &info->mclk_hwsq;

	if (mr <= 1) {
		if (pfb->ram.ranks > 1)
			hwsq_wr32(hwsq, 0x1002c8 + ((mr - 0) * 4), data);
		hwsq_wr32(hwsq, 0x1002c0 + ((mr - 0) * 4), data);
	} else
	if (mr <= 3) {
		if (pfb->ram.ranks > 1)
			hwsq_wr32(hwsq, 0x1002e8 + ((mr - 2) * 4), data);
		hwsq_wr32(hwsq, 0x1002e0 + ((mr - 2) * 4), data);
	}
}

static void
mclk_clock_set(struct nouveau_mem_exec_func *exec)
{
	struct nouveau_device *device = nouveau_dev(exec->dev);
	struct nv50_pm_state *info = exec->priv;
	struct hwsq_ucode *hwsq = &info->mclk_hwsq;
	u32 ctrl = nv_rd32(device, 0x004008);

	info->mmast = nv_rd32(device, 0x00c040);
	info->mmast &= ~0xc0000000; /* get MCLK_2 from HREF */
	info->mmast |=  0x0000c000; /* use MCLK_2 as MPLL_BYPASS clock */

	hwsq_wr32(hwsq, 0xc040, info->mmast);
	hwsq_wr32(hwsq, 0x4008, ctrl | 0x00000200); /* bypass MPLL */
	if (info->mctrl & 0x80000000)
		hwsq_wr32(hwsq, 0x400c, info->mcoef);
	hwsq_wr32(hwsq, 0x4008, info->mctrl);
}

static void
mclk_timing_set(struct nouveau_mem_exec_func *exec)
{
	struct nouveau_device *device = nouveau_dev(exec->dev);
	struct nv50_pm_state *info = exec->priv;
	struct nouveau_pm_level *perflvl = info->perflvl;
	struct hwsq_ucode *hwsq = &info->mclk_hwsq;
	int i;

	for (i = 0; i < 9; i++) {
		u32 reg = 0x100220 + (i * 4);
		u32 val = nv_rd32(device, reg);
		if (val != perflvl->timing.reg[i])
			hwsq_wr32(hwsq, reg, perflvl->timing.reg[i]);
	}
}

static int
calc_mclk(struct drm_device *dev, struct nouveau_pm_level *perflvl,
	  struct nv50_pm_state *info)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_device *device = nouveau_dev(dev);
	u32 crtc_mask = 0; /*XXX: nv50_display_active_crtcs(dev); */
	struct nouveau_mem_exec_func exec = {
		.dev = dev,
		.precharge = mclk_precharge,
		.refresh = mclk_refresh,
		.refresh_auto = mclk_refresh_auto,
		.refresh_self = mclk_refresh_self,
		.wait = mclk_wait,
		.mrg = mclk_mrg,
		.mrs = mclk_mrs,
		.clock_set = mclk_clock_set,
		.timing_set = mclk_timing_set,
		.priv = info
	};
	struct hwsq_ucode *hwsq = &info->mclk_hwsq;
	struct nvbios_pll pll;
	int N, M, P;
	int ret;

	/* use pcie refclock if possible, otherwise use mpll */
	info->mctrl  = nv_rd32(device, 0x004008);
	info->mctrl &= ~0x81ff0200;
	if (clk_same(perflvl->memory, read_clk(dev, clk_src_href))) {
		info->mctrl |= 0x00000200 | (pll.bias_p << 19);
	} else {
		ret = calc_pll(dev, 0x4008, &pll, perflvl->memory, &N, &M, &P);
		if (ret == 0)
			return -EINVAL;

		info->mctrl |= 0x80000000 | (P << 22) | (P << 16);
		info->mctrl |= pll.bias_p << 19;
		info->mcoef  = (N << 8) | M;
	}

	/* build the ucode which will reclock the memory for us */
	hwsq_init(hwsq);
	if (crtc_mask) {
		hwsq_op5f(hwsq, crtc_mask, 0x00); /* wait for scanout */
		hwsq_op5f(hwsq, crtc_mask, 0x01); /* wait for vblank */
	}
	if (nv_device(drm->device)->chipset >= 0x92)
		hwsq_wr32(hwsq, 0x611200, 0x00003300); /* disable scanout */
	hwsq_setf(hwsq, 0x10, 0); /* disable bus access */
	hwsq_op5f(hwsq, 0x00, 0x01); /* no idea :s */

	ret = nouveau_mem_exec(&exec, perflvl);
	if (ret)
		return ret;

	hwsq_setf(hwsq, 0x10, 1); /* enable bus access */
	hwsq_op5f(hwsq, 0x00, 0x00); /* no idea, reverse of 0x00, 0x01? */
	if (nv_device(drm->device)->chipset >= 0x92)
		hwsq_wr32(hwsq, 0x611200, 0x00003330); /* enable scanout */
	hwsq_fini(hwsq);
	return 0;
}

void *
nv50_pm_clocks_pre(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv50_pm_state *info;
	struct hwsq_ucode *hwsq;
	struct nvbios_pll pll;
	u32 out, mast, divs, ctrl;
	int clk, ret = -EINVAL;
	int N, M, P1, P2;

	if (nv_device(drm->device)->chipset == 0xaa ||
	    nv_device(drm->device)->chipset == 0xac)
		return ERR_PTR(-ENODEV);

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);
	info->perflvl = perflvl;

	/* memory: build hwsq ucode which we'll use to reclock memory.
	 *         use pcie refclock if possible, otherwise use mpll */
	info->mclk_hwsq.len = 0;
	if (perflvl->memory) {
		ret = calc_mclk(dev, perflvl, info);
		if (ret)
			goto error;
		info->mscript = perflvl->memscript;
	}

	divs = read_div(dev);
	mast = info->mmast;

	/* start building HWSQ script for engine reclocking */
	hwsq = &info->eclk_hwsq;
	hwsq_init(hwsq);
	hwsq_setf(hwsq, 0x10, 0); /* disable bus access */
	hwsq_op5f(hwsq, 0x00, 0x01); /* wait for access disabled? */

	/* vdec/dom6: switch to "safe" clocks temporarily */
	if (perflvl->vdec) {
		mast &= ~0x00000c00;
		divs &= ~0x00000700;
	}

	if (perflvl->dom6) {
		mast &= ~0x0c000000;
		divs &= ~0x00000007;
	}

	hwsq_wr32(hwsq, 0x00c040, mast);

	/* vdec: avoid modifying xpll until we know exactly how the other
	 * clock domains work, i suspect at least some of them can also be
	 * tied to xpll...
	 */
	if (perflvl->vdec) {
		/* see how close we can get using nvclk as a source */
		clk = calc_div(perflvl->core, perflvl->vdec, &P1);

		/* see how close we can get using xpll/hclk as a source */
		if (nv_device(drm->device)->chipset != 0x98)
			out = read_pll(dev, 0x004030);
		else
			out = read_clk(dev, clk_src_hclkm3d2);
		out = calc_div(out, perflvl->vdec, &P2);

		/* select whichever gets us closest */
		if (abs((int)perflvl->vdec - clk) <=
		    abs((int)perflvl->vdec - out)) {
			if (nv_device(drm->device)->chipset != 0x98)
				mast |= 0x00000c00;
			divs |= P1 << 8;
		} else {
			mast |= 0x00000800;
			divs |= P2 << 8;
		}
	}

	/* dom6: nfi what this is, but we're limited to various combinations
	 * of the host clock frequency
	 */
	if (perflvl->dom6) {
		if (clk_same(perflvl->dom6, read_clk(dev, clk_src_href))) {
			mast |= 0x00000000;
		} else
		if (clk_same(perflvl->dom6, read_clk(dev, clk_src_hclk))) {
			mast |= 0x08000000;
		} else {
			clk = read_clk(dev, clk_src_hclk) * 3;
			clk = calc_div(clk, perflvl->dom6, &P1);

			mast |= 0x0c000000;
			divs |= P1;
		}
	}

	/* vdec/dom6: complete switch to new clocks */
	switch (nv_device(drm->device)->chipset) {
	case 0x92:
	case 0x94:
	case 0x96:
		hwsq_wr32(hwsq, 0x004800, divs);
		break;
	default:
		hwsq_wr32(hwsq, 0x004700, divs);
		break;
	}

	hwsq_wr32(hwsq, 0x00c040, mast);

	/* core/shader: make sure sclk/nvclk are disconnected from their
	 * PLLs (nvclk to dom6, sclk to hclk)
	 */
	if (nv_device(drm->device)->chipset < 0x92)
		mast = (mast & ~0x001000b0) | 0x00100080;
	else
		mast = (mast & ~0x000000b3) | 0x00000081;

	hwsq_wr32(hwsq, 0x00c040, mast);

	/* core: for the moment at least, always use nvpll */
	clk = calc_pll(dev, 0x4028, &pll, perflvl->core, &N, &M, &P1);
	if (clk == 0)
		goto error;

	ctrl  = nv_rd32(device, 0x004028) & ~0xc03f0100;
	mast &= ~0x00100000;
	mast |= 3;

	hwsq_wr32(hwsq, 0x004028, 0x80000000 | (P1 << 19) | (P1 << 16) | ctrl);
	hwsq_wr32(hwsq, 0x00402c, (N << 8) | M);

	/* shader: tie to nvclk if possible, otherwise use spll.  have to be
	 * very careful that the shader clock is at least twice the core, or
	 * some chipsets will be very unhappy.  i expect most or all of these
	 * cases will be handled by tying to nvclk, but it's possible there's
	 * corners
	 */
	ctrl = nv_rd32(device, 0x004020) & ~0xc03f0100;

	if (P1-- && perflvl->shader == (perflvl->core << 1)) {
		hwsq_wr32(hwsq, 0x004020, (P1 << 19) | (P1 << 16) | ctrl);
		hwsq_wr32(hwsq, 0x00c040, 0x00000020 | mast);
	} else {
		clk = calc_pll(dev, 0x4020, &pll, perflvl->shader, &N, &M, &P1);
		if (clk == 0)
			goto error;
		ctrl |= 0x80000000;

		hwsq_wr32(hwsq, 0x004020, (P1 << 19) | (P1 << 16) | ctrl);
		hwsq_wr32(hwsq, 0x004024, (N << 8) | M);
		hwsq_wr32(hwsq, 0x00c040, 0x00000030 | mast);
	}

	hwsq_setf(hwsq, 0x10, 1); /* enable bus access */
	hwsq_op5f(hwsq, 0x00, 0x00); /* wait for access enabled? */
	hwsq_fini(hwsq);

	return info;
error:
	kfree(info);
	return ERR_PTR(ret);
}

static int
prog_hwsq(struct drm_device *dev, struct hwsq_ucode *hwsq)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	u32 hwsq_data, hwsq_kick;
	int i;

	if (nv_device(drm->device)->chipset < 0x94) {
		hwsq_data = 0x001400;
		hwsq_kick = 0x00000003;
	} else {
		hwsq_data = 0x080000;
		hwsq_kick = 0x00000001;
	}
	/* upload hwsq ucode */
	nv_mask(device, 0x001098, 0x00000008, 0x00000000);
	nv_wr32(device, 0x001304, 0x00000000);
	if (nv_device(drm->device)->chipset >= 0x92)
		nv_wr32(device, 0x001318, 0x00000000);
	for (i = 0; i < hwsq->len / 4; i++)
		nv_wr32(device, hwsq_data + (i * 4), hwsq->ptr.u32[i]);
	nv_mask(device, 0x001098, 0x00000018, 0x00000018);

	/* launch, and wait for completion */
	nv_wr32(device, 0x00130c, hwsq_kick);
	if (!nv_wait(device, 0x001308, 0x00000100, 0x00000000)) {
		NV_ERROR(drm, "hwsq ucode exec timed out\n");
		NV_ERROR(drm, "0x001308: 0x%08x\n", nv_rd32(device, 0x001308));
		for (i = 0; i < hwsq->len / 4; i++) {
			NV_ERROR(drm, "0x%06x: 0x%08x\n", 0x1400 + (i * 4),
				 nv_rd32(device, 0x001400 + (i * 4)));
		}

		return -EIO;
	}

	return 0;
}

int
nv50_pm_clocks_set(struct drm_device *dev, void *data)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nv50_pm_state *info = data;
	struct bit_entry M;
	int ret = -EBUSY;

	/* halt and idle execution engines */
	nv_mask(device, 0x002504, 0x00000001, 0x00000001);
	if (!nv_wait(device, 0x002504, 0x00000010, 0x00000010))
		goto resume;
	if (!nv_wait(device, 0x00251c, 0x0000003f, 0x0000003f))
		goto resume;

	/* program memory clock, if necessary - must come before engine clock
	 * reprogramming due to how we construct the hwsq scripts in pre()
	 */
#define nouveau_bios_init_exec(a,b) nouveau_bios_run_init_table((a), (b), NULL, 0)
	if (info->mclk_hwsq.len) {
		/* execute some scripts that do ??? from the vbios.. */
		if (!bit_table(dev, 'M', &M) && M.version == 1) {
			if (M.length >= 6)
				nouveau_bios_init_exec(dev, ROM16(M.data[5]));
			if (M.length >= 8)
				nouveau_bios_init_exec(dev, ROM16(M.data[7]));
			if (M.length >= 10)
				nouveau_bios_init_exec(dev, ROM16(M.data[9]));
			nouveau_bios_init_exec(dev, info->mscript);
		}

		ret = prog_hwsq(dev, &info->mclk_hwsq);
		if (ret)
			goto resume;
	}

	/* program engine clocks */
	ret = prog_hwsq(dev, &info->eclk_hwsq);

resume:
	nv_mask(device, 0x002504, 0x00000001, 0x00000000);
	kfree(info);
	return ret;
}
