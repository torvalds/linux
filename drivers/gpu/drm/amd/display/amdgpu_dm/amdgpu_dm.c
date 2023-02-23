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
#include "dc_link_dp.h"
#include "link_enc_cfg.h"
#include "dc/inc/core_types.h"
#include "dal_asic_id.h"
#include "dmub/dmub_srv.h"
#include "dc/inc/hw/dmcu.h"
#include "dc/inc/hw/abm.h"
#include "dc/dc_dmub_srv.h"
#include "dc/dc_edid_parser.h"
#include "dc/dc_stat.h"
#include "amdgpu_dm_trace.h"
#include "dc/inc/dc_link_ddc.h"

#include "vid.h"
#include "amdgpu.h"
#include "amdgpu_display.h"
#include "amdgpu_ucode.h"
#include "atom.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_plane.h"
#include "amdgpu_dm_crtc.h"
#ifdef CONFIG_DRM_AMD_DC_HDCP
#include "amdgpu_dm_hdcp.h"
#include <drm/display/drm_hdcp_helper.h>
#endif
#include "amdgpu_pm.h"
#include "amdgpu_atombios.h"

#include "amd_shared.h"
#include "amdgpu_dm_irq.h"
#include "dm_helpers.h"
#include "amdgpu_dm_mst_types.h"
#if defined(CONFIG_DEBUG_FS)
#include "amdgpu_dm_debugfs.h"
#endif
#include "amdgpu_dm_psr.h"

#include "ivsrcid/ivsrcid_vislands30.h"

#include "i2caux_interface.h"
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/pm_runtime.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/component.h>
#include <linux/dmi.h>

#include <drm/display/drm_dp_mst_helper.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_edid.h>
#include <drm/drm_vblank.h>
#include <drm/drm_audio_component.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_plane_helper.h>

#include <acpi/video.h>

#include "ivsrcid/dcn/irqsrcs_dcn_1_0.h"

#include "dcn/dcn_1_0_offset.h"
#include "dcn/dcn_1_0_sh_mask.h"
#include "soc15_hw_ip.h"
#include "soc15_common.h"
#include "vega10_ip_offset.h"

#include "gc/gc_11_0_0_offset.h"
#include "gc/gc_11_0_0_sh_mask.h"

#include "modules/inc/mod_freesync.h"
#include "modules/power/power_helpers.h"
#include "modules/inc/mod_info_packet.h"

#define FIRMWARE_RENOIR_DMUB "amdgpu/renoir_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_RENOIR_DMUB);
#define FIRMWARE_SIENNA_CICHLID_DMUB "amdgpu/sienna_cichlid_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_SIENNA_CICHLID_DMUB);
#define FIRMWARE_NAVY_FLOUNDER_DMUB "amdgpu/navy_flounder_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_NAVY_FLOUNDER_DMUB);
#define FIRMWARE_GREEN_SARDINE_DMUB "amdgpu/green_sardine_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_GREEN_SARDINE_DMUB);
#define FIRMWARE_VANGOGH_DMUB "amdgpu/vangogh_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_VANGOGH_DMUB);
#define FIRMWARE_DIMGREY_CAVEFISH_DMUB "amdgpu/dimgrey_cavefish_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DIMGREY_CAVEFISH_DMUB);
#define FIRMWARE_BEIGE_GOBY_DMUB "amdgpu/beige_goby_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_BEIGE_GOBY_DMUB);
#define FIRMWARE_YELLOW_CARP_DMUB "amdgpu/yellow_carp_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_YELLOW_CARP_DMUB);
#define FIRMWARE_DCN_314_DMUB "amdgpu/dcn_3_1_4_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN_314_DMUB);
#define FIRMWARE_DCN_315_DMUB "amdgpu/dcn_3_1_5_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN_315_DMUB);
#define FIRMWARE_DCN316_DMUB "amdgpu/dcn_3_1_6_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN316_DMUB);

#define FIRMWARE_DCN_V3_2_0_DMCUB "amdgpu/dcn_3_2_0_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN_V3_2_0_DMCUB);
#define FIRMWARE_DCN_V3_2_1_DMCUB "amdgpu/dcn_3_2_1_dmcub.bin"
MODULE_FIRMWARE(FIRMWARE_DCN_V3_2_1_DMCUB);

#define FIRMWARE_RAVEN_DMCU		"amdgpu/raven_dmcu.bin"
MODULE_FIRMWARE(FIRMWARE_RAVEN_DMCU);

#define FIRMWARE_NAVI12_DMCU            "amdgpu/navi12_dmcu.bin"
MODULE_FIRMWARE(FIRMWARE_NAVI12_DMCU);

/* Number of bytes in PSP header for firmware. */
#define PSP_HEADER_BYTES 0x100

/* Number of bytes in PSP footer for firmware. */
#define PSP_FOOTER_BYTES 0x100

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
static bool is_freesync_video_mode(const struct drm_display_mode *mode, struct amdgpu_dm_connector *aconnector);

static enum drm_mode_subconnector get_subconnector_type(struct dc_link *link)
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

static void update_subconnector_property(struct amdgpu_dm_connector *aconnector)
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

static int amdgpu_dm_connector_init(struct amdgpu_display_manager *dm,
				    struct amdgpu_dm_connector *amdgpu_dm_connector,
				    uint32_t link_index,
				    struct amdgpu_encoder *amdgpu_encoder);
static int amdgpu_dm_encoder_init(struct drm_device *dev,
				  struct amdgpu_encoder *aencoder,
				  uint32_t link_index);

static int amdgpu_dm_connector_get_modes(struct drm_connector *connector);

static void amdgpu_dm_atomic_commit_tail(struct drm_atomic_state *state);

static int amdgpu_dm_atomic_check(struct drm_device *dev,
				  struct drm_atomic_state *state);

static void handle_hpd_irq_helper(struct amdgpu_dm_connector *aconnector);
static void handle_hpd_rx_irq(void *param);

static bool
is_timing_unchanged_for_freesync(struct drm_crtc_state *old_crtc_state,
				 struct drm_crtc_state *new_crtc_state);
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

		if (acrtc->dm_irq_params.stream == NULL) {
			DRM_ERROR("dc_stream_state is NULL for crtc '%d'!\n",
				  crtc);
			return 0;
		}

		return dc_stream_get_vblank_counter(acrtc->dm_irq_params.stream);
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

		if (acrtc->dm_irq_params.stream ==  NULL) {
			DRM_ERROR("dc_stream_state is NULL for crtc '%d'!\n",
				  crtc);
			return 0;
		}

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
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_crtc *crtc;
	struct amdgpu_crtc *amdgpu_crtc;

	if (WARN_ON(otg_inst == -1))
		return adev->mode_info.crtcs[0];

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		amdgpu_crtc = to_amdgpu_crtc(crtc);

		if (amdgpu_crtc->otg_inst == otg_inst)
			return amdgpu_crtc;
	}

	return NULL;
}

static inline bool is_dc_timing_adjust_needed(struct dm_crtc_state *old_state,
					      struct dm_crtc_state *new_state)
{
	if (new_state->freesync_config.state ==  VRR_STATE_ACTIVE_FIXED)
		return true;
	else if (amdgpu_dm_vrr_active(old_state) != amdgpu_dm_vrr_active(new_state))
		return true;
	else
		return false;
}

/**
 * update_planes_and_stream_adapter() - Send planes to be updated in DC
 *
 * DC has a generic way to update planes and stream via
 * dc_update_planes_and_stream function; however, DM might need some
 * adjustments and preparation before calling it. This function is a wrapper
 * for the dc_update_planes_and_stream that does any required configuration
 * before passing control to DC.
 */
static inline bool update_planes_and_stream_adapter(struct dc *dc,
						    int update_type,
						    int planes_count,
						    struct dc_stream_state *stream,
						    struct dc_stream_update *stream_update,
						    struct dc_surface_update *array_of_surface_update)
{
	/*
	 * Previous frame finished and HW is ready for optimization.
	 */
	if (update_type == UPDATE_TYPE_FAST)
		dc_post_update_surfaces_to_stream(dc);

	return dc_update_planes_and_stream(dc,
					   array_of_surface_update,
					   planes_count,
					   stream,
					   stream_update);
}

/**
 * dm_pflip_high_irq() - Handle pageflip interrupt
 * @interrupt_params: ignored
 *
 * Handles the pageflip interrupt by notifying all interested parties
 * that the pageflip has been completed.
 */
static void dm_pflip_high_irq(void *interrupt_params)
{
	struct amdgpu_crtc *amdgpu_crtc;
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	unsigned long flags;
	struct drm_pending_vblank_event *e;
	uint32_t vpos, hpos, v_blank_start, v_blank_end;
	bool vrr_active;

	amdgpu_crtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_PFLIP);

	/* IRQ could occur when in initial stage */
	/* TODO work and BO cleanup */
	if (amdgpu_crtc == NULL) {
		DC_LOG_PFLIP("CRTC is null, returning.\n");
		return;
	}

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);

	if (amdgpu_crtc->pflip_status != AMDGPU_FLIP_SUBMITTED){
		DC_LOG_PFLIP("amdgpu_crtc->pflip_status = %d !=AMDGPU_FLIP_SUBMITTED(%d) on crtc:%d[%p] \n",
						 amdgpu_crtc->pflip_status,
						 AMDGPU_FLIP_SUBMITTED,
						 amdgpu_crtc->crtc_id,
						 amdgpu_crtc);
		spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
		return;
	}

	/* page flip completed. */
	e = amdgpu_crtc->event;
	amdgpu_crtc->event = NULL;

	WARN_ON(!e);

	vrr_active = amdgpu_dm_vrr_active_irq(amdgpu_crtc);

	/* Fixed refresh rate, or VRR scanout position outside front-porch? */
	if (!vrr_active ||
	    !dc_stream_get_scanoutpos(amdgpu_crtc->dm_irq_params.stream, &v_blank_start,
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

		list_add_tail(&e->base.link, &adev_to_drm(adev)->vblank_event_list);
		e = NULL;
	}

	/* Keep track of vblank of this flip for flip throttling. We use the
	 * cooked hw counter, as that one incremented at start of this vblank
	 * of pageflip completion, so last_flip_vblank is the forbidden count
	 * for queueing new pageflips if vsync + VRR is enabled.
	 */
	amdgpu_crtc->dm_irq_params.last_flip_vblank =
		amdgpu_get_vblank_counter_kms(&amdgpu_crtc->base);

	amdgpu_crtc->pflip_status = AMDGPU_FLIP_NONE;
	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);

	DC_LOG_PFLIP("crtc:%d[%p], pflip_stat:AMDGPU_FLIP_NONE, vrr[%d]-fp %d\n",
		     amdgpu_crtc->crtc_id, amdgpu_crtc,
		     vrr_active, (int) !e);
}

static void dm_vupdate_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_crtc *acrtc;
	struct drm_device *drm_dev;
	struct drm_vblank_crtc *vblank;
	ktime_t frame_duration_ns, previous_timestamp;
	unsigned long flags;
	int vrr_active;

	acrtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VUPDATE);

	if (acrtc) {
		vrr_active = amdgpu_dm_vrr_active_irq(acrtc);
		drm_dev = acrtc->base.dev;
		vblank = &drm_dev->vblank[acrtc->base.index];
		previous_timestamp = atomic64_read(&irq_params->previous_timestamp);
		frame_duration_ns = vblank->time - previous_timestamp;

		if (frame_duration_ns > 0) {
			trace_amdgpu_refresh_rate_track(acrtc->base.index,
						frame_duration_ns,
						ktime_divns(NSEC_PER_SEC, frame_duration_ns));
			atomic64_set(&irq_params->previous_timestamp, vblank->time);
		}

		DC_LOG_VBLANK("crtc:%d, vupdate-vrr:%d\n",
			      acrtc->crtc_id,
			      vrr_active);

		/* Core vblank handling is done here after end of front-porch in
		 * vrr mode, as vblank timestamping will give valid results
		 * while now done after front-porch. This will also deliver
		 * page-flip completion events that have been queued to us
		 * if a pageflip happened inside front-porch.
		 */
		if (vrr_active) {
			dm_crtc_handle_vblank(acrtc);

			/* BTR processing for pre-DCE12 ASICs */
			if (acrtc->dm_irq_params.stream &&
			    adev->family < AMDGPU_FAMILY_AI) {
				spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
				mod_freesync_handle_v_update(
				    adev->dm.freesync_module,
				    acrtc->dm_irq_params.stream,
				    &acrtc->dm_irq_params.vrr_params);

				dc_stream_adjust_vmin_vmax(
				    adev->dm.dc,
				    acrtc->dm_irq_params.stream,
				    &acrtc->dm_irq_params.vrr_params.adjust);
				spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
			}
		}
	}
}

/**
 * dm_crtc_high_irq() - Handles CRTC interrupt
 * @interrupt_params: used for determining the CRTC instance
 *
 * Handles the CRTC/VSYNC interrupt by notfying DRM's VBLANK
 * event handler.
 */
static void dm_crtc_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_crtc *acrtc;
	unsigned long flags;
	int vrr_active;

	acrtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VBLANK);
	if (!acrtc)
		return;

	vrr_active = amdgpu_dm_vrr_active_irq(acrtc);

	DC_LOG_VBLANK("crtc:%d, vupdate-vrr:%d, planes:%d\n", acrtc->crtc_id,
		      vrr_active, acrtc->dm_irq_params.active_planes);

	/**
	 * Core vblank handling at start of front-porch is only possible
	 * in non-vrr mode, as only there vblank timestamping will give
	 * valid results while done in front-porch. Otherwise defer it
	 * to dm_vupdate_high_irq after end of front-porch.
	 */
	if (!vrr_active)
		dm_crtc_handle_vblank(acrtc);

	/**
	 * Following stuff must happen at start of vblank, for crc
	 * computation and below-the-range btr support in vrr mode.
	 */
	amdgpu_dm_crtc_handle_crc_irq(&acrtc->base);

	/* BTR updates need to happen before VUPDATE on Vega and above. */
	if (adev->family < AMDGPU_FAMILY_AI)
		return;

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);

	if (acrtc->dm_irq_params.stream &&
	    acrtc->dm_irq_params.vrr_params.supported &&
	    acrtc->dm_irq_params.freesync_config.state ==
		    VRR_STATE_ACTIVE_VARIABLE) {
		mod_freesync_handle_v_update(adev->dm.freesync_module,
					     acrtc->dm_irq_params.stream,
					     &acrtc->dm_irq_params.vrr_params);

		dc_stream_adjust_vmin_vmax(adev->dm.dc, acrtc->dm_irq_params.stream,
					   &acrtc->dm_irq_params.vrr_params.adjust);
	}

	/*
	 * If there aren't any active_planes then DCH HUBP may be clock-gated.
	 * In that case, pageflip completion interrupts won't fire and pageflip
	 * completion events won't get delivered. Prevent this by sending
	 * pending pageflip events from here if a flip is still pending.
	 *
	 * If any planes are enabled, use dm_pflip_high_irq() instead, to
	 * avoid race conditions between flip programming and completion,
	 * which could cause too early flip completion events.
	 */
	if (adev->family >= AMDGPU_FAMILY_RV &&
	    acrtc->pflip_status == AMDGPU_FLIP_SUBMITTED &&
	    acrtc->dm_irq_params.active_planes == 0) {
		if (acrtc->event) {
			drm_crtc_send_vblank_event(&acrtc->base, acrtc->event);
			acrtc->event = NULL;
			drm_crtc_vblank_put(&acrtc->base);
		}
		acrtc->pflip_status = AMDGPU_FLIP_NONE;
	}

	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
}

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
/**
 * dm_dcn_vertical_interrupt0_high_irq() - Handles OTG Vertical interrupt0 for
 * DCN generation ASICs
 * @interrupt_params: interrupt parameters
 *
 * Used to set crc window/read out crc value at vertical line 0 position
 */
static void dm_dcn_vertical_interrupt0_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_crtc *acrtc;

	acrtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VLINE0);

	if (!acrtc)
		return;

	amdgpu_dm_crtc_handle_crc_window_irq(&acrtc->base);
}
#endif /* CONFIG_DRM_AMD_SECURE_DISPLAY */

/**
 * dmub_aux_setconfig_callback - Callback for AUX or SET_CONFIG command.
 * @adev: amdgpu_device pointer
 * @notify: dmub notification structure
 *
 * Dmub AUX or SET_CONFIG command completion processing callback
 * Copies dmub notification to DM which is to be read by AUX command.
 * issuing thread and also signals the event to wake up the thread.
 */
static void dmub_aux_setconfig_callback(struct amdgpu_device *adev,
					struct dmub_notification *notify)
{
	if (adev->dm.dmub_notify)
		memcpy(adev->dm.dmub_notify, notify, sizeof(struct dmub_notification));
	if (notify->type == DMUB_NOTIFICATION_AUX_REPLY)
		complete(&adev->dm.dmub_aux_transfer_done);
}

/**
 * dmub_hpd_callback - DMUB HPD interrupt processing callback.
 * @adev: amdgpu_device pointer
 * @notify: dmub notification structure
 *
 * Dmub Hpd interrupt processing callback. Gets displayindex through the
 * ink index and calls helper to do the processing.
 */
static void dmub_hpd_callback(struct amdgpu_device *adev,
			      struct dmub_notification *notify)
{
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_dm_connector *hpd_aconnector = NULL;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	struct dc_link *link;
	uint8_t link_index = 0;
	struct drm_device *dev;

	if (adev == NULL)
		return;

	if (notify == NULL) {
		DRM_ERROR("DMUB HPD callback notification was NULL");
		return;
	}

	if (notify->link_index > adev->dm.dc->link_count) {
		DRM_ERROR("DMUB HPD index (%u)is abnormal", notify->link_index);
		return;
	}

	link_index = notify->link_index;
	link = adev->dm.dc->links[link_index];
	dev = adev->dm.ddev;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		aconnector = to_amdgpu_dm_connector(connector);
		if (link && aconnector->dc_link == link) {
			DRM_INFO("DMUB HPD callback: link_index=%u\n", link_index);
			hpd_aconnector = aconnector;
			break;
		}
	}
	drm_connector_list_iter_end(&iter);

	if (hpd_aconnector) {
		if (notify->type == DMUB_NOTIFICATION_HPD)
			handle_hpd_irq_helper(hpd_aconnector);
		else if (notify->type == DMUB_NOTIFICATION_HPD_IRQ)
			handle_hpd_rx_irq(hpd_aconnector);
	}
}

/**
 * register_dmub_notify_callback - Sets callback for DMUB notify
 * @adev: amdgpu_device pointer
 * @type: Type of dmub notification
 * @callback: Dmub interrupt callback function
 * @dmub_int_thread_offload: offload indicator
 *
 * API to register a dmub callback handler for a dmub notification
 * Also sets indicator whether callback processing to be offloaded.
 * to dmub interrupt handling thread
 * Return: true if successfully registered, false if there is existing registration
 */
static bool register_dmub_notify_callback(struct amdgpu_device *adev,
					  enum dmub_notification_type type,
					  dmub_notify_interrupt_callback_t callback,
					  bool dmub_int_thread_offload)
{
	if (callback != NULL && type < ARRAY_SIZE(adev->dm.dmub_thread_offload)) {
		adev->dm.dmub_callback[type] = callback;
		adev->dm.dmub_thread_offload[type] = dmub_int_thread_offload;
	} else
		return false;

	return true;
}

static void dm_handle_hpd_work(struct work_struct *work)
{
	struct dmub_hpd_work *dmub_hpd_wrk;

	dmub_hpd_wrk = container_of(work, struct dmub_hpd_work, handle_hpd_work);

	if (!dmub_hpd_wrk->dmub_notify) {
		DRM_ERROR("dmub_hpd_wrk dmub_notify is NULL");
		return;
	}

	if (dmub_hpd_wrk->dmub_notify->type < ARRAY_SIZE(dmub_hpd_wrk->adev->dm.dmub_callback)) {
		dmub_hpd_wrk->adev->dm.dmub_callback[dmub_hpd_wrk->dmub_notify->type](dmub_hpd_wrk->adev,
		dmub_hpd_wrk->dmub_notify);
	}

	kfree(dmub_hpd_wrk->dmub_notify);
	kfree(dmub_hpd_wrk);

}

#define DMUB_TRACE_MAX_READ 64
/**
 * dm_dmub_outbox1_low_irq() - Handles Outbox interrupt
 * @interrupt_params: used for determining the Outbox instance
 *
 * Handles the Outbox Interrupt
 * event handler.
 */
static void dm_dmub_outbox1_low_irq(void *interrupt_params)
{
	struct dmub_notification notify;
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct dmcub_trace_buf_entry entry = { 0 };
	uint32_t count = 0;
	struct dmub_hpd_work *dmub_hpd_wrk;
	struct dc_link *plink = NULL;

	if (dc_enable_dmub_notifications(adev->dm.dc) &&
		irq_params->irq_src == DC_IRQ_SOURCE_DMCUB_OUTBOX) {

		do {
			dc_stat_get_dmub_notification(adev->dm.dc, &notify);
			if (notify.type >= ARRAY_SIZE(dm->dmub_thread_offload)) {
				DRM_ERROR("DM: notify type %d invalid!", notify.type);
				continue;
			}
			if (!dm->dmub_callback[notify.type]) {
				DRM_DEBUG_DRIVER("DMUB notification skipped, no handler: type=%d\n", notify.type);
				continue;
			}
			if (dm->dmub_thread_offload[notify.type] == true) {
				dmub_hpd_wrk = kzalloc(sizeof(*dmub_hpd_wrk), GFP_ATOMIC);
				if (!dmub_hpd_wrk) {
					DRM_ERROR("Failed to allocate dmub_hpd_wrk");
					return;
				}
				dmub_hpd_wrk->dmub_notify = kzalloc(sizeof(struct dmub_notification), GFP_ATOMIC);
				if (!dmub_hpd_wrk->dmub_notify) {
					kfree(dmub_hpd_wrk);
					DRM_ERROR("Failed to allocate dmub_hpd_wrk->dmub_notify");
					return;
				}
				INIT_WORK(&dmub_hpd_wrk->handle_hpd_work, dm_handle_hpd_work);
				if (dmub_hpd_wrk->dmub_notify)
					memcpy(dmub_hpd_wrk->dmub_notify, &notify, sizeof(struct dmub_notification));
				dmub_hpd_wrk->adev = adev;
				if (notify.type == DMUB_NOTIFICATION_HPD) {
					plink = adev->dm.dc->links[notify.link_index];
					if (plink) {
						plink->hpd_status =
							notify.hpd_status == DP_HPD_PLUG;
					}
				}
				queue_work(adev->dm.delayed_hpd_wq, &dmub_hpd_wrk->handle_hpd_work);
			} else {
				dm->dmub_callback[notify.type](adev, &notify);
			}
		} while (notify.pending_notification);
	}


	do {
		if (dc_dmub_srv_get_dmub_outbox0_msg(dm->dc, &entry)) {
			trace_amdgpu_dmub_trace_high_irq(entry.trace_code, entry.tick_count,
							entry.param0, entry.param1);

			DRM_DEBUG_DRIVER("trace_code:%u, tick_count:%u, param0:%u, param1:%u\n",
				 entry.trace_code, entry.tick_count, entry.param0, entry.param1);
		} else
			break;

		count++;

	} while (count <= DMUB_TRACE_MAX_READ);

	if (count > DMUB_TRACE_MAX_READ)
		DRM_DEBUG_DRIVER("Warning : count > DMUB_TRACE_MAX_READ");
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
	struct amdgpu_device *adev = drm_to_adev(dev);
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

static int amdgpu_dm_audio_component_get_eld(struct device *kdev, int port,
					  int pipe, bool *enabled,
					  unsigned char *buf, int max_bytes)
{
	struct drm_device *dev = dev_get_drvdata(kdev);
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct amdgpu_dm_connector *aconnector;
	int ret = 0;

	*enabled = false;

	mutex_lock(&adev->dm.audio_lock);

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->audio_inst != port)
			continue;

		*enabled = true;
		ret = drm_eld_size(connector->eld);
		memcpy(buf, connector->eld, min(max_bytes, ret));

		break;
	}
	drm_connector_list_iter_end(&conn_iter);

	mutex_unlock(&adev->dm.audio_lock);

	DRM_DEBUG_KMS("Get ELD : idx=%d ret=%d en=%d\n", port, ret, *enabled);

	return ret;
}

static const struct drm_audio_component_ops amdgpu_dm_audio_component_ops = {
	.get_eld = amdgpu_dm_audio_component_get_eld,
};

static int amdgpu_dm_audio_component_bind(struct device *kdev,
				       struct device *hda_kdev, void *data)
{
	struct drm_device *dev = dev_get_drvdata(kdev);
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct drm_audio_component *acomp = data;

	acomp->ops = &amdgpu_dm_audio_component_ops;
	acomp->dev = kdev;
	adev->dm.audio_component = acomp;

	return 0;
}

static void amdgpu_dm_audio_component_unbind(struct device *kdev,
					  struct device *hda_kdev, void *data)
{
	struct drm_device *dev = dev_get_drvdata(kdev);
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct drm_audio_component *acomp = data;

	acomp->ops = NULL;
	acomp->dev = NULL;
	adev->dm.audio_component = NULL;
}

static const struct component_ops amdgpu_dm_audio_component_bind_ops = {
	.bind	= amdgpu_dm_audio_component_bind,
	.unbind	= amdgpu_dm_audio_component_unbind,
};

static int amdgpu_dm_audio_init(struct amdgpu_device *adev)
{
	int i, ret;

	if (!amdgpu_audio)
		return 0;

	adev->mode_info.audio.enabled = true;

	adev->mode_info.audio.num_pins = adev->dm.dc->res_pool->audio_count;

	for (i = 0; i < adev->mode_info.audio.num_pins; i++) {
		adev->mode_info.audio.pin[i].channels = -1;
		adev->mode_info.audio.pin[i].rate = -1;
		adev->mode_info.audio.pin[i].bits_per_sample = -1;
		adev->mode_info.audio.pin[i].status_bits = 0;
		adev->mode_info.audio.pin[i].category_code = 0;
		adev->mode_info.audio.pin[i].connected = false;
		adev->mode_info.audio.pin[i].id =
			adev->dm.dc->res_pool->audios[i]->inst;
		adev->mode_info.audio.pin[i].offset = 0;
	}

	ret = component_add(adev->dev, &amdgpu_dm_audio_component_bind_ops);
	if (ret < 0)
		return ret;

	adev->dm.audio_registered = true;

	return 0;
}

static void amdgpu_dm_audio_fini(struct amdgpu_device *adev)
{
	if (!amdgpu_audio)
		return;

	if (!adev->mode_info.audio.enabled)
		return;

	if (adev->dm.audio_registered) {
		component_del(adev->dev, &amdgpu_dm_audio_component_bind_ops);
		adev->dm.audio_registered = false;
	}

	/* TODO: Disable audio? */

	adev->mode_info.audio.enabled = false;
}

static  void amdgpu_dm_audio_eld_notify(struct amdgpu_device *adev, int pin)
{
	struct drm_audio_component *acomp = adev->dm.audio_component;

	if (acomp && acomp->audio_ops && acomp->audio_ops->pin_eld_notify) {
		DRM_DEBUG_KMS("Notify ELD: %d\n", pin);

		acomp->audio_ops->pin_eld_notify(acomp->audio_ops->audio_ptr,
						 pin, -1);
	}
}

static int dm_dmub_hw_init(struct amdgpu_device *adev)
{
	const struct dmcub_firmware_header_v1_0 *hdr;
	struct dmub_srv *dmub_srv = adev->dm.dmub_srv;
	struct dmub_srv_fb_info *fb_info = adev->dm.dmub_fb_info;
	const struct firmware *dmub_fw = adev->dm.dmub_fw;
	struct dmcu *dmcu = adev->dm.dc->res_pool->dmcu;
	struct abm *abm = adev->dm.dc->res_pool->abm;
	struct dmub_srv_hw_params hw_params;
	enum dmub_status status;
	const unsigned char *fw_inst_const, *fw_bss_data;
	uint32_t i, fw_inst_const_size, fw_bss_data_size;
	bool has_hw_support;

	if (!dmub_srv)
		/* DMUB isn't supported on the ASIC. */
		return 0;

	if (!fb_info) {
		DRM_ERROR("No framebuffer info for DMUB service.\n");
		return -EINVAL;
	}

	if (!dmub_fw) {
		/* Firmware required for DMUB support. */
		DRM_ERROR("No firmware provided for DMUB.\n");
		return -EINVAL;
	}

	status = dmub_srv_has_hw_support(dmub_srv, &has_hw_support);
	if (status != DMUB_STATUS_OK) {
		DRM_ERROR("Error checking HW support for DMUB: %d\n", status);
		return -EINVAL;
	}

	if (!has_hw_support) {
		DRM_INFO("DMUB unsupported on ASIC\n");
		return 0;
	}

	/* Reset DMCUB if it was previously running - before we overwrite its memory. */
	status = dmub_srv_hw_reset(dmub_srv);
	if (status != DMUB_STATUS_OK)
		DRM_WARN("Error resetting DMUB HW: %d\n", status);

	hdr = (const struct dmcub_firmware_header_v1_0 *)dmub_fw->data;

	fw_inst_const = dmub_fw->data +
			le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
			PSP_HEADER_BYTES;

	fw_bss_data = dmub_fw->data +
		      le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
		      le32_to_cpu(hdr->inst_const_bytes);

	/* Copy firmware and bios info into FB memory. */
	fw_inst_const_size = le32_to_cpu(hdr->inst_const_bytes) -
			     PSP_HEADER_BYTES - PSP_FOOTER_BYTES;

	fw_bss_data_size = le32_to_cpu(hdr->bss_data_bytes);

	/* if adev->firmware.load_type == AMDGPU_FW_LOAD_PSP,
	 * amdgpu_ucode_init_single_fw will load dmub firmware
	 * fw_inst_const part to cw0; otherwise, the firmware back door load
	 * will be done by dm_dmub_hw_init
	 */
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		memcpy(fb_info->fb[DMUB_WINDOW_0_INST_CONST].cpu_addr, fw_inst_const,
				fw_inst_const_size);
	}

	if (fw_bss_data_size)
		memcpy(fb_info->fb[DMUB_WINDOW_2_BSS_DATA].cpu_addr,
		       fw_bss_data, fw_bss_data_size);

	/* Copy firmware bios info into FB memory. */
	memcpy(fb_info->fb[DMUB_WINDOW_3_VBIOS].cpu_addr, adev->bios,
	       adev->bios_size);

	/* Reset regions that need to be reset. */
	memset(fb_info->fb[DMUB_WINDOW_4_MAILBOX].cpu_addr, 0,
	fb_info->fb[DMUB_WINDOW_4_MAILBOX].size);

	memset(fb_info->fb[DMUB_WINDOW_5_TRACEBUFF].cpu_addr, 0,
	       fb_info->fb[DMUB_WINDOW_5_TRACEBUFF].size);

	memset(fb_info->fb[DMUB_WINDOW_6_FW_STATE].cpu_addr, 0,
	       fb_info->fb[DMUB_WINDOW_6_FW_STATE].size);

	/* Initialize hardware. */
	memset(&hw_params, 0, sizeof(hw_params));
	hw_params.fb_base = adev->gmc.fb_start;
	hw_params.fb_offset = adev->gmc.aper_base;

	/* backdoor load firmware and trigger dmub running */
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP)
		hw_params.load_inst_const = true;

	if (dmcu)
		hw_params.psp_version = dmcu->psp_version;

	for (i = 0; i < fb_info->num_fb; ++i)
		hw_params.fb[i] = &fb_info->fb[i];

	switch (adev->ip_versions[DCE_HWIP][0]) {
	case IP_VERSION(3, 1, 3):
	case IP_VERSION(3, 1, 4):
		hw_params.dpia_supported = true;
		hw_params.disable_dpia = adev->dm.dc->debug.dpia_debug.bits.disable_dpia;
		break;
	default:
		break;
	}

	status = dmub_srv_hw_init(dmub_srv, &hw_params);
	if (status != DMUB_STATUS_OK) {
		DRM_ERROR("Error initializing DMUB HW: %d\n", status);
		return -EINVAL;
	}

	/* Wait for firmware load to finish. */
	status = dmub_srv_wait_for_auto_load(dmub_srv, 100000);
	if (status != DMUB_STATUS_OK)
		DRM_WARN("Wait for DMUB auto-load failed: %d\n", status);

	/* Init DMCU and ABM if available. */
	if (dmcu && abm) {
		dmcu->funcs->dmcu_init(dmcu);
		abm->dmcu_is_running = dmcu->funcs->is_dmcu_initialized(dmcu);
	}

	if (!adev->dm.dc->ctx->dmub_srv)
		adev->dm.dc->ctx->dmub_srv = dc_dmub_srv_create(adev->dm.dc, dmub_srv);
	if (!adev->dm.dc->ctx->dmub_srv) {
		DRM_ERROR("Couldn't allocate DC DMUB server!\n");
		return -ENOMEM;
	}

	DRM_INFO("DMUB hardware initialized: version=0x%08X\n",
		 adev->dm.dmcub_fw_version);

	return 0;
}

static void dm_dmub_hw_resume(struct amdgpu_device *adev)
{
	struct dmub_srv *dmub_srv = adev->dm.dmub_srv;
	enum dmub_status status;
	bool init;

	if (!dmub_srv) {
		/* DMUB isn't supported on the ASIC. */
		return;
	}

	status = dmub_srv_is_hw_init(dmub_srv, &init);
	if (status != DMUB_STATUS_OK)
		DRM_WARN("DMUB hardware init check failed: %d\n", status);

	if (status == DMUB_STATUS_OK && init) {
		/* Wait for firmware load to finish. */
		status = dmub_srv_wait_for_auto_load(dmub_srv, 100000);
		if (status != DMUB_STATUS_OK)
			DRM_WARN("Wait for DMUB auto-load failed: %d\n", status);
	} else {
		/* Perform the full hardware initialization. */
		dm_dmub_hw_init(adev);
	}
}

static void mmhub_read_system_context(struct amdgpu_device *adev, struct dc_phy_addr_space_config *pa_config)
{
	uint64_t pt_base;
	uint32_t logical_addr_low;
	uint32_t logical_addr_high;
	uint32_t agp_base, agp_bot, agp_top;
	PHYSICAL_ADDRESS_LOC page_table_start, page_table_end, page_table_base;

	memset(pa_config, 0, sizeof(*pa_config));

	agp_base = 0;
	agp_bot = adev->gmc.agp_start >> 24;
	agp_top = adev->gmc.agp_end >> 24;

	/* AGP aperture is disabled */
	if (agp_bot == agp_top) {
		logical_addr_low  = adev->gmc.vram_start >> 18;
		if (adev->apu_flags & AMD_APU_IS_RAVEN2)
			/*
			 * Raven2 has a HW issue that it is unable to use the vram which
			 * is out of MC_VM_SYSTEM_APERTURE_HIGH_ADDR. So here is the
			 * workaround that increase system aperture high address (add 1)
			 * to get rid of the VM fault and hardware hang.
			 */
			logical_addr_high = (adev->gmc.fb_end >> 18) + 0x1;
		else
			logical_addr_high = adev->gmc.vram_end >> 18;
	} else {
		logical_addr_low  = min(adev->gmc.fb_start, adev->gmc.agp_start) >> 18;
		if (adev->apu_flags & AMD_APU_IS_RAVEN2)
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

	page_table_start.high_part = (u32)(adev->gmc.gart_start >> 44) & 0xF;
	page_table_start.low_part = (u32)(adev->gmc.gart_start >> 12);
	page_table_end.high_part = (u32)(adev->gmc.gart_end >> 44) & 0xF;
	page_table_end.low_part = (u32)(adev->gmc.gart_end >> 12);
	page_table_base.high_part = upper_32_bits(pt_base) & 0xF;
	page_table_base.low_part = lower_32_bits(pt_base);

	pa_config->system_aperture.start_addr = (uint64_t)logical_addr_low << 18;
	pa_config->system_aperture.end_addr = (uint64_t)logical_addr_high << 18;

	pa_config->system_aperture.agp_base = (uint64_t)agp_base << 24 ;
	pa_config->system_aperture.agp_bot = (uint64_t)agp_bot << 24;
	pa_config->system_aperture.agp_top = (uint64_t)agp_top << 24;

	pa_config->system_aperture.fb_base = adev->gmc.fb_start;
	pa_config->system_aperture.fb_offset = adev->gmc.aper_base;
	pa_config->system_aperture.fb_top = adev->gmc.fb_end;

	pa_config->gart_config.page_table_start_addr = page_table_start.quad_part << 12;
	pa_config->gart_config.page_table_end_addr = page_table_end.quad_part << 12;
	pa_config->gart_config.page_table_base_addr = page_table_base.quad_part;

	pa_config->is_hvm_enabled = adev->mode_info.gpu_vm_support;

}

static void dm_handle_hpd_rx_offload_work(struct work_struct *work)
{
	struct hpd_rx_irq_offload_work *offload_work;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *dc_link;
	struct amdgpu_device *adev;
	enum dc_connection_type new_connection_type = dc_connection_none;
	unsigned long flags;

	offload_work = container_of(work, struct hpd_rx_irq_offload_work, work);
	aconnector = offload_work->offload_wq->aconnector;

	if (!aconnector) {
		DRM_ERROR("Can't retrieve aconnector in hpd_rx_irq_offload_work");
		goto skip;
	}

	adev = drm_to_adev(aconnector->base.dev);
	dc_link = aconnector->dc_link;

	mutex_lock(&aconnector->hpd_lock);
	if (!dc_link_detect_sink(dc_link, &new_connection_type))
		DRM_ERROR("KMS: Failed to detect connector\n");
	mutex_unlock(&aconnector->hpd_lock);

	if (new_connection_type == dc_connection_none)
		goto skip;

	if (amdgpu_in_reset(adev))
		goto skip;

	mutex_lock(&adev->dm.dc_lock);
	if (offload_work->data.bytes.device_service_irq.bits.AUTOMATED_TEST)
		dc_link_dp_handle_automated_test(dc_link);
	else if ((dc_link->connector_signal != SIGNAL_TYPE_EDP) &&
			hpd_rx_irq_check_link_loss_status(dc_link, &offload_work->data) &&
			dc_link_dp_allow_hpd_rx_irq(dc_link)) {
		dc_link_dp_handle_link_loss(dc_link);
		spin_lock_irqsave(&offload_work->offload_wq->offload_lock, flags);
		offload_work->offload_wq->is_handling_link_loss = false;
		spin_unlock_irqrestore(&offload_work->offload_wq->offload_lock, flags);
	}
	mutex_unlock(&adev->dm.dc_lock);

skip:
	kfree(offload_work);

}

static struct hpd_rx_irq_offload_work_queue *hpd_rx_irq_create_workqueue(struct dc *dc)
{
	int max_caps = dc->caps.max_links;
	int i = 0;
	struct hpd_rx_irq_offload_work_queue *hpd_rx_offload_wq = NULL;

	hpd_rx_offload_wq = kcalloc(max_caps, sizeof(*hpd_rx_offload_wq), GFP_KERNEL);

	if (!hpd_rx_offload_wq)
		return NULL;


	for (i = 0; i < max_caps; i++) {
		hpd_rx_offload_wq[i].wq =
				    create_singlethread_workqueue("amdgpu_dm_hpd_rx_offload_wq");

		if (hpd_rx_offload_wq[i].wq == NULL) {
			DRM_ERROR("create amdgpu_dm_hpd_rx_offload_wq fail!");
			goto out_err;
		}

		spin_lock_init(&hpd_rx_offload_wq[i].offload_lock);
	}

	return hpd_rx_offload_wq;

out_err:
	for (i = 0; i < max_caps; i++) {
		if (hpd_rx_offload_wq[i].wq)
			destroy_workqueue(hpd_rx_offload_wq[i].wq);
	}
	kfree(hpd_rx_offload_wq);
	return NULL;
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

static const struct dmi_system_id hpd_disconnect_quirk_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision 3660"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision 3260"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision 3460"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex Tower Plus 7010"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex Tower 7010"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex SFF Plus 7010"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex SFF 7010"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex Micro Plus 7010"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex Micro 7010"),
		},
	},
	{}
	/* TODO: refactor this from a fixed table to a dynamic option */
};

static void retrieve_dmi_info(struct amdgpu_display_manager *dm)
{
	const struct dmi_system_id *dmi_id;

	dm->aux_hpd_discon_quirk = false;

	dmi_id = dmi_first_match(hpd_disconnect_quirk_table);
	if (dmi_id) {
		dm->aux_hpd_discon_quirk = true;
		DRM_INFO("aux_hpd_discon_quirk attached\n");
	}
}

static int amdgpu_dm_init(struct amdgpu_device *adev)
{
	struct dc_init_data init_data;
#ifdef CONFIG_DRM_AMD_DC_HDCP
	struct dc_callback_init init_params;
#endif
	int r;

	adev->dm.ddev = adev_to_drm(adev);
	adev->dm.adev = adev;

	/* Zero all the fields */
	memset(&init_data, 0, sizeof(init_data));
#ifdef CONFIG_DRM_AMD_DC_HDCP
	memset(&init_params, 0, sizeof(init_params));
#endif

	mutex_init(&adev->dm.dpia_aux_lock);
	mutex_init(&adev->dm.dc_lock);
	mutex_init(&adev->dm.audio_lock);
	spin_lock_init(&adev->dm.vblank_lock);

	if(amdgpu_dm_irq_init(adev)) {
		DRM_ERROR("amdgpu: failed to initialize DM IRQ support.\n");
		goto error;
	}

	init_data.asic_id.chip_family = adev->family;

	init_data.asic_id.pci_revision_id = adev->pdev->revision;
	init_data.asic_id.hw_internal_rev = adev->external_rev_id;
	init_data.asic_id.chip_id = adev->pdev->device;

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

	switch (adev->ip_versions[DCE_HWIP][0]) {
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

	switch (adev->asic_type) {
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		init_data.flags.gpu_vm_support = true;
		break;
	default:
		switch (adev->ip_versions[DCE_HWIP][0]) {
		case IP_VERSION(1, 0, 0):
		case IP_VERSION(1, 0, 1):
			/* enable S/G on PCO and RV2 */
			if ((adev->apu_flags & AMD_APU_IS_RAVEN2) ||
			    (adev->apu_flags & AMD_APU_IS_PICASSO))
				init_data.flags.gpu_vm_support = true;
			break;
		case IP_VERSION(2, 1, 0):
		case IP_VERSION(3, 0, 1):
		case IP_VERSION(3, 1, 2):
		case IP_VERSION(3, 1, 3):
		case IP_VERSION(3, 1, 6):
			init_data.flags.gpu_vm_support = true;
			break;
		default:
			break;
		}
		break;
	}

	if (init_data.flags.gpu_vm_support)
		adev->mode_info.gpu_vm_support = true;

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

	init_data.flags.seamless_boot_edp_requested = false;

	if (check_seamless_boot_capability(adev)) {
		init_data.flags.seamless_boot_edp_requested = true;
		init_data.flags.allow_seamless_boot_optimization = true;
		DRM_INFO("Seamless boot condition check passed\n");
	}

	init_data.flags.enable_mipi_converter_optimization = true;

	init_data.dcn_reg_offsets = adev->reg_offset[DCE_HWIP][0];
	init_data.nbio_reg_offsets = adev->reg_offset[NBIO_HWIP][0];

	INIT_LIST_HEAD(&adev->dm.da_list);

	retrieve_dmi_info(&adev->dm);

	/* Display Core create. */
	adev->dm.dc = dc_create(&init_data);

	if (adev->dm.dc) {
		DRM_INFO("Display Core initialized with v%s!\n", DC_VER);
	} else {
		DRM_INFO("Display Core failed to initialize with v%s!\n", DC_VER);
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

	if (amdgpu_dc_debug_mask & DC_DISABLE_DSC) {
		adev->dm.dc->debug.disable_dsc = true;
	}

	if (amdgpu_dc_debug_mask & DC_DISABLE_CLOCK_GATING)
		adev->dm.dc->debug.disable_clock_gate = true;

	if (amdgpu_dc_debug_mask & DC_FORCE_SUBVP_MCLK_SWITCH)
		adev->dm.dc->debug.force_subvp_mclk_switch = true;

	adev->dm.dc->debug.visual_confirm = amdgpu_dc_visual_confirm;

	/* TODO: Remove after DP2 receiver gets proper support of Cable ID feature */
	adev->dm.dc->debug.ignore_cable_id = true;

	r = dm_dmub_hw_init(adev);
	if (r) {
		DRM_ERROR("DMUB interface failed to initialize: status=%d\n", r);
		goto error;
	}

	dc_hardware_init(adev->dm.dc);

	adev->dm.hpd_rx_offload_wq = hpd_rx_irq_create_workqueue(adev->dm.dc);
	if (!adev->dm.hpd_rx_offload_wq) {
		DRM_ERROR("amdgpu: failed to create hpd rx offload workqueue.\n");
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
		DRM_ERROR(
		"amdgpu: failed to initialize freesync_module.\n");
	} else
		DRM_DEBUG_DRIVER("amdgpu: freesync_module init done %p.\n",
				adev->dm.freesync_module);

	amdgpu_dm_init_color_mod();

	if (adev->dm.dc->caps.max_links > 0) {
		adev->dm.vblank_control_workqueue =
			create_singlethread_workqueue("dm_vblank_control_workqueue");
		if (!adev->dm.vblank_control_workqueue)
			DRM_ERROR("amdgpu: failed to initialize vblank_workqueue.\n");
	}

#ifdef CONFIG_DRM_AMD_DC_HDCP
	if (adev->dm.dc->caps.max_links > 0 && adev->family >= AMDGPU_FAMILY_RV) {
		adev->dm.hdcp_workqueue = hdcp_create_workqueue(adev, &init_params.cp_psp, adev->dm.dc);

		if (!adev->dm.hdcp_workqueue)
			DRM_ERROR("amdgpu: failed to initialize hdcp_workqueue.\n");
		else
			DRM_DEBUG_DRIVER("amdgpu: hdcp_workqueue init done %p.\n", adev->dm.hdcp_workqueue);

		dc_init_callbacks(adev->dm.dc, &init_params);
	}
#endif
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	adev->dm.crc_rd_wrk = amdgpu_dm_crtc_secure_display_create_work();
#endif
	if (dc_is_dmub_outbox_supported(adev->dm.dc)) {
		init_completion(&adev->dm.dmub_aux_transfer_done);
		adev->dm.dmub_notify = kzalloc(sizeof(struct dmub_notification), GFP_KERNEL);
		if (!adev->dm.dmub_notify) {
			DRM_INFO("amdgpu: fail to allocate adev->dm.dmub_notify");
			goto error;
		}

		adev->dm.delayed_hpd_wq = create_singlethread_workqueue("amdgpu_dm_hpd_wq");
		if (!adev->dm.delayed_hpd_wq) {
			DRM_ERROR("amdgpu: failed to create hpd offload workqueue.\n");
			goto error;
		}

		amdgpu_dm_outbox_init(adev);
		if (!register_dmub_notify_callback(adev, DMUB_NOTIFICATION_AUX_REPLY,
			dmub_aux_setconfig_callback, false)) {
			DRM_ERROR("amdgpu: fail to register dmub aux callback");
			goto error;
		}
		if (!register_dmub_notify_callback(adev, DMUB_NOTIFICATION_HPD, dmub_hpd_callback, true)) {
			DRM_ERROR("amdgpu: fail to register dmub hpd callback");
			goto error;
		}
		if (!register_dmub_notify_callback(adev, DMUB_NOTIFICATION_HPD_IRQ, dmub_hpd_callback, true)) {
			DRM_ERROR("amdgpu: fail to register dmub hpd callback");
			goto error;
		}
	}

	/* Enable outbox notification only after IRQ handlers are registered and DMUB is alive.
	 * It is expected that DMUB will resend any pending notifications at this point, for
	 * example HPD from DPIA.
	 */
	if (dc_is_dmub_outbox_supported(adev->dm.dc))
		dc_enable_dmub_outbox(adev->dm.dc);

	if (amdgpu_dm_initialize_drm_device(adev)) {
		DRM_ERROR(
		"amdgpu: failed to initialize sw for display support.\n");
		goto error;
	}

	/* create fake encoders for MST */
	dm_dp_create_fake_mst_encoders(adev);

	/* TODO: Add_display_info? */

	/* TODO use dynamic cursor width */
	adev_to_drm(adev)->mode_config.cursor_width = adev->dm.dc->caps.max_cursor_size;
	adev_to_drm(adev)->mode_config.cursor_height = adev->dm.dc->caps.max_cursor_size;

	if (drm_vblank_init(adev_to_drm(adev), adev->dm.display_indexes_num)) {
		DRM_ERROR(
		"amdgpu: failed to initialize sw for display support.\n");
		goto error;
	}


	DRM_DEBUG_DRIVER("KMS initialized.\n");

	return 0;
error:
	amdgpu_dm_fini(adev);

	return -EINVAL;
}

static int amdgpu_dm_early_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

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

	amdgpu_dm_destroy_drm_device(&adev->dm);

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	if (adev->dm.crc_rd_wrk) {
		flush_work(&adev->dm.crc_rd_wrk->notify_ta_work);
		kfree(adev->dm.crc_rd_wrk);
		adev->dm.crc_rd_wrk = NULL;
	}
#endif
#ifdef CONFIG_DRM_AMD_DC_HDCP
	if (adev->dm.hdcp_workqueue) {
		hdcp_destroy(&adev->dev->kobj, adev->dm.hdcp_workqueue);
		adev->dm.hdcp_workqueue = NULL;
	}

	if (adev->dm.dc)
		dc_deinit_callbacks(adev->dm.dc);
#endif

	if (adev->dm.dc)
		dc_dmub_srv_destroy(&adev->dm.dc->ctx->dmub_srv);

	if (dc_enable_dmub_notifications(adev->dm.dc)) {
		kfree(adev->dm.dmub_notify);
		adev->dm.dmub_notify = NULL;
		destroy_workqueue(adev->dm.delayed_hpd_wq);
		adev->dm.delayed_hpd_wq = NULL;
	}

	if (adev->dm.dmub_bo)
		amdgpu_bo_free_kernel(&adev->dm.dmub_bo,
				      &adev->dm.dmub_bo_gpu_addr,
				      &adev->dm.dmub_bo_cpu_addr);

	if (adev->dm.hpd_rx_offload_wq) {
		for (i = 0; i < adev->dm.dc->caps.max_links; i++) {
			if (adev->dm.hpd_rx_offload_wq[i].wq) {
				destroy_workqueue(adev->dm.hpd_rx_offload_wq[i].wq);
				adev->dm.hpd_rx_offload_wq[i].wq = NULL;
			}
		}

		kfree(adev->dm.hpd_rx_offload_wq);
		adev->dm.hpd_rx_offload_wq = NULL;
	}

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

	mutex_destroy(&adev->dm.audio_lock);
	mutex_destroy(&adev->dm.dc_lock);
	mutex_destroy(&adev->dm.dpia_aux_lock);

	return;
}

static int load_dmcu_fw(struct amdgpu_device *adev)
{
	const char *fw_name_dmcu = NULL;
	int r;
	const struct dmcu_firmware_header_v1_0 *hdr;

	switch(adev->asic_type) {
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
		switch (adev->ip_versions[DCE_HWIP][0]) {
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
			return 0;
		default:
			break;
		}
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

static uint32_t amdgpu_dm_dmub_reg_read(void *ctx, uint32_t address)
{
	struct amdgpu_device *adev = ctx;

	return dm_read_reg(adev->dm.dc->ctx, address);
}

static void amdgpu_dm_dmub_reg_write(void *ctx, uint32_t address,
				     uint32_t value)
{
	struct amdgpu_device *adev = ctx;

	return dm_write_reg(adev->dm.dc->ctx, address, value);
}

static int dm_dmub_sw_init(struct amdgpu_device *adev)
{
	struct dmub_srv_create_params create_params;
	struct dmub_srv_region_params region_params;
	struct dmub_srv_region_info region_info;
	struct dmub_srv_fb_params fb_params;
	struct dmub_srv_fb_info *fb_info;
	struct dmub_srv *dmub_srv;
	const struct dmcub_firmware_header_v1_0 *hdr;
	const char *fw_name_dmub;
	enum dmub_asic dmub_asic;
	enum dmub_status status;
	int r;

	switch (adev->ip_versions[DCE_HWIP][0]) {
	case IP_VERSION(2, 1, 0):
		dmub_asic = DMUB_ASIC_DCN21;
		fw_name_dmub = FIRMWARE_RENOIR_DMUB;
		if (ASICREV_IS_GREEN_SARDINE(adev->external_rev_id))
			fw_name_dmub = FIRMWARE_GREEN_SARDINE_DMUB;
		break;
	case IP_VERSION(3, 0, 0):
		if (adev->ip_versions[GC_HWIP][0] == IP_VERSION(10, 3, 0)) {
			dmub_asic = DMUB_ASIC_DCN30;
			fw_name_dmub = FIRMWARE_SIENNA_CICHLID_DMUB;
		} else {
			dmub_asic = DMUB_ASIC_DCN30;
			fw_name_dmub = FIRMWARE_NAVY_FLOUNDER_DMUB;
		}
		break;
	case IP_VERSION(3, 0, 1):
		dmub_asic = DMUB_ASIC_DCN301;
		fw_name_dmub = FIRMWARE_VANGOGH_DMUB;
		break;
	case IP_VERSION(3, 0, 2):
		dmub_asic = DMUB_ASIC_DCN302;
		fw_name_dmub = FIRMWARE_DIMGREY_CAVEFISH_DMUB;
		break;
	case IP_VERSION(3, 0, 3):
		dmub_asic = DMUB_ASIC_DCN303;
		fw_name_dmub = FIRMWARE_BEIGE_GOBY_DMUB;
		break;
	case IP_VERSION(3, 1, 2):
	case IP_VERSION(3, 1, 3):
		dmub_asic = (adev->external_rev_id == YELLOW_CARP_B0) ? DMUB_ASIC_DCN31B : DMUB_ASIC_DCN31;
		fw_name_dmub = FIRMWARE_YELLOW_CARP_DMUB;
		break;
	case IP_VERSION(3, 1, 4):
		dmub_asic = DMUB_ASIC_DCN314;
		fw_name_dmub = FIRMWARE_DCN_314_DMUB;
		break;
	case IP_VERSION(3, 1, 5):
		dmub_asic = DMUB_ASIC_DCN315;
		fw_name_dmub = FIRMWARE_DCN_315_DMUB;
		break;
	case IP_VERSION(3, 1, 6):
		dmub_asic = DMUB_ASIC_DCN316;
		fw_name_dmub = FIRMWARE_DCN316_DMUB;
		break;
	case IP_VERSION(3, 2, 0):
		dmub_asic = DMUB_ASIC_DCN32;
		fw_name_dmub = FIRMWARE_DCN_V3_2_0_DMCUB;
		break;
	case IP_VERSION(3, 2, 1):
		dmub_asic = DMUB_ASIC_DCN321;
		fw_name_dmub = FIRMWARE_DCN_V3_2_1_DMCUB;
		break;
	default:
		/* ASIC doesn't support DMUB. */
		return 0;
	}

	r = request_firmware_direct(&adev->dm.dmub_fw, fw_name_dmub, adev->dev);
	if (r) {
		DRM_ERROR("DMUB firmware loading failed: %d\n", r);
		return 0;
	}

	r = amdgpu_ucode_validate(adev->dm.dmub_fw);
	if (r) {
		DRM_ERROR("Couldn't validate DMUB firmware: %d\n", r);
		return 0;
	}

	hdr = (const struct dmcub_firmware_header_v1_0 *)adev->dm.dmub_fw->data;
	adev->dm.dmcub_fw_version = le32_to_cpu(hdr->header.ucode_version);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		adev->firmware.ucode[AMDGPU_UCODE_ID_DMCUB].ucode_id =
			AMDGPU_UCODE_ID_DMCUB;
		adev->firmware.ucode[AMDGPU_UCODE_ID_DMCUB].fw =
			adev->dm.dmub_fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(hdr->inst_const_bytes), PAGE_SIZE);

		DRM_INFO("Loading DMUB firmware via PSP: version=0x%08X\n",
			 adev->dm.dmcub_fw_version);
	}


	adev->dm.dmub_srv = kzalloc(sizeof(*adev->dm.dmub_srv), GFP_KERNEL);
	dmub_srv = adev->dm.dmub_srv;

	if (!dmub_srv) {
		DRM_ERROR("Failed to allocate DMUB service!\n");
		return -ENOMEM;
	}

	memset(&create_params, 0, sizeof(create_params));
	create_params.user_ctx = adev;
	create_params.funcs.reg_read = amdgpu_dm_dmub_reg_read;
	create_params.funcs.reg_write = amdgpu_dm_dmub_reg_write;
	create_params.asic = dmub_asic;

	/* Create the DMUB service. */
	status = dmub_srv_create(dmub_srv, &create_params);
	if (status != DMUB_STATUS_OK) {
		DRM_ERROR("Error creating DMUB service: %d\n", status);
		return -EINVAL;
	}

	/* Calculate the size of all the regions for the DMUB service. */
	memset(&region_params, 0, sizeof(region_params));

	region_params.inst_const_size = le32_to_cpu(hdr->inst_const_bytes) -
					PSP_HEADER_BYTES - PSP_FOOTER_BYTES;
	region_params.bss_data_size = le32_to_cpu(hdr->bss_data_bytes);
	region_params.vbios_size = adev->bios_size;
	region_params.fw_bss_data = region_params.bss_data_size ?
		adev->dm.dmub_fw->data +
		le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
		le32_to_cpu(hdr->inst_const_bytes) : NULL;
	region_params.fw_inst_const =
		adev->dm.dmub_fw->data +
		le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
		PSP_HEADER_BYTES;

	status = dmub_srv_calc_region_info(dmub_srv, &region_params,
					   &region_info);

	if (status != DMUB_STATUS_OK) {
		DRM_ERROR("Error calculating DMUB region info: %d\n", status);
		return -EINVAL;
	}

	/*
	 * Allocate a framebuffer based on the total size of all the regions.
	 * TODO: Move this into GART.
	 */
	r = amdgpu_bo_create_kernel(adev, region_info.fb_size, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_VRAM, &adev->dm.dmub_bo,
				    &adev->dm.dmub_bo_gpu_addr,
				    &adev->dm.dmub_bo_cpu_addr);
	if (r)
		return r;

	/* Rebase the regions on the framebuffer address. */
	memset(&fb_params, 0, sizeof(fb_params));
	fb_params.cpu_addr = adev->dm.dmub_bo_cpu_addr;
	fb_params.gpu_addr = adev->dm.dmub_bo_gpu_addr;
	fb_params.region_info = &region_info;

	adev->dm.dmub_fb_info =
		kzalloc(sizeof(*adev->dm.dmub_fb_info), GFP_KERNEL);
	fb_info = adev->dm.dmub_fb_info;

	if (!fb_info) {
		DRM_ERROR(
			"Failed to allocate framebuffer info for DMUB service!\n");
		return -ENOMEM;
	}

	status = dmub_srv_calc_fb_info(dmub_srv, &fb_params, fb_info);
	if (status != DMUB_STATUS_OK) {
		DRM_ERROR("Error calculating DMUB FB info: %d\n", status);
		return -EINVAL;
	}

	return 0;
}

static int dm_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = dm_dmub_sw_init(adev);
	if (r)
		return r;

	return load_dmcu_fw(adev);
}

static int dm_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	kfree(adev->dm.dmub_fb_info);
	adev->dm.dmub_fb_info = NULL;

	if (adev->dm.dmub_srv) {
		dmub_srv_destroy(adev->dm.dmub_srv);
		adev->dm.dmub_srv = NULL;
	}

	release_firmware(adev->dm.dmub_fw);
	adev->dm.dmub_fw = NULL;

	release_firmware(adev->dm.fw_dmcu);
	adev->dm.fw_dmcu = NULL;

	return 0;
}

static int detect_mst_link_for_all_connectors(struct drm_device *dev)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	int ret = 0;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->dc_link->type == dc_connection_mst_branch &&
		    aconnector->mst_mgr.aux) {
			DRM_DEBUG_DRIVER("DM_MST: starting TM on aconnector: %p [id: %d]\n",
					 aconnector,
					 aconnector->base.base.id);

			ret = drm_dp_mst_topology_mgr_set_mst(&aconnector->mst_mgr, true);
			if (ret < 0) {
				DRM_ERROR("DM_MST: Failed to start MST\n");
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

static int dm_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	struct dmcu_iram_parameters params;
	unsigned int linear_lut[16];
	int i;
	struct dmcu *dmcu = NULL;

	dmcu = adev->dm.dc->res_pool->dmcu;

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

		get_edp_links(adev->dm.dc, edp_links, &edp_num);
		for (i = 0; i < edp_num; i++) {
			if (!dmub_init_abm_config(adev->dm.dc->res_pool, params, i))
				return -EINVAL;
		}
	}

	return detect_mst_link_for_all_connectors(adev_to_drm(adev));
}

static void s3_handle_mst(struct drm_device *dev, bool suspend)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	struct drm_dp_mst_topology_mgr *mgr;
	int ret;
	bool need_hotplug = false;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->dc_link->type != dc_connection_mst_branch ||
		    aconnector->mst_port)
			continue;

		mgr = &aconnector->mst_mgr;

		if (suspend) {
			drm_dp_mst_topology_mgr_suspend(mgr);
		} else {
			/* if extended timeout is supported in hardware,
			 * default to LTTPR timeout (3.2ms) first as a W/A for DP link layer
			 * CTS 4.2.1.1 regression introduced by CTS specs requirement update.
			 */
			dc_link_aux_try_to_configure_timeout(aconnector->dc_link->ddc, LINK_AUX_DEFAULT_LTTPR_TIMEOUT_PERIOD);
			if (!dp_is_lttpr_present(aconnector->dc_link))
				dc_link_aux_try_to_configure_timeout(aconnector->dc_link->ddc, LINK_AUX_DEFAULT_TIMEOUT_PERIOD);

			ret = drm_dp_mst_topology_mgr_resume(mgr, true);
			if (ret < 0) {
				dm_helpers_dp_mst_stop_top_mgr(aconnector->dc_link->ctx,
					aconnector->dc_link);
				need_hotplug = true;
			}
		}
	}
	drm_connector_list_iter_end(&iter);

	if (need_hotplug)
		drm_kms_helper_hotplug_event(dev);
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
	switch (adev->ip_versions[DCE_HWIP][0]) {
	case IP_VERSION(2, 0, 2):
	case IP_VERSION(2, 0, 0):
		break;
	default:
		return 0;
	}

	ret = amdgpu_dpm_write_watermarks_table(adev);
	if (ret) {
		DRM_ERROR("Failed to update WMTABLE!\n");
		return ret;
	}

	return 0;
}

/**
 * dm_hw_init() - Initialize DC device
 * @handle: The base driver device containing the amdgpu_dm device.
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
 * @handle: The base driver device containing the amdgpu_dm device.
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


static void dm_gpureset_toggle_interrupts(struct amdgpu_device *adev,
				 struct dc_state *state, bool enable)
{
	enum dc_irq_source irq_source;
	struct amdgpu_crtc *acrtc;
	int rc = -EBUSY;
	int i = 0;

	for (i = 0; i < state->stream_count; i++) {
		acrtc = get_crtc_by_otg_inst(
				adev, state->stream_status[i].primary_otg_inst);

		if (acrtc && state->stream_status[i].plane_count != 0) {
			irq_source = IRQ_TYPE_PFLIP + acrtc->otg_inst;
			rc = dc_interrupt_set(adev->dm.dc, irq_source, enable) ? 0 : -EBUSY;
			DRM_DEBUG_VBL("crtc %d - vupdate irq %sabling: r=%d\n",
				      acrtc->crtc_id, enable ? "en" : "dis", rc);
			if (rc)
				DRM_WARN("Failed to %s pflip interrupts\n",
					 enable ? "enable" : "disable");

			if (enable) {
				rc = dm_enable_vblank(&acrtc->base);
				if (rc)
					DRM_WARN("Failed to enable vblank interrupts\n");
			} else {
				dm_disable_vblank(&acrtc->base);
			}

		}
	}

}

static enum dc_status amdgpu_dm_commit_zero_streams(struct dc *dc)
{
	struct dc_state *context = NULL;
	enum dc_status res = DC_ERROR_UNEXPECTED;
	int i;
	struct dc_stream_state *del_streams[MAX_PIPES];
	int del_streams_count = 0;

	memset(del_streams, 0, sizeof(del_streams));

	context = dc_create_state(dc);
	if (context == NULL)
		goto context_alloc_fail;

	dc_resource_state_copy_construct_current(dc, context);

	/* First remove from context all streams */
	for (i = 0; i < context->stream_count; i++) {
		struct dc_stream_state *stream = context->streams[i];

		del_streams[del_streams_count++] = stream;
	}

	/* Remove all planes for removed streams and then remove the streams */
	for (i = 0; i < del_streams_count; i++) {
		if (!dc_rem_all_planes_for_stream(dc, del_streams[i], context)) {
			res = DC_FAIL_DETACH_SURFACES;
			goto fail;
		}

		res = dc_remove_stream_from_ctx(dc, context, del_streams[i]);
		if (res != DC_OK)
			goto fail;
	}

	res = dc_commit_state(dc, context);

fail:
	dc_release_state(context);

context_alloc_fail:
	return res;
}

static void hpd_rx_irq_work_suspend(struct amdgpu_display_manager *dm)
{
	int i;

	if (dm->hpd_rx_offload_wq) {
		for (i = 0; i < dm->dc->caps.max_links; i++)
			flush_workqueue(dm->hpd_rx_offload_wq[i].wq);
	}
}

static int dm_suspend(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct amdgpu_display_manager *dm = &adev->dm;
	int ret = 0;

	if (amdgpu_in_reset(adev)) {
		mutex_lock(&dm->dc_lock);

		dc_allow_idle_optimizations(adev->dm.dc, false);

		dm->cached_dc_state = dc_copy_state(dm->dc->current_state);

		dm_gpureset_toggle_interrupts(adev, dm->cached_dc_state, false);

		amdgpu_dm_commit_zero_streams(dm->dc);

		amdgpu_dm_irq_suspend(adev);

		hpd_rx_irq_work_suspend(dm);

		return ret;
	}

	WARN_ON(adev->dm.cached_state);
	adev->dm.cached_state = drm_atomic_helper_suspend(adev_to_drm(adev));

	s3_handle_mst(adev_to_drm(adev), true);

	amdgpu_dm_irq_suspend(adev);

	hpd_rx_irq_work_suspend(dm);

	dc_set_power_state(dm->dc, DC_ACPI_CM_POWER_STATE_D3);

	return 0;
}

struct amdgpu_dm_connector *
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

static void dm_gpureset_commit_state(struct dc_state *dc_state,
				     struct amdgpu_display_manager *dm)
{
	struct {
		struct dc_surface_update surface_updates[MAX_SURFACES];
		struct dc_plane_info plane_infos[MAX_SURFACES];
		struct dc_scaling_info scaling_infos[MAX_SURFACES];
		struct dc_flip_addrs flip_addrs[MAX_SURFACES];
		struct dc_stream_update stream_update;
	} * bundle;
	int k, m;

	bundle = kzalloc(sizeof(*bundle), GFP_KERNEL);

	if (!bundle) {
		dm_error("Failed to allocate update bundle\n");
		goto cleanup;
	}

	for (k = 0; k < dc_state->stream_count; k++) {
		bundle->stream_update.stream = dc_state->streams[k];

		for (m = 0; m < dc_state->stream_status->plane_count; m++) {
			bundle->surface_updates[m].surface =
				dc_state->stream_status->plane_states[m];
			bundle->surface_updates[m].surface->force_full_update =
				true;
		}

		update_planes_and_stream_adapter(dm->dc,
					 UPDATE_TYPE_FULL,
					 dc_state->stream_status->plane_count,
					 dc_state->streams[k],
					 &bundle->stream_update,
					 bundle->surface_updates);
	}

cleanup:
	kfree(bundle);

	return;
}

static int dm_resume(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct drm_device *ddev = adev_to_drm(adev);
	struct amdgpu_display_manager *dm = &adev->dm;
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct dm_crtc_state *dm_new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state;
	struct dm_plane_state *dm_new_plane_state;
	struct dm_atomic_state *dm_state = to_dm_atomic_state(dm->atomic_obj.state);
	enum dc_connection_type new_connection_type = dc_connection_none;
	struct dc_state *dc_state;
	int i, r, j;

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
		if (r)
			DRM_ERROR("DMUB interface failed to initialize: status=%d\n", r);

		dc_set_power_state(dm->dc, DC_ACPI_CM_POWER_STATE_D0);
		dc_resume(dm->dc);

		amdgpu_dm_irq_resume_early(adev);

		for (i = 0; i < dc_state->stream_count; i++) {
			dc_state->streams[i]->mode_changed = true;
			for (j = 0; j < dc_state->stream_status[i].plane_count; j++) {
				dc_state->stream_status[i].plane_states[j]->update_flags.raw
					= 0xffffffff;
			}
		}

		if (dc_is_dmub_outbox_supported(adev->dm.dc)) {
			amdgpu_dm_outbox_init(adev);
			dc_enable_dmub_outbox(adev->dm.dc);
		}

		WARN_ON(!dc_commit_state(dm->dc, dc_state));

		dm_gpureset_commit_state(dm->cached_dc_state, dm);

		dm_gpureset_toggle_interrupts(adev, dm->cached_dc_state, true);

		dc_release_state(dm->cached_dc_state);
		dm->cached_dc_state = NULL;

		amdgpu_dm_irq_resume_late(adev);

		mutex_unlock(&dm->dc_lock);

		return 0;
	}
	/* Recreate dc_state - DC invalidates it when setting power state to S3. */
	dc_release_state(dm_state->context);
	dm_state->context = dc_create_state(dm->dc);
	/* TODO: Remove dc_state->dccg, use dc->dccg directly. */
	dc_resource_state_construct(dm->dc, dm_state->context);

	/* Before powering on DC we need to re-initialize DMUB. */
	dm_dmub_hw_resume(adev);

	/* Re-enable outbox interrupts for DPIA. */
	if (dc_is_dmub_outbox_supported(adev->dm.dc)) {
		amdgpu_dm_outbox_init(adev);
		dc_enable_dmub_outbox(adev->dm.dc);
	}

	/* power on hardware */
	dc_set_power_state(dm->dc, DC_ACPI_CM_POWER_STATE_D0);

	/* program HPD filter */
	dc_resume(dm->dc);

	/*
	 * early enable HPD Rx IRQ, should be done before set mode as short
	 * pulse interrupts are used for MST
	 */
	amdgpu_dm_irq_resume_early(adev);

	/* On resume we need to rewrite the MSTM control bits to enable MST*/
	s3_handle_mst(ddev, false);

	/* Do detection*/
	drm_connector_list_iter_begin(ddev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		aconnector = to_amdgpu_dm_connector(connector);

		if (!aconnector->dc_link)
			continue;

		/*
		 * this is the case when traversing through already created
		 * MST connectors, should be skipped
		 */
		if (aconnector && aconnector->mst_port)
			continue;

		mutex_lock(&aconnector->hpd_lock);
		if (!dc_link_detect_sink(aconnector->dc_link, &new_connection_type))
			DRM_ERROR("KMS: Failed to detect connector\n");

		if (aconnector->base.force && new_connection_type == dc_connection_none) {
			emulated_link_detect(aconnector->dc_link);
		} else {
			mutex_lock(&dm->dc_lock);
			dc_link_detect(aconnector->dc_link, DETECT_REASON_HPD);
			mutex_unlock(&dm->dc_lock);
		}

		if (aconnector->fake_enable && aconnector->dc_link->local_sink)
			aconnector->fake_enable = false;

		if (aconnector->dc_sink)
			dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
		amdgpu_dm_update_connector_after_detect(aconnector);
		mutex_unlock(&aconnector->hpd_lock);
	}
	drm_connector_list_iter_end(&iter);

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

	amdgpu_dm_smu_write_watermarks_table(adev);

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
	.get_format_info = amd_get_format_info,
	.atomic_check = amdgpu_dm_atomic_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static struct drm_mode_config_helper_funcs amdgpu_dm_mode_config_helperfuncs = {
	.atomic_commit_tail = amdgpu_dm_atomic_commit_tail,
	.atomic_commit_setup = drm_dp_mst_atomic_setup_commit,
};

static void update_connector_ext_caps(struct amdgpu_dm_connector *aconnector)
{
	struct amdgpu_dm_backlight_caps *caps;
	struct amdgpu_display_manager *dm;
	struct drm_connector *conn_base;
	struct amdgpu_device *adev;
	struct dc_link *link = NULL;
	struct drm_luminance_range_info *luminance_range;
	int i;

	if (!aconnector || !aconnector->dc_link)
		return;

	link = aconnector->dc_link;
	if (link->connector_signal != SIGNAL_TYPE_EDP)
		return;

	conn_base = &aconnector->base;
	adev = drm_to_adev(conn_base->dev);
	dm = &adev->dm;
	for (i = 0; i < dm->num_of_edps; i++) {
		if (link == dm->backlight_link[i])
			break;
	}
	if (i >= dm->num_of_edps)
		return;
	caps = &dm->backlight_caps[i];
	caps->ext_caps = &aconnector->dc_link->dpcd_sink_ext_caps;
	caps->aux_support = false;

	if (caps->ext_caps->bits.oled == 1 /*||
	    caps->ext_caps->bits.sdr_aux_backlight_control == 1 ||
	    caps->ext_caps->bits.hdr_aux_backlight_control == 1*/)
		caps->aux_support = true;

	if (amdgpu_backlight == 0)
		caps->aux_support = false;
	else if (amdgpu_backlight == 1)
		caps->aux_support = true;

	luminance_range = &conn_base->display_info.luminance_range;
	caps->aux_min_input_signal = luminance_range->min_luminance;
	caps->aux_max_input_signal = luminance_range->max_luminance;
}

void amdgpu_dm_update_connector_after_detect(
		struct amdgpu_dm_connector *aconnector)
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
		if (aconnector->dc_sink) {
			amdgpu_dm_update_freesync_caps(connector, NULL);
			dc_sink_release(aconnector->dc_sink);
		}

		aconnector->dc_sink = sink;
		dc_sink_retain(aconnector->dc_sink);
		if (sink->dc_edid.length == 0) {
			aconnector->edid = NULL;
			if (aconnector->dc_link->aux_mode) {
				drm_dp_cec_unset_edid(
					&aconnector->dm_dp_aux.aux);
			}
		} else {
			aconnector->edid =
				(struct edid *)sink->dc_edid.raw_edid;

			if (aconnector->dc_link->aux_mode)
				drm_dp_cec_set_edid(&aconnector->dm_dp_aux.aux,
						    aconnector->edid);
		}

		drm_connector_update_edid_property(connector, aconnector->edid);
		amdgpu_dm_update_freesync_caps(connector, aconnector->edid);
		update_connector_ext_caps(aconnector);
	} else {
		drm_dp_cec_unset_edid(&aconnector->dm_dp_aux.aux);
		amdgpu_dm_update_freesync_caps(connector, NULL);
		drm_connector_update_edid_property(connector, NULL);
		aconnector->num_modes = 0;
		dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
		aconnector->edid = NULL;
#ifdef CONFIG_DRM_AMD_DC_HDCP
		/* Set CP to DESIRED if it was ENABLED, so we can re-enable it again on hotplug */
		if (connector->state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED)
			connector->state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
#endif
	}

	mutex_unlock(&dev->mode_config.mutex);

	update_subconnector_property(aconnector);

	if (sink)
		dc_sink_release(sink);
}

static void handle_hpd_irq_helper(struct amdgpu_dm_connector *aconnector)
{
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	enum dc_connection_type new_connection_type = dc_connection_none;
	struct amdgpu_device *adev = drm_to_adev(dev);
#ifdef CONFIG_DRM_AMD_DC_HDCP
	struct dm_connector_state *dm_con_state = to_dm_connector_state(connector->state);
#endif
	bool ret = false;

	if (adev->dm.disable_hpd_irq)
		return;

	/*
	 * In case of failure or MST no need to update connector status or notify the OS
	 * since (for MST case) MST does this in its own context.
	 */
	mutex_lock(&aconnector->hpd_lock);

#ifdef CONFIG_DRM_AMD_DC_HDCP
	if (adev->dm.hdcp_workqueue) {
		hdcp_reset_display(adev->dm.hdcp_workqueue, aconnector->dc_link->link_index);
		dm_con_state->update_hdcp = true;
	}
#endif
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
			drm_kms_helper_connector_hotplug_event(connector);
	} else {
		mutex_lock(&adev->dm.dc_lock);
		ret = dc_link_detect(aconnector->dc_link, DETECT_REASON_HPD);
		mutex_unlock(&adev->dm.dc_lock);
		if (ret) {
			amdgpu_dm_update_connector_after_detect(aconnector);

			drm_modeset_lock_all(dev);
			dm_restore_drm_connector_state(dev, connector);
			drm_modeset_unlock_all(dev);

			if (aconnector->base.force == DRM_FORCE_UNSPECIFIED)
				drm_kms_helper_connector_hotplug_event(connector);
		}
	}
	mutex_unlock(&aconnector->hpd_lock);

}

static void handle_hpd_irq(void *param)
{
	struct amdgpu_dm_connector *aconnector = (struct amdgpu_dm_connector *)param;

	handle_hpd_irq_helper(aconnector);

}

static void dm_handle_mst_sideband_msg(struct amdgpu_dm_connector *aconnector)
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

static void schedule_hpd_rx_offload_work(struct hpd_rx_irq_offload_work_queue *offload_wq,
							union hpd_irq_data hpd_irq_data)
{
	struct hpd_rx_irq_offload_work *offload_work =
				kzalloc(sizeof(*offload_work), GFP_KERNEL);

	if (!offload_work) {
		DRM_ERROR("Failed to allocate hpd_rx_irq_offload_work.\n");
		return;
	}

	INIT_WORK(&offload_work->work, dm_handle_hpd_rx_offload_work);
	offload_work->data = hpd_irq_data;
	offload_work->offload_wq = offload_wq;

	queue_work(offload_wq->wq, &offload_work->work);
	DRM_DEBUG_KMS("queue work to handle hpd_rx offload work");
}

static void handle_hpd_rx_irq(void *param)
{
	struct amdgpu_dm_connector *aconnector = (struct amdgpu_dm_connector *)param;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct dc_link *dc_link = aconnector->dc_link;
	bool is_mst_root_connector = aconnector->mst_mgr.mst_state;
	bool result = false;
	enum dc_connection_type new_connection_type = dc_connection_none;
	struct amdgpu_device *adev = drm_to_adev(dev);
	union hpd_irq_data hpd_irq_data;
	bool link_loss = false;
	bool has_left_work = false;
	int idx = aconnector->base.index;
	struct hpd_rx_irq_offload_work_queue *offload_wq = &adev->dm.hpd_rx_offload_wq[idx];

	memset(&hpd_irq_data, 0, sizeof(hpd_irq_data));

	if (adev->dm.disable_hpd_irq)
		return;

	/*
	 * TODO:Temporary add mutex to protect hpd interrupt not have a gpio
	 * conflict, after implement i2c helper, this mutex should be
	 * retired.
	 */
	mutex_lock(&aconnector->hpd_lock);

	result = dc_link_handle_hpd_rx_irq(dc_link, &hpd_irq_data,
						&link_loss, true, &has_left_work);

	if (!has_left_work)
		goto out;

	if (hpd_irq_data.bytes.device_service_irq.bits.AUTOMATED_TEST) {
		schedule_hpd_rx_offload_work(offload_wq, hpd_irq_data);
		goto out;
	}

	if (dc_link_dp_allow_hpd_rx_irq(dc_link)) {
		if (hpd_irq_data.bytes.device_service_irq.bits.UP_REQ_MSG_RDY ||
			hpd_irq_data.bytes.device_service_irq.bits.DOWN_REP_MSG_RDY) {
			dm_handle_mst_sideband_msg(aconnector);
			goto out;
		}

		if (link_loss) {
			bool skip = false;

			spin_lock(&offload_wq->offload_lock);
			skip = offload_wq->is_handling_link_loss;

			if (!skip)
				offload_wq->is_handling_link_loss = true;

			spin_unlock(&offload_wq->offload_lock);

			if (!skip)
				schedule_hpd_rx_offload_work(offload_wq, hpd_irq_data);

			goto out;
		}
	}

out:
	if (result && !is_mst_root_connector) {
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

			drm_kms_helper_connector_hotplug_event(connector);
		} else {
			bool ret = false;

			mutex_lock(&adev->dm.dc_lock);
			ret = dc_link_detect(dc_link, DETECT_REASON_HPDRX);
			mutex_unlock(&adev->dm.dc_lock);

			if (ret) {
				if (aconnector->fake_enable)
					aconnector->fake_enable = false;

				amdgpu_dm_update_connector_after_detect(aconnector);

				drm_modeset_lock_all(dev);
				dm_restore_drm_connector_state(dev, connector);
				drm_modeset_unlock_all(dev);

				drm_kms_helper_connector_hotplug_event(connector);
			}
		}
	}
#ifdef CONFIG_DRM_AMD_DC_HDCP
	if (hpd_irq_data.bytes.device_service_irq.bits.CP_IRQ) {
		if (adev->dm.hdcp_workqueue)
			hdcp_handle_cpirq(adev->dm.hdcp_workqueue,  aconnector->base.index);
	}
#endif

	if (dc_link->type != dc_connection_mst_branch)
		drm_dp_cec_irq(&aconnector->dm_dp_aux.aux);

	mutex_unlock(&aconnector->hpd_lock);
}

static void register_hpd_handlers(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev_to_drm(adev);
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

			if (adev->dm.hpd_rx_offload_wq)
				adev->dm.hpd_rx_offload_wq[connector->index].aconnector =
					aconnector;
		}
	}
}

#if defined(CONFIG_DRM_AMD_DC_SI)
/* Register IRQ sources and initialize IRQ callbacks */
static int dce60_register_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r;
	int i;
	unsigned client_id = AMDGPU_IRQ_CLIENTID_LEGACY;

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
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		r = amdgpu_irq_add_id(adev, client_id, i+1 , &adev->crtc_irq);
		if (r) {
			DRM_ERROR("Failed to add crtc irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i+1 , 0);

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
#endif

/* Register IRQ sources and initialize IRQ callbacks */
static int dce110_register_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r;
	int i;
	unsigned client_id = AMDGPU_IRQ_CLIENTID_LEGACY;

	if (adev->family >= AMDGPU_FAMILY_AI)
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

/* Register IRQ sources and initialize IRQ callbacks */
static int dcn10_register_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r;
	int i;
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	static const unsigned int vrtl_int_srcid[] = {
		DCN_1_0__SRCID__OTG1_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG2_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG3_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG4_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG5_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG6_VERTICAL_INTERRUPT0_CONTROL
	};
#endif

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

		amdgpu_dm_irq_register_interrupt(
			adev, &int_params, dm_crtc_high_irq, c_irq_params);
	}

	/* Use otg vertical line interrupt */
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	for (i = 0; i <= adev->mode_info.num_crtc - 1; i++) {
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE,
				vrtl_int_srcid[i], &adev->vline0_irq);

		if (r) {
			DRM_ERROR("Failed to add vline0 irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, vrtl_int_srcid[i], 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID) {
			DRM_ERROR("Failed to register vline0 irq %d!\n", vrtl_int_srcid[i]);
			break;
		}

		c_irq_params = &adev->dm.vline0_params[int_params.irq_source
					- DC_IRQ_SOURCE_DC1_VLINE0];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_dcn_vertical_interrupt0_high_irq, c_irq_params);
	}
#endif

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
			i <= DCN_1_0__SRCID__HUBP0_FLIP_INTERRUPT + dc->caps.max_otg_num - 1;
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
/* Register Outbox IRQ sources and initialize IRQ callbacks */
static int register_outbox_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r, i;

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, DCN_1_0__SRCID__DMCUB_OUTBOX_LOW_PRIORITY_READY_INT,
			&adev->dmub_outbox_irq);
	if (r) {
		DRM_ERROR("Failed to add outbox irq id!\n");
		return r;
	}

	if (dc->ctx->dmub_srv) {
		i = DCN_1_0__SRCID__DMCUB_OUTBOX_LOW_PRIORITY_READY_INT;
		int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
		int_params.irq_source =
		dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.dmub_outbox_params[0];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_dmub_outbox1_low_irq, c_irq_params);
	}

	return 0;
}

/*
 * Acquires the lock for the atomic state object and returns
 * the new atomic state.
 *
 * This should only be called during atomic check.
 */
int dm_atomic_get_state(struct drm_atomic_state *state,
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
dm_atomic_get_new_state(struct drm_atomic_state *state)
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

	adev_to_drm(adev)->mode_config.fb_base = adev->gmc.aper_base;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->context = dc_create_state(adev->dm.dc);
	if (!state->context) {
		kfree(state);
		return -ENOMEM;
	}

	dc_resource_state_copy_construct_current(adev->dm.dc, state->context);

	drm_atomic_private_obj_init(adev_to_drm(adev),
				    &adev->dm.atomic_obj,
				    &state->base,
				    &dm_atomic_state_funcs);

	r = amdgpu_display_modeset_create_props(adev);
	if (r) {
		dc_release_state(state->context);
		kfree(state);
		return r;
	}

	r = amdgpu_dm_audio_init(adev);
	if (r) {
		dc_release_state(state->context);
		kfree(state);
		return r;
	}

	return 0;
}

#define AMDGPU_DM_DEFAULT_MIN_BACKLIGHT 12
#define AMDGPU_DM_DEFAULT_MAX_BACKLIGHT 255
#define AUX_BL_DEFAULT_TRANSITION_TIME_MS 50

static void amdgpu_dm_update_backlight_caps(struct amdgpu_display_manager *dm,
					    int bl_idx)
{
#if defined(CONFIG_ACPI)
	struct amdgpu_dm_backlight_caps caps;

	memset(&caps, 0, sizeof(caps));

	if (dm->backlight_caps[bl_idx].caps_valid)
		return;

	amdgpu_acpi_get_backlight_caps(&caps);
	if (caps.caps_valid) {
		dm->backlight_caps[bl_idx].caps_valid = true;
		if (caps.aux_support)
			return;
		dm->backlight_caps[bl_idx].min_input_signal = caps.min_input_signal;
		dm->backlight_caps[bl_idx].max_input_signal = caps.max_input_signal;
	} else {
		dm->backlight_caps[bl_idx].min_input_signal =
				AMDGPU_DM_DEFAULT_MIN_BACKLIGHT;
		dm->backlight_caps[bl_idx].max_input_signal =
				AMDGPU_DM_DEFAULT_MAX_BACKLIGHT;
	}
#else
	if (dm->backlight_caps[bl_idx].aux_support)
		return;

	dm->backlight_caps[bl_idx].min_input_signal = AMDGPU_DM_DEFAULT_MIN_BACKLIGHT;
	dm->backlight_caps[bl_idx].max_input_signal = AMDGPU_DM_DEFAULT_MAX_BACKLIGHT;
#endif
}

static int get_brightness_range(const struct amdgpu_dm_backlight_caps *caps,
				unsigned *min, unsigned *max)
{
	if (!caps)
		return 0;

	if (caps->aux_support) {
		// Firmware limits are in nits, DC API wants millinits.
		*max = 1000 * caps->aux_max_input_signal;
		*min = 1000 * caps->aux_min_input_signal;
	} else {
		// Firmware limits are 8-bit, PWM control is 16-bit.
		*max = 0x101 * caps->max_input_signal;
		*min = 0x101 * caps->min_input_signal;
	}
	return 1;
}

static u32 convert_brightness_from_user(const struct amdgpu_dm_backlight_caps *caps,
					uint32_t brightness)
{
	unsigned min, max;

	if (!get_brightness_range(caps, &min, &max))
		return brightness;

	// Rescale 0..255 to min..max
	return min + DIV_ROUND_CLOSEST((max - min) * brightness,
				       AMDGPU_MAX_BL_LEVEL);
}

static u32 convert_brightness_to_user(const struct amdgpu_dm_backlight_caps *caps,
				      uint32_t brightness)
{
	unsigned min, max;

	if (!get_brightness_range(caps, &min, &max))
		return brightness;

	if (brightness < min)
		return 0;
	// Rescale min..max to 0..255
	return DIV_ROUND_CLOSEST(AMDGPU_MAX_BL_LEVEL * (brightness - min),
				 max - min);
}

static void amdgpu_dm_backlight_set_level(struct amdgpu_display_manager *dm,
					 int bl_idx,
					 u32 user_brightness)
{
	struct amdgpu_dm_backlight_caps caps;
	struct dc_link *link;
	u32 brightness;
	bool rc;

	amdgpu_dm_update_backlight_caps(dm, bl_idx);
	caps = dm->backlight_caps[bl_idx];

	dm->brightness[bl_idx] = user_brightness;
	/* update scratch register */
	if (bl_idx == 0)
		amdgpu_atombios_scratch_regs_set_backlight_level(dm->adev, dm->brightness[bl_idx]);
	brightness = convert_brightness_from_user(&caps, dm->brightness[bl_idx]);
	link = (struct dc_link *)dm->backlight_link[bl_idx];

	/* Change brightness based on AUX property */
	if (caps.aux_support) {
		rc = dc_link_set_backlight_level_nits(link, true, brightness,
						      AUX_BL_DEFAULT_TRANSITION_TIME_MS);
		if (!rc)
			DRM_DEBUG("DM: Failed to update backlight via AUX on eDP[%d]\n", bl_idx);
	} else {
		rc = dc_link_set_backlight_level(link, brightness, 0);
		if (!rc)
			DRM_DEBUG("DM: Failed to update backlight on eDP[%d]\n", bl_idx);
	}

	if (rc)
		dm->actual_brightness[bl_idx] = user_brightness;
}

static int amdgpu_dm_backlight_update_status(struct backlight_device *bd)
{
	struct amdgpu_display_manager *dm = bl_get_data(bd);
	int i;

	for (i = 0; i < dm->num_of_edps; i++) {
		if (bd == dm->backlight_dev[i])
			break;
	}
	if (i >= AMDGPU_DM_MAX_NUM_EDP)
		i = 0;
	amdgpu_dm_backlight_set_level(dm, i, bd->props.brightness);

	return 0;
}

static u32 amdgpu_dm_backlight_get_level(struct amdgpu_display_manager *dm,
					 int bl_idx)
{
	struct amdgpu_dm_backlight_caps caps;
	struct dc_link *link = (struct dc_link *)dm->backlight_link[bl_idx];

	amdgpu_dm_update_backlight_caps(dm, bl_idx);
	caps = dm->backlight_caps[bl_idx];

	if (caps.aux_support) {
		u32 avg, peak;
		bool rc;

		rc = dc_link_get_backlight_level_nits(link, &avg, &peak);
		if (!rc)
			return dm->brightness[bl_idx];
		return convert_brightness_to_user(&caps, avg);
	} else {
		int ret = dc_link_get_backlight_level(link);

		if (ret == DC_ERROR_UNEXPECTED)
			return dm->brightness[bl_idx];
		return convert_brightness_to_user(&caps, ret);
	}
}

static int amdgpu_dm_backlight_get_brightness(struct backlight_device *bd)
{
	struct amdgpu_display_manager *dm = bl_get_data(bd);
	int i;

	for (i = 0; i < dm->num_of_edps; i++) {
		if (bd == dm->backlight_dev[i])
			break;
	}
	if (i >= AMDGPU_DM_MAX_NUM_EDP)
		i = 0;
	return amdgpu_dm_backlight_get_level(dm, i);
}

static const struct backlight_ops amdgpu_dm_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = amdgpu_dm_backlight_get_brightness,
	.update_status	= amdgpu_dm_backlight_update_status,
};

static void
amdgpu_dm_register_backlight_device(struct amdgpu_display_manager *dm)
{
	char bl_name[16];
	struct backlight_properties props = { 0 };

	amdgpu_dm_update_backlight_caps(dm, dm->num_of_edps);
	dm->brightness[dm->num_of_edps] = AMDGPU_MAX_BL_LEVEL;

	if (!acpi_video_backlight_use_native()) {
		drm_info(adev_to_drm(dm->adev), "Skipping amdgpu DM backlight registration\n");
		/* Try registering an ACPI video backlight device instead. */
		acpi_video_register_backlight();
		return;
	}

	props.max_brightness = AMDGPU_MAX_BL_LEVEL;
	props.brightness = AMDGPU_MAX_BL_LEVEL;
	props.type = BACKLIGHT_RAW;

	snprintf(bl_name, sizeof(bl_name), "amdgpu_bl%d",
		 adev_to_drm(dm->adev)->primary->index + dm->num_of_edps);

	dm->backlight_dev[dm->num_of_edps] = backlight_device_register(bl_name,
								       adev_to_drm(dm->adev)->dev,
								       dm,
								       &amdgpu_dm_backlight_ops,
								       &props);

	if (IS_ERR(dm->backlight_dev[dm->num_of_edps]))
		DRM_ERROR("DM: Backlight registration failed!\n");
	else
		DRM_DEBUG_DRIVER("DM: Registered Backlight device: %s\n", bl_name);
}

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
	if ((link->connector_signal & (SIGNAL_TYPE_EDP | SIGNAL_TYPE_LVDS)) &&
	    link->type != dc_connection_none) {
		/*
		 * Event if registration failed, we should continue with
		 * DM initialization because not having a backlight control
		 * is better then a black screen.
		 */
		if (!dm->backlight_dev[dm->num_of_edps])
			amdgpu_dm_register_backlight_device(dm);

		if (dm->backlight_dev[dm->num_of_edps]) {
			dm->backlight_link[dm->num_of_edps] = link;
			dm->num_of_edps++;
		}
	}
}

static void amdgpu_set_panel_orientation(struct drm_connector *connector);

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
	bool psr_feature_enabled = false;

	dm->display_indexes_num = dm->dc->caps.max_streams;
	/* Update the actual used number of crtc */
	adev->mode_info.num_crtc = adev->dm.display_indexes_num;

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

		/* Do not create overlay if MPO disabled */
		if (amdgpu_dc_debug_mask & DC_DISABLE_MPO)
			break;

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

	/* Use Outbox interrupt */
	switch (adev->ip_versions[DCE_HWIP][0]) {
	case IP_VERSION(3, 0, 0):
	case IP_VERSION(3, 1, 2):
	case IP_VERSION(3, 1, 3):
	case IP_VERSION(3, 1, 4):
	case IP_VERSION(3, 1, 5):
	case IP_VERSION(3, 1, 6):
	case IP_VERSION(3, 2, 0):
	case IP_VERSION(3, 2, 1):
	case IP_VERSION(2, 1, 0):
		if (register_outbox_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail;
		}
		break;
	default:
		DRM_DEBUG_KMS("Unsupported DCN IP version for outbox: 0x%X\n",
			      adev->ip_versions[DCE_HWIP][0]);
	}

	/* Determine whether to enable PSR support by default. */
	if (!(amdgpu_dc_debug_mask & DC_DISABLE_PSR)) {
		switch (adev->ip_versions[DCE_HWIP][0]) {
		case IP_VERSION(3, 1, 2):
		case IP_VERSION(3, 1, 3):
		case IP_VERSION(3, 1, 4):
		case IP_VERSION(3, 1, 5):
		case IP_VERSION(3, 1, 6):
		case IP_VERSION(3, 2, 0):
		case IP_VERSION(3, 2, 1):
			psr_feature_enabled = true;
			break;
		default:
			psr_feature_enabled = amdgpu_dc_feature_mask & DC_PSR_MASK;
			break;
		}
	}

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
		} else {
			bool ret = false;

			mutex_lock(&dm->dc_lock);
			ret = dc_link_detect(link, DETECT_REASON_BOOT);
			mutex_unlock(&dm->dc_lock);

			if (ret) {
				amdgpu_dm_update_connector_after_detect(aconnector);
				register_backlight_device(dm, link);

				if (dm->num_of_edps)
					update_connector_ext_caps(aconnector);

				if (psr_feature_enabled)
					amdgpu_dm_set_psr_caps(link);

				/* TODO: Fix vblank control helpers to delay PSR entry to allow this when
				 * PSR is also supported.
				 */
				if (link->psr_settings.psr_feature_enabled)
					adev_to_drm(adev)->vblank_disable_immediate = false;
			}
		}
		amdgpu_set_panel_orientation(&aconnector->base);
	}

	/* If we didn't find a panel, notify the acpi video detection */
	if (dm->adev->flags & AMD_IS_APU && dm->num_of_edps == 0)
		acpi_video_report_nolcd();

	/* Software is initialized. Now we can register interrupt handlers. */
	switch (adev->asic_type) {
#if defined(CONFIG_DRM_AMD_DC_SI)
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_OLAND:
		if (dce60_register_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail;
		}
		break;
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
		if (dce110_register_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail;
		}
		break;
	default:
		switch (adev->ip_versions[DCE_HWIP][0]) {
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
			if (dcn10_register_irq_handlers(dm->adev)) {
				DRM_ERROR("DM: Failed to initialize IRQ\n");
				goto fail;
			}
			break;
		default:
			DRM_ERROR("Unsupported DCE IP versions: 0x%X\n",
					adev->ip_versions[DCE_HWIP][0]);
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
	struct drm_device *drm_dev = dev_get_drvdata(device);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);

	ret = kstrtoint(buf, 0, &s3_state);

	if (ret == 0) {
		if (s3_state) {
			dm_resume(adev);
			drm_kms_helper_hotplug_event(adev_to_drm(adev));
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
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	struct atom_context *ctx = mode_info->atom_context;
	int index = GetIndexIntoMasterTable(DATA, Object_Header);
	u16 data_offset;

	/* if there is no object header, skip DM */
	if (!amdgpu_atom_parse_data_header(ctx, index, NULL, NULL, NULL, &data_offset)) {
		adev->harvest_ip_mask |= AMD_HARVEST_IP_DMU_MASK;
		dev_info(adev->dev, "No object header, skipping DM\n");
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

		switch (adev->ip_versions[DCE_HWIP][0]) {
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
			adev->mode_info.num_crtc = 4;
			adev->mode_info.num_hpd = 4;
			adev->mode_info.num_dig = 4;
			break;
		default:
			DRM_ERROR("Unsupported DCE IP versions: 0x%x\n",
					adev->ip_versions[DCE_HWIP][0]);
			return -EINVAL;
		}
		break;
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
		adev_to_drm(adev)->dev,
		&dev_attr_s3_debug);
#endif

	return 0;
}

static bool modereset_required(struct drm_crtc_state *crtc_state)
{
	return !crtc_state->active && drm_atomic_crtc_needs_modeset(crtc_state);
}

static void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs amdgpu_dm_encoder_funcs = {
	.destroy = amdgpu_dm_encoder_destroy,
};

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
			    struct dc_plane_address *address,
			    bool tmz_surface,
			    bool force_disable_dcc)
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
		DRM_ERROR(
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

	ret = fill_plane_buffer_attributes(adev, afb, plane_info->format,
					   plane_info->rotation, tiling_flags,
					   &plane_info->tiling_info,
					   &plane_info->plane_size,
					   &plane_info->dcc, address,
					   tmz_surface, force_disable_dcc);
	if (ret)
		return ret;

	fill_blending_from_plane_state(
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
	bool force_disable_dcc = false;

	ret = fill_dc_scaling_info(adev, plane_state, &scaling_info);
	if (ret)
		return ret;

	dc_plane_state->src_rect = scaling_info.src_rect;
	dc_plane_state->dst_rect = scaling_info.dst_rect;
	dc_plane_state->clip_rect = scaling_info.clip_rect;
	dc_plane_state->scaling_quality = scaling_info.scaling_quality;

	force_disable_dcc = adev->asic_type == CHIP_RAVEN && adev->in_suspend;
	ret = fill_dc_plane_info_and_addr(adev, plane_state,
					  afb->tiling_flags,
					  &plane_info,
					  &dc_plane_state->address,
					  afb->tmz_surface,
					  force_disable_dcc);
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
	ret = amdgpu_dm_update_plane_color_mgmt(dm_crtc_state, dc_plane_state);
	if (ret)
		return ret;

	return 0;
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
 *
 * Today, amdgpu_dm only supports the MPO and cursor usecase.
 *
 * TODO: Also enable for FB_DAMAGE_CLIPS
 */
static void fill_dc_dirty_rects(struct drm_plane *plane,
				struct drm_plane_state *old_plane_state,
				struct drm_plane_state *new_plane_state,
				struct drm_crtc_state *crtc_state,
				struct dc_flip_addrs *flip_addrs)
{
	struct dm_crtc_state *dm_crtc_state = to_dm_crtc_state(crtc_state);
	struct rect *dirty_rects = flip_addrs->dirty_rects;
	uint32_t num_clips;
	bool bb_changed;
	bool fb_changed;
	uint32_t i = 0;

	flip_addrs->dirty_rect_count = 0;

	/*
	 * Cursor plane has it's own dirty rect update interface. See
	 * dcn10_dmub_update_cursor_data and dmub_cmd_update_cursor_info_data
	 */
	if (plane->type == DRM_PLANE_TYPE_CURSOR)
		return;

	/*
	 * Today, we only consider MPO use-case for PSR SU. If MPO not
	 * requested, and there is a plane update, do FFU.
	 */
	if (!dm_crtc_state->mpo_requested) {
		dirty_rects[0].x = 0;
		dirty_rects[0].y = 0;
		dirty_rects[0].width = dm_crtc_state->base.mode.crtc_hdisplay;
		dirty_rects[0].height = dm_crtc_state->base.mode.crtc_vdisplay;
		flip_addrs->dirty_rect_count = 1;
		DRM_DEBUG_DRIVER("[PLANE:%d] PSR FFU dirty rect size (%d, %d)\n",
				 new_plane_state->plane->base.id,
				 dm_crtc_state->base.mode.crtc_hdisplay,
				 dm_crtc_state->base.mode.crtc_vdisplay);
		return;
	}

	/*
	 * MPO is requested. Add entire plane bounding box to dirty rects if
	 * flipped to or damaged.
	 *
	 * If plane is moved or resized, also add old bounding box to dirty
	 * rects.
	 */
	num_clips = drm_plane_get_damage_clips_count(new_plane_state);
	fb_changed = old_plane_state->fb->base.id !=
		     new_plane_state->fb->base.id;
	bb_changed = (old_plane_state->crtc_x != new_plane_state->crtc_x ||
		      old_plane_state->crtc_y != new_plane_state->crtc_y ||
		      old_plane_state->crtc_w != new_plane_state->crtc_w ||
		      old_plane_state->crtc_h != new_plane_state->crtc_h);

	DRM_DEBUG_DRIVER("[PLANE:%d] PSR bb_changed:%d fb_changed:%d num_clips:%d\n",
			 new_plane_state->plane->base.id,
			 bb_changed, fb_changed, num_clips);

	if (num_clips || fb_changed || bb_changed) {
		dirty_rects[i].x = new_plane_state->crtc_x;
		dirty_rects[i].y = new_plane_state->crtc_y;
		dirty_rects[i].width = new_plane_state->crtc_w;
		dirty_rects[i].height = new_plane_state->crtc_h;
		DRM_DEBUG_DRIVER("[PLANE:%d] PSR SU dirty rect at (%d, %d) size (%d, %d)\n",
				 new_plane_state->plane->base.id,
				 dirty_rects[i].x, dirty_rects[i].y,
				 dirty_rects[i].width, dirty_rects[i].height);
		i += 1;
	}

	/* Add old plane bounding-box if plane is moved or resized */
	if (bb_changed) {
		dirty_rects[i].x = old_plane_state->crtc_x;
		dirty_rects[i].y = old_plane_state->crtc_y;
		dirty_rects[i].width = old_plane_state->crtc_w;
		dirty_rects[i].height = old_plane_state->crtc_h;
		DRM_DEBUG_DRIVER("[PLANE:%d] PSR SU dirty rect at (%d, %d) size (%d, %d)\n",
				old_plane_state->plane->base.id,
				dirty_rects[i].x, dirty_rects[i].y,
				dirty_rects[i].width, dirty_rects[i].height);
		i += 1;
	}

	flip_addrs->dirty_rect_count = i;
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

	DRM_DEBUG_KMS("Destination Rectangle x:%d  y:%d  width:%d  height:%d\n",
		      dst.x, dst.y, dst.width, dst.height);

}

static enum dc_color_depth
convert_color_depth_from_display_info(const struct drm_connector *connector,
				      bool is_y420, int requested_bpc)
{
	uint8_t bpc;

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

static bool adjust_colour_depth_from_display_info(
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

static void fill_stream_properties_from_drm_display_mode(
	struct dc_stream_state *stream,
	const struct drm_display_mode *mode_in,
	const struct drm_connector *connector,
	const struct drm_connector_state *connector_state,
	const struct dc_stream_state *old_stream,
	int requested_bpc)
{
	struct dc_crtc_timing *timing_out = &stream->timing;
	const struct drm_display_info *info = &connector->display_info;
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct hdmi_vendor_infoframe hv_frame;
	struct hdmi_avi_infoframe avi_frame;

	memset(&hv_frame, 0, sizeof(hv_frame));
	memset(&avi_frame, 0, sizeof(avi_frame));

	timing_out->h_border_left = 0;
	timing_out->h_border_right = 0;
	timing_out->v_border_top = 0;
	timing_out->v_border_bottom = 0;
	/* TODO: un-hardcode */
	if (drm_mode_is_420_only(info, mode_in)
			&& stream->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR420;
	else if (drm_mode_is_420_also(info, mode_in)
			&& aconnector->force_yuv420_output)
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR420;
	else if ((connector->display_info.color_formats & DRM_COLOR_FORMAT_YCBCR444)
			&& stream->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR444;
	else
		timing_out->pixel_encoding = PIXEL_ENCODING_RGB;

	timing_out->timing_3d_format = TIMING_3D_FORMAT_NONE;
	timing_out->display_color_depth = convert_color_depth_from_display_info(
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

	if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		drm_hdmi_avi_infoframe_from_display_mode(&avi_frame, (struct drm_connector *)connector, mode_in);
		timing_out->vic = avi_frame.video_code;
		drm_hdmi_vendor_infoframe_from_display_mode(&hv_frame, (struct drm_connector *)connector, mode_in);
		timing_out->hdmi_vic = hv_frame.vic;
	}

	if (is_freesync_video_mode(mode_in, aconnector)) {
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

	stream->out_transfer_func->type = TF_TYPE_PREDEFINED;
	stream->out_transfer_func->tf = TRANSFER_FUNCTION_SRGB;
	if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		if (!adjust_colour_depth_from_display_info(timing_out, info) &&
		    drm_mode_is_420_also(info, mode_in) &&
		    timing_out->pixel_encoding != PIXEL_ENCODING_YCBCR420) {
			timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR420;
			adjust_colour_depth_from_display_info(timing_out, info);
		}
	}

	stream->output_color_space = get_output_color_space(timing_out);
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
	struct dc_stream_state *master = NULL;

	if (stream->triggered_crtc_reset.enabled) {
		master = stream->triggered_crtc_reset.event_source;
		stream->triggered_crtc_reset.event =
			master->timing.flags.VSYNC_POSITIVE_POLARITY ?
			CRTC_EVENT_VSYNC_RISING : CRTC_EVENT_VSYNC_FALLING;
		stream->triggered_crtc_reset.delay = TRIGGER_DELAY_NEXT_PIXEL;
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
static struct drm_display_mode *
get_highest_refresh_rate_mode(struct amdgpu_dm_connector *aconnector,
		bool use_probed_modes)
{
	struct drm_display_mode *m, *m_pref = NULL;
	u16 current_refresh, highest_refresh;
	struct list_head *list_head = use_probed_modes ?
		&aconnector->base.probed_modes :
		&aconnector->base.modes;

	if (aconnector->freesync_vid_base.clock != 0)
		return &aconnector->freesync_vid_base;

	/* Find the preferred mode */
	list_for_each_entry (m, list_head, head) {
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
			DRM_DEBUG_DRIVER("No preferred mode found in EDID\n");
			return NULL;
		}
	}

	highest_refresh = drm_mode_vrefresh(m_pref);

	/*
	 * Find the mode with highest refresh rate with same resolution.
	 * For some monitors, preferred mode is not the mode with highest
	 * supported refresh rate.
	 */
	list_for_each_entry (m, list_head, head) {
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

static bool is_freesync_video_mode(const struct drm_display_mode *mode,
		struct amdgpu_dm_connector *aconnector)
{
	struct drm_display_mode *high_mode;
	int timing_diff;

	high_mode = get_highest_refresh_rate_mode(aconnector, false);
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

#if defined(CONFIG_DRM_AMD_DC_DCN)
static void update_dsc_caps(struct amdgpu_dm_connector *aconnector,
			    struct dc_sink *sink, struct dc_stream_state *stream,
			    struct dsc_dec_dpcd_caps *dsc_caps)
{
	stream->timing.flags.DSC = 0;
	dsc_caps->is_dsc_supported = false;

	if (aconnector->dc_link && (sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT ||
	    sink->sink_signal == SIGNAL_TYPE_EDP)) {
		if (sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_NONE ||
			sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER)
			dc_dsc_parse_dsc_dpcd(aconnector->dc_link->ctx->dc,
				aconnector->dc_link->dpcd_caps.dsc_caps.dsc_basic_caps.raw,
				aconnector->dc_link->dpcd_caps.dsc_caps.dsc_branch_decoder_caps.raw,
				dsc_caps);
	}
}


static void apply_dsc_policy_for_edp(struct amdgpu_dm_connector *aconnector,
				    struct dc_sink *sink, struct dc_stream_state *stream,
				    struct dsc_dec_dpcd_caps *dsc_caps,
				    uint32_t max_dsc_target_bpp_limit_override)
{
	const struct dc_link_settings *verified_link_cap = NULL;
	uint32_t link_bw_in_kbps;
	uint32_t edp_min_bpp_x16, edp_max_bpp_x16;
	struct dc *dc = sink->ctx->dc;
	struct dc_dsc_bw_range bw_range = {0};
	struct dc_dsc_config dsc_cfg = {0};

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
				&bw_range)) {

		if (bw_range.max_kbps < link_bw_in_kbps) {
			if (dc_dsc_compute_config(dc->res_pool->dscs[0],
					dsc_caps,
					dc->debug.dsc_min_slice_height_override,
					max_dsc_target_bpp_limit_override,
					0,
					&stream->timing,
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
				dc->debug.dsc_min_slice_height_override,
				max_dsc_target_bpp_limit_override,
				link_bw_in_kbps,
				&stream->timing,
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
	uint32_t link_bandwidth_kbps;
	uint32_t max_dsc_target_bpp_limit_override = 0;
	struct dc *dc = sink->ctx->dc;
	uint32_t max_supported_bw_in_kbps, timing_bw_in_kbps;
	uint32_t dsc_max_supported_bw_in_kbps;

	link_bandwidth_kbps = dc_link_bandwidth_kbps(aconnector->dc_link,
							dc_link_get_link_cap(aconnector->dc_link));
	if (stream->link && stream->link->local_sink)
		max_dsc_target_bpp_limit_override =
			stream->link->local_sink->edid_caps.panel_patch.max_dsc_target_bpp_limit;

	/* Set DSC policy according to dsc_clock_en */
	dc_dsc_policy_set_enable_dsc_when_not_needed(
		aconnector->dsc_settings.dsc_force_enable == DSC_CLK_FORCE_ENABLE);

	if (aconnector->dc_link && sink->sink_signal == SIGNAL_TYPE_EDP &&
	    !aconnector->dc_link->panel_config.dsc.disable_dsc_edp &&
	    dc->caps.edp_dsc_support && aconnector->dsc_settings.dsc_force_enable != DSC_CLK_FORCE_DISABLE) {

		apply_dsc_policy_for_edp(aconnector, sink, stream, dsc_caps, max_dsc_target_bpp_limit_override);

	} else if (aconnector->dc_link && sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT) {
		if (sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_NONE) {
			if (dc_dsc_compute_config(aconnector->dc_link->ctx->dc->res_pool->dscs[0],
						dsc_caps,
						aconnector->dc_link->ctx->dc->debug.dsc_min_slice_height_override,
						max_dsc_target_bpp_limit_override,
						link_bandwidth_kbps,
						&stream->timing,
						&stream->timing.dsc_cfg)) {
				stream->timing.flags.DSC = 1;
				DRM_DEBUG_DRIVER("%s: [%s] DSC is selected from SST RX\n", __func__, drm_connector->name);
			}
		} else if (sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER) {
			timing_bw_in_kbps = dc_bandwidth_in_kbps_from_timing(&stream->timing);
			max_supported_bw_in_kbps = link_bandwidth_kbps;
			dsc_max_supported_bw_in_kbps = link_bandwidth_kbps;

			if (timing_bw_in_kbps > max_supported_bw_in_kbps &&
					max_supported_bw_in_kbps > 0 &&
					dsc_max_supported_bw_in_kbps > 0)
				if (dc_dsc_compute_config(aconnector->dc_link->ctx->dc->res_pool->dscs[0],
						dsc_caps,
						aconnector->dc_link->ctx->dc->debug.dsc_min_slice_height_override,
						max_dsc_target_bpp_limit_override,
						dsc_max_supported_bw_in_kbps,
						&stream->timing,
						&stream->timing.dsc_cfg)) {
					stream->timing.flags.DSC = 1;
					DRM_DEBUG_DRIVER("%s: [%s] DSC is selected from DP-HDMI PCON\n",
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
#endif /* CONFIG_DRM_AMD_DC_DCN */

static struct dc_stream_state *
create_stream_for_sink(struct amdgpu_dm_connector *aconnector,
		       const struct drm_display_mode *drm_mode,
		       const struct dm_connector_state *dm_state,
		       const struct dc_stream_state *old_stream,
		       int requested_bpc)
{
	struct drm_display_mode *preferred_mode = NULL;
	struct drm_connector *drm_connector;
	const struct drm_connector_state *con_state =
		dm_state ? &dm_state->base : NULL;
	struct dc_stream_state *stream = NULL;
	struct drm_display_mode mode = *drm_mode;
	struct drm_display_mode saved_mode;
	struct drm_display_mode *freesync_mode = NULL;
	bool native_mode_found = false;
	bool recalculate_timing = false;
	bool scale = dm_state ? (dm_state->scaling != RMX_OFF) : false;
	int mode_refresh;
	int preferred_refresh = 0;
#if defined(CONFIG_DRM_AMD_DC_DCN)
	struct dsc_dec_dpcd_caps dsc_caps;
#endif

	struct dc_sink *sink = NULL;

	memset(&saved_mode, 0, sizeof(saved_mode));

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

	stream->timing.flags.LTE_340MCSC_SCRAMBLE =
		drm_connector->display_info.hdmi.scdc.scrambling.low_rates;

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
		recalculate_timing = amdgpu_freesync_vid_mode &&
				 is_freesync_video_mode(&mode, aconnector);
		if (recalculate_timing) {
			freesync_mode = get_highest_refresh_rate_mode(aconnector, false);
			drm_mode_copy(&saved_mode, &mode);
			drm_mode_copy(&mode, freesync_mode);
		} else {
			decide_crtc_timing_for_drm_display_mode(
					&mode, preferred_mode, scale);

			preferred_refresh = drm_mode_vrefresh(preferred_mode);
		}
	}

	if (recalculate_timing)
		drm_mode_set_crtcinfo(&saved_mode, 0);
	else if (!dm_state)
		drm_mode_set_crtcinfo(&mode, 0);

	/*
	* If scaling is enabled and refresh rate didn't change
	* we copy the vic and polarities of the old timings
	*/
	if (!scale || mode_refresh != preferred_refresh)
		fill_stream_properties_from_drm_display_mode(
			stream, &mode, &aconnector->base, con_state, NULL,
			requested_bpc);
	else
		fill_stream_properties_from_drm_display_mode(
			stream, &mode, &aconnector->base, con_state, old_stream,
			requested_bpc);

#if defined(CONFIG_DRM_AMD_DC_DCN)
	/* SST DSC determination policy */
	update_dsc_caps(aconnector, sink, stream, &dsc_caps);
	if (aconnector->dsc_settings.dsc_force_enable != DSC_CLK_FORCE_DISABLE && dsc_caps.is_dsc_supported)
		apply_dsc_policy_for_stream(aconnector, sink, stream, &dsc_caps);
#endif

	update_stream_scaling_settings(&mode, dm_state, stream);

	fill_audio_info(
		&stream->audio_info,
		drm_connector,
		sink);

	update_stream_signal(stream, sink);

	if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		mod_build_hf_vsif_infopacket(stream, &stream->vsp_infopacket);

	if (stream->link->psr_settings.psr_feature_enabled) {
		//
		// should decide stream support vsc sdp colorimetry capability
		// before building vsc info packet
		//
		stream->use_vsc_sdp_for_colorimetry = false;
		if (aconnector->dc_sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
			stream->use_vsc_sdp_for_colorimetry =
				aconnector->dc_sink->is_vsc_sdp_colorimetry_supported;
		} else {
			if (stream->link->dpcd_caps.dprx_feature.bits.VSC_SDP_COLORIMETRY_SUPPORTED)
				stream->use_vsc_sdp_for_colorimetry = true;
		}
		mod_build_vsc_infopacket(stream, &stream->vsc_infopacket, stream->output_color_space);
		aconnector->psr_skip_count = AMDGPU_DM_PSR_ENTRY_DELAY;

	}
finish:
	dc_sink_release(sink);

	return stream;
}

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
		connected = (aconnector->base.force == DRM_FORCE_ON ||
				aconnector->base.force == DRM_FORCE_ON_DIGITAL);

	update_subconnector_property(aconnector);

	return (connected ? connector_status_connected :
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
	struct amdgpu_device *adev = drm_to_adev(connector->dev);
	struct amdgpu_display_manager *dm = &adev->dm;
	int i;

	/*
	 * Call only if mst_mgr was initialized before since it's not done
	 * for all connector types.
	 */
	if (aconnector->mst_mgr.dev)
		drm_dp_mst_topology_mgr_destroy(&aconnector->mst_mgr);

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)
	for (i = 0; i < dm->num_of_edps; i++) {
		if ((link == dm->backlight_link[i]) && dm->backlight_dev[i]) {
			backlight_device_unregister(dm->backlight_dev[i]);
			dm->backlight_dev[i] = NULL;
		}
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
	kfree(aconnector->dm_dp_aux.aux.name);

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
		state->vcpi_slots = 0;
		state->pbn = 0;

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
	new_state->vcpi_slots = state->vcpi_slots;
	new_state->pbn = state->pbn;
	return &new_state->base;
}

static int
amdgpu_dm_connector_late_register(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
		to_amdgpu_dm_connector(connector);
	int r;

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

struct dc_stream_state *
create_validate_stream_for_sink(struct amdgpu_dm_connector *aconnector,
				const struct drm_display_mode *drm_mode,
				const struct dm_connector_state *dm_state,
				const struct dc_stream_state *old_stream)
{
	struct drm_connector *connector = &aconnector->base;
	struct amdgpu_device *adev = drm_to_adev(connector->dev);
	struct dc_stream_state *stream;
	const struct drm_connector_state *drm_state = dm_state ? &dm_state->base : NULL;
	int requested_bpc = drm_state ? drm_state->max_requested_bpc : 8;
	enum dc_status dc_result = DC_OK;

	do {
		stream = create_stream_for_sink(aconnector, drm_mode,
						dm_state, old_stream,
						requested_bpc);
		if (stream == NULL) {
			DRM_ERROR("Failed to create stream for sink!\n");
			break;
		}

		dc_result = dc_validate_stream(adev->dm.dc, stream);
		if (dc_result == DC_OK && stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
			dc_result = dm_dp_mst_is_port_support_mode(aconnector, stream);

		if (dc_result != DC_OK) {
			DRM_DEBUG_KMS("Mode %dx%d (clk %d) failed DC validation with error %d (%s)\n",
				      drm_mode->hdisplay,
				      drm_mode->vdisplay,
				      drm_mode->clock,
				      dc_result,
				      dc_status_to_str(dc_result));

			dc_stream_release(stream);
			stream = NULL;
			requested_bpc -= 2; /* lower bpc to retry validation */
		}

	} while (stream == NULL && requested_bpc >= 6);

	if (dc_result == DC_FAIL_ENC_VALIDATE && !aconnector->force_yuv420_output) {
		DRM_DEBUG_KMS("Retry forcing YCbCr420 encoding\n");

		aconnector->force_yuv420_output = true;
		stream = create_validate_stream_for_sink(aconnector, drm_mode,
						dm_state, old_stream);
		aconnector->force_yuv420_output = false;
	}

	return stream;
}

enum drm_mode_status amdgpu_dm_connector_mode_valid(struct drm_connector *connector,
				   struct drm_display_mode *mode)
{
	int result = MODE_ERROR;
	struct dc_sink *dc_sink;
	/* TODO: Unhardcode stream count */
	struct dc_stream_state *stream;
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
		DRM_ERROR("dc_sink is NULL!\n");
		goto fail;
	}

	stream = create_validate_stream_for_sink(aconnector, mode, NULL, NULL);
	if (stream) {
		dc_stream_release(stream);
		result = MODE_OK;
	}

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

static int
amdgpu_dm_connector_atomic_check(struct drm_connector *conn,
				 struct drm_atomic_state *state)
{
	struct drm_connector_state *new_con_state =
		drm_atomic_get_new_connector_state(state, conn);
	struct drm_connector_state *old_con_state =
		drm_atomic_get_old_connector_state(state, conn);
	struct drm_crtc *crtc = new_con_state->crtc;
	struct drm_crtc_state *new_crtc_state;
	struct amdgpu_dm_connector *aconn = to_amdgpu_dm_connector(conn);
	int ret;

	trace_amdgpu_dm_connector_atomic_check(new_con_state);

	if (conn->connector_type == DRM_MODE_CONNECTOR_DisplayPort) {
		ret = drm_dp_mst_root_conn_atomic_check(new_con_state, &aconn->mst_mgr);
		if (ret < 0)
			return ret;
	}

	if (!crtc)
		return 0;

	if (!drm_connector_atomic_hdr_metadata_equal(old_con_state, new_con_state)) {
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

static void dm_encoder_helper_disable(struct drm_encoder *encoder)
{

}

int convert_dc_color_depth_into_bpc(enum dc_color_depth display_color_depth)
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

static int dm_encoder_helper_atomic_check(struct drm_encoder *encoder,
					  struct drm_crtc_state *crtc_state,
					  struct drm_connector_state *conn_state)
{
	struct drm_atomic_state *state = crtc_state->state;
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

	if (!aconnector->port)
		return 0;

	mst_port = aconnector->port;
	mst_mgr = &aconnector->mst_port->mst_mgr;

	if (!crtc_state->connectors_changed && !crtc_state->mode_changed)
		return 0;

	mst_state = drm_atomic_get_mst_topology_state(state, mst_mgr);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	if (!mst_state->pbn_div)
		mst_state->pbn_div = dm_mst_get_pbn_divider(aconnector->mst_port->dc_link);

	if (!state->duplicated) {
		int max_bpc = conn_state->max_requested_bpc;
		is_y420 = drm_mode_is_420_also(&connector->display_info, adjusted_mode) &&
			  aconnector->force_yuv420_output;
		color_depth = convert_color_depth_from_display_info(connector,
								    is_y420,
								    max_bpc);
		bpp = convert_dc_color_depth_into_bpc(color_depth) * 3;
		clock = adjusted_mode->clock;
		dm_new_connector_state->pbn = drm_dp_calc_pbn_mode(clock, bpp, false);
	}

	dm_new_connector_state->vcpi_slots =
		drm_dp_atomic_find_time_slots(state, mst_mgr, mst_port,
					      dm_new_connector_state->pbn);
	if (dm_new_connector_state->vcpi_slots < 0) {
		DRM_DEBUG_ATOMIC("failed finding vcpi slots: %d\n", (int)dm_new_connector_state->vcpi_slots);
		return dm_new_connector_state->vcpi_slots;
	}
	return 0;
}

const struct drm_encoder_helper_funcs amdgpu_dm_encoder_helper_funcs = {
	.disable = dm_encoder_helper_disable,
	.atomic_check = dm_encoder_helper_atomic_check
};

#if defined(CONFIG_DRM_AMD_DC_DCN)
static int dm_update_mst_vcpi_slots_for_dsc(struct drm_atomic_state *state,
					    struct dc_state *dc_state,
					    struct dsc_mst_fairness_vars *vars)
{
	struct dc_stream_state *stream = NULL;
	struct drm_connector *connector;
	struct drm_connector_state *new_con_state;
	struct amdgpu_dm_connector *aconnector;
	struct dm_connector_state *dm_conn_state;
	int i, j, ret;
	int vcpi, pbn_div, pbn, slot_num = 0;

	for_each_new_connector_in_state(state, connector, new_con_state, i) {

		aconnector = to_amdgpu_dm_connector(connector);

		if (!aconnector->port)
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

		if (j == dc_state->stream_count)
			continue;

		slot_num = DIV_ROUND_UP(pbn, pbn_div);

		if (stream->timing.flags.DSC != 1) {
			dm_conn_state->pbn = pbn;
			dm_conn_state->vcpi_slots = slot_num;

			ret = drm_dp_mst_atomic_enable_dsc(state, aconnector->port,
							   dm_conn_state->pbn, false);
			if (ret < 0)
				return ret;

			continue;
		}

		vcpi = drm_dp_mst_atomic_enable_dsc(state, aconnector->port, pbn, true);
		if (vcpi < 0)
			return vcpi;

		dm_conn_state->pbn = pbn;
		dm_conn_state->vcpi_slots = vcpi;
	}
	return 0;
}
#endif

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
		if (!mode)
			continue;

		drm_mode_probed_add(connector, mode);
		amdgpu_dm_connector->num_modes++;
	}
}

static void amdgpu_set_panel_orientation(struct drm_connector *connector)
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

		/* Freesync capabilities are reset by calling
		 * drm_add_edid_modes() and need to be
		 * restored here.
		 */
		amdgpu_dm_update_freesync_caps(connector, edid);
	} else {
		amdgpu_dm_connector->num_modes = 0;
	}
}

static bool is_duplicate_mode(struct amdgpu_dm_connector *aconnector,
			      struct drm_display_mode *mode)
{
	struct drm_display_mode *m;

	list_for_each_entry (m, &aconnector->base.probed_modes, head) {
		if (drm_mode_equal(m, mode))
			return true;
	}

	return false;
}

static uint add_fs_modes(struct amdgpu_dm_connector *aconnector)
{
	const struct drm_display_mode *m;
	struct drm_display_mode *new_mode;
	uint i;
	uint32_t new_modes_count = 0;

	/* Standard FPS values
	 *
	 * 23.976       - TV/NTSC
	 * 24 	        - Cinema
	 * 25 	        - TV/PAL
	 * 29.97        - TV/NTSC
	 * 30 	        - TV/NTSC
	 * 48 	        - Cinema HFR
	 * 50 	        - TV/PAL
	 * 60 	        - Commonly used
	 * 48,72,96,120 - Multiples of 24
	 */
	static const uint32_t common_rates[] = {
		23976, 24000, 25000, 29970, 30000,
		48000, 50000, 60000, 72000, 96000, 120000
	};

	/*
	 * Find mode with highest refresh rate with the same resolution
	 * as the preferred mode. Some monitors report a preferred mode
	 * with lower resolution than the highest refresh rate supported.
	 */

	m = get_highest_refresh_rate_mode(aconnector, true);
	if (!m)
		return 0;

	for (i = 0; i < ARRAY_SIZE(common_rates); i++) {
		uint64_t target_vtotal, target_vtotal_diff;
		uint64_t num, den;

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
						   struct edid *edid)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
		to_amdgpu_dm_connector(connector);

	if (!(amdgpu_freesync_vid_mode && edid))
		return;

	if (amdgpu_dm_connector->max_vfreq - amdgpu_dm_connector->min_vfreq > 10)
		amdgpu_dm_connector->num_modes +=
			add_fs_modes(amdgpu_dm_connector);
}

static int amdgpu_dm_connector_get_modes(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);
	struct drm_encoder *encoder;
	struct edid *edid = amdgpu_dm_connector->edid;

	encoder = amdgpu_dm_connector_to_encoder(connector);

	if (!drm_edid_is_valid(edid)) {
		amdgpu_dm_connector->num_modes =
				drm_add_modes_noedid(connector, 640, 480);
	} else {
		amdgpu_dm_connector_ddc_get_modes(connector, edid);
		/* most eDP supports only timings from its edid,
		 * usually only detailed timings are available
		 * from eDP edid. timings which are not from edid
		 * may damage eDP
		 */
		if (connector->connector_type != DRM_MODE_CONNECTOR_eDP)
			amdgpu_dm_connector_add_common_modes(encoder, connector);
		amdgpu_dm_connector_add_freesync_modes(connector, edid);
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
	struct amdgpu_device *adev = drm_to_adev(dm->ddev);

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
	aconnector->audio_inst = -1;
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
		link->link_enc = link_enc_cfg_get_link_enc(link);
		ASSERT(link->link_enc);
		if (link->link_enc)
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

	if (!aconnector->mst_port)
		drm_connector_attach_max_bpc_property(&aconnector->base, 8, 16);

	/* This defaults to the max in the range, but we want 8bpc for non-edp. */
	aconnector->base.state->max_bpc = (connector_type == DRM_MODE_CONNECTOR_eDP) ? 16 : 8;
	aconnector->base.state->max_requested_bpc = aconnector->base.state->max_bpc;

	if (connector_type == DRM_MODE_CONNECTOR_eDP &&
	    (dc_is_dmcu_initialized(adev->dm.dc) || adev->dm.dc->ctx->dmub_srv)) {
		drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.abm_level_property, 0);
	}

	if (connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector_type == DRM_MODE_CONNECTOR_DisplayPort ||
	    connector_type == DRM_MODE_CONNECTOR_eDP) {
		drm_connector_attach_hdr_output_metadata_property(&aconnector->base);

		if (!aconnector->mst_port)
			drm_connector_attach_vrr_capable_property(&aconnector->base);

#ifdef CONFIG_DRM_AMD_DC_HDCP
		if (adev->dm.hdcp_workqueue)
			drm_connector_attach_content_protection_property(&aconnector->base, true);
#endif
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
			ddc_service->link->link_index,
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

	res = drm_connector_init_with_ddc(
			dm->ddev,
			&aconnector->base,
			&amdgpu_dm_connector_funcs,
			connector_type,
			&i2c->base);

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

	if (connector_type == DRM_MODE_CONNECTOR_DisplayPort
		|| connector_type == DRM_MODE_CONNECTOR_eDP)
		amdgpu_dm_initialize_dp_connector(dm, aconnector, link->link_index);

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

static void manage_dm_interrupts(struct amdgpu_device *adev,
				 struct amdgpu_crtc *acrtc,
				 bool enable)
{
	/*
	 * We have no guarantee that the frontend index maps to the same
	 * backend index - some even map to more than one.
	 *
	 * TODO: Use a different interrupt or check DC itself for the mapping.
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
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
		amdgpu_irq_get(
			adev,
			&adev->vline0_irq,
			irq_type);
#endif
	} else {
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
		amdgpu_irq_put(
			adev,
			&adev->vline0_irq,
			irq_type);
#endif
		amdgpu_irq_put(
			adev,
			&adev->pageflip_irq,
			irq_type);
		drm_crtc_vblank_off(&acrtc->base);
	}
}

static void dm_update_pflip_irq_state(struct amdgpu_device *adev,
				      struct amdgpu_crtc *acrtc)
{
	int irq_type =
		amdgpu_display_crtc_idx_to_irq_type(adev, acrtc->crtc_id);

	/**
	 * This reads the current state for the IRQ and force reapplies
	 * the setting to hardware.
	 */
	amdgpu_irq_update(adev, &adev->pageflip_irq, irq_type);
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

#ifdef CONFIG_DRM_AMD_DC_HDCP
static bool is_content_protection_different(struct drm_connector_state *state,
					    const struct drm_connector_state *old_state,
					    const struct drm_connector *connector, struct hdcp_workqueue *hdcp_w)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct dm_connector_state *dm_con_state = to_dm_connector_state(connector->state);

	/* Handle: Type0/1 change */
	if (old_state->hdcp_content_type != state->hdcp_content_type &&
	    state->content_protection != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
		state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		return true;
	}

	/* CP is being re enabled, ignore this
	 *
	 * Handles:	ENABLED -> DESIRED
	 */
	if (old_state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED &&
	    state->content_protection == DRM_MODE_CONTENT_PROTECTION_DESIRED) {
		state->content_protection = DRM_MODE_CONTENT_PROTECTION_ENABLED;
		return false;
	}

	/* S3 resume case, since old state will always be 0 (UNDESIRED) and the restored state will be ENABLED
	 *
	 * Handles:	UNDESIRED -> ENABLED
	 */
	if (old_state->content_protection == DRM_MODE_CONTENT_PROTECTION_UNDESIRED &&
	    state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED)
		state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;

	/* Stream removed and re-enabled
	 *
	 * Can sometimes overlap with the HPD case,
	 * thus set update_hdcp to false to avoid
	 * setting HDCP multiple times.
	 *
	 * Handles:	DESIRED -> DESIRED (Special case)
	 */
	if (!(old_state->crtc && old_state->crtc->enabled) &&
		state->crtc && state->crtc->enabled &&
		connector->state->content_protection == DRM_MODE_CONTENT_PROTECTION_DESIRED) {
		dm_con_state->update_hdcp = false;
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
	if (dm_con_state->update_hdcp && state->content_protection == DRM_MODE_CONTENT_PROTECTION_DESIRED &&
	    connector->dpms == DRM_MODE_DPMS_ON && aconnector->dc_sink != NULL) {
		dm_con_state->update_hdcp = false;
		return true;
	}

	/*
	 * Handles:	UNDESIRED -> UNDESIRED
	 *		DESIRED -> DESIRED
	 *		ENABLED -> ENABLED
	 */
	if (old_state->content_protection == state->content_protection)
		return false;

	/*
	 * Handles:	UNDESIRED -> DESIRED
	 *		DESIRED -> UNDESIRED
	 *		ENABLED -> UNDESIRED
	 */
	if (state->content_protection != DRM_MODE_CONTENT_PROTECTION_ENABLED)
		return true;

	/*
	 * Handles:	DESIRED -> ENABLED
	 */
	return false;
}

#endif
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

	DC_LOG_PFLIP("crtc:%d, pflip_stat:AMDGPU_FLIP_SUBMITTED\n",
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
		&vrr_infopacket,
		pack_sdp_v1_3);

	new_crtc_state->freesync_vrr_info_changed |=
		(memcmp(&new_crtc_state->vrr_infopacket,
			&vrr_infopacket,
			sizeof(vrr_infopacket)) != 0);

	acrtc->dm_irq_params.vrr_params = vrr_params;
	new_crtc_state->vrr_infopacket = vrr_infopacket;

	new_stream->vrr_infopacket = vrr_infopacket;

	if (new_crtc_state->freesync_vrr_info_changed)
		DRM_DEBUG_KMS("VRR packet update: crtc=%u enabled=%d state=%d",
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
		WARN_ON(dm_set_vupdate_irq(new_state->base.crtc, true) != 0);
		WARN_ON(drm_crtc_vblank_get(new_state->base.crtc) != 0);
		DRM_DEBUG_DRIVER("%s: crtc=%u VRR off->on: Get vblank ref\n",
				 __func__, new_state->base.crtc->base.id);
	} else if (old_vrr_active && !new_vrr_active) {
		/* Transition VRR active -> inactive:
		 * Allow vblank irq disable again for fixed refresh rate.
		 */
		WARN_ON(dm_set_vupdate_irq(new_state->base.crtc, false) != 0);
		drm_crtc_vblank_put(new_state->base.crtc);
		DRM_DEBUG_DRIVER("%s: crtc=%u VRR on->off: Drop vblank ref\n",
				 __func__, new_state->base.crtc->base.id);
	}
}

static void amdgpu_dm_commit_cursors(struct drm_atomic_state *state)
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
			handle_cursor_update(plane, old_plane_state);
}

static inline uint32_t get_mem_type(struct drm_framebuffer *fb)
{
	struct amdgpu_bo *abo = gem_to_amdgpu_bo(fb->obj[0]);

	return abo->tbo.resource ? abo->tbo.resource->mem_type : 0;
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
	unsigned long flags;
	uint32_t target_vblank, last_flip_vblank;
	bool vrr_active = amdgpu_dm_vrr_active(acrtc_state);
	bool cursor_update = false;
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
		struct amdgpu_framebuffer *afb = (struct amdgpu_framebuffer *)fb;
		bool plane_needs_flip;
		struct dc_plane_state *dc_plane;
		struct dm_plane_state *dm_new_plane_state = to_dm_plane_state(new_plane_state);

		/* Cursor plane is handled after stream updates */
		if (plane->type == DRM_PLANE_TYPE_CURSOR) {
			if ((fb && crtc == pcrtc) ||
			    (old_plane_state->fb && old_plane_state->crtc == pcrtc))
				cursor_update = true;

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
			bundle->surface_updates[planes_count].gamma = dc_plane->gamma_correction;
			bundle->surface_updates[planes_count].in_transfer_func = dc_plane->in_transfer_func;
			bundle->surface_updates[planes_count].gamut_remap_matrix = &dc_plane->gamut_remap_matrix;
		}

		fill_dc_scaling_info(dm->adev, new_plane_state,
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
			afb->tmz_surface, false);

		drm_dbg_state(state->dev, "plane: id=%d dcc_en=%d\n",
				 new_plane_state->plane->index,
				 bundle->plane_infos[planes_count].dcc.enable);

		bundle->surface_updates[planes_count].plane_info =
			&bundle->plane_infos[planes_count];

		if (acrtc_state->stream->link->psr_settings.psr_feature_enabled)
			fill_dc_dirty_rects(plane, old_plane_state,
					    new_plane_state, new_crtc_state,
					    &bundle->flip_addrs[planes_count]);

		/*
		 * Only allow immediate flips for fast updates that don't
		 * change memory domain, FB pitch, DCC state, rotation or
		 * mirroring.
		 */
		bundle->flip_addrs[planes_count].flip_immediate =
			crtc->state->async_flip &&
			acrtc_state->update_type == UPDATE_TYPE_FAST &&
			get_mem_type(old_plane_state->fb) == get_mem_type(fb);

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

		/**
		 * Prepare the flip event for the pageflip interrupt to handle.
		 *
		 * This only works in the case where we've already turned on the
		 * appropriate hardware blocks (eg. HUBP) so in the transition case
		 * from 0 -> n planes we have to skip a hardware generated event
		 * and rely on sending it from software.
		 */
		if (acrtc_attach->base.state->event &&
		    acrtc_state->active_planes > 0) {
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
	} else if (cursor_update && acrtc_state->active_planes > 0 &&
		   acrtc_attach->base.state->event) {
		drm_crtc_vblank_get(pcrtc);

		spin_lock_irqsave(&pcrtc->dev->event_lock, flags);

		acrtc_attach->event = acrtc_attach->base.state->event;
		acrtc_attach->base.state->event = NULL;

		spin_unlock_irqrestore(&pcrtc->dev->event_lock, flags);
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
				acrtc_state->stream->out_transfer_func;
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
		if ((acrtc_state->update_type > UPDATE_TYPE_FAST) &&
				acrtc_state->stream->link->psr_settings.psr_allow_active)
			amdgpu_dm_psr_disable(acrtc_state->stream);

		update_planes_and_stream_adapter(dm->dc,
					 acrtc_state->update_type,
					 planes_count,
					 acrtc_state->stream,
					 &bundle->stream_update,
					 bundle->surface_updates);

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

		if ((acrtc_state->update_type > UPDATE_TYPE_FAST) &&
				acrtc_state->stream->link->psr_settings.psr_version != DC_PSR_VERSION_UNSUPPORTED &&
				!acrtc_state->stream->link->psr_settings.psr_feature_enabled)
			amdgpu_dm_link_setup_psr(acrtc_state->stream);

		/* Decrement skip count when PSR is enabled and we're doing fast updates. */
		if (acrtc_state->update_type == UPDATE_TYPE_FAST &&
		    acrtc_state->stream->link->psr_settings.psr_feature_enabled) {
			struct amdgpu_dm_connector *aconn =
				(struct amdgpu_dm_connector *)acrtc_state->stream->dm_stream_context;

			if (aconn->psr_skip_count > 0)
				aconn->psr_skip_count--;

			/* Allow PSR when skip count is 0. */
			acrtc_attach->dm_irq_params.allow_psr_entry = !aconn->psr_skip_count;

			/*
			 * If sink supports PSR SU, there is no need to rely on
			 * a vblank event disable request to enable PSR. PSR SU
			 * can be enabled immediately once OS demonstrates an
			 * adequate number of fast atomic commits to notify KMD
			 * of update events. See `vblank_control_worker()`.
			 */
			if (acrtc_state->stream->link->psr_settings.psr_version >= DC_PSR_VERSION_SU_1 &&
			    acrtc_attach->dm_irq_params.allow_psr_entry &&
			    !acrtc_state->stream->link->psr_settings.psr_allow_active)
				amdgpu_dm_psr_enable(acrtc_state->stream);
		} else {
			acrtc_attach->dm_irq_params.allow_psr_entry = false;
		}

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

static void amdgpu_dm_commit_audio(struct drm_device *dev,
				   struct drm_atomic_state *state)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_state *old_con_state, *new_con_state;
	struct drm_crtc_state *new_crtc_state;
	struct dm_crtc_state *new_dm_crtc_state;
	const struct dc_stream_status *status;
	int i, inst;

	/* Notify device removals. */
	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		if (old_con_state->crtc != new_con_state->crtc) {
			/* CRTC changes require notification. */
			goto notify;
		}

		if (!new_con_state->crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(
			state, new_con_state->crtc);

		if (!new_crtc_state)
			continue;

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

	notify:
		aconnector = to_amdgpu_dm_connector(connector);

		mutex_lock(&adev->dm.audio_lock);
		inst = aconnector->audio_inst;
		aconnector->audio_inst = -1;
		mutex_unlock(&adev->dm.audio_lock);

		amdgpu_dm_audio_eld_notify(adev, inst);
	}

	/* Notify audio device additions. */
	for_each_new_connector_in_state(state, connector, new_con_state, i) {
		if (!new_con_state->crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(
			state, new_con_state->crtc);

		if (!new_crtc_state)
			continue;

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

		new_dm_crtc_state = to_dm_crtc_state(new_crtc_state);
		if (!new_dm_crtc_state->stream)
			continue;

		status = dc_stream_get_status(new_dm_crtc_state->stream);
		if (!status)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);

		mutex_lock(&adev->dm.audio_lock);
		inst = status->audio_inst;
		aconnector->audio_inst = inst;
		mutex_unlock(&adev->dm.audio_lock);

		amdgpu_dm_audio_eld_notify(adev, inst);
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
	struct amdgpu_device *adev = drm_to_adev(dev);
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
	bool mode_set_reset_required = false;
	int r;

	trace_amdgpu_dm_atomic_commit_tail_begin(state);

	r = drm_atomic_helper_wait_for_fences(dev, state, false);
	if (unlikely(r))
		DRM_ERROR("Waiting for fences timed out!");

	drm_atomic_helper_update_legacy_modeset_state(dev, state);
	drm_dp_mst_atomic_wait_for_dependencies(state);

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

	for_each_oldnew_crtc_in_state (state, crtc, old_crtc_state,
				       new_crtc_state, i) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		if (old_crtc_state->active &&
		    (!new_crtc_state->active ||
		     drm_atomic_crtc_needs_modeset(new_crtc_state))) {
			manage_dm_interrupts(adev, acrtc, false);
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

		/* Disable cursor if disabling crtc */
		if (old_crtc_state->active && !new_crtc_state->active) {
			struct dc_cursor_position position;

			memset(&position, 0, sizeof(position));
			mutex_lock(&dm->dc_lock);
			dc_stream_set_cursor_position(dm_old_crtc_state->stream, &position);
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

		if (modeset_required(new_crtc_state, dm_new_crtc_state->stream, dm_old_crtc_state->stream)) {

			DRM_DEBUG_ATOMIC("Atomic commit: SET crtc id %d: [%p]\n", acrtc->crtc_id, acrtc);

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
			mode_set_reset_required = true;
		} else if (modereset_required(new_crtc_state)) {
			DRM_DEBUG_ATOMIC("Atomic commit: RESET. crtc id %d:[%p]\n", acrtc->crtc_id, acrtc);
			/* i.e. reset mode */
			if (dm_old_crtc_state->stream)
				remove_stream(adev, acrtc, dm_old_crtc_state->stream);

			mode_set_reset_required = true;
		}
	} /* for_each_crtc_in_state() */

	if (dc_state) {
		/* if there mode set or reset, disable eDP PSR */
		if (mode_set_reset_required) {
			if (dm->vblank_control_workqueue)
				flush_workqueue(dm->vblank_control_workqueue);

			amdgpu_dm_psr_disable_all(dm);
		}

		dm_enable_per_frame_crtc_master_sync(dc_state);
		mutex_lock(&dm->dc_lock);
		WARN_ON(!dc_commit_state(dm->dc, dc_state));

		/* Allow idle optimization when vblank count is 0 for display off */
		if (dm->active_vblank_irq_count == 0)
			dc_allow_idle_optimizations(dm->dc, true);
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
#ifdef CONFIG_DRM_AMD_DC_HDCP
	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		struct dm_connector_state *dm_new_con_state = to_dm_connector_state(new_con_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(dm_new_con_state->base.crtc);
		struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);

		new_crtc_state = NULL;

		if (acrtc)
			new_crtc_state = drm_atomic_get_new_crtc_state(state, &acrtc->base);

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (dm_new_crtc_state && dm_new_crtc_state->stream == NULL &&
		    connector->state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED) {
			hdcp_reset_display(adev->dm.hdcp_workqueue, aconnector->dc_link->link_index);
			new_con_state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
			dm_new_con_state->update_hdcp = true;
			continue;
		}

		if (is_content_protection_different(new_con_state, old_con_state, connector, adev->dm.hdcp_workqueue))
			hdcp_update_display(
				adev->dm.hdcp_workqueue, aconnector->dc_link->link_index, aconnector,
				new_con_state->hdcp_content_type,
				new_con_state->content_protection == DRM_MODE_CONTENT_PROTECTION_DESIRED);
	}
#endif

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
			!drm_connector_atomic_hdr_metadata_equal(old_con_state, new_con_state);

		if (!scaling_changed && !abm_changed && !hdr_changed)
			continue;

		stream_update.stream = dm_new_crtc_state->stream;
		if (scaling_changed) {
			update_stream_scaling_settings(&dm_new_con_state->base.crtc->mode,
					dm_new_con_state, dm_new_crtc_state->stream);

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

		if (WARN_ON(!status))
			continue;

		WARN_ON(!status->plane_count);

		/*
		 * TODO: DC refuses to perform stream updates without a dc_surface_update.
		 * Here we create an empty update on each plane.
		 * To fix this, DC should permit updating only stream properties.
		 */
		for (j = 0; j < status->plane_count; j++)
			dummy_updates[j].surface = status->plane_states[0];


		mutex_lock(&dm->dc_lock);
		dc_update_planes_and_stream(dm->dc,
					    dummy_updates,
					    status->plane_count,
					    dm_new_crtc_state->stream,
					    &stream_update);
		mutex_unlock(&dm->dc_lock);
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
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
		struct crc_rd_work *crc_rd_wrk;
#endif
#endif
		/* Count number of newly disabled CRTCs for dropping PM refs later. */
		if (old_crtc_state->active && !new_crtc_state->active)
			crtc_disable_count++;

		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		/* For freesync config update on crtc state and params for irq */
		update_stream_irq_parameters(dm, dm_new_crtc_state);

#ifdef CONFIG_DEBUG_FS
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
		crc_rd_wrk = dm->crc_rd_wrk;
#endif
		spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
		cur_crc_src = acrtc->dm_irq_params.crc_src;
		spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
#endif

		if (new_crtc_state->active &&
		    (!old_crtc_state->active ||
		     drm_atomic_crtc_needs_modeset(new_crtc_state))) {
			dc_stream_retain(dm_new_crtc_state->stream);
			acrtc->dm_irq_params.stream = dm_new_crtc_state->stream;
			manage_dm_interrupts(adev, acrtc, true);
		}
		/* Handle vrr on->off / off->on transitions */
		amdgpu_dm_handle_vrr_transition(dm_old_crtc_state, dm_new_crtc_state);

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
					spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
					acrtc->dm_irq_params.crc_window.update_win = true;
					acrtc->dm_irq_params.crc_window.skip_frame_cnt = 2;
					spin_lock_irq(&crc_rd_wrk->crc_rd_work_lock);
					crc_rd_wrk->crtc = crtc;
					spin_unlock_irq(&crc_rd_wrk->crc_rd_work_lock);
					spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
				}
#endif
				if (amdgpu_dm_crtc_configure_crc_source(
					crtc, dm_new_crtc_state, cur_crc_src))
					DRM_DEBUG_DRIVER("Failed to configure crc source");
			}
		}
#endif
	}

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, j)
		if (new_crtc_state->async_flip)
			wait_for_vblank = false;

	/* update planes when needed per crtc*/
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, j) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

		if (dm_new_crtc_state->stream)
			amdgpu_dm_commit_planes(state, dc_state, dev,
						dm, crtc, wait_for_vblank);
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

	/* return the stolen vga memory back to VRAM */
	if (!adev->mman.keep_stolen_vga_memory)
		amdgpu_bo_free_kernel(&adev->mman.stolen_vga_memory, NULL, NULL);
	amdgpu_bo_free_kernel(&adev->mman.stolen_extended_memory, NULL, NULL);

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
		goto out;

	/* Attach crtc to drm_atomic_state*/
	crtc_state = drm_atomic_get_crtc_state(state, &disconnected_acrtc->base);

	ret = PTR_ERR_OR_ZERO(crtc_state);
	if (ret)
		goto out;

	/* force a restore */
	crtc_state->mode_changed = true;

	/* Attach plane to drm_atomic_state */
	plane_state = drm_atomic_get_plane_state(state, plane);

	ret = PTR_ERR_OR_ZERO(plane_state);
	if (ret)
		goto out;

	/* Call commit internally with the state we just constructed */
	ret = drm_atomic_commit(state);

out:
	drm_atomic_state_put(state);
	if (ret)
		DRM_ERROR("Restoring old state failed with %i\n", ret);

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
	bool fs_vid_mode = false;

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

static bool
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

static void set_freesync_fixed_config(struct dm_crtc_state *dm_new_crtc_state) {
	uint64_t num, den, res;
	struct drm_crtc_state *new_crtc_state = &dm_new_crtc_state->base;

	dm_new_crtc_state->freesync_config.state = VRR_STATE_ACTIVE_FIXED;

	num = (unsigned long long)new_crtc_state->mode.clock * 1000 * 1000000;
	den = (unsigned long long)new_crtc_state->mode.htotal *
	      (unsigned long long)new_crtc_state->mode.vtotal;

	res = div_u64(num, den);
	dm_new_crtc_state->freesync_config.fixed_refresh_in_uhz = res;
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

		new_stream = create_validate_stream_for_sink(aconnector,
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

		/*
		 * TODO: Check VSDB bits to decide whether this should
		 * be enabled or not.
		 */
		new_stream->triggered_crtc_reset.enabled =
			dm->force_timing_sync;

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
		if (amdgpu_freesync_vid_mode &&
		    dm_new_crtc_state->stream &&
		    is_timing_unchanged_for_freesync(new_crtc_state, old_crtc_state))
			goto skip_modeset;

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

	drm_dbg_state(state->dev,
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

		/* Unset freesync video if it was active before */
		if (dm_old_crtc_state->freesync_config.state == VRR_STATE_ACTIVE_FIXED) {
			dm_new_crtc_state->freesync_config.state = VRR_STATE_INACTIVE;
			dm_new_crtc_state->freesync_config.fixed_refresh_in_uhz = 0;
		}

		/* Now check if we should set freesync video mode */
		if (amdgpu_freesync_vid_mode && dm_new_crtc_state->stream &&
		    is_timing_unchanged_for_freesync(new_crtc_state,
						     old_crtc_state)) {
			new_crtc_state->mode_changed = false;
			DRM_DEBUG_DRIVER(
				"Mode change not required for front porch change, "
				"setting mode_changed to %d",
				new_crtc_state->mode_changed);

			set_freesync_fixed_config(dm_new_crtc_state);

			goto skip_modeset;
		} else if (amdgpu_freesync_vid_mode && aconnector &&
			   is_freesync_video_mode(&new_crtc_state->mode,
						  aconnector)) {
			struct drm_display_mode *high_mode;

			high_mode = get_highest_refresh_rate_mode(aconnector, false);
			if (!drm_mode_equal(&new_crtc_state->mode, high_mode)) {
				set_freesync_fixed_config(dm_new_crtc_state);
			}
		}

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
		if (!aconnector)
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

			DRM_DEBUG_ATOMIC("Enabling DRM crtc: %d\n",
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
	if (!(enable && aconnector && new_crtc_state->active))
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
		ret = amdgpu_dm_update_crtc_color_mgmt(dm_new_crtc_state);
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

static bool should_reset_plane(struct drm_atomic_state *state,
			       struct drm_plane *plane,
			       struct drm_plane_state *old_plane_state,
			       struct drm_plane_state *new_plane_state)
{
	struct drm_plane *other;
	struct drm_plane_state *old_other_state, *new_other_state;
	struct drm_crtc_state *new_crtc_state;
	int i;

	/*
	 * TODO: Remove this hack once the checks below are sufficient
	 * enough to determine when we need to reset all the planes on
	 * the stream.
	 */
	if (state->allow_modeset)
		return true;

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
		struct amdgpu_framebuffer *old_afb, *new_afb;
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
		DRM_DEBUG_ATOMIC("Bad cursor FB size %dx%d\n",
				 new_plane_state->fb->width,
				 new_plane_state->fb->height);
		return -EINVAL;
	}
	if (new_plane_state->src_w != fb->width << 16 ||
	    new_plane_state->src_h != fb->height << 16) {
		DRM_DEBUG_ATOMIC("Cropping not supported for cursor plane\n");
		return -EINVAL;
	}

	/* Pitch in pixels */
	pitch = fb->pitches[0] / fb->format->cpp[0];

	if (fb->width != pitch) {
		DRM_DEBUG_ATOMIC("Cursor FB width %d doesn't match pitch %d",
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
		DRM_DEBUG_ATOMIC("Bad cursor FB pitch %d px\n", pitch);
		return -EINVAL;
	}

	/* Core DRM takes care of checking FB modifiers, so we only need to
	 * check tiling flags when the FB doesn't have a modifier. */
	if (!(fb->flags & DRM_MODE_FB_MODIFIERS)) {
		if (adev->family < AMDGPU_FAMILY_AI) {
			linear = AMDGPU_TILING_GET(afb->tiling_flags, ARRAY_MODE) != DC_ARRAY_2D_TILED_THIN1 &&
			         AMDGPU_TILING_GET(afb->tiling_flags, ARRAY_MODE) != DC_ARRAY_1D_TILED_THIN1 &&
				 AMDGPU_TILING_GET(afb->tiling_flags, MICRO_TILE_MODE) == 0;
		} else {
			linear = AMDGPU_TILING_GET(afb->tiling_flags, SWIZZLE_MODE) == 0;
		}
		if (!linear) {
			DRM_DEBUG_ATOMIC("Cursor FB not linear");
			return -EINVAL;
		}
	}

	return 0;
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
	struct amdgpu_crtc *new_acrtc;
	bool needs_reset;
	int ret = 0;


	new_plane_crtc = new_plane_state->crtc;
	old_plane_crtc = old_plane_state->crtc;
	dm_new_plane_state = to_dm_plane_state(new_plane_state);
	dm_old_plane_state = to_dm_plane_state(old_plane_state);

	if (plane->type == DRM_PLANE_TYPE_CURSOR) {
		if (!enable || !new_plane_crtc ||
			drm_atomic_plane_disabling(plane->state, new_plane_state))
			return 0;

		new_acrtc = to_amdgpu_crtc(new_plane_crtc);

		if (new_plane_state->src_x != 0 || new_plane_state->src_y != 0) {
			DRM_DEBUG_ATOMIC("Cropping not supported for cursor plane\n");
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

		ret = dm_plane_helper_check_state(new_plane_state, new_crtc_state);
		if (ret)
			return ret;

		WARN_ON(dm_new_plane_state->dc_state);

		dc_new_plane_state = dc_create_plane_state(dc);
		if (!dc_new_plane_state)
			return -ENOMEM;

		DRM_DEBUG_ATOMIC("Enabling DRM plane: %d on DRM crtc %d\n",
				 plane->base.id, new_plane_crtc->base.id);

		ret = fill_dc_plane_attributes(
			drm_to_adev(new_plane_crtc->dev),
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

		dm_new_crtc_state->mpo_requested |= (plane->type == DRM_PLANE_TYPE_OVERLAY);

		/* Tell DC to do a full surface update every time there
		 * is a plane change. Inefficient, but works for now.
		 */
		dm_new_plane_state->dc_state->update_flags.bits.full_update = 1;

		*lock_and_validation_needed = true;
	}


	return ret;
}

static void dm_get_oriented_plane_size(struct drm_plane_state *plane_state,
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

static int dm_check_crtc_cursor(struct drm_atomic_state *state,
				struct drm_crtc *crtc,
				struct drm_crtc_state *new_crtc_state)
{
	struct drm_plane *cursor = crtc->cursor, *underlying;
	struct drm_plane_state *new_cursor_state, *new_underlying_state;
	int i;
	int cursor_scale_w, cursor_scale_h, underlying_scale_w, underlying_scale_h;
	int cursor_src_w, cursor_src_h;
	int underlying_src_w, underlying_src_h;

	/* On DCE and DCN there is no dedicated hardware cursor plane. We get a
	 * cursor per pipe but it's going to inherit the scaling and
	 * positioning from the underlying pipe. Check the cursor plane's
	 * blending properties match the underlying planes'. */

	new_cursor_state = drm_atomic_get_new_plane_state(state, cursor);
	if (!new_cursor_state || !new_cursor_state->fb) {
		return 0;
	}

	dm_get_oriented_plane_size(new_cursor_state, &cursor_src_w, &cursor_src_h);
	cursor_scale_w = new_cursor_state->crtc_w * 1000 / cursor_src_w;
	cursor_scale_h = new_cursor_state->crtc_h * 1000 / cursor_src_h;

	for_each_new_plane_in_state_reverse(state, underlying, new_underlying_state, i) {
		/* Narrow down to non-cursor planes on the same CRTC as the cursor */
		if (new_underlying_state->crtc != crtc || underlying == crtc->cursor)
			continue;

		/* Ignore disabled planes */
		if (!new_underlying_state->fb)
			continue;

		dm_get_oriented_plane_size(new_underlying_state,
					   &underlying_src_w, &underlying_src_h);
		underlying_scale_w = new_underlying_state->crtc_w * 1000 / underlying_src_w;
		underlying_scale_h = new_underlying_state->crtc_h * 1000 / underlying_src_h;

		if (cursor_scale_w != underlying_scale_w ||
		    cursor_scale_h != underlying_scale_h) {
			drm_dbg_atomic(crtc->dev,
				       "Cursor [PLANE:%d:%s] scaling doesn't match underlying [PLANE:%d:%s]\n",
				       cursor->base.id, cursor->name, underlying->base.id, underlying->name);
			return -EINVAL;
		}

		/* If this plane covers the whole CRTC, no need to check planes underneath */
		if (new_underlying_state->crtc_x <= 0 &&
		    new_underlying_state->crtc_y <= 0 &&
		    new_underlying_state->crtc_x + new_underlying_state->crtc_w >= new_crtc_state->mode.hdisplay &&
		    new_underlying_state->crtc_y + new_underlying_state->crtc_h >= new_crtc_state->mode.vdisplay)
			break;
	}

	return 0;
}

#if defined(CONFIG_DRM_AMD_DC_DCN)
static int add_affected_mst_dsc_crtcs(struct drm_atomic_state *state, struct drm_crtc *crtc)
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

		aconnector = to_amdgpu_dm_connector(connector);
		if (!aconnector->port || !aconnector->mst_port)
			aconnector = NULL;
		else
			break;
	}

	if (!aconnector)
		return 0;

	return drm_dp_mst_add_affected_dsc_crtcs(state, &aconnector->mst_port->mst_mgr);
}
#endif

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
				  struct drm_atomic_state *state)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dm_atomic_state *dm_state = NULL;
	struct dc *dc = adev->dm.dc;
	struct drm_connector *connector;
	struct drm_connector_state *old_con_state, *new_con_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	enum dc_status status;
	int ret, i;
	bool lock_and_validation_needed = false;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;
#if defined(CONFIG_DRM_AMD_DC_DCN)
	struct drm_dp_mst_topology_mgr *mgr;
	struct drm_dp_mst_topology_state *mst_state;
	struct dsc_mst_fairness_vars vars[MAX_PIPES];
#endif

	trace_amdgpu_dm_atomic_check_begin(state);

	ret = drm_atomic_helper_check_modeset(dev, state);
	if (ret) {
		DRM_DEBUG_DRIVER("drm_atomic_helper_check_modeset() failed\n");
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
			DRM_DEBUG_DRIVER("drm_atomic_get_crtc_state() failed\n");
			ret = PTR_ERR(new_crtc_state);
			goto fail;
		}

		if (dm_old_con_state->abm_level != dm_new_con_state->abm_level ||
		    dm_old_con_state->scaling != dm_new_con_state->scaling)
			new_crtc_state->connectors_changed = true;
	}

#if defined(CONFIG_DRM_AMD_DC_DCN)
	if (dc_resource_is_dsc_encoding_supported(dc)) {
		for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
			if (drm_atomic_crtc_needs_modeset(new_crtc_state)) {
				ret = add_affected_mst_dsc_crtcs(state, crtc);
				if (ret) {
					DRM_DEBUG_DRIVER("add_affected_mst_dsc_crtcs() failed\n");
					goto fail;
				}
			}
		}
	}
#endif
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state) &&
		    !new_crtc_state->color_mgmt_changed &&
		    old_crtc_state->vrr_enabled == new_crtc_state->vrr_enabled &&
			dm_old_crtc_state->dsc_force_changed == false)
			continue;

		ret = amdgpu_dm_verify_lut_sizes(new_crtc_state);
		if (ret) {
			DRM_DEBUG_DRIVER("amdgpu_dm_verify_lut_sizes() failed\n");
			goto fail;
		}

		if (!new_crtc_state->enable)
			continue;

		ret = drm_atomic_add_affected_connectors(state, crtc);
		if (ret) {
			DRM_DEBUG_DRIVER("drm_atomic_add_affected_connectors() failed\n");
			goto fail;
		}

		ret = drm_atomic_add_affected_planes(state, crtc);
		if (ret) {
			DRM_DEBUG_DRIVER("drm_atomic_add_affected_planes() failed\n");
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
				DRM_DEBUG_DRIVER("new_plane_state is BAD\n");
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
		drm_dbg(dev, "drm_atomic_normalize_zpos() failed\n");
		goto fail;
	}

	/* Remove exiting planes if they are modified */
	for_each_oldnew_plane_in_state_reverse(state, plane, old_plane_state, new_plane_state, i) {
		ret = dm_update_plane_state(dc, state, plane,
					    old_plane_state,
					    new_plane_state,
					    false,
					    &lock_and_validation_needed);
		if (ret) {
			DRM_DEBUG_DRIVER("dm_update_plane_state() failed\n");
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
			DRM_DEBUG_DRIVER("DISABLE: dm_update_crtc_state() failed\n");
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
			DRM_DEBUG_DRIVER("ENABLE: dm_update_crtc_state() failed\n");
			goto fail;
		}
	}

	/* Add new/modified planes */
	for_each_oldnew_plane_in_state_reverse(state, plane, old_plane_state, new_plane_state, i) {
		ret = dm_update_plane_state(dc, state, plane,
					    old_plane_state,
					    new_plane_state,
					    true,
					    &lock_and_validation_needed);
		if (ret) {
			DRM_DEBUG_DRIVER("dm_update_plane_state() failed\n");
			goto fail;
		}
	}

#if defined(CONFIG_DRM_AMD_DC_DCN)
	if (dc_resource_is_dsc_encoding_supported(dc)) {
		ret = pre_validate_dsc(state, &dm_state, vars);
		if (ret != 0)
			goto fail;
	}
#endif

	/* Run this here since we want to validate the streams we created */
	ret = drm_atomic_helper_check_planes(dev, state);
	if (ret) {
		DRM_DEBUG_DRIVER("drm_atomic_helper_check_planes() failed\n");
		goto fail;
	}

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
		if (dm_new_crtc_state->mpo_requested)
			DRM_DEBUG_DRIVER("MPO enablement requested on crtc:[%p]\n", crtc);
	}

	/* Check cursor planes scaling */
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		ret = dm_check_crtc_cursor(state, crtc, new_crtc_state);
		if (ret) {
			DRM_DEBUG_DRIVER("dm_check_crtc_cursor() failed\n");
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

#if defined(CONFIG_DRM_AMD_DC_DCN)
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
#endif

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
			DRM_DEBUG_DRIVER("dm_atomic_get_state() failed\n");
			goto fail;
		}

		ret = do_aquire_global_lock(dev, state);
		if (ret) {
			DRM_DEBUG_DRIVER("do_aquire_global_lock() failed\n");
			goto fail;
		}

#if defined(CONFIG_DRM_AMD_DC_DCN)
		ret = compute_mst_dsc_configs_for_state(state, dm_state->context, vars);
		if (ret) {
			DRM_DEBUG_DRIVER("compute_mst_dsc_configs_for_state() failed\n");
			ret = -EINVAL;
			goto fail;
		}

		ret = dm_update_mst_vcpi_slots_for_dsc(state, dm_state->context, vars);
		if (ret) {
			DRM_DEBUG_DRIVER("dm_update_mst_vcpi_slots_for_dsc() failed\n");
			goto fail;
		}
#endif

		/*
		 * Perform validation of MST topology in the state:
		 * We need to perform MST atomic check before calling
		 * dc_validate_global_state(), or there is a chance
		 * to get stuck in an infinite loop and hang eventually.
		 */
		ret = drm_dp_mst_atomic_check(state);
		if (ret) {
			DRM_DEBUG_DRIVER("drm_dp_mst_atomic_check() failed\n");
			goto fail;
		}
		status = dc_validate_global_state(dc, dm_state->context, true);
		if (status != DC_OK) {
			DRM_DEBUG_DRIVER("DC global validation failure: %s (%d)",
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
						state->private_objs[i].state);

				/* If i is not at the end of the array then the
				 * last element needs to be moved to where i was
				 * before the array can safely be truncated.
				 */
				if (i != j)
					state->private_objs[i] =
						state->private_objs[j];

				state->private_objs[j].ptr = NULL;
				state->private_objs[j].state = NULL;
				state->private_objs[j].old_state = NULL;
				state->private_objs[j].new_state = NULL;

				state->num_private_objs = j;
				break;
			}
		}
	}

	/* Store the overall update type for use later in atomic check. */
	for_each_new_crtc_in_state (state, crtc, new_crtc_state, i) {
		struct dm_crtc_state *dm_new_crtc_state =
			to_dm_crtc_state(new_crtc_state);

		dm_new_crtc_state->update_type = lock_and_validation_needed ?
							 UPDATE_TYPE_FULL :
							 UPDATE_TYPE_FAST;
	}

	/* Must be success */
	WARN_ON(ret);

	trace_amdgpu_dm_atomic_check_finish(state, ret);

	return ret;

fail:
	if (ret == -EDEADLK)
		DRM_DEBUG_DRIVER("Atomic check stopped to avoid deadlock.\n");
	else if (ret == -EINTR || ret == -EAGAIN || ret == -ERESTARTSYS)
		DRM_DEBUG_DRIVER("Atomic check stopped due to signal.\n");
	else
		DRM_DEBUG_DRIVER("Atomic check failed with err: %d \n", ret);

	trace_amdgpu_dm_atomic_check_finish(state, ret);

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

static bool dm_edid_parser_send_cea(struct amdgpu_display_manager *dm,
		unsigned int offset,
		unsigned int total_length,
		uint8_t *data,
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

	res = dc_dmub_srv_cmd_with_reply_data(dm->dc->ctx->dmub_srv, &cmd);
	if (!res) {
		DRM_ERROR("EDID CEA parser failed\n");
		return false;
	}

	output = &cmd.edid_cea.data.output;

	if (output->type == DMUB_CMD__EDID_CEA_ACK) {
		if (!output->ack.success) {
			DRM_ERROR("EDID CEA ack failed at offset %d\n",
					output->ack.offset);
		}
	} else if (output->type == DMUB_CMD__EDID_CEA_AMD_VSDB) {
		if (!output->amd_vsdb.vsdb_found)
			return false;

		vsdb->freesync_supported = output->amd_vsdb.freesync_supported;
		vsdb->amd_vsdb_version = output->amd_vsdb.amd_vsdb_version;
		vsdb->min_refresh_rate_hz = output->amd_vsdb.min_frame_rate;
		vsdb->max_refresh_rate_hz = output->amd_vsdb.max_frame_rate;
	} else {
		DRM_WARN("Unknown EDID CEA parser results\n");
		return false;
	}

	return true;
}

static bool parse_edid_cea_dmcu(struct amdgpu_display_manager *dm,
		uint8_t *edid_ext, int len,
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
		uint8_t *edid_ext, int len,
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
		uint8_t *edid_ext, int len,
		struct amdgpu_hdmi_vsdb_info *vsdb_info)
{
	struct amdgpu_device *adev = drm_to_adev(aconnector->base.dev);

	if (adev->dm.dmub_srv)
		return parse_edid_cea_dmub(&adev->dm, edid_ext, len, vsdb_info);
	else
		return parse_edid_cea_dmcu(&adev->dm, edid_ext, len, vsdb_info);
}

static int parse_hdmi_amd_vsdb(struct amdgpu_dm_connector *aconnector,
		struct edid *edid, struct amdgpu_hdmi_vsdb_info *vsdb_info)
{
	uint8_t *edid_ext = NULL;
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
 * @edid: EDID from monitor
 *
 * Amdgpu supports Freesync in DP and HDMI displays, and it is required to keep
 * track of some of the display information in the internal data struct used by
 * amdgpu_dm. This function checks which type of connector we need to set the
 * FreeSync parameters.
 */
void amdgpu_dm_update_freesync_caps(struct drm_connector *connector,
				    struct edid *edid)
{
	int i = 0;
	struct detailed_timing *timing;
	struct detailed_non_pixel *data;
	struct detailed_data_monitor_range *range;
	struct amdgpu_dm_connector *amdgpu_dm_connector =
			to_amdgpu_dm_connector(connector);
	struct dm_connector_state *dm_con_state = NULL;
	struct dc_sink *sink;

	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_hdmi_vsdb_info vsdb_info = {0};
	bool freesync_capable = false;

	if (!connector->state) {
		DRM_ERROR("%s - Connector has no state", __func__);
		goto update;
	}

	sink = amdgpu_dm_connector->dc_sink ?
		amdgpu_dm_connector->dc_sink :
		amdgpu_dm_connector->dc_em_sink;

	if (!edid || !sink) {
		dm_con_state = to_dm_connector_state(connector->state);

		amdgpu_dm_connector->min_vfreq = 0;
		amdgpu_dm_connector->max_vfreq = 0;
		amdgpu_dm_connector->pixel_clock_mhz = 0;
		connector->display_info.monitor_range.min_vfreq = 0;
		connector->display_info.monitor_range.max_vfreq = 0;
		freesync_capable = false;

		goto update;
	}

	dm_con_state = to_dm_connector_state(connector->state);

	if (!adev->dm.freesync_module)
		goto update;

	if (sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT
		|| sink->sink_signal == SIGNAL_TYPE_EDP) {
		bool edid_check_required = false;

		if (edid) {
			edid_check_required = is_dp_capable_without_timing_msa(
						adev->dm.dc,
						amdgpu_dm_connector);
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

				connector->display_info.monitor_range.min_vfreq = range->min_vfreq;
				connector->display_info.monitor_range.max_vfreq = range->max_vfreq;

				break;
			}

			if (amdgpu_dm_connector->max_vfreq -
			    amdgpu_dm_connector->min_vfreq > 10) {

				freesync_capable = true;
			}
		}
	} else if (edid && sink->sink_signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		i = parse_hdmi_amd_vsdb(amdgpu_dm_connector, edid, &vsdb_info);
		if (i >= 0 && vsdb_info.freesync_supported) {
			timing  = &edid->detailed_timings[i];
			data    = &timing->data.other_data;

			amdgpu_dm_connector->min_vfreq = vsdb_info.min_refresh_rate_hz;
			amdgpu_dm_connector->max_vfreq = vsdb_info.max_refresh_rate_hz;
			if (amdgpu_dm_connector->max_vfreq - amdgpu_dm_connector->min_vfreq > 10)
				freesync_capable = true;

			connector->display_info.monitor_range.min_vfreq = vsdb_info.min_refresh_rate_hz;
			connector->display_info.monitor_range.max_vfreq = vsdb_info.max_refresh_rate_hz;
		}
	}

update:
	if (dm_con_state)
		dm_con_state->freesync_capable = freesync_capable;

	if (connector->vrr_capable_property)
		drm_connector_set_vrr_capable_property(connector,
						       freesync_capable);
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
		       uint32_t value, const char *func_name)
{
#ifdef DM_CHECK_ADDR_0
	if (address == 0) {
		DC_ERR("invalid register write. address = 0");
		return;
	}
#endif
	cgs_write_register(ctx->cgs_device, address, value);
	trace_amdgpu_dc_wreg(&ctx->perf_trace->write_count, address, value);
}

uint32_t dm_read_reg_func(const struct dc_context *ctx, uint32_t address,
			  const char *func_name)
{
	uint32_t value;
#ifdef DM_CHECK_ADDR_0
	if (address == 0) {
		DC_ERR("invalid register read; address = 0\n");
		return 0;
	}
#endif

	if (ctx->dmub_srv &&
	    ctx->dmub_srv->reg_helper_offload.gather_in_progress &&
	    !ctx->dmub_srv->reg_helper_offload.should_burst_write) {
		ASSERT(false);
		return 0;
	}

	value = cgs_read_register(ctx->cgs_device, address);

	trace_amdgpu_dc_rreg(&ctx->perf_trace->read_count, address, value);

	return value;
}

int amdgpu_dm_process_dmub_aux_transfer_sync(
		struct dc_context *ctx,
		unsigned int link_index,
		struct aux_payload *payload,
		enum aux_return_code_type *operation_result)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct dmub_notification *p_notify = adev->dm.dmub_notify;
	int ret = -1;

	mutex_lock(&adev->dm.dpia_aux_lock);
	if (!dc_process_dmub_aux_transfer_async(ctx->dc, link_index, payload)) {
		*operation_result = AUX_RET_ERROR_ENGINE_ACQUIRE;
		goto out;
 	}

	if (!wait_for_completion_timeout(&adev->dm.dmub_aux_transfer_done, 10 * HZ)) {
		DRM_ERROR("wait_for_completion_timeout timeout!");
		*operation_result = AUX_RET_ERROR_TIMEOUT;
		goto out;
	}

	if (p_notify->result != AUX_RET_SUCCESS) {
		/*
		 * Transient states before tunneling is enabled could
		 * lead to this error. We can ignore this for now.
		 */
		if (p_notify->result != AUX_RET_ERROR_PROTOCOL_ERROR) {
			DRM_WARN("DPIA AUX failed on 0x%x(%d), error %d\n",
					payload->address, payload->length,
					p_notify->result);
		}
		*operation_result = AUX_RET_ERROR_INVALID_REPLY;
		goto out;
	}


	payload->reply[0] = adev->dm.dmub_notify->aux_reply.command;
	if (!payload->write && p_notify->aux_reply.length &&
			(payload->reply[0] == AUX_TRANSACTION_REPLY_AUX_ACK)) {

		if (payload->length != p_notify->aux_reply.length) {
			DRM_WARN("invalid read length %d from DPIA AUX 0x%x(%d)!\n",
				p_notify->aux_reply.length,
					payload->address, payload->length);
			*operation_result = AUX_RET_ERROR_INVALID_REPLY;
			goto out;
		}

		memcpy(payload->data, p_notify->aux_reply.data,
				p_notify->aux_reply.length);
	}

	/* success */
	ret = p_notify->aux_reply.length;
	*operation_result = p_notify->result;
out:
	reinit_completion(&adev->dm.dmub_aux_transfer_done);
	mutex_unlock(&adev->dm.dpia_aux_lock);
	return ret;
}

int amdgpu_dm_process_dmub_set_config_sync(
		struct dc_context *ctx,
		unsigned int link_index,
		struct set_config_cmd_payload *payload,
		enum set_config_status *operation_result)
{
	struct amdgpu_device *adev = ctx->driver_context;
	bool is_cmd_complete;
	int ret;

	mutex_lock(&adev->dm.dpia_aux_lock);
	is_cmd_complete = dc_process_dmub_set_config_async(ctx->dc,
			link_index, payload, adev->dm.dmub_notify);

	if (is_cmd_complete || wait_for_completion_timeout(&adev->dm.dmub_aux_transfer_done, 10 * HZ)) {
		ret = 0;
		*operation_result = adev->dm.dmub_notify->sc_status;
	} else {
		DRM_ERROR("wait_for_completion_timeout timeout!");
		ret = -1;
		*operation_result = SET_CONFIG_UNKNOWN_ERROR;
	}

	if (!is_cmd_complete)
		reinit_completion(&adev->dm.dmub_aux_transfer_done);
	mutex_unlock(&adev->dm.dpia_aux_lock);
	return ret;
}

/*
 * Check whether seamless boot is supported.
 *
 * So far we only support seamless boot on CHIP_VANGOGH.
 * If everything goes well, we may consider expanding
 * seamless boot to other ASICs.
 */
bool check_seamless_boot_capability(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_VANGOGH:
		if (!adev->mman.keep_stolen_vga_memory)
			return true;
		break;
	default:
		break;
	}

	return false;
}
