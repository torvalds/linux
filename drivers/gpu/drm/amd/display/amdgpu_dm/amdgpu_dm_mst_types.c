/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#include <linux/version.h>
#include <drm/drm_atomic_helper.h>
#include "dm_services.h"
#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_mst_types.h"

#include "dc.h"
#include "dm_helpers.h"

#include "dc_link_ddc.h"

/* #define TRACE_DPCD */

#ifdef TRACE_DPCD
#define SIDE_BAND_MSG(address) (address >= DP_SIDEBAND_MSG_DOWN_REQ_BASE && address < DP_SINK_COUNT_ESI)

static inline char *side_band_msg_type_to_str(uint32_t address)
{
	static char str[10] = {0};

	if (address < DP_SIDEBAND_MSG_UP_REP_BASE)
		strcpy(str, "DOWN_REQ");
	else if (address < DP_SIDEBAND_MSG_DOWN_REP_BASE)
		strcpy(str, "UP_REP");
	else if (address < DP_SIDEBAND_MSG_UP_REQ_BASE)
		strcpy(str, "DOWN_REP");
	else
		strcpy(str, "UP_REQ");

	return str;
}

static void log_dpcd(uint8_t type,
		     uint32_t address,
		     uint8_t *data,
		     uint32_t size,
		     bool res)
{
	DRM_DEBUG_KMS("Op: %s, addr: %04x, SideBand Msg: %s, Op res: %s\n",
			(type == DP_AUX_NATIVE_READ) ||
			(type == DP_AUX_I2C_READ) ?
					"Read" : "Write",
			address,
			SIDE_BAND_MSG(address) ?
					side_band_msg_type_to_str(address) : "Nop",
			res ? "OK" : "Fail");

	if (res) {
		print_hex_dump(KERN_INFO, "Body: ", DUMP_PREFIX_NONE, 16, 1, data, size, false);
	}
}
#endif

static ssize_t dm_dp_aux_transfer(struct drm_dp_aux *aux,
				  struct drm_dp_aux_msg *msg)
{
	enum i2c_mot_mode mot = (msg->request & DP_AUX_I2C_MOT) ?
		I2C_MOT_TRUE : I2C_MOT_FALSE;
	enum ddc_result res;

	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_READ:
		res = dal_ddc_service_read_dpcd_data(
				TO_DM_AUX(aux)->ddc_service,
				false,
				I2C_MOT_UNDEF,
				msg->address,
				msg->buffer,
				msg->size);
		break;
	case DP_AUX_NATIVE_WRITE:
		res = dal_ddc_service_write_dpcd_data(
				TO_DM_AUX(aux)->ddc_service,
				false,
				I2C_MOT_UNDEF,
				msg->address,
				msg->buffer,
				msg->size);
		break;
	case DP_AUX_I2C_READ:
		res = dal_ddc_service_read_dpcd_data(
				TO_DM_AUX(aux)->ddc_service,
				true,
				mot,
				msg->address,
				msg->buffer,
				msg->size);
		break;
	case DP_AUX_I2C_WRITE:
		res = dal_ddc_service_write_dpcd_data(
				TO_DM_AUX(aux)->ddc_service,
				true,
				mot,
				msg->address,
				msg->buffer,
				msg->size);
		break;
	default:
		return 0;
	}

#ifdef TRACE_DPCD
	log_dpcd(msg->request,
		 msg->address,
		 msg->buffer,
		 msg->size,
		 r == DDC_RESULT_SUCESSFULL);
#endif

	return msg->size;
}

static enum drm_connector_status
dm_dp_mst_detect(struct drm_connector *connector, bool force)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct amdgpu_dm_connector *master = aconnector->mst_port;

	enum drm_connector_status status =
		drm_dp_mst_detect_port(
			connector,
			&master->mst_mgr,
			aconnector->port);

	return status;
}

static void
dm_dp_mst_connector_destroy(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector = to_amdgpu_dm_connector(connector);
	struct amdgpu_encoder *amdgpu_encoder = amdgpu_dm_connector->mst_encoder;

	drm_encoder_cleanup(&amdgpu_encoder->base);
	kfree(amdgpu_encoder);
	drm_connector_cleanup(connector);
	kfree(amdgpu_dm_connector);
}

static const struct drm_connector_funcs dm_dp_mst_connector_funcs = {
	.detect = dm_dp_mst_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = dm_dp_mst_connector_destroy,
	.reset = amdgpu_dm_connector_funcs_reset,
	.atomic_duplicate_state = amdgpu_dm_connector_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = amdgpu_dm_connector_atomic_set_property,
	.atomic_get_property = amdgpu_dm_connector_atomic_get_property
};

static int dm_connector_update_modes(struct drm_connector *connector,
				struct edid *edid)
{
	return drm_add_edid_modes(connector, edid);
}

void dm_dp_mst_dc_sink_create(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct edid *edid;
	struct dc_sink *dc_sink;
	struct dc_sink_init_data init_params = {
			.link = aconnector->dc_link,
			.sink_signal = SIGNAL_TYPE_DISPLAY_PORT_MST };

	/*
	 * TODO: Need to further figure out why ddc.algo is NULL while MST port exists
	 */
	if (!aconnector->port || !aconnector->port->aux.ddc.algo)
		return;

	edid = drm_dp_mst_get_edid(connector, &aconnector->mst_port->mst_mgr, aconnector->port);

	if (!edid) {
		drm_mode_connector_update_edid_property(
			&aconnector->base,
			NULL);
		return;
	}

	aconnector->edid = edid;

	dc_sink = dc_link_add_remote_sink(
		aconnector->dc_link,
		(uint8_t *)aconnector->edid,
		(aconnector->edid->extensions + 1) * EDID_LENGTH,
		&init_params);

	dc_sink->priv = aconnector;
	aconnector->dc_sink = dc_sink;

	amdgpu_dm_add_sink_to_freesync_module(
			connector, aconnector->edid);

	drm_mode_connector_update_edid_property(
					&aconnector->base, aconnector->edid);
}

static int dm_dp_mst_get_modes(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	int ret = 0;

	if (!aconnector)
		return dm_connector_update_modes(connector, NULL);

	if (!aconnector->edid) {
		struct edid *edid;
		struct dc_sink *dc_sink;
		struct dc_sink_init_data init_params = {
				.link = aconnector->dc_link,
				.sink_signal = SIGNAL_TYPE_DISPLAY_PORT_MST };
		edid = drm_dp_mst_get_edid(connector, &aconnector->mst_port->mst_mgr, aconnector->port);

		if (!edid) {
			drm_mode_connector_update_edid_property(
				&aconnector->base,
				NULL);
			return ret;
		}

		aconnector->edid = edid;

		dc_sink = dc_link_add_remote_sink(
			aconnector->dc_link,
			(uint8_t *)edid,
			(edid->extensions + 1) * EDID_LENGTH,
			&init_params);

		dc_sink->priv = aconnector;
		aconnector->dc_sink = dc_sink;

		if (aconnector->dc_sink)
			amdgpu_dm_add_sink_to_freesync_module(
					connector, edid);

		drm_mode_connector_update_edid_property(
						&aconnector->base, edid);
	}

	ret = dm_connector_update_modes(connector, aconnector->edid);

	return ret;
}

static struct drm_encoder *dm_mst_best_encoder(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector = to_amdgpu_dm_connector(connector);

	return &amdgpu_dm_connector->mst_encoder->base;
}

static const struct drm_connector_helper_funcs dm_dp_mst_connector_helper_funcs = {
	.get_modes = dm_dp_mst_get_modes,
	.mode_valid = amdgpu_dm_connector_mode_valid,
	.best_encoder = dm_mst_best_encoder,
};

static void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs amdgpu_dm_encoder_funcs = {
	.destroy = amdgpu_dm_encoder_destroy,
};

static struct amdgpu_encoder *
dm_dp_create_fake_mst_encoder(struct amdgpu_dm_connector *connector)
{
	struct drm_device *dev = connector->base.dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder;
	struct drm_encoder *encoder;
	const struct drm_connector_helper_funcs *connector_funcs =
		connector->base.helper_private;
	struct drm_encoder *enc_master =
		connector_funcs->best_encoder(&connector->base);

	DRM_DEBUG_KMS("enc master is %p\n", enc_master);
	amdgpu_encoder = kzalloc(sizeof(*amdgpu_encoder), GFP_KERNEL);
	if (!amdgpu_encoder)
		return NULL;

	encoder = &amdgpu_encoder->base;
	encoder->possible_crtcs = amdgpu_dm_get_encoder_crtc_mask(adev);

	drm_encoder_init(
		dev,
		&amdgpu_encoder->base,
		&amdgpu_dm_encoder_funcs,
		DRM_MODE_ENCODER_DPMST,
		NULL);

	drm_encoder_helper_add(encoder, &amdgpu_dm_encoder_helper_funcs);

	return amdgpu_encoder;
}

static struct drm_connector *
dm_dp_add_mst_connector(struct drm_dp_mst_topology_mgr *mgr,
			struct drm_dp_mst_port *port,
			const char *pathprop)
{
	struct amdgpu_dm_connector *master = container_of(mgr, struct amdgpu_dm_connector, mst_mgr);
	struct drm_device *dev = master->base.dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->mst_port == master
				&& !aconnector->port) {
			DRM_INFO("DM_MST: reusing connector: %p [id: %d] [master: %p]\n",
						aconnector, connector->base.id, aconnector->mst_port);

			aconnector->port = port;
			drm_mode_connector_set_path_property(connector, pathprop);

			drm_connector_list_iter_end(&conn_iter);
			aconnector->mst_connected = true;
			return &aconnector->base;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	aconnector = kzalloc(sizeof(*aconnector), GFP_KERNEL);
	if (!aconnector)
		return NULL;

	connector = &aconnector->base;
	aconnector->port = port;
	aconnector->mst_port = master;

	if (drm_connector_init(
		dev,
		connector,
		&dm_dp_mst_connector_funcs,
		DRM_MODE_CONNECTOR_DisplayPort)) {
		kfree(aconnector);
		return NULL;
	}
	drm_connector_helper_add(connector, &dm_dp_mst_connector_helper_funcs);

	amdgpu_dm_connector_init_helper(
		&adev->dm,
		aconnector,
		DRM_MODE_CONNECTOR_DisplayPort,
		master->dc_link,
		master->connector_id);

	aconnector->mst_encoder = dm_dp_create_fake_mst_encoder(master);

	/*
	 * TODO: understand why this one is needed
	 */
	drm_object_attach_property(
		&connector->base,
		dev->mode_config.path_property,
		0);
	drm_object_attach_property(
		&connector->base,
		dev->mode_config.tile_property,
		0);

	drm_mode_connector_set_path_property(connector, pathprop);

	/*
	 * Initialize connector state before adding the connectror to drm and
	 * framebuffer lists
	 */
	amdgpu_dm_connector_funcs_reset(connector);

	aconnector->mst_connected = true;

	DRM_INFO("DM_MST: added connector: %p [id: %d] [master: %p]\n",
			aconnector, connector->base.id, aconnector->mst_port);

	DRM_DEBUG_KMS(":%d\n", connector->base.id);

	return connector;
}

static void dm_dp_destroy_mst_connector(struct drm_dp_mst_topology_mgr *mgr,
					struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);

	DRM_INFO("DM_MST: Disabling connector: %p [id: %d] [master: %p]\n",
				aconnector, connector->base.id, aconnector->mst_port);

	aconnector->port = NULL;
	if (aconnector->dc_sink) {
		amdgpu_dm_remove_sink_from_freesync_module(connector);
		dc_link_remove_remote_sink(aconnector->dc_link, aconnector->dc_sink);
		dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
	}
	if (aconnector->edid) {
		kfree(aconnector->edid);
		aconnector->edid = NULL;
	}

	drm_mode_connector_update_edid_property(
			&aconnector->base,
			NULL);

	aconnector->mst_connected = false;
}

static void dm_dp_mst_hotplug(struct drm_dp_mst_topology_mgr *mgr)
{
	struct amdgpu_dm_connector *master = container_of(mgr, struct amdgpu_dm_connector, mst_mgr);
	struct drm_device *dev = master->base.dev;

	drm_kms_helper_hotplug_event(dev);
}

static void dm_dp_mst_link_status_reset(struct drm_connector *connector)
{
	mutex_lock(&connector->dev->mode_config.mutex);
	drm_mode_connector_set_link_status_property(connector, DRM_MODE_LINK_STATUS_BAD);
	mutex_unlock(&connector->dev->mode_config.mutex);
}

static void dm_dp_mst_register_connector(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);

	if (adev->mode_info.rfbdev)
		drm_fb_helper_add_one_connector(&adev->mode_info.rfbdev->helper, connector);
	else
		DRM_ERROR("adev->mode_info.rfbdev is NULL\n");

	drm_connector_register(connector);

	if (aconnector->mst_connected)
		dm_dp_mst_link_status_reset(connector);
}

static const struct drm_dp_mst_topology_cbs dm_mst_cbs = {
	.add_connector = dm_dp_add_mst_connector,
	.destroy_connector = dm_dp_destroy_mst_connector,
	.hotplug = dm_dp_mst_hotplug,
	.register_connector = dm_dp_mst_register_connector
};

void amdgpu_dm_initialize_dp_connector(struct amdgpu_display_manager *dm,
				       struct amdgpu_dm_connector *aconnector)
{
	aconnector->dm_dp_aux.aux.name = "dmdc";
	aconnector->dm_dp_aux.aux.dev = dm->adev->dev;
	aconnector->dm_dp_aux.aux.transfer = dm_dp_aux_transfer;
	aconnector->dm_dp_aux.ddc_service = aconnector->dc_link->ddc;

	drm_dp_aux_register(&aconnector->dm_dp_aux.aux);
	aconnector->mst_mgr.cbs = &dm_mst_cbs;
	drm_dp_mst_topology_mgr_init(
		&aconnector->mst_mgr,
		dm->adev->ddev,
		&aconnector->dm_dp_aux.aux,
		16,
		4,
		aconnector->connector_id);
}

