/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __DPU_ENCODER_H__
#define __DPU_ENCODER_H__

#include <drm/drm_crtc.h>
#include "dpu_hw_mdss.h"

#define DPU_ENCODER_FRAME_EVENT_DONE			BIT(0)
#define DPU_ENCODER_FRAME_EVENT_ERROR			BIT(1)
#define DPU_ENCODER_FRAME_EVENT_PANEL_DEAD		BIT(2)
#define DPU_ENCODER_FRAME_EVENT_IDLE			BIT(3)

#define IDLE_TIMEOUT	(66 - 16/2)

/**
 * Encoder functions and data types
 * @intfs:	Interfaces this encoder is using, INTF_MODE_NONE if unused
 */
struct dpu_encoder_hw_resources {
	enum dpu_intf_mode intfs[INTF_MAX];
};

/**
 * dpu_encoder_get_hw_resources - Populate table of required hardware resources
 * @encoder:	encoder pointer
 * @hw_res:	resource table to populate with encoder required resources
 */
void dpu_encoder_get_hw_resources(struct drm_encoder *encoder,
				  struct dpu_encoder_hw_resources *hw_res);

/**
 * dpu_encoder_assign_crtc - Link the encoder to the crtc it's assigned to
 * @encoder:	encoder pointer
 * @crtc:	crtc pointer
 */
void dpu_encoder_assign_crtc(struct drm_encoder *encoder,
			     struct drm_crtc *crtc);

/**
 * dpu_encoder_toggle_vblank_for_crtc - Toggles vblank interrupts on or off if
 *	the encoder is assigned to the given crtc
 * @encoder:	encoder pointer
 * @crtc:	crtc pointer
 * @enable:	true if vblank should be enabled
 */
void dpu_encoder_toggle_vblank_for_crtc(struct drm_encoder *encoder,
					struct drm_crtc *crtc, bool enable);

/**
 * dpu_encoder_register_frame_event_callback - provide callback to encoder that
 *	will be called after the request is complete, or other events.
 * @encoder:	encoder pointer
 * @cb:		callback pointer, provide NULL to deregister
 * @data:	user data provided to callback
 */
void dpu_encoder_register_frame_event_callback(struct drm_encoder *encoder,
		void (*cb)(void *, u32), void *data);

/**
 * dpu_encoder_prepare_for_kickoff - schedule double buffer flip of the ctl
 *	path (i.e. ctl flush and start) at next appropriate time.
 *	Immediately: if no previous commit is outstanding.
 *	Delayed: Block until next trigger can be issued.
 * @encoder:	encoder pointer
 */
void dpu_encoder_prepare_for_kickoff(struct drm_encoder *encoder);

/**
 * dpu_encoder_trigger_kickoff_pending - Clear the flush bits from previous
 *        kickoff and trigger the ctl prepare progress for command mode display.
 * @encoder:	encoder pointer
 */
void dpu_encoder_trigger_kickoff_pending(struct drm_encoder *encoder);

/**
 * dpu_encoder_kickoff - trigger a double buffer flip of the ctl path
 *	(i.e. ctl flush and start) immediately.
 * @encoder:	encoder pointer
 */
void dpu_encoder_kickoff(struct drm_encoder *encoder);

/**
 * dpu_encoder_wakeup_time - get the time of the next vsync
 */
int dpu_encoder_vsync_time(struct drm_encoder *drm_enc, ktime_t *wakeup_time);

/**
 * dpu_encoder_wait_for_event - Waits for encoder events
 * @encoder:	encoder pointer
 * @event:      event to wait for
 * MSM_ENC_COMMIT_DONE -  Wait for hardware to have flushed the current pending
 *                        frames to hardware at a vblank or ctl_start
 *                        Encoders will map this differently depending on the
 *                        panel type.
 *	                  vid mode -> vsync_irq
 *                        cmd mode -> ctl_start
 * MSM_ENC_TX_COMPLETE -  Wait for the hardware to transfer all the pixels to
 *                        the panel. Encoders will map this differently
 *                        depending on the panel type.
 *                        vid mode -> vsync_irq
 *                        cmd mode -> pp_done
 * Returns: 0 on success, -EWOULDBLOCK if already signaled, error otherwise
 */
int dpu_encoder_wait_for_event(struct drm_encoder *drm_encoder,
						enum msm_event_wait event);

/*
 * dpu_encoder_get_intf_mode - get interface mode of the given encoder
 * @encoder: Pointer to drm encoder object
 */
enum dpu_intf_mode dpu_encoder_get_intf_mode(struct drm_encoder *encoder);

/**
 * dpu_encoder_virt_runtime_resume - pm runtime resume the encoder configs
 * @encoder:	encoder pointer
 */
void dpu_encoder_virt_runtime_resume(struct drm_encoder *encoder);

/**
 * dpu_encoder_init - initialize virtual encoder object
 * @dev:        Pointer to drm device structure
 * @disp_info:  Pointer to display information structure
 * Returns:     Pointer to newly created drm encoder
 */
struct drm_encoder *dpu_encoder_init(
		struct drm_device *dev,
		int drm_enc_mode);

/**
 * dpu_encoder_setup - setup dpu_encoder for the display probed
 * @dev:		Pointer to drm device structure
 * @enc:		Pointer to the drm_encoder
 * @disp_info:	Pointer to the display info
 */
int dpu_encoder_setup(struct drm_device *dev, struct drm_encoder *enc,
		struct msm_display_info *disp_info);

/**
 * dpu_encoder_prepare_commit - prepare encoder at the very beginning of an
 *	atomic commit, before any registers are written
 * @drm_enc:    Pointer to previously created drm encoder structure
 */
void dpu_encoder_prepare_commit(struct drm_encoder *drm_enc);

/**
 * dpu_encoder_set_idle_timeout - set the idle timeout for video
 *                    and command mode encoders.
 * @drm_enc:    Pointer to previously created drm encoder structure
 * @idle_timeout:    idle timeout duration in milliseconds
 */
void dpu_encoder_set_idle_timeout(struct drm_encoder *drm_enc,
							u32 idle_timeout);
/**
 * dpu_encoder_get_linecount - get interface line count for the encoder.
 * @drm_enc:    Pointer to previously created drm encoder structure
 */
int dpu_encoder_get_linecount(struct drm_encoder *drm_enc);

/**
 * dpu_encoder_get_frame_count - get interface frame count for the encoder.
 * @drm_enc:    Pointer to previously created drm encoder structure
 */
int dpu_encoder_get_frame_count(struct drm_encoder *drm_enc);

#endif /* __DPU_ENCODER_H__ */
