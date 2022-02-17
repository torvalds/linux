// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2018, 2020-2021 The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__
#include <linux/delay.h>
#include "dpu_encoder_phys.h"
#include "dpu_hw_interrupts.h"
#include "dpu_hw_pingpong.h"
#include "dpu_core_irq.h"
#include "dpu_formats.h"
#include "dpu_trace.h"
#include "disp/msm_disp_snapshot.h"

#define DPU_DEBUG_CMDENC(e, fmt, ...) DPU_DEBUG("enc%d intf%d " fmt, \
		(e) && (e)->base.parent ? \
		(e)->base.parent->base.id : -1, \
		(e) ? (e)->base.intf_idx - INTF_0 : -1, ##__VA_ARGS__)

#define DPU_ERROR_CMDENC(e, fmt, ...) DPU_ERROR("enc%d intf%d " fmt, \
		(e) && (e)->base.parent ? \
		(e)->base.parent->base.id : -1, \
		(e) ? (e)->base.intf_idx - INTF_0 : -1, ##__VA_ARGS__)

#define to_dpu_encoder_phys_cmd(x) \
	container_of(x, struct dpu_encoder_phys_cmd, base)

#define PP_TIMEOUT_MAX_TRIALS	10

/*
 * Tearcheck sync start and continue thresholds are empirically found
 * based on common panels In the future, may want to allow panels to override
 * these default values
 */
#define DEFAULT_TEARCHECK_SYNC_THRESH_START	4
#define DEFAULT_TEARCHECK_SYNC_THRESH_CONTINUE	4

#define DPU_ENC_WR_PTR_START_TIMEOUT_US 20000

#define DPU_ENC_MAX_POLL_TIMEOUT_US	2000

static bool dpu_encoder_phys_cmd_is_master(struct dpu_encoder_phys *phys_enc)
{
	return (phys_enc->split_role != ENC_ROLE_SLAVE);
}

static void _dpu_encoder_phys_cmd_update_intf_cfg(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_cmd *cmd_enc =
			to_dpu_encoder_phys_cmd(phys_enc);
	struct dpu_hw_ctl *ctl;
	struct dpu_hw_intf_cfg intf_cfg = { 0 };

	ctl = phys_enc->hw_ctl;
	if (!ctl->ops.setup_intf_cfg)
		return;

	intf_cfg.intf = phys_enc->intf_idx;
	intf_cfg.intf_mode_sel = DPU_CTL_MODE_SEL_CMD;
	intf_cfg.stream_sel = cmd_enc->stream_sel;
	intf_cfg.mode_3d = dpu_encoder_helper_get_3d_blend_mode(phys_enc);
	ctl->ops.setup_intf_cfg(ctl, &intf_cfg);
}

static void dpu_encoder_phys_cmd_pp_tx_done_irq(void *arg, int irq_idx)
{
	struct dpu_encoder_phys *phys_enc = arg;
	unsigned long lock_flags;
	int new_cnt;
	u32 event = DPU_ENCODER_FRAME_EVENT_DONE;

	if (!phys_enc->hw_pp)
		return;

	DPU_ATRACE_BEGIN("pp_done_irq");
	/* notify all synchronous clients first, then asynchronous clients */
	if (phys_enc->parent_ops->handle_frame_done)
		phys_enc->parent_ops->handle_frame_done(phys_enc->parent,
				phys_enc, event);

	spin_lock_irqsave(phys_enc->enc_spinlock, lock_flags);
	new_cnt = atomic_add_unless(&phys_enc->pending_kickoff_cnt, -1, 0);
	spin_unlock_irqrestore(phys_enc->enc_spinlock, lock_flags);

	trace_dpu_enc_phys_cmd_pp_tx_done(DRMID(phys_enc->parent),
					  phys_enc->hw_pp->idx - PINGPONG_0,
					  new_cnt, event);

	/* Signal any waiting atomic commit thread */
	wake_up_all(&phys_enc->pending_kickoff_wq);
	DPU_ATRACE_END("pp_done_irq");
}

static void dpu_encoder_phys_cmd_pp_rd_ptr_irq(void *arg, int irq_idx)
{
	struct dpu_encoder_phys *phys_enc = arg;
	struct dpu_encoder_phys_cmd *cmd_enc;

	if (!phys_enc->hw_pp)
		return;

	DPU_ATRACE_BEGIN("rd_ptr_irq");
	cmd_enc = to_dpu_encoder_phys_cmd(phys_enc);

	if (phys_enc->parent_ops->handle_vblank_virt)
		phys_enc->parent_ops->handle_vblank_virt(phys_enc->parent,
			phys_enc);

	atomic_add_unless(&cmd_enc->pending_vblank_cnt, -1, 0);
	wake_up_all(&cmd_enc->pending_vblank_wq);
	DPU_ATRACE_END("rd_ptr_irq");
}

static void dpu_encoder_phys_cmd_ctl_start_irq(void *arg, int irq_idx)
{
	struct dpu_encoder_phys *phys_enc = arg;

	DPU_ATRACE_BEGIN("ctl_start_irq");

	atomic_add_unless(&phys_enc->pending_ctlstart_cnt, -1, 0);

	/* Signal any waiting ctl start interrupt */
	wake_up_all(&phys_enc->pending_kickoff_wq);
	DPU_ATRACE_END("ctl_start_irq");
}

static void dpu_encoder_phys_cmd_underrun_irq(void *arg, int irq_idx)
{
	struct dpu_encoder_phys *phys_enc = arg;

	if (phys_enc->parent_ops->handle_underrun_virt)
		phys_enc->parent_ops->handle_underrun_virt(phys_enc->parent,
			phys_enc);
}

static void dpu_encoder_phys_cmd_atomic_mode_set(
		struct dpu_encoder_phys *phys_enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state)
{
	struct dpu_encoder_irq *irq;

	irq = &phys_enc->irq[INTR_IDX_CTL_START];
	irq->irq_idx = phys_enc->hw_ctl->caps->intr_start;

	irq = &phys_enc->irq[INTR_IDX_PINGPONG];
	irq->irq_idx = phys_enc->hw_pp->caps->intr_done;

	irq = &phys_enc->irq[INTR_IDX_RDPTR];
	irq->irq_idx = phys_enc->hw_pp->caps->intr_rdptr;

	irq = &phys_enc->irq[INTR_IDX_UNDERRUN];
	irq->irq_idx = phys_enc->hw_intf->cap->intr_underrun;
}

static int _dpu_encoder_phys_cmd_handle_ppdone_timeout(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_cmd *cmd_enc =
			to_dpu_encoder_phys_cmd(phys_enc);
	u32 frame_event = DPU_ENCODER_FRAME_EVENT_ERROR;
	bool do_log = false;
	struct drm_encoder *drm_enc;

	if (!phys_enc->hw_pp)
		return -EINVAL;

	drm_enc = phys_enc->parent;

	cmd_enc->pp_timeout_report_cnt++;
	if (cmd_enc->pp_timeout_report_cnt == PP_TIMEOUT_MAX_TRIALS) {
		frame_event |= DPU_ENCODER_FRAME_EVENT_PANEL_DEAD;
		do_log = true;
	} else if (cmd_enc->pp_timeout_report_cnt == 1) {
		do_log = true;
	}

	trace_dpu_enc_phys_cmd_pdone_timeout(DRMID(drm_enc),
		     phys_enc->hw_pp->idx - PINGPONG_0,
		     cmd_enc->pp_timeout_report_cnt,
		     atomic_read(&phys_enc->pending_kickoff_cnt),
		     frame_event);

	/* to avoid flooding, only log first time, and "dead" time */
	if (do_log) {
		DRM_ERROR("id:%d pp:%d kickoff timeout %d cnt %d koff_cnt %d\n",
			  DRMID(drm_enc),
			  phys_enc->hw_pp->idx - PINGPONG_0,
			  phys_enc->hw_ctl->idx - CTL_0,
			  cmd_enc->pp_timeout_report_cnt,
			  atomic_read(&phys_enc->pending_kickoff_cnt));
		msm_disp_snapshot_state(drm_enc->dev);
		dpu_encoder_helper_unregister_irq(phys_enc, INTR_IDX_RDPTR);
	}

	atomic_add_unless(&phys_enc->pending_kickoff_cnt, -1, 0);

	/* request a ctl reset before the next kickoff */
	phys_enc->enable_state = DPU_ENC_ERR_NEEDS_HW_RESET;

	if (phys_enc->parent_ops->handle_frame_done)
		phys_enc->parent_ops->handle_frame_done(
				drm_enc, phys_enc, frame_event);

	return -ETIMEDOUT;
}

static int _dpu_encoder_phys_cmd_wait_for_idle(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_cmd *cmd_enc =
			to_dpu_encoder_phys_cmd(phys_enc);
	struct dpu_encoder_wait_info wait_info;
	int ret;

	wait_info.wq = &phys_enc->pending_kickoff_wq;
	wait_info.atomic_cnt = &phys_enc->pending_kickoff_cnt;
	wait_info.timeout_ms = KICKOFF_TIMEOUT_MS;

	ret = dpu_encoder_helper_wait_for_irq(phys_enc, INTR_IDX_PINGPONG,
			&wait_info);
	if (ret == -ETIMEDOUT)
		_dpu_encoder_phys_cmd_handle_ppdone_timeout(phys_enc);
	else if (!ret)
		cmd_enc->pp_timeout_report_cnt = 0;

	return ret;
}

static int dpu_encoder_phys_cmd_control_vblank_irq(
		struct dpu_encoder_phys *phys_enc,
		bool enable)
{
	int ret = 0;
	int refcount;

	if (!phys_enc->hw_pp) {
		DPU_ERROR("invalid encoder\n");
		return -EINVAL;
	}

	refcount = atomic_read(&phys_enc->vblank_refcount);

	/* Slave encoders don't report vblank */
	if (!dpu_encoder_phys_cmd_is_master(phys_enc))
		goto end;

	/* protect against negative */
	if (!enable && refcount == 0) {
		ret = -EINVAL;
		goto end;
	}

	DRM_DEBUG_KMS("id:%u pp:%d enable=%s/%d\n", DRMID(phys_enc->parent),
		      phys_enc->hw_pp->idx - PINGPONG_0,
		      enable ? "true" : "false", refcount);

	if (enable && atomic_inc_return(&phys_enc->vblank_refcount) == 1)
		ret = dpu_encoder_helper_register_irq(phys_enc, INTR_IDX_RDPTR);
	else if (!enable && atomic_dec_return(&phys_enc->vblank_refcount) == 0)
		ret = dpu_encoder_helper_unregister_irq(phys_enc,
				INTR_IDX_RDPTR);

end:
	if (ret) {
		DRM_ERROR("vblank irq err id:%u pp:%d ret:%d, enable %s/%d\n",
			  DRMID(phys_enc->parent),
			  phys_enc->hw_pp->idx - PINGPONG_0, ret,
			  enable ? "true" : "false", refcount);
	}

	return ret;
}

static void dpu_encoder_phys_cmd_irq_control(struct dpu_encoder_phys *phys_enc,
		bool enable)
{
	trace_dpu_enc_phys_cmd_irq_ctrl(DRMID(phys_enc->parent),
			phys_enc->hw_pp->idx - PINGPONG_0,
			enable, atomic_read(&phys_enc->vblank_refcount));

	if (enable) {
		dpu_encoder_helper_register_irq(phys_enc, INTR_IDX_PINGPONG);
		dpu_encoder_helper_register_irq(phys_enc, INTR_IDX_UNDERRUN);
		dpu_encoder_phys_cmd_control_vblank_irq(phys_enc, true);

		if (dpu_encoder_phys_cmd_is_master(phys_enc))
			dpu_encoder_helper_register_irq(phys_enc,
					INTR_IDX_CTL_START);
	} else {
		if (dpu_encoder_phys_cmd_is_master(phys_enc))
			dpu_encoder_helper_unregister_irq(phys_enc,
					INTR_IDX_CTL_START);

		dpu_encoder_helper_unregister_irq(phys_enc, INTR_IDX_UNDERRUN);
		dpu_encoder_phys_cmd_control_vblank_irq(phys_enc, false);
		dpu_encoder_helper_unregister_irq(phys_enc, INTR_IDX_PINGPONG);
	}
}

static void dpu_encoder_phys_cmd_tearcheck_config(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_cmd *cmd_enc =
		to_dpu_encoder_phys_cmd(phys_enc);
	struct dpu_hw_tear_check tc_cfg = { 0 };
	struct drm_display_mode *mode;
	bool tc_enable = true;
	u32 vsync_hz;
	struct dpu_kms *dpu_kms;

	if (!phys_enc->hw_pp) {
		DPU_ERROR("invalid encoder\n");
		return;
	}
	mode = &phys_enc->cached_mode;

	DPU_DEBUG_CMDENC(cmd_enc, "pp %d\n", phys_enc->hw_pp->idx - PINGPONG_0);

	if (!phys_enc->hw_pp->ops.setup_tearcheck ||
		!phys_enc->hw_pp->ops.enable_tearcheck) {
		DPU_DEBUG_CMDENC(cmd_enc, "tearcheck not supported\n");
		return;
	}

	dpu_kms = phys_enc->dpu_kms;

	/*
	 * TE default: dsi byte clock calculated base on 70 fps;
	 * around 14 ms to complete a kickoff cycle if te disabled;
	 * vclk_line base on 60 fps; write is faster than read;
	 * init == start == rdptr;
	 *
	 * vsync_count is ratio of MDP VSYNC clock frequency to LCD panel
	 * frequency divided by the no. of rows (lines) in the LCDpanel.
	 */
	vsync_hz = dpu_kms_get_clk_rate(dpu_kms, "vsync");
	if (vsync_hz <= 0) {
		DPU_DEBUG_CMDENC(cmd_enc, "invalid - vsync_hz %u\n",
				 vsync_hz);
		return;
	}

	tc_cfg.vsync_count = vsync_hz /
				(mode->vtotal * drm_mode_vrefresh(mode));

	/*
	 * Set the sync_cfg_height to twice vtotal so that if we lose a
	 * TE event coming from the display TE pin we won't stall immediately
	 */
	tc_cfg.hw_vsync_mode = 1;
	tc_cfg.sync_cfg_height = mode->vtotal * 2;
	tc_cfg.vsync_init_val = mode->vdisplay;
	tc_cfg.sync_threshold_start = DEFAULT_TEARCHECK_SYNC_THRESH_START;
	tc_cfg.sync_threshold_continue = DEFAULT_TEARCHECK_SYNC_THRESH_CONTINUE;
	tc_cfg.start_pos = mode->vdisplay;
	tc_cfg.rd_ptr_irq = mode->vdisplay + 1;

	DPU_DEBUG_CMDENC(cmd_enc,
		"tc %d vsync_clk_speed_hz %u vtotal %u vrefresh %u\n",
		phys_enc->hw_pp->idx - PINGPONG_0, vsync_hz,
		mode->vtotal, drm_mode_vrefresh(mode));
	DPU_DEBUG_CMDENC(cmd_enc,
		"tc %d enable %u start_pos %u rd_ptr_irq %u\n",
		phys_enc->hw_pp->idx - PINGPONG_0, tc_enable, tc_cfg.start_pos,
		tc_cfg.rd_ptr_irq);
	DPU_DEBUG_CMDENC(cmd_enc,
		"tc %d hw_vsync_mode %u vsync_count %u vsync_init_val %u\n",
		phys_enc->hw_pp->idx - PINGPONG_0, tc_cfg.hw_vsync_mode,
		tc_cfg.vsync_count, tc_cfg.vsync_init_val);
	DPU_DEBUG_CMDENC(cmd_enc,
		"tc %d cfgheight %u thresh_start %u thresh_cont %u\n",
		phys_enc->hw_pp->idx - PINGPONG_0, tc_cfg.sync_cfg_height,
		tc_cfg.sync_threshold_start, tc_cfg.sync_threshold_continue);

	phys_enc->hw_pp->ops.setup_tearcheck(phys_enc->hw_pp, &tc_cfg);
	phys_enc->hw_pp->ops.enable_tearcheck(phys_enc->hw_pp, tc_enable);
}

static void _dpu_encoder_phys_cmd_pingpong_config(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_cmd *cmd_enc =
		to_dpu_encoder_phys_cmd(phys_enc);

	if (!phys_enc->hw_pp || !phys_enc->hw_ctl->ops.setup_intf_cfg) {
		DPU_ERROR("invalid arg(s), enc %d\n", phys_enc != NULL);
		return;
	}

	DPU_DEBUG_CMDENC(cmd_enc, "pp %d, enabling mode:\n",
			phys_enc->hw_pp->idx - PINGPONG_0);
	drm_mode_debug_printmodeline(&phys_enc->cached_mode);

	_dpu_encoder_phys_cmd_update_intf_cfg(phys_enc);
	dpu_encoder_phys_cmd_tearcheck_config(phys_enc);
}

static bool dpu_encoder_phys_cmd_needs_single_flush(
		struct dpu_encoder_phys *phys_enc)
{
	/**
	 * we do separate flush for each CTL and let
	 * CTL_START synchronize them
	 */
	return false;
}

static void dpu_encoder_phys_cmd_enable_helper(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_ctl *ctl;

	if (!phys_enc->hw_pp) {
		DPU_ERROR("invalid arg(s), encoder %d\n", phys_enc != NULL);
		return;
	}

	dpu_encoder_helper_split_config(phys_enc, phys_enc->intf_idx);

	_dpu_encoder_phys_cmd_pingpong_config(phys_enc);

	if (!dpu_encoder_phys_cmd_is_master(phys_enc))
		return;

	ctl = phys_enc->hw_ctl;
	ctl->ops.update_pending_flush_intf(ctl, phys_enc->intf_idx);
}

static void dpu_encoder_phys_cmd_enable(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_cmd *cmd_enc =
		to_dpu_encoder_phys_cmd(phys_enc);

	if (!phys_enc->hw_pp) {
		DPU_ERROR("invalid phys encoder\n");
		return;
	}

	DPU_DEBUG_CMDENC(cmd_enc, "pp %d\n", phys_enc->hw_pp->idx - PINGPONG_0);

	if (phys_enc->enable_state == DPU_ENC_ENABLED) {
		DPU_ERROR("already enabled\n");
		return;
	}

	dpu_encoder_phys_cmd_enable_helper(phys_enc);
	phys_enc->enable_state = DPU_ENC_ENABLED;
}

static void _dpu_encoder_phys_cmd_connect_te(
		struct dpu_encoder_phys *phys_enc, bool enable)
{
	if (!phys_enc->hw_pp || !phys_enc->hw_pp->ops.connect_external_te)
		return;

	trace_dpu_enc_phys_cmd_connect_te(DRMID(phys_enc->parent), enable);
	phys_enc->hw_pp->ops.connect_external_te(phys_enc->hw_pp, enable);
}

static void dpu_encoder_phys_cmd_prepare_idle_pc(
		struct dpu_encoder_phys *phys_enc)
{
	_dpu_encoder_phys_cmd_connect_te(phys_enc, false);
}

static int dpu_encoder_phys_cmd_get_line_count(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_pingpong *hw_pp;

	if (!phys_enc->hw_pp)
		return -EINVAL;

	if (!dpu_encoder_phys_cmd_is_master(phys_enc))
		return -EINVAL;

	hw_pp = phys_enc->hw_pp;
	if (!hw_pp->ops.get_line_count)
		return -EINVAL;

	return hw_pp->ops.get_line_count(hw_pp);
}

static void dpu_encoder_phys_cmd_disable(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_cmd *cmd_enc =
		to_dpu_encoder_phys_cmd(phys_enc);

	if (!phys_enc->hw_pp) {
		DPU_ERROR("invalid encoder\n");
		return;
	}
	DRM_DEBUG_KMS("id:%u pp:%d state:%d\n", DRMID(phys_enc->parent),
		      phys_enc->hw_pp->idx - PINGPONG_0,
		      phys_enc->enable_state);

	if (phys_enc->enable_state == DPU_ENC_DISABLED) {
		DPU_ERROR_CMDENC(cmd_enc, "already disabled\n");
		return;
	}

	if (phys_enc->hw_pp->ops.enable_tearcheck)
		phys_enc->hw_pp->ops.enable_tearcheck(phys_enc->hw_pp, false);
	phys_enc->enable_state = DPU_ENC_DISABLED;
}

static void dpu_encoder_phys_cmd_destroy(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_cmd *cmd_enc =
		to_dpu_encoder_phys_cmd(phys_enc);

	kfree(cmd_enc);
}

static void dpu_encoder_phys_cmd_prepare_for_kickoff(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_cmd *cmd_enc =
			to_dpu_encoder_phys_cmd(phys_enc);
	int ret;

	if (!phys_enc->hw_pp) {
		DPU_ERROR("invalid encoder\n");
		return;
	}
	DRM_DEBUG_KMS("id:%u pp:%d pending_cnt:%d\n", DRMID(phys_enc->parent),
		      phys_enc->hw_pp->idx - PINGPONG_0,
		      atomic_read(&phys_enc->pending_kickoff_cnt));

	/*
	 * Mark kickoff request as outstanding. If there are more than one,
	 * outstanding, then we have to wait for the previous one to complete
	 */
	ret = _dpu_encoder_phys_cmd_wait_for_idle(phys_enc);
	if (ret) {
		/* force pending_kickoff_cnt 0 to discard failed kickoff */
		atomic_set(&phys_enc->pending_kickoff_cnt, 0);
		DRM_ERROR("failed wait_for_idle: id:%u ret:%d pp:%d\n",
			  DRMID(phys_enc->parent), ret,
			  phys_enc->hw_pp->idx - PINGPONG_0);
	}

	DPU_DEBUG_CMDENC(cmd_enc, "pp:%d pending_cnt %d\n",
			phys_enc->hw_pp->idx - PINGPONG_0,
			atomic_read(&phys_enc->pending_kickoff_cnt));
}

static bool dpu_encoder_phys_cmd_is_ongoing_pptx(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_pp_vsync_info info;

	if (!phys_enc)
		return false;

	phys_enc->hw_pp->ops.get_vsync_info(phys_enc->hw_pp, &info);
	if (info.wr_ptr_line_count > 0 &&
	    info.wr_ptr_line_count < phys_enc->cached_mode.vdisplay)
		return true;

	return false;
}

static void dpu_encoder_phys_cmd_prepare_commit(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_cmd *cmd_enc =
		to_dpu_encoder_phys_cmd(phys_enc);
	int trial = 0;

	if (!phys_enc)
		return;
	if (!phys_enc->hw_pp)
		return;
	if (!dpu_encoder_phys_cmd_is_master(phys_enc))
		return;

	/* If autorefresh is already disabled, we have nothing to do */
	if (!phys_enc->hw_pp->ops.get_autorefresh(phys_enc->hw_pp, NULL))
		return;

	/*
	 * If autorefresh is enabled, disable it and make sure it is safe to
	 * proceed with current frame commit/push. Sequence fallowed is,
	 * 1. Disable TE
	 * 2. Disable autorefresh config
	 * 4. Poll for frame transfer ongoing to be false
	 * 5. Enable TE back
	 */
	_dpu_encoder_phys_cmd_connect_te(phys_enc, false);
	phys_enc->hw_pp->ops.setup_autorefresh(phys_enc->hw_pp, 0, false);

	do {
		udelay(DPU_ENC_MAX_POLL_TIMEOUT_US);
		if ((trial * DPU_ENC_MAX_POLL_TIMEOUT_US)
				> (KICKOFF_TIMEOUT_MS * USEC_PER_MSEC)) {
			DPU_ERROR_CMDENC(cmd_enc,
					"disable autorefresh failed\n");
			break;
		}

		trial++;
	} while (dpu_encoder_phys_cmd_is_ongoing_pptx(phys_enc));

	_dpu_encoder_phys_cmd_connect_te(phys_enc, true);

	DPU_DEBUG_CMDENC(to_dpu_encoder_phys_cmd(phys_enc),
			 "disabled autorefresh\n");
}

static int _dpu_encoder_phys_cmd_wait_for_ctl_start(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_cmd *cmd_enc =
			to_dpu_encoder_phys_cmd(phys_enc);
	struct dpu_encoder_wait_info wait_info;
	int ret;

	wait_info.wq = &phys_enc->pending_kickoff_wq;
	wait_info.atomic_cnt = &phys_enc->pending_ctlstart_cnt;
	wait_info.timeout_ms = KICKOFF_TIMEOUT_MS;

	ret = dpu_encoder_helper_wait_for_irq(phys_enc, INTR_IDX_CTL_START,
			&wait_info);
	if (ret == -ETIMEDOUT) {
		DPU_ERROR_CMDENC(cmd_enc, "ctl start interrupt wait failed\n");
		ret = -EINVAL;
	} else if (!ret)
		ret = 0;

	return ret;
}

static int dpu_encoder_phys_cmd_wait_for_tx_complete(
		struct dpu_encoder_phys *phys_enc)
{
	int rc;

	rc = _dpu_encoder_phys_cmd_wait_for_idle(phys_enc);
	if (rc) {
		DRM_ERROR("failed wait_for_idle: id:%u ret:%d intf:%d\n",
			  DRMID(phys_enc->parent), rc,
			  phys_enc->intf_idx - INTF_0);
	}

	return rc;
}

static int dpu_encoder_phys_cmd_wait_for_commit_done(
		struct dpu_encoder_phys *phys_enc)
{
	/* only required for master controller */
	if (!dpu_encoder_phys_cmd_is_master(phys_enc))
		return 0;

	return _dpu_encoder_phys_cmd_wait_for_ctl_start(phys_enc);
}

static int dpu_encoder_phys_cmd_wait_for_vblank(
		struct dpu_encoder_phys *phys_enc)
{
	int rc = 0;
	struct dpu_encoder_phys_cmd *cmd_enc;
	struct dpu_encoder_wait_info wait_info;

	cmd_enc = to_dpu_encoder_phys_cmd(phys_enc);

	/* only required for master controller */
	if (!dpu_encoder_phys_cmd_is_master(phys_enc))
		return rc;

	wait_info.wq = &cmd_enc->pending_vblank_wq;
	wait_info.atomic_cnt = &cmd_enc->pending_vblank_cnt;
	wait_info.timeout_ms = KICKOFF_TIMEOUT_MS;

	atomic_inc(&cmd_enc->pending_vblank_cnt);

	rc = dpu_encoder_helper_wait_for_irq(phys_enc, INTR_IDX_RDPTR,
			&wait_info);

	return rc;
}

static void dpu_encoder_phys_cmd_handle_post_kickoff(
		struct dpu_encoder_phys *phys_enc)
{
	/**
	 * re-enable external TE, either for the first time after enabling
	 * or if disabled for Autorefresh
	 */
	_dpu_encoder_phys_cmd_connect_te(phys_enc, true);
}

static void dpu_encoder_phys_cmd_trigger_start(
		struct dpu_encoder_phys *phys_enc)
{
	dpu_encoder_helper_trigger_start(phys_enc);
}

static void dpu_encoder_phys_cmd_init_ops(
		struct dpu_encoder_phys_ops *ops)
{
	ops->prepare_commit = dpu_encoder_phys_cmd_prepare_commit;
	ops->is_master = dpu_encoder_phys_cmd_is_master;
	ops->atomic_mode_set = dpu_encoder_phys_cmd_atomic_mode_set;
	ops->enable = dpu_encoder_phys_cmd_enable;
	ops->disable = dpu_encoder_phys_cmd_disable;
	ops->destroy = dpu_encoder_phys_cmd_destroy;
	ops->control_vblank_irq = dpu_encoder_phys_cmd_control_vblank_irq;
	ops->wait_for_commit_done = dpu_encoder_phys_cmd_wait_for_commit_done;
	ops->prepare_for_kickoff = dpu_encoder_phys_cmd_prepare_for_kickoff;
	ops->wait_for_tx_complete = dpu_encoder_phys_cmd_wait_for_tx_complete;
	ops->wait_for_vblank = dpu_encoder_phys_cmd_wait_for_vblank;
	ops->trigger_start = dpu_encoder_phys_cmd_trigger_start;
	ops->needs_single_flush = dpu_encoder_phys_cmd_needs_single_flush;
	ops->irq_control = dpu_encoder_phys_cmd_irq_control;
	ops->restore = dpu_encoder_phys_cmd_enable_helper;
	ops->prepare_idle_pc = dpu_encoder_phys_cmd_prepare_idle_pc;
	ops->handle_post_kickoff = dpu_encoder_phys_cmd_handle_post_kickoff;
	ops->get_line_count = dpu_encoder_phys_cmd_get_line_count;
}

struct dpu_encoder_phys *dpu_encoder_phys_cmd_init(
		struct dpu_enc_phys_init_params *p)
{
	struct dpu_encoder_phys *phys_enc = NULL;
	struct dpu_encoder_phys_cmd *cmd_enc = NULL;
	struct dpu_encoder_irq *irq;
	int i, ret = 0;

	DPU_DEBUG("intf %d\n", p->intf_idx - INTF_0);

	cmd_enc = kzalloc(sizeof(*cmd_enc), GFP_KERNEL);
	if (!cmd_enc) {
		ret = -ENOMEM;
		DPU_ERROR("failed to allocate\n");
		return ERR_PTR(ret);
	}
	phys_enc = &cmd_enc->base;
	phys_enc->hw_mdptop = p->dpu_kms->hw_mdp;
	phys_enc->intf_idx = p->intf_idx;

	dpu_encoder_phys_cmd_init_ops(&phys_enc->ops);
	phys_enc->parent = p->parent;
	phys_enc->parent_ops = p->parent_ops;
	phys_enc->dpu_kms = p->dpu_kms;
	phys_enc->split_role = p->split_role;
	phys_enc->intf_mode = INTF_MODE_CMD;
	phys_enc->enc_spinlock = p->enc_spinlock;
	cmd_enc->stream_sel = 0;
	phys_enc->enable_state = DPU_ENC_DISABLED;
	for (i = 0; i < INTR_IDX_MAX; i++) {
		irq = &phys_enc->irq[i];
		INIT_LIST_HEAD(&irq->cb.list);
		irq->irq_idx = -EINVAL;
		irq->cb.arg = phys_enc;
	}

	irq = &phys_enc->irq[INTR_IDX_CTL_START];
	irq->name = "ctl_start";
	irq->intr_idx = INTR_IDX_CTL_START;
	irq->cb.func = dpu_encoder_phys_cmd_ctl_start_irq;

	irq = &phys_enc->irq[INTR_IDX_PINGPONG];
	irq->name = "pp_done";
	irq->intr_idx = INTR_IDX_PINGPONG;
	irq->cb.func = dpu_encoder_phys_cmd_pp_tx_done_irq;

	irq = &phys_enc->irq[INTR_IDX_RDPTR];
	irq->name = "pp_rd_ptr";
	irq->intr_idx = INTR_IDX_RDPTR;
	irq->cb.func = dpu_encoder_phys_cmd_pp_rd_ptr_irq;

	irq = &phys_enc->irq[INTR_IDX_UNDERRUN];
	irq->name = "underrun";
	irq->intr_idx = INTR_IDX_UNDERRUN;
	irq->cb.func = dpu_encoder_phys_cmd_underrun_irq;

	atomic_set(&phys_enc->vblank_refcount, 0);
	atomic_set(&phys_enc->pending_kickoff_cnt, 0);
	atomic_set(&phys_enc->pending_ctlstart_cnt, 0);
	atomic_set(&cmd_enc->pending_vblank_cnt, 0);
	init_waitqueue_head(&phys_enc->pending_kickoff_wq);
	init_waitqueue_head(&cmd_enc->pending_vblank_wq);

	DPU_DEBUG_CMDENC(cmd_enc, "created\n");

	return phys_enc;
}
