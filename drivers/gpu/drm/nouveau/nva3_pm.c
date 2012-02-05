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

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_bios.h"
#include "nouveau_pm.h"

static u32 read_clk(struct drm_device *, int, bool);
static u32 read_pll(struct drm_device *, int, u32);

static u32
read_vco(struct drm_device *dev, int clk)
{
	u32 sctl = nv_rd32(dev, 0x4120 + (clk * 4));
	if ((sctl & 0x00000030) != 0x00000030)
		return read_pll(dev, 0x41, 0x00e820);
	return read_pll(dev, 0x42, 0x00e8a0);
}

static u32
read_clk(struct drm_device *dev, int clk, bool ignore_en)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 sctl, sdiv, sclk;

	/* refclk for the 0xe8xx plls is a fixed frequency */
	if (clk >= 0x40) {
		if (dev_priv->chipset == 0xaf) {
			/* no joke.. seriously.. sigh.. */
			return nv_rd32(dev, 0x00471c) * 1000;
		}

		return dev_priv->crystal;
	}

	sctl = nv_rd32(dev, 0x4120 + (clk * 4));
	if (!ignore_en && !(sctl & 0x00000100))
		return 0;

	switch (sctl & 0x00003000) {
	case 0x00000000:
		return dev_priv->crystal;
	case 0x00002000:
		if (sctl & 0x00000040)
			return 108000;
		return 100000;
	case 0x00003000:
		sclk = read_vco(dev, clk);
		sdiv = ((sctl & 0x003f0000) >> 16) + 2;
		return (sclk * 2) / sdiv;
	default:
		return 0;
	}
}

static u32
read_pll(struct drm_device *dev, int clk, u32 pll)
{
	u32 ctrl = nv_rd32(dev, pll + 0);
	u32 sclk = 0, P = 1, N = 1, M = 1;

	if (!(ctrl & 0x00000008)) {
		if (ctrl & 0x00000001) {
			u32 coef = nv_rd32(dev, pll + 4);
			M = (coef & 0x000000ff) >> 0;
			N = (coef & 0x0000ff00) >> 8;
			P = (coef & 0x003f0000) >> 16;

			/* no post-divider on these.. */
			if ((pll & 0x00ff00) == 0x00e800)
				P = 1;

			sclk = read_clk(dev, 0x00 + clk, false);
		}
	} else {
		sclk = read_clk(dev, 0x10 + clk, false);
	}

	if (M * P)
		return sclk * N / (M * P);
	return 0;
}

struct creg {
	u32 clk;
	u32 pll;
};

static int
calc_clk(struct drm_device *dev, int clk, u32 pll, u32 khz, struct creg *reg)
{
	struct pll_lims limits;
	u32 oclk, sclk, sdiv;
	int P, N, M, diff;
	int ret;

	reg->pll = 0;
	reg->clk = 0;
	if (!khz) {
		NV_DEBUG(dev, "no clock for 0x%04x/0x%02x\n", pll, clk);
		return 0;
	}

	switch (khz) {
	case 27000:
		reg->clk = 0x00000100;
		return khz;
	case 100000:
		reg->clk = 0x00002100;
		return khz;
	case 108000:
		reg->clk = 0x00002140;
		return khz;
	default:
		sclk = read_vco(dev, clk);
		sdiv = min((sclk * 2) / (khz - 2999), (u32)65);
		/* if the clock has a PLL attached, and we can get a within
		 * [-2, 3) MHz of a divider, we'll disable the PLL and use
		 * the divider instead.
		 *
		 * divider can go as low as 2, limited here because NVIDIA
		 * and the VBIOS on my NVA8 seem to prefer using the PLL
		 * for 810MHz - is there a good reason?
		 */
		if (sdiv > 4) {
			oclk = (sclk * 2) / sdiv;
			diff = khz - oclk;
			if (!pll || (diff >= -2000 && diff < 3000)) {
				reg->clk = (((sdiv - 2) << 16) | 0x00003100);
				return oclk;
			}
		}

		if (!pll) {
			NV_ERROR(dev, "bad freq %02x: %d %d\n", clk, khz, sclk);
			return -ERANGE;
		}

		break;
	}

	ret = get_pll_limits(dev, pll, &limits);
	if (ret)
		return ret;

	limits.refclk = read_clk(dev, clk - 0x10, true);
	if (!limits.refclk)
		return -EINVAL;

	ret = nva3_calc_pll(dev, &limits, khz, &N, NULL, &M, &P);
	if (ret >= 0) {
		reg->clk = nv_rd32(dev, 0x4120 + (clk * 4));
		reg->pll = (P << 16) | (N << 8) | M;
	}
	return ret;
}

static void
prog_pll(struct drm_device *dev, int clk, u32 pll, struct creg *reg)
{
	const u32 src0 = 0x004120 + (clk * 4);
	const u32 src1 = 0x004160 + (clk * 4);
	const u32 ctrl = pll + 0;
	const u32 coef = pll + 4;

	if (!reg->clk && !reg->pll) {
		NV_DEBUG(dev, "no clock for %02x\n", clk);
		return;
	}

	if (reg->pll) {
		nv_mask(dev, src0, 0x00000101, 0x00000101);
		nv_wr32(dev, coef, reg->pll);
		nv_mask(dev, ctrl, 0x00000015, 0x00000015);
		nv_mask(dev, ctrl, 0x00000010, 0x00000000);
		nv_wait(dev, ctrl, 0x00020000, 0x00020000);
		nv_mask(dev, ctrl, 0x00000010, 0x00000010);
		nv_mask(dev, ctrl, 0x00000008, 0x00000000);
		nv_mask(dev, src1, 0x00000100, 0x00000000);
		nv_mask(dev, src1, 0x00000001, 0x00000000);
	} else {
		nv_mask(dev, src1, 0x003f3141, 0x00000101 | reg->clk);
		nv_mask(dev, ctrl, 0x00000018, 0x00000018);
		udelay(20);
		nv_mask(dev, ctrl, 0x00000001, 0x00000000);
		nv_mask(dev, src0, 0x00000100, 0x00000000);
		nv_mask(dev, src0, 0x00000001, 0x00000000);
	}
}

static void
prog_clk(struct drm_device *dev, int clk, struct creg *reg)
{
	if (!reg->clk) {
		NV_DEBUG(dev, "no clock for %02x\n", clk);
		return;
	}

	nv_mask(dev, 0x004120 + (clk * 4), 0x003f3141, 0x00000101 | reg->clk);
}

int
nva3_pm_clocks_get(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	perflvl->core   = read_pll(dev, 0x00, 0x4200);
	perflvl->shader = read_pll(dev, 0x01, 0x4220);
	perflvl->memory = read_pll(dev, 0x02, 0x4000);
	perflvl->unka0  = read_clk(dev, 0x20, false);
	perflvl->vdec   = read_clk(dev, 0x21, false);
	perflvl->daemon = read_clk(dev, 0x25, false);
	perflvl->copy   = perflvl->core;
	return 0;
}

struct nva3_pm_state {
	struct nouveau_pm_level *perflvl;

	struct creg nclk;
	struct creg sclk;
	struct creg vdec;
	struct creg unka0;

	struct creg mclk;
	u8 *rammap;
	u8  rammap_ver;
	u8  rammap_len;
	u8 *ramcfg;
	u8  ramcfg_len;
	u32 r004018;
	u32 r100760;
};

void *
nva3_pm_clocks_pre(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct nva3_pm_state *info;
	u8 ramcfg_cnt;
	int ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	ret = calc_clk(dev, 0x10, 0x4200, perflvl->core, &info->nclk);
	if (ret < 0)
		goto out;

	ret = calc_clk(dev, 0x11, 0x4220, perflvl->shader, &info->sclk);
	if (ret < 0)
		goto out;

	ret = calc_clk(dev, 0x12, 0x4000, perflvl->memory, &info->mclk);
	if (ret < 0)
		goto out;

	ret = calc_clk(dev, 0x20, 0x0000, perflvl->unka0, &info->unka0);
	if (ret < 0)
		goto out;

	ret = calc_clk(dev, 0x21, 0x0000, perflvl->vdec, &info->vdec);
	if (ret < 0)
		goto out;

	info->rammap = nouveau_perf_rammap(dev, perflvl->memory,
					   &info->rammap_ver,
					   &info->rammap_len,
					   &ramcfg_cnt, &info->ramcfg_len);
	if (info->rammap_ver != 0x10 || info->rammap_len < 5)
		info->rammap = NULL;

	info->ramcfg = nouveau_perf_ramcfg(dev, perflvl->memory,
					   &info->rammap_ver,
					   &info->ramcfg_len);
	if (info->rammap_ver != 0x10)
		info->ramcfg = NULL;

	info->perflvl = perflvl;
out:
	if (ret < 0) {
		kfree(info);
		info = ERR_PTR(ret);
	}
	return info;
}

static bool
nva3_pm_grcp_idle(void *data)
{
	struct drm_device *dev = data;

	if (!(nv_rd32(dev, 0x400304) & 0x00000001))
		return true;
	if (nv_rd32(dev, 0x400308) == 0x0050001c)
		return true;
	return false;
}

static void
mclk_precharge(struct nouveau_mem_exec_func *exec)
{
	nv_wr32(exec->dev, 0x1002d4, 0x00000001);
}

static void
mclk_refresh(struct nouveau_mem_exec_func *exec)
{
	nv_wr32(exec->dev, 0x1002d0, 0x00000001);
}

static void
mclk_refresh_auto(struct nouveau_mem_exec_func *exec, bool enable)
{
	nv_wr32(exec->dev, 0x100210, enable ? 0x80000000 : 0x00000000);
}

static void
mclk_refresh_self(struct nouveau_mem_exec_func *exec, bool enable)
{
	nv_wr32(exec->dev, 0x1002dc, enable ? 0x00000001 : 0x00000000);
}

static void
mclk_wait(struct nouveau_mem_exec_func *exec, u32 nsec)
{
	udelay((nsec + 500) / 1000);
}

static u32
mclk_mrg(struct nouveau_mem_exec_func *exec, int mr)
{
	if (mr <= 1)
		return nv_rd32(exec->dev, 0x1002c0 + ((mr - 0) * 4));
	if (mr <= 3)
		return nv_rd32(exec->dev, 0x1002e0 + ((mr - 2) * 4));
	return 0;
}

static void
mclk_mrs(struct nouveau_mem_exec_func *exec, int mr, u32 data)
{
	struct drm_nouveau_private *dev_priv = exec->dev->dev_private;

	if (mr <= 1) {
		if (dev_priv->vram_rank_B)
			nv_wr32(exec->dev, 0x1002c8 + ((mr - 0) * 4), data);
		nv_wr32(exec->dev, 0x1002c0 + ((mr - 0) * 4), data);
	} else
	if (mr <= 3) {
		if (dev_priv->vram_rank_B)
			nv_wr32(exec->dev, 0x1002e8 + ((mr - 2) * 4), data);
		nv_wr32(exec->dev, 0x1002e0 + ((mr - 2) * 4), data);
	}
}

static void
mclk_clock_set(struct nouveau_mem_exec_func *exec)
{
	struct drm_device *dev = exec->dev;
	struct nva3_pm_state *info = exec->priv;
	u32 ctrl;

	ctrl = nv_rd32(dev, 0x004000);
	if (!(ctrl & 0x00000008) && info->mclk.pll) {
		nv_wr32(dev, 0x004000, (ctrl |=  0x00000008));
		nv_mask(dev, 0x1110e0, 0x00088000, 0x00088000);
		nv_wr32(dev, 0x004018, 0x00001000);
		nv_wr32(dev, 0x004000, (ctrl &= ~0x00000001));
		nv_wr32(dev, 0x004004, info->mclk.pll);
		nv_wr32(dev, 0x004000, (ctrl |=  0x00000001));
		udelay(64);
		nv_wr32(dev, 0x004018, 0x00005000 | info->r004018);
		udelay(20);
	} else
	if (!info->mclk.pll) {
		nv_mask(dev, 0x004168, 0x003f3040, info->mclk.clk);
		nv_wr32(dev, 0x004000, (ctrl |= 0x00000008));
		nv_mask(dev, 0x1110e0, 0x00088000, 0x00088000);
		nv_wr32(dev, 0x004018, 0x0000d000 | info->r004018);
	}

	if (info->rammap) {
		if (info->ramcfg && (info->rammap[4] & 0x08)) {
			u32 unk5a0 = (ROM16(info->ramcfg[5]) << 8) |
				      info->ramcfg[5];
			u32 unk5a4 = ROM16(info->ramcfg[7]);
			u32 unk804 = (info->ramcfg[9] & 0xf0) << 16 |
				     (info->ramcfg[3] & 0x0f) << 16 |
				     (info->ramcfg[9] & 0x0f) |
				     0x80000000;
			nv_wr32(dev, 0x1005a0, unk5a0);
			nv_wr32(dev, 0x1005a4, unk5a4);
			nv_wr32(dev, 0x10f804, unk804);
			nv_mask(dev, 0x10053c, 0x00001000, 0x00000000);
		} else {
			nv_mask(dev, 0x10053c, 0x00001000, 0x00001000);
			nv_mask(dev, 0x10f804, 0x80000000, 0x00000000);
			nv_mask(dev, 0x100760, 0x22222222, info->r100760);
			nv_mask(dev, 0x1007a0, 0x22222222, info->r100760);
			nv_mask(dev, 0x1007e0, 0x22222222, info->r100760);
		}
	}

	if (info->mclk.pll) {
		nv_mask(dev, 0x1110e0, 0x00088000, 0x00011000);
		nv_wr32(dev, 0x004000, (ctrl &= ~0x00000008));
	}
}

static void
mclk_timing_set(struct nouveau_mem_exec_func *exec)
{
	struct drm_device *dev = exec->dev;
	struct nva3_pm_state *info = exec->priv;
	struct nouveau_pm_level *perflvl = info->perflvl;
	int i;

	for (i = 0; i < 9; i++)
		nv_wr32(dev, 0x100220 + (i * 4), perflvl->timing.reg[i]);

	if (info->ramcfg) {
		u32 data = (info->ramcfg[2] & 0x08) ? 0x00000000 : 0x00001000;
		nv_mask(dev, 0x100200, 0x00001000, data);
	}

	if (info->ramcfg) {
		u32 unk714 = nv_rd32(dev, 0x100714) & ~0xf0000010;
		u32 unk718 = nv_rd32(dev, 0x100718) & ~0x00000100;
		u32 unk71c = nv_rd32(dev, 0x10071c) & ~0x00000100;
		if ( (info->ramcfg[2] & 0x20))
			unk714 |= 0xf0000000;
		if (!(info->ramcfg[2] & 0x04))
			unk714 |= 0x00000010;
		nv_wr32(dev, 0x100714, unk714);

		if (info->ramcfg[2] & 0x01)
			unk71c |= 0x00000100;
		nv_wr32(dev, 0x10071c, unk71c);

		if (info->ramcfg[2] & 0x02)
			unk718 |= 0x00000100;
		nv_wr32(dev, 0x100718, unk718);

		if (info->ramcfg[2] & 0x10)
			nv_wr32(dev, 0x111100, 0x48000000); /*XXX*/
	}
}

static void
prog_mem(struct drm_device *dev, struct nva3_pm_state *info)
{
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
	u32 ctrl;

	/* XXX: where the fuck does 750MHz come from? */
	if (info->perflvl->memory <= 750000) {
		info->r004018 = 0x10000000;
		info->r100760 = 0x22222222;
	}

	ctrl = nv_rd32(dev, 0x004000);
	if (ctrl & 0x00000008) {
		if (info->mclk.pll) {
			nv_mask(dev, 0x004128, 0x00000101, 0x00000101);
			nv_wr32(dev, 0x004004, info->mclk.pll);
			nv_wr32(dev, 0x004000, (ctrl |= 0x00000001));
			nv_wr32(dev, 0x004000, (ctrl &= 0xffffffef));
			nv_wait(dev, 0x004000, 0x00020000, 0x00020000);
			nv_wr32(dev, 0x004000, (ctrl |= 0x00000010));
			nv_wr32(dev, 0x004018, 0x00005000 | info->r004018);
			nv_wr32(dev, 0x004000, (ctrl |= 0x00000004));
		}
	} else {
		u32 ssel = 0x00000101;
		if (info->mclk.clk)
			ssel |= info->mclk.clk;
		else
			ssel |= 0x00080000; /* 324MHz, shouldn't matter... */
		nv_mask(dev, 0x004168, 0x003f3141, ctrl);
	}

	if (info->ramcfg) {
		if (info->ramcfg[2] & 0x10) {
			nv_mask(dev, 0x111104, 0x00000600, 0x00000000);
		} else {
			nv_mask(dev, 0x111100, 0x40000000, 0x40000000);
			nv_mask(dev, 0x111104, 0x00000180, 0x00000000);
		}
	}
	if (info->rammap && !(info->rammap[4] & 0x02))
		nv_mask(dev, 0x100200, 0x00000800, 0x00000000);
	nv_wr32(dev, 0x611200, 0x00003300);
	if (!(info->ramcfg[2] & 0x10))
		nv_wr32(dev, 0x111100, 0x4c020000); /*XXX*/

	nouveau_mem_exec(&exec, info->perflvl);

	nv_wr32(dev, 0x611200, 0x00003330);
	if (info->rammap && (info->rammap[4] & 0x02))
		nv_mask(dev, 0x100200, 0x00000800, 0x00000800);
	if (info->ramcfg) {
		if (info->ramcfg[2] & 0x10) {
			nv_mask(dev, 0x111104, 0x00000180, 0x00000180);
			nv_mask(dev, 0x111100, 0x40000000, 0x00000000);
		} else {
			nv_mask(dev, 0x111104, 0x00000600, 0x00000600);
		}
	}

	if (info->mclk.pll) {
		nv_mask(dev, 0x004168, 0x00000001, 0x00000000);
		nv_mask(dev, 0x004168, 0x00000100, 0x00000000);
	} else {
		nv_mask(dev, 0x004000, 0x00000001, 0x00000000);
		nv_mask(dev, 0x004128, 0x00000001, 0x00000000);
		nv_mask(dev, 0x004128, 0x00000100, 0x00000000);
	}
}

int
nva3_pm_clocks_set(struct drm_device *dev, void *pre_state)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nva3_pm_state *info = pre_state;
	unsigned long flags;
	int ret = -EAGAIN;

	/* prevent any new grctx switches from starting */
	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_wr32(dev, 0x400324, 0x00000000);
	nv_wr32(dev, 0x400328, 0x0050001c); /* wait flag 0x1c */
	/* wait for any pending grctx switches to complete */
	if (!nv_wait_cb(dev, nva3_pm_grcp_idle, dev)) {
		NV_ERROR(dev, "pm: ctxprog didn't go idle\n");
		goto cleanup;
	}
	/* freeze PFIFO */
	nv_mask(dev, 0x002504, 0x00000001, 0x00000001);
	if (!nv_wait(dev, 0x002504, 0x00000010, 0x00000010)) {
		NV_ERROR(dev, "pm: fifo didn't go idle\n");
		goto cleanup;
	}

	prog_pll(dev, 0x00, 0x004200, &info->nclk);
	prog_pll(dev, 0x01, 0x004220, &info->sclk);
	prog_clk(dev, 0x20, &info->unka0);
	prog_clk(dev, 0x21, &info->vdec);

	if (info->mclk.clk || info->mclk.pll)
		prog_mem(dev, info);

	ret = 0;

cleanup:
	/* unfreeze PFIFO */
	nv_mask(dev, 0x002504, 0x00000001, 0x00000000);
	/* restore ctxprog to normal */
	nv_wr32(dev, 0x400324, 0x00000000);
	nv_wr32(dev, 0x400328, 0x0070009c); /* set flag 0x1c */
	/* unblock it if necessary */
	if (nv_rd32(dev, 0x400308) == 0x0050001c)
		nv_mask(dev, 0x400824, 0x10000000, 0x10000000);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);
	kfree(info);
	return ret;
}
