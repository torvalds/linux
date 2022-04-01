// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, 2020-2021 The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__
#include "dpu_encoder_phys.h"
#include "dpu_hw_interrupts.h"
#include "dpu_hw_merge3d.h"
#include "dpu_core_irq.h"
#include "dpu_formats.h"
#include "dpu_trace.h"
#include "disp/msm_disp_snapshot.h"

#define DPU_DEBUG_VIDENC(e, fmt, ...) DPU_DEBUG("enc%d intf%d " fmt, \
		(e) && (e)->parent ? \
		(e)->parent->base.id : -1, \
		(e) && (e)->hw_intf ? \
		(e)->hw_intf->idx - INTF_0 : -1, ##__VA_ARGS__)

#define DPU_ERROR_VIDENC(e, fmt, ...) DPU_ERROR("enc%d intf%d " fmt, \
		(e) && (e)->parent ? \
		(e)->parent->base.id : -1, \
		(e) && (e)->hw_intf ? \
		(e)->hw_intf->idx - INTF_0 : -1, ##__VA_ARGS__)

#define to_dpu_encoder_phys_vid(x) \
	container_of(x, struct dpu_encoder_phys_vid, base)

static bool dpu_encoder_phys_vid_is_master(
		struct dpu_encoder_phys *phys_enc)
{
	bool ret = false;

	if (phys_enc->split_role != ENC_ROLE_SLAVE)
		ret = true;

	return ret;
}

static void drm_mode_to_intf_timing_params(
		const struct dpu_encoder_phys *phys_enc,
		const struct drm_display_mode *mode,
		struct intf_timing_params *timing)
{
	memset(timing, 0, sizeof(*timing));

	if ((mode->htotal < mode->hsync_end)
			|| (mode->hsync_start < mode->hdisplay)
			|| (mode->vtotal < mode->vsync_end)
			|| (mode->vsync_start < mode->vdisplay)
			|| (mode->hsync_end < mode->hsync_start)
			|| (mode->vsync_end < mode->vsync_start)) {
		DPU_ERROR(
		    "invalid params - hstart:%d,hend:%d,htot:%d,hdisplay:%d\n",
				mode->hsync_start, mode->hsync_end,
				mode->htotal, mode->hdisplay);
		DPU_ERROR("vstart:%d,vend:%d,vtot:%d,vdisplay:%d\n",
				mode->vsync_start, mode->vsync_end,
				mode->vtotal, mode->vdisplay);
		return;
	}

	/*
	 * https://www.kernel.org/doc/htmldocs/drm/ch02s05.html
	 *  Active Region      Front Porch   Sync   Back Porch
	 * <-----------------><------------><-----><----------->
	 * <- [hv]display --->
	 * <--------- [hv]sync_start ------>
	 * <----------------- [hv]sync_end ------->
	 * <---------------------------- [hv]total ------------->
	 */
	timing->width = mode->hdisplay;	/* active width */
	timing->height = mode->vdisplay;	/* active height */
	timing->xres = timing->width;
	timing->yres = timing->height;
	timing->h_back_porch = mode->htotal - mode->hsync_end;
	timing->h_front_porch = mode->hsync_start - mode->hdisplay;
	timing->v_back_porch = mode->vtotal - mode->vsync_end;
	timing->v_front_porch = mode->vsync_start - mode->vdisplay;
	timing->hsync_pulse_width = mode->hsync_end - mode->hsync_start;
	timing->vsync_pulse_width = mode->vsync_end - mode->vsync_start;
	timing->hsync_polarity = (mode->flags & DRM_MODE_FLAG_NHSYNC) ? 1 : 0;
	timing->vsync_polarity = (mode->flags & DRM_MODE_FLAG_NVSYNC) ? 1 : 0;
	timing->border_clr = 0;
	timing->underflow_clr = 0xff;
	timing->hsync_skew = mode->hskew;

	/* DSI controller cannot handle active-low sync signals. */
	if (phys_enc->hw_intf->cap->type == INTF_DSI) {
		timing->hsync_polarity = 0;
		timing->vsync_polarity = 0;
	}

	/*
	 * For edp only:
	 * DISPLAY_V_START = (VBP * HCYCLE) + HBP
	 * DISPLAY_V_END = (VBP + VACTIVE) * HCYCLE - 1 - HFP
	 */
	/*
	 * if (vid_enc->hw->cap->type == INTF_EDP) {
	 * display_v_start += mode->htotal - mode->hsync_start;
	 * display_v_end -= mode->hsync_start - mode->hdisplay;
	 * }
	 */
	/* for DP/EDP, Shift timings to align it to bottom right */
	if ((phys_enc->hw_intf->cap->type == INTF_DP) ||
		(phys_enc->hw_intf->cap->type == INTF_EDP)) {
		timing->h_back_porch += timing->h_front_porch;
		timing->h_front_porch = 0;
		timing->v_back_porch += timing->v_front_porch;
		timing->v_front_porch = 0;
	}
}

static u32 get_horizontal_total(const struct intf_timing_params *timing)
{
	u32 active = timing->xres;
	u32 inactive =
	    timing->h_back_porch + timing->h_front_porch +
	    timing->hsync_pulse_width;
	return active + inactive;
}

static u32 get_vertical_total(const struct intf_timing_params *timing)
{
	u32 active = timing->yres;
	u32 inactive =
	    timing->v_back_porch + timing->v_front_porch +
	    timing->vsync_pulse_width;
	return active + inactive;
}

/*
 * programmable_fetch_get_num_lines:
 *	Number of fetch lines in vertical front porch
 * @timing: Pointer to the intf timing information for the requested mode
 *
 * Returns the number of fetch lines in vertical front porch at which mdp
 * can start fetching the next frame.
 *
 * Number of needed prefetch lines is anything that cannot be absorbed in the
 * start of frame time (back porch + vsync pulse width).
 *
 * Some panels have very large VFP, however we only need a total number of
 * lines based on the chip worst case latencies.
 */
static u32 programmable_fetch_get_num_lines(
		struct dpu_encoder_phys *phys_enc,
		const struct intf_timing_params *timing)
{
	u32 worst_case_needed_lines =
	    phys_enc->hw_intf->cap->prog_fetch_lines_worst_case;
	u32 start_of_frame_lines =
	    timing->v_back_porch + timing->vsync_pulse_width;
	u32 needed_vfp_lines = worst_case_needed_lines - start_of_frame_lines;
	u32 actual_vfp_lines = 0;

	/* Fetch must be outside active lines, otherwise undefined. */
	if (start_of_frame_lines >= worst_case_needed_lines) {
		DPU_DEBUG_VIDENC(phys_enc,
				"prog fetch is not needed, large vbp+vsw\n");
		actual_vfp_lines = 0;
	} else if (timing->v_front_porch < needed_vfp_lines) {
		/* Warn fetch needed, but not enough porch in panel config */
		pr_warn_once
			("low vbp+vfp may lead to perf issues in some cases\n");
		DPU_DEBUG_VIDENC(phys_enc,
				"less vfp than fetch req, using entire vfp\n");
		actual_vfp_lines = timing->v_front_porch;
	} else {
		DPU_DEBUG_VIDENC(phys_enc, "room in vfp for needed prefetch\n");
		actual_vfp_lines = needed_vfp_lines;
	}

	DPU_DEBUG_VIDENC(phys_enc,
		"v_front_porch %u v_back_porch %u vsync_pulse_width %u\n",
		timing->v_front_porch, timing->v_back_porch,
		timing->vsync_pulse_width);
	DPU_DEBUG_VIDENC(phys_enc,
		"wc_lines %u needed_vfp_lines %u actual_vfp_lines %u\n",
		worst_case_needed_lines, needed_vfp_lines, actual_vfp_lines);

	return actual_vfp_lines;
}

/*
 * programmable_fetch_config: Programs HW to prefetch lines by offsetting
 *	the start of fetch into the vertical front porch for cases where the
 *	vsync pulse width and vertical back porch time is insufficient
 *
 *	Gets # of lines to pre-fetch, then calculate VSYNC counter value.
 *	HW layer requires VSYNC counter of first pixel of tgt VFP line.
 *
 * @timing: Pointer to the intf timing information for the requested mode
 */
static void programmable_fetch_config(struct dpu_encoder_phys *phys_enc,
				      const struct intf_timing_params *timing)
{
	struct intf_prog_fetch f = { 0 };
	u32 vfp_fetch_lines = 0;
	u32 horiz_total = 0;
	u32 vert_total = 0;
	u32 vfp_fetch_start_vsync_counter = 0;
	unsigned long lock_flags;

	if (WARN_ON_ONCE(!phys_enc->hw_intf->ops.setup_prg_fetch))
		return;

	vfp_fetch_lines = programmable_fetch_get_num_lines(phys_enc, timing);
	if (vfp_fetch_lines) {
		vert_total = get_vertical_total(timing);
		horiz_total = get_horizontal_total(timing);
		vfp_fetch_start_vsync_counter =
		    (vert_total - vfp_fetch_lines) * horiz_total + 1;
		f.enable = 1;
		f.fetch_start = vfp_fetch_start_vsync_counter;
	}

	DPU_DEBUG_VIDENC(phys_enc,
		"vfp_fetch_lines %u vfp_fetch_start_vsync_counter %u\n",
		vfp_fetch_lines, vfp_fetch_start_vsync_counter);

	spin_lock_irqsave(phys_enc->enc_spinlock, lock_flags);
	phys_enc->hw_intf->ops.setup_prg_fetch(phys_enc->hw_intf, &f);
	spin_unlock_irqrestore(phys_enc->enc_spinlock, lock_flags);
}

static bool dpu_encoder_phys_vid_mode_fixup(
		struct dpu_encoder_phys *phys_enc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adj_mode)
{
	DPU_DEBUG_VIDENC(phys_enc, "\n");

	/*
	 * Modifying mode has consequences when the mode comes back to us
	 */
	return true;
}

static void dpu_encoder_phys_vid_setup_timing_engine(
		struct dpu_encoder_phys *phys_enc)
{
	struct drm_display_mode mode;
	struct intf_timing_params timing_params = { 0 };
	const struct dpu_format *fmt = NULL;
	u32 fmt_fourcc = DRM_FORMAT_RGB888;
	unsigned long lock_flags;
	struct dpu_hw_intf_cfg intf_cfg = { 0 };

	if (!phys_enc->hw_ctl->ops.setup_intf_cfg) {
		DPU_ERROR("invalid encoder %d\n", phys_enc != NULL);
		return;
	}

	mode = phys_enc->cached_mode;
	if (!phys_enc->hw_intf->ops.setup_timing_gen) {
		DPU_ERROR("timing engine setup is not supported\n");
		return;
	}

	DPU_DEBUG_VIDENC(phys_enc, "enabling mode:\n");
	drm_mode_debug_printmodeline(&mode);

	if (phys_enc->split_role != ENC_ROLE_SOLO) {
		mode.hdisplay >>= 1;
		mode.htotal >>= 1;
		mode.hsync_start >>= 1;
		mode.hsync_end >>= 1;

		DPU_DEBUG_VIDENC(phys_enc,
			"split_role %d, halve horizontal %d %d %d %d\n",
			phys_enc->split_role,
			mode.hdisplay, mode.htotal,
			mode.hsync_start, mode.hsync_end);
	}

	drm_mode_to_intf_timing_params(phys_enc, &mode, &timing_params);

	fmt = dpu_get_dpu_format(fmt_fourcc);
	DPU_DEBUG_VIDENC(phys_enc, "fmt_fourcc 0x%X\n", fmt_fourcc);

	intf_cfg.intf = phys_enc->hw_intf->idx;
	intf_cfg.intf_mode_sel = DPU_CTL_MODE_SEL_VID;
	intf_cfg.stream_sel = 0; /* Don't care value for video mode */
	intf_cfg.mode_3d = dpu_encoder_helper_get_3d_blend_mode(phys_enc);
	if (phys_enc->hw_pp->merge_3d)
		intf_cfg.merge_3d = phys_enc->hw_pp->merge_3d->idx;

	spin_lock_irqsave(phys_enc->enc_spinlock, lock_flags);
	phys_enc->hw_intf->ops.setup_timing_gen(phys_enc->hw_intf,
			&timing_params, fmt);
	phys_enc->hw_ctl->ops.setup_intf_cfg(phys_enc->hw_ctl, &intf_cfg);

	/* setup which pp blk will connect to this intf */
	if (phys_enc->hw_intf->ops.bind_pingpong_blk)
		phys_enc->hw_intf->ops.bind_pingpong_blk(
				phys_enc->hw_intf,
				true,
				phys_enc->hw_pp->idx);

	if (phys_enc->hw_pp->merge_3d)
		phys_enc->hw_pp->merge_3d->ops.setup_3d_mode(phys_enc->hw_pp->merge_3d, intf_cfg.mode_3d);

	spin_unlock_irqrestore(phys_enc->enc_spinlock, lock_flags);

	programmable_fetch_config(phys_enc, &timing_params);
}

static void dpu_encoder_phys_vid_vblank_irq(void *arg, int irq_idx)
{
	struct dpu_encoder_phys *phys_enc = arg;
	struct dpu_hw_ctl *hw_ctl;
	unsigned long lock_flags;
	u32 flush_register = 0;

	hw_ctl = phys_enc->hw_ctl;

	DPU_ATRACE_BEGIN("vblank_irq");

	if (phys_enc->parent_ops->handle_vblank_virt)
		phys_enc->parent_ops->handle_vblank_virt(phys_enc->parent,
				phys_enc);

	atomic_read(&phys_enc->pending_kickoff_cnt);

	/*
	 * only decrement the pending flush count if we've actually flushed
	 * hardware. due to sw irq latency, vblank may have already happened
	 * so we need to double-check with hw that it accepted the flush bits
	 */
	spin_lock_irqsave(phys_enc->enc_spinlock, lock_flags);
	if (hw_ctl->ops.get_flush_register)
		flush_register = hw_ctl->ops.get_flush_register(hw_ctl);

	if (!(flush_register & hw_ctl->ops.get_pending_flush(hw_ctl)))
		atomic_add_unless(&phys_enc->pending_kickoff_cnt, -1, 0);
	spin_unlock_irqrestore(phys_enc->enc_spinlock, lock_flags);

	/* Signal any waiting atomic commit thread */
	wake_up_all(&phys_enc->pending_kickoff_wq);

	phys_enc->parent_ops->handle_frame_done(phys_enc->parent, phys_enc,
			DPU_ENCODER_FRAME_EVENT_DONE);

	DPU_ATRACE_END("vblank_irq");
}

static void dpu_encoder_phys_vid_underrun_irq(void *arg, int irq_idx)
{
	struct dpu_encoder_phys *phys_enc = arg;

	if (phys_enc->parent_ops->handle_underrun_virt)
		phys_enc->parent_ops->handle_underrun_virt(phys_enc->parent,
			phys_enc);
}

static bool dpu_encoder_phys_vid_needs_single_flush(
		struct dpu_encoder_phys *phys_enc)
{
	return phys_enc->split_role != ENC_ROLE_SOLO;
}

static void dpu_encoder_phys_vid_mode_set(
		struct dpu_encoder_phys *phys_enc,
		struct drm_display_mode *mode,
		struct drm_display_mode *adj_mode)
{
	struct dpu_encoder_irq *irq;

	if (adj_mode) {
		phys_enc->cached_mode = *adj_mode;
		drm_mode_debug_printmodeline(adj_mode);
		DPU_DEBUG_VIDENC(phys_enc, "caching mode:\n");
	}

	irq = &phys_enc->irq[INTR_IDX_VSYNC];
	irq->irq_idx = phys_enc->hw_intf->cap->intr_vsync;

	irq = &phys_enc->irq[INTR_IDX_UNDERRUN];
	irq->irq_idx = phys_enc->hw_intf->cap->intr_underrun;
}

static int dpu_encoder_phys_vid_control_vblank_irq(
		struct dpu_encoder_phys *phys_enc,
		bool enable)
{
	int ret = 0;
	int refcount;

	refcount = atomic_read(&phys_enc->vblank_refcount);

	/* Slave encoders don't report vblank */
	if (!dpu_encoder_phys_vid_is_master(phys_enc))
		goto end;

	/* protect against negative */
	if (!enable && refcount == 0) {
		ret = -EINVAL;
		goto end;
	}

	DRM_DEBUG_VBL("id:%u enable=%d/%d\n", DRMID(phys_enc->parent), enable,
		      atomic_read(&phys_enc->vblank_refcount));

	if (enable && atomic_inc_return(&phys_enc->vblank_refcount) == 1)
		ret = dpu_encoder_helper_register_irq(phys_enc, INTR_IDX_VSYNC);
	else if (!enable && atomic_dec_return(&phys_enc->vblank_refcount) == 0)
		ret = dpu_encoder_helper_unregister_irq(phys_enc,
				INTR_IDX_VSYNC);

end:
	if (ret) {
		DRM_ERROR("failed: id:%u intf:%d ret:%d enable:%d refcnt:%d\n",
			  DRMID(phys_enc->parent),
			  phys_enc->hw_intf->idx - INTF_0, ret, enable,
			  refcount);
	}
	return ret;
}

static void dpu_encoder_phys_vid_enable(struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_ctl *ctl;

	ctl = phys_enc->hw_ctl;

	DPU_DEBUG_VIDENC(phys_enc, "\n");

	if (WARN_ON(!phys_enc->hw_intf->ops.enable_timing))
		return;

	dpu_encoder_helper_split_config(phys_enc, phys_enc->hw_intf->idx);

	dpu_encoder_phys_vid_setup_timing_engine(phys_enc);

	/*
	 * For single flush cases (dual-ctl or pp-split), skip setting the
	 * flush bit for the slave intf, since both intfs use same ctl
	 * and HW will only flush the master.
	 */
	if (dpu_encoder_phys_vid_needs_single_flush(phys_enc) &&
		!dpu_encoder_phys_vid_is_master(phys_enc))
		goto skip_flush;

	ctl->ops.update_pending_flush_intf(ctl, phys_enc->hw_intf->idx);
	if (ctl->ops.update_pending_flush_merge_3d && phys_enc->hw_pp->merge_3d)
		ctl->ops.update_pending_flush_merge_3d(ctl, phys_enc->hw_pp->merge_3d->idx);

skip_flush:
	DPU_DEBUG_VIDENC(phys_enc,
		"update pending flush ctl %d intf %d\n",
		ctl->idx - CTL_0, phys_enc->hw_intf->idx);

	atomic_set(&phys_enc->underrun_cnt, 0);

	/* ctl_flush & timing engine enable will be triggered by framework */
	if (phys_enc->enable_state == DPU_ENC_DISABLED)
		phys_enc->enable_state = DPU_ENC_ENABLING;
}

static void dpu_encoder_phys_vid_destroy(struct dpu_encoder_phys *phys_enc)
{
	DPU_DEBUG_VIDENC(phys_enc, "\n");
	kfree(phys_enc);
}

static void dpu_encoder_phys_vid_get_hw_resources(
		struct dpu_encoder_phys *phys_enc,
		struct dpu_encoder_hw_resources *hw_res)
{
	hw_res->intfs[phys_enc->intf_idx - INTF_0] = INTF_MODE_VIDEO;
}

static int dpu_encoder_phys_vid_wait_for_vblank(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_encoder_wait_info wait_info;
	int ret;

	wait_info.wq = &phys_enc->pending_kickoff_wq;
	wait_info.atomic_cnt = &phys_enc->pending_kickoff_cnt;
	wait_info.timeout_ms = KICKOFF_TIMEOUT_MS;

	if (!dpu_encoder_phys_vid_is_master(phys_enc)) {
		return 0;
	}

	/* Wait for kickoff to complete */
	ret = dpu_encoder_helper_wait_for_irq(phys_enc, INTR_IDX_VSYNC,
			&wait_info);

	if (ret == -ETIMEDOUT) {
		dpu_encoder_helper_report_irq_timeout(phys_enc, INTR_IDX_VSYNC);
	}

	return ret;
}

static int dpu_encoder_phys_vid_wait_for_commit_done(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_ctl *hw_ctl = phys_enc->hw_ctl;
	int ret;

	if (!hw_ctl)
		return 0;

	ret = wait_event_timeout(phys_enc->pending_kickoff_wq,
		(hw_ctl->ops.get_flush_register(hw_ctl) == 0),
		msecs_to_jiffies(50));
	if (ret <= 0) {
		DPU_ERROR("vblank timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void dpu_encoder_phys_vid_prepare_for_kickoff(
		struct dpu_encoder_phys *phys_enc)
{
	struct dpu_hw_ctl *ctl;
	int rc;
	struct drm_encoder *drm_enc;

	drm_enc = phys_enc->parent;

	ctl = phys_enc->hw_ctl;
	if (!ctl->ops.wait_reset_status)
		return;

	/*
	 * hw supports hardware initiated ctl reset, so before we kickoff a new
	 * frame, need to check and wait for hw initiated ctl reset completion
	 */
	rc = ctl->ops.wait_reset_status(ctl);
	if (rc) {
		DPU_ERROR_VIDENC(phys_enc, "ctl %d reset failure: %d\n",
				ctl->idx, rc);
		msm_disp_snapshot_state(drm_enc->dev);
		dpu_encoder_helper_unregister_irq(phys_enc, INTR_IDX_VSYNC);
	}
}

static void dpu_encoder_phys_vid_disable(struct dpu_encoder_phys *phys_enc)
{
	unsigned long lock_flags;
	int ret;

	if (!phys_enc->parent || !phys_enc->parent->dev) {
		DPU_ERROR("invalid encoder/device\n");
		return;
	}

	if (!phys_enc->hw_intf) {
		DPU_ERROR("invalid hw_intf %d hw_ctl %d\n",
				phys_enc->hw_intf != NULL, phys_enc->hw_ctl != NULL);
		return;
	}

	if (WARN_ON(!phys_enc->hw_intf->ops.enable_timing))
		return;

	if (phys_enc->enable_state == DPU_ENC_DISABLED) {
		DPU_ERROR("already disabled\n");
		return;
	}

	spin_lock_irqsave(phys_enc->enc_spinlock, lock_flags);
	phys_enc->hw_intf->ops.enable_timing(phys_enc->hw_intf, 0);
	if (dpu_encoder_phys_vid_is_master(phys_enc))
		dpu_encoder_phys_inc_pending(phys_enc);
	spin_unlock_irqrestore(phys_enc->enc_spinlock, lock_flags);

	/*
	 * Wait for a vsync so we know the ENABLE=0 latched before
	 * the (connector) source of the vsync's gets disabled,
	 * otherwise we end up in a funny state if we re-enable
	 * before the disable latches, which results that some of
	 * the settings changes for the new modeset (like new
	 * scanout buffer) don't latch properly..
	 */
	if (dpu_encoder_phys_vid_is_master(phys_enc)) {
		ret = dpu_encoder_phys_vid_wait_for_vblank(phys_enc);
		if (ret) {
			atomic_set(&phys_enc->pending_kickoff_cnt, 0);
			DRM_ERROR("wait disable failed: id:%u intf:%d ret:%d\n",
				  DRMID(phys_enc->parent),
				  phys_enc->hw_intf->idx - INTF_0, ret);
		}
	}

	phys_enc->enable_state = DPU_ENC_DISABLED;
}

static void dpu_encoder_phys_vid_handle_post_kickoff(
		struct dpu_encoder_phys *phys_enc)
{
	unsigned long lock_flags;

	/*
	 * Video mode must flush CTL before enabling timing engine
	 * Video encoders need to turn on their interfaces now
	 */
	if (phys_enc->enable_state == DPU_ENC_ENABLING) {
		trace_dpu_enc_phys_vid_post_kickoff(DRMID(phys_enc->parent),
				    phys_enc->hw_intf->idx - INTF_0);
		spin_lock_irqsave(phys_enc->enc_spinlock, lock_flags);
		phys_enc->hw_intf->ops.enable_timing(phys_enc->hw_intf, 1);
		spin_unlock_irqrestore(phys_enc->enc_spinlock, lock_flags);
		phys_enc->enable_state = DPU_ENC_ENABLED;
	}
}

static void dpu_encoder_phys_vid_irq_control(struct dpu_encoder_phys *phys_enc,
		bool enable)
{
	int ret;

	trace_dpu_enc_phys_vid_irq_ctrl(DRMID(phys_enc->parent),
			    phys_enc->hw_intf->idx - INTF_0,
			    enable,
			    atomic_read(&phys_enc->vblank_refcount));

	if (enable) {
		ret = dpu_encoder_phys_vid_control_vblank_irq(phys_enc, true);
		if (WARN_ON(ret))
			return;

		dpu_encoder_helper_register_irq(phys_enc, INTR_IDX_UNDERRUN);
	} else {
		dpu_encoder_phys_vid_control_vblank_irq(phys_enc, false);
		dpu_encoder_helper_unregister_irq(phys_enc, INTR_IDX_UNDERRUN);
	}
}

static int dpu_encoder_phys_vid_get_line_count(
		struct dpu_encoder_phys *phys_enc)
{
	if (!dpu_encoder_phys_vid_is_master(phys_enc))
		return -EINVAL;

	if (!phys_enc->hw_intf || !phys_enc->hw_intf->ops.get_line_count)
		return -EINVAL;

	return phys_enc->hw_intf->ops.get_line_count(phys_enc->hw_intf);
}

static int dpu_encoder_phys_vid_get_frame_count(
		struct dpu_encoder_phys *phys_enc)
{
	struct intf_status s = {0};
	u32 fetch_start = 0;
	struct drm_display_mode mode = phys_enc->cached_mode;

	if (!dpu_encoder_phys_vid_is_master(phys_enc))
		return -EINVAL;

	if (!phys_enc->hw_intf || !phys_enc->hw_intf->ops.get_status)
		return -EINVAL;

	phys_enc->hw_intf->ops.get_status(phys_enc->hw_intf, &s);

	if (s.is_prog_fetch_en && s.is_en) {
		fetch_start = mode.vtotal - (mode.vsync_start - mode.vdisplay);
		if ((s.line_count > fetch_start) &&
			(s.line_count <= mode.vtotal))
			return s.frame_count + 1;
	}

	return s.frame_count;
}

static void dpu_encoder_phys_vid_init_ops(struct dpu_encoder_phys_ops *ops)
{
	ops->is_master = dpu_encoder_phys_vid_is_master;
	ops->mode_set = dpu_encoder_phys_vid_mode_set;
	ops->mode_fixup = dpu_encoder_phys_vid_mode_fixup;
	ops->enable = dpu_encoder_phys_vid_enable;
	ops->disable = dpu_encoder_phys_vid_disable;
	ops->destroy = dpu_encoder_phys_vid_destroy;
	ops->get_hw_resources = dpu_encoder_phys_vid_get_hw_resources;
	ops->control_vblank_irq = dpu_encoder_phys_vid_control_vblank_irq;
	ops->wait_for_commit_done = dpu_encoder_phys_vid_wait_for_commit_done;
	ops->wait_for_vblank = dpu_encoder_phys_vid_wait_for_vblank;
	ops->wait_for_tx_complete = dpu_encoder_phys_vid_wait_for_vblank;
	ops->irq_control = dpu_encoder_phys_vid_irq_control;
	ops->prepare_for_kickoff = dpu_encoder_phys_vid_prepare_for_kickoff;
	ops->handle_post_kickoff = dpu_encoder_phys_vid_handle_post_kickoff;
	ops->needs_single_flush = dpu_encoder_phys_vid_needs_single_flush;
	ops->get_line_count = dpu_encoder_phys_vid_get_line_count;
	ops->get_frame_count = dpu_encoder_phys_vid_get_frame_count;
}

struct dpu_encoder_phys *dpu_encoder_phys_vid_init(
		struct dpu_enc_phys_init_params *p)
{
	struct dpu_encoder_phys *phys_enc = NULL;
	struct dpu_encoder_irq *irq;
	int i;

	if (!p) {
		DPU_ERROR("failed to create encoder due to invalid parameter\n");
		return ERR_PTR(-EINVAL);
	}

	phys_enc = kzalloc(sizeof(*phys_enc), GFP_KERNEL);
	if (!phys_enc) {
		DPU_ERROR("failed to create encoder due to memory allocation error\n");
		return ERR_PTR(-ENOMEM);
	}

	phys_enc->hw_mdptop = p->dpu_kms->hw_mdp;
	phys_enc->intf_idx = p->intf_idx;

	DPU_DEBUG_VIDENC(phys_enc, "\n");

	dpu_encoder_phys_vid_init_ops(&phys_enc->ops);
	phys_enc->parent = p->parent;
	phys_enc->parent_ops = p->parent_ops;
	phys_enc->dpu_kms = p->dpu_kms;
	phys_enc->split_role = p->split_role;
	phys_enc->intf_mode = INTF_MODE_VIDEO;
	phys_enc->enc_spinlock = p->enc_spinlock;
	for (i = 0; i < INTR_IDX_MAX; i++) {
		irq = &phys_enc->irq[i];
		INIT_LIST_HEAD(&irq->cb.list);
		irq->irq_idx = -EINVAL;
		irq->cb.arg = phys_enc;
	}

	irq = &phys_enc->irq[INTR_IDX_VSYNC];
	irq->name = "vsync_irq";
	irq->intr_idx = INTR_IDX_VSYNC;
	irq->cb.func = dpu_encoder_phys_vid_vblank_irq;

	irq = &phys_enc->irq[INTR_IDX_UNDERRUN];
	irq->name = "underrun";
	irq->intr_idx = INTR_IDX_UNDERRUN;
	irq->cb.func = dpu_encoder_phys_vid_underrun_irq;

	atomic_set(&phys_enc->vblank_refcount, 0);
	atomic_set(&phys_enc->pending_kickoff_cnt, 0);
	init_waitqueue_head(&phys_enc->pending_kickoff_wq);
	phys_enc->enable_state = DPU_ENC_DISABLED;

	DPU_DEBUG_VIDENC(phys_enc, "created intf idx:%d\n", p->intf_idx);

	return phys_enc;
}
