// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "komeda_dev.h"
#include "komeda_kms.h"

void komeda_crtc_get_color_config(struct drm_crtc_state *crtc_st,
				  u32 *color_depths, u32 *color_formats)
{
	struct drm_connector *conn;
	struct drm_connector_state *conn_st;
	u32 conn_color_formats = ~0u;
	int i, min_bpc = 31, conn_bpc = 0;

	for_each_new_connector_in_state(crtc_st->state, conn, conn_st, i) {
		if (conn_st->crtc != crtc_st->crtc)
			continue;

		conn_bpc = conn->display_info.bpc ? conn->display_info.bpc : 8;
		conn_color_formats &= conn->display_info.color_formats;

		if (conn_bpc < min_bpc)
			min_bpc = conn_bpc;
	}

	/* connector doesn't config any color_format, use RGB444 as default */
	if (!conn_color_formats)
		conn_color_formats = DRM_COLOR_FORMAT_RGB444;

	*color_depths = GENMASK(min_bpc, 0);
	*color_formats = conn_color_formats;
}

static void komeda_crtc_update_clock_ratio(struct komeda_crtc_state *kcrtc_st)
{
	u64 pxlclk, aclk;

	if (!kcrtc_st->base.active) {
		kcrtc_st->clock_ratio = 0;
		return;
	}

	pxlclk = kcrtc_st->base.adjusted_mode.crtc_clock * 1000ULL;
	aclk = komeda_crtc_get_aclk(kcrtc_st);

	kcrtc_st->clock_ratio = div64_u64(aclk << 32, pxlclk);
}

/**
 * komeda_crtc_atomic_check - build display output data flow
 * @crtc: DRM crtc
 * @state: the crtc state object
 *
 * crtc_atomic_check is the final check stage, so beside build a display data
 * pipeline according to the crtc_state, but still needs to release or disable
 * the unclaimed pipeline resources.
 *
 * RETURNS:
 * Zero for success or -errno
 */
static int
komeda_crtc_atomic_check(struct drm_crtc *crtc,
			 struct drm_crtc_state *state)
{
	struct komeda_crtc *kcrtc = to_kcrtc(crtc);
	struct komeda_crtc_state *kcrtc_st = to_kcrtc_st(state);
	int err;

	if (drm_atomic_crtc_needs_modeset(state))
		komeda_crtc_update_clock_ratio(kcrtc_st);

	if (state->active) {
		err = komeda_build_display_data_flow(kcrtc, kcrtc_st);
		if (err)
			return err;
	}

	/* release unclaimed pipeline resources */
	err = komeda_release_unclaimed_resources(kcrtc->slave, kcrtc_st);
	if (err)
		return err;

	err = komeda_release_unclaimed_resources(kcrtc->master, kcrtc_st);
	if (err)
		return err;

	return 0;
}

/* For active a crtc, mainly need two parts of preparation
 * 1. adjust display operation mode.
 * 2. enable needed clk
 */
static int
komeda_crtc_prepare(struct komeda_crtc *kcrtc)
{
	struct komeda_dev *mdev = kcrtc->base.dev->dev_private;
	struct komeda_pipeline *master = kcrtc->master;
	struct komeda_crtc_state *kcrtc_st = to_kcrtc_st(kcrtc->base.state);
	struct drm_display_mode *mode = &kcrtc_st->base.adjusted_mode;
	u32 new_mode;
	int err;

	mutex_lock(&mdev->lock);

	new_mode = mdev->dpmode | BIT(master->id);
	if (WARN_ON(new_mode == mdev->dpmode)) {
		err = 0;
		goto unlock;
	}

	err = mdev->funcs->change_opmode(mdev, new_mode);
	if (err) {
		DRM_ERROR("failed to change opmode: 0x%x -> 0x%x.\n,",
			  mdev->dpmode, new_mode);
		goto unlock;
	}

	mdev->dpmode = new_mode;
	/* Only need to enable aclk on single display mode, but no need to
	 * enable aclk it on dual display mode, since the dual mode always
	 * switch from single display mode, the aclk already enabled, no need
	 * to enable it again.
	 */
	if (new_mode != KOMEDA_MODE_DUAL_DISP) {
		err = clk_set_rate(mdev->aclk, komeda_crtc_get_aclk(kcrtc_st));
		if (err)
			DRM_ERROR("failed to set aclk.\n");
		err = clk_prepare_enable(mdev->aclk);
		if (err)
			DRM_ERROR("failed to enable aclk.\n");
	}

	err = clk_set_rate(master->pxlclk, mode->crtc_clock * 1000);
	if (err)
		DRM_ERROR("failed to set pxlclk for pipe%d\n", master->id);
	err = clk_prepare_enable(master->pxlclk);
	if (err)
		DRM_ERROR("failed to enable pxl clk for pipe%d.\n", master->id);

unlock:
	mutex_unlock(&mdev->lock);

	return err;
}

static int
komeda_crtc_unprepare(struct komeda_crtc *kcrtc)
{
	struct komeda_dev *mdev = kcrtc->base.dev->dev_private;
	struct komeda_pipeline *master = kcrtc->master;
	u32 new_mode;
	int err;

	mutex_lock(&mdev->lock);

	new_mode = mdev->dpmode & (~BIT(master->id));

	if (WARN_ON(new_mode == mdev->dpmode)) {
		err = 0;
		goto unlock;
	}

	err = mdev->funcs->change_opmode(mdev, new_mode);
	if (err) {
		DRM_ERROR("failed to change opmode: 0x%x -> 0x%x.\n,",
			  mdev->dpmode, new_mode);
		goto unlock;
	}

	mdev->dpmode = new_mode;

	clk_disable_unprepare(master->pxlclk);
	if (new_mode == KOMEDA_MODE_INACTIVE)
		clk_disable_unprepare(mdev->aclk);

unlock:
	mutex_unlock(&mdev->lock);

	return err;
}

void komeda_crtc_handle_event(struct komeda_crtc   *kcrtc,
			      struct komeda_events *evts)
{
	struct drm_crtc *crtc = &kcrtc->base;
	u32 events = evts->pipes[kcrtc->master->id];

	if (events & KOMEDA_EVENT_VSYNC)
		drm_crtc_handle_vblank(crtc);

	if (events & KOMEDA_EVENT_EOW) {
		struct komeda_wb_connector *wb_conn = kcrtc->wb_conn;

		if (wb_conn)
			drm_writeback_signal_completion(&wb_conn->base, 0);
		else
			DRM_WARN("CRTC[%d]: EOW happen but no wb_connector.\n",
				 drm_crtc_index(&kcrtc->base));
	}
	/* will handle it together with the write back support */
	if (events & KOMEDA_EVENT_EOW)
		DRM_DEBUG("EOW.\n");

	if (events & KOMEDA_EVENT_FLIP) {
		unsigned long flags;
		struct drm_pending_vblank_event *event;

		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		if (kcrtc->disable_done) {
			complete_all(kcrtc->disable_done);
			kcrtc->disable_done = NULL;
		} else if (crtc->state->event) {
			event = crtc->state->event;
			/*
			 * Consume event before notifying drm core that flip
			 * happened.
			 */
			crtc->state->event = NULL;
			drm_crtc_send_vblank_event(crtc, event);
		} else {
			DRM_WARN("CRTC[%d]: FLIP happen but no pending commit.\n",
				 drm_crtc_index(&kcrtc->base));
		}
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
	}
}

static void
komeda_crtc_do_flush(struct drm_crtc *crtc,
		     struct drm_crtc_state *old)
{
	struct komeda_crtc *kcrtc = to_kcrtc(crtc);
	struct komeda_crtc_state *kcrtc_st = to_kcrtc_st(crtc->state);
	struct komeda_dev *mdev = kcrtc->base.dev->dev_private;
	struct komeda_pipeline *master = kcrtc->master;
	struct komeda_pipeline *slave = kcrtc->slave;
	struct komeda_wb_connector *wb_conn = kcrtc->wb_conn;
	struct drm_connector_state *conn_st;

	DRM_DEBUG_ATOMIC("CRTC%d_FLUSH: active_pipes: 0x%x, affected: 0x%x.\n",
			 drm_crtc_index(crtc),
			 kcrtc_st->active_pipes, kcrtc_st->affected_pipes);

	/* step 1: update the pipeline/component state to HW */
	if (has_bit(master->id, kcrtc_st->affected_pipes))
		komeda_pipeline_update(master, old->state);

	if (slave && has_bit(slave->id, kcrtc_st->affected_pipes))
		komeda_pipeline_update(slave, old->state);

	conn_st = wb_conn ? wb_conn->base.base.state : NULL;
	if (conn_st && conn_st->writeback_job)
		drm_writeback_queue_job(&wb_conn->base, conn_st);

	/* step 2: notify the HW to kickoff the update */
	mdev->funcs->flush(mdev, master->id, kcrtc_st->active_pipes);
}

static void
komeda_crtc_atomic_enable(struct drm_crtc *crtc,
			  struct drm_crtc_state *old)
{
	pm_runtime_get_sync(crtc->dev->dev);
	komeda_crtc_prepare(to_kcrtc(crtc));
	drm_crtc_vblank_on(crtc);
	WARN_ON(drm_crtc_vblank_get(crtc));
	komeda_crtc_do_flush(crtc, old);
}

static void
komeda_crtc_flush_and_wait_for_flip_done(struct komeda_crtc *kcrtc,
					 struct completion *input_flip_done)
{
	struct drm_device *drm = kcrtc->base.dev;
	struct komeda_dev *mdev = kcrtc->master->mdev;
	struct completion *flip_done;
	struct completion temp;
	int timeout;

	/* if caller doesn't send a flip_done, use a private flip_done */
	if (input_flip_done) {
		flip_done = input_flip_done;
	} else {
		init_completion(&temp);
		kcrtc->disable_done = &temp;
		flip_done = &temp;
	}

	mdev->funcs->flush(mdev, kcrtc->master->id, 0);

	/* wait the flip take affect.*/
	timeout = wait_for_completion_timeout(flip_done, HZ);
	if (timeout == 0) {
		DRM_ERROR("wait pipe%d flip done timeout\n", kcrtc->master->id);
		if (!input_flip_done) {
			unsigned long flags;

			spin_lock_irqsave(&drm->event_lock, flags);
			kcrtc->disable_done = NULL;
			spin_unlock_irqrestore(&drm->event_lock, flags);
		}
	}
}

static void
komeda_crtc_atomic_disable(struct drm_crtc *crtc,
			   struct drm_crtc_state *old)
{
	struct komeda_crtc *kcrtc = to_kcrtc(crtc);
	struct komeda_crtc_state *old_st = to_kcrtc_st(old);
	struct komeda_pipeline *master = kcrtc->master;
	struct komeda_pipeline *slave  = kcrtc->slave;
	struct completion *disable_done;
	bool needs_phase2 = false;

	DRM_DEBUG_ATOMIC("CRTC%d_DISABLE: active_pipes: 0x%x, affected: 0x%x\n",
			 drm_crtc_index(crtc),
			 old_st->active_pipes, old_st->affected_pipes);

	if (slave && has_bit(slave->id, old_st->active_pipes))
		komeda_pipeline_disable(slave, old->state);

	if (has_bit(master->id, old_st->active_pipes))
		needs_phase2 = komeda_pipeline_disable(master, old->state);

	/* crtc_disable has two scenarios according to the state->active switch.
	 * 1. active -> inactive
	 *    this commit is a disable commit. and the commit will be finished
	 *    or done after the disable operation. on this case we can directly
	 *    use the crtc->state->event to tracking the HW disable operation.
	 * 2. active -> active
	 *    the crtc->commit is not for disable, but a modeset operation when
	 *    crtc is active, such commit actually has been completed by 3
	 *    DRM operations:
	 *    crtc_disable, update_planes(crtc_flush), crtc_enable
	 *    so on this case the crtc->commit is for the whole process.
	 *    we can not use it for tracing the disable, we need a temporary
	 *    flip_done for tracing the disable. and crtc->state->event for
	 *    the crtc_enable operation.
	 *    That's also the reason why skip modeset commit in
	 *    komeda_crtc_atomic_flush()
	 */
	disable_done = (needs_phase2 || crtc->state->active) ?
		       NULL : &crtc->state->commit->flip_done;

	/* wait phase 1 disable done */
	komeda_crtc_flush_and_wait_for_flip_done(kcrtc, disable_done);

	/* phase 2 */
	if (needs_phase2) {
		komeda_pipeline_disable(kcrtc->master, old->state);

		disable_done = crtc->state->active ?
			       NULL : &crtc->state->commit->flip_done;

		komeda_crtc_flush_and_wait_for_flip_done(kcrtc, disable_done);
	}

	drm_crtc_vblank_put(crtc);
	drm_crtc_vblank_off(crtc);
	komeda_crtc_unprepare(kcrtc);
	pm_runtime_put(crtc->dev->dev);
}

static void
komeda_crtc_atomic_flush(struct drm_crtc *crtc,
			 struct drm_crtc_state *old)
{
	/* commit with modeset will be handled in enable/disable */
	if (drm_atomic_crtc_needs_modeset(crtc->state))
		return;

	komeda_crtc_do_flush(crtc, old);
}

/* Returns the minimum frequency of the aclk rate (main engine clock) in Hz */
static unsigned long
komeda_calc_min_aclk_rate(struct komeda_crtc *kcrtc,
			  unsigned long pxlclk)
{
	/* Once dual-link one display pipeline drives two display outputs,
	 * the aclk needs run on the double rate of pxlclk
	 */
	if (kcrtc->master->dual_link)
		return pxlclk * 2;
	else
		return pxlclk;
}

/* Get current aclk rate that specified by state */
unsigned long komeda_crtc_get_aclk(struct komeda_crtc_state *kcrtc_st)
{
	struct drm_crtc *crtc = kcrtc_st->base.crtc;
	struct komeda_dev *mdev = crtc->dev->dev_private;
	unsigned long pxlclk = kcrtc_st->base.adjusted_mode.crtc_clock * 1000;
	unsigned long min_aclk;

	min_aclk = komeda_calc_min_aclk_rate(to_kcrtc(crtc), pxlclk);

	return clk_round_rate(mdev->aclk, min_aclk);
}

static enum drm_mode_status
komeda_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *m)
{
	struct komeda_dev *mdev = crtc->dev->dev_private;
	struct komeda_crtc *kcrtc = to_kcrtc(crtc);
	struct komeda_pipeline *master = kcrtc->master;
	unsigned long min_pxlclk, min_aclk;

	if (m->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	min_pxlclk = m->clock * 1000;
	if (master->dual_link)
		min_pxlclk /= 2;

	if (min_pxlclk != clk_round_rate(master->pxlclk, min_pxlclk)) {
		DRM_DEBUG_ATOMIC("pxlclk doesn't support %lu Hz\n", min_pxlclk);

		return MODE_NOCLOCK;
	}

	min_aclk = komeda_calc_min_aclk_rate(to_kcrtc(crtc), min_pxlclk);
	if (clk_round_rate(mdev->aclk, min_aclk) < min_aclk) {
		DRM_DEBUG_ATOMIC("engine clk can't satisfy the requirement of %s-clk: %lu.\n",
				 m->name, min_pxlclk);

		return MODE_CLOCK_HIGH;
	}

	return MODE_OK;
}

static bool komeda_crtc_mode_fixup(struct drm_crtc *crtc,
				   const struct drm_display_mode *m,
				   struct drm_display_mode *adjusted_mode)
{
	struct komeda_crtc *kcrtc = to_kcrtc(crtc);
	unsigned long clk_rate;

	drm_mode_set_crtcinfo(adjusted_mode, 0);
	/* In dual link half the horizontal settings */
	if (kcrtc->master->dual_link) {
		adjusted_mode->crtc_clock /= 2;
		adjusted_mode->crtc_hdisplay /= 2;
		adjusted_mode->crtc_hsync_start /= 2;
		adjusted_mode->crtc_hsync_end /= 2;
		adjusted_mode->crtc_htotal /= 2;
	}

	clk_rate = adjusted_mode->crtc_clock * 1000;
	/* crtc_clock will be used as the komeda output pixel clock */
	adjusted_mode->crtc_clock = clk_round_rate(kcrtc->master->pxlclk,
						   clk_rate) / 1000;

	return true;
}

static const struct drm_crtc_helper_funcs komeda_crtc_helper_funcs = {
	.atomic_check	= komeda_crtc_atomic_check,
	.atomic_flush	= komeda_crtc_atomic_flush,
	.atomic_enable	= komeda_crtc_atomic_enable,
	.atomic_disable	= komeda_crtc_atomic_disable,
	.mode_valid	= komeda_crtc_mode_valid,
	.mode_fixup	= komeda_crtc_mode_fixup,
};

static void komeda_crtc_reset(struct drm_crtc *crtc)
{
	struct komeda_crtc_state *state;

	if (crtc->state)
		__drm_atomic_helper_crtc_destroy_state(crtc->state);

	kfree(to_kcrtc_st(crtc->state));
	crtc->state = NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_crtc_reset(crtc, &state->base);
}

static struct drm_crtc_state *
komeda_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct komeda_crtc_state *old = to_kcrtc_st(crtc->state);
	struct komeda_crtc_state *new;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &new->base);

	new->affected_pipes = old->active_pipes;
	new->clock_ratio = old->clock_ratio;
	new->max_slave_zorder = old->max_slave_zorder;

	return &new->base;
}

static void komeda_crtc_atomic_destroy_state(struct drm_crtc *crtc,
					     struct drm_crtc_state *state)
{
	__drm_atomic_helper_crtc_destroy_state(state);
	kfree(to_kcrtc_st(state));
}

static int komeda_crtc_vblank_enable(struct drm_crtc *crtc)
{
	struct komeda_dev *mdev = crtc->dev->dev_private;
	struct komeda_crtc *kcrtc = to_kcrtc(crtc);

	mdev->funcs->on_off_vblank(mdev, kcrtc->master->id, true);
	return 0;
}

static void komeda_crtc_vblank_disable(struct drm_crtc *crtc)
{
	struct komeda_dev *mdev = crtc->dev->dev_private;
	struct komeda_crtc *kcrtc = to_kcrtc(crtc);

	mdev->funcs->on_off_vblank(mdev, kcrtc->master->id, false);
}

static const struct drm_crtc_funcs komeda_crtc_funcs = {
	.gamma_set		= drm_atomic_helper_legacy_gamma_set,
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= komeda_crtc_reset,
	.atomic_duplicate_state	= komeda_crtc_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_crtc_atomic_destroy_state,
	.enable_vblank		= komeda_crtc_vblank_enable,
	.disable_vblank		= komeda_crtc_vblank_disable,
};

int komeda_kms_setup_crtcs(struct komeda_kms_dev *kms,
			   struct komeda_dev *mdev)
{
	struct komeda_crtc *crtc;
	struct komeda_pipeline *master;
	char str[16];
	int i;

	kms->n_crtcs = 0;

	for (i = 0; i < mdev->n_pipelines; i++) {
		crtc = &kms->crtcs[kms->n_crtcs];
		master = mdev->pipelines[i];

		crtc->master = master;
		crtc->slave  = komeda_pipeline_get_slave(master);

		if (crtc->slave)
			sprintf(str, "pipe-%d", crtc->slave->id);
		else
			sprintf(str, "None");

		DRM_INFO("CRTC-%d: master(pipe-%d) slave(%s).\n",
			 kms->n_crtcs, master->id, str);

		kms->n_crtcs++;
	}

	return 0;
}

static struct drm_plane *
get_crtc_primary(struct komeda_kms_dev *kms, struct komeda_crtc *crtc)
{
	struct komeda_plane *kplane;
	struct drm_plane *plane;

	drm_for_each_plane(plane, &kms->base) {
		if (plane->type != DRM_PLANE_TYPE_PRIMARY)
			continue;

		kplane = to_kplane(plane);
		/* only master can be primary */
		if (kplane->layer->base.pipeline == crtc->master)
			return plane;
	}

	return NULL;
}

static int komeda_crtc_add(struct komeda_kms_dev *kms,
			   struct komeda_crtc *kcrtc)
{
	struct drm_crtc *crtc = &kcrtc->base;
	int err;

	err = drm_crtc_init_with_planes(&kms->base, crtc,
					get_crtc_primary(kms, kcrtc), NULL,
					&komeda_crtc_funcs, NULL);
	if (err)
		return err;

	drm_crtc_helper_add(crtc, &komeda_crtc_helper_funcs);

	crtc->port = kcrtc->master->of_output_port;

	drm_crtc_enable_color_mgmt(crtc, 0, true, KOMEDA_COLOR_LUT_SIZE);

	return err;
}

int komeda_kms_add_crtcs(struct komeda_kms_dev *kms, struct komeda_dev *mdev)
{
	int i, err;

	for (i = 0; i < kms->n_crtcs; i++) {
		err = komeda_crtc_add(kms, &kms->crtcs[i]);
		if (err)
			return err;
	}

	return 0;
}
