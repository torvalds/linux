// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#include "dm_services_types.h"
#include "dc.h"
#include "dc/dc_dmub_srv.h"
#include "dc/dc_edid_parser.h"
#include "dc/dc_stat.h"
#include "dc/dc_state.h"
#include "dc/dc_stream.h"
#include "dc/inc/core_types.h"
#include "link_enc_cfg.h"
#include "link/protocols/link_dpcd.h"
#include "link_service_types.h"
#include "link/protocols/link_dp_capability.h"
#include "link/protocols/link_ddc.h"

#include "amdgpu.h"
#include "amdgpu_display.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_connector.h"
#include "amdgpu_dm_kunit_helpers.h"
#include "amdgpu_dm_plane.h"
#include "amdgpu_dm_crtc.h"
#include "amdgpu_dm_wb.h"
#include "amdgpu_dm_mst_types.h"
#if defined(CONFIG_DEBUG_FS)
#include "amdgpu_dm_debugfs.h"
#endif
#include "amdgpu_dm_backlight.h"
#include "amdgpu_dm_audio.h"
#include "amdgpu_dm_irq.h"
#include "amdgpu_dm_psr.h"
#include "dm_helpers.h"

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_eld.h>
#include <drm/drm_fixed.h>
#include <drm/drm_mode.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_utils.h>
#include <drm/display/drm_dp_mst_helper.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/drm_privacy_screen_consumer.h>
#include <drm/display/drm_hdcp_helper.h>

#include <linux/backlight.h>

#include <media/cec-notifier.h>

#include "modules/inc/mod_freesync.h"
#include "modules/inc/mod_power.h"

#include "amdgpu_dm_trace.h"

/* Encoder functions */

static void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs amdgpu_dm_encoder_funcs = {
	.destroy = amdgpu_dm_encoder_destroy,
};

static void dm_encoder_helper_disable(struct drm_encoder *encoder)
{
}

static int dm_encoder_helper_atomic_check(struct drm_encoder *encoder,
					  struct drm_crtc_state *crtc_state,
					  struct drm_connector_state *conn_state)
{
	struct drm_atomic_commit *state = crtc_state->state;
	struct drm_connector *connector = conn_state->connector;
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct dm_connector_state *dm_new_connector_state = to_dm_connector_state(conn_state);
	const struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	struct drm_dp_mst_topology_mgr *mst_mgr;
	struct drm_dp_mst_port *mst_port;
	struct drm_dp_mst_topology_state *mst_state;
	enum dc_color_depth color_depth;
	int clock, bpp = 0;
	bool is_y420 = false;

	if ((connector->connector_type == DRM_MODE_CONNECTOR_eDP) ||
	    (connector->connector_type == DRM_MODE_CONNECTOR_LVDS)) {
		struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
		struct drm_display_mode *native_mode = &amdgpu_encoder->native_mode;
		enum drm_mode_status result;

		result = drm_crtc_helper_mode_valid_fixed(encoder->crtc, adjusted_mode, native_mode);
		if (result != MODE_OK && dm_new_connector_state->scaling == RMX_OFF) {
			drm_dbg_driver(encoder->dev,
				       "mode %dx%d@%dHz is not native, enabling scaling\n",
				       adjusted_mode->hdisplay, adjusted_mode->vdisplay,
				       drm_mode_vrefresh(adjusted_mode));
			dm_new_connector_state->scaling = RMX_ASPECT;
		}
		return 0;
	}

	if (!aconnector->mst_output_port)
		return 0;

	mst_port = aconnector->mst_output_port;
	mst_mgr = &aconnector->mst_root->mst_mgr;

	if (!crtc_state->connectors_changed && !crtc_state->mode_changed)
		return 0;

	mst_state = drm_atomic_get_mst_topology_state(state, mst_mgr);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	mst_state->pbn_div.full = dm_mst_get_pbn_divider(aconnector->mst_root->dc_link);

	if (!state->duplicated) {
		int max_bpc = conn_state->max_requested_bpc;

		is_y420 = drm_mode_is_420_also(&connector->display_info, adjusted_mode) &&
			  aconnector->force_yuv420_output;
		color_depth = amdgpu_dm_convert_color_depth_from_display_info(connector,
								    is_y420,
								    max_bpc);
		bpp = amdgpu_dm_convert_dc_color_depth_into_bpc(color_depth) * 3;
		clock = adjusted_mode->clock;
		dm_new_connector_state->pbn = drm_dp_calc_pbn_mode(clock, bpp << 4);
	}

	dm_new_connector_state->vcpi_slots =
		drm_dp_atomic_find_time_slots(state, mst_mgr, mst_port,
					      dm_new_connector_state->pbn);
	if (dm_new_connector_state->vcpi_slots < 0) {
		drm_dbg_atomic(connector->dev, "failed finding vcpi slots: %d\n", (int)dm_new_connector_state->vcpi_slots);
		return dm_new_connector_state->vcpi_slots;
	}
	return 0;
}

const struct drm_encoder_helper_funcs amdgpu_dm_encoder_helper_funcs = {
	.disable = dm_encoder_helper_disable,
	.atomic_check = dm_encoder_helper_atomic_check
};

int amdgpu_dm_get_encoder_crtc_mask(struct amdgpu_device *adev)
{
	switch (adev->mode_info.num_crtc) {
	case 1:
		return 0x1;
	case 2:
		return 0x3;
	case 3:
		return 0x7;
	case 4:
		return 0xf;
	case 5:
		return 0x1f;
	case 6:
	default:
		return 0x3f;
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_get_encoder_crtc_mask);

int amdgpu_dm_encoder_init(struct drm_device *dev,
			   struct amdgpu_encoder *aencoder,
			   uint32_t link_index)
{
	struct amdgpu_device *adev = drm_to_adev(dev);

	int res = drm_encoder_init(dev,
				   &aencoder->base,
				   &amdgpu_dm_encoder_funcs,
				   DRM_MODE_ENCODER_TMDS,
				   NULL);

	aencoder->base.possible_crtcs = amdgpu_dm_get_encoder_crtc_mask(adev);

	if (!res)
		aencoder->encoder_id = link_index;
	else
		aencoder->encoder_id = -1;

	drm_encoder_helper_add(&aencoder->base, &amdgpu_dm_encoder_helper_funcs);

	return res;
}

STATIC_IFN_KUNIT enum drm_mode_subconnector get_subconnector_type(struct dc_link *link)
{
	switch (link->dpcd_caps.dongle_type) {
	case DISPLAY_DONGLE_NONE:
		return DRM_MODE_SUBCONNECTOR_Native;
	case DISPLAY_DONGLE_DP_VGA_CONVERTER:
		return DRM_MODE_SUBCONNECTOR_VGA;
	case DISPLAY_DONGLE_DP_DVI_CONVERTER:
	case DISPLAY_DONGLE_DP_DVI_DONGLE:
		return DRM_MODE_SUBCONNECTOR_DVID;
	case DISPLAY_DONGLE_DP_HDMI_CONVERTER:
	case DISPLAY_DONGLE_DP_HDMI_DONGLE:
		return DRM_MODE_SUBCONNECTOR_HDMIA;
	case DISPLAY_DONGLE_DP_HDMI_MISMATCHED_DONGLE:
	default:
		return DRM_MODE_SUBCONNECTOR_Unknown;
	}
}
EXPORT_IF_KUNIT(get_subconnector_type);

STATIC_IFN_KUNIT void update_subconnector_property(struct amdgpu_dm_connector *aconnector)
{
	struct dc_link *link = aconnector->dc_link;
	struct drm_connector *connector = &aconnector->base;
	enum drm_mode_subconnector subconnector = DRM_MODE_SUBCONNECTOR_Unknown;

	if (connector->connector_type != DRM_MODE_CONNECTOR_DisplayPort)
		return;

	if (aconnector->dc_sink)
		subconnector = get_subconnector_type(link);

	drm_object_property_set_value(&connector->base,
			connector->dev->mode_config.dp_subconnector_property,
			subconnector);
}
EXPORT_IF_KUNIT(update_subconnector_property);

static int amdgpu_dm_connector_get_modes(struct drm_connector *connector);

STATIC_IFN_KUNIT void amdgpu_dm_fbc_init(struct drm_connector *connector)
{
	struct amdgpu_device *adev = drm_to_adev(connector->dev);
	struct dm_compressor_info *compressor = &adev->dm.compressor;
	struct amdgpu_dm_connector *aconn = to_amdgpu_dm_connector(connector);
	struct drm_display_mode *mode;
	unsigned long max_size = 0;

	if (adev->dm.dc->fbc_compressor == NULL)
		return;

	if (aconn->dc_link->connector_signal != SIGNAL_TYPE_EDP)
		return;

	if (compressor->bo_ptr)
		return;


	list_for_each_entry(mode, &connector->modes, head) {
		if (max_size < (unsigned long) mode->htotal * mode->vtotal)
			max_size = (unsigned long) mode->htotal * mode->vtotal;
	}

	if (max_size) {
		int r = amdgpu_bo_create_kernel(adev, max_size * 4, PAGE_SIZE,
			    AMDGPU_GEM_DOMAIN_GTT, &compressor->bo_ptr,
			    &compressor->gpu_addr, &compressor->cpu_addr);

		if (r)
			drm_err(adev_to_drm(adev), "DM: Failed to initialize FBC\n");
		else {
			adev->dm.dc->ctx->fbc_gpu_addr = compressor->gpu_addr;
			drm_info(adev_to_drm(adev), "DM: FBC alloc %lu\n", max_size*4);
		}

	}

}
EXPORT_IF_KUNIT(amdgpu_dm_fbc_init);


int amdgpu_dm_detect_mst_link_for_all_connectors(struct drm_device *dev)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	int ret = 0;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->dc_link->type == dc_connection_mst_branch &&
		    aconnector->mst_mgr.aux) {
			drm_dbg_kms(dev, "DM_MST: starting TM on aconnector: %p [id: %d]\n",
					 aconnector,
					 aconnector->base.base.id);

			ret = drm_dp_mst_topology_mgr_set_mst(&aconnector->mst_mgr, true);
			if (ret < 0) {
				drm_err(dev, "DM_MST: Failed to start MST\n");
				aconnector->dc_link->type =
					dc_connection_single;
				ret = dm_helpers_dp_mst_stop_top_mgr(aconnector->dc_link->ctx,
								     aconnector->dc_link);
				break;
			}
		}
	}
	drm_connector_list_iter_end(&iter);

	return ret;
}
EXPORT_IF_KUNIT(amdgpu_dm_detect_mst_link_for_all_connectors);

static void hdmi_cec_unset_edid(struct amdgpu_dm_connector *aconnector)
{
	struct cec_notifier *n = aconnector->notifier;

	if (!n)
		return;

	cec_notifier_phys_addr_invalidate(n);
}

void amdgpu_dm_hdmi_cec_set_edid(struct amdgpu_dm_connector *aconnector)
{
	struct drm_connector *connector = &aconnector->base;
	struct cec_notifier *n = aconnector->notifier;

	if (!n)
		return;

	cec_notifier_set_phys_addr(n,
				   connector->display_info.source_physical_address);
}

void amdgpu_dm_s3_handle_hdmi_cec(struct drm_device *ddev, bool suspend)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(ddev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (suspend)
			hdmi_cec_unset_edid(aconnector);
		else
			amdgpu_dm_hdmi_cec_set_edid(aconnector);
	}
	drm_connector_list_iter_end(&conn_iter);
}


struct drm_connector *
amdgpu_dm_find_first_crtc_matching_connector(struct drm_atomic_commit *state,
					     struct drm_crtc *crtc)
{
	u32 i;
	struct drm_connector_state *new_con_state;
	struct drm_connector *connector;
	struct drm_crtc *crtc_from_state;

	for_each_new_connector_in_state(state, connector, new_con_state, i) {
		crtc_from_state = new_con_state->crtc;

		if (crtc_from_state == crtc)
			return connector;
	}

	return NULL;
}
EXPORT_IF_KUNIT(amdgpu_dm_find_first_crtc_matching_connector);

STATIC_IFN_KUNIT void amdgpu_dm_set_panel_type(struct amdgpu_dm_connector *aconnector)
{
	struct drm_connector *connector = &aconnector->base;
	struct drm_display_info *display_info = &connector->display_info;
	struct dc_link *link = aconnector->dc_link;
	struct amdgpu_device *adev;
	enum dc_panel_type panel_type = PANEL_TYPE_NONE;

	adev = drm_to_adev(connector->dev);

	switch (display_info->amd_vsdb.panel_type) {
	case AMD_VSDB_PANEL_TYPE_OLED:
		panel_type = PANEL_TYPE_OLED;
		break;
	case AMD_VSDB_PANEL_TYPE_MINILED:
		panel_type = PANEL_TYPE_MINILED;
		break;
	}

	/* If VSDB didn't determine panel type, check DPCD ext caps */
	if (panel_type == PANEL_TYPE_NONE) {
		if (link->dpcd_sink_ext_caps.bits.miniled == 1)
			panel_type = PANEL_TYPE_MINILED;
		if (link->dpcd_sink_ext_caps.bits.oled == 1)
			panel_type = PANEL_TYPE_OLED;
	}

	/* If VSDB and DPCD didn't determine panel type, check DID */
	if (panel_type == PANEL_TYPE_NONE) {
		if (display_info->panel_type == DRM_MODE_PANEL_TYPE_LCD)
			panel_type = PANEL_TYPE_LCD;
		else if (display_info->panel_type == DRM_MODE_PANEL_TYPE_OLED)
			panel_type = PANEL_TYPE_OLED;
	}

	if (panel_type == PANEL_TYPE_NONE) {
		struct drm_amd_vsdb_info *vsdb = &display_info->amd_vsdb;
		u32 lum1_max = vsdb->luminance_range1.max_luminance;
		u32 lum2_max = vsdb->luminance_range2.max_luminance;

		if (vsdb->version && link->local_sink &&
		    link->local_sink->edid_caps.manufacturer_id ==
		    DDC_MANUFACTURERNAME_SAMSUNG &&
		    lum1_max >= ((lum2_max * 3) / 2))
			panel_type = PANEL_TYPE_MINILED;
	}

	if (panel_type != PANEL_TYPE_NONE)
		link->panel_type = panel_type;
	else
		link->panel_type = PANEL_TYPE_LCD;

	if (link->panel_type == PANEL_TYPE_OLED)
		drm_object_property_set_value(&connector->base,
		    adev_to_drm(adev)->mode_config.panel_type_property,
		    DRM_MODE_PANEL_TYPE_OLED);
	else if (link->panel_type == PANEL_TYPE_LCD)
		drm_object_property_set_value(&connector->base,
		    adev_to_drm(adev)->mode_config.panel_type_property,
		    DRM_MODE_PANEL_TYPE_LCD);
	else
		drm_object_property_set_value(&connector->base,
		    adev_to_drm(adev)->mode_config.panel_type_property,
		    DRM_MODE_PANEL_TYPE_UNKNOWN);

	drm_dbg_kms(aconnector->base.dev, "Panel type: %d\n", link->panel_type);
}
EXPORT_IF_KUNIT(amdgpu_dm_set_panel_type);

STATIC_IFN_KUNIT void amdgpu_dm_update_cacp_caps(struct amdgpu_dm_connector *aconnector)
{
	struct amdgpu_device *adev = drm_to_adev(aconnector->base.dev);
	struct dc_link *link = aconnector->dc_link;

	link->panel_config.cacp.cacp_supported = true;

	if (amdgpu_ip_version(adev, DCE_HWIP, 0) < IP_VERSION(3, 1, 4) ||
	    amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(3, 1, 6)) {
		link->panel_config.cacp.cacp_supported = false;
		return;
	}

	if (link->connector_signal != SIGNAL_TYPE_EDP &&
	    link->connector_signal != SIGNAL_TYPE_LVDS) {
		link->panel_config.cacp.cacp_supported = false;
		return;
	}

	if (link->panel_type == PANEL_TYPE_LCD)
		link->panel_config.cacp.cacp_supported = false;

	drm_dbg_kms(aconnector->base.dev, "cacp_supported: %d\n",
		    link->panel_config.cacp.cacp_supported);
}
EXPORT_IF_KUNIT(amdgpu_dm_update_cacp_caps);

DEFINE_FREE(sink_release, struct dc_sink *, if (_T) dc_sink_release(_T))

void amdgpu_dm_update_connector_after_detect(
		struct amdgpu_dm_connector *aconnector)
{
	struct drm_connector *connector = &aconnector->base;
	struct dc_sink *sink __free(sink_release) = NULL;
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	int inst;

	/* MST handled by drm_mst framework */
	if (aconnector->mst_mgr.mst_state)
		return;

	sink = aconnector->dc_link->local_sink;
	if (sink)
		dc_sink_retain(sink);

	/*
	 * Edid mgmt connector gets first update only in mode_valid hook and then
	 * the connector sink is set to either fake or physical sink depends on link status.
	 * Skip if already done during boot.
	 */
	if (aconnector->base.force != DRM_FORCE_UNSPECIFIED
			&& aconnector->dc_em_sink) {

		/*
		 * For S3 resume with headless use eml_sink to fake stream
		 * because on resume connector->sink is set to NULL
		 */
		guard(mutex)(&dev->mode_config.mutex);

		if (sink) {
			if (aconnector->dc_sink) {
				amdgpu_dm_update_freesync_caps(connector, NULL, true);
				/*
				 * retain and release below are used to
				 * bump up refcount for sink because the link doesn't point
				 * to it anymore after disconnect, so on next crtc to connector
				 * reshuffle by UMD we will get into unwanted dc_sink release
				 */
				dc_sink_release(aconnector->dc_sink);
			}
			aconnector->dc_sink = sink;
			dc_sink_retain(aconnector->dc_sink);
			amdgpu_dm_update_freesync_caps(connector,
					aconnector->drm_edid, true);
		} else {
			amdgpu_dm_update_freesync_caps(connector, NULL, true);
			if (!aconnector->dc_sink) {
				aconnector->dc_sink = aconnector->dc_em_sink;
				dc_sink_retain(aconnector->dc_sink);
			}
		}

		return;
	}

	/*
	 * TODO: temporary guard to look for proper fix
	 * if this sink is MST sink, we should not do anything
	 */
	if (sink && sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
		return;

	if (aconnector->dc_sink == sink) {
		/*
		 * We got a DP short pulse (Link Loss, DP CTS, etc...).
		 * Do nothing!!
		 */
		drm_dbg_kms(dev, "DCHPD: connector_id=%d: dc_sink didn't change.\n",
				 aconnector->connector_id);
		return;
	}

	drm_dbg_kms(dev, "DCHPD: connector_id=%d: Old sink=%p New sink=%p\n",
		    aconnector->connector_id, aconnector->dc_sink, sink);

	/* When polling, DRM has already locked the mutex for us. */
	if (!drm_kms_helper_is_poll_worker())
		mutex_lock(&dev->mode_config.mutex);

	/*
	 * 1. Update status of the drm connector
	 * 2. Send an event and let userspace tell us what to do
	 */
	if (sink) {
		/*
		 * TODO: check if we still need the S3 mode update workaround.
		 * If yes, put it here.
		 */
		if (aconnector->dc_sink) {
			amdgpu_dm_update_freesync_caps(connector, NULL, true);
			dc_sink_release(aconnector->dc_sink);
		}

		aconnector->dc_sink = sink;
		dc_sink_retain(aconnector->dc_sink);
		drm_edid_free(aconnector->drm_edid);
		aconnector->drm_edid = NULL;
		if (sink->dc_edid.length == 0) {
			hdmi_cec_unset_edid(aconnector);
			if (aconnector->dc_link->aux_mode)
				drm_dp_cec_unset_edid(&aconnector->dm_dp_aux.aux);
		} else {
			const struct edid *edid = (const struct edid *)sink->dc_edid.raw_edid;

			aconnector->drm_edid = drm_edid_alloc(edid, sink->dc_edid.length);
			drm_edid_connector_update(connector, aconnector->drm_edid);

			amdgpu_dm_hdmi_cec_set_edid(aconnector);
			if (aconnector->dc_link->aux_mode)
				drm_dp_cec_attach(&aconnector->dm_dp_aux.aux,
						  connector->display_info.source_physical_address);
		}

		if (!aconnector->timing_requested) {
			aconnector->timing_requested =
				kzalloc_obj(struct dc_crtc_timing);
			if (!aconnector->timing_requested)
				drm_err(dev,
					"failed to create aconnector->requested_timing\n");
		}

		amdgpu_dm_update_freesync_caps(connector, aconnector->drm_edid, true);
		amdgpu_dm_update_connector_ext_caps(aconnector);
		amdgpu_dm_set_panel_type(aconnector);
		amdgpu_dm_update_cacp_caps(aconnector);

		if (aconnector->hdmi_comp_auto) {
			if (sink->sink_signal != SIGNAL_TYPE_HDMI_FRL)
				sink->sink_signal = SIGNAL_TYPE_HDMI_FRL;
		}
	} else {
		hdmi_cec_unset_edid(aconnector);
		drm_dp_cec_unset_edid(&aconnector->dm_dp_aux.aux);
		amdgpu_dm_update_freesync_caps(connector, NULL, true);
		aconnector->num_modes = 0;
		dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
		drm_edid_free(aconnector->drm_edid);
		aconnector->drm_edid = NULL;
		kfree(aconnector->timing_requested);
		aconnector->timing_requested = NULL;
		/* Set CP to DESIRED if it was ENABLED, so we can re-enable it again on hotplug */
		if (connector->state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED)
			connector->state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;

		mutex_lock(&adev->dm.audio_lock);
		inst = aconnector->audio_inst;
		aconnector->audio_inst = -1;
		mutex_unlock(&adev->dm.audio_lock);
		if (inst != -1)
			amdgpu_dm_audio_eld_notify(adev, inst);
	}

	update_subconnector_property(aconnector);

	/* When polling, the mutex will be unlocked for us by DRM. */
	if (!drm_kms_helper_is_poll_worker())
		mutex_unlock(&dev->mode_config.mutex);
}

enum dc_color_depth
amdgpu_dm_convert_color_depth_from_display_info(const struct drm_connector *connector,
						bool is_y420, int requested_bpc)
{
	u8 bpc;

	if (is_y420) {
		bpc = 8;

		/* Cap display bpc based on HDMI 2.0 HF-VSDB */
		if (connector->display_info.hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_48)
			bpc = 16;
		else if (connector->display_info.hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_36)
			bpc = 12;
		else if (connector->display_info.hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_30)
			bpc = 10;
	} else {
		bpc = (uint8_t)connector->display_info.bpc;
		/* Assume 8 bpc by default if no bpc is specified. */
		bpc = bpc ? bpc : 8;
	}

	if (requested_bpc > 0) {
		/*
		 * Cap display bpc based on the user requested value.
		 *
		 * The value for state->max_bpc may not correctly updated
		 * depending on when the connector gets added to the state
		 * or if this was called outside of atomic check, so it
		 * can't be used directly.
		 */
		bpc = min_t(u8, bpc, requested_bpc);

		/* Round down to the nearest even number. */
		bpc = bpc - (bpc & 1);
	}

	switch (bpc) {
	case 0:
		/*
		 * Temporary Work around, DRM doesn't parse color depth for
		 * EDID revision before 1.4
		 * TODO: Fix edid parsing
		 */
		return COLOR_DEPTH_888;
	case 6:
		return COLOR_DEPTH_666;
	case 8:
		return COLOR_DEPTH_888;
	case 10:
		return COLOR_DEPTH_101010;
	case 12:
		return COLOR_DEPTH_121212;
	case 14:
		return COLOR_DEPTH_141414;
	case 16:
		return COLOR_DEPTH_161616;
	default:
		return COLOR_DEPTH_UNDEFINED;
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_convert_color_depth_from_display_info);

STATIC_IFN_KUNIT enum dc_aspect_ratio
get_aspect_ratio(const struct drm_display_mode *mode_in)
{
	/* 1-1 mapping, since both enums follow the HDMI spec. */
	return (enum dc_aspect_ratio) mode_in->picture_aspect_ratio;
}
EXPORT_IF_KUNIT(get_aspect_ratio);

enum dc_color_space
amdgpu_dm_get_output_color_space(const struct dc_crtc_timing *dc_crtc_timing,
				 const struct drm_connector_state *connector_state)
{
	enum dc_color_space color_space = COLOR_SPACE_SRGB;

	switch (connector_state->colorspace) {
	case DRM_MODE_COLORIMETRY_BT601_YCC:
		if (dc_crtc_timing->flags.Y_ONLY)
			color_space = COLOR_SPACE_YCBCR601_LIMITED;
		else
			color_space = COLOR_SPACE_YCBCR601;
		break;
	case DRM_MODE_COLORIMETRY_BT709_YCC:
		if (dc_crtc_timing->flags.Y_ONLY)
			color_space = COLOR_SPACE_YCBCR709_LIMITED;
		else
			color_space = COLOR_SPACE_YCBCR709;
		break;
	case DRM_MODE_COLORIMETRY_OPRGB:
		color_space = COLOR_SPACE_ADOBERGB;
		break;
	case DRM_MODE_COLORIMETRY_BT2020_RGB:
	case DRM_MODE_COLORIMETRY_BT2020_YCC:
		if (dc_crtc_timing->pixel_encoding == PIXEL_ENCODING_RGB)
			color_space = COLOR_SPACE_2020_RGB_FULLRANGE;
		else
			color_space = COLOR_SPACE_2020_YCBCR_LIMITED;
		break;
	case DRM_MODE_COLORIMETRY_DEFAULT: /* ITU601 */
	default:
		if (dc_crtc_timing->pixel_encoding == PIXEL_ENCODING_RGB) {
			color_space = COLOR_SPACE_SRGB;
			if (connector_state->hdmi.broadcast_rgb == DRM_HDMI_BROADCAST_RGB_LIMITED)
				color_space = COLOR_SPACE_SRGB_LIMITED;
		/*
		 * 27030khz is the separation point between HDTV and SDTV
		 * according to HDMI spec, we use YCbCr709 and YCbCr601
		 * respectively
		 */
		} else if (dc_crtc_timing->pix_clk_100hz > 270300) {
			if (dc_crtc_timing->flags.Y_ONLY)
				color_space =
					COLOR_SPACE_YCBCR709_LIMITED;
			else
				color_space = COLOR_SPACE_YCBCR709;
		} else {
			if (dc_crtc_timing->flags.Y_ONLY)
				color_space =
					COLOR_SPACE_YCBCR601_LIMITED;
			else
				color_space = COLOR_SPACE_YCBCR601;
		}
		break;
	}

	return color_space;
}
EXPORT_IF_KUNIT(amdgpu_dm_get_output_color_space);

STATIC_IFN_KUNIT enum display_content_type
get_output_content_type(const struct drm_connector_state *connector_state)
{
	switch (connector_state->content_type) {
	default:
	case DRM_MODE_CONTENT_TYPE_NO_DATA:
		return DISPLAY_CONTENT_TYPE_NO_DATA;
	case DRM_MODE_CONTENT_TYPE_GRAPHICS:
		return DISPLAY_CONTENT_TYPE_GRAPHICS;
	case DRM_MODE_CONTENT_TYPE_PHOTO:
		return DISPLAY_CONTENT_TYPE_PHOTO;
	case DRM_MODE_CONTENT_TYPE_CINEMA:
		return DISPLAY_CONTENT_TYPE_CINEMA;
	case DRM_MODE_CONTENT_TYPE_GAME:
		return DISPLAY_CONTENT_TYPE_GAME;
	}
}
EXPORT_IF_KUNIT(get_output_content_type);

STATIC_IFN_KUNIT bool adjust_colour_depth_from_display_info(
	struct dc_crtc_timing *timing_out,
	const struct drm_display_info *info)
{
	enum dc_color_depth depth = timing_out->display_color_depth;
	int normalized_clk;

	do {
		normalized_clk = timing_out->pix_clk_100hz / 10;
		/* YCbCr 4:2:0 requires additional adjustment of 1/2 */
		if (timing_out->pixel_encoding == PIXEL_ENCODING_YCBCR420)
			normalized_clk /= 2;
		/* Adjusting pix clock following on HDMI spec based on colour depth */
		switch (depth) {
		case COLOR_DEPTH_888:
			break;
		case COLOR_DEPTH_101010:
			normalized_clk = (normalized_clk * 30) / 24;
			break;
		case COLOR_DEPTH_121212:
			normalized_clk = (normalized_clk * 36) / 24;
			break;
		case COLOR_DEPTH_161616:
			normalized_clk = (normalized_clk * 48) / 24;
			break;
		default:
			/* The above depths are the only ones valid for HDMI. */
			return false;
		}
		if (normalized_clk <= info->max_tmds_clock) {
			timing_out->display_color_depth = depth;
			return true;
		}
	} while (--depth > COLOR_DEPTH_666);
	return false;
}
EXPORT_IF_KUNIT(adjust_colour_depth_from_display_info);

STATIC_IFN_KUNIT void fill_stream_properties_from_drm_display_mode(
	struct dc_stream_state *stream,
	const struct drm_display_mode *mode_in,
	const struct drm_connector *connector,
	const struct drm_connector_state *connector_state,
	const struct dc_stream_state *old_stream,
	int requested_bpc)
{
	bool is_dp_or_hdmi = dc_is_hdmi_signal(stream->signal) || dc_is_dp_signal(stream->signal);
	struct dc_crtc_timing *timing_out = &stream->timing;
	const struct drm_display_info *info = &connector->display_info;
	struct amdgpu_dm_connector *aconnector = NULL;
	struct hdmi_vendor_infoframe hv_frame;
	struct hdmi_avi_infoframe avi_frame;
	bool want_420;
	bool want_422;
	ssize_t err;

	if (connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK)
		aconnector = to_amdgpu_dm_connector(connector);

	memset(&hv_frame, 0, sizeof(hv_frame));
	memset(&avi_frame, 0, sizeof(avi_frame));

	timing_out->h_border_left = 0;
	timing_out->h_border_right = 0;
	timing_out->v_border_top = 0;
	timing_out->v_border_bottom = 0;

	want_420 = (aconnector && aconnector->force_yuv_pixel_format == PIXEL_ENCODING_YCBCR420) ||
		   (connector_state->color_format == DRM_CONNECTOR_COLOR_FORMAT_YCBCR420);
	want_422 = (aconnector && aconnector->force_yuv_pixel_format == PIXEL_ENCODING_YCBCR422) ||
		   (connector_state->color_format == DRM_CONNECTOR_COLOR_FORMAT_YCBCR422);

	if (drm_mode_is_420_only(info, mode_in) &&
	    (want_420 || connector_state->color_format == DRM_CONNECTOR_COLOR_FORMAT_AUTO)) {
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR420;
	} else if (drm_mode_is_420_also(info, mode_in) && want_420) {
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR420;
	} else if ((info->color_formats & BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR422)) &&
		   want_422 && is_dp_or_hdmi) {
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR422;
	} else if (connector_state->color_format == DRM_CONNECTOR_COLOR_FORMAT_YCBCR444 &&
		   (info->color_formats & BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR444)) &&
		   is_dp_or_hdmi) {
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR444;
	} else if (connector_state->color_format == DRM_CONNECTOR_COLOR_FORMAT_RGB444 ||
		   connector_state->color_format == DRM_CONNECTOR_COLOR_FORMAT_AUTO) {
		timing_out->pixel_encoding = PIXEL_ENCODING_RGB;
	} else {
		/*
		 * If a format was explicitly requested but the requested format
		 * can't be satisfied, set it to an invalid value so that an
		 * error bubbles up to userspace. This way, userspace knows it
		 * needs to make a better choice.
		 */
		if (connector_state->color_format != DRM_CONNECTOR_COLOR_FORMAT_AUTO)
			timing_out->pixel_encoding = PIXEL_ENCODING_UNDEFINED;
		else if (drm_mode_is_420_only(info, mode_in))
			timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR420;
		else
			timing_out->pixel_encoding = PIXEL_ENCODING_RGB;
	}

	timing_out->timing_3d_format = TIMING_3D_FORMAT_NONE;
	timing_out->display_color_depth = amdgpu_dm_convert_color_depth_from_display_info(
		connector,
		(timing_out->pixel_encoding == PIXEL_ENCODING_YCBCR420),
		requested_bpc);
	timing_out->scan_type = SCANNING_TYPE_NODATA;
	timing_out->hdmi_vic = 0;

	if (old_stream) {
		timing_out->vic = old_stream->timing.vic;
		timing_out->flags.HSYNC_POSITIVE_POLARITY = old_stream->timing.flags.HSYNC_POSITIVE_POLARITY;
		timing_out->flags.VSYNC_POSITIVE_POLARITY = old_stream->timing.flags.VSYNC_POSITIVE_POLARITY;
	} else {
		timing_out->vic = drm_match_cea_mode(mode_in);
		if (mode_in->flags & DRM_MODE_FLAG_PHSYNC)
			timing_out->flags.HSYNC_POSITIVE_POLARITY = 1;
		if (mode_in->flags & DRM_MODE_FLAG_PVSYNC)
			timing_out->flags.VSYNC_POSITIVE_POLARITY = 1;
	}

	if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A ||
		stream->signal == SIGNAL_TYPE_HDMI_FRL) {
		err = drm_hdmi_avi_infoframe_from_display_mode(&avi_frame,
							       (struct drm_connector *)connector,
							       mode_in);
		if (err < 0)
			drm_warn_once(connector->dev, "Failed to setup avi infoframe on connector %s: %zd\n",
				      connector->name, err);
		timing_out->vic = avi_frame.video_code;
		err = drm_hdmi_vendor_infoframe_from_display_mode(&hv_frame,
								  (struct drm_connector *)connector,
								  mode_in);
		if (err < 0)
			drm_warn_once(connector->dev, "Failed to setup vendor infoframe on connector %s: %zd\n",
				      connector->name, err);
		timing_out->hdmi_vic = hv_frame.vic;
	}

	if (aconnector && amdgpu_dm_is_freesync_video_mode(mode_in, aconnector)) {
		timing_out->h_addressable = mode_in->hdisplay;
		timing_out->h_total = mode_in->htotal;
		timing_out->h_sync_width = mode_in->hsync_end - mode_in->hsync_start;
		timing_out->h_front_porch = mode_in->hsync_start - mode_in->hdisplay;
		timing_out->v_total = mode_in->vtotal;
		timing_out->v_addressable = mode_in->vdisplay;
		timing_out->v_front_porch = mode_in->vsync_start - mode_in->vdisplay;
		timing_out->v_sync_width = mode_in->vsync_end - mode_in->vsync_start;
		timing_out->pix_clk_100hz = mode_in->clock * 10;
	} else {
		timing_out->h_addressable = mode_in->crtc_hdisplay;
		timing_out->h_total = mode_in->crtc_htotal;
		timing_out->h_sync_width = mode_in->crtc_hsync_end - mode_in->crtc_hsync_start;
		timing_out->h_front_porch = mode_in->crtc_hsync_start - mode_in->crtc_hdisplay;
		timing_out->v_total = mode_in->crtc_vtotal;
		timing_out->v_addressable = mode_in->crtc_vdisplay;
		timing_out->v_front_porch = mode_in->crtc_vsync_start - mode_in->crtc_vdisplay;
		timing_out->v_sync_width = mode_in->crtc_vsync_end - mode_in->crtc_vsync_start;
		timing_out->pix_clk_100hz = mode_in->crtc_clock * 10;
	}

	timing_out->aspect_ratio = get_aspect_ratio(mode_in);

	stream->out_transfer_func.type = TF_TYPE_PREDEFINED;
	stream->out_transfer_func.tf = TRANSFER_FUNCTION_SRGB;
	if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		if (!adjust_colour_depth_from_display_info(timing_out, info) &&
		    drm_mode_is_420_also(info, mode_in) &&
		    timing_out->pixel_encoding != PIXEL_ENCODING_YCBCR420) {
			timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR420;
			adjust_colour_depth_from_display_info(timing_out, info);
		}
	}

	stream->output_color_space = amdgpu_dm_get_output_color_space(timing_out, connector_state);
	stream->content_type = get_output_content_type(connector_state);
}
EXPORT_IF_KUNIT(fill_stream_properties_from_drm_display_mode);

STATIC_IFN_KUNIT void
copy_crtc_timing_for_drm_display_mode(const struct drm_display_mode *src_mode,
				      struct drm_display_mode *dst_mode)
{
	dst_mode->crtc_hdisplay = src_mode->crtc_hdisplay;
	dst_mode->crtc_vdisplay = src_mode->crtc_vdisplay;
	dst_mode->crtc_clock = src_mode->crtc_clock;
	dst_mode->crtc_hblank_start = src_mode->crtc_hblank_start;
	dst_mode->crtc_hblank_end = src_mode->crtc_hblank_end;
	dst_mode->crtc_hsync_start =  src_mode->crtc_hsync_start;
	dst_mode->crtc_hsync_end = src_mode->crtc_hsync_end;
	dst_mode->crtc_htotal = src_mode->crtc_htotal;
	dst_mode->crtc_hskew = src_mode->crtc_hskew;
	dst_mode->crtc_vblank_start = src_mode->crtc_vblank_start;
	dst_mode->crtc_vblank_end = src_mode->crtc_vblank_end;
	dst_mode->crtc_vsync_start = src_mode->crtc_vsync_start;
	dst_mode->crtc_vsync_end = src_mode->crtc_vsync_end;
	dst_mode->crtc_vtotal = src_mode->crtc_vtotal;
}
EXPORT_IF_KUNIT(copy_crtc_timing_for_drm_display_mode);

STATIC_IFN_KUNIT void
decide_crtc_timing_for_drm_display_mode(struct drm_display_mode *drm_mode,
					const struct drm_display_mode *native_mode,
					bool scale_enabled)
{
	if (scale_enabled || (
	    native_mode->clock == drm_mode->clock &&
	    native_mode->htotal == drm_mode->htotal &&
	    native_mode->vtotal == drm_mode->vtotal)) {
		if (native_mode->crtc_clock)
			copy_crtc_timing_for_drm_display_mode(native_mode, drm_mode);
	} else {
		/* no scaling nor amdgpu inserted, no need to patch */
	}
}
EXPORT_IF_KUNIT(decide_crtc_timing_for_drm_display_mode);

static struct dc_sink *
create_fake_sink(struct drm_device *dev, struct dc_link *link)
{
	struct dc_sink_init_data sink_init_data = { 0 };
	struct dc_sink *sink = NULL;

	sink_init_data.link = link;
	sink_init_data.sink_signal = link->connector_signal;

	sink = dc_sink_create(&sink_init_data);
	if (!sink) {
		drm_err(dev, "Failed to create sink!\n");
		return NULL;
	}
	sink->sink_signal = SIGNAL_TYPE_VIRTUAL;

	return sink;
}

/**
 * DOC: FreeSync Video
 *
 * When a userspace application wants to play a video, the content follows a
 * standard format definition that usually specifies the FPS for that format.
 * The below list illustrates some video format and the expected FPS,
 * respectively:
 *
 * - TV/NTSC (23.976 FPS)
 * - Cinema (24 FPS)
 * - TV/PAL (25 FPS)
 * - TV/NTSC (29.97 FPS)
 * - TV/NTSC (30 FPS)
 * - Cinema HFR (48 FPS)
 * - TV/PAL (50 FPS)
 * - Commonly used (60 FPS)
 * - Multiples of 24 (48,72,96 FPS)
 *
 * The list of standards video format is not huge and can be added to the
 * connector modeset list beforehand. With that, userspace can leverage
 * FreeSync to extends the front porch in order to attain the target refresh
 * rate. Such a switch will happen seamlessly, without screen blanking or
 * reprogramming of the output in any other way. If the userspace requests a
 * modesetting change compatible with FreeSync modes that only differ in the
 * refresh rate, DC will skip the full update and avoid blink during the
 * transition. For example, the video player can change the modesetting from
 * 60Hz to 30Hz for playing TV/NTSC content when it goes full screen without
 * causing any display blink. This same concept can be applied to a mode
 * setting change.
 */
struct drm_display_mode *
amdgpu_dm_get_highest_refresh_rate_mode(struct amdgpu_dm_connector *aconnector,
		bool use_probed_modes)
{
	struct drm_display_mode *m, *m_pref = NULL;
	u16 current_refresh, highest_refresh;
	struct list_head *list_head = use_probed_modes ?
		&aconnector->base.probed_modes :
		&aconnector->base.modes;

	if (aconnector->base.connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
		return NULL;

	if (aconnector->freesync_vid_base.clock != 0)
		return &aconnector->freesync_vid_base;

	/* Find the preferred mode */
	list_for_each_entry(m, list_head, head) {
		if (m->type & DRM_MODE_TYPE_PREFERRED) {
			m_pref = m;
			break;
		}
	}

	if (!m_pref) {
		/* Probably an EDID with no preferred mode. Fallback to first entry */
		m_pref = list_first_entry_or_null(
				&aconnector->base.modes, struct drm_display_mode, head);
		if (!m_pref) {
			drm_dbg_driver(aconnector->base.dev, "No preferred mode found in EDID\n");
			return NULL;
		}
	}

	highest_refresh = drm_mode_vrefresh(m_pref);

	/*
	 * Find the mode with highest refresh rate with same resolution.
	 * For some monitors, preferred mode is not the mode with highest
	 * supported refresh rate.
	 */
	list_for_each_entry(m, list_head, head) {
		current_refresh  = drm_mode_vrefresh(m);

		if (m->hdisplay == m_pref->hdisplay &&
		    m->vdisplay == m_pref->vdisplay &&
		    highest_refresh < current_refresh) {
			highest_refresh = current_refresh;
			m_pref = m;
		}
	}

	drm_mode_copy(&aconnector->freesync_vid_base, m_pref);
	return m_pref;
}
EXPORT_IF_KUNIT(amdgpu_dm_get_highest_refresh_rate_mode);

bool amdgpu_dm_is_freesync_video_mode(const struct drm_display_mode *mode,
		struct amdgpu_dm_connector *aconnector)
{
	struct drm_display_mode *high_mode;
	int timing_diff;

	high_mode = amdgpu_dm_get_highest_refresh_rate_mode(aconnector, false);
	if (!high_mode || !mode)
		return false;

	timing_diff = high_mode->vtotal - mode->vtotal;

	if (high_mode->clock == 0 || high_mode->clock != mode->clock ||
	    high_mode->hdisplay != mode->hdisplay ||
	    high_mode->vdisplay != mode->vdisplay ||
	    high_mode->hsync_start != mode->hsync_start ||
	    high_mode->hsync_end != mode->hsync_end ||
	    high_mode->htotal != mode->htotal ||
	    high_mode->hskew != mode->hskew ||
	    high_mode->vscan != mode->vscan ||
	    high_mode->vsync_start - mode->vsync_start != timing_diff ||
	    high_mode->vsync_end - mode->vsync_end != timing_diff)
		return false;
	else
		return true;
}
EXPORT_IF_KUNIT(amdgpu_dm_is_freesync_video_mode);

#if defined(CONFIG_DRM_AMD_DC_FP)
static void update_dsc_caps(struct amdgpu_dm_connector *aconnector,
			    struct dc_sink *sink, struct dc_stream_state *stream,
			    struct dsc_dec_dpcd_caps *dsc_caps)
{
	stream->timing.flags.DSC = 0;
	dsc_caps->is_dsc_supported = false;

	if (aconnector->dc_link && (sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT ||
	    sink->sink_signal == SIGNAL_TYPE_EDP)) {
		if (sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_NONE)
			dc_dsc_parse_dsc_dpcd(aconnector->dc_link->ctx->dc,
				aconnector->dc_link->dpcd_caps.dsc_caps.dsc_basic_caps.raw,
				aconnector->dc_link->dpcd_caps.dsc_caps.dsc_branch_decoder_caps.raw,
				dsc_caps);
		else if (sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER) {
			if (aconnector->dc_link->dpcd_caps.dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_PASSTHROUGH_SUPPORT &&
					!aconnector->dsc_settings.dsc_force_disable_passthrough &&
					aconnector->dc_link->dpcd_caps.dongle_caps.dp_hdmi_frl_max_link_bw_in_kbps > 0 &&
					sink->edid_caps.frl_dsc_support &&
					sink->edid_caps.max_frl_rate > 0 &&
					sink->edid_caps.frl_dsc_max_frl_rate > 0)
				dc_dsc_parse_dsc_edid(aconnector->dc_link->ctx->dc, &sink->edid_caps, dsc_caps);
			else
				dc_dsc_parse_dsc_dpcd(aconnector->dc_link->ctx->dc,
				      aconnector->dc_link->dpcd_caps.dsc_caps.dsc_basic_caps.raw,
				      aconnector->dc_link->dpcd_caps.dsc_caps.dsc_branch_decoder_caps.raw,
				      dsc_caps);
		}
	} else if (aconnector->dc_link && sink->sink_signal == SIGNAL_TYPE_HDMI_FRL) {
		if (sink->edid_caps.frl_dsc_support &&
				sink->edid_caps.max_frl_rate > 0 &&
				sink->edid_caps.frl_dsc_max_frl_rate > 0)
			dc_dsc_parse_dsc_edid(aconnector->dc_link->ctx->dc, &sink->edid_caps, dsc_caps);
	}
}

static void apply_dsc_policy_for_edp(struct amdgpu_dm_connector *aconnector,
				    struct dc_sink *sink, struct dc_stream_state *stream,
				    struct dsc_dec_dpcd_caps *dsc_caps,
				    uint32_t max_dsc_target_bpp_limit_override)
{
	const struct dc_link_settings *verified_link_cap = NULL;
	u32 link_bw_in_kbps;
	u32 edp_min_bpp_x16, edp_max_bpp_x16;
	struct dc *dc = sink->ctx->dc;
	struct dc_dsc_bw_range bw_range = {0};
	struct dc_dsc_config dsc_cfg = {0};
	struct dc_dsc_config_options dsc_options = {0};

	dc_dsc_get_default_config_option(dc, &dsc_options);
	dsc_options.max_target_bpp_limit_override_x16 = max_dsc_target_bpp_limit_override * 16;

	verified_link_cap = dc_link_get_link_cap(stream->link);
	link_bw_in_kbps = dc_link_bandwidth_kbps(stream->link, verified_link_cap);
	edp_min_bpp_x16 = 8 * 16;
	edp_max_bpp_x16 = 8 * 16;

	if (edp_max_bpp_x16 > dsc_caps->edp_max_bits_per_pixel)
		edp_max_bpp_x16 = dsc_caps->edp_max_bits_per_pixel;

	if (edp_max_bpp_x16 < edp_min_bpp_x16)
		edp_min_bpp_x16 = edp_max_bpp_x16;

	if (dc_dsc_compute_bandwidth_range(dc->res_pool->dscs[0],
				dc->debug.dsc_min_slice_height_override,
				edp_min_bpp_x16, edp_max_bpp_x16,
				dsc_caps,
				&stream->timing,
				dc_link_get_highest_encoding_format(aconnector->dc_link),
				&bw_range)) {

		if (bw_range.max_kbps < link_bw_in_kbps) {
			if (dc_dsc_compute_config(dc->res_pool->dscs[0],
					dsc_caps,
					&dsc_options,
					0,
					&stream->timing,
					dc_link_get_highest_encoding_format(aconnector->dc_link),
					&dsc_cfg)) {
				stream->timing.dsc_cfg = dsc_cfg;
				stream->timing.flags.DSC = 1;
				stream->timing.dsc_cfg.bits_per_pixel = edp_max_bpp_x16;
			}
			return;
		}
	}

	if (dc_dsc_compute_config(dc->res_pool->dscs[0],
				dsc_caps,
				&dsc_options,
				link_bw_in_kbps,
				&stream->timing,
				dc_link_get_highest_encoding_format(aconnector->dc_link),
				&dsc_cfg)) {
		stream->timing.dsc_cfg = dsc_cfg;
		stream->timing.flags.DSC = 1;
	}
}

static void apply_dsc_policy_for_stream(struct amdgpu_dm_connector *aconnector,
					struct dc_sink *sink, struct dc_stream_state *stream,
					struct dsc_dec_dpcd_caps *dsc_caps)
{
	struct drm_connector *drm_connector = &aconnector->base;
	u32 link_bandwidth_kbps;
	struct dc *dc = sink->ctx->dc;
	const struct dc_hdmi_frl_link_settings *frl_verified_link_cap = NULL;
	u32 converter_bw_in_kbps;
	u32 sink_bw_in_kbps;
	u32 dsc_sink_bw_in_kbps;
	u32 max_supported_bw_in_kbps, timing_bw_in_kbps;
	u32 dsc_max_supported_bw_in_kbps;
	u32 max_dsc_target_bpp_limit_override =
		drm_connector->display_info.max_dsc_bpp;
	struct dc_dsc_config_options dsc_options = {0};

	if (!aconnector->dc_link)
		return;

	dc_dsc_get_default_config_option(dc, &dsc_options);
	dsc_options.max_target_bpp_limit_override_x16 = max_dsc_target_bpp_limit_override * 16;

	link_bandwidth_kbps = dc_link_bandwidth_kbps(aconnector->dc_link,
							dc_link_get_link_cap(aconnector->dc_link));

	/* Set DSC policy according to dsc_clock_en */
	dc_dsc_policy_set_enable_dsc_when_not_needed(
		aconnector->dsc_settings.dsc_force_enable == DSC_CLK_FORCE_ENABLE);

	if (sink->sink_signal == SIGNAL_TYPE_EDP &&
	    !aconnector->dc_link->panel_config.dsc.disable_dsc_edp &&
	    dc->caps.edp_dsc_support && aconnector->dsc_settings.dsc_force_enable != DSC_CLK_FORCE_DISABLE) {

		apply_dsc_policy_for_edp(aconnector, sink, stream, dsc_caps, max_dsc_target_bpp_limit_override);

	} else if (sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT) {
		if (sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_NONE) {
			if (dc_dsc_compute_config(aconnector->dc_link->ctx->dc->res_pool->dscs[0],
						dsc_caps,
						&dsc_options,
						link_bandwidth_kbps,
						&stream->timing,
						dc_link_get_highest_encoding_format(aconnector->dc_link),
						&stream->timing.dsc_cfg)) {
				stream->timing.flags.DSC = 1;
				drm_dbg_driver(drm_connector->dev, "%s: SST_DSC [%s] DSC is selected from SST RX\n",
							__func__, drm_connector->name);
			}
		} else if (sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER) {
			timing_bw_in_kbps = dc_bandwidth_in_kbps_from_timing(&stream->timing,
					dc_link_get_highest_encoding_format(aconnector->dc_link));
			converter_bw_in_kbps = aconnector->dc_link->dpcd_caps.dongle_caps.dp_hdmi_frl_max_link_bw_in_kbps;
			sink_bw_in_kbps = dc_link_bw_kbps_from_raw_frl_link_rate_data(dc, sink->edid_caps.max_frl_rate);
			dsc_sink_bw_in_kbps = dc_link_bw_kbps_from_raw_frl_link_rate_data(dc, sink->edid_caps.frl_dsc_max_frl_rate);

			if (dsc_caps->is_frl) {
				max_supported_bw_in_kbps = min(link_bandwidth_kbps, converter_bw_in_kbps);
				max_supported_bw_in_kbps = min(max_supported_bw_in_kbps, sink_bw_in_kbps);
				dsc_max_supported_bw_in_kbps = min(max_supported_bw_in_kbps, dsc_sink_bw_in_kbps);
			} else {
				max_supported_bw_in_kbps = link_bandwidth_kbps;
				dsc_max_supported_bw_in_kbps = link_bandwidth_kbps;
			}

			if (timing_bw_in_kbps > max_supported_bw_in_kbps &&
					max_supported_bw_in_kbps > 0 &&
					dsc_max_supported_bw_in_kbps > 0)
				if (dc_dsc_compute_config(aconnector->dc_link->ctx->dc->res_pool->dscs[0],
						dsc_caps,
						&dsc_options,
						dsc_max_supported_bw_in_kbps,
						&stream->timing,
						dc_link_get_highest_encoding_format(aconnector->dc_link),
						&stream->timing.dsc_cfg)) {
					stream->timing.flags.DSC = 1;
					drm_dbg_driver(drm_connector->dev, "%s: SST_DSC [%s] DSC is selected from %s\n",
							__func__, drm_connector->name,
							(dsc_caps->is_frl == 1) ? "HDMI FRL RX" : "DP-HDMI PCON");
				}
		}
	} else if (sink->sink_signal == SIGNAL_TYPE_HDMI_FRL) {
		struct dc_dsc_policy dsc_policy = {0};

		frl_verified_link_cap = dc_link_get_frl_link_cap(stream->link);
		if (frl_verified_link_cap->frl_link_rate != HDMI_FRL_LINK_RATE_DISABLE &&
			aconnector->dc_link->frl_flags.force_frl_dsc) {
			dc_dsc_policy_set_enable_dsc_when_not_needed(true);
			dc_dsc_get_policy_for_timing(&stream->timing, 0, &dsc_policy, dc_link_get_highest_encoding_format(stream->link));
		}

		timing_bw_in_kbps = dc_bandwidth_in_kbps_from_timing(&stream->timing, DC_LINK_ENCODING_HDMI_FRL);
		link_bandwidth_kbps = dc_link_frl_bandwidth_kbps(stream->link, frl_verified_link_cap->frl_link_rate);
		dsc_sink_bw_in_kbps = dc_link_bw_kbps_from_raw_frl_link_rate_data(dc, sink->edid_caps.frl_dsc_max_frl_rate);

		if ((timing_bw_in_kbps > link_bandwidth_kbps && dsc_sink_bw_in_kbps > 0) ||
		    (dsc_policy.enable_dsc_when_not_needed || dsc_options.force_dsc_when_not_needed)) {
			if (dc_dsc_compute_config(aconnector->dc_link->ctx->dc->res_pool->dscs[0],
					dsc_caps,
					&dsc_options,
					dsc_sink_bw_in_kbps,
					&stream->timing,
					dc_link_get_highest_encoding_format(aconnector->dc_link),
					&stream->timing.dsc_cfg)) {
				stream->timing.flags.DSC = 1;
				drm_dbg_driver(drm_connector->dev, "%s: HDMI_FRL_DSC [%s] DSC is selected from HDMI FRL RX\n",
						__func__, drm_connector->name);
			}
		}
	}

	/* Overwrite the stream flag if DSC is enabled through debugfs */
	if (aconnector->dsc_settings.dsc_force_enable == DSC_CLK_FORCE_ENABLE)
		stream->timing.flags.DSC = 1;

	if (stream->timing.flags.DSC && aconnector->dsc_settings.dsc_num_slices_h)
		stream->timing.dsc_cfg.num_slices_h = aconnector->dsc_settings.dsc_num_slices_h;

	if (stream->timing.flags.DSC && aconnector->dsc_settings.dsc_num_slices_v)
		stream->timing.dsc_cfg.num_slices_v = aconnector->dsc_settings.dsc_num_slices_v;

	if (stream->timing.flags.DSC && aconnector->dsc_settings.dsc_bits_per_pixel)
		stream->timing.dsc_cfg.bits_per_pixel = aconnector->dsc_settings.dsc_bits_per_pixel;
}
#endif

static struct dc_stream_state *
create_stream_for_sink(struct drm_connector *connector,
		       const struct drm_display_mode *drm_mode,
		       const struct dm_connector_state *dm_state,
		       const struct dc_stream_state *old_stream,
		       int requested_bpc)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_dm_connector *aconnector = NULL;
	struct drm_display_mode *preferred_mode = NULL;
	const struct drm_connector_state *con_state = &dm_state->base;
	struct dc_stream_state *stream = NULL;
	struct drm_display_mode mode;
	struct drm_display_mode saved_mode;
	struct drm_display_mode *freesync_mode = NULL;
	bool native_mode_found = false;
	bool recalculate_timing = false;
	bool scale = dm_state->scaling != RMX_OFF;
	int mode_refresh;
	int preferred_refresh = 0;
	enum color_transfer_func tf = TRANSFER_FUNC_UNKNOWN;
#if defined(CONFIG_DRM_AMD_DC_FP)
	struct dsc_dec_dpcd_caps dsc_caps = {0};
#endif
	struct dc_link *link = NULL;
	struct dc_sink *sink = NULL;

	drm_mode_init(&mode, drm_mode);
	memset(&saved_mode, 0, sizeof(saved_mode));

	if (connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK) {
		aconnector = NULL;
		aconnector = to_amdgpu_dm_connector(connector);
		link = aconnector->dc_link;
	} else {
		struct drm_writeback_connector *wbcon = NULL;
		struct amdgpu_dm_wb_connector *dm_wbcon = NULL;

		wbcon = drm_connector_to_writeback(connector);
		dm_wbcon = to_amdgpu_dm_wb_connector(wbcon);
		link = dm_wbcon->link;
	}

	if (!aconnector || !aconnector->dc_sink) {
		sink = create_fake_sink(dev, link);
		if (!sink)
			return stream;

	} else {
		sink = aconnector->dc_sink;
		dc_sink_retain(sink);
	}

	stream = dc_create_stream_for_sink(sink);

	if (stream == NULL) {
		drm_err(dev, "Failed to create stream for sink!\n");
		goto finish;
	}

	/* We leave this NULL for writeback connectors */
	stream->dm_stream_context = aconnector;

	stream->timing.flags.LTE_340MCSC_SCRAMBLE =
		connector->display_info.hdmi.scdc.scrambling.low_rates;

	list_for_each_entry(preferred_mode, &connector->modes, head) {
		/* Search for preferred mode */
		if (preferred_mode->type & DRM_MODE_TYPE_PREFERRED) {
			native_mode_found = true;
			break;
		}
	}
	if (!native_mode_found)
		preferred_mode = list_first_entry_or_null(
				&connector->modes,
				struct drm_display_mode,
				head);

	mode_refresh = drm_mode_vrefresh(&mode);

	if (preferred_mode == NULL) {
		/*
		 * This may not be an error, the use case is when we have no
		 * usermode calls to reset and set mode upon hotplug. In this
		 * case, we call set mode ourselves to restore the previous mode
		 * and the modelist may not be filled in time.
		 */
		drm_dbg_driver(dev, "No preferred mode found\n");
	} else if (aconnector) {
		recalculate_timing = amdgpu_freesync_vid_mode &&
				 amdgpu_dm_is_freesync_video_mode(&mode, aconnector);
		if (recalculate_timing) {
			freesync_mode = amdgpu_dm_get_highest_refresh_rate_mode(aconnector, false);
			drm_mode_copy(&saved_mode, &mode);
			saved_mode.picture_aspect_ratio = mode.picture_aspect_ratio;
			drm_mode_copy(&mode, freesync_mode);
			mode.picture_aspect_ratio = saved_mode.picture_aspect_ratio;
		} else {
			decide_crtc_timing_for_drm_display_mode(
					&mode, preferred_mode, scale);

			preferred_refresh = drm_mode_vrefresh(preferred_mode);
		}
	}

	if (recalculate_timing)
		drm_mode_set_crtcinfo(&saved_mode, 0);

	/*
	 * If scaling is enabled and refresh rate didn't change
	 * we copy the vic and polarities of the old timings
	 */
	if (!scale || mode_refresh != preferred_refresh)
		fill_stream_properties_from_drm_display_mode(
			stream, &mode, connector, con_state, NULL,
			requested_bpc);
	else
		fill_stream_properties_from_drm_display_mode(
			stream, &mode, connector, con_state, old_stream,
			requested_bpc);

	/* The rest isn't needed for writeback connectors */
	if (!aconnector)
		goto finish;

	if (aconnector->timing_changed) {
		drm_dbg(aconnector->base.dev,
			"overriding timing for automated test, bpc %d, changing to %d\n",
			stream->timing.display_color_depth,
			aconnector->timing_requested->display_color_depth);
		stream->timing = *aconnector->timing_requested;
	}

#if defined(CONFIG_DRM_AMD_DC_FP)
	/* SST DSC determination policy */
	update_dsc_caps(aconnector, sink, stream, &dsc_caps);
	if (aconnector->dsc_settings.dsc_force_enable != DSC_CLK_FORCE_DISABLE && dsc_caps.is_dsc_supported)
		apply_dsc_policy_for_stream(aconnector, sink, stream, &dsc_caps);
#endif

	amdgpu_dm_update_stream_scaling_settings(dev, &mode, dm_state, stream);

	amdgpu_dm_fill_audio_info(
		&stream->audio_info,
		connector,
		sink);

	update_stream_signal(stream, sink);

	if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A ||
	    stream->signal == SIGNAL_TYPE_HDMI_FRL)
		mod_build_hf_vsif_infopacket(stream, &stream->vsp_infopacket, false, false);

	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT ||
	    stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST ||
	    stream->signal == SIGNAL_TYPE_EDP) {
		const struct dc_edid_caps *edid_caps;
		unsigned int disable_colorimetry = 0;

		if (aconnector->dc_sink) {
			edid_caps = &aconnector->dc_sink->edid_caps;
			disable_colorimetry = edid_caps->panel_patch.disable_colorimetry;
		}

		/*
		 * should decide stream support vsc sdp colorimetry capability
		 * before building vsc info packet
		 */
		stream->use_vsc_sdp_for_colorimetry = stream->link->dpcd_caps.dpcd_rev.raw >= 0x14 &&
						      stream->link->dpcd_caps.dprx_feature.bits.VSC_SDP_COLORIMETRY_SUPPORTED &&
						      !disable_colorimetry;

		if (stream->out_transfer_func.tf == TRANSFER_FUNCTION_GAMMA22)
			tf = TRANSFER_FUNC_GAMMA_22;
		mod_build_vsc_infopacket(stream, &stream->vsc_infopacket, stream->output_color_space, tf);
		aconnector->sr_skip_count = AMDGPU_DM_PSR_ENTRY_DELAY;

	}
finish:
	dc_sink_release(sink);

	return stream;
}

/**
 * amdgpu_dm_connector_poll - Poll a connector to see if it's connected to a display
 * @aconnector: DM connector to poll (owns @base drm_connector and @dc_link)
 * @force: if true, force polling even when DAC load detection was used
 *
 * Used for connectors that don't support HPD (hotplug detection) to
 * periodically check whether the connector is connected to a display.
 *
 * When connection was determined via DAC load detection, we avoid
 * re-running it on normal polls to prevent visible glitches, unless
 * @force is set.
 *
 * Return: The probed connector status (connected/disconnected/unknown).
 */
static enum drm_connector_status
amdgpu_dm_connector_poll(struct amdgpu_dm_connector *aconnector, bool force)
{
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dc_link *link = aconnector->dc_link;
	enum dc_connection_type conn_type = dc_connection_none;
	enum drm_connector_status status = connector_status_disconnected;

	/* When we determined the connection using DAC load detection,
	 * do NOT poll the connector do detect disconnect because
	 * that would run DAC load detection again which can cause
	 * visible visual glitches.
	 *
	 * Only allow to poll such a connector again when forcing.
	 */
	if (!force && link->local_sink && link->type == dc_connection_analog_load)
		return connector->status;

	mutex_lock(&aconnector->hpd_lock);

	if (dc_link_detect_connection_type(aconnector->dc_link, &conn_type) &&
	    conn_type != dc_connection_none) {
		mutex_lock(&adev->dm.dc_lock);

		/* Only call full link detection when a sink isn't created yet,
		 * ie. just when the display is plugged in, otherwise we risk flickering.
		 */
		if (link->local_sink ||
			dc_link_detect(link, DETECT_REASON_HPD))
			status = connector_status_connected;

		mutex_unlock(&adev->dm.dc_lock);
	}

	if (connector->status != status) {
		if (status == connector_status_disconnected) {
			if (link->local_sink)
				dc_sink_release(link->local_sink);

			link->local_sink = NULL;
			link->dpcd_sink_count = 0;
			link->type = dc_connection_none;
		}

		amdgpu_dm_update_connector_after_detect(aconnector);
	}

	mutex_unlock(&aconnector->hpd_lock);
	return status;
}

/**
 * amdgpu_dm_connector_detect() - Detect whether a DRM connector is connected to a display
 *
 * A connector is considered connected when it has a sink that is not NULL.
 * For connectors that support HPD (hotplug detection), the connection is
 * handled in the HPD interrupt.
 * For connectors that may not support HPD, such as analog connectors,
 * DRM will call this function repeatedly to poll them.
 *
 * Notes:
 * 1. This interface is NOT called in context of HPD irq.
 * 2. This interface *is called* in context of user-mode ioctl. Which
 *    makes it a bad place for *any* MST-related activity.
 *
 * @connector: The DRM connector we are checking. We convert it to
 *             amdgpu_dm_connector so we can read the DC link and state.
 * @force:     If true, do a full detect again. This is used even when
 *             a lighter check would normally be used to avoid flicker.
 *
 * Return: The connector status (connected, disconnected, or unknown).
 *
 */
static enum drm_connector_status
amdgpu_dm_connector_detect(struct drm_connector *connector, bool force)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);

	update_subconnector_property(aconnector);

	if (aconnector->base.force == DRM_FORCE_ON ||
		aconnector->base.force == DRM_FORCE_ON_DIGITAL)
		return connector_status_connected;
	else if (aconnector->base.force == DRM_FORCE_OFF)
		return connector_status_disconnected;

	/* Poll analog connectors and only when either
	 * disconnected or connected to an analog display.
	 */
	if (drm_kms_helper_is_poll_worker() &&
		dc_connector_supports_analog(aconnector->dc_link->link_id.id) &&
		(!aconnector->dc_sink || aconnector->dc_sink->edid_caps.analog))
		return amdgpu_dm_connector_poll(aconnector, force);

	return (aconnector->dc_sink ? connector_status_connected :
			connector_status_disconnected);
}

int amdgpu_dm_connector_atomic_set_property(struct drm_connector *connector,
					    struct drm_connector_state *connector_state,
					    struct drm_property *property,
					    uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dm_connector_state *dm_old_state =
		to_dm_connector_state(connector->state);
	struct dm_connector_state *dm_new_state =
		to_dm_connector_state(connector_state);

	int ret = -EINVAL;

	if (property == dev->mode_config.scaling_mode_property) {
		enum amdgpu_rmx_type rmx_type;

		switch (val) {
		case DRM_MODE_SCALE_CENTER:
			rmx_type = RMX_CENTER;
			break;
		case DRM_MODE_SCALE_ASPECT:
			rmx_type = RMX_ASPECT;
			break;
		case DRM_MODE_SCALE_FULLSCREEN:
			rmx_type = RMX_FULL;
			break;
		case DRM_MODE_SCALE_NONE:
		default:
			rmx_type = RMX_OFF;
			break;
		}

		if (dm_old_state->scaling == rmx_type)
			return 0;

		dm_new_state->scaling = rmx_type;
		ret = 0;
	} else if (property == adev->mode_info.underscan_hborder_property) {
		dm_new_state->underscan_hborder = val;
		ret = 0;
	} else if (property == adev->mode_info.underscan_vborder_property) {
		dm_new_state->underscan_vborder = val;
		ret = 0;
	} else if (property == adev->mode_info.underscan_property) {
		dm_new_state->underscan_enable = val;
		ret = 0;
	} else if (property == adev->mode_info.abm_level_property) {
		switch (val) {
		case ABM_SYSFS_CONTROL:
			dm_new_state->abm_sysfs_forbidden = false;
			break;
		case ABM_LEVEL_OFF:
			dm_new_state->abm_sysfs_forbidden = true;
			dm_new_state->abm_level = ABM_LEVEL_IMMEDIATE_DISABLE;
			break;
		default:
			dm_new_state->abm_sysfs_forbidden = true;
			dm_new_state->abm_level = val;
		}
		ret = 0;
	}

	return ret;
}
EXPORT_IF_KUNIT(amdgpu_dm_connector_atomic_set_property);

int amdgpu_dm_connector_atomic_get_property(struct drm_connector *connector,
					    const struct drm_connector_state *state,
					    struct drm_property *property,
					    uint64_t *val)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dm_connector_state *dm_state =
		to_dm_connector_state(state);
	int ret = -EINVAL;

	if (property == dev->mode_config.scaling_mode_property) {
		switch (dm_state->scaling) {
		case RMX_CENTER:
			*val = DRM_MODE_SCALE_CENTER;
			break;
		case RMX_ASPECT:
			*val = DRM_MODE_SCALE_ASPECT;
			break;
		case RMX_FULL:
			*val = DRM_MODE_SCALE_FULLSCREEN;
			break;
		case RMX_OFF:
		default:
			*val = DRM_MODE_SCALE_NONE;
			break;
		}
		ret = 0;
	} else if (property == adev->mode_info.underscan_hborder_property) {
		*val = dm_state->underscan_hborder;
		ret = 0;
	} else if (property == adev->mode_info.underscan_vborder_property) {
		*val = dm_state->underscan_vborder;
		ret = 0;
	} else if (property == adev->mode_info.underscan_property) {
		*val = dm_state->underscan_enable;
		ret = 0;
	} else if (property == adev->mode_info.abm_level_property) {
		if (!dm_state->abm_sysfs_forbidden)
			*val = ABM_SYSFS_CONTROL;
		else
			*val = (dm_state->abm_level != ABM_LEVEL_IMMEDIATE_DISABLE) ?
				dm_state->abm_level : 0;
		ret = 0;
	}

	return ret;
}
EXPORT_IF_KUNIT(amdgpu_dm_connector_atomic_get_property);

static void amdgpu_dm_connector_unregister(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector = to_amdgpu_dm_connector(connector);

	if (amdgpu_dm_should_create_sysfs(amdgpu_dm_connector))
		sysfs_remove_group(&connector->kdev->kobj, &amdgpu_group);

	cec_notifier_conn_unregister(amdgpu_dm_connector->notifier);
	drm_dp_aux_unregister(&amdgpu_dm_connector->dm_dp_aux.aux);
}

static void amdgpu_dm_connector_destroy(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct amdgpu_device *adev = drm_to_adev(connector->dev);
	struct amdgpu_display_manager *dm = &adev->dm;

	/*
	 * Call only if mst_mgr was initialized before since it's not done
	 * for all connector types.
	 */
	if (aconnector->mst_mgr.dev)
		drm_dp_mst_topology_mgr_destroy(&aconnector->mst_mgr);

	/* Cancel and flush any pending HDMI HPD debounce work */
	if (aconnector->hdmi_hpd_debounce_delay_ms) {
		cancel_delayed_work_sync(&aconnector->hdmi_hpd_debounce_work);
		if (aconnector->hdmi_prev_sink) {
			dc_sink_release(aconnector->hdmi_prev_sink);
			aconnector->hdmi_prev_sink = NULL;
		}
	}

	if (aconnector->bl_idx != -1) {
		backlight_device_unregister(dm->backlight_dev[aconnector->bl_idx]);
		dm->backlight_dev[aconnector->bl_idx] = NULL;
	}

	if (aconnector->dc_em_sink)
		dc_sink_release(aconnector->dc_em_sink);
	aconnector->dc_em_sink = NULL;
	if (aconnector->dc_sink)
		dc_sink_release(aconnector->dc_sink);
	aconnector->dc_sink = NULL;

	drm_dp_cec_unregister_connector(&aconnector->dm_dp_aux.aux);
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(aconnector->dm_dp_aux.aux.name);

	kfree(connector);
}

void amdgpu_dm_connector_funcs_reset(struct drm_connector *connector)
{
	struct dm_connector_state *old_state =
		to_dm_connector_state(connector->state);
	struct dm_connector_state *state;

	state = kzalloc_obj(*state);
	if (!state)
		return;

	if (connector->state)
		__drm_atomic_helper_connector_destroy_state(connector->state);

	kfree(old_state);

	__drm_atomic_helper_connector_reset(connector, &state->base);

	state->scaling = RMX_OFF;
	state->underscan_enable = false;
	state->underscan_hborder = 0;
	state->underscan_vborder = 0;
	state->base.max_requested_bpc = 8;
	state->vcpi_slots = 0;
	state->pbn = 0;

	if (connector->connector_type == DRM_MODE_CONNECTOR_eDP) {
		if (amdgpu_dm_abm_level <= 0)
			state->abm_level = ABM_LEVEL_IMMEDIATE_DISABLE;
		else
			state->abm_level = amdgpu_dm_abm_level;
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_connector_funcs_reset);

struct drm_connector_state *
amdgpu_dm_connector_atomic_duplicate_state(struct drm_connector *connector)
{
	struct dm_connector_state *state =
		to_dm_connector_state(connector->state);

	struct dm_connector_state *new_state =
			kmemdup(state, sizeof(*state), GFP_KERNEL);

	if (!new_state)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector, &new_state->base);

	new_state->freesync_capable = state->freesync_capable;
	new_state->abm_level = state->abm_level;
	new_state->scaling = state->scaling;
	new_state->underscan_enable = state->underscan_enable;
	new_state->underscan_hborder = state->underscan_hborder;
	new_state->underscan_vborder = state->underscan_vborder;
	new_state->vcpi_slots = state->vcpi_slots;
	new_state->pbn = state->pbn;
	return &new_state->base;
}
EXPORT_IF_KUNIT(amdgpu_dm_connector_atomic_duplicate_state);

static int
amdgpu_dm_connector_late_register(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
		to_amdgpu_dm_connector(connector);
	int r;

	if (amdgpu_dm_should_create_sysfs(amdgpu_dm_connector)) {
		r = sysfs_create_group(&connector->kdev->kobj,
				       &amdgpu_group);
		if (r)
			return r;
	}

	amdgpu_dm_register_backlight_device(amdgpu_dm_connector);

	if ((connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort) ||
	    (connector->connector_type == DRM_MODE_CONNECTOR_eDP)) {
		amdgpu_dm_connector->dm_dp_aux.aux.dev = connector->kdev;
		r = drm_dp_aux_register(&amdgpu_dm_connector->dm_dp_aux.aux);
		if (r)
			return r;
	}

#if defined(CONFIG_DEBUG_FS)
	connector_debugfs_init(amdgpu_dm_connector);
#endif

	return 0;
}

static void amdgpu_dm_connector_funcs_force(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct dc_link *dc_link = aconnector->dc_link;
	struct dc_sink *dc_em_sink = aconnector->dc_em_sink;
	const struct drm_edid *drm_edid;
	struct i2c_adapter *ddc;
	struct drm_device *dev = connector->dev;

	if (dc_link && dc_link->aux_mode)
		ddc = &aconnector->dm_dp_aux.aux.ddc;
	else
		ddc = &aconnector->i2c->base;

	drm_edid = drm_edid_read_ddc(connector, ddc);
	drm_edid_connector_update(connector, drm_edid);
	if (!drm_edid) {
		drm_err(dev, "No EDID found on connector: %s.\n", connector->name);
		return;
	}

	aconnector->drm_edid = drm_edid;
	/* Update emulated (virtual) sink's EDID */
	if (dc_em_sink && dc_link) {
		/* FIXME: Get rid of drm_edid_raw() */
		const struct edid *edid = drm_edid_raw(drm_edid);

		memset(&dc_em_sink->edid_caps, 0, sizeof(struct dc_edid_caps));
		memmove(dc_em_sink->dc_edid.raw_edid, edid,
			(edid->extensions + 1) * EDID_LENGTH);
		dm_helpers_parse_edid_caps(
			dc_link,
			&dc_em_sink->dc_edid,
			&dc_em_sink->edid_caps);
	}
}

static const struct drm_connector_funcs amdgpu_dm_connector_funcs = {
	.reset = amdgpu_dm_connector_funcs_reset,
	.detect = amdgpu_dm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = amdgpu_dm_connector_destroy,
	.atomic_duplicate_state = amdgpu_dm_connector_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = amdgpu_dm_connector_atomic_set_property,
	.atomic_get_property = amdgpu_dm_connector_atomic_get_property,
	.late_register = amdgpu_dm_connector_late_register,
	.early_unregister = amdgpu_dm_connector_unregister,
	.force = amdgpu_dm_connector_funcs_force
};

static int get_modes(struct drm_connector *connector)
{
	return amdgpu_dm_connector_get_modes(connector);
}

static void create_eml_sink(struct amdgpu_dm_connector *aconnector)
{
	struct drm_connector *connector = &aconnector->base;
	struct dc_link *dc_link = aconnector->dc_link;
	struct dc_sink_init_data init_params = {
			.link = aconnector->dc_link,
			.sink_signal = SIGNAL_TYPE_VIRTUAL
	};
	const struct drm_edid *drm_edid;
	const struct edid *edid;
	struct i2c_adapter *ddc;

	if (dc_link && dc_link->aux_mode)
		ddc = &aconnector->dm_dp_aux.aux.ddc;
	else
		ddc = &aconnector->i2c->base;

	drm_edid = drm_edid_read_ddc(connector, ddc);
	drm_edid_connector_update(connector, drm_edid);
	if (!drm_edid) {
		drm_err(connector->dev, "No EDID found on connector: %s.\n", connector->name);
		return;
	}

	if (connector->display_info.is_hdmi)
		init_params.sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;

	aconnector->drm_edid = drm_edid;

	/* FIXME: Get rid of drm_edid_raw() */
	edid = drm_edid_raw(drm_edid);
	aconnector->dc_em_sink = dc_link_add_remote_sink(
		aconnector->dc_link,
		(uint8_t *)edid,
		(edid->extensions + 1) * EDID_LENGTH,
		&init_params);

	if (aconnector->base.force == DRM_FORCE_ON) {
		aconnector->dc_sink = aconnector->dc_link->local_sink ?
		aconnector->dc_link->local_sink :
		aconnector->dc_em_sink;
		if (aconnector->dc_sink)
			dc_sink_retain(aconnector->dc_sink);
	}
}

static void handle_edid_mgmt(struct amdgpu_dm_connector *aconnector)
{
	struct dc_link *link = (struct dc_link *)aconnector->dc_link;

	/*
	 * In case of headless boot with force on for DP managed connector
	 * Those settings have to be != 0 to get initial modeset
	 */
	if (link->connector_signal == SIGNAL_TYPE_DISPLAY_PORT) {
		link->verified_link_cap.lane_count = LANE_COUNT_FOUR;
		link->verified_link_cap.link_rate = LINK_RATE_HIGH2;
	}

	create_eml_sink(aconnector);
}

static enum dc_status dm_validate_stream_and_context(struct dc *dc,
						struct dc_stream_state *stream)
{
	enum dc_status dc_result = DC_ERROR_UNEXPECTED;
	struct dc_plane_state *dc_plane_state = NULL;
	struct dc_state *dc_state = NULL;

	if (!stream)
		goto cleanup;

	dc_plane_state = dc_create_plane_state(dc);
	if (!dc_plane_state)
		goto cleanup;

	dc_state = dc_state_create(dc, NULL);
	if (!dc_state)
		goto cleanup;

	/* populate stream to plane */
	dc_plane_state->src_rect.height  = stream->src.height;
	dc_plane_state->src_rect.width   = stream->src.width;
	dc_plane_state->dst_rect.height  = stream->src.height;
	dc_plane_state->dst_rect.width   = stream->src.width;
	dc_plane_state->clip_rect.height = stream->src.height;
	dc_plane_state->clip_rect.width  = stream->src.width;
	dc_plane_state->plane_size.surface_pitch = ((stream->src.width + 255) / 256) * 256;
	dc_plane_state->plane_size.surface_size.height = stream->src.height;
	dc_plane_state->plane_size.surface_size.width  = stream->src.width;
	dc_plane_state->plane_size.chroma_size.height  = stream->src.height;
	dc_plane_state->plane_size.chroma_size.width   = stream->src.width;
	dc_plane_state->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB8888;
	dc_plane_state->tiling_info.gfx9.swizzle = DC_SW_UNKNOWN;
	dc_plane_state->rotation = ROTATION_ANGLE_0;
	dc_plane_state->is_tiling_rotated = false;
	dc_plane_state->tiling_info.gfx8.array_mode = DC_ARRAY_LINEAR_GENERAL;

	dc_result = dc_validate_stream(dc, stream);
	if (dc_result == DC_OK)
		dc_result = dc_validate_plane(dc, dc_plane_state);

	if (dc_result == DC_OK)
		dc_result = dc_state_add_stream(dc, dc_state, stream);

	if (dc_result == DC_OK && !dc_state_add_plane(
						dc,
						stream,
						dc_plane_state,
						dc_state))
		dc_result = DC_FAIL_ATTACH_SURFACES;

	if (dc_result == DC_OK)
		dc_result = dc_validate_global_state(dc, dc_state, DC_VALIDATE_MODE_ONLY);

cleanup:
	if (dc_state)
		dc_state_release(dc_state);

	if (dc_plane_state)
		dc_plane_state_release(dc_plane_state);

	return dc_result;
}

static enum dc_status
dm_validate_stream_color_format(const struct drm_connector_state *drm_state,
				const struct dc_stream_state *stream)
{
	enum dc_pixel_encoding encoding;

	if (!drm_state->color_format)
		return DC_OK;

	switch (drm_state->color_format) {
	case DRM_CONNECTOR_COLOR_FORMAT_AUTO:
	case DRM_CONNECTOR_COLOR_FORMAT_RGB444:
		encoding = PIXEL_ENCODING_RGB;
		break;
	case DRM_CONNECTOR_COLOR_FORMAT_YCBCR444:
		encoding = PIXEL_ENCODING_YCBCR444;
		break;
	case DRM_CONNECTOR_COLOR_FORMAT_YCBCR422:
		encoding = PIXEL_ENCODING_YCBCR422;
		break;
	case DRM_CONNECTOR_COLOR_FORMAT_YCBCR420:
		encoding = PIXEL_ENCODING_YCBCR420;
		break;
	default:
		encoding = PIXEL_ENCODING_UNDEFINED;
		break;
	}

	return encoding == stream->timing.pixel_encoding ?
		DC_OK : DC_UNSUPPORTED_VALUE;
}

struct dc_stream_state *
amdgpu_dm_create_validate_stream_for_sink(struct drm_connector *connector,
					  const struct drm_display_mode *drm_mode,
					  const struct dm_connector_state *dm_state,
					  const struct dc_stream_state *old_stream)
{
	struct amdgpu_dm_connector *aconnector = NULL;
	struct amdgpu_device *adev = drm_to_adev(connector->dev);
	struct dc_stream_state *stream;
	const struct drm_connector_state *drm_state = dm_state ? &dm_state->base : NULL;
	int requested_bpc = drm_state ? drm_state->max_requested_bpc : 8;
	enum dc_status dc_result = DC_OK;
	uint8_t bpc_limit = 6;

	if (!dm_state)
		return NULL;

	if (connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK)
		aconnector = to_amdgpu_dm_connector(connector);

	if (aconnector &&
	    (aconnector->dc_link->connector_signal == SIGNAL_TYPE_HDMI_TYPE_A ||
	     aconnector->dc_link->connector_signal == SIGNAL_TYPE_HDMI_FRL ||
	     aconnector->dc_link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER))
		bpc_limit = 8;

	do {
		drm_dbg_kms(connector->dev, "Trying with %d bpc\n", requested_bpc);
		stream = create_stream_for_sink(connector, drm_mode,
						dm_state, old_stream,
						requested_bpc);
		if (stream == NULL) {
			drm_err(adev_to_drm(adev), "Failed to create stream for sink!\n");
			break;
		}

		dc_result = dc_validate_stream(adev->dm.dc, stream);

		if (!aconnector) /* writeback connector */
			return stream;

		if (dc_result == DC_OK && stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
			dc_result = dm_dp_mst_is_port_support_mode(aconnector, stream);

		if (dc_result == DC_OK)
			dc_result = dm_validate_stream_and_context(adev->dm.dc, stream);

		if (dc_result == DC_OK)
			dc_result = dm_validate_stream_color_format(drm_state, stream);

		if (dc_result != DC_OK) {
			drm_dbg_kms(connector->dev, "Pruned mode %d x %d (clk %d) %s %s -- %s\n",
				      drm_mode->hdisplay,
				      drm_mode->vdisplay,
				      drm_mode->clock,
				      dc_pixel_encoding_to_str(stream->timing.pixel_encoding),
				      dc_color_depth_to_str(stream->timing.display_color_depth),
				      dc_status_to_str(dc_result));

			dc_stream_release(stream);
			stream = NULL;
			requested_bpc -= 2; /* lower bpc to retry validation */
		}

	} while (stream == NULL && requested_bpc >= bpc_limit);

	switch (dc_result) {
	/*
	 * If we failed to validate DP bandwidth stream with the requested RGB color depth,
	 * we try to fallback and configure in order:
	 * YUV422 (8bpc, 6bpc)
	 * YUV420 (8bpc, 6bpc)
	 */
	case DC_FAIL_ENC_VALIDATE:
	case DC_EXCEED_DONGLE_CAP:
	case DC_NO_DP_LINK_BANDWIDTH:
		/* recursively entered twice and already tried both YUV422 and YUV420 */
		if (aconnector->force_yuv422_output && aconnector->force_yuv420_output)
			break;
		/* first failure; try YUV422 */
		if (!aconnector->force_yuv422_output) {
			drm_dbg_kms(connector->dev, "%s:%d Validation failed with %d, retrying w/ YUV422\n",
				    __func__, __LINE__, dc_result);
			aconnector->force_yuv422_output = true;
		/* recursively entered and YUV422 failed, try YUV420 */
		} else if (!aconnector->force_yuv420_output) {
			drm_dbg_kms(connector->dev, "%s:%d Validation failed with %d, retrying w/ YUV420\n",
				    __func__, __LINE__, dc_result);
			aconnector->force_yuv420_output = true;
		}
		stream = amdgpu_dm_create_validate_stream_for_sink(connector, drm_mode,
							 dm_state, old_stream);
		aconnector->force_yuv422_output = false;
		aconnector->force_yuv420_output = false;
		break;
	case DC_OK:
		break;
	default:
		drm_dbg_kms(connector->dev, "%s:%d Unhandled validation failure %d\n",
			    __func__, __LINE__, dc_result);
		break;
	}

	return stream;
}

enum drm_mode_status amdgpu_dm_connector_mode_valid(struct drm_connector *connector,
				   const struct drm_display_mode *mode)
{
	int result = MODE_ERROR;
	struct dc_sink *dc_sink;
	struct drm_display_mode *test_mode;
	/* TODO: Unhardcode stream count */
	struct dc_stream_state *stream;
	/* we always have an amdgpu_dm_connector here since we got
	 * here via the amdgpu_dm_connector_helper_funcs
	 */
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) ||
			(mode->flags & DRM_MODE_FLAG_DBLSCAN))
		return result;

	/*
	 * Only run this the first time mode_valid is called to initilialize
	 * EDID mgmt
	 */
	if (aconnector->base.force != DRM_FORCE_UNSPECIFIED &&
		!aconnector->dc_em_sink)
		handle_edid_mgmt(aconnector);

	dc_sink = to_amdgpu_dm_connector(connector)->dc_sink;

	if (dc_sink == NULL && aconnector->base.force != DRM_FORCE_ON_DIGITAL &&
				aconnector->base.force != DRM_FORCE_ON) {
		drm_err(connector->dev, "dc_sink is NULL!\n");
		goto fail;
	}

	test_mode = drm_mode_duplicate(connector->dev, mode);
	if (!test_mode)
		goto fail;

	drm_mode_set_crtcinfo(test_mode, 0);

	stream = amdgpu_dm_create_validate_stream_for_sink(connector, test_mode,
						 to_dm_connector_state(connector->state),
						 NULL);
	drm_mode_destroy(connector->dev, test_mode);
	if (stream) {
		dc_stream_release(stream);
		result = MODE_OK;
	}

fail:
	/* TODO: error handling*/
	return result;
}

int amdgpu_dm_fill_hdr_info_packet(const struct drm_connector_state *state,
				   struct dc_info_packet *out)
{
	struct hdmi_drm_infoframe frame;
	unsigned char buf[30]; /* 26 + 4 */
	ssize_t len;
	int ret, i;

	memset(out, 0, sizeof(*out));

	if (!state->hdr_output_metadata)
		return 0;

	ret = drm_hdmi_infoframe_set_hdr_metadata(&frame, state);
	if (ret)
		return ret;

	len = hdmi_drm_infoframe_pack_only(&frame, buf, sizeof(buf));
	if (len < 0)
		return (int)len;

	/* Static metadata is a fixed 26 bytes + 4 byte header. */
	if (len != 30)
		return -EINVAL;

	/* Prepare the infopacket for DC. */
	switch (state->connector->connector_type) {
	case DRM_MODE_CONNECTOR_HDMIA:
		out->hb0 = 0x87; /* type */
		out->hb1 = 0x01; /* version */
		out->hb2 = 0x1A; /* length */
		out->sb[0] = buf[3]; /* checksum */
		i = 1;
		break;

	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_eDP:
		out->hb0 = 0x00; /* sdp id, zero */
		out->hb1 = 0x87; /* type */
		out->hb2 = 0x1D; /* payload len - 1 */
		out->hb3 = (0x13 << 2); /* sdp version */
		out->sb[0] = 0x01; /* version */
		out->sb[1] = 0x1A; /* length */
		i = 2;
		break;

	default:
		return -EINVAL;
	}

	memcpy(&out->sb[i], &buf[4], 26);
	out->valid = true;

	print_hex_dump(KERN_DEBUG, "HDR SB:", DUMP_PREFIX_NONE, 16, 1, out->sb,
		       sizeof(out->sb), false);

	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_fill_hdr_info_packet);

static int
amdgpu_dm_connector_atomic_check(struct drm_connector *conn,
				 struct drm_atomic_commit *state)
{
	struct drm_connector_state *new_con_state =
		drm_atomic_get_new_connector_state(state, conn);
	struct drm_connector_state *old_con_state =
		drm_atomic_get_old_connector_state(state, conn);
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct amdgpu_dm_connector *aconn = to_amdgpu_dm_connector(conn);
	int ret;

	if (WARN_ON(unlikely(!old_con_state || !new_con_state)))
		return -EINVAL;

	trace_amdgpu_dm_connector_atomic_check(new_con_state);

	if (conn->connector_type == DRM_MODE_CONNECTOR_DisplayPort) {
		ret = drm_dp_mst_root_conn_atomic_check(new_con_state, &aconn->mst_mgr);
		if (ret < 0)
			return ret;
	}

	crtc = new_con_state->crtc;
	if (!crtc)
		return 0;

	if (new_con_state->privacy_screen_sw_state != old_con_state->privacy_screen_sw_state) {
		new_crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(new_crtc_state))
			return PTR_ERR(new_crtc_state);

		new_crtc_state->mode_changed = true;
	}

	if (new_con_state->colorspace != old_con_state->colorspace) {
		new_crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(new_crtc_state))
			return PTR_ERR(new_crtc_state);

		new_crtc_state->mode_changed = true;
	}

	if (new_con_state->content_type != old_con_state->content_type) {
		new_crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(new_crtc_state))
			return PTR_ERR(new_crtc_state);

		new_crtc_state->mode_changed = true;
	}

	if (!drm_connector_atomic_hdr_metadata_equal(old_con_state, new_con_state)) {
		struct dc_info_packet hdr_infopacket;

		ret = amdgpu_dm_fill_hdr_info_packet(new_con_state, &hdr_infopacket);
		if (ret)
			return ret;

		new_crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(new_crtc_state))
			return PTR_ERR(new_crtc_state);

		/*
		 * DC considers the stream backends changed if the
		 * static metadata changes. Forcing the modeset also
		 * gives a simple way for userspace to switch from
		 * 8bpc to 10bpc when setting the metadata to enter
		 * or exit HDR.
		 *
		 * Changing the static metadata after it's been
		 * set is permissible, however. So only force a
		 * modeset if we're entering or exiting HDR.
		 */
		new_crtc_state->mode_changed = new_crtc_state->mode_changed ||
			!old_con_state->hdr_output_metadata ||
			!new_con_state->hdr_output_metadata;
	}

	return 0;
}

static const struct drm_connector_helper_funcs
amdgpu_dm_connector_helper_funcs = {
	/*
	 * If hotplugging a second bigger display in FB Con mode, bigger resolution
	 * modes will be filtered by drm_mode_validate_size(), and those modes
	 * are missing after user start lightdm. So we need to renew modes list.
	 * in get_modes call back, not just return the modes count
	 */
	.get_modes = get_modes,
	.mode_valid = amdgpu_dm_connector_mode_valid,
	.atomic_check = amdgpu_dm_connector_atomic_check,
};

int amdgpu_dm_convert_dc_color_depth_into_bpc(enum dc_color_depth display_color_depth)
{
	switch (display_color_depth) {
	case COLOR_DEPTH_666:
		return 6;
	case COLOR_DEPTH_888:
		return 8;
	case COLOR_DEPTH_101010:
		return 10;
	case COLOR_DEPTH_121212:
		return 12;
	case COLOR_DEPTH_141414:
		return 14;
	case COLOR_DEPTH_161616:
		return 16;
	default:
		break;
	}
	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_convert_dc_color_depth_into_bpc);

STATIC_IFN_KUNIT int to_drm_connector_type(enum signal_type st, uint32_t connector_id)
{
	switch (st) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		return DRM_MODE_CONNECTOR_HDMIA;
	case SIGNAL_TYPE_EDP:
		return DRM_MODE_CONNECTOR_eDP;
	case SIGNAL_TYPE_LVDS:
		return DRM_MODE_CONNECTOR_LVDS;
	case SIGNAL_TYPE_RGB:
		return DRM_MODE_CONNECTOR_VGA;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		/* External DP bridges have a different connector type. */
		if (connector_id == CONNECTOR_ID_VGA)
			return DRM_MODE_CONNECTOR_VGA;
		else if (connector_id == CONNECTOR_ID_LVDS)
			return DRM_MODE_CONNECTOR_LVDS;

		return DRM_MODE_CONNECTOR_DisplayPort;
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
		if (connector_id == CONNECTOR_ID_SINGLE_LINK_DVII ||
			connector_id == CONNECTOR_ID_DUAL_LINK_DVII)
			return DRM_MODE_CONNECTOR_DVII;

		return DRM_MODE_CONNECTOR_DVID;
	case SIGNAL_TYPE_VIRTUAL:
		return DRM_MODE_CONNECTOR_VIRTUAL;

	default:
		return DRM_MODE_CONNECTOR_Unknown;
	}
}
EXPORT_IF_KUNIT(to_drm_connector_type);

static struct drm_encoder *amdgpu_dm_connector_to_encoder(struct drm_connector *connector)
{
	struct drm_encoder *encoder;

	/* There is only one encoder per connector */
	drm_connector_for_each_possible_encoder(connector, encoder)
		return encoder;

	return NULL;
}

static void amdgpu_dm_get_native_mode(struct drm_connector *connector)
{
	struct drm_encoder *encoder;
	struct amdgpu_encoder *amdgpu_encoder;

	encoder = amdgpu_dm_connector_to_encoder(connector);

	if (encoder == NULL)
		return;

	amdgpu_encoder = to_amdgpu_encoder(encoder);

	amdgpu_encoder->native_mode.clock = 0;

	if (!list_empty(&connector->probed_modes)) {
		struct drm_display_mode *preferred_mode = NULL;

		list_for_each_entry(preferred_mode,
				    &connector->probed_modes,
				    head) {
			if (preferred_mode->type & DRM_MODE_TYPE_PREFERRED)
				amdgpu_encoder->native_mode = *preferred_mode;

			break;
		}

	}
}

static struct drm_display_mode *
amdgpu_dm_create_common_mode(struct drm_encoder *encoder,
			     const char *name,
			     int hdisplay, int vdisplay)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *native_mode = &amdgpu_encoder->native_mode;

	mode = drm_mode_duplicate(dev, native_mode);

	if (mode == NULL)
		return NULL;

	mode->hdisplay = hdisplay;
	mode->vdisplay = vdisplay;
	mode->type &= ~DRM_MODE_TYPE_PREFERRED;
	strscpy(mode->name, name, DRM_DISPLAY_MODE_LEN);

	return mode;

}

static const struct amdgpu_dm_mode_size {
	char name[DRM_DISPLAY_MODE_LEN];
	int w;
	int h;
} common_modes[] = {
	{  "640x480",  640,  480},
	{  "800x600",  800,  600},
	{ "1024x768", 1024,  768},
	{ "1280x720", 1280,  720},
	{ "1280x800", 1280,  800},
	{"1280x1024", 1280, 1024},
	{ "1440x900", 1440,  900},
	{"1680x1050", 1680, 1050},
	{"1600x1200", 1600, 1200},
	{"1920x1080", 1920, 1080},
	{"1920x1200", 1920, 1200}
};

static void amdgpu_dm_connector_add_common_modes(struct drm_encoder *encoder,
						 struct drm_connector *connector)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *native_mode = &amdgpu_encoder->native_mode;
	struct amdgpu_dm_connector *amdgpu_dm_connector =
				to_amdgpu_dm_connector(connector);
	int i;
	int n;

	if ((connector->connector_type != DRM_MODE_CONNECTOR_eDP) &&
	    (connector->connector_type != DRM_MODE_CONNECTOR_LVDS))
		return;

	n = ARRAY_SIZE(common_modes);

	for (i = 0; i < n; i++) {
		struct drm_display_mode *curmode = NULL;
		bool mode_existed = false;

		if (common_modes[i].w > native_mode->hdisplay ||
		    common_modes[i].h > native_mode->vdisplay ||
		   (common_modes[i].w == native_mode->hdisplay &&
		    common_modes[i].h == native_mode->vdisplay))
			continue;

		list_for_each_entry(curmode, &connector->probed_modes, head) {
			if (common_modes[i].w == curmode->hdisplay &&
			    common_modes[i].h == curmode->vdisplay) {
				mode_existed = true;
				break;
			}
		}

		if (mode_existed)
			continue;

		mode = amdgpu_dm_create_common_mode(encoder,
				common_modes[i].name, common_modes[i].w,
				common_modes[i].h);
		if (!mode)
			continue;

		drm_mode_probed_add(connector, mode);
		amdgpu_dm_connector->num_modes++;
	}
}

void amdgpu_set_panel_orientation(struct drm_connector *connector)
{
	struct drm_encoder *encoder;
	struct amdgpu_encoder *amdgpu_encoder;
	const struct drm_display_mode *native_mode;

	if (connector->connector_type != DRM_MODE_CONNECTOR_eDP &&
	    connector->connector_type != DRM_MODE_CONNECTOR_LVDS)
		return;

	mutex_lock(&connector->dev->mode_config.mutex);
	amdgpu_dm_connector_get_modes(connector);
	mutex_unlock(&connector->dev->mode_config.mutex);

	encoder = amdgpu_dm_connector_to_encoder(connector);
	if (!encoder)
		return;

	amdgpu_encoder = to_amdgpu_encoder(encoder);

	native_mode = &amdgpu_encoder->native_mode;
	if (native_mode->hdisplay == 0 || native_mode->vdisplay == 0)
		return;

	drm_connector_set_panel_orientation_with_quirk(connector,
						       DRM_MODE_PANEL_ORIENTATION_UNKNOWN,
						       native_mode->hdisplay,
						       native_mode->vdisplay);
}

static void amdgpu_dm_connector_ddc_get_modes(struct drm_connector *connector,
					      const struct drm_edid *drm_edid)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);

	if (drm_edid) {
		/* empty probed_modes */
		INIT_LIST_HEAD(&connector->probed_modes);
		amdgpu_dm_connector->num_modes =
				drm_edid_connector_add_modes(connector);

		/* sorting the probed modes before calling function
		 * amdgpu_dm_get_native_mode() since EDID can have
		 * more than one preferred mode. The modes that are
		 * later in the probed mode list could be of higher
		 * and preferred resolution. For example, 3840x2160
		 * resolution in base EDID preferred timing and 4096x2160
		 * preferred resolution in DID extension block later.
		 */
		drm_mode_sort(&connector->probed_modes);
		amdgpu_dm_get_native_mode(connector);

		/* Freesync capabilities are reset by calling
		 * drm_edid_connector_add_modes() and need to be
		 * restored here.
		 */
		amdgpu_dm_update_freesync_caps(connector, drm_edid, false);
	} else {
		amdgpu_dm_connector->num_modes = 0;
	}
}

STATIC_IFN_KUNIT bool is_duplicate_mode(struct amdgpu_dm_connector *aconnector,
			      struct drm_display_mode *mode)
{
	struct drm_display_mode *m;

	list_for_each_entry(m, &aconnector->base.probed_modes, head) {
		if (drm_mode_equal(m, mode))
			return true;
	}

	return false;
}
EXPORT_IF_KUNIT(is_duplicate_mode);

static uint add_fs_modes(struct amdgpu_dm_connector *aconnector)
{
	const struct drm_display_mode *m;
	struct drm_display_mode *new_mode;
	uint i;
	u32 new_modes_count = 0;

	/* Standard FPS values
	 *
	 * 23.976       - TV/NTSC
	 * 24           - Cinema
	 * 25           - TV/PAL
	 * 29.97        - TV/NTSC
	 * 30           - TV/NTSC
	 * 48           - Cinema HFR
	 * 50           - TV/PAL
	 * 60           - Commonly used
	 * 48,72,96,120 - Multiples of 24
	 */
	static const u32 common_rates[] = {
		23976, 24000, 25000, 29970, 30000,
		48000, 50000, 60000, 72000, 96000, 120000
	};

	/*
	 * Find mode with highest refresh rate with the same resolution
	 * as the preferred mode. Some monitors report a preferred mode
	 * with lower resolution than the highest refresh rate supported.
	 */

	m = amdgpu_dm_get_highest_refresh_rate_mode(aconnector, true);
	if (!m)
		return 0;

	for (i = 0; i < ARRAY_SIZE(common_rates); i++) {
		u64 target_vtotal, target_vtotal_diff;
		u64 num, den;

		if (drm_mode_vrefresh(m) * 1000 < common_rates[i])
			continue;

		if (common_rates[i] < aconnector->min_vfreq * 1000 ||
		    common_rates[i] > aconnector->max_vfreq * 1000)
			continue;

		num = (unsigned long long)m->clock * 1000 * 1000;
		den = common_rates[i] * (unsigned long long)m->htotal;
		target_vtotal = div_u64(num, den);
		target_vtotal_diff = target_vtotal - m->vtotal;

		/* Check for illegal modes */
		if (m->vsync_start + target_vtotal_diff < m->vdisplay ||
		    m->vsync_end + target_vtotal_diff < m->vsync_start ||
		    m->vtotal + target_vtotal_diff < m->vsync_end)
			continue;

		new_mode = drm_mode_duplicate(aconnector->base.dev, m);
		if (!new_mode)
			goto out;

		new_mode->vtotal += (u16)target_vtotal_diff;
		new_mode->vsync_start += (u16)target_vtotal_diff;
		new_mode->vsync_end += (u16)target_vtotal_diff;
		new_mode->type &= ~DRM_MODE_TYPE_PREFERRED;
		new_mode->type |= DRM_MODE_TYPE_DRIVER;

		if (!is_duplicate_mode(aconnector, new_mode)) {
			drm_mode_probed_add(&aconnector->base, new_mode);
			new_modes_count += 1;
		} else
			drm_mode_destroy(aconnector->base.dev, new_mode);
	}
 out:
	return new_modes_count;
}

static void amdgpu_dm_connector_add_freesync_modes(struct drm_connector *connector,
						   const struct drm_edid *drm_edid)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
		to_amdgpu_dm_connector(connector);

	if (!(amdgpu_freesync_vid_mode && drm_edid))
		return;

	if (!amdgpu_dm_connector->dc_sink || !amdgpu_dm_connector->dc_link)
		return;

	if (!dc_supports_vrr(amdgpu_dm_connector->dc_sink->ctx->dce_version))
		return;

	if (dc_connector_supports_analog(amdgpu_dm_connector->dc_link->link_id.id) &&
	    amdgpu_dm_connector->dc_sink->edid_caps.analog)
		return;

	if (amdgpu_dm_connector->max_vfreq - amdgpu_dm_connector->min_vfreq > 10)
		amdgpu_dm_connector->num_modes +=
			add_fs_modes(amdgpu_dm_connector);
}

static int amdgpu_dm_connector_get_modes(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);
	struct dc_link *dc_link = amdgpu_dm_connector->dc_link;
	struct drm_encoder *encoder;
	const struct drm_edid *drm_edid = amdgpu_dm_connector->drm_edid;
	struct dc_link_settings *verified_link_cap = &dc_link->verified_link_cap;
	const struct dc *dc = dc_link->dc;

	encoder = amdgpu_dm_connector_to_encoder(connector);

	if (!drm_edid) {
		amdgpu_dm_connector->num_modes =
				drm_add_modes_noedid(connector, 640, 480);
		if (dc->link_srv->dp_get_encoding_format(verified_link_cap) == DP_128b_132b_ENCODING)
			amdgpu_dm_connector->num_modes +=
				drm_add_modes_noedid(connector, 1920, 1080);

		if (amdgpu_dm_connector->dc_sink &&
		    amdgpu_dm_connector->dc_sink->edid_caps.analog &&
		    dc_connector_supports_analog(dc_link->link_id.id)) {
			/* Analog monitor connected by DAC load detection.
			 * Add common modes. It will be up to the user to select one that works.
			 */
			for (int i = 0; i < ARRAY_SIZE(common_modes); i++)
				amdgpu_dm_connector->num_modes += drm_add_modes_noedid(
					connector, common_modes[i].w, common_modes[i].h);
		}
	} else {
		amdgpu_dm_connector_ddc_get_modes(connector, drm_edid);
		if (encoder)
			amdgpu_dm_connector_add_common_modes(encoder, connector);
		amdgpu_dm_connector_add_freesync_modes(connector, drm_edid);
	}
	amdgpu_dm_fbc_init(connector);

	return amdgpu_dm_connector->num_modes;
}

static const u32 supported_colorspaces =
	BIT(DRM_MODE_COLORIMETRY_BT709_YCC) |
	BIT(DRM_MODE_COLORIMETRY_OPRGB) |
	BIT(DRM_MODE_COLORIMETRY_BT2020_RGB) |
	BIT(DRM_MODE_COLORIMETRY_BT2020_YCC);

static const u32 supported_colorformats =
	BIT(DRM_OUTPUT_COLOR_FORMAT_RGB444) |
	BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR444) |
	BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR422) |
	BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR420);

static void hdmi_frl_status_polling_work(struct work_struct *work)
{
	struct amdgpu_display_manager *dm =
		container_of(to_delayed_work(work), struct amdgpu_display_manager,
				hdmi_frl_status_polling_work);
	struct dc *dc = dm->dc;
	struct dc_link *dc_link;
	bool link_update = false;

	for (int i = 0; i < MAX_LINKS; i++) {
		dc_link = dc->links[i];


		if (!dc_link || !dc_link->local_sink)
			continue;

		if (!dc_is_hdmi_signal(dc_link->connector_signal))
			continue;

		if (dc_link->connector_signal != SIGNAL_TYPE_HDMI_FRL)
			continue;

		link_update = dc_link_frl_poll_status_flag(dc_link);
		if (link_update) {
			mutex_lock(&dm->dc_lock);
			dc_link_detect(dc_link, DETECT_REASON_RETRAIN);
			mutex_unlock(&dm->dc_lock);
		}
	}

	queue_delayed_work(dm->hdmi_frl_status_polling_wq,
			   &dm->hdmi_frl_status_polling_work,
			   msecs_to_jiffies(dm->hdmi_frl_status_polling_delay_ms));
}

void amdgpu_dm_connector_init_helper(struct amdgpu_display_manager *dm,
				     struct amdgpu_dm_connector *aconnector,
				     int connector_type,
				     struct dc_link *link,
				     int link_index)
{
	struct amdgpu_device *adev = drm_to_adev(dm->ddev);

	/*
	 * Some of the properties below require access to state, like bpc.
	 * Allocate some default initial connector state with our reset helper.
	 */
	if (aconnector->base.funcs->reset)
		aconnector->base.funcs->reset(&aconnector->base);

	aconnector->connector_id = link_index;
	aconnector->bl_idx = -1;
	aconnector->dc_link = link;
	aconnector->base.interlace_allowed = false;
	aconnector->base.doublescan_allowed = false;
	aconnector->base.stereo_allowed = false;
	aconnector->base.dpms = DRM_MODE_DPMS_OFF;
	aconnector->hpd.hpd = AMDGPU_HPD_NONE; /* not used */
	aconnector->audio_inst = -1;
	aconnector->pack_sdp_v1_3 = false;
	aconnector->as_type = ADAPTIVE_SYNC_TYPE_NONE;
	memset(&aconnector->vsdb_info, 0, sizeof(aconnector->vsdb_info));
	mutex_init(&aconnector->hpd_lock);
	mutex_init(&aconnector->handle_mst_msg_ready);

	/*
	 * If HDMI HPD debounce delay is set, use the minimum between selected
	 * value and AMDGPU_DM_MAX_HDMI_HPD_DEBOUNCE_MS
	 */
	if (amdgpu_hdmi_hpd_debounce_delay_ms) {
		aconnector->hdmi_hpd_debounce_delay_ms = min(amdgpu_hdmi_hpd_debounce_delay_ms,
							     AMDGPU_DM_MAX_HDMI_HPD_DEBOUNCE_MS);
		INIT_DELAYED_WORK(&aconnector->hdmi_hpd_debounce_work, amdgpu_dm_hdmi_hpd_debounce_work);
		aconnector->hdmi_prev_sink = NULL;
	} else {
		aconnector->hdmi_hpd_debounce_delay_ms = 0;
	}

	dm->hdmi_frl_status_polling_delay_ms = 200;
	INIT_DELAYED_WORK(&dm->hdmi_frl_status_polling_work, hdmi_frl_status_polling_work);
	/*
	 * configure support HPD hot plug connector_>polled default value is 0
	 * which means HPD hot plug not supported
	 */
	switch (connector_type) {
	case DRM_MODE_CONNECTOR_HDMIA:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		aconnector->base.ycbcr_420_allowed =
			link->link_enc->features.hdmi_ycbcr420_supported ? true : false;
		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		link->link_enc = link_enc_cfg_get_link_enc(link);
		ASSERT(link->link_enc);
		if (link->link_enc)
			aconnector->base.ycbcr_420_allowed =
			link->link_enc->features.dp_ycbcr420_supported ? true : false;
		break;
	case DRM_MODE_CONNECTOR_DVID:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		break;
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_VGA:
		aconnector->base.polled =
			DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;
		break;
	default:
		break;
	}

	drm_object_attach_property(&aconnector->base.base,
				dm->ddev->mode_config.scaling_mode_property,
				DRM_MODE_SCALE_NONE);

	if (connector_type == DRM_MODE_CONNECTOR_HDMIA
		|| (connector_type == DRM_MODE_CONNECTOR_DisplayPort && !aconnector->mst_root))
		drm_connector_attach_broadcast_rgb_property(&aconnector->base);

	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_property,
				UNDERSCAN_OFF);
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_hborder_property,
				0);
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_vborder_property,
				0);

	if (!aconnector->mst_root)
		drm_connector_attach_max_bpc_property(&aconnector->base, 8, 16);

	aconnector->base.state->max_bpc = 16;
	aconnector->base.state->max_requested_bpc = aconnector->base.state->max_bpc;

	if (connector_type == DRM_MODE_CONNECTOR_HDMIA) {
		/* Content Type is currently only implemented for HDMI. */
		drm_connector_attach_content_type_property(&aconnector->base);
	}

	if (connector_type == DRM_MODE_CONNECTOR_HDMIA) {
		if (!drm_mode_create_hdmi_colorspace_property(&aconnector->base, supported_colorspaces))
			drm_connector_attach_colorspace_property(&aconnector->base);
	} else if ((connector_type == DRM_MODE_CONNECTOR_DisplayPort && !aconnector->mst_root) ||
		   connector_type == DRM_MODE_CONNECTOR_eDP) {
		if (!drm_mode_create_dp_colorspace_property(&aconnector->base, supported_colorspaces))
			drm_connector_attach_colorspace_property(&aconnector->base);
	}

	if (connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector_type == DRM_MODE_CONNECTOR_DisplayPort ||
	    connector_type == DRM_MODE_CONNECTOR_eDP) {
		drm_connector_attach_hdr_output_metadata_property(&aconnector->base);

		if (!aconnector->mst_root) {
			drm_connector_attach_vrr_capable_property(&aconnector->base);
			drm_connector_attach_color_format_property(&aconnector->base,
								   supported_colorformats);
		}


		if (adev->dm.hdcp_workqueue)
			drm_connector_attach_content_protection_property(&aconnector->base, true);
	}

	if (connector_type == DRM_MODE_CONNECTOR_eDP) {
		struct drm_privacy_screen *privacy_screen;

		drm_connector_attach_panel_type_property(&aconnector->base);

		privacy_screen = drm_privacy_screen_get(adev_to_drm(adev)->dev, NULL);
		if (!IS_ERR(privacy_screen)) {
			drm_connector_attach_privacy_screen_provider(&aconnector->base,
								     privacy_screen);
		} else if (PTR_ERR(privacy_screen) != -ENODEV) {
			drm_warn(adev_to_drm(adev), "Error getting privacy-screen\n");
		}
	}
}

static int amdgpu_dm_i2c_xfer(struct i2c_adapter *i2c_adap,
			      struct i2c_msg *msgs, int num)
{
	struct amdgpu_i2c_adapter *i2c = i2c_get_adapdata(i2c_adap);
	struct ddc_service *ddc_service = i2c->ddc_service;
	struct i2c_command cmd;
	int i;
	int result = -EIO;

	if (!ddc_service->ddc_pin)
		return result;

	cmd.payloads = kzalloc_objs(struct i2c_payload, num);

	if (!cmd.payloads)
		return result;

	cmd.number_of_payloads = num;
	cmd.engine = I2C_COMMAND_ENGINE_DEFAULT;
	cmd.speed = 100;

	for (i = 0; i < num; i++) {
		cmd.payloads[i].write = !(msgs[i].flags & I2C_M_RD);
		cmd.payloads[i].address = msgs[i].addr;
		cmd.payloads[i].length = msgs[i].len;
		cmd.payloads[i].data = msgs[i].buf;
	}

	if (i2c->oem) {
		if (dc_submit_i2c_oem(
			    ddc_service->ctx->dc,
			    &cmd))
			result = num;
	} else {
		if (dc_submit_i2c(
			    ddc_service->ctx->dc,
			    ddc_service->link->link_index,
			    &cmd))
			result = num;
	}

	kfree(cmd.payloads);
	return result;
}

static u32 amdgpu_dm_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm amdgpu_dm_i2c_algo = {
	.master_xfer = amdgpu_dm_i2c_xfer,
	.functionality = amdgpu_dm_i2c_func,
};

struct amdgpu_i2c_adapter *
amdgpu_dm_create_i2c(struct ddc_service *ddc_service, bool oem)
{
	struct amdgpu_device *adev = ddc_service->ctx->driver_context;
	struct amdgpu_i2c_adapter *i2c;

	i2c = kzalloc_obj(struct amdgpu_i2c_adapter);
	if (!i2c)
		return NULL;
	i2c->base.owner = THIS_MODULE;
	i2c->base.dev.parent = &adev->pdev->dev;
	i2c->base.algo = &amdgpu_dm_i2c_algo;
	if (oem)
		snprintf(i2c->base.name, sizeof(i2c->base.name), "AMDGPU DM i2c OEM bus");
	else
		snprintf(i2c->base.name, sizeof(i2c->base.name), "AMDGPU DM i2c hw bus %d",
			 ddc_service->link->link_index);
	i2c_set_adapdata(&i2c->base, i2c);
	i2c->ddc_service = ddc_service;
	i2c->oem = oem;

	return i2c;
}

int amdgpu_dm_initialize_hdmi_connector(struct amdgpu_dm_connector *aconnector)
{
	struct cec_connector_info conn_info;
	struct drm_device *ddev = aconnector->base.dev;
	struct device *hdmi_dev = ddev->dev;

	if (amdgpu_dc_debug_mask & DC_DISABLE_HDMI_CEC) {
		drm_info(ddev, "HDMI-CEC feature masked\n");
		return -EINVAL;
	}

	cec_fill_conn_info_from_drm(&conn_info, &aconnector->base);
	aconnector->notifier =
		cec_notifier_conn_register(hdmi_dev, NULL, &conn_info);
	if (!aconnector->notifier) {
		drm_err(ddev, "Failed to create cec notifier\n");
		return -ENOMEM;
	}

	return 0;
}

/*
 * Note: this function assumes that dc_link_detect() was called for the
 * dc_link which will be represented by this aconnector.
 */
int amdgpu_dm_connector_init(struct amdgpu_display_manager *dm,
			     struct amdgpu_dm_connector *aconnector,
			     u32 link_index,
			     struct amdgpu_encoder *aencoder)
{
	int res = 0;
	int connector_type;
	struct dc *dc = dm->dc;
	struct dc_link *link = dc_get_link_at_index(dc, link_index);
	struct amdgpu_i2c_adapter *i2c;

	/* Not needed for writeback connector */
	link->priv = aconnector;


	i2c = amdgpu_dm_create_i2c(link->ddc, false);
	if (!i2c) {
		drm_err(adev_to_drm(dm->adev), "Failed to create i2c adapter data\n");
		return -ENOMEM;
	}

	aconnector->i2c = i2c;
	res = devm_i2c_add_adapter(dm->adev->dev, &i2c->base);

	if (res) {
		drm_err(adev_to_drm(dm->adev), "Failed to register hw i2c %d\n", link->link_index);
		goto out_free;
	}

	connector_type = to_drm_connector_type(link->connector_signal, link->link_id.id);

	res = drm_connector_init_with_ddc(
			dm->ddev,
			&aconnector->base,
			&amdgpu_dm_connector_funcs,
			connector_type,
			&i2c->base);

	if (res) {
		drm_err(adev_to_drm(dm->adev), "connector_init failed\n");
		aconnector->connector_id = -1;
		goto out_free;
	}

	drm_connector_helper_add(
			&aconnector->base,
			&amdgpu_dm_connector_helper_funcs);

	amdgpu_dm_connector_init_helper(
		dm,
		aconnector,
		connector_type,
		link,
		link_index);

	drm_connector_attach_encoder(
		&aconnector->base, &aencoder->base);

	if (connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector_type == DRM_MODE_CONNECTOR_HDMIB)
		amdgpu_dm_initialize_hdmi_connector(aconnector);

	if (dc_is_dp_signal(link->connector_signal))
		amdgpu_dm_initialize_dp_connector(dm, aconnector, link->link_index);

out_free:
	if (res) {
		kfree(i2c);
		aconnector->i2c = NULL;
	}
	return res;
}

static int dm_force_atomic_commit(struct drm_connector *connector)
{
	int ret = 0;
	struct drm_device *ddev = connector->dev;
	struct drm_atomic_commit *state = drm_atomic_commit_alloc(ddev);
	struct amdgpu_crtc *disconnected_acrtc = to_amdgpu_crtc(connector->encoder->crtc);
	struct drm_plane *plane = disconnected_acrtc->base.primary;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	struct drm_plane_state *plane_state;

	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ddev->mode_config.acquire_ctx;

	/* Construct an atomic state to restore previous display setting */

	/*
	 * Attach connectors to drm_atomic_commit
	 */
	conn_state = drm_atomic_get_connector_state(state, connector);

	/* Check for error in getting connector state */
	if (IS_ERR(conn_state)) {
		ret = PTR_ERR(conn_state);
		goto out;
	}

	/* Attach crtc to drm_atomic_commit*/
	crtc_state = drm_atomic_get_crtc_state(state, &disconnected_acrtc->base);

	/* Check for error in getting crtc state */
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto out;
	}

	/* force a restore */
	crtc_state->mode_changed = true;

	/* Attach plane to drm_atomic_commit */
	plane_state = drm_atomic_get_plane_state(state, plane);

	/* Check for error in getting plane state */
	if (IS_ERR(plane_state)) {
		ret = PTR_ERR(plane_state);
		goto out;
	}

	/* Call commit internally with the state we just constructed */
	ret = drm_atomic_commit(state);

out:
	drm_atomic_commit_put(state);
	if (ret)
		drm_err(ddev, "Restoring old state failed with %i\n", ret);

	return ret;
}

/*
 * This function handles all cases when set mode does not come upon hotplug.
 * This includes when a display is unplugged then plugged back into the
 * same port and when running without usermode desktop manager support
 */
void dm_restore_drm_connector_state(struct drm_device *dev,
				    struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_crtc *disconnected_acrtc;
	struct dm_crtc_state *acrtc_state;

	if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
		return;

	aconnector = to_amdgpu_dm_connector(connector);

	if (!aconnector->dc_sink || !connector->state || !connector->encoder)
		return;

	disconnected_acrtc = to_amdgpu_crtc(connector->encoder->crtc);
	if (!disconnected_acrtc)
		return;

	acrtc_state = to_dm_crtc_state(disconnected_acrtc->base.state);
	if (!acrtc_state->stream)
		return;

	/*
	 * If the previous sink is not released and different from the current,
	 * we deduce we are in a state where we can not rely on usermode call
	 * to turn on the display, so we do it here
	 */
	if (acrtc_state->stream->sink != aconnector->dc_sink)
		dm_force_atomic_commit(&aconnector->base);
}

static bool dm_edid_parser_send_cea(struct amdgpu_display_manager *dm,
		unsigned int offset,
		unsigned int total_length,
		u8 *data,
		unsigned int length,
		struct amdgpu_hdmi_vsdb_info *vsdb)
{
	bool res;
	union dmub_rb_cmd cmd;
	struct dmub_cmd_send_edid_cea *input;
	struct dmub_cmd_edid_cea_output *output;

	if (length > DMUB_EDID_CEA_DATA_CHUNK_BYTES)
		return false;

	memset(&cmd, 0, sizeof(cmd));

	input = &cmd.edid_cea.data.input;

	cmd.edid_cea.header.type = DMUB_CMD__EDID_CEA;
	cmd.edid_cea.header.sub_type = 0;
	cmd.edid_cea.header.payload_bytes =
		sizeof(cmd.edid_cea) - sizeof(cmd.edid_cea.header);
	input->offset = offset;
	input->length = length;
	input->cea_total_length = total_length;
	memcpy(input->payload, data, length);

	res = dc_wake_and_execute_dmub_cmd(dm->dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY);
	if (!res) {
		drm_err(adev_to_drm(dm->adev), "EDID CEA parser failed\n");
		return false;
	}

	output = &cmd.edid_cea.data.output;

	if (output->type == DMUB_CMD__EDID_CEA_ACK) {
		if (!output->ack.success) {
			drm_err(adev_to_drm(dm->adev), "EDID CEA ack failed at offset %d\n",
					output->ack.offset);
		}
	} else if (output->type == DMUB_CMD__EDID_CEA_AMD_VSDB) {
		if (!output->amd_vsdb.vsdb_found)
			return false;

		vsdb->freesync_supported = output->amd_vsdb.freesync_supported;
		vsdb->amd_vsdb_version = output->amd_vsdb.amd_vsdb_version;
		vsdb->min_refresh_rate_hz = output->amd_vsdb.min_frame_rate;
		vsdb->max_refresh_rate_hz = output->amd_vsdb.max_frame_rate;
		vsdb->freesync_mccs_vcp_code = output->amd_vsdb.freesync_mccs_vcp_code;
	} else {
		drm_warn(adev_to_drm(dm->adev), "Unknown EDID CEA parser results\n");
		return false;
	}

	return true;
}

static bool parse_edid_cea_dmcu(struct amdgpu_display_manager *dm,
		u8 *edid_ext, int len,
		struct amdgpu_hdmi_vsdb_info *vsdb_info)
{
	int i;

	/* send extension block to DMCU for parsing */
	for (i = 0; i < len; i += 8) {
		bool res;
		int offset;

		/* send 8 bytes a time */
		if (!dc_edid_parser_send_cea(dm->dc, i, len, &edid_ext[i], 8))
			return false;

		if (i+8 == len) {
			/* EDID block sent completed, expect result */
			int version, min_rate, max_rate;

			res = dc_edid_parser_recv_amd_vsdb(dm->dc, &version, &min_rate, &max_rate);
			if (res) {
				/* amd vsdb found */
				vsdb_info->freesync_supported = 1;
				vsdb_info->amd_vsdb_version = version;
				vsdb_info->min_refresh_rate_hz = min_rate;
				vsdb_info->max_refresh_rate_hz = max_rate;
				/* Not enabled on DMCU*/
				vsdb_info->freesync_mccs_vcp_code = 0;
				return true;
			}
			/* not amd vsdb */
			return false;
		}

		/* check for ack*/
		res = dc_edid_parser_recv_cea_ack(dm->dc, &offset);
		if (!res)
			return false;
	}

	return false;
}

static bool parse_edid_cea_dmub(struct amdgpu_display_manager *dm,
		u8 *edid_ext, int len,
		struct amdgpu_hdmi_vsdb_info *vsdb_info)
{
	int i;

	/* send extension block to DMCU for parsing */
	for (i = 0; i < len; i += 8) {
		/* send 8 bytes a time */
		if (!dm_edid_parser_send_cea(dm, i, len, &edid_ext[i], 8, vsdb_info))
			return false;
	}

	return vsdb_info->freesync_supported;
}

static bool parse_edid_cea(struct amdgpu_dm_connector *aconnector,
		u8 *edid_ext, int len,
		struct amdgpu_hdmi_vsdb_info *vsdb_info)
{
	struct amdgpu_device *adev = drm_to_adev(aconnector->base.dev);
	bool ret;

	mutex_lock(&adev->dm.dc_lock);
	if (adev->dm.dmub_srv)
		ret = parse_edid_cea_dmub(&adev->dm, edid_ext, len, vsdb_info);
	else
		ret = parse_edid_cea_dmcu(&adev->dm, edid_ext, len, vsdb_info);
	mutex_unlock(&adev->dm.dc_lock);
	return ret;
}

static void parse_edid_displayid_vrr(struct drm_connector *connector,
				     const struct edid *edid)
{
	u8 *edid_ext = NULL;
	int i;
	int j = 0;
	u16 min_vfreq;
	u16 max_vfreq;

	if (!edid || !edid->extensions)
		return;

	/* Find DisplayID extension */
	for (i = 0; i < edid->extensions; i++) {
		edid_ext = (void *)(edid + (i + 1));
		if (edid_ext[0] == DISPLAYID_EXT)
			break;
	}

	if (i == edid->extensions)
		return;

	while (j < EDID_LENGTH) {
		/* Get dynamic video timing range from DisplayID if available */
		if (EDID_LENGTH - j > 13 && edid_ext[j] == 0x25	&&
		    (edid_ext[j+1] & 0xFE) == 0 && (edid_ext[j+2] == 9)) {
			min_vfreq = edid_ext[j+9];
			if (edid_ext[j+1] & 7)
				max_vfreq = edid_ext[j+10] + ((edid_ext[j+11] & 3) << 8);
			else
				max_vfreq = edid_ext[j+10];

			if (max_vfreq && min_vfreq) {
				connector->display_info.monitor_range.max_vfreq = max_vfreq;
				connector->display_info.monitor_range.min_vfreq = min_vfreq;

				return;
			}
		}
		j++;
	}
}

static int get_amd_vsdb(struct amdgpu_dm_connector *aconnector,
			struct amdgpu_hdmi_vsdb_info *vsdb_info)
{
	struct drm_connector *connector = &aconnector->base;

	vsdb_info->replay_mode = connector->display_info.amd_vsdb.replay_mode;
	vsdb_info->amd_vsdb_version = connector->display_info.amd_vsdb.version;

	return connector->display_info.amd_vsdb.version != 0;
}

static int parse_hdmi_amd_vsdb(struct amdgpu_dm_connector *aconnector,
			       const struct edid *edid,
			       struct amdgpu_hdmi_vsdb_info *vsdb_info)
{
	u8 *edid_ext = NULL;
	int i;
	bool valid_vsdb_found = false;

	/*----- drm_find_cea_extension() -----*/
	/* No EDID or EDID extensions */
	if (edid == NULL || edid->extensions == 0)
		return -ENODEV;

	/* Find CEA extension */
	for (i = 0; i < edid->extensions; i++) {
		edid_ext = (uint8_t *)edid + EDID_LENGTH * (i + 1);
		if (edid_ext[0] == CEA_EXT)
			break;
	}

	if (i == edid->extensions)
		return -ENODEV;

	/*----- cea_db_offsets() -----*/
	if (edid_ext[0] != CEA_EXT)
		return -ENODEV;

	valid_vsdb_found = parse_edid_cea(aconnector, edid_ext, EDID_LENGTH, vsdb_info);

	return valid_vsdb_found ? i : -ENODEV;
}

/**
 * amdgpu_dm_update_freesync_caps - Update Freesync capabilities
 *
 * @connector: Connector to query.
 * @drm_edid: DRM EDID from monitor
 * @do_mccs: Controls whether MCCS (Monitor Control Command Set) over
 *	      DDC (Display Data Channel) transactions are performed. When true,
 *	      the driver queries the monitor to get or update additional FreeSync
 *	      capability information. When false, these transactions are skipped.
 *
 * Amdgpu supports Freesync in DP and HDMI displays, and it is required to keep
 * track of some of the display information in the internal data struct used by
 * amdgpu_dm. This function checks which type of connector we need to set the
 * FreeSync parameters.
 */
void amdgpu_dm_update_freesync_caps(struct drm_connector *connector,
				    const struct drm_edid *drm_edid, bool do_mccs)
{
	int i = 0;
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);
	struct dm_connector_state *dm_con_state = NULL;
	struct dc_sink *sink;
	struct amdgpu_device *adev = drm_to_adev(connector->dev);
	struct amdgpu_hdmi_vsdb_info vsdb_info = {0};
	const struct edid *edid;
	bool freesync_capable = false;
	enum adaptive_sync_type as_type = ADAPTIVE_SYNC_TYPE_NONE;

	if (!connector->state) {
		drm_err(adev_to_drm(adev), "%s - Connector has no state", __func__);
		goto update;
	}

	sink = amdgpu_dm_connector->dc_sink ?
		amdgpu_dm_connector->dc_sink :
		amdgpu_dm_connector->dc_em_sink;

	drm_edid_connector_update(connector, drm_edid);

	if (!drm_edid || !sink) {
		dm_con_state = to_dm_connector_state(connector->state);

		amdgpu_dm_connector->min_vfreq = 0;
		amdgpu_dm_connector->max_vfreq = 0;
		freesync_capable = false;

		goto update;
	}

	dm_con_state = to_dm_connector_state(connector->state);

	if (!adev->dm.freesync_module || !dc_supports_vrr(sink->ctx->dce_version))
		goto update;

	/* FIXME: Get rid of drm_edid_raw() */
	edid = drm_edid_raw(drm_edid);

	/* Some eDP panels only have the refresh rate range info in DisplayID */
	if ((connector->display_info.monitor_range.min_vfreq == 0 ||
	     connector->display_info.monitor_range.max_vfreq == 0))
		parse_edid_displayid_vrr(connector, edid);

	if (edid && (sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT ||
		     sink->sink_signal == SIGNAL_TYPE_EDP)) {
		if (amdgpu_dm_connector->dc_link &&
		    amdgpu_dm_connector->dc_link->dpcd_caps.allow_invalid_MSA_timing_param) {
			amdgpu_dm_connector->min_vfreq = connector->display_info.monitor_range.min_vfreq;
			amdgpu_dm_connector->max_vfreq = connector->display_info.monitor_range.max_vfreq;
			if (amdgpu_dm_connector->max_vfreq - amdgpu_dm_connector->min_vfreq > 10)
				freesync_capable = true;
		}

		get_amd_vsdb(amdgpu_dm_connector, &vsdb_info);

		if (vsdb_info.replay_mode) {
			amdgpu_dm_connector->vsdb_info.replay_mode = vsdb_info.replay_mode;
			amdgpu_dm_connector->vsdb_info.amd_vsdb_version = vsdb_info.amd_vsdb_version;
			amdgpu_dm_connector->as_type = ADAPTIVE_SYNC_TYPE_EDP;
		}

	} else if (drm_edid && sink->sink_signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		i = parse_hdmi_amd_vsdb(amdgpu_dm_connector, edid, &vsdb_info);
		if (i >= 0) {
			amdgpu_dm_connector->vsdb_info = vsdb_info;
			sink->edid_caps.freesync_vcp_code = vsdb_info.freesync_mccs_vcp_code;

			if (vsdb_info.freesync_supported) {
				amdgpu_dm_connector->min_vfreq = vsdb_info.min_refresh_rate_hz;
				amdgpu_dm_connector->max_vfreq = vsdb_info.max_refresh_rate_hz;
				if (amdgpu_dm_connector->max_vfreq - amdgpu_dm_connector->min_vfreq > 10)
					freesync_capable = true;

				connector->display_info.monitor_range.min_vfreq = vsdb_info.min_refresh_rate_hz;
				connector->display_info.monitor_range.max_vfreq = vsdb_info.max_refresh_rate_hz;
			}
		}
	}

	if (amdgpu_dm_connector->dc_link)
		as_type = dm_get_adaptive_sync_support_type(amdgpu_dm_connector->dc_link);

	if (as_type == FREESYNC_TYPE_PCON_IN_WHITELIST) {
		i = parse_hdmi_amd_vsdb(amdgpu_dm_connector, edid, &vsdb_info);
		if (i >= 0) {
			amdgpu_dm_connector->vsdb_info = vsdb_info;
			sink->edid_caps.freesync_vcp_code = vsdb_info.freesync_mccs_vcp_code;

			if (vsdb_info.freesync_supported && vsdb_info.amd_vsdb_version > 0) {
				amdgpu_dm_connector->pack_sdp_v1_3 = true;
				amdgpu_dm_connector->as_type = as_type;

				amdgpu_dm_connector->min_vfreq = vsdb_info.min_refresh_rate_hz;
				amdgpu_dm_connector->max_vfreq = vsdb_info.max_refresh_rate_hz;
				if (amdgpu_dm_connector->max_vfreq - amdgpu_dm_connector->min_vfreq > 10)
					freesync_capable = true;

				connector->display_info.monitor_range.min_vfreq = vsdb_info.min_refresh_rate_hz;
				connector->display_info.monitor_range.max_vfreq = vsdb_info.max_refresh_rate_hz;
			}
		}
	}

	/* Handle MCCS */
	if (do_mccs)
		dm_helpers_read_mccs_caps(adev->dm.dc->ctx, amdgpu_dm_connector->dc_link, sink);

	if ((sink->sink_signal == SIGNAL_TYPE_HDMI_TYPE_A ||
		as_type == FREESYNC_TYPE_PCON_IN_WHITELIST) &&
		(!sink->edid_caps.freesync_vcp_code ||
		(sink->edid_caps.freesync_vcp_code && !sink->mccs_caps.freesync_supported)))
		freesync_capable = false;

	if (do_mccs && sink->mccs_caps.freesync_supported && freesync_capable)
		dm_helpers_mccs_vcp_set(adev->dm.dc->ctx, amdgpu_dm_connector->dc_link, sink);

update:
	if (dm_con_state)
		dm_con_state->freesync_capable = freesync_capable;

	if (connector->state && amdgpu_dm_connector->dc_link && !freesync_capable &&
	    amdgpu_dm_connector->dc_link->replay_settings.config.replay_supported) {
		amdgpu_dm_connector->dc_link->replay_settings.config.replay_supported = false;
		amdgpu_dm_connector->dc_link->replay_settings.replay_feature_enabled = false;
	}

	if (connector->vrr_capable_property)
		drm_connector_set_vrr_capable_property(connector,
						       freesync_capable);
}
