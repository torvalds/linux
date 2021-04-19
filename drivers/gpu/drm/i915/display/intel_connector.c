/*
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2007, 2010 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/i2c.h>
#include <linux/slab.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>

#include "display/intel_panel.h"

#include "i915_drv.h"
#include "intel_connector.h"
#include "intel_display_debugfs.h"
#include "intel_display_types.h"
#include "intel_hdcp.h"

int intel_connector_init(struct intel_connector *connector)
{
	struct intel_digital_connector_state *conn_state;

	/*
	 * Allocate enough memory to hold intel_digital_connector_state,
	 * This might be a few bytes too many, but for connectors that don't
	 * need it we'll free the state and allocate a smaller one on the first
	 * successful commit anyway.
	 */
	conn_state = kzalloc(sizeof(*conn_state), GFP_KERNEL);
	if (!conn_state)
		return -ENOMEM;

	__drm_atomic_helper_connector_reset(&connector->base,
					    &conn_state->base);

	return 0;
}

struct intel_connector *intel_connector_alloc(void)
{
	struct intel_connector *connector;

	connector = kzalloc(sizeof(*connector), GFP_KERNEL);
	if (!connector)
		return NULL;

	if (intel_connector_init(connector) < 0) {
		kfree(connector);
		return NULL;
	}

	return connector;
}

/*
 * Free the bits allocated by intel_connector_alloc.
 * This should only be used after intel_connector_alloc has returned
 * successfully, and before drm_connector_init returns successfully.
 * Otherwise the destroy callbacks for the connector and the state should
 * take care of proper cleanup/free (see intel_connector_destroy).
 */
void intel_connector_free(struct intel_connector *connector)
{
	kfree(to_intel_digital_connector_state(connector->base.state));
	kfree(connector);
}

/*
 * Connector type independent destroy hook for drm_connector_funcs.
 */
void intel_connector_destroy(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	kfree(intel_connector->detect_edid);

	intel_hdcp_cleanup(intel_connector);

	if (!IS_ERR_OR_NULL(intel_connector->edid))
		kfree(intel_connector->edid);

	intel_panel_fini(&intel_connector->panel);

	drm_connector_cleanup(connector);

	if (intel_connector->port)
		drm_dp_mst_put_port_malloc(intel_connector->port);

	kfree(connector);
}

int intel_connector_register(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	int ret;

	ret = intel_backlight_device_register(intel_connector);
	if (ret)
		goto err;

	if (i915_inject_probe_failure(to_i915(connector->dev))) {
		ret = -EFAULT;
		goto err_backlight;
	}

	intel_connector_debugfs_add(connector);

	return 0;

err_backlight:
	intel_backlight_device_unregister(intel_connector);
err:
	return ret;
}

void intel_connector_unregister(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	intel_backlight_device_unregister(intel_connector);
}

void intel_connector_attach_encoder(struct intel_connector *connector,
				    struct intel_encoder *encoder)
{
	connector->encoder = encoder;
	drm_connector_attach_encoder(&connector->base, &encoder->base);
}

/*
 * Simple connector->get_hw_state implementation for encoders that support only
 * one connector and no cloning and hence the encoder state determines the state
 * of the connector.
 */
bool intel_connector_get_hw_state(struct intel_connector *connector)
{
	enum pipe pipe = 0;
	struct intel_encoder *encoder = intel_attached_encoder(connector);

	return encoder->get_hw_state(encoder, &pipe);
}

enum pipe intel_connector_get_pipe(struct intel_connector *connector)
{
	struct drm_device *dev = connector->base.dev;

	drm_WARN_ON(dev,
		    !drm_modeset_is_locked(&dev->mode_config.connection_mutex));

	if (!connector->base.state->crtc)
		return INVALID_PIPE;

	return to_intel_crtc(connector->base.state->crtc)->pipe;
}

/**
 * intel_connector_update_modes - update connector from edid
 * @connector: DRM connector device to use
 * @edid: previously read EDID information
 */
int intel_connector_update_modes(struct drm_connector *connector,
				struct edid *edid)
{
	int ret;

	drm_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);

	return ret;
}

/**
 * intel_ddc_get_modes - get modelist from monitor
 * @connector: DRM connector device to use
 * @adapter: i2c adapter
 *
 * Fetch the EDID information from @connector using the DDC bus.
 */
int intel_ddc_get_modes(struct drm_connector *connector,
			struct i2c_adapter *adapter)
{
	struct edid *edid;
	int ret;

	edid = drm_get_edid(connector, adapter);
	if (!edid)
		return 0;

	ret = intel_connector_update_modes(connector, edid);
	kfree(edid);

	return ret;
}

static const struct drm_prop_enum_list force_audio_names[] = {
	{ HDMI_AUDIO_OFF_DVI, "force-dvi" },
	{ HDMI_AUDIO_OFF, "off" },
	{ HDMI_AUDIO_AUTO, "auto" },
	{ HDMI_AUDIO_ON, "on" },
};

void
intel_attach_force_audio_property(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_property *prop;

	prop = dev_priv->force_audio_property;
	if (prop == NULL) {
		prop = drm_property_create_enum(dev, 0,
					   "audio",
					   force_audio_names,
					   ARRAY_SIZE(force_audio_names));
		if (prop == NULL)
			return;

		dev_priv->force_audio_property = prop;
	}
	drm_object_attach_property(&connector->base, prop, 0);
}

static const struct drm_prop_enum_list broadcast_rgb_names[] = {
	{ INTEL_BROADCAST_RGB_AUTO, "Automatic" },
	{ INTEL_BROADCAST_RGB_FULL, "Full" },
	{ INTEL_BROADCAST_RGB_LIMITED, "Limited 16:235" },
};

void
intel_attach_broadcast_rgb_property(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_property *prop;

	prop = dev_priv->broadcast_rgb_property;
	if (prop == NULL) {
		prop = drm_property_create_enum(dev, DRM_MODE_PROP_ENUM,
					   "Broadcast RGB",
					   broadcast_rgb_names,
					   ARRAY_SIZE(broadcast_rgb_names));
		if (prop == NULL)
			return;

		dev_priv->broadcast_rgb_property = prop;
	}

	drm_object_attach_property(&connector->base, prop, 0);
}

void
intel_attach_aspect_ratio_property(struct drm_connector *connector)
{
	if (!drm_mode_create_aspect_ratio_property(connector->dev))
		drm_object_attach_property(&connector->base,
			connector->dev->mode_config.aspect_ratio_property,
			DRM_MODE_PICTURE_ASPECT_NONE);
}

void
intel_attach_hdmi_colorspace_property(struct drm_connector *connector)
{
	if (!drm_mode_create_hdmi_colorspace_property(connector))
		drm_object_attach_property(&connector->base,
					   connector->colorspace_property, 0);
}

void
intel_attach_dp_colorspace_property(struct drm_connector *connector)
{
	if (!drm_mode_create_dp_colorspace_property(connector))
		drm_object_attach_property(&connector->base,
					   connector->colorspace_property, 0);
}
