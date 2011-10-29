/**************************************************************************
 * Copyright (c) 2011, Intel Corporation.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

#include <linux/backlight.h>
#include <drm/drmP.h>
#include <drm/drm.h>
#include "psb_reg.h"
#include "psb_intel_reg.h"
#include "psb_drm.h"
#include "psb_drv.h"
#include "mdfld_output.h"
#include "mdfld_dsi_output.h"
#include "mid_bios.h"

/*
 *	Provide the Medfield specific backlight management
 */

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE

static int mdfld_brightness;
struct backlight_device *mdfld_backlight_device;

static int mfld_set_brightness(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(mdfld_backlight_device);
	struct drm_psb_private *dev_priv = dev->dev_private;
	int level = bd->props.brightness;

	/* Percentage 1-100% being valid */
	if (level < 1)
		level = 1;

	if (gma_power_begin(dev, 0)) {
		/* Calculate and set the brightness value */
		u32 adjusted_level;

		/* Adjust the backlight level with the percent in
		 * dev_priv->blc_adj2;
		 */
		adjusted_level = level * dev_priv->blc_adj2;
		adjusted_level = adjusted_level / 100;
#if 0
#ifndef CONFIG_MDFLD_DSI_DPU
		if(!(dev_priv->dsr_fb_update & MDFLD_DSR_MIPI_CONTROL) && 
			(dev_priv->dbi_panel_on || dev_priv->dbi_panel_on2)){
			mdfld_dsi_dbi_exit_dsr(dev,MDFLD_DSR_MIPI_CONTROL, 0, 0);
			dev_dbg(dev->dev, "Out of DSR before set brightness to %d.\n",adjusted_level);
		}
#endif
		mdfld_dsi_brightness_control(dev, 0, adjusted_level);

		if ((dev_priv->dbi_panel_on2) || (dev_priv->dpi_panel_on2))
			mdfld_dsi_brightness_control(dev, 2, adjusted_level);
#endif
		gma_power_end(dev);
	}
	mdfld_brightness = level;
	return 0;
}

int psb_get_brightness(struct backlight_device *bd)
{
	/* return locally cached var instead of HW read (due to DPST etc.) */
	/* FIXME: ideally return actual value in case firmware fiddled with
	   it */
	return mdfld_brightness;
}

static const struct backlight_ops mfld_ops = {
	.get_brightness = psb_get_brightness,
	.update_status  = mfld_set_brightness,
};

static int mdfld_backlight_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct backlight_properties props;
	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 100;
	props.type = BACKLIGHT_PLATFORM;

	mdfld_backlight_device = backlight_device_register("mfld-bl",
					NULL, (void *)dev, &mfld_ops, &props);
					
	if (IS_ERR(mdfld_backlight_device))
		return PTR_ERR(mdfld_backlight_device);

	dev_priv->blc_adj1 = 100;
	dev_priv->blc_adj2 = 100;
	mdfld_backlight_device->props.brightness = 100;
	mdfld_backlight_device->props.max_brightness = 100;
	backlight_update_status(mdfld_backlight_device);
	dev_priv->backlight_device = mdfld_backlight_device;
	return 0;
}

#endif

/*
 *	Provide the Medfield specific chip logic and low level methods for
 *	power management.
 */

static void mdfld_init_pm(struct drm_device *dev)
{
	/* No work needed here yet */
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
	if (!mdfld_panel_dpi(dev))
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
 *	mdfld_save_display_registers	-	save registers lost on suspend
 *	@dev: our DRM device
 *
 *	Save the state we need in order to be able to restore the interface
 *	upon resume from suspend
 */
static int mdfld_save_registers(struct drm_device *dev)
{
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
	return 0;
}

/**
 *	mdfld_restore_display_registers	-	restore lost register state
 *	@dev: our DRM device
 *
 *	Restore register state that was lost during suspend and resume.
 */
static int mdfld_restore_registers(struct drm_device *dev)
{
	mdfld_restore_display_registers(dev, 1);
	mdfld_restore_display_registers(dev, 0);
	mdfld_restore_display_registers(dev, 2);
	mdfld_restore_cursor_overlay_registers(dev);
	return 0;
}

static int mdfld_power_down(struct drm_device *dev)
{
	/* FIXME */
	return 0;
}

static int mdfld_power_up(struct drm_device *dev)
{
	/* FIXME */
	return 0;
}

const struct psb_ops mdfld_chip_ops = {
	.name = "Medfield",
	.accel_2d = 0,
	.pipes = 3,
	.crtcs = 2,
	.sgx_offset = MRST_SGX_OFFSET,

	.chip_setup = mid_chip_setup,

	.crtc_helper = &mdfld_helper_funcs,
	.crtc_funcs = &mdfld_intel_crtc_funcs,

	.output_init = mdfld_output_init,

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	.backlight_init = mdfld_backlight_init,
#endif

	.init_pm = mdfld_init_pm,
	.save_regs = mdfld_save_registers,
	.restore_regs = mdfld_restore_registers,
	.power_down = mdfld_power_down,
	.power_up = mdfld_power_up,
};

