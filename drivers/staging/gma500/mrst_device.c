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
#include "psb_drm.h"
#include "psb_drv.h"
#include "psb_reg.h"
#include "psb_intel_reg.h"
#include <asm/intel_scu_ipc.h>
#include "mid_bios.h"

/* IPC message and command defines used to enable/disable mipi panel voltages */
#define IPC_MSG_PANEL_ON_OFF    0xE9
#define IPC_CMD_PANEL_ON        1
#define IPC_CMD_PANEL_OFF       0

static int mrst_output_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	if (dev_priv->iLVDS_enable) {
		mrst_lvds_init(dev, &dev_priv->mode_dev);
		return 0;
	}
	dev_err(dev->dev, "DSI is not supported\n");
	return -ENODEV;
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

static struct backlight_device *mrst_backlight_device;
static int mrst_brightness;

static int mrst_set_brightness(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(mrst_backlight_device);
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
	mrst_brightness = level;
	return 0;
}

static int mrst_get_brightness(struct backlight_device *bd)
{
	/* return locally cached var instead of HW read (due to DPST etc.) */
	/* FIXME: ideally return actual value in case firmware fiddled with
	   it */
	return mrst_brightness;
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

	if (gma_power_begin(dev, false)) {
		if (value > (unsigned long long)MRST_BLC_MAX_PWM_REG_FREQ)
				return -ERANGE;
		else {
			REG_WRITE(BLC_PWM_CTL2,
					(0x80000000 | REG_READ(BLC_PWM_CTL2)));
			REG_WRITE(BLC_PWM_CTL, value | (value << 16));
		}
		gma_power_end(dev);
	}
	return 0;
}

static const struct backlight_ops mrst_ops = {
	.get_brightness = mrst_get_brightness,
	.update_status  = mrst_set_brightness,
};

int mrst_backlight_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	int ret;
	struct backlight_properties props;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 100;
	props.type = BACKLIGHT_PLATFORM;

	mrst_backlight_device = backlight_device_register("mrst-bl",
					NULL, (void *)dev, &mrst_ops, &props);
					
	if (IS_ERR(mrst_backlight_device))
		return PTR_ERR(mrst_backlight_device);

	ret = device_backlight_init(dev);
	if (ret < 0) {
		backlight_device_unregister(mrst_backlight_device);
		return ret;
	}
	mrst_backlight_device->props.brightness = 100;
	mrst_backlight_device->props.max_brightness = 100;
	backlight_update_status(mrst_backlight_device);
	dev_priv->backlight_device = mrst_backlight_device;
	return 0;
}

#endif

/*
 *	Provide the Moorestown specific chip logic and low level methods
 *	for power management
 */

static void mrst_init_pm(struct drm_device *dev)
{
}

/**
 *	mrst_save_display_registers	-	save registers lost on suspend
 *	@dev: our DRM device
 *
 *	Save the state we need in order to be able to restore the interface
 *	upon resume from suspend
 */
static int mrst_save_display_registers(struct drm_device *dev)
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
 *	mrst_restore_display_registers	-	restore lost register state
 *	@dev: our DRM device
 *
 *	Restore register state that was lost during suspend and resume.
 */
static int mrst_restore_display_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	int pp_stat;

	if (!dev_priv->iLVDS_enable) {
#ifdef CONFIG_X86_MRST
		intel_scu_ipc_simple_command(IPC_MSG_PANEL_ON_OFF,
							IPC_CMD_PANEL_ON);
		/* FIXME: can we avoid this delay ? */
		msleep(2000); /* wait 2 seconds */
#endif
	}

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
#ifdef CONFIG_X86_MRST	/* FIXME: kill define once modular */
		intel_scu_ipc_simple_command(IPC_MSG_PANEL_ON_OFF,
							IPC_CMD_PANEL_OFF);
#endif
	}
	return 0;
}

/**
 *	mrst_power_down	-	power down the display island
 *	@dev: our DRM device
 *
 *	Power down the display interface of our device
 */
static int mrst_power_down(struct drm_device *dev)
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
 * mrst_power_up
 *
 * Restore power to the specified island(s) (powergating)
 */
static int mrst_power_up(struct drm_device *dev)
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

const struct psb_ops mrst_chip_ops = {
	.name = "Moorestown",
	.accel_2d = 1,
	.pipes = 1,
	.sgx_offset = MRST_SGX_OFFSET,

	.chip_setup = mid_chip_setup,
	.crtc_helper = &mrst_helper_funcs,
	.crtc_funcs = &psb_intel_crtc_funcs,

	.output_init = mrst_output_init,

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	.backlight_init = mrst_backlight_init,
#endif
	
	.init_pm = mrst_init_pm,
	.save_regs = mrst_save_display_registers,
	.restore_regs = mrst_restore_display_registers,
	.power_down = mrst_power_down,
	.power_up = mrst_power_up,
};

