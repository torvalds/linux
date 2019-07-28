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
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "amdgpu_connectors.h"
#include "amdgpu_display.h"
#include "atom.h"
#include "atombios_encoders.h"

void
amdgpu_link_encoder_connector(struct drm_device *dev)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct drm_connector *connector;
	struct amdgpu_connector *amdgpu_connector;
	struct drm_encoder *encoder;
	struct amdgpu_encoder *amdgpu_encoder;

	/* walk the list and link encoders to connectors */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		amdgpu_connector = to_amdgpu_connector(connector);
		list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
			amdgpu_encoder = to_amdgpu_encoder(encoder);
			if (amdgpu_encoder->devices & amdgpu_connector->devices) {
				drm_connector_attach_encoder(connector, encoder);
				if (amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
					amdgpu_atombios_encoder_init_backlight(amdgpu_encoder, connector);
					adev->mode_info.bl_encoder = amdgpu_encoder;
				}
			}
		}
	}
}

void amdgpu_encoder_set_active_device(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_connector *connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);
			amdgpu_encoder->active_device = amdgpu_encoder->devices & amdgpu_connector->devices;
			DRM_DEBUG_KMS("setting active device to %08x from %08x %08x for encoder %d\n",
				  amdgpu_encoder->active_device, amdgpu_encoder->devices,
				  amdgpu_connector->devices, encoder->encoder_type);
		}
	}
}

struct drm_connector *
amdgpu_get_connector_for_encoder(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_connector *connector;
	struct amdgpu_connector *amdgpu_connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		amdgpu_connector = to_amdgpu_connector(connector);
		if (amdgpu_encoder->active_device & amdgpu_connector->devices)
			return connector;
	}
	return NULL;
}

struct drm_connector *
amdgpu_get_connector_for_encoder_init(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_connector *connector;
	struct amdgpu_connector *amdgpu_connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		amdgpu_connector = to_amdgpu_connector(connector);
		if (amdgpu_encoder->devices & amdgpu_connector->devices)
			return connector;
	}
	return NULL;
}

struct drm_encoder *amdgpu_get_external_encoder(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_encoder *other_encoder;
	struct amdgpu_encoder *other_amdgpu_encoder;

	if (amdgpu_encoder->is_ext_encoder)
		return NULL;

	list_for_each_entry(other_encoder, &dev->mode_config.encoder_list, head) {
		if (other_encoder == encoder)
			continue;
		other_amdgpu_encoder = to_amdgpu_encoder(other_encoder);
		if (other_amdgpu_encoder->is_ext_encoder &&
		    (amdgpu_encoder->devices & other_amdgpu_encoder->devices))
			return other_encoder;
	}
	return NULL;
}

u16 amdgpu_encoder_get_dp_bridge_encoder_id(struct drm_encoder *encoder)
{
	struct drm_encoder *other_encoder = amdgpu_get_external_encoder(encoder);

	if (other_encoder) {
		struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(other_encoder);

		switch (amdgpu_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_TRAVIS:
		case ENCODER_OBJECT_ID_NUTMEG:
			return amdgpu_encoder->encoder_id;
		default:
			return ENCODER_OBJECT_ID_NONE;
		}
	}
	return ENCODER_OBJECT_ID_NONE;
}

void amdgpu_panel_mode_fixup(struct drm_encoder *encoder,
			     struct drm_display_mode *adjusted_mode)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_display_mode *native_mode = &amdgpu_encoder->native_mode;
	unsigned hblank = native_mode->htotal - native_mode->hdisplay;
	unsigned vblank = native_mode->vtotal - native_mode->vdisplay;
	unsigned hover = native_mode->hsync_start - native_mode->hdisplay;
	unsigned vover = native_mode->vsync_start - native_mode->vdisplay;
	unsigned hsync_width = native_mode->hsync_end - native_mode->hsync_start;
	unsigned vsync_width = native_mode->vsync_end - native_mode->vsync_start;

	adjusted_mode->clock = native_mode->clock;
	adjusted_mode->flags = native_mode->flags;

	adjusted_mode->hdisplay = native_mode->hdisplay;
	adjusted_mode->vdisplay = native_mode->vdisplay;

	adjusted_mode->htotal = native_mode->hdisplay + hblank;
	adjusted_mode->hsync_start = native_mode->hdisplay + hover;
	adjusted_mode->hsync_end = adjusted_mode->hsync_start + hsync_width;

	adjusted_mode->vtotal = native_mode->vdisplay + vblank;
	adjusted_mode->vsync_start = native_mode->vdisplay + vover;
	adjusted_mode->vsync_end = adjusted_mode->vsync_start + vsync_width;

	drm_mode_set_crtcinfo(adjusted_mode, CRTC_INTERLACE_HALVE_V);

	adjusted_mode->crtc_hdisplay = native_mode->hdisplay;
	adjusted_mode->crtc_vdisplay = native_mode->vdisplay;

	adjusted_mode->crtc_htotal = adjusted_mode->crtc_hdisplay + hblank;
	adjusted_mode->crtc_hsync_start = adjusted_mode->crtc_hdisplay + hover;
	adjusted_mode->crtc_hsync_end = adjusted_mode->crtc_hsync_start + hsync_width;

	adjusted_mode->crtc_vtotal = adjusted_mode->crtc_vdisplay + vblank;
	adjusted_mode->crtc_vsync_start = adjusted_mode->crtc_vdisplay + vover;
	adjusted_mode->crtc_vsync_end = adjusted_mode->crtc_vsync_start + vsync_width;

}

bool amdgpu_dig_monitor_is_duallink(struct drm_encoder *encoder,
				    u32 pixel_clock)
{
	struct drm_connector *connector;
	struct amdgpu_connector *amdgpu_connector;
	struct amdgpu_connector_atom_dig *dig_connector;

	connector = amdgpu_get_connector_for_encoder(encoder);
	/* if we don't have an active device yet, just use one of
	 * the connectors tied to the encoder.
	 */
	if (!connector)
		connector = amdgpu_get_connector_for_encoder_init(encoder);
	amdgpu_connector = to_amdgpu_connector(connector);

	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_HDMIB:
		if (amdgpu_connector->use_digital) {
			/* HDMI 1.3 supports up to 340 Mhz over single link */
			if (drm_detect_hdmi_monitor(amdgpu_connector_edid(connector))) {
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
		dig_connector = amdgpu_connector->con_priv;
		if ((dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_DISPLAYPORT) ||
		    (dig_connector->dp_sink_type == CONNECTOR_OBJECT_ID_eDP))
			return false;
		else {
			/* HDMI 1.3 supports up to 340 Mhz over single link */
			if (drm_detect_hdmi_monitor(amdgpu_connector_edid(connector))) {
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
