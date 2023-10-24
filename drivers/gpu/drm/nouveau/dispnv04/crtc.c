/*
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include "nouveau_drv.h"
#include "nouveau_reg.h"
#include "nouveau_ttm.h"
#include "nouveau_bo.h"
#include "nouveau_gem.h"
#include "nouveau_encoder.h"
#include "nouveau_connector.h"
#include "nouveau_crtc.h"
#include "hw.h"
#include "nvreg.h"
#include "disp.h"
#include "nouveau_dma.h"

#include <subdev/bios/pll.h>
#include <subdev/clk.h>

#include <nvif/push006c.h>

#include <nvif/event.h>
#include <nvif/cl0046.h>

static int
nv04_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
			struct drm_framebuffer *old_fb);

static void
crtc_wr_cio_state(struct drm_crtc *crtc, struct nv04_crtc_reg *crtcstate, int index)
{
	NVWriteVgaCrtc(crtc->dev, nouveau_crtc(crtc)->index, index,
		       crtcstate->CRTC[index]);
}

static void nv_crtc_set_digital_vibrance(struct drm_crtc *crtc, int level)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct nv04_crtc_reg *regp = &nv04_display(dev)->mode_reg.crtc_reg[nv_crtc->index];

	regp->CRTC[NV_CIO_CRE_CSB] = nv_crtc->saturation = level;
	if (nv_crtc->saturation && nv_gf4_disp_arch(crtc->dev)) {
		regp->CRTC[NV_CIO_CRE_CSB] = 0x80;
		regp->CRTC[NV_CIO_CRE_5B] = nv_crtc->saturation << 2;
		crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_5B);
	}
	crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_CSB);
}

static void nv_crtc_set_image_sharpening(struct drm_crtc *crtc, int level)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct nv04_crtc_reg *regp = &nv04_display(dev)->mode_reg.crtc_reg[nv_crtc->index];

	nv_crtc->sharpness = level;
	if (level < 0)	/* blur is in hw range 0x3f -> 0x20 */
		level += 0x40;
	regp->ramdac_634 = level;
	NVWriteRAMDAC(crtc->dev, nv_crtc->index, NV_PRAMDAC_634, regp->ramdac_634);
}

#define PLLSEL_VPLL1_MASK				\
	(NV_PRAMDAC_PLL_COEFF_SELECT_SOURCE_PROG_VPLL	\
	 | NV_PRAMDAC_PLL_COEFF_SELECT_VCLK_RATIO_DB2)
#define PLLSEL_VPLL2_MASK				\
	(NV_PRAMDAC_PLL_COEFF_SELECT_PLL_SOURCE_VPLL2		\
	 | NV_PRAMDAC_PLL_COEFF_SELECT_VCLK2_RATIO_DB2)
#define PLLSEL_TV_MASK					\
	(NV_PRAMDAC_PLL_COEFF_SELECT_TV_VSCLK1		\
	 | NV_PRAMDAC_PLL_COEFF_SELECT_TV_PCLK1		\
	 | NV_PRAMDAC_PLL_COEFF_SELECT_TV_VSCLK2	\
	 | NV_PRAMDAC_PLL_COEFF_SELECT_TV_PCLK2)

/* NV4x 0x40.. pll notes:
 * gpu pll: 0x4000 + 0x4004
 * ?gpu? pll: 0x4008 + 0x400c
 * vpll1: 0x4010 + 0x4014
 * vpll2: 0x4018 + 0x401c
 * mpll: 0x4020 + 0x4024
 * mpll: 0x4038 + 0x403c
 *
 * the first register of each pair has some unknown details:
 * bits 0-7: redirected values from elsewhere? (similar to PLL_SETUP_CONTROL?)
 * bits 20-23: (mpll) something to do with post divider?
 * bits 28-31: related to single stage mode? (bit 8/12)
 */

static void nv_crtc_calc_state_ext(struct drm_crtc *crtc, struct drm_display_mode * mode, int dot_clock)
{
	struct drm_device *dev = crtc->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_bios *bios = nvxx_bios(&drm->client.device);
	struct nvkm_clk *clk = nvxx_clk(&drm->client.device);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv04_mode_state *state = &nv04_display(dev)->mode_reg;
	struct nv04_crtc_reg *regp = &state->crtc_reg[nv_crtc->index];
	struct nvkm_pll_vals *pv = &regp->pllvals;
	struct nvbios_pll pll_lim;

	if (nvbios_pll_parse(bios, nv_crtc->index ? PLL_VPLL1 : PLL_VPLL0,
			    &pll_lim))
		return;

	/* NM2 == 0 is used to determine single stage mode on two stage plls */
	pv->NM2 = 0;

	/* for newer nv4x the blob uses only the first stage of the vpll below a
	 * certain clock.  for a certain nv4b this is 150MHz.  since the max
	 * output frequency of the first stage for this card is 300MHz, it is
	 * assumed the threshold is given by vco1 maxfreq/2
	 */
	/* for early nv4x, specifically nv40 and *some* nv43 (devids 0 and 6,
	 * not 8, others unknown), the blob always uses both plls.  no problem
	 * has yet been observed in allowing the use a single stage pll on all
	 * nv43 however.  the behaviour of single stage use is untested on nv40
	 */
	if (drm->client.device.info.chipset > 0x40 && dot_clock <= (pll_lim.vco1.max_freq / 2))
		memset(&pll_lim.vco2, 0, sizeof(pll_lim.vco2));


	if (!clk->pll_calc(clk, &pll_lim, dot_clock, pv))
		return;

	state->pllsel &= PLLSEL_VPLL1_MASK | PLLSEL_VPLL2_MASK | PLLSEL_TV_MASK;

	/* The blob uses this always, so let's do the same */
	if (drm->client.device.info.family == NV_DEVICE_INFO_V0_CURIE)
		state->pllsel |= NV_PRAMDAC_PLL_COEFF_SELECT_USE_VPLL2_TRUE;
	/* again nv40 and some nv43 act more like nv3x as described above */
	if (drm->client.device.info.chipset < 0x41)
		state->pllsel |= NV_PRAMDAC_PLL_COEFF_SELECT_SOURCE_PROG_MPLL |
				 NV_PRAMDAC_PLL_COEFF_SELECT_SOURCE_PROG_NVPLL;
	state->pllsel |= nv_crtc->index ? PLLSEL_VPLL2_MASK : PLLSEL_VPLL1_MASK;

	if (pv->NM2)
		NV_DEBUG(drm, "vpll: n1 %d n2 %d m1 %d m2 %d log2p %d\n",
			 pv->N1, pv->N2, pv->M1, pv->M2, pv->log2P);
	else
		NV_DEBUG(drm, "vpll: n %d m %d log2p %d\n",
			 pv->N1, pv->M1, pv->log2P);

	nv_crtc->cursor.set_offset(nv_crtc, nv_crtc->cursor.offset);
}

static void
nv_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	unsigned char seq1 = 0, crtc17 = 0;
	unsigned char crtc1A;

	NV_DEBUG(drm, "Setting dpms mode %d on CRTC %d\n", mode,
							nv_crtc->index);

	if (nv_crtc->last_dpms == mode) /* Don't do unnecessary mode changes. */
		return;

	nv_crtc->last_dpms = mode;

	if (nv_two_heads(dev))
		NVSetOwner(dev, nv_crtc->index);

	/* nv4ref indicates these two RPC1 bits inhibit h/v sync */
	crtc1A = NVReadVgaCrtc(dev, nv_crtc->index,
					NV_CIO_CRE_RPC1_INDEX) & ~0xC0;
	switch (mode) {
	case DRM_MODE_DPMS_STANDBY:
		/* Screen: Off; HSync: Off, VSync: On -- Not Supported */
		seq1 = 0x20;
		crtc17 = 0x80;
		crtc1A |= 0x80;
		break;
	case DRM_MODE_DPMS_SUSPEND:
		/* Screen: Off; HSync: On, VSync: Off -- Not Supported */
		seq1 = 0x20;
		crtc17 = 0x80;
		crtc1A |= 0x40;
		break;
	case DRM_MODE_DPMS_OFF:
		/* Screen: Off; HSync: Off, VSync: Off */
		seq1 = 0x20;
		crtc17 = 0x00;
		crtc1A |= 0xC0;
		break;
	case DRM_MODE_DPMS_ON:
	default:
		/* Screen: On; HSync: On, VSync: On */
		seq1 = 0x00;
		crtc17 = 0x80;
		break;
	}

	NVVgaSeqReset(dev, nv_crtc->index, true);
	/* Each head has it's own sequencer, so we can turn it off when we want */
	seq1 |= (NVReadVgaSeq(dev, nv_crtc->index, NV_VIO_SR_CLOCK_INDEX) & ~0x20);
	NVWriteVgaSeq(dev, nv_crtc->index, NV_VIO_SR_CLOCK_INDEX, seq1);
	crtc17 |= (NVReadVgaCrtc(dev, nv_crtc->index, NV_CIO_CR_MODE_INDEX) & ~0x80);
	mdelay(10);
	NVWriteVgaCrtc(dev, nv_crtc->index, NV_CIO_CR_MODE_INDEX, crtc17);
	NVVgaSeqReset(dev, nv_crtc->index, false);

	NVWriteVgaCrtc(dev, nv_crtc->index, NV_CIO_CRE_RPC1_INDEX, crtc1A);
}

static void
nv_crtc_mode_set_vga(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv04_crtc_reg *regp = &nv04_display(dev)->mode_reg.crtc_reg[nv_crtc->index];
	struct drm_framebuffer *fb = crtc->primary->fb;

	/* Calculate our timings */
	int horizDisplay	= (mode->crtc_hdisplay >> 3)		- 1;
	int horizStart		= (mode->crtc_hsync_start >> 3) 	+ 1;
	int horizEnd		= (mode->crtc_hsync_end >> 3)		+ 1;
	int horizTotal		= (mode->crtc_htotal >> 3)		- 5;
	int horizBlankStart	= (mode->crtc_hdisplay >> 3)		- 1;
	int horizBlankEnd	= (mode->crtc_htotal >> 3)		- 1;
	int vertDisplay		= mode->crtc_vdisplay			- 1;
	int vertStart		= mode->crtc_vsync_start 		- 1;
	int vertEnd		= mode->crtc_vsync_end			- 1;
	int vertTotal		= mode->crtc_vtotal 			- 2;
	int vertBlankStart	= mode->crtc_vdisplay 			- 1;
	int vertBlankEnd	= mode->crtc_vtotal			- 1;

	struct drm_encoder *encoder;
	bool fp_output = false;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);

		if (encoder->crtc == crtc &&
		    (nv_encoder->dcb->type == DCB_OUTPUT_LVDS ||
		     nv_encoder->dcb->type == DCB_OUTPUT_TMDS))
			fp_output = true;
	}

	if (fp_output) {
		vertStart = vertTotal - 3;
		vertEnd = vertTotal - 2;
		vertBlankStart = vertStart;
		horizStart = horizTotal - 5;
		horizEnd = horizTotal - 2;
		horizBlankEnd = horizTotal + 4;
#if 0
		if (dev->overlayAdaptor && drm->client.device.info.family >= NV_DEVICE_INFO_V0_CELSIUS)
			/* This reportedly works around some video overlay bandwidth problems */
			horizTotal += 2;
#endif
	}

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vertTotal |= 1;

#if 0
	ErrorF("horizDisplay: 0x%X \n", horizDisplay);
	ErrorF("horizStart: 0x%X \n", horizStart);
	ErrorF("horizEnd: 0x%X \n", horizEnd);
	ErrorF("horizTotal: 0x%X \n", horizTotal);
	ErrorF("horizBlankStart: 0x%X \n", horizBlankStart);
	ErrorF("horizBlankEnd: 0x%X \n", horizBlankEnd);
	ErrorF("vertDisplay: 0x%X \n", vertDisplay);
	ErrorF("vertStart: 0x%X \n", vertStart);
	ErrorF("vertEnd: 0x%X \n", vertEnd);
	ErrorF("vertTotal: 0x%X \n", vertTotal);
	ErrorF("vertBlankStart: 0x%X \n", vertBlankStart);
	ErrorF("vertBlankEnd: 0x%X \n", vertBlankEnd);
#endif

	/*
	* compute correct Hsync & Vsync polarity
	*/
	if ((mode->flags & (DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NHSYNC))
		&& (mode->flags & (DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_NVSYNC))) {

		regp->MiscOutReg = 0x23;
		if (mode->flags & DRM_MODE_FLAG_NHSYNC)
			regp->MiscOutReg |= 0x40;
		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			regp->MiscOutReg |= 0x80;
	} else {
		int vdisplay = mode->vdisplay;
		if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
			vdisplay *= 2;
		if (mode->vscan > 1)
			vdisplay *= mode->vscan;
		if (vdisplay < 400)
			regp->MiscOutReg = 0xA3;	/* +hsync -vsync */
		else if (vdisplay < 480)
			regp->MiscOutReg = 0x63;	/* -hsync +vsync */
		else if (vdisplay < 768)
			regp->MiscOutReg = 0xE3;	/* -hsync -vsync */
		else
			regp->MiscOutReg = 0x23;	/* +hsync +vsync */
	}

	/*
	 * Time Sequencer
	 */
	regp->Sequencer[NV_VIO_SR_RESET_INDEX] = 0x00;
	/* 0x20 disables the sequencer */
	if (mode->flags & DRM_MODE_FLAG_CLKDIV2)
		regp->Sequencer[NV_VIO_SR_CLOCK_INDEX] = 0x29;
	else
		regp->Sequencer[NV_VIO_SR_CLOCK_INDEX] = 0x21;
	regp->Sequencer[NV_VIO_SR_PLANE_MASK_INDEX] = 0x0F;
	regp->Sequencer[NV_VIO_SR_CHAR_MAP_INDEX] = 0x00;
	regp->Sequencer[NV_VIO_SR_MEM_MODE_INDEX] = 0x0E;

	/*
	 * CRTC
	 */
	regp->CRTC[NV_CIO_CR_HDT_INDEX] = horizTotal;
	regp->CRTC[NV_CIO_CR_HDE_INDEX] = horizDisplay;
	regp->CRTC[NV_CIO_CR_HBS_INDEX] = horizBlankStart;
	regp->CRTC[NV_CIO_CR_HBE_INDEX] = (1 << 7) |
					  XLATE(horizBlankEnd, 0, NV_CIO_CR_HBE_4_0);
	regp->CRTC[NV_CIO_CR_HRS_INDEX] = horizStart;
	regp->CRTC[NV_CIO_CR_HRE_INDEX] = XLATE(horizBlankEnd, 5, NV_CIO_CR_HRE_HBE_5) |
					  XLATE(horizEnd, 0, NV_CIO_CR_HRE_4_0);
	regp->CRTC[NV_CIO_CR_VDT_INDEX] = vertTotal;
	regp->CRTC[NV_CIO_CR_OVL_INDEX] = XLATE(vertStart, 9, NV_CIO_CR_OVL_VRS_9) |
					  XLATE(vertDisplay, 9, NV_CIO_CR_OVL_VDE_9) |
					  XLATE(vertTotal, 9, NV_CIO_CR_OVL_VDT_9) |
					  (1 << 4) |
					  XLATE(vertBlankStart, 8, NV_CIO_CR_OVL_VBS_8) |
					  XLATE(vertStart, 8, NV_CIO_CR_OVL_VRS_8) |
					  XLATE(vertDisplay, 8, NV_CIO_CR_OVL_VDE_8) |
					  XLATE(vertTotal, 8, NV_CIO_CR_OVL_VDT_8);
	regp->CRTC[NV_CIO_CR_RSAL_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_CELL_HT_INDEX] = ((mode->flags & DRM_MODE_FLAG_DBLSCAN) ? MASK(NV_CIO_CR_CELL_HT_SCANDBL) : 0) |
					      1 << 6 |
					      XLATE(vertBlankStart, 9, NV_CIO_CR_CELL_HT_VBS_9);
	regp->CRTC[NV_CIO_CR_CURS_ST_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_CURS_END_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_SA_HI_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_SA_LO_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_TCOFF_HI_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_TCOFF_LO_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_VRS_INDEX] = vertStart;
	regp->CRTC[NV_CIO_CR_VRE_INDEX] = 1 << 5 | XLATE(vertEnd, 0, NV_CIO_CR_VRE_3_0);
	regp->CRTC[NV_CIO_CR_VDE_INDEX] = vertDisplay;
	/* framebuffer can be larger than crtc scanout area. */
	regp->CRTC[NV_CIO_CR_OFFSET_INDEX] = fb->pitches[0] / 8;
	regp->CRTC[NV_CIO_CR_ULINE_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_VBS_INDEX] = vertBlankStart;
	regp->CRTC[NV_CIO_CR_VBE_INDEX] = vertBlankEnd;
	regp->CRTC[NV_CIO_CR_MODE_INDEX] = 0x43;
	regp->CRTC[NV_CIO_CR_LCOMP_INDEX] = 0xff;

	/*
	 * Some extended CRTC registers (they are not saved with the rest of the vga regs).
	 */

	/* framebuffer can be larger than crtc scanout area. */
	regp->CRTC[NV_CIO_CRE_RPC0_INDEX] =
		XLATE(fb->pitches[0] / 8, 8, NV_CIO_CRE_RPC0_OFFSET_10_8);
	regp->CRTC[NV_CIO_CRE_42] =
		XLATE(fb->pitches[0] / 8, 11, NV_CIO_CRE_42_OFFSET_11);
	regp->CRTC[NV_CIO_CRE_RPC1_INDEX] = mode->crtc_hdisplay < 1280 ?
					    MASK(NV_CIO_CRE_RPC1_LARGE) : 0x00;
	regp->CRTC[NV_CIO_CRE_LSR_INDEX] = XLATE(horizBlankEnd, 6, NV_CIO_CRE_LSR_HBE_6) |
					   XLATE(vertBlankStart, 10, NV_CIO_CRE_LSR_VBS_10) |
					   XLATE(vertStart, 10, NV_CIO_CRE_LSR_VRS_10) |
					   XLATE(vertDisplay, 10, NV_CIO_CRE_LSR_VDE_10) |
					   XLATE(vertTotal, 10, NV_CIO_CRE_LSR_VDT_10);
	regp->CRTC[NV_CIO_CRE_HEB__INDEX] = XLATE(horizStart, 8, NV_CIO_CRE_HEB_HRS_8) |
					    XLATE(horizBlankStart, 8, NV_CIO_CRE_HEB_HBS_8) |
					    XLATE(horizDisplay, 8, NV_CIO_CRE_HEB_HDE_8) |
					    XLATE(horizTotal, 8, NV_CIO_CRE_HEB_HDT_8);
	regp->CRTC[NV_CIO_CRE_EBR_INDEX] = XLATE(vertBlankStart, 11, NV_CIO_CRE_EBR_VBS_11) |
					   XLATE(vertStart, 11, NV_CIO_CRE_EBR_VRS_11) |
					   XLATE(vertDisplay, 11, NV_CIO_CRE_EBR_VDE_11) |
					   XLATE(vertTotal, 11, NV_CIO_CRE_EBR_VDT_11);

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		horizTotal = (horizTotal >> 1) & ~1;
		regp->CRTC[NV_CIO_CRE_ILACE__INDEX] = horizTotal;
		regp->CRTC[NV_CIO_CRE_HEB__INDEX] |= XLATE(horizTotal, 8, NV_CIO_CRE_HEB_ILC_8);
	} else
		regp->CRTC[NV_CIO_CRE_ILACE__INDEX] = 0xff;  /* interlace off */

	/*
	* Graphics Display Controller
	*/
	regp->Graphics[NV_VIO_GX_SR_INDEX] = 0x00;
	regp->Graphics[NV_VIO_GX_SREN_INDEX] = 0x00;
	regp->Graphics[NV_VIO_GX_CCOMP_INDEX] = 0x00;
	regp->Graphics[NV_VIO_GX_ROP_INDEX] = 0x00;
	regp->Graphics[NV_VIO_GX_READ_MAP_INDEX] = 0x00;
	regp->Graphics[NV_VIO_GX_MODE_INDEX] = 0x40; /* 256 color mode */
	regp->Graphics[NV_VIO_GX_MISC_INDEX] = 0x05; /* map 64k mem + graphic mode */
	regp->Graphics[NV_VIO_GX_DONT_CARE_INDEX] = 0x0F;
	regp->Graphics[NV_VIO_GX_BIT_MASK_INDEX] = 0xFF;

	regp->Attribute[0]  = 0x00; /* standard colormap translation */
	regp->Attribute[1]  = 0x01;
	regp->Attribute[2]  = 0x02;
	regp->Attribute[3]  = 0x03;
	regp->Attribute[4]  = 0x04;
	regp->Attribute[5]  = 0x05;
	regp->Attribute[6]  = 0x06;
	regp->Attribute[7]  = 0x07;
	regp->Attribute[8]  = 0x08;
	regp->Attribute[9]  = 0x09;
	regp->Attribute[10] = 0x0A;
	regp->Attribute[11] = 0x0B;
	regp->Attribute[12] = 0x0C;
	regp->Attribute[13] = 0x0D;
	regp->Attribute[14] = 0x0E;
	regp->Attribute[15] = 0x0F;
	regp->Attribute[NV_CIO_AR_MODE_INDEX] = 0x01; /* Enable graphic mode */
	/* Non-vga */
	regp->Attribute[NV_CIO_AR_OSCAN_INDEX] = 0x00;
	regp->Attribute[NV_CIO_AR_PLANE_INDEX] = 0x0F; /* enable all color planes */
	regp->Attribute[NV_CIO_AR_HPP_INDEX] = 0x00;
	regp->Attribute[NV_CIO_AR_CSEL_INDEX] = 0x00;
}

/**
 * Sets up registers for the given mode/adjusted_mode pair.
 *
 * The clocks, CRTCs and outputs attached to this CRTC must be off.
 *
 * This shouldn't enable any clocks, CRTCs, or outputs, but they should
 * be easily turned on/off after this.
 */
static void
nv_crtc_mode_set_regs(struct drm_crtc *crtc, struct drm_display_mode * mode)
{
	struct drm_device *dev = crtc->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv04_crtc_reg *regp = &nv04_display(dev)->mode_reg.crtc_reg[nv_crtc->index];
	struct nv04_crtc_reg *savep = &nv04_display(dev)->saved_reg.crtc_reg[nv_crtc->index];
	const struct drm_framebuffer *fb = crtc->primary->fb;
	struct drm_encoder *encoder;
	bool lvds_output = false, tmds_output = false, tv_output = false,
		off_chip_digital = false;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
		bool digital = false;

		if (encoder->crtc != crtc)
			continue;

		if (nv_encoder->dcb->type == DCB_OUTPUT_LVDS)
			digital = lvds_output = true;
		if (nv_encoder->dcb->type == DCB_OUTPUT_TV)
			tv_output = true;
		if (nv_encoder->dcb->type == DCB_OUTPUT_TMDS)
			digital = tmds_output = true;
		if (nv_encoder->dcb->location != DCB_LOC_ON_CHIP && digital)
			off_chip_digital = true;
	}

	/* Registers not directly related to the (s)vga mode */

	/* What is the meaning of this register? */
	/* A few popular values are 0x18, 0x1c, 0x38, 0x3c */
	regp->CRTC[NV_CIO_CRE_ENH_INDEX] = savep->CRTC[NV_CIO_CRE_ENH_INDEX] & ~(1<<5);

	regp->crtc_eng_ctrl = 0;
	/* Except for rare conditions I2C is enabled on the primary crtc */
	if (nv_crtc->index == 0)
		regp->crtc_eng_ctrl |= NV_CRTC_FSEL_I2C;
#if 0
	/* Set overlay to desired crtc. */
	if (dev->overlayAdaptor) {
		NVPortPrivPtr pPriv = GET_OVERLAY_PRIVATE(dev);
		if (pPriv->overlayCRTC == nv_crtc->index)
			regp->crtc_eng_ctrl |= NV_CRTC_FSEL_OVERLAY;
	}
#endif

	/* ADDRESS_SPACE_PNVM is the same as setting HCUR_ASI */
	regp->cursor_cfg = NV_PCRTC_CURSOR_CONFIG_CUR_LINES_64 |
			     NV_PCRTC_CURSOR_CONFIG_CUR_PIXELS_64 |
			     NV_PCRTC_CURSOR_CONFIG_ADDRESS_SPACE_PNVM;
	if (drm->client.device.info.chipset >= 0x11)
		regp->cursor_cfg |= NV_PCRTC_CURSOR_CONFIG_CUR_BPP_32;
	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		regp->cursor_cfg |= NV_PCRTC_CURSOR_CONFIG_DOUBLE_SCAN_ENABLE;

	/* Unblock some timings */
	regp->CRTC[NV_CIO_CRE_53] = 0;
	regp->CRTC[NV_CIO_CRE_54] = 0;

	/* 0x00 is disabled, 0x11 is lvds, 0x22 crt and 0x88 tmds */
	if (lvds_output)
		regp->CRTC[NV_CIO_CRE_SCRATCH3__INDEX] = 0x11;
	else if (tmds_output)
		regp->CRTC[NV_CIO_CRE_SCRATCH3__INDEX] = 0x88;
	else
		regp->CRTC[NV_CIO_CRE_SCRATCH3__INDEX] = 0x22;

	/* These values seem to vary */
	/* This register seems to be used by the bios to make certain decisions on some G70 cards? */
	regp->CRTC[NV_CIO_CRE_SCRATCH4__INDEX] = savep->CRTC[NV_CIO_CRE_SCRATCH4__INDEX];

	nv_crtc_set_digital_vibrance(crtc, nv_crtc->saturation);

	/* probably a scratch reg, but kept for cargo-cult purposes:
	 * bit0: crtc0?, head A
	 * bit6: lvds, head A
	 * bit7: (only in X), head A
	 */
	if (nv_crtc->index == 0)
		regp->CRTC[NV_CIO_CRE_4B] = savep->CRTC[NV_CIO_CRE_4B] | 0x80;

	/* The blob seems to take the current value from crtc 0, add 4 to that
	 * and reuse the old value for crtc 1 */
	regp->CRTC[NV_CIO_CRE_TVOUT_LATENCY] = nv04_display(dev)->saved_reg.crtc_reg[0].CRTC[NV_CIO_CRE_TVOUT_LATENCY];
	if (!nv_crtc->index)
		regp->CRTC[NV_CIO_CRE_TVOUT_LATENCY] += 4;

	/* the blob sometimes sets |= 0x10 (which is the same as setting |=
	 * 1 << 30 on 0x60.830), for no apparent reason */
	regp->CRTC[NV_CIO_CRE_59] = off_chip_digital;

	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_RANKINE)
		regp->CRTC[0x9f] = off_chip_digital ? 0x11 : 0x1;

	regp->crtc_830 = mode->crtc_vdisplay - 3;
	regp->crtc_834 = mode->crtc_vdisplay - 1;

	if (drm->client.device.info.family == NV_DEVICE_INFO_V0_CURIE)
		/* This is what the blob does */
		regp->crtc_850 = NVReadCRTC(dev, 0, NV_PCRTC_850);

	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_RANKINE)
		regp->gpio_ext = NVReadCRTC(dev, 0, NV_PCRTC_GPIO_EXT);

	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_CELSIUS)
		regp->crtc_cfg = NV10_PCRTC_CONFIG_START_ADDRESS_HSYNC;
	else
		regp->crtc_cfg = NV04_PCRTC_CONFIG_START_ADDRESS_HSYNC;

	/* Some misc regs */
	if (drm->client.device.info.family == NV_DEVICE_INFO_V0_CURIE) {
		regp->CRTC[NV_CIO_CRE_85] = 0xFF;
		regp->CRTC[NV_CIO_CRE_86] = 0x1;
	}

	regp->CRTC[NV_CIO_CRE_PIXEL_INDEX] = (fb->format->depth + 1) / 8;
	/* Enable slaved mode (called MODE_TV in nv4ref.h) */
	if (lvds_output || tmds_output || tv_output)
		regp->CRTC[NV_CIO_CRE_PIXEL_INDEX] |= (1 << 7);

	/* Generic PRAMDAC regs */

	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_CELSIUS)
		/* Only bit that bios and blob set. */
		regp->nv10_cursync = (1 << 25);

	regp->ramdac_gen_ctrl = NV_PRAMDAC_GENERAL_CONTROL_BPC_8BITS |
				NV_PRAMDAC_GENERAL_CONTROL_VGA_STATE_SEL |
				NV_PRAMDAC_GENERAL_CONTROL_PIXMIX_ON;
	if (fb->format->depth == 16)
		regp->ramdac_gen_ctrl |= NV_PRAMDAC_GENERAL_CONTROL_ALT_MODE_SEL;
	if (drm->client.device.info.chipset >= 0x11)
		regp->ramdac_gen_ctrl |= NV_PRAMDAC_GENERAL_CONTROL_PIPE_LONG;

	regp->ramdac_630 = 0; /* turn off green mode (tv test pattern?) */
	regp->tv_setup = 0;

	nv_crtc_set_image_sharpening(crtc, nv_crtc->sharpness);

	/* Some values the blob sets */
	regp->ramdac_8c0 = 0x100;
	regp->ramdac_a20 = 0x0;
	regp->ramdac_a24 = 0xfffff;
	regp->ramdac_a34 = 0x1;
}

static int
nv_crtc_swap_fbs(struct drm_crtc *crtc, struct drm_framebuffer *old_fb)
{
	struct nv04_display *disp = nv04_display(crtc->dev);
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct nouveau_bo *nvbo = nouveau_gem_object(fb->obj[0]);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	int ret;

	ret = nouveau_bo_pin(nvbo, NOUVEAU_GEM_DOMAIN_VRAM, false);
	if (ret == 0) {
		if (disp->image[nv_crtc->index])
			nouveau_bo_unpin(disp->image[nv_crtc->index]);
		nouveau_bo_ref(nvbo, &disp->image[nv_crtc->index]);
	}

	return ret;
}

/**
 * Sets up registers for the given mode/adjusted_mode pair.
 *
 * The clocks, CRTCs and outputs attached to this CRTC must be off.
 *
 * This shouldn't enable any clocks, CRTCs, or outputs, but they should
 * be easily turned on/off after this.
 */
static int
nv_crtc_mode_set(struct drm_crtc *crtc, struct drm_display_mode *mode,
		 struct drm_display_mode *adjusted_mode,
		 int x, int y, struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nouveau_drm *drm = nouveau_drm(dev);
	int ret;

	NV_DEBUG(drm, "CTRC mode on CRTC %d:\n", nv_crtc->index);
	drm_mode_debug_printmodeline(adjusted_mode);

	ret = nv_crtc_swap_fbs(crtc, old_fb);
	if (ret)
		return ret;

	/* unlock must come after turning off FP_TG_CONTROL in output_prepare */
	nv_lock_vga_crtc_shadow(dev, nv_crtc->index, -1);

	nv_crtc_mode_set_vga(crtc, adjusted_mode);
	/* calculated in nv04_dfp_prepare, nv40 needs it written before calculating PLLs */
	if (drm->client.device.info.family == NV_DEVICE_INFO_V0_CURIE)
		NVWriteRAMDAC(dev, 0, NV_PRAMDAC_SEL_CLK, nv04_display(dev)->mode_reg.sel_clk);
	nv_crtc_mode_set_regs(crtc, adjusted_mode);
	nv_crtc_calc_state_ext(crtc, mode, adjusted_mode->clock);
	return 0;
}

static void nv_crtc_save(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct nv04_mode_state *state = &nv04_display(dev)->mode_reg;
	struct nv04_crtc_reg *crtc_state = &state->crtc_reg[nv_crtc->index];
	struct nv04_mode_state *saved = &nv04_display(dev)->saved_reg;
	struct nv04_crtc_reg *crtc_saved = &saved->crtc_reg[nv_crtc->index];

	if (nv_two_heads(crtc->dev))
		NVSetOwner(crtc->dev, nv_crtc->index);

	nouveau_hw_save_state(crtc->dev, nv_crtc->index, saved);

	/* init some state to saved value */
	state->sel_clk = saved->sel_clk & ~(0x5 << 16);
	crtc_state->CRTC[NV_CIO_CRE_LCD__INDEX] = crtc_saved->CRTC[NV_CIO_CRE_LCD__INDEX];
	state->pllsel = saved->pllsel & ~(PLLSEL_VPLL1_MASK | PLLSEL_VPLL2_MASK | PLLSEL_TV_MASK);
	crtc_state->gpio_ext = crtc_saved->gpio_ext;
}

static void nv_crtc_restore(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	int head = nv_crtc->index;
	uint8_t saved_cr21 = nv04_display(dev)->saved_reg.crtc_reg[head].CRTC[NV_CIO_CRE_21];

	if (nv_two_heads(crtc->dev))
		NVSetOwner(crtc->dev, head);

	nouveau_hw_load_state(crtc->dev, head, &nv04_display(dev)->saved_reg);
	nv_lock_vga_crtc_shadow(crtc->dev, head, saved_cr21);

	nv_crtc->last_dpms = NV_DPMS_CLEARED;
}

static void nv_crtc_prepare(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	const struct drm_crtc_helper_funcs *funcs = crtc->helper_private;

	if (nv_two_heads(dev))
		NVSetOwner(dev, nv_crtc->index);

	drm_crtc_vblank_off(crtc);
	funcs->dpms(crtc, DRM_MODE_DPMS_OFF);

	NVBlankScreen(dev, nv_crtc->index, true);

	/* Some more preparation. */
	NVWriteCRTC(dev, nv_crtc->index, NV_PCRTC_CONFIG, NV_PCRTC_CONFIG_START_ADDRESS_NON_VGA);
	if (drm->client.device.info.family == NV_DEVICE_INFO_V0_CURIE) {
		uint32_t reg900 = NVReadRAMDAC(dev, nv_crtc->index, NV_PRAMDAC_900);
		NVWriteRAMDAC(dev, nv_crtc->index, NV_PRAMDAC_900, reg900 & ~0x10000);
	}
}

static void nv_crtc_commit(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	const struct drm_crtc_helper_funcs *funcs = crtc->helper_private;
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

	nouveau_hw_load_state(dev, nv_crtc->index, &nv04_display(dev)->mode_reg);
	nv04_crtc_mode_set_base(crtc, crtc->x, crtc->y, NULL);

#ifdef __BIG_ENDIAN
	/* turn on LFB swapping */
	{
		uint8_t tmp = NVReadVgaCrtc(dev, nv_crtc->index, NV_CIO_CRE_RCR);
		tmp |= MASK(NV_CIO_CRE_RCR_ENDIAN_BIG);
		NVWriteVgaCrtc(dev, nv_crtc->index, NV_CIO_CRE_RCR, tmp);
	}
#endif

	funcs->dpms(crtc, DRM_MODE_DPMS_ON);
	drm_crtc_vblank_on(crtc);
}

static void nv_crtc_destroy(struct drm_crtc *crtc)
{
	struct nv04_display *disp = nv04_display(crtc->dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

	if (!nv_crtc)
		return;

	drm_crtc_cleanup(crtc);

	if (disp->image[nv_crtc->index])
		nouveau_bo_unpin(disp->image[nv_crtc->index]);
	nouveau_bo_ref(NULL, &disp->image[nv_crtc->index]);

	nouveau_bo_unmap(nv_crtc->cursor.nvbo);
	nouveau_bo_unpin(nv_crtc->cursor.nvbo);
	nouveau_bo_ref(NULL, &nv_crtc->cursor.nvbo);
	nvif_event_dtor(&nv_crtc->vblank);
	nvif_head_dtor(&nv_crtc->head);
	kfree(nv_crtc);
}

static void
nv_crtc_gamma_load(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_device *dev = nv_crtc->base.dev;
	struct rgb { uint8_t r, g, b; } __attribute__((packed)) *rgbs;
	u16 *r, *g, *b;
	int i;

	rgbs = (struct rgb *)nv04_display(dev)->mode_reg.crtc_reg[nv_crtc->index].DAC;
	r = crtc->gamma_store;
	g = r + crtc->gamma_size;
	b = g + crtc->gamma_size;

	for (i = 0; i < 256; i++) {
		rgbs[i].r = *r++ >> 8;
		rgbs[i].g = *g++ >> 8;
		rgbs[i].b = *b++ >> 8;
	}

	nouveau_hw_load_state_palette(dev, nv_crtc->index, &nv04_display(dev)->mode_reg);
}

static void
nv_crtc_disable(struct drm_crtc *crtc)
{
	struct nv04_display *disp = nv04_display(crtc->dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	if (disp->image[nv_crtc->index])
		nouveau_bo_unpin(disp->image[nv_crtc->index]);
	nouveau_bo_ref(NULL, &disp->image[nv_crtc->index]);
}

static int
nv_crtc_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
		  uint32_t size,
		  struct drm_modeset_acquire_ctx *ctx)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

	/* We need to know the depth before we upload, but it's possible to
	 * get called before a framebuffer is bound.  If this is the case,
	 * mark the lut values as dirty by setting depth==0, and it'll be
	 * uploaded on the first mode_set_base()
	 */
	if (!nv_crtc->base.primary->fb) {
		nv_crtc->lut.depth = 0;
		return 0;
	}

	nv_crtc_gamma_load(crtc);

	return 0;
}

static int
nv04_crtc_do_mode_set_base(struct drm_crtc *crtc,
			   struct drm_framebuffer *passed_fb,
			   int x, int y, bool atomic)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv04_crtc_reg *regp = &nv04_display(dev)->mode_reg.crtc_reg[nv_crtc->index];
	struct nouveau_bo *nvbo;
	struct drm_framebuffer *drm_fb;
	int arb_burst, arb_lwm;

	NV_DEBUG(drm, "index %d\n", nv_crtc->index);

	/* no fb bound */
	if (!atomic && !crtc->primary->fb) {
		NV_DEBUG(drm, "No FB bound\n");
		return 0;
	}

	/* If atomic, we want to switch to the fb we were passed, so
	 * now we update pointers to do that.
	 */
	if (atomic) {
		drm_fb = passed_fb;
	} else {
		drm_fb = crtc->primary->fb;
	}

	nvbo = nouveau_gem_object(drm_fb->obj[0]);
	nv_crtc->fb.offset = nvbo->offset;

	if (nv_crtc->lut.depth != drm_fb->format->depth) {
		nv_crtc->lut.depth = drm_fb->format->depth;
		nv_crtc_gamma_load(crtc);
	}

	/* Update the framebuffer format. */
	regp->CRTC[NV_CIO_CRE_PIXEL_INDEX] &= ~3;
	regp->CRTC[NV_CIO_CRE_PIXEL_INDEX] |= (drm_fb->format->depth + 1) / 8;
	regp->ramdac_gen_ctrl &= ~NV_PRAMDAC_GENERAL_CONTROL_ALT_MODE_SEL;
	if (drm_fb->format->depth == 16)
		regp->ramdac_gen_ctrl |= NV_PRAMDAC_GENERAL_CONTROL_ALT_MODE_SEL;
	crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_PIXEL_INDEX);
	NVWriteRAMDAC(dev, nv_crtc->index, NV_PRAMDAC_GENERAL_CONTROL,
		      regp->ramdac_gen_ctrl);

	regp->CRTC[NV_CIO_CR_OFFSET_INDEX] = drm_fb->pitches[0] >> 3;
	regp->CRTC[NV_CIO_CRE_RPC0_INDEX] =
		XLATE(drm_fb->pitches[0] >> 3, 8, NV_CIO_CRE_RPC0_OFFSET_10_8);
	regp->CRTC[NV_CIO_CRE_42] =
		XLATE(drm_fb->pitches[0] / 8, 11, NV_CIO_CRE_42_OFFSET_11);
	crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_RPC0_INDEX);
	crtc_wr_cio_state(crtc, regp, NV_CIO_CR_OFFSET_INDEX);
	crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_42);

	/* Update the framebuffer location. */
	regp->fb_start = nv_crtc->fb.offset & ~3;
	regp->fb_start += (y * drm_fb->pitches[0]) + (x * drm_fb->format->cpp[0]);
	nv_set_crtc_base(dev, nv_crtc->index, regp->fb_start);

	/* Update the arbitration parameters. */
	nouveau_calc_arb(dev, crtc->mode.clock, drm_fb->format->cpp[0] * 8,
			 &arb_burst, &arb_lwm);

	regp->CRTC[NV_CIO_CRE_FF_INDEX] = arb_burst;
	regp->CRTC[NV_CIO_CRE_FFLWM__INDEX] = arb_lwm & 0xff;
	crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_FF_INDEX);
	crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_FFLWM__INDEX);

	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_KELVIN) {
		regp->CRTC[NV_CIO_CRE_47] = arb_lwm >> 8;
		crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_47);
	}

	return 0;
}

static int
nv04_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
			struct drm_framebuffer *old_fb)
{
	int ret = nv_crtc_swap_fbs(crtc, old_fb);
	if (ret)
		return ret;
	return nv04_crtc_do_mode_set_base(crtc, old_fb, x, y, false);
}

static int
nv04_crtc_mode_set_base_atomic(struct drm_crtc *crtc,
			       struct drm_framebuffer *fb,
			       int x, int y, enum mode_set_atomic state)
{
	return nv04_crtc_do_mode_set_base(crtc, fb, x, y, true);
}

static void nv04_cursor_upload(struct drm_device *dev, struct nouveau_bo *src,
			       struct nouveau_bo *dst)
{
	int width = nv_cursor_width(dev);
	uint32_t pixel;
	int i, j;

	for (i = 0; i < width; i++) {
		for (j = 0; j < width; j++) {
			pixel = nouveau_bo_rd32(src, i*64 + j);

			nouveau_bo_wr16(dst, i*width + j, (pixel & 0x80000000) >> 16
				     | (pixel & 0xf80000) >> 9
				     | (pixel & 0xf800) >> 6
				     | (pixel & 0xf8) >> 3);
		}
	}
}

static void nv11_cursor_upload(struct drm_device *dev, struct nouveau_bo *src,
			       struct nouveau_bo *dst)
{
	uint32_t pixel;
	int alpha, i;

	/* nv11+ supports premultiplied (PM), or non-premultiplied (NPM) alpha
	 * cursors (though NPM in combination with fp dithering may not work on
	 * nv11, from "nv" driver history)
	 * NPM mode needs NV_PCRTC_CURSOR_CONFIG_ALPHA_BLEND set and is what the
	 * blob uses, however we get given PM cursors so we use PM mode
	 */
	for (i = 0; i < 64 * 64; i++) {
		pixel = nouveau_bo_rd32(src, i);

		/* hw gets unhappy if alpha <= rgb values.  for a PM image "less
		 * than" shouldn't happen; fix "equal to" case by adding one to
		 * alpha channel (slightly inaccurate, but so is attempting to
		 * get back to NPM images, due to limits of integer precision)
		 */
		alpha = pixel >> 24;
		if (alpha > 0 && alpha < 255)
			pixel = (pixel & 0x00ffffff) | ((alpha + 1) << 24);

#ifdef __BIG_ENDIAN
		{
			struct nouveau_drm *drm = nouveau_drm(dev);

			if (drm->client.device.info.chipset == 0x11) {
				pixel = ((pixel & 0x000000ff) << 24) |
					((pixel & 0x0000ff00) << 8) |
					((pixel & 0x00ff0000) >> 8) |
					((pixel & 0xff000000) >> 24);
			}
		}
#endif

		nouveau_bo_wr32(dst, i, pixel);
	}
}

static int
nv04_crtc_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
		     uint32_t buffer_handle, uint32_t width, uint32_t height)
{
	struct nouveau_drm *drm = nouveau_drm(crtc->dev);
	struct drm_device *dev = drm->dev;
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nouveau_bo *cursor = NULL;
	struct drm_gem_object *gem;
	int ret = 0;

	if (!buffer_handle) {
		nv_crtc->cursor.hide(nv_crtc, true);
		return 0;
	}

	if (width != 64 || height != 64)
		return -EINVAL;

	gem = drm_gem_object_lookup(file_priv, buffer_handle);
	if (!gem)
		return -ENOENT;
	cursor = nouveau_gem_object(gem);

	ret = nouveau_bo_map(cursor);
	if (ret)
		goto out;

	if (drm->client.device.info.chipset >= 0x11)
		nv11_cursor_upload(dev, cursor, nv_crtc->cursor.nvbo);
	else
		nv04_cursor_upload(dev, cursor, nv_crtc->cursor.nvbo);

	nouveau_bo_unmap(cursor);
	nv_crtc->cursor.offset = nv_crtc->cursor.nvbo->offset;
	nv_crtc->cursor.set_offset(nv_crtc, nv_crtc->cursor.offset);
	nv_crtc->cursor.show(nv_crtc, true);
out:
	drm_gem_object_put(gem);
	return ret;
}

static int
nv04_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

	nv_crtc->cursor.set_pos(nv_crtc, x, y);
	return 0;
}

struct nv04_page_flip_state {
	struct list_head head;
	struct drm_pending_vblank_event *event;
	struct drm_crtc *crtc;
	int bpp, pitch;
	u64 offset;
};

static int
nv04_finish_page_flip(struct nouveau_channel *chan,
		      struct nv04_page_flip_state *ps)
{
	struct nouveau_fence_chan *fctx = chan->fence;
	struct nouveau_drm *drm = chan->drm;
	struct drm_device *dev = drm->dev;
	struct nv04_page_flip_state *s;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	if (list_empty(&fctx->flip)) {
		NV_ERROR(drm, "unexpected pageflip\n");
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return -EINVAL;
	}

	s = list_first_entry(&fctx->flip, struct nv04_page_flip_state, head);
	if (s->event) {
		drm_crtc_arm_vblank_event(s->crtc, s->event);
	} else {
		/* Give up ownership of vblank for page-flipped crtc */
		drm_crtc_vblank_put(s->crtc);
	}

	list_del(&s->head);
	if (ps)
		*ps = *s;
	kfree(s);

	spin_unlock_irqrestore(&dev->event_lock, flags);
	return 0;
}

int
nv04_flip_complete(struct nvif_event *event, void *argv, u32 argc)
{
	struct nv04_display *disp = container_of(event, typeof(*disp), flip);
	struct nouveau_drm *drm = disp->drm;
	struct nouveau_channel *chan = drm->channel;
	struct nv04_page_flip_state state;

	if (!nv04_finish_page_flip(chan, &state)) {
		nv_set_crtc_base(drm->dev, drm_crtc_index(state.crtc),
				 state.offset + state.crtc->y *
				 state.pitch + state.crtc->x *
				 state.bpp / 8);
	}

	return NVIF_EVENT_KEEP;
}

static int
nv04_page_flip_emit(struct nouveau_channel *chan,
		    struct nouveau_bo *old_bo,
		    struct nouveau_bo *new_bo,
		    struct nv04_page_flip_state *s,
		    struct nouveau_fence **pfence)
{
	struct nouveau_fence_chan *fctx = chan->fence;
	struct nouveau_drm *drm = chan->drm;
	struct drm_device *dev = drm->dev;
	struct nvif_push *push = chan->chan.push;
	unsigned long flags;
	int ret;

	/* Queue it to the pending list */
	spin_lock_irqsave(&dev->event_lock, flags);
	list_add_tail(&s->head, &fctx->flip);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	/* Synchronize with the old framebuffer */
	ret = nouveau_fence_sync(old_bo, chan, false, false);
	if (ret)
		goto fail;

	/* Emit the pageflip */
	ret = PUSH_WAIT(push, 2);
	if (ret)
		goto fail;

	PUSH_NVSQ(push, NV_SW, NV_SW_PAGE_FLIP, 0x00000000);
	PUSH_KICK(push);

	ret = nouveau_fence_new(pfence, chan);
	if (ret)
		goto fail;

	return 0;
fail:
	spin_lock_irqsave(&dev->event_lock, flags);
	list_del(&s->head);
	spin_unlock_irqrestore(&dev->event_lock, flags);
	return ret;
}

static int
nv04_crtc_page_flip(struct drm_crtc *crtc, struct drm_framebuffer *fb,
		    struct drm_pending_vblank_event *event, u32 flags,
		    struct drm_modeset_acquire_ctx *ctx)
{
	const int swap_interval = (flags & DRM_MODE_PAGE_FLIP_ASYNC) ? 0 : 1;
	struct drm_device *dev = crtc->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct drm_framebuffer *old_fb = crtc->primary->fb;
	struct nouveau_bo *old_bo = nouveau_gem_object(old_fb->obj[0]);
	struct nouveau_bo *new_bo = nouveau_gem_object(fb->obj[0]);
	struct nv04_page_flip_state *s;
	struct nouveau_channel *chan;
	struct nouveau_cli *cli;
	struct nouveau_fence *fence;
	struct nv04_display *dispnv04 = nv04_display(dev);
	struct nvif_push *push;
	int head = nouveau_crtc(crtc)->index;
	int ret;

	chan = drm->channel;
	if (!chan)
		return -ENODEV;
	cli = (void *)chan->user.client;
	push = chan->chan.push;

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	if (new_bo != old_bo) {
		ret = nouveau_bo_pin(new_bo, NOUVEAU_GEM_DOMAIN_VRAM, true);
		if (ret)
			goto fail_free;
	}

	mutex_lock(&cli->mutex);
	ret = ttm_bo_reserve(&new_bo->bo, true, false, NULL);
	if (ret)
		goto fail_unpin;

	/* synchronise rendering channel with the kernel's channel */
	ret = nouveau_fence_sync(new_bo, chan, false, true);
	if (ret) {
		ttm_bo_unreserve(&new_bo->bo);
		goto fail_unpin;
	}

	if (new_bo != old_bo) {
		ttm_bo_unreserve(&new_bo->bo);

		ret = ttm_bo_reserve(&old_bo->bo, true, false, NULL);
		if (ret)
			goto fail_unpin;
	}

	/* Initialize a page flip struct */
	*s = (struct nv04_page_flip_state)
		{ { }, event, crtc, fb->format->cpp[0] * 8, fb->pitches[0],
		  new_bo->offset };

	/* Keep vblanks on during flip, for the target crtc of this flip */
	drm_crtc_vblank_get(crtc);

	/* Emit a page flip */
	if (swap_interval) {
		ret = PUSH_WAIT(push, 8);
		if (ret)
			goto fail_unreserve;

		PUSH_NVSQ(push, NV05F, 0x012c, 0);
		PUSH_NVSQ(push, NV05F, 0x0134, head);
		PUSH_NVSQ(push, NV05F, 0x0100, 0);
		PUSH_NVSQ(push, NV05F, 0x0130, 0);
	}

	nouveau_bo_ref(new_bo, &dispnv04->image[head]);

	ret = nv04_page_flip_emit(chan, old_bo, new_bo, s, &fence);
	if (ret)
		goto fail_unreserve;
	mutex_unlock(&cli->mutex);

	/* Update the crtc struct and cleanup */
	crtc->primary->fb = fb;

	nouveau_bo_fence(old_bo, fence, false);
	ttm_bo_unreserve(&old_bo->bo);
	if (old_bo != new_bo)
		nouveau_bo_unpin(old_bo);
	nouveau_fence_unref(&fence);
	return 0;

fail_unreserve:
	drm_crtc_vblank_put(crtc);
	ttm_bo_unreserve(&old_bo->bo);
fail_unpin:
	mutex_unlock(&cli->mutex);
	if (old_bo != new_bo)
		nouveau_bo_unpin(new_bo);
fail_free:
	kfree(s);
	return ret;
}

static const struct drm_crtc_funcs nv04_crtc_funcs = {
	.cursor_set = nv04_crtc_cursor_set,
	.cursor_move = nv04_crtc_cursor_move,
	.gamma_set = nv_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.page_flip = nv04_crtc_page_flip,
	.destroy = nv_crtc_destroy,
	.enable_vblank = nouveau_display_vblank_enable,
	.disable_vblank = nouveau_display_vblank_disable,
	.get_vblank_timestamp = drm_crtc_vblank_helper_get_vblank_timestamp,
};

static const struct drm_crtc_helper_funcs nv04_crtc_helper_funcs = {
	.dpms = nv_crtc_dpms,
	.prepare = nv_crtc_prepare,
	.commit = nv_crtc_commit,
	.mode_set = nv_crtc_mode_set,
	.mode_set_base = nv04_crtc_mode_set_base,
	.mode_set_base_atomic = nv04_crtc_mode_set_base_atomic,
	.disable = nv_crtc_disable,
	.get_scanout_position = nouveau_display_scanoutpos,
};

static const uint32_t modeset_formats[] = {
        DRM_FORMAT_XRGB8888,
        DRM_FORMAT_RGB565,
        DRM_FORMAT_XRGB1555,
};

static const struct drm_plane_funcs nv04_primary_plane_funcs = {
	DRM_PLANE_NON_ATOMIC_FUNCS,
};

static int
nv04_crtc_vblank_handler(struct nvif_event *event, void *repv, u32 repc)
{
	struct nouveau_crtc *nv_crtc = container_of(event, struct nouveau_crtc, vblank);

	drm_crtc_handle_vblank(&nv_crtc->base);
	return NVIF_EVENT_KEEP;
}

int
nv04_crtc_create(struct drm_device *dev, int crtc_num)
{
	struct nouveau_display *disp = nouveau_display(dev);
	struct nouveau_crtc *nv_crtc;
	struct drm_plane *primary;
	int ret;

	nv_crtc = kzalloc(sizeof(*nv_crtc), GFP_KERNEL);
	if (!nv_crtc)
		return -ENOMEM;

	nv_crtc->lut.depth = 0;

	nv_crtc->index = crtc_num;
	nv_crtc->last_dpms = NV_DPMS_CLEARED;

	nv_crtc->save = nv_crtc_save;
	nv_crtc->restore = nv_crtc_restore;

	primary = __drm_universal_plane_alloc(dev, sizeof(*primary), 0, 0,
					      &nv04_primary_plane_funcs,
					      modeset_formats,
					      ARRAY_SIZE(modeset_formats), NULL,
					      DRM_PLANE_TYPE_PRIMARY, NULL);
	if (IS_ERR(primary)) {
		ret = PTR_ERR(primary);
		kfree(nv_crtc);
		return ret;
	}

	drm_crtc_init_with_planes(dev, &nv_crtc->base, primary, NULL,
                                  &nv04_crtc_funcs, NULL);
	drm_crtc_helper_add(&nv_crtc->base, &nv04_crtc_helper_funcs);
	drm_mode_crtc_set_gamma_size(&nv_crtc->base, 256);

	ret = nouveau_bo_new(&nouveau_drm(dev)->client, 64*64*4, 0x100,
			     NOUVEAU_GEM_DOMAIN_VRAM, 0, 0x0000, NULL, NULL,
			     &nv_crtc->cursor.nvbo);
	if (!ret) {
		ret = nouveau_bo_pin(nv_crtc->cursor.nvbo,
				     NOUVEAU_GEM_DOMAIN_VRAM, false);
		if (!ret) {
			ret = nouveau_bo_map(nv_crtc->cursor.nvbo);
			if (ret)
				nouveau_bo_unpin(nv_crtc->cursor.nvbo);
		}
		if (ret)
			nouveau_bo_ref(NULL, &nv_crtc->cursor.nvbo);
	}

	nv04_cursor_init(nv_crtc);

	ret = nvif_head_ctor(&disp->disp, nv_crtc->base.name, nv_crtc->index, &nv_crtc->head);
	if (ret)
		return ret;

	return nvif_head_vblank_event_ctor(&nv_crtc->head, "kmsVbl", nv04_crtc_vblank_handler,
					   false, &nv_crtc->vblank);
}
