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

/* evil but including atombios.h is much worse */
bool radeon_atom_get_tv_timings(struct radeon_device *rdev, int index,
				struct drm_display_mode *mode);

static uint32_t radeon_encoder_clones(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_encoder *clone_encoder;
	uint32_t index_mask = 0;
	int count;

	/* DIG routing gets problematic */
	if (rdev->family >= CHIP_R600)
		return index_mask;
	/* LVDS/TV are too wacky */
	if (radeon_encoder->devices & ATOM_DEVICE_LCD_SUPPORT)
		return index_mask;
	/* DVO requires 2x ppll clocks depending on tmds chip */
	if (radeon_encoder->devices & ATOM_DEVICE_DFP2_SUPPORT)
		return index_mask;
	
	count = -1;
	list_for_each_entry(clone_encoder, &dev->mode_config.encoder_list, head) {
		struct radeon_encoder *radeon_clone = to_radeon_encoder(clone_encoder);
		count++;

		if (clone_encoder == encoder)
			continue;
		if (radeon_clone->devices & (ATOM_DEVICE_LCD_SUPPORT))
			continue;
		if (radeon_clone->devices & ATOM_DEVICE_DFP2_SUPPORT)
			continue;
		else
			index_mask |= (1 << count);
	}
	return index_mask;
}

void radeon_setup_encoder_clones(struct drm_device *dev)
{
	struct drm_encoder *encoder;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		encoder->possible_clones = radeon_encoder_clones(encoder);
	}
}

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

static inline bool radeon_encoder_is_digital(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_LVDS:
	case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
	case ENCODER_OBJECT_ID_INTERNAL_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_DDI:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
		return true;
	default:
		return false;
	}
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

void radeon_encoder_set_active_device(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_connector *connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			struct radeon_connector *radeon_connector = to_radeon_connector(connector);
			radeon_encoder->active_device = radeon_encoder->devices & radeon_connector->devices;
			DRM_DEBUG("setting active device to %08x from %08x %08x for encoder %d\n",
				  radeon_encoder->active_device, radeon_encoder->devices,
				  radeon_connector->devices, encoder->encoder_type);
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
		if (radeon_encoder->active_device & radeon_connector->devices)
			return connector;
	}
	return NULL;
}

static bool radeon_atom_mode_fixup(struct drm_encoder *encoder,
				   struct drm_display_mode *mode,
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

	/* get the native mode for LVDS */
	if (radeon_encoder->active_device & (ATOM_DEVICE_LCD_SUPPORT)) {
		struct drm_display_mode *native_mode = &radeon_encoder->native_mode;
		int mode_id = adjusted_mode->base.id;
		*adjusted_mode = *native_mode;
		if (!ASIC_IS_AVIVO(rdev)) {
			adjusted_mode->hdisplay = mode->hdisplay;
			adjusted_mode->vdisplay = mode->vdisplay;
			adjusted_mode->crtc_hdisplay = mode->hdisplay;
			adjusted_mode->crtc_vdisplay = mode->vdisplay;
		}
		adjusted_mode->base.id = mode_id;
	}

	/* get the native mode for TV */
	if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT)) {
		struct radeon_encoder_atom_dac *tv_dac = radeon_encoder->enc_priv;
		if (tv_dac) {
			if (tv_dac->tv_std == TV_STD_NTSC ||
			    tv_dac->tv_std == TV_STD_NTSC_J ||
			    tv_dac->tv_std == TV_STD_PAL_M)
				radeon_atom_get_tv_timings(rdev, 0, adjusted_mode);
			else
				radeon_atom_get_tv_timings(rdev, 1, adjusted_mode);
		}
	}

	if (ASIC_IS_DCE3(rdev) &&
	    (radeon_encoder->active_device & (ATOM_DEVICE_DFP_SUPPORT))) {
		struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
		radeon_dp_set_link_config(connector, mode);
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
	int index = 0, num = 0;
	struct radeon_encoder_atom_dac *dac_info = radeon_encoder->enc_priv;
	enum radeon_tv_std tv_std = TV_STD_NTSC;

	if (dac_info->tv_std)
		tv_std = dac_info->tv_std;

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

	if (radeon_encoder->active_device & (ATOM_DEVICE_CRT_SUPPORT))
		args.ucDacStandard = ATOM_DAC1_PS2;
	else if (radeon_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
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
	struct radeon_encoder_atom_dac *dac_info = radeon_encoder->enc_priv;
	enum radeon_tv_std tv_std = TV_STD_NTSC;

	if (dac_info->tv_std)
		tv_std = dac_info->tv_std;

	memset(&args, 0, sizeof(args));

	index = GetIndexIntoMasterTable(COMMAND, TVEncoderControl);

	args.sTVEncoder.ucAction = action;

	if (radeon_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
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

void
atombios_digital_setup(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	union lvds_encoder_control args;
	int index = 0;
	int hdmi_detected = 0;
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

	if (drm_detect_hdmi_monitor(radeon_connector->edid))
		hdmi_detected = 1;

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
			if (hdmi_detected)
				args.v1.ucMisc |= PANEL_ENCODER_MISC_HDMI_TYPE;
			args.v1.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
				if (dig->lvds_misc & ATOM_PANEL_MISC_DUAL)
					args.v1.ucMisc |= PANEL_ENCODER_MISC_DUAL;
				if (dig->lvds_misc & ATOM_PANEL_MISC_888RGB)
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
			if (hdmi_detected)
				args.v2.ucMisc |= PANEL_ENCODER_MISC_HDMI_TYPE;
			args.v2.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
			args.v2.ucTruncate = 0;
			args.v2.ucSpatial = 0;
			args.v2.ucTemporal = 0;
			args.v2.ucFRC = 0;
			if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
				if (dig->lvds_misc & ATOM_PANEL_MISC_DUAL)
					args.v2.ucMisc |= PANEL_ENCODER_MISC_DUAL;
				if (dig->lvds_misc & ATOM_PANEL_MISC_SPATIAL) {
					args.v2.ucSpatial = PANEL_ENCODER_SPATIAL_DITHER_EN;
					if (dig->lvds_misc & ATOM_PANEL_MISC_888RGB)
						args.v2.ucSpatial |= PANEL_ENCODER_SPATIAL_DITHER_DEPTH;
				}
				if (dig->lvds_misc & ATOM_PANEL_MISC_TEMPORAL) {
					args.v2.ucTemporal = PANEL_ENCODER_TEMPORAL_DITHER_EN;
					if (dig->lvds_misc & ATOM_PANEL_MISC_888RGB)
						args.v2.ucTemporal |= PANEL_ENCODER_TEMPORAL_DITHER_DEPTH;
					if (((dig->lvds_misc >> ATOM_PANEL_MISC_GREY_LEVEL_SHIFT) & 0x3) == 2)
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
	r600_hdmi_enable(encoder, hdmi_detected);
}

int
atombios_get_encoder_mode(struct drm_encoder *encoder)
{
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	struct radeon_connector_atom_dig *radeon_dig_connector;

	connector = radeon_get_connector_for_encoder(encoder);
	if (!connector)
		return 0;

	radeon_connector = to_radeon_connector(connector);

	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_HDMIB: /* HDMI-B is basically DL-DVI; analog works fine */
		if (drm_detect_hdmi_monitor(radeon_connector->edid))
			return ATOM_ENCODER_MODE_HDMI;
		else if (radeon_connector->use_digital)
			return ATOM_ENCODER_MODE_DVI;
		else
			return ATOM_ENCODER_MODE_CRT;
		break;
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_HDMIA:
	default:
		if (drm_detect_hdmi_monitor(radeon_connector->edid))
			return ATOM_ENCODER_MODE_HDMI;
		else
			return ATOM_ENCODER_MODE_DVI;
		break;
	case DRM_MODE_CONNECTOR_LVDS:
		return ATOM_ENCODER_MODE_LVDS;
		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_eDP:
		radeon_dig_connector = radeon_connector->con_priv;
		if ((radeon_dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_DISPLAYPORT) ||
		    (radeon_dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_eDP))
			return ATOM_ENCODER_MODE_DP;
		else if (drm_detect_hdmi_monitor(radeon_connector->edid))
			return ATOM_ENCODER_MODE_HDMI;
		else
			return ATOM_ENCODER_MODE_DVI;
		break;
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
 * Routing
 * crtc -> dig encoder -> UNIPHY/LVTMA (1 or 2 links)
 * Examples:
 * crtc0 -> dig2 -> LVTMA   links A+B -> TMDS/HDMI
 * crtc1 -> dig1 -> UNIPHY0 link  B   -> DP
 * crtc0 -> dig1 -> UNIPHY2 link  A   -> LVDS
 * crtc1 -> dig2 -> UNIPHY1 link  B+A -> TMDS/HDMI
 */
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

	if (dig->dig_encoder)
		index = GetIndexIntoMasterTable(COMMAND, DIG2EncoderControl);
	else
		index = GetIndexIntoMasterTable(COMMAND, DIG1EncoderControl);
	num = dig->dig_encoder + 1;

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

	args.ucEncoderMode = atombios_get_encoder_mode(encoder);

	if (args.ucEncoderMode == ATOM_ENCODER_MODE_DP) {
		if (dig_connector->dp_clock == 270000)
			args.ucConfig |= ATOM_ENCODER_CONFIG_DPLINKRATE_2_70GHZ;
		args.ucLaneNum = dig_connector->dp_lane_count;
	} else if (radeon_encoder->pixel_clock > 165000)
		args.ucLaneNum = 8;
	else
		args.ucLaneNum = 4;

	if (dig_connector->linkb)
		args.ucConfig |= ATOM_ENCODER_CONFIG_LINKB;
	else
		args.ucConfig |= ATOM_ENCODER_CONFIG_LINKA;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

}

union dig_transmitter_control {
	DIG_TRANSMITTER_CONTROL_PS_ALLOCATION v1;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V2 v2;
};

void
atombios_dig_transmitter_setup(struct drm_encoder *encoder, int action, uint8_t lane_num, uint8_t lane_set)
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
	bool is_dp = false;

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

	if (atombios_get_encoder_mode(encoder) == ATOM_ENCODER_MODE_DP)
		is_dp = true;

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
	if (action == ATOM_TRANSMITTER_ACTION_INIT) {
		args.v1.usInitInfo = radeon_connector->connector_object_id;
	} else if (action == ATOM_TRANSMITTER_ACTION_SETUP_VSEMPH) {
		args.v1.asMode.ucLaneSel = lane_num;
		args.v1.asMode.ucLaneSet = lane_set;
	} else {
		if (is_dp)
			args.v1.usPixelClock =
				cpu_to_le16(dig_connector->dp_clock / 10);
		else if (radeon_encoder->pixel_clock > 165000)
			args.v1.usPixelClock = cpu_to_le16((radeon_encoder->pixel_clock / 2) / 10);
		else
			args.v1.usPixelClock = cpu_to_le16(radeon_encoder->pixel_clock / 10);
	}
	if (ASIC_IS_DCE32(rdev)) {
		if (dig->dig_encoder == 1)
			args.v2.acConfig.ucEncoderSel = 1;
		if (dig_connector->linkb)
			args.v2.acConfig.ucLinkSel = 1;

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

		if (is_dp)
			args.v2.acConfig.fCoherentMode = 1;
		else if (radeon_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
			if (dig->coherent_mode)
				args.v2.acConfig.fCoherentMode = 1;
		}
	} else {

		args.v1.ucConfig = ATOM_TRANSMITTER_CONFIG_CLKSRC_PPLL;

		if (dig->dig_encoder)
			args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_DIG2_ENCODER;
		else
			args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_DIG1_ENCODER;

		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			if (rdev->flags & RADEON_IS_IGP) {
				if (radeon_encoder->pixel_clock > 165000) {
					if (dig_connector->igp_lane_info & 0x3)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_0_7;
					else if (dig_connector->igp_lane_info & 0xc)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_8_15;
				} else {
					if (dig_connector->igp_lane_info & 0x1)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_0_3;
					else if (dig_connector->igp_lane_info & 0x2)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_4_7;
					else if (dig_connector->igp_lane_info & 0x4)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_8_11;
					else if (dig_connector->igp_lane_info & 0x8)
						args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LANE_12_15;
				}
			}
			break;
		}

		if (radeon_encoder->pixel_clock > 165000)
			args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_8LANE_LINK;

		if (dig_connector->linkb)
			args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LINKB;
		else
			args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_LINKA;

		if (is_dp)
			args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_COHERENT;
		else if (radeon_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
			if (dig->coherent_mode)
				args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_COHERENT;
		}
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
radeon_atom_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION args;
	int index = 0;
	bool is_dig = false;

	memset(&args, 0, sizeof(args));

	DRM_DEBUG("encoder dpms %d to mode %d, devices %08x, active_devices %08x\n",
		  radeon_encoder->encoder_id, mode, radeon_encoder->devices,
		  radeon_encoder->active_device);
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
	}

	if (is_dig) {
		switch (mode) {
		case DRM_MODE_DPMS_ON:
			atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_ENABLE_OUTPUT, 0, 0);
			{
				struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
				dp_link_train(encoder, connector);
			}
			break;
		case DRM_MODE_DPMS_STANDBY:
		case DRM_MODE_DPMS_SUSPEND:
		case DRM_MODE_DPMS_OFF:
			atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_DISABLE_OUTPUT, 0, 0);
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
	struct radeon_encoder_atom_dig *dig;

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
			args.v2.ucEncodeMode = atombios_get_encoder_mode(encoder);
			switch (radeon_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
				dig = radeon_encoder->enc_priv;
				if (dig->dig_encoder)
					args.v2.ucEncoderID = ASIC_INT_DIG2_ENCODER_ID;
				else
					args.v2.ucEncoderID = ASIC_INT_DIG1_ENCODER_ID;
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
	if (!(radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))) {
		if (ASIC_IS_AVIVO(rdev) && (mode->flags & DRM_MODE_FLAG_INTERLACE))
			WREG32(AVIVO_D1MODE_DATA_FORMAT + radeon_crtc->crtc_offset,
			       AVIVO_D1MODE_INTERLEAVE_EN);
	}
}

static int radeon_atom_pick_dig_encoder(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_encoder *test_encoder;
	struct radeon_encoder_atom_dig *dig;
	uint32_t dig_enc_in_use = 0;
	/* on DCE32 and encoder can driver any block so just crtc id */
	if (ASIC_IS_DCE32(rdev)) {
		return radeon_crtc->crtc_id;
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

	if (radeon_encoder->active_device &
	    (ATOM_DEVICE_DFP_SUPPORT | ATOM_DEVICE_LCD_SUPPORT)) {
		struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
		if (dig)
			dig->dig_encoder = radeon_atom_pick_dig_encoder(encoder);
	}
	radeon_encoder->pixel_clock = adjusted_mode->clock;

	radeon_atombios_encoder_crtc_scratch_regs(encoder, radeon_crtc->crtc_id);
	atombios_set_encoder_crtc_source(encoder);

	if (ASIC_IS_AVIVO(rdev)) {
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
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
		/* disable the encoder and transmitter */
		atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_DISABLE, 0, 0);
		atombios_dig_encoder_setup(encoder, ATOM_DISABLE);

		/* setup and enable the encoder and transmitter */
		atombios_dig_encoder_setup(encoder, ATOM_ENABLE);
		atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_INIT, 0, 0);
		atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_SETUP, 0, 0);
		atombios_dig_transmitter_setup(encoder, ATOM_TRANSMITTER_ACTION_ENABLE, 0, 0);
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
		if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT | ATOM_DEVICE_CV_SUPPORT))
			atombios_tv_setup(encoder, ATOM_ENABLE);
		break;
	}
	atombios_apply_encoder_quirks(encoder, adjusted_mode);

	r600_hdmi_setmode(encoder, adjusted_mode);
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

		atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev);

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
		DRM_DEBUG("detect returned false \n");
		return connector_status_unknown;
	}

	if (rdev->family >= CHIP_R600)
		bios_0_scratch = RREG32(R600_BIOS_0_SCRATCH);
	else
		bios_0_scratch = RREG32(RADEON_BIOS_0_SCRATCH);

	DRM_DEBUG("Bios 0 scratch %x %08x\n", bios_0_scratch, radeon_encoder->devices);
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

static void radeon_atom_encoder_disable(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig;
	radeon_atom_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);

	if (radeon_encoder_is_digital(encoder)) {
		dig = radeon_encoder->enc_priv;
		dig->dig_encoder = -1;
	}
	radeon_encoder->active_device = 0;
}

static const struct drm_encoder_helper_funcs radeon_atom_dig_helper_funcs = {
	.dpms = radeon_atom_encoder_dpms,
	.mode_fixup = radeon_atom_mode_fixup,
	.prepare = radeon_atom_encoder_prepare,
	.mode_set = radeon_atom_encoder_mode_set,
	.commit = radeon_atom_encoder_commit,
	.disable = radeon_atom_encoder_disable,
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

struct radeon_encoder_atom_dac *
radeon_atombios_set_dac_info(struct radeon_encoder *radeon_encoder)
{
	struct radeon_encoder_atom_dac *dac = kzalloc(sizeof(struct radeon_encoder_atom_dac), GFP_KERNEL);

	if (!dac)
		return NULL;

	dac->tv_std = TV_STD_NTSC;
	return dac;
}

struct radeon_encoder_atom_dig *
radeon_atombios_set_dig_info(struct radeon_encoder *radeon_encoder)
{
	struct radeon_encoder_atom_dig *dig = kzalloc(sizeof(struct radeon_encoder_atom_dig), GFP_KERNEL);

	if (!dig)
		return NULL;

	/* coherent mode by default */
	dig->coherent_mode = true;
	dig->dig_encoder = -1;

	return dig;
}

void
radeon_add_atom_encoder(struct drm_device *dev, uint32_t encoder_id, uint32_t supported_device)
{
	struct radeon_device *rdev = dev->dev_private;
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
	if (rdev->flags & RADEON_SINGLE_CRTC)
		encoder->possible_crtcs = 0x1;
	else
		encoder->possible_crtcs = 0x3;

	radeon_encoder->enc_priv = NULL;

	radeon_encoder->encoder_id = encoder_id;
	radeon_encoder->devices = supported_device;
	radeon_encoder->rmx_type = RMX_OFF;

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
	}

	r600_hdmi_init(encoder);
}
