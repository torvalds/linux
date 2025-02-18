// SPDX-License-Identifier: GPL-2.0+

#include "vkms_config.h"
#include "vkms_connector.h"
#include "vkms_drv.h"
#include <drm/drm_managed.h>

int vkms_output_init(struct vkms_device *vkmsdev)
{
	struct drm_device *dev = &vkmsdev->drm;
	struct vkms_connector *connector;
	struct drm_encoder *encoder;
	struct vkms_output *output;
	struct vkms_plane *primary, *overlay, *cursor = NULL;
	int ret;
	int writeback;
	unsigned int n;

	/*
	 * Initialize used plane. One primary plane is required to perform the composition.
	 *
	 * The overlay and cursor planes are not mandatory, but can be used to perform complex
	 * composition.
	 */
	primary = vkms_plane_init(vkmsdev, DRM_PLANE_TYPE_PRIMARY);
	if (IS_ERR(primary))
		return PTR_ERR(primary);

	if (vkmsdev->config->cursor) {
		cursor = vkms_plane_init(vkmsdev, DRM_PLANE_TYPE_CURSOR);
		if (IS_ERR(cursor))
			return PTR_ERR(cursor);
	}

	output = vkms_crtc_init(dev, &primary->base,
				cursor ? &cursor->base : NULL);
	if (IS_ERR(output)) {
		DRM_ERROR("Failed to allocate CRTC\n");
		return PTR_ERR(output);
	}

	if (vkmsdev->config->overlay) {
		for (n = 0; n < NUM_OVERLAY_PLANES; n++) {
			overlay = vkms_plane_init(vkmsdev, DRM_PLANE_TYPE_OVERLAY);
			if (IS_ERR(overlay)) {
				DRM_DEV_ERROR(dev->dev, "Failed to init vkms plane\n");
				return PTR_ERR(overlay);
			}
			overlay->base.possible_crtcs = drm_crtc_mask(&output->crtc);
		}
	}

	connector = vkms_connector_init(vkmsdev);
	if (IS_ERR(connector)) {
		DRM_ERROR("Failed to init connector\n");
		return PTR_ERR(connector);
	}

	encoder = drmm_kzalloc(dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder) {
		DRM_ERROR("Failed to allocate encoder\n");
		return -ENOMEM;
	}
	ret = drmm_encoder_init(dev, encoder, NULL,
				DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret) {
		DRM_ERROR("Failed to init encoder\n");
		return ret;
	}
	encoder->possible_crtcs = drm_crtc_mask(&output->crtc);

	/* Attach the encoder and the connector */
	ret = drm_connector_attach_encoder(&connector->base, encoder);
	if (ret) {
		DRM_ERROR("Failed to attach connector to encoder\n");
		return ret;
	}

	/* Initialize the writeback component */
	if (vkmsdev->config->writeback) {
		writeback = vkms_enable_writeback_connector(vkmsdev, output);
		if (writeback)
			DRM_ERROR("Failed to init writeback connector\n");
	}

	drm_mode_config_reset(dev);

	return ret;
}
