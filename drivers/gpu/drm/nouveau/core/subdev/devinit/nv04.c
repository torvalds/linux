/*
 * Copyright (C) 2010 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <subdev/vga.h>

#include "fbmem.h"
#include "nv04.h"

static void
nv04_devinit_meminit(struct nouveau_devinit *devinit)
{
	struct nv04_devinit_priv *priv = (void *)devinit;
	u32 patt = 0xdeadbeef;
	struct io_mapping *fb;
	int i;

	/* Map the framebuffer aperture */
	fb = fbmem_init(nv_device(priv));
	if (!fb) {
		nv_error(priv, "failed to map fb\n");
		return;
	}

	/* Sequencer and refresh off */
	nv_wrvgas(priv, 0, 1, nv_rdvgas(priv, 0, 1) | 0x20);
	nv_mask(priv, NV04_PFB_DEBUG_0, 0, NV04_PFB_DEBUG_0_REFRESH_OFF);

	nv_mask(priv, NV04_PFB_BOOT_0, ~0,
		      NV04_PFB_BOOT_0_RAM_AMOUNT_16MB |
		      NV04_PFB_BOOT_0_RAM_WIDTH_128 |
		      NV04_PFB_BOOT_0_RAM_TYPE_SGRAM_16MBIT);

	for (i = 0; i < 4; i++)
		fbmem_poke(fb, 4 * i, patt);

	fbmem_poke(fb, 0x400000, patt + 1);

	if (fbmem_peek(fb, 0) == patt + 1) {
		nv_mask(priv, NV04_PFB_BOOT_0,
			      NV04_PFB_BOOT_0_RAM_TYPE,
			      NV04_PFB_BOOT_0_RAM_TYPE_SDRAM_16MBIT);
		nv_mask(priv, NV04_PFB_DEBUG_0,
			      NV04_PFB_DEBUG_0_REFRESH_OFF, 0);

		for (i = 0; i < 4; i++)
			fbmem_poke(fb, 4 * i, patt);

		if ((fbmem_peek(fb, 0xc) & 0xffff) != (patt & 0xffff))
			nv_mask(priv, NV04_PFB_BOOT_0,
				      NV04_PFB_BOOT_0_RAM_WIDTH_128 |
				      NV04_PFB_BOOT_0_RAM_AMOUNT,
				      NV04_PFB_BOOT_0_RAM_AMOUNT_8MB);
	} else
	if ((fbmem_peek(fb, 0xc) & 0xffff0000) != (patt & 0xffff0000)) {
		nv_mask(priv, NV04_PFB_BOOT_0,
			      NV04_PFB_BOOT_0_RAM_WIDTH_128 |
			      NV04_PFB_BOOT_0_RAM_AMOUNT,
			      NV04_PFB_BOOT_0_RAM_AMOUNT_4MB);
	} else
	if (fbmem_peek(fb, 0) != patt) {
		if (fbmem_readback(fb, 0x800000, patt))
			nv_mask(priv, NV04_PFB_BOOT_0,
				      NV04_PFB_BOOT_0_RAM_AMOUNT,
				      NV04_PFB_BOOT_0_RAM_AMOUNT_8MB);
		else
			nv_mask(priv, NV04_PFB_BOOT_0,
				      NV04_PFB_BOOT_0_RAM_AMOUNT,
				      NV04_PFB_BOOT_0_RAM_AMOUNT_4MB);

		nv_mask(priv, NV04_PFB_BOOT_0, NV04_PFB_BOOT_0_RAM_TYPE,
			      NV04_PFB_BOOT_0_RAM_TYPE_SGRAM_8MBIT);
	} else
	if (!fbmem_readback(fb, 0x800000, patt)) {
		nv_mask(priv, NV04_PFB_BOOT_0, NV04_PFB_BOOT_0_RAM_AMOUNT,
			      NV04_PFB_BOOT_0_RAM_AMOUNT_8MB);

	}

	/* Refresh on, sequencer on */
	nv_mask(priv, NV04_PFB_DEBUG_0, NV04_PFB_DEBUG_0_REFRESH_OFF, 0);
	nv_wrvgas(priv, 0, 1, nv_rdvgas(priv, 0, 1) & ~0x20);
	fbmem_fini(fb);
}

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

void
setPLL_single(struct nouveau_devinit *devinit, u32 reg,
	      struct nouveau_pll_vals *pv)
{
	int chip_version = nouveau_bios(devinit)->version.chip;
	uint32_t oldpll = nv_rd32(devinit, reg);
	int oldN = (oldpll >> 8) & 0xff, oldM = oldpll & 0xff;
	uint32_t pll = (oldpll & 0xfff80000) | pv->log2P << 16 | pv->NM1;
	uint32_t saved_powerctrl_1 = 0;
	int shift_powerctrl_1 = powerctrl_1_shift(chip_version, reg);

	if (oldpll == pll)
		return;	/* already set */

	if (shift_powerctrl_1 >= 0) {
		saved_powerctrl_1 = nv_rd32(devinit, 0x001584);
		nv_wr32(devinit, 0x001584,
			(saved_powerctrl_1 & ~(0xf << shift_powerctrl_1)) |
			1 << shift_powerctrl_1);
	}

	if (oldM && pv->M1 && (oldN / oldM < pv->N1 / pv->M1))
		/* upclock -- write new post divider first */
		nv_wr32(devinit, reg, pv->log2P << 16 | (oldpll & 0xffff));
	else
		/* downclock -- write new NM first */
		nv_wr32(devinit, reg, (oldpll & 0xffff0000) | pv->NM1);

	if ((chip_version < 0x17 || chip_version == 0x1a) &&
	    chip_version != 0x11)
		/* wait a bit on older chips */
		msleep(64);
	nv_rd32(devinit, reg);

	/* then write the other half as well */
	nv_wr32(devinit, reg, pll);

	if (shift_powerctrl_1 >= 0)
		nv_wr32(devinit, 0x001584, saved_powerctrl_1);
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

void
setPLL_double_highregs(struct nouveau_devinit *devinit, u32 reg1,
		       struct nouveau_pll_vals *pv)
{
	int chip_version = nouveau_bios(devinit)->version.chip;
	bool nv3035 = chip_version == 0x30 || chip_version == 0x35;
	uint32_t reg2 = reg1 + ((reg1 == 0x680520) ? 0x5c : 0x70);
	uint32_t oldpll1 = nv_rd32(devinit, reg1);
	uint32_t oldpll2 = !nv3035 ? nv_rd32(devinit, reg2) : 0;
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
		oldramdac580 = nv_rd32(devinit, 0x680580);
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
		saved_powerctrl_1 = nv_rd32(devinit, 0x001584);
		nv_wr32(devinit, 0x001584,
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

		savedc040 = nv_rd32(devinit, 0xc040);
		if (shift_c040 != 14)
			nv_wr32(devinit, 0xc040, savedc040 & ~(3 << shift_c040));
	}

	if (oldramdac580 != ramdac580)
		nv_wr32(devinit, 0x680580, ramdac580);

	if (!nv3035)
		nv_wr32(devinit, reg2, pll2);
	nv_wr32(devinit, reg1, pll1);

	if (shift_powerctrl_1 >= 0)
		nv_wr32(devinit, 0x001584, saved_powerctrl_1);
	if (chip_version >= 0x40)
		nv_wr32(devinit, 0xc040, savedc040);
}

void
setPLL_double_lowregs(struct nouveau_devinit *devinit, u32 NMNMreg,
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
	uint32_t oldPval = nv_rd32(devinit, Preg);
	uint32_t NMNM = pv->NM2 << 16 | pv->NM1;
	uint32_t Pval = (oldPval & (mpll ? ~(0x77 << 16) : ~(7 << 16))) |
			0xc << 28 | pv->log2P << 16;
	uint32_t saved4600 = 0;
	/* some cards have different maskc040s */
	uint32_t maskc040 = ~(3 << 14), savedc040;
	bool single_stage = !pv->NM2 || pv->N2 == pv->M2;

	if (nv_rd32(devinit, NMNMreg) == NMNM && (oldPval & 0xc0070000) == Pval)
		return;

	if (Preg == 0x4000)
		maskc040 = ~0x333;
	if (Preg == 0x4058)
		maskc040 = ~(0xc << 24);

	if (mpll) {
		struct nvbios_pll info;
		uint8_t Pval2;

		if (nvbios_pll_parse(nouveau_bios(devinit), Preg, &info))
			return;

		Pval2 = pv->log2P + info.bias_p;
		if (Pval2 > info.max_p)
			Pval2 = info.max_p;
		Pval |= 1 << 28 | Pval2 << 20;

		saved4600 = nv_rd32(devinit, 0x4600);
		nv_wr32(devinit, 0x4600, saved4600 | 8 << 28);
	}
	if (single_stage)
		Pval |= mpll ? 1 << 12 : 1 << 8;

	nv_wr32(devinit, Preg, oldPval | 1 << 28);
	nv_wr32(devinit, Preg, Pval & ~(4 << 28));
	if (mpll) {
		Pval |= 8 << 20;
		nv_wr32(devinit, 0x4020, Pval & ~(0xc << 28));
		nv_wr32(devinit, 0x4038, Pval & ~(0xc << 28));
	}

	savedc040 = nv_rd32(devinit, 0xc040);
	nv_wr32(devinit, 0xc040, savedc040 & maskc040);

	nv_wr32(devinit, NMNMreg, NMNM);
	if (NMNMreg == 0x4024)
		nv_wr32(devinit, 0x403c, NMNM);

	nv_wr32(devinit, Preg, Pval);
	if (mpll) {
		Pval &= ~(8 << 20);
		nv_wr32(devinit, 0x4020, Pval);
		nv_wr32(devinit, 0x4038, Pval);
		nv_wr32(devinit, 0x4600, saved4600);
	}

	nv_wr32(devinit, 0xc040, savedc040);

	if (mpll) {
		nv_wr32(devinit, 0x4020, Pval & ~(1 << 28));
		nv_wr32(devinit, 0x4038, Pval & ~(1 << 28));
	}
}

int
nv04_devinit_pll_set(struct nouveau_devinit *devinit, u32 type, u32 freq)
{
	struct nouveau_bios *bios = nouveau_bios(devinit);
	struct nouveau_pll_vals pv;
	struct nvbios_pll info;
	int cv = bios->version.chip;
	int N1, M1, N2, M2, P;
	int ret;

	ret = nvbios_pll_parse(bios, type > 0x405c ? type : type - 4, &info);
	if (ret)
		return ret;

	ret = nv04_pll_calc(nv_subdev(devinit), &info, freq,
			   &N1, &M1, &N2, &M2, &P);
	if (!ret)
		return -EINVAL;

	pv.refclk = info.refclk;
	pv.N1 = N1;
	pv.M1 = M1;
	pv.N2 = N2;
	pv.M2 = M2;
	pv.log2P = P;

	if (cv == 0x30 || cv == 0x31 || cv == 0x35 || cv == 0x36 ||
	    cv >= 0x40) {
		if (type > 0x405c)
			setPLL_double_highregs(devinit, type, &pv);
		else
			setPLL_double_lowregs(devinit, type, &pv);
	} else
		setPLL_single(devinit, type, &pv);

	return 0;
}

int
nv04_devinit_fini(struct nouveau_object *object, bool suspend)
{
	struct nv04_devinit_priv *priv = (void *)object;
	int ret;

	/* make i2c busses accessible */
	nv_mask(priv, 0x000200, 0x00000001, 0x00000001);

	ret = nouveau_devinit_fini(&priv->base, suspend);
	if (ret)
		return ret;

	/* unslave crtcs */
	if (priv->owner < 0)
		priv->owner = nv_rdvgaowner(priv);
	nv_wrvgaowner(priv, 0);

	return 0;
}

int
nv04_devinit_init(struct nouveau_object *object)
{
	struct nv04_devinit_priv *priv = (void *)object;

	if (!priv->base.post) {
		u32 htotal = nv_rdvgac(priv, 0, 0x06);
		htotal |= (nv_rdvgac(priv, 0, 0x07) & 0x01) << 8;
		htotal |= (nv_rdvgac(priv, 0, 0x07) & 0x20) << 4;
		htotal |= (nv_rdvgac(priv, 0, 0x25) & 0x01) << 10;
		htotal |= (nv_rdvgac(priv, 0, 0x41) & 0x01) << 11;
		if (!htotal) {
			nv_info(priv, "adaptor not initialised\n");
			priv->base.post = true;
		}
	}

	return nouveau_devinit_init(&priv->base);
}

void
nv04_devinit_dtor(struct nouveau_object *object)
{
	struct nv04_devinit_priv *priv = (void *)object;

	/* restore vga owner saved at first init */
	nv_wrvgaowner(priv, priv->owner);

	nouveau_devinit_destroy(&priv->base);
}

int
nv04_devinit_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nv04_devinit_priv *priv;
	int ret;

	ret = nouveau_devinit_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->owner = -1;
	return 0;
}

struct nouveau_oclass *
nv04_devinit_oclass = &(struct nouveau_devinit_impl) {
	.base.handle = NV_SUBDEV(DEVINIT, 0x04),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_devinit_ctor,
		.dtor = nv04_devinit_dtor,
		.init = nv04_devinit_init,
		.fini = nv04_devinit_fini,
	},
	.meminit = nv04_devinit_meminit,
	.pll_set = nv04_devinit_pll_set,
	.post = nvbios_init,
}.base;
