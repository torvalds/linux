// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************************
 * Copyright (c) 2011, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/module.h>

#include <drm/drm.h>

#include "intel_bios.h"
#include "mid_bios.h"
#include "psb_drv.h"
#include "psb_intel_reg.h"
#include "psb_reg.h"

static int oaktrail_output_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	if (dev_priv->iLVDS_enable)
		oaktrail_lvds_init(dev, &dev_priv->mode_dev);
	else
		dev_err(dev->dev, "DSI is not supported\n");
	if (dev_priv->hdmi_priv)
		oaktrail_hdmi_init(dev, &dev_priv->mode_dev);

	psb_intel_sdvo_init(dev, SDVOB);

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

static int oaktrail_backlight_init(struct drm_device *dev)
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
	struct psb_save_area *regs = &dev_priv->regs;
	struct psb_pipe *p = &regs->pipe[0];
	int i;
	u32 pp_stat;

	/* Display arbitration control + watermarks */
	regs->psb.saveDSPARB = PSB_RVDC32(DSPARB);
	regs->psb.saveDSPFW1 = PSB_RVDC32(DSPFW1);
	regs->psb.saveDSPFW2 = PSB_RVDC32(DSPFW2);
	regs->psb.saveDSPFW3 = PSB_RVDC32(DSPFW3);
	regs->psb.saveDSPFW4 = PSB_RVDC32(DSPFW4);
	regs->psb.saveDSPFW5 = PSB_RVDC32(DSPFW5);
	regs->psb.saveDSPFW6 = PSB_RVDC32(DSPFW6);
	regs->psb.saveCHICKENBIT = PSB_RVDC32(DSPCHICKENBIT);

	/* Pipe & plane A info */
	p->conf = PSB_RVDC32(PIPEACONF);
	p->src = PSB_RVDC32(PIPEASRC);
	p->fp0 = PSB_RVDC32(MRST_FPA0);
	p->fp1 = PSB_RVDC32(MRST_FPA1);
	p->dpll = PSB_RVDC32(MRST_DPLL_A);
	p->htotal = PSB_RVDC32(HTOTAL_A);
	p->hblank = PSB_RVDC32(HBLANK_A);
	p->hsync = PSB_RVDC32(HSYNC_A);
	p->vtotal = PSB_RVDC32(VTOTAL_A);
	p->vblank = PSB_RVDC32(VBLANK_A);
	p->vsync = PSB_RVDC32(VSYNC_A);
	regs->psb.saveBCLRPAT_A = PSB_RVDC32(BCLRPAT_A);
	p->cntr = PSB_RVDC32(DSPACNTR);
	p->stride = PSB_RVDC32(DSPASTRIDE);
	p->addr = PSB_RVDC32(DSPABASE);
	p->surf = PSB_RVDC32(DSPASURF);
	p->linoff = PSB_RVDC32(DSPALINOFF);
	p->tileoff = PSB_RVDC32(DSPATILEOFF);

	/* Save cursor regs */
	regs->psb.saveDSPACURSOR_CTRL = PSB_RVDC32(CURACNTR);
	regs->psb.saveDSPACURSOR_BASE = PSB_RVDC32(CURABASE);
	regs->psb.saveDSPACURSOR_POS = PSB_RVDC32(CURAPOS);

	/* Save palette (gamma) */
	for (i = 0; i < 256; i++)
		p->palette[i] = PSB_RVDC32(PALETTE_A + (i << 2));

	if (dev_priv->hdmi_priv)
		oaktrail_hdmi_save(dev);

	/* Save performance state */
	regs->psb.savePERF_MODE = PSB_RVDC32(MRST_PERF_MODE);

	/* LVDS state */
	regs->psb.savePP_CONTROL = PSB_RVDC32(PP_CONTROL);
	regs->psb.savePFIT_PGM_RATIOS = PSB_RVDC32(PFIT_PGM_RATIOS);
	regs->psb.savePFIT_AUTO_RATIOS = PSB_RVDC32(PFIT_AUTO_RATIOS);
	regs->saveBLC_PWM_CTL = PSB_RVDC32(BLC_PWM_CTL);
	regs->saveBLC_PWM_CTL2 = PSB_RVDC32(BLC_PWM_CTL2);
	regs->psb.saveLVDS = PSB_RVDC32(LVDS);
	regs->psb.savePFIT_CONTROL = PSB_RVDC32(PFIT_CONTROL);
	regs->psb.savePP_ON_DELAYS = PSB_RVDC32(LVDSPP_ON);
	regs->psb.savePP_OFF_DELAYS = PSB_RVDC32(LVDSPP_OFF);
	regs->psb.savePP_DIVISOR = PSB_RVDC32(PP_CYCLE);

	/* HW overlay */
	regs->psb.saveOV_OVADD = PSB_RVDC32(OV_OVADD);
	regs->psb.saveOV_OGAMC0 = PSB_RVDC32(OV_OGAMC0);
	regs->psb.saveOV_OGAMC1 = PSB_RVDC32(OV_OGAMC1);
	regs->psb.saveOV_OGAMC2 = PSB_RVDC32(OV_OGAMC2);
	regs->psb.saveOV_OGAMC3 = PSB_RVDC32(OV_OGAMC3);
	regs->psb.saveOV_OGAMC4 = PSB_RVDC32(OV_OGAMC4);
	regs->psb.saveOV_OGAMC5 = PSB_RVDC32(OV_OGAMC5);

	/* DPST registers */
	regs->psb.saveHISTOGRAM_INT_CONTROL_REG =
					PSB_RVDC32(HISTOGRAM_INT_CONTROL);
	regs->psb.saveHISTOGRAM_LOGIC_CONTROL_REG =
					PSB_RVDC32(HISTOGRAM_LOGIC_CONTROL);
	regs->psb.savePWM_CONTROL_LOGIC = PSB_RVDC32(PWM_CONTROL_LOGIC);

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
	struct psb_save_area *regs = &dev_priv->regs;
	struct psb_pipe *p = &regs->pipe[0];
	u32 pp_stat;
	int i;

	/* Display arbitration + watermarks */
	PSB_WVDC32(regs->psb.saveDSPARB, DSPARB);
	PSB_WVDC32(regs->psb.saveDSPFW1, DSPFW1);
	PSB_WVDC32(regs->psb.saveDSPFW2, DSPFW2);
	PSB_WVDC32(regs->psb.saveDSPFW3, DSPFW3);
	PSB_WVDC32(regs->psb.saveDSPFW4, DSPFW4);
	PSB_WVDC32(regs->psb.saveDSPFW5, DSPFW5);
	PSB_WVDC32(regs->psb.saveDSPFW6, DSPFW6);
	PSB_WVDC32(regs->psb.saveCHICKENBIT, DSPCHICKENBIT);

	/* Make sure VGA plane is off. it initializes to on after reset!*/
	PSB_WVDC32(0x80000000, VGACNTRL);

	/* set the plls */
	PSB_WVDC32(p->fp0, MRST_FPA0);
	PSB_WVDC32(p->fp1, MRST_FPA1);

	/* Actually enable it */
	PSB_WVDC32(p->dpll, MRST_DPLL_A);
	udelay(150);

	/* Restore mode */
	PSB_WVDC32(p->htotal, HTOTAL_A);
	PSB_WVDC32(p->hblank, HBLANK_A);
	PSB_WVDC32(p->hsync, HSYNC_A);
	PSB_WVDC32(p->vtotal, VTOTAL_A);
	PSB_WVDC32(p->vblank, VBLANK_A);
	PSB_WVDC32(p->vsync, VSYNC_A);
	PSB_WVDC32(p->src, PIPEASRC);
	PSB_WVDC32(regs->psb.saveBCLRPAT_A, BCLRPAT_A);

	/* Restore performance mode*/
	PSB_WVDC32(regs->psb.savePERF_MODE, MRST_PERF_MODE);

	/* Enable the pipe*/
	if (dev_priv->iLVDS_enable)
		PSB_WVDC32(p->conf, PIPEACONF);

	/* Set up the plane*/
	PSB_WVDC32(p->linoff, DSPALINOFF);
	PSB_WVDC32(p->stride, DSPASTRIDE);
	PSB_WVDC32(p->tileoff, DSPATILEOFF);

	/* Enable the plane */
	PSB_WVDC32(p->cntr, DSPACNTR);
	PSB_WVDC32(p->surf, DSPASURF);

	/* Enable Cursor A */
	PSB_WVDC32(regs->psb.saveDSPACURSOR_CTRL, CURACNTR);
	PSB_WVDC32(regs->psb.saveDSPACURSOR_POS, CURAPOS);
	PSB_WVDC32(regs->psb.saveDSPACURSOR_BASE, CURABASE);

	/* Restore palette (gamma) */
	for (i = 0; i < 256; i++)
		PSB_WVDC32(p->palette[i], PALETTE_A + (i << 2));

	if (dev_priv->hdmi_priv)
		oaktrail_hdmi_restore(dev);

	if (dev_priv->iLVDS_enable) {
		PSB_WVDC32(regs->saveBLC_PWM_CTL2, BLC_PWM_CTL2);
		PSB_WVDC32(regs->psb.saveLVDS, LVDS); /*port 61180h*/
		PSB_WVDC32(regs->psb.savePFIT_CONTROL, PFIT_CONTROL);
		PSB_WVDC32(regs->psb.savePFIT_PGM_RATIOS, PFIT_PGM_RATIOS);
		PSB_WVDC32(regs->psb.savePFIT_AUTO_RATIOS, PFIT_AUTO_RATIOS);
		PSB_WVDC32(regs->saveBLC_PWM_CTL, BLC_PWM_CTL);
		PSB_WVDC32(regs->psb.savePP_ON_DELAYS, LVDSPP_ON);
		PSB_WVDC32(regs->psb.savePP_OFF_DELAYS, LVDSPP_OFF);
		PSB_WVDC32(regs->psb.savePP_DIVISOR, PP_CYCLE);
		PSB_WVDC32(regs->psb.savePP_CONTROL, PP_CONTROL);
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
	PSB_WVDC32(regs->psb.saveOV_OVADD, OV_OVADD);
	PSB_WVDC32(regs->psb.saveOV_OGAMC0, OV_OGAMC0);
	PSB_WVDC32(regs->psb.saveOV_OGAMC1, OV_OGAMC1);
	PSB_WVDC32(regs->psb.saveOV_OGAMC2, OV_OGAMC2);
	PSB_WVDC32(regs->psb.saveOV_OGAMC3, OV_OGAMC3);
	PSB_WVDC32(regs->psb.saveOV_OGAMC4, OV_OGAMC4);
	PSB_WVDC32(regs->psb.saveOV_OGAMC5, OV_OGAMC5);

	/* DPST registers */
	PSB_WVDC32(regs->psb.saveHISTOGRAM_INT_CONTROL_REG,
						HISTOGRAM_INT_CONTROL);
	PSB_WVDC32(regs->psb.saveHISTOGRAM_LOGIC_CONTROL_REG,
						HISTOGRAM_LOGIC_CONTROL);
	PSB_WVDC32(regs->psb.savePWM_CONTROL_LOGIC, PWM_CONTROL_LOGIC);

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

/* Oaktrail */
static const struct psb_offset oaktrail_regmap[2] = {
	{
		.fp0 = MRST_FPA0,
		.fp1 = MRST_FPA1,
		.cntr = DSPACNTR,
		.conf = PIPEACONF,
		.src = PIPEASRC,
		.dpll = MRST_DPLL_A,
		.htotal = HTOTAL_A,
		.hblank = HBLANK_A,
		.hsync = HSYNC_A,
		.vtotal = VTOTAL_A,
		.vblank = VBLANK_A,
		.vsync = VSYNC_A,
		.stride = DSPASTRIDE,
		.size = DSPASIZE,
		.pos = DSPAPOS,
		.surf = DSPASURF,
		.addr = MRST_DSPABASE,
		.base = MRST_DSPABASE,
		.status = PIPEASTAT,
		.linoff = DSPALINOFF,
		.tileoff = DSPATILEOFF,
		.palette = PALETTE_A,
	},
	{
		.fp0 = FPB0,
		.fp1 = FPB1,
		.cntr = DSPBCNTR,
		.conf = PIPEBCONF,
		.src = PIPEBSRC,
		.dpll = DPLL_B,
		.htotal = HTOTAL_B,
		.hblank = HBLANK_B,
		.hsync = HSYNC_B,
		.vtotal = VTOTAL_B,
		.vblank = VBLANK_B,
		.vsync = VSYNC_B,
		.stride = DSPBSTRIDE,
		.size = DSPBSIZE,
		.pos = DSPBPOS,
		.surf = DSPBSURF,
		.addr = DSPBBASE,
		.base = DSPBBASE,
		.status = PIPEBSTAT,
		.linoff = DSPBLINOFF,
		.tileoff = DSPBTILEOFF,
		.palette = PALETTE_B,
	},
};

static int oaktrail_chip_setup(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int ret;

	if (pci_enable_msi(pdev))
		dev_warn(dev->dev, "Enabling MSI failed!\n");

	dev_priv->regmap = oaktrail_regmap;

	ret = mid_chip_setup(dev);
	if (ret < 0)
		return ret;
	if (!dev_priv->has_gct) {
		/* Now pull the BIOS data */
		psb_intel_opregion_init(dev);
		psb_intel_init_bios(dev);
	}
	gma_intel_setup_gmbus(dev);
	oaktrail_hdmi_setup(dev);
	return 0;
}

static void oaktrail_teardown(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	gma_intel_teardown_gmbus(dev);
	oaktrail_hdmi_teardown(dev);
	if (!dev_priv->has_gct)
		psb_intel_destroy_bios(dev);
}

const struct psb_ops oaktrail_chip_ops = {
	.name = "Oaktrail",
	.pipes = 2,
	.crtcs = 2,
	.hdmi_mask = (1 << 1),
	.lvds_mask = (1 << 0),
	.sdvo_mask = (1 << 1),
	.cursor_needs_phys = 0,
	.sgx_offset = MRST_SGX_OFFSET,

	.chip_setup = oaktrail_chip_setup,
	.chip_teardown = oaktrail_teardown,
	.crtc_helper = &oaktrail_helper_funcs,
	.crtc_funcs = &psb_intel_crtc_funcs,

	.output_init = oaktrail_output_init,

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	.backlight_init = oaktrail_backlight_init,
#endif

	.save_regs = oaktrail_save_display_registers,
	.restore_regs = oaktrail_restore_display_registers,
	.save_crtc = gma_crtc_save,
	.restore_crtc = gma_crtc_restore,
	.power_down = oaktrail_power_down,
	.power_up = oaktrail_power_up,

	.i2c_bus = 1,
};
