/**************************************************************************
 * Copyright (c) 2009-2011, Intel Corporation.
 * All Rights Reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Benjamin Defnet <benjamin.r.defnet@intel.com>
 *    Rajesh Poornachandran <rajesh.poornachandran@intel.com>
 * Massively reworked
 *    Alan Cox <alan@linux.intel.com>
 */
#include "psb_powermgmt.h"
#include "psb_drv.h"
#include "psb_reg.h"
#include "psb_intel_reg.h"
#include "mdfld_output.h"
#include "mdfld_dsi_output.h"
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <asm/intel_scu_ipc.h>

/* IPC message and command defines used to enable/disable mipi panel voltages */
#define IPC_MSG_PANEL_ON_OFF    0xE9
#define IPC_CMD_PANEL_ON        1
#define IPC_CMD_PANEL_OFF       0

static struct mutex power_mutex;

/**
 *	gma_power_init		-	initialise power manager
 *	@dev: our device
 *
 *	Set up for power management tracking of our hardware.
 */
void gma_power_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	/* FIXME: need to sort out fetching apm_reg for both platforms ?? */

	dev_priv->apm_base = dev_priv->apm_reg & 0xffff;
	dev_priv->ospm_base &= 0xffff;

	dev_priv->display_power = true;	/* We start active */
	dev_priv->display_count = 0;	/* Currently no users */
	dev_priv->suspended = false;	/* And not suspended */
	mutex_init(&power_mutex);

	if (!IS_MRST(dev)) {
		/* FIXME: wants further review */
		u32 gating = PSB_RSGX32(PSB_CR_CLKGATECTL);
		/* Disable 2D clock gating */
		gating &= ~3;
		gating |= 1;
		PSB_WSGX32(gating, PSB_CR_CLKGATECTL);
		PSB_RSGX32(PSB_CR_CLKGATECTL);
	}
}

/**
 *	gma_power_uninit	-	end power manager
 *	@dev: device to end for
 *
 *	Undo the effects of gma_power_init
 */
void gma_power_uninit(struct drm_device *dev)
{
	mutex_destroy(&power_mutex);
	pm_runtime_disable(&dev->pdev->dev);
	pm_runtime_set_suspended(&dev->pdev->dev);
}


/**
 *	save_display_registers	-	save registers lost on suspend
 *	@dev: our DRM device
 *
 *	Save the state we need in order to be able to restore the interface
 *	upon resume from suspend
 */
static int save_display_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct drm_connector *connector;

	/* Display arbitration control + watermarks */
	dev_priv->saveDSPARB = PSB_RVDC32(DSPARB);
	dev_priv->saveDSPFW1 = PSB_RVDC32(DSPFW1);
	dev_priv->saveDSPFW2 = PSB_RVDC32(DSPFW2);
	dev_priv->saveDSPFW3 = PSB_RVDC32(DSPFW3);
	dev_priv->saveDSPFW4 = PSB_RVDC32(DSPFW4);
	dev_priv->saveDSPFW5 = PSB_RVDC32(DSPFW5);
	dev_priv->saveDSPFW6 = PSB_RVDC32(DSPFW6);
	dev_priv->saveCHICKENBIT = PSB_RVDC32(DSPCHICKENBIT);

	/* Save crtc and output state */
	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (drm_helper_crtc_in_use(crtc))
			crtc->funcs->save(crtc);
	}

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		connector->funcs->save(connector);

	mutex_unlock(&dev->mode_config.mutex);
	return 0;
}

/**
 *	restore_display_registers	-	restore lost register state
 *	@dev: our DRM device
 *
 *	Restore register state that was lost during suspend and resume.
 */
static int restore_display_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct drm_connector *connector;

	/* Display arbitration + watermarks */
	PSB_WVDC32(dev_priv->saveDSPARB, DSPARB);
	PSB_WVDC32(dev_priv->saveDSPFW1, DSPFW1);
	PSB_WVDC32(dev_priv->saveDSPFW2, DSPFW2);
	PSB_WVDC32(dev_priv->saveDSPFW3, DSPFW3);
	PSB_WVDC32(dev_priv->saveDSPFW4, DSPFW4);
	PSB_WVDC32(dev_priv->saveDSPFW5, DSPFW5);
	PSB_WVDC32(dev_priv->saveDSPFW6, DSPFW6);
	PSB_WVDC32(dev_priv->saveCHICKENBIT, DSPCHICKENBIT);

	/*make sure VGA plane is off. it initializes to on after reset!*/
	PSB_WVDC32(0x80000000, VGACNTRL);

	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		if (drm_helper_crtc_in_use(crtc))
			crtc->funcs->restore(crtc);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		connector->funcs->restore(connector);

	mutex_unlock(&dev->mode_config.mutex);
	return 0;
}

/**
 * mdfld_save_display_registers	-	save registers for pipe
 * @dev: our device
 * @pipe: pipe to save
 *
 * Save the pipe state of the device before we power it off. Keep everything
 * we need to put it back again
 */
static int mdfld_save_display_registers(struct drm_device *dev, int pipe)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	int i;

	/* register */
	u32 dpll_reg = MRST_DPLL_A;
	u32 fp_reg = MRST_FPA0;
	u32 pipeconf_reg = PIPEACONF;
	u32 htot_reg = HTOTAL_A;
	u32 hblank_reg = HBLANK_A;
	u32 hsync_reg = HSYNC_A;
	u32 vtot_reg = VTOTAL_A;
	u32 vblank_reg = VBLANK_A;
	u32 vsync_reg = VSYNC_A;
	u32 pipesrc_reg = PIPEASRC;
	u32 dspstride_reg = DSPASTRIDE;
	u32 dsplinoff_reg = DSPALINOFF;
	u32 dsptileoff_reg = DSPATILEOFF;
	u32 dspsize_reg = DSPASIZE;
	u32 dsppos_reg = DSPAPOS;
	u32 dspsurf_reg = DSPASURF;
	u32 mipi_reg = MIPI;
	u32 dspcntr_reg = DSPACNTR;
	u32 dspstatus_reg = PIPEASTAT;
	u32 palette_reg = PALETTE_A;

	/* pointer to values */
	u32 *dpll_val = &dev_priv->saveDPLL_A;
	u32 *fp_val = &dev_priv->saveFPA0;
	u32 *pipeconf_val = &dev_priv->savePIPEACONF;
	u32 *htot_val = &dev_priv->saveHTOTAL_A;
	u32 *hblank_val = &dev_priv->saveHBLANK_A;
	u32 *hsync_val = &dev_priv->saveHSYNC_A;
	u32 *vtot_val = &dev_priv->saveVTOTAL_A;
	u32 *vblank_val = &dev_priv->saveVBLANK_A;
	u32 *vsync_val = &dev_priv->saveVSYNC_A;
	u32 *pipesrc_val = &dev_priv->savePIPEASRC;
	u32 *dspstride_val = &dev_priv->saveDSPASTRIDE;
	u32 *dsplinoff_val = &dev_priv->saveDSPALINOFF;
	u32 *dsptileoff_val = &dev_priv->saveDSPATILEOFF;
	u32 *dspsize_val = &dev_priv->saveDSPASIZE;
	u32 *dsppos_val = &dev_priv->saveDSPAPOS;
	u32 *dspsurf_val = &dev_priv->saveDSPASURF;
	u32 *mipi_val = &dev_priv->saveMIPI;
	u32 *dspcntr_val = &dev_priv->saveDSPACNTR;
	u32 *dspstatus_val = &dev_priv->saveDSPASTATUS;
	u32 *palette_val = dev_priv->save_palette_a;

	switch (pipe) {
	case 0:
		break;
	case 1:
		/* register */
		dpll_reg = MDFLD_DPLL_B;
		fp_reg = MDFLD_DPLL_DIV0;
		pipeconf_reg = PIPEBCONF;
		htot_reg = HTOTAL_B;
		hblank_reg = HBLANK_B;
		hsync_reg = HSYNC_B;
		vtot_reg = VTOTAL_B;
		vblank_reg = VBLANK_B;
		vsync_reg = VSYNC_B;
		pipesrc_reg = PIPEBSRC;
		dspstride_reg = DSPBSTRIDE;
		dsplinoff_reg = DSPBLINOFF;
		dsptileoff_reg = DSPBTILEOFF;
		dspsize_reg = DSPBSIZE;
		dsppos_reg = DSPBPOS;
		dspsurf_reg = DSPBSURF;
		dspcntr_reg = DSPBCNTR;
		dspstatus_reg = PIPEBSTAT;
		palette_reg = PALETTE_B;

		/* values */
		dpll_val = &dev_priv->saveDPLL_B;
		fp_val = &dev_priv->saveFPB0;
		pipeconf_val = &dev_priv->savePIPEBCONF;
		htot_val = &dev_priv->saveHTOTAL_B;
		hblank_val = &dev_priv->saveHBLANK_B;
		hsync_val = &dev_priv->saveHSYNC_B;
		vtot_val = &dev_priv->saveVTOTAL_B;
		vblank_val = &dev_priv->saveVBLANK_B;
		vsync_val = &dev_priv->saveVSYNC_B;
		pipesrc_val = &dev_priv->savePIPEBSRC;
		dspstride_val = &dev_priv->saveDSPBSTRIDE;
		dsplinoff_val = &dev_priv->saveDSPBLINOFF;
		dsptileoff_val = &dev_priv->saveDSPBTILEOFF;
		dspsize_val = &dev_priv->saveDSPBSIZE;
		dsppos_val = &dev_priv->saveDSPBPOS;
		dspsurf_val = &dev_priv->saveDSPBSURF;
		dspcntr_val = &dev_priv->saveDSPBCNTR;
		dspstatus_val = &dev_priv->saveDSPBSTATUS;
		palette_val = dev_priv->save_palette_b;
		break;
	case 2:
		/* register */
		pipeconf_reg = PIPECCONF;
		htot_reg = HTOTAL_C;
		hblank_reg = HBLANK_C;
		hsync_reg = HSYNC_C;
		vtot_reg = VTOTAL_C;
		vblank_reg = VBLANK_C;
		vsync_reg = VSYNC_C;
		pipesrc_reg = PIPECSRC;
		dspstride_reg = DSPCSTRIDE;
		dsplinoff_reg = DSPCLINOFF;
		dsptileoff_reg = DSPCTILEOFF;
		dspsize_reg = DSPCSIZE;
		dsppos_reg = DSPCPOS;
		dspsurf_reg = DSPCSURF;
		mipi_reg = MIPI_C;
		dspcntr_reg = DSPCCNTR;
		dspstatus_reg = PIPECSTAT;
		palette_reg = PALETTE_C;

		/* pointer to values */
		pipeconf_val = &dev_priv->savePIPECCONF;
		htot_val = &dev_priv->saveHTOTAL_C;
		hblank_val = &dev_priv->saveHBLANK_C;
		hsync_val = &dev_priv->saveHSYNC_C;
		vtot_val = &dev_priv->saveVTOTAL_C;
		vblank_val = &dev_priv->saveVBLANK_C;
		vsync_val = &dev_priv->saveVSYNC_C;
		pipesrc_val = &dev_priv->savePIPECSRC;
		dspstride_val = &dev_priv->saveDSPCSTRIDE;
		dsplinoff_val = &dev_priv->saveDSPCLINOFF;
		dsptileoff_val = &dev_priv->saveDSPCTILEOFF;
		dspsize_val = &dev_priv->saveDSPCSIZE;
		dsppos_val = &dev_priv->saveDSPCPOS;
		dspsurf_val = &dev_priv->saveDSPCSURF;
		mipi_val = &dev_priv->saveMIPI_C;
		dspcntr_val = &dev_priv->saveDSPCCNTR;
		dspstatus_val = &dev_priv->saveDSPCSTATUS;
		palette_val = dev_priv->save_palette_c;
		break;
	default:
		DRM_ERROR("%s, invalid pipe number.\n", __func__);
		return -EINVAL;
	}

	/* Pipe & plane A info */
	*dpll_val = PSB_RVDC32(dpll_reg);
	*fp_val = PSB_RVDC32(fp_reg);
	*pipeconf_val = PSB_RVDC32(pipeconf_reg);
	*htot_val = PSB_RVDC32(htot_reg);
	*hblank_val = PSB_RVDC32(hblank_reg);
	*hsync_val = PSB_RVDC32(hsync_reg);
	*vtot_val = PSB_RVDC32(vtot_reg);
	*vblank_val = PSB_RVDC32(vblank_reg);
	*vsync_val = PSB_RVDC32(vsync_reg);
	*pipesrc_val = PSB_RVDC32(pipesrc_reg);
	*dspstride_val = PSB_RVDC32(dspstride_reg);
	*dsplinoff_val = PSB_RVDC32(dsplinoff_reg);
	*dsptileoff_val = PSB_RVDC32(dsptileoff_reg);
	*dspsize_val = PSB_RVDC32(dspsize_reg);
	*dsppos_val = PSB_RVDC32(dsppos_reg);
	*dspsurf_val = PSB_RVDC32(dspsurf_reg);
	*dspcntr_val = PSB_RVDC32(dspcntr_reg);
	*dspstatus_val = PSB_RVDC32(dspstatus_reg);

	/*save palette (gamma) */
	for (i = 0; i < 256; i++)
		palette_val[i] = PSB_RVDC32(palette_reg + (i<<2));

	if (pipe == 1) {
		dev_priv->savePFIT_CONTROL = PSB_RVDC32(PFIT_CONTROL);
		dev_priv->savePFIT_PGM_RATIOS = PSB_RVDC32(PFIT_PGM_RATIOS);
		dev_priv->saveHDMIPHYMISCCTL = PSB_RVDC32(HDMIPHYMISCCTL);
		dev_priv->saveHDMIB_CONTROL = PSB_RVDC32(HDMIB_CONTROL);
		return 0;
	}
	*mipi_val = PSB_RVDC32(mipi_reg);
	return 0;
}

/**
 * mdfld_save_cursor_overlay_registers	-	save cursor overlay info
 * @dev: our device
 *
 * Save the cursor and overlay register state
 */
static int mdfld_save_cursor_overlay_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	/* Save cursor regs */
	dev_priv->saveDSPACURSOR_CTRL = PSB_RVDC32(CURACNTR);
	dev_priv->saveDSPACURSOR_BASE = PSB_RVDC32(CURABASE);
	dev_priv->saveDSPACURSOR_POS = PSB_RVDC32(CURAPOS);

	dev_priv->saveDSPBCURSOR_CTRL = PSB_RVDC32(CURBCNTR);
	dev_priv->saveDSPBCURSOR_BASE = PSB_RVDC32(CURBBASE);
	dev_priv->saveDSPBCURSOR_POS = PSB_RVDC32(CURBPOS);

	dev_priv->saveDSPCCURSOR_CTRL = PSB_RVDC32(CURCCNTR);
	dev_priv->saveDSPCCURSOR_BASE = PSB_RVDC32(CURCBASE);
	dev_priv->saveDSPCCURSOR_POS = PSB_RVDC32(CURCPOS);

	/* HW overlay */
	dev_priv->saveOV_OVADD = PSB_RVDC32(OV_OVADD);
	dev_priv->saveOV_OGAMC0 = PSB_RVDC32(OV_OGAMC0);
	dev_priv->saveOV_OGAMC1 = PSB_RVDC32(OV_OGAMC1);
	dev_priv->saveOV_OGAMC2 = PSB_RVDC32(OV_OGAMC2);
	dev_priv->saveOV_OGAMC3 = PSB_RVDC32(OV_OGAMC3);
	dev_priv->saveOV_OGAMC4 = PSB_RVDC32(OV_OGAMC4);
	dev_priv->saveOV_OGAMC5 = PSB_RVDC32(OV_OGAMC5);

	dev_priv->saveOV_OVADD_C = PSB_RVDC32(OV_OVADD + OV_C_OFFSET);
	dev_priv->saveOV_OGAMC0_C = PSB_RVDC32(OV_OGAMC0 + OV_C_OFFSET);
	dev_priv->saveOV_OGAMC1_C = PSB_RVDC32(OV_OGAMC1 + OV_C_OFFSET);
	dev_priv->saveOV_OGAMC2_C = PSB_RVDC32(OV_OGAMC2 + OV_C_OFFSET);
	dev_priv->saveOV_OGAMC3_C = PSB_RVDC32(OV_OGAMC3 + OV_C_OFFSET);
	dev_priv->saveOV_OGAMC4_C = PSB_RVDC32(OV_OGAMC4 + OV_C_OFFSET);
	dev_priv->saveOV_OGAMC5_C = PSB_RVDC32(OV_OGAMC5 + OV_C_OFFSET);

	return 0;
}
/*
 * mdfld_restore_display_registers	-	restore the state of a pipe
 * @dev: our device
 * @pipe: the pipe to restore
 *
 * Restore the state of a pipe to that which was saved by the register save
 * functions.
 */
static int mdfld_restore_display_registers(struct drm_device *dev, int pipe)
{
	/* To get  panel out of ULPS mode */
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mdfld_dsi_config *dsi_config = NULL;
	u32 i = 0;
	u32 dpll = 0;
	u32 timeout = 0;
	u32 reg_offset = 0;

	/* register */
	u32 dpll_reg = MRST_DPLL_A;
	u32 fp_reg = MRST_FPA0;
	u32 pipeconf_reg = PIPEACONF;
	u32 htot_reg = HTOTAL_A;
	u32 hblank_reg = HBLANK_A;
	u32 hsync_reg = HSYNC_A;
	u32 vtot_reg = VTOTAL_A;
	u32 vblank_reg = VBLANK_A;
	u32 vsync_reg = VSYNC_A;
	u32 pipesrc_reg = PIPEASRC;
	u32 dspstride_reg = DSPASTRIDE;
	u32 dsplinoff_reg = DSPALINOFF;
	u32 dsptileoff_reg = DSPATILEOFF;
	u32 dspsize_reg = DSPASIZE;
	u32 dsppos_reg = DSPAPOS;
	u32 dspsurf_reg = DSPASURF;
	u32 dspstatus_reg = PIPEASTAT;
	u32 mipi_reg = MIPI;
	u32 dspcntr_reg = DSPACNTR;
	u32 palette_reg = PALETTE_A;

	/* values */
	u32 dpll_val = dev_priv->saveDPLL_A & ~DPLL_VCO_ENABLE;
	u32 fp_val = dev_priv->saveFPA0;
	u32 pipeconf_val = dev_priv->savePIPEACONF;
	u32 htot_val = dev_priv->saveHTOTAL_A;
	u32 hblank_val = dev_priv->saveHBLANK_A;
	u32 hsync_val = dev_priv->saveHSYNC_A;
	u32 vtot_val = dev_priv->saveVTOTAL_A;
	u32 vblank_val = dev_priv->saveVBLANK_A;
	u32 vsync_val = dev_priv->saveVSYNC_A;
	u32 pipesrc_val = dev_priv->savePIPEASRC;
	u32 dspstride_val = dev_priv->saveDSPASTRIDE;
	u32 dsplinoff_val = dev_priv->saveDSPALINOFF;
	u32 dsptileoff_val = dev_priv->saveDSPATILEOFF;
	u32 dspsize_val = dev_priv->saveDSPASIZE;
	u32 dsppos_val = dev_priv->saveDSPAPOS;
	u32 dspsurf_val = dev_priv->saveDSPASURF;
	u32 dspstatus_val = dev_priv->saveDSPASTATUS;
	u32 mipi_val = dev_priv->saveMIPI;
	u32 dspcntr_val = dev_priv->saveDSPACNTR;
	u32 *palette_val = dev_priv->save_palette_a;

	switch (pipe) {
	case 0:
		dsi_config = dev_priv->dsi_configs[0];
		break;
	case 1:
		/* register */
		dpll_reg = MDFLD_DPLL_B;
		fp_reg = MDFLD_DPLL_DIV0;
		pipeconf_reg = PIPEBCONF;
		htot_reg = HTOTAL_B;
		hblank_reg = HBLANK_B;
		hsync_reg = HSYNC_B;
		vtot_reg = VTOTAL_B;
		vblank_reg = VBLANK_B;
		vsync_reg = VSYNC_B;
		pipesrc_reg = PIPEBSRC;
		dspstride_reg = DSPBSTRIDE;
		dsplinoff_reg = DSPBLINOFF;
		dsptileoff_reg = DSPBTILEOFF;
		dspsize_reg = DSPBSIZE;
		dsppos_reg = DSPBPOS;
		dspsurf_reg = DSPBSURF;
		dspcntr_reg = DSPBCNTR;
		palette_reg = PALETTE_B;
		dspstatus_reg = PIPEBSTAT;

		/* values */
		dpll_val = dev_priv->saveDPLL_B & ~DPLL_VCO_ENABLE;
		fp_val = dev_priv->saveFPB0;
		pipeconf_val = dev_priv->savePIPEBCONF;
		htot_val = dev_priv->saveHTOTAL_B;
		hblank_val = dev_priv->saveHBLANK_B;
		hsync_val = dev_priv->saveHSYNC_B;
		vtot_val = dev_priv->saveVTOTAL_B;
		vblank_val = dev_priv->saveVBLANK_B;
		vsync_val = dev_priv->saveVSYNC_B;
		pipesrc_val = dev_priv->savePIPEBSRC;
		dspstride_val = dev_priv->saveDSPBSTRIDE;
		dsplinoff_val = dev_priv->saveDSPBLINOFF;
		dsptileoff_val = dev_priv->saveDSPBTILEOFF;
		dspsize_val = dev_priv->saveDSPBSIZE;
		dsppos_val = dev_priv->saveDSPBPOS;
		dspsurf_val = dev_priv->saveDSPBSURF;
		dspcntr_val = dev_priv->saveDSPBCNTR;
		dspstatus_val = dev_priv->saveDSPBSTATUS;
		palette_val = dev_priv->save_palette_b;
		break;
	case 2:
		reg_offset = MIPIC_REG_OFFSET;

		/* register */
		pipeconf_reg = PIPECCONF;
		htot_reg = HTOTAL_C;
		hblank_reg = HBLANK_C;
		hsync_reg = HSYNC_C;
		vtot_reg = VTOTAL_C;
		vblank_reg = VBLANK_C;
		vsync_reg = VSYNC_C;
		pipesrc_reg = PIPECSRC;
		dspstride_reg = DSPCSTRIDE;
		dsplinoff_reg = DSPCLINOFF;
		dsptileoff_reg = DSPCTILEOFF;
		dspsize_reg = DSPCSIZE;
		dsppos_reg = DSPCPOS;
		dspsurf_reg = DSPCSURF;
		mipi_reg = MIPI_C;
		dspcntr_reg = DSPCCNTR;
		palette_reg = PALETTE_C;
		dspstatus_reg = PIPECSTAT;

		/* values */
		pipeconf_val = dev_priv->savePIPECCONF;
		htot_val = dev_priv->saveHTOTAL_C;
		hblank_val = dev_priv->saveHBLANK_C;
		hsync_val = dev_priv->saveHSYNC_C;
		vtot_val = dev_priv->saveVTOTAL_C;
		vblank_val = dev_priv->saveVBLANK_C;
		vsync_val = dev_priv->saveVSYNC_C;
		pipesrc_val = dev_priv->savePIPECSRC;
		dspstride_val = dev_priv->saveDSPCSTRIDE;
		dsplinoff_val = dev_priv->saveDSPCLINOFF;
		dsptileoff_val = dev_priv->saveDSPCTILEOFF;
		dspsize_val = dev_priv->saveDSPCSIZE;
		dsppos_val = dev_priv->saveDSPCPOS;
		dspsurf_val = dev_priv->saveDSPCSURF;
		dspstatus_val = dev_priv->saveDSPCSTATUS;
		mipi_val = dev_priv->saveMIPI_C;
		dspcntr_val = dev_priv->saveDSPCCNTR;
		palette_val = dev_priv->save_palette_c;

		dsi_config = dev_priv->dsi_configs[1];
		break;
	default:
		DRM_ERROR("%s, invalid pipe number.\n", __func__);
		return -EINVAL;
	}

	/* Make sure VGA plane is off. it initializes to on after reset!*/
	PSB_WVDC32(0x80000000, VGACNTRL);
	if (pipe == 1) {
		PSB_WVDC32(dpll_val & ~DPLL_VCO_ENABLE, dpll_reg);
		PSB_RVDC32(dpll_reg);

		PSB_WVDC32(fp_val, fp_reg);
	} else {
		dpll = PSB_RVDC32(dpll_reg);

		if (!(dpll & DPLL_VCO_ENABLE)) {

			/* When ungating power of DPLL, needs to wait 0.5us before enable the VCO */
			if (dpll & MDFLD_PWR_GATE_EN) {
				dpll &= ~MDFLD_PWR_GATE_EN;
				PSB_WVDC32(dpll, dpll_reg);
				udelay(500);	/* FIXME: 1 ? */
			}

			PSB_WVDC32(fp_val, fp_reg);
			PSB_WVDC32(dpll_val, dpll_reg);
			/* FIXME_MDFLD PO - change 500 to 1 after PO */
			udelay(500);

			dpll_val |= DPLL_VCO_ENABLE;
			PSB_WVDC32(dpll_val, dpll_reg);
			PSB_RVDC32(dpll_reg);

			/* wait for DSI PLL to lock */
			while ((timeout < 20000) && !(PSB_RVDC32(pipeconf_reg) & PIPECONF_DSIPLL_LOCK)) {
				udelay(150);
				timeout++;
			}

			if (timeout == 20000) {
				DRM_ERROR("%s, can't lock DSIPLL.\n",
							__func__);
				return -EINVAL;
			}
		}
	}
	/* Restore mode */
	PSB_WVDC32(htot_val, htot_reg);
	PSB_WVDC32(hblank_val, hblank_reg);
	PSB_WVDC32(hsync_val, hsync_reg);
	PSB_WVDC32(vtot_val, vtot_reg);
	PSB_WVDC32(vblank_val, vblank_reg);
	PSB_WVDC32(vsync_val, vsync_reg);
	PSB_WVDC32(pipesrc_val, pipesrc_reg);
	PSB_WVDC32(dspstatus_val, dspstatus_reg);

	/* Set up the plane */
	PSB_WVDC32(dspstride_val, dspstride_reg);
	PSB_WVDC32(dsplinoff_val, dsplinoff_reg);
	PSB_WVDC32(dsptileoff_val, dsptileoff_reg);
	PSB_WVDC32(dspsize_val, dspsize_reg);
	PSB_WVDC32(dsppos_val, dsppos_reg);
	PSB_WVDC32(dspsurf_val, dspsurf_reg);

	if (pipe == 1) {
		PSB_WVDC32(dev_priv->savePFIT_CONTROL, PFIT_CONTROL);
		PSB_WVDC32(dev_priv->savePFIT_PGM_RATIOS, PFIT_PGM_RATIOS);
		PSB_WVDC32(dev_priv->saveHDMIPHYMISCCTL, HDMIPHYMISCCTL);
		PSB_WVDC32(dev_priv->saveHDMIB_CONTROL, HDMIB_CONTROL);

	} else {
		/* Set up pipe related registers */
		PSB_WVDC32(mipi_val, mipi_reg);
		/* Setup MIPI adapter + MIPI IP registers */
		mdfld_dsi_controller_init(dsi_config, pipe);
		msleep(20);
	}
	/* Enable the plane */
	PSB_WVDC32(dspcntr_val, dspcntr_reg);
	msleep(20);
	/* Enable the pipe */
	PSB_WVDC32(pipeconf_val, pipeconf_reg);

	for (i = 0; i < 256; i++)
		PSB_WVDC32(palette_val[i], palette_reg + (i<<2));
	if (pipe == 1)
		return 0;
	if (IS_MFLD(dev) && !mdfld_panel_dpi(dev))
		mdfld_enable_te(dev, pipe);
	return 0;
}

/**
 * mdfld_restore_cursor_overlay_registers	-	restore cursor
 * @dev: our device
 *
 * Restore the cursor and overlay state that was saved earlier
 */
static int mdfld_restore_cursor_overlay_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	/* Enable Cursor A */
	PSB_WVDC32(dev_priv->saveDSPACURSOR_CTRL, CURACNTR);
	PSB_WVDC32(dev_priv->saveDSPACURSOR_POS, CURAPOS);
	PSB_WVDC32(dev_priv->saveDSPACURSOR_BASE, CURABASE);

	PSB_WVDC32(dev_priv->saveDSPBCURSOR_CTRL, CURBCNTR);
	PSB_WVDC32(dev_priv->saveDSPBCURSOR_POS, CURBPOS);
	PSB_WVDC32(dev_priv->saveDSPBCURSOR_BASE, CURBBASE);

	PSB_WVDC32(dev_priv->saveDSPCCURSOR_CTRL, CURCCNTR);
	PSB_WVDC32(dev_priv->saveDSPCCURSOR_POS, CURCPOS);
	PSB_WVDC32(dev_priv->saveDSPCCURSOR_BASE, CURCBASE);

	/* Restore HW overlay */
	PSB_WVDC32(dev_priv->saveOV_OVADD, OV_OVADD);
	PSB_WVDC32(dev_priv->saveOV_OGAMC0, OV_OGAMC0);
	PSB_WVDC32(dev_priv->saveOV_OGAMC1, OV_OGAMC1);
	PSB_WVDC32(dev_priv->saveOV_OGAMC2, OV_OGAMC2);
	PSB_WVDC32(dev_priv->saveOV_OGAMC3, OV_OGAMC3);
	PSB_WVDC32(dev_priv->saveOV_OGAMC4, OV_OGAMC4);
	PSB_WVDC32(dev_priv->saveOV_OGAMC5, OV_OGAMC5);

	PSB_WVDC32(dev_priv->saveOV_OVADD_C, OV_OVADD + OV_C_OFFSET);
	PSB_WVDC32(dev_priv->saveOV_OGAMC0_C, OV_OGAMC0 + OV_C_OFFSET);
	PSB_WVDC32(dev_priv->saveOV_OGAMC1_C, OV_OGAMC1 + OV_C_OFFSET);
	PSB_WVDC32(dev_priv->saveOV_OGAMC2_C, OV_OGAMC2 + OV_C_OFFSET);
	PSB_WVDC32(dev_priv->saveOV_OGAMC3_C, OV_OGAMC3 + OV_C_OFFSET);
	PSB_WVDC32(dev_priv->saveOV_OGAMC4_C, OV_OGAMC4 + OV_C_OFFSET);
	PSB_WVDC32(dev_priv->saveOV_OGAMC5_C, OV_OGAMC5 + OV_C_OFFSET);

	return 0;
}

/**
 *	power_down	-	power down the display island
 *	@dev: our DRM device
 *
 *	Power down the display interface of our device
 */
static void power_down(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 pwr_mask ;
	u32 pwr_sts;

	if (IS_MRST(dev)) {
		pwr_mask = PSB_PWRGT_DISPLAY_MASK;
		outl(pwr_mask, dev_priv->ospm_base + PSB_PM_SSC);

		while (true) {
			pwr_sts = inl(dev_priv->ospm_base + PSB_PM_SSS);
			if ((pwr_sts & pwr_mask) == pwr_mask)
				break;
			else
				udelay(10);
		}
		dev_priv->display_power = false;
	}
}


/**
 *	gma_suspend_display	-	suspend the display logic
 *	@dev: our DRM device
 *
 *	Suspend the display logic of the graphics interface
 *
 *	FIXME: This ought to be replaced by a dev_priv-> ops interface
 *	where the various platforms register their save/restore methods
 *	and keep them in their own support files.
 */
static void gma_suspend_display(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	int pp_stat;

	if (dev_priv->suspended)
		return;

	if (IS_MFLD(dev)) {
		/* FIXME: We need to shut down panels here if using them
		   and once the right bits are merged */
		mdfld_save_cursor_overlay_registers(dev);
		mdfld_save_display_registers(dev, 0);
		mdfld_save_display_registers(dev, 0);
		mdfld_save_display_registers(dev, 2);
		mdfld_save_display_registers(dev, 1);
		mdfld_disable_crtc(dev, 0);
		mdfld_disable_crtc(dev, 2);
		mdfld_disable_crtc(dev, 1);
	} else {
		save_display_registers(dev);

		if (dev_priv->iLVDS_enable) {
			/*shutdown the panel*/
			PSB_WVDC32(0, PP_CONTROL);

			do {
				pp_stat = PSB_RVDC32(PP_STATUS);
			} while (pp_stat & 0x80000000);

			/* Turn off the plane */
			PSB_WVDC32(0x58000000, DSPACNTR);
			PSB_WVDC32(0, DSPASURF);/*trigger the plane disable*/
			/* Wait ~4 ticks */
			msleep(4);

			/* Turn off pipe */
			PSB_WVDC32(0x0, PIPEACONF);
			/* Wait ~8 ticks */
			msleep(8);

			/* Turn off PLLs */
			PSB_WVDC32(0, MRST_DPLL_A);
		} else {
			PSB_WVDC32(DPI_SHUT_DOWN, DPI_CONTROL_REG);
			PSB_WVDC32(0x0, PIPEACONF);
			PSB_WVDC32(0x2faf0000, BLC_PWM_CTL);
			while (REG_READ(0x70008) & 0x40000000)
				cpu_relax();
			while ((PSB_RVDC32(GEN_FIFO_STAT_REG) & DPI_FIFO_EMPTY)
				!= DPI_FIFO_EMPTY)
				cpu_relax();
			PSB_WVDC32(0, DEVICE_READY_REG);
			/* Turn off panel power */
#ifdef CONFIG_X86_MRST
			intel_scu_ipc_simple_command(IPC_MSG_PANEL_ON_OFF,
							IPC_CMD_PANEL_OFF);
#endif
		}
	}
	power_down(dev);
}

/*
 * power_up
 *
 * Description: Restore power to the specified island(s) (powergating)
 */
static void power_up(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 pwr_mask = PSB_PWRGT_DISPLAY_MASK;
	u32 pwr_sts, pwr_cnt;

	if (IS_MRST(dev)) {
		pwr_cnt = inl(dev_priv->ospm_base + PSB_PM_SSC);
		pwr_cnt &= ~pwr_mask;
		outl(pwr_cnt, (dev_priv->ospm_base + PSB_PM_SSC));

		while (true) {
			pwr_sts = inl(dev_priv->ospm_base + PSB_PM_SSS);
			if ((pwr_sts & pwr_mask) == 0)
				break;
			else
				udelay(10);
		}
	}
	dev_priv->suspended = false;
	dev_priv->display_power = true;
}

/**
 *	gma_resume_display	-	resume display side logic
 *
 *	Resume the display hardware restoring state and enabling
 *	as necessary.
 */
static void gma_resume_display(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;

	if (dev_priv->suspended == false)
		return;

	/* turn on the display power island */
	power_up(dev);

	PSB_WVDC32(dev_priv->pge_ctl | _PSB_PGETBL_ENABLED, PSB_PGETBL_CTL);
	pci_write_config_word(pdev, PSB_GMCH_CTRL,
			dev_priv->gmch_ctrl | _PSB_GMCH_ENABLED);

	/* Don't reinitialize the GTT as it is unnecessary.  The gtt is
	 * stored in memory so it will automatically be restored.  All
	 * we need to do is restore the PGETBL_CTL which we already do
	 * above.
	 */
	/*psb_gtt_init(dev_priv->pg, 1);*/
	if (IS_MFLD(dev)) {
		mdfld_restore_display_registers(dev, 1);
		mdfld_restore_display_registers(dev, 0);
		mdfld_restore_display_registers(dev, 2);
		mdfld_restore_cursor_overlay_registers(dev);
	} else if (IS_MRST(dev)) {
		if (!dev_priv->iLVDS_enable) {
#ifdef CONFIG_X86_MRST
			intel_scu_ipc_simple_command(IPC_MSG_PANEL_ON_OFF,
							IPC_CMD_PANEL_ON);
			/* FIXME: can we avoid this delay ? */
			msleep(2000); /* wait 2 seconds */
#endif
		}
	}
	restore_display_registers(dev);
}

/**
 *	gma_suspend_pci		-	suspend PCI side
 *	@pdev: PCI device
 *
 *	Perform the suspend processing on our PCI device state
 */
static void gma_suspend_pci(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;
	int bsm, vbt;

	if (dev_priv->suspended)
		return;

	pci_save_state(pdev);
	pci_read_config_dword(pdev, 0x5C, &bsm);
	dev_priv->saveBSM = bsm;
	pci_read_config_dword(pdev, 0xFC, &vbt);
	dev_priv->saveVBT = vbt;
	pci_read_config_dword(pdev, PSB_PCIx_MSI_ADDR_LOC, &dev_priv->msi_addr);
	pci_read_config_dword(pdev, PSB_PCIx_MSI_DATA_LOC, &dev_priv->msi_data);

	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	dev_priv->suspended = true;
}

/**
 *	gma_resume_pci		-	resume helper
 *	@dev: our PCI device
 *
 *	Perform the resume processing on our PCI device state - rewrite
 *	register state and re-enable the PCI device
 */
static bool gma_resume_pci(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;
	int ret;

	if (!dev_priv->suspended)
		return true;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_write_config_dword(pdev, 0x5c, dev_priv->saveBSM);
	pci_write_config_dword(pdev, 0xFC, dev_priv->saveVBT);
	/* retoring MSI address and data in PCIx space */
	pci_write_config_dword(pdev, PSB_PCIx_MSI_ADDR_LOC, dev_priv->msi_addr);
	pci_write_config_dword(pdev, PSB_PCIx_MSI_DATA_LOC, dev_priv->msi_data);
	ret = pci_enable_device(pdev);

	if (ret != 0)
		dev_err(&pdev->dev, "pci_enable failed: %d\n", ret);
	else
		dev_priv->suspended = false;
	return !dev_priv->suspended;
}

/**
 *	gma_power_suspend		-	bus callback for suspend
 *	@pdev: our PCI device
 *	@state: suspend type
 *
 *	Called back by the PCI layer during a suspend of the system. We
 *	perform the necessary shut down steps and save enough state that
 *	we can undo this when resume is called.
 */
int gma_power_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;

	mutex_lock(&power_mutex);
	if (!dev_priv->suspended) {
		if (dev_priv->display_count) {
			mutex_unlock(&power_mutex);
			return -EBUSY;
		}
		psb_irq_uninstall(dev);
		gma_suspend_display(dev);
		gma_suspend_pci(pdev);
	}
	mutex_unlock(&power_mutex);
	return 0;
}


/**
 *	gma_power_resume		-	resume power
 *	@pdev: PCI device
 *
 *	Resume the PCI side of the graphics and then the displays
 */
int gma_power_resume(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	mutex_lock(&power_mutex);
	gma_resume_pci(pdev);
	gma_resume_display(pdev);
	psb_irq_preinstall(dev);
	psb_irq_postinstall(dev);
	mutex_unlock(&power_mutex);
	return 0;
}



/**
 *	gma_power_is_on		-	returne true if power is on
 *	@dev: our DRM device
 *
 *	Returns true if the display island power is on at this moment
 */
bool gma_power_is_on(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	return dev_priv->display_power;
}


/**
 *	gma_power_begin		-	begin requiring power
 *	@dev: our DRM device
 *	@force_on: true to force power on
 *
 *	Begin an action that requires the display power island is enabled.
 *	We refcount the islands.
 *
 *	FIXME: locking
 */
bool gma_power_begin(struct drm_device *dev, bool force_on)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	int ret;

	/* Power already on ? */
	if (dev_priv->display_power) {
		dev_priv->display_count++;
		pm_runtime_get(&dev->pdev->dev);
		return true;
	}
	if (force_on == false)
		return false;

	/* Ok power up needed */
	ret = gma_resume_pci(dev->pdev);
	if (ret == 0) {
		psb_irq_preinstall(dev);
		psb_irq_postinstall(dev);
		pm_runtime_get(&dev->pdev->dev);
		dev_priv->display_count++;
		return true;
	}
	return false;
}


/**
 *	gma_power_end		-	end use of power
 *	@dev: Our DRM device
 *
 *	Indicate that one of our gma_power_begin() requested periods when
 *	the diplay island power is needed has completed.
 */
void gma_power_end(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	dev_priv->display_count--;
	WARN_ON(dev_priv->display_count < 0);
	pm_runtime_put(&dev->pdev->dev);
}

int psb_runtime_suspend(struct device *dev)
{
	static pm_message_t dummy;
	return gma_power_suspend(to_pci_dev(dev), dummy);
}

int psb_runtime_resume(struct device *dev)
{
	return 0;
}

int psb_runtime_idle(struct device *dev)
{
	struct drm_device *drmdev = pci_get_drvdata(to_pci_dev(dev));
	struct drm_psb_private *dev_priv = drmdev->dev_private;
	if (dev_priv->display_count)
		return 0;
	else
		return 1;
}

