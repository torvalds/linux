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

#include <subdev/bios.h>
#include <subdev/bios/pll.h>

#include "nv50.h"
#include "pll.h"
#include "seq.h"

static u32
read_div(struct nv50_clock_priv *priv)
{
	switch (nv_device(priv)->chipset) {
	case 0x50: /* it exists, but only has bit 31, not the dividers.. */
	case 0x84:
	case 0x86:
	case 0x98:
	case 0xa0:
		return nv_rd32(priv, 0x004700);
	case 0x92:
	case 0x94:
	case 0x96:
		return nv_rd32(priv, 0x004800);
	default:
		return 0x00000000;
	}
}

static u32
read_pll_src(struct nv50_clock_priv *priv, u32 base)
{
	struct nouveau_clock *clk = &priv->base;
	u32 coef, ref = clk->read(clk, nv_clk_src_crystal);
	u32 rsel = nv_rd32(priv, 0x00e18c);
	int P, N, M, id;

	switch (nv_device(priv)->chipset) {
	case 0x50:
	case 0xa0:
		switch (base) {
		case 0x4020:
		case 0x4028: id = !!(rsel & 0x00000004); break;
		case 0x4008: id = !!(rsel & 0x00000008); break;
		case 0x4030: id = 0; break;
		default:
			nv_error(priv, "ref: bad pll 0x%06x\n", base);
			return 0;
		}

		coef = nv_rd32(priv, 0x00e81c + (id * 0x0c));
		ref *=  (coef & 0x01000000) ? 2 : 4;
		P    =  (coef & 0x00070000) >> 16;
		N    = ((coef & 0x0000ff00) >> 8) + 1;
		M    = ((coef & 0x000000ff) >> 0) + 1;
		break;
	case 0x84:
	case 0x86:
	case 0x92:
		coef = nv_rd32(priv, 0x00e81c);
		P    = (coef & 0x00070000) >> 16;
		N    = (coef & 0x0000ff00) >> 8;
		M    = (coef & 0x000000ff) >> 0;
		break;
	case 0x94:
	case 0x96:
	case 0x98:
		rsel = nv_rd32(priv, 0x00c050);
		switch (base) {
		case 0x4020: rsel = (rsel & 0x00000003) >> 0; break;
		case 0x4008: rsel = (rsel & 0x0000000c) >> 2; break;
		case 0x4028: rsel = (rsel & 0x00001800) >> 11; break;
		case 0x4030: rsel = 3; break;
		default:
			nv_error(priv, "ref: bad pll 0x%06x\n", base);
			return 0;
		}

		switch (rsel) {
		case 0: id = 1; break;
		case 1: return clk->read(clk, nv_clk_src_crystal);
		case 2: return clk->read(clk, nv_clk_src_href);
		case 3: id = 0; break;
		}

		coef =  nv_rd32(priv, 0x00e81c + (id * 0x28));
		P    = (nv_rd32(priv, 0x00e824 + (id * 0x28)) >> 16) & 7;
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
read_pll_ref(struct nv50_clock_priv *priv, u32 base)
{
	struct nouveau_clock *clk = &priv->base;
	u32 src, mast = nv_rd32(priv, 0x00c040);

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
		return clk->read(clk, nv_clk_src_crystal);
	default:
		nv_error(priv, "bad pll 0x%06x\n", base);
		return 0;
	}

	if (src)
		return clk->read(clk, nv_clk_src_href);
	return read_pll_src(priv, base);
}

static u32
read_pll(struct nv50_clock_priv *priv, u32 base)
{
	struct nouveau_clock *clk = &priv->base;
	u32 mast = nv_rd32(priv, 0x00c040);
	u32 ctrl = nv_rd32(priv, base + 0);
	u32 coef = nv_rd32(priv, base + 4);
	u32 ref = read_pll_ref(priv, base);
	u32 freq = 0;
	int N1, N2, M1, M2;

	if (base == 0x004028 && (mast & 0x00100000)) {
		/* wtf, appears to only disable post-divider on nva0 */
		if (nv_device(priv)->chipset != 0xa0)
			return clk->read(clk, nv_clk_src_dom6);
	}

	N2 = (coef & 0xff000000) >> 24;
	M2 = (coef & 0x00ff0000) >> 16;
	N1 = (coef & 0x0000ff00) >> 8;
	M1 = (coef & 0x000000ff);
	if ((ctrl & 0x80000000) && M1) {
		freq = ref * N1 / M1;
		if ((ctrl & 0x40000100) == 0x40000000) {
			if (M2)
				freq = freq * N2 / M2;
			else
				freq = 0;
		}
	}

	return freq;
}

static int
nv50_clock_read(struct nouveau_clock *clk, enum nv_clk_src src)
{
	struct nv50_clock_priv *priv = (void *)clk;
	u32 mast = nv_rd32(priv, 0x00c040);
	u32 P = 0;

	switch (src) {
	case nv_clk_src_crystal:
		return nv_device(priv)->crystal;
	case nv_clk_src_href:
		return 100000; /* PCIE reference clock */
	case nv_clk_src_hclk:
		return div_u64((u64)clk->read(clk, nv_clk_src_href) * 27778, 10000);
	case nv_clk_src_hclkm3:
		return clk->read(clk, nv_clk_src_hclk) * 3;
	case nv_clk_src_hclkm3d2:
		return clk->read(clk, nv_clk_src_hclk) * 3 / 2;
	case nv_clk_src_host:
		switch (mast & 0x30000000) {
		case 0x00000000: return clk->read(clk, nv_clk_src_href);
		case 0x10000000: break;
		case 0x20000000: /* !0x50 */
		case 0x30000000: return clk->read(clk, nv_clk_src_hclk);
		}
		break;
	case nv_clk_src_core:
		if (!(mast & 0x00100000))
			P = (nv_rd32(priv, 0x004028) & 0x00070000) >> 16;
		switch (mast & 0x00000003) {
		case 0x00000000: return clk->read(clk, nv_clk_src_crystal) >> P;
		case 0x00000001: return clk->read(clk, nv_clk_src_dom6);
		case 0x00000002: return read_pll(priv, 0x004020) >> P;
		case 0x00000003: return read_pll(priv, 0x004028) >> P;
		}
		break;
	case nv_clk_src_shader:
		P = (nv_rd32(priv, 0x004020) & 0x00070000) >> 16;
		switch (mast & 0x00000030) {
		case 0x00000000:
			if (mast & 0x00000080)
				return clk->read(clk, nv_clk_src_host) >> P;
			return clk->read(clk, nv_clk_src_crystal) >> P;
		case 0x00000010: break;
		case 0x00000020: return read_pll(priv, 0x004028) >> P;
		case 0x00000030: return read_pll(priv, 0x004020) >> P;
		}
		break;
	case nv_clk_src_mem:
		P = (nv_rd32(priv, 0x004008) & 0x00070000) >> 16;
		if (nv_rd32(priv, 0x004008) & 0x00000200) {
			switch (mast & 0x0000c000) {
			case 0x00000000:
				return clk->read(clk, nv_clk_src_crystal) >> P;
			case 0x00008000:
			case 0x0000c000:
				return clk->read(clk, nv_clk_src_href) >> P;
			}
		} else {
			return read_pll(priv, 0x004008) >> P;
		}
		break;
	case nv_clk_src_vdec:
		P = (read_div(priv) & 0x00000700) >> 8;
		switch (nv_device(priv)->chipset) {
		case 0x84:
		case 0x86:
		case 0x92:
		case 0x94:
		case 0x96:
		case 0xa0:
			switch (mast & 0x00000c00) {
			case 0x00000000:
				if (nv_device(priv)->chipset == 0xa0) /* wtf?? */
					return clk->read(clk, nv_clk_src_core) >> P;
				return clk->read(clk, nv_clk_src_crystal) >> P;
			case 0x00000400:
				return 0;
			case 0x00000800:
				if (mast & 0x01000000)
					return read_pll(priv, 0x004028) >> P;
				return read_pll(priv, 0x004030) >> P;
			case 0x00000c00:
				return clk->read(clk, nv_clk_src_core) >> P;
			}
			break;
		case 0x98:
			switch (mast & 0x00000c00) {
			case 0x00000000:
				return clk->read(clk, nv_clk_src_core) >> P;
			case 0x00000400:
				return 0;
			case 0x00000800:
				return clk->read(clk, nv_clk_src_hclkm3d2) >> P;
			case 0x00000c00:
				return clk->read(clk, nv_clk_src_mem) >> P;
			}
			break;
		}
		break;
	case nv_clk_src_dom6:
		switch (nv_device(priv)->chipset) {
		case 0x50:
		case 0xa0:
			return read_pll(priv, 0x00e810) >> 2;
		case 0x84:
		case 0x86:
		case 0x92:
		case 0x94:
		case 0x96:
		case 0x98:
			P = (read_div(priv) & 0x00000007) >> 0;
			switch (mast & 0x0c000000) {
			case 0x00000000: return clk->read(clk, nv_clk_src_href);
			case 0x04000000: break;
			case 0x08000000: return clk->read(clk, nv_clk_src_hclk);
			case 0x0c000000:
				return clk->read(clk, nv_clk_src_hclkm3) >> P;
			}
			break;
		default:
			break;
		}
	default:
		break;
	}

	nv_debug(priv, "unknown clock source %d 0x%08x\n", src, mast);
	return -EINVAL;
}

static u32
calc_pll(struct nv50_clock_priv *priv, u32 reg, u32 clk, int *N, int *M, int *P)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_pll pll;
	int ret;

	ret = nvbios_pll_parse(bios, reg, &pll);
	if (ret)
		return 0;

	pll.vco2.max_freq = 0;
	pll.refclk = read_pll_ref(priv, reg);
	if (!pll.refclk)
		return 0;

	return nv04_pll_calc(nv_subdev(priv), &pll, clk, N, M, NULL, NULL, P);
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

static int
nv50_clock_calc(struct nouveau_clock *clk, struct nouveau_cstate *cstate)
{
	struct nv50_clock_priv *priv = (void *)clk;
	struct nv50_clock_hwsq *hwsq = &priv->hwsq;
	const int shader = cstate->domain[nv_clk_src_shader];
	const int core = cstate->domain[nv_clk_src_core];
	const int vdec = cstate->domain[nv_clk_src_vdec];
	const int dom6 = cstate->domain[nv_clk_src_dom6];
	u32 mastm = 0, mastv = 0;
	u32 divsm = 0, divsv = 0;
	int N, M, P1, P2;
	int freq, out;

	/* prepare a hwsq script from which we'll perform the reclock */
	out = clk_init(hwsq, nv_subdev(clk));
	if (out)
		return out;

	clk_wr32(hwsq, fifo, 0x00000001); /* block fifo */
	clk_nsec(hwsq, 8000);
	clk_setf(hwsq, 0x10, 0x00); /* disable fb */
	clk_wait(hwsq, 0x00, 0x01); /* wait for fb disabled */

	/* vdec: avoid modifying xpll until we know exactly how the other
	 * clock domains work, i suspect at least some of them can also be
	 * tied to xpll...
	 */
	if (vdec) {
		/* see how close we can get using nvclk as a source */
		freq = calc_div(core, vdec, &P1);

		/* see how close we can get using xpll/hclk as a source */
		if (nv_device(priv)->chipset != 0x98)
			out = read_pll(priv, 0x004030);
		else
			out = clk->read(clk, nv_clk_src_hclkm3d2);
		out = calc_div(out, vdec, &P2);

		/* select whichever gets us closest */
		if (abs(vdec - freq) <= abs(vdec - out)) {
			if (nv_device(priv)->chipset != 0x98)
				mastv |= 0x00000c00;
			divsv |= P1 << 8;
		} else {
			mastv |= 0x00000800;
			divsv |= P2 << 8;
		}

		mastm |= 0x00000c00;
		divsm |= 0x00000700;
	}

	/* dom6: nfi what this is, but we're limited to various combinations
	 * of the host clock frequency
	 */
	if (dom6) {
		if (clk_same(dom6, clk->read(clk, nv_clk_src_href))) {
			mastv |= 0x00000000;
		} else
		if (clk_same(dom6, clk->read(clk, nv_clk_src_hclk))) {
			mastv |= 0x08000000;
		} else {
			freq = clk->read(clk, nv_clk_src_hclk) * 3;
			freq = calc_div(freq, dom6, &P1);

			mastv |= 0x0c000000;
			divsv |= P1;
		}

		mastm |= 0x0c000000;
		divsm |= 0x00000007;
	}

	/* vdec/dom6: switch to "safe" clocks temporarily, update dividers
	 * and then switch to target clocks
	 */
	clk_mask(hwsq, mast, mastm, 0x00000000);
	clk_mask(hwsq, divs, divsm, divsv);
	clk_mask(hwsq, mast, mastm, mastv);

	/* core/shader: disconnect nvclk/sclk from their PLLs (nvclk to dom6,
	 * sclk to hclk) before reprogramming
	 */
	if (nv_device(priv)->chipset < 0x92)
		clk_mask(hwsq, mast, 0x001000b0, 0x00100080);
	else
		clk_mask(hwsq, mast, 0x000000b3, 0x00000081);

	/* core: for the moment at least, always use nvpll */
	freq = calc_pll(priv, 0x4028, core, &N, &M, &P1);
	if (freq == 0)
		return -ERANGE;

	clk_mask(hwsq, nvpll[0], 0xc03f0100,
				 0x80000000 | (P1 << 19) | (P1 << 16));
	clk_mask(hwsq, nvpll[1], 0x0000ffff, (N << 8) | M);

	/* shader: tie to nvclk if possible, otherwise use spll.  have to be
	 * very careful that the shader clock is at least twice the core, or
	 * some chipsets will be very unhappy.  i expect most or all of these
	 * cases will be handled by tying to nvclk, but it's possible there's
	 * corners
	 */
	if (P1-- && shader == (core << 1)) {
		clk_mask(hwsq, spll[0], 0xc03f0100, (P1 << 19) | (P1 << 16));
		clk_mask(hwsq, mast, 0x00100033, 0x00000023);
	} else {
		freq = calc_pll(priv, 0x4020, shader, &N, &M, &P1);
		if (freq == 0)
			return -ERANGE;

		clk_mask(hwsq, spll[0], 0xc03f0100,
					0x80000000 | (P1 << 19) | (P1 << 16));
		clk_mask(hwsq, spll[1], 0x0000ffff, (N << 8) | M);
		clk_mask(hwsq, mast, 0x00100033, 0x00000033);
	}

	/* restore normal operation */
	clk_setf(hwsq, 0x10, 0x01); /* enable fb */
	clk_wait(hwsq, 0x00, 0x00); /* wait for fb enabled */
	clk_wr32(hwsq, fifo, 0x00000000); /* un-block fifo */
	return 0;
}

static int
nv50_clock_prog(struct nouveau_clock *clk)
{
	struct nv50_clock_priv *priv = (void *)clk;
	return clk_exec(&priv->hwsq, true);
}

static void
nv50_clock_tidy(struct nouveau_clock *clk)
{
	struct nv50_clock_priv *priv = (void *)clk;
	clk_exec(&priv->hwsq, false);
}

int
nv50_clock_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nv50_clock_oclass *pclass = (void *)oclass;
	struct nv50_clock_priv *priv;
	int ret;

	ret = nouveau_clock_create(parent, engine, oclass, pclass->domains,
				  &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->hwsq.r_fifo = hwsq_reg(0x002504);
	priv->hwsq.r_spll[0] = hwsq_reg(0x004020);
	priv->hwsq.r_spll[1] = hwsq_reg(0x004024);
	priv->hwsq.r_nvpll[0] = hwsq_reg(0x004028);
	priv->hwsq.r_nvpll[1] = hwsq_reg(0x00402c);
	switch (nv_device(priv)->chipset) {
	case 0x92:
	case 0x94:
	case 0x96:
		priv->hwsq.r_divs = hwsq_reg(0x004800);
		break;
	default:
		priv->hwsq.r_divs = hwsq_reg(0x004700);
		break;
	}
	priv->hwsq.r_mast = hwsq_reg(0x00c040);

	priv->base.read = nv50_clock_read;
	priv->base.calc = nv50_clock_calc;
	priv->base.prog = nv50_clock_prog;
	priv->base.tidy = nv50_clock_tidy;
	return 0;
}

static struct nouveau_clocks
nv50_domains[] = {
	{ nv_clk_src_crystal, 0xff },
	{ nv_clk_src_href   , 0xff },
	{ nv_clk_src_core   , 0xff, 0, "core", 1000 },
	{ nv_clk_src_shader , 0xff, 0, "shader", 1000 },
	{ nv_clk_src_mem    , 0xff, 0, "memory", 1000 },
	{ nv_clk_src_max }
};

struct nouveau_oclass *
nv50_clock_oclass = &(struct nv50_clock_oclass) {
	.base.handle = NV_SUBDEV(CLOCK, 0x50),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_clock_ctor,
		.dtor = _nouveau_clock_dtor,
		.init = _nouveau_clock_init,
		.fini = _nouveau_clock_fini,
	},
	.domains = nv50_domains,
}.base;
