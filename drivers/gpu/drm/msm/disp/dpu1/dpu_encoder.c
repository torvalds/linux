// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2018, 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__
#include <linux/debugfs.h>
#include <linux/kthread.h>
#include <linux/seq_file.h>

#include <drm/drm_crtc.h>
#include <drm/drm_file.h>
#include <drm/drm_probe_helper.h>

#include "msm_drv.h"
#include "dpu_kms.h"
#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_intf.h"
#include "dpu_hw_ctl.h"
#include "dpu_hw_dspp.h"
#include "dpu_formats.h"
#include "dpu_encoder_phys.h"
#include "dpu_crtc.h"
#include "dpu_trace.h"
#include "dpu_core_irq.h"
#include "disp/msm_disp_snapshot.h"

#define DPU_DEBUG_ENC(e, fmt, ...) DRM_DEBUG_ATOMIC("enc%d " fmt,\
		(e) ? (e)->base.base.id : -1, ##__VA_ARGS__)

#define DPU_ERROR_ENC(e, fmt, ...) DPU_ERROR("enc%d " fmt,\
		(e) ? (e)->base.base.id : -1, ##__VA_ARGS__)

#define DPU_DEBUG_PHYS(p, fmt, ...) DRM_DEBUG_ATOMIC("enc%d intf%d pp%d " fmt,\
		(p) ? (p)->parent->base.id : -1, \
		(p) ? (p)->intf_idx - INTF_0 : -1, \
		(p) ? ((p)->hw_pp ? (p)->hw_pp->idx - PINGPONG_0 : -1) : -1, \
		##__VA_ARGS__)

#define DPU_ERROR_PHYS(p, fmt, ...) DPU_ERROR("enc%d intf%d pp%d " fmt,\
		(p) ? (p)->parent->base.id : -1, \
		(p) ? (p)->intf_idx - INTF_0 : -1, \
		(p) ? ((p)->hw_pp ? (p)->hw_pp->idx - PINGPONG_0 : -1) : -1, \
		##__VA_ARGS__)

/*
 * Two to anticipate panels that can do cmd/vid dynamic switching
 * plan is to create all possible physical encoder types, and switch between
 * them at runtime
 */
#define NUM_PHYS_ENCODER_TYPES 2

#define MAX_PHYS_ENCODERS_PER_VIRTUAL \
	(MAX_H_TILES_PER_DISPLAY * NUM_PHYS_ENCODER_TYPES)

#define MAX_CHANNELS_PER_ENC 2

#define IDLE_SHORT_TIMEOUT	1

#define MAX_HDISPLAY_SPLIT 1080

/* timeout in frames waiting for frame done */
#define DPU_ENCODER_FRAME_DONE_TIMEOUT_FRAMES 5

/**
 * enum dpu_enc_rc_events - events for resource control state machine
 * @DPU_ENC_RC_EVENT_KICKOFF:
 *	This event happens at NORMAL priority.
 *	Event that signals the start of the transfer. When this event is
 *	received, enable MDP/DSI core clocks. Regardless of the previous
 *	state, the resource should be in ON state at the end of this event.
 * @DPU_ENC_RC_EVENT_FRAME_DONE:
 *	This event happens at INTERRUPT level.
 *	Event signals the end of the data transfer after the PP FRAME_DONE
 *	event. At the end of this event, a delayed work is scheduled to go to
 *	IDLE_PC state after IDLE_TIMEOUT time.
 * @DPU_ENC_RC_EVENT_PRE_STOP:
 *	This event happens at NORMAL priority.
 *	This event, when received during the ON state, leave the RC STATE
 *	in the PRE_OFF state. It should be followed by the STOP event as
 *	part of encoder disable.
 *	If received during IDLE or OFF states, it will do nothing.
 * @DPU_ENC_RC_EVENT_STOP:
 *	This event happens at NORMAL priority.
 *	When this event is received, disable all the MDP/DSI core clocks, and
 *	disable IRQs. It should be called from the PRE_OFF or IDLE states.
 *	IDLE is expected when IDLE_PC has run, and PRE_OFF did nothing.
 *	PRE_OFF is expected when PRE_STOP was executed during the ON state.
 *	Resource state should be in OFF at the end of the event.
 * @DPU_ENC_RC_EVENT_ENTER_IDLE:
 *	This event happens at NORMAL priority from a work item.
 *	Event signals that there were no frame updates for IDLE_TIMEOUT time.
 *	This would disable MDP/DSI core clocks and change the resource state
 *	to IDLE.
 */
enum dpu_enc_rc_events {
	DPU_ENC_RC_EVENT_KICKOFF = 1,
	DPU_ENC_RC_EVENT_FRAME_DONE,
	DPU_ENC_RC_EVENT_PRE_STOP,
	DPU_ENC_RC_EVENT_STOP,
	DPU_ENC_RC_EVENT_ENTER_IDLE
};

/*
 * enum dpu_enc_rc_states - states that the resource control maintains
 * @DPU_ENC_RC_STATE_OFF: Resource is in OFF state
 * @DPU_ENC_RC_STATE_PRE_OFF: Resource is transitioning to OFF state
 * @DPU_ENC_RC_STATE_ON: Resource is in ON state
 * @DPU_ENC_RC_STATE_MODESET: Resource is in modeset state
 * @DPU_ENC_RC_STATE_IDLE: Resource is in IDLE state
 */
enum dpu_enc_rc_states {
	DPU_ENC_RC_STATE_OFF,
	DPU_ENC_RC_STATE_PRE_OFF,
	DPU_ENC_RC_STATE_ON,
	DPU_ENC_RC_STATE_IDLE
};

/**
 * struct dpu_encoder_virt - virtual encoder. Container of one or more physical
 *	encoders. Virtual encoder manages one "logical" display. Physical
 *	encoders manage one intf block, tied to a specific panel/sub-panel.
 *	Virtual encoder defers as much as possible to the physical encoders.
 *	Virtual encoder registers itself with the DRM Framework as the encoder.
 * @base:		drm_encoder base class for registration with DRM
 * @enc_spinlock:	Virtual-Encoder-Wide Spin Lock for IRQ purposes
 * @bus_scaling_client:	Client handle to the bus scaling interface
 * @enabled:		True if the encoder is active, protected by enc_lock
 * @num_phys_encs:	Actual number of physical encoders contained.
 * @phys_encs:		Container of physical encoders managed.
 * @cur_master:		Pointer to the current master in this mode. Optimization
 *			Only valid after enable. Cleared as disable.
 * @cur_slave:		As above but for the slave encoder.
 * @hw_pp:		Handle to the pingpong blocks used for the display. No.
 *			pingpong blocks can be different than num_phys_encs.
 * @intfs_swapped:	Whether or not the phys_enc interfaces have been swapped
 *			for partial update right-only cases, such as pingpong
 *			split where virtual pingpong does not generate IRQs
 * @crtc:		Pointer to the currently assigned crtc. Normally you
 *			would use crtc->state->encoder_mask to determine the
 *			link between encoder/crtc. However in this case we need
 *			to track crtc in the disable() hook which is called
 *			_after_ encoder_mask is cleared.
 * @crtc_kickoff_cb:		Callback into CRTC that will flush & start
 *				all CTL paths
 * @crtc_kickoff_cb_data:	Opaque user data given to crtc_kickoff_cb
 * @debugfs_root:		Debug file system root file node
 * @enc_lock:			Lock around physical encoder
 *				create/destroy/enable/disable
 * @frame_busy_mask:		Bitmask tracking which phys_enc we are still
 *				busy processing current command.
 *				Bit0 = phys_encs[0] etc.
 * @crtc_frame_event_cb:	callback handler for frame event
 * @crtc_frame_event_cb_data:	callback handler private data
 * @frame_done_timeout_ms:	frame done timeout in ms
 * @frame_done_timer:		watchdog timer for frame done event
 * @vsync_event_timer:		vsync timer
 * @disp_info:			local copy of msm_display_info struct
 * @idle_pc_supported:		indicate if idle power collaps is supported
 * @rc_lock:			resource control mutex lock to protect
 *				virt encoder over various state changes
 * @rc_state:			resource controller state
 * @delayed_off_work:		delayed worker to schedule disabling of
 *				clks and resources after IDLE_TIMEOUT time.
 * @vsync_event_work:		worker to handle vsync event for autorefresh
 * @topology:                   topology of the display
 * @idle_timeout:		idle timeout duration in milliseconds
 * @dp:				msm_dp pointer, for DP encoders
 */
struct dpu_encoder_virt {
	struct drm_encoder base;
	spinlock_t enc_spinlock;
	uint32_t bus_scaling_client;

	bool enabled;

	unsigned int num_phys_encs;
	struct dpu_encoder_phys *phys_encs[MAX_PHYS_ENCODERS_PER_VIRTUAL];
	struct dpu_encoder_phys *cur_master;
	struct dpu_encoder_phys *cur_slave;
	struct dpu_hw_pingpong *hw_pp[MAX_CHANNELS_PER_ENC];

	bool intfs_swapped;

	struct drm_crtc *crtc;

	struct dentry *debugfs_root;
	struct mutex enc_lock;
	DECLARE_BITMAP(frame_busy_mask, MAX_PHYS_ENCODERS_PER_VIRTUAL);
	void (*crtc_frame_event_cb)(void *, u32 event);
	void *crtc_frame_event_cb_data;

	atomic_t frame_done_timeout_ms;
	struct timer_list frame_done_timer;
	struct timer_list vsync_event_timer;

	struct msm_display_info disp_info;

	bool idle_pc_supported;
	struct mutex rc_lock;
	enum dpu_enc_rc_states rc_state;
	struct delayed_work delayed_off_work;
	struct kthread_work vsync_event_work;
	struct msm_display_topology topology;

	u32 idle_timeout;

	struct msm_dp *dp;
};

#define to_dpu_encoder_virt(x) container_of(x, struct dpu_encoder_virt, base)

static u32 dither_matrix[DITHER_MATRIX_SZ] = {
	15, 7, 13, 5, 3, 11, 1, 9, 12, 4, 14, 6, 0, 8, 2, 10
};

static void _dpu_encoder_setup_dither(struct dpu_hw_pingpong *hw_pp, unsigned bpc)
{
	struct dpu_hw_dither_cfg dither_cfg = { 0 };

	if (!hw_pp->ops.setup_dither)
		return;

	switch (bpc) {
	case 6:
		dither_cfg.c0_bitdepth = 6;
		dither_cfg.c1_bitdepth = 6;
		dither_cfg.c2_bitdepth = 6;
		dither_cfg.c3_bitdepth = 6;
		dither_cfg.temporal_en = 0;
		break;
	default:
		hw_pp->ops.setup_dither(hw_pp, NULL);
		return;
	}

	memcpy(&dither_cfg.matrix, dither_matrix,
			sizeof(u32) * DITHER_MATRIX_SZ);

	hw_pp->ops.setup_dither(hw_pp, &dither_cfg);
}

void dpu_encoder_helper_report_irq_timeout(struct dpu_encoder_phys *phys_enc,
		enum dpu_intr_idx intr_idx)
{
	DRM_ERROR("irq timeout id=%u, intf=%d, pp=%d, intr=%d\n",
		  DRMID(phys_enc->parent), phys_enc->intf_idx - INTF_0,
		  phys_enc->hw_pp->idx - PINGPONG_0, intr_idx);

	if (phys_enc->parent_ops->handle_frame_done)
		phys_enc->parent_ops->handle_frame_done(
				phys_enc->parent, phys_enc,
				DPU_ENCODER_FRAME_EVENT_ERROR);
}

static int dpu_encoder_helper_wait_event_timeout(int32_t drm_id,
		u32 irq_idx, struct dpu_encoder_wait_info *info);

int dpu_encoder_helper_wait_for_irq(struct dpu_encoder_phys *phys_enc,
		enum dpu_intr_idx intr_idx,
		struct dpu_encoder_wait_info *wait_info)
{
	struct dpu_encoder_irq *irq;
	u32 irq_status;
	int ret;

	if (!wait_info || intr_idx >= INTR_IDX_MAX) {
		DPU_ERROR("invalid params\n");
		return -EINVAL;
	}
	irq = &phys_enc->irq[intr_idx];

	/* note: do master / slave checking outside */

	/* return EWOULDBLOCK since we know the wait isn't necessary */
	if (phys_enc->enable_state == DPU_ENC_DISABLED) {
		DRM_ERROR("encoder is disabled id=%u, intr=%d, irq=%d\n",
			  DRMID(phys_enc->parent), intr_idx,
			  irq->irq_idx);
		return -EWOULDBLOCK;
	}

	if (irq->irq_idx < 0) {
		DRM_DEBUG_KMS("skip irq wait id=%u, intr=%d, irq=%s\n",
			      DRMID(phys_enc->parent), intr_idx,
			      irq->name);
		return 0;
	}

	DRM_DEBUG_KMS("id=%u, intr=%d, irq=%d, pp=%d, pending_cnt=%d\n",
		      DRMID(phys_enc->parent), intr_idx,
		      irq->irq_idx, phys_enc->hw_pp->idx - PINGPONG_0,
		      atomic_read(wait_info->atomic_cnt));

	ret = dpu_encoder_helper_wait_event_timeout(
			DRMID(phys_enc->parent),
			irq->irq_idx,
			wait_info);

	if (ret <= 0) {
		irq_status = dpu_core_irq_read(phys_enc->dpu_kms,
				irq->irq_idx, true);
		if (irq_status) {
			unsigned long flags;

			DRM_DEBUG_KMS("irq not triggered id=%u, intr=%d, irq=%d, pp=%d, atomic_cnt=%d\n",
				      DRMID(phys_enc->parent), intr_idx,
				      irq->irq_idx,
				      phys_enc->hw_pp->idx - PINGPONG_0,
				      atomic_read(wait_info->atomic_cnt));
			local_irq_save(flags);
			irq->cb.func(phys_enc, irq->irq_idx);
			local_irq_restore(flags);
			ret = 0;
		} else {
			ret = -ETIMEDOUT;
			DRM_DEBUG_KMS("irq timeout id=%u, intr=%d, irq=%d, pp=%d, atomic_cnt=%d\n",
				      DRMID(phys_enc->parent), intr_idx,
				      irq->irq_idx,
				      phys_enc->hw_pp->idx - PINGPONG_0,
				      atomic_read(wait_info->atomic_cnt));
		}
	} else {
		ret = 0;
		trace_dpu_enc_irq_wait_success(DRMID(phys_enc->parent),
			intr_idx, irq->irq_idx,
			phys_enc->hw_pp->idx - PINGPONG_0,
			atomic_read(wait_info->atomic_cnt));
	}

	return ret;
}

int dpu_encoder_helper_register_irq(struct dpu_encoder_phys *phys_enc,
		enum dpu_intr_idx intr_idx)
{
	struct dpu_encoder_irq *irq;
	int ret = 0;

	if (intr_idx >= INTR_IDX_MAX) {
		DPU_ERROR("invalid params\n");
		return -EINVAL;
	}
	irq = &phys_enc->irq[intr_idx];

	if (irq->irq_idx < 0) {
		DPU_ERROR_PHYS(phys_enc,
			"invalid IRQ index:%d\n", irq->irq_idx);
		return -EINVAL;
	}

	ret = dpu_core_irq_register_callback(phys_enc->dpu_kms, irq->irq_idx,
			&irq->cb);
	if (ret) {
		DPU_ERROR_PHYS(phys_enc,
			"failed to register IRQ callback for %s\n",
			irq->name);
		irq->irq_idx = -EINVAL;
		return ret;
	}

	trace_dpu_enc_irq_register_success(DRMID(phys_enc->parent), intr_idx,
				irq->irq_idx);

	return ret;
}

int dpu_encoder_helper_unregister_irq(struct dpu_encoder_phys *phys_enc,
		enum dpu_intr_idx intr_idx)
{
	struct dpu_encoder_irq *irq;
	int ret;

	irq = &phys_enc->irq[intr_idx];

	/* silently skip irqs that weren't registered */
	if (irq->irq_idx < 0) {
		DRM_ERROR("duplicate unregister id=%u, intr=%d, irq=%d",
			  DRMID(phys_enc->parent), intr_idx,
			  irq->irq_idx);
		return 0;
	}

	ret = dpu_core_irq_unregister_callback(phys_enc->dpu_kms, irq->irq_idx,
			&irq->cb);
	if (ret) {
		DRM_ERROR("unreg cb fail id=%u, intr=%d, irq=%d ret=%d",
			  DRMID(phys_enc->parent), intr_idx,
			  irq->irq_idx, ret);
	}

	trace_dpu_enc_irq_unregister_success(DRMID(phys_enc->parent), intr_idx,
					     irq->irq_idx);

	return 0;
}

int dpu_encoder_get_vsync_count(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(drm_enc);
	struct dpu_encoder_phys *phys = dpu_enc ? dpu_enc->cur_master : NULL;
	return phys ? atomic_read(&phys->vsync_cnt) : 0;
}

int dpu_encoder_get_linecount(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc;
	struct dpu_encoder_phys *phys;
	int linecount = 0;

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	phys = dpu_enc ? dpu_enc->cur_master : NULL;

	if (phys && phys->ops.get_line_count)
		linecount = phys->ops.get_line_count(phys);

	return linecount;
}

void dpu_encoder_get_hw_resources(struct drm_encoder *drm_enc,
				  struct dpu_encoder_hw_resources *hw_res)
{
	struct dpu_encoder_virt *dpu_enc = NULL;
	int i = 0;

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	DPU_DEBUG_ENC(dpu_enc, "\n");

	/* Query resources used by phys encs, expected to be without overlap */
	memset(hw_res, 0, sizeof(*hw_res));

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (phys->ops.get_hw_resources)
			phys->ops.get_hw_resources(phys, hw_res);
	}
}

static void dpu_encoder_destroy(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc = NULL;
	int i = 0;

	if (!drm_enc) {
		DPU_ERROR("invalid encoder\n");
		return;
	}

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	DPU_DEBUG_ENC(dpu_enc, "\n");

	mutex_lock(&dpu_enc->enc_lock);

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (phys->ops.destroy) {
			phys->ops.destroy(phys);
			--dpu_enc->num_phys_encs;
			dpu_enc->phys_encs[i] = NULL;
		}
	}

	if (dpu_enc->num_phys_encs)
		DPU_ERROR_ENC(dpu_enc, "expected 0 num_phys_encs not %d\n",
				dpu_enc->num_phys_encs);
	dpu_enc->num_phys_encs = 0;
	mutex_unlock(&dpu_enc->enc_lock);

	drm_encoder_cleanup(drm_enc);
	mutex_destroy(&dpu_enc->enc_lock);
}

void dpu_encoder_helper_split_config(
		struct dpu_encoder_phys *phys_enc,
		enum dpu_intf interface)
{
	struct dpu_encoder_virt *dpu_enc;
	struct split_pipe_cfg cfg = { 0 };
	struct dpu_hw_mdp *hw_mdptop;
	struct msm_display_info *disp_info;

	if (!phys_enc->hw_mdptop || !phys_enc->parent) {
		DPU_ERROR("invalid arg(s), encoder %d\n", phys_enc != NULL);
		return;
	}

	dpu_enc = to_dpu_encoder_virt(phys_enc->parent);
	hw_mdptop = phys_enc->hw_mdptop;
	disp_info = &dpu_enc->disp_info;

	if (disp_info->intf_type != DRM_MODE_ENCODER_DSI)
		return;

	/**
	 * disable split modes since encoder will be operating in as the only
	 * encoder, either for the entire use case in the case of, for example,
	 * single DSI, or for this frame in the case of left/right only partial
	 * update.
	 */
	if (phys_enc->split_role == ENC_ROLE_SOLO) {
		if (hw_mdptop->ops.setup_split_pipe)
			hw_mdptop->ops.setup_split_pipe(hw_mdptop, &cfg);
		return;
	}

	cfg.en = true;
	cfg.mode = phys_enc->intf_mode;
	cfg.intf = interface;

	if (cfg.en && phys_enc->ops.needs_single_flush &&
			phys_enc->ops.needs_single_flush(phys_enc))
		cfg.split_flush_en = true;

	if (phys_enc->split_role == ENC_ROLE_MASTER) {
		DPU_DEBUG_ENC(dpu_enc, "enable %d\n", cfg.en);

		if (hw_mdptop->ops.setup_split_pipe)
			hw_mdptop->ops.setup_split_pipe(hw_mdptop, &cfg);
	}
}

static struct msm_display_topology dpu_encoder_get_topology(
			struct dpu_encoder_virt *dpu_enc,
			struct dpu_kms *dpu_kms,
			struct drm_display_mode *mode)
{
	struct msm_display_topology topology = {0};
	int i, intf_count = 0;

	for (i = 0; i < MAX_PHYS_ENCODERS_PER_VIRTUAL; i++)
		if (dpu_enc->phys_encs[i])
			intf_count++;

	/* Datapath topology selection
	 *
	 * Dual display
	 * 2 LM, 2 INTF ( Split display using 2 interfaces)
	 *
	 * Single display
	 * 1 LM, 1 INTF
	 * 2 LM, 1 INTF (stream merge to support high resolution interfaces)
	 *
	 * Adding color blocks only to primary interface if available in
	 * sufficient number
	 */
	if (intf_count == 2)
		topology.num_lm = 2;
	else if (!dpu_kms->catalog->caps->has_3d_merge)
		topology.num_lm = 1;
	else
		topology.num_lm = (mode->hdisplay > MAX_HDISPLAY_SPLIT) ? 2 : 1;

	if (dpu_enc->disp_info.intf_type == DRM_MODE_ENCODER_DSI) {
		if (dpu_kms->catalog->dspp &&
			(dpu_kms->catalog->dspp_count >= topology.num_lm))
			topology.num_dspp = topology.num_lm;
	}

	topology.num_enc = 0;
	topology.num_intf = intf_count;

	return topology;
}
static int dpu_encoder_virt_atomic_check(
		struct drm_encoder *drm_enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state)
{
	struct dpu_encoder_virt *dpu_enc;
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms;
	const struct drm_display_mode *mode;
	struct drm_display_mode *adj_mode;
	struct msm_display_topology topology;
	struct dpu_global_state *global_state;
	int i = 0;
	int ret = 0;

	if (!drm_enc || !crtc_state || !conn_state) {
		DPU_ERROR("invalid arg(s), drm_enc %d, crtc/conn state %d/%d\n",
				drm_enc != NULL, crtc_state != NULL, conn_state != NULL);
		return -EINVAL;
	}

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	DPU_DEBUG_ENC(dpu_enc, "\n");

	priv = drm_enc->dev->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);
	mode = &crtc_state->mode;
	adj_mode = &crtc_state->adjusted_mode;
	global_state = dpu_kms_get_global_state(crtc_state->state);
	if (IS_ERR(global_state))
		return PTR_ERR(global_state);

	trace_dpu_enc_atomic_check(DRMID(drm_enc));

	/* perform atomic check on the first physical encoder (master) */
	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (phys->ops.atomic_check)
			ret = phys->ops.atomic_check(phys, crtc_state,
					conn_state);
		else if (phys->ops.mode_fixup)
			if (!phys->ops.mode_fixup(phys, mode, adj_mode))
				ret = -EINVAL;

		if (ret) {
			DPU_ERROR_ENC(dpu_enc,
					"mode unsupported, phys idx %d\n", i);
			break;
		}
	}

	topology = dpu_encoder_get_topology(dpu_enc, dpu_kms, adj_mode);

	/* Reserve dynamic resources now. */
	if (!ret) {
		/*
		 * Release and Allocate resources on every modeset
		 * Dont allocate when active is false.
		 */
		if (drm_atomic_crtc_needs_modeset(crtc_state)) {
			dpu_rm_release(global_state, drm_enc);

			if (!crtc_state->active_changed || crtc_state->active)
				ret = dpu_rm_reserve(&dpu_kms->rm, global_state,
						drm_enc, crtc_state, topology);
		}
	}

	trace_dpu_enc_atomic_check_flags(DRMID(drm_enc), adj_mode->flags);

	return ret;
}

static void _dpu_encoder_update_vsync_source(struct dpu_encoder_virt *dpu_enc,
			struct msm_display_info *disp_info)
{
	struct dpu_vsync_source_cfg vsync_cfg = { 0 };
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms;
	struct dpu_hw_mdp *hw_mdptop;
	struct drm_encoder *drm_enc;
	int i;

	if (!dpu_enc || !disp_info) {
		DPU_ERROR("invalid param dpu_enc:%d or disp_info:%d\n",
					dpu_enc != NULL, disp_info != NULL);
		return;
	} else if (dpu_enc->num_phys_encs > ARRAY_SIZE(dpu_enc->hw_pp)) {
		DPU_ERROR("invalid num phys enc %d/%d\n",
				dpu_enc->num_phys_encs,
				(int) ARRAY_SIZE(dpu_enc->hw_pp));
		return;
	}

	drm_enc = &dpu_enc->base;
	/* this pointers are checked in virt_enable_helper */
	priv = drm_enc->dev->dev_private;

	dpu_kms = to_dpu_kms(priv->kms);
	hw_mdptop = dpu_kms->hw_mdp;
	if (!hw_mdptop) {
		DPU_ERROR("invalid mdptop\n");
		return;
	}

	if (hw_mdptop->ops.setup_vsync_source &&
			disp_info->capabilities & MSM_DISPLAY_CAP_CMD_MODE) {
		for (i = 0; i < dpu_enc->num_phys_encs; i++)
			vsync_cfg.ppnumber[i] = dpu_enc->hw_pp[i]->idx;

		vsync_cfg.pp_count = dpu_enc->num_phys_encs;
		if (disp_info->is_te_using_watchdog_timer)
			vsync_cfg.vsync_source = DPU_VSYNC_SOURCE_WD_TIMER_0;
		else
			vsync_cfg.vsync_source = DPU_VSYNC0_SOURCE_GPIO;

		hw_mdptop->ops.setup_vsync_source(hw_mdptop, &vsync_cfg);
	}
}

static void _dpu_encoder_irq_control(struct drm_encoder *drm_enc, bool enable)
{
	struct dpu_encoder_virt *dpu_enc;
	int i;

	if (!drm_enc) {
		DPU_ERROR("invalid encoder\n");
		return;
	}

	dpu_enc = to_dpu_encoder_virt(drm_enc);

	DPU_DEBUG_ENC(dpu_enc, "enable:%d\n", enable);
	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (phys->ops.irq_control)
			phys->ops.irq_control(phys, enable);
	}

}

static void _dpu_encoder_resource_control_helper(struct drm_encoder *drm_enc,
		bool enable)
{
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms;
	struct dpu_encoder_virt *dpu_enc;

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	priv = drm_enc->dev->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);

	trace_dpu_enc_rc_helper(DRMID(drm_enc), enable);

	if (!dpu_enc->cur_master) {
		DPU_ERROR("encoder master not set\n");
		return;
	}

	if (enable) {
		/* enable DPU core clks */
		pm_runtime_get_sync(&dpu_kms->pdev->dev);

		/* enable all the irq */
		_dpu_encoder_irq_control(drm_enc, true);

	} else {
		/* disable all the irq */
		_dpu_encoder_irq_control(drm_enc, false);

		/* disable DPU core clks */
		pm_runtime_put_sync(&dpu_kms->pdev->dev);
	}

}

static int dpu_encoder_resource_control(struct drm_encoder *drm_enc,
		u32 sw_event)
{
	struct dpu_encoder_virt *dpu_enc;
	struct msm_drm_private *priv;
	bool is_vid_mode = false;

	if (!drm_enc || !drm_enc->dev || !drm_enc->crtc) {
		DPU_ERROR("invalid parameters\n");
		return -EINVAL;
	}
	dpu_enc = to_dpu_encoder_virt(drm_enc);
	priv = drm_enc->dev->dev_private;
	is_vid_mode = dpu_enc->disp_info.capabilities &
						MSM_DISPLAY_CAP_VID_MODE;

	/*
	 * when idle_pc is not supported, process only KICKOFF, STOP and MODESET
	 * events and return early for other events (ie wb display).
	 */
	if (!dpu_enc->idle_pc_supported &&
			(sw_event != DPU_ENC_RC_EVENT_KICKOFF &&
			sw_event != DPU_ENC_RC_EVENT_STOP &&
			sw_event != DPU_ENC_RC_EVENT_PRE_STOP))
		return 0;

	trace_dpu_enc_rc(DRMID(drm_enc), sw_event, dpu_enc->idle_pc_supported,
			 dpu_enc->rc_state, "begin");

	switch (sw_event) {
	case DPU_ENC_RC_EVENT_KICKOFF:
		/* cancel delayed off work, if any */
		if (cancel_delayed_work_sync(&dpu_enc->delayed_off_work))
			DPU_DEBUG_ENC(dpu_enc, "sw_event:%d, work cancelled\n",
					sw_event);

		mutex_lock(&dpu_enc->rc_lock);

		/* return if the resource control is already in ON state */
		if (dpu_enc->rc_state == DPU_ENC_RC_STATE_ON) {
			DRM_DEBUG_ATOMIC("id;%u, sw_event:%d, rc in ON state\n",
				      DRMID(drm_enc), sw_event);
			mutex_unlock(&dpu_enc->rc_lock);
			return 0;
		} else if (dpu_enc->rc_state != DPU_ENC_RC_STATE_OFF &&
				dpu_enc->rc_state != DPU_ENC_RC_STATE_IDLE) {
			DRM_DEBUG_ATOMIC("id;%u, sw_event:%d, rc in state %d\n",
				      DRMID(drm_enc), sw_event,
				      dpu_enc->rc_state);
			mutex_unlock(&dpu_enc->rc_lock);
			return -EINVAL;
		}

		if (is_vid_mode && dpu_enc->rc_state == DPU_ENC_RC_STATE_IDLE)
			_dpu_encoder_irq_control(drm_enc, true);
		else
			_dpu_encoder_resource_control_helper(drm_enc, true);

		dpu_enc->rc_state = DPU_ENC_RC_STATE_ON;

		trace_dpu_enc_rc(DRMID(drm_enc), sw_event,
				 dpu_enc->idle_pc_supported, dpu_enc->rc_state,
				 "kickoff");

		mutex_unlock(&dpu_enc->rc_lock);
		break;

	case DPU_ENC_RC_EVENT_FRAME_DONE:
		/*
		 * mutex lock is not used as this event happens at interrupt
		 * context. And locking is not required as, the other events
		 * like KICKOFF and STOP does a wait-for-idle before executing
		 * the resource_control
		 */
		if (dpu_enc->rc_state != DPU_ENC_RC_STATE_ON) {
			DRM_DEBUG_KMS("id:%d, sw_event:%d,rc:%d-unexpected\n",
				      DRMID(drm_enc), sw_event,
				      dpu_enc->rc_state);
			return -EINVAL;
		}

		/*
		 * schedule off work item only when there are no
		 * frames pending
		 */
		if (dpu_crtc_frame_pending(drm_enc->crtc) > 1) {
			DRM_DEBUG_KMS("id:%d skip schedule work\n",
				      DRMID(drm_enc));
			return 0;
		}

		queue_delayed_work(priv->wq, &dpu_enc->delayed_off_work,
				   msecs_to_jiffies(dpu_enc->idle_timeout));

		trace_dpu_enc_rc(DRMID(drm_enc), sw_event,
				 dpu_enc->idle_pc_supported, dpu_enc->rc_state,
				 "frame done");
		break;

	case DPU_ENC_RC_EVENT_PRE_STOP:
		/* cancel delayed off work, if any */
		if (cancel_delayed_work_sync(&dpu_enc->delayed_off_work))
			DPU_DEBUG_ENC(dpu_enc, "sw_event:%d, work cancelled\n",
					sw_event);

		mutex_lock(&dpu_enc->rc_lock);

		if (is_vid_mode &&
			  dpu_enc->rc_state == DPU_ENC_RC_STATE_IDLE) {
			_dpu_encoder_irq_control(drm_enc, true);
		}
		/* skip if is already OFF or IDLE, resources are off already */
		else if (dpu_enc->rc_state == DPU_ENC_RC_STATE_OFF ||
				dpu_enc->rc_state == DPU_ENC_RC_STATE_IDLE) {
			DRM_DEBUG_KMS("id:%u, sw_event:%d, rc in %d state\n",
				      DRMID(drm_enc), sw_event,
				      dpu_enc->rc_state);
			mutex_unlock(&dpu_enc->rc_lock);
			return 0;
		}

		dpu_enc->rc_state = DPU_ENC_RC_STATE_PRE_OFF;

		trace_dpu_enc_rc(DRMID(drm_enc), sw_event,
				 dpu_enc->idle_pc_supported, dpu_enc->rc_state,
				 "pre stop");

		mutex_unlock(&dpu_enc->rc_lock);
		break;

	case DPU_ENC_RC_EVENT_STOP:
		mutex_lock(&dpu_enc->rc_lock);

		/* return if the resource control is already in OFF state */
		if (dpu_enc->rc_state == DPU_ENC_RC_STATE_OFF) {
			DRM_DEBUG_KMS("id: %u, sw_event:%d, rc in OFF state\n",
				      DRMID(drm_enc), sw_event);
			mutex_unlock(&dpu_enc->rc_lock);
			return 0;
		} else if (dpu_enc->rc_state == DPU_ENC_RC_STATE_ON) {
			DRM_ERROR("id: %u, sw_event:%d, rc in state %d\n",
				  DRMID(drm_enc), sw_event, dpu_enc->rc_state);
			mutex_unlock(&dpu_enc->rc_lock);
			return -EINVAL;
		}

		/**
		 * expect to arrive here only if in either idle state or pre-off
		 * and in IDLE state the resources are already disabled
		 */
		if (dpu_enc->rc_state == DPU_ENC_RC_STATE_PRE_OFF)
			_dpu_encoder_resource_control_helper(drm_enc, false);

		dpu_enc->rc_state = DPU_ENC_RC_STATE_OFF;

		trace_dpu_enc_rc(DRMID(drm_enc), sw_event,
				 dpu_enc->idle_pc_supported, dpu_enc->rc_state,
				 "stop");

		mutex_unlock(&dpu_enc->rc_lock);
		break;

	case DPU_ENC_RC_EVENT_ENTER_IDLE:
		mutex_lock(&dpu_enc->rc_lock);

		if (dpu_enc->rc_state != DPU_ENC_RC_STATE_ON) {
			DRM_ERROR("id: %u, sw_event:%d, rc:%d !ON state\n",
				  DRMID(drm_enc), sw_event, dpu_enc->rc_state);
			mutex_unlock(&dpu_enc->rc_lock);
			return 0;
		}

		/*
		 * if we are in ON but a frame was just kicked off,
		 * ignore the IDLE event, it's probably a stale timer event
		 */
		if (dpu_enc->frame_busy_mask[0]) {
			DRM_ERROR("id:%u, sw_event:%d, rc:%d frame pending\n",
				  DRMID(drm_enc), sw_event, dpu_enc->rc_state);
			mutex_unlock(&dpu_enc->rc_lock);
			return 0;
		}

		if (is_vid_mode)
			_dpu_encoder_irq_control(drm_enc, false);
		else
			_dpu_encoder_resource_control_helper(drm_enc, false);

		dpu_enc->rc_state = DPU_ENC_RC_STATE_IDLE;

		trace_dpu_enc_rc(DRMID(drm_enc), sw_event,
				 dpu_enc->idle_pc_supported, dpu_enc->rc_state,
				 "idle");

		mutex_unlock(&dpu_enc->rc_lock);
		break;

	default:
		DRM_ERROR("id:%u, unexpected sw_event: %d\n", DRMID(drm_enc),
			  sw_event);
		trace_dpu_enc_rc(DRMID(drm_enc), sw_event,
				 dpu_enc->idle_pc_supported, dpu_enc->rc_state,
				 "error");
		break;
	}

	trace_dpu_enc_rc(DRMID(drm_enc), sw_event,
			 dpu_enc->idle_pc_supported, dpu_enc->rc_state,
			 "end");
	return 0;
}

static void dpu_encoder_virt_mode_set(struct drm_encoder *drm_enc,
				      struct drm_display_mode *mode,
				      struct drm_display_mode *adj_mode)
{
	struct dpu_encoder_virt *dpu_enc;
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms;
	struct list_head *connector_list;
	struct drm_connector *conn = NULL, *conn_iter;
	struct drm_crtc *drm_crtc;
	struct dpu_crtc_state *cstate;
	struct dpu_global_state *global_state;
	struct dpu_hw_blk *hw_pp[MAX_CHANNELS_PER_ENC];
	struct dpu_hw_blk *hw_ctl[MAX_CHANNELS_PER_ENC];
	struct dpu_hw_blk *hw_lm[MAX_CHANNELS_PER_ENC];
	struct dpu_hw_blk *hw_dspp[MAX_CHANNELS_PER_ENC] = { NULL };
	int num_lm, num_ctl, num_pp;
	int i, j;

	if (!drm_enc) {
		DPU_ERROR("invalid encoder\n");
		return;
	}

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	DPU_DEBUG_ENC(dpu_enc, "\n");

	priv = drm_enc->dev->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);
	connector_list = &dpu_kms->dev->mode_config.connector_list;

	global_state = dpu_kms_get_existing_global_state(dpu_kms);
	if (IS_ERR_OR_NULL(global_state)) {
		DPU_ERROR("Failed to get global state");
		return;
	}

	trace_dpu_enc_mode_set(DRMID(drm_enc));

	if (drm_enc->encoder_type == DRM_MODE_ENCODER_TMDS)
		msm_dp_display_mode_set(dpu_enc->dp, drm_enc, mode, adj_mode);

	list_for_each_entry(conn_iter, connector_list, head)
		if (conn_iter->encoder == drm_enc)
			conn = conn_iter;

	if (!conn) {
		DPU_ERROR_ENC(dpu_enc, "failed to find attached connector\n");
		return;
	} else if (!conn->state) {
		DPU_ERROR_ENC(dpu_enc, "invalid connector state\n");
		return;
	}

	drm_for_each_crtc(drm_crtc, drm_enc->dev)
		if (drm_crtc->state->encoder_mask & drm_encoder_mask(drm_enc))
			break;

	/* Query resource that have been reserved in atomic check step. */
	num_pp = dpu_rm_get_assigned_resources(&dpu_kms->rm, global_state,
		drm_enc->base.id, DPU_HW_BLK_PINGPONG, hw_pp,
		ARRAY_SIZE(hw_pp));
	num_ctl = dpu_rm_get_assigned_resources(&dpu_kms->rm, global_state,
		drm_enc->base.id, DPU_HW_BLK_CTL, hw_ctl, ARRAY_SIZE(hw_ctl));
	num_lm = dpu_rm_get_assigned_resources(&dpu_kms->rm, global_state,
		drm_enc->base.id, DPU_HW_BLK_LM, hw_lm, ARRAY_SIZE(hw_lm));
	dpu_rm_get_assigned_resources(&dpu_kms->rm, global_state,
		drm_enc->base.id, DPU_HW_BLK_DSPP, hw_dspp,
		ARRAY_SIZE(hw_dspp));

	for (i = 0; i < MAX_CHANNELS_PER_ENC; i++)
		dpu_enc->hw_pp[i] = i < num_pp ? to_dpu_hw_pingpong(hw_pp[i])
						: NULL;

	cstate = to_dpu_crtc_state(drm_crtc->state);

	for (i = 0; i < num_lm; i++) {
		int ctl_idx = (i < num_ctl) ? i : (num_ctl-1);

		cstate->mixers[i].hw_lm = to_dpu_hw_mixer(hw_lm[i]);
		cstate->mixers[i].lm_ctl = to_dpu_hw_ctl(hw_ctl[ctl_idx]);
		cstate->mixers[i].hw_dspp = to_dpu_hw_dspp(hw_dspp[i]);
	}

	cstate->num_mixers = num_lm;

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		int num_blk;
		struct dpu_hw_blk *hw_blk[MAX_CHANNELS_PER_ENC];
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (!dpu_enc->hw_pp[i]) {
			DPU_ERROR_ENC(dpu_enc,
				"no pp block assigned at idx: %d\n", i);
			return;
		}

		if (!hw_ctl[i]) {
			DPU_ERROR_ENC(dpu_enc,
				"no ctl block assigned at idx: %d\n", i);
			return;
		}

		phys->hw_pp = dpu_enc->hw_pp[i];
		phys->hw_ctl = to_dpu_hw_ctl(hw_ctl[i]);

		num_blk = dpu_rm_get_assigned_resources(&dpu_kms->rm,
			global_state, drm_enc->base.id, DPU_HW_BLK_INTF,
			hw_blk, ARRAY_SIZE(hw_blk));
		for (j = 0; j < num_blk; j++) {
			struct dpu_hw_intf *hw_intf;

			hw_intf = to_dpu_hw_intf(hw_blk[i]);
			if (hw_intf->idx == phys->intf_idx)
				phys->hw_intf = hw_intf;
		}

		if (!phys->hw_intf) {
			DPU_ERROR_ENC(dpu_enc,
				      "no intf block assigned at idx: %d\n", i);
			return;
		}

		phys->connector = conn->state->connector;
		if (phys->ops.mode_set)
			phys->ops.mode_set(phys, mode, adj_mode);
	}
}

static void _dpu_encoder_virt_enable_helper(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc = NULL;
	int i;

	if (!drm_enc || !drm_enc->dev) {
		DPU_ERROR("invalid parameters\n");
		return;
	}

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	if (!dpu_enc || !dpu_enc->cur_master) {
		DPU_ERROR("invalid dpu encoder/master\n");
		return;
	}


	if (dpu_enc->disp_info.intf_type == DRM_MODE_CONNECTOR_DisplayPort &&
		dpu_enc->cur_master->hw_mdptop &&
		dpu_enc->cur_master->hw_mdptop->ops.intf_audio_select)
		dpu_enc->cur_master->hw_mdptop->ops.intf_audio_select(
			dpu_enc->cur_master->hw_mdptop);

	_dpu_encoder_update_vsync_source(dpu_enc, &dpu_enc->disp_info);

	if (dpu_enc->disp_info.intf_type == DRM_MODE_ENCODER_DSI &&
			!WARN_ON(dpu_enc->num_phys_encs == 0)) {
		unsigned bpc = dpu_enc->phys_encs[0]->connector->display_info.bpc;
		for (i = 0; i < MAX_CHANNELS_PER_ENC; i++) {
			if (!dpu_enc->hw_pp[i])
				continue;
			_dpu_encoder_setup_dither(dpu_enc->hw_pp[i], bpc);
		}
	}
}

void dpu_encoder_virt_runtime_resume(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(drm_enc);

	mutex_lock(&dpu_enc->enc_lock);

	if (!dpu_enc->enabled)
		goto out;

	if (dpu_enc->cur_slave && dpu_enc->cur_slave->ops.restore)
		dpu_enc->cur_slave->ops.restore(dpu_enc->cur_slave);
	if (dpu_enc->cur_master && dpu_enc->cur_master->ops.restore)
		dpu_enc->cur_master->ops.restore(dpu_enc->cur_master);

	_dpu_encoder_virt_enable_helper(drm_enc);

out:
	mutex_unlock(&dpu_enc->enc_lock);
}

static void dpu_encoder_virt_enable(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc = NULL;
	int ret = 0;
	struct msm_drm_private *priv;
	struct drm_display_mode *cur_mode = NULL;

	if (!drm_enc) {
		DPU_ERROR("invalid encoder\n");
		return;
	}
	dpu_enc = to_dpu_encoder_virt(drm_enc);

	mutex_lock(&dpu_enc->enc_lock);
	cur_mode = &dpu_enc->base.crtc->state->adjusted_mode;
	priv = drm_enc->dev->dev_private;

	trace_dpu_enc_enable(DRMID(drm_enc), cur_mode->hdisplay,
			     cur_mode->vdisplay);

	/* always enable slave encoder before master */
	if (dpu_enc->cur_slave && dpu_enc->cur_slave->ops.enable)
		dpu_enc->cur_slave->ops.enable(dpu_enc->cur_slave);

	if (dpu_enc->cur_master && dpu_enc->cur_master->ops.enable)
		dpu_enc->cur_master->ops.enable(dpu_enc->cur_master);

	ret = dpu_encoder_resource_control(drm_enc, DPU_ENC_RC_EVENT_KICKOFF);
	if (ret) {
		DPU_ERROR_ENC(dpu_enc, "dpu resource control failed: %d\n",
				ret);
		goto out;
	}

	_dpu_encoder_virt_enable_helper(drm_enc);

	if (drm_enc->encoder_type == DRM_MODE_ENCODER_TMDS) {
		ret = msm_dp_display_enable(dpu_enc->dp, drm_enc);
		if (ret) {
			DPU_ERROR_ENC(dpu_enc, "dp display enable failed: %d\n",
				ret);
			goto out;
		}
	}
	dpu_enc->enabled = true;

out:
	mutex_unlock(&dpu_enc->enc_lock);
}

static void dpu_encoder_virt_disable(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc = NULL;
	struct msm_drm_private *priv;
	int i = 0;

	if (!drm_enc) {
		DPU_ERROR("invalid encoder\n");
		return;
	} else if (!drm_enc->dev) {
		DPU_ERROR("invalid dev\n");
		return;
	}

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	DPU_DEBUG_ENC(dpu_enc, "\n");

	mutex_lock(&dpu_enc->enc_lock);
	dpu_enc->enabled = false;

	priv = drm_enc->dev->dev_private;

	trace_dpu_enc_disable(DRMID(drm_enc));

	/* wait for idle */
	dpu_encoder_wait_for_event(drm_enc, MSM_ENC_TX_COMPLETE);

	if (drm_enc->encoder_type == DRM_MODE_ENCODER_TMDS) {
		if (msm_dp_display_pre_disable(dpu_enc->dp, drm_enc))
			DPU_ERROR_ENC(dpu_enc, "dp display push idle failed\n");
	}

	dpu_encoder_resource_control(drm_enc, DPU_ENC_RC_EVENT_PRE_STOP);

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (phys->ops.disable)
			phys->ops.disable(phys);
	}


	/* after phys waits for frame-done, should be no more frames pending */
	if (atomic_xchg(&dpu_enc->frame_done_timeout_ms, 0)) {
		DPU_ERROR("enc%d timeout pending\n", drm_enc->base.id);
		del_timer_sync(&dpu_enc->frame_done_timer);
	}

	dpu_encoder_resource_control(drm_enc, DPU_ENC_RC_EVENT_STOP);

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		dpu_enc->phys_encs[i]->connector = NULL;
	}

	DPU_DEBUG_ENC(dpu_enc, "encoder disabled\n");

	if (drm_enc->encoder_type == DRM_MODE_ENCODER_TMDS) {
		if (msm_dp_display_disable(dpu_enc->dp, drm_enc))
			DPU_ERROR_ENC(dpu_enc, "dp display disable failed\n");
	}

	mutex_unlock(&dpu_enc->enc_lock);
}

static enum dpu_intf dpu_encoder_get_intf(struct dpu_mdss_cfg *catalog,
		enum dpu_intf_type type, u32 controller_id)
{
	int i = 0;

	for (i = 0; i < catalog->intf_count; i++) {
		if (catalog->intf[i].type == type
		    && catalog->intf[i].controller_id == controller_id) {
			return catalog->intf[i].id;
		}
	}

	return INTF_MAX;
}

static void dpu_encoder_vblank_callback(struct drm_encoder *drm_enc,
		struct dpu_encoder_phys *phy_enc)
{
	struct dpu_encoder_virt *dpu_enc = NULL;
	unsigned long lock_flags;

	if (!drm_enc || !phy_enc)
		return;

	DPU_ATRACE_BEGIN("encoder_vblank_callback");
	dpu_enc = to_dpu_encoder_virt(drm_enc);

	spin_lock_irqsave(&dpu_enc->enc_spinlock, lock_flags);
	if (dpu_enc->crtc)
		dpu_crtc_vblank_callback(dpu_enc->crtc);
	spin_unlock_irqrestore(&dpu_enc->enc_spinlock, lock_flags);

	atomic_inc(&phy_enc->vsync_cnt);
	DPU_ATRACE_END("encoder_vblank_callback");
}

static void dpu_encoder_underrun_callback(struct drm_encoder *drm_enc,
		struct dpu_encoder_phys *phy_enc)
{
	if (!phy_enc)
		return;

	DPU_ATRACE_BEGIN("encoder_underrun_callback");
	atomic_inc(&phy_enc->underrun_cnt);

	/* trigger dump only on the first underrun */
	if (atomic_read(&phy_enc->underrun_cnt) == 1)
		msm_disp_snapshot_state(drm_enc->dev);

	trace_dpu_enc_underrun_cb(DRMID(drm_enc),
				  atomic_read(&phy_enc->underrun_cnt));
	DPU_ATRACE_END("encoder_underrun_callback");
}

void dpu_encoder_assign_crtc(struct drm_encoder *drm_enc, struct drm_crtc *crtc)
{
	struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(drm_enc);
	unsigned long lock_flags;

	spin_lock_irqsave(&dpu_enc->enc_spinlock, lock_flags);
	/* crtc should always be cleared before re-assigning */
	WARN_ON(crtc && dpu_enc->crtc);
	dpu_enc->crtc = crtc;
	spin_unlock_irqrestore(&dpu_enc->enc_spinlock, lock_flags);
}

void dpu_encoder_toggle_vblank_for_crtc(struct drm_encoder *drm_enc,
					struct drm_crtc *crtc, bool enable)
{
	struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(drm_enc);
	unsigned long lock_flags;
	int i;

	trace_dpu_enc_vblank_cb(DRMID(drm_enc), enable);

	spin_lock_irqsave(&dpu_enc->enc_spinlock, lock_flags);
	if (dpu_enc->crtc != crtc) {
		spin_unlock_irqrestore(&dpu_enc->enc_spinlock, lock_flags);
		return;
	}
	spin_unlock_irqrestore(&dpu_enc->enc_spinlock, lock_flags);

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (phys->ops.control_vblank_irq)
			phys->ops.control_vblank_irq(phys, enable);
	}
}

void dpu_encoder_register_frame_event_callback(struct drm_encoder *drm_enc,
		void (*frame_event_cb)(void *, u32 event),
		void *frame_event_cb_data)
{
	struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(drm_enc);
	unsigned long lock_flags;
	bool enable;

	enable = frame_event_cb ? true : false;

	if (!drm_enc) {
		DPU_ERROR("invalid encoder\n");
		return;
	}
	trace_dpu_enc_frame_event_cb(DRMID(drm_enc), enable);

	spin_lock_irqsave(&dpu_enc->enc_spinlock, lock_flags);
	dpu_enc->crtc_frame_event_cb = frame_event_cb;
	dpu_enc->crtc_frame_event_cb_data = frame_event_cb_data;
	spin_unlock_irqrestore(&dpu_enc->enc_spinlock, lock_flags);
}

static void dpu_encoder_frame_done_callback(
		struct drm_encoder *drm_enc,
		struct dpu_encoder_phys *ready_phys, u32 event)
{
	struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(drm_enc);
	unsigned int i;

	if (event & (DPU_ENCODER_FRAME_EVENT_DONE
			| DPU_ENCODER_FRAME_EVENT_ERROR
			| DPU_ENCODER_FRAME_EVENT_PANEL_DEAD)) {

		if (!dpu_enc->frame_busy_mask[0]) {
			/**
			 * suppress frame_done without waiter,
			 * likely autorefresh
			 */
			trace_dpu_enc_frame_done_cb_not_busy(DRMID(drm_enc),
					event, ready_phys->intf_idx);
			return;
		}

		/* One of the physical encoders has become idle */
		for (i = 0; i < dpu_enc->num_phys_encs; i++) {
			if (dpu_enc->phys_encs[i] == ready_phys) {
				trace_dpu_enc_frame_done_cb(DRMID(drm_enc), i,
						dpu_enc->frame_busy_mask[0]);
				clear_bit(i, dpu_enc->frame_busy_mask);
			}
		}

		if (!dpu_enc->frame_busy_mask[0]) {
			atomic_set(&dpu_enc->frame_done_timeout_ms, 0);
			del_timer(&dpu_enc->frame_done_timer);

			dpu_encoder_resource_control(drm_enc,
					DPU_ENC_RC_EVENT_FRAME_DONE);

			if (dpu_enc->crtc_frame_event_cb)
				dpu_enc->crtc_frame_event_cb(
					dpu_enc->crtc_frame_event_cb_data,
					event);
		}
	} else {
		if (dpu_enc->crtc_frame_event_cb)
			dpu_enc->crtc_frame_event_cb(
				dpu_enc->crtc_frame_event_cb_data, event);
	}
}

static void dpu_encoder_off_work(struct work_struct *work)
{
	struct dpu_encoder_virt *dpu_enc = container_of(work,
			struct dpu_encoder_virt, delayed_off_work.work);

	dpu_encoder_resource_control(&dpu_enc->base,
						DPU_ENC_RC_EVENT_ENTER_IDLE);

	dpu_encoder_frame_done_callback(&dpu_enc->base, NULL,
				DPU_ENCODER_FRAME_EVENT_IDLE);
}

/**
 * _dpu_encoder_trigger_flush - trigger flush for a physical encoder
 * @drm_enc: Pointer to drm encoder structure
 * @phys: Pointer to physical encoder structure
 * @extra_flush_bits: Additional bit mask to include in flush trigger
 */
static void _dpu_encoder_trigger_flush(struct drm_encoder *drm_enc,
		struct dpu_encoder_phys *phys, uint32_t extra_flush_bits)
{
	struct dpu_hw_ctl *ctl;
	int pending_kickoff_cnt;
	u32 ret = UINT_MAX;

	if (!phys->hw_pp) {
		DPU_ERROR("invalid pingpong hw\n");
		return;
	}

	ctl = phys->hw_ctl;
	if (!ctl->ops.trigger_flush) {
		DPU_ERROR("missing trigger cb\n");
		return;
	}

	pending_kickoff_cnt = dpu_encoder_phys_inc_pending(phys);

	if (extra_flush_bits && ctl->ops.update_pending_flush)
		ctl->ops.update_pending_flush(ctl, extra_flush_bits);

	ctl->ops.trigger_flush(ctl);

	if (ctl->ops.get_pending_flush)
		ret = ctl->ops.get_pending_flush(ctl);

	trace_dpu_enc_trigger_flush(DRMID(drm_enc), phys->intf_idx,
				    pending_kickoff_cnt, ctl->idx,
				    extra_flush_bits, ret);
}

/**
 * _dpu_encoder_trigger_start - trigger start for a physical encoder
 * @phys: Pointer to physical encoder structure
 */
static void _dpu_encoder_trigger_start(struct dpu_encoder_phys *phys)
{
	if (!phys) {
		DPU_ERROR("invalid argument(s)\n");
		return;
	}

	if (!phys->hw_pp) {
		DPU_ERROR("invalid pingpong hw\n");
		return;
	}

	if (phys->ops.trigger_start && phys->enable_state != DPU_ENC_DISABLED)
		phys->ops.trigger_start(phys);
}

void dpu_encoder_helper_trigger_start(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_ctl *ctl;

	ctl = phys_enc->hw_ctl;
	if (ctl->ops.trigger_start) {
		ctl->ops.trigger_start(ctl);
		trace_dpu_enc_trigger_start(DRMID(phys_enc->parent), ctl->idx);
	}
}

static int dpu_encoder_helper_wait_event_timeout(
		int32_t drm_id,
		u32 irq_idx,
		struct dpu_encoder_wait_info *info)
{
	int rc = 0;
	s64 expected_time = ktime_to_ms(ktime_get()) + info->timeout_ms;
	s64 jiffies = msecs_to_jiffies(info->timeout_ms);
	s64 time;

	do {
		rc = wait_event_timeout(*(info->wq),
				atomic_read(info->atomic_cnt) == 0, jiffies);
		time = ktime_to_ms(ktime_get());

		trace_dpu_enc_wait_event_timeout(drm_id, irq_idx, rc, time,
						 expected_time,
						 atomic_read(info->atomic_cnt));
	/* If we timed out, counter is valid and time is less, wait again */
	} while (atomic_read(info->atomic_cnt) && (rc == 0) &&
			(time < expected_time));

	return rc;
}

static void dpu_encoder_helper_hw_reset(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_virt *dpu_enc;
	struct dpu_hw_ctl *ctl;
	int rc;
	struct drm_encoder *drm_enc;

	dpu_enc = to_dpu_encoder_virt(phys_enc->parent);
	ctl = phys_enc->hw_ctl;
	drm_enc = phys_enc->parent;

	if (!ctl->ops.reset)
		return;

	DRM_DEBUG_KMS("id:%u ctl %d reset\n", DRMID(drm_enc),
		      ctl->idx);

	rc = ctl->ops.reset(ctl);
	if (rc) {
		DPU_ERROR_ENC(dpu_enc, "ctl %d reset failure\n",  ctl->idx);
		msm_disp_snapshot_state(drm_enc->dev);
	}

	phys_enc->enable_state = DPU_ENC_ENABLED;
}

/**
 * _dpu_encoder_kickoff_phys - handle physical encoder kickoff
 *	Iterate through the physical encoders and perform consolidated flush
 *	and/or control start triggering as needed. This is done in the virtual
 *	encoder rather than the individual physical ones in order to handle
 *	use cases that require visibility into multiple physical encoders at
 *	a time.
 * @dpu_enc: Pointer to virtual encoder structure
 */
static void _dpu_encoder_kickoff_phys(struct dpu_encoder_virt *dpu_enc)
{
	struct dpu_hw_ctl *ctl;
	uint32_t i, pending_flush;
	unsigned long lock_flags;

	pending_flush = 0x0;

	/* update pending counts and trigger kickoff ctl flush atomically */
	spin_lock_irqsave(&dpu_enc->enc_spinlock, lock_flags);

	/* don't perform flush/start operations for slave encoders */
	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (phys->enable_state == DPU_ENC_DISABLED)
			continue;

		ctl = phys->hw_ctl;

		/*
		 * This is cleared in frame_done worker, which isn't invoked
		 * for async commits. So don't set this for async, since it'll
		 * roll over to the next commit.
		 */
		if (phys->split_role != ENC_ROLE_SLAVE)
			set_bit(i, dpu_enc->frame_busy_mask);

		if (!phys->ops.needs_single_flush ||
				!phys->ops.needs_single_flush(phys))
			_dpu_encoder_trigger_flush(&dpu_enc->base, phys, 0x0);
		else if (ctl->ops.get_pending_flush)
			pending_flush |= ctl->ops.get_pending_flush(ctl);
	}

	/* for split flush, combine pending flush masks and send to master */
	if (pending_flush && dpu_enc->cur_master) {
		_dpu_encoder_trigger_flush(
				&dpu_enc->base,
				dpu_enc->cur_master,
				pending_flush);
	}

	_dpu_encoder_trigger_start(dpu_enc->cur_master);

	spin_unlock_irqrestore(&dpu_enc->enc_spinlock, lock_flags);
}

void dpu_encoder_trigger_kickoff_pending(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc;
	struct dpu_encoder_phys *phys;
	unsigned int i;
	struct dpu_hw_ctl *ctl;
	struct msm_display_info *disp_info;

	if (!drm_enc) {
		DPU_ERROR("invalid encoder\n");
		return;
	}
	dpu_enc = to_dpu_encoder_virt(drm_enc);
	disp_info = &dpu_enc->disp_info;

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		phys = dpu_enc->phys_encs[i];

		ctl = phys->hw_ctl;
		if (ctl->ops.clear_pending_flush)
			ctl->ops.clear_pending_flush(ctl);

		/* update only for command mode primary ctl */
		if ((phys == dpu_enc->cur_master) &&
		   (disp_info->capabilities & MSM_DISPLAY_CAP_CMD_MODE)
		    && ctl->ops.trigger_pending)
			ctl->ops.trigger_pending(ctl);
	}
}

static u32 _dpu_encoder_calculate_linetime(struct dpu_encoder_virt *dpu_enc,
		struct drm_display_mode *mode)
{
	u64 pclk_rate;
	u32 pclk_period;
	u32 line_time;

	/*
	 * For linetime calculation, only operate on master encoder.
	 */
	if (!dpu_enc->cur_master)
		return 0;

	if (!dpu_enc->cur_master->ops.get_line_count) {
		DPU_ERROR("get_line_count function not defined\n");
		return 0;
	}

	pclk_rate = mode->clock; /* pixel clock in kHz */
	if (pclk_rate == 0) {
		DPU_ERROR("pclk is 0, cannot calculate line time\n");
		return 0;
	}

	pclk_period = DIV_ROUND_UP_ULL(1000000000ull, pclk_rate);
	if (pclk_period == 0) {
		DPU_ERROR("pclk period is 0\n");
		return 0;
	}

	/*
	 * Line time calculation based on Pixel clock and HTOTAL.
	 * Final unit is in ns.
	 */
	line_time = (pclk_period * mode->htotal) / 1000;
	if (line_time == 0) {
		DPU_ERROR("line time calculation is 0\n");
		return 0;
	}

	DPU_DEBUG_ENC(dpu_enc,
			"clk_rate=%lldkHz, clk_period=%d, linetime=%dns\n",
			pclk_rate, pclk_period, line_time);

	return line_time;
}

int dpu_encoder_vsync_time(struct drm_encoder *drm_enc, ktime_t *wakeup_time)
{
	struct drm_display_mode *mode;
	struct dpu_encoder_virt *dpu_enc;
	u32 cur_line;
	u32 line_time;
	u32 vtotal, time_to_vsync;
	ktime_t cur_time;

	dpu_enc = to_dpu_encoder_virt(drm_enc);

	if (!drm_enc->crtc || !drm_enc->crtc->state) {
		DPU_ERROR("crtc/crtc state object is NULL\n");
		return -EINVAL;
	}
	mode = &drm_enc->crtc->state->adjusted_mode;

	line_time = _dpu_encoder_calculate_linetime(dpu_enc, mode);
	if (!line_time)
		return -EINVAL;

	cur_line = dpu_enc->cur_master->ops.get_line_count(dpu_enc->cur_master);

	vtotal = mode->vtotal;
	if (cur_line >= vtotal)
		time_to_vsync = line_time * vtotal;
	else
		time_to_vsync = line_time * (vtotal - cur_line);

	if (time_to_vsync == 0) {
		DPU_ERROR("time to vsync should not be zero, vtotal=%d\n",
				vtotal);
		return -EINVAL;
	}

	cur_time = ktime_get();
	*wakeup_time = ktime_add_ns(cur_time, time_to_vsync);

	DPU_DEBUG_ENC(dpu_enc,
			"cur_line=%u vtotal=%u time_to_vsync=%u, cur_time=%lld, wakeup_time=%lld\n",
			cur_line, vtotal, time_to_vsync,
			ktime_to_ms(cur_time),
			ktime_to_ms(*wakeup_time));
	return 0;
}

static void dpu_encoder_vsync_event_handler(struct timer_list *t)
{
	struct dpu_encoder_virt *dpu_enc = from_timer(dpu_enc, t,
			vsync_event_timer);
	struct drm_encoder *drm_enc = &dpu_enc->base;
	struct msm_drm_private *priv;
	struct msm_drm_thread *event_thread;

	if (!drm_enc->dev || !drm_enc->crtc) {
		DPU_ERROR("invalid parameters\n");
		return;
	}

	priv = drm_enc->dev->dev_private;

	if (drm_enc->crtc->index >= ARRAY_SIZE(priv->event_thread)) {
		DPU_ERROR("invalid crtc index\n");
		return;
	}
	event_thread = &priv->event_thread[drm_enc->crtc->index];
	if (!event_thread) {
		DPU_ERROR("event_thread not found for crtc:%d\n",
				drm_enc->crtc->index);
		return;
	}

	del_timer(&dpu_enc->vsync_event_timer);
}

static void dpu_encoder_vsync_event_work_handler(struct kthread_work *work)
{
	struct dpu_encoder_virt *dpu_enc = container_of(work,
			struct dpu_encoder_virt, vsync_event_work);
	ktime_t wakeup_time;

	if (dpu_encoder_vsync_time(&dpu_enc->base, &wakeup_time))
		return;

	trace_dpu_enc_vsync_event_work(DRMID(&dpu_enc->base), wakeup_time);
	mod_timer(&dpu_enc->vsync_event_timer,
			nsecs_to_jiffies(ktime_to_ns(wakeup_time)));
}

void dpu_encoder_prepare_for_kickoff(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc;
	struct dpu_encoder_phys *phys;
	bool needs_hw_reset = false;
	unsigned int i;

	dpu_enc = to_dpu_encoder_virt(drm_enc);

	trace_dpu_enc_prepare_kickoff(DRMID(drm_enc));

	/* prepare for next kickoff, may include waiting on previous kickoff */
	DPU_ATRACE_BEGIN("enc_prepare_for_kickoff");
	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		phys = dpu_enc->phys_encs[i];
		if (phys->ops.prepare_for_kickoff)
			phys->ops.prepare_for_kickoff(phys);
		if (phys->enable_state == DPU_ENC_ERR_NEEDS_HW_RESET)
			needs_hw_reset = true;
	}
	DPU_ATRACE_END("enc_prepare_for_kickoff");

	dpu_encoder_resource_control(drm_enc, DPU_ENC_RC_EVENT_KICKOFF);

	/* if any phys needs reset, reset all phys, in-order */
	if (needs_hw_reset) {
		trace_dpu_enc_prepare_kickoff_reset(DRMID(drm_enc));
		for (i = 0; i < dpu_enc->num_phys_encs; i++) {
			dpu_encoder_helper_hw_reset(dpu_enc->phys_encs[i]);
		}
	}
}

void dpu_encoder_kickoff(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc;
	struct dpu_encoder_phys *phys;
	ktime_t wakeup_time;
	unsigned long timeout_ms;
	unsigned int i;

	DPU_ATRACE_BEGIN("encoder_kickoff");
	dpu_enc = to_dpu_encoder_virt(drm_enc);

	trace_dpu_enc_kickoff(DRMID(drm_enc));

	timeout_ms = DPU_ENCODER_FRAME_DONE_TIMEOUT_FRAMES * 1000 /
			drm_mode_vrefresh(&drm_enc->crtc->state->adjusted_mode);

	atomic_set(&dpu_enc->frame_done_timeout_ms, timeout_ms);
	mod_timer(&dpu_enc->frame_done_timer,
			jiffies + msecs_to_jiffies(timeout_ms));

	/* All phys encs are ready to go, trigger the kickoff */
	_dpu_encoder_kickoff_phys(dpu_enc);

	/* allow phys encs to handle any post-kickoff business */
	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		phys = dpu_enc->phys_encs[i];
		if (phys->ops.handle_post_kickoff)
			phys->ops.handle_post_kickoff(phys);
	}

	if (dpu_enc->disp_info.intf_type == DRM_MODE_ENCODER_DSI &&
			!dpu_encoder_vsync_time(drm_enc, &wakeup_time)) {
		trace_dpu_enc_early_kickoff(DRMID(drm_enc),
					    ktime_to_ms(wakeup_time));
		mod_timer(&dpu_enc->vsync_event_timer,
				nsecs_to_jiffies(ktime_to_ns(wakeup_time)));
	}

	DPU_ATRACE_END("encoder_kickoff");
}

void dpu_encoder_prepare_commit(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc;
	struct dpu_encoder_phys *phys;
	int i;

	if (!drm_enc) {
		DPU_ERROR("invalid encoder\n");
		return;
	}
	dpu_enc = to_dpu_encoder_virt(drm_enc);

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		phys = dpu_enc->phys_encs[i];
		if (phys->ops.prepare_commit)
			phys->ops.prepare_commit(phys);
	}
}

#ifdef CONFIG_DEBUG_FS
static int _dpu_encoder_status_show(struct seq_file *s, void *data)
{
	struct dpu_encoder_virt *dpu_enc = s->private;
	int i;

	mutex_lock(&dpu_enc->enc_lock);
	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		seq_printf(s, "intf:%d    vsync:%8d     underrun:%8d    ",
				phys->intf_idx - INTF_0,
				atomic_read(&phys->vsync_cnt),
				atomic_read(&phys->underrun_cnt));

		switch (phys->intf_mode) {
		case INTF_MODE_VIDEO:
			seq_puts(s, "mode: video\n");
			break;
		case INTF_MODE_CMD:
			seq_puts(s, "mode: command\n");
			break;
		default:
			seq_puts(s, "mode: ???\n");
			break;
		}
	}
	mutex_unlock(&dpu_enc->enc_lock);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(_dpu_encoder_status);

static int _dpu_encoder_init_debugfs(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(drm_enc);
	int i;

	char name[DPU_NAME_SIZE];

	if (!drm_enc->dev) {
		DPU_ERROR("invalid encoder or kms\n");
		return -EINVAL;
	}

	snprintf(name, DPU_NAME_SIZE, "encoder%u", drm_enc->base.id);

	/* create overall sub-directory for the encoder */
	dpu_enc->debugfs_root = debugfs_create_dir(name,
			drm_enc->dev->primary->debugfs_root);

	/* don't error check these */
	debugfs_create_file("status", 0600,
		dpu_enc->debugfs_root, dpu_enc, &_dpu_encoder_status_fops);

	for (i = 0; i < dpu_enc->num_phys_encs; i++)
		if (dpu_enc->phys_encs[i]->ops.late_register)
			dpu_enc->phys_encs[i]->ops.late_register(
					dpu_enc->phys_encs[i],
					dpu_enc->debugfs_root);

	return 0;
}
#else
static int _dpu_encoder_init_debugfs(struct drm_encoder *drm_enc)
{
	return 0;
}
#endif

static int dpu_encoder_late_register(struct drm_encoder *encoder)
{
	return _dpu_encoder_init_debugfs(encoder);
}

static void dpu_encoder_early_unregister(struct drm_encoder *encoder)
{
	struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(encoder);

	debugfs_remove_recursive(dpu_enc->debugfs_root);
}

static int dpu_encoder_virt_add_phys_encs(
		u32 display_caps,
		struct dpu_encoder_virt *dpu_enc,
		struct dpu_enc_phys_init_params *params)
{
	struct dpu_encoder_phys *enc = NULL;

	DPU_DEBUG_ENC(dpu_enc, "\n");

	/*
	 * We may create up to NUM_PHYS_ENCODER_TYPES physical encoder types
	 * in this function, check up-front.
	 */
	if (dpu_enc->num_phys_encs + NUM_PHYS_ENCODER_TYPES >=
			ARRAY_SIZE(dpu_enc->phys_encs)) {
		DPU_ERROR_ENC(dpu_enc, "too many physical encoders %d\n",
			  dpu_enc->num_phys_encs);
		return -EINVAL;
	}

	if (display_caps & MSM_DISPLAY_CAP_VID_MODE) {
		enc = dpu_encoder_phys_vid_init(params);

		if (IS_ERR_OR_NULL(enc)) {
			DPU_ERROR_ENC(dpu_enc, "failed to init vid enc: %ld\n",
				PTR_ERR(enc));
			return enc == NULL ? -EINVAL : PTR_ERR(enc);
		}

		dpu_enc->phys_encs[dpu_enc->num_phys_encs] = enc;
		++dpu_enc->num_phys_encs;
	}

	if (display_caps & MSM_DISPLAY_CAP_CMD_MODE) {
		enc = dpu_encoder_phys_cmd_init(params);

		if (IS_ERR_OR_NULL(enc)) {
			DPU_ERROR_ENC(dpu_enc, "failed to init cmd enc: %ld\n",
				PTR_ERR(enc));
			return enc == NULL ? -EINVAL : PTR_ERR(enc);
		}

		dpu_enc->phys_encs[dpu_enc->num_phys_encs] = enc;
		++dpu_enc->num_phys_encs;
	}

	if (params->split_role == ENC_ROLE_SLAVE)
		dpu_enc->cur_slave = enc;
	else
		dpu_enc->cur_master = enc;

	return 0;
}

static const struct dpu_encoder_virt_ops dpu_encoder_parent_ops = {
	.handle_vblank_virt = dpu_encoder_vblank_callback,
	.handle_underrun_virt = dpu_encoder_underrun_callback,
	.handle_frame_done = dpu_encoder_frame_done_callback,
};

static int dpu_encoder_setup_display(struct dpu_encoder_virt *dpu_enc,
				 struct dpu_kms *dpu_kms,
				 struct msm_display_info *disp_info)
{
	int ret = 0;
	int i = 0;
	enum dpu_intf_type intf_type = INTF_NONE;
	struct dpu_enc_phys_init_params phys_params;

	if (!dpu_enc) {
		DPU_ERROR("invalid arg(s), enc %d\n", dpu_enc != NULL);
		return -EINVAL;
	}

	dpu_enc->cur_master = NULL;

	memset(&phys_params, 0, sizeof(phys_params));
	phys_params.dpu_kms = dpu_kms;
	phys_params.parent = &dpu_enc->base;
	phys_params.parent_ops = &dpu_encoder_parent_ops;
	phys_params.enc_spinlock = &dpu_enc->enc_spinlock;

	switch (disp_info->intf_type) {
	case DRM_MODE_ENCODER_DSI:
		intf_type = INTF_DSI;
		break;
	case DRM_MODE_ENCODER_TMDS:
		intf_type = INTF_DP;
		break;
	}

	WARN_ON(disp_info->num_of_h_tiles < 1);

	DPU_DEBUG("dsi_info->num_of_h_tiles %d\n", disp_info->num_of_h_tiles);

	if ((disp_info->capabilities & MSM_DISPLAY_CAP_CMD_MODE) ||
	    (disp_info->capabilities & MSM_DISPLAY_CAP_VID_MODE))
		dpu_enc->idle_pc_supported =
				dpu_kms->catalog->caps->has_idle_pc;

	mutex_lock(&dpu_enc->enc_lock);
	for (i = 0; i < disp_info->num_of_h_tiles && !ret; i++) {
		/*
		 * Left-most tile is at index 0, content is controller id
		 * h_tile_instance_ids[2] = {0, 1}; DSI0 = left, DSI1 = right
		 * h_tile_instance_ids[2] = {1, 0}; DSI1 = left, DSI0 = right
		 */
		u32 controller_id = disp_info->h_tile_instance[i];

		if (disp_info->num_of_h_tiles > 1) {
			if (i == 0)
				phys_params.split_role = ENC_ROLE_MASTER;
			else
				phys_params.split_role = ENC_ROLE_SLAVE;
		} else {
			phys_params.split_role = ENC_ROLE_SOLO;
		}

		DPU_DEBUG("h_tile_instance %d = %d, split_role %d\n",
				i, controller_id, phys_params.split_role);

		phys_params.intf_idx = dpu_encoder_get_intf(dpu_kms->catalog,
													intf_type,
													controller_id);
		if (phys_params.intf_idx == INTF_MAX) {
			DPU_ERROR_ENC(dpu_enc, "could not get intf: type %d, id %d\n",
						  intf_type, controller_id);
			ret = -EINVAL;
		}

		if (!ret) {
			ret = dpu_encoder_virt_add_phys_encs(disp_info->capabilities,
												 dpu_enc,
												 &phys_params);
			if (ret)
				DPU_ERROR_ENC(dpu_enc, "failed to add phys encs\n");
		}
	}

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];
		atomic_set(&phys->vsync_cnt, 0);
		atomic_set(&phys->underrun_cnt, 0);
	}
	mutex_unlock(&dpu_enc->enc_lock);

	return ret;
}

static void dpu_encoder_frame_done_timeout(struct timer_list *t)
{
	struct dpu_encoder_virt *dpu_enc = from_timer(dpu_enc, t,
			frame_done_timer);
	struct drm_encoder *drm_enc = &dpu_enc->base;
	u32 event;

	if (!drm_enc->dev) {
		DPU_ERROR("invalid parameters\n");
		return;
	}

	if (!dpu_enc->frame_busy_mask[0] || !dpu_enc->crtc_frame_event_cb) {
		DRM_DEBUG_KMS("id:%u invalid timeout frame_busy_mask=%lu\n",
			      DRMID(drm_enc), dpu_enc->frame_busy_mask[0]);
		return;
	} else if (!atomic_xchg(&dpu_enc->frame_done_timeout_ms, 0)) {
		DRM_DEBUG_KMS("id:%u invalid timeout\n", DRMID(drm_enc));
		return;
	}

	DPU_ERROR_ENC(dpu_enc, "frame done timeout\n");

	event = DPU_ENCODER_FRAME_EVENT_ERROR;
	trace_dpu_enc_frame_done_timeout(DRMID(drm_enc), event);
	dpu_enc->crtc_frame_event_cb(dpu_enc->crtc_frame_event_cb_data, event);
}

static const struct drm_encoder_helper_funcs dpu_encoder_helper_funcs = {
	.mode_set = dpu_encoder_virt_mode_set,
	.disable = dpu_encoder_virt_disable,
	.enable = dpu_kms_encoder_enable,
	.atomic_check = dpu_encoder_virt_atomic_check,

	/* This is called by dpu_kms_encoder_enable */
	.commit = dpu_encoder_virt_enable,
};

static const struct drm_encoder_funcs dpu_encoder_funcs = {
		.destroy = dpu_encoder_destroy,
		.late_register = dpu_encoder_late_register,
		.early_unregister = dpu_encoder_early_unregister,
};

int dpu_encoder_setup(struct drm_device *dev, struct drm_encoder *enc,
		struct msm_display_info *disp_info)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct dpu_kms *dpu_kms = to_dpu_kms(priv->kms);
	struct drm_encoder *drm_enc = NULL;
	struct dpu_encoder_virt *dpu_enc = NULL;
	int ret = 0;

	dpu_enc = to_dpu_encoder_virt(enc);

	ret = dpu_encoder_setup_display(dpu_enc, dpu_kms, disp_info);
	if (ret)
		goto fail;

	atomic_set(&dpu_enc->frame_done_timeout_ms, 0);
	timer_setup(&dpu_enc->frame_done_timer,
			dpu_encoder_frame_done_timeout, 0);

	if (disp_info->intf_type == DRM_MODE_ENCODER_DSI)
		timer_setup(&dpu_enc->vsync_event_timer,
				dpu_encoder_vsync_event_handler,
				0);
	else if (disp_info->intf_type == DRM_MODE_ENCODER_TMDS)
		dpu_enc->dp = priv->dp[disp_info->h_tile_instance[0]];

	INIT_DELAYED_WORK(&dpu_enc->delayed_off_work,
			dpu_encoder_off_work);
	dpu_enc->idle_timeout = IDLE_TIMEOUT;

	kthread_init_work(&dpu_enc->vsync_event_work,
			dpu_encoder_vsync_event_work_handler);

	memcpy(&dpu_enc->disp_info, disp_info, sizeof(*disp_info));

	DPU_DEBUG_ENC(dpu_enc, "created\n");

	return ret;

fail:
	DPU_ERROR("failed to create encoder\n");
	if (drm_enc)
		dpu_encoder_destroy(drm_enc);

	return ret;


}

struct drm_encoder *dpu_encoder_init(struct drm_device *dev,
		int drm_enc_mode)
{
	struct dpu_encoder_virt *dpu_enc = NULL;
	int rc = 0;

	dpu_enc = devm_kzalloc(dev->dev, sizeof(*dpu_enc), GFP_KERNEL);
	if (!dpu_enc)
		return ERR_PTR(-ENOMEM);

	rc = drm_encoder_init(dev, &dpu_enc->base, &dpu_encoder_funcs,
			drm_enc_mode, NULL);
	if (rc) {
		devm_kfree(dev->dev, dpu_enc);
		return ERR_PTR(rc);
	}

	drm_encoder_helper_add(&dpu_enc->base, &dpu_encoder_helper_funcs);

	spin_lock_init(&dpu_enc->enc_spinlock);
	dpu_enc->enabled = false;
	mutex_init(&dpu_enc->enc_lock);
	mutex_init(&dpu_enc->rc_lock);

	return &dpu_enc->base;
}

int dpu_encoder_wait_for_event(struct drm_encoder *drm_enc,
	enum msm_event_wait event)
{
	int (*fn_wait)(struct dpu_encoder_phys *phys_enc) = NULL;
	struct dpu_encoder_virt *dpu_enc = NULL;
	int i, ret = 0;

	if (!drm_enc) {
		DPU_ERROR("invalid encoder\n");
		return -EINVAL;
	}
	dpu_enc = to_dpu_encoder_virt(drm_enc);
	DPU_DEBUG_ENC(dpu_enc, "\n");

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		switch (event) {
		case MSM_ENC_COMMIT_DONE:
			fn_wait = phys->ops.wait_for_commit_done;
			break;
		case MSM_ENC_TX_COMPLETE:
			fn_wait = phys->ops.wait_for_tx_complete;
			break;
		case MSM_ENC_VBLANK:
			fn_wait = phys->ops.wait_for_vblank;
			break;
		default:
			DPU_ERROR_ENC(dpu_enc, "unknown wait event %d\n",
					event);
			return -EINVAL;
		}

		if (fn_wait) {
			DPU_ATRACE_BEGIN("wait_for_completion_event");
			ret = fn_wait(phys);
			DPU_ATRACE_END("wait_for_completion_event");
			if (ret)
				return ret;
		}
	}

	return ret;
}

enum dpu_intf_mode dpu_encoder_get_intf_mode(struct drm_encoder *encoder)
{
	struct dpu_encoder_virt *dpu_enc = NULL;

	if (!encoder) {
		DPU_ERROR("invalid encoder\n");
		return INTF_MODE_NONE;
	}
	dpu_enc = to_dpu_encoder_virt(encoder);

	if (dpu_enc->cur_master)
		return dpu_enc->cur_master->intf_mode;

	if (dpu_enc->num_phys_encs)
		return dpu_enc->phys_encs[0]->intf_mode;

	return INTF_MODE_NONE;
}
