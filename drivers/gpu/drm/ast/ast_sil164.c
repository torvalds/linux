// SPDX-License-Identifier: MIT

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "ast_ddc.h"
#include "ast_drv.h"

/*
 * Encoder
 */

static const struct drm_encoder_funcs ast_sil164_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

/*
 * Connector
 */

static int ast_sil164_connector_helper_get_modes(struct drm_connector *connector)
{
	struct ast_connector *ast_connector = to_ast_connector(connector);
	int count;

	if (ast_connector->physical_status == connector_status_connected) {
		count = drm_connector_helper_get_modes(connector);
	} else {
		drm_edid_connector_update(connector, NULL);

		/*
		 * There's no EDID data without a connected monitor. Set BMC-
		 * compatible modes in this case. The XGA default resolution
		 * should work well for all BMCs.
		 */
		count = drm_add_modes_noedid(connector, 4096, 4096);
		if (count)
			drm_set_preferred_mode(connector, 1024, 768);
	}

	return count;
}

static int ast_sil164_connector_helper_detect_ctx(struct drm_connector *connector,
						  struct drm_modeset_acquire_ctx *ctx,
						  bool force)
{
	struct ast_connector *ast_connector = to_ast_connector(connector);
	enum drm_connector_status status;

	status = drm_connector_helper_detect_from_ddc(connector, ctx, force);

	if (status != ast_connector->physical_status)
		++connector->epoch_counter;
	ast_connector->physical_status = status;

	return connector_status_connected;
}

static const struct drm_connector_helper_funcs ast_sil164_connector_helper_funcs = {
	.get_modes = ast_sil164_connector_helper_get_modes,
	.detect_ctx = ast_sil164_connector_helper_detect_ctx,
};

static const struct drm_connector_funcs ast_sil164_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/*
 * Output
 */

int ast_sil164_output_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct drm_crtc *crtc = &ast->crtc;
	struct i2c_adapter *ddc;
	struct drm_encoder *encoder;
	struct ast_connector *ast_connector;
	struct drm_connector *connector;
	int ret;

	/* DDC */

	ddc = ast_ddc_create(ast);
	if (IS_ERR(ddc))
		return PTR_ERR(ddc);

	/* encoder */

	encoder = &ast->output.sil164.encoder;
	ret = drm_encoder_init(dev, encoder, &ast_sil164_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	/* connector */

	ast_connector = &ast->output.sil164.connector;
	connector = &ast_connector->base;
	ret = drm_connector_init_with_ddc(dev, connector, &ast_sil164_connector_funcs,
					  DRM_MODE_CONNECTOR_DVII, ddc);
	if (ret)
		return ret;
	drm_connector_helper_add(connector, &ast_sil164_connector_helper_funcs);

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;
	connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	ast_connector->physical_status = connector->status;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	return 0;
}
