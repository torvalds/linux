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
#include <linux/module.h>
#include <linux/dmi.h>
#include <drm/drmP.h>
#include <drm/drm.h>
#include "gma_drm.h"
#include "psb_drv.h"
#include "psb_reg.h"
#include "psb_intel_reg.h"
#include <asm/mrst.h>
#include <asm/intel_scu_ipc.h>
#include "mid_bios.h"
#include "intel_bios.h"

static int oaktrail_output_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	if (dev_priv->iLVDS_enable)
		oaktrail_lvds_init(dev, &dev_priv->mode_dev);
	else
		dev_err(dev->dev, "DSI is not supported\n");
	if (dev_priv->hdmi_priv)
		oaktrail_hdmi_init(dev, &dev_priv->mode_dev);
	return 0;
}

/*
 *	Provide the low level interfaces for the Moorestown backlight
 */

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE

#define MRST_BLC_MAX_PWM_REG_FREQ	    0xFFFF
#define BLC_PWM_PRECISION_FACTOR 100	/* 10000000 */
#define BLC_PWM_FREQ_CALC_CONSTANT 32
#define MHz 1000000
#define BLC_ADJUSTMENT_MAX 100

static struct backlight_device *oaktrail_backlight_device;
static int oaktrail_brightness;

static int oaktrail_set_brightness(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(oaktrail_backlight_device);
	struct drm_psb_private *dev_priv = dev->dev_private;
	int level = bd->props.brightness;
	u32 blc_pwm_ctl;
	u32 max_pwm_blc;

	/* Percentage 1-100% being valid */
	if (level < 1)
		level = 1;

	if (gma_power_begin(dev, 0)) {
		/* Calculate and set the brightness value */
		max_pwm_blc = REG_READ(BLC_PWM_CTL) >> 16;
		blc_pwm_ctl = level * max_pwm_blc / 100;

		/* Adjust the backlight level with the percent in
		 * dev_priv->blc_adj1;
		 */
		blc_pwm_ctl = blc_pwm_ctl * dev_priv->blc_adj1;
		blc_pwm_ctl = blc_pwm_ctl / 100;

		/* Adjust the backlight level with the percent in
		 * dev_priv->blc_adj2;
		 */
		blc_pwm_ctl = blc_pwm_ctl * dev_priv->blc_adj2;
		blc_pwm_ctl = blc_pwm_ctl / 100;

		/* force PWM bit on */
		REG_WRITE(BLC_PWM_CTL2, (0x80000000 | REG_READ(BLC_PWM_CTL2)));
		REG_WRITE(BLC_PWM_CTL, (max_pwm_blc << 16) | blc_pwm_ctl);
		gma_power_end(dev);
	}
	oaktrail_brightness = level;
	return 0;
}

static int oaktrail_get_brightness(struct backlight_device *bd)
{
	/* return locally cached var instead of HW read (due to DPST etc.) */
	/* FIXME: ideally return actual value in case firmware fiddled with
	   it */
	return oaktrail_brightness;
}

static int device_backlight_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	unsigned long core_clock;
	u16 bl_max_freq;
	uint32_t value;
	uint32_t blc_pwm_precision_factor;

	dev_priv->blc_adj1 = BLC_ADJUSTMENT_MAX;
	dev_priv->blc_adj2 = BLC_ADJUSTMENT_MAX;
	bl_max_freq = 256;
	/* this needs to be set elsewhere */
	blc_pwm_precision_factor = BLC_PWM_PRECISION_FACTOR;

	core_clock = dev_priv->core_freq;

	value = (core_clock * MHz) / BLC_PWM_FREQ_CALC_CONSTANT;
	value *= blc_pwm_precision_factor;
	value /= bl_max_freq;
	value /= blc_pwm_precision_factor;

	if (value > (unsigned long long)MRST_BLC_MAX_PWM_REG_FREQ)
			return -ERANGE;

	if (gma_power_begin(dev, false)) {
		REG_WRITE(BLC_PWM_CTL2, (0x80000000 | REG_READ(BLC_PWM_CTL2)));
		REG_WRITE(BLC_PWM_CTL, value | (value << 16));
		gma_power_end(dev);
	}
	return 0;
}

static const struct backlight_ops oaktrail_ops = {
	.get_brightness = oaktrail_get_brightness,
	.update_status  = oaktrail_set_brightness,
};

int oaktrail_backlight_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	int ret;
	struct backlight_properties props;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 100;
	props.type = BACKLIGHT_PLATFORM;

	oaktrail_backlight_device = backlight_device_register("oaktrail-bl",
				NULL, (void *)dev, &oaktrail_ops, &props);

	if (IS_ERR(oaktrail_backlight_device))
		return PTR_ERR(oaktrail_backlight_device);

	ret = device_backlight_init(dev);
	if (ret < 0) {
		backlight_device_unregister(oaktrail_backlight_device);
		return ret;
	}
	oaktrail_backlight_device->props.brightness = 100;
	oaktrail_backlight_device->props.max_brightness = 100;
	backlight_update_status(oaktrail_backlight_device);
	dev_priv->backlight_device = oaktrail_backlight_device;
	return 0;
}

#endif

/*
 *	Provide the Moorestown specific chip logic and low level methods
 *	for power management
 */

static void oaktrail_init_pm(struct drm_device *dev)
{
}

/**
 *	oaktrail_save_display_registers	-	save registers lost on suspend
 *	@dev: our DRM device
 *
 *	Save the state we need in order to be able to restore the interface
 *	upon resume from suspend
 */
static int oaktrail_save_display_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	int i;
	u32 pp_stat;

	/* Display arbitration control + watermarks */
	dev_priv->saveDSPARB = PSB_RVDC32(DSPARB);
	dev_priv->saveDSPFW1 = PSB_RVDC32(DSPFW1);
	dev_priv->saveDSPFW2 = PSB_RVDC32(DSPFW2);
	dev_priv->saveDSPFW3 = PSB_RVDC32(DSPFW3);
	dev_priv->saveDSPFW4 = PSB_RVDC32(DSPFW4);
	dev_priv->saveDSPFW5 = PSB_RVDC32(DSPFW5);
	dev_priv->saveDSPFW6 = PSB_RVDC32(DSPFW6);
	dev_priv->saveCHICKENBIT = PSB_RVDC32(DSPCHICKENBIT);

	/* Pipe & plane A info */
	dev_priv->savePIPEACONF = PSB_RVDC32(PIPEACONF);
	dev_priv->savePIPEASRC = PSB_RVDC32(PIPEASRC);
	dev_priv->saveFPA0 = PSB_RVDC32(MRST_FPA0);
	dev_priv->saveFPA1 = PSB_RVDC32(MRST_FPA1);
	dev_priv->saveDPLL_A = PSB_RVDC32(MRST_DPLL_A);
	dev_priv->saveHTOTAL_A = PSB_RVDC32(HTOTAL_A);
	dev_priv->saveHBLANK_A = PSB_RVDC32(HBLANK_A);
	dev_priv->saveHSYNC_A = PSB_RVDC32(HSYNC_A);
	dev_priv->saveVTOTAL_A = PSB_RVDC32(VTOTAL_A);
	dev_priv->saveVBLANK_A = PSB_RVDC32(VBLANK_A);
	dev_priv->saveVSYNC_A = PSB_RVDC32(VSYNC_A);
	dev_priv->saveBCLRPAT_A = PSB_RVDC32(BCLRPAT_A);
	dev_priv->saveDSPACNTR = PSB_RVDC32(DSPACNTR);
	dev_priv->saveDSPASTRIDE = PSB_RVDC32(DSPASTRIDE);
	dev_priv->saveDSPAADDR = PSB_RVDC32(DSPABASE);
	dev_priv->saveDSPASURF = PSB_RVDC32(DSPASURF);
	dev_priv->saveDSPALINOFF = PSB_RVDC32(DSPALINOFF);
	dev_priv->saveDSPATILEOFF = PSB_RVDC32(DSPATILEOFF);

	/* Save cursor regs */
	dev_priv->saveDSPACURSOR_CTRL = PSB_RVDC32(CURACNTR);
	dev_priv->saveDSPACURSOR_BASE = PSB_RVDC32(CURABASE);
	dev_priv->saveDSPACURSOR_POS = PSB_RVDC32(CURAPOS);

	/* Save palette (gamma) */
	for (i = 0; i < 256; i++)
		dev_priv->save_palette_a[i] = PSB_RVDC32(PALETTE_A + (i << 2));

	if (dev_priv->hdmi_priv)
		oaktrail_hdmi_save(dev);

	/* Save performance state */
	dev_priv->savePERF_MODE = PSB_RVDC32(MRST_PERF_MODE);

	/* LVDS state */
	dev_priv->savePP_CONTROL = PSB_RVDC32(PP_CONTROL);
	dev_priv->savePFIT_PGM_RATIOS = PSB_RVDC32(PFIT_PGM_RATIOS);
	dev_priv->savePFIT_AUTO_RATIOS = PSB_RVDC32(PFIT_AUTO_RATIOS);
	dev_priv->saveBLC_PWM_CTL = PSB_RVDC32(BLC_PWM_CTL);
	dev_priv->saveBLC_PWM_CTL2 = PSB_RVDC32(BLC_PWM_CTL2);
	dev_priv->saveLVDS = PSB_RVDC32(LVDS);
	dev_priv->savePFIT_CONTROL = PSB_RVDC32(PFIT_CONTROL);
	dev_priv->savePP_ON_DELAYS = PSB_RVDC32(LVDSPP_ON);
	dev_priv->savePP_OFF_DELAYS = PSB_RVDC32(LVDSPP_OFF);
	dev_priv->savePP_DIVISOR = PSB_RVDC32(PP_CYCLE);

	/* HW overlay */
	dev_priv->saveOV_OVADD = PSB_RVDC32(OV_OVADD);
	dev_priv->saveOV_OGAMC0 = PSB_RVDC32(OV_OGAMC0);
	dev_priv->saveOV_OGAMC1 = PSB_RVDC32(OV_OGAMC1);
	dev_priv->saveOV_OGAMC2 = PSB_RVDC32(OV_OGAMC2);
	dev_priv->saveOV_OGAMC3 = PSB_RVDC32(OV_OGAMC3);
	dev_priv->saveOV_OGAMC4 = PSB_RVDC32(OV_OGAMC4);
	dev_priv->saveOV_OGAMC5 = PSB_RVDC32(OV_OGAMC5);

	/* DPST registers */
	dev_priv->saveHISTOGRAM_INT_CONTROL_REG =
					PSB_RVDC32(HISTOGRAM_INT_CONTROL);
	dev_priv->saveHISTOGRAM_LOGIC_CONTROL_REG =
					PSB_RVDC32(HISTOGRAM_LOGIC_CONTROL);
	dev_priv->savePWM_CONTROL_LOGIC = PSB_RVDC32(PWM_CONTROL_LOGIC);

	if (dev_priv->iLVDS_enable) {
		/* Shut down the panel */
		PSB_WVDC32(0, PP_CONTROL);

		do {
			pp_stat = PSB_RVDC32(PP_STATUS);
		} while (pp_stat & 0x80000000);

		/* Turn off the plane */
		PSB_WVDC32(0x58000000, DSPACNTR);
		/* Trigger the plane disable */
		PSB_WVDC32(0, DSPASURF);

		/* Wait ~4 ticks */
		msleep(4);

		/* Turn off pipe */
		PSB_WVDC32(0x0, PIPEACONF);
		/* Wait ~8 ticks */
		msleep(8);

		/* Turn off PLLs */
		PSB_WVDC32(0, MRST_DPLL_A);
	}
	return 0;
}

/**
 *	oaktrail_restore_display_registers	-	restore lost register state
 *	@dev: our DRM device
 *
 *	Restore register state that was lost during suspend and resume.
 */
static int oaktrail_restore_display_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 pp_stat;
	int i;

	/* Display arbitration + watermarks */
	PSB_WVDC32(dev_priv->saveDSPARB, DSPARB);
	PSB_WVDC32(dev_priv->saveDSPFW1, DSPFW1);
	PSB_WVDC32(dev_priv->saveDSPFW2, DSPFW2);
	PSB_WVDC32(dev_priv->saveDSPFW3, DSPFW3);
	PSB_WVDC32(dev_priv->saveDSPFW4, DSPFW4);
	PSB_WVDC32(dev_priv->saveDSPFW5, DSPFW5);
	PSB_WVDC32(dev_priv->saveDSPFW6, DSPFW6);
	PSB_WVDC32(dev_priv->saveCHICKENBIT, DSPCHICKENBIT);

	/* Make sure VGA plane is off. it initializes to on after reset!*/
	PSB_WVDC32(0x80000000, VGACNTRL);

	/* set the plls */
	PSB_WVDC32(dev_priv->saveFPA0, MRST_FPA0);
	PSB_WVDC32(dev_priv->saveFPA1, MRST_FPA1);

	/* Actually enable it */
	PSB_WVDC32(dev_priv->saveDPLL_A, MRST_DPLL_A);
	DRM_UDELAY(150);

	/* Restore mode */
	PSB_WVDC32(dev_priv->saveHTOTAL_A, HTOTAL_A);
	PSB_WVDC32(dev_priv->saveHBLANK_A, HBLANK_A);
	PSB_WVDC32(dev_priv->saveHSYNC_A, HSYNC_A);
	PSB_WVDC32(dev_priv->saveVTOTAL_A, VTOTAL_A);
	PSB_WVDC32(dev_priv->saveVBLANK_A, VBLANK_A);
	PSB_WVDC32(dev_priv->saveVSYNC_A, VSYNC_A);
	PSB_WVDC32(dev_priv->savePIPEASRC, PIPEASRC);
	PSB_WVDC32(dev_priv->saveBCLRPAT_A, BCLRPAT_A);

	/* Restore performance mode*/
	PSB_WVDC32(dev_priv->savePERF_MODE, MRST_PERF_MODE);

	/* Enable the pipe*/
	if (dev_priv->iLVDS_enable)
		PSB_WVDC32(dev_priv->savePIPEACONF, PIPEACONF);

	/* Set up the plane*/
	PSB_WVDC32(dev_priv->saveDSPALINOFF, DSPALINOFF);
	PSB_WVDC32(dev_priv->saveDSPASTRIDE, DSPASTRIDE);
	PSB_WVDC32(dev_priv->saveDSPATILEOFF, DSPATILEOFF);

	/* Enable the plane */
	PSB_WVDC32(dev_priv->saveDSPACNTR, DSPACNTR);
	PSB_WVDC32(dev_priv->saveDSPASURF, DSPASURF);

	/* Enable Cursor A */
	PSB_WVDC32(dev_priv->saveDSPACURSOR_CTRL, CURACNTR);
	PSB_WVDC32(dev_priv->saveDSPACURSOR_POS, CURAPOS);
	PSB_WVDC32(dev_priv->saveDSPACURSOR_BASE, CURABASE);

	/* Restore palette (gamma) */
	for (i = 0; i < 256; i++)
		PSB_WVDC32(dev_priv->save_palette_a[i], PALETTE_A + (i << 2));

	if (dev_priv->hdmi_priv)
		oaktrail_hdmi_restore(dev);

	if (dev_priv->iLVDS_enable) {
		PSB_WVDC32(dev_priv->saveBLC_PWM_CTL2, BLC_PWM_CTL2);
		PSB_WVDC32(dev_priv->saveLVDS, LVDS); /*port 61180h*/
		PSB_WVDC32(dev_priv->savePFIT_CONTROL, PFIT_CONTROL);
		PSB_WVDC32(dev_priv->savePFIT_PGM_RATIOS, PFIT_PGM_RATIOS);
		PSB_WVDC32(dev_priv->savePFIT_AUTO_RATIOS, PFIT_AUTO_RATIOS);
		PSB_WVDC32(dev_priv->saveBLC_PWM_CTL, BLC_PWM_CTL);
		PSB_WVDC32(dev_priv->savePP_ON_DELAYS, LVDSPP_ON);
		PSB_WVDC32(dev_priv->savePP_OFF_DELAYS, LVDSPP_OFF);
		PSB_WVDC32(dev_priv->savePP_DIVISOR, PP_CYCLE);
		PSB_WVDC32(dev_priv->savePP_CONTROL, PP_CONTROL);
	}

	/* Wait for cycle delay */
	do {
		pp_stat = PSB_RVDC32(PP_STATUS);
	} while (pp_stat & 0x08000000);

	/* Wait for panel power up */
	do {
		pp_stat = PSB_RVDC32(PP_STATUS);
	} while (pp_stat & 0x10000000);

	/* Restore HW overlay */
	PSB_WVDC32(dev_priv->saveOV_OVADD, OV_OVADD);
	PSB_WVDC32(dev_priv->saveOV_OGAMC0, OV_OGAMC0);
	PSB_WVDC32(dev_priv->saveOV_OGAMC1, OV_OGAMC1);
	PSB_WVDC32(dev_priv->saveOV_OGAMC2, OV_OGAMC2);
	PSB_WVDC32(dev_priv->saveOV_OGAMC3, OV_OGAMC3);
	PSB_WVDC32(dev_priv->saveOV_OGAMC4, OV_OGAMC4);
	PSB_WVDC32(dev_priv->saveOV_OGAMC5, OV_OGAMC5);

	/* DPST registers */
	PSB_WVDC32(dev_priv->saveHISTOGRAM_INT_CONTROL_REG,
						HISTOGRAM_INT_CONTROL);
	PSB_WVDC32(dev_priv->saveHISTOGRAM_LOGIC_CONTROL_REG,
						HISTOGRAM_LOGIC_CONTROL);
	PSB_WVDC32(dev_priv->savePWM_CONTROL_LOGIC, PWM_CONTROL_LOGIC);

	return 0;
}

/**
 *	oaktrail_power_down	-	power down the display island
 *	@dev: our DRM device
 *
 *	Power down the display interface of our device
 */
static int oaktrail_power_down(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 pwr_mask ;
	u32 pwr_sts;

	pwr_mask = PSB_PWRGT_DISPLAY_MASK;
	outl(pwr_mask, dev_priv->ospm_base + PSB_PM_SSC);

	while (true) {
		pwr_sts = inl(dev_priv->ospm_base + PSB_PM_SSS);
		if ((pwr_sts & pwr_mask) == pwr_mask)
			break;
		else
			udelay(10);
	}
	return 0;
}

/*
 * oaktrail_power_up
 *
 * Restore power to the specified island(s) (powergating)
 */
static int oaktrail_power_up(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 pwr_mask = PSB_PWRGT_DISPLAY_MASK;
	u32 pwr_sts, pwr_cnt;

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
	return 0;
}


static int oaktrail_chip_setup(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct oaktrail_vbt *vbt = &dev_priv->vbt_data;
	int ret;
	
	ret = mid_chip_setup(dev);
	if (ret < 0)
		return ret;
	if (vbt->size == 0) {
		/* Now pull the BIOS data */
		gma_intel_opregion_init(dev);
		psb_intel_init_bios(dev);
	}
	return 0;
}

static void oaktrail_teardown(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct oaktrail_vbt *vbt = &dev_priv->vbt_data;

	oaktrail_hdmi_teardown(dev);
	if (vbt->size == 0)
		psb_intel_destroy_bios(dev);
}

const struct psb_ops oaktrail_chip_ops = {
	.name = "Oaktrail",
	.accel_2d = 1,
	.pipes = 2,
	.crtcs = 2,
	.sgx_offset = MRST_SGX_OFFSET,

	.chip_setup = oaktrail_chip_setup,
	.chip_teardown = oaktrail_teardown,
	.crtc_helper = &oaktrail_helper_funcs,
	.crtc_funcs = &psb_intel_crtc_funcs,

	.output_init = oaktrail_output_init,

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	.backlight_init = oaktrail_backlight_init,
#endif

	.init_pm = oaktrail_init_pm,
	.save_regs = oaktrail_save_display_registers,
	.restore_regs = oaktrail_restore_display_registers,
	.power_down = oaktrail_power_down,
	.power_up = oaktrail_power_up,

	.i2c_bus = 1,
};
