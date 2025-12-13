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
#include <drm/drm_probe_helper.h>

#include "i915_drv.h"
#include "i915_utils.h"
#include "intel_connector.h"
#include "intel_display_core.h"
#include "intel_display_debugfs.h"
#include "intel_display_types.h"
#include "intel_hdcp.h"
#include "intel_panel.h"

static void intel_connector_modeset_retry_work_fn(struct work_struct *work)
{
	struct intel_connector *connector = container_of(work, typeof(*connector),
							 modeset_retry_work);
	struct intel_display *display = to_intel_display(connector);

	drm_dbg_kms(display->drm, "[CONNECTOR:%d:%s]\n", connector->base.base.id,
		    connector->base.name);

	/* Grab the locks before changing connector property*/
	mutex_lock(&display->drm->mode_config.mutex);
	/* Set connector link status to BAD and send a Uevent to notify
	 * userspace to do a modeset.
	 */
	drm_connector_set_link_status_property(&connector->base,
					       DRM_MODE_LINK_STATUS_BAD);
	mutex_unlock(&display->drm->mode_config.mutex);
	/* Send Hotplug uevent so userspace can reprobe */
	drm_kms_helper_connector_hotplug_event(&connector->base);

	drm_connector_put(&connector->base);
}

void intel_connector_queue_modeset_retry_work(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);

	drm_connector_get(&connector->base);
	if (!queue_work(display->wq.unordered, &connector->modeset_retry_work))
		drm_connector_put(&connector->base);
}

void intel_connector_cancel_modeset_retry_work(struct intel_connector *connector)
{
	if (cancel_work_sync(&connector->modeset_retry_work))
		drm_connector_put(&connector->base);
}

static int intel_connector_init(struct intel_connector *connector)
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

	intel_panel_init_alloc(connector);

	INIT_WORK(&connector->modeset_retry_work,
		  intel_connector_modeset_retry_work_fn);

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

	drm_edid_free(intel_connector->detect_edid);

	intel_hdcp_cleanup(intel_connector);

	intel_panel_fini(intel_connector);

	drm_connector_cleanup(connector);

	if (intel_connector->mst.port)
		drm_dp_mst_put_port_malloc(intel_connector->mst.port);

	kfree(connector);
}

int intel_connector_register(struct drm_connector *_connector)
{
	struct intel_connector *connector = to_intel_connector(_connector);
	struct drm_i915_private *i915 = to_i915(_connector->dev);
	int ret;

	ret = intel_panel_register(connector);
	if (ret)
		goto err;

	if (i915_inject_probe_failure(i915)) {
		ret = -EFAULT;
		goto err_panel;
	}

	intel_connector_debugfs_add(connector);

	return 0;

err_panel:
	intel_panel_unregister(connector);
err:
	return ret;
}

void intel_connector_unregister(struct drm_connector *_connector)
{
	struct intel_connector *connector = to_intel_connector(_connector);

	intel_panel_unregister(connector);
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
	struct intel_display *display = to_intel_display(connector);

	drm_modeset_lock_assert_held(&display->drm->mode_config.connection_mutex);

	if (!connector->base.state->crtc)
		return INVALID_PIPE;

	return to_intel_crtc(connector->base.state->crtc)->pipe;
}

/**
 * intel_connector_update_modes - update connector from edid
 * @connector: DRM connector device to use
 * @drm_edid: previously read EDID information
 */
int intel_connector_update_modes(struct drm_connector *connector,
				 const struct drm_edid *drm_edid)
{
	int ret;

	drm_edid_connector_update(connector, drm_edid);
	ret = drm_edid_connector_add_modes(connector);

	return ret;
}

/**
 * intel_ddc_get_modes - get modelist from monitor
 * @connector: DRM connector device to use
 * @ddc: DDC bus i2c adapter
 *
 * Fetch the EDID information from @connector using the DDC bus.
 */
int intel_ddc_get_modes(struct drm_connector *connector,
			struct i2c_adapter *ddc)
{
	const struct drm_edid *drm_edid;
	int ret;

	drm_edid = drm_edid_read_ddc(connector, ddc);
	if (!drm_edid)
		return 0;

	ret = intel_connector_update_modes(connector, drm_edid);
	drm_edid_free(drm_edid);

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
	struct intel_display *display = to_intel_display(connector->dev);
	struct drm_property *prop;

	prop = display->properties.force_audio;
	if (prop == NULL) {
		prop = drm_property_create_enum(display->drm, 0,
						"audio",
						force_audio_names,
						ARRAY_SIZE(force_audio_names));
		if (prop == NULL)
			return;

		display->properties.force_audio = prop;
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
	struct intel_display *display = to_intel_display(connector->dev);
	struct drm_property *prop;

	prop = display->properties.broadcast_rgb;
	if (prop == NULL) {
		prop = drm_property_create_enum(display->drm, DRM_MODE_PROP_ENUM,
						"Broadcast RGB",
						broadcast_rgb_names,
						ARRAY_SIZE(broadcast_rgb_names));
		if (prop == NULL)
			return;

		display->properties.broadcast_rgb = prop;
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
	if (!drm_mode_create_hdmi_colorspace_property(connector, 0))
		drm_connector_attach_colorspace_property(connector);
}

void
intel_attach_dp_colorspace_property(struct drm_connector *connector)
{
	if (!drm_mode_create_dp_colorspace_property(connector, 0))
		drm_connector_attach_colorspace_property(connector);
}

void
intel_attach_scaling_mode_property(struct drm_connector *connector)
{
	struct intel_display *display = to_intel_display(connector->dev);
	u32 scaling_modes;

	scaling_modes = BIT(DRM_MODE_SCALE_ASPECT) |
		BIT(DRM_MODE_SCALE_FULLSCREEN);

	/* On GMCH platforms borders are only possible on the LVDS port */
	if (!HAS_GMCH(display) || connector->connector_type == DRM_MODE_CONNECTOR_LVDS)
		scaling_modes |= BIT(DRM_MODE_SCALE_CENTER);

	drm_connector_attach_scaling_mode_property(connector, scaling_modes);

	connector->state->scaling_mode = DRM_MODE_SCALE_ASPECT;
}
