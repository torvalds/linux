// SPDX-License-Identifier: GPL-2.0+

#include "vkms_drv.h"
#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

static void vkms_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs vkms_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = vkms_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int vkms_conn_get_modes(struct drm_connector *connector)
{
	int count;

	count = drm_add_modes_noedid(connector, XRES_MAX, YRES_MAX);
	drm_set_preferred_mode(connector, XRES_DEF, YRES_DEF);

	return count;
}

static const struct drm_connector_helper_funcs vkms_conn_helper_funcs = {
	.get_modes    = vkms_conn_get_modes,
};

int vkms_output_init(struct vkms_device *vkmsdev, int index)
{
	struct vkms_output *output = &vkmsdev->output;
	struct drm_device *dev = &vkmsdev->drm;
	struct drm_connector *connector = &output->connector;
	struct drm_encoder *encoder = &output->encoder;
	struct drm_crtc *crtc = &output->crtc;
	struct drm_plane *primary, *cursor = NULL;
	int ret;

	primary = vkms_plane_init(vkmsdev, DRM_PLANE_TYPE_PRIMARY, index);
	if (IS_ERR(primary))
		return PTR_ERR(primary);

	if (enable_cursor) {
		cursor = vkms_plane_init(vkmsdev, DRM_PLANE_TYPE_CURSOR, index);
		if (IS_ERR(cursor)) {
			ret = PTR_ERR(cursor);
			goto err_cursor;
		}
	}

	ret = vkms_crtc_init(dev, crtc, primary, cursor);
	if (ret)
		goto err_crtc;

	ret = drm_connector_init(dev, connector, &vkms_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret) {
		DRM_ERROR("Failed to init connector\n");
		goto err_connector;
	}

	drm_connector_helper_add(connector, &vkms_conn_helper_funcs);

	ret = drm_simple_encoder_init(dev, encoder, DRM_MODE_ENCODER_VIRTUAL);
	if (ret) {
		DRM_ERROR("Failed to init encoder\n");
		goto err_encoder;
	}
	encoder->possible_crtcs = 1;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret) {
		DRM_ERROR("Failed to attach connector to encoder\n");
		goto err_attach;
	}

	ret = vkms_enable_writeback_connector(vkmsdev);
	if (ret)
		DRM_ERROR("Failed to init writeback connector\n");

	drm_mode_config_reset(dev);

	return 0;

err_attach:
	drm_encoder_cleanup(encoder);

err_encoder:
	drm_connector_cleanup(connector);

err_connector:
	drm_crtc_cleanup(crtc);

err_crtc:
	if (enable_cursor)
		drm_plane_cleanup(cursor);

err_cursor:
	drm_plane_cleanup(primary);

	return ret;
}
