/*
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
 * Copyright 2007-2009 Stuart Bennett
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
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_hw.h"

#define CHIPSET_NFORCE 0x01a0
#define CHIPSET_NFORCE2 0x01f0

/*
 * misc hw access wrappers/control functions
 */

void
NVWriteVgaSeq(struct drm_device *dev, int head, uint8_t index, uint8_t value)
{
	NVWritePRMVIO(dev, head, NV_PRMVIO_SRX, index);
	NVWritePRMVIO(dev, head, NV_PRMVIO_SR, value);
}

uint8_t
NVReadVgaSeq(struct drm_device *dev, int head, uint8_t index)
{
	NVWritePRMVIO(dev, head, NV_PRMVIO_SRX, index);
	return NVReadPRMVIO(dev, head, NV_PRMVIO_SR);
}

void
NVWriteVgaGr(struct drm_device *dev, int head, uint8_t index, uint8_t value)
{
	NVWritePRMVIO(dev, head, NV_PRMVIO_GRX, index);
	NVWritePRMVIO(dev, head, NV_PRMVIO_GX, value);
}

uint8_t
NVReadVgaGr(struct drm_device *dev, int head, uint8_t index)
{
	NVWritePRMVIO(dev, head, NV_PRMVIO_GRX, index);
	return NVReadPRMVIO(dev, head, NV_PRMVIO_GX);
}

/* CR44 takes values 0 (head A), 3 (head B) and 4 (heads tied)
 * it affects only the 8 bit vga io regs, which we access using mmio at
 * 0xc{0,2}3c*, 0x60{1,3}3*, and 0x68{1,3}3d*
 * in general, the set value of cr44 does not matter: reg access works as
 * expected and values can be set for the appropriate head by using a 0x2000
 * offset as required
 * however:
 * a) pre nv40, the head B range of PRMVIO regs at 0xc23c* was not exposed and
 *    cr44 must be set to 0 or 3 for accessing values on the correct head
 *    through the common 0xc03c* addresses
 * b) in tied mode (4) head B is programmed to the values set on head A, and
 *    access using the head B addresses can have strange results, ergo we leave
 *    tied mode in init once we know to what cr44 should be restored on exit
 *
 * the owner parameter is slightly abused:
 * 0 and 1 are treated as head values and so the set value is (owner * 3)
 * other values are treated as literal values to set
 */
void
NVSetOwner(struct drm_device *dev, int owner)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (owner == 1)
		owner *= 3;

	if (dev_priv->chipset == 0x11) {
		/* This might seem stupid, but the blob does it and
		 * omitting it often locks the system up.
		 */
		NVReadVgaCrtc(dev, 0, NV_CIO_SR_LOCK_INDEX);
		NVReadVgaCrtc(dev, 1, NV_CIO_SR_LOCK_INDEX);
	}

	/* CR44 is always changed on CRTC0 */
	NVWriteVgaCrtc(dev, 0, NV_CIO_CRE_44, owner);

	if (dev_priv->chipset == 0x11) {	/* set me harder */
		NVWriteVgaCrtc(dev, 0, NV_CIO_CRE_2E, owner);
		NVWriteVgaCrtc(dev, 0, NV_CIO_CRE_2E, owner);
	}
}

void
NVBlankScreen(struct drm_device *dev, int head, bool blank)
{
	unsigned char seq1;

	if (nv_two_heads(dev))
		NVSetOwner(dev, head);

	seq1 = NVReadVgaSeq(dev, head, NV_VIO_SR_CLOCK_INDEX);

	NVVgaSeqReset(dev, head, true);
	if (blank)
		NVWriteVgaSeq(dev, head, NV_VIO_SR_CLOCK_INDEX, seq1 | 0x20);
	else
		NVWriteVgaSeq(dev, head, NV_VIO_SR_CLOCK_INDEX, seq1 & ~0x20);
	NVVgaSeqReset(dev, head, false);
}

/*
 * PLL setting
 */

static int
powerctrl_1_shift(int chip_version, int reg)
{
	int shift = -4;

	if (chip_version < 0x17 || chip_version == 0x1a || chip_version == 0x20)
		return shift;

	switch (reg) {
	case NV_RAMDAC_VPLL2:
		shift += 4;
	case NV_PRAMDAC_VPLL_COEFF:
		shift += 4;
	case NV_PRAMDAC_MPLL_COEFF:
		shift += 4;
	case NV_PRAMDAC_NVPLL_COEFF:
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
setPLL_single(struct drm_device *dev, uint32_t reg, struct nouveau_pll_vals *pv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int chip_version = dev_priv->vbios->chip_version;
	uint32_t oldpll = NVReadRAMDAC(dev, 0, reg);
	int oldN = (oldpll >> 8) & 0xff, oldM = oldpll & 0xff;
	uint32_t pll = (oldpll & 0xfff80000) | pv->log2P << 16 | pv->NM1;
	uint32_t saved_powerctrl_1 = 0;
	int shift_powerctrl_1 = powerctrl_1_shift(chip_version, reg);

	if (oldpll == pll)
		return;	/* already set */

	if (shift_powerctrl_1 >= 0) {
		saved_powerctrl_1 = nvReadMC(dev, NV_PBUS_POWERCTRL_1);
		nvWriteMC(dev, NV_PBUS_POWERCTRL_1,
			(saved_powerctrl_1 & ~(0xf << shift_powerctrl_1)) |
			1 << shift_powerctrl_1);
	}

	if (oldM && pv->M1 && (oldN / oldM < pv->N1 / pv->M1))
		/* upclock -- write new post divider first */
		NVWriteRAMDAC(dev, 0, reg, pv->log2P << 16 | (oldpll & 0xffff));
	else
		/* downclock -- write new NM first */
		NVWriteRAMDAC(dev, 0, reg, (oldpll & 0xffff0000) | pv->NM1);

	if (chip_version < 0x17 && chip_version != 0x11)
		/* wait a bit on older chips */
		msleep(64);
	NVReadRAMDAC(dev, 0, reg);

	/* then write the other half as well */
	NVWriteRAMDAC(dev, 0, reg, pll);

	if (shift_powerctrl_1 >= 0)
		nvWriteMC(dev, NV_PBUS_POWERCTRL_1, saved_powerctrl_1);
}

static uint32_t
new_ramdac580(uint32_t reg1, bool ss, uint32_t ramdac580)
{
	bool head_a = (reg1 == NV_PRAMDAC_VPLL_COEFF);

	if (ss)	/* single stage pll mode */
		ramdac580 |= head_a ? NV_RAMDAC_580_VPLL1_ACTIVE :
				      NV_RAMDAC_580_VPLL2_ACTIVE;
	else
		ramdac580 &= head_a ? ~NV_RAMDAC_580_VPLL1_ACTIVE :
				      ~NV_RAMDAC_580_VPLL2_ACTIVE;

	return ramdac580;
}

static void
setPLL_double_highregs(struct drm_device *dev, uint32_t reg1,
		       struct nouveau_pll_vals *pv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int chip_version = dev_priv->vbios->chip_version;
	bool nv3035 = chip_version == 0x30 || chip_version == 0x35;
	uint32_t reg2 = reg1 + ((reg1 == NV_RAMDAC_VPLL2) ? 0x5c : 0x70);
	uint32_t oldpll1 = NVReadRAMDAC(dev, 0, reg1);
	uint32_t oldpll2 = !nv3035 ? NVReadRAMDAC(dev, 0, reg2) : 0;
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
	if (chip_version > 0x40 && reg1 >= NV_PRAMDAC_VPLL_COEFF) { /* !nv40 */
		oldramdac580 = NVReadRAMDAC(dev, 0, NV_PRAMDAC_580);
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
		saved_powerctrl_1 = nvReadMC(dev, NV_PBUS_POWERCTRL_1);
		nvWriteMC(dev, NV_PBUS_POWERCTRL_1,
			(saved_powerctrl_1 & ~(0xf << shift_powerctrl_1)) |
			1 << shift_powerctrl_1);
	}

	if (chip_version >= 0x40) {
		int shift_c040 = 14;

		switch (reg1) {
		case NV_PRAMDAC_MPLL_COEFF:
			shift_c040 += 2;
		case NV_PRAMDAC_NVPLL_COEFF:
			shift_c040 += 2;
		case NV_RAMDAC_VPLL2:
			shift_c040 += 2;
		case NV_PRAMDAC_VPLL_COEFF:
			shift_c040 += 2;
		}

		savedc040 = nvReadMC(dev, 0xc040);
		if (shift_c040 != 14)
			nvWriteMC(dev, 0xc040, savedc040 & ~(3 << shift_c040));
	}

	if (oldramdac580 != ramdac580)
		NVWriteRAMDAC(dev, 0, NV_PRAMDAC_580, ramdac580);

	if (!nv3035)
		NVWriteRAMDAC(dev, 0, reg2, pll2);
	NVWriteRAMDAC(dev, 0, reg1, pll1);

	if (shift_powerctrl_1 >= 0)
		nvWriteMC(dev, NV_PBUS_POWERCTRL_1, saved_powerctrl_1);
	if (chip_version >= 0x40)
		nvWriteMC(dev, 0xc040, savedc040);
}

static void
setPLL_double_lowregs(struct drm_device *dev, uint32_t NMNMreg,
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
	uint32_t oldPval = nvReadMC(dev, Preg);
	uint32_t NMNM = pv->NM2 << 16 | pv->NM1;
	uint32_t Pval = (oldPval & (mpll ? ~(0x11 << 16) : ~(1 << 16))) |
			0xc << 28 | pv->log2P << 16;
	uint32_t saved4600 = 0;
	/* some cards have different maskc040s */
	uint32_t maskc040 = ~(3 << 14), savedc040;
	bool single_stage = !pv->NM2 || pv->N2 == pv->M2;

	if (nvReadMC(dev, NMNMreg) == NMNM && (oldPval & 0xc0070000) == Pval)
		return;

	if (Preg == 0x4000)
		maskc040 = ~0x333;
	if (Preg == 0x4058)
		maskc040 = ~(0xc << 24);

	if (mpll) {
		struct pll_lims pll_lim;
		uint8_t Pval2;

		if (get_pll_limits(dev, Preg, &pll_lim))
			return;

		Pval2 = pv->log2P + pll_lim.log2p_bias;
		if (Pval2 > pll_lim.max_log2p)
			Pval2 = pll_lim.max_log2p;
		Pval |= 1 << 28 | Pval2 << 20;

		saved4600 = nvReadMC(dev, 0x4600);
		nvWriteMC(dev, 0x4600, saved4600 | 8 << 28);
	}
	if (single_stage)
		Pval |= mpll ? 1 << 12 : 1 << 8;

	nvWriteMC(dev, Preg, oldPval | 1 << 28);
	nvWriteMC(dev, Preg, Pval & ~(4 << 28));
	if (mpll) {
		Pval |= 8 << 20;
		nvWriteMC(dev, 0x4020, Pval & ~(0xc << 28));
		nvWriteMC(dev, 0x4038, Pval & ~(0xc << 28));
	}

	savedc040 = nvReadMC(dev, 0xc040);
	nvWriteMC(dev, 0xc040, savedc040 & maskc040);

	nvWriteMC(dev, NMNMreg, NMNM);
	if (NMNMreg == 0x4024)
		nvWriteMC(dev, 0x403c, NMNM);

	nvWriteMC(dev, Preg, Pval);
	if (mpll) {
		Pval &= ~(8 << 20);
		nvWriteMC(dev, 0x4020, Pval);
		nvWriteMC(dev, 0x4038, Pval);
		nvWriteMC(dev, 0x4600, saved4600);
	}

	nvWriteMC(dev, 0xc040, savedc040);

	if (mpll) {
		nvWriteMC(dev, 0x4020, Pval & ~(1 << 28));
		nvWriteMC(dev, 0x4038, Pval & ~(1 << 28));
	}
}

void
nouveau_hw_setpll(struct drm_device *dev, uint32_t reg1,
		  struct nouveau_pll_vals *pv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int cv = dev_priv->vbios->chip_version;

	if (cv == 0x30 || cv == 0x31 || cv == 0x35 || cv == 0x36 ||
	    cv >= 0x40) {
		if (reg1 > 0x405c)
			setPLL_double_highregs(dev, reg1, pv);
		else
			setPLL_double_lowregs(dev, reg1, pv);
	} else
		setPLL_single(dev, reg1, pv);
}

/*
 * PLL getting
 */

static void
nouveau_hw_decode_pll(struct drm_device *dev, uint32_t reg1, uint32_t pll1,
		      uint32_t pll2, struct nouveau_pll_vals *pllvals)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/* to force parsing as single stage (i.e. nv40 vplls) pass pll2 as 0 */

	/* log2P is & 0x7 as never more than 7, and nv30/35 only uses 3 bits */
	pllvals->log2P = (pll1 >> 16) & 0x7;
	pllvals->N2 = pllvals->M2 = 1;

	if (reg1 <= 0x405c) {
		pllvals->NM1 = pll2 & 0xffff;
		/* single stage NVPLL and VPLLs use 1 << 8, MPLL uses 1 << 12 */
		if (!(pll1 & 0x1100))
			pllvals->NM2 = pll2 >> 16;
	} else {
		pllvals->NM1 = pll1 & 0xffff;
		if (nv_two_reg_pll(dev) && pll2 & NV31_RAMDAC_ENABLE_VCO2)
			pllvals->NM2 = pll2 & 0xffff;
		else if (dev_priv->chipset == 0x30 || dev_priv->chipset == 0x35) {
			pllvals->M1 &= 0xf; /* only 4 bits */
			if (pll1 & NV30_RAMDAC_ENABLE_VCO2) {
				pllvals->M2 = (pll1 >> 4) & 0x7;
				pllvals->N2 = ((pll1 >> 21) & 0x18) |
					      ((pll1 >> 19) & 0x7);
			}
		}
	}
}

int
nouveau_hw_get_pllvals(struct drm_device *dev, enum pll_types plltype,
		       struct nouveau_pll_vals *pllvals)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	const uint32_t nv04_regs[MAX_PLL_TYPES] = { NV_PRAMDAC_NVPLL_COEFF,
						    NV_PRAMDAC_MPLL_COEFF,
						    NV_PRAMDAC_VPLL_COEFF,
						    NV_RAMDAC_VPLL2 };
	const uint32_t nv40_regs[MAX_PLL_TYPES] = { 0x4000,
						    0x4020,
						    NV_PRAMDAC_VPLL_COEFF,
						    NV_RAMDAC_VPLL2 };
	uint32_t reg1, pll1, pll2 = 0;
	struct pll_lims pll_lim;
	int ret;

	if (dev_priv->card_type < NV_40)
		reg1 = nv04_regs[plltype];
	else
		reg1 = nv40_regs[plltype];

	pll1 = nvReadMC(dev, reg1);

	if (reg1 <= 0x405c)
		pll2 = nvReadMC(dev, reg1 + 4);
	else if (nv_two_reg_pll(dev)) {
		uint32_t reg2 = reg1 + (reg1 == NV_RAMDAC_VPLL2 ? 0x5c : 0x70);

		pll2 = nvReadMC(dev, reg2);
	}

	if (dev_priv->card_type == 0x40 && reg1 >= NV_PRAMDAC_VPLL_COEFF) {
		uint32_t ramdac580 = NVReadRAMDAC(dev, 0, NV_PRAMDAC_580);

		/* check whether vpll has been forced into single stage mode */
		if (reg1 == NV_PRAMDAC_VPLL_COEFF) {
			if (ramdac580 & NV_RAMDAC_580_VPLL1_ACTIVE)
				pll2 = 0;
		} else
			if (ramdac580 & NV_RAMDAC_580_VPLL2_ACTIVE)
				pll2 = 0;
	}

	nouveau_hw_decode_pll(dev, reg1, pll1, pll2, pllvals);

	ret = get_pll_limits(dev, plltype, &pll_lim);
	if (ret)
		return ret;

	pllvals->refclk = pll_lim.refclk;

	return 0;
}

int
nouveau_hw_pllvals_to_clk(struct nouveau_pll_vals *pv)
{
	/* Avoid divide by zero if called at an inappropriate time */
	if (!pv->M1 || !pv->M2)
		return 0;

	return pv->N1 * pv->N2 * pv->refclk / (pv->M1 * pv->M2) >> pv->log2P;
}

int
nouveau_hw_get_clock(struct drm_device *dev, enum pll_types plltype)
{
	struct nouveau_pll_vals pllvals;

	if (plltype == MPLL && (dev->pci_device & 0x0ff0) == CHIPSET_NFORCE) {
		uint32_t mpllP;

		pci_read_config_dword(pci_get_bus_and_slot(0, 3), 0x6c, &mpllP);
		if (!mpllP)
			mpllP = 4;

		return 400000 / mpllP;
	} else
	if (plltype == MPLL && (dev->pci_device & 0xff0) == CHIPSET_NFORCE2) {
		uint32_t clock;

		pci_read_config_dword(pci_get_bus_and_slot(0, 5), 0x4c, &clock);
		return clock;
	}

	nouveau_hw_get_pllvals(dev, plltype, &pllvals);

	return nouveau_hw_pllvals_to_clk(&pllvals);
}

static void
nouveau_hw_fix_bad_vpll(struct drm_device *dev, int head)
{
	/* the vpll on an unused head can come up with a random value, way
	 * beyond the pll limits.  for some reason this causes the chip to
	 * lock up when reading the dac palette regs, so set a valid pll here
	 * when such a condition detected.  only seen on nv11 to date
	 */

	struct pll_lims pll_lim;
	struct nouveau_pll_vals pv;
	uint32_t pllreg = head ? NV_RAMDAC_VPLL2 : NV_PRAMDAC_VPLL_COEFF;

	if (get_pll_limits(dev, head ? VPLL2 : VPLL1, &pll_lim))
		return;
	nouveau_hw_get_pllvals(dev, head ? VPLL2 : VPLL1, &pv);

	if (pv.M1 >= pll_lim.vco1.min_m && pv.M1 <= pll_lim.vco1.max_m &&
	    pv.N1 >= pll_lim.vco1.min_n && pv.N1 <= pll_lim.vco1.max_n &&
	    pv.log2P <= pll_lim.max_log2p)
		return;

	NV_WARN(dev, "VPLL %d outwith limits, attempting to fix\n", head + 1);

	/* set lowest clock within static limits */
	pv.M1 = pll_lim.vco1.max_m;
	pv.N1 = pll_lim.vco1.min_n;
	pv.log2P = pll_lim.max_usable_log2p;
	nouveau_hw_setpll(dev, pllreg, &pv);
}

/*
 * vga font save/restore
 */

static void nouveau_vga_font_io(struct drm_device *dev,
				void __iomem *iovram,
				bool save, unsigned plane)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned i;

	NVWriteVgaSeq(dev, 0, NV_VIO_SR_PLANE_MASK_INDEX, 1 << plane);
	NVWriteVgaGr(dev, 0, NV_VIO_GX_READ_MAP_INDEX, plane);
	for (i = 0; i < 16384; i++) {
		if (save) {
			dev_priv->saved_vga_font[plane][i] =
					ioread32_native(iovram + i * 4);
		} else {
			iowrite32_native(dev_priv->saved_vga_font[plane][i],
							iovram + i * 4);
		}
	}
}

void
nouveau_hw_save_vga_fonts(struct drm_device *dev, bool save)
{
	uint8_t misc, gr4, gr5, gr6, seq2, seq4;
	bool graphicsmode;
	unsigned plane;
	void __iomem *iovram;

	if (nv_two_heads(dev))
		NVSetOwner(dev, 0);

	NVSetEnablePalette(dev, 0, true);
	graphicsmode = NVReadVgaAttr(dev, 0, NV_CIO_AR_MODE_INDEX) & 1;
	NVSetEnablePalette(dev, 0, false);

	if (graphicsmode) /* graphics mode => framebuffer => no need to save */
		return;

	NV_INFO(dev, "%sing VGA fonts\n", save ? "Sav" : "Restor");

	/* map first 64KiB of VRAM, holds VGA fonts etc */
	iovram = ioremap(pci_resource_start(dev->pdev, 1), 65536);
	if (!iovram) {
		NV_ERROR(dev, "Failed to map VRAM, "
					"cannot save/restore VGA fonts.\n");
		return;
	}

	if (nv_two_heads(dev))
		NVBlankScreen(dev, 1, true);
	NVBlankScreen(dev, 0, true);

	/* save control regs */
	misc = NVReadPRMVIO(dev, 0, NV_PRMVIO_MISC__READ);
	seq2 = NVReadVgaSeq(dev, 0, NV_VIO_SR_PLANE_MASK_INDEX);
	seq4 = NVReadVgaSeq(dev, 0, NV_VIO_SR_MEM_MODE_INDEX);
	gr4 = NVReadVgaGr(dev, 0, NV_VIO_GX_READ_MAP_INDEX);
	gr5 = NVReadVgaGr(dev, 0, NV_VIO_GX_MODE_INDEX);
	gr6 = NVReadVgaGr(dev, 0, NV_VIO_GX_MISC_INDEX);

	NVWritePRMVIO(dev, 0, NV_PRMVIO_MISC__WRITE, 0x67);
	NVWriteVgaSeq(dev, 0, NV_VIO_SR_MEM_MODE_INDEX, 0x6);
	NVWriteVgaGr(dev, 0, NV_VIO_GX_MODE_INDEX, 0x0);
	NVWriteVgaGr(dev, 0, NV_VIO_GX_MISC_INDEX, 0x5);

	/* store font in planes 0..3 */
	for (plane = 0; plane < 4; plane++)
		nouveau_vga_font_io(dev, iovram, save, plane);

	/* restore control regs */
	NVWritePRMVIO(dev, 0, NV_PRMVIO_MISC__WRITE, misc);
	NVWriteVgaGr(dev, 0, NV_VIO_GX_READ_MAP_INDEX, gr4);
	NVWriteVgaGr(dev, 0, NV_VIO_GX_MODE_INDEX, gr5);
	NVWriteVgaGr(dev, 0, NV_VIO_GX_MISC_INDEX, gr6);
	NVWriteVgaSeq(dev, 0, NV_VIO_SR_PLANE_MASK_INDEX, seq2);
	NVWriteVgaSeq(dev, 0, NV_VIO_SR_MEM_MODE_INDEX, seq4);

	if (nv_two_heads(dev))
		NVBlankScreen(dev, 1, false);
	NVBlankScreen(dev, 0, false);

	iounmap(iovram);
}

/*
 * mode state save/load
 */

static void
rd_cio_state(struct drm_device *dev, int head,
	     struct nv04_crtc_reg *crtcstate, int index)
{
	crtcstate->CRTC[index] = NVReadVgaCrtc(dev, head, index);
}

static void
wr_cio_state(struct drm_device *dev, int head,
	     struct nv04_crtc_reg *crtcstate, int index)
{
	NVWriteVgaCrtc(dev, head, index, crtcstate->CRTC[index]);
}

static void
nv_save_state_ramdac(struct drm_device *dev, int head,
		     struct nv04_mode_state *state)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv04_crtc_reg *regp = &state->crtc_reg[head];
	int i;

	if (dev_priv->card_type >= NV_10)
		regp->nv10_cursync = NVReadRAMDAC(dev, head, NV_RAMDAC_NV10_CURSYNC);

	nouveau_hw_get_pllvals(dev, head ? VPLL2 : VPLL1, &regp->pllvals);
	state->pllsel = NVReadRAMDAC(dev, 0, NV_PRAMDAC_PLL_COEFF_SELECT);
	if (nv_two_heads(dev))
		state->sel_clk = NVReadRAMDAC(dev, 0, NV_PRAMDAC_SEL_CLK);
	if (dev_priv->chipset == 0x11)
		regp->dither = NVReadRAMDAC(dev, head, NV_RAMDAC_DITHER_NV11);

	regp->ramdac_gen_ctrl = NVReadRAMDAC(dev, head, NV_PRAMDAC_GENERAL_CONTROL);

	if (nv_gf4_disp_arch(dev))
		regp->ramdac_630 = NVReadRAMDAC(dev, head, NV_PRAMDAC_630);
	if (dev_priv->chipset >= 0x30)
		regp->ramdac_634 = NVReadRAMDAC(dev, head, NV_PRAMDAC_634);

	regp->tv_setup = NVReadRAMDAC(dev, head, NV_PRAMDAC_TV_SETUP);
	regp->tv_vtotal = NVReadRAMDAC(dev, head, NV_PRAMDAC_TV_VTOTAL);
	regp->tv_vskew = NVReadRAMDAC(dev, head, NV_PRAMDAC_TV_VSKEW);
	regp->tv_vsync_delay = NVReadRAMDAC(dev, head, NV_PRAMDAC_TV_VSYNC_DELAY);
	regp->tv_htotal = NVReadRAMDAC(dev, head, NV_PRAMDAC_TV_HTOTAL);
	regp->tv_hskew = NVReadRAMDAC(dev, head, NV_PRAMDAC_TV_HSKEW);
	regp->tv_hsync_delay = NVReadRAMDAC(dev, head, NV_PRAMDAC_TV_HSYNC_DELAY);
	regp->tv_hsync_delay2 = NVReadRAMDAC(dev, head, NV_PRAMDAC_TV_HSYNC_DELAY2);

	for (i = 0; i < 7; i++) {
		uint32_t ramdac_reg = NV_PRAMDAC_FP_VDISPLAY_END + (i * 4);
		regp->fp_vert_regs[i] = NVReadRAMDAC(dev, head, ramdac_reg);
		regp->fp_horiz_regs[i] = NVReadRAMDAC(dev, head, ramdac_reg + 0x20);
	}

	if (nv_gf4_disp_arch(dev)) {
		regp->dither = NVReadRAMDAC(dev, head, NV_RAMDAC_FP_DITHER);
		for (i = 0; i < 3; i++) {
			regp->dither_regs[i] = NVReadRAMDAC(dev, head, NV_PRAMDAC_850 + i * 4);
			regp->dither_regs[i + 3] = NVReadRAMDAC(dev, head, NV_PRAMDAC_85C + i * 4);
		}
	}

	regp->fp_control = NVReadRAMDAC(dev, head, NV_PRAMDAC_FP_TG_CONTROL);
	regp->fp_debug_0 = NVReadRAMDAC(dev, head, NV_PRAMDAC_FP_DEBUG_0);
	if (!nv_gf4_disp_arch(dev) && head == 0) {
		/* early chips don't allow access to PRAMDAC_TMDS_* without
		 * the head A FPCLK on (nv11 even locks up) */
		NVWriteRAMDAC(dev, 0, NV_PRAMDAC_FP_DEBUG_0, regp->fp_debug_0 &
			      ~NV_PRAMDAC_FP_DEBUG_0_PWRDOWN_FPCLK);
	}
	regp->fp_debug_1 = NVReadRAMDAC(dev, head, NV_PRAMDAC_FP_DEBUG_1);
	regp->fp_debug_2 = NVReadRAMDAC(dev, head, NV_PRAMDAC_FP_DEBUG_2);

	regp->fp_margin_color = NVReadRAMDAC(dev, head, NV_PRAMDAC_FP_MARGIN_COLOR);

	if (nv_gf4_disp_arch(dev))
		regp->ramdac_8c0 = NVReadRAMDAC(dev, head, NV_PRAMDAC_8C0);

	if (dev_priv->card_type == NV_40) {
		regp->ramdac_a20 = NVReadRAMDAC(dev, head, NV_PRAMDAC_A20);
		regp->ramdac_a24 = NVReadRAMDAC(dev, head, NV_PRAMDAC_A24);
		regp->ramdac_a34 = NVReadRAMDAC(dev, head, NV_PRAMDAC_A34);

		for (i = 0; i < 38; i++)
			regp->ctv_regs[i] = NVReadRAMDAC(dev, head,
							 NV_PRAMDAC_CTV + 4*i);
	}
}

static void
nv_load_state_ramdac(struct drm_device *dev, int head,
		     struct nv04_mode_state *state)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv04_crtc_reg *regp = &state->crtc_reg[head];
	uint32_t pllreg = head ? NV_RAMDAC_VPLL2 : NV_PRAMDAC_VPLL_COEFF;
	int i;

	if (dev_priv->card_type >= NV_10)
		NVWriteRAMDAC(dev, head, NV_RAMDAC_NV10_CURSYNC, regp->nv10_cursync);

	nouveau_hw_setpll(dev, pllreg, &regp->pllvals);
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_PLL_COEFF_SELECT, state->pllsel);
	if (nv_two_heads(dev))
		NVWriteRAMDAC(dev, 0, NV_PRAMDAC_SEL_CLK, state->sel_clk);
	if (dev_priv->chipset == 0x11)
		NVWriteRAMDAC(dev, head, NV_RAMDAC_DITHER_NV11, regp->dither);

	NVWriteRAMDAC(dev, head, NV_PRAMDAC_GENERAL_CONTROL, regp->ramdac_gen_ctrl);

	if (nv_gf4_disp_arch(dev))
		NVWriteRAMDAC(dev, head, NV_PRAMDAC_630, regp->ramdac_630);
	if (dev_priv->chipset >= 0x30)
		NVWriteRAMDAC(dev, head, NV_PRAMDAC_634, regp->ramdac_634);

	NVWriteRAMDAC(dev, head, NV_PRAMDAC_TV_SETUP, regp->tv_setup);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_TV_VTOTAL, regp->tv_vtotal);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_TV_VSKEW, regp->tv_vskew);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_TV_VSYNC_DELAY, regp->tv_vsync_delay);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_TV_HTOTAL, regp->tv_htotal);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_TV_HSKEW, regp->tv_hskew);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_TV_HSYNC_DELAY, regp->tv_hsync_delay);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_TV_HSYNC_DELAY2, regp->tv_hsync_delay2);

	for (i = 0; i < 7; i++) {
		uint32_t ramdac_reg = NV_PRAMDAC_FP_VDISPLAY_END + (i * 4);

		NVWriteRAMDAC(dev, head, ramdac_reg, regp->fp_vert_regs[i]);
		NVWriteRAMDAC(dev, head, ramdac_reg + 0x20, regp->fp_horiz_regs[i]);
	}

	if (nv_gf4_disp_arch(dev)) {
		NVWriteRAMDAC(dev, head, NV_RAMDAC_FP_DITHER, regp->dither);
		for (i = 0; i < 3; i++) {
			NVWriteRAMDAC(dev, head, NV_PRAMDAC_850 + i * 4, regp->dither_regs[i]);
			NVWriteRAMDAC(dev, head, NV_PRAMDAC_85C + i * 4, regp->dither_regs[i + 3]);
		}
	}

	NVWriteRAMDAC(dev, head, NV_PRAMDAC_FP_TG_CONTROL, regp->fp_control);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_FP_DEBUG_0, regp->fp_debug_0);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_FP_DEBUG_1, regp->fp_debug_1);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_FP_DEBUG_2, regp->fp_debug_2);

	NVWriteRAMDAC(dev, head, NV_PRAMDAC_FP_MARGIN_COLOR, regp->fp_margin_color);

	if (nv_gf4_disp_arch(dev))
		NVWriteRAMDAC(dev, head, NV_PRAMDAC_8C0, regp->ramdac_8c0);

	if (dev_priv->card_type == NV_40) {
		NVWriteRAMDAC(dev, head, NV_PRAMDAC_A20, regp->ramdac_a20);
		NVWriteRAMDAC(dev, head, NV_PRAMDAC_A24, regp->ramdac_a24);
		NVWriteRAMDAC(dev, head, NV_PRAMDAC_A34, regp->ramdac_a34);

		for (i = 0; i < 38; i++)
			NVWriteRAMDAC(dev, head,
				      NV_PRAMDAC_CTV + 4*i, regp->ctv_regs[i]);
	}
}

static void
nv_save_state_vga(struct drm_device *dev, int head,
		  struct nv04_mode_state *state)
{
	struct nv04_crtc_reg *regp = &state->crtc_reg[head];
	int i;

	regp->MiscOutReg = NVReadPRMVIO(dev, head, NV_PRMVIO_MISC__READ);

	for (i = 0; i < 25; i++)
		rd_cio_state(dev, head, regp, i);

	NVSetEnablePalette(dev, head, true);
	for (i = 0; i < 21; i++)
		regp->Attribute[i] = NVReadVgaAttr(dev, head, i);
	NVSetEnablePalette(dev, head, false);

	for (i = 0; i < 9; i++)
		regp->Graphics[i] = NVReadVgaGr(dev, head, i);

	for (i = 0; i < 5; i++)
		regp->Sequencer[i] = NVReadVgaSeq(dev, head, i);
}

static void
nv_load_state_vga(struct drm_device *dev, int head,
		  struct nv04_mode_state *state)
{
	struct nv04_crtc_reg *regp = &state->crtc_reg[head];
	int i;

	NVWritePRMVIO(dev, head, NV_PRMVIO_MISC__WRITE, regp->MiscOutReg);

	for (i = 0; i < 5; i++)
		NVWriteVgaSeq(dev, head, i, regp->Sequencer[i]);

	nv_lock_vga_crtc_base(dev, head, false);
	for (i = 0; i < 25; i++)
		wr_cio_state(dev, head, regp, i);
	nv_lock_vga_crtc_base(dev, head, true);

	for (i = 0; i < 9; i++)
		NVWriteVgaGr(dev, head, i, regp->Graphics[i]);

	NVSetEnablePalette(dev, head, true);
	for (i = 0; i < 21; i++)
		NVWriteVgaAttr(dev, head, i, regp->Attribute[i]);
	NVSetEnablePalette(dev, head, false);
}

static void
nv_save_state_ext(struct drm_device *dev, int head,
		  struct nv04_mode_state *state)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv04_crtc_reg *regp = &state->crtc_reg[head];
	int i;

	rd_cio_state(dev, head, regp, NV_CIO_CRE_LCD__INDEX);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_RPC0_INDEX);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_RPC1_INDEX);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_LSR_INDEX);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_PIXEL_INDEX);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_HEB__INDEX);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_ENH_INDEX);

	rd_cio_state(dev, head, regp, NV_CIO_CRE_FF_INDEX);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_FFLWM__INDEX);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_21);
	if (dev_priv->card_type >= NV_30)
		rd_cio_state(dev, head, regp, NV_CIO_CRE_47);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_49);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_HCUR_ADDR0_INDEX);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_HCUR_ADDR1_INDEX);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_HCUR_ADDR2_INDEX);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_ILACE__INDEX);

	if (dev_priv->card_type >= NV_10) {
		regp->crtc_830 = NVReadCRTC(dev, head, NV_PCRTC_830);
		regp->crtc_834 = NVReadCRTC(dev, head, NV_PCRTC_834);

		if (dev_priv->card_type >= NV_30)
			regp->gpio_ext = NVReadCRTC(dev, head, NV_PCRTC_GPIO_EXT);

		if (dev_priv->card_type == NV_40)
			regp->crtc_850 = NVReadCRTC(dev, head, NV_PCRTC_850);

		if (nv_two_heads(dev))
			regp->crtc_eng_ctrl = NVReadCRTC(dev, head, NV_PCRTC_ENGINE_CTRL);
		regp->cursor_cfg = NVReadCRTC(dev, head, NV_PCRTC_CURSOR_CONFIG);
	}

	regp->crtc_cfg = NVReadCRTC(dev, head, NV_PCRTC_CONFIG);

	rd_cio_state(dev, head, regp, NV_CIO_CRE_SCRATCH3__INDEX);
	rd_cio_state(dev, head, regp, NV_CIO_CRE_SCRATCH4__INDEX);
	if (dev_priv->card_type >= NV_10) {
		rd_cio_state(dev, head, regp, NV_CIO_CRE_EBR_INDEX);
		rd_cio_state(dev, head, regp, NV_CIO_CRE_CSB);
		rd_cio_state(dev, head, regp, NV_CIO_CRE_4B);
		rd_cio_state(dev, head, regp, NV_CIO_CRE_TVOUT_LATENCY);
	}
	/* NV11 and NV20 don't have this, they stop at 0x52. */
	if (nv_gf4_disp_arch(dev)) {
		rd_cio_state(dev, head, regp, NV_CIO_CRE_53);
		rd_cio_state(dev, head, regp, NV_CIO_CRE_54);

		for (i = 0; i < 0x10; i++)
			regp->CR58[i] = NVReadVgaCrtc5758(dev, head, i);
		rd_cio_state(dev, head, regp, NV_CIO_CRE_59);
		rd_cio_state(dev, head, regp, NV_CIO_CRE_5B);

		rd_cio_state(dev, head, regp, NV_CIO_CRE_85);
		rd_cio_state(dev, head, regp, NV_CIO_CRE_86);
	}

	regp->fb_start = NVReadCRTC(dev, head, NV_PCRTC_START);
}

static void
nv_load_state_ext(struct drm_device *dev, int head,
		  struct nv04_mode_state *state)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv04_crtc_reg *regp = &state->crtc_reg[head];
	uint32_t reg900;
	int i;

	if (dev_priv->card_type >= NV_10) {
		if (nv_two_heads(dev))
			/* setting ENGINE_CTRL (EC) *must* come before
			 * CIO_CRE_LCD, as writing CRE_LCD sets bits 16 & 17 in
			 * EC that should not be overwritten by writing stale EC
			 */
			NVWriteCRTC(dev, head, NV_PCRTC_ENGINE_CTRL, regp->crtc_eng_ctrl);

		nvWriteVIDEO(dev, NV_PVIDEO_STOP, 1);
		nvWriteVIDEO(dev, NV_PVIDEO_INTR_EN, 0);
		nvWriteVIDEO(dev, NV_PVIDEO_OFFSET_BUFF(0), 0);
		nvWriteVIDEO(dev, NV_PVIDEO_OFFSET_BUFF(1), 0);
		nvWriteVIDEO(dev, NV_PVIDEO_LIMIT(0), dev_priv->fb_available_size - 1);
		nvWriteVIDEO(dev, NV_PVIDEO_LIMIT(1), dev_priv->fb_available_size - 1);
		nvWriteVIDEO(dev, NV_PVIDEO_UVPLANE_LIMIT(0), dev_priv->fb_available_size - 1);
		nvWriteVIDEO(dev, NV_PVIDEO_UVPLANE_LIMIT(1), dev_priv->fb_available_size - 1);
		nvWriteMC(dev, NV_PBUS_POWERCTRL_2, 0);

		NVWriteCRTC(dev, head, NV_PCRTC_CURSOR_CONFIG, regp->cursor_cfg);
		NVWriteCRTC(dev, head, NV_PCRTC_830, regp->crtc_830);
		NVWriteCRTC(dev, head, NV_PCRTC_834, regp->crtc_834);

		if (dev_priv->card_type >= NV_30)
			NVWriteCRTC(dev, head, NV_PCRTC_GPIO_EXT, regp->gpio_ext);

		if (dev_priv->card_type == NV_40) {
			NVWriteCRTC(dev, head, NV_PCRTC_850, regp->crtc_850);

			reg900 = NVReadRAMDAC(dev, head, NV_PRAMDAC_900);
			if (regp->crtc_cfg == NV_PCRTC_CONFIG_START_ADDRESS_HSYNC)
				NVWriteRAMDAC(dev, head, NV_PRAMDAC_900, reg900 | 0x10000);
			else
				NVWriteRAMDAC(dev, head, NV_PRAMDAC_900, reg900 & ~0x10000);
		}
	}

	NVWriteCRTC(dev, head, NV_PCRTC_CONFIG, regp->crtc_cfg);

	wr_cio_state(dev, head, regp, NV_CIO_CRE_RPC0_INDEX);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_RPC1_INDEX);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_LSR_INDEX);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_PIXEL_INDEX);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_LCD__INDEX);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_HEB__INDEX);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_ENH_INDEX);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_FF_INDEX);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_FFLWM__INDEX);
	if (dev_priv->card_type >= NV_30)
		wr_cio_state(dev, head, regp, NV_CIO_CRE_47);

	wr_cio_state(dev, head, regp, NV_CIO_CRE_49);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_HCUR_ADDR0_INDEX);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_HCUR_ADDR1_INDEX);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_HCUR_ADDR2_INDEX);
	if (dev_priv->card_type == NV_40)
		nv_fix_nv40_hw_cursor(dev, head);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_ILACE__INDEX);

	wr_cio_state(dev, head, regp, NV_CIO_CRE_SCRATCH3__INDEX);
	wr_cio_state(dev, head, regp, NV_CIO_CRE_SCRATCH4__INDEX);
	if (dev_priv->card_type >= NV_10) {
		wr_cio_state(dev, head, regp, NV_CIO_CRE_EBR_INDEX);
		wr_cio_state(dev, head, regp, NV_CIO_CRE_CSB);
		wr_cio_state(dev, head, regp, NV_CIO_CRE_4B);
		wr_cio_state(dev, head, regp, NV_CIO_CRE_TVOUT_LATENCY);
	}
	/* NV11 and NV20 stop at 0x52. */
	if (nv_gf4_disp_arch(dev)) {
		if (dev_priv->card_type == NV_10) {
			/* Not waiting for vertical retrace before modifying
			   CRE_53/CRE_54 causes lockups. */
			nouveau_wait_until(dev, 650000000, NV_PRMCIO_INP0__COLOR, 0x8, 0x8);
			nouveau_wait_until(dev, 650000000, NV_PRMCIO_INP0__COLOR, 0x8, 0x0);
		}

		wr_cio_state(dev, head, regp, NV_CIO_CRE_53);
		wr_cio_state(dev, head, regp, NV_CIO_CRE_54);

		for (i = 0; i < 0x10; i++)
			NVWriteVgaCrtc5758(dev, head, i, regp->CR58[i]);
		wr_cio_state(dev, head, regp, NV_CIO_CRE_59);
		wr_cio_state(dev, head, regp, NV_CIO_CRE_5B);

		wr_cio_state(dev, head, regp, NV_CIO_CRE_85);
		wr_cio_state(dev, head, regp, NV_CIO_CRE_86);
	}

	NVWriteCRTC(dev, head, NV_PCRTC_START, regp->fb_start);

	/* Setting 1 on this value gives you interrupts for every vblank period. */
	NVWriteCRTC(dev, head, NV_PCRTC_INTR_EN_0, 0);
	NVWriteCRTC(dev, head, NV_PCRTC_INTR_0, NV_PCRTC_INTR_0_VBLANK);
}

static void
nv_save_state_palette(struct drm_device *dev, int head,
		      struct nv04_mode_state *state)
{
	int head_offset = head * NV_PRMDIO_SIZE, i;

	nv_wr08(dev, NV_PRMDIO_PIXEL_MASK + head_offset,
				NV_PRMDIO_PIXEL_MASK_MASK);
	nv_wr08(dev, NV_PRMDIO_READ_MODE_ADDRESS + head_offset, 0x0);

	for (i = 0; i < 768; i++) {
		state->crtc_reg[head].DAC[i] = nv_rd08(dev,
				NV_PRMDIO_PALETTE_DATA + head_offset);
	}

	NVSetEnablePalette(dev, head, false);
}

void
nouveau_hw_load_state_palette(struct drm_device *dev, int head,
			      struct nv04_mode_state *state)
{
	int head_offset = head * NV_PRMDIO_SIZE, i;

	nv_wr08(dev, NV_PRMDIO_PIXEL_MASK + head_offset,
				NV_PRMDIO_PIXEL_MASK_MASK);
	nv_wr08(dev, NV_PRMDIO_WRITE_MODE_ADDRESS + head_offset, 0x0);

	for (i = 0; i < 768; i++) {
		nv_wr08(dev, NV_PRMDIO_PALETTE_DATA + head_offset,
				state->crtc_reg[head].DAC[i]);
	}

	NVSetEnablePalette(dev, head, false);
}

void nouveau_hw_save_state(struct drm_device *dev, int head,
			   struct nv04_mode_state *state)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->chipset == 0x11)
		/* NB: no attempt is made to restore the bad pll later on */
		nouveau_hw_fix_bad_vpll(dev, head);
	nv_save_state_ramdac(dev, head, state);
	nv_save_state_vga(dev, head, state);
	nv_save_state_palette(dev, head, state);
	nv_save_state_ext(dev, head, state);
}

void nouveau_hw_load_state(struct drm_device *dev, int head,
			   struct nv04_mode_state *state)
{
	NVVgaProtect(dev, head, true);
	nv_load_state_ramdac(dev, head, state);
	nv_load_state_ext(dev, head, state);
	nouveau_hw_load_state_palette(dev, head, state);
	nv_load_state_vga(dev, head, state);
	NVVgaProtect(dev, head, false);
}
