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
#include "dc/inc/core_types.h"

#include "vid.h"
#include "amdgpu.h"
#include "amdgpu_display.h"
#include "atom.h"
#include "amdgpu_dm.h"
#include "amdgpu_pm.h"

#include "amd_shared.h"
#include "amdgpu_dm_irq.h"
#include "dm_helpers.h"
#include "dm_services_types.h"
#include "amdgpu_dm_mst_types.h"
#if defined(CONFIG_DEBUG_FS)
#include "amdgpu_dm_debugfs.h"
#endif

#include "ivsrcid/ivsrcid_vislands30.h"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/pm_runtime.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_mst_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_edid.h>

#include "modules/inc/mod_freesync.h"

#ifdef CONFIG_X86
#include "ivsrcid/irqsrcs_dcn_1_0.h"

#include "dcn/dcn_1_0_offset.h"
#include "dcn/dcn_1_0_sh_mask.h"
#include "soc15_hw_ip.h"
#include "vega10_ip_offset.h"

#include "soc15_common.h"
#endif

#include "modules/inc/mod_freesync.h"

#include "i2caux_interface.h"

/* basic init/fini API */
static int amdgpu_dm_init(struct amdgpu_device *adev);
static void amdgpu_dm_fini(struct amdgpu_device *adev);

/* initializes drm_device display related structures, based on the information
 * provided by DAL. The drm strcutures are: drm_crtc, drm_connector,
 * drm_encoder, drm_mode_config
 *
 * Returns 0 on success
 */
static int amdgpu_dm_initialize_drm_device(struct amdgpu_device *adev);
/* removes and deallocates the drm structures, created by the above function */
static void amdgpu_dm_destroy_drm_device(struct amdgpu_display_manager *dm);

static void
amdgpu_dm_update_connector_after_detect(struct amdgpu_dm_connector *aconnector);

static int amdgpu_dm_plane_init(struct amdgpu_display_manager *dm,
				struct amdgpu_plane *aplane,
				unsigned long possible_crtcs);
static int amdgpu_dm_crtc_init(struct amdgpu_display_manager *dm,
			       struct drm_plane *plane,
			       uint32_t link_index);
static int amdgpu_dm_connector_init(struct amdgpu_display_manager *dm,
				    struct amdgpu_dm_connector *amdgpu_dm_connector,
				    uint32_t link_index,
				    struct amdgpu_encoder *amdgpu_encoder);
static int amdgpu_dm_encoder_init(struct drm_device *dev,
				  struct amdgpu_encoder *aencoder,
				  uint32_t link_index);

static int amdgpu_dm_connector_get_modes(struct drm_connector *connector);

static int amdgpu_dm_atomic_commit(struct drm_device *dev,
				   struct drm_atomic_state *state,
				   bool nonblock);

static void amdgpu_dm_atomic_commit_tail(struct drm_atomic_state *state);

static int amdgpu_dm_atomic_check(struct drm_device *dev,
				  struct drm_atomic_state *state);




static const enum drm_plane_type dm_plane_type_default[AMDGPU_MAX_PLANES] = {
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
};

static const enum drm_plane_type dm_plane_type_carizzo[AMDGPU_MAX_PLANES] = {
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_OVERLAY,/* YUV Capable Underlay */
};

static const enum drm_plane_type dm_plane_type_stoney[AMDGPU_MAX_PLANES] = {
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
			DRM_ERROR("dc_stream_state is NULL for crtc '%d'!\n",
				  crtc);
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
			DRM_ERROR("dc_stream_state is NULL for crtc '%d'!\n",
				  crtc);
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

static struct amdgpu_crtc *
get_crtc_by_otg_inst(struct amdgpu_device *adev,
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
	amdgpu_dm_crtc_handle_crc_irq(&acrtc->base);
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

/* Allocate memory for FBC compressed data  */
static void amdgpu_dm_fbc_init(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct dm_comressor_info *compressor = &adev->dm.compressor;
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
		if (max_size < mode->htotal * mode->vtotal)
			max_size = mode->htotal * mode->vtotal;
	}

	if (max_size) {
		int r = amdgpu_bo_create_kernel(adev, max_size * 4, PAGE_SIZE,
			    AMDGPU_GEM_DOMAIN_GTT, &compressor->bo_ptr,
			    &compressor->gpu_addr, &compressor->cpu_addr);

		if (r)
			DRM_ERROR("DM: Failed to initialize FBC\n");
		else {
			adev->dm.dc->ctx->fbc_gpu_addr = compressor->gpu_addr;
			DRM_INFO("DM: FBC alloc %lu\n", max_size*4);
		}

	}

}


/* Init display KMS
 *
 * Returns 0 on success
 */
static int amdgpu_dm_init(struct amdgpu_device *adev)
{
	struct dc_init_data init_data;
	adev->dm.ddev = adev->ddev;
	adev->dm.adev = adev;

	/* Zero all the fields */
	memset(&init_data, 0, sizeof(init_data));

	if(amdgpu_dm_irq_init(adev)) {
		DRM_ERROR("amdgpu: failed to initialize DM IRQ support.\n");
		goto error;
	}

	init_data.asic_id.chip_family = adev->family;

	init_data.asic_id.pci_revision_id = adev->rev_id;
	init_data.asic_id.hw_internal_rev = adev->external_rev_id;

	init_data.asic_id.vram_width = adev->gmc.vram_width;
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

	/*
	 * TODO debug why this doesn't work on Raven
	 */
	if (adev->flags & AMD_IS_APU &&
	    adev->asic_type >= CHIP_CARRIZO &&
	    adev->asic_type < CHIP_RAVEN)
		init_data.flags.gpu_vm_support = true;

	/* Display Core create. */
	adev->dm.dc = dc_create(&init_data);

	if (adev->dm.dc) {
		DRM_INFO("Display Core initialized with v%s!\n", DC_VER);
	} else {
		DRM_INFO("Display Core failed to initialize with v%s!\n", DC_VER);
		goto error;
	}

	INIT_WORK(&adev->dm.mst_hotplug_work, hotplug_notify_work_func);

	adev->dm.freesync_module = mod_freesync_create(adev->dm.dc);
	if (!adev->dm.freesync_module) {
		DRM_ERROR(
		"amdgpu: failed to initialize freesync_module.\n");
	} else
		DRM_DEBUG_DRIVER("amdgpu: freesync_module init done %p.\n",
				adev->dm.freesync_module);

	amdgpu_dm_init_color_mod();

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

	DRM_DEBUG_DRIVER("KMS initialized.\n");

	return 0;
error:
	amdgpu_dm_fini(adev);

	return -1;
}

static void amdgpu_dm_fini(struct amdgpu_device *adev)
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
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	int ret = 0;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->dc_link->type == dc_connection_mst_branch &&
		    aconnector->mst_mgr.aux) {
			DRM_DEBUG_DRIVER("DM_MST: starting TM on aconnector: %p [id: %d]\n",
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
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return detect_mst_link_for_all_connectors(adev->ddev);
}

static void s3_handle_mst(struct drm_device *dev, bool suspend)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		   aconnector = to_amdgpu_dm_connector(connector);
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

	WARN_ON(adev->dm.cached_state);
	adev->dm.cached_state = drm_atomic_helper_suspend(adev->ddev);

	dc_set_power_state(dm->dc, DC_ACPI_CM_POWER_STATE_D3);

	return ret;
}

static struct amdgpu_dm_connector *
amdgpu_dm_find_first_crtc_matching_connector(struct drm_atomic_state *state,
					     struct drm_crtc *crtc)
{
	uint32_t i;
	struct drm_connector_state *new_con_state;
	struct drm_connector *connector;
	struct drm_crtc *crtc_from_state;

	for_each_new_connector_in_state(state, connector, new_con_state, i) {
		crtc_from_state = new_con_state->crtc;

		if (crtc_from_state == crtc)
			return to_amdgpu_dm_connector(connector);
	}

	return NULL;
}

static int dm_resume(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct drm_device *ddev = adev->ddev;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct dm_crtc_state *dm_new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state;
	struct dm_plane_state *dm_new_plane_state;
	int ret;
	int i;

	/* power on hardware */
	dc_set_power_state(dm->dc, DC_ACPI_CM_POWER_STATE_D0);

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
	list_for_each_entry(connector, &ddev->mode_config.connector_list, head) {
		aconnector = to_amdgpu_dm_connector(connector);

		/*
		 * this is the case when traversing through already created
		 * MST connectors, should be skipped
		 */
		if (aconnector->mst_port)
			continue;

		mutex_lock(&aconnector->hpd_lock);
		dc_link_detect(aconnector->dc_link, DETECT_REASON_HPD);

		if (aconnector->fake_enable && aconnector->dc_link->local_sink)
			aconnector->fake_enable = false;

		aconnector->dc_sink = NULL;
		amdgpu_dm_update_connector_after_detect(aconnector);
		mutex_unlock(&aconnector->hpd_lock);
	}

	/* Force mode set in atomic comit */
	for_each_new_crtc_in_state(dm->cached_state, crtc, new_crtc_state, i)
		new_crtc_state->active_changed = true;

	/*
	 * atomic_check is expected to create the dc states. We need to release
	 * them here, since they were duplicated as part of the suspend
	 * procedure.
	 */
	for_each_new_crtc_in_state(dm->cached_state, crtc, new_crtc_state, i) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		if (dm_new_crtc_state->stream) {
			WARN_ON(kref_read(&dm_new_crtc_state->stream->refcount) > 1);
			dc_stream_release(dm_new_crtc_state->stream);
			dm_new_crtc_state->stream = NULL;
		}
	}

	for_each_new_plane_in_state(dm->cached_state, plane, new_plane_state, i) {
		dm_new_plane_state = to_dm_plane_state(new_plane_state);
		if (dm_new_plane_state->dc_state) {
			WARN_ON(kref_read(&dm_new_plane_state->dc_state->refcount) > 1);
			dc_plane_state_release(dm_new_plane_state->dc_state);
			dm_new_plane_state->dc_state = NULL;
		}
	}

	ret = drm_atomic_helper_resume(ddev, dm->cached_state);

	dm->cached_state = NULL;

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


static struct drm_atomic_state *
dm_atomic_state_alloc(struct drm_device *dev)
{
	struct dm_atomic_state *state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (!state)
		return NULL;

	if (drm_atomic_state_init(dev, &state->base) < 0)
		goto fail;

	return &state->base;

fail:
	kfree(state);
	return NULL;
}

static void
dm_atomic_state_clear(struct drm_atomic_state *state)
{
	struct dm_atomic_state *dm_state = to_dm_atomic_state(state);

	if (dm_state->context) {
		dc_release_state(dm_state->context);
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
	.fb_create = amdgpu_display_user_framebuffer_create,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.atomic_check = amdgpu_dm_atomic_check,
	.atomic_commit = amdgpu_dm_atomic_commit,
	.atomic_state_alloc = dm_atomic_state_alloc,
	.atomic_state_clear = dm_atomic_state_clear,
	.atomic_state_free = dm_atomic_state_alloc_free
};

static struct drm_mode_config_helper_funcs amdgpu_dm_mode_config_helperfuncs = {
	.atomic_commit_tail = amdgpu_dm_atomic_commit_tail
};

static void
amdgpu_dm_update_connector_after_detect(struct amdgpu_dm_connector *aconnector)
{
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct dc_sink *sink;

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
		DRM_DEBUG_DRIVER("DCHPD: connector_id=%d: dc_sink didn't change.\n",
				aconnector->connector_id);
		return;
	}

	DRM_DEBUG_DRIVER("DCHPD: connector_id=%d: Old sink=%p New sink=%p\n",
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
		if (sink->dc_edid.length == 0) {
			aconnector->edid = NULL;
		} else {
			aconnector->edid =
				(struct edid *) sink->dc_edid.raw_edid;


			drm_connector_update_edid_property(connector,
					aconnector->edid);
		}
		amdgpu_dm_add_sink_to_freesync_module(connector, aconnector->edid);

	} else {
		amdgpu_dm_remove_sink_from_freesync_module(connector);
		drm_connector_update_edid_property(connector, NULL);
		aconnector->num_modes = 0;
		aconnector->dc_sink = NULL;
		aconnector->edid = NULL;
	}

	mutex_unlock(&dev->mode_config.mutex);
}

static void handle_hpd_irq(void *param)
{
	struct amdgpu_dm_connector *aconnector = (struct amdgpu_dm_connector *)param;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;

	/* In case of failure or MST no need to update connector status or notify the OS
	 * since (for MST case) MST does this in it's own context.
	 */
	mutex_lock(&aconnector->hpd_lock);

	if (aconnector->fake_enable)
		aconnector->fake_enable = false;

	if (dc_link_detect(aconnector->dc_link, DETECT_REASON_HPD)) {
		amdgpu_dm_update_connector_after_detect(aconnector);


		drm_modeset_lock_all(dev);
		dm_restore_drm_connector_state(dev, connector);
		drm_modeset_unlock_all(dev);

		if (aconnector->base.force == DRM_FORCE_UNSPECIFIED)
			drm_kms_helper_hotplug_event(dev);
	}
	mutex_unlock(&aconnector->hpd_lock);

}

static void dm_handle_hpd_rx_irq(struct amdgpu_dm_connector *aconnector)
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

		DRM_DEBUG_DRIVER("ESI %02x %02x %02x\n", esi[0], esi[1], esi[2]);
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
		} else {
			break;
		}
	}

	if (process_count == max_process_count)
		DRM_DEBUG_DRIVER("Loop exceeded max iterations\n");
}

static void handle_hpd_rx_irq(void *param)
{
	struct amdgpu_dm_connector *aconnector = (struct amdgpu_dm_connector *)param;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct dc_link *dc_link = aconnector->dc_link;
	bool is_mst_root_connector = aconnector->mst_mgr.mst_state;

	/* TODO:Temporary add mutex to protect hpd interrupt not have a gpio
	 * conflict, after implement i2c helper, this mutex should be
	 * retired.
	 */
	if (dc_link->type != dc_connection_mst_branch)
		mutex_lock(&aconnector->hpd_lock);

	if (dc_link_handle_hpd_rx_irq(dc_link, NULL, NULL) &&
			!is_mst_root_connector) {
		/* Downstream Port status changed. */
		if (dc_link_detect(dc_link, DETECT_REASON_HPDRX)) {

			if (aconnector->fake_enable)
				aconnector->fake_enable = false;

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

	if (dc_link->type != dc_connection_mst_branch)
		mutex_unlock(&aconnector->hpd_lock);
}

static void register_hpd_handlers(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev->ddev;
	struct drm_connector *connector;
	struct amdgpu_dm_connector *aconnector;
	const struct dc_link *dc_link;
	struct dc_interrupt_params int_params = {0};

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	list_for_each_entry(connector,
			&dev->mode_config.connector_list, head)	{

		aconnector = to_amdgpu_dm_connector(connector);
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
	    adev->asic_type == CHIP_VEGA12 ||
	    adev->asic_type == CHIP_VEGA20 ||
	    adev->asic_type == CHIP_RAVEN)
		client_id = SOC15_IH_CLIENTID_DCE;

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

#ifdef CONFIG_X86
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
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, i, &adev->crtc_irq);

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
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, i, &adev->pageflip_irq);
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
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, DCN_1_0__SRCID__DC_HPD1_INT,
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

	adev->ddev->mode_config.fb_base = adev->gmc.aper_base;

	r = amdgpu_display_modeset_create_props(adev);
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

static void
amdgpu_dm_register_backlight_device(struct amdgpu_display_manager *dm)
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

	if (IS_ERR(dm->backlight_dev))
		DRM_ERROR("DM: Backlight registration failed!\n");
	else
		DRM_DEBUG_DRIVER("DM: Registered Backlight device: %s\n", bl_name);
}

#endif

static int initialize_plane(struct amdgpu_display_manager *dm,
			     struct amdgpu_mode_info *mode_info,
			     int plane_id)
{
	struct amdgpu_plane *plane;
	unsigned long possible_crtcs;
	int ret = 0;

	plane = kzalloc(sizeof(struct amdgpu_plane), GFP_KERNEL);
	mode_info->planes[plane_id] = plane;

	if (!plane) {
		DRM_ERROR("KMS: Failed to allocate plane\n");
		return -ENOMEM;
	}
	plane->base.type = mode_info->plane_type[plane_id];

	/*
	 * HACK: IGT tests expect that each plane can only have one
	 * one possible CRTC. For now, set one CRTC for each
	 * plane that is not an underlay, but still allow multiple
	 * CRTCs for underlay planes.
	 */
	possible_crtcs = 1 << plane_id;
	if (plane_id >= dm->dc->caps.max_streams)
		possible_crtcs = 0xff;

	ret = amdgpu_dm_plane_init(dm, mode_info->planes[plane_id], possible_crtcs);

	if (ret) {
		DRM_ERROR("KMS: Failed to initialize plane\n");
		return ret;
	}

	return ret;
}


static void register_backlight_device(struct amdgpu_display_manager *dm,
				      struct dc_link *link)
{
#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

	if ((link->connector_signal & (SIGNAL_TYPE_EDP | SIGNAL_TYPE_LVDS)) &&
	    link->type != dc_connection_none) {
		/* Event if registration failed, we should continue with
		 * DM initialization because not having a backlight control
		 * is better then a black screen.
		 */
		amdgpu_dm_register_backlight_device(dm);

		if (dm->backlight_dev)
			dm->backlight_link = link;
	}
#endif
}


/* In this architecture, the association
 * connector -> encoder -> crtc
 * id not really requried. The crtc and connector will hold the
 * display_index as an abstraction to use with DAL component
 *
 * Returns 0 on success
 */
static int amdgpu_dm_initialize_drm_device(struct amdgpu_device *adev)
{
	struct amdgpu_display_manager *dm = &adev->dm;
	int32_t i;
	struct amdgpu_dm_connector *aconnector = NULL;
	struct amdgpu_encoder *aencoder = NULL;
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	uint32_t link_cnt;
	int32_t total_overlay_planes, total_primary_planes;

	link_cnt = dm->dc->caps.max_links;
	if (amdgpu_dm_mode_config_init(dm->adev)) {
		DRM_ERROR("DM: Failed to initialize mode config\n");
		return -1;
	}

	/* Identify the number of planes to be initialized */
	total_overlay_planes = dm->dc->caps.max_slave_planes;
	total_primary_planes = dm->dc->caps.max_planes - dm->dc->caps.max_slave_planes;

	/* First initialize overlay planes, index starting after primary planes */
	for (i = (total_overlay_planes - 1); i >= 0; i--) {
		if (initialize_plane(dm, mode_info, (total_primary_planes + i))) {
			DRM_ERROR("KMS: Failed to initialize overlay plane\n");
			goto fail;
		}
	}

	/* Initialize primary planes */
	for (i = (total_primary_planes - 1); i >= 0; i--) {
		if (initialize_plane(dm, mode_info, i)) {
			DRM_ERROR("KMS: Failed to initialize primary plane\n");
			goto fail;
		}
	}

	for (i = 0; i < dm->dc->caps.max_streams; i++)
		if (amdgpu_dm_crtc_init(dm, &mode_info->planes[i]->base, i)) {
			DRM_ERROR("KMS: Failed to initialize crtc\n");
			goto fail;
		}

	dm->display_indexes_num = dm->dc->caps.max_streams;

	/* loops over all connectors on the board */
	for (i = 0; i < link_cnt; i++) {
		struct dc_link *link = NULL;

		if (i > AMDGPU_DM_MAX_DISPLAY_INDEX) {
			DRM_ERROR(
				"KMS: Cannot support more than %d display indexes\n",
					AMDGPU_DM_MAX_DISPLAY_INDEX);
			continue;
		}

		aconnector = kzalloc(sizeof(*aconnector), GFP_KERNEL);
		if (!aconnector)
			goto fail;

		aencoder = kzalloc(sizeof(*aencoder), GFP_KERNEL);
		if (!aencoder)
			goto fail;

		if (amdgpu_dm_encoder_init(dm->ddev, aencoder, i)) {
			DRM_ERROR("KMS: Failed to initialize encoder\n");
			goto fail;
		}

		if (amdgpu_dm_connector_init(dm, aconnector, i, aencoder)) {
			DRM_ERROR("KMS: Failed to initialize connector\n");
			goto fail;
		}

		link = dc_get_link_at_index(dm->dc, i);

		if (dc_link_detect(link, DETECT_REASON_BOOT)) {
			amdgpu_dm_update_connector_after_detect(aconnector);
			register_backlight_device(dm, link);
		}


	}

	/* Software is initialized. Now we can register interrupt handlers. */
	switch (adev->asic_type) {
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_MULLINS:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
	case CHIP_POLARIS11:
	case CHIP_POLARIS10:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
		if (dce110_register_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail;
		}
		break;
#ifdef CONFIG_X86
	case CHIP_RAVEN:
		if (dcn10_register_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail;
		}
		/*
		 * Temporary disable until pplib/smu interaction is implemented
		 */
		dm->dc->debug.disable_stutter = amdgpu_pp_feature_mask & PP_STUTTER_MODE ? false : true;
		break;
#endif
	default:
		DRM_ERROR("Unsupported ASIC type: 0x%X\n", adev->asic_type);
		goto fail;
	}

	return 0;
fail:
	kfree(aencoder);
	kfree(aconnector);
	for (i = 0; i < dm->dc->caps.max_planes; i++)
		kfree(mode_info->planes[i]);
	return -1;
}

static void amdgpu_dm_destroy_drm_device(struct amdgpu_display_manager *dm)
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
		struct dc_stream_state *stream;
		stream = dc_get_stream_at_index(adev->dm.dc, i);

		mod_freesync_update_state(adev->dm.freesync_module,
					  &stream, 1, &freesync_params);
	}

	return r;
}

static const struct amdgpu_display_funcs dm_display_funcs = {
	.bandwidth_update = dm_bandwidth_update, /* called unconditionally */
	.vblank_get_counter = dm_vblank_get_counter,/* called unconditionally */
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

static ssize_t s3_debug_store(struct device *device,
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

	switch (adev->asic_type) {
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
	case CHIP_KAVERI:
		adev->mode_info.num_crtc = 4;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 7;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
	case CHIP_KABINI:
	case CHIP_MULLINS:
		adev->mode_info.num_crtc = 2;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
	case CHIP_FIJI:
	case CHIP_TONGA:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 7;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
	case CHIP_CARRIZO:
		adev->mode_info.num_crtc = 3;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		adev->mode_info.plane_type = dm_plane_type_carizzo;
		break;
	case CHIP_STONEY:
		adev->mode_info.num_crtc = 2;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		adev->mode_info.plane_type = dm_plane_type_stoney;
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		adev->mode_info.num_crtc = 5;
		adev->mode_info.num_hpd = 5;
		adev->mode_info.num_dig = 5;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
	case CHIP_POLARIS10:
	case CHIP_VEGAM:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
#ifdef CONFIG_X86
	case CHIP_RAVEN:
		adev->mode_info.num_crtc = 4;
		adev->mode_info.num_hpd = 4;
		adev->mode_info.num_dig = 4;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
#endif
	default:
		DRM_ERROR("Unsupported ASIC type: 0x%X\n", adev->asic_type);
		return -EINVAL;
	}

	amdgpu_dm_set_irq_funcs(adev);

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

static bool modeset_required(struct drm_crtc_state *crtc_state,
			     struct dc_stream_state *new_stream,
			     struct dc_stream_state *old_stream)
{
	if (!drm_atomic_crtc_needs_modeset(crtc_state))
		return false;

	if (!crtc_state->enable)
		return false;

	return crtc_state->active;
}

static bool modereset_required(struct drm_crtc_state *crtc_state)
{
	if (!drm_atomic_crtc_needs_modeset(crtc_state))
		return false;

	return !crtc_state->enable || !crtc_state->active;
}

static void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs amdgpu_dm_encoder_funcs = {
	.destroy = amdgpu_dm_encoder_destroy,
};

static bool fill_rects_from_plane_state(const struct drm_plane_state *state,
					struct dc_plane_state *plane_state)
{
	plane_state->src_rect.x = state->src_x >> 16;
	plane_state->src_rect.y = state->src_y >> 16;
	/*we ignore for now mantissa and do not to deal with floating pixels :(*/
	plane_state->src_rect.width = state->src_w >> 16;

	if (plane_state->src_rect.width == 0)
		return false;

	plane_state->src_rect.height = state->src_h >> 16;
	if (plane_state->src_rect.height == 0)
		return false;

	plane_state->dst_rect.x = state->crtc_x;
	plane_state->dst_rect.y = state->crtc_y;

	if (state->crtc_w == 0)
		return false;

	plane_state->dst_rect.width = state->crtc_w;

	if (state->crtc_h == 0)
		return false;

	plane_state->dst_rect.height = state->crtc_h;

	plane_state->clip_rect = plane_state->dst_rect;

	switch (state->rotation & DRM_MODE_ROTATE_MASK) {
	case DRM_MODE_ROTATE_0:
		plane_state->rotation = ROTATION_ANGLE_0;
		break;
	case DRM_MODE_ROTATE_90:
		plane_state->rotation = ROTATION_ANGLE_90;
		break;
	case DRM_MODE_ROTATE_180:
		plane_state->rotation = ROTATION_ANGLE_180;
		break;
	case DRM_MODE_ROTATE_270:
		plane_state->rotation = ROTATION_ANGLE_270;
		break;
	default:
		plane_state->rotation = ROTATION_ANGLE_0;
		break;
	}

	return true;
}
static int get_fb_info(const struct amdgpu_framebuffer *amdgpu_fb,
		       uint64_t *tiling_flags)
{
	struct amdgpu_bo *rbo = gem_to_amdgpu_bo(amdgpu_fb->base.obj[0]);
	int r = amdgpu_bo_reserve(rbo, false);

	if (unlikely(r)) {
		// Don't show error msg. when return -ERESTARTSYS
		if (r != -ERESTARTSYS)
			DRM_ERROR("Unable to reserve buffer: %d\n", r);
		return r;
	}

	if (tiling_flags)
		amdgpu_bo_get_tiling_flags(rbo, tiling_flags);

	amdgpu_bo_unreserve(rbo);

	return r;
}

static int fill_plane_attributes_from_fb(struct amdgpu_device *adev,
					 struct dc_plane_state *plane_state,
					 const struct amdgpu_framebuffer *amdgpu_fb)
{
	uint64_t tiling_flags;
	unsigned int awidth;
	const struct drm_framebuffer *fb = &amdgpu_fb->base;
	int ret = 0;
	struct drm_format_name_buf format_name;

	ret = get_fb_info(
		amdgpu_fb,
		&tiling_flags);

	if (ret)
		return ret;

	switch (fb->format->format) {
	case DRM_FORMAT_C8:
		plane_state->format = SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS;
		break;
	case DRM_FORMAT_RGB565:
		plane_state->format = SURFACE_PIXEL_FORMAT_GRPH_RGB565;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		plane_state->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB8888;
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		plane_state->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010;
		break;
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
		plane_state->format = SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010;
		break;
	case DRM_FORMAT_NV21:
		plane_state->format = SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr;
		break;
	case DRM_FORMAT_NV12:
		plane_state->format = SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb;
		break;
	default:
		DRM_ERROR("Unsupported screen format %s\n",
			  drm_get_format_name(fb->format->format, &format_name));
		return -EINVAL;
	}

	if (plane_state->format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
		plane_state->address.type = PLN_ADDR_TYPE_GRAPHICS;
		plane_state->plane_size.grph.surface_size.x = 0;
		plane_state->plane_size.grph.surface_size.y = 0;
		plane_state->plane_size.grph.surface_size.width = fb->width;
		plane_state->plane_size.grph.surface_size.height = fb->height;
		plane_state->plane_size.grph.surface_pitch =
				fb->pitches[0] / fb->format->cpp[0];
		/* TODO: unhardcode */
		plane_state->color_space = COLOR_SPACE_SRGB;

	} else {
		awidth = ALIGN(fb->width, 64);
		plane_state->address.type = PLN_ADDR_TYPE_VIDEO_PROGRESSIVE;
		plane_state->plane_size.video.luma_size.x = 0;
		plane_state->plane_size.video.luma_size.y = 0;
		plane_state->plane_size.video.luma_size.width = awidth;
		plane_state->plane_size.video.luma_size.height = fb->height;
		/* TODO: unhardcode */
		plane_state->plane_size.video.luma_pitch = awidth;

		plane_state->plane_size.video.chroma_size.x = 0;
		plane_state->plane_size.video.chroma_size.y = 0;
		plane_state->plane_size.video.chroma_size.width = awidth;
		plane_state->plane_size.video.chroma_size.height = fb->height;
		plane_state->plane_size.video.chroma_pitch = awidth / 2;

		/* TODO: unhardcode */
		plane_state->color_space = COLOR_SPACE_YCBCR709;
	}

	memset(&plane_state->tiling_info, 0, sizeof(plane_state->tiling_info));

	/* Fill GFX8 params */
	if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == DC_ARRAY_2D_TILED_THIN1) {
		unsigned int bankw, bankh, mtaspect, tile_split, num_banks;

		bankw = AMDGPU_TILING_GET(tiling_flags, BANK_WIDTH);
		bankh = AMDGPU_TILING_GET(tiling_flags, BANK_HEIGHT);
		mtaspect = AMDGPU_TILING_GET(tiling_flags, MACRO_TILE_ASPECT);
		tile_split = AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT);
		num_banks = AMDGPU_TILING_GET(tiling_flags, NUM_BANKS);

		/* XXX fix me for VI */
		plane_state->tiling_info.gfx8.num_banks = num_banks;
		plane_state->tiling_info.gfx8.array_mode =
				DC_ARRAY_2D_TILED_THIN1;
		plane_state->tiling_info.gfx8.tile_split = tile_split;
		plane_state->tiling_info.gfx8.bank_width = bankw;
		plane_state->tiling_info.gfx8.bank_height = bankh;
		plane_state->tiling_info.gfx8.tile_aspect = mtaspect;
		plane_state->tiling_info.gfx8.tile_mode =
				DC_ADDR_SURF_MICRO_TILING_DISPLAY;
	} else if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE)
			== DC_ARRAY_1D_TILED_THIN1) {
		plane_state->tiling_info.gfx8.array_mode = DC_ARRAY_1D_TILED_THIN1;
	}

	plane_state->tiling_info.gfx8.pipe_config =
			AMDGPU_TILING_GET(tiling_flags, PIPE_CONFIG);

	if (adev->asic_type == CHIP_VEGA10 ||
	    adev->asic_type == CHIP_VEGA12 ||
	    adev->asic_type == CHIP_VEGA20 ||
	    adev->asic_type == CHIP_RAVEN) {
		/* Fill GFX9 params */
		plane_state->tiling_info.gfx9.num_pipes =
			adev->gfx.config.gb_addr_config_fields.num_pipes;
		plane_state->tiling_info.gfx9.num_banks =
			adev->gfx.config.gb_addr_config_fields.num_banks;
		plane_state->tiling_info.gfx9.pipe_interleave =
			adev->gfx.config.gb_addr_config_fields.pipe_interleave_size;
		plane_state->tiling_info.gfx9.num_shader_engines =
			adev->gfx.config.gb_addr_config_fields.num_se;
		plane_state->tiling_info.gfx9.max_compressed_frags =
			adev->gfx.config.gb_addr_config_fields.max_compress_frags;
		plane_state->tiling_info.gfx9.num_rb_per_se =
			adev->gfx.config.gb_addr_config_fields.num_rb_per_se;
		plane_state->tiling_info.gfx9.swizzle =
			AMDGPU_TILING_GET(tiling_flags, SWIZZLE_MODE);
		plane_state->tiling_info.gfx9.shaderEnable = 1;
	}

	plane_state->visible = true;
	plane_state->scaling_quality.h_taps_c = 0;
	plane_state->scaling_quality.v_taps_c = 0;

	/* is this needed? is plane_state zeroed at allocation? */
	plane_state->scaling_quality.h_taps = 0;
	plane_state->scaling_quality.v_taps = 0;
	plane_state->stereo_format = PLANE_STEREO_FORMAT_NONE;

	return ret;

}

static int fill_plane_attributes(struct amdgpu_device *adev,
				 struct dc_plane_state *dc_plane_state,
				 struct drm_plane_state *plane_state,
				 struct drm_crtc_state *crtc_state)
{
	const struct amdgpu_framebuffer *amdgpu_fb =
		to_amdgpu_framebuffer(plane_state->fb);
	const struct drm_crtc *crtc = plane_state->crtc;
	int ret = 0;

	if (!fill_rects_from_plane_state(plane_state, dc_plane_state))
		return -EINVAL;

	ret = fill_plane_attributes_from_fb(
		crtc->dev->dev_private,
		dc_plane_state,
		amdgpu_fb);

	if (ret)
		return ret;

	/*
	 * Always set input transfer function, since plane state is refreshed
	 * every time.
	 */
	ret = amdgpu_dm_set_degamma_lut(crtc_state, dc_plane_state);
	if (ret) {
		dc_transfer_func_release(dc_plane_state->in_transfer_func);
		dc_plane_state->in_transfer_func = NULL;
	}

	return ret;
}

/*****************************************************************************/

static void update_stream_scaling_settings(const struct drm_display_mode *mode,
					   const struct dm_connector_state *dm_state,
					   struct dc_stream_state *stream)
{
	enum amdgpu_rmx_type rmx_type;

	struct rect src = { 0 }; /* viewport in composition space*/
	struct rect dst = { 0 }; /* stream addressable area */

	/* no mode. nothing to be done */
	if (!mode)
		return;

	/* Full screen scaling by default */
	src.width = mode->hdisplay;
	src.height = mode->vdisplay;
	dst.width = stream->timing.h_addressable;
	dst.height = stream->timing.v_addressable;

	if (dm_state) {
		rmx_type = dm_state->scaling;
		if (rmx_type == RMX_ASPECT || rmx_type == RMX_OFF) {
			if (src.width * dst.height <
					src.height * dst.width) {
				/* height needs less upscaling/more downscaling */
				dst.width = src.width *
						dst.height / src.height;
			} else {
				/* width needs less upscaling/more downscaling */
				dst.height = src.height *
						dst.width / src.width;
			}
		} else if (rmx_type == RMX_CENTER) {
			dst = src;
		}

		dst.x = (stream->timing.h_addressable - dst.width) / 2;
		dst.y = (stream->timing.v_addressable - dst.height) / 2;

		if (dm_state->underscan_enable) {
			dst.x += dm_state->underscan_hborder / 2;
			dst.y += dm_state->underscan_vborder / 2;
			dst.width -= dm_state->underscan_hborder;
			dst.height -= dm_state->underscan_vborder;
		}
	}

	stream->src = src;
	stream->dst = dst;

	DRM_DEBUG_DRIVER("Destination Rectangle x:%d  y:%d  width:%d  height:%d\n",
			dst.x, dst.y, dst.width, dst.height);

}

static enum dc_color_depth
convert_color_depth_from_display_info(const struct drm_connector *connector)
{
	uint32_t bpc = connector->display_info.bpc;

	switch (bpc) {
	case 0:
		/* Temporary Work around, DRM don't parse color depth for
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

static enum dc_aspect_ratio
get_aspect_ratio(const struct drm_display_mode *mode_in)
{
	int32_t width = mode_in->crtc_hdisplay * 9;
	int32_t height = mode_in->crtc_vdisplay * 16;

	if ((width - height) < 10 && (width - height) > -10)
		return ASPECT_RATIO_16_9;
	else
		return ASPECT_RATIO_4_3;
}

static enum dc_color_space
get_output_color_space(const struct dc_crtc_timing *dc_crtc_timing)
{
	enum dc_color_space color_space = COLOR_SPACE_SRGB;

	switch (dc_crtc_timing->pixel_encoding)	{
	case PIXEL_ENCODING_YCBCR422:
	case PIXEL_ENCODING_YCBCR444:
	case PIXEL_ENCODING_YCBCR420:
	{
		/*
		 * 27030khz is the separation point between HDTV and SDTV
		 * according to HDMI spec, we use YCbCr709 and YCbCr601
		 * respectively
		 */
		if (dc_crtc_timing->pix_clk_khz > 27030) {
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

	}
	break;
	case PIXEL_ENCODING_RGB:
		color_space = COLOR_SPACE_SRGB;
		break;

	default:
		WARN_ON(1);
		break;
	}

	return color_space;
}

static void reduce_mode_colour_depth(struct dc_crtc_timing *timing_out)
{
	if (timing_out->display_color_depth <= COLOR_DEPTH_888)
		return;

	timing_out->display_color_depth--;
}

static void adjust_colour_depth_from_display_info(struct dc_crtc_timing *timing_out,
						const struct drm_display_info *info)
{
	int normalized_clk;
	if (timing_out->display_color_depth <= COLOR_DEPTH_888)
		return;
	do {
		normalized_clk = timing_out->pix_clk_khz;
		/* YCbCr 4:2:0 requires additional adjustment of 1/2 */
		if (timing_out->pixel_encoding == PIXEL_ENCODING_YCBCR420)
			normalized_clk /= 2;
		/* Adjusting pix clock following on HDMI spec based on colour depth */
		switch (timing_out->display_color_depth) {
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
			return;
		}
		if (normalized_clk <= info->max_tmds_clock)
			return;
		reduce_mode_colour_depth(timing_out);

	} while (timing_out->display_color_depth > COLOR_DEPTH_888);

}
/*****************************************************************************/

static void
fill_stream_properties_from_drm_display_mode(struct dc_stream_state *stream,
					     const struct drm_display_mode *mode_in,
					     const struct drm_connector *connector)
{
	struct dc_crtc_timing *timing_out = &stream->timing;
	const struct drm_display_info *info = &connector->display_info;

	memset(timing_out, 0, sizeof(struct dc_crtc_timing));

	timing_out->h_border_left = 0;
	timing_out->h_border_right = 0;
	timing_out->v_border_top = 0;
	timing_out->v_border_bottom = 0;
	/* TODO: un-hardcode */
	if (drm_mode_is_420_only(info, mode_in)
			&& stream->sink->sink_signal == SIGNAL_TYPE_HDMI_TYPE_A)
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR420;
	else if ((connector->display_info.color_formats & DRM_COLOR_FORMAT_YCRCB444)
			&& stream->sink->sink_signal == SIGNAL_TYPE_HDMI_TYPE_A)
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR444;
	else
		timing_out->pixel_encoding = PIXEL_ENCODING_RGB;

	timing_out->timing_3d_format = TIMING_3D_FORMAT_NONE;
	timing_out->display_color_depth = convert_color_depth_from_display_info(
			connector);
	timing_out->scan_type = SCANNING_TYPE_NODATA;
	timing_out->hdmi_vic = 0;
	timing_out->vic = drm_match_cea_mode(mode_in);

	timing_out->h_addressable = mode_in->crtc_hdisplay;
	timing_out->h_total = mode_in->crtc_htotal;
	timing_out->h_sync_width =
		mode_in->crtc_hsync_end - mode_in->crtc_hsync_start;
	timing_out->h_front_porch =
		mode_in->crtc_hsync_start - mode_in->crtc_hdisplay;
	timing_out->v_total = mode_in->crtc_vtotal;
	timing_out->v_addressable = mode_in->crtc_vdisplay;
	timing_out->v_front_porch =
		mode_in->crtc_vsync_start - mode_in->crtc_vdisplay;
	timing_out->v_sync_width =
		mode_in->crtc_vsync_end - mode_in->crtc_vsync_start;
	timing_out->pix_clk_khz = mode_in->crtc_clock;
	timing_out->aspect_ratio = get_aspect_ratio(mode_in);
	if (mode_in->flags & DRM_MODE_FLAG_PHSYNC)
		timing_out->flags.HSYNC_POSITIVE_POLARITY = 1;
	if (mode_in->flags & DRM_MODE_FLAG_PVSYNC)
		timing_out->flags.VSYNC_POSITIVE_POLARITY = 1;

	stream->output_color_space = get_output_color_space(timing_out);

	stream->out_transfer_func->type = TF_TYPE_PREDEFINED;
	stream->out_transfer_func->tf = TRANSFER_FUNCTION_SRGB;
	if (stream->sink->sink_signal == SIGNAL_TYPE_HDMI_TYPE_A)
		adjust_colour_depth_from_display_info(timing_out, info);
}

static void fill_audio_info(struct audio_info *audio_info,
			    const struct drm_connector *drm_connector,
			    const struct dc_sink *dc_sink)
{
	int i = 0;
	int cea_revision = 0;
	const struct dc_edid_caps *edid_caps = &dc_sink->edid_caps;

	audio_info->manufacture_id = edid_caps->manufacturer_id;
	audio_info->product_id = edid_caps->product_id;

	cea_revision = drm_connector->display_info.cea_rev;

	strncpy(audio_info->display_name,
		edid_caps->display_name,
		AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS - 1);

	if (cea_revision >= 3) {
		audio_info->mode_count = edid_caps->audio_mode_count;

		for (i = 0; i < audio_info->mode_count; ++i) {
			audio_info->modes[i].format_code =
					(enum audio_format_code)
					(edid_caps->audio_modes[i].format_code);
			audio_info->modes[i].channel_count =
					edid_caps->audio_modes[i].channel_count;
			audio_info->modes[i].sample_rates.all =
					edid_caps->audio_modes[i].sample_rate;
			audio_info->modes[i].sample_size =
					edid_caps->audio_modes[i].sample_size;
		}
	}

	audio_info->flags.all = edid_caps->speaker_flags;

	/* TODO: We only check for the progressive mode, check for interlace mode too */
	if (drm_connector->latency_present[0]) {
		audio_info->video_latency = drm_connector->video_latency[0];
		audio_info->audio_latency = drm_connector->audio_latency[0];
	}

	/* TODO: For DP, video and audio latency should be calculated from DPCD caps */

}

static void
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

static void
decide_crtc_timing_for_drm_display_mode(struct drm_display_mode *drm_mode,
					const struct drm_display_mode *native_mode,
					bool scale_enabled)
{
	if (scale_enabled) {
		copy_crtc_timing_for_drm_display_mode(native_mode, drm_mode);
	} else if (native_mode->clock == drm_mode->clock &&
			native_mode->htotal == drm_mode->htotal &&
			native_mode->vtotal == drm_mode->vtotal) {
		copy_crtc_timing_for_drm_display_mode(native_mode, drm_mode);
	} else {
		/* no scaling nor amdgpu inserted, no need to patch */
	}
}

static struct dc_sink *
create_fake_sink(struct amdgpu_dm_connector *aconnector)
{
	struct dc_sink_init_data sink_init_data = { 0 };
	struct dc_sink *sink = NULL;
	sink_init_data.link = aconnector->dc_link;
	sink_init_data.sink_signal = aconnector->dc_link->connector_signal;

	sink = dc_sink_create(&sink_init_data);
	if (!sink) {
		DRM_ERROR("Failed to create sink!\n");
		return NULL;
	}
	sink->sink_signal = SIGNAL_TYPE_VIRTUAL;

	return sink;
}

static void set_multisync_trigger_params(
		struct dc_stream_state *stream)
{
	if (stream->triggered_crtc_reset.enabled) {
		stream->triggered_crtc_reset.event = CRTC_EVENT_VSYNC_RISING;
		stream->triggered_crtc_reset.delay = TRIGGER_DELAY_NEXT_LINE;
	}
}

static void set_master_stream(struct dc_stream_state *stream_set[],
			      int stream_count)
{
	int j, highest_rfr = 0, master_stream = 0;

	for (j = 0;  j < stream_count; j++) {
		if (stream_set[j] && stream_set[j]->triggered_crtc_reset.enabled) {
			int refresh_rate = 0;

			refresh_rate = (stream_set[j]->timing.pix_clk_khz*1000)/
				(stream_set[j]->timing.h_total*stream_set[j]->timing.v_total);
			if (refresh_rate > highest_rfr) {
				highest_rfr = refresh_rate;
				master_stream = j;
			}
		}
	}
	for (j = 0;  j < stream_count; j++) {
		if (stream_set[j])
			stream_set[j]->triggered_crtc_reset.event_source = stream_set[master_stream];
	}
}

static void dm_enable_per_frame_crtc_master_sync(struct dc_state *context)
{
	int i = 0;

	if (context->stream_count < 2)
		return;
	for (i = 0; i < context->stream_count ; i++) {
		if (!context->streams[i])
			continue;
		/* TODO: add a function to read AMD VSDB bits and will set
		 * crtc_sync_master.multi_sync_enabled flag
		 * For now its set to false
		 */
		set_multisync_trigger_params(context->streams[i]);
	}
	set_master_stream(context->streams, context->stream_count);
}

static struct dc_stream_state *
create_stream_for_sink(struct amdgpu_dm_connector *aconnector,
		       const struct drm_display_mode *drm_mode,
		       const struct dm_connector_state *dm_state)
{
	struct drm_display_mode *preferred_mode = NULL;
	struct drm_connector *drm_connector;
	struct dc_stream_state *stream = NULL;
	struct drm_display_mode mode = *drm_mode;
	bool native_mode_found = false;
	struct dc_sink *sink = NULL;
	if (aconnector == NULL) {
		DRM_ERROR("aconnector is NULL!\n");
		return stream;
	}

	drm_connector = &aconnector->base;

	if (!aconnector->dc_sink) {
		/*
		 * Create dc_sink when necessary to MST
		 * Don't apply fake_sink to MST
		 */
		if (aconnector->mst_port) {
			dm_dp_mst_dc_sink_create(drm_connector);
			return stream;
		}

		sink = create_fake_sink(aconnector);
		if (!sink)
			return stream;
	} else {
		sink = aconnector->dc_sink;
	}

	stream = dc_create_stream_for_sink(sink);

	if (stream == NULL) {
		DRM_ERROR("Failed to create stream for sink!\n");
		goto finish;
	}

	list_for_each_entry(preferred_mode, &aconnector->base.modes, head) {
		/* Search for preferred mode */
		if (preferred_mode->type & DRM_MODE_TYPE_PREFERRED) {
			native_mode_found = true;
			break;
		}
	}
	if (!native_mode_found)
		preferred_mode = list_first_entry_or_null(
				&aconnector->base.modes,
				struct drm_display_mode,
				head);

	if (preferred_mode == NULL) {
		/* This may not be an error, the use case is when we we have no
		 * usermode calls to reset and set mode upon hotplug. In this
		 * case, we call set mode ourselves to restore the previous mode
		 * and the modelist may not be filled in in time.
		 */
		DRM_DEBUG_DRIVER("No preferred mode found\n");
	} else {
		decide_crtc_timing_for_drm_display_mode(
				&mode, preferred_mode,
				dm_state ? (dm_state->scaling != RMX_OFF) : false);
	}

	if (!dm_state)
		drm_mode_set_crtcinfo(&mode, 0);

	fill_stream_properties_from_drm_display_mode(stream,
			&mode, &aconnector->base);
	update_stream_scaling_settings(&mode, dm_state, stream);

	fill_audio_info(
		&stream->audio_info,
		drm_connector,
		sink);

	update_stream_signal(stream);

	if (dm_state && dm_state->freesync_capable)
		stream->ignore_msa_timing_param = true;
finish:
	if (sink && sink->sink_signal == SIGNAL_TYPE_VIRTUAL)
		dc_sink_release(sink);

	return stream;
}

static void amdgpu_dm_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static void dm_crtc_destroy_state(struct drm_crtc *crtc,
				  struct drm_crtc_state *state)
{
	struct dm_crtc_state *cur = to_dm_crtc_state(state);

	/* TODO Destroy dc_stream objects are stream object is flattened */
	if (cur->stream)
		dc_stream_release(cur->stream);


	__drm_atomic_helper_crtc_destroy_state(state);


	kfree(state);
}

static void dm_crtc_reset_state(struct drm_crtc *crtc)
{
	struct dm_crtc_state *state;

	if (crtc->state)
		dm_crtc_destroy_state(crtc, crtc->state);

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (WARN_ON(!state))
		return;

	crtc->state = &state->base;
	crtc->state->crtc = crtc;

}

static struct drm_crtc_state *
dm_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct dm_crtc_state *state, *cur;

	cur = to_dm_crtc_state(crtc->state);

	if (WARN_ON(!crtc->state))
		return NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &state->base);

	if (cur->stream) {
		state->stream = cur->stream;
		dc_stream_retain(state->stream);
	}

	/* TODO Duplicate dc_stream after objects are stream object is flattened */

	return &state->base;
}


static inline int dm_set_vblank(struct drm_crtc *crtc, bool enable)
{
	enum dc_irq_source irq_source;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = crtc->dev->dev_private;

	irq_source = IRQ_TYPE_VBLANK + acrtc->otg_inst;
	return dc_interrupt_set(adev->dm.dc, irq_source, enable) ? 0 : -EBUSY;
}

static int dm_enable_vblank(struct drm_crtc *crtc)
{
	return dm_set_vblank(crtc, true);
}

static void dm_disable_vblank(struct drm_crtc *crtc)
{
	dm_set_vblank(crtc, false);
}

/* Implemented only the options currently availible for the driver */
static const struct drm_crtc_funcs amdgpu_dm_crtc_funcs = {
	.reset = dm_crtc_reset_state,
	.destroy = amdgpu_dm_crtc_destroy,
	.gamma_set = drm_atomic_helper_legacy_gamma_set,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = dm_crtc_duplicate_state,
	.atomic_destroy_state = dm_crtc_destroy_state,
	.set_crc_source = amdgpu_dm_crtc_set_crc_source,
	.enable_vblank = dm_enable_vblank,
	.disable_vblank = dm_disable_vblank,
};

static enum drm_connector_status
amdgpu_dm_connector_detect(struct drm_connector *connector, bool force)
{
	bool connected;
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);

	/* Notes:
	 * 1. This interface is NOT called in context of HPD irq.
	 * 2. This interface *is called* in context of user-mode ioctl. Which
	 * makes it a bad place for *any* MST-related activit. */

	if (aconnector->base.force == DRM_FORCE_UNSPECIFIED &&
	    !aconnector->fake_enable)
		connected = (aconnector->dc_sink != NULL);
	else
		connected = (aconnector->base.force == DRM_FORCE_ON);

	return (connected ? connector_status_connected :
			connector_status_disconnected);
}

int amdgpu_dm_connector_atomic_set_property(struct drm_connector *connector,
					    struct drm_connector_state *connector_state,
					    struct drm_property *property,
					    uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
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
	}

	return ret;
}

int amdgpu_dm_connector_atomic_get_property(struct drm_connector *connector,
					    const struct drm_connector_state *state,
					    struct drm_property *property,
					    uint64_t *val)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
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
	}
	return ret;
}

static void amdgpu_dm_connector_destroy(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	const struct dc_link *link = aconnector->dc_link;
	struct amdgpu_device *adev = connector->dev->dev_private;
	struct amdgpu_display_manager *dm = &adev->dm;

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

	if ((link->connector_signal & (SIGNAL_TYPE_EDP | SIGNAL_TYPE_LVDS)) &&
	    link->type != dc_connection_none &&
	    dm->backlight_dev) {
		backlight_device_unregister(dm->backlight_dev);
		dm->backlight_dev = NULL;
	}
#endif
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

void amdgpu_dm_connector_funcs_reset(struct drm_connector *connector)
{
	struct dm_connector_state *state =
		to_dm_connector_state(connector->state);

	if (connector->state)
		__drm_atomic_helper_connector_destroy_state(connector->state);

	kfree(state);

	state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (state) {
		state->scaling = RMX_OFF;
		state->underscan_enable = false;
		state->underscan_hborder = 0;
		state->underscan_vborder = 0;

		__drm_atomic_helper_connector_reset(connector, &state->base);
	}
}

struct drm_connector_state *
amdgpu_dm_connector_atomic_duplicate_state(struct drm_connector *connector)
{
	struct dm_connector_state *state =
		to_dm_connector_state(connector->state);

	struct dm_connector_state *new_state =
			kmemdup(state, sizeof(*state), GFP_KERNEL);

	if (new_state) {
		__drm_atomic_helper_connector_duplicate_state(connector,
							      &new_state->base);
		return &new_state->base;
	}

	return NULL;
}

static const struct drm_connector_funcs amdgpu_dm_connector_funcs = {
	.reset = amdgpu_dm_connector_funcs_reset,
	.detect = amdgpu_dm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = amdgpu_dm_connector_destroy,
	.atomic_duplicate_state = amdgpu_dm_connector_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = amdgpu_dm_connector_atomic_set_property,
	.atomic_get_property = amdgpu_dm_connector_atomic_get_property
};

static struct drm_encoder *best_encoder(struct drm_connector *connector)
{
	int enc_id = connector->encoder_ids[0];
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;

	DRM_DEBUG_DRIVER("Finding the best encoder\n");

	/* pick the encoder ids */
	if (enc_id) {
		obj = drm_mode_object_find(connector->dev, NULL, enc_id, DRM_MODE_OBJECT_ENCODER);
		if (!obj) {
			DRM_ERROR("Couldn't find a matching encoder for our connector\n");
			return NULL;
		}
		encoder = obj_to_encoder(obj);
		return encoder;
	}
	DRM_ERROR("No encoder id\n");
	return NULL;
}

static int get_modes(struct drm_connector *connector)
{
	return amdgpu_dm_connector_get_modes(connector);
}

static void create_eml_sink(struct amdgpu_dm_connector *aconnector)
{
	struct dc_sink_init_data init_params = {
			.link = aconnector->dc_link,
			.sink_signal = SIGNAL_TYPE_VIRTUAL
	};
	struct edid *edid;

	if (!aconnector->base.edid_blob_ptr) {
		DRM_ERROR("No EDID firmware found on connector: %s ,forcing to OFF!\n",
				aconnector->base.name);

		aconnector->base.force = DRM_FORCE_OFF;
		aconnector->base.override_edid = false;
		return;
	}

	edid = (struct edid *) aconnector->base.edid_blob_ptr->data;

	aconnector->edid = edid;

	aconnector->dc_em_sink = dc_link_add_remote_sink(
		aconnector->dc_link,
		(uint8_t *)edid,
		(edid->extensions + 1) * EDID_LENGTH,
		&init_params);

	if (aconnector->base.force == DRM_FORCE_ON)
		aconnector->dc_sink = aconnector->dc_link->local_sink ?
		aconnector->dc_link->local_sink :
		aconnector->dc_em_sink;
}

static void handle_edid_mgmt(struct amdgpu_dm_connector *aconnector)
{
	struct dc_link *link = (struct dc_link *)aconnector->dc_link;

	/* In case of headless boot with force on for DP managed connector
	 * Those settings have to be != 0 to get initial modeset
	 */
	if (link->connector_signal == SIGNAL_TYPE_DISPLAY_PORT) {
		link->verified_link_cap.lane_count = LANE_COUNT_FOUR;
		link->verified_link_cap.link_rate = LINK_RATE_HIGH2;
	}


	aconnector->base.override_edid = true;
	create_eml_sink(aconnector);
}

enum drm_mode_status amdgpu_dm_connector_mode_valid(struct drm_connector *connector,
				   struct drm_display_mode *mode)
{
	int result = MODE_ERROR;
	struct dc_sink *dc_sink;
	struct amdgpu_device *adev = connector->dev->dev_private;
	/* TODO: Unhardcode stream count */
	struct dc_stream_state *stream;
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	enum dc_status dc_result = DC_OK;

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) ||
			(mode->flags & DRM_MODE_FLAG_DBLSCAN))
		return result;

	/* Only run this the first time mode_valid is called to initilialize
	 * EDID mgmt
	 */
	if (aconnector->base.force != DRM_FORCE_UNSPECIFIED &&
		!aconnector->dc_em_sink)
		handle_edid_mgmt(aconnector);

	dc_sink = to_amdgpu_dm_connector(connector)->dc_sink;

	if (dc_sink == NULL) {
		DRM_ERROR("dc_sink is NULL!\n");
		goto fail;
	}

	stream = create_stream_for_sink(aconnector, mode, NULL);
	if (stream == NULL) {
		DRM_ERROR("Failed to create stream for sink!\n");
		goto fail;
	}

	dc_result = dc_validate_stream(adev->dm.dc, stream);

	if (dc_result == DC_OK)
		result = MODE_OK;
	else
		DRM_DEBUG_KMS("Mode %dx%d (clk %d) failed DC validation with error %d\n",
			      mode->vdisplay,
			      mode->hdisplay,
			      mode->clock,
			      dc_result);

	dc_stream_release(stream);

fail:
	/* TODO: error handling*/
	return result;
}

static const struct drm_connector_helper_funcs
amdgpu_dm_connector_helper_funcs = {
	/*
	 * If hotplug a second bigger display in FB Con mode, bigger resolution
	 * modes will be filtered by drm_mode_validate_size(), and those modes
	 * is missing after user start lightdm. So we need to renew modes list.
	 * in get_modes call back, not just return the modes count
	 */
	.get_modes = get_modes,
	.mode_valid = amdgpu_dm_connector_mode_valid,
	.best_encoder = best_encoder
};

static void dm_crtc_helper_disable(struct drm_crtc *crtc)
{
}

static int dm_crtc_helper_atomic_check(struct drm_crtc *crtc,
				       struct drm_crtc_state *state)
{
	struct amdgpu_device *adev = crtc->dev->dev_private;
	struct dc *dc = adev->dm.dc;
	struct dm_crtc_state *dm_crtc_state = to_dm_crtc_state(state);
	int ret = -EINVAL;

	if (unlikely(!dm_crtc_state->stream &&
		     modeset_required(state, NULL, dm_crtc_state->stream))) {
		WARN_ON(1);
		return ret;
	}

	/* In some use cases, like reset, no stream  is attached */
	if (!dm_crtc_state->stream)
		return 0;

	if (dc_validate_stream(dc, dm_crtc_state->stream) == DC_OK)
		return 0;

	return ret;
}

static bool dm_crtc_helper_mode_fixup(struct drm_crtc *crtc,
				      const struct drm_display_mode *mode,
				      struct drm_display_mode *adjusted_mode)
{
	return true;
}

static const struct drm_crtc_helper_funcs amdgpu_dm_crtc_helper_funcs = {
	.disable = dm_crtc_helper_disable,
	.atomic_check = dm_crtc_helper_atomic_check,
	.mode_fixup = dm_crtc_helper_mode_fixup
};

static void dm_encoder_helper_disable(struct drm_encoder *encoder)
{

}

static int dm_encoder_helper_atomic_check(struct drm_encoder *encoder,
					  struct drm_crtc_state *crtc_state,
					  struct drm_connector_state *conn_state)
{
	return 0;
}

const struct drm_encoder_helper_funcs amdgpu_dm_encoder_helper_funcs = {
	.disable = dm_encoder_helper_disable,
	.atomic_check = dm_encoder_helper_atomic_check
};

static void dm_drm_plane_reset(struct drm_plane *plane)
{
	struct dm_plane_state *amdgpu_state = NULL;

	if (plane->state)
		plane->funcs->atomic_destroy_state(plane, plane->state);

	amdgpu_state = kzalloc(sizeof(*amdgpu_state), GFP_KERNEL);
	WARN_ON(amdgpu_state == NULL);
	
	if (amdgpu_state) {
		plane->state = &amdgpu_state->base;
		plane->state->plane = plane;
		plane->state->rotation = DRM_MODE_ROTATE_0;
	}
}

static struct drm_plane_state *
dm_drm_plane_duplicate_state(struct drm_plane *plane)
{
	struct dm_plane_state *dm_plane_state, *old_dm_plane_state;

	old_dm_plane_state = to_dm_plane_state(plane->state);
	dm_plane_state = kzalloc(sizeof(*dm_plane_state), GFP_KERNEL);
	if (!dm_plane_state)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &dm_plane_state->base);

	if (old_dm_plane_state->dc_state) {
		dm_plane_state->dc_state = old_dm_plane_state->dc_state;
		dc_plane_state_retain(dm_plane_state->dc_state);
	}

	return &dm_plane_state->base;
}

void dm_drm_plane_destroy_state(struct drm_plane *plane,
				struct drm_plane_state *state)
{
	struct dm_plane_state *dm_plane_state = to_dm_plane_state(state);

	if (dm_plane_state->dc_state)
		dc_plane_state_release(dm_plane_state->dc_state);

	drm_atomic_helper_plane_destroy_state(plane, state);
}

static const struct drm_plane_funcs dm_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_plane_cleanup,
	.reset = dm_drm_plane_reset,
	.atomic_duplicate_state = dm_drm_plane_duplicate_state,
	.atomic_destroy_state = dm_drm_plane_destroy_state,
};

static int dm_plane_helper_prepare_fb(struct drm_plane *plane,
				      struct drm_plane_state *new_state)
{
	struct amdgpu_framebuffer *afb;
	struct drm_gem_object *obj;
	struct amdgpu_device *adev;
	struct amdgpu_bo *rbo;
	uint64_t chroma_addr = 0;
	struct dm_plane_state *dm_plane_state_new, *dm_plane_state_old;
	unsigned int awidth;
	uint32_t domain;
	int r;

	dm_plane_state_old = to_dm_plane_state(plane->state);
	dm_plane_state_new = to_dm_plane_state(new_state);

	if (!new_state->fb) {
		DRM_DEBUG_DRIVER("No FB bound\n");
		return 0;
	}

	afb = to_amdgpu_framebuffer(new_state->fb);
	obj = new_state->fb->obj[0];
	rbo = gem_to_amdgpu_bo(obj);
	adev = amdgpu_ttm_adev(rbo->tbo.bdev);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r != 0))
		return r;

	if (plane->type != DRM_PLANE_TYPE_CURSOR)
		domain = amdgpu_display_supported_domains(adev);
	else
		domain = AMDGPU_GEM_DOMAIN_VRAM;

	r = amdgpu_bo_pin(rbo, domain);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			DRM_ERROR("Failed to pin framebuffer with error %d\n", r);
		amdgpu_bo_unreserve(rbo);
		return r;
	}

	r = amdgpu_ttm_alloc_gart(&rbo->tbo);
	if (unlikely(r != 0)) {
		amdgpu_bo_unpin(rbo);
		amdgpu_bo_unreserve(rbo);
		DRM_ERROR("%p bind failed\n", rbo);
		return r;
	}
	amdgpu_bo_unreserve(rbo);

	afb->address = amdgpu_bo_gpu_offset(rbo);

	amdgpu_bo_ref(rbo);

	if (dm_plane_state_new->dc_state &&
			dm_plane_state_old->dc_state != dm_plane_state_new->dc_state) {
		struct dc_plane_state *plane_state = dm_plane_state_new->dc_state;

		if (plane_state->format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
			plane_state->address.grph.addr.low_part = lower_32_bits(afb->address);
			plane_state->address.grph.addr.high_part = upper_32_bits(afb->address);
		} else {
			awidth = ALIGN(new_state->fb->width, 64);
			plane_state->address.type = PLN_ADDR_TYPE_VIDEO_PROGRESSIVE;
			plane_state->address.video_progressive.luma_addr.low_part
							= lower_32_bits(afb->address);
			plane_state->address.video_progressive.luma_addr.high_part
							= upper_32_bits(afb->address);
			chroma_addr = afb->address + (u64)awidth * new_state->fb->height;
			plane_state->address.video_progressive.chroma_addr.low_part
							= lower_32_bits(chroma_addr);
			plane_state->address.video_progressive.chroma_addr.high_part
							= upper_32_bits(chroma_addr);
		}
	}

	return 0;
}

static void dm_plane_helper_cleanup_fb(struct drm_plane *plane,
				       struct drm_plane_state *old_state)
{
	struct amdgpu_bo *rbo;
	int r;

	if (!old_state->fb)
		return;

	rbo = gem_to_amdgpu_bo(old_state->fb->obj[0]);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r)) {
		DRM_ERROR("failed to reserve rbo before unpin\n");
		return;
	}

	amdgpu_bo_unpin(rbo);
	amdgpu_bo_unreserve(rbo);
	amdgpu_bo_unref(&rbo);
}

static int dm_plane_atomic_check(struct drm_plane *plane,
				 struct drm_plane_state *state)
{
	struct amdgpu_device *adev = plane->dev->dev_private;
	struct dc *dc = adev->dm.dc;
	struct dm_plane_state *dm_plane_state = to_dm_plane_state(state);

	if (!dm_plane_state->dc_state)
		return 0;

	if (!fill_rects_from_plane_state(state, dm_plane_state->dc_state))
		return -EINVAL;

	if (dc_validate_plane(dc, dm_plane_state->dc_state) == DC_OK)
		return 0;

	return -EINVAL;
}

static const struct drm_plane_helper_funcs dm_plane_helper_funcs = {
	.prepare_fb = dm_plane_helper_prepare_fb,
	.cleanup_fb = dm_plane_helper_cleanup_fb,
	.atomic_check = dm_plane_atomic_check,
};

/*
 * TODO: these are currently initialized to rgb formats only.
 * For future use cases we should either initialize them dynamically based on
 * plane capabilities, or initialize this array to all formats, so internal drm
 * check will succeed, and let DC to implement proper check
 */
static const uint32_t rgb_formats[] = {
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,
};

static const uint32_t yuv_formats[] = {
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
};

static const u32 cursor_formats[] = {
	DRM_FORMAT_ARGB8888
};

static int amdgpu_dm_plane_init(struct amdgpu_display_manager *dm,
				struct amdgpu_plane *aplane,
				unsigned long possible_crtcs)
{
	int res = -EPERM;

	switch (aplane->base.type) {
	case DRM_PLANE_TYPE_PRIMARY:
		res = drm_universal_plane_init(
				dm->adev->ddev,
				&aplane->base,
				possible_crtcs,
				&dm_plane_funcs,
				rgb_formats,
				ARRAY_SIZE(rgb_formats),
				NULL, aplane->base.type, NULL);
		break;
	case DRM_PLANE_TYPE_OVERLAY:
		res = drm_universal_plane_init(
				dm->adev->ddev,
				&aplane->base,
				possible_crtcs,
				&dm_plane_funcs,
				yuv_formats,
				ARRAY_SIZE(yuv_formats),
				NULL, aplane->base.type, NULL);
		break;
	case DRM_PLANE_TYPE_CURSOR:
		res = drm_universal_plane_init(
				dm->adev->ddev,
				&aplane->base,
				possible_crtcs,
				&dm_plane_funcs,
				cursor_formats,
				ARRAY_SIZE(cursor_formats),
				NULL, aplane->base.type, NULL);
		break;
	}

	drm_plane_helper_add(&aplane->base, &dm_plane_helper_funcs);

	/* Create (reset) the plane state */
	if (aplane->base.funcs->reset)
		aplane->base.funcs->reset(&aplane->base);


	return res;
}

static int amdgpu_dm_crtc_init(struct amdgpu_display_manager *dm,
			       struct drm_plane *plane,
			       uint32_t crtc_index)
{
	struct amdgpu_crtc *acrtc = NULL;
	struct amdgpu_plane *cursor_plane;

	int res = -ENOMEM;

	cursor_plane = kzalloc(sizeof(*cursor_plane), GFP_KERNEL);
	if (!cursor_plane)
		goto fail;

	cursor_plane->base.type = DRM_PLANE_TYPE_CURSOR;
	res = amdgpu_dm_plane_init(dm, cursor_plane, 0);

	acrtc = kzalloc(sizeof(struct amdgpu_crtc), GFP_KERNEL);
	if (!acrtc)
		goto fail;

	res = drm_crtc_init_with_planes(
			dm->ddev,
			&acrtc->base,
			plane,
			&cursor_plane->base,
			&amdgpu_dm_crtc_funcs, NULL);

	if (res)
		goto fail;

	drm_crtc_helper_add(&acrtc->base, &amdgpu_dm_crtc_helper_funcs);

	/* Create (reset) the plane state */
	if (acrtc->base.funcs->reset)
		acrtc->base.funcs->reset(&acrtc->base);

	acrtc->max_cursor_width = dm->adev->dm.dc->caps.max_cursor_size;
	acrtc->max_cursor_height = dm->adev->dm.dc->caps.max_cursor_size;

	acrtc->crtc_id = crtc_index;
	acrtc->base.enabled = false;

	dm->adev->mode_info.crtcs[crtc_index] = acrtc;
	drm_crtc_enable_color_mgmt(&acrtc->base, MAX_COLOR_LUT_ENTRIES,
				   true, MAX_COLOR_LUT_ENTRIES);
	drm_mode_crtc_set_gamma_size(&acrtc->base, MAX_COLOR_LEGACY_LUT_ENTRIES);

	return 0;

fail:
	kfree(acrtc);
	kfree(cursor_plane);
	return res;
}


static int to_drm_connector_type(enum signal_type st)
{
	switch (st) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		return DRM_MODE_CONNECTOR_HDMIA;
	case SIGNAL_TYPE_EDP:
		return DRM_MODE_CONNECTOR_eDP;
	case SIGNAL_TYPE_RGB:
		return DRM_MODE_CONNECTOR_VGA;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		return DRM_MODE_CONNECTOR_DisplayPort;
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
		return DRM_MODE_CONNECTOR_DVID;
	case SIGNAL_TYPE_VIRTUAL:
		return DRM_MODE_CONNECTOR_VIRTUAL;

	default:
		return DRM_MODE_CONNECTOR_Unknown;
	}
}

static void amdgpu_dm_get_native_mode(struct drm_connector *connector)
{
	const struct drm_connector_helper_funcs *helper =
		connector->helper_private;
	struct drm_encoder *encoder;
	struct amdgpu_encoder *amdgpu_encoder;

	encoder = helper->best_encoder(connector);

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
			     char *name,
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
	strncpy(mode->name, name, DRM_DISPLAY_MODE_LEN);

	return mode;

}

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
	struct mode_size {
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
		drm_mode_probed_add(connector, mode);
		amdgpu_dm_connector->num_modes++;
	}
}

static void amdgpu_dm_connector_ddc_get_modes(struct drm_connector *connector,
					      struct edid *edid)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);

	if (edid) {
		/* empty probed_modes */
		INIT_LIST_HEAD(&connector->probed_modes);
		amdgpu_dm_connector->num_modes =
				drm_add_edid_modes(connector, edid);

		amdgpu_dm_get_native_mode(connector);
	} else {
		amdgpu_dm_connector->num_modes = 0;
	}
}

static int amdgpu_dm_connector_get_modes(struct drm_connector *connector)
{
	const struct drm_connector_helper_funcs *helper =
			connector->helper_private;
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);
	struct drm_encoder *encoder;
	struct edid *edid = amdgpu_dm_connector->edid;

	encoder = helper->best_encoder(connector);

	if (!edid || !drm_edid_is_valid(edid)) {
		drm_add_modes_noedid(connector, 640, 480);
	} else {
		amdgpu_dm_connector_ddc_get_modes(connector, edid);
		amdgpu_dm_connector_add_common_modes(encoder, connector);
	}
	amdgpu_dm_fbc_init(connector);

	return amdgpu_dm_connector->num_modes;
}

void amdgpu_dm_connector_init_helper(struct amdgpu_display_manager *dm,
				     struct amdgpu_dm_connector *aconnector,
				     int connector_type,
				     struct dc_link *link,
				     int link_index)
{
	struct amdgpu_device *adev = dm->ddev->dev_private;

	aconnector->connector_id = link_index;
	aconnector->dc_link = link;
	aconnector->base.interlace_allowed = false;
	aconnector->base.doublescan_allowed = false;
	aconnector->base.stereo_allowed = false;
	aconnector->base.dpms = DRM_MODE_DPMS_OFF;
	aconnector->hpd.hpd = AMDGPU_HPD_NONE; /* not used */
	mutex_init(&aconnector->hpd_lock);

	/* configure support HPD hot plug connector_>polled default value is 0
	 * which means HPD hot plug not supported
	 */
	switch (connector_type) {
	case DRM_MODE_CONNECTOR_HDMIA:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		aconnector->base.ycbcr_420_allowed =
			link->link_enc->features.ycbcr420_supported ? true : false;
		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		aconnector->base.ycbcr_420_allowed =
			link->link_enc->features.ycbcr420_supported ? true : false;
		break;
	case DRM_MODE_CONNECTOR_DVID:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		break;
	default:
		break;
	}

	drm_object_attach_property(&aconnector->base.base,
				dm->ddev->mode_config.scaling_mode_property,
				DRM_MODE_SCALE_NONE);

	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_property,
				UNDERSCAN_OFF);
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_hborder_property,
				0);
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_vborder_property,
				0);

}

static int amdgpu_dm_i2c_xfer(struct i2c_adapter *i2c_adap,
			      struct i2c_msg *msgs, int num)
{
	struct amdgpu_i2c_adapter *i2c = i2c_get_adapdata(i2c_adap);
	struct ddc_service *ddc_service = i2c->ddc_service;
	struct i2c_command cmd;
	int i;
	int result = -EIO;

	cmd.payloads = kcalloc(num, sizeof(struct i2c_payload), GFP_KERNEL);

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

	if (dal_i2caux_submit_i2c_command(
			ddc_service->ctx->i2caux,
			ddc_service->ddc_pin,
			&cmd))
		result = num;

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

static struct amdgpu_i2c_adapter *
create_i2c(struct ddc_service *ddc_service,
	   int link_index,
	   int *res)
{
	struct amdgpu_device *adev = ddc_service->ctx->driver_context;
	struct amdgpu_i2c_adapter *i2c;

	i2c = kzalloc(sizeof(struct amdgpu_i2c_adapter), GFP_KERNEL);
	if (!i2c)
		return NULL;
	i2c->base.owner = THIS_MODULE;
	i2c->base.class = I2C_CLASS_DDC;
	i2c->base.dev.parent = &adev->pdev->dev;
	i2c->base.algo = &amdgpu_dm_i2c_algo;
	snprintf(i2c->base.name, sizeof(i2c->base.name), "AMDGPU DM i2c hw bus %d", link_index);
	i2c_set_adapdata(&i2c->base, i2c);
	i2c->ddc_service = ddc_service;

	return i2c;
}


/* Note: this function assumes that dc_link_detect() was called for the
 * dc_link which will be represented by this aconnector.
 */
static int amdgpu_dm_connector_init(struct amdgpu_display_manager *dm,
				    struct amdgpu_dm_connector *aconnector,
				    uint32_t link_index,
				    struct amdgpu_encoder *aencoder)
{
	int res = 0;
	int connector_type;
	struct dc *dc = dm->dc;
	struct dc_link *link = dc_get_link_at_index(dc, link_index);
	struct amdgpu_i2c_adapter *i2c;

	link->priv = aconnector;

	DRM_DEBUG_DRIVER("%s()\n", __func__);

	i2c = create_i2c(link->ddc, link->link_index, &res);
	if (!i2c) {
		DRM_ERROR("Failed to create i2c adapter data\n");
		return -ENOMEM;
	}

	aconnector->i2c = i2c;
	res = i2c_add_adapter(&i2c->base);

	if (res) {
		DRM_ERROR("Failed to register hw i2c %d\n", link->link_index);
		goto out_free;
	}

	connector_type = to_drm_connector_type(link->connector_signal);

	res = drm_connector_init(
			dm->ddev,
			&aconnector->base,
			&amdgpu_dm_connector_funcs,
			connector_type);

	if (res) {
		DRM_ERROR("connector_init failed\n");
		aconnector->connector_id = -1;
		goto out_free;
	}

	drm_connector_helper_add(
			&aconnector->base,
			&amdgpu_dm_connector_helper_funcs);

	if (aconnector->base.funcs->reset)
		aconnector->base.funcs->reset(&aconnector->base);

	amdgpu_dm_connector_init_helper(
		dm,
		aconnector,
		connector_type,
		link,
		link_index);

	drm_connector_attach_encoder(
		&aconnector->base, &aencoder->base);

	drm_connector_register(&aconnector->base);
#if defined(CONFIG_DEBUG_FS)
	res = connector_debugfs_init(aconnector);
	if (res) {
		DRM_ERROR("Failed to create debugfs for connector");
		goto out_free;
	}
#endif

	if (connector_type == DRM_MODE_CONNECTOR_DisplayPort
		|| connector_type == DRM_MODE_CONNECTOR_eDP)
		amdgpu_dm_initialize_dp_connector(dm, aconnector);

out_free:
	if (res) {
		kfree(i2c);
		aconnector->i2c = NULL;
	}
	return res;
}

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

static int amdgpu_dm_encoder_init(struct drm_device *dev,
				  struct amdgpu_encoder *aencoder,
				  uint32_t link_index)
{
	struct amdgpu_device *adev = dev->dev_private;

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

static void manage_dm_interrupts(struct amdgpu_device *adev,
				 struct amdgpu_crtc *acrtc,
				 bool enable)
{
	/*
	 * this is not correct translation but will work as soon as VBLANK
	 * constant is the same as PFLIP
	 */
	int irq_type =
		amdgpu_display_crtc_idx_to_irq_type(
			adev,
			acrtc->crtc_id);

	if (enable) {
		drm_crtc_vblank_on(&acrtc->base);
		amdgpu_irq_get(
			adev,
			&adev->pageflip_irq,
			irq_type);
	} else {

		amdgpu_irq_put(
			adev,
			&adev->pageflip_irq,
			irq_type);
		drm_crtc_vblank_off(&acrtc->base);
	}
}

static bool
is_scaling_state_different(const struct dm_connector_state *dm_state,
			   const struct dm_connector_state *old_dm_state)
{
	if (dm_state->scaling != old_dm_state->scaling)
		return true;
	if (!dm_state->underscan_enable && old_dm_state->underscan_enable) {
		if (old_dm_state->underscan_hborder != 0 && old_dm_state->underscan_vborder != 0)
			return true;
	} else  if (dm_state->underscan_enable && !old_dm_state->underscan_enable) {
		if (dm_state->underscan_hborder != 0 && dm_state->underscan_vborder != 0)
			return true;
	} else if (dm_state->underscan_hborder != old_dm_state->underscan_hborder ||
		   dm_state->underscan_vborder != old_dm_state->underscan_vborder)
		return true;
	return false;
}

static void remove_stream(struct amdgpu_device *adev,
			  struct amdgpu_crtc *acrtc,
			  struct dc_stream_state *stream)
{
	/* this is the update mode case */
	if (adev->dm.freesync_module)
		mod_freesync_remove_stream(adev->dm.freesync_module, stream);

	acrtc->otg_inst = -1;
	acrtc->enabled = false;
}

static int get_cursor_position(struct drm_plane *plane, struct drm_crtc *crtc,
			       struct dc_cursor_position *position)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	int x, y;
	int xorigin = 0, yorigin = 0;

	if (!crtc || !plane->state->fb) {
		position->enable = false;
		position->x = 0;
		position->y = 0;
		return 0;
	}

	if ((plane->state->crtc_w > amdgpu_crtc->max_cursor_width) ||
	    (plane->state->crtc_h > amdgpu_crtc->max_cursor_height)) {
		DRM_ERROR("%s: bad cursor width or height %d x %d\n",
			  __func__,
			  plane->state->crtc_w,
			  plane->state->crtc_h);
		return -EINVAL;
	}

	x = plane->state->crtc_x;
	y = plane->state->crtc_y;
	/* avivo cursor are offset into the total surface */
	x += crtc->primary->state->src_x >> 16;
	y += crtc->primary->state->src_y >> 16;
	if (x < 0) {
		xorigin = min(-x, amdgpu_crtc->max_cursor_width - 1);
		x = 0;
	}
	if (y < 0) {
		yorigin = min(-y, amdgpu_crtc->max_cursor_height - 1);
		y = 0;
	}
	position->enable = true;
	position->x = x;
	position->y = y;
	position->x_hotspot = xorigin;
	position->y_hotspot = yorigin;

	return 0;
}

static void handle_cursor_update(struct drm_plane *plane,
				 struct drm_plane_state *old_plane_state)
{
	struct amdgpu_framebuffer *afb = to_amdgpu_framebuffer(plane->state->fb);
	struct drm_crtc *crtc = afb ? plane->state->crtc : old_plane_state->crtc;
	struct dm_crtc_state *crtc_state = crtc ? to_dm_crtc_state(crtc->state) : NULL;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	uint64_t address = afb ? afb->address : 0;
	struct dc_cursor_position position;
	struct dc_cursor_attributes attributes;
	int ret;

	if (!plane->state->fb && !old_plane_state->fb)
		return;

	DRM_DEBUG_DRIVER("%s: crtc_id=%d with size %d to %d\n",
			 __func__,
			 amdgpu_crtc->crtc_id,
			 plane->state->crtc_w,
			 plane->state->crtc_h);

	ret = get_cursor_position(plane, crtc, &position);
	if (ret)
		return;

	if (!position.enable) {
		/* turn off cursor */
		if (crtc_state && crtc_state->stream)
			dc_stream_set_cursor_position(crtc_state->stream,
						      &position);
		return;
	}

	amdgpu_crtc->cursor_width = plane->state->crtc_w;
	amdgpu_crtc->cursor_height = plane->state->crtc_h;

	attributes.address.high_part = upper_32_bits(address);
	attributes.address.low_part  = lower_32_bits(address);
	attributes.width             = plane->state->crtc_w;
	attributes.height            = plane->state->crtc_h;
	attributes.color_format      = CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA;
	attributes.rotation_angle    = 0;
	attributes.attribute_flags.value = 0;

	attributes.pitch = attributes.width;

	if (crtc_state->stream) {
		if (!dc_stream_set_cursor_attributes(crtc_state->stream,
							 &attributes))
			DRM_ERROR("DC failed to set cursor attributes\n");

		if (!dc_stream_set_cursor_position(crtc_state->stream,
						   &position))
			DRM_ERROR("DC failed to set cursor position\n");
	}
}

static void prepare_flip_isr(struct amdgpu_crtc *acrtc)
{

	assert_spin_locked(&acrtc->base.dev->event_lock);
	WARN_ON(acrtc->event);

	acrtc->event = acrtc->base.state->event;

	/* Set the flip status */
	acrtc->pflip_status = AMDGPU_FLIP_SUBMITTED;

	/* Mark this event as consumed */
	acrtc->base.state->event = NULL;

	DRM_DEBUG_DRIVER("crtc:%d, pflip_stat:AMDGPU_FLIP_SUBMITTED\n",
						 acrtc->crtc_id);
}

/*
 * Executes flip
 *
 * Waits on all BO's fences and for proper vblank count
 */
static void amdgpu_dm_do_flip(struct drm_crtc *crtc,
			      struct drm_framebuffer *fb,
			      uint32_t target,
			      struct dc_state *state)
{
	unsigned long flags;
	uint32_t target_vblank;
	int r, vpos, hpos;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct amdgpu_framebuffer *afb = to_amdgpu_framebuffer(fb);
	struct amdgpu_bo *abo = gem_to_amdgpu_bo(fb->obj[0]);
	struct amdgpu_device *adev = crtc->dev->dev_private;
	bool async_flip = (crtc->state->pageflip_flags & DRM_MODE_PAGE_FLIP_ASYNC) != 0;
	struct dc_flip_addrs addr = { {0} };
	/* TODO eliminate or rename surface_update */
	struct dc_surface_update surface_updates[1] = { {0} };
	struct dm_crtc_state *acrtc_state = to_dm_crtc_state(crtc->state);


	/* Prepare wait for target vblank early - before the fence-waits */
	target_vblank = target - (uint32_t)drm_crtc_vblank_count(crtc) +
			amdgpu_get_vblank_counter_kms(crtc->dev, acrtc->crtc_id);

	/* TODO This might fail and hence better not used, wait
	 * explicitly on fences instead
	 * and in general should be called for
	 * blocking commit to as per framework helpers
	 */
	r = amdgpu_bo_reserve(abo, true);
	if (unlikely(r != 0)) {
		DRM_ERROR("failed to reserve buffer before flip\n");
		WARN_ON(1);
	}

	/* Wait for all fences on this FB */
	WARN_ON(reservation_object_wait_timeout_rcu(abo->tbo.resv, true, false,
								    MAX_SCHEDULE_TIMEOUT) < 0);

	amdgpu_bo_unreserve(abo);

	/* Wait until we're out of the vertical blank period before the one
	 * targeted by the flip
	 */
	while ((acrtc->enabled &&
		(amdgpu_display_get_crtc_scanoutpos(adev->ddev, acrtc->crtc_id,
						    0, &vpos, &hpos, NULL,
						    NULL, &crtc->hwmode)
		 & (DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_IN_VBLANK)) ==
		(DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_IN_VBLANK) &&
		(int)(target_vblank -
		  amdgpu_get_vblank_counter_kms(adev->ddev, acrtc->crtc_id)) > 0)) {
		usleep_range(1000, 1100);
	}

	/* Flip */
	spin_lock_irqsave(&crtc->dev->event_lock, flags);

	WARN_ON(acrtc->pflip_status != AMDGPU_FLIP_NONE);
	WARN_ON(!acrtc_state->stream);

	addr.address.grph.addr.low_part = lower_32_bits(afb->address);
	addr.address.grph.addr.high_part = upper_32_bits(afb->address);
	addr.flip_immediate = async_flip;


	if (acrtc->base.state->event)
		prepare_flip_isr(acrtc);

	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

	surface_updates->surface = dc_stream_get_status(acrtc_state->stream)->plane_states[0];
	surface_updates->flip_addr = &addr;

	dc_commit_updates_for_stream(adev->dm.dc,
					     surface_updates,
					     1,
					     acrtc_state->stream,
					     NULL,
					     &surface_updates->surface,
					     state);

	DRM_DEBUG_DRIVER("%s Flipping to hi: 0x%x, low: 0x%x \n",
			 __func__,
			 addr.address.grph.addr.high_part,
			 addr.address.grph.addr.low_part);
}

/*
 * TODO this whole function needs to go
 *
 * dc_surface_update is needlessly complex. See if we can just replace this
 * with a dc_plane_state and follow the atomic model a bit more closely here.
 */
static bool commit_planes_to_stream(
		struct dc *dc,
		struct dc_plane_state **plane_states,
		uint8_t new_plane_count,
		struct dm_crtc_state *dm_new_crtc_state,
		struct dm_crtc_state *dm_old_crtc_state,
		struct dc_state *state)
{
	/* no need to dynamically allocate this. it's pretty small */
	struct dc_surface_update updates[MAX_SURFACES];
	struct dc_flip_addrs *flip_addr;
	struct dc_plane_info *plane_info;
	struct dc_scaling_info *scaling_info;
	int i;
	struct dc_stream_state *dc_stream = dm_new_crtc_state->stream;
	struct dc_stream_update *stream_update =
			kzalloc(sizeof(struct dc_stream_update), GFP_KERNEL);

	if (!stream_update) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	flip_addr = kcalloc(MAX_SURFACES, sizeof(struct dc_flip_addrs),
			    GFP_KERNEL);
	plane_info = kcalloc(MAX_SURFACES, sizeof(struct dc_plane_info),
			     GFP_KERNEL);
	scaling_info = kcalloc(MAX_SURFACES, sizeof(struct dc_scaling_info),
			       GFP_KERNEL);

	if (!flip_addr || !plane_info || !scaling_info) {
		kfree(flip_addr);
		kfree(plane_info);
		kfree(scaling_info);
		kfree(stream_update);
		return false;
	}

	memset(updates, 0, sizeof(updates));

	stream_update->src = dc_stream->src;
	stream_update->dst = dc_stream->dst;
	stream_update->out_transfer_func = dc_stream->out_transfer_func;

	for (i = 0; i < new_plane_count; i++) {
		updates[i].surface = plane_states[i];
		updates[i].gamma =
			(struct dc_gamma *)plane_states[i]->gamma_correction;
		updates[i].in_transfer_func = plane_states[i]->in_transfer_func;
		flip_addr[i].address = plane_states[i]->address;
		flip_addr[i].flip_immediate = plane_states[i]->flip_immediate;
		plane_info[i].color_space = plane_states[i]->color_space;
		plane_info[i].format = plane_states[i]->format;
		plane_info[i].plane_size = plane_states[i]->plane_size;
		plane_info[i].rotation = plane_states[i]->rotation;
		plane_info[i].horizontal_mirror = plane_states[i]->horizontal_mirror;
		plane_info[i].stereo_format = plane_states[i]->stereo_format;
		plane_info[i].tiling_info = plane_states[i]->tiling_info;
		plane_info[i].visible = plane_states[i]->visible;
		plane_info[i].per_pixel_alpha = plane_states[i]->per_pixel_alpha;
		plane_info[i].dcc = plane_states[i]->dcc;
		scaling_info[i].scaling_quality = plane_states[i]->scaling_quality;
		scaling_info[i].src_rect = plane_states[i]->src_rect;
		scaling_info[i].dst_rect = plane_states[i]->dst_rect;
		scaling_info[i].clip_rect = plane_states[i]->clip_rect;

		updates[i].flip_addr = &flip_addr[i];
		updates[i].plane_info = &plane_info[i];
		updates[i].scaling_info = &scaling_info[i];
	}

	dc_commit_updates_for_stream(
			dc,
			updates,
			new_plane_count,
			dc_stream, stream_update, plane_states, state);

	kfree(flip_addr);
	kfree(plane_info);
	kfree(scaling_info);
	kfree(stream_update);
	return true;
}

static void amdgpu_dm_commit_planes(struct drm_atomic_state *state,
				    struct drm_device *dev,
				    struct amdgpu_display_manager *dm,
				    struct drm_crtc *pcrtc,
				    bool *wait_for_vblank)
{
	uint32_t i;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	struct dc_stream_state *dc_stream_attach;
	struct dc_plane_state *plane_states_constructed[MAX_SURFACES];
	struct amdgpu_crtc *acrtc_attach = to_amdgpu_crtc(pcrtc);
	struct drm_crtc_state *new_pcrtc_state =
			drm_atomic_get_new_crtc_state(state, pcrtc);
	struct dm_crtc_state *acrtc_state = to_dm_crtc_state(new_pcrtc_state);
	struct dm_crtc_state *dm_old_crtc_state =
			to_dm_crtc_state(drm_atomic_get_old_crtc_state(state, pcrtc));
	struct dm_atomic_state *dm_state = to_dm_atomic_state(state);
	int planes_count = 0;
	unsigned long flags;

	/* update planes when needed */
	for_each_oldnew_plane_in_state(state, plane, old_plane_state, new_plane_state, i) {
		struct drm_crtc *crtc = new_plane_state->crtc;
		struct drm_crtc_state *new_crtc_state;
		struct drm_framebuffer *fb = new_plane_state->fb;
		bool pflip_needed;
		struct dm_plane_state *dm_new_plane_state = to_dm_plane_state(new_plane_state);

		if (plane->type == DRM_PLANE_TYPE_CURSOR) {
			handle_cursor_update(plane, old_plane_state);
			continue;
		}

		if (!fb || !crtc || pcrtc != crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
		if (!new_crtc_state->active)
			continue;

		pflip_needed = !state->allow_modeset;

		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		if (acrtc_attach->pflip_status != AMDGPU_FLIP_NONE) {
			DRM_ERROR("%s: acrtc %d, already busy\n",
				  __func__,
				  acrtc_attach->crtc_id);
			/* In commit tail framework this cannot happen */
			WARN_ON(1);
		}
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

		if (!pflip_needed || plane->type == DRM_PLANE_TYPE_OVERLAY) {
			WARN_ON(!dm_new_plane_state->dc_state);

			plane_states_constructed[planes_count] = dm_new_plane_state->dc_state;

			dc_stream_attach = acrtc_state->stream;
			planes_count++;

		} else if (new_crtc_state->planes_changed) {
			/* Assume even ONE crtc with immediate flip means
			 * entire can't wait for VBLANK
			 * TODO Check if it's correct
			 */
			*wait_for_vblank =
					new_pcrtc_state->pageflip_flags & DRM_MODE_PAGE_FLIP_ASYNC ?
				false : true;

			/* TODO: Needs rework for multiplane flip */
			if (plane->type == DRM_PLANE_TYPE_PRIMARY)
				drm_crtc_vblank_get(crtc);

			amdgpu_dm_do_flip(
				crtc,
				fb,
				(uint32_t)drm_crtc_vblank_count(crtc) + *wait_for_vblank,
				dm_state->context);
		}

	}

	if (planes_count) {
		unsigned long flags;

		if (new_pcrtc_state->event) {

			drm_crtc_vblank_get(pcrtc);

			spin_lock_irqsave(&pcrtc->dev->event_lock, flags);
			prepare_flip_isr(acrtc_attach);
			spin_unlock_irqrestore(&pcrtc->dev->event_lock, flags);
		}


		if (false == commit_planes_to_stream(dm->dc,
							plane_states_constructed,
							planes_count,
							acrtc_state,
							dm_old_crtc_state,
							dm_state->context))
			dm_error("%s: Failed to attach plane!\n", __func__);
	} else {
		/*TODO BUG Here should go disable planes on CRTC. */
	}
}

/**
 * amdgpu_dm_crtc_copy_transient_flags - copy mirrored flags from DRM to DC
 * @crtc_state: the DRM CRTC state
 * @stream_state: the DC stream state.
 *
 * Copy the mirrored transient state flags from DRM, to DC. It is used to bring
 * a dc_stream_state's flags in sync with a drm_crtc_state's flags.
 */
static void amdgpu_dm_crtc_copy_transient_flags(struct drm_crtc_state *crtc_state,
						struct dc_stream_state *stream_state)
{
	stream_state->mode_changed = crtc_state->mode_changed;
}

static int amdgpu_dm_atomic_commit(struct drm_device *dev,
				   struct drm_atomic_state *state,
				   bool nonblock)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct amdgpu_device *adev = dev->dev_private;
	int i;

	/*
	 * We evade vblanks and pflips on crtc that
	 * should be changed. We do it here to flush & disable
	 * interrupts before drm_swap_state is called in drm_atomic_helper_commit
	 * it will update crtc->dm_crtc_state->stream pointer which is used in
	 * the ISRs.
	 */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct dm_crtc_state *dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		if (drm_atomic_crtc_needs_modeset(new_crtc_state) && dm_old_crtc_state->stream)
			manage_dm_interrupts(adev, acrtc, false);
	}
	/* Add check here for SoC's that support hardware cursor plane, to
	 * unset legacy_cursor_update */

	return drm_atomic_helper_commit(dev, state, nonblock);

	/*TODO Handle EINTR, reenable IRQ*/
}

static void amdgpu_dm_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct dm_atomic_state *dm_state;
	uint32_t i, j;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	unsigned long flags;
	bool wait_for_vblank = true;
	struct drm_connector *connector;
	struct drm_connector_state *old_con_state, *new_con_state;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	int crtc_disable_count = 0;

	drm_atomic_helper_update_legacy_modeset_state(dev, state);

	dm_state = to_dm_atomic_state(state);

	/* update changed items */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		DRM_DEBUG_DRIVER(
			"amdgpu_crtc id:%d crtc_state_flags: enable:%d, active:%d, "
			"planes_changed:%d, mode_changed:%d,active_changed:%d,"
			"connectors_changed:%d\n",
			acrtc->crtc_id,
			new_crtc_state->enable,
			new_crtc_state->active,
			new_crtc_state->planes_changed,
			new_crtc_state->mode_changed,
			new_crtc_state->active_changed,
			new_crtc_state->connectors_changed);

		/* Copy all transient state flags into dc state */
		if (dm_new_crtc_state->stream) {
			amdgpu_dm_crtc_copy_transient_flags(&dm_new_crtc_state->base,
							    dm_new_crtc_state->stream);
		}

		/* handles headless hotplug case, updating new_state and
		 * aconnector as needed
		 */

		if (modeset_required(new_crtc_state, dm_new_crtc_state->stream, dm_old_crtc_state->stream)) {

			DRM_DEBUG_DRIVER("Atomic commit: SET crtc id %d: [%p]\n", acrtc->crtc_id, acrtc);

			if (!dm_new_crtc_state->stream) {
				/*
				 * this could happen because of issues with
				 * userspace notifications delivery.
				 * In this case userspace tries to set mode on
				 * display which is disconnect in fact.
				 * dc_sink in NULL in this case on aconnector.
				 * We expect reset mode will come soon.
				 *
				 * This can also happen when unplug is done
				 * during resume sequence ended
				 *
				 * In this case, we want to pretend we still
				 * have a sink to keep the pipe running so that
				 * hw state is consistent with the sw state
				 */
				DRM_DEBUG_DRIVER("%s: Failed to create new stream for crtc %d\n",
						__func__, acrtc->base.base.id);
				continue;
			}

			if (dm_old_crtc_state->stream)
				remove_stream(adev, acrtc, dm_old_crtc_state->stream);

			pm_runtime_get_noresume(dev->dev);

			acrtc->enabled = true;
			acrtc->hw_mode = new_crtc_state->mode;
			crtc->hwmode = new_crtc_state->mode;
		} else if (modereset_required(new_crtc_state)) {
			DRM_DEBUG_DRIVER("Atomic commit: RESET. crtc id %d:[%p]\n", acrtc->crtc_id, acrtc);

			/* i.e. reset mode */
			if (dm_old_crtc_state->stream)
				remove_stream(adev, acrtc, dm_old_crtc_state->stream);
		}
	} /* for_each_crtc_in_state() */

	/*
	 * Add streams after required streams from new and replaced streams
	 * are removed from freesync module
	 */
	if (adev->dm.freesync_module) {
		for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state,
					      new_crtc_state, i) {
			struct amdgpu_dm_connector *aconnector = NULL;
			struct dm_connector_state *dm_new_con_state = NULL;
			struct amdgpu_crtc *acrtc = NULL;
			bool modeset_needed;

			dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
			dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);
			modeset_needed = modeset_required(
					new_crtc_state,
					dm_new_crtc_state->stream,
					dm_old_crtc_state->stream);
			/* We add stream to freesync if:
			 * 1. Said stream is not null, and
			 * 2. A modeset is requested. This means that the
			 *    stream was removed previously, and needs to be
			 *    replaced.
			 */
			if (dm_new_crtc_state->stream == NULL ||
					!modeset_needed)
				continue;

			acrtc = to_amdgpu_crtc(crtc);

			aconnector =
				amdgpu_dm_find_first_crtc_matching_connector(
					state, crtc);
			if (!aconnector) {
				DRM_DEBUG_DRIVER("Atomic commit: Failed to "
						 "find connector for acrtc "
						 "id:%d skipping freesync "
						 "init\n",
						 acrtc->crtc_id);
				continue;
			}

			mod_freesync_add_stream(adev->dm.freesync_module,
						dm_new_crtc_state->stream,
						&aconnector->caps);
			new_con_state = drm_atomic_get_new_connector_state(
					state, &aconnector->base);
			dm_new_con_state = to_dm_connector_state(new_con_state);

			mod_freesync_set_user_enable(adev->dm.freesync_module,
						     &dm_new_crtc_state->stream,
						     1,
						     &dm_new_con_state->user_enable);
		}
	}

	if (dm_state->context) {
		dm_enable_per_frame_crtc_master_sync(dm_state->context);
		WARN_ON(!dc_commit_state(dm->dc, dm_state->context));
	}

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (dm_new_crtc_state->stream != NULL) {
			const struct dc_stream_status *status =
					dc_stream_get_status(dm_new_crtc_state->stream);

			if (!status)
				DC_ERR("got no status for stream %p on acrtc%p\n", dm_new_crtc_state->stream, acrtc);
			else
				acrtc->otg_inst = status->primary_otg_inst;
		}
	}

	/* Handle scaling and underscan changes*/
	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);
		struct dm_connector_state *dm_old_con_state = to_dm_connector_state(old_con_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(dm_new_con_state->base.crtc);
		struct dc_stream_status *status = NULL;

		if (acrtc) {
			new_crtc_state = drm_atomic_get_new_crtc_state(state, &acrtc->base);
			old_crtc_state = drm_atomic_get_old_crtc_state(state, &acrtc->base);
		}

		/* Skip any modesets/resets */
		if (!acrtc || drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

		/* Skip any thing not scale or underscan changes */
		if (!is_scaling_state_different(dm_new_con_state, dm_old_con_state))
			continue;

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		update_stream_scaling_settings(&dm_new_con_state->base.crtc->mode,
				dm_new_con_state, (struct dc_stream_state *)dm_new_crtc_state->stream);

		if (!dm_new_crtc_state->stream)
			continue;

		status = dc_stream_get_status(dm_new_crtc_state->stream);
		WARN_ON(!status);
		WARN_ON(!status->plane_count);

		/*TODO How it works with MPO ?*/
		if (!commit_planes_to_stream(
				dm->dc,
				status->plane_states,
				status->plane_count,
				dm_new_crtc_state,
				to_dm_crtc_state(old_crtc_state),
				dm_state->context))
			dm_error("%s: Failed to update stream scaling!\n", __func__);
	}

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state,
			new_crtc_state, i) {
		/*
		 * loop to enable interrupts on newly arrived crtc
		 */
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
		bool modeset_needed;

		if (old_crtc_state->active && !new_crtc_state->active)
			crtc_disable_count++;

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);
		modeset_needed = modeset_required(
				new_crtc_state,
				dm_new_crtc_state->stream,
				dm_old_crtc_state->stream);

		if (dm_new_crtc_state->stream == NULL || !modeset_needed)
			continue;

		if (adev->dm.freesync_module)
			mod_freesync_notify_mode_change(
				adev->dm.freesync_module,
				&dm_new_crtc_state->stream, 1);

		manage_dm_interrupts(adev, acrtc, true);
	}

	/* update planes when needed per crtc*/
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, j) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (dm_new_crtc_state->stream)
			amdgpu_dm_commit_planes(state, dev, dm, crtc, &wait_for_vblank);
	}


	/*
	 * send vblank event on all events not handled in flip and
	 * mark consumed event for drm_atomic_helper_commit_hw_done
	 */
	spin_lock_irqsave(&adev->ddev->event_lock, flags);
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {

		if (new_crtc_state->event)
			drm_send_event_locked(dev, &new_crtc_state->event->base);

		new_crtc_state->event = NULL;
	}
	spin_unlock_irqrestore(&adev->ddev->event_lock, flags);

	/* Signal HW programming completion */
	drm_atomic_helper_commit_hw_done(state);

	if (wait_for_vblank)
		drm_atomic_helper_wait_for_flip_done(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);

	/* Finally, drop a runtime PM reference for each newly disabled CRTC,
	 * so we can put the GPU into runtime suspend if we're not driving any
	 * displays anymore
	 */
	for (i = 0; i < crtc_disable_count; i++)
		pm_runtime_put_autosuspend(dev->dev);
	pm_runtime_mark_last_busy(dev->dev);
}


static int dm_force_atomic_commit(struct drm_connector *connector)
{
	int ret = 0;
	struct drm_device *ddev = connector->dev;
	struct drm_atomic_state *state = drm_atomic_state_alloc(ddev);
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
	 * Attach connectors to drm_atomic_state
	 */
	conn_state = drm_atomic_get_connector_state(state, connector);

	ret = PTR_ERR_OR_ZERO(conn_state);
	if (ret)
		goto err;

	/* Attach crtc to drm_atomic_state*/
	crtc_state = drm_atomic_get_crtc_state(state, &disconnected_acrtc->base);

	ret = PTR_ERR_OR_ZERO(crtc_state);
	if (ret)
		goto err;

	/* force a restore */
	crtc_state->mode_changed = true;

	/* Attach plane to drm_atomic_state */
	plane_state = drm_atomic_get_plane_state(state, plane);

	ret = PTR_ERR_OR_ZERO(plane_state);
	if (ret)
		goto err;


	/* Call commit internally with the state we just constructed */
	ret = drm_atomic_commit(state);
	if (!ret)
		return 0;

err:
	DRM_ERROR("Restoring old state failed with %i\n", ret);
	drm_atomic_state_put(state);

	return ret;
}

/*
 * This functions handle all cases when set mode does not come upon hotplug.
 * This include when the same display is unplugged then plugged back into the
 * same port and when we are running without usermode desktop manager supprot
 */
void dm_restore_drm_connector_state(struct drm_device *dev,
				    struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct amdgpu_crtc *disconnected_acrtc;
	struct dm_crtc_state *acrtc_state;

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

/*`
 * Grabs all modesetting locks to serialize against any blocking commits,
 * Waits for completion of all non blocking commits.
 */
static int do_aquire_global_lock(struct drm_device *dev,
				 struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_commit *commit;
	long ret;

	/* Adding all modeset locks to aquire_ctx will
	 * ensure that when the framework release it the
	 * extra locks we are locking here will get released to
	 */
	ret = drm_modeset_lock_all_ctx(dev, state->acquire_ctx);
	if (ret)
		return ret;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		spin_lock(&crtc->commit_lock);
		commit = list_first_entry_or_null(&crtc->commit_list,
				struct drm_crtc_commit, commit_entry);
		if (commit)
			drm_crtc_commit_get(commit);
		spin_unlock(&crtc->commit_lock);

		if (!commit)
			continue;

		/* Make sure all pending HW programming completed and
		 * page flips done
		 */
		ret = wait_for_completion_interruptible_timeout(&commit->hw_done, 10*HZ);

		if (ret > 0)
			ret = wait_for_completion_interruptible_timeout(
					&commit->flip_done, 10*HZ);

		if (ret == 0)
			DRM_ERROR("[CRTC:%d:%s] hw_done or flip_done "
				  "timed out\n", crtc->base.id, crtc->name);

		drm_crtc_commit_put(commit);
	}

	return ret < 0 ? ret : 0;
}

static int dm_update_crtcs_state(struct dc *dc,
				 struct drm_atomic_state *state,
				 bool enable,
				 bool *lock_and_validation_needed)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	int i;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	struct dm_atomic_state *dm_state = to_dm_atomic_state(state);
	struct dc_stream_state *new_stream;
	int ret = 0;

	/*TODO Move this code into dm_crtc_atomic_check once we get rid of dc_validation_set */
	/* update changed items */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = NULL;
		struct amdgpu_dm_connector *aconnector = NULL;
		struct drm_connector_state *drm_new_conn_state = NULL, *drm_old_conn_state = NULL;
		struct dm_connector_state *dm_new_conn_state = NULL, *dm_old_conn_state = NULL;
		struct drm_plane_state *new_plane_state = NULL;

		new_stream = NULL;

		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		acrtc = to_amdgpu_crtc(crtc);

		new_plane_state = drm_atomic_get_new_plane_state(state, new_crtc_state->crtc->primary);

		if (new_crtc_state->enable && new_plane_state && !new_plane_state->fb) {
			ret = -EINVAL;
			goto fail;
		}

		aconnector = amdgpu_dm_find_first_crtc_matching_connector(state, crtc);

		/* TODO This hack should go away */
		if (aconnector && enable) {
			// Make sure fake sink is created in plug-in scenario
			drm_new_conn_state = drm_atomic_get_new_connector_state(state,
 								    &aconnector->base);
			drm_old_conn_state = drm_atomic_get_old_connector_state(state,
								    &aconnector->base);

			if (IS_ERR(drm_new_conn_state)) {
				ret = PTR_ERR_OR_ZERO(drm_new_conn_state);
				break;
			}

			dm_new_conn_state = to_dm_connector_state(drm_new_conn_state);
			dm_old_conn_state = to_dm_connector_state(drm_old_conn_state);

			new_stream = create_stream_for_sink(aconnector,
							     &new_crtc_state->mode,
							    dm_new_conn_state);

			/*
			 * we can have no stream on ACTION_SET if a display
			 * was disconnected during S3, in this case it not and
			 * error, the OS will be updated after detection, and
			 * do the right thing on next atomic commit
			 */

			if (!new_stream) {
				DRM_DEBUG_DRIVER("%s: Failed to create new stream for crtc %d\n",
						__func__, acrtc->base.base.id);
				break;
			}

			if (dc_is_stream_unchanged(new_stream, dm_old_crtc_state->stream) &&
			    dc_is_stream_scaling_unchanged(new_stream, dm_old_crtc_state->stream)) {
				new_crtc_state->mode_changed = false;
				DRM_DEBUG_DRIVER("Mode change not required, setting mode_changed to %d",
						 new_crtc_state->mode_changed);
			}
		}

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			goto next_crtc;

		DRM_DEBUG_DRIVER(
			"amdgpu_crtc id:%d crtc_state_flags: enable:%d, active:%d, "
			"planes_changed:%d, mode_changed:%d,active_changed:%d,"
			"connectors_changed:%d\n",
			acrtc->crtc_id,
			new_crtc_state->enable,
			new_crtc_state->active,
			new_crtc_state->planes_changed,
			new_crtc_state->mode_changed,
			new_crtc_state->active_changed,
			new_crtc_state->connectors_changed);

		/* Remove stream for any changed/disabled CRTC */
		if (!enable) {

			if (!dm_old_crtc_state->stream)
				goto next_crtc;

			DRM_DEBUG_DRIVER("Disabling DRM crtc: %d\n",
					crtc->base.id);

			/* i.e. reset mode */
			if (dc_remove_stream_from_ctx(
					dc,
					dm_state->context,
					dm_old_crtc_state->stream) != DC_OK) {
				ret = -EINVAL;
				goto fail;
			}

			dc_stream_release(dm_old_crtc_state->stream);
			dm_new_crtc_state->stream = NULL;

			*lock_and_validation_needed = true;

		} else {/* Add stream for any updated/enabled CRTC */
			/*
			 * Quick fix to prevent NULL pointer on new_stream when
			 * added MST connectors not found in existing crtc_state in the chained mode
			 * TODO: need to dig out the root cause of that
			 */
			if (!aconnector || (!aconnector->dc_sink && aconnector->mst_port))
				goto next_crtc;

			if (modereset_required(new_crtc_state))
				goto next_crtc;

			if (modeset_required(new_crtc_state, new_stream,
					     dm_old_crtc_state->stream)) {

				WARN_ON(dm_new_crtc_state->stream);

				dm_new_crtc_state->stream = new_stream;

				dc_stream_retain(new_stream);

				DRM_DEBUG_DRIVER("Enabling DRM crtc: %d\n",
							crtc->base.id);

				if (dc_add_stream_to_ctx(
						dc,
						dm_state->context,
						dm_new_crtc_state->stream) != DC_OK) {
					ret = -EINVAL;
					goto fail;
				}

				*lock_and_validation_needed = true;
			}
		}

next_crtc:
		/* Release extra reference */
		if (new_stream)
			 dc_stream_release(new_stream);

		/*
		 * We want to do dc stream updates that do not require a
		 * full modeset below.
		 */
		if (!(enable && aconnector && new_crtc_state->enable &&
		      new_crtc_state->active))
			continue;
		/*
		 * Given above conditions, the dc state cannot be NULL because:
		 * 1. We're in the process of enabling CRTCs (just been added
		 *    to the dc context, or already is on the context)
		 * 2. Has a valid connector attached, and
		 * 3. Is currently active and enabled.
		 * => The dc stream state currently exists.
		 */
		BUG_ON(dm_new_crtc_state->stream == NULL);

		/* Scaling or underscan settings */
		if (is_scaling_state_different(dm_old_conn_state, dm_new_conn_state))
			update_stream_scaling_settings(
				&new_crtc_state->mode, dm_new_conn_state, dm_new_crtc_state->stream);

		/*
		 * Color management settings. We also update color properties
		 * when a modeset is needed, to ensure it gets reprogrammed.
		 */
		if (dm_new_crtc_state->base.color_mgmt_changed ||
		    drm_atomic_crtc_needs_modeset(new_crtc_state)) {
			ret = amdgpu_dm_set_regamma_lut(dm_new_crtc_state);
			if (ret)
				goto fail;
			amdgpu_dm_set_ctm(dm_new_crtc_state);
		}
	}

	return ret;

fail:
	if (new_stream)
		dc_stream_release(new_stream);
	return ret;
}

static int dm_update_planes_state(struct dc *dc,
				  struct drm_atomic_state *state,
				  bool enable,
				  bool *lock_and_validation_needed)
{
	struct drm_crtc *new_plane_crtc, *old_plane_crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	struct dm_crtc_state *dm_new_crtc_state, *dm_old_crtc_state;
	struct dm_atomic_state *dm_state = to_dm_atomic_state(state);
	struct dm_plane_state *dm_new_plane_state, *dm_old_plane_state;
	int i ;
	/* TODO return page_flip_needed() function */
	bool pflip_needed  = !state->allow_modeset;
	int ret = 0;


	/* Add new planes, in reverse order as DC expectation */
	for_each_oldnew_plane_in_state_reverse(state, plane, old_plane_state, new_plane_state, i) {
		new_plane_crtc = new_plane_state->crtc;
		old_plane_crtc = old_plane_state->crtc;
		dm_new_plane_state = to_dm_plane_state(new_plane_state);
		dm_old_plane_state = to_dm_plane_state(old_plane_state);

		/*TODO Implement atomic check for cursor plane */
		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			continue;

		/* Remove any changed/removed planes */
		if (!enable) {
			if (pflip_needed &&
			    plane->type != DRM_PLANE_TYPE_OVERLAY)
				continue;

			if (!old_plane_crtc)
				continue;

			old_crtc_state = drm_atomic_get_old_crtc_state(
					state, old_plane_crtc);
			dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

			if (!dm_old_crtc_state->stream)
				continue;

			DRM_DEBUG_ATOMIC("Disabling DRM plane: %d on DRM crtc %d\n",
					plane->base.id, old_plane_crtc->base.id);

			if (!dc_remove_plane_from_context(
					dc,
					dm_old_crtc_state->stream,
					dm_old_plane_state->dc_state,
					dm_state->context)) {

				ret = EINVAL;
				return ret;
			}


			dc_plane_state_release(dm_old_plane_state->dc_state);
			dm_new_plane_state->dc_state = NULL;

			*lock_and_validation_needed = true;

		} else { /* Add new planes */
			struct dc_plane_state *dc_new_plane_state;

			if (drm_atomic_plane_disabling(plane->state, new_plane_state))
				continue;

			if (!new_plane_crtc)
				continue;

			new_crtc_state = drm_atomic_get_new_crtc_state(state, new_plane_crtc);
			dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

			if (!dm_new_crtc_state->stream)
				continue;

			if (pflip_needed &&
			    plane->type != DRM_PLANE_TYPE_OVERLAY)
				continue;

			WARN_ON(dm_new_plane_state->dc_state);

			dc_new_plane_state = dc_create_plane_state(dc);
			if (!dc_new_plane_state)
				return -ENOMEM;

			DRM_DEBUG_DRIVER("Enabling DRM plane: %d on DRM crtc %d\n",
					plane->base.id, new_plane_crtc->base.id);

			ret = fill_plane_attributes(
				new_plane_crtc->dev->dev_private,
				dc_new_plane_state,
				new_plane_state,
				new_crtc_state);
			if (ret) {
				dc_plane_state_release(dc_new_plane_state);
				return ret;
			}

			/*
			 * Any atomic check errors that occur after this will
			 * not need a release. The plane state will be attached
			 * to the stream, and therefore part of the atomic
			 * state. It'll be released when the atomic state is
			 * cleaned.
			 */
			if (!dc_add_plane_to_context(
					dc,
					dm_new_crtc_state->stream,
					dc_new_plane_state,
					dm_state->context)) {

				dc_plane_state_release(dc_new_plane_state);
				return -EINVAL;
			}

			dm_new_plane_state->dc_state = dc_new_plane_state;

			/* Tell DC to do a full surface update every time there
			 * is a plane change. Inefficient, but works for now.
			 */
			dm_new_plane_state->dc_state->update_flags.bits.full_update = 1;

			*lock_and_validation_needed = true;
		}
	}


	return ret;
}

static int amdgpu_dm_atomic_check(struct drm_device *dev,
				  struct drm_atomic_state *state)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct dc *dc = adev->dm.dc;
	struct dm_atomic_state *dm_state = to_dm_atomic_state(state);
	struct drm_connector *connector;
	struct drm_connector_state *old_con_state, *new_con_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	int ret, i;

	/*
	 * This bool will be set for true for any modeset/reset
	 * or plane update which implies non fast surface update.
	 */
	bool lock_and_validation_needed = false;

	ret = drm_atomic_helper_check_modeset(dev, state);
	if (ret)
		goto fail;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		if (!drm_atomic_crtc_needs_modeset(new_crtc_state) &&
		    !new_crtc_state->color_mgmt_changed)
			continue;

		if (!new_crtc_state->enable)
			continue;

		ret = drm_atomic_add_affected_connectors(state, crtc);
		if (ret)
			return ret;

		ret = drm_atomic_add_affected_planes(state, crtc);
		if (ret)
			goto fail;
	}

	dm_state->context = dc_create_state();
	ASSERT(dm_state->context);
	dc_resource_state_copy_construct_current(dc, dm_state->context);

	/* Remove exiting planes if they are modified */
	ret = dm_update_planes_state(dc, state, false, &lock_and_validation_needed);
	if (ret) {
		goto fail;
	}

	/* Disable all crtcs which require disable */
	ret = dm_update_crtcs_state(dc, state, false, &lock_and_validation_needed);
	if (ret) {
		goto fail;
	}

	/* Enable all crtcs which require enable */
	ret = dm_update_crtcs_state(dc, state, true, &lock_and_validation_needed);
	if (ret) {
		goto fail;
	}

	/* Add new/modified planes */
	ret = dm_update_planes_state(dc, state, true, &lock_and_validation_needed);
	if (ret) {
		goto fail;
	}

	/* Run this here since we want to validate the streams we created */
	ret = drm_atomic_helper_check_planes(dev, state);
	if (ret)
		goto fail;

	/* Check scaling and underscan changes*/
	/*TODO Removed scaling changes validation due to inability to commit
	 * new stream into context w\o causing full reset. Need to
	 * decide how to handle.
	 */
	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		struct dm_connector_state *dm_old_con_state = to_dm_connector_state(old_con_state);
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(dm_new_con_state->base.crtc);

		/* Skip any modesets/resets */
		if (!acrtc || drm_atomic_crtc_needs_modeset(
				drm_atomic_get_new_crtc_state(state, &acrtc->base)))
			continue;

		/* Skip any thing not scale or underscan changes */
		if (!is_scaling_state_different(dm_new_con_state, dm_old_con_state))
			continue;

		lock_and_validation_needed = true;
	}

	/*
	 * For full updates case when
	 * removing/adding/updating  streams on once CRTC while flipping
	 * on another CRTC,
	 * acquiring global lock  will guarantee that any such full
	 * update commit
	 * will wait for completion of any outstanding flip using DRMs
	 * synchronization events.
	 */

	if (lock_and_validation_needed) {

		ret = do_aquire_global_lock(dev, state);
		if (ret)
			goto fail;

		if (dc_validate_global_state(dc, dm_state->context) != DC_OK) {
			ret = -EINVAL;
			goto fail;
		}
	}

	/* Must be success */
	WARN_ON(ret);
	return ret;

fail:
	if (ret == -EDEADLK)
		DRM_DEBUG_DRIVER("Atomic check stopped to avoid deadlock.\n");
	else if (ret == -EINTR || ret == -EAGAIN || ret == -ERESTARTSYS)
		DRM_DEBUG_DRIVER("Atomic check stopped due to signal.\n");
	else
		DRM_DEBUG_DRIVER("Atomic check failed with err: %d \n", ret);

	return ret;
}

static bool is_dp_capable_without_timing_msa(struct dc *dc,
					     struct amdgpu_dm_connector *amdgpu_dm_connector)
{
	uint8_t dpcd_data;
	bool capable = false;

	if (amdgpu_dm_connector->dc_link &&
		dm_helpers_dp_read_dpcd(
				NULL,
				amdgpu_dm_connector->dc_link,
				DP_DOWN_STREAM_PORT_COUNT,
				&dpcd_data,
				sizeof(dpcd_data))) {
		capable = (dpcd_data & DP_MSA_TIMING_PAR_IGNORED) ? true:false;
	}

	return capable;
}
void amdgpu_dm_add_sink_to_freesync_module(struct drm_connector *connector,
					   struct edid *edid)
{
	int i;
	bool edid_check_required;
	struct detailed_timing *timing;
	struct detailed_non_pixel *data;
	struct detailed_data_monitor_range *range;
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);
	struct dm_connector_state *dm_con_state;

	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;

	if (!connector->state) {
		DRM_ERROR("%s - Connector has no state", __func__);
		return;
	}

	dm_con_state = to_dm_connector_state(connector->state);

	edid_check_required = false;
	if (!amdgpu_dm_connector->dc_sink) {
		DRM_ERROR("dc_sink NULL, could not add free_sync module.\n");
		return;
	}
	if (!adev->dm.freesync_module)
		return;
	/*
	 * if edid non zero restrict freesync only for dp and edp
	 */
	if (edid) {
		if (amdgpu_dm_connector->dc_sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT
			|| amdgpu_dm_connector->dc_sink->sink_signal == SIGNAL_TYPE_EDP) {
			edid_check_required = is_dp_capable_without_timing_msa(
						adev->dm.dc,
						amdgpu_dm_connector);
		}
	}
	dm_con_state->freesync_capable = false;
	if (edid_check_required == true && (edid->version > 1 ||
	   (edid->version == 1 && edid->revision > 1))) {
		for (i = 0; i < 4; i++) {

			timing	= &edid->detailed_timings[i];
			data	= &timing->data.other_data;
			range	= &data->data.range;
			/*
			 * Check if monitor has continuous frequency mode
			 */
			if (data->type != EDID_DETAIL_MONITOR_RANGE)
				continue;
			/*
			 * Check for flag range limits only. If flag == 1 then
			 * no additional timing information provided.
			 * Default GTF, GTF Secondary curve and CVT are not
			 * supported
			 */
			if (range->flags != 1)
				continue;

			amdgpu_dm_connector->min_vfreq = range->min_vfreq;
			amdgpu_dm_connector->max_vfreq = range->max_vfreq;
			amdgpu_dm_connector->pixel_clock_mhz =
				range->pixel_clock_mhz * 10;
			break;
		}

		if (amdgpu_dm_connector->max_vfreq -
				amdgpu_dm_connector->min_vfreq > 10) {
			amdgpu_dm_connector->caps.supported = true;
			amdgpu_dm_connector->caps.min_refresh_in_micro_hz =
					amdgpu_dm_connector->min_vfreq * 1000000;
			amdgpu_dm_connector->caps.max_refresh_in_micro_hz =
					amdgpu_dm_connector->max_vfreq * 1000000;
			dm_con_state->freesync_capable = true;
		}
	}

	/*
	 * TODO figure out how to notify user-mode or DRM of freesync caps
	 * once we figure out how to deal with freesync in an upstreamable
	 * fashion
	 */

}

void amdgpu_dm_remove_sink_from_freesync_module(struct drm_connector *connector)
{
	/*
	 * TODO fill in once we figure out how to deal with freesync in
	 * an upstreamable fashion
	 */
}
