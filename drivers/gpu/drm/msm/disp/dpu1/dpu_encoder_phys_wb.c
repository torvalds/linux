// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/debugfs.h>

#include "dpu_encoder_phys.h"
#include "dpu_formats.h"
#include "dpu_hw_top.h"
#include "dpu_hw_wb.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_blk.h"
#include "dpu_hw_merge3d.h"
#include "dpu_hw_interrupts.h"
#include "dpu_core_irq.h"
#include "dpu_vbif.h"
#include "dpu_crtc.h"
#include "disp/msm_disp_snapshot.h"

#define DEFAULT_MAX_WRITEBACK_WIDTH 2048

#define to_dpu_encoder_phys_wb(x) \
	container_of(x, struct dpu_encoder_phys_wb, base)

/**
 * dpu_encoder_phys_wb_is_master - report wb always as master encoder
 */
static bool dpu_encoder_phys_wb_is_master(struct dpu_encoder_phys *phys_enc)
{
	/* there is only one physical enc for dpu_writeback */
	return true;
}

/**
 * dpu_encoder_phys_wb_set_ot_limit - set OT limit for writeback interface
 * @phys_enc:	Pointer to physical encoder
 */
static void dpu_encoder_phys_wb_set_ot_limit(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_wb *hw_wb = phys_enc->hw_wb;
	struct dpu_vbif_set_ot_params ot_params;

	memset(&ot_params, 0, sizeof(ot_params));
	ot_params.xin_id = hw_wb->caps->xin_id;
	ot_params.num = hw_wb->idx - WB_0;
	ot_params.width = phys_enc->cached_mode.hdisplay;
	ot_params.height = phys_enc->cached_mode.vdisplay;
	ot_params.is_wfd = true;
	ot_params.frame_rate = drm_mode_vrefresh(&phys_enc->cached_mode);
	ot_params.vbif_idx = hw_wb->caps->vbif_idx;
	ot_params.clk_ctrl = hw_wb->caps->clk_ctrl;
	ot_params.rd = false;

	dpu_vbif_set_ot_limit(phys_enc->dpu_kms, &ot_params);
}

/**
 * dpu_encoder_phys_wb_set_qos_remap - set QoS remapper for writeback
 * @phys_enc:	Pointer to physical encoder
 */
static void dpu_encoder_phys_wb_set_qos_remap(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_wb *hw_wb;
	struct dpu_vbif_set_qos_params qos_params;

	if (!phys_enc || !phys_enc->parent || !phys_enc->parent->crtc) {
		DPU_ERROR("invalid arguments\n");
		return;
	}

	if (!phys_enc->hw_wb || !phys_enc->hw_wb->caps) {
		DPU_ERROR("invalid writeback hardware\n");
		return;
	}

	hw_wb = phys_enc->hw_wb;

	memset(&qos_params, 0, sizeof(qos_params));
	qos_params.vbif_idx = hw_wb->caps->vbif_idx;
	qos_params.xin_id = hw_wb->caps->xin_id;
	qos_params.clk_ctrl = hw_wb->caps->clk_ctrl;
	qos_params.num = hw_wb->idx - WB_0;
	qos_params.is_rt = false;

	DPU_DEBUG("[qos_remap] wb:%d vbif:%d xin:%d is_rt:%d\n",
			qos_params.num,
			qos_params.vbif_idx,
			qos_params.xin_id, qos_params.is_rt);

	dpu_vbif_set_qos_remap(phys_enc->dpu_kms, &qos_params);
}

/**
 * dpu_encoder_phys_wb_set_qos - set QoS/danger/safe LUTs for writeback
 * @phys_enc:	Pointer to physical encoder
 */
static void dpu_encoder_phys_wb_set_qos(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_wb *hw_wb;
	struct dpu_hw_wb_qos_cfg qos_cfg;
	struct dpu_mdss_cfg *catalog;
	struct dpu_qos_lut_tbl *qos_lut_tb;

	if (!phys_enc || !phys_enc->dpu_kms || !phys_enc->dpu_kms->catalog) {
		DPU_ERROR("invalid parameter(s)\n");
		return;
	}

	catalog = phys_enc->dpu_kms->catalog;

	hw_wb = phys_enc->hw_wb;

	memset(&qos_cfg, 0, sizeof(struct dpu_hw_wb_qos_cfg));
	qos_cfg.danger_safe_en = true;
	qos_cfg.danger_lut =
		catalog->perf.danger_lut_tbl[DPU_QOS_LUT_USAGE_NRT];

	qos_cfg.safe_lut = catalog->perf.safe_lut_tbl[DPU_QOS_LUT_USAGE_NRT];

	qos_lut_tb = &catalog->perf.qos_lut_tbl[DPU_QOS_LUT_USAGE_NRT];
	qos_cfg.creq_lut = _dpu_hw_get_qos_lut(qos_lut_tb, 0);

	if (hw_wb->ops.setup_qos_lut)
		hw_wb->ops.setup_qos_lut(hw_wb, &qos_cfg);
}

/**
 * dpu_encoder_phys_wb_setup_fb - setup output framebuffer
 * @phys_enc:	Pointer to physical encoder
 * @fb:		Pointer to output framebuffer
 * @wb_roi:	Pointer to output region of interest
 */
static void dpu_encoder_phys_wb_setup_fb(struct dpu_encoder_phys *phys_enc,
		struct drm_framebuffer *fb)
{
	struct dpu_encoder_phys_wb *wb_enc = to_dpu_encoder_phys_wb(phys_enc);
	struct dpu_hw_wb *hw_wb;
	struct dpu_hw_wb_cfg *wb_cfg;
	struct dpu_hw_cdp_cfg cdp_cfg;

	if (!phys_enc || !phys_enc->dpu_kms || !phys_enc->dpu_kms->catalog) {
		DPU_ERROR("invalid encoder\n");
		return;
	}

	hw_wb = phys_enc->hw_wb;
	wb_cfg = &wb_enc->wb_cfg;

	wb_cfg->intf_mode = phys_enc->intf_mode;
	wb_cfg->roi.x1 = 0;
	wb_cfg->roi.x2 = phys_enc->cached_mode.hdisplay;
	wb_cfg->roi.y1 = 0;
	wb_cfg->roi.y2 = phys_enc->cached_mode.vdisplay;

	if (hw_wb->ops.setup_roi)
		hw_wb->ops.setup_roi(hw_wb, wb_cfg);

	if (hw_wb->ops.setup_outformat)
		hw_wb->ops.setup_outformat(hw_wb, wb_cfg);

	if (hw_wb->ops.setup_cdp) {
		memset(&cdp_cfg, 0, sizeof(struct dpu_hw_cdp_cfg));

		cdp_cfg.enable = phys_enc->dpu_kms->catalog->perf.cdp_cfg
				[DPU_PERF_CDP_USAGE_NRT].wr_enable;
		cdp_cfg.ubwc_meta_enable =
				DPU_FORMAT_IS_UBWC(wb_cfg->dest.format);
		cdp_cfg.tile_amortize_enable =
				DPU_FORMAT_IS_UBWC(wb_cfg->dest.format) ||
				DPU_FORMAT_IS_TILE(wb_cfg->dest.format);
		cdp_cfg.preload_ahead = DPU_WB_CDP_PRELOAD_AHEAD_64;

		hw_wb->ops.setup_cdp(hw_wb, &cdp_cfg);
	}

	if (hw_wb->ops.setup_outaddress)
		hw_wb->ops.setup_outaddress(hw_wb, wb_cfg);
}

/**
 * dpu_encoder_phys_wb_setup_cdp - setup chroma down prefetch block
 * @phys_enc:Pointer to physical encoder
 */
static void dpu_encoder_phys_wb_setup_cdp(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_wb *hw_wb;
	struct dpu_hw_ctl *ctl;

	if (!phys_enc) {
		DPU_ERROR("invalid encoder\n");
		return;
	}

	hw_wb = phys_enc->hw_wb;
	ctl = phys_enc->hw_ctl;

	if (test_bit(DPU_CTL_ACTIVE_CFG, &ctl->caps->features) &&
		(phys_enc->hw_ctl &&
		 phys_enc->hw_ctl->ops.setup_intf_cfg)) {
		struct dpu_hw_intf_cfg intf_cfg = {0};
		struct dpu_hw_pingpong *hw_pp = phys_enc->hw_pp;
		enum dpu_3d_blend_mode mode_3d;

		mode_3d = dpu_encoder_helper_get_3d_blend_mode(phys_enc);

		intf_cfg.intf = DPU_NONE;
		intf_cfg.wb = hw_wb->idx;

		if (mode_3d && hw_pp && hw_pp->merge_3d)
			intf_cfg.merge_3d = hw_pp->merge_3d->idx;

		if (phys_enc->hw_pp->merge_3d && phys_enc->hw_pp->merge_3d->ops.setup_3d_mode)
			phys_enc->hw_pp->merge_3d->ops.setup_3d_mode(phys_enc->hw_pp->merge_3d,
					mode_3d);

		/* setup which pp blk will connect to this wb */
		if (hw_pp && phys_enc->hw_wb->ops.bind_pingpong_blk)
			phys_enc->hw_wb->ops.bind_pingpong_blk(phys_enc->hw_wb, true,
					phys_enc->hw_pp->idx);

		phys_enc->hw_ctl->ops.setup_intf_cfg(phys_enc->hw_ctl, &intf_cfg);
	} else if (phys_enc->hw_ctl && phys_enc->hw_ctl->ops.setup_intf_cfg) {
		struct dpu_hw_intf_cfg intf_cfg = {0};

		intf_cfg.intf = DPU_NONE;
		intf_cfg.wb = hw_wb->idx;
		intf_cfg.mode_3d =
			dpu_encoder_helper_get_3d_blend_mode(phys_enc);
		phys_enc->hw_ctl->ops.setup_intf_cfg(phys_enc->hw_ctl, &intf_cfg);
	}
}

/**
 * dpu_encoder_phys_wb_atomic_check - verify and fixup given atomic states
 * @phys_enc:	Pointer to physical encoder
 * @crtc_state:	Pointer to CRTC atomic state
 * @conn_state:	Pointer to connector atomic state
 */
static int dpu_encoder_phys_wb_atomic_check(
		struct dpu_encoder_phys *phys_enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state)
{
	struct drm_framebuffer *fb;
	const struct drm_display_mode *mode = &crtc_state->mode;

	DPU_DEBUG("[atomic_check:%d, \"%s\",%d,%d]\n",
			phys_enc->wb_idx, mode->name, mode->hdisplay, mode->vdisplay);

	if (!conn_state->writeback_job || !conn_state->writeback_job->fb)
		return 0;

	fb = conn_state->writeback_job->fb;

	if (!conn_state || !conn_state->connector) {
		DPU_ERROR("invalid connector state\n");
		return -EINVAL;
	} else if (conn_state->connector->status !=
			connector_status_connected) {
		DPU_ERROR("connector not connected %d\n",
				conn_state->connector->status);
		return -EINVAL;
	}

	DPU_DEBUG("[fb_id:%u][fb:%u,%u]\n", fb->base.id,
			fb->width, fb->height);

	if (fb->width != mode->hdisplay) {
		DPU_ERROR("invalid fb w=%d, mode w=%d\n", fb->width,
				mode->hdisplay);
		return -EINVAL;
	} else if (fb->height != mode->vdisplay) {
		DPU_ERROR("invalid fb h=%d, mode h=%d\n", fb->height,
				  mode->vdisplay);
		return -EINVAL;
	} else if (fb->width > DEFAULT_MAX_WRITEBACK_WIDTH) {
		DPU_ERROR("invalid fb w=%d, maxlinewidth=%u\n",
				  fb->width, DEFAULT_MAX_WRITEBACK_WIDTH);
		return -EINVAL;
	}

	return 0;
}


/**
 * _dpu_encoder_phys_wb_update_flush - flush hardware update
 * @phys_enc:	Pointer to physical encoder
 */
static void _dpu_encoder_phys_wb_update_flush(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_wb *hw_wb;
	struct dpu_hw_ctl *hw_ctl;
	struct dpu_hw_pingpong *hw_pp;
	u32 pending_flush = 0;

	if (!phys_enc)
		return;

	hw_wb = phys_enc->hw_wb;
	hw_pp = phys_enc->hw_pp;
	hw_ctl = phys_enc->hw_ctl;

	DPU_DEBUG("[wb:%d]\n", hw_wb->idx - WB_0);

	if (!hw_ctl) {
		DPU_DEBUG("[wb:%d] no ctl assigned\n", hw_wb->idx - WB_0);
		return;
	}

	if (hw_ctl->ops.update_pending_flush_wb)
		hw_ctl->ops.update_pending_flush_wb(hw_ctl, hw_wb->idx);

	if (hw_ctl->ops.update_pending_flush_merge_3d && hw_pp && hw_pp->merge_3d)
		hw_ctl->ops.update_pending_flush_merge_3d(hw_ctl,
				hw_pp->merge_3d->idx);

	if (hw_ctl->ops.get_pending_flush)
		pending_flush = hw_ctl->ops.get_pending_flush(hw_ctl);

	DPU_DEBUG("Pending flush mask for CTL_%d is 0x%x, WB %d\n",
			hw_ctl->idx - CTL_0, pending_flush,
			hw_wb->idx - WB_0);
}

/**
 * dpu_encoder_phys_wb_setup - setup writeback encoder
 * @phys_enc:	Pointer to physical encoder
 */
static void dpu_encoder_phys_wb_setup(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_wb *hw_wb = phys_enc->hw_wb;
	struct drm_display_mode mode = phys_enc->cached_mode;
	struct drm_framebuffer *fb = NULL;

	DPU_DEBUG("[mode_set:%d, \"%s\",%d,%d]\n",
			hw_wb->idx - WB_0, mode.name,
			mode.hdisplay, mode.vdisplay);

	dpu_encoder_phys_wb_set_ot_limit(phys_enc);

	dpu_encoder_phys_wb_set_qos_remap(phys_enc);

	dpu_encoder_phys_wb_set_qos(phys_enc);

	dpu_encoder_phys_wb_setup_fb(phys_enc, fb);

	dpu_encoder_phys_wb_setup_cdp(phys_enc);

}

static void _dpu_encoder_phys_wb_frame_done_helper(void *arg)
{
	struct dpu_encoder_phys *phys_enc = arg;
	struct dpu_encoder_phys_wb *wb_enc = to_dpu_encoder_phys_wb(phys_enc);

	struct dpu_hw_wb *hw_wb = phys_enc->hw_wb;
	unsigned long lock_flags;
	u32 event = DPU_ENCODER_FRAME_EVENT_DONE;

	DPU_DEBUG("[wb:%d]\n", hw_wb->idx - WB_0);

	if (phys_enc->parent_ops->handle_frame_done)
		phys_enc->parent_ops->handle_frame_done(phys_enc->parent,
				phys_enc, event);

	if (phys_enc->parent_ops->handle_vblank_virt)
		phys_enc->parent_ops->handle_vblank_virt(phys_enc->parent,
				phys_enc);

	spin_lock_irqsave(phys_enc->enc_spinlock, lock_flags);
	atomic_add_unless(&phys_enc->pending_kickoff_cnt, -1, 0);
	spin_unlock_irqrestore(phys_enc->enc_spinlock, lock_flags);

	if (wb_enc->wb_conn)
		drm_writeback_signal_completion(wb_enc->wb_conn, 0);

	/* Signal any waiting atomic commit thread */
	wake_up_all(&phys_enc->pending_kickoff_wq);
}

/**
 * dpu_encoder_phys_wb_done_irq - writeback interrupt handler
 * @arg:	Pointer to writeback encoder
 * @irq_idx:	interrupt index
 */
static void dpu_encoder_phys_wb_done_irq(void *arg, int irq_idx)
{
	_dpu_encoder_phys_wb_frame_done_helper(arg);
}

/**
 * dpu_encoder_phys_wb_irq_ctrl - irq control of WB
 * @phys:	Pointer to physical encoder
 * @enable:	indicates enable or disable interrupts
 */
static void dpu_encoder_phys_wb_irq_ctrl(
		struct dpu_encoder_phys *phys, bool enable)
{

	struct dpu_encoder_phys_wb *wb_enc = to_dpu_encoder_phys_wb(phys);

	if (enable && atomic_inc_return(&wb_enc->wbirq_refcount) == 1)
		dpu_core_irq_register_callback(phys->dpu_kms,
				phys->irq[INTR_IDX_WB_DONE], dpu_encoder_phys_wb_done_irq, phys);
	else if (!enable &&
			atomic_dec_return(&wb_enc->wbirq_refcount) == 0)
		dpu_core_irq_unregister_callback(phys->dpu_kms, phys->irq[INTR_IDX_WB_DONE]);
}

static void dpu_encoder_phys_wb_atomic_mode_set(
		struct dpu_encoder_phys *phys_enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state)
{

	phys_enc->irq[INTR_IDX_WB_DONE] = phys_enc->hw_wb->caps->intr_wb_done;
}

static void _dpu_encoder_phys_wb_handle_wbdone_timeout(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_wb *wb_enc = to_dpu_encoder_phys_wb(phys_enc);
	u32 frame_event = DPU_ENCODER_FRAME_EVENT_ERROR;

	wb_enc->wb_done_timeout_cnt++;

	if (wb_enc->wb_done_timeout_cnt == 1)
		msm_disp_snapshot_state(phys_enc->parent->dev);

	atomic_add_unless(&phys_enc->pending_kickoff_cnt, -1, 0);

	/* request a ctl reset before the next kickoff */
	phys_enc->enable_state = DPU_ENC_ERR_NEEDS_HW_RESET;

	if (wb_enc->wb_conn)
		drm_writeback_signal_completion(wb_enc->wb_conn, 0);

	if (phys_enc->parent_ops->handle_frame_done)
		phys_enc->parent_ops->handle_frame_done(
				phys_enc->parent, phys_enc, frame_event);
}

/**
 * dpu_encoder_phys_wb_wait_for_commit_done - wait until request is committed
 * @phys_enc:	Pointer to physical encoder
 */
static int dpu_encoder_phys_wb_wait_for_commit_done(
		struct dpu_encoder_phys *phys_enc)
{
	unsigned long ret;
	struct dpu_encoder_wait_info wait_info;
	struct dpu_encoder_phys_wb *wb_enc = to_dpu_encoder_phys_wb(phys_enc);

	wait_info.wq = &phys_enc->pending_kickoff_wq;
	wait_info.atomic_cnt = &phys_enc->pending_kickoff_cnt;
	wait_info.timeout_ms = KICKOFF_TIMEOUT_MS;

	ret = dpu_encoder_helper_wait_for_irq(phys_enc, INTR_IDX_WB_DONE,
			dpu_encoder_phys_wb_done_irq, &wait_info);
	if (ret == -ETIMEDOUT)
		_dpu_encoder_phys_wb_handle_wbdone_timeout(phys_enc);
	else if (!ret)
		wb_enc->wb_done_timeout_cnt = 0;

	return ret;
}

/**
 * dpu_encoder_phys_wb_prepare_for_kickoff - pre-kickoff processing
 * @phys_enc:	Pointer to physical encoder
 * Returns:	Zero on success
 */
static void dpu_encoder_phys_wb_prepare_for_kickoff(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_wb *wb_enc = to_dpu_encoder_phys_wb(phys_enc);
	struct drm_connector *drm_conn;
	struct drm_connector_state *state;

	DPU_DEBUG("[wb:%d]\n", phys_enc->hw_wb->idx - WB_0);

	if (!wb_enc->wb_conn || !wb_enc->wb_job) {
		DPU_ERROR("invalid wb_conn or wb_job\n");
		return;
	}

	drm_conn = &wb_enc->wb_conn->base;
	state = drm_conn->state;

	if (wb_enc->wb_conn && wb_enc->wb_job)
		drm_writeback_queue_job(wb_enc->wb_conn, state);

	dpu_encoder_phys_wb_setup(phys_enc);

	_dpu_encoder_phys_wb_update_flush(phys_enc);
}

/**
 * dpu_encoder_phys_wb_needs_single_flush - trigger flush processing
 * @phys_enc:	Pointer to physical encoder
 */
static bool dpu_encoder_phys_wb_needs_single_flush(struct dpu_encoder_phys *phys_enc)
{
	DPU_DEBUG("[wb:%d]\n", phys_enc->hw_wb->idx - WB_0);
	return false;
}

/**
 * dpu_encoder_phys_wb_handle_post_kickoff - post-kickoff processing
 * @phys_enc:	Pointer to physical encoder
 */
static void dpu_encoder_phys_wb_handle_post_kickoff(
		struct dpu_encoder_phys *phys_enc)
{
	DPU_DEBUG("[wb:%d]\n", phys_enc->hw_wb->idx - WB_0);

}

/**
 * dpu_encoder_phys_wb_enable - enable writeback encoder
 * @phys_enc:	Pointer to physical encoder
 */
static void dpu_encoder_phys_wb_enable(struct dpu_encoder_phys *phys_enc)
{
	DPU_DEBUG("[wb:%d]\n", phys_enc->hw_wb->idx - WB_0);
	phys_enc->enable_state = DPU_ENC_ENABLED;
}
/**
 * dpu_encoder_phys_wb_disable - disable writeback encoder
 * @phys_enc:	Pointer to physical encoder
 */
static void dpu_encoder_phys_wb_disable(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_wb *hw_wb = phys_enc->hw_wb;
	struct dpu_hw_ctl *hw_ctl = phys_enc->hw_ctl;

	DPU_DEBUG("[wb:%d]\n", hw_wb->idx - WB_0);

	if (phys_enc->enable_state == DPU_ENC_DISABLED) {
		DPU_ERROR("encoder is already disabled\n");
		return;
	}

	/* reset h/w before final flush */
	if (phys_enc->hw_ctl->ops.clear_pending_flush)
		phys_enc->hw_ctl->ops.clear_pending_flush(phys_enc->hw_ctl);

	/*
	 * New CTL reset sequence from 5.0 MDP onwards.
	 * If has_3d_merge_reset is not set, legacy reset
	 * sequence is executed.
	 *
	 * Legacy reset sequence has not been implemented yet.
	 * Any target earlier than SM8150 will need it and when
	 * WB support is added to those targets will need to add
	 * the legacy teardown sequence as well.
	 */
	if (hw_ctl->caps->features & BIT(DPU_CTL_ACTIVE_CFG))
		dpu_encoder_helper_phys_cleanup(phys_enc);

	phys_enc->enable_state = DPU_ENC_DISABLED;
}

/**
 * dpu_encoder_phys_wb_destroy - destroy writeback encoder
 * @phys_enc:	Pointer to physical encoder
 */
static void dpu_encoder_phys_wb_destroy(struct dpu_encoder_phys *phys_enc)
{
	if (!phys_enc)
		return;

	DPU_DEBUG("[wb:%d]\n", phys_enc->wb_idx - WB_0);

	kfree(phys_enc);
}

static void dpu_encoder_phys_wb_prepare_wb_job(struct dpu_encoder_phys *phys_enc,
		struct drm_writeback_job *job)
{
	const struct msm_format *format;
	struct msm_gem_address_space *aspace;
	struct dpu_hw_wb_cfg *wb_cfg;
	int ret;
	struct dpu_encoder_phys_wb *wb_enc = to_dpu_encoder_phys_wb(phys_enc);

	if (!job->fb)
		return;

	wb_enc->wb_job = job;
	wb_enc->wb_conn = job->connector;
	aspace = phys_enc->dpu_kms->base.aspace;

	wb_cfg = &wb_enc->wb_cfg;

	memset(wb_cfg, 0, sizeof(struct dpu_hw_wb_cfg));

	ret = msm_framebuffer_prepare(job->fb, aspace, false);
	if (ret) {
		DPU_ERROR("prep fb failed, %d\n", ret);
		return;
	}

	format = msm_framebuffer_format(job->fb);

	wb_cfg->dest.format = dpu_get_dpu_format_ext(
			format->pixel_format, job->fb->modifier);
	if (!wb_cfg->dest.format) {
		/* this error should be detected during atomic_check */
		DPU_ERROR("failed to get format %x\n", format->pixel_format);
		return;
	}

	ret = dpu_format_populate_layout(aspace, job->fb, &wb_cfg->dest);
	if (ret) {
		DPU_DEBUG("failed to populate layout %d\n", ret);
		return;
	}

	wb_cfg->dest.width = job->fb->width;
	wb_cfg->dest.height = job->fb->height;
	wb_cfg->dest.num_planes = wb_cfg->dest.format->num_planes;

	if ((wb_cfg->dest.format->fetch_planes == DPU_PLANE_PLANAR) &&
			(wb_cfg->dest.format->element[0] == C1_B_Cb))
		swap(wb_cfg->dest.plane_addr[1], wb_cfg->dest.plane_addr[2]);

	DPU_DEBUG("[fb_offset:%8.8x,%8.8x,%8.8x,%8.8x]\n",
			wb_cfg->dest.plane_addr[0], wb_cfg->dest.plane_addr[1],
			wb_cfg->dest.plane_addr[2], wb_cfg->dest.plane_addr[3]);

	DPU_DEBUG("[fb_stride:%8.8x,%8.8x,%8.8x,%8.8x]\n",
			wb_cfg->dest.plane_pitch[0], wb_cfg->dest.plane_pitch[1],
			wb_cfg->dest.plane_pitch[2], wb_cfg->dest.plane_pitch[3]);
}

static void dpu_encoder_phys_wb_cleanup_wb_job(struct dpu_encoder_phys *phys_enc,
		struct drm_writeback_job *job)
{
	struct dpu_encoder_phys_wb *wb_enc = to_dpu_encoder_phys_wb(phys_enc);
	struct msm_gem_address_space *aspace;

	if (!job->fb)
		return;

	aspace = phys_enc->dpu_kms->base.aspace;

	msm_framebuffer_cleanup(job->fb, aspace, false);
	wb_enc->wb_job = NULL;
	wb_enc->wb_conn = NULL;
}

static bool dpu_encoder_phys_wb_is_valid_for_commit(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_phys_wb *wb_enc = to_dpu_encoder_phys_wb(phys_enc);

	if (wb_enc->wb_job)
		return true;
	else
		return false;
}

/**
 * dpu_encoder_phys_wb_init_ops - initialize writeback operations
 * @ops:	Pointer to encoder operation table
 */
static void dpu_encoder_phys_wb_init_ops(struct dpu_encoder_phys_ops *ops)
{
	ops->is_master = dpu_encoder_phys_wb_is_master;
	ops->atomic_mode_set = dpu_encoder_phys_wb_atomic_mode_set;
	ops->enable = dpu_encoder_phys_wb_enable;
	ops->disable = dpu_encoder_phys_wb_disable;
	ops->destroy = dpu_encoder_phys_wb_destroy;
	ops->atomic_check = dpu_encoder_phys_wb_atomic_check;
	ops->wait_for_commit_done = dpu_encoder_phys_wb_wait_for_commit_done;
	ops->prepare_for_kickoff = dpu_encoder_phys_wb_prepare_for_kickoff;
	ops->handle_post_kickoff = dpu_encoder_phys_wb_handle_post_kickoff;
	ops->needs_single_flush = dpu_encoder_phys_wb_needs_single_flush;
	ops->trigger_start = dpu_encoder_helper_trigger_start;
	ops->prepare_wb_job = dpu_encoder_phys_wb_prepare_wb_job;
	ops->cleanup_wb_job = dpu_encoder_phys_wb_cleanup_wb_job;
	ops->irq_control = dpu_encoder_phys_wb_irq_ctrl;
	ops->is_valid_for_commit = dpu_encoder_phys_wb_is_valid_for_commit;

}

/**
 * dpu_encoder_phys_wb_init - initialize writeback encoder
 * @init:	Pointer to init info structure with initialization params
 */
struct dpu_encoder_phys *dpu_encoder_phys_wb_init(
		struct dpu_enc_phys_init_params *p)
{
	struct dpu_encoder_phys *phys_enc = NULL;
	struct dpu_encoder_phys_wb *wb_enc = NULL;
	int ret = 0;
	int i;

	DPU_DEBUG("\n");

	if (!p || !p->parent) {
		DPU_ERROR("invalid params\n");
		ret = -EINVAL;
		goto fail_alloc;
	}

	wb_enc = kzalloc(sizeof(*wb_enc), GFP_KERNEL);
	if (!wb_enc) {
		DPU_ERROR("failed to allocate wb phys_enc enc\n");
		ret = -ENOMEM;
		goto fail_alloc;
	}

	phys_enc = &wb_enc->base;
	phys_enc->hw_mdptop = p->dpu_kms->hw_mdp;
	phys_enc->wb_idx = p->wb_idx;

	dpu_encoder_phys_wb_init_ops(&phys_enc->ops);
	phys_enc->parent = p->parent;
	phys_enc->parent_ops = p->parent_ops;
	phys_enc->dpu_kms = p->dpu_kms;
	phys_enc->split_role = p->split_role;
	phys_enc->intf_mode = INTF_MODE_WB_LINE;
	phys_enc->wb_idx = p->wb_idx;
	phys_enc->enc_spinlock = p->enc_spinlock;

	atomic_set(&wb_enc->wbirq_refcount, 0);

	for (i = 0; i < ARRAY_SIZE(phys_enc->irq); i++)
		phys_enc->irq[i] = -EINVAL;

	atomic_set(&phys_enc->pending_kickoff_cnt, 0);
	atomic_set(&phys_enc->vblank_refcount, 0);
	wb_enc->wb_done_timeout_cnt = 0;

	init_waitqueue_head(&phys_enc->pending_kickoff_wq);
	phys_enc->enable_state = DPU_ENC_DISABLED;

	DPU_DEBUG("Created dpu_encoder_phys for wb %d\n",
			phys_enc->wb_idx);

	return phys_enc;

fail_alloc:
	return ERR_PTR(ret);
}
