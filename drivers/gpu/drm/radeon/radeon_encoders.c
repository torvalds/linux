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

extern int atom_debug;

uint32_t
radeon_get_encoder_id(struct drm_device *dev, uint32_t supported_device, uint8_t dac)
{
	struct radeon_device *rdev = dev->dev_private;
	uint32_t ret = 0;

	switch (supported_device) {
	case ATOM_DEVICE_CRT1_SUPPORT:
	case ATOM_DEVICE_TV1_SUPPORT:
	case ATOM_DEVICE_TV2_SUPPORT:
	case ATOM_DEVICE_CRT2_SUPPORT:
	case ATOM_DEVICE_CV_SUPPORT:
		switch (dac) {
		case 1: /* dac a */
			if ((rdev->family == CHIP_RS300) ||
			    (rdev->family == CHIP_RS400) ||
			    (rdev->family == CHIP_RS480))
				ret = ENCODER_OBJECT_ID_INTERNAL_DAC2;
			else if (ASIC_IS_AVIVO(rdev))
				ret = ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1;
			else
				ret = ENCODER_OBJECT_ID_INTERNAL_DAC1;
			break;
		case 2: /* dac b */
			if (ASIC_IS_AVIVO(rdev))
				ret = ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2;
			else {
				/*if (rdev->family == CHIP_R200)
				  ret = ENCODER_OBJECT_ID_INTERNAL_DVO1;
				  else*/
				ret = ENCODER_OBJECT_ID_INTERNAL_DAC2;
			}
			break;
		case 3: /* external dac */
			if (ASIC_IS_AVIVO(rdev))
				ret = ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1;
			else
				ret = ENCODER_OBJECT_ID_INTERNAL_DVO1;
			break;
		}
		break;
	case ATOM_DEVICE_LCD1_SUPPORT:
		if (ASIC_IS_AVIVO(rdev))
			ret = ENCODER_OBJECT_ID_INTERNAL_LVTM1;
		else
			ret = ENCODER_OBJECT_ID_INTERNAL_LVDS;
		break;
	case ATOM_DEVICE_DFP1_SUPPORT:
		if ((rdev->family == CHIP_RS300) ||
		    (rdev->family == CHIP_RS400) ||
		    (rdev->family == CHIP_RS480))
			ret = ENCODER_OBJECT_ID_INTERNAL_DVO1;
		else if (ASIC_IS_AVIVO(rdev))
			ret = ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1;
		else
			ret = ENCODER_OBJECT_ID_INTERNAL_TMDS1;
		break;
	case ATOM_DEVICE_LCD2_SUPPORT:
	case ATOM_DEVICE_DFP2_SUPPORT:
		if ((rdev->family == CHIP_RS600) ||
		    (rdev->family == CHIP_RS690) ||
		    (rdev->family == CHIP_RS740))
			ret = ENCODER_OBJECT_ID_INTERNAL_DDI;
		else if (ASIC_IS_AVIVO(rdev))
			ret = ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1;
		else
			ret = ENCODER_OBJECT_ID_INTERNAL_DVO1;
		break;
	case ATOM_DEVICE_DFP3_SUPPORT:
		ret = ENCODER_OBJECT_ID_INTERNAL_LVTM1;
		break;
	}

	return ret;
}

void
radeon_link_encoder_connector(struct drm_device *dev)
{
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	struct drm_encoder *encoder;
	struct radeon_encoder *radeon_encoder;

	/* walk the list and link encoders to connectors */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		radeon_connector = to_radeon_connector(connector);
		list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
			radeon_encoder = to_radeon_encoder(encoder);
			if (radeon_encoder->devices & radeon_connector->devices)
				drm_mode_connector_attach_encoder(connector, encoder);
		}
	}
}

static struct drm_connector *
radeon_get_connector_for_encoder(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		radeon_connector = to_radeon_connector(connector);
		if (radeon_encoder->devices & radeon_connector->devices)
			return connector;
	}
	return NULL;
}

/* used for both atom and legacy */
void radeon_rmx_mode_fixup(struct drm_encoder *encoder,
			   struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_native_mode *native_mode = &radeon_encoder->native_mode;

	if (mode->hdisplay < native_mode->panel_xres ||
	    mode->vdisplay < native_mode->panel_yres) {
		radeon_encoder->flags |= RADEON_USE_RMX;
		if (ASIC_IS_AVIVO(rdev)) {
			adjusted_mode->hdisplay = native_mode->panel_xres;
			adjusted_mode->vdisplay = native_mode->panel_yres;
			adjusted_mode->htotal = native_mode->panel_xres + native_mode->hblank;
			adjusted_mode->hsync_start = native_mode->panel_xres + native_mode->hoverplus;
			adjusted_mode->hsync_end = adjusted_mode->hsync_start + native_mode->hsync_width;
			adjusted_mode->vtotal = native_mode->panel_yres + native_mode->vblank;
			adjusted_mode->vsync_start = native_mode->panel_yres + native_mode->voverplus;
			adjusted_mode->vsync_end = adjusted_mode->vsync_start + native_mode->vsync_width;
			/* update crtc values */
			drm_mode_set_crtcinfo(adjusted_mode, CRTC_INTERLACE_HALVE_V);
			/* adjust crtc values */
			adjusted_mode->crtc_hdisplay = native_mode->panel_xres;
			adjusted_mode->crtc_vdisplay = native_mode->panel_yres;
			adjusted_mode->crtc_htotal = adjusted_mode->crtc_hdisplay + native_mode->hblank;
			adjusted_mode->crtc_hsync_start = adjusted_mode->crtc_hdisplay + native_mode->hoverplus;
			adjusted_mode->crtc_hsync_end = adjusted_mode->crtc_hsync_start + native_mode->hsync_width;
			adjusted_mode->crtc_vtotal = adjusted_mode->crtc_vdisplay + native_mode->vblank;
			adjusted_mode->crtc_vsync_start = adjusted_mode->crtc_vdisplay + native_mode->voverplus;
			adjusted_mode->crtc_vsync_end = adjusted_mode->crtc_vsync_start + native_mode->vsync_width;
		} else {
			adjusted_mode->htotal = native_mode->panel_xres + native_mode->hblank;
			adjusted_mode->hsync_start = native_mode->panel_xres + native_mode->hoverplus;
			adjusted_mode->hsync_end = adjusted_mode->hsync_start + native_mode->hsync_width;
			adjusted_mode->vtotal = native_mode->panel_yres + native_mode->vblank;
			adjusted_mode->vsync_start = native_mode->panel_yres + native_mode->voverplus;
			adjusted_mode->vsync_end = adjusted_mode->vsync_start + native_mode->vsync_width;
			/* update crtc values */
			drm_mode_set_crtcinfo(adjusted_mode, CRTC_INTERLACE_HALVE_V);
			/* adjust crtc values */
			adjusted_mode->crtc_htotal = adjusted_mode->crtc_hdisplay + native_mode->hblank;
			adjusted_mode->crtc_hsync_start = adjusted_mode->crtc_hdisplay + native_mode->hoverplus;
			adjusted_mode->crtc_hsync_end = adjusted_mode->crtc_hsync_start + native_mode->hsync_width;
			adjusted_mode->crtc_vtotal = adjusted_mode->crtc_vdisplay + native_mode->vblank;
			adjusted_mode->crtc_vsync_start = adjusted_mode->crtc_vdisplay + native_mode->voverplus;
			adjusted_mode->crtc_vsync_end = adjusted_mode->crtc_vsync_start + native_mode->vsync_width;
		}
		adjusted_mode->flags = native_mode->flags;
		adjusted_mode->clock = native_mode->dotclock;
	}
}

static bool radeon_atom_mode_fixup(struct drm_encoder *encoder,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{

	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);

	radeon_encoder->flags &= ~RADEON_USE_RMX;

	drm_mode_set_crtcinfo(adjusted_mode, 0);

	if (radeon_encoder->rmx_type != RMX_OFF)
		radeon_rmx_mode_fixup(encoder, mode, adjusted_mode);

	/* hw bug */
	if ((mode->flags & DRM_MODE_FLAG_INTERLACE)
	    && (mode->crtc_vsync_start < (mode->crtc_vdisplay + 2)))
		adjusted_mode->crtc_vsync_start = adjusted_mode->crtc_vdisplay + 2;

	return true;
}

static void
atombios_dac_setup(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	DAC_ENCODER_CONTROL_PS_ALLOCATION args;
	int index = 0, num = 0;
	/* fixme - fill in enc_priv for atom dac */
	enum radeon_tv_std tv_std = TV_STD_NTSC;

	memset(&args, 0, sizeof(args));

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
		index = GetIndexIntoMasterTable(COMMAND, DAC1EncoderControl);
		num = 1;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		index = GetIndexIntoMasterTable(COMMAND, DAC2EncoderControl);
		num = 2;
		break;
	}

	args.ucAction = action;

	if (radeon_encoder->devices & (ATOM_DEVICE_CRT_SUPPORT))
		args.ucDacStandard = ATOM_DAC1_PS2;
	else if (radeon_encoder->devices & (ATOM_DEVICE_CV_SUPPORT))
		args.ucDacStandard = ATOM_DAC1_CV;
	else {
		switch (tv_std) {
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
	/* fixme - fill in enc_priv for atom dac */
	enum radeon_tv_std tv_std = TV_STD_NTSC;

	memset(&args, 0, sizeof(args));

	index = GetIndexIntoMasterTable(COMMAND, TVEncoderControl);

	args.sTVEncoder.ucAction = action;

	if (radeon_encoder->devices & (ATOM_DEVICE_CV_SUPPORT))
		args.sTVEncoder.ucTvStandard = ATOM_TV_CV;
	else {
		switch (tv_std) {
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

void
atombios_external_tmds_setup(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	ENABLE_EXTERNAL_TMDS_ENCODER_PS_ALLOCATION args;
	int index = 0;

	memset(&args, 0, sizeof(args));

	index = GetIndexIntoMasterTable(COMMAND, DVOEncoderControl);

	args.sXTmdsEncoder.ucEnable = action;

	if (radeon_encoder->pixel_clock > 165000)
		args.sXTmdsEncoder.ucMisc = PANEL_ENCODER_MISC_DUAL;

	/*if (pScrn->rgbBits == 8)*/
	args.sXTmdsEncoder.ucMisc |= (1 << 1);

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

}

static void
atombios_ddia_setup(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	DVO_ENCODER_CONTROL_PS_ALLOCATION args;
	int index = 0;

	memset(&args, 0, sizeof(args));

	index = GetIndexIntoMasterTable(COMMAND, DVOEncoderControl);

	args.sDVOEncoder.ucAction = action;
	args.sDVOEncoder.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);

	if (radeon_encoder->pixel_clock > 165000)
		args.sDVOEncoder.usDevAttr.sDigAttrib.ucAttribute = PANEL_ENCODER_MISC_DUAL;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

}

union lvds_encoder_control {
	LVDS_ENCODER_CONTROL_PS_ALLOCATION    v1;
	LVDS_ENCODER_CONTROL_PS_ALLOCATION_V2 v2;
};

static void
atombios_digital_setup(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	union lvds_encoder_control args;
	int index = 0;
	uint8_t frev, crev;
	struct radeon_encoder_atom_dig *dig;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	struct radeon_connector_atom_dig *dig_connector;

	connector = radeon_get_connector_for_encoder(encoder);
	if (!connector)
		return;

	radeon_connector = to_radeon_connector(connector);

	if (!radeon_encoder->enc_priv)
		return;

	dig = radeon_encoder->enc_priv;

	if (!radeon_connector->con_priv)
		return;

	dig_connector = radeon_connector->con_priv;

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

	atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev);

	switch (frev) {
	case 1:
	case 2:
		switch (crev) {
		case 1:
			args.v1.ucMisc = 0;
			args.v1.ucAction = action;
			if (drm_detect_hdmi_monitor((struct edid *)connector->edid_blob_ptr))
				args.v1.ucMisc |= PANEL_ENCODER_MISC_HDMI_TYPE;
			args.v1.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
				if (dig->lvds_misc & (1 << 0))
					args.v1.ucMisc |= PANEL_ENCODER_MISC_DUAL;
				if (dig->lvds_misc & (1 << 1))
					args.v1.ucMisc |= (1 << 1);
			} else {
				if (dig_connector->linkb)
					args.v1.ucMisc |= PANEL_ENCODER_MISC_TMDS_LINKB;
				if (radeon_encoder->pixel_clock > 165000)
					args.v1.ucMisc |= PANEL_ENCODER_MISC_DUAL;
				/*if (pScrn->rgbBits == 8) */
				args.v1.ucMisc |= (1 << 1);
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
			if (drm_detect_hdmi_monitor((struct edid *)connector->edid_blob_ptr))
				args.v2.ucMisc |= PANEL_ENCODER_MISC_HDMI_TYPE;
			args.v2.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			args.v2.ucTruncate = 0;
			args.v2.ucSpatial = 0;
			args.v2.ucTemporal = 0;
			args.v2.ucFRC = 0;
			if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
				if (dig->lvds_misc & (1 << 0))
					args.v2.ucMisc |= PANEL_ENCODER_MISC_DUAL;
				if (dig->lvds_misc & (1 << 5)) {
					args.v2.ucSpatial = PANEL_ENCODER_SPATIAL_DITHER_EN;
					if (dig->lvds_misc & (1 << 1))
						args.v2.ucSpatial |= PANEL_ENCODER_SPATIAL_DITHER_DEPTH;
				}
				if (dig->lvds_misc & (1 << 6)) {
					args.v2.ucTemporal = PANEL_ENCODER_TEMPORAL_DITHER_EN;
					if (dig->lvds_misc & (1 << 1))
						args.v2.ucTemporal |= PANEL_ENCODER_TEMPORAL_DITHER_DEPTH;
					if (((dig->lvds_misc >> 2) & 0x3) == 2)
						args.v2.ucTemporal |= PANEL_ENCODER_TEMPORAL_LEVEL_4;
				}
			} else {
				if (dig_connector->linkb)
					args.v2.ucMisc |= PANEL_ENCODER_MISC_TMDS_LINKB;
				if (radeon_encoder->pixel_clock > 165000)
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
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;

	connector = radeon_get_connector_for_encoder(encoder);
	if (!connector)
		return 0;

	radeon_connector = to_radeon_connector(connector);

	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_DVII:
		if (drm_detect_hdmi_monitor((struct edid *)connector->edid_blob_ptr))
			return ATOM_ENCODER_MODE_HDMI;
		else if (radeon_connector->use_digital)
			return ATOM_ENCODER_MODE_DVI;
		else
			return ATOM_ENCODER_MODE_CRT;
		break;
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_HDMIA:
	case DRM_MODE_CONNECTOR_HDMIB:
	default:
		if (drm_detect_hdmi_monitor((struct edid *)connector->edid_blob_ptr))
			return ATOM_ENCODER_MODE_HDMI;
		else
			return ATOM_ENCODER_MODE_DVI;
		break;
	case DRM_MODE_CONNECTOR_LVDS:
		return ATOM_ENCODER_MODE_LVDS;
		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
		/*if (radeon_output->MonType == MT_DP)
		  return ATOM_ENCODER_MODE_DP;
		  else*/
		if (drm_detect_hdmi_monitor((struct edid *)connector->edid_blob_ptr))
			return ATOM_ENCODER_MODE_HDMI;
		else
			return ATOM_ENCODER_MODE_DVI;
		break;
	case CONNECTOR_DVI_A:
	case CONNECTOR_VGA:
		return ATOM_ENCODER_MODE_CRT;
		break;
	case CONNECTOR_STV:
	case CONNECTOR_CTV:
	case CONNECTOR_DIN:
		/* fix me */
		return ATOM_ENCODER_MODE_TV;
		/*return ATOM_ENCODER_MODE_CV;*/
		break;
	}
}

static void
atombios_dig_encoder_setup(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	DIG_ENCODER_CONTROL_PS_ALLOCATION args;
	int index = 0, num = 0;
	uint8_t frev, crev;
	struct radeon_encoder_atom_dig *dig;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	struct radeon_connector_atom_dig *dig_connector;

	connector = radeon_get_connector_for_encoder(encoder);
	if (!connector)
		return;

	radeon_connector = to_radeon_connector(connector);

	if (!radeon_connector->con_priv)
		return;

	dig_connector = radeon_connector->con_priv;

	if (!radeon_encoder->enc_priv)
		return;

	dig = radeon_encoder->enc_priv;

	memset(&args, 0, sizeof(args));

	if (ASIC_IS_DCE32(rdev)) {
		if (dig->dig_block)
			index = GetIndexIntoMasterTable(COMMAND, DIG2EncoderControl);
		else
			index = GetIndexIntoMasterTable(COMMAND, DIG1EncoderControl);
		num = dig->dig_block + 1;
	} else {
		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			index = GetIndexIntoMasterTable(COMMAND, DIG1EncoderControl);
			num = 1;
			break;
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
			index = GetIndexIntoMasterTable(COMMAND, DIG2EncoderControl);
			num = 2;
			break;
		}
	}

	atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev);

	args.ucAction = action;
	args.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);

	if (ASIC_IS_DCE32(rdev)) {
		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			args.ucConfig = ATOM_ENCODER_CONFIG_V2_TRANSMITTER1;
			break;
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
			args.ucConfig = ATOM_ENCODER_CONFIG_V2_TRANSMITTER2;
			break;
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
			args.ucConfig = ATOM_ENCODER_CONFIG_V2_TRANSMITTER3;
			break;
		}
	} else {
		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			args.ucConfig = ATOM_ENCODER_CONFIG_TRANSMITTER1;
			break;
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
			args.ucConfig = ATOM_ENCODER_CONFIG_TRANSMITTER2;
			break;
		}
	}

	if (radeon_encoder->pixel_clock > 165000) {
		args.ucConfig |= ATOM_ENCODER_CONFIG_LINKA_B;
		args.ucLaneNum = 8;
	} else {
		if (dig_connector->linkb)
			args.ucConfig |= ATOM_ENCODER_CONFIG_LINKB;
		else
			args.ucConfig |= ATOM_ENCODER_CONFIG_LINKA;
		args.ucLaneNum = 4;
	}

	args.ucEncoderMode = atombios_get_encoder_mode(encoder);

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

}

union dig_transmitter_control {
	DIG_TRANSMITTER_CONTROL_PS_ALLOCATION v1;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V2 v2;
};

static void
atombios_dig_transmitter_setup(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	union dig_transmitter_control args;
	int index = 0, num = 0;
	uint8_t frev, crev;
	struct radeon_encoder_atom_dig *dig;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	struct radeon_connector_atom_dig *dig_connector;

	connector = radeon_get_connector_for_encoder(encoder);
	if (!connector)
		return;

	radeon_connector = to_radeon_connector(connector);

	if (!radeon_encoder->enc_priv)
		return;

	dig = radeon_encoder->enc_priv;

	if (!radeon_connector->con_priv)
		return;

	dig_connector = radeon_connector->con_priv;

	memset(&args, 0, sizeof(args));

	if (ASIC_IS_DCE32(rdev))
		index = GetIndexIntoMasterTable(COMMAND, UNIPHYTransmitterControl);
	else {
		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			index = GetIndexIntoMasterTable(COMMAND, DIG1TransmitterControl);
			break;
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
			index = GetIndexIntoMasterTable(COMMAND, DIG2TransmitterControl);
			break;
		}
	}

	atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev);

	args.v1.ucAction = action;

	if (ASIC_IS_DCE32(rdev)) {
		if (radeon_encoder->pixel_clock > 165000) {
			args.v2.usPixelClock = cpu_to_le16((radeon_encoder->pixel_clock * 10 * 2) / 100);
			args.v2.acConfig.fDualLinkConnector = 1;
		} else {
			args.v2.usPixelClock = cpu_to_le16((radeon_encoder->pixel_clock * 10 * 4) / 100);
		}
		if (dig->dig_block)
			args.v2.acConfig.ucEncoderSel = 1;

		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			args.v2.acConfig.ucTransmitterSel = 0;
			num = 0;
			break;
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
			args.v2.acConfig.ucTransmitterSel = 1;
			num = 1;
			break;
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
			args.v2.acConfig.ucTransmitterSel = 2;
			num = 2;
			break;
		}

		if (radeon_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
			if (dig->coherent_mode)
				args.v2.acConfig.fCoherentMode = 1;
		}
	} else {
		args.v1.ucConfig = ATOM_TRANSMITTER_CONFIG_CLKSRC_PPLL;
		args.v1.usPixelClock = cpu_to_le16((radeon_encoder->pixel_clock) / 10);

		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_DIG1_ENCODER;
			if (rdev->flags & RADEON_IS_IGP) {
				if (radeon_encoder->pixel_clock > 165000) {
					args.v1.ucConfig |= (ATOM_TRANSMITTER_CONFIG_8LANE_LINK |
							     ATOM_TRANSMITTER_CONFIG_LINKA_B);
					if (dig_connector->igp_lane_info & 0x3)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_0_7;
					else if (dig_connector->igp_lane_info & 0xc)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_8_15;
				} else {
					args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LINKA;
					if (dig_connector->igp_lane_info & 0x1)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_0_3;
					else if (dig_connector->igp_lane_info & 0x2)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_4_7;
					else if (dig_connector->igp_lane_info & 0x4)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_8_11;
					else if (dig_connector->igp_lane_info & 0x8)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_12_15;
				}
			} else {
				if (radeon_encoder->pixel_clock > 165000)
					args.v1.ucConfig |= (ATOM_TRANSMITTER_CONFIG_8LANE_LINK |
							     ATOM_TRANSMITTER_CONFIG_LINKA_B |
							     ATOM_TRANSMITTER_CONFIG_LANE_0_7);
				else {
					if (dig_connector->linkb)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LINKB | ATOM_TRANSMITTER_CONFIG_LANE_0_3;
					else
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LINKA | ATOM_TRANSMITTER_CONFIG_LANE_0_3;
				}
			}
			break;
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
			args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_DIG2_ENCODER;
			if (radeon_encoder->pixel_clock > 165000)
				args.v1.ucConfig |= (ATOM_TRANSMITTER_CONFIG_8LANE_LINK |
						     ATOM_TRANSMITTER_CONFIG_LINKA_B |
						     ATOM_TRANSMITTER_CONFIG_LANE_0_7);
			else {
				if (dig_connector->linkb)
					args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LINKB | ATOM_TRANSMITTER_CONFIG_LANE_0_3;
				else
					args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LINKA | ATOM_TRANSMITTER_CONFIG_LANE_0_3;
			}
			break;
		}

		if (radeon_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
			if (dig->coherent_mode)
				args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_COHERENT;
		}
	}

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

}

static void atom_rv515_force_tv_scaler(struct radeon_device *rdev)
{

	WREG32(0x659C, 0x0);
	WREG32(0x6594, 0x705);
	WREG32(0x65A4, 0x10001);
	WREG32(0x65D8, 0x0);
	WREG32(0x65B0, 0x0);
	WREG32(0x65C0, 0x0);
	WREG32(0x65D4, 0x0);
	WREG32(0x6578, 0x0);
	WREG32(0x657C, 0x841880A8);
	WREG32(0x6578, 0x1);
	WREG32(0x657C, 0x84208680);
	WREG32(0x6578, 0x2);
	WREG32(0x657C, 0xBFF880B0);
	WREG32(0x6578, 0x100);
	WREG32(0x657C, 0x83D88088);
	WREG32(0x6578, 0x101);
	WREG32(0x657C, 0x84608680);
	WREG32(0x6578, 0x102);
	WREG32(0x657C, 0xBFF080D0);
	WREG32(0x6578, 0x200);
	WREG32(0x657C, 0x83988068);
	WREG32(0x6578, 0x201);
	WREG32(0x657C, 0x84A08680);
	WREG32(0x6578, 0x202);
	WREG32(0x657C, 0xBFF080F8);
	WREG32(0x6578, 0x300);
	WREG32(0x657C, 0x83588058);
	WREG32(0x6578, 0x301);
	WREG32(0x657C, 0x84E08660);
	WREG32(0x6578, 0x302);
	WREG32(0x657C, 0xBFF88120);
	WREG32(0x6578, 0x400);
	WREG32(0x657C, 0x83188040);
	WREG32(0x6578, 0x401);
	WREG32(0x657C, 0x85008660);
	WREG32(0x6578, 0x402);
	WREG32(0x657C, 0xBFF88150);
	WREG32(0x6578, 0x500);
	WREG32(0x657C, 0x82D88030);
	WREG32(0x6578, 0x501);
	WREG32(0x657C, 0x85408640);
	WREG32(0x6578, 0x502);
	WREG32(0x657C, 0xBFF88180);
	WREG32(0x6578, 0x600);
	WREG32(0x657C, 0x82A08018);
	WREG32(0x6578, 0x601);
	WREG32(0x657C, 0x85808620);
	WREG32(0x6578, 0x602);
	WREG32(0x657C, 0xBFF081B8);
	WREG32(0x6578, 0x700);
	WREG32(0x657C, 0x82608010);
	WREG32(0x6578, 0x701);
	WREG32(0x657C, 0x85A08600);
	WREG32(0x6578, 0x702);
	WREG32(0x657C, 0x800081F0);
	WREG32(0x6578, 0x800);
	WREG32(0x657C, 0x8228BFF8);
	WREG32(0x6578, 0x801);
	WREG32(0x657C, 0x85E085E0);
	WREG32(0x6578, 0x802);
	WREG32(0x657C, 0xBFF88228);
	WREG32(0x6578, 0x10000);
	WREG32(0x657C, 0x82A8BF00);
	WREG32(0x6578, 0x10001);
	WREG32(0x657C, 0x82A08CC0);
	WREG32(0x6578, 0x10002);
	WREG32(0x657C, 0x8008BEF8);
	WREG32(0x6578, 0x10100);
	WREG32(0x657C, 0x81F0BF28);
	WREG32(0x6578, 0x10101);
	WREG32(0x657C, 0x83608CA0);
	WREG32(0x6578, 0x10102);
	WREG32(0x657C, 0x8018BED0);
	WREG32(0x6578, 0x10200);
	WREG32(0x657C, 0x8148BF38);
	WREG32(0x6578, 0x10201);
	WREG32(0x657C, 0x84408C80);
	WREG32(0x6578, 0x10202);
	WREG32(0x657C, 0x8008BEB8);
	WREG32(0x6578, 0x10300);
	WREG32(0x657C, 0x80B0BF78);
	WREG32(0x6578, 0x10301);
	WREG32(0x657C, 0x85008C20);
	WREG32(0x6578, 0x10302);
	WREG32(0x657C, 0x8020BEA0);
	WREG32(0x6578, 0x10400);
	WREG32(0x657C, 0x8028BF90);
	WREG32(0x6578, 0x10401);
	WREG32(0x657C, 0x85E08BC0);
	WREG32(0x6578, 0x10402);
	WREG32(0x657C, 0x8018BE90);
	WREG32(0x6578, 0x10500);
	WREG32(0x657C, 0xBFB8BFB0);
	WREG32(0x6578, 0x10501);
	WREG32(0x657C, 0x86C08B40);
	WREG32(0x6578, 0x10502);
	WREG32(0x657C, 0x8010BE90);
	WREG32(0x6578, 0x10600);
	WREG32(0x657C, 0xBF58BFC8);
	WREG32(0x6578, 0x10601);
	WREG32(0x657C, 0x87A08AA0);
	WREG32(0x6578, 0x10602);
	WREG32(0x657C, 0x8010BE98);
	WREG32(0x6578, 0x10700);
	WREG32(0x657C, 0xBF10BFF0);
	WREG32(0x6578, 0x10701);
	WREG32(0x657C, 0x886089E0);
	WREG32(0x6578, 0x10702);
	WREG32(0x657C, 0x8018BEB0);
	WREG32(0x6578, 0x10800);
	WREG32(0x657C, 0xBED8BFE8);
	WREG32(0x6578, 0x10801);
	WREG32(0x657C, 0x89408940);
	WREG32(0x6578, 0x10802);
	WREG32(0x657C, 0xBFE8BED8);
	WREG32(0x6578, 0x20000);
	WREG32(0x657C, 0x80008000);
	WREG32(0x6578, 0x20001);
	WREG32(0x657C, 0x90008000);
	WREG32(0x6578, 0x20002);
	WREG32(0x657C, 0x80008000);
	WREG32(0x6578, 0x20003);
	WREG32(0x657C, 0x80008000);
	WREG32(0x6578, 0x20100);
	WREG32(0x657C, 0x80108000);
	WREG32(0x6578, 0x20101);
	WREG32(0x657C, 0x8FE0BF70);
	WREG32(0x6578, 0x20102);
	WREG32(0x657C, 0xBFE880C0);
	WREG32(0x6578, 0x20103);
	WREG32(0x657C, 0x80008000);
	WREG32(0x6578, 0x20200);
	WREG32(0x657C, 0x8018BFF8);
	WREG32(0x6578, 0x20201);
	WREG32(0x657C, 0x8F80BF08);
	WREG32(0x6578, 0x20202);
	WREG32(0x657C, 0xBFD081A0);
	WREG32(0x6578, 0x20203);
	WREG32(0x657C, 0xBFF88000);
	WREG32(0x6578, 0x20300);
	WREG32(0x657C, 0x80188000);
	WREG32(0x6578, 0x20301);
	WREG32(0x657C, 0x8EE0BEC0);
	WREG32(0x6578, 0x20302);
	WREG32(0x657C, 0xBFB082A0);
	WREG32(0x6578, 0x20303);
	WREG32(0x657C, 0x80008000);
	WREG32(0x6578, 0x20400);
	WREG32(0x657C, 0x80188000);
	WREG32(0x6578, 0x20401);
	WREG32(0x657C, 0x8E00BEA0);
	WREG32(0x6578, 0x20402);
	WREG32(0x657C, 0xBF8883C0);
	WREG32(0x6578, 0x20403);
	WREG32(0x657C, 0x80008000);
	WREG32(0x6578, 0x20500);
	WREG32(0x657C, 0x80188000);
	WREG32(0x6578, 0x20501);
	WREG32(0x657C, 0x8D00BE90);
	WREG32(0x6578, 0x20502);
	WREG32(0x657C, 0xBF588500);
	WREG32(0x6578, 0x20503);
	WREG32(0x657C, 0x80008008);
	WREG32(0x6578, 0x20600);
	WREG32(0x657C, 0x80188000);
	WREG32(0x6578, 0x20601);
	WREG32(0x657C, 0x8BC0BE98);
	WREG32(0x6578, 0x20602);
	WREG32(0x657C, 0xBF308660);
	WREG32(0x6578, 0x20603);
	WREG32(0x657C, 0x80008008);
	WREG32(0x6578, 0x20700);
	WREG32(0x657C, 0x80108000);
	WREG32(0x6578, 0x20701);
	WREG32(0x657C, 0x8A80BEB0);
	WREG32(0x6578, 0x20702);
	WREG32(0x657C, 0xBF0087C0);
	WREG32(0x6578, 0x20703);
	WREG32(0x657C, 0x80008008);
	WREG32(0x6578, 0x20800);
	WREG32(0x657C, 0x80108000);
	WREG32(0x6578, 0x20801);
	WREG32(0x657C, 0x8920BED0);
	WREG32(0x6578, 0x20802);
	WREG32(0x657C, 0xBED08920);
	WREG32(0x6578, 0x20803);
	WREG32(0x657C, 0x80008010);
	WREG32(0x6578, 0x30000);
	WREG32(0x657C, 0x90008000);
	WREG32(0x6578, 0x30001);
	WREG32(0x657C, 0x80008000);
	WREG32(0x6578, 0x30100);
	WREG32(0x657C, 0x8FE0BF90);
	WREG32(0x6578, 0x30101);
	WREG32(0x657C, 0xBFF880A0);
	WREG32(0x6578, 0x30200);
	WREG32(0x657C, 0x8F60BF40);
	WREG32(0x6578, 0x30201);
	WREG32(0x657C, 0xBFE88180);
	WREG32(0x6578, 0x30300);
	WREG32(0x657C, 0x8EC0BF00);
	WREG32(0x6578, 0x30301);
	WREG32(0x657C, 0xBFC88280);
	WREG32(0x6578, 0x30400);
	WREG32(0x657C, 0x8DE0BEE0);
	WREG32(0x6578, 0x30401);
	WREG32(0x657C, 0xBFA083A0);
	WREG32(0x6578, 0x30500);
	WREG32(0x657C, 0x8CE0BED0);
	WREG32(0x6578, 0x30501);
	WREG32(0x657C, 0xBF7884E0);
	WREG32(0x6578, 0x30600);
	WREG32(0x657C, 0x8BA0BED8);
	WREG32(0x6578, 0x30601);
	WREG32(0x657C, 0xBF508640);
	WREG32(0x6578, 0x30700);
	WREG32(0x657C, 0x8A60BEE8);
	WREG32(0x6578, 0x30701);
	WREG32(0x657C, 0xBF2087A0);
	WREG32(0x6578, 0x30800);
	WREG32(0x657C, 0x8900BF00);
	WREG32(0x6578, 0x30801);
	WREG32(0x657C, 0xBF008900);
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
	if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT))
		WREG32(reg, (ATOM_S3_TV1_ACTIVE |
			     (radeon_crtc->crtc_id << 18)));
	else if (radeon_encoder->devices & (ATOM_DEVICE_CV_SUPPORT))
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
atombios_overscan_setup(struct drm_encoder *encoder,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	SET_CRTC_OVERSCAN_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, SetCRTC_OverScan);

	memset(&args, 0, sizeof(args));

	args.usOverscanRight = 0;
	args.usOverscanLeft = 0;
	args.usOverscanBottom = 0;
	args.usOverscanTop = 0;
	args.ucCRTC = radeon_crtc->crtc_id;

	if (radeon_encoder->flags & RADEON_USE_RMX) {
		if (radeon_encoder->rmx_type == RMX_FULL) {
			args.usOverscanRight = 0;
			args.usOverscanLeft = 0;
			args.usOverscanBottom = 0;
			args.usOverscanTop = 0;
		} else if (radeon_encoder->rmx_type == RMX_CENTER) {
			args.usOverscanTop = (adjusted_mode->crtc_vdisplay - mode->crtc_vdisplay) / 2;
			args.usOverscanBottom = (adjusted_mode->crtc_vdisplay - mode->crtc_vdisplay) / 2;
			args.usOverscanLeft = (adjusted_mode->crtc_hdisplay - mode->crtc_hdisplay) / 2;
			args.usOverscanRight = (adjusted_mode->crtc_hdisplay - mode->crtc_hdisplay) / 2;
		} else if (radeon_encoder->rmx_type == RMX_ASPECT) {
			int a1 = mode->crtc_vdisplay * adjusted_mode->crtc_hdisplay;
			int a2 = adjusted_mode->crtc_vdisplay * mode->crtc_hdisplay;

			if (a1 > a2) {
				args.usOverscanLeft = (adjusted_mode->crtc_hdisplay - (a2 / mode->crtc_vdisplay)) / 2;
				args.usOverscanRight = (adjusted_mode->crtc_hdisplay - (a2 / mode->crtc_vdisplay)) / 2;
			} else if (a2 > a1) {
				args.usOverscanLeft = (adjusted_mode->crtc_vdisplay - (a1 / mode->crtc_hdisplay)) / 2;
				args.usOverscanRight = (adjusted_mode->crtc_vdisplay - (a1 / mode->crtc_hdisplay)) / 2;
			}
		}
	}

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

}

static void
atombios_scaler_setup(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	ENABLE_SCALER_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, EnableScaler);
	/* fixme - fill in enc_priv for atom dac */
	enum radeon_tv_std tv_std = TV_STD_NTSC;

	if (!ASIC_IS_AVIVO(rdev) && radeon_crtc->crtc_id)
		return;

	memset(&args, 0, sizeof(args));

	args.ucScaler = radeon_crtc->crtc_id;

	if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT)) {
		switch (tv_std) {
		case TV_STD_NTSC:
		default:
			args.ucTVStandard = ATOM_TV_NTSC;
			break;
		case TV_STD_PAL:
			args.ucTVStandard = ATOM_TV_PAL;
			break;
		case TV_STD_PAL_M:
			args.ucTVStandard = ATOM_TV_PALM;
			break;
		case TV_STD_PAL_60:
			args.ucTVStandard = ATOM_TV_PAL60;
			break;
		case TV_STD_NTSC_J:
			args.ucTVStandard = ATOM_TV_NTSCJ;
			break;
		case TV_STD_SCART_PAL:
			args.ucTVStandard = ATOM_TV_PAL; /* ??? */
			break;
		case TV_STD_SECAM:
			args.ucTVStandard = ATOM_TV_SECAM;
			break;
		case TV_STD_PAL_CN:
			args.ucTVStandard = ATOM_TV_PALCN;
			break;
		}
		args.ucEnable = SCALER_ENABLE_MULTITAP_MODE;
	} else if (radeon_encoder->devices & (ATOM_DEVICE_CV_SUPPORT)) {
		args.ucTVStandard = ATOM_TV_CV;
		args.ucEnable = SCALER_ENABLE_MULTITAP_MODE;
	} else if (radeon_encoder->flags & RADEON_USE_RMX) {
		if (radeon_encoder->rmx_type == RMX_FULL)
			args.ucEnable = ATOM_SCALER_EXPANSION;
		else if (radeon_encoder->rmx_type == RMX_CENTER)
			args.ucEnable = ATOM_SCALER_CENTER;
		else if (radeon_encoder->rmx_type == RMX_ASPECT)
			args.ucEnable = ATOM_SCALER_EXPANSION;
	} else {
		if (ASIC_IS_AVIVO(rdev))
			args.ucEnable = ATOM_SCALER_DISABLE;
		else
			args.ucEnable = ATOM_SCALER_CENTER;
	}

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

	if (radeon_encoder->devices & (ATOM_DEVICE_CV_SUPPORT | ATOM_DEVICE_TV_SUPPORT)
	    && rdev->family >= CHIP_RV515 && rdev->family <= CHIP_RV570) {
		atom_rv515_force_tv_scaler(rdev);
	}

}

static void
radeon_atom_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION args;
	int index = 0;
	bool is_dig = false;

	memset(&args, 0, sizeof(args));

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
		index = GetIndexIntoMasterTable(COMMAND, TMDSAOutputControl);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
		is_dig = true;
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
		if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT))
			index = GetIndexIntoMasterTable(COMMAND, TV1OutputControl);
		else if (radeon_encoder->devices & (ATOM_DEVICE_CV_SUPPORT))
			index = GetIndexIntoMasterTable(COMMAND, CV1OutputControl);
		else
			index = GetIndexIntoMasterTable(COMMAND, DAC1OutputControl);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT))
			index = GetIndexIntoMasterTable(COMMAND, TV1OutputControl);
		else if (radeon_encoder->devices & (ATOM_DEVICE_CV_SUPPORT))
			index = GetIndexIntoMasterTable(COMMAND, CV1OutputControl);
		else
			index = GetIndexIntoMasterTable(COMMAND, DAC2OutputControl);
		break;
	}

	if (is_dig) {
		switch (mode) {
		case DRM_MODE_DPMS_ON:
			atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_ENABLE);
			break;
		case DRM_MODE_DPMS_STANDBY:
		case DRM_MODE_DPMS_SUSPEND:
		case DRM_MODE_DPMS_OFF:
			atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_DISABLE);
			break;
		}
	} else {
		switch (mode) {
		case DRM_MODE_DPMS_ON:
			args.ucAction = ATOM_ENABLE;
			break;
		case DRM_MODE_DPMS_STANDBY:
		case DRM_MODE_DPMS_SUSPEND:
		case DRM_MODE_DPMS_OFF:
			args.ucAction = ATOM_DISABLE;
			break;
		}
		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
	}
	radeon_atombios_encoder_dpms_scratch_regs(encoder, (mode == DRM_MODE_DPMS_ON) ? true : false);
}

union crtc_sourc_param {
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
	union crtc_sourc_param args;
	int index = GetIndexIntoMasterTable(COMMAND, SelectCRTC_Source);
	uint8_t frev, crev;

	memset(&args, 0, sizeof(args));

	atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev);

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
				if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT))
					args.v1.ucDevice = ATOM_DEVICE_TV1_INDEX;
				else if (radeon_encoder->devices & (ATOM_DEVICE_CV_SUPPORT))
					args.v1.ucDevice = ATOM_DEVICE_CV_INDEX;
				else
					args.v1.ucDevice = ATOM_DEVICE_CRT1_INDEX;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_DAC2:
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
				if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT))
					args.v1.ucDevice = ATOM_DEVICE_TV1_INDEX;
				else if (radeon_encoder->devices & (ATOM_DEVICE_CV_SUPPORT))
					args.v1.ucDevice = ATOM_DEVICE_CV_INDEX;
				else
					args.v1.ucDevice = ATOM_DEVICE_CRT2_INDEX;
				break;
			}
			break;
		case 2:
			args.v2.ucCRTC = radeon_crtc->crtc_id;
			args.v2.ucEncodeMode = atombios_get_encoder_mode(encoder);
			switch (radeon_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
				if (ASIC_IS_DCE32(rdev)) {
					if (radeon_crtc->crtc_id)
						args.v2.ucEncoderID = ASIC_INT_DIG2_ENCODER_ID;
					else
						args.v2.ucEncoderID = ASIC_INT_DIG1_ENCODER_ID;
				} else
					args.v2.ucEncoderID = ASIC_INT_DIG1_ENCODER_ID;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
				args.v2.ucEncoderID = ASIC_INT_DVO_ENCODER_ID;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
				args.v2.ucEncoderID = ASIC_INT_DIG2_ENCODER_ID;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
				if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT))
					args.v2.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else if (radeon_encoder->devices & (ATOM_DEVICE_CV_SUPPORT))
					args.v2.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else
					args.v2.ucEncoderID = ASIC_INT_DAC1_ENCODER_ID;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
				if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT))
					args.v2.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else if (radeon_encoder->devices & (ATOM_DEVICE_CV_SUPPORT))
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
	if (ASIC_IS_AVIVO(rdev) && (mode->flags & DRM_MODE_FLAG_INTERLACE))
		WREG32(AVIVO_D1MODE_DATA_FORMAT + radeon_crtc->crtc_offset, AVIVO_D1MODE_INTERLEAVE_EN);
}

static void
radeon_atom_encoder_mode_set(struct drm_encoder *encoder,
			     struct drm_display_mode *mode,
			     struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);

	if (radeon_encoder->enc_priv) {
		struct radeon_encoder_atom_dig *dig;

		dig = radeon_encoder->enc_priv;
		dig->dig_block = radeon_crtc->crtc_id;
	}
	radeon_encoder->pixel_clock = adjusted_mode->clock;

	radeon_atombios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);
	atombios_overscan_setup(encoder, mode, adjusted_mode);
	atombios_scaler_setup(encoder);
	atombios_set_encoder_crtc_source(encoder);

	if (ASIC_IS_AVIVO(rdev)) {
		if (radeon_encoder->devices & (ATOM_DEVICE_CV_SUPPORT | ATOM_DEVICE_TV_SUPPORT))
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
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
		/* disable the encoder and transmitter */
		atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_DISABLE);
		atombios_dig_encoder_setup(encoder, ATOM_DISABLE);

		/* setup and enable the encoder and transmitter */
		atombios_dig_encoder_setup(encoder, ATOM_ENABLE);
		atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_SETUP);
		atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_ENABLE);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DDI:
		atombios_ddia_setup(encoder, ATOM_ENABLE);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
		atombios_external_tmds_setup(encoder, ATOM_ENABLE);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_DAC2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		atombios_dac_setup(encoder, ATOM_ENABLE);
		if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT | ATOM_DEVICE_CV_SUPPORT))
			atombios_tv_setup(encoder, ATOM_ENABLE);
		break;
	}
	atombios_apply_encoder_quirks(encoder, adjusted_mode);
}

static bool
atombios_dac_load_detect(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);

	if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT |
				       ATOM_DEVICE_CV_SUPPORT |
				       ATOM_DEVICE_CRT_SUPPORT)) {
		DAC_LOAD_DETECTION_PS_ALLOCATION args;
		int index = GetIndexIntoMasterTable(COMMAND, DAC_LoadDetection);
		uint8_t frev, crev;

		memset(&args, 0, sizeof(args));

		atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev);

		args.sDacload.ucMisc = 0;

		if ((radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_DAC1) ||
		    (radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1))
			args.sDacload.ucDacType = ATOM_DAC_A;
		else
			args.sDacload.ucDacType = ATOM_DAC_B;

		if (radeon_encoder->devices & ATOM_DEVICE_CRT1_SUPPORT)
			args.sDacload.usDeviceID = cpu_to_le16(ATOM_DEVICE_CRT1_SUPPORT);
		else if (radeon_encoder->devices & ATOM_DEVICE_CRT2_SUPPORT)
			args.sDacload.usDeviceID = cpu_to_le16(ATOM_DEVICE_CRT2_SUPPORT);
		else if (radeon_encoder->devices & ATOM_DEVICE_CV_SUPPORT) {
			args.sDacload.usDeviceID = cpu_to_le16(ATOM_DEVICE_CV_SUPPORT);
			if (crev >= 3)
				args.sDacload.ucMisc = DAC_LOAD_MISC_YPrPb;
		} else if (radeon_encoder->devices & ATOM_DEVICE_TV1_SUPPORT) {
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
	uint32_t bios_0_scratch;

	if (!atombios_dac_load_detect(encoder)) {
		DRM_DEBUG("detect returned false \n");
		return connector_status_unknown;
	}

	if (rdev->family >= CHIP_R600)
		bios_0_scratch = RREG32(R600_BIOS_0_SCRATCH);
	else
		bios_0_scratch = RREG32(RADEON_BIOS_0_SCRATCH);

	DRM_DEBUG("Bios 0 scratch %x\n", bios_0_scratch);
	if (radeon_encoder->devices & ATOM_DEVICE_CRT1_SUPPORT) {
		if (bios_0_scratch & ATOM_S0_CRT1_MASK)
			return connector_status_connected;
	} else if (radeon_encoder->devices & ATOM_DEVICE_CRT2_SUPPORT) {
		if (bios_0_scratch & ATOM_S0_CRT2_MASK)
			return connector_status_connected;
	} else if (radeon_encoder->devices & ATOM_DEVICE_CV_SUPPORT) {
		if (bios_0_scratch & (ATOM_S0_CV_MASK|ATOM_S0_CV_MASK_A))
			return connector_status_connected;
	} else if (radeon_encoder->devices & ATOM_DEVICE_TV1_SUPPORT) {
		if (bios_0_scratch & (ATOM_S0_TV1_COMPOSITE | ATOM_S0_TV1_COMPOSITE_A))
			return connector_status_connected; /* CTV */
		else if (bios_0_scratch & (ATOM_S0_TV1_SVIDEO | ATOM_S0_TV1_SVIDEO_A))
			return connector_status_connected; /* STV */
	}
	return connector_status_disconnected;
}

static void radeon_atom_encoder_prepare(struct drm_encoder *encoder)
{
	radeon_atom_output_lock(encoder, true);
	radeon_atom_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void radeon_atom_encoder_commit(struct drm_encoder *encoder)
{
	radeon_atom_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
	radeon_atom_output_lock(encoder, false);
}

static const struct drm_encoder_helper_funcs radeon_atom_dig_helper_funcs = {
	.dpms = radeon_atom_encoder_dpms,
	.mode_fixup = radeon_atom_mode_fixup,
	.prepare = radeon_atom_encoder_prepare,
	.mode_set = radeon_atom_encoder_mode_set,
	.commit = radeon_atom_encoder_commit,
	/* no detect for TMDS/LVDS yet */
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
	kfree(radeon_encoder->enc_priv);
	drm_encoder_cleanup(encoder);
	kfree(radeon_encoder);
}

static const struct drm_encoder_funcs radeon_atom_enc_funcs = {
	.destroy = radeon_enc_destroy,
};

struct radeon_encoder_atom_dig *
radeon_atombios_set_dig_info(struct radeon_encoder *radeon_encoder)
{
	struct radeon_encoder_atom_dig *dig = kzalloc(sizeof(struct radeon_encoder_atom_dig), GFP_KERNEL);

	if (!dig)
		return NULL;

	/* coherent mode by default */
	dig->coherent_mode = true;

	return dig;
}

void
radeon_add_atom_encoder(struct drm_device *dev, uint32_t encoder_id, uint32_t supported_device)
{
	struct drm_encoder *encoder;
	struct radeon_encoder *radeon_encoder;

	/* see if we already added it */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		radeon_encoder = to_radeon_encoder(encoder);
		if (radeon_encoder->encoder_id == encoder_id) {
			radeon_encoder->devices |= supported_device;
			return;
		}

	}

	/* add a new one */
	radeon_encoder = kzalloc(sizeof(struct radeon_encoder), GFP_KERNEL);
	if (!radeon_encoder)
		return;

	encoder = &radeon_encoder->base;
	encoder->possible_crtcs = 0x3;
	encoder->possible_clones = 0;

	radeon_encoder->enc_priv = NULL;

	radeon_encoder->encoder_id = encoder_id;
	radeon_encoder->devices = supported_device;

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_LVDS:
	case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
		if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
			radeon_encoder->rmx_type = RMX_FULL;
			drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs, DRM_MODE_ENCODER_LVDS);
			radeon_encoder->enc_priv = radeon_atombios_get_lvds_info(radeon_encoder);
		} else {
			drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs, DRM_MODE_ENCODER_TMDS);
			radeon_encoder->enc_priv = radeon_atombios_set_dig_info(radeon_encoder);
		}
		drm_encoder_helper_add(encoder, &radeon_atom_dig_helper_funcs);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC1:
		drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs, DRM_MODE_ENCODER_DAC);
		drm_encoder_helper_add(encoder, &radeon_atom_dac_helper_funcs);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DAC2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs, DRM_MODE_ENCODER_TVDAC);
		drm_encoder_helper_add(encoder, &radeon_atom_dac_helper_funcs);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_DDI:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
		drm_encoder_init(dev, encoder, &radeon_atom_enc_funcs, DRM_MODE_ENCODER_TMDS);
		radeon_encoder->enc_priv = radeon_atombios_set_dig_info(radeon_encoder);
		drm_encoder_helper_add(encoder, &radeon_atom_dig_helper_funcs);
		break;
	}
}
