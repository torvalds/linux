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

#include <linux/vmalloc.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dp_mst_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fixed.h>
#include <drm/drm_edid.h>
#include "dm_services.h"
#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_mst_types.h"
#include "amdgpu_dm_hdcp.h"

#include "dc.h"
#include "dm_helpers.h"

#include "ddc_service_types.h"
#include "dpcd_defs.h"

#include "dmub_cmd.h"
#if defined(CONFIG_DEBUG_FS)
#include "amdgpu_dm_debugfs.h"
#endif

#include "dc/resource/dcn20/dcn20_resource.h"

#define PEAK_FACTOR_X1000 1006

static ssize_t dm_dp_aux_transfer(struct drm_dp_aux *aux,
				  struct drm_dp_aux_msg *msg)
{
	ssize_t result = 0;
	struct aux_payload payload;
	enum aux_return_code_type operation_result;
	struct amdgpu_device *adev;
	struct ddc_service *ddc;

	if (WARN_ON(msg->size > 16))
		return -E2BIG;

	payload.address = msg->address;
	payload.data = msg->buffer;
	payload.length = msg->size;
	payload.reply = &msg->reply;
	payload.i2c_over_aux = (msg->request & DP_AUX_NATIVE_WRITE) == 0;
	payload.write = (msg->request & DP_AUX_I2C_READ) == 0;
	payload.mot = (msg->request & DP_AUX_I2C_MOT) != 0;
	payload.write_status_update =
			(msg->request & DP_AUX_I2C_WRITE_STATUS_UPDATE) != 0;
	payload.defer_delay = 0;

	result = dc_link_aux_transfer_raw(TO_DM_AUX(aux)->ddc_service, &payload,
				      &operation_result);

	/*
	 * w/a on certain intel platform where hpd is unexpected to pull low during
	 * 1st sideband message transaction by return AUX_RET_ERROR_HPD_DISCON
	 * aux transaction is succuess in such case, therefore bypass the error
	 */
	ddc = TO_DM_AUX(aux)->ddc_service;
	adev = ddc->ctx->driver_context;
	if (adev->dm.aux_hpd_discon_quirk) {
		if (msg->address == DP_SIDEBAND_MSG_DOWN_REQ_BASE &&
			operation_result == AUX_RET_ERROR_HPD_DISCON) {
			result = 0;
			operation_result = AUX_RET_SUCCESS;
		}
	}

	if (payload.write && result >= 0)
		result = msg->size;

	if (result < 0)
		switch (operation_result) {
		case AUX_RET_SUCCESS:
			break;
		case AUX_RET_ERROR_HPD_DISCON:
		case AUX_RET_ERROR_UNKNOWN:
		case AUX_RET_ERROR_INVALID_OPERATION:
		case AUX_RET_ERROR_PROTOCOL_ERROR:
			result = -EIO;
			break;
		case AUX_RET_ERROR_INVALID_REPLY:
		case AUX_RET_ERROR_ENGINE_ACQUIRE:
			result = -EBUSY;
			break;
		case AUX_RET_ERROR_TIMEOUT:
			result = -ETIMEDOUT;
			break;
		}

	return result;
}

static void
dm_dp_mst_connector_destroy(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector =
		to_amdgpu_dm_connector(connector);

	if (aconnector->dc_sink) {
		dc_link_remove_remote_sink(aconnector->dc_link,
					   aconnector->dc_sink);
		dc_sink_release(aconnector->dc_sink);
	}

	kfree(aconnector->edid);

	drm_connector_cleanup(connector);
	drm_dp_mst_put_port_malloc(aconnector->mst_output_port);
	kfree(aconnector);
}

static int
amdgpu_dm_mst_connector_late_register(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
		to_amdgpu_dm_connector(connector);
	int r;

	r = drm_dp_mst_connector_late_register(connector,
					       amdgpu_dm_connector->mst_output_port);
	if (r < 0)
		return r;

#if defined(CONFIG_DEBUG_FS)
	connector_debugfs_init(amdgpu_dm_connector);
#endif

	return 0;
}

static void
amdgpu_dm_mst_connector_early_unregister(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector =
		to_amdgpu_dm_connector(connector);
	struct drm_dp_mst_port *port = aconnector->mst_output_port;
	struct amdgpu_dm_connector *root = aconnector->mst_root;
	struct dc_link *dc_link = aconnector->dc_link;
	struct dc_sink *dc_sink = aconnector->dc_sink;

	drm_dp_mst_connector_early_unregister(connector, port);

	/*
	 * Release dc_sink for connector which its attached port is
	 * no longer in the mst topology
	 */
	drm_modeset_lock(&root->mst_mgr.base.lock, NULL);
	if (dc_sink) {
		if (dc_link->sink_count)
			dc_link_remove_remote_sink(dc_link, dc_sink);

		drm_dbg_dp(connector->dev,
			   "DM_MST: remove remote sink 0x%p, %d remaining\n",
			   dc_sink, dc_link->sink_count);

		dc_sink_release(dc_sink);
		aconnector->dc_sink = NULL;
		aconnector->edid = NULL;
		aconnector->dsc_aux = NULL;
		port->passthrough_aux = NULL;
	}

	aconnector->mst_status = MST_STATUS_DEFAULT;
	drm_modeset_unlock(&root->mst_mgr.base.lock);
}

static const struct drm_connector_funcs dm_dp_mst_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = dm_dp_mst_connector_destroy,
	.reset = amdgpu_dm_connector_funcs_reset,
	.atomic_duplicate_state = amdgpu_dm_connector_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = amdgpu_dm_connector_atomic_set_property,
	.atomic_get_property = amdgpu_dm_connector_atomic_get_property,
	.late_register = amdgpu_dm_mst_connector_late_register,
	.early_unregister = amdgpu_dm_mst_connector_early_unregister,
};

bool needs_dsc_aux_workaround(struct dc_link *link)
{
	if (link->dpcd_caps.branch_dev_id == DP_BRANCH_DEVICE_ID_90CC24 &&
	    (link->dpcd_caps.dpcd_rev.raw == DPCD_REV_14 || link->dpcd_caps.dpcd_rev.raw == DPCD_REV_12) &&
	    link->dpcd_caps.sink_count.bits.SINK_COUNT >= 2)
		return true;

	return false;
}

#if defined(CONFIG_DRM_AMD_DC_FP)
static bool is_synaptics_cascaded_panamera(struct dc_link *link, struct drm_dp_mst_port *port)
{
	u8 branch_vendor_data[4] = { 0 }; // Vendor data 0x50C ~ 0x50F

	if (drm_dp_dpcd_read(port->mgr->aux, DP_BRANCH_VENDOR_SPECIFIC_START, &branch_vendor_data, 4) == 4) {
		if (link->dpcd_caps.branch_dev_id == DP_BRANCH_DEVICE_ID_90CC24 &&
				IS_SYNAPTICS_CASCADED_PANAMERA(link->dpcd_caps.branch_dev_name, branch_vendor_data)) {
			DRM_INFO("Synaptics Cascaded MST hub\n");
			return true;
		}
	}

	return false;
}

static bool validate_dsc_caps_on_connector(struct amdgpu_dm_connector *aconnector)
{
	struct dc_sink *dc_sink = aconnector->dc_sink;
	struct drm_dp_mst_port *port = aconnector->mst_output_port;
	u8 dsc_caps[16] = { 0 };
	u8 dsc_branch_dec_caps_raw[3] = { 0 };	// DSC branch decoder caps 0xA0 ~ 0xA2
	u8 *dsc_branch_dec_caps = NULL;

	aconnector->dsc_aux = drm_dp_mst_dsc_aux_for_port(port);

	/*
	 * drm_dp_mst_dsc_aux_for_port() will return NULL for certain configs
	 * because it only check the dsc/fec caps of the "port variable" and not the dock
	 *
	 * This case will return NULL: DSC capabe MST dock connected to a non fec/dsc capable display
	 *
	 * Workaround: explicitly check the use case above and use the mst dock's aux as dsc_aux
	 *
	 */
	if (!aconnector->dsc_aux && !port->parent->port_parent &&
	    needs_dsc_aux_workaround(aconnector->dc_link))
		aconnector->dsc_aux = &aconnector->mst_root->dm_dp_aux.aux;

	/* synaptics cascaded MST hub case */
	if (!aconnector->dsc_aux && is_synaptics_cascaded_panamera(aconnector->dc_link, port))
		aconnector->dsc_aux = port->mgr->aux;

	if (!aconnector->dsc_aux)
		return false;

	if (drm_dp_dpcd_read(aconnector->dsc_aux, DP_DSC_SUPPORT, dsc_caps, 16) < 0)
		return false;

	if (drm_dp_dpcd_read(aconnector->dsc_aux,
			DP_DSC_BRANCH_OVERALL_THROUGHPUT_0, dsc_branch_dec_caps_raw, 3) == 3)
		dsc_branch_dec_caps = dsc_branch_dec_caps_raw;

	if (!dc_dsc_parse_dsc_dpcd(aconnector->dc_link->ctx->dc,
				  dsc_caps, dsc_branch_dec_caps,
				  &dc_sink->dsc_caps.dsc_dec_caps))
		return false;

	return true;
}
#endif

static bool retrieve_downstream_port_device(struct amdgpu_dm_connector *aconnector)
{
	union dp_downstream_port_present ds_port_present;

	if (!aconnector->dsc_aux)
		return false;

	if (drm_dp_dpcd_read(aconnector->dsc_aux, DP_DOWNSTREAMPORT_PRESENT, &ds_port_present, 1) < 0) {
		DRM_INFO("Failed to read downstream_port_present 0x05 from DFP of branch device\n");
		return false;
	}

	aconnector->mst_downstream_port_present = ds_port_present;
	DRM_INFO("Downstream port present %d, type %d\n",
			ds_port_present.fields.PORT_PRESENT, ds_port_present.fields.PORT_TYPE);

	return true;
}

static int dm_dp_mst_get_modes(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	int ret = 0;

	if (!aconnector)
		return drm_add_edid_modes(connector, NULL);

	if (!aconnector->edid) {
		struct edid *edid;

		edid = drm_dp_mst_get_edid(connector, &aconnector->mst_root->mst_mgr, aconnector->mst_output_port);

		if (!edid) {
			amdgpu_dm_set_mst_status(&aconnector->mst_status,
			MST_REMOTE_EDID, false);

			drm_connector_update_edid_property(
				&aconnector->base,
				NULL);

			DRM_DEBUG_KMS("Can't get EDID of %s. Add default remote sink.", connector->name);
			if (!aconnector->dc_sink) {
				struct dc_sink *dc_sink;
				struct dc_sink_init_data init_params = {
					.link = aconnector->dc_link,
					.sink_signal = SIGNAL_TYPE_DISPLAY_PORT_MST };

				dc_sink = dc_link_add_remote_sink(
					aconnector->dc_link,
					NULL,
					0,
					&init_params);

				if (!dc_sink) {
					DRM_ERROR("Unable to add a remote sink\n");
					return 0;
				}

				drm_dbg_dp(connector->dev,
					   "DM_MST: add remote sink 0x%p, %d remaining\n",
					   dc_sink,
					   aconnector->dc_link->sink_count);

				dc_sink->priv = aconnector;
				aconnector->dc_sink = dc_sink;
			}

			return ret;
		}

		aconnector->edid = edid;
		amdgpu_dm_set_mst_status(&aconnector->mst_status,
			MST_REMOTE_EDID, true);
	}

	if (aconnector->dc_sink && aconnector->dc_sink->sink_signal == SIGNAL_TYPE_VIRTUAL) {
		dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
	}

	if (!aconnector->dc_sink) {
		struct dc_sink *dc_sink;
		struct dc_sink_init_data init_params = {
				.link = aconnector->dc_link,
				.sink_signal = SIGNAL_TYPE_DISPLAY_PORT_MST };
		dc_sink = dc_link_add_remote_sink(
			aconnector->dc_link,
			(uint8_t *)aconnector->edid,
			(aconnector->edid->extensions + 1) * EDID_LENGTH,
			&init_params);

		if (!dc_sink) {
			DRM_ERROR("Unable to add a remote sink\n");
			return 0;
		}

		drm_dbg_dp(connector->dev,
			   "DM_MST: add remote sink 0x%p, %d remaining\n",
			   dc_sink, aconnector->dc_link->sink_count);

		dc_sink->priv = aconnector;
		/* dc_link_add_remote_sink returns a new reference */
		aconnector->dc_sink = dc_sink;

		/* when display is unplugged from mst hub, connctor will be
		 * destroyed within dm_dp_mst_connector_destroy. connector
		 * hdcp perperties, like type, undesired, desired, enabled,
		 * will be lost. So, save hdcp properties into hdcp_work within
		 * amdgpu_dm_atomic_commit_tail. if the same display is
		 * plugged back with same display index, its hdcp properties
		 * will be retrieved from hdcp_work within dm_dp_mst_get_modes
		 */
		if (aconnector->dc_sink && connector->state) {
			struct drm_device *dev = connector->dev;
			struct amdgpu_device *adev = drm_to_adev(dev);

			if (adev->dm.hdcp_workqueue) {
				struct hdcp_workqueue *hdcp_work = adev->dm.hdcp_workqueue;
				struct hdcp_workqueue *hdcp_w =
					&hdcp_work[aconnector->dc_link->link_index];

				connector->state->hdcp_content_type =
				hdcp_w->hdcp_content_type[connector->index];
				connector->state->content_protection =
				hdcp_w->content_protection[connector->index];
			}
		}

		if (aconnector->dc_sink) {
			amdgpu_dm_update_freesync_caps(
					connector, aconnector->edid);

#if defined(CONFIG_DRM_AMD_DC_FP)
			if (!validate_dsc_caps_on_connector(aconnector))
				memset(&aconnector->dc_sink->dsc_caps,
				       0, sizeof(aconnector->dc_sink->dsc_caps));
#endif

			if (!retrieve_downstream_port_device(aconnector))
				memset(&aconnector->mst_downstream_port_present,
					0, sizeof(aconnector->mst_downstream_port_present));
		}
	}

	drm_connector_update_edid_property(
					&aconnector->base, aconnector->edid);

	ret = drm_add_edid_modes(connector, aconnector->edid);

	return ret;
}

static struct drm_encoder *
dm_mst_atomic_best_encoder(struct drm_connector *connector,
			   struct drm_atomic_state *state)
{
	struct drm_connector_state *connector_state = drm_atomic_get_new_connector_state(state,
											 connector);
	struct amdgpu_device *adev = drm_to_adev(connector->dev);
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(connector_state->crtc);

	return &adev->dm.mst_encoders[acrtc->crtc_id].base;
}

static int
dm_dp_mst_detect(struct drm_connector *connector,
		 struct drm_modeset_acquire_ctx *ctx, bool force)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct amdgpu_dm_connector *master = aconnector->mst_root;
	struct drm_dp_mst_port *port = aconnector->mst_output_port;
	int connection_status;

	if (drm_connector_is_unregistered(connector))
		return connector_status_disconnected;

	connection_status = drm_dp_mst_detect_port(connector, ctx, &master->mst_mgr,
							aconnector->mst_output_port);

	if (port->pdt != DP_PEER_DEVICE_NONE && !port->dpcd_rev) {
		uint8_t dpcd_rev;
		int ret;

		ret = drm_dp_dpcd_readb(&port->aux, DP_DP13_DPCD_REV, &dpcd_rev);

		if (ret == 1) {
			port->dpcd_rev = dpcd_rev;

			/* Could be DP1.2 DP Rx case*/
			if (!dpcd_rev) {
				ret = drm_dp_dpcd_readb(&port->aux, DP_DPCD_REV, &dpcd_rev);

				if (ret == 1)
					port->dpcd_rev = dpcd_rev;
			}

			if (!dpcd_rev)
				DRM_DEBUG_KMS("Can't decide DPCD revision number!");
		}

		/*
		 * Could be legacy sink, logical port etc on DP1.2.
		 * Will get Nack under these cases when issue remote
		 * DPCD read.
		 */
		if (ret != 1)
			DRM_DEBUG_KMS("Can't access DPCD");
	} else if (port->pdt == DP_PEER_DEVICE_NONE) {
		port->dpcd_rev = 0;
	}

	/*
	 * Release dc_sink for connector which unplug event is notified by CSN msg
	 */
	if (connection_status == connector_status_disconnected && aconnector->dc_sink) {
		if (aconnector->dc_link->sink_count)
			dc_link_remove_remote_sink(aconnector->dc_link, aconnector->dc_sink);

		drm_dbg_dp(connector->dev,
			   "DM_MST: remove remote sink 0x%p, %d remaining\n",
			   aconnector->dc_link,
			   aconnector->dc_link->sink_count);

		dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
		aconnector->edid = NULL;
		aconnector->dsc_aux = NULL;
		port->passthrough_aux = NULL;

		amdgpu_dm_set_mst_status(&aconnector->mst_status,
			MST_REMOTE_EDID | MST_ALLOCATE_NEW_PAYLOAD | MST_CLEAR_ALLOCATED_PAYLOAD,
			false);
	}

	return connection_status;
}

static int dm_dp_mst_atomic_check(struct drm_connector *connector,
				  struct drm_atomic_state *state)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct drm_dp_mst_topology_mgr *mst_mgr = &aconnector->mst_root->mst_mgr;
	struct drm_dp_mst_port *mst_port = aconnector->mst_output_port;

	return drm_dp_atomic_release_time_slots(state, mst_mgr, mst_port);
}

static const struct drm_connector_helper_funcs dm_dp_mst_connector_helper_funcs = {
	.get_modes = dm_dp_mst_get_modes,
	.mode_valid = amdgpu_dm_connector_mode_valid,
	.atomic_best_encoder = dm_mst_atomic_best_encoder,
	.detect_ctx = dm_dp_mst_detect,
	.atomic_check = dm_dp_mst_atomic_check,
};

static void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs amdgpu_dm_encoder_funcs = {
	.destroy = amdgpu_dm_encoder_destroy,
};

void
dm_dp_create_fake_mst_encoders(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev_to_drm(adev);
	int i;

	for (i = 0; i < adev->dm.display_indexes_num; i++) {
		struct amdgpu_encoder *amdgpu_encoder = &adev->dm.mst_encoders[i];
		struct drm_encoder *encoder = &amdgpu_encoder->base;

		encoder->possible_crtcs = amdgpu_dm_get_encoder_crtc_mask(adev);

		drm_encoder_init(
			dev,
			&amdgpu_encoder->base,
			&amdgpu_dm_encoder_funcs,
			DRM_MODE_ENCODER_DPMST,
			NULL);

		drm_encoder_helper_add(encoder, &amdgpu_dm_encoder_helper_funcs);
	}
}

static struct drm_connector *
dm_dp_add_mst_connector(struct drm_dp_mst_topology_mgr *mgr,
			struct drm_dp_mst_port *port,
			const char *pathprop)
{
	struct amdgpu_dm_connector *master = container_of(mgr, struct amdgpu_dm_connector, mst_mgr);
	struct drm_device *dev = master->base.dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	int i;

	aconnector = kzalloc(sizeof(*aconnector), GFP_KERNEL);
	if (!aconnector)
		return NULL;

	connector = &aconnector->base;
	aconnector->mst_output_port = port;
	aconnector->mst_root = master;
	amdgpu_dm_set_mst_status(&aconnector->mst_status,
			MST_PROBE, true);

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

	for (i = 0; i < adev->dm.display_indexes_num; i++) {
		drm_connector_attach_encoder(&aconnector->base,
					     &adev->dm.mst_encoders[i].base);
	}

	connector->max_bpc_property = master->base.max_bpc_property;
	if (connector->max_bpc_property)
		drm_connector_attach_max_bpc_property(connector, 8, 16);

	connector->vrr_capable_property = master->base.vrr_capable_property;
	if (connector->vrr_capable_property)
		drm_connector_attach_vrr_capable_property(connector);

	drm_object_attach_property(
		&connector->base,
		dev->mode_config.path_property,
		0);
	drm_object_attach_property(
		&connector->base,
		dev->mode_config.tile_property,
		0);
	connector->colorspace_property = master->base.colorspace_property;
	if (connector->colorspace_property)
		drm_connector_attach_colorspace_property(connector);

	drm_connector_set_path_property(connector, pathprop);

	/*
	 * Initialize connector state before adding the connectror to drm and
	 * framebuffer lists
	 */
	amdgpu_dm_connector_funcs_reset(connector);

	drm_dp_mst_get_port_malloc(port);

	return connector;
}

void dm_handle_mst_sideband_msg_ready_event(
	struct drm_dp_mst_topology_mgr *mgr,
	enum mst_msg_ready_type msg_rdy_type)
{
	uint8_t esi[DP_PSR_ERROR_STATUS - DP_SINK_COUNT_ESI] = { 0 };
	uint8_t dret;
	bool new_irq_handled = false;
	int dpcd_addr;
	uint8_t dpcd_bytes_to_read;
	const uint8_t max_process_count = 30;
	uint8_t process_count = 0;
	u8 retry;
	struct amdgpu_dm_connector *aconnector =
			container_of(mgr, struct amdgpu_dm_connector, mst_mgr);


	const struct dc_link_status *link_status = dc_link_get_status(aconnector->dc_link);

	if (link_status->dpcd_caps->dpcd_rev.raw < 0x12) {
		dpcd_bytes_to_read = DP_LANE0_1_STATUS - DP_SINK_COUNT;
		/* DPCD 0x200 - 0x201 for downstream IRQ */
		dpcd_addr = DP_SINK_COUNT;
	} else {
		dpcd_bytes_to_read = DP_PSR_ERROR_STATUS - DP_SINK_COUNT_ESI;
		/* DPCD 0x2002 - 0x2005 for downstream IRQ */
		dpcd_addr = DP_SINK_COUNT_ESI;
	}

	mutex_lock(&aconnector->handle_mst_msg_ready);

	while (process_count < max_process_count) {
		u8 ack[DP_PSR_ERROR_STATUS - DP_SINK_COUNT_ESI] = {};

		process_count++;

		dret = drm_dp_dpcd_read(
			&aconnector->dm_dp_aux.aux,
			dpcd_addr,
			esi,
			dpcd_bytes_to_read);

		if (dret != dpcd_bytes_to_read) {
			DRM_DEBUG_KMS("DPCD read and acked number is not as expected!");
			break;
		}

		DRM_DEBUG_DRIVER("ESI %02x %02x %02x\n", esi[0], esi[1], esi[2]);

		switch (msg_rdy_type) {
		case DOWN_REP_MSG_RDY_EVENT:
			/* Only handle DOWN_REP_MSG_RDY case*/
			esi[1] &= DP_DOWN_REP_MSG_RDY;
			break;
		case UP_REQ_MSG_RDY_EVENT:
			/* Only handle UP_REQ_MSG_RDY case*/
			esi[1] &= DP_UP_REQ_MSG_RDY;
			break;
		default:
			/* Handle both cases*/
			esi[1] &= (DP_DOWN_REP_MSG_RDY | DP_UP_REQ_MSG_RDY);
			break;
		}

		if (!esi[1])
			break;

		/* handle MST irq */
		if (aconnector->mst_mgr.mst_state)
			drm_dp_mst_hpd_irq_handle_event(&aconnector->mst_mgr,
						 esi,
						 ack,
						 &new_irq_handled);

		if (new_irq_handled) {
			/* ACK at DPCD to notify down stream */
			for (retry = 0; retry < 3; retry++) {
				ssize_t wret;

				wret = drm_dp_dpcd_writeb(&aconnector->dm_dp_aux.aux,
							  dpcd_addr + 1,
							  ack[1]);
				if (wret == 1)
					break;
			}

			if (retry == 3) {
				DRM_ERROR("Failed to ack MST event.\n");
				break;
			}

			drm_dp_mst_hpd_irq_send_new_request(&aconnector->mst_mgr);

			new_irq_handled = false;
		} else {
			break;
		}
	}

	mutex_unlock(&aconnector->handle_mst_msg_ready);

	if (process_count == max_process_count)
		DRM_DEBUG_DRIVER("Loop exceeded max iterations\n");
}

static void dm_handle_mst_down_rep_msg_ready(struct drm_dp_mst_topology_mgr *mgr)
{
	dm_handle_mst_sideband_msg_ready_event(mgr, DOWN_REP_MSG_RDY_EVENT);
}

static const struct drm_dp_mst_topology_cbs dm_mst_cbs = {
	.add_connector = dm_dp_add_mst_connector,
	.poll_hpd_irq = dm_handle_mst_down_rep_msg_ready,
};

void amdgpu_dm_initialize_dp_connector(struct amdgpu_display_manager *dm,
				       struct amdgpu_dm_connector *aconnector,
				       int link_index)
{
	struct dc_link_settings max_link_enc_cap = {0};

	aconnector->dm_dp_aux.aux.name =
		kasprintf(GFP_KERNEL, "AMDGPU DM aux hw bus %d",
			  link_index);
	aconnector->dm_dp_aux.aux.transfer = dm_dp_aux_transfer;
	aconnector->dm_dp_aux.aux.drm_dev = dm->ddev;
	aconnector->dm_dp_aux.ddc_service = aconnector->dc_link->ddc;

	drm_dp_aux_init(&aconnector->dm_dp_aux.aux);
	drm_dp_cec_register_connector(&aconnector->dm_dp_aux.aux,
				      &aconnector->base);

	if (aconnector->base.connector_type == DRM_MODE_CONNECTOR_eDP)
		return;

	dc_link_dp_get_max_link_enc_cap(aconnector->dc_link, &max_link_enc_cap);
	aconnector->mst_mgr.cbs = &dm_mst_cbs;
	drm_dp_mst_topology_mgr_init(&aconnector->mst_mgr, adev_to_drm(dm->adev),
				     &aconnector->dm_dp_aux.aux, 16, 4, aconnector->connector_id);

	drm_connector_attach_dp_subconnector_property(&aconnector->base);
}

int dm_mst_get_pbn_divider(struct dc_link *link)
{
	if (!link)
		return 0;

	return dc_link_bandwidth_kbps(link,
			dc_link_get_link_cap(link)) / (8 * 1000 * 54);
}

struct dsc_mst_fairness_params {
	struct dc_crtc_timing *timing;
	struct dc_sink *sink;
	struct dc_dsc_bw_range bw_range;
	bool compression_possible;
	struct drm_dp_mst_port *port;
	enum dsc_clock_force_state clock_force_enable;
	uint32_t num_slices_h;
	uint32_t num_slices_v;
	uint32_t bpp_overwrite;
	struct amdgpu_dm_connector *aconnector;
};

#if defined(CONFIG_DRM_AMD_DC_FP)
static int kbps_to_peak_pbn(int kbps)
{
	u64 peak_kbps = kbps;

	peak_kbps *= 1006;
	peak_kbps = div_u64(peak_kbps, 1000);
	return (int) DIV64_U64_ROUND_UP(peak_kbps * 64, (54 * 8 * 1000));
}

static void set_dsc_configs_from_fairness_vars(struct dsc_mst_fairness_params *params,
		struct dsc_mst_fairness_vars *vars,
		int count,
		int k)
{
	struct drm_connector *drm_connector;
	int i;
	struct dc_dsc_config_options dsc_options = {0};

	for (i = 0; i < count; i++) {
		drm_connector = &params[i].aconnector->base;

		dc_dsc_get_default_config_option(params[i].sink->ctx->dc, &dsc_options);
		dsc_options.max_target_bpp_limit_override_x16 = drm_connector->display_info.max_dsc_bpp * 16;

		memset(&params[i].timing->dsc_cfg, 0, sizeof(params[i].timing->dsc_cfg));
		if (vars[i + k].dsc_enabled && dc_dsc_compute_config(
					params[i].sink->ctx->dc->res_pool->dscs[0],
					&params[i].sink->dsc_caps.dsc_dec_caps,
					&dsc_options,
					0,
					params[i].timing,
					dc_link_get_highest_encoding_format(params[i].aconnector->dc_link),
					&params[i].timing->dsc_cfg)) {
			params[i].timing->flags.DSC = 1;

			if (params[i].bpp_overwrite)
				params[i].timing->dsc_cfg.bits_per_pixel = params[i].bpp_overwrite;
			else
				params[i].timing->dsc_cfg.bits_per_pixel = vars[i + k].bpp_x16;

			if (params[i].num_slices_h)
				params[i].timing->dsc_cfg.num_slices_h = params[i].num_slices_h;

			if (params[i].num_slices_v)
				params[i].timing->dsc_cfg.num_slices_v = params[i].num_slices_v;
		} else {
			params[i].timing->flags.DSC = 0;
		}
		params[i].timing->dsc_cfg.mst_pbn = vars[i + k].pbn;
	}

	for (i = 0; i < count; i++) {
		if (params[i].sink) {
			if (params[i].sink->sink_signal != SIGNAL_TYPE_VIRTUAL &&
				params[i].sink->sink_signal != SIGNAL_TYPE_NONE)
				DRM_DEBUG_DRIVER("%s i=%d dispname=%s\n", __func__, i,
					params[i].sink->edid_caps.display_name);
		}

		DRM_DEBUG_DRIVER("dsc=%d bits_per_pixel=%d pbn=%d\n",
			params[i].timing->flags.DSC,
			params[i].timing->dsc_cfg.bits_per_pixel,
			vars[i + k].pbn);
	}
}

static int bpp_x16_from_pbn(struct dsc_mst_fairness_params param, int pbn)
{
	struct dc_dsc_config dsc_config;
	u64 kbps;

	struct drm_connector *drm_connector = &param.aconnector->base;
	struct dc_dsc_config_options dsc_options = {0};

	dc_dsc_get_default_config_option(param.sink->ctx->dc, &dsc_options);
	dsc_options.max_target_bpp_limit_override_x16 = drm_connector->display_info.max_dsc_bpp * 16;

	kbps = div_u64((u64)pbn * 994 * 8 * 54, 64);
	dc_dsc_compute_config(
			param.sink->ctx->dc->res_pool->dscs[0],
			&param.sink->dsc_caps.dsc_dec_caps,
			&dsc_options,
			(int) kbps, param.timing,
			dc_link_get_highest_encoding_format(param.aconnector->dc_link),
			&dsc_config);

	return dsc_config.bits_per_pixel;
}

static int increase_dsc_bpp(struct drm_atomic_state *state,
			    struct drm_dp_mst_topology_state *mst_state,
			    struct dc_link *dc_link,
			    struct dsc_mst_fairness_params *params,
			    struct dsc_mst_fairness_vars *vars,
			    int count,
			    int k)
{
	int i;
	bool bpp_increased[MAX_PIPES];
	int initial_slack[MAX_PIPES];
	int min_initial_slack;
	int next_index;
	int remaining_to_increase = 0;
	int link_timeslots_used;
	int fair_pbn_alloc;
	int ret = 0;

	for (i = 0; i < count; i++) {
		if (vars[i + k].dsc_enabled) {
			initial_slack[i] =
			kbps_to_peak_pbn(params[i].bw_range.max_kbps) - vars[i + k].pbn;
			bpp_increased[i] = false;
			remaining_to_increase += 1;
		} else {
			initial_slack[i] = 0;
			bpp_increased[i] = true;
		}
	}

	while (remaining_to_increase) {
		next_index = -1;
		min_initial_slack = -1;
		for (i = 0; i < count; i++) {
			if (!bpp_increased[i]) {
				if (min_initial_slack == -1 || min_initial_slack > initial_slack[i]) {
					min_initial_slack = initial_slack[i];
					next_index = i;
				}
			}
		}

		if (next_index == -1)
			break;

		link_timeslots_used = 0;

		for (i = 0; i < count; i++)
			link_timeslots_used += DIV_ROUND_UP(vars[i + k].pbn, dfixed_trunc(mst_state->pbn_div));

		fair_pbn_alloc =
			(63 - link_timeslots_used) / remaining_to_increase * dfixed_trunc(mst_state->pbn_div);

		if (initial_slack[next_index] > fair_pbn_alloc) {
			vars[next_index].pbn += fair_pbn_alloc;
			ret = drm_dp_atomic_find_time_slots(state,
							    params[next_index].port->mgr,
							    params[next_index].port,
							    vars[next_index].pbn);
			if (ret < 0)
				return ret;

			ret = drm_dp_mst_atomic_check(state);
			if (ret == 0) {
				vars[next_index].bpp_x16 = bpp_x16_from_pbn(params[next_index], vars[next_index].pbn);
			} else {
				vars[next_index].pbn -= fair_pbn_alloc;
				ret = drm_dp_atomic_find_time_slots(state,
								    params[next_index].port->mgr,
								    params[next_index].port,
								    vars[next_index].pbn);
				if (ret < 0)
					return ret;
			}
		} else {
			vars[next_index].pbn += initial_slack[next_index];
			ret = drm_dp_atomic_find_time_slots(state,
							    params[next_index].port->mgr,
							    params[next_index].port,
							    vars[next_index].pbn);
			if (ret < 0)
				return ret;

			ret = drm_dp_mst_atomic_check(state);
			if (ret == 0) {
				vars[next_index].bpp_x16 = params[next_index].bw_range.max_target_bpp_x16;
			} else {
				vars[next_index].pbn -= initial_slack[next_index];
				ret = drm_dp_atomic_find_time_slots(state,
								    params[next_index].port->mgr,
								    params[next_index].port,
								    vars[next_index].pbn);
				if (ret < 0)
					return ret;
			}
		}

		bpp_increased[next_index] = true;
		remaining_to_increase--;
	}
	return 0;
}

static int try_disable_dsc(struct drm_atomic_state *state,
			   struct dc_link *dc_link,
			   struct dsc_mst_fairness_params *params,
			   struct dsc_mst_fairness_vars *vars,
			   int count,
			   int k)
{
	int i;
	bool tried[MAX_PIPES];
	int kbps_increase[MAX_PIPES];
	int max_kbps_increase;
	int next_index;
	int remaining_to_try = 0;
	int ret;

	for (i = 0; i < count; i++) {
		if (vars[i + k].dsc_enabled
				&& vars[i + k].bpp_x16 == params[i].bw_range.max_target_bpp_x16
				&& params[i].clock_force_enable == DSC_CLK_FORCE_DEFAULT) {
			kbps_increase[i] = params[i].bw_range.stream_kbps - params[i].bw_range.max_kbps;
			tried[i] = false;
			remaining_to_try += 1;
		} else {
			kbps_increase[i] = 0;
			tried[i] = true;
		}
	}

	while (remaining_to_try) {
		next_index = -1;
		max_kbps_increase = -1;
		for (i = 0; i < count; i++) {
			if (!tried[i]) {
				if (max_kbps_increase == -1 || max_kbps_increase < kbps_increase[i]) {
					max_kbps_increase = kbps_increase[i];
					next_index = i;
				}
			}
		}

		if (next_index == -1)
			break;

		vars[next_index].pbn = kbps_to_peak_pbn(params[next_index].bw_range.stream_kbps);
		ret = drm_dp_atomic_find_time_slots(state,
						    params[next_index].port->mgr,
						    params[next_index].port,
						    vars[next_index].pbn);
		if (ret < 0)
			return ret;

		ret = drm_dp_mst_atomic_check(state);
		if (ret == 0) {
			vars[next_index].dsc_enabled = false;
			vars[next_index].bpp_x16 = 0;
		} else {
			vars[next_index].pbn = kbps_to_peak_pbn(
				params[next_index].bw_range.max_kbps);
			ret = drm_dp_atomic_find_time_slots(state,
							    params[next_index].port->mgr,
							    params[next_index].port,
							    vars[next_index].pbn);
			if (ret < 0)
				return ret;
		}

		tried[next_index] = true;
		remaining_to_try--;
	}
	return 0;
}

static int compute_mst_dsc_configs_for_link(struct drm_atomic_state *state,
					    struct dc_state *dc_state,
					    struct dc_link *dc_link,
					    struct dsc_mst_fairness_vars *vars,
					    struct drm_dp_mst_topology_mgr *mgr,
					    int *link_vars_start_index)
{
	struct dc_stream_state *stream;
	struct dsc_mst_fairness_params params[MAX_PIPES];
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_topology_state *mst_state = drm_atomic_get_mst_topology_state(state, mgr);
	int count = 0;
	int i, k, ret;
	bool debugfs_overwrite = false;

	memset(params, 0, sizeof(params));

	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	/* Set up params */
	for (i = 0; i < dc_state->stream_count; i++) {
		struct dc_dsc_policy dsc_policy = {0};

		stream = dc_state->streams[i];

		if (stream->link != dc_link)
			continue;

		aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;
		if (!aconnector)
			continue;

		if (!aconnector->mst_output_port)
			continue;

		stream->timing.flags.DSC = 0;

		params[count].timing = &stream->timing;
		params[count].sink = stream->sink;
		params[count].aconnector = aconnector;
		params[count].port = aconnector->mst_output_port;
		params[count].clock_force_enable = aconnector->dsc_settings.dsc_force_enable;
		if (params[count].clock_force_enable == DSC_CLK_FORCE_ENABLE)
			debugfs_overwrite = true;
		params[count].num_slices_h = aconnector->dsc_settings.dsc_num_slices_h;
		params[count].num_slices_v = aconnector->dsc_settings.dsc_num_slices_v;
		params[count].bpp_overwrite = aconnector->dsc_settings.dsc_bits_per_pixel;
		params[count].compression_possible = stream->sink->dsc_caps.dsc_dec_caps.is_dsc_supported;
		dc_dsc_get_policy_for_timing(params[count].timing, 0, &dsc_policy);
		if (!dc_dsc_compute_bandwidth_range(
				stream->sink->ctx->dc->res_pool->dscs[0],
				stream->sink->ctx->dc->debug.dsc_min_slice_height_override,
				dsc_policy.min_target_bpp * 16,
				dsc_policy.max_target_bpp * 16,
				&stream->sink->dsc_caps.dsc_dec_caps,
				&stream->timing,
				dc_link_get_highest_encoding_format(dc_link),
				&params[count].bw_range))
			params[count].bw_range.stream_kbps = dc_bandwidth_in_kbps_from_timing(&stream->timing,
					dc_link_get_highest_encoding_format(dc_link));

		count++;
	}

	if (count == 0) {
		ASSERT(0);
		return 0;
	}

	/* k is start index of vars for current phy link used by mst hub */
	k = *link_vars_start_index;
	/* set vars start index for next mst hub phy link */
	*link_vars_start_index += count;

	/* Try no compression */
	for (i = 0; i < count; i++) {
		vars[i + k].aconnector = params[i].aconnector;
		vars[i + k].pbn = kbps_to_peak_pbn(params[i].bw_range.stream_kbps);
		vars[i + k].dsc_enabled = false;
		vars[i + k].bpp_x16 = 0;
		ret = drm_dp_atomic_find_time_slots(state, params[i].port->mgr, params[i].port,
						    vars[i + k].pbn);
		if (ret < 0)
			return ret;
	}
	ret = drm_dp_mst_atomic_check(state);
	if (ret == 0 && !debugfs_overwrite) {
		set_dsc_configs_from_fairness_vars(params, vars, count, k);
		return 0;
	} else if (ret != -ENOSPC) {
		return ret;
	}

	/* Try max compression */
	for (i = 0; i < count; i++) {
		if (params[i].compression_possible && params[i].clock_force_enable != DSC_CLK_FORCE_DISABLE) {
			vars[i + k].pbn = kbps_to_peak_pbn(params[i].bw_range.min_kbps);
			vars[i + k].dsc_enabled = true;
			vars[i + k].bpp_x16 = params[i].bw_range.min_target_bpp_x16;
			ret = drm_dp_atomic_find_time_slots(state, params[i].port->mgr,
							    params[i].port, vars[i + k].pbn);
			if (ret < 0)
				return ret;
		} else {
			vars[i + k].pbn = kbps_to_peak_pbn(params[i].bw_range.stream_kbps);
			vars[i + k].dsc_enabled = false;
			vars[i + k].bpp_x16 = 0;
			ret = drm_dp_atomic_find_time_slots(state, params[i].port->mgr,
							    params[i].port, vars[i + k].pbn);
			if (ret < 0)
				return ret;
		}
	}
	ret = drm_dp_mst_atomic_check(state);
	if (ret != 0)
		return ret;

	/* Optimize degree of compression */
	ret = increase_dsc_bpp(state, mst_state, dc_link, params, vars, count, k);
	if (ret < 0)
		return ret;

	ret = try_disable_dsc(state, dc_link, params, vars, count, k);
	if (ret < 0)
		return ret;

	set_dsc_configs_from_fairness_vars(params, vars, count, k);

	return 0;
}

static bool is_dsc_need_re_compute(
	struct drm_atomic_state *state,
	struct dc_state *dc_state,
	struct dc_link *dc_link)
{
	int i, j;
	bool is_dsc_need_re_compute = false;
	struct amdgpu_dm_connector *stream_on_link[MAX_PIPES];
	int new_stream_on_link_num = 0;
	struct amdgpu_dm_connector *aconnector;
	struct dc_stream_state *stream;
	const struct dc *dc = dc_link->dc;

	/* only check phy used by dsc mst branch */
	if (dc_link->type != dc_connection_mst_branch)
		return false;

	/* add a check for older MST DSC with no virtual DPCDs */
	if (needs_dsc_aux_workaround(dc_link)  &&
		(!(dc_link->dpcd_caps.dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_SUPPORT ||
		dc_link->dpcd_caps.dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_PASSTHROUGH_SUPPORT)))
		return false;

	for (i = 0; i < MAX_PIPES; i++)
		stream_on_link[i] = NULL;

	/* check if there is mode change in new request */
	for (i = 0; i < dc_state->stream_count; i++) {
		struct drm_crtc_state *new_crtc_state;
		struct drm_connector_state *new_conn_state;

		stream = dc_state->streams[i];
		if (!stream)
			continue;

		/* check if stream using the same link for mst */
		if (stream->link != dc_link)
			continue;

		aconnector = (struct amdgpu_dm_connector *) stream->dm_stream_context;
		if (!aconnector || !aconnector->dsc_aux)
			continue;

		stream_on_link[new_stream_on_link_num] = aconnector;
		new_stream_on_link_num++;

		new_conn_state = drm_atomic_get_new_connector_state(state, &aconnector->base);
		if (!new_conn_state)
			continue;

		if (IS_ERR(new_conn_state))
			continue;

		if (!new_conn_state->crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
		if (!new_crtc_state)
			continue;

		if (IS_ERR(new_crtc_state))
			continue;

		if (new_crtc_state->enable && new_crtc_state->active) {
			if (new_crtc_state->mode_changed || new_crtc_state->active_changed ||
				new_crtc_state->connectors_changed)
				return true;
		}
	}

	if (new_stream_on_link_num == 0)
		return false;

	/* check current_state if there stream on link but it is not in
	 * new request state
	 */
	for (i = 0; i < dc->current_state->stream_count; i++) {
		stream = dc->current_state->streams[i];
		/* only check stream on the mst hub */
		if (stream->link != dc_link)
			continue;

		aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;
		if (!aconnector)
			continue;

		for (j = 0; j < new_stream_on_link_num; j++) {
			if (stream_on_link[j]) {
				if (aconnector == stream_on_link[j])
					break;
			}
		}

		if (j == new_stream_on_link_num) {
			/* not in new state */
			is_dsc_need_re_compute = true;
			break;
		}
	}

	return is_dsc_need_re_compute;
}

int compute_mst_dsc_configs_for_state(struct drm_atomic_state *state,
				      struct dc_state *dc_state,
				      struct dsc_mst_fairness_vars *vars)
{
	int i, j;
	struct dc_stream_state *stream;
	bool computed_streams[MAX_PIPES];
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_topology_mgr *mst_mgr;
	struct resource_pool *res_pool;
	int link_vars_start_index = 0;
	int ret = 0;

	for (i = 0; i < dc_state->stream_count; i++)
		computed_streams[i] = false;

	for (i = 0; i < dc_state->stream_count; i++) {
		stream = dc_state->streams[i];
		res_pool = stream->ctx->dc->res_pool;

		if (stream->signal != SIGNAL_TYPE_DISPLAY_PORT_MST)
			continue;

		aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;

		if (!aconnector || !aconnector->dc_sink || !aconnector->mst_output_port)
			continue;

		if (!aconnector->dc_sink->dsc_caps.dsc_dec_caps.is_dsc_supported)
			continue;

		if (computed_streams[i])
			continue;

		if (res_pool->funcs->remove_stream_from_ctx &&
		    res_pool->funcs->remove_stream_from_ctx(stream->ctx->dc, dc_state, stream) != DC_OK)
			return -EINVAL;

		if (!is_dsc_need_re_compute(state, dc_state, stream->link))
			continue;

		mst_mgr = aconnector->mst_output_port->mgr;
		ret = compute_mst_dsc_configs_for_link(state, dc_state, stream->link, vars, mst_mgr,
						       &link_vars_start_index);
		if (ret != 0)
			return ret;

		for (j = 0; j < dc_state->stream_count; j++) {
			if (dc_state->streams[j]->link == stream->link)
				computed_streams[j] = true;
		}
	}

	for (i = 0; i < dc_state->stream_count; i++) {
		stream = dc_state->streams[i];

		if (stream->timing.flags.DSC == 1)
			if (dc_stream_add_dsc_to_resource(stream->ctx->dc, dc_state, stream) != DC_OK)
				return -EINVAL;
	}

	return ret;
}

static int pre_compute_mst_dsc_configs_for_state(struct drm_atomic_state *state,
						 struct dc_state *dc_state,
						 struct dsc_mst_fairness_vars *vars)
{
	int i, j;
	struct dc_stream_state *stream;
	bool computed_streams[MAX_PIPES];
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_topology_mgr *mst_mgr;
	int link_vars_start_index = 0;
	int ret = 0;

	for (i = 0; i < dc_state->stream_count; i++)
		computed_streams[i] = false;

	for (i = 0; i < dc_state->stream_count; i++) {
		stream = dc_state->streams[i];

		if (stream->signal != SIGNAL_TYPE_DISPLAY_PORT_MST)
			continue;

		aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;

		if (!aconnector || !aconnector->dc_sink || !aconnector->mst_output_port)
			continue;

		if (!aconnector->dc_sink->dsc_caps.dsc_dec_caps.is_dsc_supported)
			continue;

		if (computed_streams[i])
			continue;

		if (!is_dsc_need_re_compute(state, dc_state, stream->link))
			continue;

		mst_mgr = aconnector->mst_output_port->mgr;
		ret = compute_mst_dsc_configs_for_link(state, dc_state, stream->link, vars, mst_mgr,
						       &link_vars_start_index);
		if (ret != 0)
			return ret;

		for (j = 0; j < dc_state->stream_count; j++) {
			if (dc_state->streams[j]->link == stream->link)
				computed_streams[j] = true;
		}
	}

	return ret;
}

static int find_crtc_index_in_state_by_stream(struct drm_atomic_state *state,
					      struct dc_stream_state *stream)
{
	int i;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_state, *old_state;

	for_each_oldnew_crtc_in_state(state, crtc, old_state, new_state, i) {
		struct dm_crtc_state *dm_state = to_dm_crtc_state(new_state);

		if (dm_state->stream == stream)
			return i;
	}
	return -1;
}

static bool is_link_to_dschub(struct dc_link *dc_link)
{
	union dpcd_dsc_basic_capabilities *dsc_caps =
			&dc_link->dpcd_caps.dsc_caps.dsc_basic_caps;

	/* only check phy used by dsc mst branch */
	if (dc_link->type != dc_connection_mst_branch)
		return false;

	if (!(dsc_caps->fields.dsc_support.DSC_SUPPORT ||
	      dsc_caps->fields.dsc_support.DSC_PASSTHROUGH_SUPPORT))
		return false;
	return true;
}

static bool is_dsc_precompute_needed(struct drm_atomic_state *state)
{
	int i;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	bool ret = false;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct dm_crtc_state *dm_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (!amdgpu_dm_find_first_crtc_matching_connector(state, crtc)) {
			ret =  false;
			break;
		}
		if (dm_crtc_state->stream && dm_crtc_state->stream->link)
			if (is_link_to_dschub(dm_crtc_state->stream->link))
				ret = true;
	}
	return ret;
}

int pre_validate_dsc(struct drm_atomic_state *state,
		     struct dm_atomic_state **dm_state_ptr,
		     struct dsc_mst_fairness_vars *vars)
{
	int i;
	struct dm_atomic_state *dm_state;
	struct dc_state *local_dc_state = NULL;
	int ret = 0;

	if (!is_dsc_precompute_needed(state)) {
		DRM_INFO_ONCE("DSC precompute is not needed.\n");
		return 0;
	}
	ret = dm_atomic_get_state(state, dm_state_ptr);
	if (ret != 0) {
		DRM_INFO_ONCE("dm_atomic_get_state() failed\n");
		return ret;
	}
	dm_state = *dm_state_ptr;

	/*
	 * create local vailable for dc_state. copy content of streams of dm_state->context
	 * to local variable. make sure stream pointer of local variable not the same as stream
	 * from dm_state->context.
	 */

	local_dc_state = vmalloc(sizeof(struct dc_state));
	if (!local_dc_state)
		return -ENOMEM;
	memcpy(local_dc_state, dm_state->context, sizeof(struct dc_state));

	for (i = 0; i < local_dc_state->stream_count; i++) {
		struct dc_stream_state *stream = dm_state->context->streams[i];
		int ind = find_crtc_index_in_state_by_stream(state, stream);

		if (ind >= 0) {
			struct drm_connector *connector;
			struct amdgpu_dm_connector *aconnector;
			struct drm_connector_state *drm_new_conn_state;
			struct dm_connector_state *dm_new_conn_state;
			struct dm_crtc_state *dm_old_crtc_state;

			connector =
				amdgpu_dm_find_first_crtc_matching_connector(state,
									     state->crtcs[ind].ptr);
			aconnector = to_amdgpu_dm_connector(connector);
			drm_new_conn_state =
				drm_atomic_get_new_connector_state(state,
								   &aconnector->base);
			dm_new_conn_state = to_dm_connector_state(drm_new_conn_state);
			dm_old_crtc_state = to_dm_crtc_state(state->crtcs[ind].old_state);

			local_dc_state->streams[i] =
				create_validate_stream_for_sink(aconnector,
								&state->crtcs[ind].new_state->mode,
								dm_new_conn_state,
								dm_old_crtc_state->stream);
			if (local_dc_state->streams[i] == NULL) {
				ret = -EINVAL;
				break;
			}
		}
	}

	if (ret != 0)
		goto clean_exit;

	ret = pre_compute_mst_dsc_configs_for_state(state, local_dc_state, vars);
	if (ret != 0) {
		DRM_INFO_ONCE("pre_compute_mst_dsc_configs_for_state() failed\n");
		ret = -EINVAL;
		goto clean_exit;
	}

	/*
	 * compare local_streams -> timing  with dm_state->context,
	 * if the same set crtc_state->mode-change = 0;
	 */
	for (i = 0; i < local_dc_state->stream_count; i++) {
		struct dc_stream_state *stream = dm_state->context->streams[i];

		if (local_dc_state->streams[i] &&
		    dc_is_timing_changed(stream, local_dc_state->streams[i])) {
			DRM_INFO_ONCE("crtc[%d] needs mode_changed\n", i);
		} else {
			int ind = find_crtc_index_in_state_by_stream(state, stream);

			if (ind >= 0)
				state->crtcs[ind].new_state->mode_changed = 0;
		}
	}
clean_exit:
	for (i = 0; i < local_dc_state->stream_count; i++) {
		struct dc_stream_state *stream = dm_state->context->streams[i];

		if (local_dc_state->streams[i] != stream)
			dc_stream_release(local_dc_state->streams[i]);
	}

	vfree(local_dc_state);

	return ret;
}

static unsigned int kbps_from_pbn(unsigned int pbn)
{
	unsigned int kbps = pbn;

	kbps *= (1000000 / PEAK_FACTOR_X1000);
	kbps *= 8;
	kbps *= 54;
	kbps /= 64;

	return kbps;
}

static bool is_dsc_common_config_possible(struct dc_stream_state *stream,
					  struct dc_dsc_bw_range *bw_range)
{
	struct dc_dsc_policy dsc_policy = {0};

	dc_dsc_get_policy_for_timing(&stream->timing, 0, &dsc_policy);
	dc_dsc_compute_bandwidth_range(stream->sink->ctx->dc->res_pool->dscs[0],
				       stream->sink->ctx->dc->debug.dsc_min_slice_height_override,
				       dsc_policy.min_target_bpp * 16,
				       dsc_policy.max_target_bpp * 16,
				       &stream->sink->dsc_caps.dsc_dec_caps,
				       &stream->timing, dc_link_get_highest_encoding_format(stream->link), bw_range);

	return bw_range->max_target_bpp_x16 && bw_range->min_target_bpp_x16;
}
#endif

#if defined(CONFIG_DRM_AMD_DC_FP)
static bool dp_get_link_current_set_bw(struct drm_dp_aux *aux, uint32_t *cur_link_bw)
{
	uint32_t total_data_bw_efficiency_x10000 = 0;
	uint32_t link_rate_per_lane_kbps = 0;
	enum dc_link_rate link_rate;
	union lane_count_set lane_count;
	u8 dp_link_encoding;
	u8 link_bw_set = 0;

	*cur_link_bw = 0;

	if (drm_dp_dpcd_read(aux, DP_MAIN_LINK_CHANNEL_CODING_SET, &dp_link_encoding, 1) != 1 ||
		drm_dp_dpcd_read(aux, DP_LANE_COUNT_SET, &lane_count.raw, 1) != 1 ||
		drm_dp_dpcd_read(aux, DP_LINK_BW_SET, &link_bw_set, 1) != 1)
		return false;

	switch (dp_link_encoding) {
	case DP_8b_10b_ENCODING:
		link_rate = link_bw_set;
		link_rate_per_lane_kbps = link_rate * LINK_RATE_REF_FREQ_IN_KHZ * BITS_PER_DP_BYTE;
		total_data_bw_efficiency_x10000 = DATA_EFFICIENCY_8b_10b_x10000;
		total_data_bw_efficiency_x10000 /= 100;
		total_data_bw_efficiency_x10000 *= DATA_EFFICIENCY_8b_10b_FEC_EFFICIENCY_x100;
		break;
	case DP_128b_132b_ENCODING:
		switch (link_bw_set) {
		case DP_LINK_BW_10:
			link_rate = LINK_RATE_UHBR10;
			break;
		case DP_LINK_BW_13_5:
			link_rate = LINK_RATE_UHBR13_5;
			break;
		case DP_LINK_BW_20:
			link_rate = LINK_RATE_UHBR20;
			break;
		default:
			return false;
		}

		link_rate_per_lane_kbps = link_rate * 10000;
		total_data_bw_efficiency_x10000 = DATA_EFFICIENCY_128b_132b_x10000;
		break;
	default:
		return false;
	}

	*cur_link_bw = link_rate_per_lane_kbps * lane_count.bits.LANE_COUNT_SET / 10000 * total_data_bw_efficiency_x10000;
	return true;
}
#endif

enum dc_status dm_dp_mst_is_port_support_mode(
	struct amdgpu_dm_connector *aconnector,
	struct dc_stream_state *stream)
{
#if defined(CONFIG_DRM_AMD_DC_FP)
	int branch_max_throughput_mps = 0;
	struct dc_link_settings cur_link_settings;
	uint32_t end_to_end_bw_in_kbps = 0;
	uint32_t root_link_bw_in_kbps = 0;
	uint32_t virtual_channel_bw_in_kbps = 0;
	struct dc_dsc_bw_range bw_range = {0};
	struct dc_dsc_config_options dsc_options = {0};
	uint32_t stream_kbps;

	/* DSC unnecessary case
	 * Check if timing could be supported within end-to-end BW
	 */
	stream_kbps =
		dc_bandwidth_in_kbps_from_timing(&stream->timing,
			dc_link_get_highest_encoding_format(stream->link));
	cur_link_settings = stream->link->verified_link_cap;
	root_link_bw_in_kbps = dc_link_bandwidth_kbps(aconnector->dc_link, &cur_link_settings);
	virtual_channel_bw_in_kbps = kbps_from_pbn(aconnector->mst_output_port->full_pbn);

	/* pick the end to end bw bottleneck */
	end_to_end_bw_in_kbps = min(root_link_bw_in_kbps, virtual_channel_bw_in_kbps);

	if (stream_kbps <= end_to_end_bw_in_kbps) {
		DRM_DEBUG_DRIVER("No DSC needed. End-to-end bw sufficient.");
		return DC_OK;
	}

	/*DSC necessary case*/
	if (!aconnector->dsc_aux)
		return DC_FAIL_BANDWIDTH_VALIDATE;

	if (is_dsc_common_config_possible(stream, &bw_range)) {

		/*capable of dsc passthough. dsc bitstream along the entire path*/
		if (aconnector->mst_output_port->passthrough_aux) {
			if (bw_range.min_kbps > end_to_end_bw_in_kbps) {
				DRM_DEBUG_DRIVER("DSC passthrough. Max dsc compression can't fit into end-to-end bw\n");
				return DC_FAIL_BANDWIDTH_VALIDATE;
			}
		} else {
			/*dsc bitstream decoded at the dp last link*/
			struct drm_dp_mst_port *immediate_upstream_port = NULL;
			uint32_t end_link_bw = 0;

			/*Get last DP link BW capability*/
			if (dp_get_link_current_set_bw(&aconnector->mst_output_port->aux, &end_link_bw)) {
				if (stream_kbps > end_link_bw) {
					DRM_DEBUG_DRIVER("DSC decode at last link. Mode required bw can't fit into available bw\n");
					return DC_FAIL_BANDWIDTH_VALIDATE;
				}
			}

			/*Get virtual channel bandwidth between source and the link before the last link*/
			if (aconnector->mst_output_port->parent->port_parent)
				immediate_upstream_port = aconnector->mst_output_port->parent->port_parent;

			if (immediate_upstream_port) {
				virtual_channel_bw_in_kbps = kbps_from_pbn(immediate_upstream_port->full_pbn);
				virtual_channel_bw_in_kbps = min(root_link_bw_in_kbps, virtual_channel_bw_in_kbps);
				if (bw_range.min_kbps > virtual_channel_bw_in_kbps) {
					DRM_DEBUG_DRIVER("DSC decode at last link. Max dsc compression can't fit into MST available bw\n");
					return DC_FAIL_BANDWIDTH_VALIDATE;
				}
			}
		}

		/*Confirm if we can obtain dsc config*/
		dc_dsc_get_default_config_option(stream->link->dc, &dsc_options);
		dsc_options.max_target_bpp_limit_override_x16 = aconnector->base.display_info.max_dsc_bpp * 16;
		if (dc_dsc_compute_config(stream->sink->ctx->dc->res_pool->dscs[0],
				&stream->sink->dsc_caps.dsc_dec_caps,
				&dsc_options,
				end_to_end_bw_in_kbps,
				&stream->timing,
				dc_link_get_highest_encoding_format(stream->link),
				&stream->timing.dsc_cfg)) {
			stream->timing.flags.DSC = 1;
			DRM_DEBUG_DRIVER("Require dsc and dsc config found\n");
		} else {
			DRM_DEBUG_DRIVER("Require dsc but can't find appropriate dsc config\n");
			return DC_FAIL_BANDWIDTH_VALIDATE;
		}

		/* check is mst dsc output bandwidth branch_overall_throughput_0_mps */
		switch (stream->timing.pixel_encoding) {
		case PIXEL_ENCODING_RGB:
		case PIXEL_ENCODING_YCBCR444:
			branch_max_throughput_mps =
				aconnector->dc_sink->dsc_caps.dsc_dec_caps.branch_overall_throughput_0_mps;
			break;
		case PIXEL_ENCODING_YCBCR422:
		case PIXEL_ENCODING_YCBCR420:
			branch_max_throughput_mps =
				aconnector->dc_sink->dsc_caps.dsc_dec_caps.branch_overall_throughput_1_mps;
			break;
		default:
			break;
		}

		if (branch_max_throughput_mps != 0 &&
			((stream->timing.pix_clk_100hz / 10) >  branch_max_throughput_mps * 1000)) {
			DRM_DEBUG_DRIVER("DSC is required but max throughput mps fails");
			return DC_FAIL_BANDWIDTH_VALIDATE;
		}
	} else {
		DRM_DEBUG_DRIVER("DSC is required but can't find common dsc config.");
		return DC_FAIL_BANDWIDTH_VALIDATE;
	}
#endif
	return DC_OK;
}
