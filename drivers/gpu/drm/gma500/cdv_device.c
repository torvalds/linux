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

	drm_mode_create_scaling_mode_property(dev);

	cdv_disable_vga(dev);

	cdv_intel_crt_init(dev, &dev_priv->mode_dev);
	cdv_intel_lvds_init(dev, &dev_priv->mode_dev);

	/* These bits indicate HDMI not SDVO on CDV */
	if (REG_READ(SDVOB) & SDVO_DETECTED)
		cdv_hdmi_init(dev, &dev_priv->mode_dev, SDVOB);
	if (REG_READ(SDVOC) & SDVO_DETECTED)
		cdv_hdmi_init(dev, &dev_priv->mode_dev, SDVOC);
	return 0;
}

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE

/*
 *	Cedartrail Backlght Interfaces
 */

static struct backlight_device *cdv_backlight_device;

static int cdv_backlight_combination_mode(struct drm_device *dev)
{
	return REG_READ(BLC_PWM_CTL2) & PWM_LEGACY_MODE;
}

static int cdv_get_brightness(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(bd);
	u32 val = REG_READ(BLC_PWM_CTL) & BACKLIGHT_DUTY_CYCLE_MASK;

	if (cdv_backlight_combination_mode(dev)) {
		u8 lbpc;

		val &= ~1;
		pci_read_config_byte(dev->pdev, 0xF4, &lbpc);
		val *= lbpc;
	}
	return val;
}

static u32 cdv_get_max_backlight(struct drm_device *dev)
{
	u32 max = REG_READ(BLC_PWM_CTL);

	if (max == 0) {
		DRM_DEBUG_KMS("LVDS Panel PWM value is 0!\n");
		/* i915 does this, I believe which means that we should not
		 * smash PWM control as firmware will take control of it. */
		return 1;
	}

	max >>= 16;
	if (cdv_backlight_combination_mode(dev))
		max *= 0xff;
	return max;
}

static int cdv_set_brightness(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(bd);
	int level = bd->props.brightness;
	u32 blc_pwm_ctl;

	/* Percentage 1-100% being valid */
	if (level < 1)
		level = 1;

	if (cdv_backlight_combination_mode(dev)) {
		u32 max = cdv_get_max_backlight(dev);
		u8 lbpc;

		lbpc = level * 0xfe / max + 1;
		level /= lbpc;

		pci_write_config_byte(dev->pdev, 0xF4, lbpc);
	}

	blc_pwm_ctl = REG_READ(BLC_PWM_CTL) & ~BACKLIGHT_DUTY_CYCLE_MASK;
	REG_WRITE(BLC_PWM_CTL, (blc_pwm_ctl |
				(level << BACKLIGHT_DUTY_CYCLE_SHIFT)));
	return 0;
}

static const struct backlight_ops cdv_ops = {
	.get_brightness = cdv_get_brightness,
	.update_status  = cdv_set_brightness,
};

static int cdv_backlight_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct backlight_properties props;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 100;
	props.type = BACKLIGHT_PLATFORM;

	cdv_backlight_device = backlight_device_register("psb-bl",
					NULL, (void *)dev, &cdv_ops, &props);
	if (IS_ERR(cdv_backlight_device))
		return PTR_ERR(cdv_backlight_device);

	cdv_backlight_device->props.brightness =
			cdv_get_brightness(cdv_backlight_device);
	cdv_backlight_device->props.max_brightness = cdv_get_max_backlight(dev);
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

#define PSB_PM_SSC			0x20
#define PSB_PM_SSS			0x30
#define PSB_PWRGT_GFX_ON		0x02
#define PSB_PWRGT_GFX_OFF		0x01
#define PSB_PWRGT_GFX_D0		0x00
#define PSB_PWRGT_GFX_D3		0x03

static void cdv_init_pm(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 pwr_cnt;
	int i;

	dev_priv->apm_base = CDV_MSG_READ32(PSB_PUNIT_PORT,
							PSB_APMBA) & 0xFFFF;
	dev_priv->ospm_base = CDV_MSG_READ32(PSB_PUNIT_PORT,
							PSB_OSPMBA) & 0xFFFF;

	/* Power status */
	pwr_cnt = inl(dev_priv->apm_base + PSB_APM_CMD);

	/* Enable the GPU */
	pwr_cnt &= ~PSB_PWRGT_GFX_MASK;
	pwr_cnt |= PSB_PWRGT_GFX_ON;
	outl(pwr_cnt, dev_priv->apm_base + PSB_APM_CMD);

	/* Wait for the GPU power */
	for (i = 0; i < 5; i++) {
		u32 pwr_sts = inl(dev_priv->apm_base + PSB_APM_STS);
		if ((pwr_sts & PSB_PWRGT_GFX_MASK) == 0)
			return;
		udelay(10);
	}
	dev_err(dev->dev, "GPU: power management timed out.\n");
}

static void cdv_errata(struct drm_device *dev)
{
	/* Disable bonus launch.
	 *	CPU and GPU competes for memory and display misses updates and
	 *	flickers. Worst with dual core, dual displays.
	 *
	 *	Fixes were done to Win 7 gfx driver to disable a feature called
	 *	Bonus Launch to work around the issue, by degrading
	 *	performance.
	 */
	 CDV_MSG_WRITE32(3, 0x30, 0x08027108);
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
	struct psb_save_area *regs = &dev_priv->regs;
	struct drm_connector *connector;

	dev_info(dev->dev, "Saving GPU registers.\n");

	pci_read_config_byte(dev->pdev, 0xF4, &regs->cdv.saveLBB);

	regs->cdv.saveDSPCLK_GATE_D = REG_READ(DSPCLK_GATE_D);
	regs->cdv.saveRAMCLK_GATE_D = REG_READ(RAMCLK_GATE_D);

	regs->cdv.saveDSPARB = REG_READ(DSPARB);
	regs->cdv.saveDSPFW[0] = REG_READ(DSPFW1);
	regs->cdv.saveDSPFW[1] = REG_READ(DSPFW2);
	regs->cdv.saveDSPFW[2] = REG_READ(DSPFW3);
	regs->cdv.saveDSPFW[3] = REG_READ(DSPFW4);
	regs->cdv.saveDSPFW[4] = REG_READ(DSPFW5);
	regs->cdv.saveDSPFW[5] = REG_READ(DSPFW6);

	regs->cdv.saveADPA = REG_READ(ADPA);

	regs->cdv.savePP_CONTROL = REG_READ(PP_CONTROL);
	regs->cdv.savePFIT_PGM_RATIOS = REG_READ(PFIT_PGM_RATIOS);
	regs->saveBLC_PWM_CTL = REG_READ(BLC_PWM_CTL);
	regs->saveBLC_PWM_CTL2 = REG_READ(BLC_PWM_CTL2);
	regs->cdv.saveLVDS = REG_READ(LVDS);

	regs->cdv.savePFIT_CONTROL = REG_READ(PFIT_CONTROL);

	regs->cdv.savePP_ON_DELAYS = REG_READ(PP_ON_DELAYS);
	regs->cdv.savePP_OFF_DELAYS = REG_READ(PP_OFF_DELAYS);
	regs->cdv.savePP_CYCLE = REG_READ(PP_CYCLE);

	regs->cdv.saveVGACNTRL = REG_READ(VGACNTRL);

	regs->cdv.saveIER = REG_READ(PSB_INT_ENABLE_R);
	regs->cdv.saveIMR = REG_READ(PSB_INT_MASK_R);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		connector->funcs->dpms(connector, DRM_MODE_DPMS_OFF);

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
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_save_area *regs = &dev_priv->regs;
	struct drm_connector *connector;
	u32 temp;

	pci_write_config_byte(dev->pdev, 0xF4, regs->cdv.saveLBB);

	REG_WRITE(DSPCLK_GATE_D, regs->cdv.saveDSPCLK_GATE_D);
	REG_WRITE(RAMCLK_GATE_D, regs->cdv.saveRAMCLK_GATE_D);

	/* BIOS does below anyway */
	REG_WRITE(DPIO_CFG, 0);
	REG_WRITE(DPIO_CFG, DPIO_MODE_SELECT_0 | DPIO_CMN_RESET_N);

	temp = REG_READ(DPLL_A);
	if ((temp & DPLL_SYNCLOCK_ENABLE) == 0) {
		REG_WRITE(DPLL_A, temp | DPLL_SYNCLOCK_ENABLE);
		REG_READ(DPLL_A);
	}

	temp = REG_READ(DPLL_B);
	if ((temp & DPLL_SYNCLOCK_ENABLE) == 0) {
		REG_WRITE(DPLL_B, temp | DPLL_SYNCLOCK_ENABLE);
		REG_READ(DPLL_B);
	}

	udelay(500);

	REG_WRITE(DSPFW1, regs->cdv.saveDSPFW[0]);
	REG_WRITE(DSPFW2, regs->cdv.saveDSPFW[1]);
	REG_WRITE(DSPFW3, regs->cdv.saveDSPFW[2]);
	REG_WRITE(DSPFW4, regs->cdv.saveDSPFW[3]);
	REG_WRITE(DSPFW5, regs->cdv.saveDSPFW[4]);
	REG_WRITE(DSPFW6, regs->cdv.saveDSPFW[5]);

	REG_WRITE(DSPARB, regs->cdv.saveDSPARB);
	REG_WRITE(ADPA, regs->cdv.saveADPA);

	REG_WRITE(BLC_PWM_CTL2, regs->saveBLC_PWM_CTL2);
	REG_WRITE(LVDS, regs->cdv.saveLVDS);
	REG_WRITE(PFIT_CONTROL, regs->cdv.savePFIT_CONTROL);
	REG_WRITE(PFIT_PGM_RATIOS, regs->cdv.savePFIT_PGM_RATIOS);
	REG_WRITE(BLC_PWM_CTL, regs->saveBLC_PWM_CTL);
	REG_WRITE(PP_ON_DELAYS, regs->cdv.savePP_ON_DELAYS);
	REG_WRITE(PP_OFF_DELAYS, regs->cdv.savePP_OFF_DELAYS);
	REG_WRITE(PP_CYCLE, regs->cdv.savePP_CYCLE);
	REG_WRITE(PP_CONTROL, regs->cdv.savePP_CONTROL);

	REG_WRITE(VGACNTRL, regs->cdv.saveVGACNTRL);

	REG_WRITE(PSB_INT_ENABLE_R, regs->cdv.saveIER);
	REG_WRITE(PSB_INT_MASK_R, regs->cdv.saveIMR);

	/* Fix arbitration bug */
	cdv_errata(dev);

	drm_mode_config_reset(dev);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		connector->funcs->dpms(connector, DRM_MODE_DPMS_ON);

	/* Resume the modeset for every activated CRTC */
	drm_helper_resume_force_mode(dev);
	return 0;
}

static int cdv_power_down(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 pwr_cnt, pwr_mask, pwr_sts;
	int tries = 5;

	pwr_cnt = inl(dev_priv->apm_base + PSB_APM_CMD);
	pwr_cnt &= ~PSB_PWRGT_GFX_MASK;
	pwr_cnt |= PSB_PWRGT_GFX_OFF;
	pwr_mask = PSB_PWRGT_GFX_MASK;

	outl(pwr_cnt, dev_priv->apm_base + PSB_APM_CMD);

	while (tries--) {
		pwr_sts = inl(dev_priv->apm_base + PSB_APM_STS);
		if ((pwr_sts & pwr_mask) == PSB_PWRGT_GFX_D3)
			return 0;
		udelay(10);
	}
	return 0;
}

static int cdv_power_up(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 pwr_cnt, pwr_mask, pwr_sts;
	int tries = 5;

	pwr_cnt = inl(dev_priv->apm_base + PSB_APM_CMD);
	pwr_cnt &= ~PSB_PWRGT_GFX_MASK;
	pwr_cnt |= PSB_PWRGT_GFX_ON;
	pwr_mask = PSB_PWRGT_GFX_MASK;

	outl(pwr_cnt, dev_priv->apm_base + PSB_APM_CMD);

	while (tries--) {
		pwr_sts = inl(dev_priv->apm_base + PSB_APM_STS);
		if ((pwr_sts & pwr_mask) == PSB_PWRGT_GFX_D0)
			return 0;
		udelay(10);
	}
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

static void cdv_hotplug_work_func(struct work_struct *work)
{
        struct drm_psb_private *dev_priv = container_of(work, struct drm_psb_private,
							hotplug_work);                 
        struct drm_device *dev = dev_priv->dev;

        /* Just fire off a uevent and let userspace tell us what to do */
        drm_helper_hpd_irq_event(dev);
}                       

/* The core driver has received a hotplug IRQ. We are in IRQ context
   so extract the needed information and kick off queued processing */
   
static int cdv_hotplug_event(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	schedule_work(&dev_priv->hotplug_work);
	REG_WRITE(PORT_HOTPLUG_STAT, REG_READ(PORT_HOTPLUG_STAT));
	return 1;
}

static void cdv_hotplug_enable(struct drm_device *dev, bool on)
{
	if (on) {
		u32 hotplug = REG_READ(PORT_HOTPLUG_EN);
		hotplug |= HDMIB_HOTPLUG_INT_EN | HDMIC_HOTPLUG_INT_EN |
			   HDMID_HOTPLUG_INT_EN | CRT_HOTPLUG_INT_EN;
		REG_WRITE(PORT_HOTPLUG_EN, hotplug);
	}  else {
		REG_WRITE(PORT_HOTPLUG_EN, 0);
		REG_WRITE(PORT_HOTPLUG_STAT, REG_READ(PORT_HOTPLUG_STAT));
	}	
}

/* Cedarview */
static const struct psb_offset cdv_regmap[2] = {
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

static int cdv_chip_setup(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	INIT_WORK(&dev_priv->hotplug_work, cdv_hotplug_work_func);
	dev_priv->regmap = cdv_regmap;
	cdv_get_core_freq(dev);
	psb_intel_opregion_init(dev);
	psb_intel_init_bios(dev);
	cdv_hotplug_enable(dev, false);
	return 0;
}

/* CDV is much like Poulsbo but has MID like SGX offsets and PM */

const struct psb_ops cdv_chip_ops = {
	.name = "GMA3600/3650",
	.accel_2d = 0,
	.pipes = 2,
	.crtcs = 2,
	.hdmi_mask = (1 << 0) | (1 << 1),
	.lvds_mask = (1 << 1),
	.sgx_offset = MRST_SGX_OFFSET,
	.chip_setup = cdv_chip_setup,
	.errata = cdv_errata,

	.crtc_helper = &cdv_intel_helper_funcs,
	.crtc_funcs = &cdv_intel_crtc_funcs,

	.output_init = cdv_output_init,
	.hotplug = cdv_hotplug_event,
	.hotplug_enable = cdv_hotplug_enable,

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	.backlight_init = cdv_backlight_init,
#endif

	.init_pm = cdv_init_pm,
	.save_regs = cdv_save_display_registers,
	.restore_regs = cdv_restore_display_registers,
	.power_down = cdv_power_down,
	.power_up = cdv_power_up,
};
