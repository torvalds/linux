/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include "vid.h"
#include "amdgpu.h"
#include "amdgpu_display.h"
#include "atom.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_types.h"

#include "amd_shared.h"
#include "amdgpu_dm_irq.h"
#include "dm_helpers.h"

#include "ivsrcid/ivsrcid_vislands30.h"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_mst_helper.h>

#include "modules/inc/mod_freesync.h"

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
#include "ivsrcid/irqsrcs_dcn_1_0.h"

#include "raven1/DCN/dcn_1_0_offset.h"
#include "raven1/DCN/dcn_1_0_sh_mask.h"
#include "vega10/soc15ip.h"

#include "soc15_common.h"
#endif

static enum drm_plane_type dm_surfaces_type_default[AMDGPU_MAX_PLANES] = {
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
};

static enum drm_plane_type dm_surfaces_type_carizzo[AMDGPU_MAX_PLANES] = {
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_OVERLAY,/* YUV Capable Underlay */
};

static enum drm_plane_type dm_surfaces_type_stoney[AMDGPU_MAX_PLANES] = {
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_OVERLAY, /* YUV Capable Underlay */
};

/*
 * dm_vblank_get_counter
 *
 * @brief
 * Get counter for number of vertical blanks
 *
 * @param
 * struct amdgpu_device *adev - [in] desired amdgpu device
 * int disp_idx - [in] which CRTC to get the counter from
 *
 * @return
 * Counter for vertical blanks
 */
static u32 dm_vblank_get_counter(struct amdgpu_device *adev, int crtc)
{
	if (crtc >= adev->mode_info.num_crtc)
		return 0;
	else {
		struct amdgpu_crtc *acrtc = adev->mode_info.crtcs[crtc];
		struct dm_crtc_state *acrtc_state = to_dm_crtc_state(
				acrtc->base.state);


		if (acrtc_state->stream == NULL) {
			DRM_ERROR("dc_stream is NULL for crtc '%d'!\n", crtc);
			return 0;
		}

		return dc_stream_get_vblank_counter(acrtc_state->stream);
	}
}

static int dm_crtc_get_scanoutpos(struct amdgpu_device *adev, int crtc,
					u32 *vbl, u32 *position)
{
	uint32_t v_blank_start, v_blank_end, h_position, v_position;

	if ((crtc < 0) || (crtc >= adev->mode_info.num_crtc))
		return -EINVAL;
	else {
		struct amdgpu_crtc *acrtc = adev->mode_info.crtcs[crtc];
		struct dm_crtc_state *acrtc_state = to_dm_crtc_state(
						acrtc->base.state);

		if (acrtc_state->stream ==  NULL) {
			DRM_ERROR("dc_stream is NULL for crtc '%d'!\n", crtc);
			return 0;
		}

		/*
		 * TODO rework base driver to use values directly.
		 * for now parse it back into reg-format
		 */
		dc_stream_get_scanoutpos(acrtc_state->stream,
					 &v_blank_start,
					 &v_blank_end,
					 &h_position,
					 &v_position);

		*position = v_position | (h_position << 16);
		*vbl = v_blank_start | (v_blank_end << 16);
	}

	return 0;
}

static bool dm_is_idle(void *handle)
{
	/* XXX todo */
	return true;
}

static int dm_wait_for_idle(void *handle)
{
	/* XXX todo */
	return 0;
}

static bool dm_check_soft_reset(void *handle)
{
	return false;
}

static int dm_soft_reset(void *handle)
{
	/* XXX todo */
	return 0;
}

static struct amdgpu_crtc *get_crtc_by_otg_inst(
	struct amdgpu_device *adev,
	int otg_inst)
{
	struct drm_device *dev = adev->ddev;
	struct drm_crtc *crtc;
	struct amdgpu_crtc *amdgpu_crtc;

	/*
	 * following if is check inherited from both functions where this one is
	 * used now. Need to be checked why it could happen.
	 */
	if (otg_inst == -1) {
		WARN_ON(1);
		return adev->mode_info.crtcs[0];
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		amdgpu_crtc = to_amdgpu_crtc(crtc);

		if (amdgpu_crtc->otg_inst == otg_inst)
			return amdgpu_crtc;
	}

	return NULL;
}

static void dm_pflip_high_irq(void *interrupt_params)
{
	struct amdgpu_crtc *amdgpu_crtc;
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	unsigned long flags;

	amdgpu_crtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_PFLIP);

	/* IRQ could occur when in initial stage */
	/*TODO work and BO cleanup */
	if (amdgpu_crtc == NULL) {
		DRM_DEBUG_DRIVER("CRTC is null, returning.\n");
		return;
	}

	spin_lock_irqsave(&adev->ddev->event_lock, flags);

	if (amdgpu_crtc->pflip_status != AMDGPU_FLIP_SUBMITTED){
		DRM_DEBUG_DRIVER("amdgpu_crtc->pflip_status = %d !=AMDGPU_FLIP_SUBMITTED(%d) on crtc:%d[%p] \n",
						 amdgpu_crtc->pflip_status,
						 AMDGPU_FLIP_SUBMITTED,
						 amdgpu_crtc->crtc_id,
						 amdgpu_crtc);
		spin_unlock_irqrestore(&adev->ddev->event_lock, flags);
		return;
	}


	/* wakeup usersapce */
	if (amdgpu_crtc->event) {
		/* Update to correct count/ts if racing with vblank irq */
		drm_crtc_accurate_vblank_count(&amdgpu_crtc->base);

		drm_crtc_send_vblank_event(&amdgpu_crtc->base, amdgpu_crtc->event);

		/* page flip completed. clean up */
		amdgpu_crtc->event = NULL;

	} else
		WARN_ON(1);

	amdgpu_crtc->pflip_status = AMDGPU_FLIP_NONE;
	spin_unlock_irqrestore(&adev->ddev->event_lock, flags);

	DRM_DEBUG_DRIVER("%s - crtc :%d[%p], pflip_stat:AMDGPU_FLIP_NONE\n",
					__func__, amdgpu_crtc->crtc_id, amdgpu_crtc);

	drm_crtc_vblank_put(&amdgpu_crtc->base);
}

static void dm_crtc_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	uint8_t crtc_index = 0;
	struct amdgpu_crtc *acrtc;

	acrtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VBLANK);

	if (acrtc)
		crtc_index = acrtc->crtc_id;

	drm_handle_vblank(adev->ddev, crtc_index);
}

static int dm_set_clockgating_state(void *handle,
		  enum amd_clockgating_state state)
{
	return 0;
}

static int dm_set_powergating_state(void *handle,
		  enum amd_powergating_state state)
{
	return 0;
}

/* Prototypes of private functions */
static int dm_early_init(void* handle);

static void hotplug_notify_work_func(struct work_struct *work)
{
	struct amdgpu_display_manager *dm = container_of(work, struct amdgpu_display_manager, mst_hotplug_work);
	struct drm_device *dev = dm->ddev;

	drm_kms_helper_hotplug_event(dev);
}

/* Init display KMS
 *
 * Returns 0 on success
 */
int amdgpu_dm_init(struct amdgpu_device *adev)
{
	struct dc_init_data init_data;
	adev->dm.ddev = adev->ddev;
	adev->dm.adev = adev;

	DRM_INFO("DAL is enabled\n");
	/* Zero all the fields */
	memset(&init_data, 0, sizeof(init_data));

	/* initialize DAL's lock (for SYNC context use) */
	spin_lock_init(&adev->dm.dal_lock);

	/* initialize DAL's mutex */
	mutex_init(&adev->dm.dal_mutex);

	if(amdgpu_dm_irq_init(adev)) {
		DRM_ERROR("amdgpu: failed to initialize DM IRQ support.\n");
		goto error;
	}

	init_data.asic_id.chip_family = adev->family;

	init_data.asic_id.pci_revision_id = adev->rev_id;
	init_data.asic_id.hw_internal_rev = adev->external_rev_id;

	init_data.asic_id.vram_width = adev->mc.vram_width;
	/* TODO: initialize init_data.asic_id.vram_type here!!!! */
	init_data.asic_id.atombios_base_address =
		adev->mode_info.atom_context->bios;

	init_data.driver = adev;

	adev->dm.cgs_device = amdgpu_cgs_create_device(adev);

	if (!adev->dm.cgs_device) {
		DRM_ERROR("amdgpu: failed to create cgs device.\n");
		goto error;
	}

	init_data.cgs_device = adev->dm.cgs_device;

	adev->dm.dal = NULL;

	init_data.dce_environment = DCE_ENV_PRODUCTION_DRV;

	/* Display Core create. */
	adev->dm.dc = dc_create(&init_data);

	if (!adev->dm.dc)
		DRM_INFO("Display Core failed to initialize!\n");

	INIT_WORK(&adev->dm.mst_hotplug_work, hotplug_notify_work_func);

	adev->dm.freesync_module = mod_freesync_create(adev->dm.dc);
	if (!adev->dm.freesync_module) {
		DRM_ERROR(
		"amdgpu: failed to initialize freesync_module.\n");
	} else
		DRM_INFO("amdgpu: freesync_module init done %p.\n",
				adev->dm.freesync_module);

	if (amdgpu_dm_initialize_drm_device(adev)) {
		DRM_ERROR(
		"amdgpu: failed to initialize sw for display support.\n");
		goto error;
	}

	/* Update the actual used number of crtc */
	adev->mode_info.num_crtc = adev->dm.display_indexes_num;

	/* TODO: Add_display_info? */

	/* TODO use dynamic cursor width */
	adev->ddev->mode_config.cursor_width = adev->dm.dc->caps.max_cursor_size;
	adev->ddev->mode_config.cursor_height = adev->dm.dc->caps.max_cursor_size;

	if (drm_vblank_init(adev->ddev, adev->dm.display_indexes_num)) {
		DRM_ERROR(
		"amdgpu: failed to initialize sw for display support.\n");
		goto error;
	}

	DRM_INFO("KMS initialized.\n");

	return 0;
error:
	amdgpu_dm_fini(adev);

	return -1;
}

void amdgpu_dm_fini(struct amdgpu_device *adev)
{
	amdgpu_dm_destroy_drm_device(&adev->dm);
	/*
	 * TODO: pageflip, vlank interrupt
	 *
	 * amdgpu_dm_irq_fini(adev);
	 */

	if (adev->dm.cgs_device) {
		amdgpu_cgs_destroy_device(adev->dm.cgs_device);
		adev->dm.cgs_device = NULL;
	}
	if (adev->dm.freesync_module) {
		mod_freesync_destroy(adev->dm.freesync_module);
		adev->dm.freesync_module = NULL;
	}
	/* DC Destroy TODO: Replace destroy DAL */
	if (adev->dm.dc)
		dc_destroy(&adev->dm.dc);
	return;
}

/* moved from amdgpu_dm_kms.c */
void amdgpu_dm_destroy()
{
}

static int dm_sw_init(void *handle)
{
	return 0;
}

static int dm_sw_fini(void *handle)
{
	return 0;
}

static int detect_mst_link_for_all_connectors(struct drm_device *dev)
{
	struct amdgpu_connector *aconnector;
	struct drm_connector *connector;
	int ret = 0;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		   aconnector = to_amdgpu_connector(connector);
		if (aconnector->dc_link->type == dc_connection_mst_branch) {
			DRM_INFO("DM_MST: starting TM on aconnector: %p [id: %d]\n",
					aconnector, aconnector->base.base.id);

			ret = drm_dp_mst_topology_mgr_set_mst(&aconnector->mst_mgr, true);
			if (ret < 0) {
				DRM_ERROR("DM_MST: Failed to start MST\n");
				((struct dc_link *)aconnector->dc_link)->type = dc_connection_single;
				return ret;
				}
			}
	}

	drm_modeset_unlock(&dev->mode_config.connection_mutex);
	return ret;
}

static int dm_late_init(void *handle)
{
	struct drm_device *dev = ((struct amdgpu_device *)handle)->ddev;
	int r = detect_mst_link_for_all_connectors(dev);

	return r;
}

static void s3_handle_mst(struct drm_device *dev, bool suspend)
{
	struct amdgpu_connector *aconnector;
	struct drm_connector *connector;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		   aconnector = to_amdgpu_connector(connector);
		   if (aconnector->dc_link->type == dc_connection_mst_branch &&
				   !aconnector->mst_port) {

			   if (suspend)
				   drm_dp_mst_topology_mgr_suspend(&aconnector->mst_mgr);
			   else
				   drm_dp_mst_topology_mgr_resume(&aconnector->mst_mgr);
		   }
	}

	drm_modeset_unlock(&dev->mode_config.connection_mutex);
}

static int dm_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	/* Create DAL display manager */
	amdgpu_dm_init(adev);
	amdgpu_dm_hpd_init(adev);

	return 0;
}

static int dm_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_dm_hpd_fini(adev);

	amdgpu_dm_irq_fini(adev);
	amdgpu_dm_fini(adev);
	return 0;
}

static int dm_suspend(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct amdgpu_display_manager *dm = &adev->dm;
	int ret = 0;

	s3_handle_mst(adev->ddev, true);

	amdgpu_dm_irq_suspend(adev);

	adev->dm.cached_state = drm_atomic_helper_suspend(adev->ddev);

	dc_set_power_state(
		dm->dc,
		DC_ACPI_CM_POWER_STATE_D3
		);

	return ret;
}

struct amdgpu_connector *amdgpu_dm_find_first_crct_matching_connector(
	struct drm_atomic_state *state,
	struct drm_crtc *crtc,
	bool from_state_var)
{
	uint32_t i;
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	struct drm_crtc *crtc_from_state;

	for_each_connector_in_state(
		state,
		connector,
		conn_state,
		i) {
		crtc_from_state =
			from_state_var ?
				conn_state->crtc :
				connector->state->crtc;

		if (crtc_from_state == crtc)
			return to_amdgpu_connector(connector);
	}

	return NULL;
}

static int dm_resume(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct amdgpu_display_manager *dm = &adev->dm;

	/* power on hardware */
	dc_set_power_state(
		dm->dc,
		DC_ACPI_CM_POWER_STATE_D0
		);

	return 0;
}

int amdgpu_dm_display_resume(struct amdgpu_device *adev )
{
	struct drm_device *ddev = adev->ddev;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct amdgpu_connector *aconnector;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int ret = 0;
	int i;

	/* program HPD filter */
	dc_resume(dm->dc);

	/* On resume we need to  rewrite the MSTM control bits to enamble MST*/
	s3_handle_mst(ddev, false);

	/*
	 * early enable HPD Rx IRQ, should be done before set mode as short
	 * pulse interrupts are used for MST
	 */
	amdgpu_dm_irq_resume_early(adev);

	/* Do detection*/
	list_for_each_entry(connector,
			&ddev->mode_config.connector_list, head) {
		aconnector = to_amdgpu_connector(connector);

		/*
		 * this is the case when traversing through already created
		 * MST connectors, should be skipped
		 */
		if (aconnector->mst_port)
			continue;

		mutex_lock(&aconnector->hpd_lock);
		dc_link_detect(aconnector->dc_link, false);
		aconnector->dc_sink = NULL;
		amdgpu_dm_update_connector_after_detect(aconnector);
		mutex_unlock(&aconnector->hpd_lock);
	}

	/* Force mode set in atomic comit */
	for_each_crtc_in_state(adev->dm.cached_state, crtc, crtc_state, i)
			crtc_state->active_changed = true;

	ret = drm_atomic_helper_resume(ddev, adev->dm.cached_state);

	amdgpu_dm_irq_resume_late(adev);

	return ret;
}

static const struct amd_ip_funcs amdgpu_dm_funcs = {
	.name = "dm",
	.early_init = dm_early_init,
	.late_init = dm_late_init,
	.sw_init = dm_sw_init,
	.sw_fini = dm_sw_fini,
	.hw_init = dm_hw_init,
	.hw_fini = dm_hw_fini,
	.suspend = dm_suspend,
	.resume = dm_resume,
	.is_idle = dm_is_idle,
	.wait_for_idle = dm_wait_for_idle,
	.check_soft_reset = dm_check_soft_reset,
	.soft_reset = dm_soft_reset,
	.set_clockgating_state = dm_set_clockgating_state,
	.set_powergating_state = dm_set_powergating_state,
};

const struct amdgpu_ip_block_version dm_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_DCE,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &amdgpu_dm_funcs,
};


struct drm_atomic_state *
dm_atomic_state_alloc(struct drm_device *dev)
{
	struct dm_atomic_state *state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (!state || drm_atomic_state_init(dev, &state->base) < 0) {
		kfree(state);
		return NULL;
	}

	return &state->base;
}

static void
dm_atomic_state_clear(struct drm_atomic_state *state)
{
	struct dm_atomic_state *dm_state = to_dm_atomic_state(state);

	if (dm_state->context) {
		dc_release_validate_context(dm_state->context);
		dm_state->context = NULL;
	}

	drm_atomic_state_default_clear(state);
}

static void
dm_atomic_state_alloc_free(struct drm_atomic_state *state)
{
	struct dm_atomic_state *dm_state = to_dm_atomic_state(state);
	drm_atomic_state_default_release(state);
	kfree(dm_state);
}

static const struct drm_mode_config_funcs amdgpu_dm_mode_funcs = {
	.fb_create = amdgpu_user_framebuffer_create,
	.output_poll_changed = amdgpu_output_poll_changed,
	.atomic_check = amdgpu_dm_atomic_check,
	.atomic_commit = amdgpu_dm_atomic_commit,
	.atomic_state_alloc = dm_atomic_state_alloc,
	.atomic_state_clear = dm_atomic_state_clear,
	.atomic_state_free = dm_atomic_state_alloc_free
};

static struct drm_mode_config_helper_funcs amdgpu_dm_mode_config_helperfuncs = {
	.atomic_commit_tail = amdgpu_dm_atomic_commit_tail
};

void amdgpu_dm_update_connector_after_detect(
	struct amdgpu_connector *aconnector)
{
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	const struct dc_sink *sink;

	/* MST handled by drm_mst framework */
	if (aconnector->mst_mgr.mst_state == true)
		return;


	sink = aconnector->dc_link->local_sink;

	/* Edid mgmt connector gets first update only in mode_valid hook and then
	 * the connector sink is set to either fake or physical sink depends on link status.
	 * don't do it here if u are during boot
	 */
	if (aconnector->base.force != DRM_FORCE_UNSPECIFIED
			&& aconnector->dc_em_sink) {

		/* For S3 resume with headless use eml_sink to fake stream
		 * because on resume connecotr->sink is set ti NULL
		 */
		mutex_lock(&dev->mode_config.mutex);

		if (sink) {
			if (aconnector->dc_sink) {
				amdgpu_dm_remove_sink_from_freesync_module(
								connector);
				/* retain and release bellow are used for
				 * bump up refcount for sink because the link don't point
				 * to it anymore after disconnect so on next crtc to connector
				 * reshuffle by UMD we will get into unwanted dc_sink release
				 */
				if (aconnector->dc_sink != aconnector->dc_em_sink)
					dc_sink_release(aconnector->dc_sink);
			}
			aconnector->dc_sink = sink;
			amdgpu_dm_add_sink_to_freesync_module(
						connector, aconnector->edid);
		} else {
			amdgpu_dm_remove_sink_from_freesync_module(connector);
			if (!aconnector->dc_sink)
				aconnector->dc_sink = aconnector->dc_em_sink;
			else if (aconnector->dc_sink != aconnector->dc_em_sink)
				dc_sink_retain(aconnector->dc_sink);
		}

		mutex_unlock(&dev->mode_config.mutex);
		return;
	}

	/*
	 * TODO: temporary guard to look for proper fix
	 * if this sink is MST sink, we should not do anything
	 */
	if (sink && sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
		return;

	if (aconnector->dc_sink == sink) {
		/* We got a DP short pulse (Link Loss, DP CTS, etc...).
		 * Do nothing!! */
		DRM_INFO("DCHPD: connector_id=%d: dc_sink didn't change.\n",
				aconnector->connector_id);
		return;
	}

	DRM_INFO("DCHPD: connector_id=%d: Old sink=%p New sink=%p\n",
		aconnector->connector_id, aconnector->dc_sink, sink);

	mutex_lock(&dev->mode_config.mutex);

	/* 1. Update status of the drm connector
	 * 2. Send an event and let userspace tell us what to do */
	if (sink) {
		/* TODO: check if we still need the S3 mode update workaround.
		 * If yes, put it here. */
		if (aconnector->dc_sink)
			amdgpu_dm_remove_sink_from_freesync_module(
							connector);

		aconnector->dc_sink = sink;
		if (sink->dc_edid.length == 0)
			aconnector->edid = NULL;
		else {
			aconnector->edid =
				(struct edid *) sink->dc_edid.raw_edid;


			drm_mode_connector_update_edid_property(connector,
					aconnector->edid);
		}
		amdgpu_dm_add_sink_to_freesync_module(connector, aconnector->edid);

	} else {
		amdgpu_dm_remove_sink_from_freesync_module(connector);
		drm_mode_connector_update_edid_property(connector, NULL);
		aconnector->num_modes = 0;
		aconnector->dc_sink = NULL;
	}

	mutex_unlock(&dev->mode_config.mutex);
}

static void handle_hpd_irq(void *param)
{
	struct amdgpu_connector *aconnector = (struct amdgpu_connector *)param;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;

	/* In case of failure or MST no need to update connector status or notify the OS
	 * since (for MST case) MST does this in it's own context.
	 */
	mutex_lock(&aconnector->hpd_lock);
	if (dc_link_detect(aconnector->dc_link, false)) {
		amdgpu_dm_update_connector_after_detect(aconnector);


		drm_modeset_lock_all(dev);
		dm_restore_drm_connector_state(dev, connector);
		drm_modeset_unlock_all(dev);

		if (aconnector->base.force == DRM_FORCE_UNSPECIFIED)
			drm_kms_helper_hotplug_event(dev);
	}
	mutex_unlock(&aconnector->hpd_lock);

}

static void dm_handle_hpd_rx_irq(struct amdgpu_connector *aconnector)
{
	uint8_t esi[DP_PSR_ERROR_STATUS - DP_SINK_COUNT_ESI] = { 0 };
	uint8_t dret;
	bool new_irq_handled = false;
	int dpcd_addr;
	int dpcd_bytes_to_read;

	const int max_process_count = 30;
	int process_count = 0;

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

	dret = drm_dp_dpcd_read(
		&aconnector->dm_dp_aux.aux,
		dpcd_addr,
		esi,
		dpcd_bytes_to_read);

	while (dret == dpcd_bytes_to_read &&
		process_count < max_process_count) {
		uint8_t retry;
		dret = 0;

		process_count++;

		DRM_DEBUG_KMS("ESI %02x %02x %02x\n", esi[0], esi[1], esi[2]);
		/* handle HPD short pulse irq */
		if (aconnector->mst_mgr.mst_state)
			drm_dp_mst_hpd_irq(
				&aconnector->mst_mgr,
				esi,
				&new_irq_handled);

		if (new_irq_handled) {
			/* ACK at DPCD to notify down stream */
			const int ack_dpcd_bytes_to_write =
				dpcd_bytes_to_read - 1;

			for (retry = 0; retry < 3; retry++) {
				uint8_t wret;

				wret = drm_dp_dpcd_write(
					&aconnector->dm_dp_aux.aux,
					dpcd_addr + 1,
					&esi[1],
					ack_dpcd_bytes_to_write);
				if (wret == ack_dpcd_bytes_to_write)
					break;
			}

			/* check if there is new irq to be handle */
			dret = drm_dp_dpcd_read(
				&aconnector->dm_dp_aux.aux,
				dpcd_addr,
				esi,
				dpcd_bytes_to_read);

			new_irq_handled = false;
		} else
			break;
	}

	if (process_count == max_process_count)
		DRM_DEBUG_KMS("Loop exceeded max iterations\n");
}

static void handle_hpd_rx_irq(void *param)
{
	struct amdgpu_connector *aconnector = (struct amdgpu_connector *)param;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	const struct dc_link *dc_link = aconnector->dc_link;
	bool is_mst_root_connector = aconnector->mst_mgr.mst_state;

	/* TODO:Temporary add mutex to protect hpd interrupt not have a gpio
	 * conflict, after implement i2c helper, this mutex should be
	 * retired.
	 */
	if (aconnector->dc_link->type != dc_connection_mst_branch)
		mutex_lock(&aconnector->hpd_lock);

	if (dc_link_handle_hpd_rx_irq(aconnector->dc_link) &&
			!is_mst_root_connector) {
		/* Downstream Port status changed. */
		if (dc_link_detect(aconnector->dc_link, false)) {
			amdgpu_dm_update_connector_after_detect(aconnector);


			drm_modeset_lock_all(dev);
			dm_restore_drm_connector_state(dev, connector);
			drm_modeset_unlock_all(dev);

			drm_kms_helper_hotplug_event(dev);
		}
	}
	if ((dc_link->cur_link_settings.lane_count != LANE_COUNT_UNKNOWN) ||
				(dc_link->type == dc_connection_mst_branch))
		dm_handle_hpd_rx_irq(aconnector);

	if (aconnector->dc_link->type != dc_connection_mst_branch)
		mutex_unlock(&aconnector->hpd_lock);
}

static void register_hpd_handlers(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev->ddev;
	struct drm_connector *connector;
	struct amdgpu_connector *aconnector;
	const struct dc_link *dc_link;
	struct dc_interrupt_params int_params = {0};

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	list_for_each_entry(connector,
			&dev->mode_config.connector_list, head)	{

		aconnector = to_amdgpu_connector(connector);
		dc_link = aconnector->dc_link;

		if (DC_IRQ_SOURCE_INVALID != dc_link->irq_source_hpd) {
			int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
			int_params.irq_source = dc_link->irq_source_hpd;

			amdgpu_dm_irq_register_interrupt(adev, &int_params,
					handle_hpd_irq,
					(void *) aconnector);
		}

		if (DC_IRQ_SOURCE_INVALID != dc_link->irq_source_hpd_rx) {

			/* Also register for DP short pulse (hpd_rx). */
			int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
			int_params.irq_source =	dc_link->irq_source_hpd_rx;

			amdgpu_dm_irq_register_interrupt(adev, &int_params,
					handle_hpd_rx_irq,
					(void *) aconnector);
		}
	}
}

/* Register IRQ sources and initialize IRQ callbacks */
static int dce110_register_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r;
	int i;
	unsigned client_id = AMDGPU_IH_CLIENTID_LEGACY;

	if (adev->asic_type == CHIP_VEGA10 ||
	    adev->asic_type == CHIP_RAVEN)
		client_id = AMDGPU_IH_CLIENTID_DCE;

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	/* Actions of amdgpu_irq_add_id():
	 * 1. Register a set() function with base driver.
	 *    Base driver will call set() function to enable/disable an
	 *    interrupt in DC hardware.
	 * 2. Register amdgpu_dm_irq_handler().
	 *    Base driver will call amdgpu_dm_irq_handler() for ALL interrupts
	 *    coming from DC hardware.
	 *    amdgpu_dm_irq_handler() will re-direct the interrupt to DC
	 *    for acknowledging and handling. */

	/* Use VBLANK interrupt */
	for (i = VISLANDS30_IV_SRCID_D1_VERTICAL_INTERRUPT0; i <= VISLANDS30_IV_SRCID_D6_VERTICAL_INTERRUPT0; i++) {
		r = amdgpu_irq_add_id(adev, client_id, i, &adev->crtc_irq);
		if (r) {
			DRM_ERROR("Failed to add crtc irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.vblank_params[int_params.irq_source - DC_IRQ_SOURCE_VBLANK1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_crtc_high_irq, c_irq_params);
	}

	/* Use GRPH_PFLIP interrupt */
	for (i = VISLANDS30_IV_SRCID_D1_GRPH_PFLIP;
			i <= VISLANDS30_IV_SRCID_D6_GRPH_PFLIP; i += 2) {
		r = amdgpu_irq_add_id(adev, client_id, i, &adev->pageflip_irq);
		if (r) {
			DRM_ERROR("Failed to add page flip irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.pflip_params[int_params.irq_source - DC_IRQ_SOURCE_PFLIP_FIRST];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_pflip_high_irq, c_irq_params);

	}

	/* HPD */
	r = amdgpu_irq_add_id(adev, client_id,
			VISLANDS30_IV_SRCID_HOTPLUG_DETECT_A, &adev->hpd_irq);
	if (r) {
		DRM_ERROR("Failed to add hpd irq id!\n");
		return r;
	}

	register_hpd_handlers(adev);

	return 0;
}

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
/* Register IRQ sources and initialize IRQ callbacks */
static int dcn10_register_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r;
	int i;

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	/* Actions of amdgpu_irq_add_id():
	 * 1. Register a set() function with base driver.
	 *    Base driver will call set() function to enable/disable an
	 *    interrupt in DC hardware.
	 * 2. Register amdgpu_dm_irq_handler().
	 *    Base driver will call amdgpu_dm_irq_handler() for ALL interrupts
	 *    coming from DC hardware.
	 *    amdgpu_dm_irq_handler() will re-direct the interrupt to DC
	 *    for acknowledging and handling.
	 * */

	/* Use VSTARTUP interrupt */
	for (i = DCN_1_0__SRCID__DC_D1_OTG_VSTARTUP;
			i <= DCN_1_0__SRCID__DC_D1_OTG_VSTARTUP + adev->mode_info.num_crtc - 1;
			i++) {
		r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_DCE, i, &adev->crtc_irq);

		if (r) {
			DRM_ERROR("Failed to add crtc irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.vblank_params[int_params.irq_source - DC_IRQ_SOURCE_VBLANK1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_crtc_high_irq, c_irq_params);
	}

	/* Use GRPH_PFLIP interrupt */
	for (i = DCN_1_0__SRCID__HUBP0_FLIP_INTERRUPT;
			i <= DCN_1_0__SRCID__HUBP0_FLIP_INTERRUPT + adev->mode_info.num_crtc - 1;
			i++) {
		r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_DCE, i, &adev->pageflip_irq);
		if (r) {
			DRM_ERROR("Failed to add page flip irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.pflip_params[int_params.irq_source - DC_IRQ_SOURCE_PFLIP_FIRST];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_pflip_high_irq, c_irq_params);

	}

	/* HPD */
	r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_DCE, DCN_1_0__SRCID__DC_HPD1_INT,
			&adev->hpd_irq);
	if (r) {
		DRM_ERROR("Failed to add hpd irq id!\n");
		return r;
	}

	register_hpd_handlers(adev);

	return 0;
}
#endif

static int amdgpu_dm_mode_config_init(struct amdgpu_device *adev)
{
	int r;

	adev->mode_info.mode_config_initialized = true;

	adev->ddev->mode_config.funcs = (void *)&amdgpu_dm_mode_funcs;
	adev->ddev->mode_config.helper_private = &amdgpu_dm_mode_config_helperfuncs;

	adev->ddev->mode_config.max_width = 16384;
	adev->ddev->mode_config.max_height = 16384;

	adev->ddev->mode_config.preferred_depth = 24;
	adev->ddev->mode_config.prefer_shadow = 1;
	/* indicate support of immediate flip */
	adev->ddev->mode_config.async_page_flip = true;

	adev->ddev->mode_config.fb_base = adev->mc.aper_base;

	r = amdgpu_modeset_create_props(adev);
	if (r)
		return r;

	return 0;
}

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

static int amdgpu_dm_backlight_update_status(struct backlight_device *bd)
{
	struct amdgpu_display_manager *dm = bl_get_data(bd);

	if (dc_link_set_backlight_level(dm->backlight_link,
			bd->props.brightness, 0, 0))
		return 0;
	else
		return 1;
}

static int amdgpu_dm_backlight_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static const struct backlight_ops amdgpu_dm_backlight_ops = {
	.get_brightness = amdgpu_dm_backlight_get_brightness,
	.update_status	= amdgpu_dm_backlight_update_status,
};

void amdgpu_dm_register_backlight_device(struct amdgpu_display_manager *dm)
{
	char bl_name[16];
	struct backlight_properties props = { 0 };

	props.max_brightness = AMDGPU_MAX_BL_LEVEL;
	props.type = BACKLIGHT_RAW;

	snprintf(bl_name, sizeof(bl_name), "amdgpu_bl%d",
			dm->adev->ddev->primary->index);

	dm->backlight_dev = backlight_device_register(bl_name,
			dm->adev->ddev->dev,
			dm,
			&amdgpu_dm_backlight_ops,
			&props);

	if (NULL == dm->backlight_dev)
		DRM_ERROR("DM: Backlight registration failed!\n");
	else
		DRM_INFO("DM: Registered Backlight device: %s\n", bl_name);
}

#endif

/* In this architecture, the association
 * connector -> encoder -> crtc
 * id not really requried. The crtc and connector will hold the
 * display_index as an abstraction to use with DAL component
 *
 * Returns 0 on success
 */
int amdgpu_dm_initialize_drm_device(struct amdgpu_device *adev)
{
	struct amdgpu_display_manager *dm = &adev->dm;
	uint32_t i;
	struct amdgpu_connector *aconnector = NULL;
	struct amdgpu_encoder *aencoder = NULL;
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	uint32_t link_cnt;
	unsigned long possible_crtcs;

	link_cnt = dm->dc->caps.max_links;
	if (amdgpu_dm_mode_config_init(dm->adev)) {
		DRM_ERROR("DM: Failed to initialize mode config\n");
		return -1;
	}

	for (i = 0; i < dm->dc->caps.max_surfaces; i++) {
		mode_info->planes[i] = kzalloc(sizeof(struct amdgpu_plane),
								 GFP_KERNEL);
		if (!mode_info->planes[i]) {
			DRM_ERROR("KMS: Failed to allocate surface\n");
			goto fail_free_planes;
		}
		mode_info->planes[i]->base.type = mode_info->plane_type[i];

		/*
		 * HACK: IGT tests expect that each plane can only have one
		 * one possible CRTC. For now, set one CRTC for each
		 * plane that is not an underlay, but still allow multiple
		 * CRTCs for underlay planes.
		 */
		possible_crtcs = 1 << i;
		if (i >= dm->dc->caps.max_streams)
			possible_crtcs = 0xff;

		if (amdgpu_dm_plane_init(dm, mode_info->planes[i], possible_crtcs)) {
			DRM_ERROR("KMS: Failed to initialize plane\n");
			goto fail_free_planes;
		}
	}

	for (i = 0; i < dm->dc->caps.max_streams; i++)
		if (amdgpu_dm_crtc_init(dm, &mode_info->planes[i]->base, i)) {
			DRM_ERROR("KMS: Failed to initialize crtc\n");
			goto fail_free_planes;
		}

	dm->display_indexes_num = dm->dc->caps.max_streams;

	/* loops over all connectors on the board */
	for (i = 0; i < link_cnt; i++) {

		if (i > AMDGPU_DM_MAX_DISPLAY_INDEX) {
			DRM_ERROR(
				"KMS: Cannot support more than %d display indexes\n",
					AMDGPU_DM_MAX_DISPLAY_INDEX);
			continue;
		}

		aconnector = kzalloc(sizeof(*aconnector), GFP_KERNEL);
		if (!aconnector)
			goto fail_free_planes;

		aencoder = kzalloc(sizeof(*aencoder), GFP_KERNEL);
		if (!aencoder) {
			goto fail_free_connector;
		}

		if (amdgpu_dm_encoder_init(dm->ddev, aencoder, i)) {
			DRM_ERROR("KMS: Failed to initialize encoder\n");
			goto fail_free_encoder;
		}

		if (amdgpu_dm_connector_init(dm, aconnector, i, aencoder)) {
			DRM_ERROR("KMS: Failed to initialize connector\n");
			goto fail_free_encoder;
		}

		if (dc_link_detect(dc_get_link_at_index(dm->dc, i), true))
			amdgpu_dm_update_connector_after_detect(aconnector);
	}

	/* Software is initialized. Now we can register interrupt handlers. */
	switch (adev->asic_type) {
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
	case CHIP_POLARIS11:
	case CHIP_POLARIS10:
	case CHIP_POLARIS12:
	case CHIP_VEGA10:
		if (dce110_register_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail_free_encoder;
		}
		break;
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	case CHIP_RAVEN:
		if (dcn10_register_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail_free_encoder;
		}
		break;
#endif
	default:
		DRM_ERROR("Usupported ASIC type: 0x%X\n", adev->asic_type);
		goto fail_free_encoder;
	}

	drm_mode_config_reset(dm->ddev);

	return 0;
fail_free_encoder:
	kfree(aencoder);
fail_free_connector:
	kfree(aconnector);
fail_free_planes:
	for (i = 0; i < dm->dc->caps.max_surfaces; i++)
		kfree(mode_info->planes[i]);
	return -1;
}

void amdgpu_dm_destroy_drm_device(struct amdgpu_display_manager *dm)
{
	drm_mode_config_cleanup(dm->ddev);
	return;
}

/******************************************************************************
 * amdgpu_display_funcs functions
 *****************************************************************************/

/**
 * dm_bandwidth_update - program display watermarks
 *
 * @adev: amdgpu_device pointer
 *
 * Calculate and program the display watermarks and line buffer allocation.
 */
static void dm_bandwidth_update(struct amdgpu_device *adev)
{
	/* TODO: implement later */
}

static void dm_set_backlight_level(struct amdgpu_encoder *amdgpu_encoder,
				     u8 level)
{
	/* TODO: translate amdgpu_encoder to display_index and call DAL */
}

static u8 dm_get_backlight_level(struct amdgpu_encoder *amdgpu_encoder)
{
	/* TODO: translate amdgpu_encoder to display_index and call DAL */
	return 0;
}

static int amdgpu_notify_freesync(struct drm_device *dev, void *data,
				struct drm_file *filp)
{
	struct mod_freesync_params freesync_params;
	uint8_t num_streams;
	uint8_t i;

	struct amdgpu_device *adev = dev->dev_private;
	int r = 0;

	/* Get freesync enable flag from DRM */

	num_streams = dc_get_current_stream_count(adev->dm.dc);

	for (i = 0; i < num_streams; i++) {
		const struct dc_stream *stream;
		stream = dc_get_stream_at_index(adev->dm.dc, i);

		mod_freesync_update_state(adev->dm.freesync_module,
					  &stream, 1, &freesync_params);
	}

	return r;
}

static const struct amdgpu_display_funcs dm_display_funcs = {
	.bandwidth_update = dm_bandwidth_update, /* called unconditionally */
	.vblank_get_counter = dm_vblank_get_counter,/* called unconditionally */
	.vblank_wait = NULL,
	.backlight_set_level =
		dm_set_backlight_level,/* called unconditionally */
	.backlight_get_level =
		dm_get_backlight_level,/* called unconditionally */
	.hpd_sense = NULL,/* called unconditionally */
	.hpd_set_polarity = NULL, /* called unconditionally */
	.hpd_get_gpio_reg = NULL, /* VBIOS parsing. DAL does it. */
	.page_flip_get_scanoutpos =
		dm_crtc_get_scanoutpos,/* called unconditionally */
	.add_encoder = NULL, /* VBIOS parsing. DAL does it. */
	.add_connector = NULL, /* VBIOS parsing. DAL does it. */
	.notify_freesync = amdgpu_notify_freesync,

};


#if defined(CONFIG_DEBUG_KERNEL_DC)

static ssize_t s3_debug_store(
	struct device *device,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret;
	int s3_state;
	struct pci_dev *pdev = to_pci_dev(device);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct amdgpu_device *adev = drm_dev->dev_private;

	ret = kstrtoint(buf, 0, &s3_state);

	if (ret == 0) {
		if (s3_state) {
			dm_resume(adev);
			amdgpu_dm_display_resume(adev);
			drm_kms_helper_hotplug_event(adev->ddev);
		} else
			dm_suspend(adev);
	}

	return ret == 0 ? count : 0;
}

DEVICE_ATTR_WO(s3_debug);

#endif

static int dm_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->ddev->driver->driver_features |= DRIVER_ATOMIC;
	amdgpu_dm_set_irq_funcs(adev);

	switch (adev->asic_type) {
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		adev->mode_info.plane_type = dm_surfaces_type_default;
		break;
	case CHIP_FIJI:
	case CHIP_TONGA:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 7;
		adev->mode_info.plane_type = dm_surfaces_type_default;
		break;
	case CHIP_CARRIZO:
		adev->mode_info.num_crtc = 3;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		adev->mode_info.plane_type = dm_surfaces_type_carizzo;
		break;
	case CHIP_STONEY:
		adev->mode_info.num_crtc = 2;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		adev->mode_info.plane_type = dm_surfaces_type_stoney;
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		adev->mode_info.num_crtc = 5;
		adev->mode_info.num_hpd = 5;
		adev->mode_info.num_dig = 5;
		adev->mode_info.plane_type = dm_surfaces_type_default;
		break;
	case CHIP_POLARIS10:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		adev->mode_info.plane_type = dm_surfaces_type_default;
		break;
	case CHIP_VEGA10:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		adev->mode_info.plane_type = dm_surfaces_type_default;
		break;
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	case CHIP_RAVEN:
		adev->mode_info.num_crtc = 4;
		adev->mode_info.num_hpd = 4;
		adev->mode_info.num_dig = 4;
		adev->mode_info.plane_type = dm_surfaces_type_default;
		break;
#endif
	default:
		DRM_ERROR("Usupported ASIC type: 0x%X\n", adev->asic_type);
		return -EINVAL;
	}

	if (adev->mode_info.funcs == NULL)
		adev->mode_info.funcs = &dm_display_funcs;

	/* Note: Do NOT change adev->audio_endpt_rreg and
	 * adev->audio_endpt_wreg because they are initialised in
	 * amdgpu_device_init() */
#if defined(CONFIG_DEBUG_KERNEL_DC)
	device_create_file(
		adev->ddev->dev,
		&dev_attr_s3_debug);
#endif

	return 0;
}

bool amdgpu_dm_acquire_dal_lock(struct amdgpu_display_manager *dm)
{
	/* TODO */
	return true;
}

bool amdgpu_dm_release_dal_lock(struct amdgpu_display_manager *dm)
{
	/* TODO */
	return true;
}


