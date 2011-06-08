/*
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 */
#include "drmP.h"
#include "drm_crtc_helper.h"
#include "radeon_drm.h"
#include "radeon.h"
#include "atom.h"
#include <linux/backlight.h>
#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif

static void radeon_legacy_encoder_disable(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_encoder_helper_funcs *encoder_funcs;

	encoder_funcs = encoder->helper_private;
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_OFF);
	radeon_encoder->active_device = 0;
}

static void radeon_legacy_lvds_update(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t lvds_gen_cntl, lvds_pll_cntl, pixclks_cntl, disp_pwr_man;
	int panel_pwr_delay = 2000;
	bool is_mac = false;
	uint8_t backlight_level;
	DRM_DEBUG_KMS("\n");

	lvds_gen_cntl = RREG32(RADEON_LVDS_GEN_CNTL);
	backlight_level = (lvds_gen_cntl >> RADEON_LVDS_BL_MOD_LEVEL_SHIFT) & 0xff;

	if (radeon_encoder->enc_priv) {
		if (rdev->is_atom_bios) {
			struct radeon_encoder_atom_dig *lvds = radeon_encoder->enc_priv;
			panel_pwr_delay = lvds->panel_pwr_delay;
			if (lvds->bl_dev)
				backlight_level = lvds->backlight_level;
		} else {
			struct radeon_encoder_lvds *lvds = radeon_encoder->enc_priv;
			panel_pwr_delay = lvds->panel_pwr_delay;
			if (lvds->bl_dev)
				backlight_level = lvds->backlight_level;
		}
	}

	/* macs (and possibly some x86 oem systems?) wire up LVDS strangely
	 * Taken from radeonfb.
	 */
	if ((rdev->mode_info.connector_table == CT_IBOOK) ||
	    (rdev->mode_info.connector_table == CT_POWERBOOK_EXTERNAL) ||
	    (rdev->mode_info.connector_table == CT_POWERBOOK_INTERNAL) ||
	    (rdev->mode_info.connector_table == CT_POWERBOOK_VGA))
		is_mac = true;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		disp_pwr_man = RREG32(RADEON_DISP_PWR_MAN);
		disp_pwr_man |= RADEON_AUTO_PWRUP_EN;
		WREG32(RADEON_DISP_PWR_MAN, disp_pwr_man);
		lvds_pll_cntl = RREG32(RADEON_LVDS_PLL_CNTL);
		lvds_pll_cntl |= RADEON_LVDS_PLL_EN;
		WREG32(RADEON_LVDS_PLL_CNTL, lvds_pll_cntl);
		udelay(1000);

		lvds_pll_cntl = RREG32(RADEON_LVDS_PLL_CNTL);
		lvds_pll_cntl &= ~RADEON_LVDS_PLL_RESET;
		WREG32(RADEON_LVDS_PLL_CNTL, lvds_pll_cntl);

		lvds_gen_cntl &= ~(RADEON_LVDS_DISPLAY_DIS |
				   RADEON_LVDS_BL_MOD_LEVEL_MASK);
		lvds_gen_cntl |= (RADEON_LVDS_ON | RADEON_LVDS_EN |
				  RADEON_LVDS_DIGON | RADEON_LVDS_BLON |
				  (backlight_level << RADEON_LVDS_BL_MOD_LEVEL_SHIFT));
		if (is_mac)
			lvds_gen_cntl |= RADEON_LVDS_BL_MOD_EN;
		udelay(panel_pwr_delay * 1000);
		WREG32(RADEON_LVDS_GEN_CNTL, lvds_gen_cntl);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		pixclks_cntl = RREG32_PLL(RADEON_PIXCLKS_CNTL);
		WREG32_PLL_P(RADEON_PIXCLKS_CNTL, 0, ~RADEON_PIXCLK_LVDS_ALWAYS_ONb);
		lvds_gen_cntl |= RADEON_LVDS_DISPLAY_DIS;
		if (is_mac) {
			lvds_gen_cntl &= ~RADEON_LVDS_BL_MOD_EN;
			WREG32(RADEON_LVDS_GEN_CNTL, lvds_gen_cntl);
			lvds_gen_cntl &= ~(RADEON_LVDS_ON | RADEON_LVDS_EN);
		} else {
			WREG32(RADEON_LVDS_GEN_CNTL, lvds_gen_cntl);
			lvds_gen_cntl &= ~(RADEON_LVDS_ON | RADEON_LVDS_BLON | RADEON_LVDS_EN | RADEON_LVDS_DIGON);
		}
		udelay(panel_pwr_delay * 1000);
		WREG32(RADEON_LVDS_GEN_CNTL, lvds_gen_cntl);
		WREG32_PLL(RADEON_PIXCLKS_CNTL, pixclks_cntl);
		udelay(panel_pwr_delay * 1000);
		break;
	}

	if (rdev->is_atom_bios)
		radeon_atombios_encoder_dpms_scratch_regs(encoder, (mode == DRM_MODE_DPMS_ON) ? true : false);
	else
		radeon_combios_encoder_dpms_scratch_regs(encoder, (mode == DRM_MODE_DPMS_ON) ? true : false);

}

static void radeon_legacy_lvds_dpms(struct drm_encoder *encoder, int mode)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	DRM_DEBUG("\n");

	if (radeon_encoder->enc_priv) {
		if (rdev->is_atom_bios) {
			struct radeon_encoder_atom_dig *lvds = radeon_encoder->enc_priv;
			lvds->dpms_mode = mode;
		} else {
			struct radeon_encoder_lvds *lvds = radeon_encoder->enc_priv;
			lvds->dpms_mode = mode;
		}
	}

	radeon_legacy_lvds_update(encoder, mode);
}

static void radeon_legacy_lvds_prepare(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;

	if (rdev->is_atom_bios)
		radeon_atom_output_lock(encoder, true);
	else
		radeon_combios_output_lock(encoder, true);
	radeon_legacy_lvds_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void radeon_legacy_lvds_commit(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;

	radeon_legacy_lvds_dpms(encoder, DRM_MODE_DPMS_ON);
	if (rdev->is_atom_bios)
		radeon_atom_output_lock(encoder, false);
	else
		radeon_combios_output_lock(encoder, false);
}

static void radeon_legacy_lvds_mode_set(struct drm_encoder *encoder,
					struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t lvds_pll_cntl, lvds_gen_cntl, lvds_ss_gen_cntl;

	DRM_DEBUG_KMS("\n");

	lvds_pll_cntl = RREG32(RADEON_LVDS_PLL_CNTL);
	lvds_pll_cntl &= ~RADEON_LVDS_PLL_EN;

	lvds_ss_gen_cntl = RREG32(RADEON_LVDS_SS_GEN_CNTL);
	if (rdev->is_atom_bios) {
		/* LVDS_GEN_CNTL parameters are computed in LVDSEncoderControl
		 * need to call that on resume to set up the reg properly.
		 */
		radeon_encoder->pixel_clock = adjusted_mode->clock;
		atombios_digital_setup(encoder, PANEL_ENCODER_ACTION_ENABLE);
		lvds_gen_cntl = RREG32(RADEON_LVDS_GEN_CNTL);
	} else {
		struct radeon_encoder_lvds *lvds = (struct radeon_encoder_lvds *)radeon_encoder->enc_priv;
		if (lvds) {
			DRM_DEBUG_KMS("bios LVDS_GEN_CNTL: 0x%x\n", lvds->lvds_gen_cntl);
			lvds_gen_cntl = lvds->lvds_gen_cntl;
			lvds_ss_gen_cntl &= ~((0xf << RADEON_LVDS_PWRSEQ_DELAY1_SHIFT) |
					      (0xf << RADEON_LVDS_PWRSEQ_DELAY2_SHIFT));
			lvds_ss_gen_cntl |= ((lvds->panel_digon_delay << RADEON_LVDS_PWRSEQ_DELAY1_SHIFT) |
					     (lvds->panel_blon_delay << RADEON_LVDS_PWRSEQ_DELAY2_SHIFT));
		} else
			lvds_gen_cntl = RREG32(RADEON_LVDS_GEN_CNTL);
	}
	lvds_gen_cntl |= RADEON_LVDS_DISPLAY_DIS;
	lvds_gen_cntl &= ~(RADEON_LVDS_ON |
			   RADEON_LVDS_BLON |
			   RADEON_LVDS_EN |
			   RADEON_LVDS_RST_FM);

	if (ASIC_IS_R300(rdev))
		lvds_pll_cntl &= ~(R300_LVDS_SRC_SEL_MASK);

	if (radeon_crtc->crtc_id == 0) {
		if (ASIC_IS_R300(rdev)) {
			if (radeon_encoder->rmx_type != RMX_OFF)
				lvds_pll_cntl |= R300_LVDS_SRC_SEL_RMX;
		} else
			lvds_gen_cntl &= ~RADEON_LVDS_SEL_CRTC2;
	} else {
		if (ASIC_IS_R300(rdev))
			lvds_pll_cntl |= R300_LVDS_SRC_SEL_CRTC2;
		else
			lvds_gen_cntl |= RADEON_LVDS_SEL_CRTC2;
	}

	WREG32(RADEON_LVDS_GEN_CNTL, lvds_gen_cntl);
	WREG32(RADEON_LVDS_PLL_CNTL, lvds_pll_cntl);
	WREG32(RADEON_LVDS_SS_GEN_CNTL, lvds_ss_gen_cntl);

	if (rdev->family == CHIP_RV410)
		WREG32(RADEON_CLOCK_CNTL_INDEX, 0);

	if (rdev->is_atom_bios)
		radeon_atombios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);
	else
		radeon_combios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);
}

static bool radeon_legacy_mode_fixup(struct drm_encoder *encoder,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);

	/* set the active encoder to connector routing */
	radeon_encoder_set_active_device(encoder);
	drm_mode_set_crtcinfo(adjusted_mode, 0);

	/* get the native mode for LVDS */
	if (radeon_encoder->active_device & (ATOM_DEVICE_LCD_SUPPORT))
		radeon_panel_mode_fixup(encoder, adjusted_mode);

	return true;
}

static const struct drm_encoder_helper_funcs radeon_legacy_lvds_helper_funcs = {
	.dpms = radeon_legacy_lvds_dpms,
	.mode_fixup = radeon_legacy_mode_fixup,
	.prepare = radeon_legacy_lvds_prepare,
	.mode_set = radeon_legacy_lvds_mode_set,
	.commit = radeon_legacy_lvds_commit,
	.disable = radeon_legacy_encoder_disable,
};

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) || defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

#define MAX_RADEON_LEVEL 0xFF

struct radeon_backlight_privdata {
	struct radeon_encoder *encoder;
	uint8_t negative;
};

static uint8_t radeon_legacy_lvds_level(struct backlight_device *bd)
{
	struct radeon_backlight_privdata *pdata = bl_get_data(bd);
	uint8_t level;

	/* Convert brightness to hardware level */
	if (bd->props.brightness < 0)
		level = 0;
	else if (bd->props.brightness > MAX_RADEON_LEVEL)
		level = MAX_RADEON_LEVEL;
	else
		level = bd->props.brightness;

	if (pdata->negative)
		level = MAX_RADEON_LEVEL - level;

	return level;
}

static int radeon_legacy_backlight_update_status(struct backlight_device *bd)
{
	struct radeon_backlight_privdata *pdata = bl_get_data(bd);
	struct radeon_encoder *radeon_encoder = pdata->encoder;
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	int dpms_mode = DRM_MODE_DPMS_ON;

	if (radeon_encoder->enc_priv) {
		if (rdev->is_atom_bios) {
			struct radeon_encoder_atom_dig *lvds = radeon_encoder->enc_priv;
			dpms_mode = lvds->dpms_mode;
			lvds->backlight_level = radeon_legacy_lvds_level(bd);
		} else {
			struct radeon_encoder_lvds *lvds = radeon_encoder->enc_priv;
			dpms_mode = lvds->dpms_mode;
			lvds->backlight_level = radeon_legacy_lvds_level(bd);
		}
	}

	if (bd->props.brightness > 0)
		radeon_legacy_lvds_update(&radeon_encoder->base, dpms_mode);
	else
		radeon_legacy_lvds_update(&radeon_encoder->base, DRM_MODE_DPMS_OFF);

	return 0;
}

static int radeon_legacy_backlight_get_brightness(struct backlight_device *bd)
{
	struct radeon_backlight_privdata *pdata = bl_get_data(bd);
	struct radeon_encoder *radeon_encoder = pdata->encoder;
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	uint8_t backlight_level;

	backlight_level = (RREG32(RADEON_LVDS_GEN_CNTL) >>
			   RADEON_LVDS_BL_MOD_LEVEL_SHIFT) & 0xff;

	return pdata->negative ? MAX_RADEON_LEVEL - backlight_level : backlight_level;
}

static const struct backlight_ops radeon_backlight_ops = {
	.get_brightness = radeon_legacy_backlight_get_brightness,
	.update_status	= radeon_legacy_backlight_update_status,
};

void radeon_legacy_backlight_init(struct radeon_encoder *radeon_encoder,
				  struct drm_connector *drm_connector)
{
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct backlight_device *bd;
	struct backlight_properties props;
	struct radeon_backlight_privdata *pdata;
	uint8_t backlight_level;

	if (!radeon_encoder->enc_priv)
		return;

#ifdef CONFIG_PMAC_BACKLIGHT
	if (!pmac_has_backlight_type("ati") &&
	    !pmac_has_backlight_type("mnca"))
		return;
#endif

	pdata = kmalloc(sizeof(struct radeon_backlight_privdata), GFP_KERNEL);
	if (!pdata) {
		DRM_ERROR("Memory allocation failed\n");
		goto error;
	}

	props.max_brightness = MAX_RADEON_LEVEL;
	props.type = BACKLIGHT_RAW;
	bd = backlight_device_register("radeon_bl", &drm_connector->kdev,
				       pdata, &radeon_backlight_ops, &props);
	if (IS_ERR(bd)) {
		DRM_ERROR("Backlight registration failed\n");
		goto error;
	}

	pdata->encoder = radeon_encoder;

	backlight_level = (RREG32(RADEON_LVDS_GEN_CNTL) >>
			   RADEON_LVDS_BL_MOD_LEVEL_SHIFT) & 0xff;

	/* First, try to detect backlight level sense based on the assumption
	 * that firmware set it up at full brightness
	 */
	if (backlight_level == 0)
		pdata->negative = true;
	else if (backlight_level == 0xff)
		pdata->negative = false;
	else {
		/* XXX hack... maybe some day we can figure out in what direction
		 * backlight should work on a given panel?
		 */
		pdata->negative = (rdev->family != CHIP_RV200 &&
				   rdev->family != CHIP_RV250 &&
				   rdev->family != CHIP_RV280 &&
				   rdev->family != CHIP_RV350);

#ifdef CONFIG_PMAC_BACKLIGHT
		pdata->negative = (pdata->negative ||
				   of_machine_is_compatible("PowerBook4,3") ||
				   of_machine_is_compatible("PowerBook6,3") ||
				   of_machine_is_compatible("PowerBook6,5"));
#endif
	}

	if (rdev->is_atom_bios) {
		struct radeon_encoder_atom_dig *lvds = radeon_encoder->enc_priv;
		lvds->bl_dev = bd;
	} else {
		struct radeon_encoder_lvds *lvds = radeon_encoder->enc_priv;
		lvds->bl_dev = bd;
	}

	bd->props.brightness = radeon_legacy_backlight_get_brightness(bd);
	bd->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(bd);

	DRM_INFO("radeon legacy LVDS backlight initialized\n");

	return;

error:
	kfree(pdata);
	return;
}

static void radeon_legacy_backlight_exit(struct radeon_encoder *radeon_encoder)
{
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct backlight_device *bd = NULL;

	if (!radeon_encoder->enc_priv)
		return;

	if (rdev->is_atom_bios) {
		struct radeon_encoder_atom_dig *lvds = radeon_encoder->enc_priv;
		bd = lvds->bl_dev;
		lvds->bl_dev = NULL;
	} else {
		struct radeon_encoder_lvds *lvds = radeon_encoder->enc_priv;
		bd = lvds->bl_dev;
		lvds->bl_dev = NULL;
	}

	if (bd) {
		struct radeon_legacy_backlight_privdata *pdata;

		pdata = bl_get_data(bd);
		backlight_device_unregister(bd);
		kfree(pdata);

		DRM_INFO("radeon legacy LVDS backlight unloaded\n");
	}
}

#else /* !CONFIG_BACKLIGHT_CLASS_DEVICE */

void radeon_legacy_backlight_init(struct radeon_encoder *encoder)
{
}

static void radeon_legacy_backlight_exit(struct radeon_encoder *encoder)
{
}

#endif


static void radeon_lvds_enc_destroy(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);

	if (radeon_encoder->enc_priv) {
		radeon_legacy_backlight_exit(radeon_encoder);
		kfree(radeon_encoder->enc_priv);
	}
	drm_encoder_cleanup(encoder);
	kfree(radeon_encoder);
}

static const struct drm_encoder_funcs radeon_legacy_lvds_enc_funcs = {
	.destroy = radeon_lvds_enc_destroy,
};

static void radeon_legacy_primary_dac_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t crtc_ext_cntl = RREG32(RADEON_CRTC_EXT_CNTL);
	uint32_t dac_cntl = RREG32(RADEON_DAC_CNTL);
	uint32_t dac_macro_cntl = RREG32(RADEON_DAC_MACRO_CNTL);

	DRM_DEBUG_KMS("\n");

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		crtc_ext_cntl |= RADEON_CRTC_CRT_ON;
		dac_cntl &= ~RADEON_DAC_PDWN;
		dac_macro_cntl &= ~(RADEON_DAC_PDWN_R |
				    RADEON_DAC_PDWN_G |
				    RADEON_DAC_PDWN_B);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		crtc_ext_cntl &= ~RADEON_CRTC_CRT_ON;
		dac_cntl |= RADEON_DAC_PDWN;
		dac_macro_cntl |= (RADEON_DAC_PDWN_R |
				   RADEON_DAC_PDWN_G |
				   RADEON_DAC_PDWN_B);
		break;
	}

	WREG32(RADEON_CRTC_EXT_CNTL, crtc_ext_cntl);
	WREG32(RADEON_DAC_CNTL, dac_cntl);
	WREG32(RADEON_DAC_MACRO_CNTL, dac_macro_cntl);

	if (rdev->is_atom_bios)
		radeon_atombios_encoder_dpms_scratch_regs(encoder, (mode == DRM_MODE_DPMS_ON) ? true : false);
	else
		radeon_combios_encoder_dpms_scratch_regs(encoder, (mode == DRM_MODE_DPMS_ON) ? true : false);

}

static void radeon_legacy_primary_dac_prepare(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;

	if (rdev->is_atom_bios)
		radeon_atom_output_lock(encoder, true);
	else
		radeon_combios_output_lock(encoder, true);
	radeon_legacy_primary_dac_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void radeon_legacy_primary_dac_commit(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;

	radeon_legacy_primary_dac_dpms(encoder, DRM_MODE_DPMS_ON);

	if (rdev->is_atom_bios)
		radeon_atom_output_lock(encoder, false);
	else
		radeon_combios_output_lock(encoder, false);
}

static void radeon_legacy_primary_dac_mode_set(struct drm_encoder *encoder,
					       struct drm_display_mode *mode,
					       struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t disp_output_cntl, dac_cntl, dac2_cntl, dac_macro_cntl;

	DRM_DEBUG_KMS("\n");

	if (radeon_crtc->crtc_id == 0) {
		if (rdev->family == CHIP_R200 || ASIC_IS_R300(rdev)) {
			disp_output_cntl = RREG32(RADEON_DISP_OUTPUT_CNTL) &
				~(RADEON_DISP_DAC_SOURCE_MASK);
			WREG32(RADEON_DISP_OUTPUT_CNTL, disp_output_cntl);
		} else {
			dac2_cntl = RREG32(RADEON_DAC_CNTL2)  & ~(RADEON_DAC2_DAC_CLK_SEL);
			WREG32(RADEON_DAC_CNTL2, dac2_cntl);
		}
	} else {
		if (rdev->family == CHIP_R200 || ASIC_IS_R300(rdev)) {
			disp_output_cntl = RREG32(RADEON_DISP_OUTPUT_CNTL) &
				~(RADEON_DISP_DAC_SOURCE_MASK);
			disp_output_cntl |= RADEON_DISP_DAC_SOURCE_CRTC2;
			WREG32(RADEON_DISP_OUTPUT_CNTL, disp_output_cntl);
		} else {
			dac2_cntl = RREG32(RADEON_DAC_CNTL2) | RADEON_DAC2_DAC_CLK_SEL;
			WREG32(RADEON_DAC_CNTL2, dac2_cntl);
		}
	}

	dac_cntl = (RADEON_DAC_MASK_ALL |
		    RADEON_DAC_VGA_ADR_EN |
		    /* TODO 6-bits */
		    RADEON_DAC_8BIT_EN);

	WREG32_P(RADEON_DAC_CNTL,
		       dac_cntl,
		       RADEON_DAC_RANGE_CNTL |
		       RADEON_DAC_BLANKING);

	if (radeon_encoder->enc_priv) {
		struct radeon_encoder_primary_dac *p_dac = (struct radeon_encoder_primary_dac *)radeon_encoder->enc_priv;
		dac_macro_cntl = p_dac->ps2_pdac_adj;
	} else
		dac_macro_cntl = RREG32(RADEON_DAC_MACRO_CNTL);
	dac_macro_cntl |= RADEON_DAC_PDWN_R | RADEON_DAC_PDWN_G | RADEON_DAC_PDWN_B;
	WREG32(RADEON_DAC_MACRO_CNTL, dac_macro_cntl);

	if (rdev->is_atom_bios)
		radeon_atombios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);
	else
		radeon_combios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);
}

static enum drm_connector_status radeon_legacy_primary_dac_detect(struct drm_encoder *encoder,
								  struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t vclk_ecp_cntl, crtc_ext_cntl;
	uint32_t dac_ext_cntl, dac_cntl, dac_macro_cntl, tmp;
	enum drm_connector_status found = connector_status_disconnected;
	bool color = true;

	/* save the regs we need */
	vclk_ecp_cntl = RREG32_PLL(RADEON_VCLK_ECP_CNTL);
	crtc_ext_cntl = RREG32(RADEON_CRTC_EXT_CNTL);
	dac_ext_cntl = RREG32(RADEON_DAC_EXT_CNTL);
	dac_cntl = RREG32(RADEON_DAC_CNTL);
	dac_macro_cntl = RREG32(RADEON_DAC_MACRO_CNTL);

	tmp = vclk_ecp_cntl &
		~(RADEON_PIXCLK_ALWAYS_ONb | RADEON_PIXCLK_DAC_ALWAYS_ONb);
	WREG32_PLL(RADEON_VCLK_ECP_CNTL, tmp);

	tmp = crtc_ext_cntl | RADEON_CRTC_CRT_ON;
	WREG32(RADEON_CRTC_EXT_CNTL, tmp);

	tmp = RADEON_DAC_FORCE_BLANK_OFF_EN |
		RADEON_DAC_FORCE_DATA_EN;

	if (color)
		tmp |= RADEON_DAC_FORCE_DATA_SEL_RGB;
	else
		tmp |= RADEON_DAC_FORCE_DATA_SEL_G;

	if (ASIC_IS_R300(rdev))
		tmp |= (0x1b6 << RADEON_DAC_FORCE_DATA_SHIFT);
	else
		tmp |= (0x180 << RADEON_DAC_FORCE_DATA_SHIFT);

	WREG32(RADEON_DAC_EXT_CNTL, tmp);

	tmp = dac_cntl & ~(RADEON_DAC_RANGE_CNTL_MASK | RADEON_DAC_PDWN);
	tmp |= RADEON_DAC_RANGE_CNTL_PS2 | RADEON_DAC_CMP_EN;
	WREG32(RADEON_DAC_CNTL, tmp);

	tmp &= ~(RADEON_DAC_PDWN_R |
		 RADEON_DAC_PDWN_G |
		 RADEON_DAC_PDWN_B);

	WREG32(RADEON_DAC_MACRO_CNTL, tmp);

	udelay(2000);

	if (RREG32(RADEON_DAC_CNTL) & RADEON_DAC_CMP_OUTPUT)
		found = connector_status_connected;

	/* restore the regs we used */
	WREG32(RADEON_DAC_CNTL, dac_cntl);
	WREG32(RADEON_DAC_MACRO_CNTL, dac_macro_cntl);
	WREG32(RADEON_DAC_EXT_CNTL, dac_ext_cntl);
	WREG32(RADEON_CRTC_EXT_CNTL, crtc_ext_cntl);
	WREG32_PLL(RADEON_VCLK_ECP_CNTL, vclk_ecp_cntl);

	return found;
}

static const struct drm_encoder_helper_funcs radeon_legacy_primary_dac_helper_funcs = {
	.dpms = radeon_legacy_primary_dac_dpms,
	.mode_fixup = radeon_legacy_mode_fixup,
	.prepare = radeon_legacy_primary_dac_prepare,
	.mode_set = radeon_legacy_primary_dac_mode_set,
	.commit = radeon_legacy_primary_dac_commit,
	.detect = radeon_legacy_primary_dac_detect,
	.disable = radeon_legacy_encoder_disable,
};


static const struct drm_encoder_funcs radeon_legacy_primary_dac_enc_funcs = {
	.destroy = radeon_enc_destroy,
};

static void radeon_legacy_tmds_int_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t fp_gen_cntl = RREG32(RADEON_FP_GEN_CNTL);
	DRM_DEBUG_KMS("\n");

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		fp_gen_cntl |= (RADEON_FP_FPON | RADEON_FP_TMDS_EN);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		fp_gen_cntl &= ~(RADEON_FP_FPON | RADEON_FP_TMDS_EN);
		break;
	}

	WREG32(RADEON_FP_GEN_CNTL, fp_gen_cntl);

	if (rdev->is_atom_bios)
		radeon_atombios_encoder_dpms_scratch_regs(encoder, (mode == DRM_MODE_DPMS_ON) ? true : false);
	else
		radeon_combios_encoder_dpms_scratch_regs(encoder, (mode == DRM_MODE_DPMS_ON) ? true : false);

}

static void radeon_legacy_tmds_int_prepare(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;

	if (rdev->is_atom_bios)
		radeon_atom_output_lock(encoder, true);
	else
		radeon_combios_output_lock(encoder, true);
	radeon_legacy_tmds_int_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void radeon_legacy_tmds_int_commit(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;

	radeon_legacy_tmds_int_dpms(encoder, DRM_MODE_DPMS_ON);

	if (rdev->is_atom_bios)
		radeon_atom_output_lock(encoder, true);
	else
		radeon_combios_output_lock(encoder, true);
}

static void radeon_legacy_tmds_int_mode_set(struct drm_encoder *encoder,
					    struct drm_display_mode *mode,
					    struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t tmp, tmds_pll_cntl, tmds_transmitter_cntl, fp_gen_cntl;
	int i;

	DRM_DEBUG_KMS("\n");

	tmp = tmds_pll_cntl = RREG32(RADEON_TMDS_PLL_CNTL);
	tmp &= 0xfffff;
	if (rdev->family == CHIP_RV280) {
		/* bit 22 of TMDS_PLL_CNTL is read-back inverted */
		tmp ^= (1 << 22);
		tmds_pll_cntl ^= (1 << 22);
	}

	if (radeon_encoder->enc_priv) {
		struct radeon_encoder_int_tmds *tmds = (struct radeon_encoder_int_tmds *)radeon_encoder->enc_priv;

		for (i = 0; i < 4; i++) {
			if (tmds->tmds_pll[i].freq == 0)
				break;
			if ((uint32_t)(mode->clock / 10) < tmds->tmds_pll[i].freq) {
				tmp = tmds->tmds_pll[i].value ;
				break;
			}
		}
	}

	if (ASIC_IS_R300(rdev) || (rdev->family == CHIP_RV280)) {
		if (tmp & 0xfff00000)
			tmds_pll_cntl = tmp;
		else {
			tmds_pll_cntl &= 0xfff00000;
			tmds_pll_cntl |= tmp;
		}
	} else
		tmds_pll_cntl = tmp;

	tmds_transmitter_cntl = RREG32(RADEON_TMDS_TRANSMITTER_CNTL) &
		~(RADEON_TMDS_TRANSMITTER_PLLRST);

    if (rdev->family == CHIP_R200 ||
	rdev->family == CHIP_R100 ||
	ASIC_IS_R300(rdev))
	    tmds_transmitter_cntl &= ~(RADEON_TMDS_TRANSMITTER_PLLEN);
    else /* RV chips got this bit reversed */
	    tmds_transmitter_cntl |= RADEON_TMDS_TRANSMITTER_PLLEN;

    fp_gen_cntl = (RREG32(RADEON_FP_GEN_CNTL) |
		   (RADEON_FP_CRTC_DONT_SHADOW_VPAR |
		    RADEON_FP_CRTC_DONT_SHADOW_HEND));

    fp_gen_cntl &= ~(RADEON_FP_FPON | RADEON_FP_TMDS_EN);

    fp_gen_cntl &= ~(RADEON_FP_RMX_HVSYNC_CONTROL_EN |
		     RADEON_FP_DFP_SYNC_SEL |
		     RADEON_FP_CRT_SYNC_SEL |
		     RADEON_FP_CRTC_LOCK_8DOT |
		     RADEON_FP_USE_SHADOW_EN |
		     RADEON_FP_CRTC_USE_SHADOW_VEND |
		     RADEON_FP_CRT_SYNC_ALT);

    if (1) /*  FIXME rgbBits == 8 */
	    fp_gen_cntl |= RADEON_FP_PANEL_FORMAT;  /* 24 bit format */
    else
	    fp_gen_cntl &= ~RADEON_FP_PANEL_FORMAT;/* 18 bit format */

    if (radeon_crtc->crtc_id == 0) {
	    if (ASIC_IS_R300(rdev) || rdev->family == CHIP_R200) {
		    fp_gen_cntl &= ~R200_FP_SOURCE_SEL_MASK;
		    if (radeon_encoder->rmx_type != RMX_OFF)
			    fp_gen_cntl |= R200_FP_SOURCE_SEL_RMX;
		    else
			    fp_gen_cntl |= R200_FP_SOURCE_SEL_CRTC1;
	    } else
		    fp_gen_cntl &= ~RADEON_FP_SEL_CRTC2;
    } else {
	    if (ASIC_IS_R300(rdev) || rdev->family == CHIP_R200) {
		    fp_gen_cntl &= ~R200_FP_SOURCE_SEL_MASK;
		    fp_gen_cntl |= R200_FP_SOURCE_SEL_CRTC2;
	    } else
		    fp_gen_cntl |= RADEON_FP_SEL_CRTC2;
    }

    WREG32(RADEON_TMDS_PLL_CNTL, tmds_pll_cntl);
    WREG32(RADEON_TMDS_TRANSMITTER_CNTL, tmds_transmitter_cntl);
    WREG32(RADEON_FP_GEN_CNTL, fp_gen_cntl);

	if (rdev->is_atom_bios)
		radeon_atombios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);
	else
		radeon_combios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);
}

static const struct drm_encoder_helper_funcs radeon_legacy_tmds_int_helper_funcs = {
	.dpms = radeon_legacy_tmds_int_dpms,
	.mode_fixup = radeon_legacy_mode_fixup,
	.prepare = radeon_legacy_tmds_int_prepare,
	.mode_set = radeon_legacy_tmds_int_mode_set,
	.commit = radeon_legacy_tmds_int_commit,
	.disable = radeon_legacy_encoder_disable,
};


static const struct drm_encoder_funcs radeon_legacy_tmds_int_enc_funcs = {
	.destroy = radeon_enc_destroy,
};

static void radeon_legacy_tmds_ext_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t fp2_gen_cntl = RREG32(RADEON_FP2_GEN_CNTL);
	DRM_DEBUG_KMS("\n");

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		fp2_gen_cntl &= ~RADEON_FP2_BLANK_EN;
		fp2_gen_cntl |= (RADEON_FP2_ON | RADEON_FP2_DVO_EN);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		fp2_gen_cntl |= RADEON_FP2_BLANK_EN;
		fp2_gen_cntl &= ~(RADEON_FP2_ON | RADEON_FP2_DVO_EN);
		break;
	}

	WREG32(RADEON_FP2_GEN_CNTL, fp2_gen_cntl);

	if (rdev->is_atom_bios)
		radeon_atombios_encoder_dpms_scratch_regs(encoder, (mode == DRM_MODE_DPMS_ON) ? true : false);
	else
		radeon_combios_encoder_dpms_scratch_regs(encoder, (mode == DRM_MODE_DPMS_ON) ? true : false);

}

static void radeon_legacy_tmds_ext_prepare(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;

	if (rdev->is_atom_bios)
		radeon_atom_output_lock(encoder, true);
	else
		radeon_combios_output_lock(encoder, true);
	radeon_legacy_tmds_ext_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void radeon_legacy_tmds_ext_commit(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	radeon_legacy_tmds_ext_dpms(encoder, DRM_MODE_DPMS_ON);

	if (rdev->is_atom_bios)
		radeon_atom_output_lock(encoder, false);
	else
		radeon_combios_output_lock(encoder, false);
}

static void radeon_legacy_tmds_ext_mode_set(struct drm_encoder *encoder,
					    struct drm_display_mode *mode,
					    struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t fp2_gen_cntl;

	DRM_DEBUG_KMS("\n");

	if (rdev->is_atom_bios) {
		radeon_encoder->pixel_clock = adjusted_mode->clock;
		atombios_dvo_setup(encoder, ATOM_ENABLE);
		fp2_gen_cntl = RREG32(RADEON_FP2_GEN_CNTL);
	} else {
		fp2_gen_cntl = RREG32(RADEON_FP2_GEN_CNTL);

		if (1) /*  FIXME rgbBits == 8 */
			fp2_gen_cntl |= RADEON_FP2_PANEL_FORMAT; /* 24 bit format, */
		else
			fp2_gen_cntl &= ~RADEON_FP2_PANEL_FORMAT;/* 18 bit format, */

		fp2_gen_cntl &= ~(RADEON_FP2_ON |
				  RADEON_FP2_DVO_EN |
				  RADEON_FP2_DVO_RATE_SEL_SDR);

		/* XXX: these are oem specific */
		if (ASIC_IS_R300(rdev)) {
			if ((dev->pdev->device == 0x4850) &&
			    (dev->pdev->subsystem_vendor == 0x1028) &&
			    (dev->pdev->subsystem_device == 0x2001)) /* Dell Inspiron 8600 */
				fp2_gen_cntl |= R300_FP2_DVO_CLOCK_MODE_SINGLE;
			else
				fp2_gen_cntl |= RADEON_FP2_PAD_FLOP_EN | R300_FP2_DVO_CLOCK_MODE_SINGLE;

			/*if (mode->clock > 165000)
			  fp2_gen_cntl |= R300_FP2_DVO_DUAL_CHANNEL_EN;*/
		}
		if (!radeon_combios_external_tmds_setup(encoder))
			radeon_external_tmds_setup(encoder);
	}

	if (radeon_crtc->crtc_id == 0) {
		if ((rdev->family == CHIP_R200) || ASIC_IS_R300(rdev)) {
			fp2_gen_cntl &= ~R200_FP2_SOURCE_SEL_MASK;
			if (radeon_encoder->rmx_type != RMX_OFF)
				fp2_gen_cntl |= R200_FP2_SOURCE_SEL_RMX;
			else
				fp2_gen_cntl |= R200_FP2_SOURCE_SEL_CRTC1;
		} else
			fp2_gen_cntl &= ~RADEON_FP2_SRC_SEL_CRTC2;
	} else {
		if ((rdev->family == CHIP_R200) || ASIC_IS_R300(rdev)) {
			fp2_gen_cntl &= ~R200_FP2_SOURCE_SEL_MASK;
			fp2_gen_cntl |= R200_FP2_SOURCE_SEL_CRTC2;
		} else
			fp2_gen_cntl |= RADEON_FP2_SRC_SEL_CRTC2;
	}

	WREG32(RADEON_FP2_GEN_CNTL, fp2_gen_cntl);

	if (rdev->is_atom_bios)
		radeon_atombios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);
	else
		radeon_combios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);
}

static void radeon_ext_tmds_enc_destroy(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_ext_tmds *tmds = radeon_encoder->enc_priv;
	if (tmds) {
		if (tmds->i2c_bus)
			radeon_i2c_destroy(tmds->i2c_bus);
	}
	kfree(radeon_encoder->enc_priv);
	drm_encoder_cleanup(encoder);
	kfree(radeon_encoder);
}

static const struct drm_encoder_helper_funcs radeon_legacy_tmds_ext_helper_funcs = {
	.dpms = radeon_legacy_tmds_ext_dpms,
	.mode_fixup = radeon_legacy_mode_fixup,
	.prepare = radeon_legacy_tmds_ext_prepare,
	.mode_set = radeon_legacy_tmds_ext_mode_set,
	.commit = radeon_legacy_tmds_ext_commit,
	.disable = radeon_legacy_encoder_disable,
};


static const struct drm_encoder_funcs radeon_legacy_tmds_ext_enc_funcs = {
	.destroy = radeon_ext_tmds_enc_destroy,
};

static void radeon_legacy_tv_dac_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t fp2_gen_cntl = 0, crtc2_gen_cntl = 0, tv_dac_cntl = 0;
	uint32_t tv_master_cntl = 0;
	bool is_tv;
	DRM_DEBUG_KMS("\n");

	is_tv = radeon_encoder->active_device & ATOM_DEVICE_TV_SUPPORT ? true : false;

	if (rdev->family == CHIP_R200)
		fp2_gen_cntl = RREG32(RADEON_FP2_GEN_CNTL);
	else {
		if (is_tv)
			tv_master_cntl = RREG32(RADEON_TV_MASTER_CNTL);
		else
			crtc2_gen_cntl = RREG32(RADEON_CRTC2_GEN_CNTL);
		tv_dac_cntl = RREG32(RADEON_TV_DAC_CNTL);
	}

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		if (rdev->family == CHIP_R200) {
			fp2_gen_cntl |= (RADEON_FP2_ON | RADEON_FP2_DVO_EN);
		} else {
			if (is_tv)
				tv_master_cntl |= RADEON_TV_ON;
			else
				crtc2_gen_cntl |= RADEON_CRTC2_CRT2_ON;

			if (rdev->family == CHIP_R420 ||
			    rdev->family == CHIP_R423 ||
			    rdev->family == CHIP_RV410)
				tv_dac_cntl &= ~(R420_TV_DAC_RDACPD |
						 R420_TV_DAC_GDACPD |
						 R420_TV_DAC_BDACPD |
						 RADEON_TV_DAC_BGSLEEP);
			else
				tv_dac_cntl &= ~(RADEON_TV_DAC_RDACPD |
						 RADEON_TV_DAC_GDACPD |
						 RADEON_TV_DAC_BDACPD |
						 RADEON_TV_DAC_BGSLEEP);
		}
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		if (rdev->family == CHIP_R200)
			fp2_gen_cntl &= ~(RADEON_FP2_ON | RADEON_FP2_DVO_EN);
		else {
			if (is_tv)
				tv_master_cntl &= ~RADEON_TV_ON;
			else
				crtc2_gen_cntl &= ~RADEON_CRTC2_CRT2_ON;

			if (rdev->family == CHIP_R420 ||
			    rdev->family == CHIP_R423 ||
			    rdev->family == CHIP_RV410)
				tv_dac_cntl |= (R420_TV_DAC_RDACPD |
						R420_TV_DAC_GDACPD |
						R420_TV_DAC_BDACPD |
						RADEON_TV_DAC_BGSLEEP);
			else
				tv_dac_cntl |= (RADEON_TV_DAC_RDACPD |
						RADEON_TV_DAC_GDACPD |
						RADEON_TV_DAC_BDACPD |
						RADEON_TV_DAC_BGSLEEP);
		}
		break;
	}

	if (rdev->family == CHIP_R200) {
		WREG32(RADEON_FP2_GEN_CNTL, fp2_gen_cntl);
	} else {
		if (is_tv)
			WREG32(RADEON_TV_MASTER_CNTL, tv_master_cntl);
		else
			WREG32(RADEON_CRTC2_GEN_CNTL, crtc2_gen_cntl);
		WREG32(RADEON_TV_DAC_CNTL, tv_dac_cntl);
	}

	if (rdev->is_atom_bios)
		radeon_atombios_encoder_dpms_scratch_regs(encoder, (mode == DRM_MODE_DPMS_ON) ? true : false);
	else
		radeon_combios_encoder_dpms_scratch_regs(encoder, (mode == DRM_MODE_DPMS_ON) ? true : false);

}

static void radeon_legacy_tv_dac_prepare(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;

	if (rdev->is_atom_bios)
		radeon_atom_output_lock(encoder, true);
	else
		radeon_combios_output_lock(encoder, true);
	radeon_legacy_tv_dac_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void radeon_legacy_tv_dac_commit(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;

	radeon_legacy_tv_dac_dpms(encoder, DRM_MODE_DPMS_ON);

	if (rdev->is_atom_bios)
		radeon_atom_output_lock(encoder, true);
	else
		radeon_combios_output_lock(encoder, true);
}

static void radeon_legacy_tv_dac_mode_set(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_tv_dac *tv_dac = radeon_encoder->enc_priv;
	uint32_t tv_dac_cntl, gpiopad_a = 0, dac2_cntl, disp_output_cntl = 0;
	uint32_t disp_hw_debug = 0, fp2_gen_cntl = 0, disp_tv_out_cntl = 0;
	bool is_tv = false;

	DRM_DEBUG_KMS("\n");

	is_tv = radeon_encoder->active_device & ATOM_DEVICE_TV_SUPPORT ? true : false;

	if (rdev->family != CHIP_R200) {
		tv_dac_cntl = RREG32(RADEON_TV_DAC_CNTL);
		if (rdev->family == CHIP_R420 ||
		    rdev->family == CHIP_R423 ||
		    rdev->family == CHIP_RV410) {
			tv_dac_cntl &= ~(RADEON_TV_DAC_STD_MASK |
					 RADEON_TV_DAC_BGADJ_MASK |
					 R420_TV_DAC_DACADJ_MASK |
					 R420_TV_DAC_RDACPD |
					 R420_TV_DAC_GDACPD |
					 R420_TV_DAC_BDACPD |
					 R420_TV_DAC_TVENABLE);
		} else {
			tv_dac_cntl &= ~(RADEON_TV_DAC_STD_MASK |
					 RADEON_TV_DAC_BGADJ_MASK |
					 RADEON_TV_DAC_DACADJ_MASK |
					 RADEON_TV_DAC_RDACPD |
					 RADEON_TV_DAC_GDACPD |
					 RADEON_TV_DAC_BDACPD);
		}

		tv_dac_cntl |= RADEON_TV_DAC_NBLANK | RADEON_TV_DAC_NHOLD;

		if (is_tv) {
			if (tv_dac->tv_std == TV_STD_NTSC ||
			    tv_dac->tv_std == TV_STD_NTSC_J ||
			    tv_dac->tv_std == TV_STD_PAL_M ||
			    tv_dac->tv_std == TV_STD_PAL_60)
				tv_dac_cntl |= tv_dac->ntsc_tvdac_adj;
			else
				tv_dac_cntl |= tv_dac->pal_tvdac_adj;

			if (tv_dac->tv_std == TV_STD_NTSC ||
			    tv_dac->tv_std == TV_STD_NTSC_J)
				tv_dac_cntl |= RADEON_TV_DAC_STD_NTSC;
			else
				tv_dac_cntl |= RADEON_TV_DAC_STD_PAL;
		} else
			tv_dac_cntl |= (RADEON_TV_DAC_STD_PS2 |
					tv_dac->ps2_tvdac_adj);

		WREG32(RADEON_TV_DAC_CNTL, tv_dac_cntl);
	}

	if (ASIC_IS_R300(rdev)) {
		gpiopad_a = RREG32(RADEON_GPIOPAD_A) | 1;
		disp_output_cntl = RREG32(RADEON_DISP_OUTPUT_CNTL);
	} else if (rdev->family != CHIP_R200)
		disp_hw_debug = RREG32(RADEON_DISP_HW_DEBUG);
	else if (rdev->family == CHIP_R200)
		fp2_gen_cntl = RREG32(RADEON_FP2_GEN_CNTL);

	if (rdev->family >= CHIP_R200)
		disp_tv_out_cntl = RREG32(RADEON_DISP_TV_OUT_CNTL);

	if (is_tv) {
		uint32_t dac_cntl;

		dac_cntl = RREG32(RADEON_DAC_CNTL);
		dac_cntl &= ~RADEON_DAC_TVO_EN;
		WREG32(RADEON_DAC_CNTL, dac_cntl);

		if (ASIC_IS_R300(rdev))
			gpiopad_a = RREG32(RADEON_GPIOPAD_A) & ~1;

		dac2_cntl = RREG32(RADEON_DAC_CNTL2) & ~RADEON_DAC2_DAC2_CLK_SEL;
		if (radeon_crtc->crtc_id == 0) {
			if (ASIC_IS_R300(rdev)) {
				disp_output_cntl &= ~RADEON_DISP_TVDAC_SOURCE_MASK;
				disp_output_cntl |= (RADEON_DISP_TVDAC_SOURCE_CRTC |
						     RADEON_DISP_TV_SOURCE_CRTC);
			}
			if (rdev->family >= CHIP_R200) {
				disp_tv_out_cntl &= ~RADEON_DISP_TV_PATH_SRC_CRTC2;
			} else {
				disp_hw_debug |= RADEON_CRT2_DISP1_SEL;
			}
		} else {
			if (ASIC_IS_R300(rdev)) {
				disp_output_cntl &= ~RADEON_DISP_TVDAC_SOURCE_MASK;
				disp_output_cntl |= RADEON_DISP_TV_SOURCE_CRTC;
			}
			if (rdev->family >= CHIP_R200) {
				disp_tv_out_cntl |= RADEON_DISP_TV_PATH_SRC_CRTC2;
			} else {
				disp_hw_debug &= ~RADEON_CRT2_DISP1_SEL;
			}
		}
		WREG32(RADEON_DAC_CNTL2, dac2_cntl);
	} else {

		dac2_cntl = RREG32(RADEON_DAC_CNTL2) | RADEON_DAC2_DAC2_CLK_SEL;

		if (radeon_crtc->crtc_id == 0) {
			if (ASIC_IS_R300(rdev)) {
				disp_output_cntl &= ~RADEON_DISP_TVDAC_SOURCE_MASK;
				disp_output_cntl |= RADEON_DISP_TVDAC_SOURCE_CRTC;
			} else if (rdev->family == CHIP_R200) {
				fp2_gen_cntl &= ~(R200_FP2_SOURCE_SEL_MASK |
						  RADEON_FP2_DVO_RATE_SEL_SDR);
			} else
				disp_hw_debug |= RADEON_CRT2_DISP1_SEL;
		} else {
			if (ASIC_IS_R300(rdev)) {
				disp_output_cntl &= ~RADEON_DISP_TVDAC_SOURCE_MASK;
				disp_output_cntl |= RADEON_DISP_TVDAC_SOURCE_CRTC2;
			} else if (rdev->family == CHIP_R200) {
				fp2_gen_cntl &= ~(R200_FP2_SOURCE_SEL_MASK |
						  RADEON_FP2_DVO_RATE_SEL_SDR);
				fp2_gen_cntl |= R200_FP2_SOURCE_SEL_CRTC2;
			} else
				disp_hw_debug &= ~RADEON_CRT2_DISP1_SEL;
		}
		WREG32(RADEON_DAC_CNTL2, dac2_cntl);
	}

	if (ASIC_IS_R300(rdev)) {
		WREG32_P(RADEON_GPIOPAD_A, gpiopad_a, ~1);
		WREG32(RADEON_DISP_OUTPUT_CNTL, disp_output_cntl);
	} else if (rdev->family != CHIP_R200)
		WREG32(RADEON_DISP_HW_DEBUG, disp_hw_debug);
	else if (rdev->family == CHIP_R200)
		WREG32(RADEON_FP2_GEN_CNTL, fp2_gen_cntl);

	if (rdev->family >= CHIP_R200)
		WREG32(RADEON_DISP_TV_OUT_CNTL, disp_tv_out_cntl);

	if (is_tv)
		radeon_legacy_tv_mode_set(encoder, mode, adjusted_mode);

	if (rdev->is_atom_bios)
		radeon_atombios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);
	else
		radeon_combios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);

}

static bool r300_legacy_tv_detect(struct drm_encoder *encoder,
				  struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t crtc2_gen_cntl, tv_dac_cntl, dac_cntl2, dac_ext_cntl;
	uint32_t disp_output_cntl, gpiopad_a, tmp;
	bool found = false;

	/* save regs needed */
	gpiopad_a = RREG32(RADEON_GPIOPAD_A);
	dac_cntl2 = RREG32(RADEON_DAC_CNTL2);
	crtc2_gen_cntl = RREG32(RADEON_CRTC2_GEN_CNTL);
	dac_ext_cntl = RREG32(RADEON_DAC_EXT_CNTL);
	tv_dac_cntl = RREG32(RADEON_TV_DAC_CNTL);
	disp_output_cntl = RREG32(RADEON_DISP_OUTPUT_CNTL);

	WREG32_P(RADEON_GPIOPAD_A, 0, ~1);

	WREG32(RADEON_DAC_CNTL2, RADEON_DAC2_DAC2_CLK_SEL);

	WREG32(RADEON_CRTC2_GEN_CNTL,
	       RADEON_CRTC2_CRT2_ON | RADEON_CRTC2_VSYNC_TRISTAT);

	tmp = disp_output_cntl & ~RADEON_DISP_TVDAC_SOURCE_MASK;
	tmp |= RADEON_DISP_TVDAC_SOURCE_CRTC2;
	WREG32(RADEON_DISP_OUTPUT_CNTL, tmp);

	WREG32(RADEON_DAC_EXT_CNTL,
	       RADEON_DAC2_FORCE_BLANK_OFF_EN |
	       RADEON_DAC2_FORCE_DATA_EN |
	       RADEON_DAC_FORCE_DATA_SEL_RGB |
	       (0xec << RADEON_DAC_FORCE_DATA_SHIFT));

	WREG32(RADEON_TV_DAC_CNTL,
	       RADEON_TV_DAC_STD_NTSC |
	       (8 << RADEON_TV_DAC_BGADJ_SHIFT) |
	       (6 << RADEON_TV_DAC_DACADJ_SHIFT));

	RREG32(RADEON_TV_DAC_CNTL);
	mdelay(4);

	WREG32(RADEON_TV_DAC_CNTL,
	       RADEON_TV_DAC_NBLANK |
	       RADEON_TV_DAC_NHOLD |
	       RADEON_TV_MONITOR_DETECT_EN |
	       RADEON_TV_DAC_STD_NTSC |
	       (8 << RADEON_TV_DAC_BGADJ_SHIFT) |
	       (6 << RADEON_TV_DAC_DACADJ_SHIFT));

	RREG32(RADEON_TV_DAC_CNTL);
	mdelay(6);

	tmp = RREG32(RADEON_TV_DAC_CNTL);
	if ((tmp & RADEON_TV_DAC_GDACDET) != 0) {
		found = true;
		DRM_DEBUG_KMS("S-video TV connection detected\n");
	} else if ((tmp & RADEON_TV_DAC_BDACDET) != 0) {
		found = true;
		DRM_DEBUG_KMS("Composite TV connection detected\n");
	}

	WREG32(RADEON_TV_DAC_CNTL, tv_dac_cntl);
	WREG32(RADEON_DAC_EXT_CNTL, dac_ext_cntl);
	WREG32(RADEON_CRTC2_GEN_CNTL, crtc2_gen_cntl);
	WREG32(RADEON_DISP_OUTPUT_CNTL, disp_output_cntl);
	WREG32(RADEON_DAC_CNTL2, dac_cntl2);
	WREG32_P(RADEON_GPIOPAD_A, gpiopad_a, ~1);
	return found;
}

static bool radeon_legacy_tv_detect(struct drm_encoder *encoder,
				    struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t tv_dac_cntl, dac_cntl2;
	uint32_t config_cntl, tv_pre_dac_mux_cntl, tv_master_cntl, tmp;
	bool found = false;

	if (ASIC_IS_R300(rdev))
		return r300_legacy_tv_detect(encoder, connector);

	dac_cntl2 = RREG32(RADEON_DAC_CNTL2);
	tv_master_cntl = RREG32(RADEON_TV_MASTER_CNTL);
	tv_dac_cntl = RREG32(RADEON_TV_DAC_CNTL);
	config_cntl = RREG32(RADEON_CONFIG_CNTL);
	tv_pre_dac_mux_cntl = RREG32(RADEON_TV_PRE_DAC_MUX_CNTL);

	tmp = dac_cntl2 & ~RADEON_DAC2_DAC2_CLK_SEL;
	WREG32(RADEON_DAC_CNTL2, tmp);

	tmp = tv_master_cntl | RADEON_TV_ON;
	tmp &= ~(RADEON_TV_ASYNC_RST |
		 RADEON_RESTART_PHASE_FIX |
		 RADEON_CRT_FIFO_CE_EN |
		 RADEON_TV_FIFO_CE_EN |
		 RADEON_RE_SYNC_NOW_SEL_MASK);
	tmp |= RADEON_TV_FIFO_ASYNC_RST | RADEON_CRT_ASYNC_RST;
	WREG32(RADEON_TV_MASTER_CNTL, tmp);

	tmp = RADEON_TV_DAC_NBLANK | RADEON_TV_DAC_NHOLD |
		RADEON_TV_MONITOR_DETECT_EN | RADEON_TV_DAC_STD_NTSC |
		(8 << RADEON_TV_DAC_BGADJ_SHIFT);

	if (config_cntl & RADEON_CFG_ATI_REV_ID_MASK)
		tmp |= (4 << RADEON_TV_DAC_DACADJ_SHIFT);
	else
		tmp |= (8 << RADEON_TV_DAC_DACADJ_SHIFT);
	WREG32(RADEON_TV_DAC_CNTL, tmp);

	tmp = RADEON_C_GRN_EN | RADEON_CMP_BLU_EN |
		RADEON_RED_MX_FORCE_DAC_DATA |
		RADEON_GRN_MX_FORCE_DAC_DATA |
		RADEON_BLU_MX_FORCE_DAC_DATA |
		(0x109 << RADEON_TV_FORCE_DAC_DATA_SHIFT);
	WREG32(RADEON_TV_PRE_DAC_MUX_CNTL, tmp);

	mdelay(3);
	tmp = RREG32(RADEON_TV_DAC_CNTL);
	if (tmp & RADEON_TV_DAC_GDACDET) {
		found = true;
		DRM_DEBUG_KMS("S-video TV connection detected\n");
	} else if ((tmp & RADEON_TV_DAC_BDACDET) != 0) {
		found = true;
		DRM_DEBUG_KMS("Composite TV connection detected\n");
	}

	WREG32(RADEON_TV_PRE_DAC_MUX_CNTL, tv_pre_dac_mux_cntl);
	WREG32(RADEON_TV_DAC_CNTL, tv_dac_cntl);
	WREG32(RADEON_TV_MASTER_CNTL, tv_master_cntl);
	WREG32(RADEON_DAC_CNTL2, dac_cntl2);
	return found;
}

static enum drm_connector_status radeon_legacy_tv_dac_detect(struct drm_encoder *encoder,
							     struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t crtc2_gen_cntl, tv_dac_cntl, dac_cntl2, dac_ext_cntl;
	uint32_t disp_hw_debug, disp_output_cntl, gpiopad_a, pixclks_cntl, tmp;
	enum drm_connector_status found = connector_status_disconnected;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_tv_dac *tv_dac = radeon_encoder->enc_priv;
	bool color = true;
	struct drm_crtc *crtc;

	/* find out if crtc2 is in use or if this encoder is using it */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
		if ((radeon_crtc->crtc_id == 1) && crtc->enabled) {
			if (encoder->crtc != crtc) {
				return connector_status_disconnected;
			}
		}
	}

	if (connector->connector_type == DRM_MODE_CONNECTOR_SVIDEO ||
	    connector->connector_type == DRM_MODE_CONNECTOR_Composite ||
	    connector->connector_type == DRM_MODE_CONNECTOR_9PinDIN) {
		bool tv_detect;

		if (radeon_encoder->active_device && !(radeon_encoder->active_device & ATOM_DEVICE_TV_SUPPORT))
			return connector_status_disconnected;

		tv_detect = radeon_legacy_tv_detect(encoder, connector);
		if (tv_detect && tv_dac)
			found = connector_status_connected;
		return found;
	}

	/* don't probe if the encoder is being used for something else not CRT related */
	if (radeon_encoder->active_device && !(radeon_encoder->active_device & ATOM_DEVICE_CRT_SUPPORT)) {
		DRM_INFO("not detecting due to %08x\n", radeon_encoder->active_device);
		return connector_status_disconnected;
	}

	/* save the regs we need */
	pixclks_cntl = RREG32_PLL(RADEON_PIXCLKS_CNTL);
	gpiopad_a = ASIC_IS_R300(rdev) ? RREG32(RADEON_GPIOPAD_A) : 0;
	disp_output_cntl = ASIC_IS_R300(rdev) ? RREG32(RADEON_DISP_OUTPUT_CNTL) : 0;
	disp_hw_debug = ASIC_IS_R300(rdev) ? 0 : RREG32(RADEON_DISP_HW_DEBUG);
	crtc2_gen_cntl = RREG32(RADEON_CRTC2_GEN_CNTL);
	tv_dac_cntl = RREG32(RADEON_TV_DAC_CNTL);
	dac_ext_cntl = RREG32(RADEON_DAC_EXT_CNTL);
	dac_cntl2 = RREG32(RADEON_DAC_CNTL2);

	tmp = pixclks_cntl & ~(RADEON_PIX2CLK_ALWAYS_ONb
			       | RADEON_PIX2CLK_DAC_ALWAYS_ONb);
	WREG32_PLL(RADEON_PIXCLKS_CNTL, tmp);

	if (ASIC_IS_R300(rdev))
		WREG32_P(RADEON_GPIOPAD_A, 1, ~1);

	tmp = crtc2_gen_cntl & ~RADEON_CRTC2_PIX_WIDTH_MASK;
	tmp |= RADEON_CRTC2_CRT2_ON |
		(2 << RADEON_CRTC2_PIX_WIDTH_SHIFT);

	WREG32(RADEON_CRTC2_GEN_CNTL, tmp);

	if (ASIC_IS_R300(rdev)) {
		tmp = disp_output_cntl & ~RADEON_DISP_TVDAC_SOURCE_MASK;
		tmp |= RADEON_DISP_TVDAC_SOURCE_CRTC2;
		WREG32(RADEON_DISP_OUTPUT_CNTL, tmp);
	} else {
		tmp = disp_hw_debug & ~RADEON_CRT2_DISP1_SEL;
		WREG32(RADEON_DISP_HW_DEBUG, tmp);
	}

	tmp = RADEON_TV_DAC_NBLANK |
		RADEON_TV_DAC_NHOLD |
		RADEON_TV_MONITOR_DETECT_EN |
		RADEON_TV_DAC_STD_PS2;

	WREG32(RADEON_TV_DAC_CNTL, tmp);

	tmp = RADEON_DAC2_FORCE_BLANK_OFF_EN |
		RADEON_DAC2_FORCE_DATA_EN;

	if (color)
		tmp |= RADEON_DAC_FORCE_DATA_SEL_RGB;
	else
		tmp |= RADEON_DAC_FORCE_DATA_SEL_G;

	if (ASIC_IS_R300(rdev))
		tmp |= (0x1b6 << RADEON_DAC_FORCE_DATA_SHIFT);
	else
		tmp |= (0x180 << RADEON_DAC_FORCE_DATA_SHIFT);

	WREG32(RADEON_DAC_EXT_CNTL, tmp);

	tmp = dac_cntl2 | RADEON_DAC2_DAC2_CLK_SEL | RADEON_DAC2_CMP_EN;
	WREG32(RADEON_DAC_CNTL2, tmp);

	udelay(10000);

	if (ASIC_IS_R300(rdev)) {
		if (RREG32(RADEON_DAC_CNTL2) & RADEON_DAC2_CMP_OUT_B)
			found = connector_status_connected;
	} else {
		if (RREG32(RADEON_DAC_CNTL2) & RADEON_DAC2_CMP_OUTPUT)
			found = connector_status_connected;
	}

	/* restore regs we used */
	WREG32(RADEON_DAC_CNTL2, dac_cntl2);
	WREG32(RADEON_DAC_EXT_CNTL, dac_ext_cntl);
	WREG32(RADEON_TV_DAC_CNTL, tv_dac_cntl);
	WREG32(RADEON_CRTC2_GEN_CNTL, crtc2_gen_cntl);

	if (ASIC_IS_R300(rdev)) {
		WREG32(RADEON_DISP_OUTPUT_CNTL, disp_output_cntl);
		WREG32_P(RADEON_GPIOPAD_A, gpiopad_a, ~1);
	} else {
		WREG32(RADEON_DISP_HW_DEBUG, disp_hw_debug);
	}
	WREG32_PLL(RADEON_PIXCLKS_CNTL, pixclks_cntl);

	return found;

}

static const struct drm_encoder_helper_funcs radeon_legacy_tv_dac_helper_funcs = {
	.dpms = radeon_legacy_tv_dac_dpms,
	.mode_fixup = radeon_legacy_mode_fixup,
	.prepare = radeon_legacy_tv_dac_prepare,
	.mode_set = radeon_legacy_tv_dac_mode_set,
	.commit = radeon_legacy_tv_dac_commit,
	.detect = radeon_legacy_tv_dac_detect,
	.disable = radeon_legacy_encoder_disable,
};


static const struct drm_encoder_funcs radeon_legacy_tv_dac_enc_funcs = {
	.destroy = radeon_enc_destroy,
};


static struct radeon_encoder_int_tmds *radeon_legacy_get_tmds_info(struct radeon_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder_int_tmds *tmds = NULL;
	bool ret;

	tmds = kzalloc(sizeof(struct radeon_encoder_int_tmds), GFP_KERNEL);

	if (!tmds)
		return NULL;

	if (rdev->is_atom_bios)
		ret = radeon_atombios_get_tmds_info(encoder, tmds);
	else
		ret = radeon_legacy_get_tmds_info_from_combios(encoder, tmds);

	if (ret == false)
		radeon_legacy_get_tmds_info_from_table(encoder, tmds);

	return tmds;
}

static struct radeon_encoder_ext_tmds *radeon_legacy_get_ext_tmds_info(struct radeon_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder_ext_tmds *tmds = NULL;
	bool ret;

	if (rdev->is_atom_bios)
		return NULL;

	tmds = kzalloc(sizeof(struct radeon_encoder_ext_tmds), GFP_KERNEL);

	if (!tmds)
		return NULL;

	ret = radeon_legacy_get_ext_tmds_info_from_combios(encoder, tmds);

	if (ret == false)
		radeon_legacy_get_ext_tmds_info_from_table(encoder, tmds);

	return tmds;
}

void
radeon_add_legacy_encoder(struct drm_device *dev, uint32_t encoder_enum, uint32_t supported_device)
{
	struct radeon_device *rdev = dev->dev_private;
	struct drm_encoder *encoder;
	struct radeon_encoder *radeon_encoder;

	/* see if we already added it */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		radeon_encoder = to_radeon_encoder(encoder);
		if (radeon_encoder->encoder_enum == encoder_enum) {
			radeon_encoder->devices |= supported_device;
			return;
		}

	}

	/* add a new one */
	radeon_encoder = kzalloc(sizeof(struct radeon_encoder), GFP_KERNEL);
	if (!radeon_encoder)
		return;

	encoder = &radeon_encoder->base;
	if (rdev->flags & RADEON_SINGLE_CRTC)
		encoder->possible_crtcs = 0x1;
	else
		encoder->possible_crtcs = 0x3;

	radeon_encoder->enc_priv = NULL;

	radeon_encoder->encoder_enum = encoder_enum;
	radeon_encoder->encoder_id = (encoder_enum & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
	radeon_encoder->devices = supported_device;
	radeon_encoder->rmx_type = RMX_OFF;

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_LVDS:
		encoder->possible_crtcs = 0x1;
		drm_encoder_init(dev, encoder, &radeon_legacy_lvds_enc_funcs, DRM_MODE_ENCODER_LVDS);
		drm_encoder_helper_add(encoder, &radeon_legacy_lvds_helper_funcs);
		if (rdev->is_atom_bios)
			radeon_encoder->enc_priv = radeon_atombios_get_lvds_info(radeon_encoder);
		else
			radeon_encoder->enc_priv = radeon_combios_get_lvds_info(radeon_encoder);
		radeon_encoder->rmx_type = RMX_FULL;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
		drm_encoder_init(dev, encoder, &radeon_legacy_tmds_int_enc_funcs, DRM_MODE_ENCODER_TMDS);
		drm_encoder_helper_add(encoder, &radeon_legacy_tmds_int_helper_funcs);
		radeon_encoder->enc_priv = radeon_legacy_get_tmds_info(radeon_encoder);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC1:
		drm_encoder_init(dev, encoder, &radeon_legacy_primary_dac_enc_funcs, DRM_MODE_ENCODER_DAC);
		drm_encoder_helper_add(encoder, &radeon_legacy_primary_dac_helper_funcs);
		if (rdev->is_atom_bios)
			radeon_encoder->enc_priv = radeon_atombios_get_primary_dac_info(radeon_encoder);
		else
			radeon_encoder->enc_priv = radeon_combios_get_primary_dac_info(radeon_encoder);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC2:
		drm_encoder_init(dev, encoder, &radeon_legacy_tv_dac_enc_funcs, DRM_MODE_ENCODER_TVDAC);
		drm_encoder_helper_add(encoder, &radeon_legacy_tv_dac_helper_funcs);
		if (rdev->is_atom_bios)
			radeon_encoder->enc_priv = radeon_atombios_get_tv_dac_info(radeon_encoder);
		else
			radeon_encoder->enc_priv = radeon_combios_get_tv_dac_info(radeon_encoder);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DVO1:
		drm_encoder_init(dev, encoder, &radeon_legacy_tmds_ext_enc_funcs, DRM_MODE_ENCODER_TMDS);
		drm_encoder_helper_add(encoder, &radeon_legacy_tmds_ext_helper_funcs);
		if (!rdev->is_atom_bios)
			radeon_encoder->enc_priv = radeon_legacy_get_ext_tmds_info(radeon_encoder);
		break;
	}
}
