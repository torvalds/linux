/*
 * Copyright 2007-11 Advanced Micro Devices, Inc.
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
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/radeon_drm.h>
#include "radeon.h"
#include "radeon_audio.h"
#include "atom.h"
#include <linux/backlight.h>

extern int atom_debug;

static u8
radeon_atom_get_backlight_level_from_reg(struct radeon_device *rdev)
{
	u8 backlight_level;
	u32 bios_2_scratch;

	if (rdev->family >= CHIP_R600)
		bios_2_scratch = RREG32(R600_BIOS_2_SCRATCH);
	else
		bios_2_scratch = RREG32(RADEON_BIOS_2_SCRATCH);

	backlight_level = ((bios_2_scratch & ATOM_S2_CURRENT_BL_LEVEL_MASK) >>
			   ATOM_S2_CURRENT_BL_LEVEL_SHIFT);

	return backlight_level;
}

static void
radeon_atom_set_backlight_level_to_reg(struct radeon_device *rdev,
				       u8 backlight_level)
{
	u32 bios_2_scratch;

	if (rdev->family >= CHIP_R600)
		bios_2_scratch = RREG32(R600_BIOS_2_SCRATCH);
	else
		bios_2_scratch = RREG32(RADEON_BIOS_2_SCRATCH);

	bios_2_scratch &= ~ATOM_S2_CURRENT_BL_LEVEL_MASK;
	bios_2_scratch |= ((backlight_level << ATOM_S2_CURRENT_BL_LEVEL_SHIFT) &
			   ATOM_S2_CURRENT_BL_LEVEL_MASK);

	if (rdev->family >= CHIP_R600)
		WREG32(R600_BIOS_2_SCRATCH, bios_2_scratch);
	else
		WREG32(RADEON_BIOS_2_SCRATCH, bios_2_scratch);
}

u8
atombios_get_backlight_level(struct radeon_encoder *radeon_encoder)
{
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;

	if (!(rdev->mode_info.firmware_flags & ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU))
		return 0;

	return radeon_atom_get_backlight_level_from_reg(rdev);
}

void
atombios_set_backlight_level(struct radeon_encoder *radeon_encoder, u8 level)
{
	struct drm_encoder *encoder = &radeon_encoder->base;
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder_atom_dig *dig;
	DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION args;
	int index;

	if (!(rdev->mode_info.firmware_flags & ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU))
		return;

	if ((radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) &&
	    radeon_encoder->enc_priv) {
		dig = radeon_encoder->enc_priv;
		dig->backlight_level = level;
		radeon_atom_set_backlight_level_to_reg(rdev, dig->backlight_level);

		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_LVDS:
		case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
			index = GetIndexIntoMasterTable(COMMAND, LCD1OutputControl);
			if (dig->backlight_level == 0) {
				args.ucAction = ATOM_LCD_BLOFF;
				atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
			} else {
				args.ucAction = ATOM_LCD_BL_BRIGHTNESS_CONTROL;
				atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
				args.ucAction = ATOM_LCD_BLON;
				atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
			}
			break;
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
			if (dig->backlight_level == 0)
				atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_LCD_BLOFF, 0, 0);
			else {
				atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_BL_BRIGHTNESS_CONTROL, 0, 0);
				atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_LCD_BLON, 0, 0);
			}
			break;
		default:
			break;
		}
	}
}

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) || defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

static u8 radeon_atom_bl_level(struct backlight_device *bd)
{
	u8 level;

	/* Convert brightness to hardware level */
	if (bd->props.brightness < 0)
		level = 0;
	else if (bd->props.brightness > RADEON_MAX_BL_LEVEL)
		level = RADEON_MAX_BL_LEVEL;
	else
		level = bd->props.brightness;

	return level;
}

static int radeon_atom_backlight_update_status(struct backlight_device *bd)
{
	struct radeon_backlight_privdata *pdata = bl_get_data(bd);
	struct radeon_encoder *radeon_encoder = pdata->encoder;

	atombios_set_backlight_level(radeon_encoder, radeon_atom_bl_level(bd));

	return 0;
}

static int radeon_atom_backlight_get_brightness(struct backlight_device *bd)
{
	struct radeon_backlight_privdata *pdata = bl_get_data(bd);
	struct radeon_encoder *radeon_encoder = pdata->encoder;
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;

	return radeon_atom_get_backlight_level_from_reg(rdev);
}

static const struct backlight_ops radeon_atom_backlight_ops = {
	.get_brightness = radeon_atom_backlight_get_brightness,
	.update_status	= radeon_atom_backlight_update_status,
};

void radeon_atom_backlight_init(struct radeon_encoder *radeon_encoder,
				struct drm_connector *drm_connector)
{
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct backlight_device *bd;
	struct backlight_properties props;
	struct radeon_backlight_privdata *pdata;
	struct radeon_encoder_atom_dig *dig;
	char bl_name[16];

	/* Mac laptops with multiple GPUs use the gmux driver for backlight
	 * so don't register a backlight device
	 */
	if ((rdev->pdev->subsystem_vendor == PCI_VENDOR_ID_APPLE) &&
	    (rdev->pdev->device == 0x6741))
		return;

	if (!radeon_encoder->enc_priv)
		return;

	if (!rdev->is_atom_bios)
		return;

	if (!(rdev->mode_info.firmware_flags & ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU))
		return;

	pdata = kmalloc(sizeof(struct radeon_backlight_privdata), GFP_KERNEL);
	if (!pdata) {
		DRM_ERROR("Memory allocation failed\n");
		goto error;
	}

	memset(&props, 0, sizeof(props));
	props.max_brightness = RADEON_MAX_BL_LEVEL;
	props.type = BACKLIGHT_RAW;
	snprintf(bl_name, sizeof(bl_name),
		 "radeon_bl%d", dev->primary->index);
	bd = backlight_device_register(bl_name, drm_connector->kdev,
				       pdata, &radeon_atom_backlight_ops, &props);
	if (IS_ERR(bd)) {
		DRM_ERROR("Backlight registration failed\n");
		goto error;
	}

	pdata->encoder = radeon_encoder;

	dig = radeon_encoder->enc_priv;
	dig->bl_dev = bd;

	bd->props.brightness = radeon_atom_backlight_get_brightness(bd);
	/* Set a reasonable default here if the level is 0 otherwise
	 * fbdev will attempt to turn the backlight on after console
	 * unblanking and it will try and restore 0 which turns the backlight
	 * off again.
	 */
	if (bd->props.brightness == 0)
		bd->props.brightness = RADEON_MAX_BL_LEVEL;
	bd->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(bd);

	DRM_INFO("radeon atom DIG backlight initialized\n");
	rdev->mode_info.bl_encoder = radeon_encoder;

	return;

error:
	kfree(pdata);
	return;
}

static void radeon_atom_backlight_exit(struct radeon_encoder *radeon_encoder)
{
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct backlight_device *bd = NULL;
	struct radeon_encoder_atom_dig *dig;

	if (!radeon_encoder->enc_priv)
		return;

	if (!rdev->is_atom_bios)
		return;

	if (!(rdev->mode_info.firmware_flags & ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU))
		return;

	dig = radeon_encoder->enc_priv;
	bd = dig->bl_dev;
	dig->bl_dev = NULL;

	if (bd) {
		struct radeon_legacy_backlight_privdata *pdata;

		pdata = bl_get_data(bd);
		backlight_device_unregister(bd);
		kfree(pdata);

		DRM_INFO("radeon atom LVDS backlight unloaded\n");
	}
}

#else /* !CONFIG_BACKLIGHT_CLASS_DEVICE */

void radeon_atom_backlight_init(struct radeon_encoder *encoder)
{
}

static void radeon_atom_backlight_exit(struct radeon_encoder *encoder)
{
}

#endif

/* evil but including atombios.h is much worse */
bool radeon_atom_get_tv_timings(struct radeon_device *rdev, int index,
				struct drm_display_mode *mode);

static bool radeon_atom_mode_fixup(struct drm_encoder *encoder,
				   const struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;

	/* set the active encoder to connector routing */
	radeon_encoder_set_active_device(encoder);
	drm_mode_set_crtcinfo(adjusted_mode, 0);

	/* hw bug */
	if ((mode->flags & DRM_MODE_FLAG_INTERLACE)
	    && (mode->crtc_vsync_start < (mode->crtc_vdisplay + 2)))
		adjusted_mode->crtc_vsync_start = adjusted_mode->crtc_vdisplay + 2;

	/* get the native mode for scaling */
	if (radeon_encoder->active_device & (ATOM_DEVICE_LCD_SUPPORT)) {
		radeon_panel_mode_fixup(encoder, adjusted_mode);
	} else if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT)) {
		struct radeon_encoder_atom_dac *tv_dac = radeon_encoder->enc_priv;
		if (tv_dac) {
			if (tv_dac->tv_std == TV_STD_NTSC ||
			    tv_dac->tv_std == TV_STD_NTSC_J ||
			    tv_dac->tv_std == TV_STD_PAL_M)
				radeon_atom_get_tv_timings(rdev, 0, adjusted_mode);
			else
				radeon_atom_get_tv_timings(rdev, 1, adjusted_mode);
		}
	} else if (radeon_encoder->rmx_type != RMX_OFF) {
		radeon_panel_mode_fixup(encoder, adjusted_mode);
	}

	if (ASIC_IS_DCE3(rdev) &&
	    ((radeon_encoder->active_device & (ATOM_DEVICE_DFP_SUPPORT | ATOM_DEVICE_LCD_SUPPORT)) ||
	     (radeon_encoder_get_dp_bridge_encoder_id(encoder) != ENCODER_OBJECT_ID_NONE))) {
		struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
		radeon_dp_set_link_config(connector, adjusted_mode);
	}

	return true;
}

static void
atombios_dac_setup(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	DAC_ENCODER_CONTROL_PS_ALLOCATION args;
	int index = 0;
	struct radeon_encoder_atom_dac *dac_info = radeon_encoder->enc_priv;

	memset(&args, 0, sizeof(args));

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
		index = GetIndexIntoMasterTable(COMMAND, DAC1EncoderControl);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		index = GetIndexIntoMasterTable(COMMAND, DAC2EncoderControl);
		break;
	}

	args.ucAction = action;

	if (radeon_encoder->active_device & (ATOM_DEVICE_CRT_SUPPORT))
		args.ucDacStandard = ATOM_DAC1_PS2;
	else if (radeon_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
		args.ucDacStandard = ATOM_DAC1_CV;
	else {
		switch (dac_info->tv_std) {
		case TV_STD_PAL:
		case TV_STD_PAL_M:
		case TV_STD_SCART_PAL:
		case TV_STD_SECAM:
		case TV_STD_PAL_CN:
			args.ucDacStandard = ATOM_DAC1_PAL;
			break;
		case TV_STD_NTSC:
		case TV_STD_NTSC_J:
		case TV_STD_PAL_60:
		default:
			args.ucDacStandard = ATOM_DAC1_NTSC;
			break;
		}
	}
	args.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

}

static void
atombios_tv_setup(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	TV_ENCODER_CONTROL_PS_ALLOCATION args;
	int index = 0;
	struct radeon_encoder_atom_dac *dac_info = radeon_encoder->enc_priv;

	memset(&args, 0, sizeof(args));

	index = GetIndexIntoMasterTable(COMMAND, TVEncoderControl);

	args.sTVEncoder.ucAction = action;

	if (radeon_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
		args.sTVEncoder.ucTvStandard = ATOM_TV_CV;
	else {
		switch (dac_info->tv_std) {
		case TV_STD_NTSC:
			args.sTVEncoder.ucTvStandard = ATOM_TV_NTSC;
			break;
		case TV_STD_PAL:
			args.sTVEncoder.ucTvStandard = ATOM_TV_PAL;
			break;
		case TV_STD_PAL_M:
			args.sTVEncoder.ucTvStandard = ATOM_TV_PALM;
			break;
		case TV_STD_PAL_60:
			args.sTVEncoder.ucTvStandard = ATOM_TV_PAL60;
			break;
		case TV_STD_NTSC_J:
			args.sTVEncoder.ucTvStandard = ATOM_TV_NTSCJ;
			break;
		case TV_STD_SCART_PAL:
			args.sTVEncoder.ucTvStandard = ATOM_TV_PAL; /* ??? */
			break;
		case TV_STD_SECAM:
			args.sTVEncoder.ucTvStandard = ATOM_TV_SECAM;
			break;
		case TV_STD_PAL_CN:
			args.sTVEncoder.ucTvStandard = ATOM_TV_PALCN;
			break;
		default:
			args.sTVEncoder.ucTvStandard = ATOM_TV_NTSC;
			break;
		}
	}

	args.sTVEncoder.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

}

static u8 radeon_atom_get_bpc(struct drm_encoder *encoder)
{
	int bpc = 8;

	if (encoder->crtc) {
		struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
		bpc = radeon_crtc->bpc;
	}

	switch (bpc) {
	case 0:
		return PANEL_BPC_UNDEFINE;
	case 6:
		return PANEL_6BIT_PER_COLOR;
	case 8:
	default:
		return PANEL_8BIT_PER_COLOR;
	case 10:
		return PANEL_10BIT_PER_COLOR;
	case 12:
		return PANEL_12BIT_PER_COLOR;
	case 16:
		return PANEL_16BIT_PER_COLOR;
	}
}

union dvo_encoder_control {
	ENABLE_EXTERNAL_TMDS_ENCODER_PS_ALLOCATION ext_tmds;
	DVO_ENCODER_CONTROL_PS_ALLOCATION dvo;
	DVO_ENCODER_CONTROL_PS_ALLOCATION_V3 dvo_v3;
	DVO_ENCODER_CONTROL_PS_ALLOCATION_V1_4 dvo_v4;
};

void
atombios_dvo_setup(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	union dvo_encoder_control args;
	int index = GetIndexIntoMasterTable(COMMAND, DVOEncoderControl);
	uint8_t frev, crev;

	memset(&args, 0, sizeof(args));

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return;

	/* some R4xx chips have the wrong frev */
	if (rdev->family <= CHIP_RV410)
		frev = 1;

	switch (frev) {
	case 1:
		switch (crev) {
		case 1:
			/* R4xx, R5xx */
			args.ext_tmds.sXTmdsEncoder.ucEnable = action;

			if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
				args.ext_tmds.sXTmdsEncoder.ucMisc |= PANEL_ENCODER_MISC_DUAL;

			args.ext_tmds.sXTmdsEncoder.ucMisc |= ATOM_PANEL_MISC_888RGB;
			break;
		case 2:
			/* RS600/690/740 */
			args.dvo.sDVOEncoder.ucAction = action;
			args.dvo.sDVOEncoder.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			/* DFP1, CRT1, TV1 depending on the type of port */
			args.dvo.sDVOEncoder.ucDeviceType = ATOM_DEVICE_DFP1_INDEX;

			if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
				args.dvo.sDVOEncoder.usDevAttr.sDigAttrib.ucAttribute |= PANEL_ENCODER_MISC_DUAL;
			break;
		case 3:
			/* R6xx */
			args.dvo_v3.ucAction = action;
			args.dvo_v3.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			args.dvo_v3.ucDVOConfig = 0; /* XXX */
			break;
		case 4:
			/* DCE8 */
			args.dvo_v4.ucAction = action;
			args.dvo_v4.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			args.dvo_v4.ucDVOConfig = 0; /* XXX */
			args.dvo_v4.ucBitPerColor = radeon_atom_get_bpc(encoder);
			break;
		default:
			DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
			break;
		}
		break;
	default:
		DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
		break;
	}

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

union lvds_encoder_control {
	LVDS_ENCODER_CONTROL_PS_ALLOCATION    v1;
	LVDS_ENCODER_CONTROL_PS_ALLOCATION_V2 v2;
};

void
atombios_digital_setup(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	union lvds_encoder_control args;
	int index = 0;
	int hdmi_detected = 0;
	uint8_t frev, crev;

	if (!dig)
		return;

	if (atombios_get_encoder_mode(encoder) == ATOM_ENCODER_MODE_HDMI)
		hdmi_detected = 1;

	memset(&args, 0, sizeof(args));

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_LVDS:
		index = GetIndexIntoMasterTable(COMMAND, LVDSEncoderControl);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
		index = GetIndexIntoMasterTable(COMMAND, TMDS1EncoderControl);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
		if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
			index = GetIndexIntoMasterTable(COMMAND, LVDSEncoderControl);
		else
			index = GetIndexIntoMasterTable(COMMAND, TMDS2EncoderControl);
		break;
	}

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return;

	switch (frev) {
	case 1:
	case 2:
		switch (crev) {
		case 1:
			args.v1.ucMisc = 0;
			args.v1.ucAction = action;
			if (hdmi_detected)
				args.v1.ucMisc |= PANEL_ENCODER_MISC_HDMI_TYPE;
			args.v1.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
				if (dig->lcd_misc & ATOM_PANEL_MISC_DUAL)
					args.v1.ucMisc |= PANEL_ENCODER_MISC_DUAL;
				if (dig->lcd_misc & ATOM_PANEL_MISC_888RGB)
					args.v1.ucMisc |= ATOM_PANEL_MISC_888RGB;
			} else {
				if (dig->linkb)
					args.v1.ucMisc |= PANEL_ENCODER_MISC_TMDS_LINKB;
				if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
					args.v1.ucMisc |= PANEL_ENCODER_MISC_DUAL;
				/*if (pScrn->rgbBits == 8) */
				args.v1.ucMisc |= ATOM_PANEL_MISC_888RGB;
			}
			break;
		case 2:
		case 3:
			args.v2.ucMisc = 0;
			args.v2.ucAction = action;
			if (crev == 3) {
				if (dig->coherent_mode)
					args.v2.ucMisc |= PANEL_ENCODER_MISC_COHERENT;
			}
			if (hdmi_detected)
				args.v2.ucMisc |= PANEL_ENCODER_MISC_HDMI_TYPE;
			args.v2.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			args.v2.ucTruncate = 0;
			args.v2.ucSpatial = 0;
			args.v2.ucTemporal = 0;
			args.v2.ucFRC = 0;
			if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
				if (dig->lcd_misc & ATOM_PANEL_MISC_DUAL)
					args.v2.ucMisc |= PANEL_ENCODER_MISC_DUAL;
				if (dig->lcd_misc & ATOM_PANEL_MISC_SPATIAL) {
					args.v2.ucSpatial = PANEL_ENCODER_SPATIAL_DITHER_EN;
					if (dig->lcd_misc & ATOM_PANEL_MISC_888RGB)
						args.v2.ucSpatial |= PANEL_ENCODER_SPATIAL_DITHER_DEPTH;
				}
				if (dig->lcd_misc & ATOM_PANEL_MISC_TEMPORAL) {
					args.v2.ucTemporal = PANEL_ENCODER_TEMPORAL_DITHER_EN;
					if (dig->lcd_misc & ATOM_PANEL_MISC_888RGB)
						args.v2.ucTemporal |= PANEL_ENCODER_TEMPORAL_DITHER_DEPTH;
					if (((dig->lcd_misc >> ATOM_PANEL_MISC_GREY_LEVEL_SHIFT) & 0x3) == 2)
						args.v2.ucTemporal |= PANEL_ENCODER_TEMPORAL_LEVEL_4;
				}
			} else {
				if (dig->linkb)
					args.v2.ucMisc |= PANEL_ENCODER_MISC_TMDS_LINKB;
				if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
					args.v2.ucMisc |= PANEL_ENCODER_MISC_DUAL;
			}
			break;
		default:
			DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
			break;
		}
		break;
	default:
		DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
		break;
	}

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

int
atombios_get_encoder_mode(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	struct radeon_connector_atom_dig *dig_connector;
	struct radeon_encoder_atom_dig *dig_enc;

	if (radeon_encoder_is_digital(encoder)) {
		dig_enc = radeon_encoder->enc_priv;
		if (dig_enc->active_mst_links)
			return ATOM_ENCODER_MODE_DP_MST;
	}
	if (radeon_encoder->is_mst_encoder || radeon_encoder->offset)
		return ATOM_ENCODER_MODE_DP_MST;
	/* dp bridges are always DP */
	if (radeon_encoder_get_dp_bridge_encoder_id(encoder) != ENCODER_OBJECT_ID_NONE)
		return ATOM_ENCODER_MODE_DP;

	/* DVO is always DVO */
	if ((radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_DVO1) ||
	    (radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1))
		return ATOM_ENCODER_MODE_DVO;

	connector = radeon_get_connector_for_encoder(encoder);
	/* if we don't have an active device yet, just use one of
	 * the connectors tied to the encoder.
	 */
	if (!connector)
		connector = radeon_get_connector_for_encoder_init(encoder);
	radeon_connector = to_radeon_connector(connector);

	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_HDMIB: /* HDMI-B is basically DL-DVI; analog works fine */
		if (radeon_audio != 0) {
			if (radeon_connector->use_digital &&
			    (radeon_connector->audio == RADEON_AUDIO_ENABLE))
				return ATOM_ENCODER_MODE_HDMI;
			else if (drm_detect_hdmi_monitor(radeon_connector_edid(connector)) &&
				 (radeon_connector->audio == RADEON_AUDIO_AUTO))
				return ATOM_ENCODER_MODE_HDMI;
			else if (radeon_connector->use_digital)
				return ATOM_ENCODER_MODE_DVI;
			else
				return ATOM_ENCODER_MODE_CRT;
		} else if (radeon_connector->use_digital) {
			return ATOM_ENCODER_MODE_DVI;
		} else {
			return ATOM_ENCODER_MODE_CRT;
		}
		break;
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_HDMIA:
	default:
		if (radeon_audio != 0) {
			if (radeon_connector->audio == RADEON_AUDIO_ENABLE)
				return ATOM_ENCODER_MODE_HDMI;
			else if (drm_detect_hdmi_monitor(radeon_connector_edid(connector)) &&
				 (radeon_connector->audio == RADEON_AUDIO_AUTO))
				return ATOM_ENCODER_MODE_HDMI;
			else
				return ATOM_ENCODER_MODE_DVI;
		} else {
			return ATOM_ENCODER_MODE_DVI;
		}
		break;
	case DRM_MODE_CONNECTOR_LVDS:
		return ATOM_ENCODER_MODE_LVDS;
		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
		dig_connector = radeon_connector->con_priv;
		if ((dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_DISPLAYPORT) ||
		    (dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_eDP)) {
			if (radeon_audio != 0 &&
			    drm_detect_monitor_audio(radeon_connector_edid(connector)) &&
			    ASIC_IS_DCE4(rdev) && !ASIC_IS_DCE5(rdev))
				return ATOM_ENCODER_MODE_DP_AUDIO;
			return ATOM_ENCODER_MODE_DP;
		} else if (radeon_audio != 0) {
			if (radeon_connector->audio == RADEON_AUDIO_ENABLE)
				return ATOM_ENCODER_MODE_HDMI;
			else if (drm_detect_hdmi_monitor(radeon_connector_edid(connector)) &&
				 (radeon_connector->audio == RADEON_AUDIO_AUTO))
				return ATOM_ENCODER_MODE_HDMI;
			else
				return ATOM_ENCODER_MODE_DVI;
		} else {
			return ATOM_ENCODER_MODE_DVI;
		}
		break;
	case DRM_MODE_CONNECTOR_eDP:
		if (radeon_audio != 0 &&
		    drm_detect_monitor_audio(radeon_connector_edid(connector)) &&
		    ASIC_IS_DCE4(rdev) && !ASIC_IS_DCE5(rdev))
			return ATOM_ENCODER_MODE_DP_AUDIO;
		return ATOM_ENCODER_MODE_DP;
	case DRM_MODE_CONNECTOR_DVIA:
	case DRM_MODE_CONNECTOR_VGA:
		return ATOM_ENCODER_MODE_CRT;
		break;
	case DRM_MODE_CONNECTOR_Composite:
	case DRM_MODE_CONNECTOR_SVIDEO:
	case DRM_MODE_CONNECTOR_9PinDIN:
		/* fix me */
		return ATOM_ENCODER_MODE_TV;
		/*return ATOM_ENCODER_MODE_CV;*/
		break;
	}
}

/*
 * DIG Encoder/Transmitter Setup
 *
 * DCE 3.0/3.1
 * - 2 DIG transmitter blocks. UNIPHY (links A and B) and LVTMA.
 * Supports up to 3 digital outputs
 * - 2 DIG encoder blocks.
 * DIG1 can drive UNIPHY link A or link B
 * DIG2 can drive UNIPHY link B or LVTMA
 *
 * DCE 3.2
 * - 3 DIG transmitter blocks. UNIPHY0/1/2 (links A and B).
 * Supports up to 5 digital outputs
 * - 2 DIG encoder blocks.
 * DIG1/2 can drive UNIPHY0/1/2 link A or link B
 *
 * DCE 4.0/5.0/6.0
 * - 3 DIG transmitter blocks UNIPHY0/1/2 (links A and B).
 * Supports up to 6 digital outputs
 * - 6 DIG encoder blocks.
 * - DIG to PHY mapping is hardcoded
 * DIG1 drives UNIPHY0 link A, A+B
 * DIG2 drives UNIPHY0 link B
 * DIG3 drives UNIPHY1 link A, A+B
 * DIG4 drives UNIPHY1 link B
 * DIG5 drives UNIPHY2 link A, A+B
 * DIG6 drives UNIPHY2 link B
 *
 * DCE 4.1
 * - 3 DIG transmitter blocks UNIPHY0/1/2 (links A and B).
 * Supports up to 6 digital outputs
 * - 2 DIG encoder blocks.
 * llano
 * DIG1/2 can drive UNIPHY0/1/2 link A or link B
 * ontario
 * DIG1 drives UNIPHY0/1/2 link A
 * DIG2 drives UNIPHY0/1/2 link B
 *
 * Routing
 * crtc -> dig encoder -> UNIPHY/LVTMA (1 or 2 links)
 * Examples:
 * crtc0 -> dig2 -> LVTMA   links A+B -> TMDS/HDMI
 * crtc1 -> dig1 -> UNIPHY0 link  B   -> DP
 * crtc0 -> dig1 -> UNIPHY2 link  A   -> LVDS
 * crtc1 -> dig2 -> UNIPHY1 link  B+A -> TMDS/HDMI
 */

union dig_encoder_control {
	DIG_ENCODER_CONTROL_PS_ALLOCATION v1;
	DIG_ENCODER_CONTROL_PARAMETERS_V2 v2;
	DIG_ENCODER_CONTROL_PARAMETERS_V3 v3;
	DIG_ENCODER_CONTROL_PARAMETERS_V4 v4;
};

void
atombios_dig_encoder_setup2(struct drm_encoder *encoder, int action, int panel_mode, int enc_override)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
	union dig_encoder_control args;
	int index = 0;
	uint8_t frev, crev;
	int dp_clock = 0;
	int dp_lane_count = 0;
	int hpd_id = RADEON_HPD_NONE;

	if (connector) {
		struct radeon_connector *radeon_connector = to_radeon_connector(connector);
		struct radeon_connector_atom_dig *dig_connector =
			radeon_connector->con_priv;

		dp_clock = dig_connector->dp_clock;
		dp_lane_count = dig_connector->dp_lane_count;
		hpd_id = radeon_connector->hpd.hpd;
	}

	/* no dig encoder assigned */
	if (dig->dig_encoder == -1)
		return;

	memset(&args, 0, sizeof(args));

	if (ASIC_IS_DCE4(rdev))
		index = GetIndexIntoMasterTable(COMMAND, DIGxEncoderControl);
	else {
		if (dig->dig_encoder)
			index = GetIndexIntoMasterTable(COMMAND, DIG2EncoderControl);
		else
			index = GetIndexIntoMasterTable(COMMAND, DIG1EncoderControl);
	}

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return;

	switch (frev) {
	case 1:
		switch (crev) {
		case 1:
			args.v1.ucAction = action;
			args.v1.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			if (action == ATOM_ENCODER_CMD_SETUP_PANEL_MODE)
				args.v3.ucPanelMode = panel_mode;
			else
				args.v1.ucEncoderMode = atombios_get_encoder_mode(encoder);

			if (ENCODER_MODE_IS_DP(args.v1.ucEncoderMode))
				args.v1.ucLaneNum = dp_lane_count;
			else if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
				args.v1.ucLaneNum = 8;
			else
				args.v1.ucLaneNum = 4;

			if (ENCODER_MODE_IS_DP(args.v1.ucEncoderMode) && (dp_clock == 270000))
				args.v1.ucConfig |= ATOM_ENCODER_CONFIG_DPLINKRATE_2_70GHZ;
			switch (radeon_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
				args.v1.ucConfig = ATOM_ENCODER_CONFIG_V2_TRANSMITTER1;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
				args.v1.ucConfig = ATOM_ENCODER_CONFIG_V2_TRANSMITTER2;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
				args.v1.ucConfig = ATOM_ENCODER_CONFIG_V2_TRANSMITTER3;
				break;
			}
			if (dig->linkb)
				args.v1.ucConfig |= ATOM_ENCODER_CONFIG_LINKB;
			else
				args.v1.ucConfig |= ATOM_ENCODER_CONFIG_LINKA;
			break;
		case 2:
		case 3:
			args.v3.ucAction = action;
			args.v3.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			if (action == ATOM_ENCODER_CMD_SETUP_PANEL_MODE)
				args.v3.ucPanelMode = panel_mode;
			else
				args.v3.ucEncoderMode = atombios_get_encoder_mode(encoder);

			if (ENCODER_MODE_IS_DP(args.v3.ucEncoderMode))
				args.v3.ucLaneNum = dp_lane_count;
			else if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
				args.v3.ucLaneNum = 8;
			else
				args.v3.ucLaneNum = 4;

			if (ENCODER_MODE_IS_DP(args.v3.ucEncoderMode) && (dp_clock == 270000))
				args.v1.ucConfig |= ATOM_ENCODER_CONFIG_V3_DPLINKRATE_2_70GHZ;
			if (enc_override != -1)
				args.v3.acConfig.ucDigSel = enc_override;
			else
				args.v3.acConfig.ucDigSel = dig->dig_encoder;
			args.v3.ucBitPerColor = radeon_atom_get_bpc(encoder);
			break;
		case 4:
			args.v4.ucAction = action;
			args.v4.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			if (action == ATOM_ENCODER_CMD_SETUP_PANEL_MODE)
				args.v4.ucPanelMode = panel_mode;
			else
				args.v4.ucEncoderMode = atombios_get_encoder_mode(encoder);

			if (ENCODER_MODE_IS_DP(args.v4.ucEncoderMode))
				args.v4.ucLaneNum = dp_lane_count;
			else if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
				args.v4.ucLaneNum = 8;
			else
				args.v4.ucLaneNum = 4;

			if (ENCODER_MODE_IS_DP(args.v4.ucEncoderMode)) {
				if (dp_clock == 540000)
					args.v1.ucConfig |= ATOM_ENCODER_CONFIG_V4_DPLINKRATE_5_40GHZ;
				else if (dp_clock == 324000)
					args.v1.ucConfig |= ATOM_ENCODER_CONFIG_V4_DPLINKRATE_3_24GHZ;
				else if (dp_clock == 270000)
					args.v1.ucConfig |= ATOM_ENCODER_CONFIG_V4_DPLINKRATE_2_70GHZ;
				else
					args.v1.ucConfig |= ATOM_ENCODER_CONFIG_V4_DPLINKRATE_1_62GHZ;
			}

			if (enc_override != -1)
				args.v4.acConfig.ucDigSel = enc_override;
			else
				args.v4.acConfig.ucDigSel = dig->dig_encoder;
			args.v4.ucBitPerColor = radeon_atom_get_bpc(encoder);
			if (hpd_id == RADEON_HPD_NONE)
				args.v4.ucHPD_ID = 0;
			else
				args.v4.ucHPD_ID = hpd_id + 1;
			break;
		default:
			DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
			break;
		}
		break;
	default:
		DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
		break;
	}

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

}

void
atombios_dig_encoder_setup(struct drm_encoder *encoder, int action, int panel_mode)
{
	atombios_dig_encoder_setup2(encoder, action, panel_mode, -1);
}

union dig_transmitter_control {
	DIG_TRANSMITTER_CONTROL_PS_ALLOCATION v1;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V2 v2;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V3 v3;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V4 v4;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V1_5 v5;
};

void
atombios_dig_transmitter_setup2(struct drm_encoder *encoder, int action, uint8_t lane_num, uint8_t lane_set, int fe)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct drm_connector *connector;
	union dig_transmitter_control args;
	int index = 0;
	uint8_t frev, crev;
	bool is_dp = false;
	int pll_id = 0;
	int dp_clock = 0;
	int dp_lane_count = 0;
	int connector_object_id = 0;
	int igp_lane_info = 0;
	int dig_encoder = dig->dig_encoder;
	int hpd_id = RADEON_HPD_NONE;

	if (action == ATOM_TRANSMITTER_ACTION_INIT) {
		connector = radeon_get_connector_for_encoder_init(encoder);
		/* just needed to avoid bailing in the encoder check.  the encoder
		 * isn't used for init
		 */
		dig_encoder = 0;
	} else
		connector = radeon_get_connector_for_encoder(encoder);

	if (connector) {
		struct radeon_connector *radeon_connector = to_radeon_connector(connector);
		struct radeon_connector_atom_dig *dig_connector =
			radeon_connector->con_priv;

		hpd_id = radeon_connector->hpd.hpd;
		dp_clock = dig_connector->dp_clock;
		dp_lane_count = dig_connector->dp_lane_count;
		connector_object_id =
			(radeon_connector->connector_object_id & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
		igp_lane_info = dig_connector->igp_lane_info;
	}

	if (encoder->crtc) {
		struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
		pll_id = radeon_crtc->pll_id;
	}

	/* no dig encoder assigned */
	if (dig_encoder == -1)
		return;

	if (ENCODER_MODE_IS_DP(atombios_get_encoder_mode(encoder)))
		is_dp = true;

	memset(&args, 0, sizeof(args));

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
		index = GetIndexIntoMasterTable(COMMAND, DVOOutputControl);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
		index = GetIndexIntoMasterTable(COMMAND, UNIPHYTransmitterControl);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
		index = GetIndexIntoMasterTable(COMMAND, LVTMATransmitterControl);
		break;
	}

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return;

	switch (frev) {
	case 1:
		switch (crev) {
		case 1:
			args.v1.ucAction = action;
			if (action == ATOM_TRANSMITTER_ACTION_INIT) {
				args.v1.usInitInfo = cpu_to_le16(connector_object_id);
			} else if (action == ATOM_TRANSMITTER_ACTION_SETUP_VSEMPH) {
				args.v1.asMode.ucLaneSel = lane_num;
				args.v1.asMode.ucLaneSet = lane_set;
			} else {
				if (is_dp)
					args.v1.usPixelClock = cpu_to_le16(dp_clock / 10);
				else if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
					args.v1.usPixelClock = cpu_to_le16((radeon_encoder->pixel_clock / 2) / 10);
				else
					args.v1.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			}

			args.v1.ucConfig = ATOM_TRANSMITTER_CONFIG_CLKSRC_PPLL;

			if (dig_encoder)
				args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_DIG2_ENCODER;
			else
				args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_DIG1_ENCODER;

			if ((rdev->flags & RADEON_IS_IGP) &&
			    (radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_UNIPHY)) {
				if (is_dp ||
				    !radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock)) {
					if (igp_lane_info & 0x1)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_0_3;
					else if (igp_lane_info & 0x2)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_4_7;
					else if (igp_lane_info & 0x4)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_8_11;
					else if (igp_lane_info & 0x8)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_12_15;
				} else {
					if (igp_lane_info & 0x3)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_0_7;
					else if (igp_lane_info & 0xc)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_8_15;
				}
			}

			if (dig->linkb)
				args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LINKB;
			else
				args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LINKA;

			if (is_dp)
				args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_COHERENT;
			else if (radeon_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
				if (dig->coherent_mode)
					args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_COHERENT;
				if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
					args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_8LANE_LINK;
			}
			break;
		case 2:
			args.v2.ucAction = action;
			if (action == ATOM_TRANSMITTER_ACTION_INIT) {
				args.v2.usInitInfo = cpu_to_le16(connector_object_id);
			} else if (action == ATOM_TRANSMITTER_ACTION_SETUP_VSEMPH) {
				args.v2.asMode.ucLaneSel = lane_num;
				args.v2.asMode.ucLaneSet = lane_set;
			} else {
				if (is_dp)
					args.v2.usPixelClock = cpu_to_le16(dp_clock / 10);
				else if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
					args.v2.usPixelClock = cpu_to_le16((radeon_encoder->pixel_clock / 2) / 10);
				else
					args.v2.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			}

			args.v2.acConfig.ucEncoderSel = dig_encoder;
			if (dig->linkb)
				args.v2.acConfig.ucLinkSel = 1;

			switch (radeon_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
				args.v2.acConfig.ucTransmitterSel = 0;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
				args.v2.acConfig.ucTransmitterSel = 1;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
				args.v2.acConfig.ucTransmitterSel = 2;
				break;
			}

			if (is_dp) {
				args.v2.acConfig.fCoherentMode = 1;
				args.v2.acConfig.fDPConnector = 1;
			} else if (radeon_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
				if (dig->coherent_mode)
					args.v2.acConfig.fCoherentMode = 1;
				if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
					args.v2.acConfig.fDualLinkConnector = 1;
			}
			break;
		case 3:
			args.v3.ucAction = action;
			if (action == ATOM_TRANSMITTER_ACTION_INIT) {
				args.v3.usInitInfo = cpu_to_le16(connector_object_id);
			} else if (action == ATOM_TRANSMITTER_ACTION_SETUP_VSEMPH) {
				args.v3.asMode.ucLaneSel = lane_num;
				args.v3.asMode.ucLaneSet = lane_set;
			} else {
				if (is_dp)
					args.v3.usPixelClock = cpu_to_le16(dp_clock / 10);
				else if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
					args.v3.usPixelClock = cpu_to_le16((radeon_encoder->pixel_clock / 2) / 10);
				else
					args.v3.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			}

			if (is_dp)
				args.v3.ucLaneNum = dp_lane_count;
			else if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
				args.v3.ucLaneNum = 8;
			else
				args.v3.ucLaneNum = 4;

			if (dig->linkb)
				args.v3.acConfig.ucLinkSel = 1;
			if (dig_encoder & 1)
				args.v3.acConfig.ucEncoderSel = 1;

			/* Select the PLL for the PHY
			 * DP PHY should be clocked from external src if there is
			 * one.
			 */
			/* On DCE4, if there is an external clock, it generates the DP ref clock */
			if (is_dp && rdev->clock.dp_extclk)
				args.v3.acConfig.ucRefClkSource = 2; /* external src */
			else
				args.v3.acConfig.ucRefClkSource = pll_id;

			switch (radeon_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
				args.v3.acConfig.ucTransmitterSel = 0;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
				args.v3.acConfig.ucTransmitterSel = 1;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
				args.v3.acConfig.ucTransmitterSel = 2;
				break;
			}

			if (is_dp)
				args.v3.acConfig.fCoherentMode = 1; /* DP requires coherent */
			else if (radeon_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
				if (dig->coherent_mode)
					args.v3.acConfig.fCoherentMode = 1;
				if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
					args.v3.acConfig.fDualLinkConnector = 1;
			}
			break;
		case 4:
			args.v4.ucAction = action;
			if (action == ATOM_TRANSMITTER_ACTION_INIT) {
				args.v4.usInitInfo = cpu_to_le16(connector_object_id);
			} else if (action == ATOM_TRANSMITTER_ACTION_SETUP_VSEMPH) {
				args.v4.asMode.ucLaneSel = lane_num;
				args.v4.asMode.ucLaneSet = lane_set;
			} else {
				if (is_dp)
					args.v4.usPixelClock = cpu_to_le16(dp_clock / 10);
				else if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
					args.v4.usPixelClock = cpu_to_le16((radeon_encoder->pixel_clock / 2) / 10);
				else
					args.v4.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			}

			if (is_dp)
				args.v4.ucLaneNum = dp_lane_count;
			else if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
				args.v4.ucLaneNum = 8;
			else
				args.v4.ucLaneNum = 4;

			if (dig->linkb)
				args.v4.acConfig.ucLinkSel = 1;
			if (dig_encoder & 1)
				args.v4.acConfig.ucEncoderSel = 1;

			/* Select the PLL for the PHY
			 * DP PHY should be clocked from external src if there is
			 * one.
			 */
			/* On DCE5 DCPLL usually generates the DP ref clock */
			if (is_dp) {
				if (rdev->clock.dp_extclk)
					args.v4.acConfig.ucRefClkSource = ENCODER_REFCLK_SRC_EXTCLK;
				else
					args.v4.acConfig.ucRefClkSource = ENCODER_REFCLK_SRC_DCPLL;
			} else
				args.v4.acConfig.ucRefClkSource = pll_id;

			switch (radeon_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
				args.v4.acConfig.ucTransmitterSel = 0;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
				args.v4.acConfig.ucTransmitterSel = 1;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
				args.v4.acConfig.ucTransmitterSel = 2;
				break;
			}

			if (is_dp)
				args.v4.acConfig.fCoherentMode = 1; /* DP requires coherent */
			else if (radeon_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
				if (dig->coherent_mode)
					args.v4.acConfig.fCoherentMode = 1;
				if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
					args.v4.acConfig.fDualLinkConnector = 1;
			}
			break;
		case 5:
			args.v5.ucAction = action;
			if (is_dp)
				args.v5.usSymClock = cpu_to_le16(dp_clock / 10);
			else
				args.v5.usSymClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);

			switch (radeon_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
				if (dig->linkb)
					args.v5.ucPhyId = ATOM_PHY_ID_UNIPHYB;
				else
					args.v5.ucPhyId = ATOM_PHY_ID_UNIPHYA;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
				if (dig->linkb)
					args.v5.ucPhyId = ATOM_PHY_ID_UNIPHYD;
				else
					args.v5.ucPhyId = ATOM_PHY_ID_UNIPHYC;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
				if (dig->linkb)
					args.v5.ucPhyId = ATOM_PHY_ID_UNIPHYF;
				else
					args.v5.ucPhyId = ATOM_PHY_ID_UNIPHYE;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
				args.v5.ucPhyId = ATOM_PHY_ID_UNIPHYG;
				break;
			}
			if (is_dp)
				args.v5.ucLaneNum = dp_lane_count;
			else if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
				args.v5.ucLaneNum = 8;
			else
				args.v5.ucLaneNum = 4;
			args.v5.ucConnObjId = connector_object_id;
			args.v5.ucDigMode = atombios_get_encoder_mode(encoder);

			if (is_dp && rdev->clock.dp_extclk)
				args.v5.asConfig.ucPhyClkSrcId = ENCODER_REFCLK_SRC_EXTCLK;
			else
				args.v5.asConfig.ucPhyClkSrcId = pll_id;

			if (is_dp)
				args.v5.asConfig.ucCoherentMode = 1; /* DP requires coherent */
			else if (radeon_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
				if (dig->coherent_mode)
					args.v5.asConfig.ucCoherentMode = 1;
			}
			if (hpd_id == RADEON_HPD_NONE)
				args.v5.asConfig.ucHPDSel = 0;
			else
				args.v5.asConfig.ucHPDSel = hpd_id + 1;
			args.v5.ucDigEncoderSel = (fe != -1) ? (1 << fe) : (1 << dig_encoder);
			args.v5.ucDPLaneSet = lane_set;
			break;
		default:
			DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
			break;
		}
		break;
	default:
		DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
		break;
	}

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

void
atombios_dig_transmitter_setup(struct drm_encoder *encoder, int action, uint8_t lane_num, uint8_t lane_set)
{
	atombios_dig_transmitter_setup2(encoder, action, lane_num, lane_set, -1);
}

bool
atombios_set_edp_panel_power(struct drm_connector *connector, int action)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct drm_device *dev = radeon_connector->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	union dig_transmitter_control args;
	int index = GetIndexIntoMasterTable(COMMAND, UNIPHYTransmitterControl);
	uint8_t frev, crev;

	if (connector->connector_type != DRM_MODE_CONNECTOR_eDP)
		goto done;

	if (!ASIC_IS_DCE4(rdev))
		goto done;

	if ((action != ATOM_TRANSMITTER_ACTION_POWER_ON) &&
	    (action != ATOM_TRANSMITTER_ACTION_POWER_OFF))
		goto done;

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		goto done;

	memset(&args, 0, sizeof(args));

	args.v1.ucAction = action;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

	/* wait for the panel to power up */
	if (action == ATOM_TRANSMITTER_ACTION_POWER_ON) {
		int i;

		for (i = 0; i < 300; i++) {
			if (radeon_hpd_sense(rdev, radeon_connector->hpd.hpd))
				return true;
			mdelay(1);
		}
		return false;
	}
done:
	return true;
}

union external_encoder_control {
	EXTERNAL_ENCODER_CONTROL_PS_ALLOCATION v1;
	EXTERNAL_ENCODER_CONTROL_PS_ALLOCATION_V3 v3;
};

static void
atombios_external_encoder_setup(struct drm_encoder *encoder,
				struct drm_encoder *ext_encoder,
				int action)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder *ext_radeon_encoder = to_radeon_encoder(ext_encoder);
	union external_encoder_control args;
	struct drm_connector *connector;
	int index = GetIndexIntoMasterTable(COMMAND, ExternalEncoderControl);
	u8 frev, crev;
	int dp_clock = 0;
	int dp_lane_count = 0;
	int connector_object_id = 0;
	u32 ext_enum = (ext_radeon_encoder->encoder_enum & ENUM_ID_MASK) >> ENUM_ID_SHIFT;

	if (action == EXTERNAL_ENCODER_ACTION_V3_ENCODER_INIT)
		connector = radeon_get_connector_for_encoder_init(encoder);
	else
		connector = radeon_get_connector_for_encoder(encoder);

	if (connector) {
		struct radeon_connector *radeon_connector = to_radeon_connector(connector);
		struct radeon_connector_atom_dig *dig_connector =
			radeon_connector->con_priv;

		dp_clock = dig_connector->dp_clock;
		dp_lane_count = dig_connector->dp_lane_count;
		connector_object_id =
			(radeon_connector->connector_object_id & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
	}

	memset(&args, 0, sizeof(args));

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return;

	switch (frev) {
	case 1:
		/* no params on frev 1 */
		break;
	case 2:
		switch (crev) {
		case 1:
		case 2:
			args.v1.sDigEncoder.ucAction = action;
			args.v1.sDigEncoder.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			args.v1.sDigEncoder.ucEncoderMode = atombios_get_encoder_mode(encoder);

			if (ENCODER_MODE_IS_DP(args.v1.sDigEncoder.ucEncoderMode)) {
				if (dp_clock == 270000)
					args.v1.sDigEncoder.ucConfig |= ATOM_ENCODER_CONFIG_DPLINKRATE_2_70GHZ;
				args.v1.sDigEncoder.ucLaneNum = dp_lane_count;
			} else if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
				args.v1.sDigEncoder.ucLaneNum = 8;
			else
				args.v1.sDigEncoder.ucLaneNum = 4;
			break;
		case 3:
			args.v3.sExtEncoder.ucAction = action;
			if (action == EXTERNAL_ENCODER_ACTION_V3_ENCODER_INIT)
				args.v3.sExtEncoder.usConnectorId = cpu_to_le16(connector_object_id);
			else
				args.v3.sExtEncoder.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			args.v3.sExtEncoder.ucEncoderMode = atombios_get_encoder_mode(encoder);

			if (ENCODER_MODE_IS_DP(args.v3.sExtEncoder.ucEncoderMode)) {
				if (dp_clock == 270000)
					args.v3.sExtEncoder.ucConfig |= EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_2_70GHZ;
				else if (dp_clock == 540000)
					args.v3.sExtEncoder.ucConfig |= EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_5_40GHZ;
				args.v3.sExtEncoder.ucLaneNum = dp_lane_count;
			} else if (radeon_dig_monitor_is_duallink(encoder, radeon_encoder->pixel_clock))
				args.v3.sExtEncoder.ucLaneNum = 8;
			else
				args.v3.sExtEncoder.ucLaneNum = 4;
			switch (ext_enum) {
			case GRAPH_OBJECT_ENUM_ID1:
				args.v3.sExtEncoder.ucConfig |= EXTERNAL_ENCODER_CONFIG_V3_ENCODER1;
				break;
			case GRAPH_OBJECT_ENUM_ID2:
				args.v3.sExtEncoder.ucConfig |= EXTERNAL_ENCODER_CONFIG_V3_ENCODER2;
				break;
			case GRAPH_OBJECT_ENUM_ID3:
				args.v3.sExtEncoder.ucConfig |= EXTERNAL_ENCODER_CONFIG_V3_ENCODER3;
				break;
			}
			args.v3.sExtEncoder.ucBitPerColor = radeon_atom_get_bpc(encoder);
			break;
		default:
			DRM_ERROR("Unknown table version: %d, %d\n", frev, crev);
			return;
		}
		break;
	default:
		DRM_ERROR("Unknown table version: %d, %d\n", frev, crev);
		return;
	}
	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

static void
atombios_yuv_setup(struct drm_encoder *encoder, bool enable)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	ENABLE_YUV_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, EnableYUV);
	uint32_t temp, reg;

	memset(&args, 0, sizeof(args));

	if (rdev->family >= CHIP_R600)
		reg = R600_BIOS_3_SCRATCH;
	else
		reg = RADEON_BIOS_3_SCRATCH;

	/* XXX: fix up scratch reg handling */
	temp = RREG32(reg);
	if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
		WREG32(reg, (ATOM_S3_TV1_ACTIVE |
			     (radeon_crtc->crtc_id << 18)));
	else if (radeon_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
		WREG32(reg, (ATOM_S3_CV_ACTIVE | (radeon_crtc->crtc_id << 24)));
	else
		WREG32(reg, 0);

	if (enable)
		args.ucEnable = ATOM_ENABLE;
	args.ucCRTC = radeon_crtc->crtc_id;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

	WREG32(reg, temp);
}

static void
radeon_atom_encoder_dpms_avivo(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION args;
	int index = 0;

	memset(&args, 0, sizeof(args));

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
		index = GetIndexIntoMasterTable(COMMAND, TMDSAOutputControl);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_DDI:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
		index = GetIndexIntoMasterTable(COMMAND, DVOOutputControl);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_LVDS:
		index = GetIndexIntoMasterTable(COMMAND, LCD1OutputControl);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
		if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
			index = GetIndexIntoMasterTable(COMMAND, LCD1OutputControl);
		else
			index = GetIndexIntoMasterTable(COMMAND, LVTMAOutputControl);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
		if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
			index = GetIndexIntoMasterTable(COMMAND, TV1OutputControl);
		else if (radeon_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
			index = GetIndexIntoMasterTable(COMMAND, CV1OutputControl);
		else
			index = GetIndexIntoMasterTable(COMMAND, DAC1OutputControl);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
			index = GetIndexIntoMasterTable(COMMAND, TV1OutputControl);
		else if (radeon_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
			index = GetIndexIntoMasterTable(COMMAND, CV1OutputControl);
		else
			index = GetIndexIntoMasterTable(COMMAND, DAC2OutputControl);
		break;
	default:
		return;
	}

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		args.ucAction = ATOM_ENABLE;
		/* workaround for DVOOutputControl on some RS690 systems */
		if (radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_DDI) {
			u32 reg = RREG32(RADEON_BIOS_3_SCRATCH);
			WREG32(RADEON_BIOS_3_SCRATCH, reg & ~ATOM_S3_DFP2I_ACTIVE);
			atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
			WREG32(RADEON_BIOS_3_SCRATCH, reg);
		} else
			atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
		if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
			if (rdev->mode_info.bl_encoder) {
				struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;

				atombios_set_backlight_level(radeon_encoder, dig->backlight_level);
			} else {
				args.ucAction = ATOM_LCD_BLON;
				atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
			}
		}
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		args.ucAction = ATOM_DISABLE;
		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
		if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
			args.ucAction = ATOM_LCD_BLOFF;
			atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
		}
		break;
	}
}

static void
radeon_atom_encoder_dpms_dig(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_encoder *ext_encoder = radeon_get_external_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
	struct radeon_connector *radeon_connector = NULL;
	struct radeon_connector_atom_dig *radeon_dig_connector = NULL;
	bool travis_quirk = false;

	if (connector) {
		radeon_connector = to_radeon_connector(connector);
		radeon_dig_connector = radeon_connector->con_priv;
		if ((radeon_connector_encoder_get_dp_bridge_encoder_id(connector) ==
		     ENCODER_OBJECT_ID_TRAVIS) &&
		    (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) &&
		    !ASIC_IS_DCE5(rdev))
			travis_quirk = true;
	}

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		if (ASIC_IS_DCE41(rdev) || ASIC_IS_DCE5(rdev)) {
			if (!connector)
				dig->panel_mode = DP_PANEL_MODE_EXTERNAL_DP_MODE;
			else
				dig->panel_mode = radeon_dp_get_panel_mode(encoder, connector);

			/* setup and enable the encoder */
			atombios_dig_encoder_setup(encoder, ATOM_ENCODER_CMD_SETUP, 0);
			atombios_dig_encoder_setup(encoder,
						   ATOM_ENCODER_CMD_SETUP_PANEL_MODE,
						   dig->panel_mode);
			if (ext_encoder) {
				if (ASIC_IS_DCE41(rdev) || ASIC_IS_DCE61(rdev))
					atombios_external_encoder_setup(encoder, ext_encoder,
									EXTERNAL_ENCODER_ACTION_V3_ENCODER_SETUP);
			}
		} else if (ASIC_IS_DCE4(rdev)) {
			/* setup and enable the encoder */
			atombios_dig_encoder_setup(encoder, ATOM_ENCODER_CMD_SETUP, 0);
		} else {
			/* setup and enable the encoder and transmitter */
			atombios_dig_encoder_setup(encoder, ATOM_ENABLE, 0);
			atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_SETUP, 0, 0);
		}
		if (ENCODER_MODE_IS_DP(atombios_get_encoder_mode(encoder)) && connector) {
			if (connector->connector_type == DRM_MODE_CONNECTOR_eDP) {
				atombios_set_edp_panel_power(connector,
							     ATOM_TRANSMITTER_ACTION_POWER_ON);
				radeon_dig_connector->edp_on = true;
			}
		}
		/* enable the transmitter */
		atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_ENABLE, 0, 0);
		if (ENCODER_MODE_IS_DP(atombios_get_encoder_mode(encoder)) && connector) {
			/* DP_SET_POWER_D0 is set in radeon_dp_link_train */
			radeon_dp_link_train(encoder, connector);
			if (ASIC_IS_DCE4(rdev))
				atombios_dig_encoder_setup(encoder, ATOM_ENCODER_CMD_DP_VIDEO_ON, 0);
		}
		if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
			if (rdev->mode_info.bl_encoder)
				atombios_set_backlight_level(radeon_encoder, dig->backlight_level);
			else
				atombios_dig_transmitter_setup(encoder,
							       ATOM_TRANSMITTER_ACTION_LCD_BLON, 0, 0);
		}
		if (ext_encoder)
			atombios_external_encoder_setup(encoder, ext_encoder, ATOM_ENABLE);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:

		/* don't power off encoders with active MST links */
		if (dig->active_mst_links)
			return;

		if (ASIC_IS_DCE4(rdev)) {
			if (ENCODER_MODE_IS_DP(atombios_get_encoder_mode(encoder)) && connector)
				atombios_dig_encoder_setup(encoder, ATOM_ENCODER_CMD_DP_VIDEO_OFF, 0);
		}
		if (ext_encoder)
			atombios_external_encoder_setup(encoder, ext_encoder, ATOM_DISABLE);
		if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
			atombios_dig_transmitter_setup(encoder,
						       ATOM_TRANSMITTER_ACTION_LCD_BLOFF, 0, 0);

		if (ENCODER_MODE_IS_DP(atombios_get_encoder_mode(encoder)) &&
		    connector && !travis_quirk)
			radeon_dp_set_rx_power_state(connector, DP_SET_POWER_D3);
		if (ASIC_IS_DCE4(rdev)) {
			/* disable the transmitter */
			atombios_dig_transmitter_setup(encoder,
						       ATOM_TRANSMITTER_ACTION_DISABLE, 0, 0);
		} else {
			/* disable the encoder and transmitter */
			atombios_dig_transmitter_setup(encoder,
						       ATOM_TRANSMITTER_ACTION_DISABLE, 0, 0);
			atombios_dig_encoder_setup(encoder, ATOM_DISABLE, 0);
		}
		if (ENCODER_MODE_IS_DP(atombios_get_encoder_mode(encoder)) && connector) {
			if (travis_quirk)
				radeon_dp_set_rx_power_state(connector, DP_SET_POWER_D3);
			if (connector->connector_type == DRM_MODE_CONNECTOR_eDP) {
				atombios_set_edp_panel_power(connector,
							     ATOM_TRANSMITTER_ACTION_POWER_OFF);
				radeon_dig_connector->edp_on = false;
			}
		}
		break;
	}
}

static void
radeon_atom_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	int encoder_mode = atombios_get_encoder_mode(encoder);

	DRM_DEBUG_KMS("encoder dpms %d to mode %d, devices %08x, active_devices %08x\n",
		  radeon_encoder->encoder_id, mode, radeon_encoder->devices,
		  radeon_encoder->active_device);

	if ((radeon_audio != 0) &&
	    ((encoder_mode == ATOM_ENCODER_MODE_HDMI) ||
	     ENCODER_MODE_IS_DP(encoder_mode)))
		radeon_audio_dpms(encoder, mode);

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_LVDS:
	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
	case ENCODER_OBJECT_ID_INTERNAL_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_DDI:
	case ENCODER_OBJECT_ID_INTERNAL_DAC2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		radeon_atom_encoder_dpms_avivo(encoder, mode);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
		radeon_atom_encoder_dpms_dig(encoder, mode);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
		if (ASIC_IS_DCE5(rdev)) {
			switch (mode) {
			case DRM_MODE_DPMS_ON:
				atombios_dvo_setup(encoder, ATOM_ENABLE);
				break;
			case DRM_MODE_DPMS_STANDBY:
			case DRM_MODE_DPMS_SUSPEND:
			case DRM_MODE_DPMS_OFF:
				atombios_dvo_setup(encoder, ATOM_DISABLE);
				break;
			}
		} else if (ASIC_IS_DCE3(rdev))
			radeon_atom_encoder_dpms_dig(encoder, mode);
		else
			radeon_atom_encoder_dpms_avivo(encoder, mode);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
		if (ASIC_IS_DCE5(rdev)) {
			switch (mode) {
			case DRM_MODE_DPMS_ON:
				atombios_dac_setup(encoder, ATOM_ENABLE);
				break;
			case DRM_MODE_DPMS_STANDBY:
			case DRM_MODE_DPMS_SUSPEND:
			case DRM_MODE_DPMS_OFF:
				atombios_dac_setup(encoder, ATOM_DISABLE);
				break;
			}
		} else
			radeon_atom_encoder_dpms_avivo(encoder, mode);
		break;
	default:
		return;
	}

	radeon_atombios_encoder_dpms_scratch_regs(encoder, (mode == DRM_MODE_DPMS_ON) ? true : false);

}

union crtc_source_param {
	SELECT_CRTC_SOURCE_PS_ALLOCATION v1;
	SELECT_CRTC_SOURCE_PARAMETERS_V2 v2;
};

static void
atombios_set_encoder_crtc_source(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	union crtc_source_param args;
	int index = GetIndexIntoMasterTable(COMMAND, SelectCRTC_Source);
	uint8_t frev, crev;
	struct radeon_encoder_atom_dig *dig;

	memset(&args, 0, sizeof(args));

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return;

	switch (frev) {
	case 1:
		switch (crev) {
		case 1:
		default:
			if (ASIC_IS_AVIVO(rdev))
				args.v1.ucCRTC = radeon_crtc->crtc_id;
			else {
				if (radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_DAC1) {
					args.v1.ucCRTC = radeon_crtc->crtc_id;
				} else {
					args.v1.ucCRTC = radeon_crtc->crtc_id << 2;
				}
			}
			switch (radeon_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
				args.v1.ucDevice = ATOM_DEVICE_DFP1_INDEX;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_LVDS:
			case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
				if (radeon_encoder->devices & ATOM_DEVICE_LCD1_SUPPORT)
					args.v1.ucDevice = ATOM_DEVICE_LCD1_INDEX;
				else
					args.v1.ucDevice = ATOM_DEVICE_DFP3_INDEX;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_DVO1:
			case ENCODER_OBJECT_ID_INTERNAL_DDI:
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
				args.v1.ucDevice = ATOM_DEVICE_DFP2_INDEX;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_DAC1:
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
				if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
					args.v1.ucDevice = ATOM_DEVICE_TV1_INDEX;
				else if (radeon_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
					args.v1.ucDevice = ATOM_DEVICE_CV_INDEX;
				else
					args.v1.ucDevice = ATOM_DEVICE_CRT1_INDEX;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_DAC2:
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
				if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
					args.v1.ucDevice = ATOM_DEVICE_TV1_INDEX;
				else if (radeon_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
					args.v1.ucDevice = ATOM_DEVICE_CV_INDEX;
				else
					args.v1.ucDevice = ATOM_DEVICE_CRT2_INDEX;
				break;
			}
			break;
		case 2:
			args.v2.ucCRTC = radeon_crtc->crtc_id;
			if (radeon_encoder_get_dp_bridge_encoder_id(encoder) != ENCODER_OBJECT_ID_NONE) {
				struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);

				if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS)
					args.v2.ucEncodeMode = ATOM_ENCODER_MODE_LVDS;
				else if (connector->connector_type == DRM_MODE_CONNECTOR_VGA)
					args.v2.ucEncodeMode = ATOM_ENCODER_MODE_CRT;
				else
					args.v2.ucEncodeMode = atombios_get_encoder_mode(encoder);
			} else if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
				args.v2.ucEncodeMode = ATOM_ENCODER_MODE_LVDS;
			} else {
				args.v2.ucEncodeMode = atombios_get_encoder_mode(encoder);
			}
			switch (radeon_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
				dig = radeon_encoder->enc_priv;
				switch (dig->dig_encoder) {
				case 0:
					args.v2.ucEncoderID = ASIC_INT_DIG1_ENCODER_ID;
					break;
				case 1:
					args.v2.ucEncoderID = ASIC_INT_DIG2_ENCODER_ID;
					break;
				case 2:
					args.v2.ucEncoderID = ASIC_INT_DIG3_ENCODER_ID;
					break;
				case 3:
					args.v2.ucEncoderID = ASIC_INT_DIG4_ENCODER_ID;
					break;
				case 4:
					args.v2.ucEncoderID = ASIC_INT_DIG5_ENCODER_ID;
					break;
				case 5:
					args.v2.ucEncoderID = ASIC_INT_DIG6_ENCODER_ID;
					break;
				case 6:
					args.v2.ucEncoderID = ASIC_INT_DIG7_ENCODER_ID;
					break;
				}
				break;
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
				args.v2.ucEncoderID = ASIC_INT_DVO_ENCODER_ID;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
				if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
					args.v2.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else if (radeon_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
					args.v2.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else
					args.v2.ucEncoderID = ASIC_INT_DAC1_ENCODER_ID;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
				if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
					args.v2.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else if (radeon_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
					args.v2.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else
					args.v2.ucEncoderID = ASIC_INT_DAC2_ENCODER_ID;
				break;
			}
			break;
		}
		break;
	default:
		DRM_ERROR("Unknown table version: %d, %d\n", frev, crev);
		return;
	}

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

	/* update scratch regs with new routing */
	radeon_atombios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);
}

void
atombios_set_mst_encoder_crtc_source(struct drm_encoder *encoder, int fe)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	int index = GetIndexIntoMasterTable(COMMAND, SelectCRTC_Source);
	uint8_t frev, crev;
	union crtc_source_param args;

	memset(&args, 0, sizeof(args));

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return;

	if (frev != 1 && crev != 2)
		DRM_ERROR("Unknown table for MST %d, %d\n", frev, crev);

	args.v2.ucCRTC = radeon_crtc->crtc_id;
	args.v2.ucEncodeMode = ATOM_ENCODER_MODE_DP_MST;

	switch (fe) {
	case 0:
		args.v2.ucEncoderID = ASIC_INT_DIG1_ENCODER_ID;
		break;
	case 1:
		args.v2.ucEncoderID = ASIC_INT_DIG2_ENCODER_ID;
		break;
	case 2:
		args.v2.ucEncoderID = ASIC_INT_DIG3_ENCODER_ID;
		break;
	case 3:
		args.v2.ucEncoderID = ASIC_INT_DIG4_ENCODER_ID;
		break;
	case 4:
		args.v2.ucEncoderID = ASIC_INT_DIG5_ENCODER_ID;
		break;
	case 5:
		args.v2.ucEncoderID = ASIC_INT_DIG6_ENCODER_ID;
		break;
	case 6:
		args.v2.ucEncoderID = ASIC_INT_DIG7_ENCODER_ID;
		break;
	}
	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

static void
atombios_apply_encoder_quirks(struct drm_encoder *encoder,
			      struct drm_display_mode *mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);

	/* Funky macbooks */
	if ((dev->pdev->device == 0x71C5) &&
	    (dev->pdev->subsystem_vendor == 0x106b) &&
	    (dev->pdev->subsystem_device == 0x0080)) {
		if (radeon_encoder->devices & ATOM_DEVICE_LCD1_SUPPORT) {
			uint32_t lvtma_bit_depth_control = RREG32(AVIVO_LVTMA_BIT_DEPTH_CONTROL);

			lvtma_bit_depth_control &= ~AVIVO_LVTMA_BIT_DEPTH_CONTROL_TRUNCATE_EN;
			lvtma_bit_depth_control &= ~AVIVO_LVTMA_BIT_DEPTH_CONTROL_SPATIAL_DITHER_EN;

			WREG32(AVIVO_LVTMA_BIT_DEPTH_CONTROL, lvtma_bit_depth_control);
		}
	}

	/* set scaler clears this on some chips */
	if (ASIC_IS_AVIVO(rdev) &&
	    (!(radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT)))) {
		if (ASIC_IS_DCE8(rdev)) {
			if (mode->flags & DRM_MODE_FLAG_INTERLACE)
				WREG32(CIK_LB_DATA_FORMAT + radeon_crtc->crtc_offset,
				       CIK_INTERLEAVE_EN);
			else
				WREG32(CIK_LB_DATA_FORMAT + radeon_crtc->crtc_offset, 0);
		} else if (ASIC_IS_DCE4(rdev)) {
			if (mode->flags & DRM_MODE_FLAG_INTERLACE)
				WREG32(EVERGREEN_DATA_FORMAT + radeon_crtc->crtc_offset,
				       EVERGREEN_INTERLEAVE_EN);
			else
				WREG32(EVERGREEN_DATA_FORMAT + radeon_crtc->crtc_offset, 0);
		} else {
			if (mode->flags & DRM_MODE_FLAG_INTERLACE)
				WREG32(AVIVO_D1MODE_DATA_FORMAT + radeon_crtc->crtc_offset,
				       AVIVO_D1MODE_INTERLEAVE_EN);
			else
				WREG32(AVIVO_D1MODE_DATA_FORMAT + radeon_crtc->crtc_offset, 0);
		}
	}
}

void radeon_atom_release_dig_encoder(struct radeon_device *rdev, int enc_idx)
{
	if (enc_idx < 0)
		return;
	rdev->mode_info.active_encoders &= ~(1 << enc_idx);
}

int radeon_atom_pick_dig_encoder(struct drm_encoder *encoder, int fe_idx)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_encoder *test_encoder;
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t dig_enc_in_use = 0;
	int enc_idx = -1;

	if (fe_idx >= 0) {
		enc_idx = fe_idx;
		goto assigned;
	}
	if (ASIC_IS_DCE6(rdev)) {
		/* DCE6 */
		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			if (dig->linkb)
				enc_idx = 1;
			else
				enc_idx = 0;
			break;
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
			if (dig->linkb)
				enc_idx = 3;
			else
				enc_idx = 2;
			break;
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
			if (dig->linkb)
				enc_idx = 5;
			else
				enc_idx = 4;
			break;
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
			enc_idx = 6;
			break;
		}
		goto assigned;
	} else if (ASIC_IS_DCE4(rdev)) {
		/* DCE4/5 */
		if (ASIC_IS_DCE41(rdev) && !ASIC_IS_DCE61(rdev)) {
			/* ontario follows DCE4 */
			if (rdev->family == CHIP_PALM) {
				if (dig->linkb)
					enc_idx = 1;
				else
					enc_idx = 0;
			} else
				/* llano follows DCE3.2 */
				enc_idx = radeon_crtc->crtc_id;
		} else {
			switch (radeon_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
				if (dig->linkb)
					enc_idx = 1;
				else
					enc_idx = 0;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
				if (dig->linkb)
					enc_idx = 3;
				else
					enc_idx = 2;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
				if (dig->linkb)
					enc_idx = 5;
				else
					enc_idx = 4;
				break;
			}
		}
		goto assigned;
	}

	/* on DCE32 and encoder can driver any block so just crtc id */
	if (ASIC_IS_DCE32(rdev)) {
		enc_idx = radeon_crtc->crtc_id;
		goto assigned;
	}

	/* on DCE3 - LVTMA can only be driven by DIGB */
	list_for_each_entry(test_encoder, &dev->mode_config.encoder_list, head) {
		struct radeon_encoder *radeon_test_encoder;

		if (encoder == test_encoder)
			continue;

		if (!radeon_encoder_is_digital(test_encoder))
			continue;

		radeon_test_encoder = to_radeon_encoder(test_encoder);
		dig = radeon_test_encoder->enc_priv;

		if (dig->dig_encoder >= 0)
			dig_enc_in_use |= (1 << dig->dig_encoder);
	}

	if (radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA) {
		if (dig_enc_in_use & 0x2)
			DRM_ERROR("LVDS required digital encoder 2 but it was in use - stealing\n");
		return 1;
	}
	if (!(dig_enc_in_use & 1))
		return 0;
	return 1;

assigned:
	if (enc_idx == -1) {
		DRM_ERROR("Got encoder index incorrect - returning 0\n");
		return 0;
	}
	if (rdev->mode_info.active_encoders & (1 << enc_idx)) {
		DRM_ERROR("chosen encoder in use %d\n", enc_idx);
	}
	rdev->mode_info.active_encoders |= (1 << enc_idx);
	return enc_idx;
}

/* This only needs to be called once at startup */
void
radeon_atom_encoder_init(struct radeon_device *rdev)
{
	struct drm_device *dev = rdev->ddev;
	struct drm_encoder *encoder;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
		struct drm_encoder *ext_encoder = radeon_get_external_encoder(encoder);

		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
			atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_INIT, 0, 0);
			break;
		default:
			break;
		}

		if (ext_encoder && (ASIC_IS_DCE41(rdev) || ASIC_IS_DCE61(rdev)))
			atombios_external_encoder_setup(encoder, ext_encoder,
							EXTERNAL_ENCODER_ACTION_V3_ENCODER_INIT);
	}
}

static void
radeon_atom_encoder_mode_set(struct drm_encoder *encoder,
			     struct drm_display_mode *mode,
			     struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
	int encoder_mode;

	radeon_encoder->pixel_clock = adjusted_mode->clock;

	/* need to call this here rather than in prepare() since we need some crtc info */
	radeon_atom_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);

	if (ASIC_IS_AVIVO(rdev) && !ASIC_IS_DCE4(rdev)) {
		if (radeon_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT | ATOM_DEVICE_TV_SUPPORT))
			atombios_yuv_setup(encoder, true);
		else
			atombios_yuv_setup(encoder, false);
	}

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_LVDS:
	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
		atombios_digital_setup(encoder, PANEL_ENCODER_ACTION_ENABLE);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
		/* handled in dpms */
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DDI:
	case ENCODER_OBJECT_ID_INTERNAL_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
		atombios_dvo_setup(encoder, ATOM_ENABLE);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_DAC2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		atombios_dac_setup(encoder, ATOM_ENABLE);
		if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT | ATOM_DEVICE_CV_SUPPORT)) {
			if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT | ATOM_DEVICE_CV_SUPPORT))
				atombios_tv_setup(encoder, ATOM_ENABLE);
			else
				atombios_tv_setup(encoder, ATOM_DISABLE);
		}
		break;
	}

	atombios_apply_encoder_quirks(encoder, adjusted_mode);

	encoder_mode = atombios_get_encoder_mode(encoder);
	if (connector && (radeon_audio != 0) &&
	    ((encoder_mode == ATOM_ENCODER_MODE_HDMI) ||
	     ENCODER_MODE_IS_DP(encoder_mode)))
		radeon_audio_mode_set(encoder, adjusted_mode);
}

static bool
atombios_dac_load_detect(struct drm_encoder *encoder, struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);

	if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT |
				       ATOM_DEVICE_CV_SUPPORT |
				       ATOM_DEVICE_CRT_SUPPORT)) {
		DAC_LOAD_DETECTION_PS_ALLOCATION args;
		int index = GetIndexIntoMasterTable(COMMAND, DAC_LoadDetection);
		uint8_t frev, crev;

		memset(&args, 0, sizeof(args));

		if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
			return false;

		args.sDacload.ucMisc = 0;

		if ((radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_DAC1) ||
		    (radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1))
			args.sDacload.ucDacType = ATOM_DAC_A;
		else
			args.sDacload.ucDacType = ATOM_DAC_B;

		if (radeon_connector->devices & ATOM_DEVICE_CRT1_SUPPORT)
			args.sDacload.usDeviceID = cpu_to_le16(ATOM_DEVICE_CRT1_SUPPORT);
		else if (radeon_connector->devices & ATOM_DEVICE_CRT2_SUPPORT)
			args.sDacload.usDeviceID = cpu_to_le16(ATOM_DEVICE_CRT2_SUPPORT);
		else if (radeon_connector->devices & ATOM_DEVICE_CV_SUPPORT) {
			args.sDacload.usDeviceID = cpu_to_le16(ATOM_DEVICE_CV_SUPPORT);
			if (crev >= 3)
				args.sDacload.ucMisc = DAC_LOAD_MISC_YPrPb;
		} else if (radeon_connector->devices & ATOM_DEVICE_TV1_SUPPORT) {
			args.sDacload.usDeviceID = cpu_to_le16(ATOM_DEVICE_TV1_SUPPORT);
			if (crev >= 3)
				args.sDacload.ucMisc = DAC_LOAD_MISC_YPrPb;
		}

		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

		return true;
	} else
		return false;
}

static enum drm_connector_status
radeon_atom_dac_detect(struct drm_encoder *encoder, struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	uint32_t bios_0_scratch;

	if (!atombios_dac_load_detect(encoder, connector)) {
		DRM_DEBUG_KMS("detect returned false \n");
		return connector_status_unknown;
	}

	if (rdev->family >= CHIP_R600)
		bios_0_scratch = RREG32(R600_BIOS_0_SCRATCH);
	else
		bios_0_scratch = RREG32(RADEON_BIOS_0_SCRATCH);

	DRM_DEBUG_KMS("Bios 0 scratch %x %08x\n", bios_0_scratch, radeon_encoder->devices);
	if (radeon_connector->devices & ATOM_DEVICE_CRT1_SUPPORT) {
		if (bios_0_scratch & ATOM_S0_CRT1_MASK)
			return connector_status_connected;
	}
	if (radeon_connector->devices & ATOM_DEVICE_CRT2_SUPPORT) {
		if (bios_0_scratch & ATOM_S0_CRT2_MASK)
			return connector_status_connected;
	}
	if (radeon_connector->devices & ATOM_DEVICE_CV_SUPPORT) {
		if (bios_0_scratch & (ATOM_S0_CV_MASK|ATOM_S0_CV_MASK_A))
			return connector_status_connected;
	}
	if (radeon_connector->devices & ATOM_DEVICE_TV1_SUPPORT) {
		if (bios_0_scratch & (ATOM_S0_TV1_COMPOSITE | ATOM_S0_TV1_COMPOSITE_A))
			return connector_status_connected; /* CTV */
		else if (bios_0_scratch & (ATOM_S0_TV1_SVIDEO | ATOM_S0_TV1_SVIDEO_A))
			return connector_status_connected; /* STV */
	}
	return connector_status_disconnected;
}

static enum drm_connector_status
radeon_atom_dig_detect(struct drm_encoder *encoder, struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct drm_encoder *ext_encoder = radeon_get_external_encoder(encoder);
	u32 bios_0_scratch;

	if (!ASIC_IS_DCE4(rdev))
		return connector_status_unknown;

	if (!ext_encoder)
		return connector_status_unknown;

	if ((radeon_connector->devices & ATOM_DEVICE_CRT_SUPPORT) == 0)
		return connector_status_unknown;

	/* load detect on the dp bridge */
	atombios_external_encoder_setup(encoder, ext_encoder,
					EXTERNAL_ENCODER_ACTION_V3_DACLOAD_DETECTION);

	bios_0_scratch = RREG32(R600_BIOS_0_SCRATCH);

	DRM_DEBUG_KMS("Bios 0 scratch %x %08x\n", bios_0_scratch, radeon_encoder->devices);
	if (radeon_connector->devices & ATOM_DEVICE_CRT1_SUPPORT) {
		if (bios_0_scratch & ATOM_S0_CRT1_MASK)
			return connector_status_connected;
	}
	if (radeon_connector->devices & ATOM_DEVICE_CRT2_SUPPORT) {
		if (bios_0_scratch & ATOM_S0_CRT2_MASK)
			return connector_status_connected;
	}
	if (radeon_connector->devices & ATOM_DEVICE_CV_SUPPORT) {
		if (bios_0_scratch & (ATOM_S0_CV_MASK|ATOM_S0_CV_MASK_A))
			return connector_status_connected;
	}
	if (radeon_connector->devices & ATOM_DEVICE_TV1_SUPPORT) {
		if (bios_0_scratch & (ATOM_S0_TV1_COMPOSITE | ATOM_S0_TV1_COMPOSITE_A))
			return connector_status_connected; /* CTV */
		else if (bios_0_scratch & (ATOM_S0_TV1_SVIDEO | ATOM_S0_TV1_SVIDEO_A))
			return connector_status_connected; /* STV */
	}
	return connector_status_disconnected;
}

void
radeon_atom_ext_encoder_setup_ddc(struct drm_encoder *encoder)
{
	struct drm_encoder *ext_encoder = radeon_get_external_encoder(encoder);

	if (ext_encoder)
		/* ddc_setup on the dp bridge */
		atombios_external_encoder_setup(encoder, ext_encoder,
						EXTERNAL_ENCODER_ACTION_V3_DDC_SETUP);

}

static void radeon_atom_encoder_prepare(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);

	if ((radeon_encoder->active_device &
	     (ATOM_DEVICE_DFP_SUPPORT | ATOM_DEVICE_LCD_SUPPORT)) ||
	    (radeon_encoder_get_dp_bridge_encoder_id(encoder) !=
	     ENCODER_OBJECT_ID_NONE)) {
		struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
		if (dig) {
			if (dig->dig_encoder >= 0)
				radeon_atom_release_dig_encoder(rdev, dig->dig_encoder);
			dig->dig_encoder = radeon_atom_pick_dig_encoder(encoder, -1);
			if (radeon_encoder->active_device & ATOM_DEVICE_DFP_SUPPORT) {
				if (rdev->family >= CHIP_R600)
					dig->afmt = rdev->mode_info.afmt[dig->dig_encoder];
				else
					/* RS600/690/740 have only 1 afmt block */
					dig->afmt = rdev->mode_info.afmt[0];
			}
		}
	}

	radeon_atom_output_lock(encoder, true);

	if (connector) {
		struct radeon_connector *radeon_connector = to_radeon_connector(connector);

		/* select the clock/data port if it uses a router */
		if (radeon_connector->router.cd_valid)
			radeon_router_select_cd_port(radeon_connector);

		/* turn eDP panel on for mode set */
		if (connector->connector_type == DRM_MODE_CONNECTOR_eDP)
			atombios_set_edp_panel_power(connector,
						     ATOM_TRANSMITTER_ACTION_POWER_ON);
	}

	/* this is needed for the pll/ss setup to work correctly in some cases */
	atombios_set_encoder_crtc_source(encoder);
	/* set up the FMT blocks */
	if (ASIC_IS_DCE8(rdev))
		dce8_program_fmt(encoder);
	else if (ASIC_IS_DCE4(rdev))
		dce4_program_fmt(encoder);
	else if (ASIC_IS_DCE3(rdev))
		dce3_program_fmt(encoder);
	else if (ASIC_IS_AVIVO(rdev))
		avivo_program_fmt(encoder);
}

static void radeon_atom_encoder_commit(struct drm_encoder *encoder)
{
	/* need to call this here as we need the crtc set up */
	radeon_atom_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
	radeon_atom_output_lock(encoder, false);
}

static void radeon_atom_encoder_disable(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig;

	/* check for pre-DCE3 cards with shared encoders;
	 * can't really use the links individually, so don't disable
	 * the encoder if it's in use by another connector
	 */
	if (!ASIC_IS_DCE3(rdev)) {
		struct drm_encoder *other_encoder;
		struct radeon_encoder *other_radeon_encoder;

		list_for_each_entry(other_encoder, &dev->mode_config.encoder_list, head) {
			other_radeon_encoder = to_radeon_encoder(other_encoder);
			if ((radeon_encoder->encoder_id == other_radeon_encoder->encoder_id) &&
			    drm_helper_encoder_in_use(other_encoder))
				goto disable_done;
		}
	}

	radeon_atom_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_LVDS:
	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
		atombios_digital_setup(encoder, PANEL_ENCODER_ACTION_DISABLE);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
		/* handled in dpms */
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DDI:
	case ENCODER_OBJECT_ID_INTERNAL_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
		atombios_dvo_setup(encoder, ATOM_DISABLE);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_DAC2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		atombios_dac_setup(encoder, ATOM_DISABLE);
		if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT | ATOM_DEVICE_CV_SUPPORT))
			atombios_tv_setup(encoder, ATOM_DISABLE);
		break;
	}

disable_done:
	if (radeon_encoder_is_digital(encoder)) {
		if (atombios_get_encoder_mode(encoder) == ATOM_ENCODER_MODE_HDMI) {
			if (rdev->asic->display.hdmi_enable)
				radeon_hdmi_enable(rdev, encoder, false);
		}
		if (atombios_get_encoder_mode(encoder) != ATOM_ENCODER_MODE_DP_MST) {
			dig = radeon_encoder->enc_priv;
			radeon_atom_release_dig_encoder(rdev, dig->dig_encoder);
			dig->dig_encoder = -1;
			radeon_encoder->active_device = 0;
		}
	} else
		radeon_encoder->active_device = 0;
}

/* these are handled by the primary encoders */
static void radeon_atom_ext_prepare(struct drm_encoder *encoder)
{

}

static void radeon_atom_ext_commit(struct drm_encoder *encoder)
{

}

static void
radeon_atom_ext_mode_set(struct drm_encoder *encoder,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode)
{

}

static void radeon_atom_ext_disable(struct drm_encoder *encoder)
{

}

static void
radeon_atom_ext_dpms(struct drm_encoder *encoder, int mode)
{

}

static const struct drm_encoder_helper_funcs radeon_atom_ext_helper_funcs = {
	.dpms = radeon_atom_ext_dpms,
	.prepare = radeon_atom_ext_prepare,
	.mode_set = radeon_atom_ext_mode_set,
	.commit = radeon_atom_ext_commit,
	.disable = radeon_atom_ext_disable,
	/* no detect for TMDS/LVDS yet */
};

static const struct drm_encoder_helper_funcs radeon_atom_dig_helper_funcs = {
	.dpms = radeon_atom_encoder_dpms,
	.mode_fixup = radeon_atom_mode_fixup,
	.prepare = radeon_atom_encoder_prepare,
	.mode_set = radeon_atom_encoder_mode_set,
	.commit = radeon_atom_encoder_commit,
	.disable = radeon_atom_encoder_disable,
	.detect = radeon_atom_dig_detect,
};

static const struct drm_encoder_helper_funcs radeon_atom_dac_helper_funcs = {
	.dpms = radeon_atom_encoder_dpms,
	.mode_fixup = radeon_atom_mode_fixup,
	.prepare = radeon_atom_encoder_prepare,
	.mode_set = radeon_atom_encoder_mode_set,
	.commit = radeon_atom_encoder_commit,
	.detect = radeon_atom_dac_detect,
};

void radeon_enc_destroy(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
		radeon_atom_backlight_exit(radeon_encoder);
	kfree(radeon_encoder->enc_priv);
	drm_encoder_cleanup(encoder);
	kfree(radeon_encoder);
}

static const struct drm_encoder_funcs radeon_atom_enc_funcs = {
	.destroy = radeon_enc_destroy,
};

static struct radeon_encoder_atom_dac *
radeon_atombios_set_dac_info(struct radeon_encoder *radeon_encoder)
{
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder_atom_dac *dac = kzalloc(sizeof(struct radeon_encoder_atom_dac), GFP_KERNEL);

	if (!dac)
		return NULL;

	dac->tv_std = radeon_atombios_get_tv_info(rdev);
	return dac;
}

static struct radeon_encoder_atom_dig *
radeon_atombios_set_dig_info(struct radeon_encoder *radeon_encoder)
{
	int encoder_enum = (radeon_encoder->encoder_enum & ENUM_ID_MASK) >> ENUM_ID_SHIFT;
	struct radeon_encoder_atom_dig *dig = kzalloc(sizeof(struct radeon_encoder_atom_dig), GFP_KERNEL);

	if (!dig)
		return NULL;

	/* coherent mode by default */
	dig->coherent_mode = true;
	dig->dig_encoder = -1;

	if (encoder_enum == 2)
		dig->linkb = true;
	else
		dig->linkb = false;

	return dig;
}

void
radeon_add_atom_encoder(struct drm_device *dev,
			uint32_t encoder_enum,
			uint32_t supported_device,
			u16 caps)
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
	switch (rdev->num_crtc) {
	case 1:
		encoder->possible_crtcs = 0x1;
		break;
	case 2:
	default:
		encoder->possible_crtcs = 0x3;
		break;
	case 4:
		encoder->possible_crtcs = 0xf;
		break;
	case 6:
		encoder->possible_crtcs = 0x3f;
		break;
	}

	radeon_encoder->enc_priv = NULL;

	radeon_encoder->encoder_enum = encoder_enum;
	radeon_encoder->encoder_id = (encoder_enum & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
	radeon_encoder->devices = supported_device;
	radeon_encoder->rmx_type = RMX_OFF;
	radeon_encoder->underscan_type = UNDERSCAN_OFF;
	radeon_encoder->is_ext_encoder = false;
	radeon_encoder->caps = caps;

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_LVDS:
	case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
		if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
			radeon_encoder->rmx_type = RMX_FULL;
			drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs,
					 DRM_MODE_ENCODER_LVDS, NULL);
			radeon_encoder->enc_priv = radeon_atombios_get_lvds_info(radeon_encoder);
		} else {
			drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs,
					 DRM_MODE_ENCODER_TMDS, NULL);
			radeon_encoder->enc_priv = radeon_atombios_set_dig_info(radeon_encoder);
		}
		drm_encoder_helper_add(encoder, &radeon_atom_dig_helper_funcs);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC1:
		drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs,
				 DRM_MODE_ENCODER_DAC, NULL);
		radeon_encoder->enc_priv = radeon_atombios_set_dac_info(radeon_encoder);
		drm_encoder_helper_add(encoder, &radeon_atom_dac_helper_funcs);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs,
				 DRM_MODE_ENCODER_TVDAC, NULL);
		radeon_encoder->enc_priv = radeon_atombios_set_dac_info(radeon_encoder);
		drm_encoder_helper_add(encoder, &radeon_atom_dac_helper_funcs);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_DDI:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
		if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
			radeon_encoder->rmx_type = RMX_FULL;
			drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs,
					 DRM_MODE_ENCODER_LVDS, NULL);
			radeon_encoder->enc_priv = radeon_atombios_get_lvds_info(radeon_encoder);
		} else if (radeon_encoder->devices & (ATOM_DEVICE_CRT_SUPPORT)) {
			drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs,
					 DRM_MODE_ENCODER_DAC, NULL);
			radeon_encoder->enc_priv = radeon_atombios_set_dig_info(radeon_encoder);
		} else {
			drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs,
					 DRM_MODE_ENCODER_TMDS, NULL);
			radeon_encoder->enc_priv = radeon_atombios_set_dig_info(radeon_encoder);
		}
		drm_encoder_helper_add(encoder, &radeon_atom_dig_helper_funcs);
		break;
	case ENCODER_OBJECT_ID_SI170B:
	case ENCODER_OBJECT_ID_CH7303:
	case ENCODER_OBJECT_ID_EXTERNAL_SDVOA:
	case ENCODER_OBJECT_ID_EXTERNAL_SDVOB:
	case ENCODER_OBJECT_ID_TITFP513:
	case ENCODER_OBJECT_ID_VT1623:
	case ENCODER_OBJECT_ID_HDMI_SI1930:
	case ENCODER_OBJECT_ID_TRAVIS:
	case ENCODER_OBJECT_ID_NUTMEG:
		/* these are handled by the primary encoders */
		radeon_encoder->is_ext_encoder = true;
		if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
			drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs,
					 DRM_MODE_ENCODER_LVDS, NULL);
		else if (radeon_encoder->devices & (ATOM_DEVICE_CRT_SUPPORT))
			drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs,
					 DRM_MODE_ENCODER_DAC, NULL);
		else
			drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs,
					 DRM_MODE_ENCODER_TMDS, NULL);
		drm_encoder_helper_add(encoder, &radeon_atom_ext_helper_funcs);
		break;
	}
}
