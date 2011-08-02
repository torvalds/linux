/*
 *
 * Copyright 2008 (c) Intel Corporation
 *   Jesse Barnes <jbarnes@virtuousgeek.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "intel_drv.h"

static bool i915_pipe_enabled(struct drm_device *dev, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32	dpll_reg;

	if (HAS_PCH_SPLIT(dev))
		dpll_reg = (pipe == PIPE_A) ? _PCH_DPLL_A : _PCH_DPLL_B;
	else
		dpll_reg = (pipe == PIPE_A) ? _DPLL_A : _DPLL_B;

	return (I915_READ(dpll_reg) & DPLL_VCO_ENABLE);
}

static void i915_save_palette(struct drm_device *dev, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long reg = (pipe == PIPE_A ? _PALETTE_A : _PALETTE_B);
	u32 *array;
	int i;

	if (!i915_pipe_enabled(dev, pipe))
		return;

	if (HAS_PCH_SPLIT(dev))
		reg = (pipe == PIPE_A) ? _LGC_PALETTE_A : _LGC_PALETTE_B;

	if (pipe == PIPE_A)
		array = dev_priv->save_palette_a;
	else
		array = dev_priv->save_palette_b;

	for(i = 0; i < 256; i++)
		array[i] = I915_READ(reg + (i << 2));
}

static void i915_restore_palette(struct drm_device *dev, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long reg = (pipe == PIPE_A ? _PALETTE_A : _PALETTE_B);
	u32 *array;
	int i;

	if (!i915_pipe_enabled(dev, pipe))
		return;

	if (HAS_PCH_SPLIT(dev))
		reg = (pipe == PIPE_A) ? _LGC_PALETTE_A : _LGC_PALETTE_B;

	if (pipe == PIPE_A)
		array = dev_priv->save_palette_a;
	else
		array = dev_priv->save_palette_b;

	for(i = 0; i < 256; i++)
		I915_WRITE(reg + (i << 2), array[i]);
}

static u8 i915_read_indexed(struct drm_device *dev, u16 index_port, u16 data_port, u8 reg)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE8(index_port, reg);
	return I915_READ8(data_port);
}

static u8 i915_read_ar(struct drm_device *dev, u16 st01, u8 reg, u16 palette_enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_READ8(st01);
	I915_WRITE8(VGA_AR_INDEX, palette_enable | reg);
	return I915_READ8(VGA_AR_DATA_READ);
}

static void i915_write_ar(struct drm_device *dev, u16 st01, u8 reg, u8 val, u16 palette_enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_READ8(st01);
	I915_WRITE8(VGA_AR_INDEX, palette_enable | reg);
	I915_WRITE8(VGA_AR_DATA_WRITE, val);
}

static void i915_write_indexed(struct drm_device *dev, u16 index_port, u16 data_port, u8 reg, u8 val)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE8(index_port, reg);
	I915_WRITE8(data_port, val);
}

static void i915_save_vga(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;
	u16 cr_index, cr_data, st01;

	/* VGA color palette registers */
	dev_priv->saveDACMASK = I915_READ8(VGA_DACMASK);

	/* MSR bits */
	dev_priv->saveMSR = I915_READ8(VGA_MSR_READ);
	if (dev_priv->saveMSR & VGA_MSR_CGA_MODE) {
		cr_index = VGA_CR_INDEX_CGA;
		cr_data = VGA_CR_DATA_CGA;
		st01 = VGA_ST01_CGA;
	} else {
		cr_index = VGA_CR_INDEX_MDA;
		cr_data = VGA_CR_DATA_MDA;
		st01 = VGA_ST01_MDA;
	}

	/* CRT controller regs */
	i915_write_indexed(dev, cr_index, cr_data, 0x11,
			   i915_read_indexed(dev, cr_index, cr_data, 0x11) &
			   (~0x80));
	for (i = 0; i <= 0x24; i++)
		dev_priv->saveCR[i] =
			i915_read_indexed(dev, cr_index, cr_data, i);
	/* Make sure we don't turn off CR group 0 writes */
	dev_priv->saveCR[0x11] &= ~0x80;

	/* Attribute controller registers */
	I915_READ8(st01);
	dev_priv->saveAR_INDEX = I915_READ8(VGA_AR_INDEX);
	for (i = 0; i <= 0x14; i++)
		dev_priv->saveAR[i] = i915_read_ar(dev, st01, i, 0);
	I915_READ8(st01);
	I915_WRITE8(VGA_AR_INDEX, dev_priv->saveAR_INDEX);
	I915_READ8(st01);

	/* Graphics controller registers */
	for (i = 0; i < 9; i++)
		dev_priv->saveGR[i] =
			i915_read_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, i);

	dev_priv->saveGR[0x10] =
		i915_read_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, 0x10);
	dev_priv->saveGR[0x11] =
		i915_read_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, 0x11);
	dev_priv->saveGR[0x18] =
		i915_read_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, 0x18);

	/* Sequencer registers */
	for (i = 0; i < 8; i++)
		dev_priv->saveSR[i] =
			i915_read_indexed(dev, VGA_SR_INDEX, VGA_SR_DATA, i);
}

static void i915_restore_vga(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;
	u16 cr_index, cr_data, st01;

	/* MSR bits */
	I915_WRITE8(VGA_MSR_WRITE, dev_priv->saveMSR);
	if (dev_priv->saveMSR & VGA_MSR_CGA_MODE) {
		cr_index = VGA_CR_INDEX_CGA;
		cr_data = VGA_CR_DATA_CGA;
		st01 = VGA_ST01_CGA;
	} else {
		cr_index = VGA_CR_INDEX_MDA;
		cr_data = VGA_CR_DATA_MDA;
		st01 = VGA_ST01_MDA;
	}

	/* Sequencer registers, don't write SR07 */
	for (i = 0; i < 7; i++)
		i915_write_indexed(dev, VGA_SR_INDEX, VGA_SR_DATA, i,
				   dev_priv->saveSR[i]);

	/* CRT controller regs */
	/* Enable CR group 0 writes */
	i915_write_indexed(dev, cr_index, cr_data, 0x11, dev_priv->saveCR[0x11]);
	for (i = 0; i <= 0x24; i++)
		i915_write_indexed(dev, cr_index, cr_data, i, dev_priv->saveCR[i]);

	/* Graphics controller regs */
	for (i = 0; i < 9; i++)
		i915_write_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, i,
				   dev_priv->saveGR[i]);

	i915_write_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, 0x10,
			   dev_priv->saveGR[0x10]);
	i915_write_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, 0x11,
			   dev_priv->saveGR[0x11]);
	i915_write_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, 0x18,
			   dev_priv->saveGR[0x18]);

	/* Attribute controller registers */
	I915_READ8(st01); /* switch back to index mode */
	for (i = 0; i <= 0x14; i++)
		i915_write_ar(dev, st01, i, dev_priv->saveAR[i], 0);
	I915_READ8(st01); /* switch back to index mode */
	I915_WRITE8(VGA_AR_INDEX, dev_priv->saveAR_INDEX | 0x20);
	I915_READ8(st01);

	/* VGA color palette registers */
	I915_WRITE8(VGA_DACMASK, dev_priv->saveDACMASK);
}

static void i915_save_modeset_reg(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	/* Cursor state */
	dev_priv->saveCURACNTR = I915_READ(_CURACNTR);
	dev_priv->saveCURAPOS = I915_READ(_CURAPOS);
	dev_priv->saveCURABASE = I915_READ(_CURABASE);
	dev_priv->saveCURBCNTR = I915_READ(_CURBCNTR);
	dev_priv->saveCURBPOS = I915_READ(_CURBPOS);
	dev_priv->saveCURBBASE = I915_READ(_CURBBASE);
	if (IS_GEN2(dev))
		dev_priv->saveCURSIZE = I915_READ(CURSIZE);

	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->savePCH_DREF_CONTROL = I915_READ(PCH_DREF_CONTROL);
		dev_priv->saveDISP_ARB_CTL = I915_READ(DISP_ARB_CTL);
	}

	/* Pipe & plane A info */
	dev_priv->savePIPEACONF = I915_READ(_PIPEACONF);
	dev_priv->savePIPEASRC = I915_READ(_PIPEASRC);
	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->saveFPA0 = I915_READ(_PCH_FPA0);
		dev_priv->saveFPA1 = I915_READ(_PCH_FPA1);
		dev_priv->saveDPLL_A = I915_READ(_PCH_DPLL_A);
	} else {
		dev_priv->saveFPA0 = I915_READ(_FPA0);
		dev_priv->saveFPA1 = I915_READ(_FPA1);
		dev_priv->saveDPLL_A = I915_READ(_DPLL_A);
	}
	if (INTEL_INFO(dev)->gen >= 4 && !HAS_PCH_SPLIT(dev))
		dev_priv->saveDPLL_A_MD = I915_READ(_DPLL_A_MD);
	dev_priv->saveHTOTAL_A = I915_READ(_HTOTAL_A);
	dev_priv->saveHBLANK_A = I915_READ(_HBLANK_A);
	dev_priv->saveHSYNC_A = I915_READ(_HSYNC_A);
	dev_priv->saveVTOTAL_A = I915_READ(_VTOTAL_A);
	dev_priv->saveVBLANK_A = I915_READ(_VBLANK_A);
	dev_priv->saveVSYNC_A = I915_READ(_VSYNC_A);
	if (!HAS_PCH_SPLIT(dev))
		dev_priv->saveBCLRPAT_A = I915_READ(_BCLRPAT_A);

	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->savePIPEA_DATA_M1 = I915_READ(_PIPEA_DATA_M1);
		dev_priv->savePIPEA_DATA_N1 = I915_READ(_PIPEA_DATA_N1);
		dev_priv->savePIPEA_LINK_M1 = I915_READ(_PIPEA_LINK_M1);
		dev_priv->savePIPEA_LINK_N1 = I915_READ(_PIPEA_LINK_N1);

		dev_priv->saveFDI_TXA_CTL = I915_READ(_FDI_TXA_CTL);
		dev_priv->saveFDI_RXA_CTL = I915_READ(_FDI_RXA_CTL);

		dev_priv->savePFA_CTL_1 = I915_READ(_PFA_CTL_1);
		dev_priv->savePFA_WIN_SZ = I915_READ(_PFA_WIN_SZ);
		dev_priv->savePFA_WIN_POS = I915_READ(_PFA_WIN_POS);

		dev_priv->saveTRANSACONF = I915_READ(_TRANSACONF);
		dev_priv->saveTRANS_HTOTAL_A = I915_READ(_TRANS_HTOTAL_A);
		dev_priv->saveTRANS_HBLANK_A = I915_READ(_TRANS_HBLANK_A);
		dev_priv->saveTRANS_HSYNC_A = I915_READ(_TRANS_HSYNC_A);
		dev_priv->saveTRANS_VTOTAL_A = I915_READ(_TRANS_VTOTAL_A);
		dev_priv->saveTRANS_VBLANK_A = I915_READ(_TRANS_VBLANK_A);
		dev_priv->saveTRANS_VSYNC_A = I915_READ(_TRANS_VSYNC_A);
	}

	dev_priv->saveDSPACNTR = I915_READ(_DSPACNTR);
	dev_priv->saveDSPASTRIDE = I915_READ(_DSPASTRIDE);
	dev_priv->saveDSPASIZE = I915_READ(_DSPASIZE);
	dev_priv->saveDSPAPOS = I915_READ(_DSPAPOS);
	dev_priv->saveDSPAADDR = I915_READ(_DSPAADDR);
	if (INTEL_INFO(dev)->gen >= 4) {
		dev_priv->saveDSPASURF = I915_READ(_DSPASURF);
		dev_priv->saveDSPATILEOFF = I915_READ(_DSPATILEOFF);
	}
	i915_save_palette(dev, PIPE_A);
	dev_priv->savePIPEASTAT = I915_READ(_PIPEASTAT);

	/* Pipe & plane B info */
	dev_priv->savePIPEBCONF = I915_READ(_PIPEBCONF);
	dev_priv->savePIPEBSRC = I915_READ(_PIPEBSRC);
	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->saveFPB0 = I915_READ(_PCH_FPB0);
		dev_priv->saveFPB1 = I915_READ(_PCH_FPB1);
		dev_priv->saveDPLL_B = I915_READ(_PCH_DPLL_B);
	} else {
		dev_priv->saveFPB0 = I915_READ(_FPB0);
		dev_priv->saveFPB1 = I915_READ(_FPB1);
		dev_priv->saveDPLL_B = I915_READ(_DPLL_B);
	}
	if (INTEL_INFO(dev)->gen >= 4 && !HAS_PCH_SPLIT(dev))
		dev_priv->saveDPLL_B_MD = I915_READ(_DPLL_B_MD);
	dev_priv->saveHTOTAL_B = I915_READ(_HTOTAL_B);
	dev_priv->saveHBLANK_B = I915_READ(_HBLANK_B);
	dev_priv->saveHSYNC_B = I915_READ(_HSYNC_B);
	dev_priv->saveVTOTAL_B = I915_READ(_VTOTAL_B);
	dev_priv->saveVBLANK_B = I915_READ(_VBLANK_B);
	dev_priv->saveVSYNC_B = I915_READ(_VSYNC_B);
	if (!HAS_PCH_SPLIT(dev))
		dev_priv->saveBCLRPAT_B = I915_READ(_BCLRPAT_B);

	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->savePIPEB_DATA_M1 = I915_READ(_PIPEB_DATA_M1);
		dev_priv->savePIPEB_DATA_N1 = I915_READ(_PIPEB_DATA_N1);
		dev_priv->savePIPEB_LINK_M1 = I915_READ(_PIPEB_LINK_M1);
		dev_priv->savePIPEB_LINK_N1 = I915_READ(_PIPEB_LINK_N1);

		dev_priv->saveFDI_TXB_CTL = I915_READ(_FDI_TXB_CTL);
		dev_priv->saveFDI_RXB_CTL = I915_READ(_FDI_RXB_CTL);

		dev_priv->savePFB_CTL_1 = I915_READ(_PFB_CTL_1);
		dev_priv->savePFB_WIN_SZ = I915_READ(_PFB_WIN_SZ);
		dev_priv->savePFB_WIN_POS = I915_READ(_PFB_WIN_POS);

		dev_priv->saveTRANSBCONF = I915_READ(_TRANSBCONF);
		dev_priv->saveTRANS_HTOTAL_B = I915_READ(_TRANS_HTOTAL_B);
		dev_priv->saveTRANS_HBLANK_B = I915_READ(_TRANS_HBLANK_B);
		dev_priv->saveTRANS_HSYNC_B = I915_READ(_TRANS_HSYNC_B);
		dev_priv->saveTRANS_VTOTAL_B = I915_READ(_TRANS_VTOTAL_B);
		dev_priv->saveTRANS_VBLANK_B = I915_READ(_TRANS_VBLANK_B);
		dev_priv->saveTRANS_VSYNC_B = I915_READ(_TRANS_VSYNC_B);
	}

	dev_priv->saveDSPBCNTR = I915_READ(_DSPBCNTR);
	dev_priv->saveDSPBSTRIDE = I915_READ(_DSPBSTRIDE);
	dev_priv->saveDSPBSIZE = I915_READ(_DSPBSIZE);
	dev_priv->saveDSPBPOS = I915_READ(_DSPBPOS);
	dev_priv->saveDSPBADDR = I915_READ(_DSPBADDR);
	if (INTEL_INFO(dev)->gen >= 4) {
		dev_priv->saveDSPBSURF = I915_READ(_DSPBSURF);
		dev_priv->saveDSPBTILEOFF = I915_READ(_DSPBTILEOFF);
	}
	i915_save_palette(dev, PIPE_B);
	dev_priv->savePIPEBSTAT = I915_READ(_PIPEBSTAT);

	/* Fences */
	switch (INTEL_INFO(dev)->gen) {
	case 6:
		for (i = 0; i < 16; i++)
			dev_priv->saveFENCE[i] = I915_READ64(FENCE_REG_SANDYBRIDGE_0 + (i * 8));
		break;
	case 5:
	case 4:
		for (i = 0; i < 16; i++)
			dev_priv->saveFENCE[i] = I915_READ64(FENCE_REG_965_0 + (i * 8));
		break;
	case 3:
		if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
			for (i = 0; i < 8; i++)
				dev_priv->saveFENCE[i+8] = I915_READ(FENCE_REG_945_8 + (i * 4));
	case 2:
		for (i = 0; i < 8; i++)
			dev_priv->saveFENCE[i] = I915_READ(FENCE_REG_830_0 + (i * 4));
		break;
	}

	return;
}

static void i915_restore_modeset_reg(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int dpll_a_reg, fpa0_reg, fpa1_reg;
	int dpll_b_reg, fpb0_reg, fpb1_reg;
	int i;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	/* Fences */
	switch (INTEL_INFO(dev)->gen) {
	case 6:
		for (i = 0; i < 16; i++)
			I915_WRITE64(FENCE_REG_SANDYBRIDGE_0 + (i * 8), dev_priv->saveFENCE[i]);
		break;
	case 5:
	case 4:
		for (i = 0; i < 16; i++)
			I915_WRITE64(FENCE_REG_965_0 + (i * 8), dev_priv->saveFENCE[i]);
		break;
	case 3:
	case 2:
		if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
			for (i = 0; i < 8; i++)
				I915_WRITE(FENCE_REG_945_8 + (i * 4), dev_priv->saveFENCE[i+8]);
		for (i = 0; i < 8; i++)
			I915_WRITE(FENCE_REG_830_0 + (i * 4), dev_priv->saveFENCE[i]);
		break;
	}


	if (HAS_PCH_SPLIT(dev)) {
		dpll_a_reg = _PCH_DPLL_A;
		dpll_b_reg = _PCH_DPLL_B;
		fpa0_reg = _PCH_FPA0;
		fpb0_reg = _PCH_FPB0;
		fpa1_reg = _PCH_FPA1;
		fpb1_reg = _PCH_FPB1;
	} else {
		dpll_a_reg = _DPLL_A;
		dpll_b_reg = _DPLL_B;
		fpa0_reg = _FPA0;
		fpb0_reg = _FPB0;
		fpa1_reg = _FPA1;
		fpb1_reg = _FPB1;
	}

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(PCH_DREF_CONTROL, dev_priv->savePCH_DREF_CONTROL);
		I915_WRITE(DISP_ARB_CTL, dev_priv->saveDISP_ARB_CTL);
	}

	/* Pipe & plane A info */
	/* Prime the clock */
	if (dev_priv->saveDPLL_A & DPLL_VCO_ENABLE) {
		I915_WRITE(dpll_a_reg, dev_priv->saveDPLL_A &
			   ~DPLL_VCO_ENABLE);
		POSTING_READ(dpll_a_reg);
		udelay(150);
	}
	I915_WRITE(fpa0_reg, dev_priv->saveFPA0);
	I915_WRITE(fpa1_reg, dev_priv->saveFPA1);
	/* Actually enable it */
	I915_WRITE(dpll_a_reg, dev_priv->saveDPLL_A);
	POSTING_READ(dpll_a_reg);
	udelay(150);
	if (INTEL_INFO(dev)->gen >= 4 && !HAS_PCH_SPLIT(dev)) {
		I915_WRITE(_DPLL_A_MD, dev_priv->saveDPLL_A_MD);
		POSTING_READ(_DPLL_A_MD);
	}
	udelay(150);

	/* Restore mode */
	I915_WRITE(_HTOTAL_A, dev_priv->saveHTOTAL_A);
	I915_WRITE(_HBLANK_A, dev_priv->saveHBLANK_A);
	I915_WRITE(_HSYNC_A, dev_priv->saveHSYNC_A);
	I915_WRITE(_VTOTAL_A, dev_priv->saveVTOTAL_A);
	I915_WRITE(_VBLANK_A, dev_priv->saveVBLANK_A);
	I915_WRITE(_VSYNC_A, dev_priv->saveVSYNC_A);
	if (!HAS_PCH_SPLIT(dev))
		I915_WRITE(_BCLRPAT_A, dev_priv->saveBCLRPAT_A);

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(_PIPEA_DATA_M1, dev_priv->savePIPEA_DATA_M1);
		I915_WRITE(_PIPEA_DATA_N1, dev_priv->savePIPEA_DATA_N1);
		I915_WRITE(_PIPEA_LINK_M1, dev_priv->savePIPEA_LINK_M1);
		I915_WRITE(_PIPEA_LINK_N1, dev_priv->savePIPEA_LINK_N1);

		I915_WRITE(_FDI_RXA_CTL, dev_priv->saveFDI_RXA_CTL);
		I915_WRITE(_FDI_TXA_CTL, dev_priv->saveFDI_TXA_CTL);

		I915_WRITE(_PFA_CTL_1, dev_priv->savePFA_CTL_1);
		I915_WRITE(_PFA_WIN_SZ, dev_priv->savePFA_WIN_SZ);
		I915_WRITE(_PFA_WIN_POS, dev_priv->savePFA_WIN_POS);

		I915_WRITE(_TRANSACONF, dev_priv->saveTRANSACONF);
		I915_WRITE(_TRANS_HTOTAL_A, dev_priv->saveTRANS_HTOTAL_A);
		I915_WRITE(_TRANS_HBLANK_A, dev_priv->saveTRANS_HBLANK_A);
		I915_WRITE(_TRANS_HSYNC_A, dev_priv->saveTRANS_HSYNC_A);
		I915_WRITE(_TRANS_VTOTAL_A, dev_priv->saveTRANS_VTOTAL_A);
		I915_WRITE(_TRANS_VBLANK_A, dev_priv->saveTRANS_VBLANK_A);
		I915_WRITE(_TRANS_VSYNC_A, dev_priv->saveTRANS_VSYNC_A);
	}

	/* Restore plane info */
	I915_WRITE(_DSPASIZE, dev_priv->saveDSPASIZE);
	I915_WRITE(_DSPAPOS, dev_priv->saveDSPAPOS);
	I915_WRITE(_PIPEASRC, dev_priv->savePIPEASRC);
	I915_WRITE(_DSPAADDR, dev_priv->saveDSPAADDR);
	I915_WRITE(_DSPASTRIDE, dev_priv->saveDSPASTRIDE);
	if (INTEL_INFO(dev)->gen >= 4) {
		I915_WRITE(_DSPASURF, dev_priv->saveDSPASURF);
		I915_WRITE(_DSPATILEOFF, dev_priv->saveDSPATILEOFF);
	}

	I915_WRITE(_PIPEACONF, dev_priv->savePIPEACONF);

	i915_restore_palette(dev, PIPE_A);
	/* Enable the plane */
	I915_WRITE(_DSPACNTR, dev_priv->saveDSPACNTR);
	I915_WRITE(_DSPAADDR, I915_READ(_DSPAADDR));

	/* Pipe & plane B info */
	if (dev_priv->saveDPLL_B & DPLL_VCO_ENABLE) {
		I915_WRITE(dpll_b_reg, dev_priv->saveDPLL_B &
			   ~DPLL_VCO_ENABLE);
		POSTING_READ(dpll_b_reg);
		udelay(150);
	}
	I915_WRITE(fpb0_reg, dev_priv->saveFPB0);
	I915_WRITE(fpb1_reg, dev_priv->saveFPB1);
	/* Actually enable it */
	I915_WRITE(dpll_b_reg, dev_priv->saveDPLL_B);
	POSTING_READ(dpll_b_reg);
	udelay(150);
	if (INTEL_INFO(dev)->gen >= 4 && !HAS_PCH_SPLIT(dev)) {
		I915_WRITE(_DPLL_B_MD, dev_priv->saveDPLL_B_MD);
		POSTING_READ(_DPLL_B_MD);
	}
	udelay(150);

	/* Restore mode */
	I915_WRITE(_HTOTAL_B, dev_priv->saveHTOTAL_B);
	I915_WRITE(_HBLANK_B, dev_priv->saveHBLANK_B);
	I915_WRITE(_HSYNC_B, dev_priv->saveHSYNC_B);
	I915_WRITE(_VTOTAL_B, dev_priv->saveVTOTAL_B);
	I915_WRITE(_VBLANK_B, dev_priv->saveVBLANK_B);
	I915_WRITE(_VSYNC_B, dev_priv->saveVSYNC_B);
	if (!HAS_PCH_SPLIT(dev))
		I915_WRITE(_BCLRPAT_B, dev_priv->saveBCLRPAT_B);

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(_PIPEB_DATA_M1, dev_priv->savePIPEB_DATA_M1);
		I915_WRITE(_PIPEB_DATA_N1, dev_priv->savePIPEB_DATA_N1);
		I915_WRITE(_PIPEB_LINK_M1, dev_priv->savePIPEB_LINK_M1);
		I915_WRITE(_PIPEB_LINK_N1, dev_priv->savePIPEB_LINK_N1);

		I915_WRITE(_FDI_RXB_CTL, dev_priv->saveFDI_RXB_CTL);
		I915_WRITE(_FDI_TXB_CTL, dev_priv->saveFDI_TXB_CTL);

		I915_WRITE(_PFB_CTL_1, dev_priv->savePFB_CTL_1);
		I915_WRITE(_PFB_WIN_SZ, dev_priv->savePFB_WIN_SZ);
		I915_WRITE(_PFB_WIN_POS, dev_priv->savePFB_WIN_POS);

		I915_WRITE(_TRANSBCONF, dev_priv->saveTRANSBCONF);
		I915_WRITE(_TRANS_HTOTAL_B, dev_priv->saveTRANS_HTOTAL_B);
		I915_WRITE(_TRANS_HBLANK_B, dev_priv->saveTRANS_HBLANK_B);
		I915_WRITE(_TRANS_HSYNC_B, dev_priv->saveTRANS_HSYNC_B);
		I915_WRITE(_TRANS_VTOTAL_B, dev_priv->saveTRANS_VTOTAL_B);
		I915_WRITE(_TRANS_VBLANK_B, dev_priv->saveTRANS_VBLANK_B);
		I915_WRITE(_TRANS_VSYNC_B, dev_priv->saveTRANS_VSYNC_B);
	}

	/* Restore plane info */
	I915_WRITE(_DSPBSIZE, dev_priv->saveDSPBSIZE);
	I915_WRITE(_DSPBPOS, dev_priv->saveDSPBPOS);
	I915_WRITE(_PIPEBSRC, dev_priv->savePIPEBSRC);
	I915_WRITE(_DSPBADDR, dev_priv->saveDSPBADDR);
	I915_WRITE(_DSPBSTRIDE, dev_priv->saveDSPBSTRIDE);
	if (INTEL_INFO(dev)->gen >= 4) {
		I915_WRITE(_DSPBSURF, dev_priv->saveDSPBSURF);
		I915_WRITE(_DSPBTILEOFF, dev_priv->saveDSPBTILEOFF);
	}

	I915_WRITE(_PIPEBCONF, dev_priv->savePIPEBCONF);

	i915_restore_palette(dev, PIPE_B);
	/* Enable the plane */
	I915_WRITE(_DSPBCNTR, dev_priv->saveDSPBCNTR);
	I915_WRITE(_DSPBADDR, I915_READ(_DSPBADDR));

	/* Cursor state */
	I915_WRITE(_CURAPOS, dev_priv->saveCURAPOS);
	I915_WRITE(_CURACNTR, dev_priv->saveCURACNTR);
	I915_WRITE(_CURABASE, dev_priv->saveCURABASE);
	I915_WRITE(_CURBPOS, dev_priv->saveCURBPOS);
	I915_WRITE(_CURBCNTR, dev_priv->saveCURBCNTR);
	I915_WRITE(_CURBBASE, dev_priv->saveCURBBASE);
	if (IS_GEN2(dev))
		I915_WRITE(CURSIZE, dev_priv->saveCURSIZE);

	return;
}

static void i915_save_display(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Display arbitration control */
	dev_priv->saveDSPARB = I915_READ(DSPARB);

	/* This is only meaningful in non-KMS mode */
	/* Don't save them in KMS mode */
	i915_save_modeset_reg(dev);

	/* CRT state */
	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->saveADPA = I915_READ(PCH_ADPA);
	} else {
		dev_priv->saveADPA = I915_READ(ADPA);
	}

	/* LVDS state */
	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->savePP_CONTROL = I915_READ(PCH_PP_CONTROL);
		dev_priv->saveBLC_PWM_CTL = I915_READ(BLC_PWM_PCH_CTL1);
		dev_priv->saveBLC_PWM_CTL2 = I915_READ(BLC_PWM_PCH_CTL2);
		dev_priv->saveBLC_CPU_PWM_CTL = I915_READ(BLC_PWM_CPU_CTL);
		dev_priv->saveBLC_CPU_PWM_CTL2 = I915_READ(BLC_PWM_CPU_CTL2);
		dev_priv->saveLVDS = I915_READ(PCH_LVDS);
	} else {
		dev_priv->savePP_CONTROL = I915_READ(PP_CONTROL);
		dev_priv->savePFIT_PGM_RATIOS = I915_READ(PFIT_PGM_RATIOS);
		dev_priv->saveBLC_PWM_CTL = I915_READ(BLC_PWM_CTL);
		dev_priv->saveBLC_HIST_CTL = I915_READ(BLC_HIST_CTL);
		if (INTEL_INFO(dev)->gen >= 4)
			dev_priv->saveBLC_PWM_CTL2 = I915_READ(BLC_PWM_CTL2);
		if (IS_MOBILE(dev) && !IS_I830(dev))
			dev_priv->saveLVDS = I915_READ(LVDS);
	}

	if (!IS_I830(dev) && !IS_845G(dev) && !HAS_PCH_SPLIT(dev))
		dev_priv->savePFIT_CONTROL = I915_READ(PFIT_CONTROL);

	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->savePP_ON_DELAYS = I915_READ(PCH_PP_ON_DELAYS);
		dev_priv->savePP_OFF_DELAYS = I915_READ(PCH_PP_OFF_DELAYS);
		dev_priv->savePP_DIVISOR = I915_READ(PCH_PP_DIVISOR);
	} else {
		dev_priv->savePP_ON_DELAYS = I915_READ(PP_ON_DELAYS);
		dev_priv->savePP_OFF_DELAYS = I915_READ(PP_OFF_DELAYS);
		dev_priv->savePP_DIVISOR = I915_READ(PP_DIVISOR);
	}

	/* Display Port state */
	if (SUPPORTS_INTEGRATED_DP(dev)) {
		dev_priv->saveDP_B = I915_READ(DP_B);
		dev_priv->saveDP_C = I915_READ(DP_C);
		dev_priv->saveDP_D = I915_READ(DP_D);
		dev_priv->savePIPEA_GMCH_DATA_M = I915_READ(_PIPEA_GMCH_DATA_M);
		dev_priv->savePIPEB_GMCH_DATA_M = I915_READ(_PIPEB_GMCH_DATA_M);
		dev_priv->savePIPEA_GMCH_DATA_N = I915_READ(_PIPEA_GMCH_DATA_N);
		dev_priv->savePIPEB_GMCH_DATA_N = I915_READ(_PIPEB_GMCH_DATA_N);
		dev_priv->savePIPEA_DP_LINK_M = I915_READ(_PIPEA_DP_LINK_M);
		dev_priv->savePIPEB_DP_LINK_M = I915_READ(_PIPEB_DP_LINK_M);
		dev_priv->savePIPEA_DP_LINK_N = I915_READ(_PIPEA_DP_LINK_N);
		dev_priv->savePIPEB_DP_LINK_N = I915_READ(_PIPEB_DP_LINK_N);
	}
	/* FIXME: save TV & SDVO state */

	/* Only save FBC state on the platform that supports FBC */
	if (I915_HAS_FBC(dev)) {
		if (HAS_PCH_SPLIT(dev)) {
			dev_priv->saveDPFC_CB_BASE = I915_READ(ILK_DPFC_CB_BASE);
		} else if (IS_GM45(dev)) {
			dev_priv->saveDPFC_CB_BASE = I915_READ(DPFC_CB_BASE);
		} else {
			dev_priv->saveFBC_CFB_BASE = I915_READ(FBC_CFB_BASE);
			dev_priv->saveFBC_LL_BASE = I915_READ(FBC_LL_BASE);
			dev_priv->saveFBC_CONTROL2 = I915_READ(FBC_CONTROL2);
			dev_priv->saveFBC_CONTROL = I915_READ(FBC_CONTROL);
		}
	}

	/* VGA state */
	dev_priv->saveVGA0 = I915_READ(VGA0);
	dev_priv->saveVGA1 = I915_READ(VGA1);
	dev_priv->saveVGA_PD = I915_READ(VGA_PD);
	if (HAS_PCH_SPLIT(dev))
		dev_priv->saveVGACNTRL = I915_READ(CPU_VGACNTRL);
	else
		dev_priv->saveVGACNTRL = I915_READ(VGACNTRL);

	i915_save_vga(dev);
}

static void i915_restore_display(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Display arbitration */
	I915_WRITE(DSPARB, dev_priv->saveDSPARB);

	/* Display port ratios (must be done before clock is set) */
	if (SUPPORTS_INTEGRATED_DP(dev)) {
		I915_WRITE(_PIPEA_GMCH_DATA_M, dev_priv->savePIPEA_GMCH_DATA_M);
		I915_WRITE(_PIPEB_GMCH_DATA_M, dev_priv->savePIPEB_GMCH_DATA_M);
		I915_WRITE(_PIPEA_GMCH_DATA_N, dev_priv->savePIPEA_GMCH_DATA_N);
		I915_WRITE(_PIPEB_GMCH_DATA_N, dev_priv->savePIPEB_GMCH_DATA_N);
		I915_WRITE(_PIPEA_DP_LINK_M, dev_priv->savePIPEA_DP_LINK_M);
		I915_WRITE(_PIPEB_DP_LINK_M, dev_priv->savePIPEB_DP_LINK_M);
		I915_WRITE(_PIPEA_DP_LINK_N, dev_priv->savePIPEA_DP_LINK_N);
		I915_WRITE(_PIPEB_DP_LINK_N, dev_priv->savePIPEB_DP_LINK_N);
	}

	/* This is only meaningful in non-KMS mode */
	/* Don't restore them in KMS mode */
	i915_restore_modeset_reg(dev);

	/* CRT state */
	if (HAS_PCH_SPLIT(dev))
		I915_WRITE(PCH_ADPA, dev_priv->saveADPA);
	else
		I915_WRITE(ADPA, dev_priv->saveADPA);

	/* LVDS state */
	if (INTEL_INFO(dev)->gen >= 4 && !HAS_PCH_SPLIT(dev))
		I915_WRITE(BLC_PWM_CTL2, dev_priv->saveBLC_PWM_CTL2);

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(PCH_LVDS, dev_priv->saveLVDS);
	} else if (IS_MOBILE(dev) && !IS_I830(dev))
		I915_WRITE(LVDS, dev_priv->saveLVDS);

	if (!IS_I830(dev) && !IS_845G(dev) && !HAS_PCH_SPLIT(dev))
		I915_WRITE(PFIT_CONTROL, dev_priv->savePFIT_CONTROL);

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(BLC_PWM_PCH_CTL1, dev_priv->saveBLC_PWM_CTL);
		I915_WRITE(BLC_PWM_PCH_CTL2, dev_priv->saveBLC_PWM_CTL2);
		I915_WRITE(BLC_PWM_CPU_CTL, dev_priv->saveBLC_CPU_PWM_CTL);
		I915_WRITE(BLC_PWM_CPU_CTL2, dev_priv->saveBLC_CPU_PWM_CTL2);
		I915_WRITE(PCH_PP_ON_DELAYS, dev_priv->savePP_ON_DELAYS);
		I915_WRITE(PCH_PP_OFF_DELAYS, dev_priv->savePP_OFF_DELAYS);
		I915_WRITE(PCH_PP_DIVISOR, dev_priv->savePP_DIVISOR);
		I915_WRITE(PCH_PP_CONTROL, dev_priv->savePP_CONTROL);
		I915_WRITE(RSTDBYCTL,
			   dev_priv->saveMCHBAR_RENDER_STANDBY);
	} else {
		I915_WRITE(PFIT_PGM_RATIOS, dev_priv->savePFIT_PGM_RATIOS);
		I915_WRITE(BLC_PWM_CTL, dev_priv->saveBLC_PWM_CTL);
		I915_WRITE(BLC_HIST_CTL, dev_priv->saveBLC_HIST_CTL);
		I915_WRITE(PP_ON_DELAYS, dev_priv->savePP_ON_DELAYS);
		I915_WRITE(PP_OFF_DELAYS, dev_priv->savePP_OFF_DELAYS);
		I915_WRITE(PP_DIVISOR, dev_priv->savePP_DIVISOR);
		I915_WRITE(PP_CONTROL, dev_priv->savePP_CONTROL);
	}

	/* Display Port state */
	if (SUPPORTS_INTEGRATED_DP(dev)) {
		I915_WRITE(DP_B, dev_priv->saveDP_B);
		I915_WRITE(DP_C, dev_priv->saveDP_C);
		I915_WRITE(DP_D, dev_priv->saveDP_D);
	}
	/* FIXME: restore TV & SDVO state */

	/* only restore FBC info on the platform that supports FBC*/
	if (I915_HAS_FBC(dev)) {
		if (HAS_PCH_SPLIT(dev)) {
			ironlake_disable_fbc(dev);
			I915_WRITE(ILK_DPFC_CB_BASE, dev_priv->saveDPFC_CB_BASE);
		} else if (IS_GM45(dev)) {
			g4x_disable_fbc(dev);
			I915_WRITE(DPFC_CB_BASE, dev_priv->saveDPFC_CB_BASE);
		} else {
			i8xx_disable_fbc(dev);
			I915_WRITE(FBC_CFB_BASE, dev_priv->saveFBC_CFB_BASE);
			I915_WRITE(FBC_LL_BASE, dev_priv->saveFBC_LL_BASE);
			I915_WRITE(FBC_CONTROL2, dev_priv->saveFBC_CONTROL2);
			I915_WRITE(FBC_CONTROL, dev_priv->saveFBC_CONTROL);
		}
	}
	/* VGA state */
	if (HAS_PCH_SPLIT(dev))
		I915_WRITE(CPU_VGACNTRL, dev_priv->saveVGACNTRL);
	else
		I915_WRITE(VGACNTRL, dev_priv->saveVGACNTRL);

	I915_WRITE(VGA0, dev_priv->saveVGA0);
	I915_WRITE(VGA1, dev_priv->saveVGA1);
	I915_WRITE(VGA_PD, dev_priv->saveVGA_PD);
	POSTING_READ(VGA_PD);
	udelay(150);

	i915_restore_vga(dev);
}

int i915_save_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	pci_read_config_byte(dev->pdev, LBB, &dev_priv->saveLBB);

	mutex_lock(&dev->struct_mutex);

	/* Hardware status page */
	dev_priv->saveHWS = I915_READ(HWS_PGA);

	i915_save_display(dev);

	/* Interrupt state */
	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->saveDEIER = I915_READ(DEIER);
		dev_priv->saveDEIMR = I915_READ(DEIMR);
		dev_priv->saveGTIER = I915_READ(GTIER);
		dev_priv->saveGTIMR = I915_READ(GTIMR);
		dev_priv->saveFDI_RXA_IMR = I915_READ(_FDI_RXA_IMR);
		dev_priv->saveFDI_RXB_IMR = I915_READ(_FDI_RXB_IMR);
		dev_priv->saveMCHBAR_RENDER_STANDBY =
			I915_READ(RSTDBYCTL);
	} else {
		dev_priv->saveIER = I915_READ(IER);
		dev_priv->saveIMR = I915_READ(IMR);
	}

	if (IS_IRONLAKE_M(dev))
		ironlake_disable_drps(dev);
	if (IS_GEN6(dev))
		gen6_disable_rps(dev);

	/* Cache mode state */
	dev_priv->saveCACHE_MODE_0 = I915_READ(CACHE_MODE_0);

	/* Memory Arbitration state */
	dev_priv->saveMI_ARB_STATE = I915_READ(MI_ARB_STATE);

	/* Scratch space */
	for (i = 0; i < 16; i++) {
		dev_priv->saveSWF0[i] = I915_READ(SWF00 + (i << 2));
		dev_priv->saveSWF1[i] = I915_READ(SWF10 + (i << 2));
	}
	for (i = 0; i < 3; i++)
		dev_priv->saveSWF2[i] = I915_READ(SWF30 + (i << 2));

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

int i915_restore_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	pci_write_config_byte(dev->pdev, LBB, dev_priv->saveLBB);

	mutex_lock(&dev->struct_mutex);

	/* Hardware status page */
	I915_WRITE(HWS_PGA, dev_priv->saveHWS);

	i915_restore_display(dev);

	/* Interrupt state */
	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(DEIER, dev_priv->saveDEIER);
		I915_WRITE(DEIMR, dev_priv->saveDEIMR);
		I915_WRITE(GTIER, dev_priv->saveGTIER);
		I915_WRITE(GTIMR, dev_priv->saveGTIMR);
		I915_WRITE(_FDI_RXA_IMR, dev_priv->saveFDI_RXA_IMR);
		I915_WRITE(_FDI_RXB_IMR, dev_priv->saveFDI_RXB_IMR);
	} else {
		I915_WRITE(IER, dev_priv->saveIER);
		I915_WRITE(IMR, dev_priv->saveIMR);
	}
	mutex_unlock(&dev->struct_mutex);

	intel_init_clock_gating(dev);

	if (IS_IRONLAKE_M(dev)) {
		ironlake_enable_drps(dev);
		intel_init_emon(dev);
	}

	if (IS_GEN6(dev))
		gen6_enable_rps(dev_priv);

	mutex_lock(&dev->struct_mutex);

	/* Cache mode state */
	I915_WRITE (CACHE_MODE_0, dev_priv->saveCACHE_MODE_0 | 0xffff0000);

	/* Memory arbitration state */
	I915_WRITE (MI_ARB_STATE, dev_priv->saveMI_ARB_STATE | 0xffff0000);

	for (i = 0; i < 16; i++) {
		I915_WRITE(SWF00 + (i << 2), dev_priv->saveSWF0[i]);
		I915_WRITE(SWF10 + (i << 2), dev_priv->saveSWF1[i]);
	}
	for (i = 0; i < 3; i++)
		I915_WRITE(SWF30 + (i << 2), dev_priv->saveSWF2[i]);

	mutex_unlock(&dev->struct_mutex);

	intel_i2c_reset(dev);

	return 0;
}
