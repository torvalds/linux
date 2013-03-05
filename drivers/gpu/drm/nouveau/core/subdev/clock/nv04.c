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

#include <subdev/clock.h>
#include <subdev/bios.h>
#include <subdev/bios/pll.h>

#include "pll.h"

struct nv04_clock_priv {
	struct nouveau_clock base;
};

static int
powerctrl_1_shift(int chip_version, int reg)
{
	int shift = -4;

	if (chip_version < 0x17 || chip_version == 0x1a || chip_version == 0x20)
		return shift;

	switch (reg) {
	case 0x680520:
		shift += 4;
	case 0x680508:
		shift += 4;
	case 0x680504:
		shift += 4;
	case 0x680500:
		shift += 4;
	}

	/*
	 * the shift for vpll regs is only used for nv3x chips with a single
	 * stage pll
	 */
	if (shift > 4 && (chip_version < 0x32 || chip_version == 0x35 ||
			  chip_version == 0x36 || chip_version >= 0x40))
		shift = -4;

	return shift;
}

static void
setPLL_single(struct nv04_clock_priv *priv, u32 reg,
	      struct nouveau_pll_vals *pv)
{
	int chip_version = nouveau_bios(priv)->version.chip;
	uint32_t oldpll = nv_rd32(priv, reg);
	int oldN = (oldpll >> 8) & 0xff, oldM = oldpll & 0xff;
	uint32_t pll = (oldpll & 0xfff80000) | pv->log2P << 16 | pv->NM1;
	uint32_t saved_powerctrl_1 = 0;
	int shift_powerctrl_1 = powerctrl_1_shift(chip_version, reg);

	if (oldpll == pll)
		return;	/* already set */

	if (shift_powerctrl_1 >= 0) {
		saved_powerctrl_1 = nv_rd32(priv, 0x001584);
		nv_wr32(priv, 0x001584,
			(saved_powerctrl_1 & ~(0xf << shift_powerctrl_1)) |
			1 << shift_powerctrl_1);
	}

	if (oldM && pv->M1 && (oldN / oldM < pv->N1 / pv->M1))
		/* upclock -- write new post divider first */
		nv_wr32(priv, reg, pv->log2P << 16 | (oldpll & 0xffff));
	else
		/* downclock -- write new NM first */
		nv_wr32(priv, reg, (oldpll & 0xffff0000) | pv->NM1);

	if (chip_version < 0x17 && chip_version != 0x11)
		/* wait a bit on older chips */
		msleep(64);
	nv_rd32(priv, reg);

	/* then write the other half as well */
	nv_wr32(priv, reg, pll);

	if (shift_powerctrl_1 >= 0)
		nv_wr32(priv, 0x001584, saved_powerctrl_1);
}

static uint32_t
new_ramdac580(uint32_t reg1, bool ss, uint32_t ramdac580)
{
	bool head_a = (reg1 == 0x680508);

	if (ss)	/* single stage pll mode */
		ramdac580 |= head_a ? 0x00000100 : 0x10000000;
	else
		ramdac580 &= head_a ? 0xfffffeff : 0xefffffff;

	return ramdac580;
}

static void
setPLL_double_highregs(struct nv04_clock_priv *priv, u32 reg1,
		       struct nouveau_pll_vals *pv)
{
	int chip_version = nouveau_bios(priv)->version.chip;
	bool nv3035 = chip_version == 0x30 || chip_version == 0x35;
	uint32_t reg2 = reg1 + ((reg1 == 0x680520) ? 0x5c : 0x70);
	uint32_t oldpll1 = nv_rd32(priv, reg1);
	uint32_t oldpll2 = !nv3035 ? nv_rd32(priv, reg2) : 0;
	uint32_t pll1 = (oldpll1 & 0xfff80000) | pv->log2P << 16 | pv->NM1;
	uint32_t pll2 = (oldpll2 & 0x7fff0000) | 1 << 31 | pv->NM2;
	uint32_t oldramdac580 = 0, ramdac580 = 0;
	bool single_stage = !pv->NM2 || pv->N2 == pv->M2;	/* nv41+ only */
	uint32_t saved_powerctrl_1 = 0, savedc040 = 0;
	int shift_powerctrl_1 = powerctrl_1_shift(chip_version, reg1);

	/* model specific additions to generic pll1 and pll2 set up above */
	if (nv3035) {
		pll1 = (pll1 & 0xfcc7ffff) | (pv->N2 & 0x18) << 21 |
		       (pv->N2 & 0x7) << 19 | 8 << 4 | (pv->M2 & 7) << 4;
		pll2 = 0;
	}
	if (chip_version > 0x40 && reg1 >= 0x680508) { /* !nv40 */
		oldramdac580 = nv_rd32(priv, 0x680580);
		ramdac580 = new_ramdac580(reg1, single_stage, oldramdac580);
		if (oldramdac580 != ramdac580)
			oldpll1 = ~0;	/* force mismatch */
		if (single_stage)
			/* magic value used by nvidia in single stage mode */
			pll2 |= 0x011f;
	}
	if (chip_version > 0x70)
		/* magic bits set by the blob (but not the bios) on g71-73 */
		pll1 = (pll1 & 0x7fffffff) | (single_stage ? 0x4 : 0xc) << 28;

	if (oldpll1 == pll1 && oldpll2 == pll2)
		return;	/* already set */

	if (shift_powerctrl_1 >= 0) {
		saved_powerctrl_1 = nv_rd32(priv, 0x001584);
		nv_wr32(priv, 0x001584,
			(saved_powerctrl_1 & ~(0xf << shift_powerctrl_1)) |
			1 << shift_powerctrl_1);
	}

	if (chip_version >= 0x40) {
		int shift_c040 = 14;

		switch (reg1) {
		case 0x680504:
			shift_c040 += 2;
		case 0x680500:
			shift_c040 += 2;
		case 0x680520:
			shift_c040 += 2;
		case 0x680508:
			shift_c040 += 2;
		}

		savedc040 = nv_rd32(priv, 0xc040);
		if (shift_c040 != 14)
			nv_wr32(priv, 0xc040, savedc040 & ~(3 << shift_c040));
	}

	if (oldramdac580 != ramdac580)
		nv_wr32(priv, 0x680580, ramdac580);

	if (!nv3035)
		nv_wr32(priv, reg2, pll2);
	nv_wr32(priv, reg1, pll1);

	if (shift_powerctrl_1 >= 0)
		nv_wr32(priv, 0x001584, saved_powerctrl_1);
	if (chip_version >= 0x40)
		nv_wr32(priv, 0xc040, savedc040);
}

static void
setPLL_double_lowregs(struct nv04_clock_priv *priv, u32 NMNMreg,
		      struct nouveau_pll_vals *pv)
{
	/* When setting PLLs, there is a merry game of disabling and enabling
	 * various bits of hardware during the process. This function is a
	 * synthesis of six nv4x traces, nearly each card doing a subtly
	 * different thing. With luck all the necessary bits for each card are
	 * combined herein. Without luck it deviates from each card's formula
	 * so as to not work on any :)
	 */

	uint32_t Preg = NMNMreg - 4;
	bool mpll = Preg == 0x4020;
	uint32_t oldPval = nv_rd32(priv, Preg);
	uint32_t NMNM = pv->NM2 << 16 | pv->NM1;
	uint32_t Pval = (oldPval & (mpll ? ~(0x77 << 16) : ~(7 << 16))) |
			0xc << 28 | pv->log2P << 16;
	uint32_t saved4600 = 0;
	/* some cards have different maskc040s */
	uint32_t maskc040 = ~(3 << 14), savedc040;
	bool single_stage = !pv->NM2 || pv->N2 == pv->M2;

	if (nv_rd32(priv, NMNMreg) == NMNM && (oldPval & 0xc0070000) == Pval)
		return;

	if (Preg == 0x4000)
		maskc040 = ~0x333;
	if (Preg == 0x4058)
		maskc040 = ~(0xc << 24);

	if (mpll) {
		struct nvbios_pll info;
		uint8_t Pval2;

		if (nvbios_pll_parse(nouveau_bios(priv), Preg, &info))
			return;

		Pval2 = pv->log2P + info.bias_p;
		if (Pval2 > info.max_p)
			Pval2 = info.max_p;
		Pval |= 1 << 28 | Pval2 << 20;

		saved4600 = nv_rd32(priv, 0x4600);
		nv_wr32(priv, 0x4600, saved4600 | 8 << 28);
	}
	if (single_stage)
		Pval |= mpll ? 1 << 12 : 1 << 8;

	nv_wr32(priv, Preg, oldPval | 1 << 28);
	nv_wr32(priv, Preg, Pval & ~(4 << 28));
	if (mpll) {
		Pval |= 8 << 20;
		nv_wr32(priv, 0x4020, Pval & ~(0xc << 28));
		nv_wr32(priv, 0x4038, Pval & ~(0xc << 28));
	}

	savedc040 = nv_rd32(priv, 0xc040);
	nv_wr32(priv, 0xc040, savedc040 & maskc040);

	nv_wr32(priv, NMNMreg, NMNM);
	if (NMNMreg == 0x4024)
		nv_wr32(priv, 0x403c, NMNM);

	nv_wr32(priv, Preg, Pval);
	if (mpll) {
		Pval &= ~(8 << 20);
		nv_wr32(priv, 0x4020, Pval);
		nv_wr32(priv, 0x4038, Pval);
		nv_wr32(priv, 0x4600, saved4600);
	}

	nv_wr32(priv, 0xc040, savedc040);

	if (mpll) {
		nv_wr32(priv, 0x4020, Pval & ~(1 << 28));
		nv_wr32(priv, 0x4038, Pval & ~(1 << 28));
	}
}

int
nv04_clock_pll_set(struct nouveau_clock *clk, u32 type, u32 freq)
{
	struct nv04_clock_priv *priv = (void *)clk;
	struct nouveau_pll_vals pv;
	struct nvbios_pll info;
	int ret;

	ret = nvbios_pll_parse(nouveau_bios(priv), type > 0x405c ?
			       type : type - 4, &info);
	if (ret)
		return ret;

	ret = clk->pll_calc(clk, &info, freq, &pv);
	if (!ret)
		return ret;

	return clk->pll_prog(clk, type, &pv);
}

int
nv04_clock_pll_calc(struct nouveau_clock *clock, struct nvbios_pll *info,
		    int clk, struct nouveau_pll_vals *pv)
{
	int N1, M1, N2, M2, P;
	int ret = nv04_pll_calc(nv_subdev(clock), info, clk, &N1, &M1, &N2, &M2, &P);
	if (ret) {
		pv->refclk = info->refclk;
		pv->N1 = N1;
		pv->M1 = M1;
		pv->N2 = N2;
		pv->M2 = M2;
		pv->log2P = P;
	}
	return ret;
}

int
nv04_clock_pll_prog(struct nouveau_clock *clk, u32 reg1,
		    struct nouveau_pll_vals *pv)
{
	struct nv04_clock_priv *priv = (void *)clk;
	int cv = nouveau_bios(clk)->version.chip;

	if (cv == 0x30 || cv == 0x31 || cv == 0x35 || cv == 0x36 ||
	    cv >= 0x40) {
		if (reg1 > 0x405c)
			setPLL_double_highregs(priv, reg1, pv);
		else
			setPLL_double_lowregs(priv, reg1, pv);
	} else
		setPLL_single(priv, reg1, pv);

	return 0;
}

static int
nv04_clock_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nv04_clock_priv *priv;
	int ret;

	ret = nouveau_clock_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.pll_set = nv04_clock_pll_set;
	priv->base.pll_calc = nv04_clock_pll_calc;
	priv->base.pll_prog = nv04_clock_pll_prog;
	return 0;
}

struct nouveau_oclass
nv04_clock_oclass = {
	.handle = NV_SUBDEV(CLOCK, 0x04),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_clock_ctor,
		.dtor = _nouveau_clock_dtor,
		.init = _nouveau_clock_init,
		.fini = _nouveau_clock_fini,
	},
};
