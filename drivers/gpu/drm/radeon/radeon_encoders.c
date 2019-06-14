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

#include <drm/drm_crtc_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_pci.h>
#include <drm/radeon_drm.h>

#include "radeon.h"
#include "atom.h"

extern void
radeon_legacy_backlight_init(struct radeon_encoder *radeon_encoder,
			     struct drm_connector *drm_connector);
extern void
radeon_atom_backlight_init(struct radeon_encoder *radeon_encoder,
			   struct drm_connector *drm_connector);


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
radeon_get_encoder_enum(struct drm_device *dev, uint32_t supported_device, uint8_t dac)
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
				ret = ENCODER_INTERNAL_DAC2_ENUM_ID1;
			else if (ASIC_IS_AVIVO(rdev))
				ret = ENCODER_INTERNAL_KLDSCP_DAC1_ENUM_ID1;
			else
				ret = ENCODER_INTERNAL_DAC1_ENUM_ID1;
			break;
		case 2: /* dac b */
			if (ASIC_IS_AVIVO(rdev))
				ret = ENCODER_INTERNAL_KLDSCP_DAC2_ENUM_ID1;
			else {
				/*if (rdev->family == CHIP_R200)
				  ret = ENCODER_INTERNAL_DVO1_ENUM_ID1;
				  else*/
				ret = ENCODER_INTERNAL_DAC2_ENUM_ID1;
			}
			break;
		case 3: /* external dac */
			if (ASIC_IS_AVIVO(rdev))
				ret = ENCODER_INTERNAL_KLDSCP_DVO1_ENUM_ID1;
			else
				ret = ENCODER_INTERNAL_DVO1_ENUM_ID1;
			break;
		}
		break;
	case ATOM_DEVICE_LCD1_SUPPORT:
		if (ASIC_IS_AVIVO(rdev))
			ret = ENCODER_INTERNAL_LVTM1_ENUM_ID1;
		else
			ret = ENCODER_INTERNAL_LVDS_ENUM_ID1;
		break;
	case ATOM_DEVICE_DFP1_SUPPORT:
		if ((rdev->family == CHIP_RS300) ||
		    (rdev->family == CHIP_RS400) ||
		    (rdev->family == CHIP_RS480))
			ret = ENCODER_INTERNAL_DVO1_ENUM_ID1;
		else if (ASIC_IS_AVIVO(rdev))
			ret = ENCODER_INTERNAL_KLDSCP_TMDS1_ENUM_ID1;
		else
			ret = ENCODER_INTERNAL_TMDS1_ENUM_ID1;
		break;
	case ATOM_DEVICE_LCD2_SUPPORT:
	case ATOM_DEVICE_DFP2_SUPPORT:
		if ((rdev->family == CHIP_RS600) ||
		    (rdev->family == CHIP_RS690) ||
		    (rdev->family == CHIP_RS740))
			ret = ENCODER_INTERNAL_DDI_ENUM_ID1;
		else if (ASIC_IS_AVIVO(rdev))
			ret = ENCODER_INTERNAL_KLDSCP_DVO1_ENUM_ID1;
		else
			ret = ENCODER_INTERNAL_DVO1_ENUM_ID1;
		break;
	case ATOM_DEVICE_DFP3_SUPPORT:
		ret = ENCODER_INTERNAL_LVTM1_ENUM_ID1;
		break;
	}

	return ret;
}

static void radeon_encoder_add_backlight(struct radeon_encoder *radeon_encoder,
					 struct drm_connector *connector)
{
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	bool use_bl = false;

	if (!(radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)))
		return;

	if (radeon_backlight == 0) {
		return;
	} else if (radeon_backlight == 1) {
		use_bl = true;
	} else if (radeon_backlight == -1) {
		/* Quirks */
		/* Amilo Xi 2550 only works with acpi bl */
		if ((rdev->pdev->device == 0x9583) &&
		    (rdev->pdev->subsystem_vendor == 0x1734) &&
		    (rdev->pdev->subsystem_device == 0x1107))
			use_bl = false;
/* Older PPC macs use on-GPU backlight controller */
#ifndef CONFIG_PPC_PMAC
		/* disable native backlight control on older asics */
		else if (rdev->family < CHIP_R600)
			use_bl = false;
#endif
		else
			use_bl = true;
	}

	if (use_bl) {
		if (rdev->is_atom_bios)
			radeon_atom_backlight_init(radeon_encoder, connector);
		else
			radeon_legacy_backlight_init(radeon_encoder, connector);
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
			if (radeon_encoder->devices & radeon_connector->devices) {
				drm_connector_attach_encoder(connector, encoder);
				if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
					radeon_encoder_add_backlight(radeon_encoder, connector);
			}
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
			DRM_DEBUG_KMS("setting active device to %08x from %08x %08x for encoder %d\n",
				  radeon_encoder->active_device, radeon_encoder->devices,
				  radeon_connector->devices, encoder->encoder_type);
		}
	}
}

struct drm_connector *
radeon_get_connector_for_encoder(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		radeon_connector = to_radeon_connector(connector);
		if (radeon_encoder->is_mst_encoder) {
			struct radeon_encoder_mst *mst_enc;

			if (!radeon_connector->is_mst_connector)
				continue;

			mst_enc = radeon_encoder->enc_priv;
			if (mst_enc->connector == radeon_connector->mst_port)
				return connector;
		} else if (radeon_encoder->active_device & radeon_connector->devices)
			return connector;
	}
	return NULL;
}

struct drm_connector *
radeon_get_connector_for_encoder_init(struct drm_encoder *encoder)
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

struct drm_encoder *radeon_get_external_encoder(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_encoder *other_encoder;
	struct radeon_encoder *other_radeon_encoder;

	if (radeon_encoder->is_ext_encoder)
		return NULL;

	list_for_each_entry(other_encoder, &dev->mode_config.encoder_list, head) {
		if (other_encoder == encoder)
			continue;
		other_radeon_encoder = to_radeon_encoder(other_encoder);
		if (other_radeon_encoder->is_ext_encoder &&
		    (radeon_encoder->devices & other_radeon_encoder->devices))
			return other_encoder;
	}
	return NULL;
}

u16 radeon_encoder_get_dp_bridge_encoder_id(struct drm_encoder *encoder)
{
	struct drm_encoder *other_encoder = radeon_get_external_encoder(encoder);

	if (other_encoder) {
		struct radeon_encoder *radeon_encoder = to_radeon_encoder(other_encoder);

		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_TRAVIS:
		case ENCODER_OBJECT_ID_NUTMEG:
			return radeon_encoder->encoder_id;
		default:
			return ENCODER_OBJECT_ID_NONE;
		}
	}
	return ENCODER_OBJECT_ID_NONE;
}

void radeon_panel_mode_fixup(struct drm_encoder *encoder,
			     struct drm_display_mode *adjusted_mode)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_display_mode *native_mode = &radeon_encoder->native_mode;
	unsigned hblank = native_mode->htotal - native_mode->hdisplay;
	unsigned vblank = native_mode->vtotal - native_mode->vdisplay;
	unsigned hover = native_mode->hsync_start - native_mode->hdisplay;
	unsigned vover = native_mode->vsync_start - native_mode->vdisplay;
	unsigned hsync_width = native_mode->hsync_end - native_mode->hsync_start;
	unsigned vsync_width = native_mode->vsync_end - native_mode->vsync_start;

	adjusted_mode->clock = native_mode->clock;
	adjusted_mode->flags = native_mode->flags;

	if (ASIC_IS_AVIVO(rdev)) {
		adjusted_mode->hdisplay = native_mode->hdisplay;
		adjusted_mode->vdisplay = native_mode->vdisplay;
	}

	adjusted_mode->htotal = native_mode->hdisplay + hblank;
	adjusted_mode->hsync_start = native_mode->hdisplay + hover;
	adjusted_mode->hsync_end = adjusted_mode->hsync_start + hsync_width;

	adjusted_mode->vtotal = native_mode->vdisplay + vblank;
	adjusted_mode->vsync_start = native_mode->vdisplay + vover;
	adjusted_mode->vsync_end = adjusted_mode->vsync_start + vsync_width;

	drm_mode_set_crtcinfo(adjusted_mode, CRTC_INTERLACE_HALVE_V);

	if (ASIC_IS_AVIVO(rdev)) {
		adjusted_mode->crtc_hdisplay = native_mode->hdisplay;
		adjusted_mode->crtc_vdisplay = native_mode->vdisplay;
	}

	adjusted_mode->crtc_htotal = adjusted_mode->crtc_hdisplay + hblank;
	adjusted_mode->crtc_hsync_start = adjusted_mode->crtc_hdisplay + hover;
	adjusted_mode->crtc_hsync_end = adjusted_mode->crtc_hsync_start + hsync_width;

	adjusted_mode->crtc_vtotal = adjusted_mode->crtc_vdisplay + vblank;
	adjusted_mode->crtc_vsync_start = adjusted_mode->crtc_vdisplay + vover;
	adjusted_mode->crtc_vsync_end = adjusted_mode->crtc_vsync_start + vsync_width;

}

bool radeon_dig_monitor_is_duallink(struct drm_encoder *encoder,
				    u32 pixel_clock)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	struct radeon_connector_atom_dig *dig_connector;

	connector = radeon_get_connector_for_encoder(encoder);
	/* if we don't have an active device yet, just use one of
	 * the connectors tied to the encoder.
	 */
	if (!connector)
		connector = radeon_get_connector_for_encoder_init(encoder);
	radeon_connector = to_radeon_connector(connector);

	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_HDMIB:
		if (radeon_connector->use_digital) {
			/* HDMI 1.3 supports up to 340 Mhz over single link */
			if (ASIC_IS_DCE6(rdev) && drm_detect_hdmi_monitor(radeon_connector_edid(connector))) {
				if (pixel_clock > 340000)
					return true;
				else
					return false;
			} else {
				if (pixel_clock > 165000)
					return true;
				else
					return false;
			}
		} else
			return false;
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_HDMIA:
	case DRM_MODE_CONNECTOR_DisplayPort:
		if (radeon_connector->is_mst_connector)
			return false;

		dig_connector = radeon_connector->con_priv;
		if ((dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_DISPLAYPORT) ||
		    (dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_eDP))
			return false;
		else {
			/* HDMI 1.3 supports up to 340 Mhz over single link */
			if (ASIC_IS_DCE6(rdev) && drm_detect_hdmi_monitor(radeon_connector_edid(connector))) {
				if (pixel_clock > 340000)
					return true;
				else
					return false;
			} else {
				if (pixel_clock > 165000)
					return true;
				else
					return false;
			}
		}
	default:
		return false;
	}
}

bool radeon_encoder_is_digital(struct drm_encoder *encoder)
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
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
		return true;
	default:
		return false;
	}
}
