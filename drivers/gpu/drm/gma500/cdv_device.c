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
#include "gma_drm.h"
#include "psb_drv.h"
#include "psb_reg.h"
#include "psb_intel_reg.h"
#include "intel_bios.h"
#include "cdv_device.h"

#define VGA_SR_INDEX		0x3c4
#define VGA_SR_DATA		0x3c5

static void cdv_disable_vga(struct drm_device *dev)
{
	u8 sr1;
	u32 vga_reg;

	vga_reg = VGACNTRL;

	outb(1, VGA_SR_INDEX);
	sr1 = inb(VGA_SR_DATA);
	outb(sr1 | 1<<5, VGA_SR_DATA);
	udelay(300);

	REG_WRITE(vga_reg, VGA_DISP_DISABLE);
	REG_READ(vga_reg);
}

static int cdv_output_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	cdv_disable_vga(dev);

	cdv_intel_crt_init(dev, &dev_priv->mode_dev);
	cdv_intel_lvds_init(dev, &dev_priv->mode_dev);

	/* These bits indicate HDMI not SDVO on CDV, but we don't yet support
	   the HDMI interface */
	if (REG_READ(SDVOB) & SDVO_DETECTED)
		cdv_hdmi_init(dev, &dev_priv->mode_dev, SDVOB);
	if (REG_READ(SDVOC) & SDVO_DETECTED)
		cdv_hdmi_init(dev, &dev_priv->mode_dev, SDVOC);
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
 *	Provide the Cedarview specific chip logic and low level methods
 *	for power management
 *
 *	FIXME: we need to implement the apm/ospm base management bits
 *	for this and the MID devices.
 */

static inline u32 CDV_MSG_READ32(uint port, uint offset)
{
	int mcr = (0x10<<24) | (port << 16) | (offset << 8);
	uint32_t ret_val = 0;
	struct pci_dev *pci_root = pci_get_bus_and_slot(0, 0);
	pci_write_config_dword(pci_root, 0xD0, mcr);
	pci_read_config_dword(pci_root, 0xD4, &ret_val);
	pci_dev_put(pci_root);
	return ret_val;
}

static inline void CDV_MSG_WRITE32(uint port, uint offset, u32 value)
{
	int mcr = (0x11<<24) | (port << 16) | (offset << 8) | 0xF0;
	struct pci_dev *pci_root = pci_get_bus_and_slot(0, 0);
	pci_write_config_dword(pci_root, 0xD4, value);
	pci_write_config_dword(pci_root, 0xD0, mcr);
	pci_dev_put(pci_root);
}

#define PSB_APM_CMD			0x0
#define PSB_APM_STS			0x04
#define PSB_PM_SSC			0x20
#define PSB_PM_SSS			0x30
#define PSB_PWRGT_GFX_MASK		0x3
#define CDV_PWRGT_DISPLAY_CNTR		0x000fc00c
#define CDV_PWRGT_DISPLAY_STS		0x000fc00c

static void cdv_init_pm(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 pwr_cnt;
	int i;

	dev_priv->apm_base = CDV_MSG_READ32(PSB_PUNIT_PORT,
							PSB_APMBA) & 0xFFFF;
	dev_priv->ospm_base = CDV_MSG_READ32(PSB_PUNIT_PORT,
							PSB_OSPMBA) & 0xFFFF;

	/* Force power on for now */
	pwr_cnt = inl(dev_priv->apm_base + PSB_APM_CMD);
	pwr_cnt &= ~PSB_PWRGT_GFX_MASK;

	outl(pwr_cnt, dev_priv->apm_base + PSB_APM_CMD);
	for (i = 0; i < 5; i++) {
		u32 pwr_sts = inl(dev_priv->apm_base + PSB_APM_STS);
		if ((pwr_sts & PSB_PWRGT_GFX_MASK) == 0)
			break;
		udelay(10);
	}
	pwr_cnt = inl(dev_priv->ospm_base + PSB_PM_SSC);
	pwr_cnt &= ~CDV_PWRGT_DISPLAY_CNTR;
	outl(pwr_cnt, dev_priv->ospm_base + PSB_PM_SSC);
	for (i = 0; i < 5; i++) {
		u32 pwr_sts = inl(dev_priv->ospm_base + PSB_PM_SSS);
		if ((pwr_sts & CDV_PWRGT_DISPLAY_STS) == 0)
			break;
		udelay(10);
	}
}

/**
 *	cdv_save_display_registers	-	save registers lost on suspend
 *	@dev: our DRM device
 *
 *	Save the state we need in order to be able to restore the interface
 *	upon resume from suspend
 *
 *	FIXME: review
 */
static int cdv_save_display_registers(struct drm_device *dev)
{
	return 0;
}

/**
 *	cdv_restore_display_registers	-	restore lost register state
 *	@dev: our DRM device
 *
 *	Restore register state that was lost during suspend and resume.
 *
 *	FIXME: review
 */
static int cdv_restore_display_registers(struct drm_device *dev)
{
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
	gma_intel_opregion_init(dev);
	psb_intel_init_bios(dev);
	REG_WRITE(PORT_HOTPLUG_EN, 0);
	REG_WRITE(PORT_HOTPLUG_STAT, REG_READ(PORT_HOTPLUG_STAT));
	return 0;
}

/* CDV is much like Poulsbo but has MID like SGX offsets and PM */

const struct psb_ops cdv_chip_ops = {
	.name = "GMA3600/3650",
	.accel_2d = 0,
	.pipes = 2,
	.crtcs = 2,
	.sgx_offset = MRST_SGX_OFFSET,
	.chip_setup = cdv_chip_setup,

	.crtc_helper = &cdv_intel_helper_funcs,
	.crtc_funcs = &cdv_intel_crtc_funcs,

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
