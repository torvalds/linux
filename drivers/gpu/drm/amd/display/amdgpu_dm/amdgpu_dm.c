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

/* The caprices of the preprocessor require that this be declared right here */
#define CREATE_TRACE_POINTS

#include "dm_services_types.h"
#include "dc.h"
#include "dc/inc/core_types.h"
#include "dal_asic_id.h"

#include "vid.h"
#include "amdgpu.h"
#include "amdgpu_display.h"
#include "amdgpu_ucode.h"
#include "atom.h"
#include "amdgpu_dm.h"
#include "amdgpu_pm.h"

#include "amd_shared.h"
#include "amdgpu_dm_irq.h"
#include "dm_helpers.h"
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
#include <linux/firmware.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_mst_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_edid.h>

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
#include "ivsrcid/irqsrcs_dcn_1_0.h"

#include "dcn/dcn_1_0_offset.h"
#include "dcn/dcn_1_0_sh_mask.h"
#include "soc15_hw_ip.h"
#include "vega10_ip_offset.h"

#include "soc15_common.h"
#endif

#include "modules/inc/mod_freesync.h"
#include "modules/power/power_helpers.h"
#include "modules/inc/mod_info_packet.h"

#define FIRMWARE_RAVEN_DMCU		"amdgpu/raven_dmcu.bin"
MODULE_FIRMWARE(FIRMWARE_RAVEN_DMCU);

/**
 * DOC: overview
 *
 * The AMDgpu display manager, **amdgpu_dm** (or even simpler,
 * **dm**) sits between DRM and DC. It acts as a liason, converting DRM
 * requests into DC requests, and DC responses into DRM responses.
 *
 * The root control structure is &struct amdgpu_display_manager.
 */

/* basic init/fini API */
static int amdgpu_dm_init(struct amdgpu_device *adev);
static void amdgpu_dm_fini(struct amdgpu_device *adev);

/*
 * initializes drm_device display related structures, based on the information
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
				struct drm_plane *plane,
				unsigned long possible_crtcs,
				const struct dc_plane_cap *plane_cap);
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

static void handle_cursor_update(struct drm_plane *plane,
				 struct drm_plane_state *old_plane_state);

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

static inline bool amdgpu_dm_vrr_active(struct dm_crtc_state *dm_state)
{
	return dm_state->freesync_config.state == VRR_STATE_ACTIVE_VARIABLE ||
	       dm_state->freesync_config.state == VRR_STATE_ACTIVE_FIXED;
}

static void dm_pflip_high_irq(void *interrupt_params)
{
	struct amdgpu_crtc *amdgpu_crtc;
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	unsigned long flags;
	struct drm_pending_vblank_event *e;
	struct dm_crtc_state *acrtc_state;
	uint32_t vpos, hpos, v_blank_start, v_blank_end;
	bool vrr_active;

	amdgpu_crtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_PFLIP);

	/* IRQ could occur when in initial stage */
	/* TODO work and BO cleanup */
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

	/* page flip completed. */
	e = amdgpu_crtc->event;
	amdgpu_crtc->event = NULL;

	if (!e)
		WARN_ON(1);

	acrtc_state = to_dm_crtc_state(amdgpu_crtc->base.state);
	vrr_active = amdgpu_dm_vrr_active(acrtc_state);

	/* Fixed refresh rate, or VRR scanout position outside front-porch? */
	if (!vrr_active ||
	    !dc_stream_get_scanoutpos(acrtc_state->stream, &v_blank_start,
				      &v_blank_end, &hpos, &vpos) ||
	    (vpos < v_blank_start)) {
		/* Update to correct count and vblank timestamp if racing with
		 * vblank irq. This also updates to the correct vblank timestamp
		 * even in VRR mode, as scanout is past the front-porch atm.
		 */
		drm_crtc_accurate_vblank_count(&amdgpu_crtc->base);

		/* Wake up userspace by sending the pageflip event with proper
		 * count and timestamp of vblank of flip completion.
		 */
		if (e) {
			drm_crtc_send_vblank_event(&amdgpu_crtc->base, e);

			/* Event sent, so done with vblank for this flip */
			drm_crtc_vblank_put(&amdgpu_crtc->base);
		}
	} else if (e) {
		/* VRR active and inside front-porch: vblank count and
		 * timestamp for pageflip event will only be up to date after
		 * drm_crtc_handle_vblank() has been executed from late vblank
		 * irq handler after start of back-porch (vline 0). We queue the
		 * pageflip event for send-out by drm_crtc_handle_vblank() with
		 * updated timestamp and count, once it runs after us.
		 *
		 * We need to open-code this instead of using the helper
		 * drm_crtc_arm_vblank_event(), as that helper would
		 * call drm_crtc_accurate_vblank_count(), which we must
		 * not call in VRR mode while we are in front-porch!
		 */

		/* sequence will be replaced by real count during send-out. */
		e->sequence = drm_crtc_vblank_count(&amdgpu_crtc->base);
		e->pipe = amdgpu_crtc->crtc_id;

		list_add_tail(&e->base.link, &adev->ddev->vblank_event_list);
		e = NULL;
	}

	/* Keep track of vblank of this flip for flip throttling. We use the
	 * cooked hw counter, as that one incremented at start of this vblank
	 * of pageflip completion, so last_flip_vblank is the forbidden count
	 * for queueing new pageflips if vsync + VRR is enabled.
	 */
	amdgpu_crtc->last_flip_vblank = amdgpu_get_vblank_counter_kms(adev->ddev,
							amdgpu_crtc->crtc_id);

	amdgpu_crtc->pflip_status = AMDGPU_FLIP_NONE;
	spin_unlock_irqrestore(&adev->ddev->event_lock, flags);

	DRM_DEBUG_DRIVER("crtc:%d[%p], pflip_stat:AMDGPU_FLIP_NONE, vrr[%d]-fp %d\n",
			 amdgpu_crtc->crtc_id, amdgpu_crtc,
			 vrr_active, (int) !e);
}

static void dm_vupdate_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_crtc *acrtc;
	struct dm_crtc_state *acrtc_state;
	unsigned long flags;

	acrtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VUPDATE);

	if (acrtc) {
		acrtc_state = to_dm_crtc_state(acrtc->base.state);

		DRM_DEBUG_DRIVER("crtc:%d, vupdate-vrr:%d\n", acrtc->crtc_id,
				 amdgpu_dm_vrr_active(acrtc_state));

		/* Core vblank handling is done here after end of front-porch in
		 * vrr mode, as vblank timestamping will give valid results
		 * while now done after front-porch. This will also deliver
		 * page-flip completion events that have been queued to us
		 * if a pageflip happened inside front-porch.
		 */
		if (amdgpu_dm_vrr_active(acrtc_state)) {
			drm_crtc_handle_vblank(&acrtc->base);

			/* BTR processing for pre-DCE12 ASICs */
			if (acrtc_state->stream &&
			    adev->family < AMDGPU_FAMILY_AI) {
				spin_lock_irqsave(&adev->ddev->event_lock, flags);
				mod_freesync_handle_v_update(
				    adev->dm.freesync_module,
				    acrtc_state->stream,
				    &acrtc_state->vrr_params);

				dc_stream_adjust_vmin_vmax(
				    adev->dm.dc,
				    acrtc_state->stream,
				    &acrtc_state->vrr_params.adjust);
				spin_unlock_irqrestore(&adev->ddev->event_lock, flags);
			}
		}
	}
}

static void dm_crtc_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_crtc *acrtc;
	struct dm_crtc_state *acrtc_state;
	unsigned long flags;

	acrtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VBLANK);

	if (acrtc) {
		acrtc_state = to_dm_crtc_state(acrtc->base.state);

		DRM_DEBUG_DRIVER("crtc:%d, vupdate-vrr:%d\n", acrtc->crtc_id,
				 amdgpu_dm_vrr_active(acrtc_state));

		/* Core vblank handling at start of front-porch is only possible
		 * in non-vrr mode, as only there vblank timestamping will give
		 * valid results while done in front-porch. Otherwise defer it
		 * to dm_vupdate_high_irq after end of front-porch.
		 */
		if (!amdgpu_dm_vrr_active(acrtc_state))
			drm_crtc_handle_vblank(&acrtc->base);

		/* Following stuff must happen at start of vblank, for crc
		 * computation and below-the-range btr support in vrr mode.
		 */
		amdgpu_dm_crtc_handle_crc_irq(&acrtc->base);

		if (acrtc_state->stream && adev->family >= AMDGPU_FAMILY_AI &&
		    acrtc_state->vrr_params.supported &&
		    acrtc_state->freesync_config.state == VRR_STATE_ACTIVE_VARIABLE) {
			spin_lock_irqsave(&adev->ddev->event_lock, flags);
			mod_freesync_handle_v_update(
				adev->dm.freesync_module,
				acrtc_state->stream,
				&acrtc_state->vrr_params);

			dc_stream_adjust_vmin_vmax(
				adev->dm.dc,
				acrtc_state->stream,
				&acrtc_state->vrr_params.adjust);
			spin_unlock_irqrestore(&adev->ddev->event_lock, flags);
		}
	}
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

static int amdgpu_dm_init(struct amdgpu_device *adev)
{
	struct dc_init_data init_data;
	adev->dm.ddev = adev->ddev;
	adev->dm.adev = adev;

	/* Zero all the fields */
	memset(&init_data, 0, sizeof(init_data));

	mutex_init(&adev->dm.dc_lock);

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

	init_data.dce_environment = DCE_ENV_PRODUCTION_DRV;

	/*
	 * TODO debug why this doesn't work on Raven
	 */
	if (adev->flags & AMD_IS_APU &&
	    adev->asic_type >= CHIP_CARRIZO &&
	    adev->asic_type < CHIP_RAVEN)
		init_data.flags.gpu_vm_support = true;

	if (amdgpu_dc_feature_mask & DC_FBC_MASK)
		init_data.flags.fbc_support = true;

	init_data.flags.power_down_display_on_boot = true;

	/* Display Core create. */
	adev->dm.dc = dc_create(&init_data);

	if (adev->dm.dc) {
		DRM_INFO("Display Core initialized with v%s!\n", DC_VER);
	} else {
		DRM_INFO("Display Core failed to initialize with v%s!\n", DC_VER);
		goto error;
	}

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

#if defined(CONFIG_DEBUG_FS)
	if (dtn_debugfs_init(adev))
		DRM_ERROR("amdgpu: failed initialize dtn debugfs support.\n");
#endif

	DRM_DEBUG_DRIVER("KMS initialized.\n");

	return 0;
error:
	amdgpu_dm_fini(adev);

	return -EINVAL;
}

static void amdgpu_dm_fini(struct amdgpu_device *adev)
{
	amdgpu_dm_destroy_drm_device(&adev->dm);

	/* DC Destroy TODO: Replace destroy DAL */
	if (adev->dm.dc)
		dc_destroy(&adev->dm.dc);
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

	mutex_destroy(&adev->dm.dc_lock);

	return;
}

static int load_dmcu_fw(struct amdgpu_device *adev)
{
	const char *fw_name_dmcu = NULL;
	int r;
	const struct dmcu_firmware_header_v1_0 *hdr;

	switch(adev->asic_type) {
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
		return 0;
	case CHIP_RAVEN:
		if (ASICREV_IS_PICASSO(adev->external_rev_id))
			fw_name_dmcu = FIRMWARE_RAVEN_DMCU;
		else if (ASICREV_IS_RAVEN2(adev->external_rev_id))
			fw_name_dmcu = FIRMWARE_RAVEN_DMCU;
		else
			return 0;
		break;
	default:
		DRM_ERROR("Unsupported ASIC type: 0x%X\n", adev->asic_type);
		return -EINVAL;
	}

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		DRM_DEBUG_KMS("dm: DMCU firmware not supported on direct or SMU loading\n");
		return 0;
	}

	r = request_firmware_direct(&adev->dm.fw_dmcu, fw_name_dmcu, adev->dev);
	if (r == -ENOENT) {
		/* DMCU firmware is not necessary, so don't raise a fuss if it's missing */
		DRM_DEBUG_KMS("dm: DMCU firmware not found\n");
		adev->dm.fw_dmcu = NULL;
		return 0;
	}
	if (r) {
		dev_err(adev->dev, "amdgpu_dm: Can't load firmware \"%s\"\n",
			fw_name_dmcu);
		return r;
	}

	r = amdgpu_ucode_validate(adev->dm.fw_dmcu);
	if (r) {
		dev_err(adev->dev, "amdgpu_dm: Can't validate firmware \"%s\"\n",
			fw_name_dmcu);
		release_firmware(adev->dm.fw_dmcu);
		adev->dm.fw_dmcu = NULL;
		return r;
	}

	hdr = (const struct dmcu_firmware_header_v1_0 *)adev->dm.fw_dmcu->data;
	adev->firmware.ucode[AMDGPU_UCODE_ID_DMCU_ERAM].ucode_id = AMDGPU_UCODE_ID_DMCU_ERAM;
	adev->firmware.ucode[AMDGPU_UCODE_ID_DMCU_ERAM].fw = adev->dm.fw_dmcu;
	adev->firmware.fw_size +=
		ALIGN(le32_to_cpu(hdr->header.ucode_size_bytes) - le32_to_cpu(hdr->intv_size_bytes), PAGE_SIZE);

	adev->firmware.ucode[AMDGPU_UCODE_ID_DMCU_INTV].ucode_id = AMDGPU_UCODE_ID_DMCU_INTV;
	adev->firmware.ucode[AMDGPU_UCODE_ID_DMCU_INTV].fw = adev->dm.fw_dmcu;
	adev->firmware.fw_size +=
		ALIGN(le32_to_cpu(hdr->intv_size_bytes), PAGE_SIZE);

	adev->dm.dmcu_fw_version = le32_to_cpu(hdr->header.ucode_version);

	DRM_DEBUG_KMS("PSP loading DMCU firmware\n");

	return 0;
}

static int dm_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return load_dmcu_fw(adev);
}

static int dm_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if(adev->dm.fw_dmcu) {
		release_firmware(adev->dm.fw_dmcu);
		adev->dm.fw_dmcu = NULL;
	}

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

	struct dmcu_iram_parameters params;
	unsigned int linear_lut[16];
	int i;
	struct dmcu *dmcu = adev->dm.dc->res_pool->dmcu;
	bool ret;

	for (i = 0; i < 16; i++)
		linear_lut[i] = 0xFFFF * i / 15;

	params.set = 0;
	params.backlight_ramping_start = 0xCCCC;
	params.backlight_ramping_reduction = 0xCCCCCCCC;
	params.backlight_lut_array_size = 16;
	params.backlight_lut_array = linear_lut;

	ret = dmcu_load_iram(dmcu, params);

	if (!ret)
		return -EINVAL;

	return detect_mst_link_for_all_connectors(adev->ddev);
}

static void s3_handle_mst(struct drm_device *dev, bool suspend)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_dp_mst_topology_mgr *mgr;
	int ret;
	bool need_hotplug = false;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	list_for_each_entry(connector, &dev->mode_config.connector_list,
			    head) {
		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->dc_link->type != dc_connection_mst_branch ||
		    aconnector->mst_port)
			continue;

		mgr = &aconnector->mst_mgr;

		if (suspend) {
			drm_dp_mst_topology_mgr_suspend(mgr);
		} else {
			ret = drm_dp_mst_topology_mgr_resume(mgr);
			if (ret < 0) {
				drm_dp_mst_topology_mgr_set_mst(mgr, false);
				need_hotplug = true;
			}
		}
	}

	drm_modeset_unlock(&dev->mode_config.connection_mutex);

	if (need_hotplug)
		drm_kms_helper_hotplug_event(dev);
}

/**
 * dm_hw_init() - Initialize DC device
 * @handle: The base driver device containing the amdpgu_dm device.
 *
 * Initialize the &struct amdgpu_display_manager device. This involves calling
 * the initializers of each DM component, then populating the struct with them.
 *
 * Although the function implies hardware initialization, both hardware and
 * software are initialized here. Splitting them out to their relevant init
 * hooks is a future TODO item.
 *
 * Some notable things that are initialized here:
 *
 * - Display Core, both software and hardware
 * - DC modules that we need (freesync and color management)
 * - DRM software states
 * - Interrupt sources and handlers
 * - Vblank support
 * - Debug FS entries, if enabled
 */
static int dm_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	/* Create DAL display manager */
	amdgpu_dm_init(adev);
	amdgpu_dm_hpd_init(adev);

	return 0;
}

/**
 * dm_hw_fini() - Teardown DC device
 * @handle: The base driver device containing the amdpgu_dm device.
 *
 * Teardown components within &struct amdgpu_display_manager that require
 * cleanup. This involves cleaning up the DRM device, DC, and any modules that
 * were loaded. Also flush IRQ workqueues and disable them.
 */
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

	WARN_ON(adev->dm.cached_state);
	adev->dm.cached_state = drm_atomic_helper_suspend(adev->ddev);

	s3_handle_mst(adev->ddev, true);

	amdgpu_dm_irq_suspend(adev);


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

static void emulated_link_detect(struct dc_link *link)
{
	struct dc_sink_init_data sink_init_data = { 0 };
	struct display_sink_capability sink_caps = { 0 };
	enum dc_edid_status edid_status;
	struct dc_context *dc_ctx = link->ctx;
	struct dc_sink *sink = NULL;
	struct dc_sink *prev_sink = NULL;

	link->type = dc_connection_none;
	prev_sink = link->local_sink;

	if (prev_sink != NULL)
		dc_sink_retain(prev_sink);

	switch (link->connector_signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A: {
		sink_caps.transaction_type = DDC_TRANSACTION_TYPE_I2C;
		sink_caps.signal = SIGNAL_TYPE_HDMI_TYPE_A;
		break;
	}

	case SIGNAL_TYPE_DVI_SINGLE_LINK: {
		sink_caps.transaction_type = DDC_TRANSACTION_TYPE_I2C;
		sink_caps.signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;
	}

	case SIGNAL_TYPE_DVI_DUAL_LINK: {
		sink_caps.transaction_type = DDC_TRANSACTION_TYPE_I2C;
		sink_caps.signal = SIGNAL_TYPE_DVI_DUAL_LINK;
		break;
	}

	case SIGNAL_TYPE_LVDS: {
		sink_caps.transaction_type = DDC_TRANSACTION_TYPE_I2C;
		sink_caps.signal = SIGNAL_TYPE_LVDS;
		break;
	}

	case SIGNAL_TYPE_EDP: {
		sink_caps.transaction_type =
			DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
		sink_caps.signal = SIGNAL_TYPE_EDP;
		break;
	}

	case SIGNAL_TYPE_DISPLAY_PORT: {
		sink_caps.transaction_type =
			DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
		sink_caps.signal = SIGNAL_TYPE_VIRTUAL;
		break;
	}

	default:
		DC_ERROR("Invalid connector type! signal:%d\n",
			link->connector_signal);
		return;
	}

	sink_init_data.link = link;
	sink_init_data.sink_signal = sink_caps.signal;

	sink = dc_sink_create(&sink_init_data);
	if (!sink) {
		DC_ERROR("Failed to create sink!\n");
		return;
	}

	/* dc_sink_create returns a new reference */
	link->local_sink = sink;

	edid_status = dm_helpers_read_local_edid(
			link->ctx,
			link,
			sink);

	if (edid_status != EDID_OK)
		DC_ERROR("Failed to read EDID");

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
	struct dm_atomic_state *dm_state = to_dm_atomic_state(dm->atomic_obj.state);
	enum dc_connection_type new_connection_type = dc_connection_none;
	int i;

	/* Recreate dc_state - DC invalidates it when setting power state to S3. */
	dc_release_state(dm_state->context);
	dm_state->context = dc_create_state(dm->dc);
	/* TODO: Remove dc_state->dccg, use dc->dccg directly. */
	dc_resource_state_construct(dm->dc, dm_state->context);

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
		if (!dc_link_detect_sink(aconnector->dc_link, &new_connection_type))
			DRM_ERROR("KMS: Failed to detect connector\n");

		if (aconnector->base.force && new_connection_type == dc_connection_none)
			emulated_link_detect(aconnector->dc_link);
		else
			dc_link_detect(aconnector->dc_link, DETECT_REASON_HPD);

		if (aconnector->fake_enable && aconnector->dc_link->local_sink)
			aconnector->fake_enable = false;

		if (aconnector->dc_sink)
			dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
		amdgpu_dm_update_connector_after_detect(aconnector);
		mutex_unlock(&aconnector->hpd_lock);
	}

	/* Force mode set in atomic commit */
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

	drm_atomic_helper_resume(ddev, dm->cached_state);

	dm->cached_state = NULL;

	amdgpu_dm_irq_resume_late(adev);

	return 0;
}

/**
 * DOC: DM Lifecycle
 *
 * DM (and consequently DC) is registered in the amdgpu base driver as a IP
 * block. When CONFIG_DRM_AMD_DC is enabled, the DM device IP block is added to
 * the base driver's device list to be initialized and torn down accordingly.
 *
 * The functions to do so are provided as hooks in &struct amd_ip_funcs.
 */

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


/**
 * DOC: atomic
 *
 * *WIP*
 */

static const struct drm_mode_config_funcs amdgpu_dm_mode_funcs = {
	.fb_create = amdgpu_display_user_framebuffer_create,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.atomic_check = amdgpu_dm_atomic_check,
	.atomic_commit = amdgpu_dm_atomic_commit,
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
		mutex_lock(&dev->mode_config.mutex);

		if (sink) {
			if (aconnector->dc_sink) {
				amdgpu_dm_update_freesync_caps(connector, NULL);
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
					aconnector->edid);
		} else {
			amdgpu_dm_update_freesync_caps(connector, NULL);
			if (!aconnector->dc_sink) {
				aconnector->dc_sink = aconnector->dc_em_sink;
				dc_sink_retain(aconnector->dc_sink);
			}
		}

		mutex_unlock(&dev->mode_config.mutex);

		if (sink)
			dc_sink_release(sink);
		return;
	}

	/*
	 * TODO: temporary guard to look for proper fix
	 * if this sink is MST sink, we should not do anything
	 */
	if (sink && sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		dc_sink_release(sink);
		return;
	}

	if (aconnector->dc_sink == sink) {
		/*
		 * We got a DP short pulse (Link Loss, DP CTS, etc...).
		 * Do nothing!!
		 */
		DRM_DEBUG_DRIVER("DCHPD: connector_id=%d: dc_sink didn't change.\n",
				aconnector->connector_id);
		if (sink)
			dc_sink_release(sink);
		return;
	}

	DRM_DEBUG_DRIVER("DCHPD: connector_id=%d: Old sink=%p New sink=%p\n",
		aconnector->connector_id, aconnector->dc_sink, sink);

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
		if (aconnector->dc_sink)
			amdgpu_dm_update_freesync_caps(connector, NULL);

		aconnector->dc_sink = sink;
		dc_sink_retain(aconnector->dc_sink);
		if (sink->dc_edid.length == 0) {
			aconnector->edid = NULL;
			drm_dp_cec_unset_edid(&aconnector->dm_dp_aux.aux);
		} else {
			aconnector->edid =
				(struct edid *) sink->dc_edid.raw_edid;


			drm_connector_update_edid_property(connector,
					aconnector->edid);
			drm_dp_cec_set_edid(&aconnector->dm_dp_aux.aux,
					    aconnector->edid);
		}
		amdgpu_dm_update_freesync_caps(connector, aconnector->edid);

	} else {
		drm_dp_cec_unset_edid(&aconnector->dm_dp_aux.aux);
		amdgpu_dm_update_freesync_caps(connector, NULL);
		drm_connector_update_edid_property(connector, NULL);
		aconnector->num_modes = 0;
		dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
		aconnector->edid = NULL;
	}

	mutex_unlock(&dev->mode_config.mutex);

	if (sink)
		dc_sink_release(sink);
}

static void handle_hpd_irq(void *param)
{
	struct amdgpu_dm_connector *aconnector = (struct amdgpu_dm_connector *)param;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	enum dc_connection_type new_connection_type = dc_connection_none;

	/*
	 * In case of failure or MST no need to update connector status or notify the OS
	 * since (for MST case) MST does this in its own context.
	 */
	mutex_lock(&aconnector->hpd_lock);

	if (aconnector->fake_enable)
		aconnector->fake_enable = false;

	if (!dc_link_detect_sink(aconnector->dc_link, &new_connection_type))
		DRM_ERROR("KMS: Failed to detect connector\n");

	if (aconnector->base.force && new_connection_type == dc_connection_none) {
		emulated_link_detect(aconnector->dc_link);


		drm_modeset_lock_all(dev);
		dm_restore_drm_connector_state(dev, connector);
		drm_modeset_unlock_all(dev);

		if (aconnector->base.force == DRM_FORCE_UNSPECIFIED)
			drm_kms_helper_hotplug_event(dev);

	} else if (dc_link_detect(aconnector->dc_link, DETECT_REASON_HPD)) {
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

			/* check if there is new irq to be handled */
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
	enum dc_connection_type new_connection_type = dc_connection_none;

	/*
	 * TODO:Temporary add mutex to protect hpd interrupt not have a gpio
	 * conflict, after implement i2c helper, this mutex should be
	 * retired.
	 */
	if (dc_link->type != dc_connection_mst_branch)
		mutex_lock(&aconnector->hpd_lock);

	if (dc_link_handle_hpd_rx_irq(dc_link, NULL, NULL) &&
			!is_mst_root_connector) {
		/* Downstream Port status changed. */
		if (!dc_link_detect_sink(dc_link, &new_connection_type))
			DRM_ERROR("KMS: Failed to detect connector\n");

		if (aconnector->base.force && new_connection_type == dc_connection_none) {
			emulated_link_detect(dc_link);

			if (aconnector->fake_enable)
				aconnector->fake_enable = false;

			amdgpu_dm_update_connector_after_detect(aconnector);


			drm_modeset_lock_all(dev);
			dm_restore_drm_connector_state(dev, connector);
			drm_modeset_unlock_all(dev);

			drm_kms_helper_hotplug_event(dev);
		} else if (dc_link_detect(dc_link, DETECT_REASON_HPDRX)) {

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

	if (dc_link->type != dc_connection_mst_branch) {
		drm_dp_cec_irq(&aconnector->dm_dp_aux.aux);
		mutex_unlock(&aconnector->hpd_lock);
	}
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
	unsigned client_id = AMDGPU_IRQ_CLIENTID_LEGACY;

	if (adev->asic_type == CHIP_VEGA10 ||
	    adev->asic_type == CHIP_VEGA12 ||
	    adev->asic_type == CHIP_VEGA20 ||
	    adev->asic_type == CHIP_RAVEN)
		client_id = SOC15_IH_CLIENTID_DCE;

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	/*
	 * Actions of amdgpu_irq_add_id():
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

	/* Use VUPDATE interrupt */
	for (i = VISLANDS30_IV_SRCID_D1_V_UPDATE_INT; i <= VISLANDS30_IV_SRCID_D6_V_UPDATE_INT; i += 2) {
		r = amdgpu_irq_add_id(adev, client_id, i, &adev->vupdate_irq);
		if (r) {
			DRM_ERROR("Failed to add vupdate irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.vupdate_params[int_params.irq_source - DC_IRQ_SOURCE_VUPDATE1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_vupdate_high_irq, c_irq_params);
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

	/*
	 * Actions of amdgpu_irq_add_id():
	 * 1. Register a set() function with base driver.
	 *    Base driver will call set() function to enable/disable an
	 *    interrupt in DC hardware.
	 * 2. Register amdgpu_dm_irq_handler().
	 *    Base driver will call amdgpu_dm_irq_handler() for ALL interrupts
	 *    coming from DC hardware.
	 *    amdgpu_dm_irq_handler() will re-direct the interrupt to DC
	 *    for acknowledging and handling.
	 */

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

	/* Use VUPDATE_NO_LOCK interrupt on DCN, which seems to correspond to
	 * the regular VUPDATE interrupt on DCE. We want DC_IRQ_SOURCE_VUPDATEx
	 * to trigger at end of each vblank, regardless of state of the lock,
	 * matching DCE behaviour.
	 */
	for (i = DCN_1_0__SRCID__OTG0_IHC_V_UPDATE_NO_LOCK_INTERRUPT;
	     i <= DCN_1_0__SRCID__OTG0_IHC_V_UPDATE_NO_LOCK_INTERRUPT + adev->mode_info.num_crtc - 1;
	     i++) {
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, i, &adev->vupdate_irq);

		if (r) {
			DRM_ERROR("Failed to add vupdate irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.vupdate_params[int_params.irq_source - DC_IRQ_SOURCE_VUPDATE1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_vupdate_high_irq, c_irq_params);
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

/*
 * Acquires the lock for the atomic state object and returns
 * the new atomic state.
 *
 * This should only be called during atomic check.
 */
static int dm_atomic_get_state(struct drm_atomic_state *state,
			       struct dm_atomic_state **dm_state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct drm_private_state *priv_state;

	if (*dm_state)
		return 0;

	priv_state = drm_atomic_get_private_obj_state(state, &dm->atomic_obj);
	if (IS_ERR(priv_state))
		return PTR_ERR(priv_state);

	*dm_state = to_dm_atomic_state(priv_state);

	return 0;
}

struct dm_atomic_state *
dm_atomic_get_new_state(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct drm_private_obj *obj;
	struct drm_private_state *new_obj_state;
	int i;

	for_each_new_private_obj_in_state(state, obj, new_obj_state, i) {
		if (obj->funcs == dm->atomic_obj.funcs)
			return to_dm_atomic_state(new_obj_state);
	}

	return NULL;
}

struct dm_atomic_state *
dm_atomic_get_old_state(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct drm_private_obj *obj;
	struct drm_private_state *old_obj_state;
	int i;

	for_each_old_private_obj_in_state(state, obj, old_obj_state, i) {
		if (obj->funcs == dm->atomic_obj.funcs)
			return to_dm_atomic_state(old_obj_state);
	}

	return NULL;
}

static struct drm_private_state *
dm_atomic_duplicate_state(struct drm_private_obj *obj)
{
	struct dm_atomic_state *old_state, *new_state;

	new_state = kzalloc(sizeof(*new_state), GFP_KERNEL);
	if (!new_state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &new_state->base);

	old_state = to_dm_atomic_state(obj->state);

	if (old_state && old_state->context)
		new_state->context = dc_copy_state(old_state->context);

	if (!new_state->context) {
		kfree(new_state);
		return NULL;
	}

	return &new_state->base;
}

static void dm_atomic_destroy_state(struct drm_private_obj *obj,
				    struct drm_private_state *state)
{
	struct dm_atomic_state *dm_state = to_dm_atomic_state(state);

	if (dm_state && dm_state->context)
		dc_release_state(dm_state->context);

	kfree(dm_state);
}

static struct drm_private_state_funcs dm_atomic_state_funcs = {
	.atomic_duplicate_state = dm_atomic_duplicate_state,
	.atomic_destroy_state = dm_atomic_destroy_state,
};

static int amdgpu_dm_mode_config_init(struct amdgpu_device *adev)
{
	struct dm_atomic_state *state;
	int r;

	adev->mode_info.mode_config_initialized = true;

	adev->ddev->mode_config.funcs = (void *)&amdgpu_dm_mode_funcs;
	adev->ddev->mode_config.helper_private = &amdgpu_dm_mode_config_helperfuncs;

	adev->ddev->mode_config.max_width = 16384;
	adev->ddev->mode_config.max_height = 16384;

	adev->ddev->mode_config.preferred_depth = 24;
	adev->ddev->mode_config.prefer_shadow = 1;
	/* indicates support for immediate flip */
	adev->ddev->mode_config.async_page_flip = true;

	adev->ddev->mode_config.fb_base = adev->gmc.aper_base;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->context = dc_create_state(adev->dm.dc);
	if (!state->context) {
		kfree(state);
		return -ENOMEM;
	}

	dc_resource_state_copy_construct_current(adev->dm.dc, state->context);

	drm_atomic_private_obj_init(adev->ddev,
				    &adev->dm.atomic_obj,
				    &state->base,
				    &dm_atomic_state_funcs);

	r = amdgpu_display_modeset_create_props(adev);
	if (r)
		return r;

	return 0;
}

#define AMDGPU_DM_DEFAULT_MIN_BACKLIGHT 12
#define AMDGPU_DM_DEFAULT_MAX_BACKLIGHT 255

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

static void amdgpu_dm_update_backlight_caps(struct amdgpu_display_manager *dm)
{
#if defined(CONFIG_ACPI)
	struct amdgpu_dm_backlight_caps caps;

	if (dm->backlight_caps.caps_valid)
		return;

	amdgpu_acpi_get_backlight_caps(dm->adev, &caps);
	if (caps.caps_valid) {
		dm->backlight_caps.min_input_signal = caps.min_input_signal;
		dm->backlight_caps.max_input_signal = caps.max_input_signal;
		dm->backlight_caps.caps_valid = true;
	} else {
		dm->backlight_caps.min_input_signal =
				AMDGPU_DM_DEFAULT_MIN_BACKLIGHT;
		dm->backlight_caps.max_input_signal =
				AMDGPU_DM_DEFAULT_MAX_BACKLIGHT;
	}
#else
	dm->backlight_caps.min_input_signal = AMDGPU_DM_DEFAULT_MIN_BACKLIGHT;
	dm->backlight_caps.max_input_signal = AMDGPU_DM_DEFAULT_MAX_BACKLIGHT;
#endif
}

static int amdgpu_dm_backlight_update_status(struct backlight_device *bd)
{
	struct amdgpu_display_manager *dm = bl_get_data(bd);
	struct amdgpu_dm_backlight_caps caps;
	uint32_t brightness = bd->props.brightness;

	amdgpu_dm_update_backlight_caps(dm);
	caps = dm->backlight_caps;
	/*
	 * The brightness input is in the range 0-255
	 * It needs to be rescaled to be between the
	 * requested min and max input signal
	 *
	 * It also needs to be scaled up by 0x101 to
	 * match the DC interface which has a range of
	 * 0 to 0xffff
	 */
	brightness =
		brightness
		* 0x101
		* (caps.max_input_signal - caps.min_input_signal)
		/ AMDGPU_MAX_BL_LEVEL
		+ caps.min_input_signal * 0x101;

	if (dc_link_set_backlight_level(dm->backlight_link,
			brightness, 0))
		return 0;
	else
		return 1;
}

static int amdgpu_dm_backlight_get_brightness(struct backlight_device *bd)
{
	struct amdgpu_display_manager *dm = bl_get_data(bd);
	int ret = dc_link_get_backlight_level(dm->backlight_link);

	if (ret == DC_ERROR_UNEXPECTED)
		return bd->props.brightness;
	return ret;
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

	amdgpu_dm_update_backlight_caps(dm);

	props.max_brightness = AMDGPU_MAX_BL_LEVEL;
	props.brightness = AMDGPU_MAX_BL_LEVEL;
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
			    struct amdgpu_mode_info *mode_info, int plane_id,
			    enum drm_plane_type plane_type,
			    const struct dc_plane_cap *plane_cap)
{
	struct drm_plane *plane;
	unsigned long possible_crtcs;
	int ret = 0;

	plane = kzalloc(sizeof(struct drm_plane), GFP_KERNEL);
	if (!plane) {
		DRM_ERROR("KMS: Failed to allocate plane\n");
		return -ENOMEM;
	}
	plane->type = plane_type;

	/*
	 * HACK: IGT tests expect that the primary plane for a CRTC
	 * can only have one possible CRTC. Only expose support for
	 * any CRTC if they're not going to be used as a primary plane
	 * for a CRTC - like overlay or underlay planes.
	 */
	possible_crtcs = 1 << plane_id;
	if (plane_id >= dm->dc->caps.max_streams)
		possible_crtcs = 0xff;

	ret = amdgpu_dm_plane_init(dm, plane, possible_crtcs, plane_cap);

	if (ret) {
		DRM_ERROR("KMS: Failed to initialize plane\n");
		kfree(plane);
		return ret;
	}

	if (mode_info)
		mode_info->planes[plane_id] = plane;

	return ret;
}


static void register_backlight_device(struct amdgpu_display_manager *dm,
				      struct dc_link *link)
{
#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

	if ((link->connector_signal & (SIGNAL_TYPE_EDP | SIGNAL_TYPE_LVDS)) &&
	    link->type != dc_connection_none) {
		/*
		 * Event if registration failed, we should continue with
		 * DM initialization because not having a backlight control
		 * is better then a black screen.
		 */
		amdgpu_dm_register_backlight_device(dm);

		if (dm->backlight_dev)
			dm->backlight_link = link;
	}
#endif
}


/*
 * In this architecture, the association
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
	int32_t primary_planes;
	enum dc_connection_type new_connection_type = dc_connection_none;
	const struct dc_plane_cap *plane;

	link_cnt = dm->dc->caps.max_links;
	if (amdgpu_dm_mode_config_init(dm->adev)) {
		DRM_ERROR("DM: Failed to initialize mode config\n");
		return -EINVAL;
	}

	/* There is one primary plane per CRTC */
	primary_planes = dm->dc->caps.max_streams;
	ASSERT(primary_planes <= AMDGPU_MAX_PLANES);

	/*
	 * Initialize primary planes, implicit planes for legacy IOCTLS.
	 * Order is reversed to match iteration order in atomic check.
	 */
	for (i = (primary_planes - 1); i >= 0; i--) {
		plane = &dm->dc->caps.planes[i];

		if (initialize_plane(dm, mode_info, i,
				     DRM_PLANE_TYPE_PRIMARY, plane)) {
			DRM_ERROR("KMS: Failed to initialize primary plane\n");
			goto fail;
		}
	}

	/*
	 * Initialize overlay planes, index starting after primary planes.
	 * These planes have a higher DRM index than the primary planes since
	 * they should be considered as having a higher z-order.
	 * Order is reversed to match iteration order in atomic check.
	 *
	 * Only support DCN for now, and only expose one so we don't encourage
	 * userspace to use up all the pipes.
	 */
	for (i = 0; i < dm->dc->caps.max_planes; ++i) {
		struct dc_plane_cap *plane = &dm->dc->caps.planes[i];

		if (plane->type != DC_PLANE_TYPE_DCN_UNIVERSAL)
			continue;

		if (!plane->blends_with_above || !plane->blends_with_below)
			continue;

		if (!plane->pixel_format_support.argb8888)
			continue;

		if (initialize_plane(dm, NULL, primary_planes + i,
				     DRM_PLANE_TYPE_OVERLAY, plane)) {
			DRM_ERROR("KMS: Failed to initialize overlay plane\n");
			goto fail;
		}

		/* Only create one overlay plane. */
		break;
	}

	for (i = 0; i < dm->dc->caps.max_streams; i++)
		if (amdgpu_dm_crtc_init(dm, mode_info->planes[i], i)) {
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

		if (!dc_link_detect_sink(link, &new_connection_type))
			DRM_ERROR("KMS: Failed to detect connector\n");

		if (aconnector->base.force && new_connection_type == dc_connection_none) {
			emulated_link_detect(link);
			amdgpu_dm_update_connector_after_detect(aconnector);

		} else if (dc_link_detect(link, DETECT_REASON_BOOT)) {
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
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	case CHIP_RAVEN:
		if (dcn10_register_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail;
		}
		break;
#endif
	default:
		DRM_ERROR("Unsupported ASIC type: 0x%X\n", adev->asic_type);
		goto fail;
	}

	if (adev->asic_type != CHIP_CARRIZO && adev->asic_type != CHIP_STONEY)
		dm->dc->debug.disable_stutter = amdgpu_pp_feature_mask & PP_STUTTER_MODE ? false : true;

	return 0;
fail:
	kfree(aencoder);
	kfree(aconnector);

	return -EINVAL;
}

static void amdgpu_dm_destroy_drm_device(struct amdgpu_display_manager *dm)
{
	drm_mode_config_cleanup(dm->ddev);
	drm_atomic_private_obj_fini(&dm->atomic_obj);
	return;
}

/******************************************************************************
 * amdgpu_display_funcs functions
 *****************************************************************************/

/*
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

static const struct amdgpu_display_funcs dm_display_funcs = {
	.bandwidth_update = dm_bandwidth_update, /* called unconditionally */
	.vblank_get_counter = dm_vblank_get_counter,/* called unconditionally */
	.backlight_set_level = NULL, /* never called for DC */
	.backlight_get_level = NULL, /* never called for DC */
	.hpd_sense = NULL,/* called unconditionally */
	.hpd_set_polarity = NULL, /* called unconditionally */
	.hpd_get_gpio_reg = NULL, /* VBIOS parsing. DAL does it. */
	.page_flip_get_scanoutpos =
		dm_crtc_get_scanoutpos,/* called unconditionally */
	.add_encoder = NULL, /* VBIOS parsing. DAL does it. */
	.add_connector = NULL, /* VBIOS parsing. DAL does it. */
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
		break;
	case CHIP_KAVERI:
		adev->mode_info.num_crtc = 4;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 7;
		break;
	case CHIP_KABINI:
	case CHIP_MULLINS:
		adev->mode_info.num_crtc = 2;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		break;
	case CHIP_FIJI:
	case CHIP_TONGA:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 7;
		break;
	case CHIP_CARRIZO:
		adev->mode_info.num_crtc = 3;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		break;
	case CHIP_STONEY:
		adev->mode_info.num_crtc = 2;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		adev->mode_info.num_crtc = 5;
		adev->mode_info.num_hpd = 5;
		adev->mode_info.num_dig = 5;
		break;
	case CHIP_POLARIS10:
	case CHIP_VEGAM:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		break;
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		break;
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	case CHIP_RAVEN:
		adev->mode_info.num_crtc = 4;
		adev->mode_info.num_hpd = 4;
		adev->mode_info.num_dig = 4;
		break;
#endif
	default:
		DRM_ERROR("Unsupported ASIC type: 0x%X\n", adev->asic_type);
		return -EINVAL;
	}

	amdgpu_dm_set_irq_funcs(adev);

	if (adev->mode_info.funcs == NULL)
		adev->mode_info.funcs = &dm_display_funcs;

	/*
	 * Note: Do NOT change adev->audio_endpt_rreg and
	 * adev->audio_endpt_wreg because they are initialised in
	 * amdgpu_device_init()
	 */
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


static int fill_dc_scaling_info(const struct drm_plane_state *state,
				struct dc_scaling_info *scaling_info)
{
	int scale_w, scale_h;

	memset(scaling_info, 0, sizeof(*scaling_info));

	/* Source is fixed 16.16 but we ignore mantissa for now... */
	scaling_info->src_rect.x = state->src_x >> 16;
	scaling_info->src_rect.y = state->src_y >> 16;

	scaling_info->src_rect.width = state->src_w >> 16;
	if (scaling_info->src_rect.width == 0)
		return -EINVAL;

	scaling_info->src_rect.height = state->src_h >> 16;
	if (scaling_info->src_rect.height == 0)
		return -EINVAL;

	scaling_info->dst_rect.x = state->crtc_x;
	scaling_info->dst_rect.y = state->crtc_y;

	if (state->crtc_w == 0)
		return -EINVAL;

	scaling_info->dst_rect.width = state->crtc_w;

	if (state->crtc_h == 0)
		return -EINVAL;

	scaling_info->dst_rect.height = state->crtc_h;

	/* DRM doesn't specify clipping on destination output. */
	scaling_info->clip_rect = scaling_info->dst_rect;

	/* TODO: Validate scaling per-format with DC plane caps */
	scale_w = scaling_info->dst_rect.width * 1000 /
		  scaling_info->src_rect.width;

	if (scale_w < 250 || scale_w > 16000)
		return -EINVAL;

	scale_h = scaling_info->dst_rect.height * 1000 /
		  scaling_info->src_rect.height;

	if (scale_h < 250 || scale_h > 16000)
		return -EINVAL;

	/*
	 * The "scaling_quality" can be ignored for now, quality = 0 has DC
	 * assume reasonable defaults based on the format.
	 */

	return 0;
}

static int get_fb_info(const struct amdgpu_framebuffer *amdgpu_fb,
		       uint64_t *tiling_flags)
{
	struct amdgpu_bo *rbo = gem_to_amdgpu_bo(amdgpu_fb->base.obj[0]);
	int r = amdgpu_bo_reserve(rbo, false);

	if (unlikely(r)) {
		/* Don't show error message when returning -ERESTARTSYS */
		if (r != -ERESTARTSYS)
			DRM_ERROR("Unable to reserve buffer: %d\n", r);
		return r;
	}

	if (tiling_flags)
		amdgpu_bo_get_tiling_flags(rbo, tiling_flags);

	amdgpu_bo_unreserve(rbo);

	return r;
}

static inline uint64_t get_dcc_address(uint64_t address, uint64_t tiling_flags)
{
	uint32_t offset = AMDGPU_TILING_GET(tiling_flags, DCC_OFFSET_256B);

	return offset ? (address + offset * 256) : 0;
}

static int
fill_plane_dcc_attributes(struct amdgpu_device *adev,
			  const struct amdgpu_framebuffer *afb,
			  const enum surface_pixel_format format,
			  const enum dc_rotation_angle rotation,
			  const union plane_size *plane_size,
			  const union dc_tiling_info *tiling_info,
			  const uint64_t info,
			  struct dc_plane_dcc_param *dcc,
			  struct dc_plane_address *address)
{
	struct dc *dc = adev->dm.dc;
	struct dc_dcc_surface_param input;
	struct dc_surface_dcc_cap output;
	uint32_t offset = AMDGPU_TILING_GET(info, DCC_OFFSET_256B);
	uint32_t i64b = AMDGPU_TILING_GET(info, DCC_INDEPENDENT_64B) != 0;
	uint64_t dcc_address;

	memset(&input, 0, sizeof(input));
	memset(&output, 0, sizeof(output));

	if (!offset)
		return 0;

	if (format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN)
		return 0;

	if (!dc->cap_funcs.get_dcc_compression_cap)
		return -EINVAL;

	input.format = format;
	input.surface_size.width = plane_size->grph.surface_size.width;
	input.surface_size.height = plane_size->grph.surface_size.height;
	input.swizzle_mode = tiling_info->gfx9.swizzle;

	if (rotation == ROTATION_ANGLE_0 || rotation == ROTATION_ANGLE_180)
		input.scan = SCAN_DIRECTION_HORIZONTAL;
	else if (rotation == ROTATION_ANGLE_90 || rotation == ROTATION_ANGLE_270)
		input.scan = SCAN_DIRECTION_VERTICAL;

	if (!dc->cap_funcs.get_dcc_compression_cap(dc, &input, &output))
		return -EINVAL;

	if (!output.capable)
		return -EINVAL;

	if (i64b == 0 && output.grph.rgb.independent_64b_blks != 0)
		return -EINVAL;

	dcc->enable = 1;
	dcc->grph.meta_pitch =
		AMDGPU_TILING_GET(info, DCC_PITCH_MAX) + 1;
	dcc->grph.independent_64b_blks = i64b;

	dcc_address = get_dcc_address(afb->address, info);
	address->grph.meta_addr.low_part = lower_32_bits(dcc_address);
	address->grph.meta_addr.high_part = upper_32_bits(dcc_address);

	return 0;
}

static int
fill_plane_buffer_attributes(struct amdgpu_device *adev,
			     const struct amdgpu_framebuffer *afb,
			     const enum surface_pixel_format format,
			     const enum dc_rotation_angle rotation,
			     const uint64_t tiling_flags,
			     union dc_tiling_info *tiling_info,
			     union plane_size *plane_size,
			     struct dc_plane_dcc_param *dcc,
			     struct dc_plane_address *address)
{
	const struct drm_framebuffer *fb = &afb->base;
	int ret;

	memset(tiling_info, 0, sizeof(*tiling_info));
	memset(plane_size, 0, sizeof(*plane_size));
	memset(dcc, 0, sizeof(*dcc));
	memset(address, 0, sizeof(*address));

	if (format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
		plane_size->grph.surface_size.x = 0;
		plane_size->grph.surface_size.y = 0;
		plane_size->grph.surface_size.width = fb->width;
		plane_size->grph.surface_size.height = fb->height;
		plane_size->grph.surface_pitch =
			fb->pitches[0] / fb->format->cpp[0];

		address->type = PLN_ADDR_TYPE_GRAPHICS;
		address->grph.addr.low_part = lower_32_bits(afb->address);
		address->grph.addr.high_part = upper_32_bits(afb->address);
	} else if (format < SURFACE_PIXEL_FORMAT_INVALID) {
		uint64_t chroma_addr = afb->address + fb->offsets[1];

		plane_size->video.luma_size.x = 0;
		plane_size->video.luma_size.y = 0;
		plane_size->video.luma_size.width = fb->width;
		plane_size->video.luma_size.height = fb->height;
		plane_size->video.luma_pitch =
			fb->pitches[0] / fb->format->cpp[0];

		plane_size->video.chroma_size.x = 0;
		plane_size->video.chroma_size.y = 0;
		/* TODO: set these based on surface format */
		plane_size->video.chroma_size.width = fb->width / 2;
		plane_size->video.chroma_size.height = fb->height / 2;

		plane_size->video.chroma_pitch =
			fb->pitches[1] / fb->format->cpp[1];

		address->type = PLN_ADDR_TYPE_VIDEO_PROGRESSIVE;
		address->video_progressive.luma_addr.low_part =
			lower_32_bits(afb->address);
		address->video_progressive.luma_addr.high_part =
			upper_32_bits(afb->address);
		address->video_progressive.chroma_addr.low_part =
			lower_32_bits(chroma_addr);
		address->video_progressive.chroma_addr.high_part =
			upper_32_bits(chroma_addr);
	}

	/* Fill GFX8 params */
	if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == DC_ARRAY_2D_TILED_THIN1) {
		unsigned int bankw, bankh, mtaspect, tile_split, num_banks;

		bankw = AMDGPU_TILING_GET(tiling_flags, BANK_WIDTH);
		bankh = AMDGPU_TILING_GET(tiling_flags, BANK_HEIGHT);
		mtaspect = AMDGPU_TILING_GET(tiling_flags, MACRO_TILE_ASPECT);
		tile_split = AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT);
		num_banks = AMDGPU_TILING_GET(tiling_flags, NUM_BANKS);

		/* XXX fix me for VI */
		tiling_info->gfx8.num_banks = num_banks;
		tiling_info->gfx8.array_mode =
				DC_ARRAY_2D_TILED_THIN1;
		tiling_info->gfx8.tile_split = tile_split;
		tiling_info->gfx8.bank_width = bankw;
		tiling_info->gfx8.bank_height = bankh;
		tiling_info->gfx8.tile_aspect = mtaspect;
		tiling_info->gfx8.tile_mode =
				DC_ADDR_SURF_MICRO_TILING_DISPLAY;
	} else if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE)
			== DC_ARRAY_1D_TILED_THIN1) {
		tiling_info->gfx8.array_mode = DC_ARRAY_1D_TILED_THIN1;
	}

	tiling_info->gfx8.pipe_config =
			AMDGPU_TILING_GET(tiling_flags, PIPE_CONFIG);

	if (adev->asic_type == CHIP_VEGA10 ||
	    adev->asic_type == CHIP_VEGA12 ||
	    adev->asic_type == CHIP_VEGA20 ||
	    adev->asic_type == CHIP_RAVEN) {
		/* Fill GFX9 params */
		tiling_info->gfx9.num_pipes =
			adev->gfx.config.gb_addr_config_fields.num_pipes;
		tiling_info->gfx9.num_banks =
			adev->gfx.config.gb_addr_config_fields.num_banks;
		tiling_info->gfx9.pipe_interleave =
			adev->gfx.config.gb_addr_config_fields.pipe_interleave_size;
		tiling_info->gfx9.num_shader_engines =
			adev->gfx.config.gb_addr_config_fields.num_se;
		tiling_info->gfx9.max_compressed_frags =
			adev->gfx.config.gb_addr_config_fields.max_compress_frags;
		tiling_info->gfx9.num_rb_per_se =
			adev->gfx.config.gb_addr_config_fields.num_rb_per_se;
		tiling_info->gfx9.swizzle =
			AMDGPU_TILING_GET(tiling_flags, SWIZZLE_MODE);
		tiling_info->gfx9.shaderEnable = 1;

		ret = fill_plane_dcc_attributes(adev, afb, format, rotation,
						plane_size, tiling_info,
						tiling_flags, dcc, address);
		if (ret)
			return ret;
	}

	return 0;
}

static void
fill_blending_from_plane_state(const struct drm_plane_state *plane_state,
			       bool *per_pixel_alpha, bool *global_alpha,
			       int *global_alpha_value)
{
	*per_pixel_alpha = false;
	*global_alpha = false;
	*global_alpha_value = 0xff;

	if (plane_state->plane->type != DRM_PLANE_TYPE_OVERLAY)
		return;

	if (plane_state->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI) {
		static const uint32_t alpha_formats[] = {
			DRM_FORMAT_ARGB8888,
			DRM_FORMAT_RGBA8888,
			DRM_FORMAT_ABGR8888,
		};
		uint32_t format = plane_state->fb->format->format;
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(alpha_formats); ++i) {
			if (format == alpha_formats[i]) {
				*per_pixel_alpha = true;
				break;
			}
		}
	}

	if (plane_state->alpha < 0xffff) {
		*global_alpha = true;
		*global_alpha_value = plane_state->alpha >> 8;
	}
}

static int
fill_plane_color_attributes(const struct drm_plane_state *plane_state,
			    const enum surface_pixel_format format,
			    enum dc_color_space *color_space)
{
	bool full_range;

	*color_space = COLOR_SPACE_SRGB;

	/* DRM color properties only affect non-RGB formats. */
	if (format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN)
		return 0;

	full_range = (plane_state->color_range == DRM_COLOR_YCBCR_FULL_RANGE);

	switch (plane_state->color_encoding) {
	case DRM_COLOR_YCBCR_BT601:
		if (full_range)
			*color_space = COLOR_SPACE_YCBCR601;
		else
			*color_space = COLOR_SPACE_YCBCR601_LIMITED;
		break;

	case DRM_COLOR_YCBCR_BT709:
		if (full_range)
			*color_space = COLOR_SPACE_YCBCR709;
		else
			*color_space = COLOR_SPACE_YCBCR709_LIMITED;
		break;

	case DRM_COLOR_YCBCR_BT2020:
		if (full_range)
			*color_space = COLOR_SPACE_2020_YCBCR;
		else
			return -EINVAL;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int
fill_dc_plane_info_and_addr(struct amdgpu_device *adev,
			    const struct drm_plane_state *plane_state,
			    const uint64_t tiling_flags,
			    struct dc_plane_info *plane_info,
			    struct dc_plane_address *address)
{
	const struct drm_framebuffer *fb = plane_state->fb;
	const struct amdgpu_framebuffer *afb =
		to_amdgpu_framebuffer(plane_state->fb);
	struct drm_format_name_buf format_name;
	int ret;

	memset(plane_info, 0, sizeof(*plane_info));

	switch (fb->format->format) {
	case DRM_FORMAT_C8:
		plane_info->format =
			SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS;
		break;
	case DRM_FORMAT_RGB565:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_RGB565;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB8888;
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010;
		break;
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010;
		break;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ABGR8888;
		break;
	case DRM_FORMAT_NV21:
		plane_info->format = SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr;
		break;
	case DRM_FORMAT_NV12:
		plane_info->format = SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb;
		break;
	default:
		DRM_ERROR(
			"Unsupported screen format %s\n",
			drm_get_format_name(fb->format->format, &format_name));
		return -EINVAL;
	}

	switch (plane_state->rotation & DRM_MODE_ROTATE_MASK) {
	case DRM_MODE_ROTATE_0:
		plane_info->rotation = ROTATION_ANGLE_0;
		break;
	case DRM_MODE_ROTATE_90:
		plane_info->rotation = ROTATION_ANGLE_90;
		break;
	case DRM_MODE_ROTATE_180:
		plane_info->rotation = ROTATION_ANGLE_180;
		break;
	case DRM_MODE_ROTATE_270:
		plane_info->rotation = ROTATION_ANGLE_270;
		break;
	default:
		plane_info->rotation = ROTATION_ANGLE_0;
		break;
	}

	plane_info->visible = true;
	plane_info->stereo_format = PLANE_STEREO_FORMAT_NONE;

	ret = fill_plane_color_attributes(plane_state, plane_info->format,
					  &plane_info->color_space);
	if (ret)
		return ret;

	ret = fill_plane_buffer_attributes(adev, afb, plane_info->format,
					   plane_info->rotation, tiling_flags,
					   &plane_info->tiling_info,
					   &plane_info->plane_size,
					   &plane_info->dcc, address);
	if (ret)
		return ret;

	fill_blending_from_plane_state(
		plane_state, &plane_info->per_pixel_alpha,
		&plane_info->global_alpha, &plane_info->global_alpha_value);

	return 0;
}

static int fill_dc_plane_attributes(struct amdgpu_device *adev,
				    struct dc_plane_state *dc_plane_state,
				    struct drm_plane_state *plane_state,
				    struct drm_crtc_state *crtc_state)
{
	const struct amdgpu_framebuffer *amdgpu_fb =
		to_amdgpu_framebuffer(plane_state->fb);
	struct dc_scaling_info scaling_info;
	struct dc_plane_info plane_info;
	uint64_t tiling_flags;
	int ret;

	ret = fill_dc_scaling_info(plane_state, &scaling_info);
	if (ret)
		return ret;

	dc_plane_state->src_rect = scaling_info.src_rect;
	dc_plane_state->dst_rect = scaling_info.dst_rect;
	dc_plane_state->clip_rect = scaling_info.clip_rect;
	dc_plane_state->scaling_quality = scaling_info.scaling_quality;

	ret = get_fb_info(amdgpu_fb, &tiling_flags);
	if (ret)
		return ret;

	ret = fill_dc_plane_info_and_addr(adev, plane_state, tiling_flags,
					  &plane_info,
					  &dc_plane_state->address);
	if (ret)
		return ret;

	dc_plane_state->format = plane_info.format;
	dc_plane_state->color_space = plane_info.color_space;
	dc_plane_state->format = plane_info.format;
	dc_plane_state->plane_size = plane_info.plane_size;
	dc_plane_state->rotation = plane_info.rotation;
	dc_plane_state->horizontal_mirror = plane_info.horizontal_mirror;
	dc_plane_state->stereo_format = plane_info.stereo_format;
	dc_plane_state->tiling_info = plane_info.tiling_info;
	dc_plane_state->visible = plane_info.visible;
	dc_plane_state->per_pixel_alpha = plane_info.per_pixel_alpha;
	dc_plane_state->global_alpha = plane_info.global_alpha;
	dc_plane_state->global_alpha_value = plane_info.global_alpha_value;
	dc_plane_state->dcc = plane_info.dcc;

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
convert_color_depth_from_display_info(const struct drm_connector *connector,
				      const struct drm_connector_state *state)
{
	uint32_t bpc = connector->display_info.bpc;

	if (!state)
		state = connector->state;

	if (state) {
		bpc = state->max_bpc;
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

static enum dc_aspect_ratio
get_aspect_ratio(const struct drm_display_mode *mode_in)
{
	/* 1-1 mapping, since both enums follow the HDMI spec. */
	return (enum dc_aspect_ratio) mode_in->picture_aspect_ratio;
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
		if (dc_crtc_timing->pix_clk_100hz > 270300) {
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
		normalized_clk = timing_out->pix_clk_100hz / 10;
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

static void fill_stream_properties_from_drm_display_mode(
	struct dc_stream_state *stream,
	const struct drm_display_mode *mode_in,
	const struct drm_connector *connector,
	const struct drm_connector_state *connector_state,
	const struct dc_stream_state *old_stream)
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
			&& stream->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR420;
	else if ((connector->display_info.color_formats & DRM_COLOR_FORMAT_YCRCB444)
			&& stream->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR444;
	else
		timing_out->pixel_encoding = PIXEL_ENCODING_RGB;

	timing_out->timing_3d_format = TIMING_3D_FORMAT_NONE;
	timing_out->display_color_depth = convert_color_depth_from_display_info(
		connector, connector_state);
	timing_out->scan_type = SCANNING_TYPE_NODATA;
	timing_out->hdmi_vic = 0;

	if(old_stream) {
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
	timing_out->pix_clk_100hz = mode_in->crtc_clock * 10;
	timing_out->aspect_ratio = get_aspect_ratio(mode_in);

	stream->output_color_space = get_output_color_space(timing_out);

	stream->out_transfer_func->type = TF_TYPE_PREDEFINED;
	stream->out_transfer_func->tf = TRANSFER_FUNCTION_SRGB;
	if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A)
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

	strscpy(audio_info->display_name,
		edid_caps->display_name,
		AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS);

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

			refresh_rate = (stream_set[j]->timing.pix_clk_100hz*100)/
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
		/*
		 * TODO: add a function to read AMD VSDB bits and set
		 * crtc_sync_master.multi_sync_enabled flag
		 * For now it's set to false
		 */
		set_multisync_trigger_params(context->streams[i]);
	}
	set_master_stream(context->streams, context->stream_count);
}

static struct dc_stream_state *
create_stream_for_sink(struct amdgpu_dm_connector *aconnector,
		       const struct drm_display_mode *drm_mode,
		       const struct dm_connector_state *dm_state,
		       const struct dc_stream_state *old_stream)
{
	struct drm_display_mode *preferred_mode = NULL;
	struct drm_connector *drm_connector;
	const struct drm_connector_state *con_state =
		dm_state ? &dm_state->base : NULL;
	struct dc_stream_state *stream = NULL;
	struct drm_display_mode mode = *drm_mode;
	bool native_mode_found = false;
	bool scale = dm_state ? (dm_state->scaling != RMX_OFF) : false;
	int mode_refresh;
	int preferred_refresh = 0;

	struct dc_sink *sink = NULL;
	if (aconnector == NULL) {
		DRM_ERROR("aconnector is NULL!\n");
		return stream;
	}

	drm_connector = &aconnector->base;

	if (!aconnector->dc_sink) {
		sink = create_fake_sink(aconnector);
		if (!sink)
			return stream;
	} else {
		sink = aconnector->dc_sink;
		dc_sink_retain(sink);
	}

	stream = dc_create_stream_for_sink(sink);

	if (stream == NULL) {
		DRM_ERROR("Failed to create stream for sink!\n");
		goto finish;
	}

	stream->dm_stream_context = aconnector;

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

	mode_refresh = drm_mode_vrefresh(&mode);

	if (preferred_mode == NULL) {
		/*
		 * This may not be an error, the use case is when we have no
		 * usermode calls to reset and set mode upon hotplug. In this
		 * case, we call set mode ourselves to restore the previous mode
		 * and the modelist may not be filled in in time.
		 */
		DRM_DEBUG_DRIVER("No preferred mode found\n");
	} else {
		decide_crtc_timing_for_drm_display_mode(
				&mode, preferred_mode,
				dm_state ? (dm_state->scaling != RMX_OFF) : false);
		preferred_refresh = drm_mode_vrefresh(preferred_mode);
	}

	if (!dm_state)
		drm_mode_set_crtcinfo(&mode, 0);

	/*
	* If scaling is enabled and refresh rate didn't change
	* we copy the vic and polarities of the old timings
	*/
	if (!scale || mode_refresh != preferred_refresh)
		fill_stream_properties_from_drm_display_mode(stream,
			&mode, &aconnector->base, con_state, NULL);
	else
		fill_stream_properties_from_drm_display_mode(stream,
			&mode, &aconnector->base, con_state, old_stream);

	update_stream_scaling_settings(&mode, dm_state, stream);

	fill_audio_info(
		&stream->audio_info,
		drm_connector,
		sink);

	update_stream_signal(stream, sink);

finish:
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

	state->active_planes = cur->active_planes;
	state->interrupts_enabled = cur->interrupts_enabled;
	state->vrr_params = cur->vrr_params;
	state->vrr_infopacket = cur->vrr_infopacket;
	state->abm_level = cur->abm_level;
	state->vrr_supported = cur->vrr_supported;
	state->freesync_config = cur->freesync_config;
	state->crc_enabled = cur->crc_enabled;

	/* TODO Duplicate dc_stream after objects are stream object is flattened */

	return &state->base;
}

static inline int dm_set_vupdate_irq(struct drm_crtc *crtc, bool enable)
{
	enum dc_irq_source irq_source;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = crtc->dev->dev_private;
	int rc;

	irq_source = IRQ_TYPE_VUPDATE + acrtc->otg_inst;

	rc = dc_interrupt_set(adev->dm.dc, irq_source, enable) ? 0 : -EBUSY;

	DRM_DEBUG_DRIVER("crtc %d - vupdate irq %sabling: r=%d\n",
			 acrtc->crtc_id, enable ? "en" : "dis", rc);
	return rc;
}

static inline int dm_set_vblank(struct drm_crtc *crtc, bool enable)
{
	enum dc_irq_source irq_source;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = crtc->dev->dev_private;
	struct dm_crtc_state *acrtc_state = to_dm_crtc_state(crtc->state);
	int rc = 0;

	if (enable) {
		/* vblank irq on -> Only need vupdate irq in vrr mode */
		if (amdgpu_dm_vrr_active(acrtc_state))
			rc = dm_set_vupdate_irq(crtc, true);
	} else {
		/* vblank irq off -> vupdate irq off */
		rc = dm_set_vupdate_irq(crtc, false);
	}

	if (rc)
		return rc;

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
	.verify_crc_source = amdgpu_dm_crtc_verify_crc_source,
	.enable_vblank = dm_enable_vblank,
	.disable_vblank = dm_disable_vblank,
};

static enum drm_connector_status
amdgpu_dm_connector_detect(struct drm_connector *connector, bool force)
{
	bool connected;
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);

	/*
	 * Notes:
	 * 1. This interface is NOT called in context of HPD irq.
	 * 2. This interface *is called* in context of user-mode ioctl. Which
	 * makes it a bad place for *any* MST-related activity.
	 */

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
	} else if (property == adev->mode_info.abm_level_property) {
		dm_new_state->abm_level = val;
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
	} else if (property == adev->mode_info.abm_level_property) {
		*val = dm_state->abm_level;
		ret = 0;
	}

	return ret;
}

static void amdgpu_dm_connector_unregister(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector = to_amdgpu_dm_connector(connector);

	drm_dp_aux_unregister(&amdgpu_dm_connector->dm_dp_aux.aux);
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

	if (aconnector->dc_em_sink)
		dc_sink_release(aconnector->dc_em_sink);
	aconnector->dc_em_sink = NULL;
	if (aconnector->dc_sink)
		dc_sink_release(aconnector->dc_sink);
	aconnector->dc_sink = NULL;

	drm_dp_cec_unregister_connector(&aconnector->dm_dp_aux.aux);
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	if (aconnector->i2c) {
		i2c_del_adapter(&aconnector->i2c->base);
		kfree(aconnector->i2c);
	}

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
		state->base.max_requested_bpc = 8;

		if (connector->connector_type == DRM_MODE_CONNECTOR_eDP)
			state->abm_level = amdgpu_dm_abm_level;

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

	if (!new_state)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector, &new_state->base);

	new_state->freesync_capable = state->freesync_capable;
	new_state->abm_level = state->abm_level;
	new_state->scaling = state->scaling;
	new_state->underscan_enable = state->underscan_enable;
	new_state->underscan_hborder = state->underscan_hborder;
	new_state->underscan_vborder = state->underscan_vborder;

	return &new_state->base;
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
	.early_unregister = amdgpu_dm_connector_unregister
};

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

	if (aconnector->base.force == DRM_FORCE_ON) {
		aconnector->dc_sink = aconnector->dc_link->local_sink ?
		aconnector->dc_link->local_sink :
		aconnector->dc_em_sink;
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

	/*
	 * Only run this the first time mode_valid is called to initilialize
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

	stream = create_stream_for_sink(aconnector, mode, NULL, NULL);
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

static int fill_hdr_info_packet(const struct drm_connector_state *state,
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

static bool
is_hdr_metadata_different(const struct drm_connector_state *old_state,
			  const struct drm_connector_state *new_state)
{
	struct drm_property_blob *old_blob = old_state->hdr_output_metadata;
	struct drm_property_blob *new_blob = new_state->hdr_output_metadata;

	if (old_blob != new_blob) {
		if (old_blob && new_blob &&
		    old_blob->length == new_blob->length)
			return memcmp(old_blob->data, new_blob->data,
				      old_blob->length);

		return true;
	}

	return false;
}

static int
amdgpu_dm_connector_atomic_check(struct drm_connector *conn,
				 struct drm_connector_state *new_con_state)
{
	struct drm_atomic_state *state = new_con_state->state;
	struct drm_connector_state *old_con_state =
		drm_atomic_get_old_connector_state(state, conn);
	struct drm_crtc *crtc = new_con_state->crtc;
	struct drm_crtc_state *new_crtc_state;
	int ret;

	if (!crtc)
		return 0;

	if (is_hdr_metadata_different(old_con_state, new_con_state)) {
		struct dc_info_packet hdr_infopacket;

		ret = fill_hdr_info_packet(new_con_state, &hdr_infopacket);
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
		new_crtc_state->mode_changed =
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

static void dm_crtc_helper_disable(struct drm_crtc *crtc)
{
}

static bool does_crtc_have_active_cursor(struct drm_crtc_state *new_crtc_state)
{
	struct drm_device *dev = new_crtc_state->crtc->dev;
	struct drm_plane *plane;

	drm_for_each_plane_mask(plane, dev, new_crtc_state->plane_mask) {
		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			return true;
	}

	return false;
}

static int count_crtc_active_planes(struct drm_crtc_state *new_crtc_state)
{
	struct drm_atomic_state *state = new_crtc_state->state;
	struct drm_plane *plane;
	int num_active = 0;

	drm_for_each_plane_mask(plane, state->dev, new_crtc_state->plane_mask) {
		struct drm_plane_state *new_plane_state;

		/* Cursor planes are "fake". */
		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			continue;

		new_plane_state = drm_atomic_get_new_plane_state(state, plane);

		if (!new_plane_state) {
			/*
			 * The plane is enable on the CRTC and hasn't changed
			 * state. This means that it previously passed
			 * validation and is therefore enabled.
			 */
			num_active += 1;
			continue;
		}

		/* We need a framebuffer to be considered enabled. */
		num_active += (new_plane_state->fb != NULL);
	}

	return num_active;
}

/*
 * Sets whether interrupts should be enabled on a specific CRTC.
 * We require that the stream be enabled and that there exist active
 * DC planes on the stream.
 */
static void
dm_update_crtc_interrupt_state(struct drm_crtc *crtc,
			       struct drm_crtc_state *new_crtc_state)
{
	struct dm_crtc_state *dm_new_crtc_state =
		to_dm_crtc_state(new_crtc_state);

	dm_new_crtc_state->active_planes = 0;
	dm_new_crtc_state->interrupts_enabled = false;

	if (!dm_new_crtc_state->stream)
		return;

	dm_new_crtc_state->active_planes =
		count_crtc_active_planes(new_crtc_state);

	dm_new_crtc_state->interrupts_enabled =
		dm_new_crtc_state->active_planes > 0;
}

static int dm_crtc_helper_atomic_check(struct drm_crtc *crtc,
				       struct drm_crtc_state *state)
{
	struct amdgpu_device *adev = crtc->dev->dev_private;
	struct dc *dc = adev->dm.dc;
	struct dm_crtc_state *dm_crtc_state = to_dm_crtc_state(state);
	int ret = -EINVAL;

	/*
	 * Update interrupt state for the CRTC. This needs to happen whenever
	 * the CRTC has changed or whenever any of its planes have changed.
	 * Atomic check satisfies both of these requirements since the CRTC
	 * is added to the state by DRM during drm_atomic_helper_check_planes.
	 */
	dm_update_crtc_interrupt_state(crtc, state);

	if (unlikely(!dm_crtc_state->stream &&
		     modeset_required(state, NULL, dm_crtc_state->stream))) {
		WARN_ON(1);
		return ret;
	}

	/* In some use cases, like reset, no stream is attached */
	if (!dm_crtc_state->stream)
		return 0;

	/*
	 * We want at least one hardware plane enabled to use
	 * the stream with a cursor enabled.
	 */
	if (state->enable && state->active &&
	    does_crtc_have_active_cursor(state) &&
	    dm_crtc_state->active_planes == 0)
		return -EINVAL;

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

	if (amdgpu_state)
		__drm_atomic_helper_plane_reset(plane, &amdgpu_state->base);
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
	.destroy	= drm_primary_helper_destroy,
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
	struct dm_plane_state *dm_plane_state_new, *dm_plane_state_old;
	struct list_head list;
	struct ttm_validate_buffer tv;
	struct ww_acquire_ctx ticket;
	uint64_t tiling_flags;
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
	INIT_LIST_HEAD(&list);

	tv.bo = &rbo->tbo;
	tv.num_shared = 1;
	list_add(&tv.head, &list);

	r = ttm_eu_reserve_buffers(&ticket, &list, false, NULL, true);
	if (r) {
		dev_err(adev->dev, "fail to reserve bo (%d)\n", r);
		return r;
	}

	if (plane->type != DRM_PLANE_TYPE_CURSOR)
		domain = amdgpu_display_supported_domains(adev);
	else
		domain = AMDGPU_GEM_DOMAIN_VRAM;

	r = amdgpu_bo_pin(rbo, domain);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			DRM_ERROR("Failed to pin framebuffer with error %d\n", r);
		ttm_eu_backoff_reservation(&ticket, &list);
		return r;
	}

	r = amdgpu_ttm_alloc_gart(&rbo->tbo);
	if (unlikely(r != 0)) {
		amdgpu_bo_unpin(rbo);
		ttm_eu_backoff_reservation(&ticket, &list);
		DRM_ERROR("%p bind failed\n", rbo);
		return r;
	}

	amdgpu_bo_get_tiling_flags(rbo, &tiling_flags);

	ttm_eu_backoff_reservation(&ticket, &list);

	afb->address = amdgpu_bo_gpu_offset(rbo);

	amdgpu_bo_ref(rbo);

	if (dm_plane_state_new->dc_state &&
			dm_plane_state_old->dc_state != dm_plane_state_new->dc_state) {
		struct dc_plane_state *plane_state = dm_plane_state_new->dc_state;

		fill_plane_buffer_attributes(
			adev, afb, plane_state->format, plane_state->rotation,
			tiling_flags, &plane_state->tiling_info,
			&plane_state->plane_size, &plane_state->dcc,
			&plane_state->address);
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
	struct dm_plane_state *dm_plane_state;
	struct dc_scaling_info scaling_info;
	int ret;

	dm_plane_state = to_dm_plane_state(state);

	if (!dm_plane_state->dc_state)
		return 0;

	ret = fill_dc_scaling_info(state, &scaling_info);
	if (ret)
		return ret;

	if (dc_validate_plane(dc, dm_plane_state->dc_state) == DC_OK)
		return 0;

	return -EINVAL;
}

static int dm_plane_atomic_async_check(struct drm_plane *plane,
				       struct drm_plane_state *new_plane_state)
{
	struct drm_plane_state *old_plane_state =
		drm_atomic_get_old_plane_state(new_plane_state->state, plane);

	/* Only support async updates on cursor planes. */
	if (plane->type != DRM_PLANE_TYPE_CURSOR)
		return -EINVAL;

	/*
	 * DRM calls prepare_fb and cleanup_fb on new_plane_state for
	 * async commits so don't allow fb changes.
	 */
	if (old_plane_state->fb != new_plane_state->fb)
		return -EINVAL;

	return 0;
}

static void dm_plane_atomic_async_update(struct drm_plane *plane,
					 struct drm_plane_state *new_state)
{
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(new_state->state, plane);

	if (plane->state->fb != new_state->fb)
		drm_atomic_set_fb_for_plane(plane->state, new_state->fb);

	plane->state->src_x = new_state->src_x;
	plane->state->src_y = new_state->src_y;
	plane->state->src_w = new_state->src_w;
	plane->state->src_h = new_state->src_h;
	plane->state->crtc_x = new_state->crtc_x;
	plane->state->crtc_y = new_state->crtc_y;
	plane->state->crtc_w = new_state->crtc_w;
	plane->state->crtc_h = new_state->crtc_h;

	handle_cursor_update(plane, old_state);
}

static const struct drm_plane_helper_funcs dm_plane_helper_funcs = {
	.prepare_fb = dm_plane_helper_prepare_fb,
	.cleanup_fb = dm_plane_helper_cleanup_fb,
	.atomic_check = dm_plane_atomic_check,
	.atomic_async_check = dm_plane_atomic_async_check,
	.atomic_async_update = dm_plane_atomic_async_update
};

/*
 * TODO: these are currently initialized to rgb formats only.
 * For future use cases we should either initialize them dynamically based on
 * plane capabilities, or initialize this array to all formats, so internal drm
 * check will succeed, and let DC implement proper check
 */
static const uint32_t rgb_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB565,
};

static const uint32_t overlay_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB565
};

static const u32 cursor_formats[] = {
	DRM_FORMAT_ARGB8888
};

static int get_plane_formats(const struct drm_plane *plane,
			     const struct dc_plane_cap *plane_cap,
			     uint32_t *formats, int max_formats)
{
	int i, num_formats = 0;

	/*
	 * TODO: Query support for each group of formats directly from
	 * DC plane caps. This will require adding more formats to the
	 * caps list.
	 */

	switch (plane->type) {
	case DRM_PLANE_TYPE_PRIMARY:
		for (i = 0; i < ARRAY_SIZE(rgb_formats); ++i) {
			if (num_formats >= max_formats)
				break;

			formats[num_formats++] = rgb_formats[i];
		}

		if (plane_cap && plane_cap->pixel_format_support.nv12)
			formats[num_formats++] = DRM_FORMAT_NV12;
		break;

	case DRM_PLANE_TYPE_OVERLAY:
		for (i = 0; i < ARRAY_SIZE(overlay_formats); ++i) {
			if (num_formats >= max_formats)
				break;

			formats[num_formats++] = overlay_formats[i];
		}
		break;

	case DRM_PLANE_TYPE_CURSOR:
		for (i = 0; i < ARRAY_SIZE(cursor_formats); ++i) {
			if (num_formats >= max_formats)
				break;

			formats[num_formats++] = cursor_formats[i];
		}
		break;
	}

	return num_formats;
}

static int amdgpu_dm_plane_init(struct amdgpu_display_manager *dm,
				struct drm_plane *plane,
				unsigned long possible_crtcs,
				const struct dc_plane_cap *plane_cap)
{
	uint32_t formats[32];
	int num_formats;
	int res = -EPERM;

	num_formats = get_plane_formats(plane, plane_cap, formats,
					ARRAY_SIZE(formats));

	res = drm_universal_plane_init(dm->adev->ddev, plane, possible_crtcs,
				       &dm_plane_funcs, formats, num_formats,
				       NULL, plane->type, NULL);
	if (res)
		return res;

	if (plane->type == DRM_PLANE_TYPE_OVERLAY &&
	    plane_cap && plane_cap->per_pixel_alpha) {
		unsigned int blend_caps = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					  BIT(DRM_MODE_BLEND_PREMULTI);

		drm_plane_create_alpha_property(plane);
		drm_plane_create_blend_mode_property(plane, blend_caps);
	}

	if (plane->type == DRM_PLANE_TYPE_PRIMARY &&
	    plane_cap && plane_cap->pixel_format_support.nv12) {
		/* This only affects YUV formats. */
		drm_plane_create_color_properties(
			plane,
			BIT(DRM_COLOR_YCBCR_BT601) |
			BIT(DRM_COLOR_YCBCR_BT709),
			BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) |
			BIT(DRM_COLOR_YCBCR_FULL_RANGE),
			DRM_COLOR_YCBCR_BT709, DRM_COLOR_YCBCR_LIMITED_RANGE);
	}

	drm_plane_helper_add(plane, &dm_plane_helper_funcs);

	/* Create (reset) the plane state */
	if (plane->funcs->reset)
		plane->funcs->reset(plane);

	return 0;
}

static int amdgpu_dm_crtc_init(struct amdgpu_display_manager *dm,
			       struct drm_plane *plane,
			       uint32_t crtc_index)
{
	struct amdgpu_crtc *acrtc = NULL;
	struct drm_plane *cursor_plane;

	int res = -ENOMEM;

	cursor_plane = kzalloc(sizeof(*cursor_plane), GFP_KERNEL);
	if (!cursor_plane)
		goto fail;

	cursor_plane->type = DRM_PLANE_TYPE_CURSOR;
	res = amdgpu_dm_plane_init(dm, cursor_plane, 0, NULL);

	acrtc = kzalloc(sizeof(struct amdgpu_crtc), GFP_KERNEL);
	if (!acrtc)
		goto fail;

	res = drm_crtc_init_with_planes(
			dm->ddev,
			&acrtc->base,
			plane,
			cursor_plane,
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
	acrtc->otg_inst = -1;

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
	case SIGNAL_TYPE_LVDS:
		return DRM_MODE_CONNECTOR_LVDS;
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

static struct drm_encoder *amdgpu_dm_connector_to_encoder(struct drm_connector *connector)
{
	return drm_encoder_find(connector->dev, NULL, connector->encoder_ids[0]);
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
	strscpy(mode->name, name, DRM_DISPLAY_MODE_LEN);

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
	} else {
		amdgpu_dm_connector->num_modes = 0;
	}
}

static int amdgpu_dm_connector_get_modes(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);
	struct drm_encoder *encoder;
	struct edid *edid = amdgpu_dm_connector->edid;

	encoder = amdgpu_dm_connector_to_encoder(connector);

	if (!edid || !drm_edid_is_valid(edid)) {
		amdgpu_dm_connector->num_modes =
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

	/*
	 * Some of the properties below require access to state, like bpc.
	 * Allocate some default initial connector state with our reset helper.
	 */
	if (aconnector->base.funcs->reset)
		aconnector->base.funcs->reset(&aconnector->base);

	aconnector->connector_id = link_index;
	aconnector->dc_link = link;
	aconnector->base.interlace_allowed = false;
	aconnector->base.doublescan_allowed = false;
	aconnector->base.stereo_allowed = false;
	aconnector->base.dpms = DRM_MODE_DPMS_OFF;
	aconnector->hpd.hpd = AMDGPU_HPD_NONE; /* not used */
	mutex_init(&aconnector->hpd_lock);

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
		aconnector->base.ycbcr_420_allowed =
			link->link_enc->features.dp_ycbcr420_supported ? true : false;
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

	drm_connector_attach_max_bpc_property(&aconnector->base, 8, 16);

	/* This defaults to the max in the range, but we want 8bpc. */
	aconnector->base.state->max_bpc = 8;
	aconnector->base.state->max_requested_bpc = 8;

	if (connector_type == DRM_MODE_CONNECTOR_eDP &&
	    dc_is_dmcu_initialized(adev->dm.dc)) {
		drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.abm_level_property, 0);
	}

	if (connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector_type == DRM_MODE_CONNECTOR_DisplayPort ||
	    connector_type == DRM_MODE_CONNECTOR_eDP) {
		drm_object_attach_property(
			&aconnector->base.base,
			dm->ddev->mode_config.hdr_output_metadata_property, 0);

		drm_connector_attach_vrr_capable_property(
			&aconnector->base);
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

	if (dc_submit_i2c(
			ddc_service->ctx->dc,
			ddc_service->ddc_pin->hw_info.ddc_channel,
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
	i2c->ddc_service->ddc_pin->hw_info.ddc_channel = link_index;

	return i2c;
}


/*
 * Note: this function assumes that dc_link_detect() was called for the
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
	aconnector->debugfs_dpcd_address = 0;
	aconnector->debugfs_dpcd_size = 0;
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

	acrtc->otg_inst = -1;
	acrtc->enabled = false;
}

static int get_cursor_position(struct drm_plane *plane, struct drm_crtc *crtc,
			       struct dc_cursor_position *position)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	int x, y;
	int xorigin = 0, yorigin = 0;

	position->enable = false;
	position->x = 0;
	position->y = 0;

	if (!crtc || !plane->state->fb)
		return 0;

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

	if (x <= -amdgpu_crtc->max_cursor_width ||
	    y <= -amdgpu_crtc->max_cursor_height)
		return 0;

	if (crtc->primary->state) {
		/* avivo cursor are offset into the total surface */
		x += crtc->primary->state->src_x >> 16;
		y += crtc->primary->state->src_y >> 16;
	}

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
	struct amdgpu_device *adev = plane->dev->dev_private;
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
		if (crtc_state && crtc_state->stream) {
			mutex_lock(&adev->dm.dc_lock);
			dc_stream_set_cursor_position(crtc_state->stream,
						      &position);
			mutex_unlock(&adev->dm.dc_lock);
		}
		return;
	}

	amdgpu_crtc->cursor_width = plane->state->crtc_w;
	amdgpu_crtc->cursor_height = plane->state->crtc_h;

	memset(&attributes, 0, sizeof(attributes));
	attributes.address.high_part = upper_32_bits(address);
	attributes.address.low_part  = lower_32_bits(address);
	attributes.width             = plane->state->crtc_w;
	attributes.height            = plane->state->crtc_h;
	attributes.color_format      = CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA;
	attributes.rotation_angle    = 0;
	attributes.attribute_flags.value = 0;

	attributes.pitch = attributes.width;

	if (crtc_state->stream) {
		mutex_lock(&adev->dm.dc_lock);
		if (!dc_stream_set_cursor_attributes(crtc_state->stream,
							 &attributes))
			DRM_ERROR("DC failed to set cursor attributes\n");

		if (!dc_stream_set_cursor_position(crtc_state->stream,
						   &position))
			DRM_ERROR("DC failed to set cursor position\n");
		mutex_unlock(&adev->dm.dc_lock);
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

static void update_freesync_state_on_stream(
	struct amdgpu_display_manager *dm,
	struct dm_crtc_state *new_crtc_state,
	struct dc_stream_state *new_stream,
	struct dc_plane_state *surface,
	u32 flip_timestamp_in_us)
{
	struct mod_vrr_params vrr_params;
	struct dc_info_packet vrr_infopacket = {0};
	struct amdgpu_device *adev = dm->adev;
	unsigned long flags;

	if (!new_stream)
		return;

	/*
	 * TODO: Determine why min/max totals and vrefresh can be 0 here.
	 * For now it's sufficient to just guard against these conditions.
	 */

	if (!new_stream->timing.h_total || !new_stream->timing.v_total)
		return;

	spin_lock_irqsave(&adev->ddev->event_lock, flags);
	vrr_params = new_crtc_state->vrr_params;

	if (surface) {
		mod_freesync_handle_preflip(
			dm->freesync_module,
			surface,
			new_stream,
			flip_timestamp_in_us,
			&vrr_params);

		if (adev->family < AMDGPU_FAMILY_AI &&
		    amdgpu_dm_vrr_active(new_crtc_state)) {
			mod_freesync_handle_v_update(dm->freesync_module,
						     new_stream, &vrr_params);

			/* Need to call this before the frame ends. */
			dc_stream_adjust_vmin_vmax(dm->dc,
						   new_crtc_state->stream,
						   &vrr_params.adjust);
		}
	}

	mod_freesync_build_vrr_infopacket(
		dm->freesync_module,
		new_stream,
		&vrr_params,
		PACKET_TYPE_VRR,
		TRANSFER_FUNC_UNKNOWN,
		&vrr_infopacket);

	new_crtc_state->freesync_timing_changed |=
		(memcmp(&new_crtc_state->vrr_params.adjust,
			&vrr_params.adjust,
			sizeof(vrr_params.adjust)) != 0);

	new_crtc_state->freesync_vrr_info_changed |=
		(memcmp(&new_crtc_state->vrr_infopacket,
			&vrr_infopacket,
			sizeof(vrr_infopacket)) != 0);

	new_crtc_state->vrr_params = vrr_params;
	new_crtc_state->vrr_infopacket = vrr_infopacket;

	new_stream->adjust = new_crtc_state->vrr_params.adjust;
	new_stream->vrr_infopacket = vrr_infopacket;

	if (new_crtc_state->freesync_vrr_info_changed)
		DRM_DEBUG_KMS("VRR packet update: crtc=%u enabled=%d state=%d",
			      new_crtc_state->base.crtc->base.id,
			      (int)new_crtc_state->base.vrr_enabled,
			      (int)vrr_params.state);

	spin_unlock_irqrestore(&adev->ddev->event_lock, flags);
}

static void pre_update_freesync_state_on_stream(
	struct amdgpu_display_manager *dm,
	struct dm_crtc_state *new_crtc_state)
{
	struct dc_stream_state *new_stream = new_crtc_state->stream;
	struct mod_vrr_params vrr_params;
	struct mod_freesync_config config = new_crtc_state->freesync_config;
	struct amdgpu_device *adev = dm->adev;
	unsigned long flags;

	if (!new_stream)
		return;

	/*
	 * TODO: Determine why min/max totals and vrefresh can be 0 here.
	 * For now it's sufficient to just guard against these conditions.
	 */
	if (!new_stream->timing.h_total || !new_stream->timing.v_total)
		return;

	spin_lock_irqsave(&adev->ddev->event_lock, flags);
	vrr_params = new_crtc_state->vrr_params;

	if (new_crtc_state->vrr_supported &&
	    config.min_refresh_in_uhz &&
	    config.max_refresh_in_uhz) {
		config.state = new_crtc_state->base.vrr_enabled ?
			VRR_STATE_ACTIVE_VARIABLE :
			VRR_STATE_INACTIVE;
	} else {
		config.state = VRR_STATE_UNSUPPORTED;
	}

	mod_freesync_build_vrr_params(dm->freesync_module,
				      new_stream,
				      &config, &vrr_params);

	new_crtc_state->freesync_timing_changed |=
		(memcmp(&new_crtc_state->vrr_params.adjust,
			&vrr_params.adjust,
			sizeof(vrr_params.adjust)) != 0);

	new_crtc_state->vrr_params = vrr_params;
	spin_unlock_irqrestore(&adev->ddev->event_lock, flags);
}

static void amdgpu_dm_handle_vrr_transition(struct dm_crtc_state *old_state,
					    struct dm_crtc_state *new_state)
{
	bool old_vrr_active = amdgpu_dm_vrr_active(old_state);
	bool new_vrr_active = amdgpu_dm_vrr_active(new_state);

	if (!old_vrr_active && new_vrr_active) {
		/* Transition VRR inactive -> active:
		 * While VRR is active, we must not disable vblank irq, as a
		 * reenable after disable would compute bogus vblank/pflip
		 * timestamps if it likely happened inside display front-porch.
		 *
		 * We also need vupdate irq for the actual core vblank handling
		 * at end of vblank.
		 */
		dm_set_vupdate_irq(new_state->base.crtc, true);
		drm_crtc_vblank_get(new_state->base.crtc);
		DRM_DEBUG_DRIVER("%s: crtc=%u VRR off->on: Get vblank ref\n",
				 __func__, new_state->base.crtc->base.id);
	} else if (old_vrr_active && !new_vrr_active) {
		/* Transition VRR active -> inactive:
		 * Allow vblank irq disable again for fixed refresh rate.
		 */
		dm_set_vupdate_irq(new_state->base.crtc, false);
		drm_crtc_vblank_put(new_state->base.crtc);
		DRM_DEBUG_DRIVER("%s: crtc=%u VRR on->off: Drop vblank ref\n",
				 __func__, new_state->base.crtc->base.id);
	}
}

static void amdgpu_dm_commit_cursors(struct drm_atomic_state *state)
{
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	int i;

	/*
	 * TODO: Make this per-stream so we don't issue redundant updates for
	 * commits with multiple streams.
	 */
	for_each_oldnew_plane_in_state(state, plane, old_plane_state,
				       new_plane_state, i)
		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			handle_cursor_update(plane, old_plane_state);
}

static void amdgpu_dm_commit_planes(struct drm_atomic_state *state,
				    struct dc_state *dc_state,
				    struct drm_device *dev,
				    struct amdgpu_display_manager *dm,
				    struct drm_crtc *pcrtc,
				    bool wait_for_vblank)
{
	uint32_t i;
	uint64_t timestamp_ns;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	struct amdgpu_crtc *acrtc_attach = to_amdgpu_crtc(pcrtc);
	struct drm_crtc_state *new_pcrtc_state =
			drm_atomic_get_new_crtc_state(state, pcrtc);
	struct dm_crtc_state *acrtc_state = to_dm_crtc_state(new_pcrtc_state);
	struct dm_crtc_state *dm_old_crtc_state =
			to_dm_crtc_state(drm_atomic_get_old_crtc_state(state, pcrtc));
	int planes_count = 0, vpos, hpos;
	long r;
	unsigned long flags;
	struct amdgpu_bo *abo;
	uint64_t tiling_flags;
	uint32_t target_vblank, last_flip_vblank;
	bool vrr_active = amdgpu_dm_vrr_active(acrtc_state);
	bool pflip_present = false;
	struct {
		struct dc_surface_update surface_updates[MAX_SURFACES];
		struct dc_plane_info plane_infos[MAX_SURFACES];
		struct dc_scaling_info scaling_infos[MAX_SURFACES];
		struct dc_flip_addrs flip_addrs[MAX_SURFACES];
		struct dc_stream_update stream_update;
	} *bundle;

	bundle = kzalloc(sizeof(*bundle), GFP_KERNEL);

	if (!bundle) {
		dm_error("Failed to allocate update bundle\n");
		goto cleanup;
	}

	/*
	 * Disable the cursor first if we're disabling all the planes.
	 * It'll remain on the screen after the planes are re-enabled
	 * if we don't.
	 */
	if (acrtc_state->active_planes == 0)
		amdgpu_dm_commit_cursors(state);

	/* update planes when needed */
	for_each_oldnew_plane_in_state(state, plane, old_plane_state, new_plane_state, i) {
		struct drm_crtc *crtc = new_plane_state->crtc;
		struct drm_crtc_state *new_crtc_state;
		struct drm_framebuffer *fb = new_plane_state->fb;
		bool plane_needs_flip;
		struct dc_plane_state *dc_plane;
		struct dm_plane_state *dm_new_plane_state = to_dm_plane_state(new_plane_state);

		/* Cursor plane is handled after stream updates */
		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			continue;

		if (!fb || !crtc || pcrtc != crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
		if (!new_crtc_state->active)
			continue;

		dc_plane = dm_new_plane_state->dc_state;

		bundle->surface_updates[planes_count].surface = dc_plane;
		if (new_pcrtc_state->color_mgmt_changed) {
			bundle->surface_updates[planes_count].gamma = dc_plane->gamma_correction;
			bundle->surface_updates[planes_count].in_transfer_func = dc_plane->in_transfer_func;
		}

		fill_dc_scaling_info(new_plane_state,
				     &bundle->scaling_infos[planes_count]);

		bundle->surface_updates[planes_count].scaling_info =
			&bundle->scaling_infos[planes_count];

		plane_needs_flip = old_plane_state->fb && new_plane_state->fb;

		pflip_present = pflip_present || plane_needs_flip;

		if (!plane_needs_flip) {
			planes_count += 1;
			continue;
		}

		abo = gem_to_amdgpu_bo(fb->obj[0]);

		/*
		 * Wait for all fences on this FB. Do limited wait to avoid
		 * deadlock during GPU reset when this fence will not signal
		 * but we hold reservation lock for the BO.
		 */
		r = reservation_object_wait_timeout_rcu(abo->tbo.resv, true,
							false,
							msecs_to_jiffies(5000));
		if (unlikely(r <= 0))
			DRM_ERROR("Waiting for fences timed out or interrupted!");

		/*
		 * TODO This might fail and hence better not used, wait
		 * explicitly on fences instead
		 * and in general should be called for
		 * blocking commit to as per framework helpers
		 */
		r = amdgpu_bo_reserve(abo, true);
		if (unlikely(r != 0))
			DRM_ERROR("failed to reserve buffer before flip\n");

		amdgpu_bo_get_tiling_flags(abo, &tiling_flags);

		amdgpu_bo_unreserve(abo);

		fill_dc_plane_info_and_addr(
			dm->adev, new_plane_state, tiling_flags,
			&bundle->plane_infos[planes_count],
			&bundle->flip_addrs[planes_count].address);

		bundle->surface_updates[planes_count].plane_info =
			&bundle->plane_infos[planes_count];

		bundle->flip_addrs[planes_count].flip_immediate =
				(crtc->state->pageflip_flags & DRM_MODE_PAGE_FLIP_ASYNC) != 0;

		timestamp_ns = ktime_get_ns();
		bundle->flip_addrs[planes_count].flip_timestamp_in_us = div_u64(timestamp_ns, 1000);
		bundle->surface_updates[planes_count].flip_addr = &bundle->flip_addrs[planes_count];
		bundle->surface_updates[planes_count].surface = dc_plane;

		if (!bundle->surface_updates[planes_count].surface) {
			DRM_ERROR("No surface for CRTC: id=%d\n",
					acrtc_attach->crtc_id);
			continue;
		}

		if (plane == pcrtc->primary)
			update_freesync_state_on_stream(
				dm,
				acrtc_state,
				acrtc_state->stream,
				dc_plane,
				bundle->flip_addrs[planes_count].flip_timestamp_in_us);

		DRM_DEBUG_DRIVER("%s Flipping to hi: 0x%x, low: 0x%x\n",
				 __func__,
				 bundle->flip_addrs[planes_count].address.grph.addr.high_part,
				 bundle->flip_addrs[planes_count].address.grph.addr.low_part);

		planes_count += 1;

	}

	if (pflip_present) {
		if (!vrr_active) {
			/* Use old throttling in non-vrr fixed refresh rate mode
			 * to keep flip scheduling based on target vblank counts
			 * working in a backwards compatible way, e.g., for
			 * clients using the GLX_OML_sync_control extension or
			 * DRI3/Present extension with defined target_msc.
			 */
			last_flip_vblank = amdgpu_get_vblank_counter_kms(dm->ddev, acrtc_attach->crtc_id);
		}
		else {
			/* For variable refresh rate mode only:
			 * Get vblank of last completed flip to avoid > 1 vrr
			 * flips per video frame by use of throttling, but allow
			 * flip programming anywhere in the possibly large
			 * variable vrr vblank interval for fine-grained flip
			 * timing control and more opportunity to avoid stutter
			 * on late submission of flips.
			 */
			spin_lock_irqsave(&pcrtc->dev->event_lock, flags);
			last_flip_vblank = acrtc_attach->last_flip_vblank;
			spin_unlock_irqrestore(&pcrtc->dev->event_lock, flags);
		}

		target_vblank = last_flip_vblank + wait_for_vblank;

		/*
		 * Wait until we're out of the vertical blank period before the one
		 * targeted by the flip
		 */
		while ((acrtc_attach->enabled &&
			(amdgpu_display_get_crtc_scanoutpos(dm->ddev, acrtc_attach->crtc_id,
							    0, &vpos, &hpos, NULL,
							    NULL, &pcrtc->hwmode)
			 & (DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_IN_VBLANK)) ==
			(DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_IN_VBLANK) &&
			(int)(target_vblank -
			  amdgpu_get_vblank_counter_kms(dm->ddev, acrtc_attach->crtc_id)) > 0)) {
			usleep_range(1000, 1100);
		}

		if (acrtc_attach->base.state->event) {
			drm_crtc_vblank_get(pcrtc);

			spin_lock_irqsave(&pcrtc->dev->event_lock, flags);

			WARN_ON(acrtc_attach->pflip_status != AMDGPU_FLIP_NONE);
			prepare_flip_isr(acrtc_attach);

			spin_unlock_irqrestore(&pcrtc->dev->event_lock, flags);
		}

		if (acrtc_state->stream) {
			if (acrtc_state->freesync_vrr_info_changed)
				bundle->stream_update.vrr_infopacket =
					&acrtc_state->stream->vrr_infopacket;
		}
	}

	/* Update the planes if changed or disable if we don't have any. */
	if (planes_count || acrtc_state->active_planes == 0) {
		if (new_pcrtc_state->mode_changed) {
			bundle->stream_update.src = acrtc_state->stream->src;
			bundle->stream_update.dst = acrtc_state->stream->dst;
		}

		if (new_pcrtc_state->color_mgmt_changed)
			bundle->stream_update.out_transfer_func = acrtc_state->stream->out_transfer_func;

		acrtc_state->stream->abm_level = acrtc_state->abm_level;
		if (acrtc_state->abm_level != dm_old_crtc_state->abm_level)
			bundle->stream_update.abm_level = &acrtc_state->abm_level;

		/*
		 * If FreeSync state on the stream has changed then we need to
		 * re-adjust the min/max bounds now that DC doesn't handle this
		 * as part of commit.
		 */
		if (amdgpu_dm_vrr_active(dm_old_crtc_state) !=
		    amdgpu_dm_vrr_active(acrtc_state)) {
			spin_lock_irqsave(&pcrtc->dev->event_lock, flags);
			dc_stream_adjust_vmin_vmax(
				dm->dc, acrtc_state->stream,
				&acrtc_state->vrr_params.adjust);
			spin_unlock_irqrestore(&pcrtc->dev->event_lock, flags);
		}

		mutex_lock(&dm->dc_lock);
		dc_commit_updates_for_stream(dm->dc,
						     bundle->surface_updates,
						     planes_count,
						     acrtc_state->stream,
						     &bundle->stream_update,
						     dc_state);
		mutex_unlock(&dm->dc_lock);
	}

	/*
	 * Update cursor state *after* programming all the planes.
	 * This avoids redundant programming in the case where we're going
	 * to be disabling a single plane - those pipes are being disabled.
	 */
	if (acrtc_state->active_planes)
		amdgpu_dm_commit_cursors(state);

cleanup:
	kfree(bundle);
}

/*
 * Enable interrupts on CRTCs that are newly active, undergone
 * a modeset, or have active planes again.
 *
 * Done in two passes, based on the for_modeset flag:
 * Pass 1: For CRTCs going through modeset
 * Pass 2: For CRTCs going from 0 to n active planes
 *
 * Interrupts can only be enabled after the planes are programmed,
 * so this requires a two-pass approach since we don't want to
 * just defer the interrupts until after commit planes every time.
 */
static void amdgpu_dm_enable_crtc_interrupts(struct drm_device *dev,
					     struct drm_atomic_state *state,
					     bool for_modeset)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	int i;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state,
				      new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
		struct dm_crtc_state *dm_new_crtc_state =
			to_dm_crtc_state(new_crtc_state);
		struct dm_crtc_state *dm_old_crtc_state =
			to_dm_crtc_state(old_crtc_state);
		bool modeset = drm_atomic_crtc_needs_modeset(new_crtc_state);
		bool run_pass;

		run_pass = (for_modeset && modeset) ||
			   (!for_modeset && !modeset &&
			    !dm_old_crtc_state->interrupts_enabled);

		if (!run_pass)
			continue;

		if (!dm_new_crtc_state->interrupts_enabled)
			continue;

		manage_dm_interrupts(adev, acrtc, true);

#ifdef CONFIG_DEBUG_FS
		/* The stream has changed so CRC capture needs to re-enabled. */
		if (dm_new_crtc_state->crc_enabled) {
			dm_new_crtc_state->crc_enabled = false;
			amdgpu_dm_crtc_set_crc_source(crtc, "auto");
		}
#endif
	}
}

/*
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
	stream_state->mode_changed = drm_atomic_crtc_needs_modeset(crtc_state);
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
	 * We evade vblank and pflip interrupts on CRTCs that are undergoing
	 * a modeset, being disabled, or have no active planes.
	 *
	 * It's done in atomic commit rather than commit tail for now since
	 * some of these interrupt handlers access the current CRTC state and
	 * potentially the stream pointer itself.
	 *
	 * Since the atomic state is swapped within atomic commit and not within
	 * commit tail this would leave to new state (that hasn't been committed yet)
	 * being accesssed from within the handlers.
	 *
	 * TODO: Fix this so we can do this in commit tail and not have to block
	 * in atomic check.
	 */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct dm_crtc_state *dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);
		struct dm_crtc_state *dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		if (dm_old_crtc_state->interrupts_enabled &&
		    (!dm_new_crtc_state->interrupts_enabled ||
		     drm_atomic_crtc_needs_modeset(new_crtc_state))) {
			/*
			 * Drop the extra vblank reference added by CRC
			 * capture if applicable.
			 */
			if (dm_new_crtc_state->crc_enabled)
				drm_crtc_vblank_put(crtc);

			/*
			 * Only keep CRC capture enabled if there's
			 * still a stream for the CRTC.
			 */
			if (!dm_new_crtc_state->stream)
				dm_new_crtc_state->crc_enabled = false;

			manage_dm_interrupts(adev, acrtc, false);
		}
	}
	/*
	 * Add check here for SoC's that support hardware cursor plane, to
	 * unset legacy_cursor_update
	 */

	return drm_atomic_helper_commit(dev, state, nonblock);

	/*TODO Handle EINTR, reenable IRQ*/
}

/**
 * amdgpu_dm_atomic_commit_tail() - AMDgpu DM's commit tail implementation.
 * @state: The atomic state to commit
 *
 * This will tell DC to commit the constructed DC state from atomic_check,
 * programming the hardware. Any failures here implies a hardware failure, since
 * atomic check should have filtered anything non-kosher.
 */
static void amdgpu_dm_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct dm_atomic_state *dm_state;
	struct dc_state *dc_state = NULL, *dc_state_temp = NULL;
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

	dm_state = dm_atomic_get_new_state(state);
	if (dm_state && dm_state->context) {
		dc_state = dm_state->context;
	} else {
		/* No state changes, retain current state. */
		dc_state_temp = dc_create_state(dm->dc);
		ASSERT(dc_state_temp);
		dc_state = dc_state_temp;
		dc_resource_state_copy_construct_current(dm->dc, dc_state);
	}

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
				 * display which is disconnected in fact.
				 * dc_sink is NULL in this case on aconnector.
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

	if (dc_state) {
		dm_enable_per_frame_crtc_master_sync(dc_state);
		mutex_lock(&dm->dc_lock);
		WARN_ON(!dc_commit_state(dm->dc, dc_state));
		mutex_unlock(&dm->dc_lock);
	}

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (dm_new_crtc_state->stream != NULL) {
			const struct dc_stream_status *status =
					dc_stream_get_status(dm_new_crtc_state->stream);

			if (!status)
				status = dc_stream_get_status_from_state(dc_state,
									 dm_new_crtc_state->stream);

			if (!status)
				DC_ERR("got no status for stream %p on acrtc%p\n", dm_new_crtc_state->stream, acrtc);
			else
				acrtc->otg_inst = status->primary_otg_inst;
		}
	}

	/* Handle connector state changes */
	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);
		struct dm_connector_state *dm_old_con_state = to_dm_connector_state(old_con_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(dm_new_con_state->base.crtc);
		struct dc_surface_update dummy_updates[MAX_SURFACES];
		struct dc_stream_update stream_update;
		struct dc_info_packet hdr_packet;
		struct dc_stream_status *status = NULL;
		bool abm_changed, hdr_changed, scaling_changed;

		memset(&dummy_updates, 0, sizeof(dummy_updates));
		memset(&stream_update, 0, sizeof(stream_update));

		if (acrtc) {
			new_crtc_state = drm_atomic_get_new_crtc_state(state, &acrtc->base);
			old_crtc_state = drm_atomic_get_old_crtc_state(state, &acrtc->base);
		}

		/* Skip any modesets/resets */
		if (!acrtc || drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		scaling_changed = is_scaling_state_different(dm_new_con_state,
							     dm_old_con_state);

		abm_changed = dm_new_crtc_state->abm_level !=
			      dm_old_crtc_state->abm_level;

		hdr_changed =
			is_hdr_metadata_different(old_con_state, new_con_state);

		if (!scaling_changed && !abm_changed && !hdr_changed)
			continue;

		if (scaling_changed) {
			update_stream_scaling_settings(&dm_new_con_state->base.crtc->mode,
					dm_new_con_state, (struct dc_stream_state *)dm_new_crtc_state->stream);

			stream_update.src = dm_new_crtc_state->stream->src;
			stream_update.dst = dm_new_crtc_state->stream->dst;
		}

		if (abm_changed) {
			dm_new_crtc_state->stream->abm_level = dm_new_crtc_state->abm_level;

			stream_update.abm_level = &dm_new_crtc_state->abm_level;
		}

		if (hdr_changed) {
			fill_hdr_info_packet(new_con_state, &hdr_packet);
			stream_update.hdr_static_metadata = &hdr_packet;
		}

		status = dc_stream_get_status(dm_new_crtc_state->stream);
		WARN_ON(!status);
		WARN_ON(!status->plane_count);

		/*
		 * TODO: DC refuses to perform stream updates without a dc_surface_update.
		 * Here we create an empty update on each plane.
		 * To fix this, DC should permit updating only stream properties.
		 */
		for (j = 0; j < status->plane_count; j++)
			dummy_updates[j].surface = status->plane_states[0];


		mutex_lock(&dm->dc_lock);
		dc_commit_updates_for_stream(dm->dc,
						     dummy_updates,
						     status->plane_count,
						     dm_new_crtc_state->stream,
						     &stream_update,
						     dc_state);
		mutex_unlock(&dm->dc_lock);
	}

	/* Count number of newly disabled CRTCs for dropping PM refs later. */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state,
				      new_crtc_state, i) {
		if (old_crtc_state->active && !new_crtc_state->active)
			crtc_disable_count++;

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		/* Update freesync active state. */
		pre_update_freesync_state_on_stream(dm, dm_new_crtc_state);

		/* Handle vrr on->off / off->on transitions */
		amdgpu_dm_handle_vrr_transition(dm_old_crtc_state,
						dm_new_crtc_state);
	}

	/* Enable interrupts for CRTCs going through a modeset. */
	amdgpu_dm_enable_crtc_interrupts(dev, state, true);

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, j)
		if (new_crtc_state->pageflip_flags & DRM_MODE_PAGE_FLIP_ASYNC)
			wait_for_vblank = false;

	/* update planes when needed per crtc*/
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, j) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (dm_new_crtc_state->stream)
			amdgpu_dm_commit_planes(state, dc_state, dev,
						dm, crtc, wait_for_vblank);
	}

	/* Enable interrupts for CRTCs going from 0 to n active planes. */
	amdgpu_dm_enable_crtc_interrupts(dev, state, false);

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

	/*
	 * Finally, drop a runtime PM reference for each newly disabled CRTC,
	 * so we can put the GPU into runtime suspend if we're not driving any
	 * displays anymore
	 */
	for (i = 0; i < crtc_disable_count; i++)
		pm_runtime_put_autosuspend(dev->dev);
	pm_runtime_mark_last_busy(dev->dev);

	if (dc_state_temp)
		dc_release_state(dc_state_temp);
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
 * This function handles all cases when set mode does not come upon hotplug.
 * This includes when a display is unplugged then plugged back into the
 * same port and when running without usermode desktop manager supprot
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

/*
 * Grabs all modesetting locks to serialize against any blocking commits,
 * Waits for completion of all non blocking commits.
 */
static int do_aquire_global_lock(struct drm_device *dev,
				 struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_commit *commit;
	long ret;

	/*
	 * Adding all modeset locks to aquire_ctx will
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

		/*
		 * Make sure all pending HW programming completed and
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

static void get_freesync_config_for_crtc(
	struct dm_crtc_state *new_crtc_state,
	struct dm_connector_state *new_con_state)
{
	struct mod_freesync_config config = {0};
	struct amdgpu_dm_connector *aconnector =
			to_amdgpu_dm_connector(new_con_state->base.connector);
	struct drm_display_mode *mode = &new_crtc_state->base.mode;
	int vrefresh = drm_mode_vrefresh(mode);

	new_crtc_state->vrr_supported = new_con_state->freesync_capable &&
					vrefresh >= aconnector->min_vfreq &&
					vrefresh <= aconnector->max_vfreq;

	if (new_crtc_state->vrr_supported) {
		new_crtc_state->stream->ignore_msa_timing_param = true;
		config.state = new_crtc_state->base.vrr_enabled ?
				VRR_STATE_ACTIVE_VARIABLE :
				VRR_STATE_INACTIVE;
		config.min_refresh_in_uhz =
				aconnector->min_vfreq * 1000000;
		config.max_refresh_in_uhz =
				aconnector->max_vfreq * 1000000;
		config.vsif_supported = true;
		config.btr = true;
	}

	new_crtc_state->freesync_config = config;
}

static void reset_freesync_config_for_crtc(
	struct dm_crtc_state *new_crtc_state)
{
	new_crtc_state->vrr_supported = false;

	memset(&new_crtc_state->vrr_params, 0,
	       sizeof(new_crtc_state->vrr_params));
	memset(&new_crtc_state->vrr_infopacket, 0,
	       sizeof(new_crtc_state->vrr_infopacket));
}

static int dm_update_crtc_state(struct amdgpu_display_manager *dm,
				struct drm_atomic_state *state,
				struct drm_crtc *crtc,
				struct drm_crtc_state *old_crtc_state,
				struct drm_crtc_state *new_crtc_state,
				bool enable,
				bool *lock_and_validation_needed)
{
	struct dm_atomic_state *dm_state = NULL;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	struct dc_stream_state *new_stream;
	int ret = 0;

	/*
	 * TODO Move this code into dm_crtc_atomic_check once we get rid of dc_validation_set
	 * update changed items
	 */
	struct amdgpu_crtc *acrtc = NULL;
	struct amdgpu_dm_connector *aconnector = NULL;
	struct drm_connector_state *drm_new_conn_state = NULL, *drm_old_conn_state = NULL;
	struct dm_connector_state *dm_new_conn_state = NULL, *dm_old_conn_state = NULL;

	new_stream = NULL;

	dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);
	dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
	acrtc = to_amdgpu_crtc(crtc);
	aconnector = amdgpu_dm_find_first_crtc_matching_connector(state, crtc);

	/* TODO This hack should go away */
	if (aconnector && enable) {
		/* Make sure fake sink is created in plug-in scenario */
		drm_new_conn_state = drm_atomic_get_new_connector_state(state,
							    &aconnector->base);
		drm_old_conn_state = drm_atomic_get_old_connector_state(state,
							    &aconnector->base);

		if (IS_ERR(drm_new_conn_state)) {
			ret = PTR_ERR_OR_ZERO(drm_new_conn_state);
			goto fail;
		}

		dm_new_conn_state = to_dm_connector_state(drm_new_conn_state);
		dm_old_conn_state = to_dm_connector_state(drm_old_conn_state);

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			goto skip_modeset;

		new_stream = create_stream_for_sink(aconnector,
						     &new_crtc_state->mode,
						    dm_new_conn_state,
						    dm_old_crtc_state->stream);

		/*
		 * we can have no stream on ACTION_SET if a display
		 * was disconnected during S3, in this case it is not an
		 * error, the OS will be updated after detection, and
		 * will do the right thing on next atomic commit
		 */

		if (!new_stream) {
			DRM_DEBUG_DRIVER("%s: Failed to create new stream for crtc %d\n",
					__func__, acrtc->base.base.id);
			ret = -ENOMEM;
			goto fail;
		}

		dm_new_crtc_state->abm_level = dm_new_conn_state->abm_level;

		ret = fill_hdr_info_packet(drm_new_conn_state,
					   &new_stream->hdr_static_metadata);
		if (ret)
			goto fail;

		/*
		 * If we already removed the old stream from the context
		 * (and set the new stream to NULL) then we can't reuse
		 * the old stream even if the stream and scaling are unchanged.
		 * We'll hit the BUG_ON and black screen.
		 *
		 * TODO: Refactor this function to allow this check to work
		 * in all conditions.
		 */
		if (dm_new_crtc_state->stream &&
		    dc_is_stream_unchanged(new_stream, dm_old_crtc_state->stream) &&
		    dc_is_stream_scaling_unchanged(new_stream, dm_old_crtc_state->stream)) {
			new_crtc_state->mode_changed = false;
			DRM_DEBUG_DRIVER("Mode change not required, setting mode_changed to %d",
					 new_crtc_state->mode_changed);
		}
	}

	/* mode_changed flag may get updated above, need to check again */
	if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
		goto skip_modeset;

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
			goto skip_modeset;

		ret = dm_atomic_get_state(state, &dm_state);
		if (ret)
			goto fail;

		DRM_DEBUG_DRIVER("Disabling DRM crtc: %d\n",
				crtc->base.id);

		/* i.e. reset mode */
		if (dc_remove_stream_from_ctx(
				dm->dc,
				dm_state->context,
				dm_old_crtc_state->stream) != DC_OK) {
			ret = -EINVAL;
			goto fail;
		}

		dc_stream_release(dm_old_crtc_state->stream);
		dm_new_crtc_state->stream = NULL;

		reset_freesync_config_for_crtc(dm_new_crtc_state);

		*lock_and_validation_needed = true;

	} else {/* Add stream for any updated/enabled CRTC */
		/*
		 * Quick fix to prevent NULL pointer on new_stream when
		 * added MST connectors not found in existing crtc_state in the chained mode
		 * TODO: need to dig out the root cause of that
		 */
		if (!aconnector || (!aconnector->dc_sink && aconnector->mst_port))
			goto skip_modeset;

		if (modereset_required(new_crtc_state))
			goto skip_modeset;

		if (modeset_required(new_crtc_state, new_stream,
				     dm_old_crtc_state->stream)) {

			WARN_ON(dm_new_crtc_state->stream);

			ret = dm_atomic_get_state(state, &dm_state);
			if (ret)
				goto fail;

			dm_new_crtc_state->stream = new_stream;

			dc_stream_retain(new_stream);

			DRM_DEBUG_DRIVER("Enabling DRM crtc: %d\n",
						crtc->base.id);

			if (dc_add_stream_to_ctx(
					dm->dc,
					dm_state->context,
					dm_new_crtc_state->stream) != DC_OK) {
				ret = -EINVAL;
				goto fail;
			}

			*lock_and_validation_needed = true;
		}
	}

skip_modeset:
	/* Release extra reference */
	if (new_stream)
		 dc_stream_release(new_stream);

	/*
	 * We want to do dc stream updates that do not require a
	 * full modeset below.
	 */
	if (!(enable && aconnector && new_crtc_state->enable &&
	      new_crtc_state->active))
		return 0;
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

	/* ABM settings */
	dm_new_crtc_state->abm_level = dm_new_conn_state->abm_level;

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

	/* Update Freesync settings. */
	get_freesync_config_for_crtc(dm_new_crtc_state,
				     dm_new_conn_state);

	return ret;

fail:
	if (new_stream)
		dc_stream_release(new_stream);
	return ret;
}

static bool should_reset_plane(struct drm_atomic_state *state,
			       struct drm_plane *plane,
			       struct drm_plane_state *old_plane_state,
			       struct drm_plane_state *new_plane_state)
{
	struct drm_plane *other;
	struct drm_plane_state *old_other_state, *new_other_state;
	struct drm_crtc_state *new_crtc_state;
	int i;

	/* Exit early if we know that we're adding or removing the plane. */
	if (old_plane_state->crtc != new_plane_state->crtc)
		return true;

	/* old crtc == new_crtc == NULL, plane not in context. */
	if (!new_plane_state->crtc)
		return false;

	new_crtc_state =
		drm_atomic_get_new_crtc_state(state, new_plane_state->crtc);

	if (!new_crtc_state)
		return true;

	/* CRTC Degamma changes currently require us to recreate planes. */
	if (new_crtc_state->color_mgmt_changed)
		return true;

	if (drm_atomic_crtc_needs_modeset(new_crtc_state))
		return true;

	/*
	 * If there are any new primary or overlay planes being added or
	 * removed then the z-order can potentially change. To ensure
	 * correct z-order and pipe acquisition the current DC architecture
	 * requires us to remove and recreate all existing planes.
	 *
	 * TODO: Come up with a more elegant solution for this.
	 */
	for_each_oldnew_plane_in_state(state, other, old_other_state, new_other_state, i) {
		if (other->type == DRM_PLANE_TYPE_CURSOR)
			continue;

		if (old_other_state->crtc != new_plane_state->crtc &&
		    new_other_state->crtc != new_plane_state->crtc)
			continue;

		if (old_other_state->crtc != new_other_state->crtc)
			return true;

		/* TODO: Remove this once we can handle fast format changes. */
		if (old_other_state->fb && new_other_state->fb &&
		    old_other_state->fb->format != new_other_state->fb->format)
			return true;
	}

	return false;
}

static int dm_update_plane_state(struct dc *dc,
				 struct drm_atomic_state *state,
				 struct drm_plane *plane,
				 struct drm_plane_state *old_plane_state,
				 struct drm_plane_state *new_plane_state,
				 bool enable,
				 bool *lock_and_validation_needed)
{

	struct dm_atomic_state *dm_state = NULL;
	struct drm_crtc *new_plane_crtc, *old_plane_crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct dm_crtc_state *dm_new_crtc_state, *dm_old_crtc_state;
	struct dm_plane_state *dm_new_plane_state, *dm_old_plane_state;
	bool needs_reset;
	int ret = 0;


	new_plane_crtc = new_plane_state->crtc;
	old_plane_crtc = old_plane_state->crtc;
	dm_new_plane_state = to_dm_plane_state(new_plane_state);
	dm_old_plane_state = to_dm_plane_state(old_plane_state);

	/*TODO Implement atomic check for cursor plane */
	if (plane->type == DRM_PLANE_TYPE_CURSOR)
		return 0;

	needs_reset = should_reset_plane(state, plane, old_plane_state,
					 new_plane_state);

	/* Remove any changed/removed planes */
	if (!enable) {
		if (!needs_reset)
			return 0;

		if (!old_plane_crtc)
			return 0;

		old_crtc_state = drm_atomic_get_old_crtc_state(
				state, old_plane_crtc);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		if (!dm_old_crtc_state->stream)
			return 0;

		DRM_DEBUG_ATOMIC("Disabling DRM plane: %d on DRM crtc %d\n",
				plane->base.id, old_plane_crtc->base.id);

		ret = dm_atomic_get_state(state, &dm_state);
		if (ret)
			return ret;

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
			return 0;

		if (!new_plane_crtc)
			return 0;

		new_crtc_state = drm_atomic_get_new_crtc_state(state, new_plane_crtc);
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (!dm_new_crtc_state->stream)
			return 0;

		if (!needs_reset)
			return 0;

		WARN_ON(dm_new_plane_state->dc_state);

		dc_new_plane_state = dc_create_plane_state(dc);
		if (!dc_new_plane_state)
			return -ENOMEM;

		DRM_DEBUG_DRIVER("Enabling DRM plane: %d on DRM crtc %d\n",
				plane->base.id, new_plane_crtc->base.id);

		ret = fill_dc_plane_attributes(
			new_plane_crtc->dev->dev_private,
			dc_new_plane_state,
			new_plane_state,
			new_crtc_state);
		if (ret) {
			dc_plane_state_release(dc_new_plane_state);
			return ret;
		}

		ret = dm_atomic_get_state(state, &dm_state);
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


	return ret;
}

static int
dm_determine_update_type_for_commit(struct amdgpu_display_manager *dm,
				    struct drm_atomic_state *state,
				    enum surface_update_type *out_type)
{
	struct dc *dc = dm->dc;
	struct dm_atomic_state *dm_state = NULL, *old_dm_state = NULL;
	int i, j, num_plane, ret = 0;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	struct dm_plane_state *new_dm_plane_state, *old_dm_plane_state;
	struct drm_crtc *new_plane_crtc, *old_plane_crtc;
	struct drm_plane *plane;

	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state, *old_crtc_state;
	struct dm_crtc_state *new_dm_crtc_state, *old_dm_crtc_state;
	struct dc_stream_status *status = NULL;

	struct dc_surface_update *updates;
	enum surface_update_type update_type = UPDATE_TYPE_FAST;

	updates = kcalloc(MAX_SURFACES, sizeof(*updates), GFP_KERNEL);

	if (!updates) {
		DRM_ERROR("Failed to allocate plane updates\n");
		/* Set type to FULL to avoid crashing in DC*/
		update_type = UPDATE_TYPE_FULL;
		goto cleanup;
	}

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct dc_scaling_info scaling_info;
		struct dc_stream_update stream_update;

		memset(&stream_update, 0, sizeof(stream_update));

		new_dm_crtc_state = to_dm_crtc_state(new_crtc_state);
		old_dm_crtc_state = to_dm_crtc_state(old_crtc_state);
		num_plane = 0;

		if (new_dm_crtc_state->stream != old_dm_crtc_state->stream) {
			update_type = UPDATE_TYPE_FULL;
			goto cleanup;
		}

		if (!new_dm_crtc_state->stream)
			continue;

		for_each_oldnew_plane_in_state(state, plane, old_plane_state, new_plane_state, j) {
			new_plane_crtc = new_plane_state->crtc;
			old_plane_crtc = old_plane_state->crtc;
			new_dm_plane_state = to_dm_plane_state(new_plane_state);
			old_dm_plane_state = to_dm_plane_state(old_plane_state);

			if (plane->type == DRM_PLANE_TYPE_CURSOR)
				continue;

			if (new_dm_plane_state->dc_state != old_dm_plane_state->dc_state) {
				update_type = UPDATE_TYPE_FULL;
				goto cleanup;
			}

			if (crtc != new_plane_crtc)
				continue;

			updates[num_plane].surface = new_dm_plane_state->dc_state;

			if (new_crtc_state->mode_changed) {
				stream_update.dst = new_dm_crtc_state->stream->dst;
				stream_update.src = new_dm_crtc_state->stream->src;
			}

			if (new_crtc_state->color_mgmt_changed) {
				updates[num_plane].gamma =
						new_dm_plane_state->dc_state->gamma_correction;
				updates[num_plane].in_transfer_func =
						new_dm_plane_state->dc_state->in_transfer_func;
				stream_update.gamut_remap =
						&new_dm_crtc_state->stream->gamut_remap_matrix;
				stream_update.out_transfer_func =
						new_dm_crtc_state->stream->out_transfer_func;
			}

			ret = fill_dc_scaling_info(new_plane_state,
						   &scaling_info);
			if (ret)
				goto cleanup;

			updates[num_plane].scaling_info = &scaling_info;

			num_plane++;
		}

		if (num_plane == 0)
			continue;

		ret = dm_atomic_get_state(state, &dm_state);
		if (ret)
			goto cleanup;

		old_dm_state = dm_atomic_get_old_state(state);
		if (!old_dm_state) {
			ret = -EINVAL;
			goto cleanup;
		}

		status = dc_stream_get_status_from_state(old_dm_state->context,
							 new_dm_crtc_state->stream);

		/*
		 * TODO: DC modifies the surface during this call so we need
		 * to lock here - find a way to do this without locking.
		 */
		mutex_lock(&dm->dc_lock);
		update_type = dc_check_update_surfaces_for_stream(dc, updates, num_plane,
								  &stream_update, status);
		mutex_unlock(&dm->dc_lock);

		if (update_type > UPDATE_TYPE_MED) {
			update_type = UPDATE_TYPE_FULL;
			goto cleanup;
		}
	}

cleanup:
	kfree(updates);

	*out_type = update_type;
	return ret;
}

/**
 * amdgpu_dm_atomic_check() - Atomic check implementation for AMDgpu DM.
 * @dev: The DRM device
 * @state: The atomic state to commit
 *
 * Validate that the given atomic state is programmable by DC into hardware.
 * This involves constructing a &struct dc_state reflecting the new hardware
 * state we wish to commit, then querying DC to see if it is programmable. It's
 * important not to modify the existing DC state. Otherwise, atomic_check
 * may unexpectedly commit hardware changes.
 *
 * When validating the DC state, it's important that the right locks are
 * acquired. For full updates case which removes/adds/updates streams on one
 * CRTC while flipping on another CRTC, acquiring global lock will guarantee
 * that any such full update commit will wait for completion of any outstanding
 * flip using DRMs synchronization events. See
 * dm_determine_update_type_for_commit()
 *
 * Note that DM adds the affected connectors for all CRTCs in state, when that
 * might not seem necessary. This is because DC stream creation requires the
 * DC sink, which is tied to the DRM connector state. Cleaning this up should
 * be possible but non-trivial - a possible TODO item.
 *
 * Return: -Error code if validation failed.
 */
static int amdgpu_dm_atomic_check(struct drm_device *dev,
				  struct drm_atomic_state *state)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct dm_atomic_state *dm_state = NULL;
	struct dc *dc = adev->dm.dc;
	struct drm_connector *connector;
	struct drm_connector_state *old_con_state, *new_con_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	enum surface_update_type update_type = UPDATE_TYPE_FAST;
	enum surface_update_type overall_update_type = UPDATE_TYPE_FAST;

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
		    !new_crtc_state->color_mgmt_changed &&
		    old_crtc_state->vrr_enabled == new_crtc_state->vrr_enabled)
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

	/*
	 * Add all primary and overlay planes on the CRTC to the state
	 * whenever a plane is enabled to maintain correct z-ordering
	 * and to enable fast surface updates.
	 */
	drm_for_each_crtc(crtc, dev) {
		bool modified = false;

		for_each_oldnew_plane_in_state(state, plane, old_plane_state, new_plane_state, i) {
			if (plane->type == DRM_PLANE_TYPE_CURSOR)
				continue;

			if (new_plane_state->crtc == crtc ||
			    old_plane_state->crtc == crtc) {
				modified = true;
				break;
			}
		}

		if (!modified)
			continue;

		drm_for_each_plane_mask(plane, state->dev, crtc->state->plane_mask) {
			if (plane->type == DRM_PLANE_TYPE_CURSOR)
				continue;

			new_plane_state =
				drm_atomic_get_plane_state(state, plane);

			if (IS_ERR(new_plane_state)) {
				ret = PTR_ERR(new_plane_state);
				goto fail;
			}
		}
	}

	/* Remove exiting planes if they are modified */
	for_each_oldnew_plane_in_state_reverse(state, plane, old_plane_state, new_plane_state, i) {
		ret = dm_update_plane_state(dc, state, plane,
					    old_plane_state,
					    new_plane_state,
					    false,
					    &lock_and_validation_needed);
		if (ret)
			goto fail;
	}

	/* Disable all crtcs which require disable */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		ret = dm_update_crtc_state(&adev->dm, state, crtc,
					   old_crtc_state,
					   new_crtc_state,
					   false,
					   &lock_and_validation_needed);
		if (ret)
			goto fail;
	}

	/* Enable all crtcs which require enable */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		ret = dm_update_crtc_state(&adev->dm, state, crtc,
					   old_crtc_state,
					   new_crtc_state,
					   true,
					   &lock_and_validation_needed);
		if (ret)
			goto fail;
	}

	/* Add new/modified planes */
	for_each_oldnew_plane_in_state_reverse(state, plane, old_plane_state, new_plane_state, i) {
		ret = dm_update_plane_state(dc, state, plane,
					    old_plane_state,
					    new_plane_state,
					    true,
					    &lock_and_validation_needed);
		if (ret)
			goto fail;
	}

	/* Run this here since we want to validate the streams we created */
	ret = drm_atomic_helper_check_planes(dev, state);
	if (ret)
		goto fail;

	/* Check scaling and underscan changes*/
	/* TODO Removed scaling changes validation due to inability to commit
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

		overall_update_type = UPDATE_TYPE_FULL;
		lock_and_validation_needed = true;
	}

	ret = dm_determine_update_type_for_commit(&adev->dm, state, &update_type);
	if (ret)
		goto fail;

	if (overall_update_type < update_type)
		overall_update_type = update_type;

	/*
	 * lock_and_validation_needed was an old way to determine if we need to set
	 * the global lock. Leaving it in to check if we broke any corner cases
	 * lock_and_validation_needed true = UPDATE_TYPE_FULL or UPDATE_TYPE_MED
	 * lock_and_validation_needed false = UPDATE_TYPE_FAST
	 */
	if (lock_and_validation_needed && overall_update_type <= UPDATE_TYPE_FAST)
		WARN(1, "Global lock should be Set, overall_update_type should be UPDATE_TYPE_MED or UPDATE_TYPE_FULL");

	if (overall_update_type > UPDATE_TYPE_FAST) {
		ret = dm_atomic_get_state(state, &dm_state);
		if (ret)
			goto fail;

		ret = do_aquire_global_lock(dev, state);
		if (ret)
			goto fail;

		if (dc_validate_global_state(dc, dm_state->context, false) != DC_OK) {
			ret = -EINVAL;
			goto fail;
		}
	} else if (state->legacy_cursor_update) {
		/*
		 * This is a fast cursor update coming from the plane update
		 * helper, check if it can be done asynchronously for better
		 * performance.
		 */
		state->async_update = !drm_atomic_helper_async_check(dev, state);
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
void amdgpu_dm_update_freesync_caps(struct drm_connector *connector,
					struct edid *edid)
{
	int i;
	bool edid_check_required;
	struct detailed_timing *timing;
	struct detailed_non_pixel *data;
	struct detailed_data_monitor_range *range;
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);
	struct dm_connector_state *dm_con_state = NULL;

	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
	bool freesync_capable = false;

	if (!connector->state) {
		DRM_ERROR("%s - Connector has no state", __func__);
		goto update;
	}

	if (!edid) {
		dm_con_state = to_dm_connector_state(connector->state);

		amdgpu_dm_connector->min_vfreq = 0;
		amdgpu_dm_connector->max_vfreq = 0;
		amdgpu_dm_connector->pixel_clock_mhz = 0;

		goto update;
	}

	dm_con_state = to_dm_connector_state(connector->state);

	edid_check_required = false;
	if (!amdgpu_dm_connector->dc_sink) {
		DRM_ERROR("dc_sink NULL, could not add free_sync module.\n");
		goto update;
	}
	if (!adev->dm.freesync_module)
		goto update;
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

			freesync_capable = true;
		}
	}

update:
	if (dm_con_state)
		dm_con_state->freesync_capable = freesync_capable;

	if (connector->vrr_capable_property)
		drm_connector_set_vrr_capable_property(connector,
						       freesync_capable);
}

