// SPDX-License-Identifier: GPL-2.0-only

#include <drm/drm_atomic_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>

#include "mgag200_ddc.h"
#include "mgag200_drv.h"

static const struct drm_encoder_funcs mgag200_dac_encoder_funcs = {
	.destroy = drm_encoder_cleanup
};

static const struct drm_connector_helper_funcs mgag200_vga_connector_helper_funcs = {
	.get_modes = drm_connector_helper_get_modes,
	.detect_ctx = drm_connector_helper_detect_from_ddc
};

static const struct drm_connector_funcs mgag200_vga_connector_funcs = {
	.reset                  = drm_atomic_helper_connector_reset,
	.fill_modes             = drm_helper_probe_single_connector_modes,
	.destroy                = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_connector_destroy_state
};

int mgag200_vga_output_init(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct drm_crtc *crtc = &mdev->crtc;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct i2c_adapter *ddc;
	int ret;

	encoder = &mdev->output.vga.encoder;
	ret = drm_encoder_init(dev, encoder, &mgag200_dac_encoder_funcs,
			       DRM_MODE_ENCODER_DAC, NULL);
	if (ret) {
		drm_err(dev, "drm_encoder_init() failed: %d\n", ret);
		return ret;
	}
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ddc = mgag200_ddc_create(mdev);
	if (IS_ERR(ddc)) {
		ret = PTR_ERR(ddc);
		drm_err(dev, "failed to add DDC bus: %d\n", ret);
		return ret;
	}

	connector = &mdev->output.vga.connector;
	ret = drm_connector_init_with_ddc(dev, connector,
					  &mgag200_vga_connector_funcs,
					  DRM_MODE_CONNECTOR_VGA, ddc);
	if (ret) {
		drm_err(dev, "drm_connector_init_with_ddc() failed: %d\n", ret);
		return ret;
	}
	drm_connector_helper_add(connector, &mgag200_vga_connector_helper_funcs);

	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			    DRM_CONNECTOR_POLL_DISCONNECT;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret) {
		drm_err(dev, "drm_connector_attach_encoder() failed: %d\n", ret);
		return ret;
	}

	return 0;
}
