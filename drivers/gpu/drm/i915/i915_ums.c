/*
 *
 * Copyright 2008 (c) Intel Corporation
 *   Jesse Barnes <jbarnes@virtuousgeek.org>
 * Copyright 2013 (c) Intel Corporation
 *   Daniel Vetter <daniel.vetter@ffwll.ch>
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

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "intel_drv.h"
#include "i915_reg.h"

static bool i915_pipe_enabled(struct drm_device *dev, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32	dpll_reg;

	/* On IVB, 3rd pipe shares PLL with another one */
	if (pipe > 1)
		return false;

	if (HAS_PCH_SPLIT(dev))
		dpll_reg = _PCH_DPLL(pipe);
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
		array = dev_priv->regfile.save_palette_a;
	else
		array = dev_priv->regfile.save_palette_b;

	for (i = 0; i < 256; i++)
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
		array = dev_priv->regfile.save_palette_a;
	else
		array = dev_priv->regfile.save_palette_b;

	for (i = 0; i < 256; i++)
		I915_WRITE(reg + (i << 2), array[i]);
}

void i915_save_display_reg(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	/* Cursor state */
	dev_priv->regfile.saveCURACNTR = I915_READ(_CURACNTR);
	dev_priv->regfile.saveCURAPOS = I915_READ(_CURAPOS);
	dev_priv->regfile.saveCURABASE = I915_READ(_CURABASE);
	dev_priv->regfile.saveCURBCNTR = I915_READ(_CURBCNTR);
	dev_priv->regfile.saveCURBPOS = I915_READ(_CURBPOS);
	dev_priv->regfile.saveCURBBASE = I915_READ(_CURBBASE);
	if (IS_GEN2(dev))
		dev_priv->regfile.saveCURSIZE = I915_READ(CURSIZE);

	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->regfile.savePCH_DREF_CONTROL = I915_READ(PCH_DREF_CONTROL);
		dev_priv->regfile.saveDISP_ARB_CTL = I915_READ(DISP_ARB_CTL);
	}

	/* Pipe & plane A info */
	dev_priv->regfile.savePIPEACONF = I915_READ(_PIPEACONF);
	dev_priv->regfile.savePIPEASRC = I915_READ(_PIPEASRC);
	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->regfile.saveFPA0 = I915_READ(_PCH_FPA0);
		dev_priv->regfile.saveFPA1 = I915_READ(_PCH_FPA1);
		dev_priv->regfile.saveDPLL_A = I915_READ(_PCH_DPLL_A);
	} else {
		dev_priv->regfile.saveFPA0 = I915_READ(_FPA0);
		dev_priv->regfile.saveFPA1 = I915_READ(_FPA1);
		dev_priv->regfile.saveDPLL_A = I915_READ(_DPLL_A);
	}
	if (INTEL_INFO(dev)->gen >= 4 && !HAS_PCH_SPLIT(dev))
		dev_priv->regfile.saveDPLL_A_MD = I915_READ(_DPLL_A_MD);
	dev_priv->regfile.saveHTOTAL_A = I915_READ(_HTOTAL_A);
	dev_priv->regfile.saveHBLANK_A = I915_READ(_HBLANK_A);
	dev_priv->regfile.saveHSYNC_A = I915_READ(_HSYNC_A);
	dev_priv->regfile.saveVTOTAL_A = I915_READ(_VTOTAL_A);
	dev_priv->regfile.saveVBLANK_A = I915_READ(_VBLANK_A);
	dev_priv->regfile.saveVSYNC_A = I915_READ(_VSYNC_A);
	if (!HAS_PCH_SPLIT(dev))
		dev_priv->regfile.saveBCLRPAT_A = I915_READ(_BCLRPAT_A);

	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->regfile.savePIPEA_DATA_M1 = I915_READ(_PIPEA_DATA_M1);
		dev_priv->regfile.savePIPEA_DATA_N1 = I915_READ(_PIPEA_DATA_N1);
		dev_priv->regfile.savePIPEA_LINK_M1 = I915_READ(_PIPEA_LINK_M1);
		dev_priv->regfile.savePIPEA_LINK_N1 = I915_READ(_PIPEA_LINK_N1);

		dev_priv->regfile.saveFDI_TXA_CTL = I915_READ(_FDI_TXA_CTL);
		dev_priv->regfile.saveFDI_RXA_CTL = I915_READ(_FDI_RXA_CTL);

		dev_priv->regfile.savePFA_CTL_1 = I915_READ(_PFA_CTL_1);
		dev_priv->regfile.savePFA_WIN_SZ = I915_READ(_PFA_WIN_SZ);
		dev_priv->regfile.savePFA_WIN_POS = I915_READ(_PFA_WIN_POS);

		dev_priv->regfile.saveTRANSACONF = I915_READ(_TRANSACONF);
		dev_priv->regfile.saveTRANS_HTOTAL_A = I915_READ(_TRANS_HTOTAL_A);
		dev_priv->regfile.saveTRANS_HBLANK_A = I915_READ(_TRANS_HBLANK_A);
		dev_priv->regfile.saveTRANS_HSYNC_A = I915_READ(_TRANS_HSYNC_A);
		dev_priv->regfile.saveTRANS_VTOTAL_A = I915_READ(_TRANS_VTOTAL_A);
		dev_priv->regfile.saveTRANS_VBLANK_A = I915_READ(_TRANS_VBLANK_A);
		dev_priv->regfile.saveTRANS_VSYNC_A = I915_READ(_TRANS_VSYNC_A);
	}

	dev_priv->regfile.saveDSPACNTR = I915_READ(_DSPACNTR);
	dev_priv->regfile.saveDSPASTRIDE = I915_READ(_DSPASTRIDE);
	dev_priv->regfile.saveDSPASIZE = I915_READ(_DSPASIZE);
	dev_priv->regfile.saveDSPAPOS = I915_READ(_DSPAPOS);
	dev_priv->regfile.saveDSPAADDR = I915_READ(_DSPAADDR);
	if (INTEL_INFO(dev)->gen >= 4) {
		dev_priv->regfile.saveDSPASURF = I915_READ(_DSPASURF);
		dev_priv->regfile.saveDSPATILEOFF = I915_READ(_DSPATILEOFF);
	}
	i915_save_palette(dev, PIPE_A);
	dev_priv->regfile.savePIPEASTAT = I915_READ(_PIPEASTAT);

	/* Pipe & plane B info */
	dev_priv->regfile.savePIPEBCONF = I915_READ(_PIPEBCONF);
	dev_priv->regfile.savePIPEBSRC = I915_READ(_PIPEBSRC);
	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->regfile.saveFPB0 = I915_READ(_PCH_FPB0);
		dev_priv->regfile.saveFPB1 = I915_READ(_PCH_FPB1);
		dev_priv->regfile.saveDPLL_B = I915_READ(_PCH_DPLL_B);
	} else {
		dev_priv->regfile.saveFPB0 = I915_READ(_FPB0);
		dev_priv->regfile.saveFPB1 = I915_READ(_FPB1);
		dev_priv->regfile.saveDPLL_B = I915_READ(_DPLL_B);
	}
	if (INTEL_INFO(dev)->gen >= 4 && !HAS_PCH_SPLIT(dev))
		dev_priv->regfile.saveDPLL_B_MD = I915_READ(_DPLL_B_MD);
	dev_priv->regfile.saveHTOTAL_B = I915_READ(_HTOTAL_B);
	dev_priv->regfile.saveHBLANK_B = I915_READ(_HBLANK_B);
	dev_priv->regfile.saveHSYNC_B = I915_READ(_HSYNC_B);
	dev_priv->regfile.saveVTOTAL_B = I915_READ(_VTOTAL_B);
	dev_priv->regfile.saveVBLANK_B = I915_READ(_VBLANK_B);
	dev_priv->regfile.saveVSYNC_B = I915_READ(_VSYNC_B);
	if (!HAS_PCH_SPLIT(dev))
		dev_priv->regfile.saveBCLRPAT_B = I915_READ(_BCLRPAT_B);

	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->regfile.savePIPEB_DATA_M1 = I915_READ(_PIPEB_DATA_M1);
		dev_priv->regfile.savePIPEB_DATA_N1 = I915_READ(_PIPEB_DATA_N1);
		dev_priv->regfile.savePIPEB_LINK_M1 = I915_READ(_PIPEB_LINK_M1);
		dev_priv->regfile.savePIPEB_LINK_N1 = I915_READ(_PIPEB_LINK_N1);

		dev_priv->regfile.saveFDI_TXB_CTL = I915_READ(_FDI_TXB_CTL);
		dev_priv->regfile.saveFDI_RXB_CTL = I915_READ(_FDI_RXB_CTL);

		dev_priv->regfile.savePFB_CTL_1 = I915_READ(_PFB_CTL_1);
		dev_priv->regfile.savePFB_WIN_SZ = I915_READ(_PFB_WIN_SZ);
		dev_priv->regfile.savePFB_WIN_POS = I915_READ(_PFB_WIN_POS);

		dev_priv->regfile.saveTRANSBCONF = I915_READ(_TRANSBCONF);
		dev_priv->regfile.saveTRANS_HTOTAL_B = I915_READ(_TRANS_HTOTAL_B);
		dev_priv->regfile.saveTRANS_HBLANK_B = I915_READ(_TRANS_HBLANK_B);
		dev_priv->regfile.saveTRANS_HSYNC_B = I915_READ(_TRANS_HSYNC_B);
		dev_priv->regfile.saveTRANS_VTOTAL_B = I915_READ(_TRANS_VTOTAL_B);
		dev_priv->regfile.saveTRANS_VBLANK_B = I915_READ(_TRANS_VBLANK_B);
		dev_priv->regfile.saveTRANS_VSYNC_B = I915_READ(_TRANS_VSYNC_B);
	}

	dev_priv->regfile.saveDSPBCNTR = I915_READ(_DSPBCNTR);
	dev_priv->regfile.saveDSPBSTRIDE = I915_READ(_DSPBSTRIDE);
	dev_priv->regfile.saveDSPBSIZE = I915_READ(_DSPBSIZE);
	dev_priv->regfile.saveDSPBPOS = I915_READ(_DSPBPOS);
	dev_priv->regfile.saveDSPBADDR = I915_READ(_DSPBADDR);
	if (INTEL_INFO(dev)->gen >= 4) {
		dev_priv->regfile.saveDSPBSURF = I915_READ(_DSPBSURF);
		dev_priv->regfile.saveDSPBTILEOFF = I915_READ(_DSPBTILEOFF);
	}
	i915_save_palette(dev, PIPE_B);
	dev_priv->regfile.savePIPEBSTAT = I915_READ(_PIPEBSTAT);

	/* Fences */
	switch (INTEL_INFO(dev)->gen) {
	case 7:
	case 6:
		for (i = 0; i < 16; i++)
			dev_priv->regfile.saveFENCE[i] = I915_READ64(FENCE_REG_SANDYBRIDGE_0 + (i * 8));
		break;
	case 5:
	case 4:
		for (i = 0; i < 16; i++)
			dev_priv->regfile.saveFENCE[i] = I915_READ64(FENCE_REG_965_0 + (i * 8));
		break;
	case 3:
		if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
			for (i = 0; i < 8; i++)
				dev_priv->regfile.saveFENCE[i+8] = I915_READ(FENCE_REG_945_8 + (i * 4));
	case 2:
		for (i = 0; i < 8; i++)
			dev_priv->regfile.saveFENCE[i] = I915_READ(FENCE_REG_830_0 + (i * 4));
		break;
	}

	/* CRT state */
	if (HAS_PCH_SPLIT(dev))
		dev_priv->regfile.saveADPA = I915_READ(PCH_ADPA);
	else
		dev_priv->regfile.saveADPA = I915_READ(ADPA);

	/* Display Port state */
	if (SUPPORTS_INTEGRATED_DP(dev)) {
		dev_priv->regfile.saveDP_B = I915_READ(DP_B);
		dev_priv->regfile.saveDP_C = I915_READ(DP_C);
		dev_priv->regfile.saveDP_D = I915_READ(DP_D);
		dev_priv->regfile.savePIPEA_GMCH_DATA_M = I915_READ(_PIPEA_GMCH_DATA_M);
		dev_priv->regfile.savePIPEB_GMCH_DATA_M = I915_READ(_PIPEB_GMCH_DATA_M);
		dev_priv->regfile.savePIPEA_GMCH_DATA_N = I915_READ(_PIPEA_GMCH_DATA_N);
		dev_priv->regfile.savePIPEB_GMCH_DATA_N = I915_READ(_PIPEB_GMCH_DATA_N);
		dev_priv->regfile.savePIPEA_DP_LINK_M = I915_READ(_PIPEA_DP_LINK_M);
		dev_priv->regfile.savePIPEB_DP_LINK_M = I915_READ(_PIPEB_DP_LINK_M);
		dev_priv->regfile.savePIPEA_DP_LINK_N = I915_READ(_PIPEA_DP_LINK_N);
		dev_priv->regfile.savePIPEB_DP_LINK_N = I915_READ(_PIPEB_DP_LINK_N);
	}
	/* FIXME: regfile.save TV & SDVO state */

	return;
}

void i915_restore_display_reg(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int dpll_a_reg, fpa0_reg, fpa1_reg;
	int dpll_b_reg, fpb0_reg, fpb1_reg;
	int i;

	/* Display port ratios (must be done before clock is set) */
	if (SUPPORTS_INTEGRATED_DP(dev)) {
		I915_WRITE(_PIPEA_GMCH_DATA_M, dev_priv->regfile.savePIPEA_GMCH_DATA_M);
		I915_WRITE(_PIPEB_GMCH_DATA_M, dev_priv->regfile.savePIPEB_GMCH_DATA_M);
		I915_WRITE(_PIPEA_GMCH_DATA_N, dev_priv->regfile.savePIPEA_GMCH_DATA_N);
		I915_WRITE(_PIPEB_GMCH_DATA_N, dev_priv->regfile.savePIPEB_GMCH_DATA_N);
		I915_WRITE(_PIPEA_DP_LINK_M, dev_priv->regfile.savePIPEA_DP_LINK_M);
		I915_WRITE(_PIPEB_DP_LINK_M, dev_priv->regfile.savePIPEB_DP_LINK_M);
		I915_WRITE(_PIPEA_DP_LINK_N, dev_priv->regfile.savePIPEA_DP_LINK_N);
		I915_WRITE(_PIPEB_DP_LINK_N, dev_priv->regfile.savePIPEB_DP_LINK_N);
	}

	/* Fences */
	switch (INTEL_INFO(dev)->gen) {
	case 7:
	case 6:
		for (i = 0; i < 16; i++)
			I915_WRITE64(FENCE_REG_SANDYBRIDGE_0 + (i * 8), dev_priv->regfile.saveFENCE[i]);
		break;
	case 5:
	case 4:
		for (i = 0; i < 16; i++)
			I915_WRITE64(FENCE_REG_965_0 + (i * 8), dev_priv->regfile.saveFENCE[i]);
		break;
	case 3:
	case 2:
		if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
			for (i = 0; i < 8; i++)
				I915_WRITE(FENCE_REG_945_8 + (i * 4), dev_priv->regfile.saveFENCE[i+8]);
		for (i = 0; i < 8; i++)
			I915_WRITE(FENCE_REG_830_0 + (i * 4), dev_priv->regfile.saveFENCE[i]);
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
		I915_WRITE(PCH_DREF_CONTROL, dev_priv->regfile.savePCH_DREF_CONTROL);
		I915_WRITE(DISP_ARB_CTL, dev_priv->regfile.saveDISP_ARB_CTL);
	}

	/* Pipe & plane A info */
	/* Prime the clock */
	if (dev_priv->regfile.saveDPLL_A & DPLL_VCO_ENABLE) {
		I915_WRITE(dpll_a_reg, dev_priv->regfile.saveDPLL_A &
			   ~DPLL_VCO_ENABLE);
		POSTING_READ(dpll_a_reg);
		udelay(150);
	}
	I915_WRITE(fpa0_reg, dev_priv->regfile.saveFPA0);
	I915_WRITE(fpa1_reg, dev_priv->regfile.saveFPA1);
	/* Actually enable it */
	I915_WRITE(dpll_a_reg, dev_priv->regfile.saveDPLL_A);
	POSTING_READ(dpll_a_reg);
	udelay(150);
	if (INTEL_INFO(dev)->gen >= 4 && !HAS_PCH_SPLIT(dev)) {
		I915_WRITE(_DPLL_A_MD, dev_priv->regfile.saveDPLL_A_MD);
		POSTING_READ(_DPLL_A_MD);
	}
	udelay(150);

	/* Restore mode */
	I915_WRITE(_HTOTAL_A, dev_priv->regfile.saveHTOTAL_A);
	I915_WRITE(_HBLANK_A, dev_priv->regfile.saveHBLANK_A);
	I915_WRITE(_HSYNC_A, dev_priv->regfile.saveHSYNC_A);
	I915_WRITE(_VTOTAL_A, dev_priv->regfile.saveVTOTAL_A);
	I915_WRITE(_VBLANK_A, dev_priv->regfile.saveVBLANK_A);
	I915_WRITE(_VSYNC_A, dev_priv->regfile.saveVSYNC_A);
	if (!HAS_PCH_SPLIT(dev))
		I915_WRITE(_BCLRPAT_A, dev_priv->regfile.saveBCLRPAT_A);

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(_PIPEA_DATA_M1, dev_priv->regfile.savePIPEA_DATA_M1);
		I915_WRITE(_PIPEA_DATA_N1, dev_priv->regfile.savePIPEA_DATA_N1);
		I915_WRITE(_PIPEA_LINK_M1, dev_priv->regfile.savePIPEA_LINK_M1);
		I915_WRITE(_PIPEA_LINK_N1, dev_priv->regfile.savePIPEA_LINK_N1);

		I915_WRITE(_FDI_RXA_CTL, dev_priv->regfile.saveFDI_RXA_CTL);
		I915_WRITE(_FDI_TXA_CTL, dev_priv->regfile.saveFDI_TXA_CTL);

		I915_WRITE(_PFA_CTL_1, dev_priv->regfile.savePFA_CTL_1);
		I915_WRITE(_PFA_WIN_SZ, dev_priv->regfile.savePFA_WIN_SZ);
		I915_WRITE(_PFA_WIN_POS, dev_priv->regfile.savePFA_WIN_POS);

		I915_WRITE(_TRANSACONF, dev_priv->regfile.saveTRANSACONF);
		I915_WRITE(_TRANS_HTOTAL_A, dev_priv->regfile.saveTRANS_HTOTAL_A);
		I915_WRITE(_TRANS_HBLANK_A, dev_priv->regfile.saveTRANS_HBLANK_A);
		I915_WRITE(_TRANS_HSYNC_A, dev_priv->regfile.saveTRANS_HSYNC_A);
		I915_WRITE(_TRANS_VTOTAL_A, dev_priv->regfile.saveTRANS_VTOTAL_A);
		I915_WRITE(_TRANS_VBLANK_A, dev_priv->regfile.saveTRANS_VBLANK_A);
		I915_WRITE(_TRANS_VSYNC_A, dev_priv->regfile.saveTRANS_VSYNC_A);
	}

	/* Restore plane info */
	I915_WRITE(_DSPASIZE, dev_priv->regfile.saveDSPASIZE);
	I915_WRITE(_DSPAPOS, dev_priv->regfile.saveDSPAPOS);
	I915_WRITE(_PIPEASRC, dev_priv->regfile.savePIPEASRC);
	I915_WRITE(_DSPAADDR, dev_priv->regfile.saveDSPAADDR);
	I915_WRITE(_DSPASTRIDE, dev_priv->regfile.saveDSPASTRIDE);
	if (INTEL_INFO(dev)->gen >= 4) {
		I915_WRITE(_DSPASURF, dev_priv->regfile.saveDSPASURF);
		I915_WRITE(_DSPATILEOFF, dev_priv->regfile.saveDSPATILEOFF);
	}

	I915_WRITE(_PIPEACONF, dev_priv->regfile.savePIPEACONF);

	i915_restore_palette(dev, PIPE_A);
	/* Enable the plane */
	I915_WRITE(_DSPACNTR, dev_priv->regfile.saveDSPACNTR);
	I915_WRITE(_DSPAADDR, I915_READ(_DSPAADDR));

	/* Pipe & plane B info */
	if (dev_priv->regfile.saveDPLL_B & DPLL_VCO_ENABLE) {
		I915_WRITE(dpll_b_reg, dev_priv->regfile.saveDPLL_B &
			   ~DPLL_VCO_ENABLE);
		POSTING_READ(dpll_b_reg);
		udelay(150);
	}
	I915_WRITE(fpb0_reg, dev_priv->regfile.saveFPB0);
	I915_WRITE(fpb1_reg, dev_priv->regfile.saveFPB1);
	/* Actually enable it */
	I915_WRITE(dpll_b_reg, dev_priv->regfile.saveDPLL_B);
	POSTING_READ(dpll_b_reg);
	udelay(150);
	if (INTEL_INFO(dev)->gen >= 4 && !HAS_PCH_SPLIT(dev)) {
		I915_WRITE(_DPLL_B_MD, dev_priv->regfile.saveDPLL_B_MD);
		POSTING_READ(_DPLL_B_MD);
	}
	udelay(150);

	/* Restore mode */
	I915_WRITE(_HTOTAL_B, dev_priv->regfile.saveHTOTAL_B);
	I915_WRITE(_HBLANK_B, dev_priv->regfile.saveHBLANK_B);
	I915_WRITE(_HSYNC_B, dev_priv->regfile.saveHSYNC_B);
	I915_WRITE(_VTOTAL_B, dev_priv->regfile.saveVTOTAL_B);
	I915_WRITE(_VBLANK_B, dev_priv->regfile.saveVBLANK_B);
	I915_WRITE(_VSYNC_B, dev_priv->regfile.saveVSYNC_B);
	if (!HAS_PCH_SPLIT(dev))
		I915_WRITE(_BCLRPAT_B, dev_priv->regfile.saveBCLRPAT_B);

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(_PIPEB_DATA_M1, dev_priv->regfile.savePIPEB_DATA_M1);
		I915_WRITE(_PIPEB_DATA_N1, dev_priv->regfile.savePIPEB_DATA_N1);
		I915_WRITE(_PIPEB_LINK_M1, dev_priv->regfile.savePIPEB_LINK_M1);
		I915_WRITE(_PIPEB_LINK_N1, dev_priv->regfile.savePIPEB_LINK_N1);

		I915_WRITE(_FDI_RXB_CTL, dev_priv->regfile.saveFDI_RXB_CTL);
		I915_WRITE(_FDI_TXB_CTL, dev_priv->regfile.saveFDI_TXB_CTL);

		I915_WRITE(_PFB_CTL_1, dev_priv->regfile.savePFB_CTL_1);
		I915_WRITE(_PFB_WIN_SZ, dev_priv->regfile.savePFB_WIN_SZ);
		I915_WRITE(_PFB_WIN_POS, dev_priv->regfile.savePFB_WIN_POS);

		I915_WRITE(_TRANSBCONF, dev_priv->regfile.saveTRANSBCONF);
		I915_WRITE(_TRANS_HTOTAL_B, dev_priv->regfile.saveTRANS_HTOTAL_B);
		I915_WRITE(_TRANS_HBLANK_B, dev_priv->regfile.saveTRANS_HBLANK_B);
		I915_WRITE(_TRANS_HSYNC_B, dev_priv->regfile.saveTRANS_HSYNC_B);
		I915_WRITE(_TRANS_VTOTAL_B, dev_priv->regfile.saveTRANS_VTOTAL_B);
		I915_WRITE(_TRANS_VBLANK_B, dev_priv->regfile.saveTRANS_VBLANK_B);
		I915_WRITE(_TRANS_VSYNC_B, dev_priv->regfile.saveTRANS_VSYNC_B);
	}

	/* Restore plane info */
	I915_WRITE(_DSPBSIZE, dev_priv->regfile.saveDSPBSIZE);
	I915_WRITE(_DSPBPOS, dev_priv->regfile.saveDSPBPOS);
	I915_WRITE(_PIPEBSRC, dev_priv->regfile.savePIPEBSRC);
	I915_WRITE(_DSPBADDR, dev_priv->regfile.saveDSPBADDR);
	I915_WRITE(_DSPBSTRIDE, dev_priv->regfile.saveDSPBSTRIDE);
	if (INTEL_INFO(dev)->gen >= 4) {
		I915_WRITE(_DSPBSURF, dev_priv->regfile.saveDSPBSURF);
		I915_WRITE(_DSPBTILEOFF, dev_priv->regfile.saveDSPBTILEOFF);
	}

	I915_WRITE(_PIPEBCONF, dev_priv->regfile.savePIPEBCONF);

	i915_restore_palette(dev, PIPE_B);
	/* Enable the plane */
	I915_WRITE(_DSPBCNTR, dev_priv->regfile.saveDSPBCNTR);
	I915_WRITE(_DSPBADDR, I915_READ(_DSPBADDR));

	/* Cursor state */
	I915_WRITE(_CURAPOS, dev_priv->regfile.saveCURAPOS);
	I915_WRITE(_CURACNTR, dev_priv->regfile.saveCURACNTR);
	I915_WRITE(_CURABASE, dev_priv->regfile.saveCURABASE);
	I915_WRITE(_CURBPOS, dev_priv->regfile.saveCURBPOS);
	I915_WRITE(_CURBCNTR, dev_priv->regfile.saveCURBCNTR);
	I915_WRITE(_CURBBASE, dev_priv->regfile.saveCURBBASE);
	if (IS_GEN2(dev))
		I915_WRITE(CURSIZE, dev_priv->regfile.saveCURSIZE);

	/* CRT state */
	if (HAS_PCH_SPLIT(dev))
		I915_WRITE(PCH_ADPA, dev_priv->regfile.saveADPA);
	else
		I915_WRITE(ADPA, dev_priv->regfile.saveADPA);

	/* Display Port state */
	if (SUPPORTS_INTEGRATED_DP(dev)) {
		I915_WRITE(DP_B, dev_priv->regfile.saveDP_B);
		I915_WRITE(DP_C, dev_priv->regfile.saveDP_C);
		I915_WRITE(DP_D, dev_priv->regfile.saveDP_D);
	}
	/* FIXME: restore TV & SDVO state */

	return;
}
