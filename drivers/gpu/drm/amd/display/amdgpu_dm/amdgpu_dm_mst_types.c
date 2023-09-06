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

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_mst_helper.h>
#include <drm/drm_dp_helper.h>
#include "dm_services.h"
#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_mst_types.h"

#include "dc.h"
#include "dm_helpers.h"

#include "dc_link_ddc.h"
#include "ddc_service_types.h"
#include "dpcd_defs.h"

#include "i2caux_interface.h"
#include "dmub_cmd.h"
#if defined(CONFIG_DEBUG_FS)
#include "amdgpu_dm_debugfs.h"
#endif

#if defined(CONFIG_DRM_AMD_DC_DCN)
#include "dc/dcn20/dcn20_resource.h"
#endif

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
	drm_dp_mst_put_port_malloc(aconnector->port);
	kfree(aconnector);
}

static int
amdgpu_dm_mst_connector_late_register(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
		to_amdgpu_dm_connector(connector);
	int r;

	r = drm_dp_mst_connector_late_register(connector,
					       amdgpu_dm_connector->port);
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
	struct amdgpu_dm_connector *amdgpu_dm_connector =
		to_amdgpu_dm_connector(connector);
	struct drm_dp_mst_port *port = amdgpu_dm_connector->port;

	drm_dp_mst_connector_early_unregister(connector, port);
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

#if defined(CONFIG_DRM_AMD_DC_DCN)
static bool needs_dsc_aux_workaround(struct dc_link *link)
{
	if (link->dpcd_caps.branch_dev_id == DP_BRANCH_DEVICE_ID_90CC24 &&
	    (link->dpcd_caps.dpcd_rev.raw == DPCD_REV_14 || link->dpcd_caps.dpcd_rev.raw == DPCD_REV_12) &&
	    link->dpcd_caps.sink_count.bits.SINK_COUNT >= 2)
		return true;

	return false;
}

static bool validate_dsc_caps_on_connector(struct amdgpu_dm_connector *aconnector)
{
	struct dc_sink *dc_sink = aconnector->dc_sink;
	struct drm_dp_mst_port *port = aconnector->port;
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
		aconnector->dsc_aux = &aconnector->mst_port->dm_dp_aux.aux;

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

static int dm_dp_mst_get_modes(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	int ret = 0;

	if (!aconnector)
		return drm_add_edid_modes(connector, NULL);

	if (!aconnector->edid) {
		struct edid *edid;
		edid = drm_dp_mst_get_edid(connector, &aconnector->mst_port->mst_mgr, aconnector->port);

		if (!edid) {
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

				dc_sink->priv = aconnector;
				aconnector->dc_sink = dc_sink;
			}

			return ret;
		}

		aconnector->edid = edid;
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

		dc_sink->priv = aconnector;
		/* dc_link_add_remote_sink returns a new reference */
		aconnector->dc_sink = dc_sink;

		if (aconnector->dc_sink) {
			amdgpu_dm_update_freesync_caps(
					connector, aconnector->edid);

#if defined(CONFIG_DRM_AMD_DC_DCN)
			if (!validate_dsc_caps_on_connector(aconnector))
				memset(&aconnector->dc_sink->dsc_caps,
				       0, sizeof(aconnector->dc_sink->dsc_caps));
#endif
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
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(connector_state->crtc);

	return &adev->dm.mst_encoders[acrtc->crtc_id].base;
}

static int
dm_dp_mst_detect(struct drm_connector *connector,
		 struct drm_modeset_acquire_ctx *ctx, bool force)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct amdgpu_dm_connector *master = aconnector->mst_port;

	if (drm_connector_is_unregistered(connector))
		return connector_status_disconnected;

	return drm_dp_mst_detect_port(connector, ctx, &master->mst_mgr,
				      aconnector->port);
}

static int dm_dp_mst_atomic_check(struct drm_connector *connector,
				struct drm_atomic_state *state)
{
	struct drm_connector_state *new_conn_state =
			drm_atomic_get_new_connector_state(state, connector);
	struct drm_connector_state *old_conn_state =
			drm_atomic_get_old_connector_state(state, connector);
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct drm_crtc_state *new_crtc_state;
	struct drm_dp_mst_topology_mgr *mst_mgr;
	struct drm_dp_mst_port *mst_port;

	mst_port = aconnector->port;
	mst_mgr = &aconnector->mst_port->mst_mgr;

	if (!old_conn_state->crtc)
		return 0;

	if (new_conn_state->crtc) {
		new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
		if (!new_crtc_state ||
		    !drm_atomic_crtc_needs_modeset(new_crtc_state) ||
		    new_crtc_state->enable)
			return 0;
		}

	return drm_dp_atomic_release_vcpi_slots(state,
						mst_mgr,
						mst_port);
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
	kfree(encoder);
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

	drm_connector_set_path_property(connector, pathprop);

	/*
	 * Initialize connector state before adding the connectror to drm and
	 * framebuffer lists
	 */
	amdgpu_dm_connector_funcs_reset(connector);

	drm_dp_mst_get_port_malloc(port);

	return connector;
}

static const struct drm_dp_mst_topology_cbs dm_mst_cbs = {
	.add_connector = dm_dp_add_mst_connector,
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
	drm_dp_mst_topology_mgr_init(
		&aconnector->mst_mgr,
		adev_to_drm(dm->adev),
		&aconnector->dm_dp_aux.aux,
		16,
		4,
		max_link_enc_cap.lane_count,
		drm_dp_bw_code_to_link_rate(max_link_enc_cap.link_rate),
		aconnector->connector_id);

	drm_connector_attach_dp_subconnector_property(&aconnector->base);
}

int dm_mst_get_pbn_divider(struct dc_link *link)
{
	if (!link)
		return 0;

	return dc_link_bandwidth_kbps(link,
			dc_link_get_link_cap(link)) / (8 * 1000 * 54);
}

#if defined(CONFIG_DRM_AMD_DC_DCN)

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

static int kbps_to_peak_pbn(int kbps)
{
	u64 peak_kbps = kbps;

	peak_kbps *= 1006;
	peak_kbps = div_u64(peak_kbps, 1000);
	return (int) DIV64_U64_ROUND_UP(peak_kbps * 64, (54 * 8 * 1000));
}

static void set_dsc_configs_from_fairness_vars(struct dsc_mst_fairness_params *params,
		struct dsc_mst_fairness_vars *vars,
		int count)
{
	int i;

	for (i = 0; i < count; i++) {
		memset(&params[i].timing->dsc_cfg, 0, sizeof(params[i].timing->dsc_cfg));
		if (vars[i].dsc_enabled && dc_dsc_compute_config(
					params[i].sink->ctx->dc->res_pool->dscs[0],
					&params[i].sink->dsc_caps.dsc_dec_caps,
					params[i].sink->ctx->dc->debug.dsc_min_slice_height_override,
					0,
					0,
					params[i].timing,
					&params[i].timing->dsc_cfg)) {
			params[i].timing->flags.DSC = 1;

			if (params[i].bpp_overwrite)
				params[i].timing->dsc_cfg.bits_per_pixel = params[i].bpp_overwrite;
			else
				params[i].timing->dsc_cfg.bits_per_pixel = vars[i].bpp_x16;

			if (params[i].num_slices_h)
				params[i].timing->dsc_cfg.num_slices_h = params[i].num_slices_h;

			if (params[i].num_slices_v)
				params[i].timing->dsc_cfg.num_slices_v = params[i].num_slices_v;
		} else {
			params[i].timing->flags.DSC = 0;
		}
	}
}

static int bpp_x16_from_pbn(struct dsc_mst_fairness_params param, int pbn)
{
	struct dc_dsc_config dsc_config;
	u64 kbps;

	kbps = div_u64((u64)pbn * 994 * 8 * 54, 64);
	dc_dsc_compute_config(
			param.sink->ctx->dc->res_pool->dscs[0],
			&param.sink->dsc_caps.dsc_dec_caps,
			param.sink->ctx->dc->debug.dsc_min_slice_height_override,
			0,
			(int) kbps, param.timing, &dsc_config);

	return dsc_config.bits_per_pixel;
}

static void increase_dsc_bpp(struct drm_atomic_state *state,
			     struct dc_link *dc_link,
			     struct dsc_mst_fairness_params *params,
			     struct dsc_mst_fairness_vars *vars,
			     int count)
{
	int i;
	bool bpp_increased[MAX_PIPES];
	int initial_slack[MAX_PIPES];
	int min_initial_slack;
	int next_index;
	int remaining_to_increase = 0;
	int pbn_per_timeslot;
	int link_timeslots_used;
	int fair_pbn_alloc;

	pbn_per_timeslot = dm_mst_get_pbn_divider(dc_link);

	for (i = 0; i < count; i++) {
		if (vars[i].dsc_enabled) {
			initial_slack[i] = kbps_to_peak_pbn(params[i].bw_range.max_kbps) - vars[i].pbn;
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
			link_timeslots_used += DIV_ROUND_UP(vars[i].pbn, pbn_per_timeslot);

		fair_pbn_alloc = (63 - link_timeslots_used) / remaining_to_increase * pbn_per_timeslot;

		if (initial_slack[next_index] > fair_pbn_alloc) {
			vars[next_index].pbn += fair_pbn_alloc;
			if (drm_dp_atomic_find_vcpi_slots(state,
							  params[next_index].port->mgr,
							  params[next_index].port,
							  vars[next_index].pbn,
							  pbn_per_timeslot) < 0)
				return;
			if (!drm_dp_mst_atomic_check(state)) {
				vars[next_index].bpp_x16 = bpp_x16_from_pbn(params[next_index], vars[next_index].pbn);
			} else {
				vars[next_index].pbn -= fair_pbn_alloc;
				if (drm_dp_atomic_find_vcpi_slots(state,
								  params[next_index].port->mgr,
								  params[next_index].port,
								  vars[next_index].pbn,
								  pbn_per_timeslot) < 0)
					return;
			}
		} else {
			vars[next_index].pbn += initial_slack[next_index];
			if (drm_dp_atomic_find_vcpi_slots(state,
							  params[next_index].port->mgr,
							  params[next_index].port,
							  vars[next_index].pbn,
							  pbn_per_timeslot) < 0)
				return;
			if (!drm_dp_mst_atomic_check(state)) {
				vars[next_index].bpp_x16 = params[next_index].bw_range.max_target_bpp_x16;
			} else {
				vars[next_index].pbn -= initial_slack[next_index];
				if (drm_dp_atomic_find_vcpi_slots(state,
								  params[next_index].port->mgr,
								  params[next_index].port,
								  vars[next_index].pbn,
								  pbn_per_timeslot) < 0)
					return;
			}
		}

		bpp_increased[next_index] = true;
		remaining_to_increase--;
	}
}

static void try_disable_dsc(struct drm_atomic_state *state,
			    struct dc_link *dc_link,
			    struct dsc_mst_fairness_params *params,
			    struct dsc_mst_fairness_vars *vars,
			    int count)
{
	int i;
	bool tried[MAX_PIPES];
	int kbps_increase[MAX_PIPES];
	int max_kbps_increase;
	int next_index;
	int remaining_to_try = 0;

	for (i = 0; i < count; i++) {
		if (vars[i].dsc_enabled
				&& vars[i].bpp_x16 == params[i].bw_range.max_target_bpp_x16
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
		if (drm_dp_atomic_find_vcpi_slots(state,
						  params[next_index].port->mgr,
						  params[next_index].port,
						  vars[next_index].pbn,
						  dm_mst_get_pbn_divider(dc_link)) < 0)
			return;

		if (!drm_dp_mst_atomic_check(state)) {
			vars[next_index].dsc_enabled = false;
			vars[next_index].bpp_x16 = 0;
		} else {
			vars[next_index].pbn = kbps_to_peak_pbn(params[next_index].bw_range.max_kbps);
			if (drm_dp_atomic_find_vcpi_slots(state,
							  params[next_index].port->mgr,
							  params[next_index].port,
							  vars[next_index].pbn,
							  dm_mst_get_pbn_divider(dc_link)) < 0)
				return;
		}

		tried[next_index] = true;
		remaining_to_try--;
	}
}

static bool compute_mst_dsc_configs_for_link(struct drm_atomic_state *state,
					     struct dc_state *dc_state,
					     struct dc_link *dc_link,
					     struct dsc_mst_fairness_vars *vars)
{
	int i;
	struct dc_stream_state *stream;
	struct dsc_mst_fairness_params params[MAX_PIPES];
	struct amdgpu_dm_connector *aconnector;
	int count = 0;
	bool debugfs_overwrite = false;

	memset(params, 0, sizeof(params));

	/* Set up params */
	for (i = 0; i < dc_state->stream_count; i++) {
		struct dc_dsc_policy dsc_policy = {0};

		stream = dc_state->streams[i];

		if (stream->link != dc_link)
			continue;

		stream->timing.flags.DSC = 0;

		params[count].timing = &stream->timing;
		params[count].sink = stream->sink;
		aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;
		params[count].aconnector = aconnector;
		params[count].port = aconnector->port;
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
				&stream->timing, &params[count].bw_range))
			params[count].bw_range.stream_kbps = dc_bandwidth_in_kbps_from_timing(&stream->timing);

		count++;
	}
	/* Try no compression */
	for (i = 0; i < count; i++) {
		vars[i].aconnector = params[i].aconnector;
		vars[i].pbn = kbps_to_peak_pbn(params[i].bw_range.stream_kbps);
		vars[i].dsc_enabled = false;
		vars[i].bpp_x16 = 0;
		if (drm_dp_atomic_find_vcpi_slots(state,
						 params[i].port->mgr,
						 params[i].port,
						 vars[i].pbn,
						 dm_mst_get_pbn_divider(dc_link)) < 0)
			return false;
	}
	if (!drm_dp_mst_atomic_check(state) && !debugfs_overwrite) {
		set_dsc_configs_from_fairness_vars(params, vars, count);
		return true;
	}

	/* Try max compression */
	for (i = 0; i < count; i++) {
		if (params[i].compression_possible && params[i].clock_force_enable != DSC_CLK_FORCE_DISABLE) {
			vars[i].pbn = kbps_to_peak_pbn(params[i].bw_range.min_kbps);
			vars[i].dsc_enabled = true;
			vars[i].bpp_x16 = params[i].bw_range.min_target_bpp_x16;
			if (drm_dp_atomic_find_vcpi_slots(state,
							  params[i].port->mgr,
							  params[i].port,
							  vars[i].pbn,
							  dm_mst_get_pbn_divider(dc_link)) < 0)
				return false;
		} else {
			vars[i].pbn = kbps_to_peak_pbn(params[i].bw_range.stream_kbps);
			vars[i].dsc_enabled = false;
			vars[i].bpp_x16 = 0;
			if (drm_dp_atomic_find_vcpi_slots(state,
							  params[i].port->mgr,
							  params[i].port,
							  vars[i].pbn,
							  dm_mst_get_pbn_divider(dc_link)) < 0)
				return false;
		}
	}
	if (drm_dp_mst_atomic_check(state))
		return false;

	/* Optimize degree of compression */
	increase_dsc_bpp(state, dc_link, params, vars, count);

	try_disable_dsc(state, dc_link, params, vars, count);

	set_dsc_configs_from_fairness_vars(params, vars, count);

	return true;
}

bool compute_mst_dsc_configs_for_state(struct drm_atomic_state *state,
				       struct dc_state *dc_state,
				       struct dsc_mst_fairness_vars *vars)
{
	int i, j;
	struct dc_stream_state *stream;
	bool computed_streams[MAX_PIPES];
	struct amdgpu_dm_connector *aconnector;

	for (i = 0; i < dc_state->stream_count; i++)
		computed_streams[i] = false;

	for (i = 0; i < dc_state->stream_count; i++) {
		stream = dc_state->streams[i];

		if (stream->signal != SIGNAL_TYPE_DISPLAY_PORT_MST)
			continue;

		aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;

		if (!aconnector || !aconnector->dc_sink)
			continue;

		if (!aconnector->dc_sink->dsc_caps.dsc_dec_caps.is_dsc_supported)
			continue;

		if (computed_streams[i])
			continue;

		if (dcn20_remove_stream_from_ctx(stream->ctx->dc, dc_state, stream) != DC_OK)
			return false;

		mutex_lock(&aconnector->mst_mgr.lock);
		if (!compute_mst_dsc_configs_for_link(state, dc_state, stream->link, vars)) {
			mutex_unlock(&aconnector->mst_mgr.lock);
			return false;
		}
		mutex_unlock(&aconnector->mst_mgr.lock);

		for (j = 0; j < dc_state->stream_count; j++) {
			if (dc_state->streams[j]->link == stream->link)
				computed_streams[j] = true;
		}
	}

	for (i = 0; i < dc_state->stream_count; i++) {
		stream = dc_state->streams[i];

		if (stream->timing.flags.DSC == 1)
			if (dc_stream_add_dsc_to_resource(stream->ctx->dc, dc_state, stream) != DC_OK)
				return false;
	}

	return true;
}

#endif
