// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************************
 * Copyright (c) 2011, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#include <linux/delay.h>

#include <drm/drm.h>
#include <drm/drm_crtc_helper.h>

#include "cdv_device.h"
#include "gma_device.h"
#include "intel_bios.h"
#include "psb_drv.h"
#include "psb_intel_reg.h"
#include "psb_reg.h"

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
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);

	drm_mode_create_scaling_mode_property(dev);

	cdv_disable_vga(dev);

	cdv_intel_crt_init(dev, &dev_priv->mode_dev);
	cdv_intel_lvds_init(dev, &dev_priv->mode_dev);

	/* These bits indicate HDMI not SDVO on CDV */
	if (REG_READ(SDVOB) & SDVO_DETECTED) {
		cdv_hdmi_init(dev, &dev_priv->mode_dev, SDVOB);
		if (REG_READ(DP_B) & DP_DETECTED)
			cdv_intel_dp_init(dev, &dev_priv->mode_dev, DP_B);
	}

	if (REG_READ(SDVOC) & SDVO_DETECTED) {
		cdv_hdmi_init(dev, &dev_priv->mode_dev, SDVOC);
		if (REG_READ(DP_C) & DP_DETECTED)
			cdv_intel_dp_init(dev, &dev_priv->mode_dev, DP_C);
	}
	return 0;
}

/*
 *	Cedartrail Backlght Interfaces
 */

static int cdv_backlight_combination_mode(struct drm_device *dev)
{
	return REG_READ(BLC_PWM_CTL2) & PWM_LEGACY_MODE;
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

static int cdv_get_brightness(struct drm_device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	u32 val = REG_READ(BLC_PWM_CTL) & BACKLIGHT_DUTY_CYCLE_MASK;

	if (cdv_backlight_combination_mode(dev)) {
		u8 lbpc;

		val &= ~1;
		pci_read_config_byte(pdev, 0xF4, &lbpc);
		val *= lbpc;
	}
	return (val * 100)/cdv_get_max_backlight(dev);
}

static void cdv_set_brightness(struct drm_device *dev, int level)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	u32 blc_pwm_ctl;

	level *= cdv_get_max_backlight(dev);
	level /= 100;

	if (cdv_backlight_combination_mode(dev)) {
		u32 max = cdv_get_max_backlight(dev);
		u8 lbpc;

		lbpc = level * 0xfe / max + 1;
		level /= lbpc;

		pci_write_config_byte(pdev, 0xF4, lbpc);
	}

	blc_pwm_ctl = REG_READ(BLC_PWM_CTL) & ~BACKLIGHT_DUTY_CYCLE_MASK;
	REG_WRITE(BLC_PWM_CTL, (blc_pwm_ctl |
				(level << BACKLIGHT_DUTY_CYCLE_SHIFT)));
}

static int cdv_backlight_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);

	dev_priv->backlight_level = cdv_get_brightness(dev);
	cdv_set_brightness(dev, dev_priv->backlight_level);

	return 0;
}

/*
 *	Provide the Cedarview specific chip logic and low level methods
 *	for power management
 *
 *	FIXME: we need to implement the apm/ospm base management bits
 *	for this and the MID devices.
 */

static inline u32 CDV_MSG_READ32(int domain, uint port, uint offset)
{
	int mcr = (0x10<<24) | (port << 16) | (offset << 8);
	uint32_t ret_val = 0;
	struct pci_dev *pci_root = pci_get_domain_bus_and_slot(domain, 0, 0);
	pci_write_config_dword(pci_root, 0xD0, mcr);
	pci_read_config_dword(pci_root, 0xD4, &ret_val);
	pci_dev_put(pci_root);
	return ret_val;
}

static inline void CDV_MSG_WRITE32(int domain, uint port, uint offset,
				   u32 value)
{
	int mcr = (0x11<<24) | (port << 16) | (offset << 8) | 0xF0;
	struct pci_dev *pci_root = pci_get_domain_bus_and_slot(domain, 0, 0);
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
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	u32 pwr_cnt;
	int domain = pci_domain_nr(pdev->bus);
	int i;

	dev_priv->apm_base = CDV_MSG_READ32(domain, PSB_PUNIT_PORT,
							PSB_APMBA) & 0xFFFF;
	dev_priv->ospm_base = CDV_MSG_READ32(domain, PSB_PUNIT_PORT,
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
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	/* Disable bonus launch.
	 *	CPU and GPU competes for memory and display misses updates and
	 *	flickers. Worst with dual core, dual displays.
	 *
	 *	Fixes were done to Win 7 gfx driver to disable a feature called
	 *	Bonus Launch to work around the issue, by degrading
	 *	performance.
	 */
	CDV_MSG_WRITE32(pci_domain_nr(pdev->bus), 3, 0x30, 0x08027108);
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
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct psb_save_area *regs = &dev_priv->regs;
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;

	dev_dbg(dev->dev, "Saving GPU registers.\n");

	pci_read_config_byte(pdev, 0xF4, &regs->cdv.saveLBB);

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

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter)
		connector->funcs->dpms(connector, DRM_MODE_DPMS_OFF);
	drm_connector_list_iter_end(&conn_iter);

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
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct psb_save_area *regs = &dev_priv->regs;
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;
	u32 temp;

	pci_write_config_byte(pdev, 0xF4, regs->cdv.saveLBB);

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

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter)
		connector->funcs->dpms(connector, DRM_MODE_DPMS_ON);
	drm_connector_list_iter_end(&conn_iter);

	/* Resume the modeset for every activated CRTC */
	drm_helper_resume_force_mode(dev);
	return 0;
}

static int cdv_power_down(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
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
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
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

static void cdv_hotplug_work_func(struct work_struct *work)
{
        struct drm_psb_private *dev_priv = container_of(work, struct drm_psb_private,
							hotplug_work);
	struct drm_device *dev = &dev_priv->dev;

        /* Just fire off a uevent and let userspace tell us what to do */
        drm_helper_hpd_irq_event(dev);
}

/* The core driver has received a hotplug IRQ. We are in IRQ context
   so extract the needed information and kick off queued processing */

static int cdv_hotplug_event(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
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

static const char *force_audio_names[] = {
	"off",
	"auto",
	"on",
};

void cdv_intel_attach_force_audio_property(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct drm_property *prop;
	int i;

	prop = dev_priv->force_audio_property;
	if (prop == NULL) {
		prop = drm_property_create(dev, DRM_MODE_PROP_ENUM,
					   "audio",
					   ARRAY_SIZE(force_audio_names));
		if (prop == NULL)
			return;

		for (i = 0; i < ARRAY_SIZE(force_audio_names); i++)
			drm_property_add_enum(prop, i-1, force_audio_names[i]);

		dev_priv->force_audio_property = prop;
	}
	drm_object_attach_property(&connector->base, prop, 0);
}


static const char *broadcast_rgb_names[] = {
	"Full",
	"Limited 16:235",
};

void cdv_intel_attach_broadcast_rgb_property(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct drm_property *prop;
	int i;

	prop = dev_priv->broadcast_rgb_property;
	if (prop == NULL) {
		prop = drm_property_create(dev, DRM_MODE_PROP_ENUM,
					   "Broadcast RGB",
					   ARRAY_SIZE(broadcast_rgb_names));
		if (prop == NULL)
			return;

		for (i = 0; i < ARRAY_SIZE(broadcast_rgb_names); i++)
			drm_property_add_enum(prop, i, broadcast_rgb_names[i]);

		dev_priv->broadcast_rgb_property = prop;
	}

	drm_object_attach_property(&connector->base, prop, 0);
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
		.dpll_md = DPLL_A_MD,
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
		.dpll_md = DPLL_B_MD,
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
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	INIT_WORK(&dev_priv->hotplug_work, cdv_hotplug_work_func);

	dev_priv->use_msi = true;
	dev_priv->regmap = cdv_regmap;
	gma_get_core_freq(dev);
	psb_intel_opregion_init(dev);
	psb_intel_init_bios(dev);
	cdv_hotplug_enable(dev, false);
	return 0;
}

/* CDV is much like Poulsbo but has MID like SGX offsets and PM */

const struct psb_ops cdv_chip_ops = {
	.name = "GMA3600/3650",
	.pipes = 2,
	.crtcs = 2,
	.hdmi_mask = (1 << 0) | (1 << 1),
	.lvds_mask = (1 << 1),
	.sdvo_mask = (1 << 0),
	.cursor_needs_phys = 0,
	.sgx_offset = MRST_SGX_OFFSET,
	.chip_setup = cdv_chip_setup,
	.errata = cdv_errata,

	.crtc_helper = &cdv_intel_helper_funcs,
	.clock_funcs = &cdv_clock_funcs,

	.output_init = cdv_output_init,
	.hotplug = cdv_hotplug_event,
	.hotplug_enable = cdv_hotplug_enable,

	.backlight_init = cdv_backlight_init,
	.backlight_get = cdv_get_brightness,
	.backlight_set = cdv_set_brightness,
	.backlight_name = "psb-bl",

	.init_pm = cdv_init_pm,
	.save_regs = cdv_save_display_registers,
	.restore_regs = cdv_restore_display_registers,
	.save_crtc = gma_crtc_save,
	.restore_crtc = gma_crtc_restore,
	.power_down = cdv_power_down,
	.power_up = cdv_power_up,
	.update_wm = cdv_update_wm,
	.disable_sr = cdv_disable_sr,
};
