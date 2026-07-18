// SPDX-License-Identifier: MIT
/*
 * Copyright 2015-2026 Advanced Micro Devices, Inc.
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
#include "link_enc_cfg.h"
#include "dc/inc/core_types.h"
#include "dal_asic_id.h"
#include "dmub/dmub_srv.h"
#include "dc/inc/hw/dmcu.h"
#include "dc/inc/hw/abm.h"
#include "dc/dc_dmub_srv.h"
#include "dc/dc_edid_parser.h"
#include "dc/dc_stat.h"
#include "dc/dc_state.h"
#include "amdgpu_dm_trace.h"
#include "link/protocols/link_dpcd.h"
#include "link_service_types.h"
#include "link/protocols/link_dp_capability.h"
#include "link/protocols/link_ddc.h"

#include "amdgpu.h"
#include "amdgpu_display.h"
#include "amdgpu_ucode.h"
#include "atom.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_plane.h"
#include "amdgpu_dm_crtc.h"
#include "amdgpu_dm_hdcp.h"
#include <drm/display/drm_hdcp_helper.h>
#include "amdgpu_dm_wb.h"
#include "amdgpu_atombios.h"

#include "amd_shared.h"
#include "amdgpu_dm_irq.h"
#include "dm_helpers.h"
#include "amdgpu_dm_mst_types.h"
#if defined(CONFIG_DEBUG_FS)
#include "amdgpu_dm_debugfs.h"
#endif
#include "amdgpu_dm_psr.h"
#include "amdgpu_dm_replay.h"
#include "amdgpu_dm_backlight.h"
#include "amdgpu_dm_audio.h"
#include "amdgpu_dm_dmub.h"
#include "amdgpu_dm_connector.h"
#include "amdgpu_dm_kunit_helpers.h"

#include "ivsrcid/ivsrcid_vislands30.h"

#include <linux/backlight.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/pm_runtime.h>
#include <linux/pci.h>
#include <linux/power_supply.h>
#include <linux/firmware.h>
#include <linux/component.h>
#include <linux/sort.h>

#include <drm/drm_privacy_screen_consumer.h>
#include <drm/display/drm_dp_mst_helper.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_fixed.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_edid.h>
#include <drm/drm_eld.h>
#include <drm/drm_mode.h>
#include <drm/drm_utils.h>
#include <drm/drm_vblank.h>
#include <drm/drm_colorop.h>
#include <drm/drm_gem_atomic_helper.h>

#include <media/cec-notifier.h>
#include <acpi/video.h>

#include "ivsrcid/dcn/irqsrcs_dcn_1_0.h"

#include "modules/inc/mod_freesync.h"
#include "modules/inc/mod_power.h"
#include "modules/power/power_helpers.h"

MODULE_FIRMWARE(FIRMWARE_RAVEN_DMCU);
MODULE_FIRMWARE(FIRMWARE_NAVI12_DMCU);

/**
 * DOC: overview
 *
 * The AMDgpu display manager, **amdgpu_dm** (or even simpler,
 * **dm**) sits between DRM and DC. It acts as a liaison, converting DRM
 * requests into DC requests, and DC responses into DRM responses.
 *
 * The root control structure is &struct amdgpu_display_manager.
 */

/* basic init/fini API */
static int amdgpu_dm_init(struct amdgpu_device *adev);
static void amdgpu_dm_fini(struct amdgpu_device *adev);
static void reset_freesync_config_for_crtc(struct dm_crtc_state *new_crtc_state);

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

static int amdgpu_dm_atomic_setup_commit(struct drm_atomic_commit *state);
static void amdgpu_dm_atomic_commit_tail(struct drm_atomic_commit *state);
static void dm_enable_per_frame_crtc_master_sync(struct dc_state *context);

static int amdgpu_dm_atomic_check(struct drm_device *dev,
				  struct drm_atomic_commit *state);

STATIC_IFN_KUNIT bool
is_timing_unchanged_for_freesync(struct drm_crtc_state *old_crtc_state,
				 struct drm_crtc_state *new_crtc_state);

static inline void amdgpu_dm_exit_ips_for_hw_access(struct dc *dc)
{
	if (dc->ctx->dmub_srv && !dc->ctx->dmub_srv->idle_exit_counter)
		dc_exit_ips_for_hw_access(dc);
}

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
	struct amdgpu_crtc *acrtc = NULL;

	if (crtc >= adev->mode_info.num_crtc)
		return 0;

	acrtc = adev->mode_info.crtcs[crtc];

	if (!acrtc->dm_irq_params.stream) {
		drm_err(adev_to_drm(adev), "dc_stream_state is NULL for crtc '%d'!\n",
			  crtc);
		return 0;
	}

	return dc_stream_get_vblank_counter(acrtc->dm_irq_params.stream);
}

static int dm_crtc_get_scanoutpos(struct amdgpu_device *adev, int crtc,
				  u32 *vbl, u32 *position)
{
	u32 v_blank_start = 0, v_blank_end = 0, h_position = 0, v_position = 0;
	struct amdgpu_crtc *acrtc = NULL;
	struct dc *dc = adev->dm.dc;

	if ((crtc < 0) || (crtc >= adev->mode_info.num_crtc))
		return -EINVAL;

	acrtc = adev->mode_info.crtcs[crtc];

	if (!acrtc->dm_irq_params.stream) {
		drm_err(adev_to_drm(adev), "dc_stream_state is NULL for crtc '%d'!\n",
			  crtc);
		return 0;
	}

	if (dc && dc->caps.ips_support && dc->idle_optimizations_allowed)
		dc_allow_idle_optimizations(dc, false);

	/*
	 * TODO rework base driver to use values directly.
	 * for now parse it back into reg-format
	 */
	dc_stream_get_scanoutpos(acrtc->dm_irq_params.stream,
				 &v_blank_start,
				 &v_blank_end,
				 &h_position,
				 &v_position);

	*position = v_position | (h_position << 16);
	*vbl = v_blank_start | (v_blank_end << 16);

	return 0;
}

static bool dm_is_idle(struct amdgpu_ip_block *ip_block)
{
	/* XXX todo */
	return true;
}

static int dm_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	/* XXX todo */
	return 0;
}

static int dm_soft_reset(struct amdgpu_ip_block *ip_block)
{
	/* XXX todo */
	return 0;
}

STATIC_IFN_KUNIT bool is_dc_timing_adjust_needed(struct dm_crtc_state *old_state,
						 struct dm_crtc_state *new_state)
{
	if (new_state->stream->adjust.timing_adjust_pending)
		return true;
	if (new_state->freesync_config.state ==  VRR_STATE_ACTIVE_FIXED)
		return true;
	else if (amdgpu_dm_crtc_vrr_active(old_state) != amdgpu_dm_crtc_vrr_active(new_state))
		return true;
	else
		return false;
}
EXPORT_IF_KUNIT(is_dc_timing_adjust_needed);

/*
 * DC will program planes with their z-order determined by their ordering
 * in the dc_surface_updates array. This comparator is used to sort them
 * by descending zpos.
 */
STATIC_IFN_KUNIT int dm_plane_layer_index_cmp(const void *a, const void *b)
{
	const struct dc_surface_update *sa = (struct dc_surface_update *)a;
	const struct dc_surface_update *sb = (struct dc_surface_update *)b;

	/* Sort by descending dc_plane layer_index (i.e. normalized_zpos) */
	return sb->surface->layer_index - sa->surface->layer_index;
}
EXPORT_IF_KUNIT(dm_plane_layer_index_cmp);

/**
 * update_planes_and_stream_adapter() - Send planes to be updated in DC
 *
 * DC has a generic way to update planes and stream via
 * dc_update_planes_and_stream function; however, DM might need some
 * adjustments and preparation before calling it. This function is a wrapper
 * for the dc_update_planes_and_stream that does any required configuration
 * before passing control to DC.
 *
 * @dc: Display Core control structure
 * @update_type: specify whether it is FULL/MEDIUM/FAST update
 * @planes_count: planes count to update
 * @stream: stream state
 * @stream_update: stream update
 * @array_of_surface_update: dc surface update pointer
 *
 */
static inline bool update_planes_and_stream_adapter(struct dc *dc,
						    int update_type,
						    int planes_count,
						    struct dc_stream_state *stream,
						    struct dc_stream_update *stream_update,
						    struct dc_surface_update *array_of_surface_update)
{
	sort(array_of_surface_update, planes_count,
	     sizeof(*array_of_surface_update), dm_plane_layer_index_cmp, NULL);

	/*
	 * Previous frame finished and HW is ready for optimization.
	 */
	dc_post_update_surfaces_to_stream(dc);

	return dc_update_planes_and_stream(dc,
					   array_of_surface_update,
					   planes_count,
					   stream,
					   stream_update);
}

static int dm_set_clockgating_state(struct amdgpu_ip_block *ip_block,
		  enum amd_clockgating_state state)
{
	return 0;
}

static int dm_set_powergating_state(struct amdgpu_ip_block *ip_block,
		  enum amd_powergating_state state)
{
	return 0;
}

/* Prototypes of private functions */
static int dm_early_init(struct amdgpu_ip_block *ip_block);

/* Allocate memory for FBC compressed data  */
static void mmhub_read_system_context(struct amdgpu_device *adev, struct dc_phy_addr_space_config *pa_config)
{
	u64 pt_base;
	u32 logical_addr_low;
	u32 logical_addr_high;
	u32 agp_base, agp_bot, agp_top;
	PHYSICAL_ADDRESS_LOC page_table_start, page_table_end, page_table_base;

	memset(pa_config, 0, sizeof(*pa_config));

	agp_base = 0;
	agp_bot = adev->gmc.agp_start >> 24;
	agp_top = adev->gmc.agp_end >> 24;

	/* AGP aperture is disabled */
	if (agp_bot > agp_top) {
		logical_addr_low = adev->gmc.fb_start >> 18;
		if (adev->apu_flags & (AMD_APU_IS_RAVEN2 |
				       AMD_APU_IS_RENOIR |
				       AMD_APU_IS_GREEN_SARDINE))
			/*
			 * Raven2 has a HW issue that it is unable to use the vram which
			 * is out of MC_VM_SYSTEM_APERTURE_HIGH_ADDR. So here is the
			 * workaround that increase system aperture high address (add 1)
			 * to get rid of the VM fault and hardware hang.
			 */
			logical_addr_high = (adev->gmc.fb_end >> 18) + 0x1;
		else
			logical_addr_high = adev->gmc.fb_end >> 18;
	} else {
		logical_addr_low = min(adev->gmc.fb_start, adev->gmc.agp_start) >> 18;
		if (adev->apu_flags & (AMD_APU_IS_RAVEN2 |
				       AMD_APU_IS_RENOIR |
				       AMD_APU_IS_GREEN_SARDINE))
			/*
			 * Raven2 has a HW issue that it is unable to use the vram which
			 * is out of MC_VM_SYSTEM_APERTURE_HIGH_ADDR. So here is the
			 * workaround that increase system aperture high address (add 1)
			 * to get rid of the VM fault and hardware hang.
			 */
			logical_addr_high = max((adev->gmc.fb_end >> 18) + 0x1, adev->gmc.agp_end >> 18);
		else
			logical_addr_high = max(adev->gmc.fb_end, adev->gmc.agp_end) >> 18;
	}

	pt_base = amdgpu_gmc_pd_addr(adev->gart.bo);

	page_table_start.high_part = upper_32_bits(adev->gmc.gart_start >>
						   AMDGPU_GPU_PAGE_SHIFT);
	page_table_start.low_part = lower_32_bits(adev->gmc.gart_start >>
						  AMDGPU_GPU_PAGE_SHIFT);
	page_table_end.high_part = upper_32_bits(adev->gmc.gart_end >>
						 AMDGPU_GPU_PAGE_SHIFT);
	page_table_end.low_part = lower_32_bits(adev->gmc.gart_end >>
						AMDGPU_GPU_PAGE_SHIFT);
	page_table_base.high_part = upper_32_bits(pt_base);
	page_table_base.low_part = lower_32_bits(pt_base);

	pa_config->system_aperture.start_addr = (uint64_t)logical_addr_low << 18;
	pa_config->system_aperture.end_addr = (uint64_t)logical_addr_high << 18;

	pa_config->system_aperture.agp_base = (uint64_t)agp_base << 24;
	pa_config->system_aperture.agp_bot = (uint64_t)agp_bot << 24;
	pa_config->system_aperture.agp_top = (uint64_t)agp_top << 24;

	pa_config->system_aperture.fb_base = adev->gmc.fb_start;
	pa_config->system_aperture.fb_offset = adev->vm_manager.vram_base_offset;
	pa_config->system_aperture.fb_top = adev->gmc.fb_end;

	pa_config->gart_config.page_table_start_addr = page_table_start.quad_part << 12;
	pa_config->gart_config.page_table_end_addr = page_table_end.quad_part << 12;
	pa_config->gart_config.page_table_base_addr = page_table_base.quad_part;

	pa_config->is_hvm_enabled = adev->mode_info.gpu_vm_support;

}

struct amdgpu_stutter_quirk {
	u16 chip_vendor;
	u16 chip_device;
	u16 subsys_vendor;
	u16 subsys_device;
	u8 revision;
};

static const struct amdgpu_stutter_quirk amdgpu_stutter_quirk_list[] = {
	/* https://bugzilla.kernel.org/show_bug.cgi?id=214417 */
	{ 0x1002, 0x15dd, 0x1002, 0x15dd, 0xc8 },
	{ 0, 0, 0, 0, 0 },
};

static bool dm_should_disable_stutter(struct pci_dev *pdev)
{
	const struct amdgpu_stutter_quirk *p = amdgpu_stutter_quirk_list;

	while (p && p->chip_device != 0) {
		if (pdev->vendor == p->chip_vendor &&
		    pdev->device == p->chip_device &&
		    pdev->subsystem_vendor == p->subsys_vendor &&
		    pdev->subsystem_device == p->subsys_device &&
		    pdev->revision == p->revision) {
			return true;
		}
		++p;
	}
	return false;
}


void*
dm_allocate_gpu_mem(
		struct amdgpu_device *adev,
		enum dc_gpu_mem_alloc_type type,
		size_t size,
		long long *addr)
{
	struct dal_allocation *da;
	u32 domain = (type == DC_MEM_ALLOC_TYPE_GART) ?
		AMDGPU_GEM_DOMAIN_GTT : AMDGPU_GEM_DOMAIN_VRAM;
	int ret;

	da = kzalloc_obj(struct dal_allocation);
	if (!da)
		return NULL;

	ret = amdgpu_bo_create_kernel(adev, size, PAGE_SIZE,
				      domain, &da->bo,
				      &da->gpu_addr, &da->cpu_ptr);

	*addr = da->gpu_addr;

	if (ret) {
		kfree(da);
		return NULL;
	}

	/* add da to list in dm */
	list_add(&da->list, &adev->dm.da_list);

	return da->cpu_ptr;
}

void
dm_free_gpu_mem(
		struct amdgpu_device *adev,
		enum dc_gpu_mem_alloc_type type,
		void *pvMem)
{
	struct dal_allocation *da;

	/* walk the da list in DM */
	list_for_each_entry(da, &adev->dm.da_list, list) {
		if (pvMem == da->cpu_ptr) {
			amdgpu_bo_free_kernel(&da->bo, &da->gpu_addr, &da->cpu_ptr);
			list_del(&da->list);
			kfree(da);
			break;
		}
	}

}

static int amdgpu_dm_init_power_module(struct amdgpu_display_manager *dm)
{
	struct mod_power_init_params init_data[MAX_NUM_EDP];

	if (dm->num_of_edps == 0) {
		drm_dbg_driver(
			dm->ddev,
			"amdgpu: No eDP detected, skip initializing power module\n");
		return 0;
	}

	/* Initialize all the power module parameters */
	for (int i = 0; i < dm->num_of_edps; i++) {
		init_data[i].allow_psr_smu_optimizations =
			!!(amdgpu_dc_feature_mask & DC_PSR_ALLOW_SMU_OPT);
		init_data[i].allow_psr_multi_disp_optimizations =
			!!(amdgpu_dc_feature_mask & DC_PSR_ALLOW_MULTI_DISP_OPT);
		/* See dm_late_init */
		init_data[i].backlight_ramping_override = false;
		init_data[i].backlight_ramping_start = 0xCCCC;
		init_data[i].backlight_ramping_reduction = 0xCCCCCCCC;
		init_data[i].def_varibright_level = 0;
		init_data[i].abm_config_setting = 0;
		init_data[i].num_backlight_levels = 101;
		init_data[i].use_nits_based_brightness = false;
		init_data[i].panel_max_millinits = 0;
		init_data[i].panel_min_millinits = 0;
		init_data[i].disable_fractional_pwm =
			!(amdgpu_dc_feature_mask & DC_DISABLE_FRACTIONAL_PWM_MASK);
		init_data[i].use_custom_backlight_caps = false;
		init_data[i].custom_backlight_caps_config_no = 0;
		init_data[i].use_linear_backlight_curve = false;
		init_data[i].def_varibright_enable = 0;
		init_data[i].varibright_level = 0;
		/*
		 * Power module uses 16-bit backlight levels (0xFFFF max) rather
		 * than 8-bit(0XFF max)
		 */
		init_data[i].min_backlight_pwm =
			dm->backlight_caps[i].min_input_signal * 0x101;
		init_data[i].max_backlight_pwm =
			dm->backlight_caps[i].max_input_signal * 0x101;
		init_data[i].min_abm_backlight =
			dm->backlight_caps[i].min_input_signal * 0x101;

		/* Min backlight level after ABM reduction,  Don't allow below 1%
		 * 0xFFFF x 0.01 = 0x28F
		 */
		init_data[i].min_abm_backlight = (init_data[i].min_abm_backlight < 0x28F) ?
			0x28F : init_data[i].min_abm_backlight;
	}

	dm->power_module = mod_power_create(dm->dc, init_data, dm->num_of_edps);
	if (!dm->power_module) {
		drm_err(dm->ddev, "amdgpu: Error allocating memory for power module\n");
		return -ENOMEM;
	}

	mod_power_hw_init(dm->power_module);
	drm_dbg_driver(dm->ddev, "amdgpu: Power module init done\n");

	return 0;
}

static int amdgpu_dm_init(struct amdgpu_device *adev)
{
	struct dc_init_data init_data;
	struct dc_callback_init init_params;
	int r;

	adev->dm.ddev = adev_to_drm(adev);
	adev->dm.adev = adev;

	/* Zero all the fields */
	memset(&init_data, 0, sizeof(init_data));
	memset(&init_params, 0, sizeof(init_params));

	mutex_init(&adev->dm.dpia_aux_lock);
	mutex_init(&adev->dm.dc_lock);
	mutex_init(&adev->dm.audio_lock);

	spin_lock_init(&adev->dm.dmub_lock);

	if (amdgpu_dm_irq_init(adev)) {
		drm_err(adev_to_drm(adev), "failed to initialize DM IRQ support.\n");
		goto error;
	}

	/* special handling for early revisions of GC 11.5.4 */
	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(11, 5, 4))
		init_data.asic_id.chip_family = AMDGPU_FAMILY_GC_11_5_4;
	else
		init_data.asic_id.chip_family = adev->family;

	init_data.asic_id.pci_revision_id = adev->pdev->revision;
	init_data.asic_id.hw_internal_rev = adev->external_rev_id;
	init_data.asic_id.chip_id = adev->pdev->device;

	init_data.asic_id.vram_width = adev->gmc.vram_width;
	/* TODO: initialize init_data.asic_id.vram_type here!!!! */
	init_data.asic_id.atombios_base_address =
		adev->mode_info.atom_context->bios;

	init_data.driver = adev;

	/* cgs_device was created in dm_sw_init() */
	init_data.cgs_device = adev->dm.cgs_device;

	init_data.dce_environment = DCE_ENV_PRODUCTION_DRV;

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(2, 1, 0):
		switch (adev->dm.dmcub_fw_version) {
		case 0: /* development */
		case 0x1: /* linux-firmware.git hash 6d9f399 */
		case 0x01000000: /* linux-firmware.git hash 9a0b0f4 */
			init_data.flags.disable_dmcu = false;
			break;
		default:
			init_data.flags.disable_dmcu = true;
		}
		break;
	case IP_VERSION(2, 0, 3):
		init_data.flags.disable_dmcu = true;
		break;
	default:
		break;
	}

	/* APU support S/G display by default except:
	 * ASICs before Carrizo,
	 * RAVEN1 (Users reported stability issue)
	 */

	if (adev->asic_type < CHIP_CARRIZO) {
		init_data.flags.gpu_vm_support = false;
	} else if (adev->asic_type == CHIP_RAVEN) {
		if (adev->apu_flags & AMD_APU_IS_RAVEN)
			init_data.flags.gpu_vm_support = false;
		else
			init_data.flags.gpu_vm_support = (amdgpu_sg_display != 0);
	} else {
		if (amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(2, 0, 3))
			init_data.flags.gpu_vm_support = (amdgpu_sg_display == 1);
		else
			init_data.flags.gpu_vm_support =
				(amdgpu_sg_display != 0) && (adev->flags & AMD_IS_APU);
	}

	adev->mode_info.gpu_vm_support = init_data.flags.gpu_vm_support;

	if (amdgpu_dc_feature_mask & DC_FBC_MASK)
		init_data.flags.fbc_support = true;

	if (amdgpu_dc_feature_mask & DC_MULTI_MON_PP_MCLK_SWITCH_MASK)
		init_data.flags.multi_mon_pp_mclk_switch = true;

	if (amdgpu_dc_feature_mask & DC_DISABLE_FRACTIONAL_PWM_MASK)
		init_data.flags.disable_fractional_pwm = true;

	if (amdgpu_dc_feature_mask & DC_EDP_NO_POWER_SEQUENCING)
		init_data.flags.edp_no_power_sequencing = true;

	if (amdgpu_dc_feature_mask & DC_DISABLE_LTTPR_DP1_4A)
		init_data.flags.allow_lttpr_non_transparent_mode.bits.DP1_4A = true;
	if (amdgpu_dc_feature_mask & DC_DISABLE_LTTPR_DP2_0)
		init_data.flags.allow_lttpr_non_transparent_mode.bits.DP2_0 = true;

	if (amdgpu_dc_feature_mask & DC_FRL_MASK)
		init_data.flags.enable_frl = true;

	init_data.flags.seamless_boot_edp_requested = false;

	if (amdgpu_device_seamless_boot_supported(adev)) {
		init_data.flags.seamless_boot_edp_requested = true;
		init_data.flags.allow_seamless_boot_optimization = true;
		drm_dbg(adev->dm.ddev, "Seamless boot requested\n");
	}

	init_data.flags.enable_mipi_converter_optimization = true;

	init_data.dcn_reg_offsets = adev->reg_offset[DCE_HWIP][0];
	init_data.nbio_reg_offsets = adev->reg_offset[NBIO_HWIP][0];
	init_data.clk_reg_offsets = adev->reg_offset[CLK_HWIP][0];

	if (amdgpu_dc_debug_mask & DC_DISABLE_IPS)
		init_data.flags.disable_ips = DMUB_IPS_DISABLE_ALL;
	else if (amdgpu_dc_debug_mask & DC_DISABLE_IPS_DYNAMIC)
		init_data.flags.disable_ips = DMUB_IPS_DISABLE_DYNAMIC;
	else if (amdgpu_dc_debug_mask & DC_DISABLE_IPS2_DYNAMIC)
		init_data.flags.disable_ips = DMUB_IPS_RCG_IN_ACTIVE_IPS2_IN_OFF;
	else if (amdgpu_dc_debug_mask & DC_FORCE_IPS_ENABLE)
		init_data.flags.disable_ips = DMUB_IPS_ENABLE;
	else
		init_data.flags.disable_ips = dm_get_default_ips_mode(adev);

	init_data.flags.disable_ips_in_vpb = 0;

	/* DCN35 and above supports dynamic DTBCLK switch */
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) >= IP_VERSION(3, 5, 0))
		init_data.flags.allow_0_dtb_clk = true;

	/* Enable DWB for tested platforms only */
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) >= IP_VERSION(3, 0, 0))
		init_data.num_virtual_links = 1;

	/* DCN42 and above dpia switch to unified link training path */
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) >= IP_VERSION(4, 2, 0)) {
		init_data.flags.consolidated_dpia_dp_lt = true;
		init_data.flags.enable_dpia_pre_training = true;
		init_data.flags.unify_link_enc_assignment = true;
		init_data.flags.usb4_bw_alloc_support = true;
	}
	retrieve_dmi_info(&adev->dm);
	if (adev->dm.edp0_on_dp1_quirk)
		init_data.flags.support_edp0_on_dp1 = true;

	if (adev->dm.bb_from_dmub)
		init_data.bb_from_dmub = adev->dm.bb_from_dmub;
	else
		init_data.bb_from_dmub = NULL;

	/* Display Core create. */
	adev->dm.dc = dc_create(&init_data);

	if (adev->dm.dc) {
		drm_info(adev_to_drm(adev), "Display Core v%s initialized on %s\n", DC_VER,
			 dce_version_to_string(adev->dm.dc->ctx->dce_version));
	} else {
		drm_info(adev_to_drm(adev), "Display Core failed to initialize with v%s!\n", DC_VER);
		goto error;
	}

	if (amdgpu_dc_debug_mask & DC_DISABLE_PIPE_SPLIT) {
		adev->dm.dc->debug.force_single_disp_pipe_split = false;
		adev->dm.dc->debug.pipe_split_policy = MPC_SPLIT_AVOID;
	}

	if (adev->asic_type != CHIP_CARRIZO && adev->asic_type != CHIP_STONEY)
		adev->dm.dc->debug.disable_stutter = amdgpu_pp_feature_mask & PP_STUTTER_MODE ? false : true;
	if (dm_should_disable_stutter(adev->pdev))
		adev->dm.dc->debug.disable_stutter = true;

	if (amdgpu_dc_debug_mask & DC_DISABLE_STUTTER)
		adev->dm.dc->debug.disable_stutter = true;

	if (amdgpu_dc_debug_mask & DC_DISABLE_DSC)
		adev->dm.dc->debug.disable_dsc = true;

	if (amdgpu_dc_debug_mask & DC_DISABLE_CLOCK_GATING)
		adev->dm.dc->debug.disable_clock_gate = true;

	if (amdgpu_dc_debug_mask & DC_FORCE_SUBVP_MCLK_SWITCH)
		adev->dm.dc->debug.force_subvp_mclk_switch = true;

	if (amdgpu_dc_debug_mask & DC_DISABLE_SUBVP_FAMS) {
		adev->dm.dc->debug.force_disable_subvp = true;
		adev->dm.dc->debug.fams2_config.bits.enable = false;
	}

	if (amdgpu_dc_debug_mask & DC_ENABLE_DML2) {
		adev->dm.dc->debug.using_dml2 = true;
		adev->dm.dc->debug.using_dml21 = true;
	}

	if (amdgpu_dc_debug_mask & DC_HDCP_LC_FORCE_FW_ENABLE)
		adev->dm.dc->debug.hdcp_lc_force_fw_enable = true;

	if (amdgpu_dc_debug_mask & DC_HDCP_LC_ENABLE_SW_FALLBACK)
		adev->dm.dc->debug.hdcp_lc_enable_sw_fallback = true;

	if (amdgpu_dc_debug_mask & DC_SKIP_DETECTION_LT)
		adev->dm.dc->debug.skip_detection_link_training = true;

	adev->dm.dc->debug.visual_confirm = amdgpu_dc_visual_confirm;

	/* TODO: Remove after DP2 receiver gets proper support of Cable ID feature */
	adev->dm.dc->debug.ignore_cable_id = true;

	if (adev->dm.dc->caps.dp_hdmi21_pcon_support)
		drm_info(adev_to_drm(adev), "DP-HDMI FRL PCON supported\n");

	r = dm_dmub_hw_init(adev);
	if (r) {
		drm_err(adev_to_drm(adev), "DMUB interface failed to initialize: status=%d\n", r);
		goto error;
	}

	dc_hardware_init(adev->dm.dc);

	/* GOP/vBIOS may leave an OPTC enabled for a display present at power-on
	 * but no longer driven (e.g. an external DP unplugged at boot). Such a
	 * dangling pipe keeps DCN out of idle and blocks s0i3. Power it down
	 * here when nothing needs to be preserved.
	 */
	if (adev->flags & AMD_IS_APU)
		dc_disable_dangling_timing_generators(adev->dm.dc);

	adev->dm.hpd_rx_offload_wq = amdgpu_dm_hpd_rx_irq_create_workqueue(adev);
	if (!adev->dm.hpd_rx_offload_wq) {
		drm_err(adev_to_drm(adev), "failed to create hpd rx offload workqueue.\n");
		goto error;
	}

	if ((adev->flags & AMD_IS_APU) && (adev->asic_type >= CHIP_CARRIZO)) {
		struct dc_phy_addr_space_config pa_config;

		mmhub_read_system_context(adev, &pa_config);

		// Call the DC init_memory func
		dc_setup_system_context(adev->dm.dc, &pa_config);
	}

	adev->dm.freesync_module = mod_freesync_create(adev->dm.dc);
	if (!adev->dm.freesync_module) {
		drm_err(adev_to_drm(adev),
		"failed to initialize freesync_module.\n");
	} else
		drm_dbg_driver(adev_to_drm(adev), "freesync_module init done %p.\n",
				adev->dm.freesync_module);

	amdgpu_dm_init_color_mod();

	if (adev->dm.dc->caps.max_links > 0) {
		adev->dm.vblank_control_workqueue =
			create_singlethread_workqueue("dm_vblank_control_workqueue");
		if (!adev->dm.vblank_control_workqueue)
			drm_err(adev_to_drm(adev), "failed to initialize vblank_workqueue.\n");
	}

	if (adev->dm.dc->caps.ips_support &&
	    adev->dm.dc->config.disable_ips != DMUB_IPS_DISABLE_ALL)
		adev->dm.idle_workqueue = idle_create_workqueue(adev);

	if (adev->dm.dc->caps.max_links > 0 && adev->family >= AMDGPU_FAMILY_RV) {
		adev->dm.hdcp_workqueue = hdcp_create_workqueue(adev, &init_params.cp_psp, adev->dm.dc);

		if (!adev->dm.hdcp_workqueue)
			drm_err(adev_to_drm(adev), "failed to initialize hdcp_workqueue.\n");
		else
			drm_dbg_driver(adev_to_drm(adev),
				       "hdcp_workqueue init done %p.\n",
				       adev->dm.hdcp_workqueue);

		dc_init_callbacks(adev->dm.dc, &init_params);
	}
	if (adev->dm.dc->caps.max_links > 0) {
		adev->dm.hdmi_frl_status_polling_wq =
			create_singlethread_workqueue("hdmi_frl_status_polling_workqueue");
		if (!adev->dm.hdmi_frl_status_polling_wq)
			drm_err(adev_to_drm(adev), "failed to initialize hdmi_frl_status_polling_workqueue\n");
	}
	if (dc_is_dmub_outbox_supported(adev->dm.dc)) {
		init_completion(&adev->dm.dmub_aux_transfer_done);
		adev->dm.dmub_notify = kzalloc_obj(struct dmub_notification);
		if (!adev->dm.dmub_notify) {
			drm_info(adev_to_drm(adev), "fail to allocate adev->dm.dmub_notify");
			goto error;
		}

		adev->dm.delayed_hpd_wq = create_singlethread_workqueue("amdgpu_dm_hpd_wq");
		if (!adev->dm.delayed_hpd_wq) {
			drm_err(adev_to_drm(adev), "failed to create hpd offload workqueue.\n");
			goto error;
		}

		amdgpu_dm_outbox_init(adev);
		if (!dm_register_dmub_notify_callback(adev, DMUB_NOTIFICATION_AUX_REPLY,
			dm_dmub_aux_setconfig_callback, false)) {
			drm_err(adev_to_drm(adev), "fail to register dmub aux callback");
			goto error;
		}

		for (size_t i = 0; i < ARRAY_SIZE(adev->dm.fused_io); i++)
			init_completion(&adev->dm.fused_io[i].replied);

		if (!dm_register_dmub_notify_callback(adev, DMUB_NOTIFICATION_FUSED_IO,
			dm_dmub_aux_fused_io_callback, false)) {
			drm_err(adev_to_drm(adev), "fail to register dmub fused io callback");
			goto error;
		}
		/* Enable outbox notification only after IRQ handlers are registered and DMUB is alive.
		 * It is expected that DMUB will resend any pending notifications at this point. Note
		 * that hpd and hpd_irq handler registration are deferred to
		 * amdgpu_dm_register_hpd_handlers() to align legacy interface initialization
		 * sequence. Connection status will be proactivly detected once in the
		 * amdgpu_dm_initialize_drm_device.
		 */
		dc_enable_dmub_outbox(adev->dm.dc);

		/* DPIA trace goes to dmesg logs only if outbox is enabled */
		if (amdgpu_dc_debug_mask & DC_ENABLE_DPIA_TRACE)
			dc_dmub_srv_enable_dpia_trace(adev->dm.dc);
	}

	if (amdgpu_dm_initialize_drm_device(adev)) {
		drm_err(adev_to_drm(adev),
		"failed to initialize sw for display support.\n");
		goto error;
	}

	if (amdgpu_dm_init_power_module(&adev->dm))
		goto error;

	/* create fake encoders for MST */
	dm_dp_create_fake_mst_encoders(adev);

	/* TODO: Add_display_info? */

	/* TODO use dynamic cursor width */
	adev_to_drm(adev)->mode_config.cursor_width = adev->dm.dc->caps.max_cursor_size;
	adev_to_drm(adev)->mode_config.cursor_height = adev->dm.dc->caps.max_cursor_size;

	if (drm_vblank_init(adev_to_drm(adev), adev->dm.display_indexes_num)) {
		drm_err(adev_to_drm(adev),
		"failed to initialize vblank for display support.\n");
		goto error;
	}

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	amdgpu_dm_crtc_secure_display_create_contexts(adev);
	if (!adev->dm.secure_display_ctx.crtc_ctx)
		drm_err(adev_to_drm(adev), "failed to initialize secure display contexts.\n");

	if (amdgpu_ip_version(adev, DCE_HWIP, 0) >= IP_VERSION(4, 0, 1))
		adev->dm.secure_display_ctx.support_mul_roi = true;

#endif

	drm_dbg_driver(adev_to_drm(adev), "KMS initialized.\n");

	return 0;
error:
	amdgpu_dm_fini(adev);

	return -EINVAL;
}

static int amdgpu_dm_early_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	amdgpu_dm_audio_fini(adev);

	return 0;
}

static void amdgpu_dm_fini(struct amdgpu_device *adev)
{
	int i;

	if (adev->dm.vblank_control_workqueue) {
		destroy_workqueue(adev->dm.vblank_control_workqueue);
		adev->dm.vblank_control_workqueue = NULL;
	}

	if (adev->dm.idle_workqueue) {
		if (adev->dm.idle_workqueue->running) {
			adev->dm.idle_workqueue->enable = false;
			flush_work(&adev->dm.idle_workqueue->work);
		}

		kfree(adev->dm.idle_workqueue);
		adev->dm.idle_workqueue = NULL;
	}

	/*
	 * Disable ISM before dc_destroy() invalidates dm->dc.
	 *
	 * Quiesce workers first without dc_lock (they take dc_lock
	 * themselves, so syncing under it would deadlock), then drive the
	 * FSM back to FULL_POWER_RUNNING under dc_lock.
	 */
	amdgpu_dm_ism_disable(&adev->dm);
	scoped_guard(mutex, &adev->dm.dc_lock)
		amdgpu_dm_ism_force_full_power(&adev->dm);

	amdgpu_dm_destroy_drm_device(&adev->dm);

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	if (adev->dm.secure_display_ctx.crtc_ctx) {
		for (i = 0; i < adev->mode_info.num_crtc; i++) {
			if (adev->dm.secure_display_ctx.crtc_ctx[i].crtc) {
				flush_work(&adev->dm.secure_display_ctx.crtc_ctx[i].notify_ta_work);
				flush_work(&adev->dm.secure_display_ctx.crtc_ctx[i].forward_roi_work);
			}
		}
		kfree(adev->dm.secure_display_ctx.crtc_ctx);
		adev->dm.secure_display_ctx.crtc_ctx = NULL;
	}
#endif
	if (adev->dm.hdcp_workqueue) {
		hdcp_destroy(&adev->dev->kobj, adev->dm.hdcp_workqueue);
		adev->dm.hdcp_workqueue = NULL;
	}

	if (adev->dm.dc) {
		dc_deinit_callbacks(adev->dm.dc);
		dc_dmub_srv_destroy(&adev->dm.dc->ctx->dmub_srv);
		if (dc_enable_dmub_notifications(adev->dm.dc)) {
			kfree(adev->dm.dmub_notify);
			adev->dm.dmub_notify = NULL;
			destroy_workqueue(adev->dm.delayed_hpd_wq);
			adev->dm.delayed_hpd_wq = NULL;
		}
	}

	if (adev->dm.dmub_bo)
		amdgpu_bo_free_kernel(&adev->dm.dmub_bo,
				      &adev->dm.dmub_bo_gpu_addr,
				      &adev->dm.dmub_bo_cpu_addr);

	if (adev->dm.boot_time_crc_info.bo_ptr)
		amdgpu_bo_free_kernel(&adev->dm.boot_time_crc_info.bo_ptr,
					&adev->dm.boot_time_crc_info.gpu_addr,
					&adev->dm.boot_time_crc_info.cpu_addr);

	if (adev->dm.hpd_rx_offload_wq && adev->dm.dc) {
		for (i = 0; i < adev->dm.dc->caps.max_links; i++) {
			if (adev->dm.hpd_rx_offload_wq[i].wq) {
				destroy_workqueue(adev->dm.hpd_rx_offload_wq[i].wq);
				adev->dm.hpd_rx_offload_wq[i].wq = NULL;
			}
		}

		kfree(adev->dm.hpd_rx_offload_wq);
		adev->dm.hpd_rx_offload_wq = NULL;
	}

	amdgpu_dm_irq_fini(adev);

	/* DC Destroy TODO: Replace destroy DAL */
	if (adev->dm.dc)
		dc_destroy(&adev->dm.dc);

	if (adev->dm.cgs_device) {
		amdgpu_cgs_destroy_device(adev->dm.cgs_device);
		adev->dm.cgs_device = NULL;
	}
	if (adev->dm.freesync_module) {
		mod_freesync_destroy(adev->dm.freesync_module);
		adev->dm.freesync_module = NULL;
	}

	if (adev->dm.power_module) {
		mod_power_destroy(adev->dm.power_module);
		adev->dm.power_module = NULL;
	}
	mutex_destroy(&adev->dm.audio_lock);
	mutex_destroy(&adev->dm.dc_lock);
	mutex_destroy(&adev->dm.dpia_aux_lock);
}

static int load_dmcu_fw(struct amdgpu_device *adev)
{
	const char *fw_name_dmcu = NULL;
	int r;
	const struct dmcu_firmware_header_v1_0 *hdr;

	switch (adev->asic_type) {
#if defined(CONFIG_DRM_AMD_DC_SI)
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_OLAND:
#endif
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
	case CHIP_NAVI12:
		fw_name_dmcu = FIRMWARE_NAVI12_DMCU;
		break;
	case CHIP_RAVEN:
		if (ASICREV_IS_PICASSO(adev->external_rev_id))
			fw_name_dmcu = FIRMWARE_RAVEN_DMCU;
		else if (ASICREV_IS_RAVEN2(adev->external_rev_id))
			fw_name_dmcu = FIRMWARE_RAVEN_DMCU;
		else
			return 0;
		break;
	default:
		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(2, 0, 2):
		case IP_VERSION(2, 0, 3):
		case IP_VERSION(2, 0, 0):
		case IP_VERSION(2, 1, 0):
		case IP_VERSION(3, 0, 0):
		case IP_VERSION(3, 0, 2):
		case IP_VERSION(3, 0, 3):
		case IP_VERSION(3, 0, 1):
		case IP_VERSION(3, 1, 2):
		case IP_VERSION(3, 1, 3):
		case IP_VERSION(3, 1, 4):
		case IP_VERSION(3, 1, 5):
		case IP_VERSION(3, 1, 6):
		case IP_VERSION(3, 2, 0):
		case IP_VERSION(3, 2, 1):
		case IP_VERSION(3, 5, 0):
		case IP_VERSION(3, 5, 1):
		case IP_VERSION(3, 6, 0):
		case IP_VERSION(4, 0, 1):
		case IP_VERSION(4, 2, 0):
		case IP_VERSION(4, 2, 1):
			return 0;
		default:
			break;
		}
		drm_err(adev_to_drm(adev), "Unsupported ASIC type: 0x%X\n", adev->asic_type);
		return -EINVAL;
	}

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		drm_dbg_kms(adev_to_drm(adev), "dm: DMCU firmware not supported on direct or SMU loading\n");
		return 0;
	}

	r = amdgpu_ucode_request(adev, &adev->dm.fw_dmcu, AMDGPU_UCODE_REQUIRED,
				 "%s", fw_name_dmcu);
	if (r == -ENODEV) {
		/* DMCU firmware is not necessary, so don't raise a fuss if it's missing */
		drm_dbg_kms(adev_to_drm(adev), "dm: DMCU firmware not found\n");
		adev->dm.fw_dmcu = NULL;
		return 0;
	}
	if (r) {
		drm_err(adev_to_drm(adev), "amdgpu_dm: Can't validate firmware \"%s\"\n",
			fw_name_dmcu);
		amdgpu_ucode_release(&adev->dm.fw_dmcu);
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

	drm_dbg_kms(adev_to_drm(adev), "PSP loading DMCU firmware\n");

	return 0;
}

static int dm_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	adev->dm.cgs_device = amdgpu_cgs_create_device(adev);

	if (!adev->dm.cgs_device) {
		drm_err(adev_to_drm(adev), "failed to create cgs device.\n");
		return -EINVAL;
	}

	/* Moved from dm init since we need to use allocations for storing bounding box data */
	INIT_LIST_HEAD(&adev->dm.da_list);

	r = dm_dmub_sw_init(adev);
	if (r)
		return r;

	return load_dmcu_fw(adev);
}

static int dm_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct dal_allocation *da;

	list_for_each_entry(da, &adev->dm.da_list, list) {
		if (adev->dm.bb_from_dmub == (void *) da->cpu_ptr) {
			amdgpu_bo_free_kernel(&da->bo, &da->gpu_addr, &da->cpu_ptr);
			list_del(&da->list);
			kfree(da);
			adev->dm.bb_from_dmub = NULL;
			break;
		}
	}


	kfree(adev->dm.dmub_fb_info);
	adev->dm.dmub_fb_info = NULL;

	if (adev->dm.dmub_srv) {
		dmub_srv_destroy(adev->dm.dmub_srv);
		kfree(adev->dm.dmub_srv);
		adev->dm.dmub_srv = NULL;
	}

	amdgpu_ucode_release(&adev->dm.dmub_fw);
	amdgpu_ucode_release(&adev->dm.fw_dmcu);

	return 0;
}


static void amdgpu_dm_boot_time_crc_init(struct amdgpu_device *adev)
{
	struct dm_boot_time_crc_info *bootcrc_info = NULL;
	struct dmub_srv *dmub = NULL;
	union dmub_fw_boot_options option = {0};
	int ret = 0;
	const uint32_t fb_size = 3 * 1024 * 1024;	/* 3MB for DCC pattern */

	if (!adev || !adev->dm.dc || !adev->dm.dc->ctx ||
		!adev->dm.dc->ctx->dmub_srv) {
		return;
	}

	dmub = adev->dm.dc->ctx->dmub_srv->dmub;
	bootcrc_info = &adev->dm.boot_time_crc_info;

	if (!dmub || !dmub->hw_funcs.get_fw_boot_option) {
		drm_dbg(adev_to_drm(adev), "failed to init boot time crc buffer\n");
		return;
	}

	option = dmub->hw_funcs.get_fw_boot_option(dmub);

	/* Return if boot time CRC is not enabled */
	if (option.bits.bootcrc_en_at_S0i3 == 0)
		return;

	/* Create a buffer for boot time CRC */
	ret = amdgpu_bo_create_kernel(adev, fb_size, PAGE_SIZE,
		AMDGPU_GEM_DOMAIN_VRAM | AMDGPU_GEM_DOMAIN_GTT,
		&bootcrc_info->bo_ptr,
		&bootcrc_info->gpu_addr,
		&bootcrc_info->cpu_addr);

	if (ret) {
		drm_dbg(adev_to_drm(adev), "failed to create boot time crc buffer\n");
	} else {
		bootcrc_info->size = fb_size;

		drm_dbg(adev_to_drm(adev), "boot time crc buffer created addr 0x%llx, size %u\n",
			bootcrc_info->gpu_addr, bootcrc_info->size);

		/* Send the buffer info to DMUB */
		dc_dmub_srv_boot_time_crc_init(adev->dm.dc,
			bootcrc_info->gpu_addr, bootcrc_info->size);
	}
}

static int dm_late_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	struct dmcu_iram_parameters params;
	unsigned int linear_lut[16];
	int i;
	struct dmcu *dmcu = NULL;

	dmcu = adev->dm.dc->res_pool->dmcu;

	/* Init the boot time CRC (skip in resume) */
	if ((adev->in_suspend == 0) &&
		(amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(3, 6, 0)))
		amdgpu_dm_boot_time_crc_init(adev);

	for (i = 0; i < 16; i++)
		linear_lut[i] = 0xFFFF * i / 15;

	params.set = 0;
	params.backlight_ramping_override = false;
	params.backlight_ramping_start = 0xCCCC;
	params.backlight_ramping_reduction = 0xCCCCCCCC;
	params.backlight_lut_array_size = 16;
	params.backlight_lut_array = linear_lut;

	/* Min backlight level after ABM reduction,  Don't allow below 1%
	 * 0xFFFF x 0.01 = 0x28F
	 */
	params.min_abm_backlight = 0x28F;
	/* In the case where abm is implemented on dmcub,
	 * dmcu object will be null.
	 * ABM 2.4 and up are implemented on dmcub.
	 */
	if (dmcu) {
		if (!dmcu_load_iram(dmcu, params))
			return -EINVAL;
	} else if (adev->dm.dc->ctx->dmub_srv) {
		struct dc_link *edp_links[MAX_NUM_EDP];
		int edp_num;

		dc_get_edp_links(adev->dm.dc, edp_links, &edp_num);
		for (i = 0; i < edp_num; i++) {
			if (!dmub_init_abm_config(adev->dm.dc->res_pool, params, i))
				return -EINVAL;
		}
	}

	return amdgpu_dm_detect_mst_link_for_all_connectors(adev_to_drm(adev));
}

static void resume_mst_branch_status(struct drm_dp_mst_topology_mgr *mgr)
{
	u8 buf[UUID_SIZE];
	guid_t guid;
	int ret;

	mutex_lock(&mgr->lock);
	if (!mgr->mst_primary)
		goto out_fail;

	if (drm_dp_read_dpcd_caps(mgr->aux, mgr->dpcd) < 0) {
		drm_dbg_kms(mgr->dev, "dpcd read failed - undocked during suspend?\n");
		goto out_fail;
	}

	ret = drm_dp_dpcd_writeb(mgr->aux, DP_MSTM_CTRL,
				 DP_MST_EN |
				 DP_UP_REQ_EN |
				 DP_UPSTREAM_IS_SRC);
	if (ret < 0) {
		drm_dbg_kms(mgr->dev, "mst write failed - undocked during suspend?\n");
		goto out_fail;
	}

	/* Some hubs forget their guids after they resume */
	ret = drm_dp_dpcd_read(mgr->aux, DP_GUID, buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		drm_dbg_kms(mgr->dev, "dpcd read failed - undocked during suspend?\n");
		goto out_fail;
	}

	import_guid(&guid, buf);

	if (guid_is_null(&guid)) {
		guid_gen(&guid);
		export_guid(buf, &guid);

		ret = drm_dp_dpcd_write(mgr->aux, DP_GUID, buf, sizeof(buf));

		if (ret != sizeof(buf)) {
			drm_dbg_kms(mgr->dev, "check mstb guid failed - undocked during suspend?\n");
			goto out_fail;
		}
	}

	guid_copy(&mgr->mst_primary->guid, &guid);

out_fail:
	mutex_unlock(&mgr->lock);
}

static void s3_handle_mst(struct drm_device *dev, bool suspend)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	struct drm_dp_mst_topology_mgr *mgr;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->dc_link->type != dc_connection_mst_branch ||
		    aconnector->mst_root)
			continue;

		mgr = &aconnector->mst_mgr;

		if (suspend) {
			drm_dp_mst_topology_mgr_suspend(mgr);
		} else {
			/* if extended timeout is supported in hardware,
			 * default to LTTPR timeout (3.2ms) first as a W/A for DP link layer
			 * CTS 4.2.1.1 regression introduced by CTS specs requirement update.
			 */
			try_to_configure_aux_timeout(aconnector->dc_link->ddc, LINK_AUX_DEFAULT_LTTPR_TIMEOUT_PERIOD);
			if (!dp_is_lttpr_present(aconnector->dc_link))
				try_to_configure_aux_timeout(aconnector->dc_link->ddc, LINK_AUX_DEFAULT_TIMEOUT_PERIOD);

			/* TODO: move resume_mst_branch_status() into drm mst resume again
			 * once topology probing work is pulled out from mst resume into mst
			 * resume 2nd step. mst resume 2nd step should be called after old
			 * state getting restored (i.e. drm_atomic_helper_resume()).
			 */
			resume_mst_branch_status(mgr);
		}
	}
	drm_connector_list_iter_end(&iter);
}

static int amdgpu_dm_smu_write_watermarks_table(struct amdgpu_device *adev)
{
	int ret = 0;

	/* This interface is for dGPU Navi1x.Linux dc-pplib interface depends
	 * on window driver dc implementation.
	 * For Navi1x, clock settings of dcn watermarks are fixed. the settings
	 * should be passed to smu during boot up and resume from s3.
	 * boot up: dc calculate dcn watermark clock settings within dc_create,
	 * dcn20_resource_construct
	 * then call pplib functions below to pass the settings to smu:
	 * smu_set_watermarks_for_clock_ranges
	 * smu_set_watermarks_table
	 * navi10_set_watermarks_table
	 * smu_write_watermarks_table
	 *
	 * For Renoir, clock settings of dcn watermark are also fixed values.
	 * dc has implemented different flow for window driver:
	 * dc_hardware_init / dc_set_power_state
	 * dcn10_init_hw
	 * notify_wm_ranges
	 * set_wm_ranges
	 * -- Linux
	 * smu_set_watermarks_for_clock_ranges
	 * renoir_set_watermarks_table
	 * smu_write_watermarks_table
	 *
	 * For Linux,
	 * dc_hardware_init -> amdgpu_dm_init
	 * dc_set_power_state --> dm_resume
	 *
	 * therefore, this function apply to navi10/12/14 but not Renoir
	 * *
	 */
	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(2, 0, 2):
	case IP_VERSION(2, 0, 0):
		break;
	default:
		return 0;
	}

	ret = amdgpu_dpm_write_watermarks_table(adev);
	if (ret) {
		drm_err(adev_to_drm(adev), "Failed to update WMTABLE!\n");
		return ret;
	}

	return 0;
}

static int dm_oem_i2c_hw_init(struct amdgpu_device *adev)
{
	struct amdgpu_display_manager *dm = &adev->dm;
	struct amdgpu_i2c_adapter *oem_i2c;
	struct ddc_service *oem_ddc_service;
	int r;

	oem_ddc_service = dc_get_oem_i2c_device(adev->dm.dc);
	if (oem_ddc_service) {
		oem_i2c = amdgpu_dm_create_i2c(oem_ddc_service, true);
		if (!oem_i2c) {
			drm_info(adev_to_drm(adev), "Failed to create oem i2c adapter data\n");
			return -ENOMEM;
		}

		r = devm_i2c_add_adapter(adev->dev, &oem_i2c->base);
		if (r) {
			drm_info(adev_to_drm(adev), "Failed to register oem i2c\n");
			kfree(oem_i2c);
			return r;
		}
		dm->oem_i2c = oem_i2c;
	}

	return 0;
}

/**
 * dm_hw_init() - Initialize DC device
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
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
static int dm_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	adev->dm.i2c_devres_group = devres_open_group(adev->dev, NULL, GFP_KERNEL);
	if (!adev->dm.i2c_devres_group)
		return -ENOMEM;

	/* Create DAL display manager */
	r = amdgpu_dm_init(adev);
	if (r)
		goto err_release_i2c;
	amdgpu_dm_hpd_init(adev);

	r = dm_oem_i2c_hw_init(adev);
	if (r)
		drm_info(adev_to_drm(adev), "Failed to add OEM i2c bus\n");

	devres_close_group(adev->dev, adev->dm.i2c_devres_group);
	return 0;

err_release_i2c:
	devres_release_group(adev->dev, adev->dm.i2c_devres_group);
	return r;
}

/**
 * dm_hw_fini() - Teardown DC device
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Teardown components within &struct amdgpu_display_manager that require
 * cleanup. This involves cleaning up the DRM device, DC, and any modules that
 * were loaded. Also flush IRQ workqueues and disable them.
 */
static int dm_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	if (adev->dm.i2c_devres_group)
		devres_release_group(adev->dev, adev->dm.i2c_devres_group);

	amdgpu_dm_hpd_fini(adev);

	amdgpu_dm_fini(adev);
	return 0;
}


static void dm_gpureset_toggle_interrupts(struct amdgpu_device *adev,
				 struct dc_state *state, bool enable)
{
	enum dc_irq_source irq_source;
	struct amdgpu_crtc *acrtc;
	int rc = -EBUSY;
	int i = 0;

	for (i = 0; i < state->stream_count; i++) {
		acrtc = amdgpu_dm_get_crtc_by_otg_inst(
				adev, state->stream_status[i].primary_otg_inst);

		if (acrtc && state->stream_status[i].plane_count != 0 &&
		    amdgpu_ip_version(adev, DCE_HWIP, 0) == 0) {
			irq_source = IRQ_TYPE_PFLIP + acrtc->otg_inst;
			rc = dc_interrupt_set(adev->dm.dc, irq_source, enable) ? 0 : -EBUSY;
			if (rc)
				drm_warn(adev_to_drm(adev), "Failed to %s pflip interrupts\n",
					 enable ? "enable" : "disable");

			if (dc_supports_vrr(adev->dm.dc->ctx->dce_version)) {
				if (enable) {
					if (amdgpu_dm_crtc_vrr_active(
							to_dm_crtc_state(acrtc->base.state)))
						rc = amdgpu_dm_crtc_set_vupdate_irq(
							&acrtc->base, true);
				} else
					rc = amdgpu_dm_crtc_set_vupdate_irq(
							&acrtc->base, false);

				if (rc)
					drm_warn(adev_to_drm(adev), "Failed to %sable vupdate interrupt\n",
						enable ? "en" : "dis");
			}

			irq_source = IRQ_TYPE_VBLANK + acrtc->otg_inst;
			/* During gpu-reset we disable and then enable vblank irq, so
			 * don't use amdgpu_irq_get/put() to avoid refcount change.
			 */
			if (!dc_interrupt_set(adev->dm.dc, irq_source, enable))
				drm_warn(adev_to_drm(adev), "Failed to %sable vblank interrupt\n", enable ? "en" : "dis");

		} else if (acrtc && state->stream_status[i].plane_count != 0) {
			/* DCN only needs to toggle VUPDATE_NO_LOCK */
			rc = amdgpu_dm_crtc_set_vupdate_irq(&acrtc->base, enable);
			if (rc)
				drm_warn(adev_to_drm(adev), "Failed to %sable vupdate interrupt\n",
					 enable ? "en" : "dis");
		}
	}

}

DEFINE_FREE(state_release, struct dc_state *, if (_T) dc_state_release(_T))

static enum dc_status amdgpu_dm_commit_zero_streams(struct dc *dc)
{
	struct dc_state *context __free(state_release) = NULL;
	int i;
	struct dc_stream_state *del_streams[MAX_PIPES];
	int del_streams_count = 0;
	struct dc_commit_streams_params params = {};

	memset(del_streams, 0, sizeof(del_streams));

	context = dc_state_create_current_copy(dc);
	if (context == NULL)
		return DC_ERROR_UNEXPECTED;

	/* First remove from context all streams */
	for (i = 0; i < context->stream_count; i++) {
		struct dc_stream_state *stream = context->streams[i];

		del_streams[del_streams_count++] = stream;
	}

	/* Remove all planes for removed streams and then remove the streams */
	for (i = 0; i < del_streams_count; i++) {
		enum dc_status res;

		if (!dc_state_rem_all_planes_for_stream(dc, del_streams[i], context))
			return DC_FAIL_DETACH_SURFACES;

		res = dc_state_remove_stream(dc, context, del_streams[i]);
		if (res != DC_OK)
			return res;
	}

	params.streams = context->streams;
	params.stream_count = context->stream_count;

	return dc_commit_streams(dc, &params);
}

static int dm_cache_state(struct amdgpu_device *adev)
{
	int r;

	adev->dm.cached_state = drm_atomic_helper_suspend(adev_to_drm(adev));
	if (IS_ERR(adev->dm.cached_state)) {
		r = PTR_ERR(adev->dm.cached_state);
		adev->dm.cached_state = NULL;
	}

	return adev->dm.cached_state ? 0 : r;
}

static void dm_destroy_cached_state(struct amdgpu_device *adev)
{
	struct amdgpu_display_manager *dm = &adev->dm;
	struct drm_device *ddev = adev_to_drm(adev);
	struct dm_plane_state *dm_new_plane_state;
	struct drm_plane_state *new_plane_state;
	struct dm_crtc_state *dm_new_crtc_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	int i;

	if (!dm->cached_state)
		return;

	/* Force mode set in atomic commit */
	for_each_new_crtc_in_state(dm->cached_state, crtc, new_crtc_state, i) {
		new_crtc_state->active_changed = true;
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		reset_freesync_config_for_crtc(dm_new_crtc_state);
	}

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
		dm_new_crtc_state->base.color_mgmt_changed = true;
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
}

static int dm_suspend(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_display_manager *dm = &adev->dm;

	if (amdgpu_in_reset(adev)) {
		enum dc_status res;

		/* Quiesce ISM workers before taking dc_lock (workers take
		 * dc_lock themselves; syncing under it would deadlock).
		 */
		amdgpu_dm_ism_disable(dm);

		mutex_lock(&dm->dc_lock);

		amdgpu_dm_ism_force_full_power(dm);
		dc_allow_idle_optimizations(adev->dm.dc, false);

		dm->cached_dc_state = dc_state_create_copy(dm->dc->current_state);

		if (dm->cached_dc_state)
			dm_gpureset_toggle_interrupts(adev, dm->cached_dc_state, false);

		res = amdgpu_dm_commit_zero_streams(dm->dc);
		if (res != DC_OK) {
			drm_err(adev_to_drm(adev), "Failed to commit zero streams: %d\n", res);
			return -EINVAL;
		}

		amdgpu_dm_irq_suspend(adev);

		amdgpu_dm_hpd_rx_irq_work_suspend(dm);

		return 0;
	}

	if (!adev->dm.cached_state) {
		int r = dm_cache_state(adev);

		if (r)
			return r;
	}

	amdgpu_dm_s3_handle_hdmi_cec(adev_to_drm(adev), true);

	s3_handle_mst(adev_to_drm(adev), true);

	amdgpu_dm_irq_suspend(adev);

	/*
	 * Quiesce ISM workers before taking dc_lock (workers take dc_lock
	 * themselves; syncing under it would deadlock).
	 */
	amdgpu_dm_ism_disable(dm);
	scoped_guard(mutex, &dm->dc_lock)
		amdgpu_dm_ism_force_full_power(dm);

	amdgpu_dm_hpd_rx_irq_work_suspend(dm);

	dc_set_power_state(dm->dc, DC_ACPI_CM_POWER_STATE_D3);

	if (dm->dc->caps.ips_support && adev->in_s0ix)
		dc_allow_idle_optimizations(dm->dc, true);

	dc_dmub_srv_set_power_state(dm->dc->ctx->dmub_srv, DC_ACPI_CM_POWER_STATE_D3);

	return 0;
}

void amdgpu_dm_emulated_link_detect(struct dc_link *link)
{
	struct dc_sink_init_data sink_init_data = { 0 };
	struct display_sink_capability sink_caps = { 0 };
	enum dc_edid_status edid_status;
	struct dc_context *dc_ctx = link->ctx;
	struct drm_device *dev = adev_to_drm(dc_ctx->driver_context);
	struct dc_sink *sink = NULL;
	struct dc_sink *prev_sink = NULL;

	link->type = dc_connection_none;
	prev_sink = link->local_sink;

	if (prev_sink)
		dc_sink_release(prev_sink);

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
		drm_err(dev, "Invalid connector type! signal:%d\n",
			link->connector_signal);
		return;
	}

	sink_init_data.link = link;
	sink_init_data.sink_signal = sink_caps.signal;

	sink = dc_sink_create(&sink_init_data);
	if (!sink) {
		drm_err(dev, "Failed to create sink!\n");
		return;
	}

	/* dc_sink_create returns a new reference */
	link->local_sink = sink;

	edid_status = dm_helpers_read_local_edid(
			link->ctx,
			link,
			sink);

	if (edid_status != EDID_OK)
		drm_err(dev, "Failed to read EDID\n");

}

static void dm_gpureset_commit_state(struct dc_state *dc_state,
				     struct amdgpu_display_manager *dm)
{
	struct {
		struct dc_surface_update surface_updates[MAX_SURFACES];
		struct dc_plane_info plane_infos[MAX_SURFACES];
		struct dc_scaling_info scaling_infos[MAX_SURFACES];
		struct dc_flip_addrs flip_addrs[MAX_SURFACES];
		struct dc_stream_update stream_update;
	} *bundle __free(kfree);
	int k, m;

	bundle = kzalloc_obj(*bundle);

	if (!bundle) {
		drm_err(dm->ddev, "Failed to allocate update bundle\n");
		return;
	}

	for (k = 0; k < dc_state->stream_count; k++) {
		bundle->stream_update.stream = dc_state->streams[k];

		for (m = 0; m < dc_state->stream_status[k].plane_count; m++) {
			bundle->surface_updates[m].surface =
				dc_state->stream_status[k].plane_states[m];
			bundle->surface_updates[m].surface->force_full_update =
				true;
		}

		update_planes_and_stream_adapter(dm->dc,
					 UPDATE_TYPE_FULL,
					 dc_state->stream_status[k].plane_count,
					 dc_state->streams[k],
					 &bundle->stream_update,
					 bundle->surface_updates);
	}
}

void amdgpu_dm_apply_delay_after_dpcd_poweroff(struct amdgpu_device *adev,
											   struct dc_sink *sink)
{
	struct dc_panel_patch *ppatch = NULL;

	if (!sink)
		return;

	ppatch = &sink->edid_caps.panel_patch;
	if (ppatch->wait_after_dpcd_poweroff_ms) {
		msleep(ppatch->wait_after_dpcd_poweroff_ms);
		drm_dbg_driver(adev_to_drm(adev),
			       "%s: adding a %ds delay as w/a for panel\n",
			       __func__,
			       ppatch->wait_after_dpcd_poweroff_ms / 1000);
	}
}

/**
 * amdgpu_dm_dump_links_and_sinks - Debug dump of all DC links and their sinks
 * @adev: amdgpu device pointer
 *
 * Iterates through all DC links and dumps information about local and remote
 * (MST) sinks. Should be called after connector detection is complete to see
 * the final state of all links.
 */
static void amdgpu_dm_dump_links_and_sinks(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct drm_device *dev = adev_to_drm(adev);
	int li;

	if (!dc)
		return;

	for (li = 0; li < dc->link_count; li++) {
		struct dc_link *l = dc->links[li];
		const char *name = NULL;
		int rs;

		if (!l)
			continue;
		if (l->local_sink && l->local_sink->edid_caps.display_name[0])
			name = l->local_sink->edid_caps.display_name;
		else
			name = "n/a";

		drm_dbg_kms(dev,
			"LINK_DUMP[%d]: local_sink=%p type=%d sink_signal=%d sink_count=%u edid_name=%s mst_capable=%d mst_alloc_streams=%d\n",
			li,
			l->local_sink,
			l->type,
			l->local_sink ? l->local_sink->sink_signal : SIGNAL_TYPE_NONE,
			l->sink_count,
			name,
			l->dpcd_caps.is_mst_capable,
			l->mst_stream_alloc_table.stream_count);

		/* Dump remote (MST) sinks if any */
		for (rs = 0; rs < l->sink_count; rs++) {
			struct dc_sink *rsink = l->remote_sinks[rs];
			const char *rname = NULL;

			if (!rsink)
				continue;
			if (rsink->edid_caps.display_name[0])
				rname = rsink->edid_caps.display_name;
			else
				rname = "n/a";
			drm_dbg_kms(dev,
				"  REMOTE_SINK[%d:%d]: sink=%p signal=%d edid_name=%s\n",
				li, rs,
				rsink,
				rsink->sink_signal,
				rname);
		}
	}
}

static int dm_resume(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct drm_device *ddev = adev_to_drm(adev);
	struct amdgpu_display_manager *dm = &adev->dm;
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	struct dm_atomic_state *dm_state = to_dm_atomic_state(dm->atomic_obj.state);
	enum dc_connection_type new_connection_type = dc_connection_none;
	struct dc_state *dc_state;
	int i, r, j;
	struct dc_commit_streams_params commit_params = {};

	if (dm->dc->caps.ips_support) {
		if (!amdgpu_in_reset(adev))
			mutex_lock(&dm->dc_lock);

		/* Need to set POWER_STATE_D0 first or it will not execute
		 * idle_power_optimizations command to DMUB.
		 */
		dc_dmub_srv_set_power_state(dm->dc->ctx->dmub_srv, DC_ACPI_CM_POWER_STATE_D0);
		dc_dmub_srv_apply_idle_power_optimizations(dm->dc, false);

		if (!amdgpu_in_reset(adev))
			mutex_unlock(&dm->dc_lock);
	}

	if (amdgpu_in_reset(adev)) {
		dc_state = dm->cached_dc_state;

		/*
		 * The dc->current_state is backed up into dm->cached_dc_state
		 * before we commit 0 streams.
		 *
		 * DC will clear link encoder assignments on the real state
		 * but the changes won't propagate over to the copy we made
		 * before the 0 streams commit.
		 *
		 * DC expects that link encoder assignments are *not* valid
		 * when committing a state, so as a workaround we can copy
		 * off of the current state.
		 *
		 * We lose the previous assignments, but we had already
		 * commit 0 streams anyway.
		 */
		link_enc_cfg_copy(adev->dm.dc->current_state, dc_state);

		r = dm_dmub_hw_init(adev);
		if (r) {
			drm_err(adev_to_drm(adev), "DMUB interface failed to initialize: status=%d\n", r);
			return r;
		}

		dc_dmub_srv_set_power_state(dm->dc->ctx->dmub_srv, DC_ACPI_CM_POWER_STATE_D0);
		dc_set_power_state(dm->dc, DC_ACPI_CM_POWER_STATE_D0);

		dc_resume(dm->dc);

		amdgpu_dm_ism_enable(dm);
		amdgpu_dm_irq_resume_early(adev);

		for (i = 0; i < dc_state->stream_count; i++) {
			dc_state->streams[i]->mode_changed = true;
			for (j = 0; j < dc_state->stream_status[i].plane_count; j++) {
				dc_pipe_update_bits_set_full(&dc_state->stream_status[i].plane_states[j]->update_bits);
			}
		}

		if (dc_is_dmub_outbox_supported(adev->dm.dc)) {
			amdgpu_dm_outbox_init(adev);
			dc_enable_dmub_outbox(adev->dm.dc);
		}

		commit_params.streams = dc_state->streams;
		commit_params.stream_count = dc_state->stream_count;
		dc_exit_ips_for_hw_access(dm->dc);
		WARN_ON(!dc_commit_streams(dm->dc, &commit_params));

		dm_gpureset_commit_state(dm->cached_dc_state, dm);

		dm_gpureset_toggle_interrupts(adev, dm->cached_dc_state, true);

		dc_state_release(dm->cached_dc_state);
		dm->cached_dc_state = NULL;

		amdgpu_dm_irq_resume_late(adev);

		mutex_unlock(&dm->dc_lock);

		/* set the backlight after a reset */
		for (i = 0; i < dm->num_of_edps; i++) {
			if (dm->backlight_dev[i])
				amdgpu_dm_backlight_set_level(dm, i, dm->brightness[i]);
		}

		return 0;
	}
	/* Recreate dc_state - DC invalidates it when setting power state to S3. */
	dc_state_release(dm_state->context);
	dm_state->context = dc_state_create(dm->dc, NULL);
	/* TODO: Remove dc_state->dccg, use dc->dccg directly. */

	/* Before powering on DC we need to re-initialize DMUB. */
	dm_dmub_hw_resume(adev);

	/* Re-enable outbox interrupts for DPIA. */
	if (dc_is_dmub_outbox_supported(adev->dm.dc)) {
		amdgpu_dm_outbox_init(adev);
		dc_enable_dmub_outbox(adev->dm.dc);
	}

	/* power on hardware */
	dc_dmub_srv_set_power_state(dm->dc->ctx->dmub_srv, DC_ACPI_CM_POWER_STATE_D0);
	dc_set_power_state(dm->dc, DC_ACPI_CM_POWER_STATE_D0);

	/* program HPD filter */
	dc_resume(dm->dc);

	scoped_guard(mutex, &dm->dc_lock)
		amdgpu_dm_ism_enable(dm);

	/*
	 * early enable HPD Rx IRQ, should be done before set mode as short
	 * pulse interrupts are used for MST
	 */
	amdgpu_dm_irq_resume_early(adev);

	amdgpu_dm_s3_handle_hdmi_cec(ddev, false);

	/* On resume we need to rewrite the MSTM control bits to enable MST*/
	s3_handle_mst(ddev, false);

	/* Do detection*/
	drm_connector_list_iter_begin(ddev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		bool ret;

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);

		if (!aconnector->dc_link)
			continue;

		/*
		 * this is the case when traversing through already created end sink
		 * MST connectors, should be skipped
		 */
		if (aconnector->mst_root)
			continue;

		/* Skip eDP detection, when there is no sink present */
		if (aconnector->dc_link->connector_signal == SIGNAL_TYPE_EDP &&
		    !aconnector->dc_link->edp_sink_present)
			continue;

		guard(mutex)(&aconnector->hpd_lock);
		if (!dc_link_detect_connection_type(aconnector->dc_link, &new_connection_type))
			drm_err(adev_to_drm(adev), "KMS: Failed to detect connector\n");

		if (aconnector->base.force && new_connection_type == dc_connection_none) {
			amdgpu_dm_emulated_link_detect(aconnector->dc_link);
		} else {
			guard(mutex)(&dm->dc_lock);
			dc_exit_ips_for_hw_access(dm->dc);
			ret = dc_link_detect(aconnector->dc_link, DETECT_REASON_RESUMEFROMS3S4);
			if (ret) {
				/* w/a delay for certain panels */
				amdgpu_dm_apply_delay_after_dpcd_poweroff(adev, aconnector->dc_sink);
			}
		}

		if (aconnector->fake_enable && aconnector->dc_link->local_sink)
			aconnector->fake_enable = false;

		if (aconnector->dc_sink)
			dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
		amdgpu_dm_update_connector_after_detect(aconnector);
	}
	drm_connector_list_iter_end(&iter);

	dm_destroy_cached_state(adev);

	/* Do mst topology probing after resuming cached state*/
	drm_connector_list_iter_begin(ddev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		bool init = false;

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->dc_link->type != dc_connection_mst_branch ||
		    aconnector->mst_root)
			continue;

		scoped_guard(mutex, &aconnector->mst_mgr.lock) {
			init = !aconnector->mst_mgr.mst_primary;
		}
		if (init)
			dm_helpers_dp_mst_start_top_mgr(aconnector->dc_link->ctx,
				aconnector->dc_link, false);
		else
			drm_dp_mst_topology_queue_probe(&aconnector->mst_mgr);
	}
	drm_connector_list_iter_end(&iter);

	/* Debug dump: list all DC links and their associated sinks after detection
	 * is complete for all connectors. This provides a comprehensive view of the
	 * final state without repeating the dump for each connector.
	 */
	amdgpu_dm_dump_links_and_sinks(adev);

	amdgpu_dm_irq_resume_late(adev);

	amdgpu_dm_smu_write_watermarks_table(adev);

	drm_kms_helper_hotplug_event(ddev);

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
	.early_fini = amdgpu_dm_early_fini,
	.hw_init = dm_hw_init,
	.hw_fini = dm_hw_fini,
	.suspend = dm_suspend,
	.resume = dm_resume,
	.is_idle = dm_is_idle,
	.wait_for_idle = dm_wait_for_idle,
	.soft_reset = dm_soft_reset,
	.set_clockgating_state = dm_set_clockgating_state,
	.set_powergating_state = dm_set_powergating_state,
};

const struct amdgpu_ip_block_version dm_ip_block = {
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
	.get_format_info = amdgpu_dm_plane_get_format_info,
	.atomic_check = amdgpu_dm_atomic_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static struct drm_mode_config_helper_funcs amdgpu_dm_mode_config_helperfuncs = {
	.atomic_commit_tail = amdgpu_dm_atomic_commit_tail,
	.atomic_commit_setup = amdgpu_dm_atomic_setup_commit,
};

/*
 * Acquires the lock for the atomic state object and returns
 * the new atomic state.
 *
 * This should only be called during atomic check.
 */
int dm_atomic_get_state(struct drm_atomic_commit *state,
			struct dm_atomic_state **dm_state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
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

static struct dm_atomic_state *
dm_atomic_get_new_state(struct drm_atomic_commit *state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
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

static struct drm_private_state *
dm_atomic_duplicate_state(struct drm_private_obj *obj)
{
	struct dm_atomic_state *old_state, *new_state;

	new_state = kzalloc_obj(*new_state);
	if (!new_state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &new_state->base);

	old_state = to_dm_atomic_state(obj->state);

	if (old_state && old_state->context)
		new_state->context = dc_state_create_copy(old_state->context);

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
		dc_state_release(dm_state->context);

	kfree(dm_state);
}

static struct drm_private_state *
dm_atomic_create_state(struct drm_private_obj *obj)
{
	struct amdgpu_device *adev = drm_to_adev(obj->dev);
	struct dm_atomic_state *dm_state;
	struct dc_state *context;

	dm_state = kzalloc_obj(*dm_state);
	if (!dm_state)
		return ERR_PTR(-ENOMEM);

	context = dc_state_create_current_copy(adev->dm.dc);
	if (!context) {
		kfree(dm_state);
		return ERR_PTR(-ENOMEM);
	}

	__drm_atomic_helper_private_obj_create_state(obj, &dm_state->base);
	dm_state->context = context;

	return &dm_state->base;
}

static struct drm_private_state_funcs dm_atomic_state_funcs = {
	.atomic_create_state = dm_atomic_create_state,
	.atomic_duplicate_state = dm_atomic_duplicate_state,
	.atomic_destroy_state = dm_atomic_destroy_state,
};

static int amdgpu_dm_mode_config_init(struct amdgpu_device *adev)
{
	int r;

	adev->mode_info.mode_config_initialized = true;

	adev_to_drm(adev)->mode_config.funcs = (void *)&amdgpu_dm_mode_funcs;
	adev_to_drm(adev)->mode_config.helper_private = &amdgpu_dm_mode_config_helperfuncs;

	adev_to_drm(adev)->mode_config.max_width = 16384;
	adev_to_drm(adev)->mode_config.max_height = 16384;

	adev_to_drm(adev)->mode_config.preferred_depth = 24;
	if (adev->asic_type == CHIP_HAWAII)
		/* disable prefer shadow for now due to hibernation issues */
		adev_to_drm(adev)->mode_config.prefer_shadow = 0;
	else
		adev_to_drm(adev)->mode_config.prefer_shadow = 1;
	/* indicates support for immediate flip */
	adev_to_drm(adev)->mode_config.async_page_flip = true;

	drm_atomic_private_obj_init(adev_to_drm(adev),
				    &adev->dm.atomic_obj,
				    &dm_atomic_state_funcs);

	r = amdgpu_display_modeset_create_props(adev);
	if (r)
		return r;

#ifdef AMD_PRIVATE_COLOR
	if (amdgpu_dm_create_color_properties(adev))
		return -ENOMEM;
#endif

	r = amdgpu_dm_audio_init(adev);
	if (r)
		return r;

	return 0;
}

static int initialize_plane(struct amdgpu_display_manager *dm,
			    struct amdgpu_mode_info *mode_info, int plane_id,
			    enum drm_plane_type plane_type,
			    const struct dc_plane_cap *plane_cap)
{
	struct drm_plane *plane;
	unsigned long possible_crtcs;
	int ret = 0;

	plane = kzalloc_obj(struct drm_plane);
	if (!plane) {
		drm_err(adev_to_drm(dm->adev), "KMS: Failed to allocate plane\n");
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
		drm_err(adev_to_drm(dm->adev), "KMS: Failed to initialize plane\n");
		kfree(plane);
		return ret;
	}

	if (mode_info)
		mode_info->planes[plane_id] = plane;

	return ret;
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
	s32 i;
	struct amdgpu_dm_connector *aconnector = NULL;
	struct amdgpu_encoder *aencoder = NULL;
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	u32 link_cnt;
	s32 primary_planes;
	enum dc_connection_type new_connection_type = dc_connection_none;
	const struct dc_plane_cap *plane;
	bool psr_feature_enabled = false;
	bool replay_feature_enabled = false;
	int max_overlay = dm->dc->caps.max_slave_planes;

	dm->display_indexes_num = dm->dc->caps.max_streams;
	/* Update the actual used number of crtc */
	adev->mode_info.num_crtc = adev->dm.display_indexes_num;

	amdgpu_dm_set_irq_funcs(adev);

	link_cnt = dm->dc->caps.max_links;
	if (amdgpu_dm_mode_config_init(dm->adev)) {
		drm_err(adev_to_drm(adev), "DM: Failed to initialize mode config\n");
		return -EINVAL;
	}

	/* There is one primary plane per CRTC */
	primary_planes = dm->dc->caps.max_streams;
	if (primary_planes > AMDGPU_MAX_PLANES) {
		drm_err(adev_to_drm(adev), "DM: Plane nums out of 6 planes\n");
		return -EINVAL;
	}

	/*
	 * Initialize primary planes, implicit planes for legacy IOCTLS.
	 * Order is reversed to match iteration order in atomic check.
	 */
	for (i = (primary_planes - 1); i >= 0; i--) {
		plane = &dm->dc->caps.planes[i];

		if (initialize_plane(dm, mode_info, i,
				     DRM_PLANE_TYPE_PRIMARY, plane)) {
			drm_err(adev_to_drm(adev), "KMS: Failed to initialize primary plane\n");
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

		/* Do not create overlay if MPO disabled */
		if (amdgpu_dc_debug_mask & DC_DISABLE_MPO)
			break;

		if (plane->type != DC_PLANE_TYPE_DCN_UNIVERSAL)
			continue;

		if (!plane->pixel_format_support.argb8888)
			continue;

		if (max_overlay-- == 0)
			break;

		if (initialize_plane(dm, NULL, primary_planes + i,
				     DRM_PLANE_TYPE_OVERLAY, plane)) {
			drm_err(adev_to_drm(adev), "KMS: Failed to initialize overlay plane\n");
			goto fail;
		}
	}

	for (i = 0; i < dm->dc->caps.max_streams; i++)
		if (amdgpu_dm_crtc_init(dm, mode_info->planes[i], i)) {
			drm_err(adev_to_drm(adev), "KMS: Failed to initialize crtc\n");
			goto fail;
		}

	/* Use Outbox interrupt */
	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(3, 0, 0):
	case IP_VERSION(3, 1, 2):
	case IP_VERSION(3, 1, 3):
	case IP_VERSION(3, 1, 4):
	case IP_VERSION(3, 1, 5):
	case IP_VERSION(3, 1, 6):
	case IP_VERSION(3, 2, 0):
	case IP_VERSION(3, 2, 1):
	case IP_VERSION(2, 1, 0):
	case IP_VERSION(3, 5, 0):
	case IP_VERSION(3, 5, 1):
	case IP_VERSION(3, 6, 0):
	case IP_VERSION(4, 0, 1):
	case IP_VERSION(4, 2, 0):
	case IP_VERSION(4, 2, 1):
		if (amdgpu_dm_register_outbox_irq_handlers(dm->adev)) {
			drm_err(adev_to_drm(adev), "DM: Failed to initialize IRQ\n");
			goto fail;
		}
		break;
	default:
		drm_dbg_kms(adev_to_drm(adev), "Unsupported DCN IP version for outbox: 0x%X\n",
			      amdgpu_ip_version(adev, DCE_HWIP, 0));
	}

	/* Determine whether to enable PSR support by default. */
	if (!(amdgpu_dc_debug_mask & DC_DISABLE_PSR)) {
		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(3, 1, 2):
		case IP_VERSION(3, 1, 3):
		case IP_VERSION(3, 1, 4):
		case IP_VERSION(3, 1, 5):
		case IP_VERSION(3, 1, 6):
		case IP_VERSION(3, 2, 0):
		case IP_VERSION(3, 2, 1):
		case IP_VERSION(3, 5, 0):
		case IP_VERSION(3, 5, 1):
		case IP_VERSION(3, 6, 0):
		case IP_VERSION(4, 0, 1):
		case IP_VERSION(4, 2, 0):
		case IP_VERSION(4, 2, 1):
			psr_feature_enabled = true;
			break;
		default:
			psr_feature_enabled = amdgpu_dc_feature_mask & DC_PSR_MASK;
			break;
		}
	}

	/* Determine whether to enable Replay support by default. */
	if (!(amdgpu_dc_debug_mask & DC_DISABLE_REPLAY)) {
		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(3, 1, 4):
		case IP_VERSION(3, 2, 0):
		case IP_VERSION(3, 2, 1):
		case IP_VERSION(3, 5, 0):
		case IP_VERSION(3, 5, 1):
		case IP_VERSION(3, 6, 0):
		case IP_VERSION(4, 2, 0):
		case IP_VERSION(4, 2, 1):
			replay_feature_enabled = true;
			break;

		default:
			replay_feature_enabled = amdgpu_dc_feature_mask & DC_REPLAY_MASK;
			break;
		}
	}

	if (link_cnt > MAX_LINKS) {
		drm_err(adev_to_drm(adev),
			"KMS: Cannot support more than %d display indexes\n",
				MAX_LINKS);
		goto fail;
	}

	/* loops over all connectors on the board */
	for (i = 0; i < link_cnt; i++) {
		struct dc_link *link = NULL;

		link = dc_get_link_at_index(dm->dc, i);

		if (link->connector_signal == SIGNAL_TYPE_VIRTUAL) {
			struct amdgpu_dm_wb_connector *wbcon = kzalloc_obj(*wbcon);

			if (!wbcon) {
				drm_err(adev_to_drm(adev), "KMS: Failed to allocate writeback connector\n");
				continue;
			}

			if (amdgpu_dm_wb_connector_init(dm, wbcon, i)) {
				drm_err(adev_to_drm(adev), "KMS: Failed to initialize writeback connector\n");
				kfree(wbcon);
				continue;
			}

			link->psr_settings.psr_feature_enabled = false;
			link->psr_settings.psr_version = DC_PSR_VERSION_UNSUPPORTED;

			continue;
		}

		aconnector = kzalloc_obj(*aconnector);
		if (!aconnector)
			goto fail;

		aencoder = kzalloc_obj(*aencoder);
		if (!aencoder)
			goto fail;

		if (amdgpu_dm_encoder_init(dm->ddev, aencoder, i)) {
			drm_err(adev_to_drm(adev), "KMS: Failed to initialize encoder\n");
			goto fail;
		}

		if (amdgpu_dm_connector_init(dm, aconnector, i, aencoder)) {
			drm_err(adev_to_drm(adev), "KMS: Failed to initialize connector\n");
			goto fail;
		}

		if (dm->hpd_rx_offload_wq)
			dm->hpd_rx_offload_wq[aconnector->base.index].aconnector =
				aconnector;

		if (!dc_link_detect_connection_type(link, &new_connection_type))
			drm_err(adev_to_drm(adev), "KMS: Failed to detect connector\n");

		if (aconnector->base.force && new_connection_type == dc_connection_none) {
			amdgpu_dm_emulated_link_detect(link);
			amdgpu_dm_update_connector_after_detect(aconnector);
		} else {
			bool ret = false;

			mutex_lock(&dm->dc_lock);
			dc_exit_ips_for_hw_access(dm->dc);
			ret = dc_link_detect(link, DETECT_REASON_BOOT);
			mutex_unlock(&dm->dc_lock);

			if (ret) {
				amdgpu_dm_update_connector_after_detect(aconnector);
				amdgpu_dm_setup_backlight_device(dm, aconnector);

				/* Disable PSR if Replay can be enabled */
				if (replay_feature_enabled)
					if (amdgpu_dm_set_replay_caps(link, aconnector))
						psr_feature_enabled = false;

				if (psr_feature_enabled) {
					amdgpu_dm_set_psr_caps(link, aconnector);
					drm_info(adev_to_drm(adev), "%s: PSR support %d, DC PSR ver %d, sink PSR ver %d DPCD caps 0x%x su_y_granularity %d\n",
						 aconnector->base.name,
						 link->psr_settings.psr_feature_enabled,
						 link->psr_settings.psr_version,
						 link->dpcd_caps.psr_info.psr_version,
						 link->dpcd_caps.psr_info.psr_dpcd_caps.raw,
						 link->dpcd_caps.psr_info.psr2_su_y_granularity_cap);
				}
			}
		}
		amdgpu_set_panel_orientation(&aconnector->base);
	}

	/* Debug dump: list all DC links and their associated sinks after detection
	 * is complete for all connectors. This provides a comprehensive view of the
	 * final state without repeating the dump for each connector.
	 */
	amdgpu_dm_dump_links_and_sinks(adev);

	/* Software is initialized. Now we can register interrupt handlers. */
	switch (adev->asic_type) {
#if defined(CONFIG_DRM_AMD_DC_SI)
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_OLAND:
#endif
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
		if (amdgpu_dm_dce110_register_irq_handlers(dm->adev)) {
			drm_err(adev_to_drm(adev), "DM: Failed to initialize IRQ\n");
			goto fail;
		}
		break;
	default:
		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(1, 0, 0):
		case IP_VERSION(1, 0, 1):
		case IP_VERSION(2, 0, 2):
		case IP_VERSION(2, 0, 3):
		case IP_VERSION(2, 0, 0):
		case IP_VERSION(2, 1, 0):
		case IP_VERSION(3, 0, 0):
		case IP_VERSION(3, 0, 2):
		case IP_VERSION(3, 0, 3):
		case IP_VERSION(3, 0, 1):
		case IP_VERSION(3, 1, 2):
		case IP_VERSION(3, 1, 3):
		case IP_VERSION(3, 1, 4):
		case IP_VERSION(3, 1, 5):
		case IP_VERSION(3, 1, 6):
		case IP_VERSION(3, 2, 0):
		case IP_VERSION(3, 2, 1):
		case IP_VERSION(3, 5, 0):
		case IP_VERSION(3, 5, 1):
		case IP_VERSION(3, 6, 0):
		case IP_VERSION(4, 0, 1):
		case IP_VERSION(4, 2, 0):
		case IP_VERSION(4, 2, 1):
			if (amdgpu_dm_dcn10_register_irq_handlers(dm->adev)) {
				drm_err(adev_to_drm(adev), "DM: Failed to initialize IRQ\n");
				goto fail;
			}
			break;
		default:
			drm_err(adev_to_drm(adev), "Unsupported DCE IP versions: 0x%X\n",
					amdgpu_ip_version(adev, DCE_HWIP, 0));
			goto fail;
		}
		break;
	}

	return 0;
fail:
	kfree(aencoder);
	kfree(aconnector);

	return -EINVAL;
}

static void amdgpu_dm_destroy_drm_device(struct amdgpu_display_manager *dm)
{
	if (dm->atomic_obj.state)
		drm_atomic_private_obj_fini(&dm->atomic_obj);
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
	struct drm_device *drm_dev = dev_get_drvdata(device);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);
	struct amdgpu_ip_block *ip_block;

	ip_block = amdgpu_device_ip_get_ip_block(adev, AMD_IP_BLOCK_TYPE_DCE);
	if (!ip_block)
		return -EINVAL;

	ret = kstrtoint(buf, 0, &s3_state);

	if (ret == 0) {
		if (s3_state) {
			dm_resume(ip_block);
			drm_kms_helper_hotplug_event(adev_to_drm(adev));
		} else
			dm_suspend(ip_block);
	}

	return ret == 0 ? count : 0;
}

DEVICE_ATTR_WO(s3_debug);

#endif

static int dm_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	struct atom_context *ctx = mode_info->atom_context;
	int index = GetIndexIntoMasterTable(DATA, Object_Header);
	u16 data_offset;

	/* if there is no object header, skip DM */
	if (!amdgpu_atom_parse_data_header(ctx, index, NULL, NULL, NULL, &data_offset)) {
		adev->harvest_ip_mask |= AMD_HARVEST_IP_DMU_MASK;
		drm_info(adev_to_drm(adev), "No object header, skipping DM\n");
		return -ENOENT;
	}

	switch (adev->asic_type) {
#if defined(CONFIG_DRM_AMD_DC_SI)
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		break;
	case CHIP_OLAND:
		adev->mode_info.num_crtc = 2;
		adev->mode_info.num_hpd = 2;
		adev->mode_info.num_dig = 2;
		break;
#endif
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
	default:

		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(2, 0, 2):
		case IP_VERSION(3, 0, 0):
			adev->mode_info.num_crtc = 6;
			adev->mode_info.num_hpd = 6;
			adev->mode_info.num_dig = 6;
			break;
		case IP_VERSION(2, 0, 0):
		case IP_VERSION(3, 0, 2):
			adev->mode_info.num_crtc = 5;
			adev->mode_info.num_hpd = 5;
			adev->mode_info.num_dig = 5;
			break;
		case IP_VERSION(2, 0, 3):
		case IP_VERSION(3, 0, 3):
			adev->mode_info.num_crtc = 2;
			adev->mode_info.num_hpd = 2;
			adev->mode_info.num_dig = 2;
			break;
		case IP_VERSION(1, 0, 0):
		case IP_VERSION(1, 0, 1):
		case IP_VERSION(3, 0, 1):
		case IP_VERSION(2, 1, 0):
		case IP_VERSION(3, 1, 2):
		case IP_VERSION(3, 1, 3):
		case IP_VERSION(3, 1, 4):
		case IP_VERSION(3, 1, 5):
		case IP_VERSION(3, 1, 6):
		case IP_VERSION(3, 2, 0):
		case IP_VERSION(3, 2, 1):
		case IP_VERSION(3, 5, 0):
		case IP_VERSION(3, 5, 1):
		case IP_VERSION(3, 6, 0):
		case IP_VERSION(4, 0, 1):
		case IP_VERSION(4, 2, 0):
		case IP_VERSION(4, 2, 1):
			adev->mode_info.num_crtc = 4;
			adev->mode_info.num_hpd = 4;
			adev->mode_info.num_dig = 4;
			break;
		default:
			drm_err(adev_to_drm(adev), "Unsupported DCE IP versions: 0x%x\n",
					amdgpu_ip_version(adev, DCE_HWIP, 0));
			return -EINVAL;
		}
		break;
	}

	if (adev->mode_info.funcs == NULL)
		adev->mode_info.funcs = &dm_display_funcs;

	/*
	 * Note: Do NOT change adev->reg.audio_endpt.rreg and
	 * adev->reg.audio_endpt.wreg because they are initialised in
	 * amdgpu_device_init()
	 */
#if defined(CONFIG_DEBUG_KERNEL_DC)
	device_create_file(
		adev_to_drm(adev)->dev,
		&dev_attr_s3_debug);
#endif
	adev->dc_enabled = true;

	return dm_init_microcode(adev);
}

STATIC_IFN_KUNIT bool modereset_required(struct drm_crtc_state *crtc_state)
{
	return !crtc_state->active && drm_atomic_crtc_needs_modeset(crtc_state);
}
EXPORT_IF_KUNIT(modereset_required);

STATIC_IFN_KUNIT int
fill_plane_color_attributes(const struct drm_plane_state *plane_state,
			    const enum surface_pixel_format format,
			    enum dc_color_space *color_space)
{
	bool full_range;

	*color_space = COLOR_SPACE_SRGB;

	/* Ignore properties when DRM_CLIENT_CAP_PLANE_COLOR_PIPELINE is set */
	if (plane_state->state && plane_state->state->plane_color_pipeline)
		return 0;

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
			*color_space = COLOR_SPACE_2020_YCBCR_FULL;
		else
			*color_space = COLOR_SPACE_2020_YCBCR_LIMITED;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_IF_KUNIT(fill_plane_color_attributes);

static int
fill_dc_plane_info_and_addr(struct amdgpu_device *adev,
			    const struct drm_plane_state *plane_state,
			    const u64 tiling_flags,
			    struct dc_plane_info *plane_info,
			    struct dc_plane_address *address,
			    bool tmz_surface)
{
	const struct drm_framebuffer *fb = plane_state->fb;
	const struct amdgpu_framebuffer *afb =
		to_amdgpu_framebuffer(plane_state->fb);
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
	case DRM_FORMAT_P010:
		plane_info->format = SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb;
		break;
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F;
		break;
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F;
		break;
	case DRM_FORMAT_XRGB16161616:
	case DRM_FORMAT_ARGB16161616:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616;
		break;
	case DRM_FORMAT_XBGR16161616:
	case DRM_FORMAT_ABGR16161616:
		plane_info->format = SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616;
		break;
	default:
		drm_err(adev_to_drm(adev),
			"Unsupported screen format %p4cc\n",
			&fb->format->format);
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

	plane_info->layer_index = plane_state->normalized_zpos;

	ret = fill_plane_color_attributes(plane_state, plane_info->format,
					  &plane_info->color_space);
	if (ret)
		return ret;

	ret = amdgpu_dm_plane_fill_plane_buffer_attributes(adev, afb, plane_info->format,
					   plane_info->rotation, tiling_flags,
					   &plane_info->tiling_info,
					   &plane_info->plane_size,
					   &plane_info->dcc, address,
					   tmz_surface);
	if (ret)
		return ret;

	amdgpu_dm_plane_fill_blending_from_plane_state(
		plane_state, &plane_info->per_pixel_alpha, &plane_info->pre_multiplied_alpha,
		&plane_info->global_alpha, &plane_info->global_alpha_value);

	return 0;
}

static int fill_dc_plane_attributes(struct amdgpu_device *adev,
				    struct dc_plane_state *dc_plane_state,
				    struct drm_plane_state *plane_state,
				    struct drm_crtc_state *crtc_state)
{
	struct dm_crtc_state *dm_crtc_state = to_dm_crtc_state(crtc_state);
	struct amdgpu_framebuffer *afb = (struct amdgpu_framebuffer *)plane_state->fb;
	struct dc_scaling_info scaling_info;
	struct dc_plane_info plane_info;
	int ret;

	ret = amdgpu_dm_plane_fill_dc_scaling_info(adev, plane_state, &scaling_info);
	if (ret)
		return ret;

	dc_plane_state->src_rect = scaling_info.src_rect;
	dc_plane_state->dst_rect = scaling_info.dst_rect;
	dc_plane_state->clip_rect = scaling_info.clip_rect;
	dc_plane_state->scaling_quality = scaling_info.scaling_quality;

	ret = fill_dc_plane_info_and_addr(adev, plane_state,
					  afb->tiling_flags,
					  &plane_info,
					  &dc_plane_state->address,
					  afb->tmz_surface);
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
	dc_plane_state->pre_multiplied_alpha = plane_info.pre_multiplied_alpha;
	dc_plane_state->global_alpha = plane_info.global_alpha;
	dc_plane_state->global_alpha_value = plane_info.global_alpha_value;
	dc_plane_state->dcc = plane_info.dcc;
	dc_plane_state->layer_index = plane_info.layer_index;
	dc_plane_state->flip_int_enabled = true;

	/*
	 * Always set input transfer function, since plane state is refreshed
	 * every time.
	 */
	ret = amdgpu_dm_update_plane_color_mgmt(dm_crtc_state,
						plane_state,
						dc_plane_state);
	if (ret)
		return ret;

	return 0;
}

static inline void fill_dc_dirty_rect(struct drm_plane *plane,
				      struct rect *dirty_rect, int32_t x,
				      s32 y, s32 width, s32 height,
				      int *i, bool ffu)
{
	WARN_ON(*i >= DC_MAX_DIRTY_RECTS);

	dirty_rect->x = x;
	dirty_rect->y = y;
	dirty_rect->width = width;
	dirty_rect->height = height;

	if (ffu)
		drm_dbg(plane->dev,
			"[PLANE:%d] PSR FFU dirty rect size (%d, %d)\n",
			plane->base.id, width, height);
	else
		drm_dbg(plane->dev,
			"[PLANE:%d] PSR SU dirty rect at (%d, %d) size (%d, %d)",
			plane->base.id, x, y, width, height);

	(*i)++;
}

/**
 * fill_dc_dirty_rects() - Fill DC dirty regions for PSR selective updates
 *
 * @plane: DRM plane containing dirty regions that need to be flushed to the eDP
 *         remote fb
 * @old_plane_state: Old state of @plane
 * @new_plane_state: New state of @plane
 * @crtc_state: New state of CRTC connected to the @plane
 * @flip_addrs: DC flip tracking struct, which also tracts dirty rects
 * @is_psr_su: Flag indicating whether Panel Self Refresh Selective Update (PSR SU) is enabled.
 *             If PSR SU is enabled and damage clips are available, only the regions of the screen
 *             that have changed will be updated. If PSR SU is not enabled,
 *             or if damage clips are not available, the entire screen will be updated.
 * @dirty_regions_changed: dirty regions changed
 *
 * For PSR SU, DC informs the DMUB uController of dirty rectangle regions
 * (referred to as "damage clips" in DRM nomenclature) that require updating on
 * the eDP remote buffer. The responsibility of specifying the dirty regions is
 * amdgpu_dm's.
 *
 * A damage-aware DRM client should fill the FB_DAMAGE_CLIPS property on the
 * plane with regions that require flushing to the eDP remote buffer. In
 * addition, certain use cases - such as cursor and multi-plane overlay (MPO) -
 * implicitly provide damage clips without any client support via the plane
 * bounds.
 */
static void fill_dc_dirty_rects(struct drm_plane *plane,
				struct drm_plane_state *old_plane_state,
				struct drm_plane_state *new_plane_state,
				struct drm_crtc_state *crtc_state,
				struct dc_flip_addrs *flip_addrs,
				bool is_psr_su,
				bool *dirty_regions_changed)
{
	struct dm_crtc_state *dm_crtc_state = to_dm_crtc_state(crtc_state);
	struct rect *dirty_rects = flip_addrs->dirty_rects;
	u32 num_clips = 0;
	struct drm_mode_rect *clips = NULL;
	bool bb_changed;
	bool fb_changed;
	u32 i = 0;
	*dirty_regions_changed = false;

	/*
	 * Cursor plane has it's own dirty rect update interface. See
	 * dcn10_dmub_update_cursor_data and dmub_cmd_update_cursor_info_data
	 */
	if (plane->type == DRM_PLANE_TYPE_CURSOR)
		return;

	if (new_plane_state->rotation != DRM_MODE_ROTATE_0)
		goto ffu;

	if (!new_plane_state->ignore_damage_clips) {
		num_clips = drm_plane_get_damage_clips_count(new_plane_state);
		clips = drm_plane_get_damage_clips(new_plane_state);
	}

	if (num_clips && (!amdgpu_damage_clips || (amdgpu_damage_clips < 0 &&
						   is_psr_su)))
		goto ffu;

	if (!dm_crtc_state->mpo_requested) {
		if (!num_clips || num_clips > DC_MAX_DIRTY_RECTS)
			goto ffu;

		for (; flip_addrs->dirty_rect_count < num_clips; clips++)
			fill_dc_dirty_rect(new_plane_state->plane,
					   &dirty_rects[flip_addrs->dirty_rect_count],
					   clips->x1, clips->y1,
					   clips->x2 - clips->x1, clips->y2 - clips->y1,
					   &flip_addrs->dirty_rect_count,
					   false);
		return;
	}

	/*
	 * MPO is requested. Add entire plane bounding box to dirty rects if
	 * flipped to or damaged.
	 *
	 * If plane is moved or resized, also add old bounding box to dirty
	 * rects.
	 */
	fb_changed = old_plane_state->fb->base.id !=
		     new_plane_state->fb->base.id;
	bb_changed = (old_plane_state->crtc_x != new_plane_state->crtc_x ||
		      old_plane_state->crtc_y != new_plane_state->crtc_y ||
		      old_plane_state->crtc_w != new_plane_state->crtc_w ||
		      old_plane_state->crtc_h != new_plane_state->crtc_h);

	drm_dbg(plane->dev,
		"[PLANE:%d] PSR bb_changed:%d fb_changed:%d num_clips:%d\n",
		new_plane_state->plane->base.id,
		bb_changed, fb_changed, num_clips);

	*dirty_regions_changed = bb_changed;

	if ((num_clips + (bb_changed ? 2 : 0)) > DC_MAX_DIRTY_RECTS)
		goto ffu;

	if (bb_changed) {
		fill_dc_dirty_rect(new_plane_state->plane, &dirty_rects[i],
				   new_plane_state->crtc_x,
				   new_plane_state->crtc_y,
				   new_plane_state->crtc_w,
				   new_plane_state->crtc_h, &i, false);

		/* Add old plane bounding-box if plane is moved or resized */
		fill_dc_dirty_rect(new_plane_state->plane, &dirty_rects[i],
				   old_plane_state->crtc_x,
				   old_plane_state->crtc_y,
				   old_plane_state->crtc_w,
				   old_plane_state->crtc_h, &i, false);
	}

	if (num_clips) {
		for (; i < num_clips; clips++)
			fill_dc_dirty_rect(new_plane_state->plane,
					   &dirty_rects[i], clips->x1,
					   clips->y1, clips->x2 - clips->x1,
					   clips->y2 - clips->y1, &i, false);
	} else if (fb_changed && !bb_changed) {
		fill_dc_dirty_rect(new_plane_state->plane, &dirty_rects[i],
				   new_plane_state->crtc_x,
				   new_plane_state->crtc_y,
				   new_plane_state->crtc_w,
				   new_plane_state->crtc_h, &i, false);
	}

	flip_addrs->dirty_rect_count = i;
	return;

ffu:
	fill_dc_dirty_rect(new_plane_state->plane, &dirty_rects[0], 0, 0,
			   dm_crtc_state->base.mode.crtc_hdisplay,
			   dm_crtc_state->base.mode.crtc_vdisplay,
			   &flip_addrs->dirty_rect_count, true);
}

void amdgpu_dm_update_stream_scaling_settings(struct drm_device *dev,
					   const struct drm_display_mode *mode,
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

	drm_dbg_kms(dev, "Destination Rectangle x:%d  y:%d  width:%d  height:%d\n",
		    dst.x, dst.y, dst.width, dst.height);

}

static int dm_update_mst_vcpi_slots_for_dsc(struct drm_atomic_commit *state,
					    struct dc_state *dc_state,
					    struct dsc_mst_fairness_vars *vars)
{
	struct dc_stream_state *stream = NULL;
	struct drm_connector *connector;
	struct drm_connector_state *new_con_state;
	struct amdgpu_dm_connector *aconnector;
	struct dm_connector_state *dm_conn_state;
	int i, j, ret;
	int vcpi, pbn_div, pbn = 0, slot_num = 0;

	for_each_new_connector_in_state(state, connector, new_con_state, i) {

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);

		if (!aconnector->mst_output_port)
			continue;

		if (!new_con_state || !new_con_state->crtc)
			continue;

		dm_conn_state = to_dm_connector_state(new_con_state);

		for (j = 0; j < dc_state->stream_count; j++) {
			stream = dc_state->streams[j];
			if (!stream)
				continue;

			if ((struct amdgpu_dm_connector *)stream->dm_stream_context == aconnector)
				break;

			stream = NULL;
		}

		if (!stream)
			continue;

		pbn_div = dm_mst_get_pbn_divider(stream->link);
		/* pbn is calculated by compute_mst_dsc_configs_for_state*/
		for (j = 0; j < dc_state->stream_count; j++) {
			if (vars[j].aconnector == aconnector) {
				pbn = vars[j].pbn;
				break;
			}
		}

		if (j == dc_state->stream_count || pbn_div == 0)
			continue;

		slot_num = DIV_ROUND_UP(pbn, pbn_div);

		if (stream->timing.flags.DSC != 1) {
			dm_conn_state->pbn = pbn;
			dm_conn_state->vcpi_slots = slot_num;

			ret = drm_dp_mst_atomic_enable_dsc(state, aconnector->mst_output_port,
							   dm_conn_state->pbn, false);
			if (ret < 0)
				return ret;

			continue;
		}

		vcpi = drm_dp_mst_atomic_enable_dsc(state, aconnector->mst_output_port, pbn, true);
		if (vcpi < 0)
			return vcpi;

		dm_conn_state->pbn = pbn;
		dm_conn_state->vcpi_slots = vcpi;
	}
	return 0;
}

static void manage_dm_interrupts(struct amdgpu_device *adev,
				 struct amdgpu_crtc *acrtc,
				 struct dm_crtc_state *acrtc_state)
{	/*
	 * We cannot be sure that the frontend index maps to the same
	 * backend index - some even map to more than one.
	 * So we have to go through the CRTC to find the right IRQ.
	 */
	int irq_type = amdgpu_display_crtc_idx_to_irq_type(
			adev,
			acrtc->crtc_id);
	struct drm_device *dev = adev_to_drm(adev);

	struct drm_vblank_crtc_config config = {0};
	struct dc_crtc_timing *timing;
	int offdelay;

	if (acrtc_state) {
		timing = &acrtc_state->stream->timing;

		if (amdgpu_ip_version(adev, DCE_HWIP, 0) <
			   IP_VERSION(3, 5, 0) ||
			   !(adev->flags & AMD_IS_APU)) {
			/*
			 * Older HW and DGPU have issues with instant off;
			 * use a 2 frame offdelay.
			 */
			offdelay = DIV64_U64_ROUND_UP((u64)20 *
						      timing->v_total *
						      timing->h_total,
						      timing->pix_clk_100hz);

			config.offdelay_ms = offdelay ?: 30;
		} else {
			/* offdelay_ms = 0 will never disable vblank */
			config.offdelay_ms = 1;
			config.disable_immediate = true;
		}

		drm_crtc_vblank_on_config(&acrtc->base,
					  &config);
		/*
		 * Since pflip_high_irq is no longer registered for DCN, grab an
		 * extra reference to vupdate irq instead to workaround this
		 * issue:
		 * https://gitlab.freedesktop.org/drm/amd/-/work_items/3936
		 *
		 * The callbacks to drm_vblank_on/off should really take care of
		 * this though.
		 */
		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(3, 0, 0):
		case IP_VERSION(3, 0, 2):
		case IP_VERSION(3, 0, 3):
		case IP_VERSION(3, 2, 0):
			if (amdgpu_irq_get(adev, &adev->vupdate_irq, irq_type))
				drm_err(dev, "DM_IRQ: Cannot get vupdate irq!\n");
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
			if (amdgpu_irq_get(adev, &adev->vline0_irq, irq_type))
				drm_err(dev, "DM_IRQ: Cannot get vline0 irq!\n");
#endif
		}

	} else {
		/* Allow RX6xxx, RX7700, RX7800 GPUs to call amdgpu_irq_put.*/
		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(3, 0, 0):
		case IP_VERSION(3, 0, 2):
		case IP_VERSION(3, 0, 3):
		case IP_VERSION(3, 2, 0):
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
			if (amdgpu_irq_put(adev, &adev->vline0_irq, irq_type))
				drm_err(dev, "DM_IRQ: Cannot put vline0 irq!\n");
#endif
			if (amdgpu_irq_put(adev, &adev->vupdate_irq, irq_type))
				drm_err(dev, "DM_IRQ: Cannot put vupdate irq!\n");
		}

		drm_crtc_vblank_off(&acrtc->base);
	}
}

static void dm_update_pflip_irq_state(struct amdgpu_device *adev,
				      struct amdgpu_crtc *acrtc)
{
	int irq_type =
		amdgpu_display_crtc_idx_to_irq_type(adev, acrtc->crtc_id);

	/* GRPH_PFLIP is not used on DCN; nothing to reapply. */
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) != 0)
		return;

	/**
	 * This reads the current state for the IRQ and force reapplies
	 * the setting to hardware.
	 */
	amdgpu_irq_update(adev, &adev->pageflip_irq, irq_type);
}

STATIC_IFN_KUNIT bool
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
EXPORT_IF_KUNIT(is_scaling_state_different);

static bool is_content_protection_different(struct drm_crtc_state *new_crtc_state,
					    struct drm_crtc_state *old_crtc_state,
					    struct drm_connector_state *new_conn_state,
					    struct drm_connector_state *old_conn_state,
					    const struct drm_connector *connector,
					    struct hdcp_workqueue *hdcp_w)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct dm_connector_state *dm_con_state = to_dm_connector_state(connector->state);

	pr_debug("[HDCP_DM] connector->index: %x connect_status: %x dpms: %x\n",
		connector->index, connector->status, connector->dpms);
	pr_debug("[HDCP_DM] state protection old: %x new: %x\n",
		old_conn_state->content_protection, new_conn_state->content_protection);

	if (old_crtc_state)
		pr_debug("[HDCP_DM] old crtc en: %x a: %x m: %x a-chg: %x c-chg: %x\n",
		old_crtc_state->enable,
		old_crtc_state->active,
		old_crtc_state->mode_changed,
		old_crtc_state->active_changed,
		old_crtc_state->connectors_changed);

	if (new_crtc_state)
		pr_debug("[HDCP_DM] NEW crtc en: %x a: %x m: %x a-chg: %x c-chg: %x\n",
		new_crtc_state->enable,
		new_crtc_state->active,
		new_crtc_state->mode_changed,
		new_crtc_state->active_changed,
		new_crtc_state->connectors_changed);

	/* hdcp content type change */
	if (old_conn_state->hdcp_content_type != new_conn_state->hdcp_content_type &&
	    new_conn_state->content_protection != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
		new_conn_state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		pr_debug("[HDCP_DM] Type0/1 change %s :true\n", __func__);
		return true;
	}

	/* CP is being re enabled, ignore this */
	if (old_conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED &&
	    new_conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_DESIRED) {
		if (new_crtc_state && new_crtc_state->mode_changed) {
			new_conn_state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
			pr_debug("[HDCP_DM] ENABLED->DESIRED & mode_changed %s :true\n", __func__);
			return true;
		}
		new_conn_state->content_protection = DRM_MODE_CONTENT_PROTECTION_ENABLED;
		pr_debug("[HDCP_DM] ENABLED -> DESIRED %s :false\n", __func__);
		return false;
	}

	/* S3 resume case, since old state will always be 0 (UNDESIRED) and the restored state will be ENABLED
	 *
	 * Handles:	UNDESIRED -> ENABLED
	 */
	if (old_conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_UNDESIRED &&
	    new_conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED)
		new_conn_state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;

	/* Stream removed and re-enabled
	 *
	 * Can sometimes overlap with the HPD case,
	 * thus set update_hdcp to false to avoid
	 * setting HDCP multiple times.
	 *
	 * Handles:	DESIRED -> DESIRED (Special case)
	 */
	if (!(old_conn_state->crtc && old_conn_state->crtc->enabled) &&
		new_conn_state->crtc && new_conn_state->crtc->enabled &&
		connector->state->content_protection == DRM_MODE_CONTENT_PROTECTION_DESIRED) {
		dm_con_state->update_hdcp = false;
		pr_debug("[HDCP_DM] DESIRED->DESIRED (Stream removed and re-enabled) %s :true\n",
			__func__);
		return true;
	}

	/* Hot-plug, headless s3, dpms
	 *
	 * Only start HDCP if the display is connected/enabled.
	 * update_hdcp flag will be set to false until the next
	 * HPD comes in.
	 *
	 * Handles:	DESIRED -> DESIRED (Special case)
	 */
	if (dm_con_state->update_hdcp &&
	new_conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_DESIRED &&
	connector->dpms == DRM_MODE_DPMS_ON && aconnector->dc_sink != NULL) {
		dm_con_state->update_hdcp = false;
		pr_debug("[HDCP_DM] DESIRED->DESIRED (Hot-plug, headless s3, dpms) %s :true\n",
			__func__);
		return true;
	}

	if (old_conn_state->content_protection == new_conn_state->content_protection) {
		if (new_conn_state->content_protection >= DRM_MODE_CONTENT_PROTECTION_DESIRED) {
			if (new_crtc_state && new_crtc_state->mode_changed) {
				pr_debug("[HDCP_DM] DESIRED->DESIRED or ENABLE->ENABLE mode_change %s :true\n",
					__func__);
				return true;
			}
			pr_debug("[HDCP_DM] DESIRED->DESIRED & ENABLE->ENABLE %s :false\n",
				__func__);
			return false;
		}

		pr_debug("[HDCP_DM] UNDESIRED->UNDESIRED %s :false\n", __func__);
		return false;
	}

	if (new_conn_state->content_protection != DRM_MODE_CONTENT_PROTECTION_ENABLED) {
		pr_debug("[HDCP_DM] UNDESIRED->DESIRED or DESIRED->UNDESIRED or ENABLED->UNDESIRED %s :true\n",
			__func__);
		return true;
	}

	pr_debug("[HDCP_DM] DESIRED->ENABLED %s :false\n", __func__);
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

static void prepare_flip_isr(struct amdgpu_crtc *acrtc)
{

	assert_spin_locked(&acrtc->base.dev->event_lock);
	WARN_ON(acrtc->event);

	acrtc->event = acrtc->base.state->event;

	/* Set the flip status */
	acrtc->pflip_status = AMDGPU_FLIP_SUBMITTED;

	/* Mark this event as consumed */
	acrtc->base.state->event = NULL;

	drm_dbg_state(acrtc->base.dev,
		      "crtc:%d, pflip_stat:AMDGPU_FLIP_SUBMITTED\n",
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
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(new_crtc_state->base.crtc);
	unsigned long flags;
	bool pack_sdp_v1_3 = false;
	struct amdgpu_dm_connector *aconn;
	enum vrr_packet_type packet_type = PACKET_TYPE_VRR;

	if (!new_stream)
		return;

	/*
	 * TODO: Determine why min/max totals and vrefresh can be 0 here.
	 * For now it's sufficient to just guard against these conditions.
	 */

	if (!new_stream->timing.h_total || !new_stream->timing.v_total)
		return;

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
	vrr_params = acrtc->dm_irq_params.vrr_params;

	if (surface) {
		mod_freesync_handle_preflip(
			dm->freesync_module,
			surface,
			new_stream,
			flip_timestamp_in_us,
			&vrr_params);

		if (adev->family < AMDGPU_FAMILY_AI &&
		    amdgpu_dm_crtc_vrr_active(new_crtc_state)) {
			mod_freesync_handle_v_update(dm->freesync_module,
						     new_stream, &vrr_params);

			/* Need to call this before the frame ends. */
			dc_stream_adjust_vmin_vmax(dm->dc,
						   new_crtc_state->stream,
						   &vrr_params.adjust);
		}
	}

	aconn = (struct amdgpu_dm_connector *)new_stream->dm_stream_context;

	if (aconn && (aconn->as_type == FREESYNC_TYPE_PCON_IN_WHITELIST || aconn->vsdb_info.replay_mode)) {
		pack_sdp_v1_3 = aconn->pack_sdp_v1_3;

		if (aconn->vsdb_info.amd_vsdb_version == 1)
			packet_type = PACKET_TYPE_FS_V1;
		else if (aconn->vsdb_info.amd_vsdb_version == 2)
			packet_type = PACKET_TYPE_FS_V2;
		else if (aconn->vsdb_info.amd_vsdb_version == 3)
			packet_type = PACKET_TYPE_FS_V3;

		mod_build_adaptive_sync_infopacket(new_stream, aconn->as_type, NULL,
					&new_stream->adaptive_sync_infopacket);
	}

	mod_freesync_build_vrr_infopacket(
		dm->freesync_module,
		new_stream,
		&vrr_params,
		packet_type,
		TRANSFER_FUNC_UNKNOWN,
		&vrr_infopacket,
		pack_sdp_v1_3);

	new_crtc_state->freesync_vrr_info_changed |=
		(memcmp(&new_crtc_state->vrr_infopacket,
			&vrr_infopacket,
			sizeof(vrr_infopacket)) != 0);

	acrtc->dm_irq_params.vrr_params = vrr_params;
	new_crtc_state->vrr_infopacket = vrr_infopacket;

	new_stream->vrr_infopacket = vrr_infopacket;
	new_stream->allow_freesync = mod_freesync_get_freesync_enabled(&vrr_params);

	if (new_crtc_state->freesync_vrr_info_changed)
		drm_dbg_kms(adev_to_drm(adev), "VRR packet update: crtc=%u enabled=%d state=%d",
			      new_crtc_state->base.crtc->base.id,
			      (int)new_crtc_state->base.vrr_enabled,
			      (int)vrr_params.state);

	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
}

static void update_stream_irq_parameters(
	struct amdgpu_display_manager *dm,
	struct dm_crtc_state *new_crtc_state)
{
	struct dc_stream_state *new_stream = new_crtc_state->stream;
	struct mod_vrr_params vrr_params;
	struct mod_freesync_config config = new_crtc_state->freesync_config;
	struct amdgpu_device *adev = dm->adev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(new_crtc_state->base.crtc);
	unsigned long flags;

	if (!new_stream)
		return;

	/*
	 * TODO: Determine why min/max totals and vrefresh can be 0 here.
	 * For now it's sufficient to just guard against these conditions.
	 */
	if (!new_stream->timing.h_total || !new_stream->timing.v_total)
		return;

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
	vrr_params = acrtc->dm_irq_params.vrr_params;

	if (new_crtc_state->vrr_supported &&
	    config.min_refresh_in_uhz &&
	    config.max_refresh_in_uhz) {
		/*
		 * if freesync compatible mode was set, config.state will be set
		 * in atomic check
		 */
		if (config.state == VRR_STATE_ACTIVE_FIXED && config.fixed_refresh_in_uhz &&
		    (!drm_atomic_crtc_needs_modeset(&new_crtc_state->base) ||
		     new_crtc_state->freesync_config.state == VRR_STATE_ACTIVE_FIXED)) {
			vrr_params.max_refresh_in_uhz = config.max_refresh_in_uhz;
			vrr_params.min_refresh_in_uhz = config.min_refresh_in_uhz;
			vrr_params.fixed_refresh_in_uhz = config.fixed_refresh_in_uhz;
			vrr_params.state = VRR_STATE_ACTIVE_FIXED;
		} else {
			config.state = new_crtc_state->base.vrr_enabled ?
						     VRR_STATE_ACTIVE_VARIABLE :
						     VRR_STATE_INACTIVE;
		}
	} else {
		config.state = VRR_STATE_UNSUPPORTED;
	}

	mod_freesync_build_vrr_params(dm->freesync_module,
				      new_stream,
				      &config, &vrr_params);

	new_crtc_state->freesync_config = config;
	/* Copy state for access from DM IRQ handler */
	acrtc->dm_irq_params.freesync_config = config;
	acrtc->dm_irq_params.active_planes = new_crtc_state->active_planes;
	acrtc->dm_irq_params.vrr_params = vrr_params;
	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
}

static void amdgpu_dm_handle_vrr_transition(struct amdgpu_display_manager *dm,
					    struct dm_crtc_state *old_state,
					    struct dm_crtc_state *new_state)
{
	struct amdgpu_device *adev = dm->adev;
	bool old_vrr_active = amdgpu_dm_crtc_vrr_active(old_state);
	bool new_vrr_active = amdgpu_dm_crtc_vrr_active(new_state);

	/* Only DCE gates vupdate on VRR, keep it enabled for DCN */
	bool vrr_gates_vupdate = amdgpu_ip_version(adev, DCE_HWIP, 0) == 0;

	if (!old_vrr_active && new_vrr_active) {
		/* Transition VRR inactive -> active:
		 * While VRR is active, we must not disable vblank irq, as a
		 * reenable after disable would compute bogus vblank/pflip
		 * timestamps if it likely happened inside display front-porch.
		 *
		 * We also need vupdate irq for the actual core vblank handling
		 * at end of vblank.
		 */
		if (vrr_gates_vupdate)
			WARN_ON(amdgpu_dm_crtc_set_vupdate_irq(new_state->base.crtc, true) != 0);
		WARN_ON(drm_crtc_vblank_get(new_state->base.crtc) != 0);
		drm_dbg_driver(new_state->base.crtc->dev, "%s: crtc=%u VRR off->on: Get vblank ref\n",
				 __func__, new_state->base.crtc->base.id);

		scoped_guard(mutex, &dm->dc_lock) {
			dc_exit_ips_for_hw_access(dm->dc);
			amdgpu_dm_psr_set_event(dm, new_state->stream, true,
				psr_event_vrr_transition, true);
			amdgpu_dm_replay_set_event(dm, new_state->stream, true,
				replay_event_vrr, true);
		}
	} else if (old_vrr_active && !new_vrr_active) {
		/* Transition VRR active -> inactive:
		 * Allow vblank irq disable again for fixed refresh rate.
		 */
		if (vrr_gates_vupdate)
			WARN_ON(amdgpu_dm_crtc_set_vupdate_irq(new_state->base.crtc, false) != 0);
		drm_crtc_vblank_put(new_state->base.crtc);
		drm_dbg_driver(new_state->base.crtc->dev, "%s: crtc=%u VRR on->off: Drop vblank ref\n",
				 __func__, new_state->base.crtc->base.id);

		scoped_guard(mutex, &dm->dc_lock) {
			dc_exit_ips_for_hw_access(dm->dc);
			amdgpu_dm_psr_set_event(dm, new_state->stream, false,
				psr_event_vrr_transition, false);
			amdgpu_dm_replay_set_event(dm, new_state->stream, false,
				replay_event_vrr, false);
		}
	}
}

static void amdgpu_dm_commit_cursors(struct drm_atomic_commit *state)
{
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	int i;

	/*
	 * TODO: Make this per-stream so we don't issue redundant updates for
	 * commits with multiple streams.
	 */
	for_each_old_plane_in_state(state, plane, old_plane_state, i)
		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			amdgpu_dm_plane_handle_cursor_update(plane, old_plane_state);
}

static inline uint32_t get_mem_type(struct drm_framebuffer *fb)
{
	struct amdgpu_bo *abo = gem_to_amdgpu_bo(fb->obj[0]);

	return abo->tbo.resource ? abo->tbo.resource->mem_type : 0;
}

static void amdgpu_dm_update_cursor(struct drm_plane *plane,
				    struct drm_plane_state *old_plane_state,
				    struct dc_stream_update *update)
{
	struct amdgpu_device *adev = drm_to_adev(plane->dev);
	struct amdgpu_framebuffer *afb = to_amdgpu_framebuffer(plane->state->fb);
	struct drm_crtc *crtc = afb ? plane->state->crtc : old_plane_state->crtc;
	struct dm_crtc_state *crtc_state = crtc ? to_dm_crtc_state(crtc->state) : NULL;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	uint64_t address = afb ? afb->address : 0;
	struct dc_cursor_position position = {0};
	struct dc_cursor_attributes attributes;
	int ret;

	if (!plane->state->fb && !old_plane_state->fb)
		return;

	drm_dbg_atomic(plane->dev, "crtc_id=%d with size %d to %d\n",
		       amdgpu_crtc->crtc_id, plane->state->crtc_w,
		       plane->state->crtc_h);

	ret = amdgpu_dm_plane_get_cursor_position(plane, crtc, &position);
	if (ret)
		return;

	if (!position.enable) {
		/* turn off cursor */
		if (crtc_state && crtc_state->stream) {
			dc_stream_set_cursor_position(crtc_state->stream,
						      &position);
			update->cursor_position = &crtc_state->stream->cursor_position;
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

	/* Enable cursor degamma ROM on DCN3+ for implicit sRGB degamma in DRM
	 * legacy gamma setup.
	 */
	if (crtc_state->cm_is_degamma_srgb &&
	    adev->dm.dc->caps.color.dpp.gamma_corr)
		attributes.attribute_flags.bits.ENABLE_CURSOR_DEGAMMA = 1;

	if (afb)
		attributes.pitch = afb->base.pitches[0] / afb->base.format->cpp[0];

	if (crtc_state->stream) {
		if (!dc_stream_set_cursor_attributes(crtc_state->stream,
						     &attributes))
			drm_err(adev_to_drm(adev), "DC failed to set cursor attributes\n");

		update->cursor_attributes = &crtc_state->stream->cursor_attributes;

		if (!dc_stream_set_cursor_position(crtc_state->stream,
						   &position))
			drm_err(adev_to_drm(adev), "DC failed to set cursor position\n");

		update->cursor_position = &crtc_state->stream->cursor_position;
	}
}

static void amdgpu_dm_enable_self_refresh(struct amdgpu_display_manager *dm,
					  struct amdgpu_crtc *acrtc_attach,
					  const struct dm_crtc_state *acrtc_state,
					  const u64 current_ts)
{
	struct psr_settings *psr = &acrtc_state->stream->link->psr_settings;
	struct replay_settings *pr = &acrtc_state->stream->link->replay_settings;
	struct amdgpu_dm_connector *aconn =
		(struct amdgpu_dm_connector *)acrtc_state->stream->dm_stream_context;

	/* Decrement skip count when SR is enabled and we're doing fast updates. */
	if (acrtc_state->update_type == UPDATE_TYPE_FAST &&
	    (psr->psr_feature_enabled || pr->replay_feature_enabled)) {
		if (aconn->sr_skip_count > 0)
			aconn->sr_skip_count--;

		/* Allow SR when skip count is 0. */
		acrtc_attach->dm_irq_params.allow_sr_entry = !aconn->sr_skip_count;

		/*
		 * If sink supports PSR SU/Panel Replay, there is no need to rely on
		 * a vblank event disable request to enable PSR/RP. PSR SU/RP
		 * can be enabled immediately once OS demonstrates an
		 * adequate number of fast atomic commits to notify KMD
		 * of update events.
		 * See `amdgpu_dm_crtc_vblank_control_worker()`.
		 */
		if (acrtc_attach->dm_irq_params.allow_sr_entry &&
			(current_ts - psr->psr_dirty_rects_change_timestamp_ns) > 500000000) {
			amdgpu_dm_psr_set_event(dm, acrtc_state->stream, false,
				psr_event_hw_programming, false);

			amdgpu_dm_replay_set_event(dm, acrtc_state->stream, false,
				replay_event_hw_programming, false);
		}
	} else {
		acrtc_attach->dm_irq_params.allow_sr_entry = false;
	}
}

static void dm_arm_vblank_event(struct amdgpu_crtc *acrtc,
				struct dm_crtc_state *acrtc_state,
				bool pflip_update,
				bool cursor_update)
{
	assert_spin_locked(&acrtc->base.dev->event_lock);

	if (!acrtc->base.state->event || acrtc_state->active_planes == 0)
		return;

	if (pflip_update) {
		drm_crtc_vblank_get(&acrtc->base);
		WARN_ON(acrtc->pflip_status != AMDGPU_FLIP_NONE);
		/* Arm flip completion handling and event delivery after programming. */
		prepare_flip_isr(acrtc);
	} else if (cursor_update) {
		drm_crtc_vblank_get(&acrtc->base);
		acrtc->event = acrtc->base.state->event;
		acrtc->base.state->event = NULL;
	}
}

static void amdgpu_dm_commit_planes(struct drm_atomic_commit *state,
				    struct drm_device *dev,
				    struct amdgpu_display_manager *dm,
				    struct drm_crtc *pcrtc,
				    bool wait_for_vblank)
{
	u32 i;
	u64 timestamp_ns = ktime_get_ns();
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	struct amdgpu_crtc *acrtc_attach = to_amdgpu_crtc(pcrtc);
	struct drm_crtc_state *new_pcrtc_state =
			drm_atomic_get_new_crtc_state(state, pcrtc);
	struct dm_crtc_state *acrtc_state = to_dm_crtc_state(new_pcrtc_state);
	struct dm_crtc_state *dm_old_crtc_state =
			to_dm_crtc_state(drm_atomic_get_old_crtc_state(state, pcrtc));
	int planes_count = 0, vpos, hpos;
	unsigned long flags;
	u32 target_vblank, last_flip_vblank;
	bool vrr_active = amdgpu_dm_crtc_vrr_active(acrtc_state);
	bool cursor_update = false;
	bool pflip_present = false;
	bool immediate_flip = false;
	bool flip_latched_during_prog = false;
	bool dirty_rects_changed = false;
	bool updated_planes_and_streams = false;
	struct {
		struct dc_surface_update surface_updates[MAX_SURFACES];
		struct dc_plane_info plane_infos[MAX_SURFACES];
		struct dc_scaling_info scaling_infos[MAX_SURFACES];
		struct dc_flip_addrs flip_addrs[MAX_SURFACES];
		struct dc_stream_update stream_update;
	} *bundle;

	bundle = kzalloc_obj(*bundle);

	if (!bundle) {
		drm_err(dev, "Failed to allocate update bundle\n");
		goto cleanup;
	}

	/*
	 * Disable the cursor first if we're disabling all the planes.
	 * It'll remain on the screen after the planes are re-enabled
	 * if we don't.
	 *
	 * If the cursor is transitioning from native to overlay mode, the
	 * native cursor needs to be disabled first.
	 */
	if (acrtc_state->cursor_mode == DM_CURSOR_OVERLAY_MODE &&
	    dm_old_crtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE) {
		struct dc_cursor_position cursor_position = {0};

		if (!dc_stream_set_cursor_position(acrtc_state->stream,
						   &cursor_position))
			drm_err(dev, "DC failed to disable native cursor\n");

		bundle->stream_update.cursor_position =
				&acrtc_state->stream->cursor_position;
	}

	if (acrtc_state->active_planes == 0 &&
	    dm_old_crtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE)
		amdgpu_dm_commit_cursors(state);

	/* update planes when needed */
	for_each_oldnew_plane_in_state(state, plane, old_plane_state, new_plane_state, i) {
		struct drm_crtc *crtc = new_plane_state->crtc;
		struct drm_crtc_state *new_crtc_state;
		struct drm_framebuffer *fb = new_plane_state->fb;
		struct amdgpu_framebuffer *afb = (struct amdgpu_framebuffer *)fb;
		bool plane_needs_flip;
		struct dc_plane_state *dc_plane;
		struct dm_plane_state *dm_new_plane_state = to_dm_plane_state(new_plane_state);

		/* Cursor plane is handled after stream updates */
		if (plane->type == DRM_PLANE_TYPE_CURSOR &&
		    acrtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE) {
			if ((fb && crtc == pcrtc) ||
			    (old_plane_state->fb && old_plane_state->crtc == pcrtc)) {
				cursor_update = true;
				if (amdgpu_ip_version(dm->adev, DCE_HWIP, 0) != 0)
					amdgpu_dm_update_cursor(plane, old_plane_state, &bundle->stream_update);
			}

			continue;
		}

		if (!fb || !crtc || pcrtc != crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
		if (!new_crtc_state->active)
			continue;

		dc_plane = dm_new_plane_state->dc_state;
		if (!dc_plane)
			continue;

		bundle->surface_updates[planes_count].surface = dc_plane;
		if (new_pcrtc_state->color_mgmt_changed) {
			bundle->surface_updates[planes_count].gamma = &dc_plane->gamma_correction;
			bundle->surface_updates[planes_count].in_transfer_func = &dc_plane->in_transfer_func;
			bundle->surface_updates[planes_count].gamut_remap_matrix = &dc_plane->gamut_remap_matrix;
			bundle->surface_updates[planes_count].hdr_mult = dc_plane->hdr_mult;
			bundle->surface_updates[planes_count].cm = &dc_plane->cm;
		}

		amdgpu_dm_plane_fill_dc_scaling_info(dm->adev, new_plane_state,
				     &bundle->scaling_infos[planes_count]);

		bundle->surface_updates[planes_count].scaling_info =
			&bundle->scaling_infos[planes_count];

		plane_needs_flip = old_plane_state->fb && new_plane_state->fb;

		pflip_present = pflip_present || plane_needs_flip;

		if (!plane_needs_flip) {
			planes_count += 1;
			continue;
		}

		fill_dc_plane_info_and_addr(
			dm->adev, new_plane_state,
			afb->tiling_flags,
			&bundle->plane_infos[planes_count],
			&bundle->flip_addrs[planes_count].address,
			afb->tmz_surface);

		drm_dbg_state(state->dev, "plane: id=%d dcc_en=%d\n",
				 new_plane_state->plane->index,
				 bundle->plane_infos[planes_count].dcc.enable);

		bundle->surface_updates[planes_count].plane_info =
			&bundle->plane_infos[planes_count];

		if (acrtc_state->stream->link->psr_settings.psr_feature_enabled ||
		    acrtc_state->stream->link->replay_settings.replay_feature_enabled) {
			fill_dc_dirty_rects(plane, old_plane_state,
					    new_plane_state, new_crtc_state,
					    &bundle->flip_addrs[planes_count],
					    acrtc_state->stream->link->psr_settings.psr_version ==
					    DC_PSR_VERSION_SU_1,
					    &dirty_rects_changed);

			/*
			 * If the dirty regions changed, PSR-SU need to be disabled temporarily
			 * and enabled it again after dirty regions are stable to avoid video glitch.
			 * PSR-SU will be enabled in
			 * amdgpu_dm_crtc_vblank_control_worker() if user
			 * pause the video during the PSR-SU was disabled.
			 */
			if (acrtc_state->stream->link->psr_settings.psr_version >= DC_PSR_VERSION_SU_1 &&
			    acrtc_attach->dm_irq_params.allow_sr_entry &&
			    dirty_rects_changed) {
				mutex_lock(&dm->dc_lock);
				acrtc_state->stream->link->psr_settings.psr_dirty_rects_change_timestamp_ns =
				timestamp_ns;
				dc_exit_ips_for_hw_access(dm->dc);
				amdgpu_dm_psr_set_event(dm, acrtc_state->stream, true,
					psr_event_hw_programming, true);
				mutex_unlock(&dm->dc_lock);
			}
		}

		/*
		 * Only allow immediate flips for fast updates that don't
		 * change memory domain, FB pitch, DCC state, rotation or
		 * mirroring.
		 *
		 * dm_crtc_helper_atomic_check() only accepts async flips with
		 * fast updates.
		 */
		if (crtc->state->async_flip &&
		    (acrtc_state->update_type != UPDATE_TYPE_FAST ||
		     get_mem_type(old_plane_state->fb) != get_mem_type(fb)))
			drm_warn_once(state->dev,
				      "[PLANE:%d:%s] async flip with non-fast update\n",
				      plane->base.id, plane->name);

		bundle->flip_addrs[planes_count].flip_immediate =
			crtc->state->async_flip &&
			acrtc_state->update_type == UPDATE_TYPE_FAST &&
			get_mem_type(old_plane_state->fb) == get_mem_type(fb);

		immediate_flip |= bundle->flip_addrs[planes_count].flip_immediate;

		timestamp_ns = ktime_get_ns();
		bundle->flip_addrs[planes_count].flip_timestamp_in_us = div_u64(timestamp_ns, 1000);
		bundle->surface_updates[planes_count].flip_addr = &bundle->flip_addrs[planes_count];
		bundle->surface_updates[planes_count].surface = dc_plane;

		if (!bundle->surface_updates[planes_count].surface) {
			drm_err(dev, "No surface for CRTC: id=%d\n",
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

		drm_dbg_state(state->dev, "%s Flipping to hi: 0x%x, low: 0x%x\n",
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
			last_flip_vblank = amdgpu_get_vblank_counter_kms(pcrtc);
		} else {
			/* For variable refresh rate mode only:
			 * Get vblank of last completed flip to avoid > 1 vrr
			 * flips per video frame by use of throttling, but allow
			 * flip programming anywhere in the possibly large
			 * variable vrr vblank interval for fine-grained flip
			 * timing control and more opportunity to avoid stutter
			 * on late submission of flips.
			 */
			spin_lock_irqsave(&pcrtc->dev->event_lock, flags);
			last_flip_vblank = acrtc_attach->dm_irq_params.last_flip_vblank;
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
			  amdgpu_get_vblank_counter_kms(pcrtc)) > 0)) {
			usleep_range(1000, 1100);
		}

		if (acrtc_state->stream) {
			if (acrtc_state->freesync_vrr_info_changed)
				bundle->stream_update.vrr_infopacket =
					&acrtc_state->stream->vrr_infopacket;
		}
	}

	/*
	 * DCE depends on a combination of GRPH_FLIP, VLINE0, and VUPDATE for
	 * event delivery. Only GRPH_FLIP handler can send pflip events, and it
	 * only fires if HW latched to the flip. Maintain legacy behavior by
	 * arming event before programming.
	 */
	if (amdgpu_ip_version(dm->adev, DCE_HWIP, 0) == 0) {
		scoped_guard(spinlock_irqsave, &pcrtc->dev->event_lock) {
			dm_arm_vblank_event(acrtc_attach, acrtc_state,
					pflip_present, cursor_update);
		}
	}

	/* Update the planes if changed or disable if we don't have any. */
	if ((planes_count || acrtc_state->active_planes == 0) &&
		acrtc_state->stream) {
		/*
		 * If PSR or idle optimizations are enabled then flush out
		 * any pending work before hardware programming.
		 */
		if (dm->vblank_control_workqueue)
			flush_workqueue(dm->vblank_control_workqueue);

		bundle->stream_update.stream = acrtc_state->stream;
		if (new_pcrtc_state->mode_changed) {
			bundle->stream_update.src = acrtc_state->stream->src;
			bundle->stream_update.dst = acrtc_state->stream->dst;
		}

		if (new_pcrtc_state->color_mgmt_changed) {
			/*
			 * TODO: This isn't fully correct since we've actually
			 * already modified the stream in place.
			 */
			bundle->stream_update.gamut_remap =
				&acrtc_state->stream->gamut_remap_matrix;
			bundle->stream_update.output_csc_transform =
				&acrtc_state->stream->csc_color_matrix;
			bundle->stream_update.out_transfer_func =
				&acrtc_state->stream->out_transfer_func;
			bundle->stream_update.lut3d_func =
				(struct dc_3dlut *) acrtc_state->stream->lut3d_func;
			bundle->stream_update.func_shaper =
				(struct dc_transfer_func *) acrtc_state->stream->func_shaper;
		}

		acrtc_state->stream->abm_level = acrtc_state->abm_level;
		if (acrtc_state->abm_level != dm_old_crtc_state->abm_level)
			bundle->stream_update.abm_level = &acrtc_state->abm_level;

		/*
		 * If FreeSync state on the stream has changed then we need to
		 * re-adjust the min/max bounds now that DC doesn't handle this
		 * as part of commit.
		 */
		if (is_dc_timing_adjust_needed(dm_old_crtc_state, acrtc_state)) {
			spin_lock_irqsave(&pcrtc->dev->event_lock, flags);
			dc_stream_adjust_vmin_vmax(
				dm->dc, acrtc_state->stream,
				&acrtc_attach->dm_irq_params.vrr_params.adjust);
			spin_unlock_irqrestore(&pcrtc->dev->event_lock, flags);
		}
		mutex_lock(&dm->dc_lock);
		update_planes_and_stream_adapter(dm->dc,
					 acrtc_state->update_type,
					 planes_count,
					 acrtc_state->stream,
					 &bundle->stream_update,
					 bundle->surface_updates);
		updated_planes_and_streams = true;

		/**
		 * Enable or disable the interrupts on the backend.
		 *
		 * Most pipes are put into power gating when unused.
		 *
		 * When power gating is enabled on a pipe we lose the
		 * interrupt enablement state when power gating is disabled.
		 *
		 * So we need to update the IRQ control state in hardware
		 * whenever the pipe turns on (since it could be previously
		 * power gated) or off (since some pipes can't be power gated
		 * on some ASICs).
		 */
		if (dm_old_crtc_state->active_planes != acrtc_state->active_planes)
			dm_update_pflip_irq_state(drm_to_adev(dev),
						  acrtc_attach);
		amdgpu_dm_enable_self_refresh(dm, acrtc_attach, acrtc_state,
					      timestamp_ns);
		mutex_unlock(&dm->dc_lock);
	}

	/*
	 * Update cursor state *after* programming all the planes.
	 * This avoids redundant programming in the case where we're going
	 * to be disabling a single plane - those pipes are being disabled.
	 */
	if (acrtc_state->active_planes &&
	    (!updated_planes_and_streams || amdgpu_ip_version(dm->adev, DCE_HWIP, 0) == 0) &&
	    acrtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE)
		amdgpu_dm_commit_cursors(state);

	/*
	 * DCN specific vblank handling
	 * ============================
	 *
	 * With the event_lock held, arm the vblank event, and determine whether
	 * deliver it immediately, or in VUPDATE_NO_LOCK IRQ (i.e. HW latch
	 * point) handler. Do this *after* programming so that the IRQ handler
	 * will not deliver the event before HW laches onto the programmed
	 * values:
	 *
	 *     Commit thread      IRQ handler                       HW
	 *     -----------------------------------------------------------------
	 *     arm_vblank_event()
	 *                                                          vupdate()
	 *                        vupdate_handler()
	 *                          cook_timestamp()
	 *                          # prev flip already latched,
	 *                          # so flip_latched == true.
	 *                          if event_armed && flip_latched:
	 *                            send_vblank_event()
	 *                            # sent before latch, **BAD!**
	 *     hw_program()
	 *                                                          vupdate()
	 *                                                          **latch**
	 *
	 * There's a consequence of arming after: it's possible for HW to latch
	 * between start of HW programming and acrtc->event/pflip_status arming.
	 * When this happens, the IRQ handler will send the event on the next
	 * immediate latch point, even though HW has already latched. This is
	 * handled by optimistically checking for HW latch after programming,
	 * and if latched, send the event immediately:
	 *
	 *     Commit thread             IRQ handler                HW
	 *     -----------------------------------------------------------------
	 *     hw_program()
	 *                                                          vupdate()
	 *                                                          **latch**
	 *                               vupdate_handler()
	 *                                 cook_timestamp()
	 *                                 # event_armed == false
	 *                                 # **no event sent!**
	 *     arm_vblank_event()
	 *     if flip_latched:
	 *       **send_vblank_event()**
	 *       disarm_vblank_event()
	 *
	 * The IRQ handler is expected to cook the timestamp, but we need to
	 * cook the timestamp before optimistic sending as well. That's because
	 * the following sequence is possible:
	 *
	 *     Commit thread              IRQ handler              HW
	 *     -----------------------------------------------------------------
	 *     hw_program()
	 *     arm_vblank_event()
	 *                                                         vupdate()
	 *                                                         **latch**
	 *     if flip_latched:
	 *       # Need cook before send!
	 *       **cook_timestamp()**
	 *       send_vblank_event()
	 *       disarm_vblank_event()
	 *                                vupdate_handler()
	 *                                  cook_timestamp()
	 *                                  # event_armed == false
	 *                                  # no event sent!
	 *
	 * Cooking twice is OK, since DRM scanout accurate timestamps report A)
	 * the previous vactive start if currently in vactive, or B) the next
	 * vactive start if currently in vblank (see &get_vblank_counter). 'A)'
	 * is what we want for the optimistic send, and for 'B)', we'll cook a
	 * timestamp no later than the next IRQ handler run.
	 *
	 * The more correct fix is to wrap programming and arming with the
	 * event_lock and thus serializing it with the IRQ handler. However,
	 * there are various sleep-waits within
	 * update_planes_and_stream_adapter() that makes spin locking illegal.
	 * And on full updates, it can take 1-2 frame-times to return (see
	 * commit_planes_for_stream).
	 *
	 * On DCE, GRPH_PFLIP IRQ is used and takes care of this.
	 */
	if (amdgpu_ip_version(dm->adev, DCE_HWIP, 0) != 0) {
		spin_lock_irqsave(&pcrtc->dev->event_lock, flags);

		if (updated_planes_and_streams) {
			flip_latched_during_prog =
				!dc_get_flip_pending_on_otg(dm->dc, acrtc_attach->otg_inst);
		}

		dm_arm_vblank_event(acrtc_attach, acrtc_state,
				    pflip_present, cursor_update);

		/*
		 * Deliver the event immediately on immediate flip, or on a
		 * update that has already latched.
		 */
		if ((immediate_flip || flip_latched_during_prog) &&
		    acrtc_attach->pflip_status == AMDGPU_FLIP_SUBMITTED &&
		    acrtc_attach->event) {
			drm_crtc_accurate_vblank_count(&acrtc_attach->base);
			drm_crtc_send_vblank_event(&acrtc_attach->base,
						   acrtc_attach->event);
			acrtc_attach->event = NULL;
			drm_crtc_vblank_put(&acrtc_attach->base);
			acrtc_attach->pflip_status = AMDGPU_FLIP_NONE;
		}
		spin_unlock_irqrestore(&pcrtc->dev->event_lock, flags);
	}

cleanup:
	kfree(bundle);
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

/**
 * amdgpu_dm_crtc_complete_writeback - finish a pending writeback job
 * @acrtc: the CRTC whose pending writeback should be completed
 *
 * Clears the pending state, signals the writeback out fence and releases the
 * vblank reference taken in dm_set_writeback() while the writeback was armed.
 * The pending flag is tested and cleared under the writeback job lock, so this
 * is safe to call concurrently from the completion vblank IRQ
 * (dm_crtc_high_irq()) and from the writeback teardown path
 * (dm_clear_writeback()); only the caller that observes the pending job
 * performs the completion.
 *
 * Return: true if a pending writeback job was completed by this call.
 */
bool amdgpu_dm_crtc_complete_writeback(struct amdgpu_crtc *acrtc)
{
	unsigned long flags;
	bool pending;

	if (!acrtc->wb_conn)
		return false;

	spin_lock_irqsave(&acrtc->wb_conn->job_lock, flags);
	pending = acrtc->wb_pending;
	acrtc->wb_pending = false;
	spin_unlock_irqrestore(&acrtc->wb_conn->job_lock, flags);

	if (!pending)
		return false;

	drm_writeback_signal_completion(acrtc->wb_conn, 0);
	drm_crtc_vblank_put(&acrtc->base);

	return true;
}

static void dm_clear_writeback(struct amdgpu_display_manager *dm,
			      struct amdgpu_crtc *acrtc,
			      struct dm_crtc_state *crtc_state)
{
	dc_stream_remove_writeback(dm->dc, crtc_state->stream, 0);

	/*
	 * If the writeback is still pending when it is torn down (its
	 * completion vblank IRQ never fired), signal the out fence so a
	 * waiting client does not stall and release the vblank reference
	 * taken in dm_set_writeback().
	 */
	amdgpu_dm_crtc_complete_writeback(acrtc);
}

/**
 * amdgpu_dm_mod_power_update_streams - update mod_power stream state on modeset
 * @state: the drm atomic state
 * @dm: the display manager to update mod_power on
 *
 * Notify mod_power of stream changes on modeset events, and disable PSR/Replay
 * in preparation for hardware programming. See also
 * amdgpu_dm_mod_power_setup_streams() for post-modeset mod_power setup.
 */
static void amdgpu_dm_mod_power_update_streams(struct drm_atomic_commit *state,
					       struct amdgpu_display_manager *dm)
{
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct amdgpu_dm_connector *aconnector;
	struct drm_crtc *crtc;
	int i = 0;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

		/*
		 * Update mod_power on modeset event in preparation for hw
		 * programming. Always use the old stream, since it would have
		 * been previously added to mod_power. If old stream is null (on
		 * crtc enable, for example), mod_power will no-op, which is the
		 * desried behavior.
		 */
		if (old_crtc_state->active) {
			scoped_guard(mutex, &dm->dc_lock) {
				dc_exit_ips_for_hw_access(dm->dc);
				amdgpu_dm_psr_set_event(dm, dm_old_crtc_state->stream, true,
					psr_event_hw_programming, true);
				amdgpu_dm_replay_set_event(dm, dm_old_crtc_state->stream, true,
					replay_event_hw_programming, true);
				amdgpu_dm_replay_set_event(dm, dm_old_crtc_state->stream, false,
					replay_event_general_ui, false);
			}
		}

		if (new_crtc_state->active) {
			aconnector = (struct amdgpu_dm_connector *)
				dm_new_crtc_state->stream->dm_stream_context;
			if (old_crtc_state->active) {
				mod_power_replace_stream(dm->power_module,
					dm_old_crtc_state->stream,
					dm_new_crtc_state->stream,
					&aconnector->psr_caps);
			} else {
				mod_power_add_stream(dm->power_module,
					dm_new_crtc_state->stream,
					&aconnector->psr_caps);
			}
		} else if (old_crtc_state->active) {
			mod_power_remove_stream(dm->power_module,
				dm_old_crtc_state->stream);
		}
	}
}

/**
 * amdgpu_dm_mod_power_setup_streams - setup mod_power stream state post modeset
 * @state: the drm atomic state
 * @dm: the display manager to update mod_power on
 *
 * Notify mod_power of mode_change. This needs to be done after dc_stream
 * updates have been committed, and VRR parameters have been updated.
 */
static void amdgpu_dm_mod_power_setup_streams(struct drm_atomic_commit *state,
					      struct amdgpu_display_manager *dm)
{
	struct dm_crtc_state *dm_new_crtc_state;
	struct drm_crtc_state *new_crtc_state;
	struct amdgpu_crtc *acrtc;
	struct drm_crtc *crtc;
	int i = 0;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		acrtc = to_amdgpu_crtc(crtc);

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

		if (new_crtc_state->active) {
			amdgpu_dm_link_setup_replay(dm_new_crtc_state->stream,
					&acrtc->dm_irq_params.vrr_params);
			mod_power_notify_mode_change(dm->power_module,
						dm_new_crtc_state->stream,
						false);

			/*
			 * Block PSR / Replay on the new stream until display settles post-modeset.
			 * These events will be cleared by amdgpu_dm_enable_self_refresh() once
			 * allow_sr_entry becomes true.
			 */
			amdgpu_dm_psr_set_event(dm, dm_new_crtc_state->stream, true,
				psr_event_hw_programming, true);

			amdgpu_dm_replay_set_event(dm, dm_new_crtc_state->stream, true,
				replay_event_hw_programming | replay_event_general_ui,
				true);
		}
	}

}

static void amdgpu_dm_commit_streams(struct drm_atomic_commit *state,
					struct dc_state *dc_state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_display_manager *dm = &adev->dm;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	struct drm_connector_state *old_con_state;
	struct drm_connector *connector;
	bool mode_set_reset_required = false;
	u32 i;
	struct dc_commit_streams_params params = {dc_state->streams, dc_state->stream_count};
	bool set_backlight_level = false;

	/* Disable writeback */
	for_each_old_connector_in_state(state, connector, old_con_state, i) {
		struct dm_connector_state *dm_old_con_state;
		struct amdgpu_crtc *acrtc;

		if (connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		old_crtc_state = NULL;

		dm_old_con_state = to_dm_connector_state(old_con_state);
		if (!dm_old_con_state->base.crtc)
			continue;

		acrtc = to_amdgpu_crtc(dm_old_con_state->base.crtc);
		if (acrtc)
			old_crtc_state = drm_atomic_get_old_crtc_state(state, &acrtc->base);

		if (!acrtc || !acrtc->wb_enabled)
			continue;

		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		dm_clear_writeback(dm, acrtc, dm_old_crtc_state);
		acrtc->wb_enabled = false;
	}

	amdgpu_dm_mod_power_update_streams(state, dm);

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state,
				      new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		if (old_crtc_state->active &&
		    (!new_crtc_state->active ||
		     drm_atomic_crtc_needs_modeset(new_crtc_state))) {
			manage_dm_interrupts(adev, acrtc, NULL);
			dc_stream_release(dm_old_crtc_state->stream);
		}
	}

	drm_atomic_helper_calc_timestamping_constants(state);

	/* update changed items */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		drm_dbg_state(state->dev,
			"amdgpu_crtc id:%d crtc_state_flags: enable:%d, active:%d, planes_changed:%d, mode_changed:%d,active_changed:%d,connectors_changed:%d\n",
			acrtc->crtc_id,
			new_crtc_state->enable,
			new_crtc_state->active,
			new_crtc_state->planes_changed,
			new_crtc_state->mode_changed,
			new_crtc_state->active_changed,
			new_crtc_state->connectors_changed);

		/* Disable cursor if disabling crtc */
		if (old_crtc_state->active && !new_crtc_state->active) {
			struct dc_cursor_position position;

			memset(&position, 0, sizeof(position));
			mutex_lock(&dm->dc_lock);
			dc_exit_ips_for_hw_access(dm->dc);
			dc_stream_program_cursor_position(dm_old_crtc_state->stream, &position);
			mutex_unlock(&dm->dc_lock);
		}

		/* Copy all transient state flags into dc state */
		if (dm_new_crtc_state->stream) {
			amdgpu_dm_crtc_copy_transient_flags(&dm_new_crtc_state->base,
							    dm_new_crtc_state->stream);
		}

		/* handles headless hotplug case, updating new_state and
		 * aconnector as needed
		 */

		if (amdgpu_dm_crtc_modeset_required(new_crtc_state, dm_new_crtc_state->stream, dm_old_crtc_state->stream)) {

			drm_dbg_atomic(dev,
				       "Atomic commit: SET crtc id %d: [%p]\n",
				       acrtc->crtc_id, acrtc);

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
				drm_dbg_atomic(dev,
					       "Failed to create new stream for crtc %d\n",
						acrtc->base.base.id);
				continue;
			}

			if (dm_old_crtc_state->stream)
				remove_stream(adev, acrtc, dm_old_crtc_state->stream);

			pm_runtime_get_noresume(dev->dev);

			acrtc->enabled = true;
			acrtc->hw_mode = new_crtc_state->mode;
			crtc->hwmode = new_crtc_state->mode;
			mode_set_reset_required = true;
			set_backlight_level = true;
		} else if (modereset_required(new_crtc_state)) {
			drm_dbg_atomic(dev,
				       "Atomic commit: RESET. crtc id %d:[%p]\n",
				       acrtc->crtc_id, acrtc);
			/* i.e. reset mode */
			if (dm_old_crtc_state->stream)
				remove_stream(adev, acrtc, dm_old_crtc_state->stream);

			mode_set_reset_required = true;
		}
	} /* for_each_crtc_in_state() */

	/* if there mode set or reset, flush vblank work queue */
	if (mode_set_reset_required) {
		if (dm->vblank_control_workqueue)
			flush_workqueue(dm->vblank_control_workqueue);
	}

	dm_enable_per_frame_crtc_master_sync(dc_state);
	mutex_lock(&dm->dc_lock);
	dc_exit_ips_for_hw_access(dm->dc);
	WARN_ON(!dc_commit_streams(dm->dc, &params));

	bool frl_stream_found = false;

	for (i = 0; i < params.stream_count; i++) {
		struct dc_stream_state *stream = params.streams[i];

		if (stream->signal == SIGNAL_TYPE_HDMI_FRL) {
			frl_stream_found = true;
			break;
		}
	}
	if (frl_stream_found) {
		if (queue_delayed_work(dm->hdmi_frl_status_polling_wq,
				       &dm->hdmi_frl_status_polling_work,
				       msecs_to_jiffies(dm->hdmi_frl_status_polling_delay_ms)))
			drm_dbg_kms(dev, "200ms frl status polling starts ...\n");
	} else {
		if (cancel_delayed_work_sync(&dm->hdmi_frl_status_polling_work))
			drm_dbg_kms(dev, "200ms frl status polling stops ...\n");
	}
	/* Allow idle optimization when vblank count is 0 for display off */
	if ((dm->active_vblank_irq_count == 0) && amdgpu_dm_is_headless(dm->adev))
		dc_allow_idle_optimizations(dm->dc, true);
	mutex_unlock(&dm->dc_lock);

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (dm_new_crtc_state->stream != NULL) {
			const struct dc_stream_status *status =
					dc_stream_get_status(dm_new_crtc_state->stream);

			if (!status)
				status = dc_state_get_stream_status(dc_state,
									 dm_new_crtc_state->stream);
			if (!status)
				drm_err(dev,
					"got no status for stream %p on acrtc%p\n",
					dm_new_crtc_state->stream, acrtc);
			else
				acrtc->otg_inst = status->primary_otg_inst;
		}
	}

	/* During boot up and resume the DC layer will reset the panel brightness
	 * to fix a flicker issue.
	 * It will cause the dm->actual_brightness is not the current panel brightness
	 * level. (the dm->brightness is the correct panel level)
	 * So we set the backlight level with dm->brightness value after set mode
	 */
	if (set_backlight_level) {
		for (i = 0; i < dm->num_of_edps; i++) {
			if (dm->backlight_dev[i])
				amdgpu_dm_backlight_set_level(dm, i, dm->brightness[i]);
		}
	}
}

static void dm_set_writeback(struct amdgpu_display_manager *dm,
			      struct dm_crtc_state *crtc_state,
			      struct drm_connector *connector,
			      struct drm_connector_state *new_con_state)
{
	struct drm_writeback_connector *wb_conn = drm_connector_to_writeback(connector);
	struct amdgpu_device *adev = dm->adev;
	struct amdgpu_crtc *acrtc;
	struct dc_writeback_info *wb_info;
	struct pipe_ctx *pipe = NULL;
	struct amdgpu_framebuffer *afb;
	int i = 0;

	wb_info = kzalloc_obj(*wb_info);
	if (!wb_info) {
		drm_err(adev_to_drm(adev), "Failed to allocate wb_info\n");
		return;
	}

	acrtc = to_amdgpu_crtc(wb_conn->encoder.crtc);
	if (!acrtc) {
		drm_err(adev_to_drm(adev), "no amdgpu_crtc found\n");
		kfree(wb_info);
		return;
	}

	afb = to_amdgpu_framebuffer(new_con_state->writeback_job->fb);
	if (!afb) {
		drm_err(adev_to_drm(adev), "No amdgpu_framebuffer found\n");
		kfree(wb_info);
		return;
	}

	for (i = 0; i < MAX_PIPES; i++) {
		if (dm->dc->current_state->res_ctx.pipe_ctx[i].stream == crtc_state->stream) {
			pipe = &dm->dc->current_state->res_ctx.pipe_ctx[i];
			break;
		}
	}

	/* fill in wb_info */
	wb_info->wb_enabled = true;

	wb_info->dwb_pipe_inst = 0;
	wb_info->dwb_params.dwbscl_black_color = 0;
	wb_info->dwb_params.hdr_mult = 0x1F000;
	wb_info->dwb_params.csc_params.gamut_adjust_type = CM_GAMUT_ADJUST_TYPE_BYPASS;
	wb_info->dwb_params.csc_params.gamut_coef_format = CM_GAMUT_REMAP_COEF_FORMAT_S2_13;
	wb_info->dwb_params.output_depth = DWB_OUTPUT_PIXEL_DEPTH_10BPC;
	wb_info->dwb_params.cnv_params.cnv_out_bpc = DWB_CNV_OUT_BPC_10BPC;

	/* width & height from crtc */
	wb_info->dwb_params.cnv_params.src_width = acrtc->base.mode.crtc_hdisplay;
	wb_info->dwb_params.cnv_params.src_height = acrtc->base.mode.crtc_vdisplay;
	wb_info->dwb_params.dest_width = acrtc->base.mode.crtc_hdisplay;
	wb_info->dwb_params.dest_height = acrtc->base.mode.crtc_vdisplay;

	wb_info->dwb_params.cnv_params.crop_en = false;
	wb_info->dwb_params.stereo_params.stereo_enabled = false;

	wb_info->dwb_params.cnv_params.out_max_pix_val = 0x3ff;	// 10 bits
	wb_info->dwb_params.cnv_params.out_min_pix_val = 0;
	wb_info->dwb_params.cnv_params.fc_out_format = DWB_OUT_FORMAT_32BPP_ARGB;
	wb_info->dwb_params.cnv_params.out_denorm_mode = DWB_OUT_DENORM_BYPASS;

	wb_info->dwb_params.out_format = dwb_scaler_mode_bypass444;

	wb_info->dwb_params.capture_rate = dwb_capture_rate_0;

	wb_info->dwb_params.scaler_taps.h_taps = 1;
	wb_info->dwb_params.scaler_taps.v_taps = 1;
	wb_info->dwb_params.scaler_taps.h_taps_c = 1;
	wb_info->dwb_params.scaler_taps.v_taps_c = 1;
	wb_info->dwb_params.subsample_position = DWB_INTERSTITIAL_SUBSAMPLING;

	wb_info->mcif_buf_params.luma_pitch = afb->base.pitches[0];
	wb_info->mcif_buf_params.chroma_pitch = afb->base.pitches[1];

	for (i = 0; i < DWB_MCIF_BUF_COUNT; i++) {
		wb_info->mcif_buf_params.luma_address[i] = afb->address;
		wb_info->mcif_buf_params.chroma_address[i] = 0;
	}

	wb_info->mcif_buf_params.p_vmid = 1;
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) >= IP_VERSION(3, 0, 0)) {
		wb_info->mcif_warmup_params.start_address.quad_part = afb->address;
		wb_info->mcif_warmup_params.region_size =
			wb_info->mcif_buf_params.luma_pitch * wb_info->dwb_params.dest_height;
	}
	wb_info->mcif_warmup_params.p_vmid = 1;
	wb_info->writeback_source_plane = pipe->plane_state;

	dc_stream_add_writeback(dm->dc, crtc_state->stream, wb_info);

	acrtc->wb_conn = wb_conn;
	drm_writeback_queue_job(wb_conn, new_con_state);

	/*
	 * Writeback completion is detected in the CRTC vblank IRQ
	 * (dm_crtc_high_irq()). Take a vblank reference so the vblank interrupt
	 * stays enabled while the writeback is pending; otherwise a
	 * writeback-only commit right after drm_crtc_vblank_on() (e.g.
	 * re-enabling a CRTC that was disabled) has no other vblank reference,
	 * the IRQ never fires and the out fence times out. The matching put
	 * happens once completion is signalled in dm_crtc_high_irq(), or when
	 * the writeback is torn down in dm_clear_writeback().
	 *
	 * Arm wb_pending only after the reference is held so the completion IRQ
	 * cannot run its matching vblank_put before this get.
	 */
	WARN_ON(drm_crtc_vblank_get(&acrtc->base));
	acrtc->wb_pending = true;
}

static void amdgpu_dm_update_hdcp(struct drm_atomic_commit *state)
{
	struct drm_connector_state *old_con_state, *new_con_state;
	struct drm_device *dev = state->dev;
	struct drm_connector *connector;
	struct amdgpu_device *adev = drm_to_adev(dev);
	int i;

	if (!adev->dm.hdcp_workqueue)
		return;

	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(dm_new_con_state->base.crtc);
		struct drm_crtc_state *old_crtc_state, *new_crtc_state;
		struct dm_crtc_state *dm_new_crtc_state;
		struct amdgpu_dm_connector *aconnector;

		if (!connector || connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);

		drm_dbg(dev, "[HDCP_DM] -------------- i : %x ----------\n", i);

		drm_dbg(dev, "[HDCP_DM] connector->index: %x connect_status: %x dpms: %x\n",
			connector->index, connector->status, connector->dpms);
		drm_dbg(dev, "[HDCP_DM] state protection old: %x new: %x\n",
			old_con_state->content_protection, new_con_state->content_protection);

		if (aconnector->dc_sink) {
			if (aconnector->dc_sink->sink_signal != SIGNAL_TYPE_VIRTUAL &&
				aconnector->dc_sink->sink_signal != SIGNAL_TYPE_NONE) {
				drm_dbg(dev, "[HDCP_DM] pipe_ctx dispname=%s\n",
				aconnector->dc_sink->edid_caps.display_name);
			}
		}

		new_crtc_state = NULL;
		old_crtc_state = NULL;

		if (acrtc) {
			new_crtc_state = drm_atomic_get_new_crtc_state(state, &acrtc->base);
			old_crtc_state = drm_atomic_get_old_crtc_state(state, &acrtc->base);
		}

		if (old_crtc_state)
			drm_dbg(dev, "old crtc en: %x a: %x m: %x a-chg: %x c-chg: %x\n",
			old_crtc_state->enable,
			old_crtc_state->active,
			old_crtc_state->mode_changed,
			old_crtc_state->active_changed,
			old_crtc_state->connectors_changed);

		if (new_crtc_state)
			drm_dbg(dev, "NEW crtc en: %x a: %x m: %x a-chg: %x c-chg: %x\n",
			new_crtc_state->enable,
			new_crtc_state->active,
			new_crtc_state->mode_changed,
			new_crtc_state->active_changed,
			new_crtc_state->connectors_changed);


		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (dm_new_crtc_state && dm_new_crtc_state->stream == NULL &&
		    connector->state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED) {
			hdcp_reset_display(adev->dm.hdcp_workqueue, aconnector->dc_link->link_index);
			new_con_state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
			dm_new_con_state->update_hdcp = true;
			continue;
		}

		if (is_content_protection_different(new_crtc_state, old_crtc_state, new_con_state,
											old_con_state, connector, adev->dm.hdcp_workqueue)) {
			/* when display is unplugged from mst hub, connctor will
			 * be destroyed within dm_dp_mst_connector_destroy. connector
			 * hdcp perperties, like type, undesired, desired, enabled,
			 * will be lost. So, save hdcp properties into hdcp_work within
			 * amdgpu_dm_atomic_commit_tail. if the same display is
			 * plugged back with same display index, its hdcp properties
			 * will be retrieved from hdcp_work within dm_dp_mst_get_modes
			 */

			bool enable_encryption = false;

			if (new_con_state->content_protection == DRM_MODE_CONTENT_PROTECTION_DESIRED)
				enable_encryption = true;

			if (aconnector->dc_link && aconnector->dc_sink &&
				aconnector->dc_link->type == dc_connection_mst_branch) {
				struct hdcp_workqueue *hdcp_work = adev->dm.hdcp_workqueue;
				struct hdcp_workqueue *hdcp_w =
					&hdcp_work[aconnector->dc_link->link_index];

				hdcp_w->hdcp_content_type[connector->index] =
					new_con_state->hdcp_content_type;
				hdcp_w->content_protection[connector->index] =
					new_con_state->content_protection;
			}

			if (new_crtc_state && new_crtc_state->mode_changed &&
				new_con_state->content_protection >= DRM_MODE_CONTENT_PROTECTION_DESIRED)
				enable_encryption = true;

			drm_info(dev, "[HDCP_DM] hdcp_update_display enable_encryption = %x\n", enable_encryption);

			if (aconnector->dc_link)
				hdcp_update_display(
					adev->dm.hdcp_workqueue, aconnector->dc_link->link_index, aconnector,
					new_con_state->hdcp_content_type, enable_encryption);
		}
	}
}

static int amdgpu_dm_atomic_setup_commit(struct drm_atomic_commit *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	int i, ret;

	ret = drm_dp_mst_atomic_setup_commit(state);
	if (ret)
		return ret;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		/*
		 * Color management settings. We also update color properties
		 * when a modeset is needed, to ensure it gets reprogrammed.
		 */
		if (dm_new_crtc_state->base.active && dm_new_crtc_state->stream &&
		    (dm_new_crtc_state->base.color_mgmt_changed ||
		     dm_old_crtc_state->regamma_tf != dm_new_crtc_state->regamma_tf ||
		     drm_atomic_crtc_needs_modeset(new_crtc_state))) {
			ret = amdgpu_dm_update_crtc_color_mgmt(dm_new_crtc_state);
			if (ret) {
				drm_dbg_atomic(state->dev, "Failed to update color state\n");
				return ret;
			}
		}
	}

	return 0;
}

STATIC_IFN_KUNIT void set_multisync_trigger_params(
		struct dc_stream_state *stream)
{
	struct dc_stream_state *master = NULL;

	if (stream->triggered_crtc_reset.enabled) {
		master = stream->triggered_crtc_reset.event_source;
		stream->triggered_crtc_reset.event =
			master->timing.flags.VSYNC_POSITIVE_POLARITY ?
			CRTC_EVENT_VSYNC_RISING : CRTC_EVENT_VSYNC_FALLING;
		stream->triggered_crtc_reset.delay = TRIGGER_DELAY_NEXT_PIXEL;
	}
}
EXPORT_IF_KUNIT(set_multisync_trigger_params);

STATIC_IFN_KUNIT void set_master_stream(struct dc_stream_state *stream_set[],
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
EXPORT_IF_KUNIT(set_master_stream);

static void dm_enable_per_frame_crtc_master_sync(struct dc_state *context)
{
	int i = 0;
	struct dc_stream_state *stream;

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
	}

	set_master_stream(context->streams, context->stream_count);

	for (i = 0; i < context->stream_count ; i++) {
		stream = context->streams[i];

		if (!stream)
			continue;

		set_multisync_trigger_params(stream);
	}
}

/**
 * amdgpu_dm_atomic_commit_tail() - AMDgpu DM's commit tail implementation.
 * @state: The atomic state to commit
 *
 * This will tell DC to commit the constructed DC state from atomic_check,
 * programming the hardware. Any failures here implies a hardware failure, since
 * atomic check should have filtered anything non-kosher.
 */
static void amdgpu_dm_atomic_commit_tail(struct drm_atomic_commit *state)
{
	struct drm_device *dev = state->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_display_manager *dm = &adev->dm;
	struct dm_atomic_state *dm_state;
	struct dc_state *dc_state = NULL;
	u32 i, j;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	unsigned long flags;
	bool wait_for_vblank = true;
	struct drm_connector *connector;
	struct drm_connector_state *old_con_state = NULL, *new_con_state = NULL;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	int crtc_disable_count = 0;

	trace_amdgpu_dm_atomic_commit_tail_begin(state);

	drm_atomic_helper_update_legacy_modeset_state(dev, state);
	drm_dp_mst_atomic_wait_for_dependencies(state);

	dm_state = dm_atomic_get_new_state(state);
	if (dm_state && dm_state->context) {
		dc_state = dm_state->context;
		amdgpu_dm_commit_streams(state, dc_state);
	}

	amdgpu_dm_update_hdcp(state);

	/* Handle connector state changes */
	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);
		struct dm_connector_state *dm_old_con_state = to_dm_connector_state(old_con_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(dm_new_con_state->base.crtc);
		struct dc_surface_update *dummy_updates;
		struct dc_stream_update stream_update;
		struct dc_info_packet hdr_packet;
		struct dc_stream_status *status = NULL;
		bool abm_changed, hdr_changed, scaling_changed, output_color_space_changed = false;

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

		if ((new_con_state->hdmi.broadcast_rgb != old_con_state->hdmi.broadcast_rgb) &&
			(dm_old_crtc_state->stream->output_color_space !=
				amdgpu_dm_get_output_color_space(&dm_new_crtc_state->stream->timing, new_con_state)))
			output_color_space_changed = true;

		abm_changed = dm_new_crtc_state->abm_level !=
			      dm_old_crtc_state->abm_level;

		hdr_changed =
			!drm_connector_atomic_hdr_metadata_equal(old_con_state, new_con_state);

		if (!scaling_changed && !abm_changed && !hdr_changed && !output_color_space_changed)
			continue;

		stream_update.stream = dm_new_crtc_state->stream;
		if (scaling_changed) {
			amdgpu_dm_update_stream_scaling_settings(dev, &dm_new_con_state->base.crtc->mode,
					dm_new_con_state, dm_new_crtc_state->stream);

			stream_update.src = dm_new_crtc_state->stream->src;
			stream_update.dst = dm_new_crtc_state->stream->dst;
		}

		if (output_color_space_changed) {
			dm_new_crtc_state->stream->output_color_space
				= amdgpu_dm_get_output_color_space(&dm_new_crtc_state->stream->timing, new_con_state);

			stream_update.output_color_space = &dm_new_crtc_state->stream->output_color_space;
		}

		if (abm_changed) {
			dm_new_crtc_state->stream->abm_level = dm_new_crtc_state->abm_level;

			stream_update.abm_level = &dm_new_crtc_state->abm_level;
		}

		if (hdr_changed) {
			amdgpu_dm_fill_hdr_info_packet(new_con_state, &hdr_packet);
			stream_update.hdr_static_metadata = &hdr_packet;
		}

		status = dc_stream_get_status(dm_new_crtc_state->stream);

		if (WARN_ON(!status))
			continue;

		WARN_ON(!status->plane_count);

		/*
		 * TODO: DC refuses to perform stream updates without a dc_surface_update.
		 * Here we create an empty update on each plane.
		 * To fix this, DC should permit updating only stream properties.
		 */
		dummy_updates = kzalloc(sizeof(struct dc_surface_update) * MAX_SURFACES, GFP_KERNEL);
		if (!dummy_updates) {
			drm_err(adev_to_drm(adev), "Failed to allocate memory for dummy_updates.\n");
			continue;
		}
		for (j = 0; j < status->plane_count; j++)
			dummy_updates[j].surface = status->plane_states[j];

		sort(dummy_updates, status->plane_count,
		     sizeof(*dummy_updates), dm_plane_layer_index_cmp, NULL);

		mutex_lock(&dm->dc_lock);
		dc_exit_ips_for_hw_access(dm->dc);
		dc_update_planes_and_stream(dm->dc,
					    dummy_updates,
					    status->plane_count,
					    dm_new_crtc_state->stream,
					    &stream_update);
		mutex_unlock(&dm->dc_lock);
		kfree(dummy_updates);

		drm_connector_update_privacy_screen(new_con_state);
	}

	/**
	 * Enable interrupts for CRTCs that are newly enabled or went through
	 * a modeset. It was intentionally deferred until after the front end
	 * state was modified to wait until the OTG was on and so the IRQ
	 * handlers didn't access stale or invalid state.
	 */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
#ifdef CONFIG_DEBUG_FS
		enum amdgpu_dm_pipe_crc_source cur_crc_src;
#endif
		/* Count number of newly disabled CRTCs for dropping PM refs later. */
		if (old_crtc_state->active && !new_crtc_state->active)
			crtc_disable_count++;

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		/* For freesync config update on crtc state and params for irq */
		update_stream_irq_parameters(dm, dm_new_crtc_state);

#ifdef CONFIG_DEBUG_FS
		spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
		cur_crc_src = acrtc->dm_irq_params.crc_src;
		spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
#endif

		if (new_crtc_state->active &&
		    (!old_crtc_state->active ||
		     drm_atomic_crtc_needs_modeset(new_crtc_state))) {
			dc_stream_retain(dm_new_crtc_state->stream);
			acrtc->dm_irq_params.stream = dm_new_crtc_state->stream;
			manage_dm_interrupts(adev, acrtc, dm_new_crtc_state);
		}
		/* Handle vrr on->off / off->on transitions */
		amdgpu_dm_handle_vrr_transition(dm, dm_old_crtc_state, dm_new_crtc_state);

#ifdef CONFIG_DEBUG_FS
		if (new_crtc_state->active &&
		    (!old_crtc_state->active ||
		     drm_atomic_crtc_needs_modeset(new_crtc_state))) {
			/**
			 * Frontend may have changed so reapply the CRC capture
			 * settings for the stream.
			 */
			if (amdgpu_dm_is_valid_crc_source(cur_crc_src)) {
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
				if (amdgpu_dm_crc_window_is_activated(crtc)) {
					uint8_t cnt;

					spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
					for (cnt = 0; cnt < MAX_CRC_WINDOW_NUM; cnt++) {
						if (acrtc->dm_irq_params.window_param[cnt].enable) {
							acrtc->dm_irq_params.window_param[cnt].update_win = true;

							/**
							 * It takes 2 frames for HW to stably generate CRC when
							 * resuming from suspend, so we set skip_frame_cnt 2.
							 */
							acrtc->dm_irq_params.window_param[cnt].skip_frame_cnt = 2;
						}
					}
					spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
				}
#endif
				if (amdgpu_dm_crtc_configure_crc_source(
					crtc, dm_new_crtc_state, cur_crc_src))
					drm_dbg_atomic(dev, "Failed to configure crc source");
			}
		}
#endif
	}

	amdgpu_dm_mod_power_setup_streams(state, dm);

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, j)
		if (new_crtc_state->async_flip)
			wait_for_vblank = false;

	/* update planes when needed per crtc*/
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, j) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (dm_new_crtc_state->stream)
			amdgpu_dm_commit_planes(state, dev, dm, crtc, wait_for_vblank);
	}

	/* Enable writeback */
	for_each_new_connector_in_state(state, connector, new_con_state, i) {
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(dm_new_con_state->base.crtc);

		if (connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		if (!new_con_state->writeback_job)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(state, &acrtc->base);

		if (!new_crtc_state)
			continue;

		if (acrtc->wb_enabled)
			continue;

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		dm_set_writeback(dm, dm_new_crtc_state, connector, new_con_state);
		acrtc->wb_enabled = true;
	}

	/* Update audio instances for each connector. */
	amdgpu_dm_commit_audio(dev, state);

	/* restore the backlight level */
	for (i = 0; i < dm->num_of_edps; i++) {
		if (dm->backlight_dev[i] &&
		    (dm->actual_brightness[i] != dm->brightness[i]))
			amdgpu_dm_backlight_set_level(dm, i, dm->brightness[i]);
	}

	/*
	 * send vblank event on all events not handled in flip and
	 * mark consumed event for drm_atomic_helper_commit_hw_done
	 */
	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {

		if (new_crtc_state->event)
			drm_send_event_locked(dev, &new_crtc_state->event->base);

		new_crtc_state->event = NULL;
	}
	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);

	/* Signal HW programming completion */
	drm_atomic_helper_commit_hw_done(state);

	if (wait_for_vblank)
		drm_atomic_helper_wait_for_flip_done(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);

	/* Don't free the memory if we are hitting this as part of suspend.
	 * This way we don't free any memory during suspend; see
	 * amdgpu_bo_free_kernel().  The memory will be freed in the first
	 * non-suspend modeset or when the driver is torn down.
	 */
	if (!adev->in_suspend) {
		/* return the stolen vga memory back to VRAM */
		if (!adev->mman.keep_stolen_vga_memory)
			amdgpu_ttm_unmark_vram_reserved(adev, AMDGPU_RESV_STOLEN_VGA);
		amdgpu_ttm_unmark_vram_reserved(adev, AMDGPU_RESV_STOLEN_EXTENDED);
	}

	/*
	 * Finally, drop a runtime PM reference for each newly disabled CRTC,
	 * so we can put the GPU into runtime suspend if we're not driving any
	 * displays anymore
	 */
	for (i = 0; i < crtc_disable_count; i++)
		pm_runtime_put_autosuspend(dev->dev);
	pm_runtime_mark_last_busy(dev->dev);

	trace_amdgpu_dm_atomic_commit_tail_finish(state);
}

/*
 * Grabs all modesetting locks to serialize against any blocking commits,
 * Waits for completion of all non blocking commits.
 */
static int do_aquire_global_lock(struct drm_device *dev,
				 struct drm_atomic_commit *state)
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
			drm_err(dev, "[CRTC:%d:%s] hw_done or flip_done timed out\n",
				  crtc->base.id, crtc->name);

		drm_crtc_commit_put(commit);
	}

	return ret < 0 ? ret : 0;
}

static void get_freesync_config_for_crtc(
	struct dm_crtc_state *new_crtc_state,
	struct dm_connector_state *new_con_state)
{
	struct mod_freesync_config config = {0};
	struct amdgpu_dm_connector *aconnector;
	struct drm_display_mode *mode = &new_crtc_state->base.mode;
	int vrefresh = drm_mode_vrefresh(mode);
	bool fs_vid_mode = false;

	if (new_con_state->base.connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
		return;

	aconnector = to_amdgpu_dm_connector(new_con_state->base.connector);

	new_crtc_state->vrr_supported = new_con_state->freesync_capable &&
					vrefresh >= aconnector->min_vfreq &&
					vrefresh <= aconnector->max_vfreq;

	if (new_crtc_state->vrr_supported) {
		new_crtc_state->stream->ignore_msa_timing_param = true;
		fs_vid_mode = new_crtc_state->freesync_config.state == VRR_STATE_ACTIVE_FIXED;

		config.min_refresh_in_uhz = aconnector->min_vfreq * 1000000;
		config.max_refresh_in_uhz = aconnector->max_vfreq * 1000000;
		config.vsif_supported = true;
		config.btr = true;

		if (fs_vid_mode) {
			config.state = VRR_STATE_ACTIVE_FIXED;
			config.fixed_refresh_in_uhz = new_crtc_state->freesync_config.fixed_refresh_in_uhz;
			goto out;
		} else if (new_crtc_state->base.vrr_enabled) {
			config.state = VRR_STATE_ACTIVE_VARIABLE;
		} else {
			config.state = VRR_STATE_INACTIVE;
		}
	} else {
		config.state = VRR_STATE_UNSUPPORTED;
	}
out:
	new_crtc_state->freesync_config = config;
}

static void reset_freesync_config_for_crtc(
	struct dm_crtc_state *new_crtc_state)
{
	new_crtc_state->vrr_supported = false;

	memset(&new_crtc_state->vrr_infopacket, 0,
	       sizeof(new_crtc_state->vrr_infopacket));
}

STATIC_IFN_KUNIT bool
is_timing_unchanged_for_freesync(struct drm_crtc_state *old_crtc_state,
				 struct drm_crtc_state *new_crtc_state)
{
	const struct drm_display_mode *old_mode, *new_mode;

	if (!old_crtc_state || !new_crtc_state)
		return false;

	old_mode = &old_crtc_state->mode;
	new_mode = &new_crtc_state->mode;

	if (old_mode->clock       == new_mode->clock &&
	    old_mode->hdisplay    == new_mode->hdisplay &&
	    old_mode->vdisplay    == new_mode->vdisplay &&
	    old_mode->htotal      == new_mode->htotal &&
	    old_mode->vtotal      != new_mode->vtotal &&
	    old_mode->hsync_start == new_mode->hsync_start &&
	    old_mode->vsync_start != new_mode->vsync_start &&
	    old_mode->hsync_end   == new_mode->hsync_end &&
	    old_mode->vsync_end   != new_mode->vsync_end &&
	    old_mode->hskew       == new_mode->hskew &&
	    old_mode->vscan       == new_mode->vscan &&
	    (old_mode->vsync_end - old_mode->vsync_start) ==
	    (new_mode->vsync_end - new_mode->vsync_start))
		return true;

	return false;
}
EXPORT_IF_KUNIT(is_timing_unchanged_for_freesync);

STATIC_IFN_KUNIT void set_freesync_fixed_config(struct dm_crtc_state *dm_new_crtc_state)
{
	u64 num, den, res;
	struct drm_crtc_state *new_crtc_state = &dm_new_crtc_state->base;

	dm_new_crtc_state->freesync_config.state = VRR_STATE_ACTIVE_FIXED;

	num = (unsigned long long)new_crtc_state->mode.clock * 1000 * 1000000;
	den = (unsigned long long)new_crtc_state->mode.htotal *
	      (unsigned long long)new_crtc_state->mode.vtotal;

	res = div_u64(num, den);
	dm_new_crtc_state->freesync_config.fixed_refresh_in_uhz = res;
}
EXPORT_IF_KUNIT(set_freesync_fixed_config);

static int dm_update_crtc_state(struct amdgpu_display_manager *dm,
			 struct drm_atomic_commit *state,
			 struct drm_crtc *crtc,
			 struct drm_crtc_state *old_crtc_state,
			 struct drm_crtc_state *new_crtc_state,
			 bool enable,
			 bool *lock_and_validation_needed)
{
	struct dm_atomic_state *dm_state = NULL;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	struct dc_stream_state *new_stream;
	struct amdgpu_device *adev = dm->adev;
	int ret = 0;

	/*
	 * TODO Move this code into dm_crtc_atomic_check once we get rid of dc_validation_set
	 * update changed items
	 */
	struct amdgpu_crtc *acrtc = NULL;
	struct drm_connector *connector = NULL;
	struct amdgpu_dm_connector *aconnector = NULL;
	struct drm_connector_state *drm_new_conn_state = NULL, *drm_old_conn_state = NULL;
	struct dm_connector_state *dm_new_conn_state = NULL, *dm_old_conn_state = NULL;

	new_stream = NULL;

	dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);
	dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
	acrtc = to_amdgpu_crtc(crtc);
	connector = amdgpu_dm_find_first_crtc_matching_connector(state, crtc);
	if (connector)
		aconnector = to_amdgpu_dm_connector(connector);

	/* TODO This hack should go away */
	if (connector && enable) {
		/* Make sure fake sink is created in plug-in scenario */
		drm_new_conn_state = drm_atomic_get_new_connector_state(state,
									connector);
		drm_old_conn_state = drm_atomic_get_old_connector_state(state,
									connector);

		if (WARN_ON(!drm_new_conn_state)) {
			ret = -EINVAL;
			goto fail;
		}

		dm_new_conn_state = to_dm_connector_state(drm_new_conn_state);
		dm_old_conn_state = to_dm_connector_state(drm_old_conn_state);

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			goto skip_modeset;

		new_stream = amdgpu_dm_create_validate_stream_for_sink(connector,
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
			drm_dbg_driver(adev_to_drm(adev), "%s: Failed to create new stream for crtc %d\n",
					__func__, acrtc->base.base.id);
			ret = -ENOMEM;
			goto fail;
		}

		/*
		 * TODO: Check VSDB bits to decide whether this should
		 * be enabled or not.
		 */
		new_stream->triggered_crtc_reset.enabled =
			dm->force_timing_sync;

		dm_new_crtc_state->abm_level = dm_new_conn_state->abm_level;

		ret = amdgpu_dm_fill_hdr_info_packet(drm_new_conn_state,
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
		if (amdgpu_freesync_vid_mode &&
		    dm_new_crtc_state->stream &&
		    is_timing_unchanged_for_freesync(new_crtc_state, old_crtc_state))
			goto skip_modeset;

		if (dm_new_crtc_state->stream &&
		    dc_is_stream_unchanged(new_stream, dm_old_crtc_state->stream) &&
		    dc_is_stream_scaling_unchanged(new_stream, dm_old_crtc_state->stream)) {
			new_crtc_state->mode_changed = false;
			drm_dbg_driver(adev_to_drm(adev), "Mode change not required, setting mode_changed to %d",
					 new_crtc_state->mode_changed);
		}
	}

	/* mode_changed flag may get updated above, need to check again */
	if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
		goto skip_modeset;

	drm_dbg_state(state->dev,
		"amdgpu_crtc id:%d crtc_state_flags: enable:%d, active:%d, planes_changed:%d, mode_changed:%d,active_changed:%d,connectors_changed:%d\n",
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

		/* Unset freesync video if it was active before */
		if (dm_old_crtc_state->freesync_config.state == VRR_STATE_ACTIVE_FIXED) {
			dm_new_crtc_state->freesync_config.state = VRR_STATE_INACTIVE;
			dm_new_crtc_state->freesync_config.fixed_refresh_in_uhz = 0;
		}

		/* Now check if we should set freesync video mode */
		if (amdgpu_freesync_vid_mode && dm_new_crtc_state->stream &&
		    dc_is_stream_unchanged(new_stream, dm_old_crtc_state->stream) &&
		    dc_is_stream_scaling_unchanged(new_stream, dm_old_crtc_state->stream) &&
		    is_timing_unchanged_for_freesync(new_crtc_state,
						     old_crtc_state)) {
			new_crtc_state->mode_changed = false;
			drm_dbg_driver(adev_to_drm(adev),
				"Mode change not required for front porch change, setting mode_changed to %d",
				new_crtc_state->mode_changed);

			set_freesync_fixed_config(dm_new_crtc_state);

			goto skip_modeset;
		} else if (amdgpu_freesync_vid_mode && aconnector &&
			   amdgpu_dm_is_freesync_video_mode(&new_crtc_state->mode,
						  aconnector)) {
			struct drm_display_mode *high_mode;

			high_mode = amdgpu_dm_get_highest_refresh_rate_mode(aconnector, false);
			if (!drm_mode_equal(&new_crtc_state->mode, high_mode))
				set_freesync_fixed_config(dm_new_crtc_state);
		}

		ret = dm_atomic_get_state(state, &dm_state);
		if (ret)
			goto fail;

		drm_dbg_driver(adev_to_drm(adev), "Disabling DRM crtc: %d\n",
				crtc->base.id);

		/* i.e. reset mode */
		if (dc_state_remove_stream(
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
		if (!connector)
			goto skip_modeset;

		if (modereset_required(new_crtc_state))
			goto skip_modeset;

		if (amdgpu_dm_crtc_modeset_required(new_crtc_state, new_stream,
				     dm_old_crtc_state->stream)) {

			WARN_ON(dm_new_crtc_state->stream);

			ret = dm_atomic_get_state(state, &dm_state);
			if (ret)
				goto fail;

			dm_new_crtc_state->stream = new_stream;

			dc_stream_retain(new_stream);

			drm_dbg_atomic(adev_to_drm(adev), "Enabling DRM crtc: %d\n",
					 crtc->base.id);

			if (dc_state_add_stream(
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
	new_stream = NULL;

	/*
	 * We want to do dc stream updates that do not require a
	 * full modeset below.
	 */
	if (!(enable && connector && new_crtc_state->active))
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
	if (is_scaling_state_different(dm_old_conn_state, dm_new_conn_state) ||
				drm_atomic_crtc_needs_modeset(new_crtc_state))
		amdgpu_dm_update_stream_scaling_settings(adev_to_drm(adev),
			&new_crtc_state->mode, dm_new_conn_state, dm_new_crtc_state->stream);

	/* ABM settings */
	dm_new_crtc_state->abm_level = dm_new_conn_state->abm_level;

	/*
	 * Color management settings. We also update color properties
	 * when a modeset is needed, to ensure it gets reprogrammed.
	 */
	if (dm_new_crtc_state->base.color_mgmt_changed ||
	    dm_old_crtc_state->regamma_tf != dm_new_crtc_state->regamma_tf ||
	    drm_atomic_crtc_needs_modeset(new_crtc_state)) {
		ret = amdgpu_dm_check_crtc_color_mgmt(dm_new_crtc_state, true);
		if (ret)
			goto fail;
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

static bool should_reset_plane(struct drm_atomic_commit *state,
			       struct drm_plane *plane,
			       struct drm_plane_state *old_plane_state,
			       struct drm_plane_state *new_plane_state)
{
	struct drm_plane *other;
	struct drm_plane_state *old_other_state, *new_other_state;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct dm_crtc_state *old_dm_crtc_state, *new_dm_crtc_state;
	struct amdgpu_device *adev = drm_to_adev(plane->dev);
	struct drm_connector_state *new_con_state;
	struct drm_connector *connector;
	int i;

	/*
	 * TODO: Remove this hack for all asics once it proves that the
	 * fast updates works fine on DCN3.2+.
	 */
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) < IP_VERSION(3, 2, 0) &&
	    state->allow_modeset)
		return true;

	/* Check for writeback commit */
	for_each_new_connector_in_state(state, connector, new_con_state, i) {
		if (connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		if (new_con_state->writeback_job)
			return true;
	}

	if (amdgpu_in_reset(adev) && state->allow_modeset)
		return true;

	/* Exit early if we know that we're adding or removing the plane. */
	if (old_plane_state->crtc != new_plane_state->crtc)
		return true;

	/* old crtc == new_crtc == NULL, plane not in context. */
	if (!new_plane_state->crtc)
		return false;

	new_crtc_state =
		drm_atomic_get_new_crtc_state(state, new_plane_state->crtc);
	old_crtc_state =
		drm_atomic_get_old_crtc_state(state, old_plane_state->crtc);

	if (!new_crtc_state)
		return true;

	/*
	 * A change in cursor mode means a new dc pipe needs to be acquired or
	 * released from the state
	 */
	old_dm_crtc_state = to_dm_crtc_state(old_crtc_state);
	new_dm_crtc_state = to_dm_crtc_state(new_crtc_state);
	if (plane->type == DRM_PLANE_TYPE_CURSOR &&
	    old_dm_crtc_state != NULL &&
	    old_dm_crtc_state->cursor_mode != new_dm_crtc_state->cursor_mode) {
		return true;
	}

	/* CRTC Degamma changes currently require us to recreate planes. */
	if (new_crtc_state->color_mgmt_changed)
		return true;

	/*
	 * On zpos change, planes need to be reordered by removing and re-adding
	 * them one by one to the dc state, in order of descending zpos.
	 *
	 * TODO: We can likely skip bandwidth validation if the only thing that
	 * changed about the plane was it'z z-ordering.
	 */
	if (old_plane_state->normalized_zpos != new_plane_state->normalized_zpos)
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
		struct amdgpu_framebuffer *old_afb, *new_afb;
		struct dm_plane_state *dm_new_other_state, *dm_old_other_state;

		dm_new_other_state = to_dm_plane_state(new_other_state);
		dm_old_other_state = to_dm_plane_state(old_other_state);

		if (other->type == DRM_PLANE_TYPE_CURSOR)
			continue;

		if (old_other_state->crtc != new_plane_state->crtc &&
		    new_other_state->crtc != new_plane_state->crtc)
			continue;

		if (old_other_state->crtc != new_other_state->crtc)
			return true;

		/* Src/dst size and scaling updates. */
		if (old_other_state->src_w != new_other_state->src_w ||
		    old_other_state->src_h != new_other_state->src_h ||
		    old_other_state->crtc_w != new_other_state->crtc_w ||
		    old_other_state->crtc_h != new_other_state->crtc_h)
			return true;

		/* Rotation / mirroring updates. */
		if (old_other_state->rotation != new_other_state->rotation)
			return true;

		/* Blending updates. */
		if (old_other_state->pixel_blend_mode !=
		    new_other_state->pixel_blend_mode)
			return true;

		/* Alpha updates. */
		if (old_other_state->alpha != new_other_state->alpha)
			return true;

		/* Colorspace changes. */
		if (old_other_state->color_range != new_other_state->color_range ||
		    old_other_state->color_encoding != new_other_state->color_encoding)
			return true;

		/* HDR/Transfer Function changes. */
		if (dm_old_other_state->degamma_tf != dm_new_other_state->degamma_tf ||
		    dm_old_other_state->degamma_lut != dm_new_other_state->degamma_lut ||
		    dm_old_other_state->hdr_mult != dm_new_other_state->hdr_mult ||
		    dm_old_other_state->ctm != dm_new_other_state->ctm ||
		    dm_old_other_state->shaper_lut != dm_new_other_state->shaper_lut ||
		    dm_old_other_state->shaper_tf != dm_new_other_state->shaper_tf ||
		    dm_old_other_state->lut3d != dm_new_other_state->lut3d ||
		    dm_old_other_state->blend_lut != dm_new_other_state->blend_lut ||
		    dm_old_other_state->blend_tf != dm_new_other_state->blend_tf)
			return true;

		/* Framebuffer checks fall at the end. */
		if (!old_other_state->fb || !new_other_state->fb)
			continue;

		/* Pixel format changes can require bandwidth updates. */
		if (old_other_state->fb->format != new_other_state->fb->format)
			return true;

		old_afb = (struct amdgpu_framebuffer *)old_other_state->fb;
		new_afb = (struct amdgpu_framebuffer *)new_other_state->fb;

		/* Tiling and DCC changes also require bandwidth updates. */
		if (old_afb->tiling_flags != new_afb->tiling_flags ||
		    old_afb->base.modifier != new_afb->base.modifier)
			return true;
	}

	return false;
}

static int dm_check_cursor_fb(struct amdgpu_crtc *new_acrtc,
			      struct drm_plane_state *new_plane_state,
			      struct drm_framebuffer *fb)
{
	struct amdgpu_device *adev = drm_to_adev(new_acrtc->base.dev);
	struct amdgpu_framebuffer *afb = to_amdgpu_framebuffer(fb);
	unsigned int pitch;
	bool linear;

	if (fb->width > new_acrtc->max_cursor_width ||
	    fb->height > new_acrtc->max_cursor_height) {
		drm_dbg_atomic(adev_to_drm(adev), "Bad cursor FB size %dx%d\n",
				 new_plane_state->fb->width,
				 new_plane_state->fb->height);
		return -EINVAL;
	}
	if (new_plane_state->src_w != fb->width << 16 ||
	    new_plane_state->src_h != fb->height << 16) {
		drm_dbg_atomic(adev_to_drm(adev), "Cropping not supported for cursor plane\n");
		return -EINVAL;
	}

	/* Pitch in pixels */
	pitch = fb->pitches[0] / fb->format->cpp[0];

	if (fb->width != pitch) {
		drm_dbg_atomic(adev_to_drm(adev), "Cursor FB width %d doesn't match pitch %d",
				 fb->width, pitch);
		return -EINVAL;
	}

	switch (pitch) {
	case 64:
	case 128:
	case 256:
		/* FB pitch is supported by cursor plane */
		break;
	default:
		drm_dbg_atomic(adev_to_drm(adev), "Bad cursor FB pitch %d px\n", pitch);
		return -EINVAL;
	}

	/* Core DRM takes care of checking FB modifiers, so we only need to
	 * check tiling flags when the FB doesn't have a modifier.
	 */
	if (!(fb->flags & DRM_MODE_FB_MODIFIERS)) {
		if (adev->family == AMDGPU_FAMILY_GC_12_0_0) {
			linear = AMDGPU_TILING_GET(afb->tiling_flags, GFX12_SWIZZLE_MODE) == 0;
		} else if (adev->family >= AMDGPU_FAMILY_AI) {
			linear = AMDGPU_TILING_GET(afb->tiling_flags, SWIZZLE_MODE) == 0;
		} else {
			linear = AMDGPU_TILING_GET(afb->tiling_flags, ARRAY_MODE) != DC_ARRAY_2D_TILED_THIN1 &&
				 AMDGPU_TILING_GET(afb->tiling_flags, ARRAY_MODE) != DC_ARRAY_1D_TILED_THIN1 &&
				 AMDGPU_TILING_GET(afb->tiling_flags, MICRO_TILE_MODE) == 0;
		}
		if (!linear) {
			drm_dbg_atomic(adev_to_drm(adev), "Cursor FB not linear");
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * Helper function for checking the cursor in native mode
 */
static int dm_check_native_cursor_state(struct drm_crtc *new_plane_crtc,
					struct drm_plane *plane,
					struct drm_plane_state *new_plane_state,
					bool enable)
{

	struct amdgpu_crtc *new_acrtc;
	int ret;

	if (!enable || !new_plane_crtc ||
	    drm_atomic_plane_disabling(plane->state, new_plane_state))
		return 0;

	new_acrtc = to_amdgpu_crtc(new_plane_crtc);

	if (new_plane_state->src_x != 0 || new_plane_state->src_y != 0) {
		drm_dbg_atomic(new_plane_crtc->dev, "Cropping not supported for cursor plane\n");
		return -EINVAL;
	}

	if (new_plane_state->fb) {
		ret = dm_check_cursor_fb(new_acrtc, new_plane_state,
						new_plane_state->fb);
		if (ret)
			return ret;
	}

	return 0;
}

static bool dm_should_update_native_cursor(struct drm_atomic_commit *state,
					   struct drm_crtc *old_plane_crtc,
					   struct drm_crtc *new_plane_crtc,
					   bool enable)
{
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;

	if (!enable) {
		if (old_plane_crtc == NULL)
			return true;

		old_crtc_state = drm_atomic_get_old_crtc_state(
			state, old_plane_crtc);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		return dm_old_crtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE;
	} else {
		if (new_plane_crtc == NULL)
			return true;

		new_crtc_state = drm_atomic_get_new_crtc_state(
			state, new_plane_crtc);
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		return dm_new_crtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE;
	}
}

static int dm_update_plane_state(struct dc *dc,
				 struct drm_atomic_commit *state,
				 struct drm_plane *plane,
				 struct drm_plane_state *old_plane_state,
				 struct drm_plane_state *new_plane_state,
				 bool enable,
				 bool *lock_and_validation_needed,
				 bool *is_top_most_overlay)
{

	struct dm_atomic_state *dm_state = NULL;
	struct drm_crtc *new_plane_crtc, *old_plane_crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct dm_crtc_state *dm_new_crtc_state, *dm_old_crtc_state;
	struct dm_plane_state *dm_new_plane_state, *dm_old_plane_state;
	bool needs_reset, update_native_cursor;
	int ret = 0;


	new_plane_crtc = new_plane_state->crtc;
	old_plane_crtc = old_plane_state->crtc;
	dm_new_plane_state = to_dm_plane_state(new_plane_state);
	dm_old_plane_state = to_dm_plane_state(old_plane_state);

	update_native_cursor = dm_should_update_native_cursor(state,
							      old_plane_crtc,
							      new_plane_crtc,
							      enable);

	if (plane->type == DRM_PLANE_TYPE_CURSOR && update_native_cursor) {
		ret = dm_check_native_cursor_state(new_plane_crtc, plane,
						    new_plane_state, enable);
		if (ret)
			return ret;

		return 0;
	}

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

		drm_dbg_atomic(old_plane_crtc->dev, "Disabling DRM plane: %d on DRM crtc %d\n",
				plane->base.id, old_plane_crtc->base.id);

		ret = dm_atomic_get_state(state, &dm_state);
		if (ret)
			return ret;

		if (!dc_state_remove_plane(
				dc,
				dm_old_crtc_state->stream,
				dm_old_plane_state->dc_state,
				dm_state->context)) {

			return -EINVAL;
		}

		if (dm_old_plane_state->dc_state)
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

		ret = amdgpu_dm_plane_helper_check_state(new_plane_state, new_crtc_state);
		if (ret)
			goto out;

		WARN_ON(dm_new_plane_state->dc_state);

		dc_new_plane_state = dc_create_plane_state(dc);
		if (!dc_new_plane_state) {
			ret = -ENOMEM;
			goto out;
		}

		drm_dbg_atomic(new_plane_crtc->dev, "Enabling DRM plane: %d on DRM crtc %d\n",
				 plane->base.id, new_plane_crtc->base.id);

		ret = fill_dc_plane_attributes(
			drm_to_adev(new_plane_crtc->dev),
			dc_new_plane_state,
			new_plane_state,
			new_crtc_state);
		if (ret) {
			dc_plane_state_release(dc_new_plane_state);
			goto out;
		}

		ret = dm_atomic_get_state(state, &dm_state);
		if (ret) {
			dc_plane_state_release(dc_new_plane_state);
			goto out;
		}

		/*
		 * Any atomic check errors that occur after this will
		 * not need a release. The plane state will be attached
		 * to the stream, and therefore part of the atomic
		 * state. It'll be released when the atomic state is
		 * cleaned.
		 */
		if (!dc_state_add_plane(
				dc,
				dm_new_crtc_state->stream,
				dc_new_plane_state,
				dm_state->context)) {

			dc_plane_state_release(dc_new_plane_state);
			ret = -EINVAL;
			goto out;
		}

		dm_new_plane_state->dc_state = dc_new_plane_state;

		dm_new_crtc_state->mpo_requested |= (plane->type == DRM_PLANE_TYPE_OVERLAY);

		/* Tell DC to do a full surface update every time there
		 * is a plane change. Inefficient, but works for now.
		 */
		dm_new_plane_state->dc_state->update_bits.full_update = 1;

		*lock_and_validation_needed = true;
	}

out:
	/* If enabling cursor overlay failed, attempt fallback to native mode */
	if (enable && ret == -EINVAL && plane->type == DRM_PLANE_TYPE_CURSOR) {
		ret = dm_check_native_cursor_state(new_plane_crtc, plane,
						    new_plane_state, enable);
		if (ret)
			return ret;

		dm_new_crtc_state->cursor_mode = DM_CURSOR_NATIVE_MODE;
	}

	return ret;
}

STATIC_IFN_KUNIT void dm_get_oriented_plane_size(struct drm_plane_state *plane_state,
					 int *src_w, int *src_h)
{
	switch (plane_state->rotation & DRM_MODE_ROTATE_MASK) {
	case DRM_MODE_ROTATE_90:
	case DRM_MODE_ROTATE_270:
		*src_w = plane_state->src_h >> 16;
		*src_h = plane_state->src_w >> 16;
		break;
	case DRM_MODE_ROTATE_0:
	case DRM_MODE_ROTATE_180:
	default:
		*src_w = plane_state->src_w >> 16;
		*src_h = plane_state->src_h >> 16;
		break;
	}
}
EXPORT_IF_KUNIT(dm_get_oriented_plane_size);

STATIC_IFN_KUNIT void
dm_get_plane_scale(struct drm_plane_state *plane_state,
		   int *out_plane_scale_w, int *out_plane_scale_h)
{
	int plane_src_w, plane_src_h;

	dm_get_oriented_plane_size(plane_state, &plane_src_w, &plane_src_h);
	*out_plane_scale_w = plane_src_w ? plane_state->crtc_w * 1000 / plane_src_w : 0;
	*out_plane_scale_h = plane_src_h ? plane_state->crtc_h * 1000 / plane_src_h : 0;
}
EXPORT_IF_KUNIT(dm_get_plane_scale);

/*
 * The normalized_zpos value cannot be used by this iterator directly. It's only
 * calculated for enabled planes, potentially causing normalized_zpos collisions
 * between enabled/disabled planes in the atomic state. We need a unique value
 * so that the iterator will not generate the same object twice, or loop
 * indefinitely.
 */
static inline struct __drm_planes_state *__get_next_zpos(
	struct drm_atomic_commit *state,
	struct __drm_planes_state *prev)
{
	unsigned int highest_zpos = 0, prev_zpos = 256;
	uint32_t highest_id = 0, prev_id = UINT_MAX;
	struct drm_plane_state *new_plane_state;
	struct drm_plane *plane;
	int i, highest_i = -1;

	if (prev != NULL) {
		prev_zpos = prev->new_state->zpos;
		prev_id = prev->ptr->base.id;
	}

	for_each_new_plane_in_state(state, plane, new_plane_state, i) {
		/* Skip planes with higher zpos than the previously returned */
		if (new_plane_state->zpos > prev_zpos ||
		    (new_plane_state->zpos == prev_zpos &&
		     plane->base.id >= prev_id))
			continue;

		/* Save the index of the plane with highest zpos */
		if (new_plane_state->zpos > highest_zpos ||
		    (new_plane_state->zpos == highest_zpos &&
		     plane->base.id > highest_id)) {
			highest_zpos = new_plane_state->zpos;
			highest_id = plane->base.id;
			highest_i = i;
		}
	}

	if (highest_i < 0)
		return NULL;

	return &state->planes[highest_i];
}

/*
 * Use the uniqueness of the plane's (zpos, drm obj ID) combination to iterate
 * by descending zpos, as read from the new plane state. This is the same
 * ordering as defined by drm_atomic_normalize_zpos().
 */
#define for_each_oldnew_plane_in_descending_zpos(__state, plane, old_plane_state, new_plane_state) \
	for (struct __drm_planes_state *__i = __get_next_zpos((__state), NULL); \
	     __i != NULL; __i = __get_next_zpos((__state), __i))		\
		for_each_if(((plane) = __i->ptr,				\
			     (void)(plane) /* Only to avoid unused-but-set-variable warning */, \
			     (old_plane_state) = __i->old_state,		\
			     (new_plane_state) = __i->new_state, 1))

static int add_affected_mst_dsc_crtcs(struct drm_atomic_commit *state, struct drm_crtc *crtc)
{
	struct drm_connector *connector;
	struct drm_connector_state *conn_state, *old_conn_state;
	struct amdgpu_dm_connector *aconnector = NULL;
	int i;

	for_each_oldnew_connector_in_state(state, connector, old_conn_state, conn_state, i) {
		if (!conn_state->crtc)
			conn_state = old_conn_state;

		if (conn_state->crtc != crtc)
			continue;

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (!aconnector->mst_output_port || !aconnector->mst_root)
			aconnector = NULL;
		else
			break;
	}

	if (!aconnector)
		return 0;

	return drm_dp_mst_add_affected_dsc_crtcs(state, &aconnector->mst_root->mst_mgr);
}

/**
 * DOC: Cursor Modes - Native vs Overlay
 *
 * In native mode, the cursor uses a integrated cursor pipe within each DCN hw
 * plane. It does not require a dedicated hw plane to enable, but it is
 * subjected to the same z-order and scaling as the hw plane. It also has format
 * restrictions, a RGB cursor in native mode cannot be enabled within a non-RGB
 * hw plane.
 *
 * In overlay mode, the cursor uses a separate DCN hw plane, and thus has its
 * own scaling and z-pos. It also has no blending restrictions. It lends to a
 * cursor behavior more akin to a DRM client's expectations. However, it does
 * occupy an extra DCN plane, and therefore will only be used if a DCN plane is
 * available.
 */

/**
 * dm_plane_color_pipeline_active() - Check if a plane's color pipeline active.
 * @state: DRM atomic state
 * @plane: DRM plane to check
 * @use_old: if true, inspect the old colorop states; otherwise the new ones
 *
 * A color pipeline may be selected (color_pipeline != NULL) but still is
 * inactive if every colorop in the chain is bypassed.  Only return
 * true when at least one colorop has bypass == false, meaning the cursor
 * would be subjected to the transformation in native mode.
 *
 * Return: true if the pipeline modifies pixels, false otherwise.
 */
static bool dm_plane_color_pipeline_active(struct drm_atomic_commit *state,
					   struct drm_plane *plane,
					   bool use_old)
{
	struct drm_colorop *colorop;
	struct drm_colorop_state *old_colorop_state, *new_colorop_state;
	int i;

	for_each_oldnew_colorop_in_state(state, colorop, old_colorop_state, new_colorop_state, i) {
		struct drm_colorop_state *cstate = use_old ? old_colorop_state : new_colorop_state;

		if (cstate->colorop->plane != plane)
			continue;
		if (!cstate->bypass)
			return true;
	}
	return false;
}

/**
 * dm_crtc_get_cursor_mode() - Determine the required cursor mode on crtc
 * @adev: amdgpu device
 * @state: DRM atomic state
 * @dm_crtc_state: amdgpu state for the CRTC containing the cursor
 * @cursor_mode: Returns the required cursor mode on dm_crtc_state
 *
 * Get whether the cursor should be enabled in native mode, or overlay mode, on
 * the dm_crtc_state.
 *
 * The cursor should be enabled in overlay mode if there exists an underlying
 * plane - on which the cursor may be blended - that is either YUV formatted,
 * scaled differently from the cursor, or has a color pipeline active.
 *
 * Since zpos info is required, drm_atomic_normalize_zpos must be called before
 * calling this function.
 *
 * Return: 0 on success, or an error code if getting the cursor plane state
 * failed.
 */
static int dm_crtc_get_cursor_mode(struct amdgpu_device *adev,
				   struct drm_atomic_commit *state,
				   struct dm_crtc_state *dm_crtc_state,
				   enum amdgpu_dm_cursor_mode *cursor_mode)
{
	struct drm_plane_state *old_plane_state, *plane_state, *cursor_state;
	struct drm_crtc_state *crtc_state = &dm_crtc_state->base;
	struct drm_plane *plane;
	bool consider_mode_change = false;
	bool entire_crtc_covered = false;
	bool cursor_changed = false;
	int underlying_scale_w, underlying_scale_h;
	int cursor_scale_w, cursor_scale_h;
	int i;

	/* Overlay cursor not supported on HW before DCN
	 * DCN401/420 does not have the cursor-on-scaled-plane or cursor-on-yuv-plane restrictions
	 * as previous DCN generations, so enable native mode on DCN401/420
	 *
	 * Always set native cursor mode when the CRTC is disabled,
	 * to make sure it doesn't cause atomic commits to fail when
	 * they are trying to disable the CRTC.
	 */
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(4, 0, 1) ||
	    amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(4, 2, 0) ||
	    amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(4, 2, 1) ||
	    !dm_crtc_state->base.enable) {
		*cursor_mode = DM_CURSOR_NATIVE_MODE;
		return 0;
	}

	/* Init cursor_mode to be the same as current */
	*cursor_mode = dm_crtc_state->cursor_mode;

	/*
	 * Cursor mode can change if a plane's format changes, scale changes, is
	 * enabled/disabled, z-order changes, or color management properties change.
	 */
	for_each_oldnew_plane_in_state(state, plane, old_plane_state, plane_state, i) {
		int new_scale_w, new_scale_h, old_scale_w, old_scale_h;

		/* Only care about planes on this CRTC */
		if ((drm_plane_mask(plane) & crtc_state->plane_mask) == 0)
			continue;

		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			cursor_changed = true;

		if (drm_atomic_plane_enabling(old_plane_state, plane_state) ||
		    drm_atomic_plane_disabling(old_plane_state, plane_state) ||
		    old_plane_state->fb->format != plane_state->fb->format) {
			consider_mode_change = true;
			break;
		}

		dm_get_plane_scale(plane_state, &new_scale_w, &new_scale_h);
		dm_get_plane_scale(old_plane_state, &old_scale_w, &old_scale_h);
		if (new_scale_w != old_scale_w || new_scale_h != old_scale_h) {
			consider_mode_change = true;
			break;
		}

		/*
		 * A non-cursor plane moving or resizing (without a scale change)
		 * changes how much of the CRTC it covers. This can create or
		 * remove a hole under the cursor and thus flip the required
		 * cursor mode (native vs overlay), so its destination rect must
		 * be re-evaluated too.
		 *
		 * The cursor plane itself is deliberately excluded: the cursor
		 * mode depends on the underlying planes' coverage, not on the
		 * cursor's position (see the entire_crtc_covered logic below).
		 * Triggering on cursor movement would force every legacy cursor
		 * update off its fast path, and in a cursor-only commit - where
		 * the underlying planes are not part of the state - the coverage
		 * loop would see no covering plane and misevaluate the mode as
		 * overlay, regressing flip-vs-cursor-legacy.
		 */
		if (plane->type != DRM_PLANE_TYPE_CURSOR &&
		    (old_plane_state->crtc_x != plane_state->crtc_x ||
		     old_plane_state->crtc_y != plane_state->crtc_y ||
		     old_plane_state->crtc_w != plane_state->crtc_w ||
		     old_plane_state->crtc_h != plane_state->crtc_h)) {
			consider_mode_change = true;
			break;
		}

		if (dm_plane_color_pipeline_active(state, plane, true) !=
		    dm_plane_color_pipeline_active(state, plane, false)) {
			consider_mode_change = true;
			break;
		}
	}

	if (!consider_mode_change && !crtc_state->zpos_changed)
		return 0;

	/*
	 * If no cursor change on this CRTC, and not enabled on this CRTC, then
	 * no need to set cursor mode. This avoids needlessly locking the cursor
	 * state.
	 */
	if (!cursor_changed &&
	    !(drm_plane_mask(crtc_state->crtc->cursor) & crtc_state->plane_mask)) {
		return 0;
	}

	cursor_state = drm_atomic_get_plane_state(state,
						  crtc_state->crtc->cursor);
	if (IS_ERR(cursor_state))
		return PTR_ERR(cursor_state);

	/* Cursor is disabled */
	if (!cursor_state->fb)
		return 0;

	/* For all planes in descending z-order (all of which are below cursor
	 * as per zpos definitions), check their scaling and format
	 */
	for_each_oldnew_plane_in_descending_zpos(state, plane, old_plane_state, plane_state) {

		/* Only care about non-cursor planes on this CRTC */
		if ((drm_plane_mask(plane) & crtc_state->plane_mask) == 0 ||
		    plane->type == DRM_PLANE_TYPE_CURSOR)
			continue;

		/* Underlying plane is YUV format - use overlay cursor */
		if (amdgpu_dm_plane_is_video_format(plane_state->fb->format->format)) {
			*cursor_mode = DM_CURSOR_OVERLAY_MODE;
			return 0;
		}

		/* Underlying plane has an active color pipeline - cursor would be transformed */
		if (dm_plane_color_pipeline_active(state, plane, false)) {
			*cursor_mode = DM_CURSOR_OVERLAY_MODE;
			return 0;
		}

		dm_get_plane_scale(plane_state,
				   &underlying_scale_w, &underlying_scale_h);
		dm_get_plane_scale(cursor_state,
				   &cursor_scale_w, &cursor_scale_h);

		/* Underlying plane has different scale - use overlay cursor */
		if (cursor_scale_w != underlying_scale_w &&
		    cursor_scale_h != underlying_scale_h) {
			*cursor_mode = DM_CURSOR_OVERLAY_MODE;
			return 0;
		}

		/* If this plane covers the whole CRTC, no need to check planes underneath */
		if (plane_state->crtc_x <= 0 && plane_state->crtc_y <= 0 &&
		    plane_state->crtc_x + plane_state->crtc_w >= crtc_state->mode.hdisplay &&
		    plane_state->crtc_y + plane_state->crtc_h >= crtc_state->mode.vdisplay) {
			entire_crtc_covered = true;
			break;
		}
	}

	/* If planes do not cover the entire CRTC, use overlay mode to enable
	 * cursor over holes
	 */
	if (entire_crtc_covered)
		*cursor_mode = DM_CURSOR_NATIVE_MODE;
	else
		*cursor_mode = DM_CURSOR_OVERLAY_MODE;

	return 0;
}

static bool amdgpu_dm_crtc_mem_type_changed(struct drm_device *dev,
					    struct drm_atomic_commit *state,
					    struct drm_crtc_state *crtc_state)
{
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state, *old_plane_state;

	drm_for_each_plane_mask(plane, dev, crtc_state->plane_mask) {
		new_plane_state = drm_atomic_get_new_plane_state(state, plane);
		old_plane_state = drm_atomic_get_old_plane_state(state, plane);

		if (!old_plane_state || !new_plane_state)
			continue;

		if (old_plane_state->fb && new_plane_state->fb &&
		    get_mem_type(old_plane_state->fb) != get_mem_type(new_plane_state->fb))
			return true;
	}

	return false;
}

/**
 * amdgpu_dm_atomic_check() - Atomic check implementation for AMDgpu DM.
 *
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
 * flip using DRMs synchronization events.
 *
 * Note that DM adds the affected connectors for all CRTCs in state, when that
 * might not seem necessary. This is because DC stream creation requires the
 * DC sink, which is tied to the DRM connector state. Cleaning this up should
 * be possible but non-trivial - a possible TODO item.
 *
 * Return: -Error code if validation failed.
 */
static int amdgpu_dm_atomic_check(struct drm_device *dev,
				  struct drm_atomic_commit *state)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dm_atomic_state *dm_state = NULL;
	struct dc *dc = adev->dm.dc;
	struct drm_connector *connector;
	struct drm_connector_state *old_con_state, *new_con_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state, *new_cursor_state;
	enum dc_status status;
	int ret, i;
	bool lock_and_validation_needed = false;
	bool is_top_most_overlay = true;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
	struct drm_dp_mst_topology_mgr *mgr;
	struct drm_dp_mst_topology_state *mst_state;
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {0};

	trace_amdgpu_dm_atomic_check_begin(state);

	ret = drm_atomic_helper_check_modeset(dev, state);
	if (ret) {
		drm_dbg_atomic(dev, "drm_atomic_helper_check_modeset() failed: %pe\n", ERR_PTR(ret));
		goto fail;
	}

	/* Check connector changes */
	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		struct dm_connector_state *dm_old_con_state = to_dm_connector_state(old_con_state);
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);

		/* Skip connectors that are disabled or part of modeset already. */
		if (!new_con_state->crtc)
			continue;

		new_crtc_state = drm_atomic_get_crtc_state(state, new_con_state->crtc);
		if (IS_ERR(new_crtc_state)) {
			drm_dbg_atomic(dev, "drm_atomic_get_crtc_state() failed: %pe\n", new_crtc_state);
			ret = PTR_ERR(new_crtc_state);
			goto fail;
		}

		if (dm_old_con_state->abm_level != dm_new_con_state->abm_level ||
		    dm_old_con_state->scaling != dm_new_con_state->scaling)
			new_crtc_state->connectors_changed = true;
	}

	if (dc_resource_is_dsc_encoding_supported(dc)) {
		for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
			dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
			dm_new_crtc_state->mode_changed_independent_from_dsc = new_crtc_state->mode_changed;
		}

		for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
			if (drm_atomic_crtc_needs_modeset(new_crtc_state)) {
				ret = add_affected_mst_dsc_crtcs(state, crtc);
				if (ret) {
					drm_dbg_atomic(dev, "add_affected_mst_dsc_crtcs() failed: %pe\n", ERR_PTR(ret));
					goto fail;
				}
			}
		}
	}
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state) &&
		    !new_crtc_state->color_mgmt_changed &&
		    old_crtc_state->vrr_enabled == new_crtc_state->vrr_enabled &&
			dm_old_crtc_state->dsc_force_changed == false)
			continue;

		ret = amdgpu_dm_verify_lut_sizes(new_crtc_state);
		if (ret) {
			drm_dbg_atomic(dev, "amdgpu_dm_verify_lut_sizes() failed: %pe\n", ERR_PTR(ret));
			goto fail;
		}

		if (!new_crtc_state->enable)
			continue;

		ret = drm_atomic_add_affected_connectors(state, crtc);
		if (ret) {
			drm_dbg_atomic(dev, "drm_atomic_add_affected_connectors() failed: %pe\n", ERR_PTR(ret));
			goto fail;
		}

		ret = drm_atomic_add_affected_planes(state, crtc);
		if (ret) {
			drm_dbg_atomic(dev, "drm_atomic_add_affected_planes() failed: %pe\n", ERR_PTR(ret));
			goto fail;
		}

		if (dm_old_crtc_state->dsc_force_changed)
			new_crtc_state->mode_changed = true;
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
				drm_dbg_atomic(dev, "new_plane_state is BAD: %pe\n", new_plane_state);
				goto fail;
			}
		}
	}

	/*
	 * DC consults the zpos (layer_index in DC terminology) to determine the
	 * hw plane on which to enable the hw cursor (see
	 * `dcn10_can_pipe_disable_cursor`). By now, all modified planes are in
	 * atomic state, so call drm helper to normalize zpos.
	 */
	ret = drm_atomic_normalize_zpos(dev, state);
	if (ret) {
		drm_dbg(dev, "drm_atomic_normalize_zpos() failed: %pe\n", ERR_PTR(ret));
		goto fail;
	}

	/*
	 * Determine whether cursors on each CRTC should be enabled in native or
	 * overlay mode.
	 */
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		ret = dm_crtc_get_cursor_mode(adev, state, dm_new_crtc_state,
					      &dm_new_crtc_state->cursor_mode);
		if (ret) {
			drm_dbg(dev, "Failed to determine cursor mode: %pe\n", ERR_PTR(ret));
			goto fail;
		}

		/*
		 * If overlay cursor is needed, DC cannot go through the
		 * native cursor update path. All enabled planes on the CRTC
		 * need to be added for DC to not disable a plane by mistake
		 */
		if (dm_new_crtc_state->cursor_mode == DM_CURSOR_OVERLAY_MODE) {
			if (amdgpu_ip_version(adev, DCE_HWIP, 0) == 0) {
				drm_dbg(dev, "Overlay cursor not supported on DCE\n");
				ret = -EINVAL;
				goto fail;
			}

			ret = drm_atomic_add_affected_planes(state, crtc);
			if (ret)
				goto fail;
		}
	}

	/* Remove exiting planes if they are modified */
	for_each_oldnew_plane_in_descending_zpos(state, plane, old_plane_state, new_plane_state) {

		ret = dm_update_plane_state(dc, state, plane,
					    old_plane_state,
					    new_plane_state,
					    false,
					    &lock_and_validation_needed,
					    &is_top_most_overlay);
		if (ret) {
			drm_dbg_atomic(dev, "dm_update_plane_state() failed: %pe\n", ERR_PTR(ret));
			goto fail;
		}
	}

	/* Disable all crtcs which require disable */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		ret = dm_update_crtc_state(&adev->dm, state, crtc,
					   old_crtc_state,
					   new_crtc_state,
					   false,
					   &lock_and_validation_needed);
		if (ret) {
			drm_dbg_atomic(dev, "DISABLE: dm_update_crtc_state() failed: %pe\n", ERR_PTR(ret));
			goto fail;
		}
	}

	/* Enable all crtcs which require enable */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		ret = dm_update_crtc_state(&adev->dm, state, crtc,
					   old_crtc_state,
					   new_crtc_state,
					   true,
					   &lock_and_validation_needed);
		if (ret) {
			drm_dbg_atomic(dev, "ENABLE: dm_update_crtc_state() failed: %pe\n", ERR_PTR(ret));
			goto fail;
		}
	}

	/* Add new/modified planes */
	for_each_oldnew_plane_in_descending_zpos(state, plane, old_plane_state, new_plane_state) {
		ret = dm_update_plane_state(dc, state, plane,
					    old_plane_state,
					    new_plane_state,
					    true,
					    &lock_and_validation_needed,
					    &is_top_most_overlay);
		if (ret) {
			drm_dbg_atomic(dev, "dm_update_plane_state() failed: %pe\n", ERR_PTR(ret));
			goto fail;
		}
	}

#if defined(CONFIG_DRM_AMD_DC_FP)
	if (dc_resource_is_dsc_encoding_supported(dc)) {
		ret = pre_validate_dsc(state, &dm_state, vars);
		if (ret != 0)
			goto fail;
	}
#endif

	/* Run this here since we want to validate the streams we created */
	ret = drm_atomic_helper_check_planes(dev, state);
	if (ret) {
		drm_dbg_atomic(dev, "drm_atomic_helper_check_planes() failed: %pe\n", ERR_PTR(ret));
		goto fail;
	}

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		if (dm_new_crtc_state->mpo_requested)
			drm_dbg_atomic(dev, "MPO enablement requested on crtc:[%p]\n", crtc);
	}

	/* Check cursor restrictions */
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		enum amdgpu_dm_cursor_mode required_cursor_mode;
		int is_rotated, is_scaled;

		/* Overlay cusor not subject to native cursor restrictions */
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		if (dm_new_crtc_state->cursor_mode == DM_CURSOR_OVERLAY_MODE)
			continue;

		/* Check if rotation or scaling is enabled on DCN401 */
		if ((drm_plane_mask(crtc->cursor) &
		     new_crtc_state->plane_mask) &&
		    (amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(4, 2, 1) ||
		     amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(4, 2, 0) ||
		     amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(4, 0, 1))) {
			new_cursor_state = drm_atomic_get_new_plane_state(state, crtc->cursor);

			is_rotated = new_cursor_state &&
				((new_cursor_state->rotation & DRM_MODE_ROTATE_MASK) != DRM_MODE_ROTATE_0);
			is_scaled = new_cursor_state && ((new_cursor_state->src_w >> 16 != new_cursor_state->crtc_w) ||
				(new_cursor_state->src_h >> 16 != new_cursor_state->crtc_h));

			if (is_rotated || is_scaled) {
				drm_dbg_driver(
					crtc->dev,
					"[CRTC:%d:%s] cannot enable hardware cursor due to rotation/scaling\n",
					crtc->base.id, crtc->name);
				ret = -EINVAL;
				goto fail;
			}
		}

		/* If HW can only do native cursor, check restrictions again */
		ret = dm_crtc_get_cursor_mode(adev, state, dm_new_crtc_state,
					      &required_cursor_mode);
		if (ret) {
			drm_dbg_driver(crtc->dev,
				       "[CRTC:%d:%s] Checking cursor mode failed\n",
				       crtc->base.id, crtc->name);
			goto fail;
		} else if (required_cursor_mode == DM_CURSOR_OVERLAY_MODE) {
			drm_dbg_driver(crtc->dev,
				       "[CRTC:%d:%s] Cannot enable native cursor due to scaling, YUV, or color pipeline restrictions\n",
				       crtc->base.id, crtc->name);
			ret = -EINVAL;
			goto fail;
		}
	}

	if (state->legacy_cursor_update) {
		/*
		 * This is a fast cursor update coming from the plane update
		 * helper, check if it can be done asynchronously for better
		 * performance.
		 */
		state->async_update =
			!drm_atomic_helper_async_check(dev, state);

		/*
		 * Skip the remaining global validation if this is an async
		 * update. Cursor updates can be done without affecting
		 * state or bandwidth calcs and this avoids the performance
		 * penalty of locking the private state object and
		 * allocating a new dc_state.
		 */
		if (state->async_update)
			return 0;
	}

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

		lock_and_validation_needed = true;
	}

	/* set the slot info for each mst_state based on the link encoding format */
	for_each_new_mst_mgr_in_state(state, mgr, mst_state, i) {
		struct amdgpu_dm_connector *aconnector;
		struct drm_connector *connector;
		struct drm_connector_list_iter iter;
		u8 link_coding_cap;

		drm_connector_list_iter_begin(dev, &iter);
		drm_for_each_connector_iter(connector, &iter) {
			if (connector->index == mst_state->mgr->conn_base_id) {
				aconnector = to_amdgpu_dm_connector(connector);
				link_coding_cap = dc_link_dp_mst_decide_link_encoding_format(aconnector->dc_link);
				drm_dp_mst_update_slots(mst_state, link_coding_cap);

				break;
			}
		}
		drm_connector_list_iter_end(&iter);
	}

	/**
	 * Streams and planes are reset when there are changes that affect
	 * bandwidth. Anything that affects bandwidth needs to go through
	 * DC global validation to ensure that the configuration can be applied
	 * to hardware.
	 *
	 * We have to currently stall out here in atomic_check for outstanding
	 * commits to finish in this case because our IRQ handlers reference
	 * DRM state directly - we can end up disabling interrupts too early
	 * if we don't.
	 *
	 * TODO: Remove this stall and drop DM state private objects.
	 */
	if (lock_and_validation_needed) {
		ret = dm_atomic_get_state(state, &dm_state);
		if (ret) {
			drm_dbg_atomic(dev, "dm_atomic_get_state() failed: %pe\n", ERR_PTR(ret));
			goto fail;
		}

		ret = do_aquire_global_lock(dev, state);
		if (ret) {
			drm_dbg_atomic(dev, "do_aquire_global_lock() failed: %pe\n", ERR_PTR(ret));
			goto fail;
		}

#if defined(CONFIG_DRM_AMD_DC_FP)
		if (dc_resource_is_dsc_encoding_supported(dc)) {
			ret = compute_mst_dsc_configs_for_state(state, dm_state->context, vars);
			if (ret) {
				drm_dbg_atomic(dev, "MST_DSC compute_mst_dsc_configs_for_state() failed: %pe\n", ERR_PTR(ret));
				ret = -EINVAL;
				goto fail;
			}
		}
#endif

		ret = dm_update_mst_vcpi_slots_for_dsc(state, dm_state->context, vars);
		if (ret) {
			drm_dbg_atomic(dev, "dm_update_mst_vcpi_slots_for_dsc() failed: %pe\n", ERR_PTR(ret));
			goto fail;
		}

		/*
		 * Perform validation of MST topology in the state:
		 * We need to perform MST atomic check before calling
		 * dc_validate_global_state(), or there is a chance
		 * to get stuck in an infinite loop and hang eventually.
		 */
		ret = drm_dp_mst_atomic_check(state);
		if (ret) {
			drm_dbg_atomic(dev, "MST drm_dp_mst_atomic_check() failed: %pe\n", ERR_PTR(ret));
			goto fail;
		}
		status = dc_validate_global_state(dc, dm_state->context, DC_VALIDATE_MODE_ONLY);
		if (status != DC_OK) {
			drm_dbg_atomic(dev, "DC global validation failure: %s (%d)",
				       dc_status_to_str(status), status);
			ret = -EINVAL;
			goto fail;
		}
	} else {
		/*
		 * The commit is a fast update. Fast updates shouldn't change
		 * the DC context, affect global validation, and can have their
		 * commit work done in parallel with other commits not touching
		 * the same resource. If we have a new DC context as part of
		 * the DM atomic state from validation we need to free it and
		 * retain the existing one instead.
		 *
		 * Furthermore, since the DM atomic state only contains the DC
		 * context and can safely be annulled, we can free the state
		 * and clear the associated private object now to free
		 * some memory and avoid a possible use-after-free later.
		 */

		for (i = 0; i < state->num_private_objs; i++) {
			struct drm_private_obj *obj = state->private_objs[i].ptr;

			if (obj->funcs == adev->dm.atomic_obj.funcs) {
				int j = state->num_private_objs-1;

				dm_atomic_destroy_state(obj,
						state->private_objs[i].state_to_destroy);

				/* If i is not at the end of the array then the
				 * last element needs to be moved to where i was
				 * before the array can safely be truncated.
				 */
				if (i != j)
					state->private_objs[i] =
						state->private_objs[j];

				state->private_objs[j].ptr = NULL;
				state->private_objs[j].state_to_destroy = NULL;
				state->private_objs[j].old_state = NULL;
				state->private_objs[j].new_state = NULL;

				state->num_private_objs = j;
				break;
			}
		}
	}

	/* Store the overall update type for use later in atomic check. */
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		struct dm_crtc_state *dm_new_crtc_state =
			to_dm_crtc_state(new_crtc_state);

		/*
		 * Only allow async flips for fast updates that don't change
		 * the FB pitch, the DCC state, rotation, mem_type, etc.
		 */
		if (new_crtc_state->async_flip &&
		    (lock_and_validation_needed ||
		     amdgpu_dm_crtc_mem_type_changed(dev, state, new_crtc_state))) {
			drm_dbg_atomic(crtc->dev,
				       "[CRTC:%d:%s] async flips are only supported for fast updates\n",
				       crtc->base.id, crtc->name);
			ret = -EINVAL;
			goto fail;
		}

		dm_new_crtc_state->update_type = lock_and_validation_needed ?
			UPDATE_TYPE_FULL : UPDATE_TYPE_FAST;
	}

	/* Must be success */
	WARN_ON(ret);

	trace_amdgpu_dm_atomic_check_finish(state, ret);

	return ret;

fail:
	if (ret == -EDEADLK)
		drm_dbg_atomic(dev, "Atomic check stopped to avoid deadlock.\n");
	else if (ret == -EINTR || ret == -EAGAIN || ret == -ERESTARTSYS)
		drm_dbg_atomic(dev, "Atomic check stopped due to signal.\n");
	else
		drm_dbg_atomic(dev, "Atomic check failed: %pe\n", ERR_PTR(ret));

	trace_amdgpu_dm_atomic_check_finish(state, ret);

	return ret;
}

void amdgpu_dm_trigger_timing_sync(struct drm_device *dev)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dc *dc = adev->dm.dc;
	int i;

	mutex_lock(&adev->dm.dc_lock);
	if (dc->current_state) {
		for (i = 0; i < dc->current_state->stream_count; ++i)
			dc->current_state->streams[i]
				->triggered_crtc_reset.enabled =
				adev->dm.force_timing_sync;

		dm_enable_per_frame_crtc_master_sync(dc->current_state);
		dc_trigger_sync(dc, dc->current_state);
	}
	mutex_unlock(&adev->dm.dc_lock);
}

void dm_write_reg_func(const struct dc_context *ctx, uint32_t address,
		       u32 value, const char *func_name)
{
#ifdef DM_CHECK_ADDR_0
	if (address == 0) {
		drm_err(adev_to_drm(ctx->driver_context),
			"invalid register write. address = 0");
		return;
	}
#endif

	amdgpu_dm_exit_ips_for_hw_access(ctx->dc);
	cgs_write_register(ctx->cgs_device, address, value);
	trace_amdgpu_dc_wreg(&ctx->perf_trace->write_count, address, value);
}

uint32_t dm_read_reg_func(const struct dc_context *ctx, uint32_t address,
			  const char *func_name)
{
	u32 value;
#ifdef DM_CHECK_ADDR_0
	if (address == 0) {
		drm_err(adev_to_drm(ctx->driver_context),
			"invalid register read; address = 0\n");
		return 0;
	}
#endif

	amdgpu_dm_exit_ips_for_hw_access(ctx->dc);

	value = cgs_read_register(ctx->cgs_device, address);

	trace_amdgpu_dc_rreg(&ctx->perf_trace->read_count, address, value);

	return value;
}

void dm_acpi_process_phy_transition_interlock(
	const struct dc_context *ctx,
	struct dm_process_phy_transition_init_params process_phy_transition_init_params)
{
	// Not yet implemented
}
