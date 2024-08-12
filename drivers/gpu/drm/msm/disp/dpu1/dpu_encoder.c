// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Copyright (c) 2014-2018, 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Author: Rob Clark <robdclark@gmail.com>
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__
#include <linux/debugfs.h>
#include <linux/kthread.h>
#include <linux/seq_file.h>

#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <drm/drm_file.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_framebuffer.h>

#include "msm_drv.h"
#include "dpu_kms.h"
#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_intf.h"
#include "dpu_hw_ctl.h"
#include "dpu_hw_dspp.h"
#include "dpu_hw_dsc.h"
#include "dpu_hw_merge3d.h"
#include "dpu_hw_cdm.h"
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

#define DPU_ERROR_ENC_RATELIMITED(e, fmt, ...) DPU_ERROR_RATELIMITED("enc%d " fmt,\
		(e) ? (e)->base.base.id : -1, ##__VA_ARGS__)

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
 * @enabled:		True if the encoder is active, protected by enc_lock
 * @commit_done_timedout: True if there has been a timeout on commit after
 *			enabling the encoder.
 * @num_phys_encs:	Actual number of physical encoders contained.
 * @phys_encs:		Container of physical encoders managed.
 * @cur_master:		Pointer to the current master in this mode. Optimization
 *			Only valid after enable. Cleared as disable.
 * @cur_slave:		As above but for the slave encoder.
 * @hw_pp:		Handle to the pingpong blocks used for the display. No.
 *			pingpong blocks can be different than num_phys_encs.
 * @hw_dsc:		Handle to the DSC blocks used for the display.
 * @dsc_mask:		Bitmask of used DSC blocks.
 * @intfs_swapped:	Whether or not the phys_enc interfaces have been swapped
 *			for partial update right-only cases, such as pingpong
 *			split where virtual pingpong does not generate IRQs
 * @crtc:		Pointer to the currently assigned crtc. Normally you
 *			would use crtc->state->encoder_mask to determine the
 *			link between encoder/crtc. However in this case we need
 *			to track crtc in the disable() hook which is called
 *			_after_ encoder_mask is cleared.
 * @connector:		If a mode is set, cached pointer to the active connector
 * @enc_lock:			Lock around physical encoder
 *				create/destroy/enable/disable
 * @frame_busy_mask:		Bitmask tracking which phys_enc we are still
 *				busy processing current command.
 *				Bit0 = phys_encs[0] etc.
 * @frame_done_timeout_ms:	frame done timeout in ms
 * @frame_done_timeout_cnt:	atomic counter tracking the number of frame
 * 				done timeouts
 * @frame_done_timer:		watchdog timer for frame done event
 * @disp_info:			local copy of msm_display_info struct
 * @idle_pc_supported:		indicate if idle power collaps is supported
 * @rc_lock:			resource control mutex lock to protect
 *				virt encoder over various state changes
 * @rc_state:			resource controller state
 * @delayed_off_work:		delayed worker to schedule disabling of
 *				clks and resources after IDLE_TIMEOUT time.
 * @topology:                   topology of the display
 * @idle_timeout:		idle timeout duration in milliseconds
 * @wide_bus_en:		wide bus is enabled on this interface
 * @dsc:			drm_dsc_config pointer, for DSC-enabled encoders
 */
struct dpu_encoder_virt {
	struct drm_encoder base;
	spinlock_t enc_spinlock;

	bool enabled;
	bool commit_done_timedout;

	unsigned int num_phys_encs;
	struct dpu_encoder_phys *phys_encs[MAX_PHYS_ENCODERS_PER_VIRTUAL];
	struct dpu_encoder_phys *cur_master;
	struct dpu_encoder_phys *cur_slave;
	struct dpu_hw_pingpong *hw_pp[MAX_CHANNELS_PER_ENC];
	struct dpu_hw_dsc *hw_dsc[MAX_CHANNELS_PER_ENC];

	unsigned int dsc_mask;

	bool intfs_swapped;

	struct drm_crtc *crtc;
	struct drm_connector *connector;

	struct mutex enc_lock;
	DECLARE_BITMAP(frame_busy_mask, MAX_PHYS_ENCODERS_PER_VIRTUAL);

	atomic_t frame_done_timeout_ms;
	atomic_t frame_done_timeout_cnt;
	struct timer_list frame_done_timer;

	struct msm_display_info disp_info;

	bool idle_pc_supported;
	struct mutex rc_lock;
	enum dpu_enc_rc_states rc_state;
	struct delayed_work delayed_off_work;
	struct msm_display_topology topology;

	u32 idle_timeout;

	bool wide_bus_en;

	/* DSC configuration */
	struct drm_dsc_config *dsc;
};

#define to_dpu_encoder_virt(x) container_of(x, struct dpu_encoder_virt, base)

static u32 dither_matrix[DITHER_MATRIX_SZ] = {
	15, 7, 13, 5, 3, 11, 1, 9, 12, 4, 14, 6, 0, 8, 2, 10
};

u32 dpu_encoder_get_drm_fmt(struct dpu_encoder_phys *phys_enc)
{
	struct drm_encoder *drm_enc;
	struct dpu_encoder_virt *dpu_enc;
	struct drm_display_info *info;
	struct drm_display_mode *mode;

	drm_enc = phys_enc->parent;
	dpu_enc = to_dpu_encoder_virt(drm_enc);
	info = &dpu_enc->connector->display_info;
	mode = &phys_enc->cached_mode;

	if (drm_mode_is_420_only(info, mode))
		return DRM_FORMAT_YUV420;

	return DRM_FORMAT_RGB888;
}

bool dpu_encoder_needs_periph_flush(struct dpu_encoder_phys *phys_enc)
{
	struct drm_encoder *drm_enc;
	struct dpu_encoder_virt *dpu_enc;
	struct msm_display_info *disp_info;
	struct msm_drm_private *priv;
	struct drm_display_mode *mode;

	drm_enc = phys_enc->parent;
	dpu_enc = to_dpu_encoder_virt(drm_enc);
	disp_info = &dpu_enc->disp_info;
	priv = drm_enc->dev->dev_private;
	mode = &phys_enc->cached_mode;

	return phys_enc->hw_intf->cap->type == INTF_DP &&
	       msm_dp_needs_periph_flush(priv->dp[disp_info->h_tile_instance[0]], mode);
}

bool dpu_encoder_is_widebus_enabled(const struct drm_encoder *drm_enc)
{
	const struct dpu_encoder_virt *dpu_enc;
	struct msm_drm_private *priv = drm_enc->dev->dev_private;
	const struct msm_display_info *disp_info;
	int index;

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	disp_info = &dpu_enc->disp_info;
	index = disp_info->h_tile_instance[0];

	if (disp_info->intf_type == INTF_DP)
		return msm_dp_wide_bus_available(priv->dp[index]);
	else if (disp_info->intf_type == INTF_DSI)
		return msm_dsi_wide_bus_enabled(priv->dsi[index]);

	return false;
}

bool dpu_encoder_is_dsc_enabled(const struct drm_encoder *drm_enc)
{
	const struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(drm_enc);

	return dpu_enc->dsc ? true : false;
}

int dpu_encoder_get_crc_values_cnt(const struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc;
	int i, num_intf = 0;

	dpu_enc = to_dpu_encoder_virt(drm_enc);

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (phys->hw_intf && phys->hw_intf->ops.setup_misr
				&& phys->hw_intf->ops.collect_misr)
			num_intf++;
	}

	return num_intf;
}

void dpu_encoder_setup_misr(const struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc;

	int i;

	dpu_enc = to_dpu_encoder_virt(drm_enc);

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (!phys->hw_intf || !phys->hw_intf->ops.setup_misr)
			continue;

		phys->hw_intf->ops.setup_misr(phys->hw_intf);
	}
}

int dpu_encoder_get_crc(const struct drm_encoder *drm_enc, u32 *crcs, int pos)
{
	struct dpu_encoder_virt *dpu_enc;

	int i, rc = 0, entries_added = 0;

	if (!drm_enc->crtc) {
		DRM_ERROR("no crtc found for encoder %d\n", drm_enc->index);
		return -EINVAL;
	}

	dpu_enc = to_dpu_encoder_virt(drm_enc);

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (!phys->hw_intf || !phys->hw_intf->ops.collect_misr)
			continue;

		rc = phys->hw_intf->ops.collect_misr(phys->hw_intf, &crcs[pos + entries_added]);
		if (rc)
			return rc;
		entries_added++;
	}

	return entries_added;
}

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

static char *dpu_encoder_helper_get_intf_type(enum dpu_intf_mode intf_mode)
{
	switch (intf_mode) {
	case INTF_MODE_VIDEO:
		return "INTF_MODE_VIDEO";
	case INTF_MODE_CMD:
		return "INTF_MODE_CMD";
	case INTF_MODE_WB_BLOCK:
		return "INTF_MODE_WB_BLOCK";
	case INTF_MODE_WB_LINE:
		return "INTF_MODE_WB_LINE";
	default:
		return "INTF_MODE_UNKNOWN";
	}
}

void dpu_encoder_helper_report_irq_timeout(struct dpu_encoder_phys *phys_enc,
		enum dpu_intr_idx intr_idx)
{
	DRM_ERROR("irq timeout id=%u, intf_mode=%s intf=%d wb=%d, pp=%d, intr=%d\n",
			DRMID(phys_enc->parent),
			dpu_encoder_helper_get_intf_type(phys_enc->intf_mode),
			phys_enc->hw_intf ? phys_enc->hw_intf->idx - INTF_0 : -1,
			phys_enc->hw_wb ? phys_enc->hw_wb->idx - WB_0 : -1,
			phys_enc->hw_pp->idx - PINGPONG_0, intr_idx);

	dpu_encoder_frame_done_callback(phys_enc->parent, phys_enc,
				DPU_ENCODER_FRAME_EVENT_ERROR);
}

static int dpu_encoder_helper_wait_event_timeout(int32_t drm_id,
		u32 irq_idx, struct dpu_encoder_wait_info *info);

int dpu_encoder_helper_wait_for_irq(struct dpu_encoder_phys *phys_enc,
		unsigned int irq_idx,
		void (*func)(void *arg),
		struct dpu_encoder_wait_info *wait_info)
{
	u32 irq_status;
	int ret;

	if (!wait_info) {
		DPU_ERROR("invalid params\n");
		return -EINVAL;
	}
	/* note: do master / slave checking outside */

	/* return EWOULDBLOCK since we know the wait isn't necessary */
	if (phys_enc->enable_state == DPU_ENC_DISABLED) {
		DRM_ERROR("encoder is disabled id=%u, callback=%ps, IRQ=[%d, %d]\n",
			  DRMID(phys_enc->parent), func,
			  DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));
		return -EWOULDBLOCK;
	}

	if (irq_idx == 0) {
		DRM_DEBUG_KMS("skip irq wait id=%u, callback=%ps\n",
			      DRMID(phys_enc->parent), func);
		return 0;
	}

	DRM_DEBUG_KMS("id=%u, callback=%ps, IRQ=[%d, %d], pp=%d, pending_cnt=%d\n",
		      DRMID(phys_enc->parent), func,
		      DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx), phys_enc->hw_pp->idx - PINGPONG_0,
		      atomic_read(wait_info->atomic_cnt));

	ret = dpu_encoder_helper_wait_event_timeout(
			DRMID(phys_enc->parent),
			irq_idx,
			wait_info);

	if (ret <= 0) {
		irq_status = dpu_core_irq_read(phys_enc->dpu_kms, irq_idx);
		if (irq_status) {
			unsigned long flags;

			DRM_DEBUG_KMS("IRQ=[%d, %d] not triggered id=%u, callback=%ps, pp=%d, atomic_cnt=%d\n",
				      DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx),
				      DRMID(phys_enc->parent), func,
				      phys_enc->hw_pp->idx - PINGPONG_0,
				      atomic_read(wait_info->atomic_cnt));
			local_irq_save(flags);
			func(phys_enc);
			local_irq_restore(flags);
			ret = 0;
		} else {
			ret = -ETIMEDOUT;
			DRM_DEBUG_KMS("IRQ=[%d, %d] timeout id=%u, callback=%ps, pp=%d, atomic_cnt=%d\n",
				      DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx),
				      DRMID(phys_enc->parent), func,
				      phys_enc->hw_pp->idx - PINGPONG_0,
				      atomic_read(wait_info->atomic_cnt));
		}
	} else {
		ret = 0;
		trace_dpu_enc_irq_wait_success(DRMID(phys_enc->parent),
			func, DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx),
			phys_enc->hw_pp->idx - PINGPONG_0,
			atomic_read(wait_info->atomic_cnt));
	}

	return ret;
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

	if (disp_info->intf_type != INTF_DSI)
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

bool dpu_encoder_use_dsc_merge(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(drm_enc);
	int i, intf_count = 0, num_dsc = 0;

	for (i = 0; i < MAX_PHYS_ENCODERS_PER_VIRTUAL; i++)
		if (dpu_enc->phys_encs[i])
			intf_count++;

	/* See dpu_encoder_get_topology, we only support 2:2:1 topology */
	if (dpu_enc->dsc)
		num_dsc = 2;

	return (num_dsc > 0) && (num_dsc > intf_count);
}

struct drm_dsc_config *dpu_encoder_get_dsc_config(struct drm_encoder *drm_enc)
{
	struct msm_drm_private *priv = drm_enc->dev->dev_private;
	struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(drm_enc);
	int index = dpu_enc->disp_info.h_tile_instance[0];

	if (dpu_enc->disp_info.intf_type == INTF_DSI)
		return msm_dsi_get_dsc_config(priv->dsi[index]);

	return NULL;
}

static struct msm_display_topology dpu_encoder_get_topology(
			struct dpu_encoder_virt *dpu_enc,
			struct dpu_kms *dpu_kms,
			struct drm_display_mode *mode,
			struct drm_crtc_state *crtc_state,
			struct drm_dsc_config *dsc)
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
	 * Add dspps to the reservation requirements if ctm is requested
	 */
	if (intf_count == 2)
		topology.num_lm = 2;
	else if (!dpu_kms->catalog->caps->has_3d_merge)
		topology.num_lm = 1;
	else
		topology.num_lm = (mode->hdisplay > MAX_HDISPLAY_SPLIT) ? 2 : 1;

	if (crtc_state->ctm)
		topology.num_dspp = topology.num_lm;

	topology.num_intf = intf_count;

	if (dsc) {
		/*
		 * In case of Display Stream Compression (DSC), we would use
		 * 2 DSC encoders, 2 layer mixers and 1 interface
		 * this is power optimal and can drive up to (including) 4k
		 * screens
		 */
		topology.num_dsc = 2;
		topology.num_lm = 2;
		topology.num_intf = 1;
	}

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
	struct drm_display_mode *adj_mode;
	struct msm_display_topology topology;
	struct msm_display_info *disp_info;
	struct dpu_global_state *global_state;
	struct drm_framebuffer *fb;
	struct drm_dsc_config *dsc;
	int ret = 0;

	if (!drm_enc || !crtc_state || !conn_state) {
		DPU_ERROR("invalid arg(s), drm_enc %d, crtc/conn state %d/%d\n",
				drm_enc != NULL, crtc_state != NULL, conn_state != NULL);
		return -EINVAL;
	}

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	DPU_DEBUG_ENC(dpu_enc, "\n");

	priv = drm_enc->dev->dev_private;
	disp_info = &dpu_enc->disp_info;
	dpu_kms = to_dpu_kms(priv->kms);
	adj_mode = &crtc_state->adjusted_mode;
	global_state = dpu_kms_get_global_state(crtc_state->state);
	if (IS_ERR(global_state))
		return PTR_ERR(global_state);

	trace_dpu_enc_atomic_check(DRMID(drm_enc));

	dsc = dpu_encoder_get_dsc_config(drm_enc);

	topology = dpu_encoder_get_topology(dpu_enc, dpu_kms, adj_mode, crtc_state, dsc);

	/*
	 * Use CDM only for writeback or DP at the moment as other interfaces cannot handle it.
	 * If writeback itself cannot handle cdm for some reason it will fail in its atomic_check()
	 * earlier.
	 */
	if (disp_info->intf_type == INTF_WB && conn_state->writeback_job) {
		fb = conn_state->writeback_job->fb;

		if (fb && MSM_FORMAT_IS_YUV(msm_framebuffer_format(fb)))
			topology.needs_cdm = true;
	} else if (disp_info->intf_type == INTF_DP) {
		if (msm_dp_is_yuv_420_enabled(priv->dp[disp_info->h_tile_instance[0]], adj_mode))
			topology.needs_cdm = true;
	}

	if (topology.needs_cdm && !dpu_enc->cur_master->hw_cdm)
		crtc_state->mode_changed = true;
	else if (!topology.needs_cdm && dpu_enc->cur_master->hw_cdm)
		crtc_state->mode_changed = true;
	/*
	 * Release and Allocate resources on every modeset
	 * Dont allocate when active is false.
	 */
	if (drm_atomic_crtc_needs_modeset(crtc_state)) {
		dpu_rm_release(global_state, drm_enc);

		if (!crtc_state->active_changed || crtc_state->enable)
			ret = dpu_rm_reserve(&dpu_kms->rm, global_state,
					drm_enc, crtc_state, topology);
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
	struct dpu_encoder_phys *phys_enc;
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

	if (hw_mdptop->ops.setup_vsync_source) {
		for (i = 0; i < dpu_enc->num_phys_encs; i++)
			vsync_cfg.ppnumber[i] = dpu_enc->hw_pp[i]->idx;

		vsync_cfg.pp_count = dpu_enc->num_phys_encs;
		vsync_cfg.frame_rate = drm_mode_vrefresh(&dpu_enc->base.crtc->state->adjusted_mode);

		vsync_cfg.vsync_source = disp_info->vsync_source;

		hw_mdptop->ops.setup_vsync_source(hw_mdptop, &vsync_cfg);

		for (i = 0; i < dpu_enc->num_phys_encs; i++) {
			phys_enc = dpu_enc->phys_encs[i];

			if (phys_enc->has_intf_te && phys_enc->hw_intf->ops.vsync_sel)
				phys_enc->hw_intf->ops.vsync_sel(phys_enc->hw_intf,
						vsync_cfg.vsync_source);
		}
	}
}

static void _dpu_encoder_irq_enable(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc;
	int i;

	if (!drm_enc) {
		DPU_ERROR("invalid encoder\n");
		return;
	}

	dpu_enc = to_dpu_encoder_virt(drm_enc);

	DPU_DEBUG_ENC(dpu_enc, "\n");
	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		phys->ops.irq_enable(phys);
	}
}

static void _dpu_encoder_irq_disable(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc;
	int i;

	if (!drm_enc) {
		DPU_ERROR("invalid encoder\n");
		return;
	}

	dpu_enc = to_dpu_encoder_virt(drm_enc);

	DPU_DEBUG_ENC(dpu_enc, "\n");
	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		phys->ops.irq_disable(phys);
	}
}

static void _dpu_encoder_resource_enable(struct drm_encoder *drm_enc)
{
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms;
	struct dpu_encoder_virt *dpu_enc;

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	priv = drm_enc->dev->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);

	trace_dpu_enc_rc_enable(DRMID(drm_enc));

	if (!dpu_enc->cur_master) {
		DPU_ERROR("encoder master not set\n");
		return;
	}

	/* enable DPU core clks */
	pm_runtime_get_sync(&dpu_kms->pdev->dev);

	/* enable all the irq */
	_dpu_encoder_irq_enable(drm_enc);
}

static void _dpu_encoder_resource_disable(struct drm_encoder *drm_enc)
{
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms;
	struct dpu_encoder_virt *dpu_enc;

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	priv = drm_enc->dev->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);

	trace_dpu_enc_rc_disable(DRMID(drm_enc));

	if (!dpu_enc->cur_master) {
		DPU_ERROR("encoder master not set\n");
		return;
	}

	/* disable all the irq */
	_dpu_encoder_irq_disable(drm_enc);

	/* disable DPU core clks */
	pm_runtime_put_sync(&dpu_kms->pdev->dev);
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
	is_vid_mode = !dpu_enc->disp_info.is_cmd_mode;

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
			_dpu_encoder_irq_enable(drm_enc);
		else
			_dpu_encoder_resource_enable(drm_enc);

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
			_dpu_encoder_irq_enable(drm_enc);
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
			_dpu_encoder_resource_disable(drm_enc);

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
			_dpu_encoder_irq_disable(drm_enc);
		else
			_dpu_encoder_resource_disable(drm_enc);

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

void dpu_encoder_prepare_wb_job(struct drm_encoder *drm_enc,
		struct drm_writeback_job *job)
{
	struct dpu_encoder_virt *dpu_enc;
	int i;

	dpu_enc = to_dpu_encoder_virt(drm_enc);

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (phys->ops.prepare_wb_job)
			phys->ops.prepare_wb_job(phys, job);

	}
}

void dpu_encoder_cleanup_wb_job(struct drm_encoder *drm_enc,
		struct drm_writeback_job *job)
{
	struct dpu_encoder_virt *dpu_enc;
	int i;

	dpu_enc = to_dpu_encoder_virt(drm_enc);

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		if (phys->ops.cleanup_wb_job)
			phys->ops.cleanup_wb_job(phys, job);

	}
}

static void dpu_encoder_virt_atomic_mode_set(struct drm_encoder *drm_enc,
					     struct drm_crtc_state *crtc_state,
					     struct drm_connector_state *conn_state)
{
	struct dpu_encoder_virt *dpu_enc;
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms;
	struct dpu_crtc_state *cstate;
	struct dpu_global_state *global_state;
	struct dpu_hw_blk *hw_pp[MAX_CHANNELS_PER_ENC];
	struct dpu_hw_blk *hw_ctl[MAX_CHANNELS_PER_ENC];
	struct dpu_hw_blk *hw_lm[MAX_CHANNELS_PER_ENC];
	struct dpu_hw_blk *hw_dspp[MAX_CHANNELS_PER_ENC] = { NULL };
	struct dpu_hw_blk *hw_dsc[MAX_CHANNELS_PER_ENC];
	int num_lm, num_ctl, num_pp, num_dsc;
	unsigned int dsc_mask = 0;
	int i;

	if (!drm_enc) {
		DPU_ERROR("invalid encoder\n");
		return;
	}

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	DPU_DEBUG_ENC(dpu_enc, "\n");

	priv = drm_enc->dev->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);

	global_state = dpu_kms_get_existing_global_state(dpu_kms);
	if (IS_ERR_OR_NULL(global_state)) {
		DPU_ERROR("Failed to get global state");
		return;
	}

	trace_dpu_enc_mode_set(DRMID(drm_enc));

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

	num_dsc = dpu_rm_get_assigned_resources(&dpu_kms->rm, global_state,
						drm_enc->base.id, DPU_HW_BLK_DSC,
						hw_dsc, ARRAY_SIZE(hw_dsc));
	for (i = 0; i < num_dsc; i++) {
		dpu_enc->hw_dsc[i] = to_dpu_hw_dsc(hw_dsc[i]);
		dsc_mask |= BIT(dpu_enc->hw_dsc[i]->idx - DSC_0);
	}

	dpu_enc->dsc_mask = dsc_mask;

	if ((dpu_enc->disp_info.intf_type == INTF_WB && conn_state->writeback_job) ||
	    dpu_enc->disp_info.intf_type == INTF_DP) {
		struct dpu_hw_blk *hw_cdm = NULL;

		dpu_rm_get_assigned_resources(&dpu_kms->rm, global_state,
					      drm_enc->base.id, DPU_HW_BLK_CDM,
					      &hw_cdm, 1);
		dpu_enc->cur_master->hw_cdm = hw_cdm ? to_dpu_hw_cdm(hw_cdm) : NULL;
	}

	cstate = to_dpu_crtc_state(crtc_state);

	for (i = 0; i < num_lm; i++) {
		int ctl_idx = (i < num_ctl) ? i : (num_ctl-1);

		cstate->mixers[i].hw_lm = to_dpu_hw_mixer(hw_lm[i]);
		cstate->mixers[i].lm_ctl = to_dpu_hw_ctl(hw_ctl[ctl_idx]);
		cstate->mixers[i].hw_dspp = to_dpu_hw_dspp(hw_dspp[i]);
	}

	cstate->num_mixers = num_lm;

	dpu_enc->connector = conn_state->connector;

	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
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

		phys->cached_mode = crtc_state->adjusted_mode;
		if (phys->ops.atomic_mode_set)
			phys->ops.atomic_mode_set(phys, crtc_state, conn_state);
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


	if (dpu_enc->disp_info.intf_type == INTF_DP &&
		dpu_enc->cur_master->hw_mdptop &&
		dpu_enc->cur_master->hw_mdptop->ops.intf_audio_select)
		dpu_enc->cur_master->hw_mdptop->ops.intf_audio_select(
			dpu_enc->cur_master->hw_mdptop);

	if (dpu_enc->disp_info.is_cmd_mode)
		_dpu_encoder_update_vsync_source(dpu_enc, &dpu_enc->disp_info);

	if (dpu_enc->disp_info.intf_type == INTF_DSI &&
			!WARN_ON(dpu_enc->num_phys_encs == 0)) {
		unsigned bpc = dpu_enc->connector->display_info.bpc;
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

static void dpu_encoder_virt_atomic_enable(struct drm_encoder *drm_enc,
					struct drm_atomic_state *state)
{
	struct dpu_encoder_virt *dpu_enc = NULL;
	int ret = 0;
	struct drm_display_mode *cur_mode = NULL;

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	dpu_enc->dsc = dpu_encoder_get_dsc_config(drm_enc);

	atomic_set(&dpu_enc->frame_done_timeout_cnt, 0);

	mutex_lock(&dpu_enc->enc_lock);

	dpu_enc->commit_done_timedout = false;

	cur_mode = &dpu_enc->base.crtc->state->adjusted_mode;

	dpu_enc->wide_bus_en = dpu_encoder_is_widebus_enabled(drm_enc);

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

	dpu_enc->enabled = true;

out:
	mutex_unlock(&dpu_enc->enc_lock);
}

static void dpu_encoder_virt_atomic_disable(struct drm_encoder *drm_enc,
					struct drm_atomic_state *state)
{
	struct dpu_encoder_virt *dpu_enc = NULL;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_state = NULL;
	int i = 0;

	dpu_enc = to_dpu_encoder_virt(drm_enc);
	DPU_DEBUG_ENC(dpu_enc, "\n");

	crtc = drm_atomic_get_old_crtc_for_encoder(state, drm_enc);
	if (crtc)
		old_state = drm_atomic_get_old_crtc_state(state, crtc);

	/*
	 * The encoder is already disabled if self refresh mode was set earlier,
	 * in the old_state for the corresponding crtc.
	 */
	if (old_state && old_state->self_refresh_active)
		return;

	mutex_lock(&dpu_enc->enc_lock);
	dpu_enc->enabled = false;

	trace_dpu_enc_disable(DRMID(drm_enc));

	/* wait for idle */
	dpu_encoder_wait_for_tx_complete(drm_enc);

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

	dpu_enc->connector = NULL;

	DPU_DEBUG_ENC(dpu_enc, "encoder disabled\n");

	mutex_unlock(&dpu_enc->enc_lock);
}

static struct dpu_hw_intf *dpu_encoder_get_intf(const struct dpu_mdss_cfg *catalog,
		struct dpu_rm *dpu_rm,
		enum dpu_intf_type type, u32 controller_id)
{
	int i = 0;

	if (type == INTF_WB)
		return NULL;

	for (i = 0; i < catalog->intf_count; i++) {
		if (catalog->intf[i].type == type
		    && catalog->intf[i].controller_id == controller_id) {
			return dpu_rm_get_intf(dpu_rm, catalog->intf[i].id);
		}
	}

	return NULL;
}

void dpu_encoder_vblank_callback(struct drm_encoder *drm_enc,
		struct dpu_encoder_phys *phy_enc)
{
	struct dpu_encoder_virt *dpu_enc = NULL;
	unsigned long lock_flags;

	if (!drm_enc || !phy_enc)
		return;

	DPU_ATRACE_BEGIN("encoder_vblank_callback");
	dpu_enc = to_dpu_encoder_virt(drm_enc);

	atomic_inc(&phy_enc->vsync_cnt);

	spin_lock_irqsave(&dpu_enc->enc_spinlock, lock_flags);
	if (dpu_enc->crtc)
		dpu_crtc_vblank_callback(dpu_enc->crtc);
	spin_unlock_irqrestore(&dpu_enc->enc_spinlock, lock_flags);

	DPU_ATRACE_END("encoder_vblank_callback");
}

void dpu_encoder_underrun_callback(struct drm_encoder *drm_enc,
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

void dpu_encoder_frame_done_callback(
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
			trace_dpu_enc_frame_done_cb_not_busy(DRMID(drm_enc), event,
					dpu_encoder_helper_get_intf_type(ready_phys->intf_mode),
					ready_phys->hw_intf ? ready_phys->hw_intf->idx : -1,
					ready_phys->hw_wb ? ready_phys->hw_wb->idx : -1);
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

			if (dpu_enc->crtc)
				dpu_crtc_frame_event_cb(dpu_enc->crtc, event);
		}
	} else {
		if (dpu_enc->crtc)
			dpu_crtc_frame_event_cb(dpu_enc->crtc, event);
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

	trace_dpu_enc_trigger_flush(DRMID(drm_enc),
			dpu_encoder_helper_get_intf_type(phys->intf_mode),
			phys->hw_intf ? phys->hw_intf->idx : -1,
			phys->hw_wb ? phys->hw_wb->idx : -1,
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
		unsigned int irq_idx,
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

		trace_dpu_enc_wait_event_timeout(drm_id,
						 DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx),
						 rc, time,
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
		ctl->ops.clear_pending_flush(ctl);

		/* update only for command mode primary ctl */
		if ((phys == dpu_enc->cur_master) &&
		    disp_info->is_cmd_mode
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

static u32
dpu_encoder_dsc_initial_line_calc(struct drm_dsc_config *dsc,
				  u32 enc_ip_width)
{
	int ssm_delay, total_pixels, soft_slice_per_enc;

	soft_slice_per_enc = enc_ip_width / dsc->slice_width;

	/*
	 * minimum number of initial line pixels is a sum of:
	 * 1. sub-stream multiplexer delay (83 groups for 8bpc,
	 *    91 for 10 bpc) * 3
	 * 2. for two soft slice cases, add extra sub-stream multiplexer * 3
	 * 3. the initial xmit delay
	 * 4. total pipeline delay through the "lock step" of encoder (47)
	 * 5. 6 additional pixels as the output of the rate buffer is
	 *    48 bits wide
	 */
	ssm_delay = ((dsc->bits_per_component < 10) ? 84 : 92);
	total_pixels = ssm_delay * 3 + dsc->initial_xmit_delay + 47;
	if (soft_slice_per_enc > 1)
		total_pixels += (ssm_delay * 3);
	return DIV_ROUND_UP(total_pixels, dsc->slice_width);
}

static void dpu_encoder_dsc_pipe_cfg(struct dpu_hw_ctl *ctl,
				     struct dpu_hw_dsc *hw_dsc,
				     struct dpu_hw_pingpong *hw_pp,
				     struct drm_dsc_config *dsc,
				     u32 common_mode,
				     u32 initial_lines)
{
	if (hw_dsc->ops.dsc_config)
		hw_dsc->ops.dsc_config(hw_dsc, dsc, common_mode, initial_lines);

	if (hw_dsc->ops.dsc_config_thresh)
		hw_dsc->ops.dsc_config_thresh(hw_dsc, dsc);

	if (hw_pp->ops.setup_dsc)
		hw_pp->ops.setup_dsc(hw_pp);

	if (hw_dsc->ops.dsc_bind_pingpong_blk)
		hw_dsc->ops.dsc_bind_pingpong_blk(hw_dsc, hw_pp->idx);

	if (hw_pp->ops.enable_dsc)
		hw_pp->ops.enable_dsc(hw_pp);

	if (ctl->ops.update_pending_flush_dsc)
		ctl->ops.update_pending_flush_dsc(ctl, hw_dsc->idx);
}

static void dpu_encoder_prep_dsc(struct dpu_encoder_virt *dpu_enc,
				 struct drm_dsc_config *dsc)
{
	/* coding only for 2LM, 2enc, 1 dsc config */
	struct dpu_encoder_phys *enc_master = dpu_enc->cur_master;
	struct dpu_hw_ctl *ctl = enc_master->hw_ctl;
	struct dpu_hw_dsc *hw_dsc[MAX_CHANNELS_PER_ENC];
	struct dpu_hw_pingpong *hw_pp[MAX_CHANNELS_PER_ENC];
	int this_frame_slices;
	int intf_ip_w, enc_ip_w;
	int dsc_common_mode;
	int pic_width;
	u32 initial_lines;
	int i;

	for (i = 0; i < MAX_CHANNELS_PER_ENC; i++) {
		hw_pp[i] = dpu_enc->hw_pp[i];
		hw_dsc[i] = dpu_enc->hw_dsc[i];

		if (!hw_pp[i] || !hw_dsc[i]) {
			DPU_ERROR_ENC(dpu_enc, "invalid params for DSC\n");
			return;
		}
	}

	dsc_common_mode = 0;
	pic_width = dsc->pic_width;

	dsc_common_mode = DSC_MODE_SPLIT_PANEL;
	if (dpu_encoder_use_dsc_merge(enc_master->parent))
		dsc_common_mode |= DSC_MODE_MULTIPLEX;
	if (enc_master->intf_mode == INTF_MODE_VIDEO)
		dsc_common_mode |= DSC_MODE_VIDEO;

	this_frame_slices = pic_width / dsc->slice_width;
	intf_ip_w = this_frame_slices * dsc->slice_width;

	/*
	 * dsc merge case: when using 2 encoders for the same stream,
	 * no. of slices need to be same on both the encoders.
	 */
	enc_ip_w = intf_ip_w / 2;
	initial_lines = dpu_encoder_dsc_initial_line_calc(dsc, enc_ip_w);

	for (i = 0; i < MAX_CHANNELS_PER_ENC; i++)
		dpu_encoder_dsc_pipe_cfg(ctl, hw_dsc[i], hw_pp[i],
					 dsc, dsc_common_mode, initial_lines);
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

	if (dpu_enc->dsc)
		dpu_encoder_prep_dsc(dpu_enc, dpu_enc->dsc);
}

bool dpu_encoder_is_valid_for_commit(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc;
	unsigned int i;
	struct dpu_encoder_phys *phys;

	dpu_enc = to_dpu_encoder_virt(drm_enc);

	if (drm_enc->encoder_type == DRM_MODE_ENCODER_VIRTUAL) {
		for (i = 0; i < dpu_enc->num_phys_encs; i++) {
			phys = dpu_enc->phys_encs[i];
			if (phys->ops.is_valid_for_commit && !phys->ops.is_valid_for_commit(phys)) {
				DPU_DEBUG("invalid FB not kicking off\n");
				return false;
			}
		}
	}

	return true;
}

void dpu_encoder_kickoff(struct drm_encoder *drm_enc)
{
	struct dpu_encoder_virt *dpu_enc;
	struct dpu_encoder_phys *phys;
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

	DPU_ATRACE_END("encoder_kickoff");
}

static void dpu_encoder_helper_reset_mixers(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_mixer_cfg mixer;
	int i, num_lm;
	struct dpu_global_state *global_state;
	struct dpu_hw_blk *hw_lm[2];
	struct dpu_hw_mixer *hw_mixer[2];
	struct dpu_hw_ctl *ctl = phys_enc->hw_ctl;

	memset(&mixer, 0, sizeof(mixer));

	/* reset all mixers for this encoder */
	if (phys_enc->hw_ctl->ops.clear_all_blendstages)
		phys_enc->hw_ctl->ops.clear_all_blendstages(phys_enc->hw_ctl);

	global_state = dpu_kms_get_existing_global_state(phys_enc->dpu_kms);

	num_lm = dpu_rm_get_assigned_resources(&phys_enc->dpu_kms->rm, global_state,
		phys_enc->parent->base.id, DPU_HW_BLK_LM, hw_lm, ARRAY_SIZE(hw_lm));

	for (i = 0; i < num_lm; i++) {
		hw_mixer[i] = to_dpu_hw_mixer(hw_lm[i]);
		if (phys_enc->hw_ctl->ops.update_pending_flush_mixer)
			phys_enc->hw_ctl->ops.update_pending_flush_mixer(ctl, hw_mixer[i]->idx);

		/* clear all blendstages */
		if (phys_enc->hw_ctl->ops.setup_blendstage)
			phys_enc->hw_ctl->ops.setup_blendstage(ctl, hw_mixer[i]->idx, NULL);
	}
}

static void dpu_encoder_dsc_pipe_clr(struct dpu_hw_ctl *ctl,
				     struct dpu_hw_dsc *hw_dsc,
				     struct dpu_hw_pingpong *hw_pp)
{
	if (hw_dsc->ops.dsc_disable)
		hw_dsc->ops.dsc_disable(hw_dsc);

	if (hw_pp->ops.disable_dsc)
		hw_pp->ops.disable_dsc(hw_pp);

	if (hw_dsc->ops.dsc_bind_pingpong_blk)
		hw_dsc->ops.dsc_bind_pingpong_blk(hw_dsc, PINGPONG_NONE);

	if (ctl->ops.update_pending_flush_dsc)
		ctl->ops.update_pending_flush_dsc(ctl, hw_dsc->idx);
}

static void dpu_encoder_unprep_dsc(struct dpu_encoder_virt *dpu_enc)
{
	/* coding only for 2LM, 2enc, 1 dsc config */
	struct dpu_encoder_phys *enc_master = dpu_enc->cur_master;
	struct dpu_hw_ctl *ctl = enc_master->hw_ctl;
	struct dpu_hw_dsc *hw_dsc[MAX_CHANNELS_PER_ENC];
	struct dpu_hw_pingpong *hw_pp[MAX_CHANNELS_PER_ENC];
	int i;

	for (i = 0; i < MAX_CHANNELS_PER_ENC; i++) {
		hw_pp[i] = dpu_enc->hw_pp[i];
		hw_dsc[i] = dpu_enc->hw_dsc[i];

		if (hw_pp[i] && hw_dsc[i])
			dpu_encoder_dsc_pipe_clr(ctl, hw_dsc[i], hw_pp[i]);
	}
}

void dpu_encoder_helper_phys_cleanup(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_ctl *ctl = phys_enc->hw_ctl;
	struct dpu_hw_intf_cfg intf_cfg = { 0 };
	int i;
	struct dpu_encoder_virt *dpu_enc;

	dpu_enc = to_dpu_encoder_virt(phys_enc->parent);

	phys_enc->hw_ctl->ops.reset(ctl);

	dpu_encoder_helper_reset_mixers(phys_enc);

	/*
	 * TODO: move the once-only operation like CTL flush/trigger
	 * into dpu_encoder_virt_disable() and all operations which need
	 * to be done per phys encoder into the phys_disable() op.
	 */
	if (phys_enc->hw_wb) {
		/* disable the PP block */
		if (phys_enc->hw_wb->ops.bind_pingpong_blk)
			phys_enc->hw_wb->ops.bind_pingpong_blk(phys_enc->hw_wb, PINGPONG_NONE);

		/* mark WB flush as pending */
		if (phys_enc->hw_ctl->ops.update_pending_flush_wb)
			phys_enc->hw_ctl->ops.update_pending_flush_wb(ctl, phys_enc->hw_wb->idx);
	} else {
		for (i = 0; i < dpu_enc->num_phys_encs; i++) {
			if (dpu_enc->phys_encs[i] && phys_enc->hw_intf->ops.bind_pingpong_blk)
				phys_enc->hw_intf->ops.bind_pingpong_blk(
						dpu_enc->phys_encs[i]->hw_intf,
						PINGPONG_NONE);

			/* mark INTF flush as pending */
			if (phys_enc->hw_ctl->ops.update_pending_flush_intf)
				phys_enc->hw_ctl->ops.update_pending_flush_intf(phys_enc->hw_ctl,
						dpu_enc->phys_encs[i]->hw_intf->idx);
		}
	}

	/* reset the merge 3D HW block */
	if (phys_enc->hw_pp && phys_enc->hw_pp->merge_3d) {
		phys_enc->hw_pp->merge_3d->ops.setup_3d_mode(phys_enc->hw_pp->merge_3d,
				BLEND_3D_NONE);
		if (phys_enc->hw_ctl->ops.update_pending_flush_merge_3d)
			phys_enc->hw_ctl->ops.update_pending_flush_merge_3d(ctl,
					phys_enc->hw_pp->merge_3d->idx);
	}

	if (phys_enc->hw_cdm) {
		if (phys_enc->hw_cdm->ops.bind_pingpong_blk && phys_enc->hw_pp)
			phys_enc->hw_cdm->ops.bind_pingpong_blk(phys_enc->hw_cdm,
								PINGPONG_NONE);
		if (phys_enc->hw_ctl->ops.update_pending_flush_cdm)
			phys_enc->hw_ctl->ops.update_pending_flush_cdm(phys_enc->hw_ctl,
								       phys_enc->hw_cdm->idx);
	}

	if (dpu_enc->dsc) {
		dpu_encoder_unprep_dsc(dpu_enc);
		dpu_enc->dsc = NULL;
	}

	intf_cfg.stream_sel = 0; /* Don't care value for video mode */
	intf_cfg.mode_3d = dpu_encoder_helper_get_3d_blend_mode(phys_enc);
	intf_cfg.dsc = dpu_encoder_helper_get_dsc(phys_enc);

	if (phys_enc->hw_intf)
		intf_cfg.intf = phys_enc->hw_intf->idx;
	if (phys_enc->hw_wb)
		intf_cfg.wb = phys_enc->hw_wb->idx;

	if (phys_enc->hw_pp && phys_enc->hw_pp->merge_3d)
		intf_cfg.merge_3d = phys_enc->hw_pp->merge_3d->idx;

	if (ctl->ops.reset_intf_cfg)
		ctl->ops.reset_intf_cfg(ctl, &intf_cfg);

	ctl->ops.trigger_flush(ctl);
	ctl->ops.trigger_start(ctl);
	ctl->ops.clear_pending_flush(ctl);
}

void dpu_encoder_helper_phys_setup_cdm(struct dpu_encoder_phys *phys_enc,
				       const struct msm_format *dpu_fmt,
				       u32 output_type)
{
	struct dpu_hw_cdm *hw_cdm;
	struct dpu_hw_cdm_cfg *cdm_cfg;
	struct dpu_hw_pingpong *hw_pp;
	int ret;

	if (!phys_enc)
		return;

	cdm_cfg = &phys_enc->cdm_cfg;
	hw_pp = phys_enc->hw_pp;
	hw_cdm = phys_enc->hw_cdm;

	if (!hw_cdm)
		return;

	if (!MSM_FORMAT_IS_YUV(dpu_fmt)) {
		DPU_DEBUG("[enc:%d] cdm_disable fmt:%p4cc\n", DRMID(phys_enc->parent),
			  &dpu_fmt->pixel_format);
		if (hw_cdm->ops.bind_pingpong_blk)
			hw_cdm->ops.bind_pingpong_blk(hw_cdm, PINGPONG_NONE);

		return;
	}

	memset(cdm_cfg, 0, sizeof(struct dpu_hw_cdm_cfg));

	cdm_cfg->output_width = phys_enc->cached_mode.hdisplay;
	cdm_cfg->output_height = phys_enc->cached_mode.vdisplay;
	cdm_cfg->output_fmt = dpu_fmt;
	cdm_cfg->output_type = output_type;
	cdm_cfg->output_bit_depth = MSM_FORMAT_IS_DX(dpu_fmt) ?
			CDM_CDWN_OUTPUT_10BIT : CDM_CDWN_OUTPUT_8BIT;
	cdm_cfg->csc_cfg = &dpu_csc10_rgb2yuv_601l;

	/* enable 10 bit logic */
	switch (cdm_cfg->output_fmt->chroma_sample) {
	case CHROMA_FULL:
		cdm_cfg->h_cdwn_type = CDM_CDWN_DISABLE;
		cdm_cfg->v_cdwn_type = CDM_CDWN_DISABLE;
		break;
	case CHROMA_H2V1:
		cdm_cfg->h_cdwn_type = CDM_CDWN_COSITE;
		cdm_cfg->v_cdwn_type = CDM_CDWN_DISABLE;
		break;
	case CHROMA_420:
		cdm_cfg->h_cdwn_type = CDM_CDWN_COSITE;
		cdm_cfg->v_cdwn_type = CDM_CDWN_OFFSITE;
		break;
	case CHROMA_H1V2:
	default:
		DPU_ERROR("[enc:%d] unsupported chroma sampling type\n",
			  DRMID(phys_enc->parent));
		cdm_cfg->h_cdwn_type = CDM_CDWN_DISABLE;
		cdm_cfg->v_cdwn_type = CDM_CDWN_DISABLE;
		break;
	}

	DPU_DEBUG("[enc:%d] cdm_enable:%d,%d,%p4cc,%d,%d,%d,%d]\n",
		  DRMID(phys_enc->parent), cdm_cfg->output_width,
		  cdm_cfg->output_height, &cdm_cfg->output_fmt->pixel_format,
		  cdm_cfg->output_type, cdm_cfg->output_bit_depth,
		  cdm_cfg->h_cdwn_type, cdm_cfg->v_cdwn_type);

	if (hw_cdm->ops.enable) {
		cdm_cfg->pp_id = hw_pp->idx;
		ret = hw_cdm->ops.enable(hw_cdm, cdm_cfg);
		if (ret < 0) {
			DPU_ERROR("[enc:%d] failed to enable CDM; ret:%d\n",
				  DRMID(phys_enc->parent), ret);
			return;
		}
	}
}

#ifdef CONFIG_DEBUG_FS
static int _dpu_encoder_status_show(struct seq_file *s, void *data)
{
	struct drm_encoder *drm_enc = s->private;
	struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(drm_enc);
	int i;

	mutex_lock(&dpu_enc->enc_lock);
	for (i = 0; i < dpu_enc->num_phys_encs; i++) {
		struct dpu_encoder_phys *phys = dpu_enc->phys_encs[i];

		seq_printf(s, "intf:%d  wb:%d  vsync:%8d     underrun:%8d    frame_done_cnt:%d",
				phys->hw_intf ? phys->hw_intf->idx - INTF_0 : -1,
				phys->hw_wb ? phys->hw_wb->idx - WB_0 : -1,
				atomic_read(&phys->vsync_cnt),
				atomic_read(&phys->underrun_cnt),
				atomic_read(&dpu_enc->frame_done_timeout_cnt));

		seq_printf(s, "mode: %s\n", dpu_encoder_helper_get_intf_type(phys->intf_mode));
	}
	mutex_unlock(&dpu_enc->enc_lock);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(_dpu_encoder_status);

static void dpu_encoder_debugfs_init(struct drm_encoder *drm_enc, struct dentry *root)
{
	/* don't error check these */
	debugfs_create_file("status", 0600,
			    root, drm_enc, &_dpu_encoder_status_fops);
}
#else
#define dpu_encoder_debugfs_init NULL
#endif

static int dpu_encoder_virt_add_phys_encs(
		struct drm_device *dev,
		struct msm_display_info *disp_info,
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


	if (disp_info->intf_type == INTF_WB) {
		enc = dpu_encoder_phys_wb_init(dev, params);

		if (IS_ERR(enc)) {
			DPU_ERROR_ENC(dpu_enc, "failed to init wb enc: %ld\n",
				PTR_ERR(enc));
			return PTR_ERR(enc);
		}

		dpu_enc->phys_encs[dpu_enc->num_phys_encs] = enc;
		++dpu_enc->num_phys_encs;
	} else if (disp_info->is_cmd_mode) {
		enc = dpu_encoder_phys_cmd_init(dev, params);

		if (IS_ERR(enc)) {
			DPU_ERROR_ENC(dpu_enc, "failed to init cmd enc: %ld\n",
				PTR_ERR(enc));
			return PTR_ERR(enc);
		}

		dpu_enc->phys_encs[dpu_enc->num_phys_encs] = enc;
		++dpu_enc->num_phys_encs;
	} else {
		enc = dpu_encoder_phys_vid_init(dev, params);

		if (IS_ERR(enc)) {
			DPU_ERROR_ENC(dpu_enc, "failed to init vid enc: %ld\n",
				PTR_ERR(enc));
			return PTR_ERR(enc);
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

static int dpu_encoder_setup_display(struct dpu_encoder_virt *dpu_enc,
				 struct dpu_kms *dpu_kms,
				 struct msm_display_info *disp_info)
{
	int ret = 0;
	int i = 0;
	struct dpu_enc_phys_init_params phys_params;

	if (!dpu_enc) {
		DPU_ERROR("invalid arg(s), enc %d\n", dpu_enc != NULL);
		return -EINVAL;
	}

	dpu_enc->cur_master = NULL;

	memset(&phys_params, 0, sizeof(phys_params));
	phys_params.dpu_kms = dpu_kms;
	phys_params.parent = &dpu_enc->base;
	phys_params.enc_spinlock = &dpu_enc->enc_spinlock;

	WARN_ON(disp_info->num_of_h_tiles < 1);

	DPU_DEBUG("dsi_info->num_of_h_tiles %d\n", disp_info->num_of_h_tiles);

	if (disp_info->intf_type != INTF_WB)
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

		phys_params.hw_intf = dpu_encoder_get_intf(dpu_kms->catalog, &dpu_kms->rm,
							   disp_info->intf_type,
							   controller_id);

		if (disp_info->intf_type == INTF_WB && controller_id < WB_MAX)
			phys_params.hw_wb = dpu_rm_get_wb(&dpu_kms->rm, controller_id);

		if (!phys_params.hw_intf && !phys_params.hw_wb) {
			DPU_ERROR_ENC(dpu_enc, "no intf or wb block assigned at idx: %d\n", i);
			ret = -EINVAL;
			break;
		}

		if (phys_params.hw_intf && phys_params.hw_wb) {
			DPU_ERROR_ENC(dpu_enc,
					"invalid phys both intf and wb block at idx: %d\n", i);
			ret = -EINVAL;
			break;
		}

		ret = dpu_encoder_virt_add_phys_encs(dpu_kms->dev, disp_info,
				dpu_enc, &phys_params);
		if (ret) {
			DPU_ERROR_ENC(dpu_enc, "failed to add phys encs\n");
			break;
		}
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

	if (!dpu_enc->frame_busy_mask[0] || !dpu_enc->crtc) {
		DRM_DEBUG_KMS("id:%u invalid timeout frame_busy_mask=%lu\n",
			      DRMID(drm_enc), dpu_enc->frame_busy_mask[0]);
		return;
	} else if (!atomic_xchg(&dpu_enc->frame_done_timeout_ms, 0)) {
		DRM_DEBUG_KMS("id:%u invalid timeout\n", DRMID(drm_enc));
		return;
	}

	DPU_ERROR_ENC_RATELIMITED(dpu_enc, "frame done timeout\n");

	if (atomic_inc_return(&dpu_enc->frame_done_timeout_cnt) == 1)
		msm_disp_snapshot_state(drm_enc->dev);

	event = DPU_ENCODER_FRAME_EVENT_ERROR;
	trace_dpu_enc_frame_done_timeout(DRMID(drm_enc), event);
	dpu_crtc_frame_event_cb(dpu_enc->crtc, event);
}

static const struct drm_encoder_helper_funcs dpu_encoder_helper_funcs = {
	.atomic_mode_set = dpu_encoder_virt_atomic_mode_set,
	.atomic_disable = dpu_encoder_virt_atomic_disable,
	.atomic_enable = dpu_encoder_virt_atomic_enable,
	.atomic_check = dpu_encoder_virt_atomic_check,
};

static const struct drm_encoder_funcs dpu_encoder_funcs = {
	.debugfs_init = dpu_encoder_debugfs_init,
};

struct drm_encoder *dpu_encoder_init(struct drm_device *dev,
		int drm_enc_mode,
		struct msm_display_info *disp_info)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct dpu_kms *dpu_kms = to_dpu_kms(priv->kms);
	struct dpu_encoder_virt *dpu_enc;
	int ret;

	dpu_enc = drmm_encoder_alloc(dev, struct dpu_encoder_virt, base,
				     &dpu_encoder_funcs, drm_enc_mode, NULL);
	if (IS_ERR(dpu_enc))
		return ERR_CAST(dpu_enc);

	drm_encoder_helper_add(&dpu_enc->base, &dpu_encoder_helper_funcs);

	spin_lock_init(&dpu_enc->enc_spinlock);
	dpu_enc->enabled = false;
	mutex_init(&dpu_enc->enc_lock);
	mutex_init(&dpu_enc->rc_lock);

	ret = dpu_encoder_setup_display(dpu_enc, dpu_kms, disp_info);
	if (ret) {
		DPU_ERROR("failed to setup encoder\n");
		return ERR_PTR(-ENOMEM);
	}

	atomic_set(&dpu_enc->frame_done_timeout_ms, 0);
	atomic_set(&dpu_enc->frame_done_timeout_cnt, 0);
	timer_setup(&dpu_enc->frame_done_timer,
			dpu_encoder_frame_done_timeout, 0);

	INIT_DELAYED_WORK(&dpu_enc->delayed_off_work,
			dpu_encoder_off_work);
	dpu_enc->idle_timeout = IDLE_TIMEOUT;

	memcpy(&dpu_enc->disp_info, disp_info, sizeof(*disp_info));

	DPU_DEBUG_ENC(dpu_enc, "created\n");

	return &dpu_enc->base;
}

/**
 * dpu_encoder_wait_for_commit_done() - Wait for encoder to flush pending state
 * @drm_enc:	encoder pointer
 *
 * Wait for hardware to have flushed the current pending changes to hardware at
 * a vblank or CTL_START. Physical encoders will map this differently depending
 * on the type: vid mode -> vsync_irq, cmd mode -> CTL_START.
 *
 * Return: 0 on success, -EWOULDBLOCK if already signaled, error otherwise
 */
int dpu_encoder_wait_for_commit_done(struct drm_encoder *drm_enc)
{
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

		if (phys->ops.wait_for_commit_done) {
			DPU_ATRACE_BEGIN("wait_for_commit_done");
			ret = phys->ops.wait_for_commit_done(phys);
			DPU_ATRACE_END("wait_for_commit_done");
			if (ret == -ETIMEDOUT && !dpu_enc->commit_done_timedout) {
				dpu_enc->commit_done_timedout = true;
				msm_disp_snapshot_state(drm_enc->dev);
			}
			if (ret)
				return ret;
		}
	}

	return ret;
}

/**
 * dpu_encoder_wait_for_tx_complete() - Wait for encoder to transfer pixels to panel
 * @drm_enc:	encoder pointer
 *
 * Wait for the hardware to transfer all the pixels to the panel. Physical
 * encoders will map this differently depending on the type: vid mode -> vsync_irq,
 * cmd mode -> pp_done.
 *
 * Return: 0 on success, -EWOULDBLOCK if already signaled, error otherwise
 */
int dpu_encoder_wait_for_tx_complete(struct drm_encoder *drm_enc)
{
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

		if (phys->ops.wait_for_tx_complete) {
			DPU_ATRACE_BEGIN("wait_for_tx_complete");
			ret = phys->ops.wait_for_tx_complete(phys);
			DPU_ATRACE_END("wait_for_tx_complete");
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

unsigned int dpu_encoder_helper_get_dsc(struct dpu_encoder_phys *phys_enc)
{
	struct drm_encoder *encoder = phys_enc->parent;
	struct dpu_encoder_virt *dpu_enc = to_dpu_encoder_virt(encoder);

	return dpu_enc->dsc_mask;
}

void dpu_encoder_phys_init(struct dpu_encoder_phys *phys_enc,
			  struct dpu_enc_phys_init_params *p)
{
	phys_enc->hw_mdptop = p->dpu_kms->hw_mdp;
	phys_enc->hw_intf = p->hw_intf;
	phys_enc->hw_wb = p->hw_wb;
	phys_enc->parent = p->parent;
	phys_enc->dpu_kms = p->dpu_kms;
	phys_enc->split_role = p->split_role;
	phys_enc->enc_spinlock = p->enc_spinlock;
	phys_enc->enable_state = DPU_ENC_DISABLED;

	atomic_set(&phys_enc->pending_kickoff_cnt, 0);
	atomic_set(&phys_enc->pending_ctlstart_cnt, 0);

	atomic_set(&phys_enc->vsync_cnt, 0);
	atomic_set(&phys_enc->underrun_cnt, 0);

	init_waitqueue_head(&phys_enc->pending_kickoff_wq);
}
