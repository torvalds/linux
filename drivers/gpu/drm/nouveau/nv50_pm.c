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
#include "nouveau_hw.h"
#include "nouveau_pm.h"

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
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	switch (dev_priv->chipset) {
	case 0x50: /* it exists, but only has bit 31, not the dividers.. */
	case 0x84:
	case 0x86:
	case 0x98:
	case 0xa0:
		return nv_rd32(dev, 0x004700);
	case 0x92:
	case 0x94:
	case 0x96:
		return nv_rd32(dev, 0x004800);
	default:
		return 0x00000000;
	}
}

static u32
read_pll_src(struct drm_device *dev, u32 base)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 coef, ref = read_clk(dev, clk_src_crystal);
	u32 rsel = nv_rd32(dev, 0x00e18c);
	int P, N, M, id;

	switch (dev_priv->chipset) {
	case 0x50:
	case 0xa0:
		switch (base) {
		case 0x4020:
		case 0x4028: id = !!(rsel & 0x00000004); break;
		case 0x4008: id = !!(rsel & 0x00000008); break;
		case 0x4030: id = 0; break;
		default:
			NV_ERROR(dev, "ref: bad pll 0x%06x\n", base);
			return 0;
		}

		coef = nv_rd32(dev, 0x00e81c + (id * 0x0c));
		ref *=  (coef & 0x01000000) ? 2 : 4;
		P    =  (coef & 0x00070000) >> 16;
		N    = ((coef & 0x0000ff00) >> 8) + 1;
		M    = ((coef & 0x000000ff) >> 0) + 1;
		break;
	case 0x84:
	case 0x86:
	case 0x92:
		coef = nv_rd32(dev, 0x00e81c);
		P    = (coef & 0x00070000) >> 16;
		N    = (coef & 0x0000ff00) >> 8;
		M    = (coef & 0x000000ff) >> 0;
		break;
	case 0x94:
	case 0x96:
	case 0x98:
		rsel = nv_rd32(dev, 0x00c050);
		switch (base) {
		case 0x4020: rsel = (rsel & 0x00000003) >> 0; break;
		case 0x4008: rsel = (rsel & 0x0000000c) >> 2; break;
		case 0x4028: rsel = (rsel & 0x00001800) >> 11; break;
		case 0x4030: rsel = 3; break;
		default:
			NV_ERROR(dev, "ref: bad pll 0x%06x\n", base);
			return 0;
		}

		switch (rsel) {
		case 0: id = 1; break;
		case 1: return read_clk(dev, clk_src_crystal);
		case 2: return read_clk(dev, clk_src_href);
		case 3: id = 0; break;
		}

		coef =  nv_rd32(dev, 0x00e81c + (id * 0x28));
		P    = (nv_rd32(dev, 0x00e824 + (id * 0x28)) >> 16) & 7;
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
	u32 src, mast = nv_rd32(dev, 0x00c040);

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
		NV_ERROR(dev, "bad pll 0x%06x\n", base);
		return 0;
	}

	if (src)
		return read_clk(dev, clk_src_href);
	return read_pll_src(dev, base);
}

static u32
read_pll(struct drm_device *dev, u32 base)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 mast = nv_rd32(dev, 0x00c040);
	u32 ctrl = nv_rd32(dev, base + 0);
	u32 coef = nv_rd32(dev, base + 4);
	u32 ref = read_pll_ref(dev, base);
	u32 clk = 0;
	int N1, N2, M1, M2;

	if (base == 0x004028 && (mast & 0x00100000)) {
		/* wtf, appears to only disable post-divider on nva0 */
		if (dev_priv->chipset != 0xa0)
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 mast = nv_rd32(dev, 0x00c040);
	u32 P = 0;

	switch (src) {
	case clk_src_crystal:
		return dev_priv->crystal;
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
			P = (nv_rd32(dev, 0x004028) & 0x00070000) >> 16;
		switch (mast & 0x00000003) {
		case 0x00000000: return read_clk(dev, clk_src_crystal) >> P;
		case 0x00000001: return read_clk(dev, clk_src_dom6);
		case 0x00000002: return read_pll(dev, 0x004020) >> P;
		case 0x00000003: return read_pll(dev, 0x004028) >> P;
		}
		break;
	case clk_src_sclk:
		P = (nv_rd32(dev, 0x004020) & 0x00070000) >> 16;
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
		P = (nv_rd32(dev, 0x004008) & 0x00070000) >> 16;
		if (nv_rd32(dev, 0x004008) & 0x00000200) {
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
		switch (dev_priv->chipset) {
		case 0x84:
		case 0x86:
		case 0x92:
		case 0x94:
		case 0x96:
		case 0xa0:
			switch (mast & 0x00000c00) {
			case 0x00000000:
				if (dev_priv->chipset == 0xa0) /* wtf?? */
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
				return read_pll(dev, clk_src_mclk) >> P;
			}
			break;
		}
		break;
	case clk_src_dom6:
		switch (dev_priv->chipset) {
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

	NV_DEBUG(dev, "unknown clock source %d 0x%08x\n", src, mast);
	return 0;
}

int
nv50_pm_clocks_get(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	if (dev_priv->chipset == 0xaa ||
	    dev_priv->chipset == 0xac)
		return 0;

	perflvl->core   = read_clk(dev, clk_src_nvclk);
	perflvl->shader = read_clk(dev, clk_src_sclk);
	perflvl->memory = read_clk(dev, clk_src_mclk);
	if (dev_priv->chipset != 0x50) {
		perflvl->vdec = read_clk(dev, clk_src_vdec);
		perflvl->dom6 = read_clk(dev, clk_src_dom6);
	}

	return 0;
}

struct nv50_pm_state {
	u32 emast;
	u32 nctrl;
	u32 ncoef;
	u32 sctrl;
	u32 scoef;

	u32 amast;
	u32 pdivs;

	u32 mscript;
	u32 mctrl;
	u32 mcoef;
};

static u32
calc_pll(struct drm_device *dev, u32 reg, struct pll_lims *pll,
	 u32 clk, int *N1, int *M1, int *log2P)
{
	struct nouveau_pll_vals coef;
	int ret;

	ret = get_pll_limits(dev, reg, pll);
	if (ret)
		return 0;

	pll->vco2.maxfreq = 0;
	pll->refclk = read_pll_ref(dev, reg);
	if (!pll->refclk)
		return 0;

	ret = nouveau_calc_pll_mnp(dev, pll, clk, &coef);
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

void *
nv50_pm_clocks_pre(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_pm_state *info;
	struct pll_lims pll;
	int ret = -EINVAL;
	int N, M, P1, P2;
	u32 clk, out;

	if (dev_priv->chipset == 0xaa ||
	    dev_priv->chipset == 0xac)
		return ERR_PTR(-ENODEV);

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	/* core: for the moment at least, always use nvpll */
	clk = calc_pll(dev, 0x4028, &pll, perflvl->core, &N, &M, &P1);
	if (clk == 0)
		goto error;

	info->emast = 0x00000003;
	info->nctrl = 0x80000000 | (P1 << 19) | (P1 << 16);
	info->ncoef = (N << 8) | M;

	/* shader: tie to nvclk if possible, otherwise use spll.  have to be
	 * very careful that the shader clock is at least twice the core, or
	 * some chipsets will be very unhappy.  i expect most or all of these
	 * cases will be handled by tying to nvclk, but it's possible there's
	 * corners
	 */
	if (P1-- && perflvl->shader == (perflvl->core << 1)) {
		info->emast |= 0x00000020;
		info->sctrl  = 0x00000000 | (P1 << 19) | (P1 << 16);
		info->scoef  = nv_rd32(dev, 0x004024);
	} else {
		clk = calc_pll(dev, 0x4020, &pll, perflvl->shader, &N, &M, &P1);
		if (clk == 0)
			goto error;

		info->emast |= 0x00000030;
		info->sctrl  = 0x80000000 | (P1 << 19) | (P1 << 16);
		info->scoef  = (N << 8) | M;
	}

	/* memory: use pcie refclock if possible, otherwise use mpll */
	info->mscript = perflvl->memscript;
	if (clk_same(perflvl->memory, read_clk(dev, clk_src_href))) {
		info->mctrl = nv_rd32(dev, 0x4008) | 0x00000200;
		info->mcoef = nv_rd32(dev, 0x400c);
	} else
	if (perflvl->memory) {
		clk = calc_pll(dev, 0x4008, &pll, perflvl->memory,
			       &N, &M, &P1);
		if (clk == 0)
			goto error;

		info->mctrl  = 0x80000000 | (P1 << 22) | (P1 << 16);
		info->mctrl |= pll.log2p_bias << 19;
		info->mcoef  = (N << 8) | M;
	} else {
		info->mctrl = 0x00000000;
	}

	/* vdec: avoid modifying xpll until we know exactly how the other
	 * clock domains work, i suspect at least some of them can also be
	 * tied to xpll...
	 */
	info->amast = nv_rd32(dev, 0x00c040);
	info->pdivs = read_div(dev);
	if (perflvl->vdec) {
		/* see how close we can get using nvclk as a source */
		clk = calc_div(perflvl->core, perflvl->vdec, &P1);

		/* see how close we can get using xpll/hclk as a source */
		if (dev_priv->chipset != 0x98)
			out = read_pll(dev, 0x004030);
		else
			out = read_clk(dev, clk_src_hclkm3d2);
		out = calc_div(out, perflvl->vdec, &P2);

		/* select whichever gets us closest */
		info->amast &= ~0x00000c00;
		info->pdivs &= ~0x00000700;
		if (abs((int)perflvl->vdec - clk) <=
		    abs((int)perflvl->vdec - out)) {
			if (dev_priv->chipset != 0x98)
				info->amast |= 0x00000c00;
			info->pdivs |= P1 << 8;
		} else {
			info->amast |= 0x00000800;
			info->pdivs |= P2 << 8;
		}
	}

	/* dom6: nfi what this is, but we're limited to various combinations
	 * of the host clock frequency
	 */
	if (perflvl->dom6) {
		info->amast &= ~0x0c000000;
		if (clk_same(perflvl->dom6, read_clk(dev, clk_src_href))) {
			info->amast |= 0x00000000;
		} else
		if (clk_same(perflvl->dom6, read_clk(dev, clk_src_hclk))) {
			info->amast |= 0x08000000;
		} else {
			clk = read_clk(dev, clk_src_hclk) * 3;
			clk = calc_div(clk, perflvl->dom6, &P1);

			info->amast |= 0x0c000000;
			info->pdivs  = (info->pdivs & ~0x00000007) | P1;
		}
	}

	return info;
error:
	kfree(info);
	return ERR_PTR(ret);
}

int
nv50_pm_clocks_set(struct drm_device *dev, void *data)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_pm_state *info = data;
	struct bit_entry M;
	int ret = 0;

	/* halt and idle execution engines */
	nv_mask(dev, 0x002504, 0x00000001, 0x00000001);
	if (!nv_wait(dev, 0x002504, 0x00000010, 0x00000010))
		goto error;

	/* reclock vdec/dom6 */
	nv_mask(dev, 0x00c040, 0x00000c00, 0x00000000);
	switch (dev_priv->chipset) {
	case 0x92:
	case 0x94:
	case 0x96:
		nv_mask(dev, 0x004800, 0x00000707, info->pdivs);
		break;
	default:
		nv_mask(dev, 0x004700, 0x00000707, info->pdivs);
		break;
	}
	nv_mask(dev, 0x00c040, 0x0c000c00, info->amast);

	/* core/shader: make sure sclk/nvclk are disconnected from their
	 * plls (nvclk to dom6, sclk to hclk), modify the plls, and
	 * reconnect sclk/nvclk to their new clock source
	 */
	if (dev_priv->chipset < 0x92)
		nv_mask(dev, 0x00c040, 0x001000b0, 0x00100080); /* grrr! */
	else
		nv_mask(dev, 0x00c040, 0x000000b3, 0x00000081);
	nv_mask(dev, 0x004020, 0xc03f0100, info->sctrl);
	nv_wr32(dev, 0x004024, info->scoef);
	nv_mask(dev, 0x004028, 0xc03f0100, info->nctrl);
	nv_wr32(dev, 0x00402c, info->ncoef);
	nv_mask(dev, 0x00c040, 0x00100033, info->emast);

	/* memory */
	if (!info->mctrl)
		goto resume;

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

	/* disable display */
	nv_wr32(dev, 0x611200, 0x00003300);
	udelay(100);

	/* prepare ram for reclocking */
	nv_wr32(dev, 0x1002d4, 0x00000001); /* precharge */
	nv_wr32(dev, 0x1002d0, 0x00000001); /* refresh */
	nv_wr32(dev, 0x1002d0, 0x00000001); /* refresh */
	nv_mask(dev, 0x100210, 0x80000000, 0x00000000); /* no auto-refresh */
	nv_wr32(dev, 0x1002dc, 0x00000001); /* enable self-refresh */

	/* modify mpll */
	nv_mask(dev, 0x00c040, 0x0000c000, 0x0000c000);
	nv_mask(dev, 0x004008, 0x81ff0200, 0x00000200 | info->mctrl);
	nv_wr32(dev, 0x00400c, info->mcoef);
	udelay(100);
	nv_mask(dev, 0x004008, 0x81ff0200, info->mctrl);

	/* re-enable normal operation of memory controller */
	nv_wr32(dev, 0x1002dc, 0x00000000);
	nv_mask(dev, 0x100210, 0x80000000, 0x80000000);
	udelay(100);

	/* re-enable display */
	nv_wr32(dev, 0x611200, 0x00003330);

	goto resume;
error:
	ret = -EBUSY;
resume:
	nv_mask(dev, 0x002504, 0x00000001, 0x00000000);
	kfree(info);
	return ret;
}

static int
pwm_info(struct drm_device *dev, struct dcb_gpio_entry *gpio,
	 int *ctrl, int *line, int *indx)
{
	if (gpio->line == 0x04) {
		*ctrl = 0x00e100;
		*line = 4;
		*indx = 0;
	} else
	if (gpio->line == 0x09) {
		*ctrl = 0x00e100;
		*line = 9;
		*indx = 1;
	} else
	if (gpio->line == 0x10) {
		*ctrl = 0x00e28c;
		*line = 0;
		*indx = 0;
	} else {
		NV_ERROR(dev, "unknown pwm ctrl for gpio %d\n", gpio->line);
		return -ENODEV;
	}

	return 0;
}

int
nv50_pm_pwm_get(struct drm_device *dev, struct dcb_gpio_entry *gpio,
		u32 *divs, u32 *duty)
{
	int ctrl, line, id, ret = pwm_info(dev, gpio, &ctrl, &line, &id);
	if (ret)
		return ret;

	if (nv_rd32(dev, ctrl) & (1 << line)) {
		*divs = nv_rd32(dev, 0x00e114 + (id * 8));
		*duty = nv_rd32(dev, 0x00e118 + (id * 8));
		return 0;
	}

	return -EINVAL;
}

int
nv50_pm_pwm_set(struct drm_device *dev, struct dcb_gpio_entry *gpio,
		u32 divs, u32 duty)
{
	int ctrl, line, id, ret = pwm_info(dev, gpio, &ctrl, &line, &id);
	if (ret)
		return ret;

	nv_mask(dev, ctrl, 0x00010001 << line, 0x00000001 << line);
	nv_wr32(dev, 0x00e114 + (id * 8), divs);
	nv_wr32(dev, 0x00e118 + (id * 8), duty | 0x80000000);
	return 0;
}
