/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2018 The Linux Foundation. All rights reserved.
 */

#ifndef __DPU_ENCODER_PHYS_H__
#define __DPU_ENCODER_PHYS_H__

#include <linux/jiffies.h>

#include "dpu_kms.h"
#include "dpu_hw_intf.h"
#include "dpu_hw_pingpong.h"
#include "dpu_hw_ctl.h"
#include "dpu_hw_top.h"
#include "dpu_encoder.h"
#include "dpu_crtc.h"

#define DPU_ENCODER_NAME_MAX	16

/* wait for at most 2 vsync for lowest refresh rate (24hz) */
#define KICKOFF_TIMEOUT_MS		84
#define KICKOFF_TIMEOUT_JIFFIES		msecs_to_jiffies(KICKOFF_TIMEOUT_MS)

/**
 * enum dpu_enc_split_role - Role this physical encoder will play in a
 *	split-panel configuration, where one panel is master, and others slaves.
 *	Masters have extra responsibilities, like managing the VBLANK IRQ.
 * @ENC_ROLE_SOLO:	This is the one and only panel. This encoder is master.
 * @ENC_ROLE_MASTER:	This encoder is the master of a split panel config.
 * @ENC_ROLE_SLAVE:	This encoder is not the master of a split panel config.
 */
enum dpu_enc_split_role {
	ENC_ROLE_SOLO,
	ENC_ROLE_MASTER,
	ENC_ROLE_SLAVE,
};

/**
 * enum dpu_enc_enable_state - current enabled state of the physical encoder
 * @DPU_ENC_DISABLING:	Encoder transitioning to disable state
 *			Events bounding transition are encoder type specific
 * @DPU_ENC_DISABLED:	Encoder is disabled
 * @DPU_ENC_ENABLING:	Encoder transitioning to enabled
 *			Events bounding transition are encoder type specific
 * @DPU_ENC_ENABLED:	Encoder is enabled
 * @DPU_ENC_ERR_NEEDS_HW_RESET:	Encoder is enabled, but requires a hw_reset
 *				to recover from a previous error
 */
enum dpu_enc_enable_state {
	DPU_ENC_DISABLING,
	DPU_ENC_DISABLED,
	DPU_ENC_ENABLING,
	DPU_ENC_ENABLED,
	DPU_ENC_ERR_NEEDS_HW_RESET
};

struct dpu_encoder_phys;

/**
 * struct dpu_encoder_virt_ops - Interface the containing virtual encoder
 *	provides for the physical encoders to use to callback.
 * @handle_vblank_virt:	Notify virtual encoder of vblank IRQ reception
 *			Note: This is called from IRQ handler context.
 * @handle_underrun_virt: Notify virtual encoder of underrun IRQ reception
 *			Note: This is called from IRQ handler context.
 * @handle_frame_done:	Notify virtual encoder that this phys encoder
 *			completes last request frame.
 */
struct dpu_encoder_virt_ops {
	void (*handle_vblank_virt)(struct drm_encoder *,
			struct dpu_encoder_phys *phys);
	void (*handle_underrun_virt)(struct drm_encoder *,
			struct dpu_encoder_phys *phys);
	void (*handle_frame_done)(struct drm_encoder *,
			struct dpu_encoder_phys *phys, u32 event);
};

/**
 * struct dpu_encoder_phys_ops - Interface the physical encoders provide to
 *	the containing virtual encoder.
 * @late_register:		DRM Call. Add Userspace interfaces, debugfs.
 * @prepare_commit:		MSM Atomic Call, start of atomic commit sequence
 * @is_master:			Whether this phys_enc is the current master
 *				encoder. Can be switched at enable time. Based
 *				on split_role and current mode (CMD/VID).
 * @mode_fixup:			DRM Call. Fixup a DRM mode.
 * @mode_set:			DRM Call. Set a DRM mode.
 *				This likely caches the mode, for use at enable.
 * @enable:			DRM Call. Enable a DRM mode.
 * @disable:			DRM Call. Disable mode.
 * @atomic_check:		DRM Call. Atomic check new DRM state.
 * @destroy:			DRM Call. Destroy and release resources.
 * @get_hw_resources:		Populate the structure with the hardware
 *				resources that this phys_enc is using.
 *				Expect no overlap between phys_encs.
 * @control_vblank_irq		Register/Deregister for VBLANK IRQ
 * @wait_for_commit_done:	Wait for hardware to have flushed the
 *				current pending frames to hardware
 * @wait_for_tx_complete:	Wait for hardware to transfer the pixels
 *				to the panel
 * @wait_for_vblank:		Wait for VBLANK, for sub-driver internal use
 * @prepare_for_kickoff:	Do any work necessary prior to a kickoff
 *				For CMD encoder, may wait for previous tx done
 * @handle_post_kickoff:	Do any work necessary post-kickoff work
 * @trigger_start:		Process start event on physical encoder
 * @needs_single_flush:		Whether encoder slaves need to be flushed
 * @irq_control:		Handler to enable/disable all the encoder IRQs
 * @prepare_idle_pc:		phys encoder can update the vsync_enable status
 *                              on idle power collapse prepare
 * @restore:			Restore all the encoder configs.
 * @get_line_count:		Obtain current vertical line count
 */

struct dpu_encoder_phys_ops {
	int (*late_register)(struct dpu_encoder_phys *encoder,
			struct dentry *debugfs_root);
	void (*prepare_commit)(struct dpu_encoder_phys *encoder);
	bool (*is_master)(struct dpu_encoder_phys *encoder);
	bool (*mode_fixup)(struct dpu_encoder_phys *encoder,
			const struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode);
	void (*mode_set)(struct dpu_encoder_phys *encoder,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode);
	void (*enable)(struct dpu_encoder_phys *encoder);
	void (*disable)(struct dpu_encoder_phys *encoder);
	int (*atomic_check)(struct dpu_encoder_phys *encoder,
			    struct drm_crtc_state *crtc_state,
			    struct drm_connector_state *conn_state);
	void (*destroy)(struct dpu_encoder_phys *encoder);
	void (*get_hw_resources)(struct dpu_encoder_phys *encoder,
				 struct dpu_encoder_hw_resources *hw_res);
	int (*control_vblank_irq)(struct dpu_encoder_phys *enc, bool enable);
	int (*wait_for_commit_done)(struct dpu_encoder_phys *phys_enc);
	int (*wait_for_tx_complete)(struct dpu_encoder_phys *phys_enc);
	int (*wait_for_vblank)(struct dpu_encoder_phys *phys_enc);
	void (*prepare_for_kickoff)(struct dpu_encoder_phys *phys_enc);
	void (*handle_post_kickoff)(struct dpu_encoder_phys *phys_enc);
	void (*trigger_start)(struct dpu_encoder_phys *phys_enc);
	bool (*needs_single_flush)(struct dpu_encoder_phys *phys_enc);
	void (*irq_control)(struct dpu_encoder_phys *phys, bool enable);
	void (*prepare_idle_pc)(struct dpu_encoder_phys *phys_enc);
	void (*restore)(struct dpu_encoder_phys *phys);
	int (*get_line_count)(struct dpu_encoder_phys *phys);
	int (*get_frame_count)(struct dpu_encoder_phys *phys);
};

/**
 * enum dpu_intr_idx - dpu encoder interrupt index
 * @INTR_IDX_VSYNC:    Vsync interrupt for video mode panel
 * @INTR_IDX_PINGPONG: Pingpong done unterrupt for cmd mode panel
 * @INTR_IDX_UNDERRUN: Underrun unterrupt for video and cmd mode panel
 * @INTR_IDX_RDPTR:    Readpointer done unterrupt for cmd mode panel
 */
enum dpu_intr_idx {
	INTR_IDX_VSYNC,
	INTR_IDX_PINGPONG,
	INTR_IDX_UNDERRUN,
	INTR_IDX_CTL_START,
	INTR_IDX_RDPTR,
	INTR_IDX_MAX,
};

/**
 * dpu_encoder_irq - tracking structure for interrupts
 * @name:		string name of interrupt
 * @intr_idx:		Encoder interrupt enumeration
 * @irq_idx:		IRQ interface lookup index from DPU IRQ framework
 *			will be -EINVAL if IRQ is not registered
 * @irq_cb:		interrupt callback
 */
struct dpu_encoder_irq {
	const char *name;
	enum dpu_intr_idx intr_idx;
	int irq_idx;
	struct dpu_irq_callback cb;
};

/**
 * struct dpu_encoder_phys - physical encoder that drives a single INTF block
 *	tied to a specific panel / sub-panel. Abstract type, sub-classed by
 *	phys_vid or phys_cmd for video mode or command mode encs respectively.
 * @parent:		Pointer to the containing virtual encoder
 * @connector:		If a mode is set, cached pointer to the active connector
 * @ops:		Operations exposed to the virtual encoder
 * @parent_ops:		Callbacks exposed by the parent to the phys_enc
 * @hw_mdptop:		Hardware interface to the top registers
 * @hw_ctl:		Hardware interface to the ctl registers
 * @hw_pp:		Hardware interface to the ping pong registers
 * @hw_intf:		Hardware interface to the intf registers
 * @dpu_kms:		Pointer to the dpu_kms top level
 * @cached_mode:	DRM mode cached at mode_set time, acted on in enable
 * @enabled:		Whether the encoder has enabled and running a mode
 * @split_role:		Role to play in a split-panel configuration
 * @intf_mode:		Interface mode
 * @intf_idx:		Interface index on dpu hardware
 * @enc_spinlock:	Virtual-Encoder-Wide Spin Lock for IRQ purposes
 * @enable_state:	Enable state tracking
 * @vblank_refcount:	Reference count of vblank request
 * @vsync_cnt:		Vsync count for the physical encoder
 * @underrun_cnt:	Underrun count for the physical encoder
 * @pending_kickoff_cnt:	Atomic counter tracking the number of kickoffs
 *				vs. the number of done/vblank irqs. Should hover
 *				between 0-2 Incremented when a new kickoff is
 *				scheduled. Decremented in irq handler
 * @pending_ctlstart_cnt:	Atomic counter tracking the number of ctl start
 *                              pending.
 * @pending_kickoff_wq:		Wait queue for blocking until kickoff completes
 * @irq:			IRQ tracking structures
 */
struct dpu_encoder_phys {
	struct drm_encoder *parent;
	struct drm_connector *connector;
	struct dpu_encoder_phys_ops ops;
	const struct dpu_encoder_virt_ops *parent_ops;
	struct dpu_hw_mdp *hw_mdptop;
	struct dpu_hw_ctl *hw_ctl;
	struct dpu_hw_pingpong *hw_pp;
	struct dpu_hw_intf *hw_intf;
	struct dpu_kms *dpu_kms;
	struct drm_display_mode cached_mode;
	enum dpu_enc_split_role split_role;
	enum dpu_intf_mode intf_mode;
	enum dpu_intf intf_idx;
	spinlock_t *enc_spinlock;
	enum dpu_enc_enable_state enable_state;
	atomic_t vblank_refcount;
	atomic_t vsync_cnt;
	atomic_t underrun_cnt;
	atomic_t pending_ctlstart_cnt;
	atomic_t pending_kickoff_cnt;
	wait_queue_head_t pending_kickoff_wq;
	struct dpu_encoder_irq irq[INTR_IDX_MAX];
};

static inline int dpu_encoder_phys_inc_pending(struct dpu_encoder_phys *phys)
{
	atomic_inc_return(&phys->pending_ctlstart_cnt);
	return atomic_inc_return(&phys->pending_kickoff_cnt);
}

/**
 * struct dpu_encoder_phys_cmd - sub-class of dpu_encoder_phys to handle command
 *	mode specific operations
 * @base:	Baseclass physical encoder structure
 * @intf_idx:	Intf Block index used by this phys encoder
 * @stream_sel:	Stream selection for multi-stream interfaces
 * @serialize_wait4pp:	serialize wait4pp feature waits for pp_done interrupt
 *			after ctl_start instead of before next frame kickoff
 * @pp_timeout_report_cnt: number of pingpong done irq timeout errors
 * @pending_vblank_cnt: Atomic counter tracking pending wait for VBLANK
 * @pending_vblank_wq: Wait queue for blocking until VBLANK received
 */
struct dpu_encoder_phys_cmd {
	struct dpu_encoder_phys base;
	int stream_sel;
	bool serialize_wait4pp;
	int pp_timeout_report_cnt;
	atomic_t pending_vblank_cnt;
	wait_queue_head_t pending_vblank_wq;
};

/**
 * struct dpu_enc_phys_init_params - initialization parameters for phys encs
 * @dpu_kms:		Pointer to the dpu_kms top level
 * @parent:		Pointer to the containing virtual encoder
 * @parent_ops:		Callbacks exposed by the parent to the phys_enc
 * @split_role:		Role to play in a split-panel configuration
 * @intf_idx:		Interface index this phys_enc will control
 * @enc_spinlock:	Virtual-Encoder-Wide Spin Lock for IRQ purposes
 */
struct dpu_enc_phys_init_params {
	struct dpu_kms *dpu_kms;
	struct drm_encoder *parent;
	const struct dpu_encoder_virt_ops *parent_ops;
	enum dpu_enc_split_role split_role;
	enum dpu_intf intf_idx;
	spinlock_t *enc_spinlock;
};

/**
 * dpu_encoder_wait_info - container for passing arguments to irq wait functions
 * @wq: wait queue structure
 * @atomic_cnt: wait until atomic_cnt equals zero
 * @timeout_ms: timeout value in milliseconds
 */
struct dpu_encoder_wait_info {
	wait_queue_head_t *wq;
	atomic_t *atomic_cnt;
	s64 timeout_ms;
};

/**
 * dpu_encoder_phys_vid_init - Construct a new video mode physical encoder
 * @p:	Pointer to init params structure
 * Return: Error code or newly allocated encoder
 */
struct dpu_encoder_phys *dpu_encoder_phys_vid_init(
		struct dpu_enc_phys_init_params *p);

/**
 * dpu_encoder_phys_cmd_init - Construct a new command mode physical encoder
 * @p:	Pointer to init params structure
 * Return: Error code or newly allocated encoder
 */
struct dpu_encoder_phys *dpu_encoder_phys_cmd_init(
		struct dpu_enc_phys_init_params *p);

/**
 * dpu_encoder_helper_trigger_start - control start helper function
 *	This helper function may be optionally specified by physical
 *	encoders if they require ctl_start triggering.
 * @phys_enc: Pointer to physical encoder structure
 */
void dpu_encoder_helper_trigger_start(struct dpu_encoder_phys *phys_enc);

static inline enum dpu_3d_blend_mode dpu_encoder_helper_get_3d_blend_mode(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_crtc_state *dpu_cstate;

	if (!phys_enc || phys_enc->enable_state == DPU_ENC_DISABLING)
		return BLEND_3D_NONE;

	dpu_cstate = to_dpu_crtc_state(phys_enc->parent->crtc->state);

	if (phys_enc->split_role == ENC_ROLE_SOLO &&
	    dpu_cstate->num_mixers == CRTC_DUAL_MIXERS)
		return BLEND_3D_H_ROW_INT;

	return BLEND_3D_NONE;
}

/**
 * dpu_encoder_helper_split_config - split display configuration helper function
 *	This helper function may be used by physical encoders to configure
 *	the split display related registers.
 * @phys_enc: Pointer to physical encoder structure
 * @interface: enum dpu_intf setting
 */
void dpu_encoder_helper_split_config(
		struct dpu_encoder_phys *phys_enc,
		enum dpu_intf interface);

/**
 * dpu_encoder_helper_report_irq_timeout - utility to report error that irq has
 *	timed out, including reporting frame error event to crtc and debug dump
 * @phys_enc: Pointer to physical encoder structure
 * @intr_idx: Failing interrupt index
 */
void dpu_encoder_helper_report_irq_timeout(struct dpu_encoder_phys *phys_enc,
		enum dpu_intr_idx intr_idx);

/**
 * dpu_encoder_helper_wait_for_irq - utility to wait on an irq.
 *	note: will call dpu_encoder_helper_wait_for_irq on timeout
 * @phys_enc: Pointer to physical encoder structure
 * @intr_idx: encoder interrupt index
 * @wait_info: wait info struct
 * @Return: 0 or -ERROR
 */
int dpu_encoder_helper_wait_for_irq(struct dpu_encoder_phys *phys_enc,
		enum dpu_intr_idx intr_idx,
		struct dpu_encoder_wait_info *wait_info);

/**
 * dpu_encoder_helper_register_irq - register and enable an irq
 * @phys_enc: Pointer to physical encoder structure
 * @intr_idx: encoder interrupt index
 * @Return: 0 or -ERROR
 */
int dpu_encoder_helper_register_irq(struct dpu_encoder_phys *phys_enc,
		enum dpu_intr_idx intr_idx);

/**
 * dpu_encoder_helper_unregister_irq - unregister and disable an irq
 * @phys_enc: Pointer to physical encoder structure
 * @intr_idx: encoder interrupt index
 * @Return: 0 or -ERROR
 */
int dpu_encoder_helper_unregister_irq(struct dpu_encoder_phys *phys_enc,
		enum dpu_intr_idx intr_idx);

#endif /* __dpu_encoder_phys_H__ */
