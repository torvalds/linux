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
		(e) ? (e)->base.hw_intf->idx - INTF_0 : -1, ##__VA_ARGS__)

#define DPU_ERROR_CMDENC(e, fmt, ...) DPU_ERROR("enc%d intf%d " fmt, \
		(e) && (e)->base.parent ? \
		(e)->base.parent->base.id : -1, \
		(e) ? (e)->base.hw_intf->idx - INTF_0 : -1, ##__VA_ARGS__)

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

static void dpu_encoder_phys_cmd_enable_te(struct dpu_encoder_phys *phys_enc);

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
	struct dpu_hw_intf_cmd_mode_cfg cmd_mode_cfg = {};

	ctl = phys_enc->hw_ctl;
	if (!ctl->ops.setup_intf_cfg)
		return;

	intf_cfg.intf = phys_enc->hw_intf->idx;
	intf_cfg.intf_mode_sel = DPU_CTL_MODE_SEL_CMD;
	intf_cfg.stream_sel = cmd_enc->stream_sel;
	intf_cfg.mode_3d = dpu_encoder_helper_get_3d_blend_mode(phys_enc);
	intf_cfg.dsc = dpu_encoder_helper_get_dsc(phys_enc);
	ctl->ops.setup_intf_cfg(ctl, &intf_cfg);

	/* setup which pp blk will connect to this intf */
	if (test_bit(DPU_CTL_ACTIVE_CFG, &ctl->caps->features) && phys_enc->hw_intf->ops.bind_pingpong_blk)
		phys_enc->hw_intf->ops.bind_pingpong_blk(
				phys_enc->hw_intf,
				phys_enc->hw_pp->idx);

	if (intf_cfg.dsc != 0)
		cmd_mode_cfg.data_compress = true;

	cmd_mode_cfg.wide_bus_en = dpu_encoder_is_widebus_enabled(phys_enc->parent);

	if (phys_enc->hw_intf->ops.program_intf_cmd_cfg)
		phys_enc->hw_intf->ops.program_intf_cmd_cfg(phys_enc->hw_intf, &cmd_mode_cfg);
}

static void dpu_encoder_phys_cmd_pp_tx_done_irq(void *arg)
{
	struct dpu_encoder_phys *phys_enc = arg;
	unsigned long lock_flags;
	int new_cnt;
	u32 event = DPU_ENCODER_FRAME_EVENT_DONE;

	if (!phys_enc->hw_pp)
		return;

	DPU_ATRACE_BEGIN("pp_done_irq");
	/* notify all synchronous clients first, then asynchronous clients */
	dpu_encoder_frame_done_callback(phys_enc->parent, phys_enc, event);

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

static void dpu_encoder_phys_cmd_te_rd_ptr_irq(void *arg)
{
	struct dpu_encoder_phys *phys_enc = arg;
	struct dpu_encoder_phys_cmd *cmd_enc;

	DPU_ATRACE_BEGIN("rd_ptr_irq");
	cmd_enc = to_dpu_encoder_phys_cmd(phys_enc);

	dpu_encoder_vblank_callback(phys_enc->parent, phys_enc);

	atomic_add_unless(&cmd_enc->pending_vblank_cnt, -1, 0);
	wake_up_all(&cmd_enc->pending_vblank_wq);
	DPU_ATRACE_END("rd_ptr_irq");
}

static void dpu_encoder_phys_cmd_ctl_start_irq(void *arg)
{
	struct dpu_encoder_phys *phys_enc = arg;

	DPU_ATRACE_BEGIN("ctl_start_irq");

	atomic_add_unless(&phys_enc->pending_ctlstart_cnt, -1, 0);

	/* Signal any waiting ctl start interrupt */
	wake_up_all(&phys_enc->pending_kickoff_wq);
	DPU_ATRACE_END("ctl_start_irq");
}

static void dpu_encoder_phys_cmd_underrun_irq(void *arg)
{
	struct dpu_encoder_phys *phys_enc = arg;

	dpu_encoder_underrun_callback(phys_enc->parent, phys_enc);
}

static void dpu_encoder_phys_cmd_atomic_mode_set(
		struct dpu_encoder_phys *phys_enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state)
{
	phys_enc->irq[INTR_IDX_CTL_START] = phys_enc->hw_ctl->caps->intr_start;

	phys_enc->irq[INTR_IDX_PINGPONG] = phys_enc->hw_pp->caps->intr_done;

	if (phys_enc->has_intf_te)
		phys_enc->irq[INTR_IDX_RDPTR] = phys_enc->hw_intf->cap->intr_tear_rd_ptr;
	else
		phys_enc->irq[INTR_IDX_RDPTR] = phys_enc->hw_pp->caps->intr_rdptr;

	phys_enc->irq[INTR_IDX_UNDERRUN] = phys_enc->hw_intf->cap->intr_underrun;
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
		dpu_core_irq_unregister_callback(phys_enc->dpu_kms,
				phys_enc->irq[INTR_IDX_RDPTR]);
	}

	atomic_add_unless(&phys_enc->pending_kickoff_cnt, -1, 0);

	/* request a ctl reset before the next kickoff */
	phys_enc->enable_state = DPU_ENC_ERR_NEEDS_HW_RESET;

	dpu_encoder_frame_done_callback(phys_enc->parent, phys_enc, frame_event);

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

	ret = dpu_encoder_helper_wait_for_irq(phys_enc,
			phys_enc->irq[INTR_IDX_PINGPONG],
			dpu_encoder_phys_cmd_pp_tx_done_irq,
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
		ret = dpu_core_irq_register_callback(phys_enc->dpu_kms,
				phys_enc->irq[INTR_IDX_RDPTR],
				dpu_encoder_phys_cmd_te_rd_ptr_irq,
				phys_enc);
	else if (!enable && atomic_dec_return(&phys_enc->vblank_refcount) == 0)
		ret = dpu_core_irq_unregister_callback(phys_enc->dpu_kms,
				phys_enc->irq[INTR_IDX_RDPTR]);

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
		dpu_core_irq_register_callback(phys_enc->dpu_kms,
				phys_enc->irq[INTR_IDX_PINGPONG],
				dpu_encoder_phys_cmd_pp_tx_done_irq,
				phys_enc);
		dpu_core_irq_register_callback(phys_enc->dpu_kms,
				phys_enc->irq[INTR_IDX_UNDERRUN],
				dpu_encoder_phys_cmd_underrun_irq,
				phys_enc);
		dpu_encoder_phys_cmd_control_vblank_irq(phys_enc, true);

		if (dpu_encoder_phys_cmd_is_master(phys_enc))
			dpu_core_irq_register_callback(phys_enc->dpu_kms,
					phys_enc->irq[INTR_IDX_CTL_START],
					dpu_encoder_phys_cmd_ctl_start_irq,
					phys_enc);
	} else {
		if (dpu_encoder_phys_cmd_is_master(phys_enc))
			dpu_core_irq_unregister_callback(phys_enc->dpu_kms,
					phys_enc->irq[INTR_IDX_CTL_START]);

		dpu_core_irq_unregister_callback(phys_enc->dpu_kms,
				phys_enc->irq[INTR_IDX_UNDERRUN]);
		dpu_encoder_phys_cmd_control_vblank_irq(phys_enc, false);
		dpu_core_irq_unregister_callback(phys_enc->dpu_kms,
				phys_enc->irq[INTR_IDX_PINGPONG]);
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
	unsigned long vsync_hz;
	struct dpu_kms *dpu_kms;

	/*
	 * TODO: if/when resource allocation is refactored, move this to a
	 * place where the driver can actually return an error.
	 */
	if (!phys_enc->has_intf_te &&
	    (!phys_enc->hw_pp ||
	     !phys_enc->hw_pp->ops.enable_tearcheck)) {
		DPU_DEBUG_CMDENC(cmd_enc, "tearcheck not supported\n");
		return;
	}

	DPU_DEBUG_CMDENC(cmd_enc, "intf %d pp %d\n",
			 phys_enc->hw_intf ? phys_enc->hw_intf->idx - INTF_0 : -1,
			 phys_enc->hw_pp ? phys_enc->hw_pp->idx - PINGPONG_0 : -1);

	mode = &phys_enc->cached_mode;

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
	if (!vsync_hz) {
		DPU_DEBUG_CMDENC(cmd_enc, "invalid - no vsync clock\n");
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
		"tc vsync_clk_speed_hz %lu vtotal %u vrefresh %u\n",
		vsync_hz, mode->vtotal, drm_mode_vrefresh(mode));
	DPU_DEBUG_CMDENC(cmd_enc,
		"tc enable %u start_pos %u rd_ptr_irq %u\n",
		tc_enable, tc_cfg.start_pos, tc_cfg.rd_ptr_irq);
	DPU_DEBUG_CMDENC(cmd_enc,
		"tc hw_vsync_mode %u vsync_count %u vsync_init_val %u\n",
		tc_cfg.hw_vsync_mode, tc_cfg.vsync_count,
		tc_cfg.vsync_init_val);
	DPU_DEBUG_CMDENC(cmd_enc,
		"tc cfgheight %u thresh_start %u thresh_cont %u\n",
		tc_cfg.sync_cfg_height, tc_cfg.sync_threshold_start,
		tc_cfg.sync_threshold_continue);

	if (phys_enc->has_intf_te)
		phys_enc->hw_intf->ops.enable_tearcheck(phys_enc->hw_intf, &tc_cfg);
	else
		phys_enc->hw_pp->ops.enable_tearcheck(phys_enc->hw_pp, &tc_cfg);
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

	dpu_encoder_helper_split_config(phys_enc, phys_enc->hw_intf->idx);

	_dpu_encoder_phys_cmd_pingpong_config(phys_enc);

	if (!dpu_encoder_phys_cmd_is_master(phys_enc))
		return;

	ctl = phys_enc->hw_ctl;
	ctl->ops.update_pending_flush_intf(ctl, phys_enc->hw_intf->idx);
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
	if (phys_enc->has_intf_te) {
		if (!phys_enc->hw_intf || !phys_enc->hw_intf->ops.connect_external_te)
			return;

		trace_dpu_enc_phys_cmd_connect_te(DRMID(phys_enc->parent), enable);
		phys_enc->hw_intf->ops.connect_external_te(phys_enc->hw_intf, enable);
	} else {
		if (!phys_enc->hw_pp || !phys_enc->hw_pp->ops.connect_external_te)
			return;

		trace_dpu_enc_phys_cmd_connect_te(DRMID(phys_enc->parent), enable);
		phys_enc->hw_pp->ops.connect_external_te(phys_enc->hw_pp, enable);
	}
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
	struct dpu_hw_intf *hw_intf;

	if (!dpu_encoder_phys_cmd_is_master(phys_enc))
		return -EINVAL;

	if (phys_enc->has_intf_te) {
		hw_intf = phys_enc->hw_intf;
		if (!hw_intf || !hw_intf->ops.get_line_count)
			return -EINVAL;
		return hw_intf->ops.get_line_count(hw_intf);
	}

	hw_pp = phys_enc->hw_pp;
	if (!hw_pp || !hw_pp->ops.get_line_count)
		return -EINVAL;
	return hw_pp->ops.get_line_count(hw_pp);
}

static void dpu_encoder_phys_cmd_disable(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_cmd *cmd_enc =
		to_dpu_encoder_phys_cmd(phys_enc);
	struct dpu_hw_ctl *ctl;

	if (phys_enc->enable_state == DPU_ENC_DISABLED) {
		DPU_ERROR_CMDENC(cmd_enc, "already disabled\n");
		return;
	}

	if (phys_enc->has_intf_te) {
		DRM_DEBUG_KMS("id:%u intf:%d state:%d\n", DRMID(phys_enc->parent),
			      phys_enc->hw_intf->idx - INTF_0,
			      phys_enc->enable_state);

		if (phys_enc->hw_intf->ops.disable_tearcheck)
			phys_enc->hw_intf->ops.disable_tearcheck(phys_enc->hw_intf);
	} else {
		if (!phys_enc->hw_pp) {
			DPU_ERROR("invalid encoder\n");
			return;
		}

		DRM_DEBUG_KMS("id:%u pp:%d state:%d\n", DRMID(phys_enc->parent),
			      phys_enc->hw_pp->idx - PINGPONG_0,
			      phys_enc->enable_state);

		if (phys_enc->hw_pp->ops.disable_tearcheck)
			phys_enc->hw_pp->ops.disable_tearcheck(phys_enc->hw_pp);
	}

	if (phys_enc->hw_intf->ops.bind_pingpong_blk) {
		phys_enc->hw_intf->ops.bind_pingpong_blk(
				phys_enc->hw_intf,
				PINGPONG_NONE);

		ctl = phys_enc->hw_ctl;
		ctl->ops.update_pending_flush_intf(ctl, phys_enc->hw_intf->idx);
	}

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

	dpu_encoder_phys_cmd_enable_te(phys_enc);

	DPU_DEBUG_CMDENC(cmd_enc, "pp:%d pending_cnt %d\n",
			phys_enc->hw_pp->idx - PINGPONG_0,
			atomic_read(&phys_enc->pending_kickoff_cnt));
}

static void dpu_encoder_phys_cmd_enable_te(struct dpu_encoder_phys *phys_enc)
{
	if (!phys_enc)
		return;
	if (!dpu_encoder_phys_cmd_is_master(phys_enc))
		return;

	if (phys_enc->has_intf_te) {
		if (!phys_enc->hw_intf->ops.disable_autorefresh)
			return;

		phys_enc->hw_intf->ops.disable_autorefresh(
				phys_enc->hw_intf,
				DRMID(phys_enc->parent),
				phys_enc->cached_mode.vdisplay);
	} else {
		if (!phys_enc->hw_pp ||
		    !phys_enc->hw_pp->ops.disable_autorefresh)
			return;

		phys_enc->hw_pp->ops.disable_autorefresh(
				phys_enc->hw_pp,
				DRMID(phys_enc->parent),
				phys_enc->cached_mode.vdisplay);
	}
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

	ret = dpu_encoder_helper_wait_for_irq(phys_enc,
			phys_enc->irq[INTR_IDX_CTL_START],
			dpu_encoder_phys_cmd_ctl_start_irq,
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
			  phys_enc->hw_intf->idx - INTF_0);
	}

	return rc;
}

static int dpu_encoder_phys_cmd_wait_for_commit_done(
		struct dpu_encoder_phys *phys_enc)
{
	/* only required for master controller */
	if (!dpu_encoder_phys_cmd_is_master(phys_enc))
		return 0;

	if (phys_enc->hw_ctl->ops.is_started(phys_enc->hw_ctl))
		return dpu_encoder_phys_cmd_wait_for_tx_complete(phys_enc);

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

	rc = dpu_encoder_helper_wait_for_irq(phys_enc,
			phys_enc->irq[INTR_IDX_RDPTR],
			dpu_encoder_phys_cmd_te_rd_ptr_irq,
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

	DPU_DEBUG("intf\n");

	cmd_enc = kzalloc(sizeof(*cmd_enc), GFP_KERNEL);
	if (!cmd_enc) {
		DPU_ERROR("failed to allocate\n");
		return ERR_PTR(-ENOMEM);
	}
	phys_enc = &cmd_enc->base;

	dpu_encoder_phys_init(phys_enc, p);

	dpu_encoder_phys_cmd_init_ops(&phys_enc->ops);
	phys_enc->intf_mode = INTF_MODE_CMD;
	cmd_enc->stream_sel = 0;

	if (!phys_enc->hw_intf) {
		DPU_ERROR_CMDENC(cmd_enc, "no INTF provided\n");
		return ERR_PTR(-EINVAL);
	}

	/* DPU before 5.0 use PINGPONG for TE handling */
	if (phys_enc->dpu_kms->catalog->mdss_ver->core_major_ver >= 5)
		phys_enc->has_intf_te = true;

	if (phys_enc->has_intf_te && !phys_enc->hw_intf->ops.enable_tearcheck) {
		DPU_ERROR_CMDENC(cmd_enc, "tearcheck not supported\n");
		return ERR_PTR(-EINVAL);
	}

	atomic_set(&cmd_enc->pending_vblank_cnt, 0);
	init_waitqueue_head(&cmd_enc->pending_vblank_wq);

	DPU_DEBUG_CMDENC(cmd_enc, "created\n");

	return phys_enc;
}
