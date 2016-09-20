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
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "amdgpu_connectors.h"
#include "atom.h"
#include "atombios_encoders.h"
#include "atombios_dp.h"
#include <linux/backlight.h>
#include "bif/bif_4_1_d.h"

static u8
amdgpu_atombios_encoder_get_backlight_level_from_reg(struct amdgpu_device *adev)
{
	u8 backlight_level;
	u32 bios_2_scratch;

	bios_2_scratch = RREG32(mmBIOS_SCRATCH_2);

	backlight_level = ((bios_2_scratch & ATOM_S2_CURRENT_BL_LEVEL_MASK) >>
			   ATOM_S2_CURRENT_BL_LEVEL_SHIFT);

	return backlight_level;
}

static void
amdgpu_atombios_encoder_set_backlight_level_to_reg(struct amdgpu_device *adev,
					    u8 backlight_level)
{
	u32 bios_2_scratch;

	bios_2_scratch = RREG32(mmBIOS_SCRATCH_2);

	bios_2_scratch &= ~ATOM_S2_CURRENT_BL_LEVEL_MASK;
	bios_2_scratch |= ((backlight_level << ATOM_S2_CURRENT_BL_LEVEL_SHIFT) &
			   ATOM_S2_CURRENT_BL_LEVEL_MASK);

	WREG32(mmBIOS_SCRATCH_2, bios_2_scratch);
}

u8
amdgpu_atombios_encoder_get_backlight_level(struct amdgpu_encoder *amdgpu_encoder)
{
	struct drm_device *dev = amdgpu_encoder->base.dev;
	struct amdgpu_device *adev = dev->dev_private;

	if (!(adev->mode_info.firmware_flags & ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU))
		return 0;

	return amdgpu_atombios_encoder_get_backlight_level_from_reg(adev);
}

void
amdgpu_atombios_encoder_set_backlight_level(struct amdgpu_encoder *amdgpu_encoder,
				     u8 level)
{
	struct drm_encoder *encoder = &amdgpu_encoder->base;
	struct drm_device *dev = amdgpu_encoder->base.dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder_atom_dig *dig;

	if (!(adev->mode_info.firmware_flags & ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU))
		return;

	if ((amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) &&
	    amdgpu_encoder->enc_priv) {
		dig = amdgpu_encoder->enc_priv;
		dig->backlight_level = level;
		amdgpu_atombios_encoder_set_backlight_level_to_reg(adev, dig->backlight_level);

		switch (amdgpu_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
			if (dig->backlight_level == 0)
				amdgpu_atombios_encoder_setup_dig_transmitter(encoder,
								       ATOM_TRANSMITTER_ACTION_LCD_BLOFF, 0, 0);
			else {
				amdgpu_atombios_encoder_setup_dig_transmitter(encoder,
								       ATOM_TRANSMITTER_ACTION_BL_BRIGHTNESS_CONTROL, 0, 0);
				amdgpu_atombios_encoder_setup_dig_transmitter(encoder,
								       ATOM_TRANSMITTER_ACTION_LCD_BLON, 0, 0);
			}
			break;
		default:
			break;
		}
	}
}

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) || defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

static u8 amdgpu_atombios_encoder_backlight_level(struct backlight_device *bd)
{
	u8 level;

	/* Convert brightness to hardware level */
	if (bd->props.brightness < 0)
		level = 0;
	else if (bd->props.brightness > AMDGPU_MAX_BL_LEVEL)
		level = AMDGPU_MAX_BL_LEVEL;
	else
		level = bd->props.brightness;

	return level;
}

static int amdgpu_atombios_encoder_update_backlight_status(struct backlight_device *bd)
{
	struct amdgpu_backlight_privdata *pdata = bl_get_data(bd);
	struct amdgpu_encoder *amdgpu_encoder = pdata->encoder;

	amdgpu_atombios_encoder_set_backlight_level(amdgpu_encoder,
					     amdgpu_atombios_encoder_backlight_level(bd));

	return 0;
}

static int
amdgpu_atombios_encoder_get_backlight_brightness(struct backlight_device *bd)
{
	struct amdgpu_backlight_privdata *pdata = bl_get_data(bd);
	struct amdgpu_encoder *amdgpu_encoder = pdata->encoder;
	struct drm_device *dev = amdgpu_encoder->base.dev;
	struct amdgpu_device *adev = dev->dev_private;

	return amdgpu_atombios_encoder_get_backlight_level_from_reg(adev);
}

static const struct backlight_ops amdgpu_atombios_encoder_backlight_ops = {
	.get_brightness = amdgpu_atombios_encoder_get_backlight_brightness,
	.update_status	= amdgpu_atombios_encoder_update_backlight_status,
};

void amdgpu_atombios_encoder_init_backlight(struct amdgpu_encoder *amdgpu_encoder,
				     struct drm_connector *drm_connector)
{
	struct drm_device *dev = amdgpu_encoder->base.dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct backlight_device *bd;
	struct backlight_properties props;
	struct amdgpu_backlight_privdata *pdata;
	struct amdgpu_encoder_atom_dig *dig;
	u8 backlight_level;
	char bl_name[16];

	/* Mac laptops with multiple GPUs use the gmux driver for backlight
	 * so don't register a backlight device
	 */
	if ((adev->pdev->subsystem_vendor == PCI_VENDOR_ID_APPLE) &&
	    (adev->pdev->device == 0x6741))
		return;

	if (!amdgpu_encoder->enc_priv)
		return;

	if (!adev->is_atom_bios)
		return;

	if (!(adev->mode_info.firmware_flags & ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU))
		return;

	pdata = kmalloc(sizeof(struct amdgpu_backlight_privdata), GFP_KERNEL);
	if (!pdata) {
		DRM_ERROR("Memory allocation failed\n");
		goto error;
	}

	memset(&props, 0, sizeof(props));
	props.max_brightness = AMDGPU_MAX_BL_LEVEL;
	props.type = BACKLIGHT_RAW;
	snprintf(bl_name, sizeof(bl_name),
		 "amdgpu_bl%d", dev->primary->index);
	bd = backlight_device_register(bl_name, drm_connector->kdev,
				       pdata, &amdgpu_atombios_encoder_backlight_ops, &props);
	if (IS_ERR(bd)) {
		DRM_ERROR("Backlight registration failed\n");
		goto error;
	}

	pdata->encoder = amdgpu_encoder;

	backlight_level = amdgpu_atombios_encoder_get_backlight_level_from_reg(adev);

	dig = amdgpu_encoder->enc_priv;
	dig->bl_dev = bd;

	bd->props.brightness = amdgpu_atombios_encoder_get_backlight_brightness(bd);
	bd->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(bd);

	DRM_INFO("amdgpu atom DIG backlight initialized\n");

	return;

error:
	kfree(pdata);
	return;
}

void
amdgpu_atombios_encoder_fini_backlight(struct amdgpu_encoder *amdgpu_encoder)
{
	struct drm_device *dev = amdgpu_encoder->base.dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct backlight_device *bd = NULL;
	struct amdgpu_encoder_atom_dig *dig;

	if (!amdgpu_encoder->enc_priv)
		return;

	if (!adev->is_atom_bios)
		return;

	if (!(adev->mode_info.firmware_flags & ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU))
		return;

	dig = amdgpu_encoder->enc_priv;
	bd = dig->bl_dev;
	dig->bl_dev = NULL;

	if (bd) {
		struct amdgpu_legacy_backlight_privdata *pdata;

		pdata = bl_get_data(bd);
		backlight_device_unregister(bd);
		kfree(pdata);

		DRM_INFO("amdgpu atom LVDS backlight unloaded\n");
	}
}

#else /* !CONFIG_BACKLIGHT_CLASS_DEVICE */

void amdgpu_atombios_encoder_init_backlight(struct amdgpu_encoder *encoder)
{
}

void amdgpu_atombios_encoder_fini_backlight(struct amdgpu_encoder *encoder)
{
}

#endif

bool amdgpu_atombios_encoder_is_digital(struct drm_encoder *encoder)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	switch (amdgpu_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
		return true;
	default:
		return false;
	}
}

bool amdgpu_atombios_encoder_mode_fixup(struct drm_encoder *encoder,
				 const struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);

	/* set the active encoder to connector routing */
	amdgpu_encoder_set_active_device(encoder);
	drm_mode_set_crtcinfo(adjusted_mode, 0);

	/* hw bug */
	if ((mode->flags & DRM_MODE_FLAG_INTERLACE)
	    && (mode->crtc_vsync_start < (mode->crtc_vdisplay + 2)))
		adjusted_mode->crtc_vsync_start = adjusted_mode->crtc_vdisplay + 2;

	/* vertical FP must be at least 1 */
	if (mode->crtc_vsync_start == mode->crtc_vdisplay)
		adjusted_mode->crtc_vsync_start++;

	/* get the native mode for scaling */
	if (amdgpu_encoder->active_device & (ATOM_DEVICE_LCD_SUPPORT))
		amdgpu_panel_mode_fixup(encoder, adjusted_mode);
	else if (amdgpu_encoder->rmx_type != RMX_OFF)
		amdgpu_panel_mode_fixup(encoder, adjusted_mode);

	if ((amdgpu_encoder->active_device & (ATOM_DEVICE_DFP_SUPPORT | ATOM_DEVICE_LCD_SUPPORT)) ||
	    (amdgpu_encoder_get_dp_bridge_encoder_id(encoder) != ENCODER_OBJECT_ID_NONE)) {
		struct drm_connector *connector = amdgpu_get_connector_for_encoder(encoder);
		amdgpu_atombios_dp_set_link_config(connector, adjusted_mode);
	}

	return true;
}

static void
amdgpu_atombios_encoder_setup_dac(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	DAC_ENCODER_CONTROL_PS_ALLOCATION args;
	int index = 0;

	memset(&args, 0, sizeof(args));

	switch (amdgpu_encoder->encoder_id) {
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
	args.ucDacStandard = ATOM_DAC1_PS2;
	args.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

}

static u8 amdgpu_atombios_encoder_get_bpc(struct drm_encoder *encoder)
{
	int bpc = 8;

	if (encoder->crtc) {
		struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(encoder->crtc);
		bpc = amdgpu_crtc->bpc;
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

static void
amdgpu_atombios_encoder_setup_dvo(struct drm_encoder *encoder, int action)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	union dvo_encoder_control args;
	int index = GetIndexIntoMasterTable(COMMAND, DVOEncoderControl);
	uint8_t frev, crev;

	memset(&args, 0, sizeof(args));

	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev, &crev))
		return;

	switch (frev) {
	case 1:
		switch (crev) {
		case 1:
			/* R4xx, R5xx */
			args.ext_tmds.sXTmdsEncoder.ucEnable = action;

			if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
				args.ext_tmds.sXTmdsEncoder.ucMisc |= PANEL_ENCODER_MISC_DUAL;

			args.ext_tmds.sXTmdsEncoder.ucMisc |= ATOM_PANEL_MISC_888RGB;
			break;
		case 2:
			/* RS600/690/740 */
			args.dvo.sDVOEncoder.ucAction = action;
			args.dvo.sDVOEncoder.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);
			/* DFP1, CRT1, TV1 depending on the type of port */
			args.dvo.sDVOEncoder.ucDeviceType = ATOM_DEVICE_DFP1_INDEX;

			if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
				args.dvo.sDVOEncoder.usDevAttr.sDigAttrib.ucAttribute |= PANEL_ENCODER_MISC_DUAL;
			break;
		case 3:
			/* R6xx */
			args.dvo_v3.ucAction = action;
			args.dvo_v3.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);
			args.dvo_v3.ucDVOConfig = 0; /* XXX */
			break;
		case 4:
			/* DCE8 */
			args.dvo_v4.ucAction = action;
			args.dvo_v4.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);
			args.dvo_v4.ucDVOConfig = 0; /* XXX */
			args.dvo_v4.ucBitPerColor = amdgpu_atombios_encoder_get_bpc(encoder);
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

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

int amdgpu_atombios_encoder_get_encoder_mode(struct drm_encoder *encoder)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_connector *connector;
	struct amdgpu_connector *amdgpu_connector;
	struct amdgpu_connector_atom_dig *dig_connector;

	/* dp bridges are always DP */
	if (amdgpu_encoder_get_dp_bridge_encoder_id(encoder) != ENCODER_OBJECT_ID_NONE)
		return ATOM_ENCODER_MODE_DP;

	/* DVO is always DVO */
	if ((amdgpu_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_DVO1) ||
	    (amdgpu_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1))
		return ATOM_ENCODER_MODE_DVO;

	connector = amdgpu_get_connector_for_encoder(encoder);
	/* if we don't have an active device yet, just use one of
	 * the connectors tied to the encoder.
	 */
	if (!connector)
		connector = amdgpu_get_connector_for_encoder_init(encoder);
	amdgpu_connector = to_amdgpu_connector(connector);

	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_HDMIB: /* HDMI-B is basically DL-DVI; analog works fine */
		if (amdgpu_audio != 0) {
			if (amdgpu_connector->use_digital &&
			    (amdgpu_connector->audio == AMDGPU_AUDIO_ENABLE))
				return ATOM_ENCODER_MODE_HDMI;
			else if (drm_detect_hdmi_monitor(amdgpu_connector_edid(connector)) &&
				 (amdgpu_connector->audio == AMDGPU_AUDIO_AUTO))
				return ATOM_ENCODER_MODE_HDMI;
			else if (amdgpu_connector->use_digital)
				return ATOM_ENCODER_MODE_DVI;
			else
				return ATOM_ENCODER_MODE_CRT;
		} else if (amdgpu_connector->use_digital) {
			return ATOM_ENCODER_MODE_DVI;
		} else {
			return ATOM_ENCODER_MODE_CRT;
		}
		break;
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_HDMIA:
	default:
		if (amdgpu_audio != 0) {
			if (amdgpu_connector->audio == AMDGPU_AUDIO_ENABLE)
				return ATOM_ENCODER_MODE_HDMI;
			else if (drm_detect_hdmi_monitor(amdgpu_connector_edid(connector)) &&
				 (amdgpu_connector->audio == AMDGPU_AUDIO_AUTO))
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
		dig_connector = amdgpu_connector->con_priv;
		if ((dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_DISPLAYPORT) ||
		    (dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_eDP)) {
			return ATOM_ENCODER_MODE_DP;
		} else if (amdgpu_audio != 0) {
			if (amdgpu_connector->audio == AMDGPU_AUDIO_ENABLE)
				return ATOM_ENCODER_MODE_HDMI;
			else if (drm_detect_hdmi_monitor(amdgpu_connector_edid(connector)) &&
				 (amdgpu_connector->audio == AMDGPU_AUDIO_AUTO))
				return ATOM_ENCODER_MODE_HDMI;
			else
				return ATOM_ENCODER_MODE_DVI;
		} else {
			return ATOM_ENCODER_MODE_DVI;
		}
		break;
	case DRM_MODE_CONNECTOR_eDP:
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
 * DCE 6.0
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
amdgpu_atombios_encoder_setup_dig_encoder(struct drm_encoder *encoder,
				   int action, int panel_mode)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	struct drm_connector *connector = amdgpu_get_connector_for_encoder(encoder);
	union dig_encoder_control args;
	int index = GetIndexIntoMasterTable(COMMAND, DIGxEncoderControl);
	uint8_t frev, crev;
	int dp_clock = 0;
	int dp_lane_count = 0;
	int hpd_id = AMDGPU_HPD_NONE;

	if (connector) {
		struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);
		struct amdgpu_connector_atom_dig *dig_connector =
			amdgpu_connector->con_priv;

		dp_clock = dig_connector->dp_clock;
		dp_lane_count = dig_connector->dp_lane_count;
		hpd_id = amdgpu_connector->hpd.hpd;
	}

	/* no dig encoder assigned */
	if (dig->dig_encoder == -1)
		return;

	memset(&args, 0, sizeof(args));

	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev, &crev))
		return;

	switch (frev) {
	case 1:
		switch (crev) {
		case 1:
			args.v1.ucAction = action;
			args.v1.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);
			if (action == ATOM_ENCODER_CMD_SETUP_PANEL_MODE)
				args.v3.ucPanelMode = panel_mode;
			else
				args.v1.ucEncoderMode = amdgpu_atombios_encoder_get_encoder_mode(encoder);

			if (ENCODER_MODE_IS_DP(args.v1.ucEncoderMode))
				args.v1.ucLaneNum = dp_lane_count;
			else if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
				args.v1.ucLaneNum = 8;
			else
				args.v1.ucLaneNum = 4;

			if (ENCODER_MODE_IS_DP(args.v1.ucEncoderMode) && (dp_clock == 270000))
				args.v1.ucConfig |= ATOM_ENCODER_CONFIG_DPLINKRATE_2_70GHZ;
			switch (amdgpu_encoder->encoder_id) {
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
			args.v3.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);
			if (action == ATOM_ENCODER_CMD_SETUP_PANEL_MODE)
				args.v3.ucPanelMode = panel_mode;
			else
				args.v3.ucEncoderMode = amdgpu_atombios_encoder_get_encoder_mode(encoder);

			if (ENCODER_MODE_IS_DP(args.v3.ucEncoderMode))
				args.v3.ucLaneNum = dp_lane_count;
			else if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
				args.v3.ucLaneNum = 8;
			else
				args.v3.ucLaneNum = 4;

			if (ENCODER_MODE_IS_DP(args.v3.ucEncoderMode) && (dp_clock == 270000))
				args.v1.ucConfig |= ATOM_ENCODER_CONFIG_V3_DPLINKRATE_2_70GHZ;
			args.v3.acConfig.ucDigSel = dig->dig_encoder;
			args.v3.ucBitPerColor = amdgpu_atombios_encoder_get_bpc(encoder);
			break;
		case 4:
			args.v4.ucAction = action;
			args.v4.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);
			if (action == ATOM_ENCODER_CMD_SETUP_PANEL_MODE)
				args.v4.ucPanelMode = panel_mode;
			else
				args.v4.ucEncoderMode = amdgpu_atombios_encoder_get_encoder_mode(encoder);

			if (ENCODER_MODE_IS_DP(args.v4.ucEncoderMode))
				args.v4.ucLaneNum = dp_lane_count;
			else if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
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
			args.v4.acConfig.ucDigSel = dig->dig_encoder;
			args.v4.ucBitPerColor = amdgpu_atombios_encoder_get_bpc(encoder);
			if (hpd_id == AMDGPU_HPD_NONE)
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

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

}

union dig_transmitter_control {
	DIG_TRANSMITTER_CONTROL_PS_ALLOCATION v1;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V2 v2;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V3 v3;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V4 v4;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V1_5 v5;
};

void
amdgpu_atombios_encoder_setup_dig_transmitter(struct drm_encoder *encoder, int action,
				       uint8_t lane_num, uint8_t lane_set)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
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
	int hpd_id = AMDGPU_HPD_NONE;

	if (action == ATOM_TRANSMITTER_ACTION_INIT) {
		connector = amdgpu_get_connector_for_encoder_init(encoder);
		/* just needed to avoid bailing in the encoder check.  the encoder
		 * isn't used for init
		 */
		dig_encoder = 0;
	} else
		connector = amdgpu_get_connector_for_encoder(encoder);

	if (connector) {
		struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);
		struct amdgpu_connector_atom_dig *dig_connector =
			amdgpu_connector->con_priv;

		hpd_id = amdgpu_connector->hpd.hpd;
		dp_clock = dig_connector->dp_clock;
		dp_lane_count = dig_connector->dp_lane_count;
		connector_object_id =
			(amdgpu_connector->connector_object_id & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
	}

	if (encoder->crtc) {
		struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(encoder->crtc);
		pll_id = amdgpu_crtc->pll_id;
	}

	/* no dig encoder assigned */
	if (dig_encoder == -1)
		return;

	if (ENCODER_MODE_IS_DP(amdgpu_atombios_encoder_get_encoder_mode(encoder)))
		is_dp = true;

	memset(&args, 0, sizeof(args));

	switch (amdgpu_encoder->encoder_id) {
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

	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev, &crev))
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
				else if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
					args.v1.usPixelClock = cpu_to_le16((amdgpu_encoder->pixel_clock / 2) / 10);
				else
					args.v1.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);
			}

			args.v1.ucConfig = ATOM_TRANSMITTER_CONFIG_CLKSRC_PPLL;

			if (dig_encoder)
				args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_DIG2_ENCODER;
			else
				args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_DIG1_ENCODER;

			if ((adev->flags & AMD_IS_APU) &&
			    (amdgpu_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_UNIPHY)) {
				if (is_dp ||
				    !amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock)) {
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
			else if (amdgpu_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
				if (dig->coherent_mode)
					args.v1.ucConfig |= ATOM_TRANSMITTER_CONFIG_COHERENT;
				if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
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
				else if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
					args.v2.usPixelClock = cpu_to_le16((amdgpu_encoder->pixel_clock / 2) / 10);
				else
					args.v2.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);
			}

			args.v2.acConfig.ucEncoderSel = dig_encoder;
			if (dig->linkb)
				args.v2.acConfig.ucLinkSel = 1;

			switch (amdgpu_encoder->encoder_id) {
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
			} else if (amdgpu_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
				if (dig->coherent_mode)
					args.v2.acConfig.fCoherentMode = 1;
				if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
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
				else if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
					args.v3.usPixelClock = cpu_to_le16((amdgpu_encoder->pixel_clock / 2) / 10);
				else
					args.v3.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);
			}

			if (is_dp)
				args.v3.ucLaneNum = dp_lane_count;
			else if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
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
			if (is_dp && adev->clock.dp_extclk)
				args.v3.acConfig.ucRefClkSource = 2; /* external src */
			else
				args.v3.acConfig.ucRefClkSource = pll_id;

			switch (amdgpu_encoder->encoder_id) {
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
			else if (amdgpu_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
				if (dig->coherent_mode)
					args.v3.acConfig.fCoherentMode = 1;
				if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
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
				else if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
					args.v4.usPixelClock = cpu_to_le16((amdgpu_encoder->pixel_clock / 2) / 10);
				else
					args.v4.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);
			}

			if (is_dp)
				args.v4.ucLaneNum = dp_lane_count;
			else if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
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
				if (adev->clock.dp_extclk)
					args.v4.acConfig.ucRefClkSource = ENCODER_REFCLK_SRC_EXTCLK;
				else
					args.v4.acConfig.ucRefClkSource = ENCODER_REFCLK_SRC_DCPLL;
			} else
				args.v4.acConfig.ucRefClkSource = pll_id;

			switch (amdgpu_encoder->encoder_id) {
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
			else if (amdgpu_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
				if (dig->coherent_mode)
					args.v4.acConfig.fCoherentMode = 1;
				if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
					args.v4.acConfig.fDualLinkConnector = 1;
			}
			break;
		case 5:
			args.v5.ucAction = action;
			if (is_dp)
				args.v5.usSymClock = cpu_to_le16(dp_clock / 10);
			else
				args.v5.usSymClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);

			switch (amdgpu_encoder->encoder_id) {
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
			else if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
				args.v5.ucLaneNum = 8;
			else
				args.v5.ucLaneNum = 4;
			args.v5.ucConnObjId = connector_object_id;
			args.v5.ucDigMode = amdgpu_atombios_encoder_get_encoder_mode(encoder);

			if (is_dp && adev->clock.dp_extclk)
				args.v5.asConfig.ucPhyClkSrcId = ENCODER_REFCLK_SRC_EXTCLK;
			else
				args.v5.asConfig.ucPhyClkSrcId = pll_id;

			if (is_dp)
				args.v5.asConfig.ucCoherentMode = 1; /* DP requires coherent */
			else if (amdgpu_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
				if (dig->coherent_mode)
					args.v5.asConfig.ucCoherentMode = 1;
			}
			if (hpd_id == AMDGPU_HPD_NONE)
				args.v5.asConfig.ucHPDSel = 0;
			else
				args.v5.asConfig.ucHPDSel = hpd_id + 1;
			args.v5.ucDigEncoderSel = 1 << dig_encoder;
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

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

bool
amdgpu_atombios_encoder_set_edp_panel_power(struct drm_connector *connector,
				     int action)
{
	struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);
	struct drm_device *dev = amdgpu_connector->base.dev;
	struct amdgpu_device *adev = dev->dev_private;
	union dig_transmitter_control args;
	int index = GetIndexIntoMasterTable(COMMAND, UNIPHYTransmitterControl);
	uint8_t frev, crev;

	if (connector->connector_type != DRM_MODE_CONNECTOR_eDP)
		goto done;

	if ((action != ATOM_TRANSMITTER_ACTION_POWER_ON) &&
	    (action != ATOM_TRANSMITTER_ACTION_POWER_OFF))
		goto done;

	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev, &crev))
		goto done;

	memset(&args, 0, sizeof(args));

	args.v1.ucAction = action;

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

	/* wait for the panel to power up */
	if (action == ATOM_TRANSMITTER_ACTION_POWER_ON) {
		int i;

		for (i = 0; i < 300; i++) {
			if (amdgpu_display_hpd_sense(adev, amdgpu_connector->hpd.hpd))
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
amdgpu_atombios_encoder_setup_external_encoder(struct drm_encoder *encoder,
					struct drm_encoder *ext_encoder,
					int action)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder *ext_amdgpu_encoder = to_amdgpu_encoder(ext_encoder);
	union external_encoder_control args;
	struct drm_connector *connector;
	int index = GetIndexIntoMasterTable(COMMAND, ExternalEncoderControl);
	u8 frev, crev;
	int dp_clock = 0;
	int dp_lane_count = 0;
	int connector_object_id = 0;
	u32 ext_enum = (ext_amdgpu_encoder->encoder_enum & ENUM_ID_MASK) >> ENUM_ID_SHIFT;

	if (action == EXTERNAL_ENCODER_ACTION_V3_ENCODER_INIT)
		connector = amdgpu_get_connector_for_encoder_init(encoder);
	else
		connector = amdgpu_get_connector_for_encoder(encoder);

	if (connector) {
		struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);
		struct amdgpu_connector_atom_dig *dig_connector =
			amdgpu_connector->con_priv;

		dp_clock = dig_connector->dp_clock;
		dp_lane_count = dig_connector->dp_lane_count;
		connector_object_id =
			(amdgpu_connector->connector_object_id & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
	}

	memset(&args, 0, sizeof(args));

	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev, &crev))
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
			args.v1.sDigEncoder.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);
			args.v1.sDigEncoder.ucEncoderMode =
				amdgpu_atombios_encoder_get_encoder_mode(encoder);

			if (ENCODER_MODE_IS_DP(args.v1.sDigEncoder.ucEncoderMode)) {
				if (dp_clock == 270000)
					args.v1.sDigEncoder.ucConfig |= ATOM_ENCODER_CONFIG_DPLINKRATE_2_70GHZ;
				args.v1.sDigEncoder.ucLaneNum = dp_lane_count;
			} else if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
				args.v1.sDigEncoder.ucLaneNum = 8;
			else
				args.v1.sDigEncoder.ucLaneNum = 4;
			break;
		case 3:
			args.v3.sExtEncoder.ucAction = action;
			if (action == EXTERNAL_ENCODER_ACTION_V3_ENCODER_INIT)
				args.v3.sExtEncoder.usConnectorId = cpu_to_le16(connector_object_id);
			else
				args.v3.sExtEncoder.usPixelClock = cpu_to_le16(amdgpu_encoder->pixel_clock / 10);
			args.v3.sExtEncoder.ucEncoderMode =
				amdgpu_atombios_encoder_get_encoder_mode(encoder);

			if (ENCODER_MODE_IS_DP(args.v3.sExtEncoder.ucEncoderMode)) {
				if (dp_clock == 270000)
					args.v3.sExtEncoder.ucConfig |= EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_2_70GHZ;
				else if (dp_clock == 540000)
					args.v3.sExtEncoder.ucConfig |= EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_5_40GHZ;
				args.v3.sExtEncoder.ucLaneNum = dp_lane_count;
			} else if (amdgpu_dig_monitor_is_duallink(encoder, amdgpu_encoder->pixel_clock))
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
			args.v3.sExtEncoder.ucBitPerColor = amdgpu_atombios_encoder_get_bpc(encoder);
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
	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

static void
amdgpu_atombios_encoder_setup_dig(struct drm_encoder *encoder, int action)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_encoder *ext_encoder = amdgpu_get_external_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	struct drm_connector *connector = amdgpu_get_connector_for_encoder(encoder);
	struct amdgpu_connector *amdgpu_connector = NULL;
	struct amdgpu_connector_atom_dig *amdgpu_dig_connector = NULL;

	if (connector) {
		amdgpu_connector = to_amdgpu_connector(connector);
		amdgpu_dig_connector = amdgpu_connector->con_priv;
	}

	if (action == ATOM_ENABLE) {
		if (!connector)
			dig->panel_mode = DP_PANEL_MODE_EXTERNAL_DP_MODE;
		else
			dig->panel_mode = amdgpu_atombios_dp_get_panel_mode(encoder, connector);

		/* setup and enable the encoder */
		amdgpu_atombios_encoder_setup_dig_encoder(encoder, ATOM_ENCODER_CMD_SETUP, 0);
		amdgpu_atombios_encoder_setup_dig_encoder(encoder,
						   ATOM_ENCODER_CMD_SETUP_PANEL_MODE,
						   dig->panel_mode);
		if (ext_encoder)
			amdgpu_atombios_encoder_setup_external_encoder(encoder, ext_encoder,
								EXTERNAL_ENCODER_ACTION_V3_ENCODER_SETUP);
		if (ENCODER_MODE_IS_DP(amdgpu_atombios_encoder_get_encoder_mode(encoder)) &&
		    connector) {
			if (connector->connector_type == DRM_MODE_CONNECTOR_eDP) {
				amdgpu_atombios_encoder_set_edp_panel_power(connector,
								     ATOM_TRANSMITTER_ACTION_POWER_ON);
				amdgpu_dig_connector->edp_on = true;
			}
		}
		/* enable the transmitter */
		amdgpu_atombios_encoder_setup_dig_transmitter(encoder,
						       ATOM_TRANSMITTER_ACTION_ENABLE,
						       0, 0);
		if (ENCODER_MODE_IS_DP(amdgpu_atombios_encoder_get_encoder_mode(encoder)) &&
		    connector) {
			/* DP_SET_POWER_D0 is set in amdgpu_atombios_dp_link_train */
			amdgpu_atombios_dp_link_train(encoder, connector);
			amdgpu_atombios_encoder_setup_dig_encoder(encoder, ATOM_ENCODER_CMD_DP_VIDEO_ON, 0);
		}
		if (amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
			amdgpu_atombios_encoder_set_backlight_level(amdgpu_encoder, dig->backlight_level);
		if (ext_encoder)
			amdgpu_atombios_encoder_setup_external_encoder(encoder, ext_encoder, ATOM_ENABLE);
	} else {
		if (ENCODER_MODE_IS_DP(amdgpu_atombios_encoder_get_encoder_mode(encoder)) &&
		    connector)
			amdgpu_atombios_encoder_setup_dig_encoder(encoder,
							   ATOM_ENCODER_CMD_DP_VIDEO_OFF, 0);
		if (ext_encoder)
			amdgpu_atombios_encoder_setup_external_encoder(encoder, ext_encoder, ATOM_DISABLE);
		if (amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
			amdgpu_atombios_encoder_setup_dig_transmitter(encoder,
							       ATOM_TRANSMITTER_ACTION_LCD_BLOFF, 0, 0);

		if (ENCODER_MODE_IS_DP(amdgpu_atombios_encoder_get_encoder_mode(encoder)) &&
		    connector)
			amdgpu_atombios_dp_set_rx_power_state(connector, DP_SET_POWER_D3);
		/* disable the transmitter */
		amdgpu_atombios_encoder_setup_dig_transmitter(encoder,
						       ATOM_TRANSMITTER_ACTION_DISABLE, 0, 0);
		if (ENCODER_MODE_IS_DP(amdgpu_atombios_encoder_get_encoder_mode(encoder)) &&
		    connector) {
			if (connector->connector_type == DRM_MODE_CONNECTOR_eDP) {
				amdgpu_atombios_encoder_set_edp_panel_power(connector,
								     ATOM_TRANSMITTER_ACTION_POWER_OFF);
				amdgpu_dig_connector->edp_on = false;
			}
		}
	}
}

void
amdgpu_atombios_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);

	DRM_DEBUG_KMS("encoder dpms %d to mode %d, devices %08x, active_devices %08x\n",
		  amdgpu_encoder->encoder_id, mode, amdgpu_encoder->devices,
		  amdgpu_encoder->active_device);
	switch (amdgpu_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
		switch (mode) {
		case DRM_MODE_DPMS_ON:
			amdgpu_atombios_encoder_setup_dig(encoder, ATOM_ENABLE);
			break;
		case DRM_MODE_DPMS_STANDBY:
		case DRM_MODE_DPMS_SUSPEND:
		case DRM_MODE_DPMS_OFF:
			amdgpu_atombios_encoder_setup_dig(encoder, ATOM_DISABLE);
			break;
		}
		break;
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
		switch (mode) {
		case DRM_MODE_DPMS_ON:
			amdgpu_atombios_encoder_setup_dvo(encoder, ATOM_ENABLE);
			break;
		case DRM_MODE_DPMS_STANDBY:
		case DRM_MODE_DPMS_SUSPEND:
		case DRM_MODE_DPMS_OFF:
			amdgpu_atombios_encoder_setup_dvo(encoder, ATOM_DISABLE);
			break;
		}
		break;
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
		switch (mode) {
		case DRM_MODE_DPMS_ON:
			amdgpu_atombios_encoder_setup_dac(encoder, ATOM_ENABLE);
			break;
		case DRM_MODE_DPMS_STANDBY:
		case DRM_MODE_DPMS_SUSPEND:
		case DRM_MODE_DPMS_OFF:
			amdgpu_atombios_encoder_setup_dac(encoder, ATOM_DISABLE);
			break;
		}
		break;
	default:
		return;
	}
}

union crtc_source_param {
	SELECT_CRTC_SOURCE_PS_ALLOCATION v1;
	SELECT_CRTC_SOURCE_PARAMETERS_V2 v2;
	SELECT_CRTC_SOURCE_PARAMETERS_V3 v3;
};

void
amdgpu_atombios_encoder_set_crtc_source(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(encoder->crtc);
	union crtc_source_param args;
	int index = GetIndexIntoMasterTable(COMMAND, SelectCRTC_Source);
	uint8_t frev, crev;
	struct amdgpu_encoder_atom_dig *dig;

	memset(&args, 0, sizeof(args));

	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev, &crev))
		return;

	switch (frev) {
	case 1:
		switch (crev) {
		case 1:
		default:
			args.v1.ucCRTC = amdgpu_crtc->crtc_id;
			switch (amdgpu_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
				args.v1.ucDevice = ATOM_DEVICE_DFP1_INDEX;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_LVDS:
			case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
				if (amdgpu_encoder->devices & ATOM_DEVICE_LCD1_SUPPORT)
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
				if (amdgpu_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
					args.v1.ucDevice = ATOM_DEVICE_TV1_INDEX;
				else if (amdgpu_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
					args.v1.ucDevice = ATOM_DEVICE_CV_INDEX;
				else
					args.v1.ucDevice = ATOM_DEVICE_CRT1_INDEX;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_DAC2:
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
				if (amdgpu_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
					args.v1.ucDevice = ATOM_DEVICE_TV1_INDEX;
				else if (amdgpu_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
					args.v1.ucDevice = ATOM_DEVICE_CV_INDEX;
				else
					args.v1.ucDevice = ATOM_DEVICE_CRT2_INDEX;
				break;
			}
			break;
		case 2:
			args.v2.ucCRTC = amdgpu_crtc->crtc_id;
			if (amdgpu_encoder_get_dp_bridge_encoder_id(encoder) != ENCODER_OBJECT_ID_NONE) {
				struct drm_connector *connector = amdgpu_get_connector_for_encoder(encoder);

				if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS)
					args.v2.ucEncodeMode = ATOM_ENCODER_MODE_LVDS;
				else if (connector->connector_type == DRM_MODE_CONNECTOR_VGA)
					args.v2.ucEncodeMode = ATOM_ENCODER_MODE_CRT;
				else
					args.v2.ucEncodeMode = amdgpu_atombios_encoder_get_encoder_mode(encoder);
			} else if (amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
				args.v2.ucEncodeMode = ATOM_ENCODER_MODE_LVDS;
			} else {
				args.v2.ucEncodeMode = amdgpu_atombios_encoder_get_encoder_mode(encoder);
			}
			switch (amdgpu_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
				dig = amdgpu_encoder->enc_priv;
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
				if (amdgpu_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
					args.v2.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else if (amdgpu_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
					args.v2.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else
					args.v2.ucEncoderID = ASIC_INT_DAC1_ENCODER_ID;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
				if (amdgpu_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
					args.v2.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else if (amdgpu_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
					args.v2.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else
					args.v2.ucEncoderID = ASIC_INT_DAC2_ENCODER_ID;
				break;
			}
			break;
		case 3:
			args.v3.ucCRTC = amdgpu_crtc->crtc_id;
			if (amdgpu_encoder_get_dp_bridge_encoder_id(encoder) != ENCODER_OBJECT_ID_NONE) {
				struct drm_connector *connector = amdgpu_get_connector_for_encoder(encoder);

				if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS)
					args.v2.ucEncodeMode = ATOM_ENCODER_MODE_LVDS;
				else if (connector->connector_type == DRM_MODE_CONNECTOR_VGA)
					args.v2.ucEncodeMode = ATOM_ENCODER_MODE_CRT;
				else
					args.v2.ucEncodeMode = amdgpu_atombios_encoder_get_encoder_mode(encoder);
			} else if (amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
				args.v2.ucEncodeMode = ATOM_ENCODER_MODE_LVDS;
			} else {
				args.v2.ucEncodeMode = amdgpu_atombios_encoder_get_encoder_mode(encoder);
			}
			args.v3.ucDstBpc = amdgpu_atombios_encoder_get_bpc(encoder);
			switch (amdgpu_encoder->encoder_id) {
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
			case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
				dig = amdgpu_encoder->enc_priv;
				switch (dig->dig_encoder) {
				case 0:
					args.v3.ucEncoderID = ASIC_INT_DIG1_ENCODER_ID;
					break;
				case 1:
					args.v3.ucEncoderID = ASIC_INT_DIG2_ENCODER_ID;
					break;
				case 2:
					args.v3.ucEncoderID = ASIC_INT_DIG3_ENCODER_ID;
					break;
				case 3:
					args.v3.ucEncoderID = ASIC_INT_DIG4_ENCODER_ID;
					break;
				case 4:
					args.v3.ucEncoderID = ASIC_INT_DIG5_ENCODER_ID;
					break;
				case 5:
					args.v3.ucEncoderID = ASIC_INT_DIG6_ENCODER_ID;
					break;
				case 6:
					args.v3.ucEncoderID = ASIC_INT_DIG7_ENCODER_ID;
					break;
				}
				break;
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
				args.v3.ucEncoderID = ASIC_INT_DVO_ENCODER_ID;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
				if (amdgpu_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
					args.v3.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else if (amdgpu_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
					args.v3.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else
					args.v3.ucEncoderID = ASIC_INT_DAC1_ENCODER_ID;
				break;
			case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
				if (amdgpu_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
					args.v3.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else if (amdgpu_encoder->active_device & (ATOM_DEVICE_CV_SUPPORT))
					args.v3.ucEncoderID = ASIC_INT_TV_ENCODER_ID;
				else
					args.v3.ucEncoderID = ASIC_INT_DAC2_ENCODER_ID;
				break;
			}
			break;
		}
		break;
	default:
		DRM_ERROR("Unknown table version: %d, %d\n", frev, crev);
		return;
	}

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

/* This only needs to be called once at startup */
void
amdgpu_atombios_encoder_init_dig(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev->ddev;
	struct drm_encoder *encoder;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
		struct drm_encoder *ext_encoder = amdgpu_get_external_encoder(encoder);

		switch (amdgpu_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
		case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
			amdgpu_atombios_encoder_setup_dig_transmitter(encoder, ATOM_TRANSMITTER_ACTION_INIT,
							       0, 0);
			break;
		}

		if (ext_encoder)
			amdgpu_atombios_encoder_setup_external_encoder(encoder, ext_encoder,
								EXTERNAL_ENCODER_ACTION_V3_ENCODER_INIT);
	}
}

static bool
amdgpu_atombios_encoder_dac_load_detect(struct drm_encoder *encoder,
				 struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);

	if (amdgpu_encoder->devices & (ATOM_DEVICE_TV_SUPPORT |
				       ATOM_DEVICE_CV_SUPPORT |
				       ATOM_DEVICE_CRT_SUPPORT)) {
		DAC_LOAD_DETECTION_PS_ALLOCATION args;
		int index = GetIndexIntoMasterTable(COMMAND, DAC_LoadDetection);
		uint8_t frev, crev;

		memset(&args, 0, sizeof(args));

		if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev, &crev))
			return false;

		args.sDacload.ucMisc = 0;

		if ((amdgpu_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_DAC1) ||
		    (amdgpu_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1))
			args.sDacload.ucDacType = ATOM_DAC_A;
		else
			args.sDacload.ucDacType = ATOM_DAC_B;

		if (amdgpu_connector->devices & ATOM_DEVICE_CRT1_SUPPORT)
			args.sDacload.usDeviceID = cpu_to_le16(ATOM_DEVICE_CRT1_SUPPORT);
		else if (amdgpu_connector->devices & ATOM_DEVICE_CRT2_SUPPORT)
			args.sDacload.usDeviceID = cpu_to_le16(ATOM_DEVICE_CRT2_SUPPORT);
		else if (amdgpu_connector->devices & ATOM_DEVICE_CV_SUPPORT) {
			args.sDacload.usDeviceID = cpu_to_le16(ATOM_DEVICE_CV_SUPPORT);
			if (crev >= 3)
				args.sDacload.ucMisc = DAC_LOAD_MISC_YPrPb;
		} else if (amdgpu_connector->devices & ATOM_DEVICE_TV1_SUPPORT) {
			args.sDacload.usDeviceID = cpu_to_le16(ATOM_DEVICE_TV1_SUPPORT);
			if (crev >= 3)
				args.sDacload.ucMisc = DAC_LOAD_MISC_YPrPb;
		}

		amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

		return true;
	} else
		return false;
}

enum drm_connector_status
amdgpu_atombios_encoder_dac_detect(struct drm_encoder *encoder,
			    struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);
	uint32_t bios_0_scratch;

	if (!amdgpu_atombios_encoder_dac_load_detect(encoder, connector)) {
		DRM_DEBUG_KMS("detect returned false \n");
		return connector_status_unknown;
	}

	bios_0_scratch = RREG32(mmBIOS_SCRATCH_0);

	DRM_DEBUG_KMS("Bios 0 scratch %x %08x\n", bios_0_scratch, amdgpu_encoder->devices);
	if (amdgpu_connector->devices & ATOM_DEVICE_CRT1_SUPPORT) {
		if (bios_0_scratch & ATOM_S0_CRT1_MASK)
			return connector_status_connected;
	}
	if (amdgpu_connector->devices & ATOM_DEVICE_CRT2_SUPPORT) {
		if (bios_0_scratch & ATOM_S0_CRT2_MASK)
			return connector_status_connected;
	}
	if (amdgpu_connector->devices & ATOM_DEVICE_CV_SUPPORT) {
		if (bios_0_scratch & (ATOM_S0_CV_MASK|ATOM_S0_CV_MASK_A))
			return connector_status_connected;
	}
	if (amdgpu_connector->devices & ATOM_DEVICE_TV1_SUPPORT) {
		if (bios_0_scratch & (ATOM_S0_TV1_COMPOSITE | ATOM_S0_TV1_COMPOSITE_A))
			return connector_status_connected; /* CTV */
		else if (bios_0_scratch & (ATOM_S0_TV1_SVIDEO | ATOM_S0_TV1_SVIDEO_A))
			return connector_status_connected; /* STV */
	}
	return connector_status_disconnected;
}

enum drm_connector_status
amdgpu_atombios_encoder_dig_detect(struct drm_encoder *encoder,
			    struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);
	struct drm_encoder *ext_encoder = amdgpu_get_external_encoder(encoder);
	u32 bios_0_scratch;

	if (!ext_encoder)
		return connector_status_unknown;

	if ((amdgpu_connector->devices & ATOM_DEVICE_CRT_SUPPORT) == 0)
		return connector_status_unknown;

	/* load detect on the dp bridge */
	amdgpu_atombios_encoder_setup_external_encoder(encoder, ext_encoder,
						EXTERNAL_ENCODER_ACTION_V3_DACLOAD_DETECTION);

	bios_0_scratch = RREG32(mmBIOS_SCRATCH_0);

	DRM_DEBUG_KMS("Bios 0 scratch %x %08x\n", bios_0_scratch, amdgpu_encoder->devices);
	if (amdgpu_connector->devices & ATOM_DEVICE_CRT1_SUPPORT) {
		if (bios_0_scratch & ATOM_S0_CRT1_MASK)
			return connector_status_connected;
	}
	if (amdgpu_connector->devices & ATOM_DEVICE_CRT2_SUPPORT) {
		if (bios_0_scratch & ATOM_S0_CRT2_MASK)
			return connector_status_connected;
	}
	if (amdgpu_connector->devices & ATOM_DEVICE_CV_SUPPORT) {
		if (bios_0_scratch & (ATOM_S0_CV_MASK|ATOM_S0_CV_MASK_A))
			return connector_status_connected;
	}
	if (amdgpu_connector->devices & ATOM_DEVICE_TV1_SUPPORT) {
		if (bios_0_scratch & (ATOM_S0_TV1_COMPOSITE | ATOM_S0_TV1_COMPOSITE_A))
			return connector_status_connected; /* CTV */
		else if (bios_0_scratch & (ATOM_S0_TV1_SVIDEO | ATOM_S0_TV1_SVIDEO_A))
			return connector_status_connected; /* STV */
	}
	return connector_status_disconnected;
}

void
amdgpu_atombios_encoder_setup_ext_encoder_ddc(struct drm_encoder *encoder)
{
	struct drm_encoder *ext_encoder = amdgpu_get_external_encoder(encoder);

	if (ext_encoder)
		/* ddc_setup on the dp bridge */
		amdgpu_atombios_encoder_setup_external_encoder(encoder, ext_encoder,
							EXTERNAL_ENCODER_ACTION_V3_DDC_SETUP);

}

void
amdgpu_atombios_encoder_set_bios_scratch_regs(struct drm_connector *connector,
				       struct drm_encoder *encoder,
				       bool connected)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_connector *amdgpu_connector =
	    to_amdgpu_connector(connector);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	uint32_t bios_0_scratch, bios_3_scratch, bios_6_scratch;

	bios_0_scratch = RREG32(mmBIOS_SCRATCH_0);
	bios_3_scratch = RREG32(mmBIOS_SCRATCH_3);
	bios_6_scratch = RREG32(mmBIOS_SCRATCH_6);

	if ((amdgpu_encoder->devices & ATOM_DEVICE_LCD1_SUPPORT) &&
	    (amdgpu_connector->devices & ATOM_DEVICE_LCD1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("LCD1 connected\n");
			bios_0_scratch |= ATOM_S0_LCD1;
			bios_3_scratch |= ATOM_S3_LCD1_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_LCD1;
		} else {
			DRM_DEBUG_KMS("LCD1 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_LCD1;
			bios_3_scratch &= ~ATOM_S3_LCD1_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_LCD1;
		}
	}
	if ((amdgpu_encoder->devices & ATOM_DEVICE_CRT1_SUPPORT) &&
	    (amdgpu_connector->devices & ATOM_DEVICE_CRT1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("CRT1 connected\n");
			bios_0_scratch |= ATOM_S0_CRT1_COLOR;
			bios_3_scratch |= ATOM_S3_CRT1_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_CRT1;
		} else {
			DRM_DEBUG_KMS("CRT1 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_CRT1_MASK;
			bios_3_scratch &= ~ATOM_S3_CRT1_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_CRT1;
		}
	}
	if ((amdgpu_encoder->devices & ATOM_DEVICE_CRT2_SUPPORT) &&
	    (amdgpu_connector->devices & ATOM_DEVICE_CRT2_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("CRT2 connected\n");
			bios_0_scratch |= ATOM_S0_CRT2_COLOR;
			bios_3_scratch |= ATOM_S3_CRT2_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_CRT2;
		} else {
			DRM_DEBUG_KMS("CRT2 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_CRT2_MASK;
			bios_3_scratch &= ~ATOM_S3_CRT2_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_CRT2;
		}
	}
	if ((amdgpu_encoder->devices & ATOM_DEVICE_DFP1_SUPPORT) &&
	    (amdgpu_connector->devices & ATOM_DEVICE_DFP1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP1 connected\n");
			bios_0_scratch |= ATOM_S0_DFP1;
			bios_3_scratch |= ATOM_S3_DFP1_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP1;
		} else {
			DRM_DEBUG_KMS("DFP1 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP1;
			bios_3_scratch &= ~ATOM_S3_DFP1_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP1;
		}
	}
	if ((amdgpu_encoder->devices & ATOM_DEVICE_DFP2_SUPPORT) &&
	    (amdgpu_connector->devices & ATOM_DEVICE_DFP2_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP2 connected\n");
			bios_0_scratch |= ATOM_S0_DFP2;
			bios_3_scratch |= ATOM_S3_DFP2_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP2;
		} else {
			DRM_DEBUG_KMS("DFP2 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP2;
			bios_3_scratch &= ~ATOM_S3_DFP2_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP2;
		}
	}
	if ((amdgpu_encoder->devices & ATOM_DEVICE_DFP3_SUPPORT) &&
	    (amdgpu_connector->devices & ATOM_DEVICE_DFP3_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP3 connected\n");
			bios_0_scratch |= ATOM_S0_DFP3;
			bios_3_scratch |= ATOM_S3_DFP3_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP3;
		} else {
			DRM_DEBUG_KMS("DFP3 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP3;
			bios_3_scratch &= ~ATOM_S3_DFP3_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP3;
		}
	}
	if ((amdgpu_encoder->devices & ATOM_DEVICE_DFP4_SUPPORT) &&
	    (amdgpu_connector->devices & ATOM_DEVICE_DFP4_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP4 connected\n");
			bios_0_scratch |= ATOM_S0_DFP4;
			bios_3_scratch |= ATOM_S3_DFP4_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP4;
		} else {
			DRM_DEBUG_KMS("DFP4 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP4;
			bios_3_scratch &= ~ATOM_S3_DFP4_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP4;
		}
	}
	if ((amdgpu_encoder->devices & ATOM_DEVICE_DFP5_SUPPORT) &&
	    (amdgpu_connector->devices & ATOM_DEVICE_DFP5_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP5 connected\n");
			bios_0_scratch |= ATOM_S0_DFP5;
			bios_3_scratch |= ATOM_S3_DFP5_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP5;
		} else {
			DRM_DEBUG_KMS("DFP5 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP5;
			bios_3_scratch &= ~ATOM_S3_DFP5_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP5;
		}
	}
	if ((amdgpu_encoder->devices & ATOM_DEVICE_DFP6_SUPPORT) &&
	    (amdgpu_connector->devices & ATOM_DEVICE_DFP6_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP6 connected\n");
			bios_0_scratch |= ATOM_S0_DFP6;
			bios_3_scratch |= ATOM_S3_DFP6_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP6;
		} else {
			DRM_DEBUG_KMS("DFP6 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP6;
			bios_3_scratch &= ~ATOM_S3_DFP6_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP6;
		}
	}

	WREG32(mmBIOS_SCRATCH_0, bios_0_scratch);
	WREG32(mmBIOS_SCRATCH_3, bios_3_scratch);
	WREG32(mmBIOS_SCRATCH_6, bios_6_scratch);
}

union lvds_info {
	struct _ATOM_LVDS_INFO info;
	struct _ATOM_LVDS_INFO_V12 info_12;
};

struct amdgpu_encoder_atom_dig *
amdgpu_atombios_encoder_get_lcd_info(struct amdgpu_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, LVDS_Info);
	uint16_t data_offset, misc;
	union lvds_info *lvds_info;
	uint8_t frev, crev;
	struct amdgpu_encoder_atom_dig *lvds = NULL;
	int encoder_enum = (encoder->encoder_enum & ENUM_ID_MASK) >> ENUM_ID_SHIFT;

	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		lvds_info =
			(union lvds_info *)(mode_info->atom_context->bios + data_offset);
		lvds =
		    kzalloc(sizeof(struct amdgpu_encoder_atom_dig), GFP_KERNEL);

		if (!lvds)
			return NULL;

		lvds->native_mode.clock =
		    le16_to_cpu(lvds_info->info.sLCDTiming.usPixClk) * 10;
		lvds->native_mode.hdisplay =
		    le16_to_cpu(lvds_info->info.sLCDTiming.usHActive);
		lvds->native_mode.vdisplay =
		    le16_to_cpu(lvds_info->info.sLCDTiming.usVActive);
		lvds->native_mode.htotal = lvds->native_mode.hdisplay +
			le16_to_cpu(lvds_info->info.sLCDTiming.usHBlanking_Time);
		lvds->native_mode.hsync_start = lvds->native_mode.hdisplay +
			le16_to_cpu(lvds_info->info.sLCDTiming.usHSyncOffset);
		lvds->native_mode.hsync_end = lvds->native_mode.hsync_start +
			le16_to_cpu(lvds_info->info.sLCDTiming.usHSyncWidth);
		lvds->native_mode.vtotal = lvds->native_mode.vdisplay +
			le16_to_cpu(lvds_info->info.sLCDTiming.usVBlanking_Time);
		lvds->native_mode.vsync_start = lvds->native_mode.vdisplay +
			le16_to_cpu(lvds_info->info.sLCDTiming.usVSyncOffset);
		lvds->native_mode.vsync_end = lvds->native_mode.vsync_start +
			le16_to_cpu(lvds_info->info.sLCDTiming.usVSyncWidth);
		lvds->panel_pwr_delay =
		    le16_to_cpu(lvds_info->info.usOffDelayInMs);
		lvds->lcd_misc = lvds_info->info.ucLVDS_Misc;

		misc = le16_to_cpu(lvds_info->info.sLCDTiming.susModeMiscInfo.usAccess);
		if (misc & ATOM_VSYNC_POLARITY)
			lvds->native_mode.flags |= DRM_MODE_FLAG_NVSYNC;
		if (misc & ATOM_HSYNC_POLARITY)
			lvds->native_mode.flags |= DRM_MODE_FLAG_NHSYNC;
		if (misc & ATOM_COMPOSITESYNC)
			lvds->native_mode.flags |= DRM_MODE_FLAG_CSYNC;
		if (misc & ATOM_INTERLACE)
			lvds->native_mode.flags |= DRM_MODE_FLAG_INTERLACE;
		if (misc & ATOM_DOUBLE_CLOCK_MODE)
			lvds->native_mode.flags |= DRM_MODE_FLAG_DBLSCAN;

		lvds->native_mode.width_mm = le16_to_cpu(lvds_info->info.sLCDTiming.usImageHSize);
		lvds->native_mode.height_mm = le16_to_cpu(lvds_info->info.sLCDTiming.usImageVSize);

		/* set crtc values */
		drm_mode_set_crtcinfo(&lvds->native_mode, CRTC_INTERLACE_HALVE_V);

		lvds->lcd_ss_id = lvds_info->info.ucSS_Id;

		encoder->native_mode = lvds->native_mode;

		if (encoder_enum == 2)
			lvds->linkb = true;
		else
			lvds->linkb = false;

		/* parse the lcd record table */
		if (le16_to_cpu(lvds_info->info.usModePatchTableOffset)) {
			ATOM_FAKE_EDID_PATCH_RECORD *fake_edid_record;
			ATOM_PANEL_RESOLUTION_PATCH_RECORD *panel_res_record;
			bool bad_record = false;
			u8 *record;

			if ((frev == 1) && (crev < 2))
				/* absolute */
				record = (u8 *)(mode_info->atom_context->bios +
						le16_to_cpu(lvds_info->info.usModePatchTableOffset));
			else
				/* relative */
				record = (u8 *)(mode_info->atom_context->bios +
						data_offset +
						le16_to_cpu(lvds_info->info.usModePatchTableOffset));
			while (*record != ATOM_RECORD_END_TYPE) {
				switch (*record) {
				case LCD_MODE_PATCH_RECORD_MODE_TYPE:
					record += sizeof(ATOM_PATCH_RECORD_MODE);
					break;
				case LCD_RTS_RECORD_TYPE:
					record += sizeof(ATOM_LCD_RTS_RECORD);
					break;
				case LCD_CAP_RECORD_TYPE:
					record += sizeof(ATOM_LCD_MODE_CONTROL_CAP);
					break;
				case LCD_FAKE_EDID_PATCH_RECORD_TYPE:
					fake_edid_record = (ATOM_FAKE_EDID_PATCH_RECORD *)record;
					if (fake_edid_record->ucFakeEDIDLength) {
						struct edid *edid;
						int edid_size =
							max((int)EDID_LENGTH, (int)fake_edid_record->ucFakeEDIDLength);
						edid = kmalloc(edid_size, GFP_KERNEL);
						if (edid) {
							memcpy((u8 *)edid, (u8 *)&fake_edid_record->ucFakeEDIDString[0],
							       fake_edid_record->ucFakeEDIDLength);

							if (drm_edid_is_valid(edid)) {
								adev->mode_info.bios_hardcoded_edid = edid;
								adev->mode_info.bios_hardcoded_edid_size = edid_size;
							} else
								kfree(edid);
						}
					}
					record += fake_edid_record->ucFakeEDIDLength ?
						fake_edid_record->ucFakeEDIDLength + 2 :
						sizeof(ATOM_FAKE_EDID_PATCH_RECORD);
					break;
				case LCD_PANEL_RESOLUTION_RECORD_TYPE:
					panel_res_record = (ATOM_PANEL_RESOLUTION_PATCH_RECORD *)record;
					lvds->native_mode.width_mm = panel_res_record->usHSize;
					lvds->native_mode.height_mm = panel_res_record->usVSize;
					record += sizeof(ATOM_PANEL_RESOLUTION_PATCH_RECORD);
					break;
				default:
					DRM_ERROR("Bad LCD record %d\n", *record);
					bad_record = true;
					break;
				}
				if (bad_record)
					break;
			}
		}
	}
	return lvds;
}

struct amdgpu_encoder_atom_dig *
amdgpu_atombios_encoder_get_dig_info(struct amdgpu_encoder *amdgpu_encoder)
{
	int encoder_enum = (amdgpu_encoder->encoder_enum & ENUM_ID_MASK) >> ENUM_ID_SHIFT;
	struct amdgpu_encoder_atom_dig *dig = kzalloc(sizeof(struct amdgpu_encoder_atom_dig), GFP_KERNEL);

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

