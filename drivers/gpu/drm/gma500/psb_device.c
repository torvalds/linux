// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************************
 * Copyright (c) 2011, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#include <drm/drm.h>
#include <drm/drm_crtc_helper.h>

#include "gma_device.h"
#include "intel_bios.h"
#include "psb_device.h"
#include "psb_drv.h"
#include "psb_intel_reg.h"
#include "psb_reg.h"

static int psb_output_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	psb_intel_lvds_init(dev, &dev_priv->mode_dev);
	psb_intel_sdvo_init(dev, SDVOB);
	return 0;
}

/*
 *	Poulsbo Backlight Interfaces
 */

#define BLC_PWM_PRECISION_FACTOR 100	/* 10000000 */
#define BLC_PWM_FREQ_CALC_CONSTANT 32
#define MHz 1000000

#define PSB_BLC_PWM_PRECISION_FACTOR    10
#define PSB_BLC_MAX_PWM_REG_FREQ        0xFFFE
#define PSB_BLC_MIN_PWM_REG_FREQ        0x2

#define PSB_BACKLIGHT_PWM_POLARITY_BIT_CLEAR (0xFFFE)
#define PSB_BACKLIGHT_PWM_CTL_SHIFT	(16)

static int psb_backlight_setup(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	unsigned long core_clock;
	/* u32 bl_max_freq; */
	/* unsigned long value; */
	u16 bl_max_freq;
	uint32_t value;
	uint32_t blc_pwm_precision_factor;

	/* get bl_max_freq and pol from dev_priv*/
	if (!dev_priv->lvds_bl) {
		dev_err(dev->dev, "Has no valid LVDS backlight info\n");
		return -ENOENT;
	}
	bl_max_freq = dev_priv->lvds_bl->freq;
	blc_pwm_precision_factor = PSB_BLC_PWM_PRECISION_FACTOR;

	core_clock = dev_priv->core_freq;

	value = (core_clock * MHz) / BLC_PWM_FREQ_CALC_CONSTANT;
	value *= blc_pwm_precision_factor;
	value /= bl_max_freq;
	value /= blc_pwm_precision_factor;

	if (value > (unsigned long long)PSB_BLC_MAX_PWM_REG_FREQ ||
		 value < (unsigned long long)PSB_BLC_MIN_PWM_REG_FREQ)
				return -ERANGE;
	else {
		value &= PSB_BACKLIGHT_PWM_POLARITY_BIT_CLEAR;
		REG_WRITE(BLC_PWM_CTL,
			(value << PSB_BACKLIGHT_PWM_CTL_SHIFT) | (value));
	}

	psb_intel_lvds_set_brightness(dev, PSB_MAX_BRIGHTNESS);

	return 0;
}

/*
 *	Provide the Poulsbo specific chip logic and low level methods
 *	for power management
 */

static void psb_init_pm(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);

	u32 gating = PSB_RSGX32(PSB_CR_CLKGATECTL);
	gating &= ~3;	/* Disable 2D clock gating */
	gating |= 1;
	PSB_WSGX32(gating, PSB_CR_CLKGATECTL);
	PSB_RSGX32(PSB_CR_CLKGATECTL);
}

/**
 *	psb_save_display_registers	-	save registers lost on suspend
 *	@dev: our DRM device
 *
 *	Save the state we need in order to be able to restore the interface
 *	upon resume from suspend
 */
static int psb_save_display_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct gma_connector *gma_connector;
	struct drm_crtc *crtc;
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;
	struct psb_state *regs = &dev_priv->regs.psb;

	/* Display arbitration control + watermarks */
	regs->saveDSPARB = PSB_RVDC32(DSPARB);
	regs->saveDSPFW1 = PSB_RVDC32(DSPFW1);
	regs->saveDSPFW2 = PSB_RVDC32(DSPFW2);
	regs->saveDSPFW3 = PSB_RVDC32(DSPFW3);
	regs->saveDSPFW4 = PSB_RVDC32(DSPFW4);
	regs->saveDSPFW5 = PSB_RVDC32(DSPFW5);
	regs->saveDSPFW6 = PSB_RVDC32(DSPFW6);
	regs->saveCHICKENBIT = PSB_RVDC32(DSPCHICKENBIT);

	/* Save crtc and output state */
	drm_modeset_lock_all(dev);
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (drm_helper_crtc_in_use(crtc))
			dev_priv->ops->save_crtc(crtc);
	}

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		gma_connector = to_gma_connector(connector);
		if (gma_connector->save)
			gma_connector->save(connector);
	}
	drm_connector_list_iter_end(&conn_iter);

	drm_modeset_unlock_all(dev);
	return 0;
}

/**
 *	psb_restore_display_registers	-	restore lost register state
 *	@dev: our DRM device
 *
 *	Restore register state that was lost during suspend and resume.
 */
static int psb_restore_display_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct gma_connector *gma_connector;
	struct drm_crtc *crtc;
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;
	struct psb_state *regs = &dev_priv->regs.psb;

	/* Display arbitration + watermarks */
	PSB_WVDC32(regs->saveDSPARB, DSPARB);
	PSB_WVDC32(regs->saveDSPFW1, DSPFW1);
	PSB_WVDC32(regs->saveDSPFW2, DSPFW2);
	PSB_WVDC32(regs->saveDSPFW3, DSPFW3);
	PSB_WVDC32(regs->saveDSPFW4, DSPFW4);
	PSB_WVDC32(regs->saveDSPFW5, DSPFW5);
	PSB_WVDC32(regs->saveDSPFW6, DSPFW6);
	PSB_WVDC32(regs->saveCHICKENBIT, DSPCHICKENBIT);

	/*make sure VGA plane is off. it initializes to on after reset!*/
	PSB_WVDC32(0x80000000, VGACNTRL);

	drm_modeset_lock_all(dev);
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		if (drm_helper_crtc_in_use(crtc))
			dev_priv->ops->restore_crtc(crtc);

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		gma_connector = to_gma_connector(connector);
		if (gma_connector->restore)
			gma_connector->restore(connector);
	}
	drm_connector_list_iter_end(&conn_iter);

	drm_modeset_unlock_all(dev);
	return 0;
}

static int psb_power_down(struct drm_device *dev)
{
	return 0;
}

static int psb_power_up(struct drm_device *dev)
{
	return 0;
}

/* Poulsbo */
static const struct psb_offset psb_regmap[2] = {
	{
		.fp0 = FPA0,
		.fp1 = FPA1,
		.cntr = DSPACNTR,
		.conf = PIPEACONF,
		.src = PIPEASRC,
		.dpll = DPLL_A,
		.htotal = HTOTAL_A,
		.hblank = HBLANK_A,
		.hsync = HSYNC_A,
		.vtotal = VTOTAL_A,
		.vblank = VBLANK_A,
		.vsync = VSYNC_A,
		.stride = DSPASTRIDE,
		.size = DSPASIZE,
		.pos = DSPAPOS,
		.base = DSPABASE,
		.surf = DSPASURF,
		.addr = DSPABASE,
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
		.base = DSPBBASE,
		.surf = DSPBSURF,
		.addr = DSPBBASE,
		.status = PIPEBSTAT,
		.linoff = DSPBLINOFF,
		.tileoff = DSPBTILEOFF,
		.palette = PALETTE_B,
	}
};

static int psb_chip_setup(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	dev_priv->regmap = psb_regmap;
	gma_get_core_freq(dev);
	gma_intel_setup_gmbus(dev);
	psb_intel_opregion_init(dev);
	psb_intel_init_bios(dev);
	return 0;
}

static void psb_chip_teardown(struct drm_device *dev)
{
	gma_intel_teardown_gmbus(dev);
}

const struct psb_ops psb_chip_ops = {
	.name = "Poulsbo",
	.pipes = 2,
	.crtcs = 2,
	.hdmi_mask = (1 << 0),
	.lvds_mask = (1 << 1),
	.sdvo_mask = (1 << 0),
	.cursor_needs_phys = 1,
	.sgx_offset = PSB_SGX_OFFSET,
	.chip_setup = psb_chip_setup,
	.chip_teardown = psb_chip_teardown,

	.crtc_helper = &psb_intel_helper_funcs,
	.clock_funcs = &psb_clock_funcs,

	.output_init = psb_output_init,

	.backlight_init = psb_backlight_setup,
	.backlight_set = psb_intel_lvds_set_brightness,
	.backlight_name = "psb-bl",

	.init_pm = psb_init_pm,
	.save_regs = psb_save_display_registers,
	.restore_regs = psb_restore_display_registers,
	.save_crtc = gma_crtc_save,
	.restore_crtc = gma_crtc_restore,
	.power_down = psb_power_down,
	.power_up = psb_power_up,
};

