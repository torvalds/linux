/*
 * Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
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
 * @needs_cdm:	Encoder requests a CDM based on pixel format conversion needs
 * @display_num_of_h_tiles: Number of horizontal tiles in case of split
 *                          interface
 * @topology:   Topology of the display
 */
struct dpu_encoder_hw_resources {
	enum dpu_intf_mode intfs[INTF_MAX];
	bool needs_cdm;
	u32 display_num_of_h_tiles;
};

/**
 * dpu_encoder_kickoff_params - info encoder requires at kickoff
 * @affected_displays:  bitmask, bit set means the ROI of the commit lies within
 *                      the bounds of the physical display at the bit index
 */
struct dpu_encoder_kickoff_params {
	unsigned long affected_displays;
};

/**
 * dpu_encoder_get_hw_resources - Populate table of required hardware resources
 * @encoder:	encoder pointer
 * @hw_res:	resource table to populate with encoder required resources
 * @conn_state:	report hw reqs based on this proposed connector state
 */
void dpu_encoder_get_hw_resources(struct drm_encoder *encoder,
		struct dpu_encoder_hw_resources *hw_res,
		struct drm_connector_state *conn_state);

/**
 * dpu_encoder_register_vblank_callback - provide callback to encoder that
 *	will be called on the next vblank.
 * @encoder:	encoder pointer
 * @cb:		callback pointer, provide NULL to deregister and disable IRQs
 * @data:	user data provided to callback
 */
void dpu_encoder_register_vblank_callback(struct drm_encoder *encoder,
		void (*cb)(void *), void *data);

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
 * @params:	kickoff time parameters
 */
void dpu_encoder_prepare_for_kickoff(struct drm_encoder *encoder,
		struct dpu_encoder_kickoff_params *params);

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
 * dpu_encoder_virt_restore - restore the encoder configs
 * @encoder:	encoder pointer
 */
void dpu_encoder_virt_restore(struct drm_encoder *encoder);

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

#endif /* __DPU_ENCODER_H__ */
