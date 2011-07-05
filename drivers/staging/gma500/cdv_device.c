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
#include "psb_intel_bios.h"


static int cdv_output_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	psb_intel_lvds_init(dev, &dev_priv->mode_dev);
	psb_intel_sdvo_init(dev, SDVOB);
	return 0;
}

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE

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

static int cdv_brightness;
static struct backlight_device *cdv_backlight_device;

static int cdv_get_brightness(struct backlight_device *bd)
{
	/* return locally cached var instead of HW read (due to DPST etc.) */
	/* FIXME: ideally return actual value in case firmware fiddled with
	   it */
	return cdv_brightness;
}


static int cdv_backlight_setup(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
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
		/* FIXME */
	}
	return 0;
}

static int cdv_set_brightness(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(cdv_backlight_device);
	int level = bd->props.brightness;

	/* Percentage 1-100% being valid */
	if (level < 1)
		level = 1;

	/*cdv_intel_lvds_set_brightness(dev, level); FIXME */
	cdv_brightness = level;
	return 0;
}

static const struct backlight_ops cdv_ops = {
	.get_brightness = cdv_get_brightness,
	.update_status  = cdv_set_brightness,
};

static int cdv_backlight_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	int ret;
	struct backlight_properties props;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 100;
	props.type = BACKLIGHT_PLATFORM;

	cdv_backlight_device = backlight_device_register("psb-bl",
					NULL, (void *)dev, &cdv_ops, &props);
	if (IS_ERR(cdv_backlight_device))
		return PTR_ERR(cdv_backlight_device);

	ret = cdv_backlight_setup(dev);
	if (ret < 0) {
		backlight_device_unregister(cdv_backlight_device);
		cdv_backlight_device = NULL;
		return ret;
	}
	cdv_backlight_device->props.brightness = 100;
	cdv_backlight_device->props.max_brightness = 100;
	backlight_update_status(cdv_backlight_device);
	dev_priv->backlight_device = cdv_backlight_device;
	return 0;
}

#endif

/*
 *	Provide the Poulsbo specific chip logic and low level methods
 *	for power management
 */

static void cdv_init_pm(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	u32 gating = PSB_RSGX32(PSB_CR_CLKGATECTL);
	gating &= ~3;	/* Disable 2D clock gating */
	gating |= 1;
	PSB_WSGX32(gating, PSB_CR_CLKGATECTL);
	PSB_RSGX32(PSB_CR_CLKGATECTL);
}

/**
 *	cdv_save_display_registers	-	save registers lost on suspend
 *	@dev: our DRM device
 *
 *	Save the state we need in order to be able to restore the interface
 *	upon resume from suspend
 */
static int cdv_save_display_registers(struct drm_device *dev)
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
 *	cdv_restore_display_registers	-	restore lost register state
 *	@dev: our DRM device
 *
 *	Restore register state that was lost during suspend and resume.
 */
static int cdv_restore_display_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	int pp_stat;

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
	}
	return 0;
}

static int cdv_power_down(struct drm_device *dev)
{
	return 0;
}

static int cdv_power_up(struct drm_device *dev)
{
	return 0;
}

/* FIXME ? - shared with Poulsbo */
static void cdv_get_core_freq(struct drm_device *dev)
{
	uint32_t clock;
	struct pci_dev *pci_root = pci_get_bus_and_slot(0, 0);
	struct drm_psb_private *dev_priv = dev->dev_private;

	/*pci_write_config_dword(pci_root, 0xD4, 0x00C32004);*/
	/*pci_write_config_dword(pci_root, 0xD0, 0xE0033000);*/

	pci_write_config_dword(pci_root, 0xD0, 0xD0050300);
	pci_read_config_dword(pci_root, 0xD4, &clock);
	pci_dev_put(pci_root);

	switch (clock & 0x07) {
	case 0:
		dev_priv->core_freq = 100;
		break;
	case 1:
		dev_priv->core_freq = 133;
		break;
	case 2:
		dev_priv->core_freq = 150;
		break;
	case 3:
		dev_priv->core_freq = 178;
		break;
	case 4:
		dev_priv->core_freq = 200;
		break;
	case 5:
	case 6:
	case 7:
		dev_priv->core_freq = 266;
	default:
		dev_priv->core_freq = 0;
	}
}

static int cdv_chip_setup(struct drm_device *dev)
{
	cdv_get_core_freq(dev);
	psb_intel_opregion_init(dev);
	psb_intel_init_bios(dev);
	return 0;
}

/* CDV is much like Poulsbo but has MID like SGX offsets */

const struct psb_ops cdv_chip_ops = {
	.name = "Cedartrail",
	.accel_2d = 0,
	.pipes = 2,
	.sgx_offset = MRST_SGX_OFFSET,
	.chip_setup = cdv_chip_setup,

	.crtc_helper = &psb_intel_helper_funcs,
	.crtc_funcs = &psb_intel_crtc_funcs,

	.output_init = cdv_output_init,

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	.backlight_init = cdv_backlight_init,
#endif

	.init_pm = cdv_init_pm,
	.save_regs = cdv_save_display_registers,
	.restore_regs = cdv_restore_display_registers,
	.power_down = cdv_power_down,
	.power_up = cdv_power_up,	
};

