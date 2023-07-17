// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>

#include "lsdc_drv.h"
#include "lsdc_output.h"

/*
 * The display controller in the LS7A1000 exports two DVO interfaces, thus
 * external encoder is required, except connected to the DPI panel directly.
 *
 *       ___________________                                     _________
 *      |            -------|                                   |         |
 *      |  CRTC0 --> | DVO0 ----> Encoder0 ---> Connector0 ---> | Display |
 *      |  _   _     -------|        ^             ^            |_________|
 *      | | | | |  +------+ |        |             |
 *      | |_| |_|  | i2c6 | <--------+-------------+
 *      |          +------+ |
 *      |                   |
 *      |  DC in LS7A1000   |
 *      |                   |
 *      |  _   _   +------+ |
 *      | | | | |  | i2c7 | <--------+-------------+
 *      | |_| |_|  +------+ |        |             |             _________
 *      |            -------|        |             |            |         |
 *      |  CRTC1 --> | DVO1 ----> Encoder1 ---> Connector1 ---> |  Panel  |
 *      |            -------|                                   |_________|
 *      |___________________|
 *
 * Currently, we assume the external encoders connected to the DVO are
 * transparent. Loongson's DVO interface can directly drive RGB888 panels.
 *
 *  TODO: Add support for non-transparent encoders
 */

static int ls7a1000_dpi_connector_get_modes(struct drm_connector *conn)
{
	unsigned int num = 0;
	struct edid *edid;

	if (conn->ddc) {
		edid = drm_get_edid(conn, conn->ddc);
		if (edid) {
			drm_connector_update_edid_property(conn, edid);
			num = drm_add_edid_modes(conn, edid);
			kfree(edid);
		}

		return num;
	}

	num = drm_add_modes_noedid(conn, 1920, 1200);

	drm_set_preferred_mode(conn, 1024, 768);

	return num;
}

static struct drm_encoder *
ls7a1000_dpi_connector_get_best_encoder(struct drm_connector *connector,
					struct drm_atomic_state *state)
{
	struct lsdc_output *output = connector_to_lsdc_output(connector);

	return &output->encoder;
}

static const struct drm_connector_helper_funcs
ls7a1000_dpi_connector_helpers = {
	.atomic_best_encoder = ls7a1000_dpi_connector_get_best_encoder,
	.get_modes = ls7a1000_dpi_connector_get_modes,
};

static enum drm_connector_status
ls7a1000_dpi_connector_detect(struct drm_connector *connector, bool force)
{
	struct i2c_adapter *ddc = connector->ddc;

	if (ddc) {
		if (drm_probe_ddc(ddc))
			return connector_status_connected;

		return connector_status_disconnected;
	}

	return connector_status_unknown;
}

static const struct drm_connector_funcs ls7a1000_dpi_connector_funcs = {
	.detect = ls7a1000_dpi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state
};

static void ls7a1000_pipe0_encoder_reset(struct drm_encoder *encoder)
{
	struct drm_device *ddev = encoder->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);

	/*
	 * We need this for S3 support, screen will not lightup if don't set
	 * this register correctly.
	 */
	lsdc_wreg32(ldev, LSDC_CRTC0_DVO_CONF_REG,
		    PHY_CLOCK_POL | PHY_CLOCK_EN | PHY_DATA_EN);
}

static void ls7a1000_pipe1_encoder_reset(struct drm_encoder *encoder)
{
	struct drm_device *ddev = encoder->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);

	/*
	 * We need this for S3 support, screen will not lightup if don't set
	 * this register correctly.
	 */

	/* DVO */
	lsdc_wreg32(ldev, LSDC_CRTC1_DVO_CONF_REG,
		    BIT(31) | PHY_CLOCK_POL | PHY_CLOCK_EN | PHY_DATA_EN);
}

static const struct drm_encoder_funcs ls7a1000_encoder_funcs[2] = {
	{
		.reset = ls7a1000_pipe0_encoder_reset,
		.destroy = drm_encoder_cleanup,
	},
	{
		.reset = ls7a1000_pipe1_encoder_reset,
		.destroy = drm_encoder_cleanup,
	},
};

int ls7a1000_output_init(struct drm_device *ddev,
			 struct lsdc_display_pipe *dispipe,
			 struct i2c_adapter *ddc,
			 unsigned int index)
{
	struct lsdc_output *output = &dispipe->output;
	struct drm_encoder *encoder = &output->encoder;
	struct drm_connector *connector = &output->connector;
	int ret;

	ret = drm_encoder_init(ddev, encoder, &ls7a1000_encoder_funcs[index],
			       DRM_MODE_ENCODER_TMDS, "encoder-%u", index);
	if (ret)
		return ret;

	encoder->possible_crtcs = BIT(index);

	ret = drm_connector_init_with_ddc(ddev, connector,
					  &ls7a1000_dpi_connector_funcs,
					  DRM_MODE_CONNECTOR_DPI, ddc);
	if (ret)
		return ret;

	drm_info(ddev, "display pipe-%u has a DVO\n", index);

	drm_connector_helper_add(connector, &ls7a1000_dpi_connector_helpers);

	drm_connector_attach_encoder(connector, encoder);

	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			    DRM_CONNECTOR_POLL_DISCONNECT;

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	return 0;
}
