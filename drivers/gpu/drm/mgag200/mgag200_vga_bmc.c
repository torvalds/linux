// SPDX-License-Identifier: GPL-2.0-only

#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>

#include "mgag200_ddc.h"
#include "mgag200_drv.h"

static void mgag200_vga_bmc_encoder_atomic_disable(struct drm_encoder *encoder,
						   struct drm_atomic_state *state)
{
	struct mga_device *mdev = to_mga_device(encoder->dev);

	if (mdev->info->sync_bmc)
		mgag200_bmc_stop_scanout(mdev);
}

static void mgag200_vga_bmc_encoder_atomic_enable(struct drm_encoder *encoder,
						  struct drm_atomic_state *state)
{
	struct mga_device *mdev = to_mga_device(encoder->dev);

	if (mdev->info->sync_bmc)
		mgag200_bmc_start_scanout(mdev);
}

static int mgag200_vga_bmc_encoder_atomic_check(struct drm_encoder *encoder,
						struct drm_crtc_state *new_crtc_state,
						struct drm_connector_state *new_connector_state)
{
	struct mga_device *mdev = to_mga_device(encoder->dev);
	struct mgag200_crtc_state *new_mgag200_crtc_state = to_mgag200_crtc_state(new_crtc_state);

	new_mgag200_crtc_state->set_vidrst = mdev->info->sync_bmc;

	return 0;
}

static const struct drm_encoder_helper_funcs mgag200_dac_encoder_helper_funcs = {
	.atomic_disable = mgag200_vga_bmc_encoder_atomic_disable,
	.atomic_enable = mgag200_vga_bmc_encoder_atomic_enable,
	.atomic_check = mgag200_vga_bmc_encoder_atomic_check,
};

static const struct drm_encoder_funcs mgag200_dac_encoder_funcs = {
	.destroy = drm_encoder_cleanup
};

static int mgag200_vga_bmc_connector_helper_get_modes(struct drm_connector *connector)
{
	struct mga_device *mdev = to_mga_device(connector->dev);
	const struct mgag200_device_info *minfo = mdev->info;
	int count;

	count = drm_connector_helper_get_modes(connector);

	if (!count) {
		/*
		 * There's no EDID data without a connected monitor. Set BMC-
		 * compatible modes in this case. The XGA default resolution
		 * should work well for all BMCs.
		 */
		count = drm_add_modes_noedid(connector, minfo->max_hdisplay, minfo->max_vdisplay);
		if (count)
			drm_set_preferred_mode(connector, 1024, 768);
	}

	return count;
}

/*
 * There's no monitor connected if the DDC did not return an EDID. Still
 * return 'connected' as there's always a BMC. Incrementing the connector's
 * epoch counter triggers an update of the related properties.
 */
static int mgag200_vga_bmc_connector_helper_detect_ctx(struct drm_connector *connector,
						       struct drm_modeset_acquire_ctx *ctx,
						       bool force)
{
	enum drm_connector_status old_status, status;

	if (connector->edid_blob_ptr)
		old_status = connector_status_connected;
	else
		old_status = connector_status_disconnected;

	status = drm_connector_helper_detect_from_ddc(connector, ctx, force);

	if (status != old_status)
		++connector->epoch_counter;
	return connector_status_connected;
}

static const struct drm_connector_helper_funcs mgag200_vga_connector_helper_funcs = {
	.get_modes = mgag200_vga_bmc_connector_helper_get_modes,
	.detect_ctx = mgag200_vga_bmc_connector_helper_detect_ctx,
};

static const struct drm_connector_funcs mgag200_vga_connector_funcs = {
	.reset                  = drm_atomic_helper_connector_reset,
	.fill_modes             = drm_helper_probe_single_connector_modes,
	.destroy                = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_connector_destroy_state
};

int mgag200_vga_bmc_output_init(struct mga_device *mdev)
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
	drm_encoder_helper_add(encoder, &mgag200_dac_encoder_helper_funcs);

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
