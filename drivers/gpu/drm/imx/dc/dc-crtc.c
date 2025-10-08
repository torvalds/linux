// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "dc-de.h"
#include "dc-drv.h"
#include "dc-kms.h"
#include "dc-pe.h"

#define dc_crtc_dbg(crtc, fmt, ...)					\
do {									\
	struct drm_crtc *_crtc = (crtc);				\
	drm_dbg_kms(_crtc->dev, "[CRTC:%d:%s] " fmt,			\
		    _crtc->base.id, _crtc->name, ##__VA_ARGS__);	\
} while (0)

#define dc_crtc_err(crtc, fmt, ...)					\
do {									\
	struct drm_crtc *_crtc = (crtc);				\
	drm_err(_crtc->dev, "[CRTC:%d:%s] " fmt,			\
		_crtc->base.id, _crtc->name, ##__VA_ARGS__);		\
} while (0)

#define DC_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(c)				\
do {									\
	unsigned long ret;						\
	ret = wait_for_completion_timeout(&dc_crtc->c, HZ);		\
	if (ret == 0)							\
		dc_crtc_err(crtc, "%s: wait for " #c " timeout\n",	\
							__func__);	\
} while (0)

#define DC_CRTC_CHECK_FRAMEGEN_FIFO(fg)					\
do {									\
	struct dc_fg *_fg = (fg);					\
	if (dc_fg_secondary_requests_to_read_empty_fifo(_fg)) {		\
		dc_fg_secondary_clear_channel_status(_fg);		\
		dc_crtc_err(crtc, "%s: FrameGen FIFO empty\n",		\
							__func__);	\
	}								\
} while (0)

#define DC_CRTC_WAIT_FOR_FRAMEGEN_SECONDARY_SYNCUP(fg)			\
do {									\
	if (dc_fg_wait_for_secondary_syncup(fg))			\
		dc_crtc_err(crtc,					\
			"%s: FrameGen secondary channel isn't syncup\n",\
							__func__);	\
} while (0)

static inline struct dc_crtc *to_dc_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct dc_crtc, base);
}

static u32 dc_crtc_get_vblank_counter(struct drm_crtc *crtc)
{
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);

	return dc_fg_get_frame_index(dc_crtc->fg);
}

static int dc_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);

	enable_irq(dc_crtc->irq_dec_framecomplete);

	return 0;
}

static void dc_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);

	/* nosync due to atomic context */
	disable_irq_nosync(dc_crtc->irq_dec_framecomplete);
}

static const struct drm_crtc_funcs dc_crtc_funcs = {
	.reset			= drm_atomic_helper_crtc_reset,
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.get_vblank_counter	= dc_crtc_get_vblank_counter,
	.enable_vblank		= dc_crtc_enable_vblank,
	.disable_vblank		= dc_crtc_disable_vblank,
	.get_vblank_timestamp	= drm_crtc_vblank_helper_get_vblank_timestamp,
};

static void dc_crtc_queue_state_event(struct drm_crtc_state *crtc_state)
{
	struct drm_crtc *crtc = crtc_state->crtc;
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc_state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc));
		WARN_ON(dc_crtc->event);
		dc_crtc->event = crtc_state->event;
		crtc_state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

static inline enum drm_mode_status
dc_crtc_check_clock(struct dc_crtc *dc_crtc, int clk_khz)
{
	return dc_fg_check_clock(dc_crtc->fg, clk_khz);
}

static enum drm_mode_status
dc_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode)
{
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);
	enum drm_mode_status status;

	status = dc_crtc_check_clock(dc_crtc, mode->clock);
	if (status != MODE_OK)
		return status;

	if (mode->crtc_clock > DC_FRAMEGEN_MAX_CLOCK_KHZ)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static int
dc_crtc_atomic_check(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_crtc_state *new_crtc_state =
				drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_display_mode *adj = &new_crtc_state->adjusted_mode;
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);
	enum drm_mode_status status;

	status = dc_crtc_check_clock(dc_crtc, adj->clock);
	if (status != MODE_OK)
		return -EINVAL;

	return 0;
}

static void
dc_crtc_atomic_begin(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_crtc_state *new_crtc_state =
				drm_atomic_get_new_crtc_state(state, crtc);
	struct dc_drm_device *dc_drm = to_dc_drm_device(crtc->dev);
	int idx, ret;

	if (!drm_atomic_crtc_needs_modeset(new_crtc_state) ||
	    !new_crtc_state->active)
		return;

	if (!drm_dev_enter(crtc->dev, &idx))
		return;

	/* request pixel engine power-on when CRTC starts to be active */
	ret = pm_runtime_resume_and_get(dc_drm->pe->dev);
	if (ret)
		dc_crtc_err(crtc, "failed to get DC pixel engine RPM: %d\n",
			    ret);

	drm_dev_exit(idx);
}

static void
dc_crtc_atomic_flush(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_crtc_state =
				drm_atomic_get_old_crtc_state(state, crtc);
	struct drm_crtc_state *new_crtc_state =
				drm_atomic_get_new_crtc_state(state, crtc);
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);
	int idx;

	if (drm_atomic_crtc_needs_modeset(new_crtc_state) ||
	    (!old_crtc_state->active && !new_crtc_state->active))
		return;

	if (!drm_dev_enter(crtc->dev, &idx))
		goto out;

	enable_irq(dc_crtc->irq_ed_cont_shdload);

	/* flush plane update out to display */
	dc_ed_pec_sync_trigger(dc_crtc->ed_cont);

	DC_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(ed_cont_shdload_done);

	disable_irq(dc_crtc->irq_ed_cont_shdload);

	DC_CRTC_CHECK_FRAMEGEN_FIFO(dc_crtc->fg);

	drm_dev_exit(idx);

out:
	dc_crtc_queue_state_event(new_crtc_state);
}

static void
dc_crtc_atomic_enable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_crtc_state *new_crtc_state =
				drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_display_mode *adj = &new_crtc_state->adjusted_mode;
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);
	enum dc_link_id cf_link;
	int idx, ret;

	dc_crtc_dbg(crtc, "mode " DRM_MODE_FMT "\n", DRM_MODE_ARG(adj));

	drm_crtc_vblank_on(crtc);

	if (!drm_dev_enter(crtc->dev, &idx))
		goto out;

	/* request display engine power-on when CRTC is enabled */
	ret = pm_runtime_resume_and_get(dc_crtc->de->dev);
	if (ret < 0)
		dc_crtc_err(crtc, "failed to get DC display engine RPM: %d\n",
			    ret);

	enable_irq(dc_crtc->irq_dec_shdload);
	enable_irq(dc_crtc->irq_ed_cont_shdload);
	enable_irq(dc_crtc->irq_ed_safe_shdload);

	dc_fg_cfg_videomode(dc_crtc->fg, adj);

	dc_cf_framedimensions(dc_crtc->cf_cont,
			      adj->crtc_hdisplay, adj->crtc_vdisplay);
	dc_cf_framedimensions(dc_crtc->cf_safe,
			      adj->crtc_hdisplay, adj->crtc_vdisplay);

	/* constframe in safety stream shows blue frame */
	dc_cf_constantcolor_blue(dc_crtc->cf_safe);
	cf_link = dc_cf_get_link_id(dc_crtc->cf_safe);
	dc_ed_pec_src_sel(dc_crtc->ed_safe, cf_link);

	/* show CRTC background if no plane is enabled */
	if (new_crtc_state->plane_mask == 0) {
		/* constframe in content stream shows black frame */
		dc_cf_constantcolor_black(dc_crtc->cf_cont);

		cf_link = dc_cf_get_link_id(dc_crtc->cf_cont);
		dc_ed_pec_src_sel(dc_crtc->ed_cont, cf_link);
	}

	dc_fg_enable_clock(dc_crtc->fg);
	dc_ed_pec_sync_trigger(dc_crtc->ed_cont);
	dc_ed_pec_sync_trigger(dc_crtc->ed_safe);
	dc_fg_shdtokgen(dc_crtc->fg);
	dc_fg_enable(dc_crtc->fg);

	DC_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(ed_safe_shdload_done);
	DC_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(ed_cont_shdload_done);
	DC_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(dec_shdload_done);

	disable_irq(dc_crtc->irq_ed_safe_shdload);
	disable_irq(dc_crtc->irq_ed_cont_shdload);
	disable_irq(dc_crtc->irq_dec_shdload);

	DC_CRTC_WAIT_FOR_FRAMEGEN_SECONDARY_SYNCUP(dc_crtc->fg);

	DC_CRTC_CHECK_FRAMEGEN_FIFO(dc_crtc->fg);

	drm_dev_exit(idx);

out:
	dc_crtc_queue_state_event(new_crtc_state);
}

static void
dc_crtc_atomic_disable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_crtc_state *new_crtc_state =
				drm_atomic_get_new_crtc_state(state, crtc);
	struct dc_drm_device *dc_drm = to_dc_drm_device(crtc->dev);
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);
	int idx, ret;

	if (!drm_dev_enter(crtc->dev, &idx))
		goto out;

	enable_irq(dc_crtc->irq_dec_seqcomplete);
	dc_fg_disable(dc_crtc->fg);
	DC_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(dec_seqcomplete_done);
	disable_irq(dc_crtc->irq_dec_seqcomplete);

	dc_fg_disable_clock(dc_crtc->fg);

	/* request pixel engine power-off as plane is off too */
	ret = pm_runtime_put(dc_drm->pe->dev);
	if (ret)
		dc_crtc_err(crtc, "failed to put DC pixel engine RPM: %d\n",
			    ret);

	/* request display engine power-off when CRTC is disabled */
	ret = pm_runtime_put(dc_crtc->de->dev);
	if (ret < 0)
		dc_crtc_err(crtc, "failed to put DC display engine RPM: %d\n",
			    ret);

	drm_dev_exit(idx);

out:
	drm_crtc_vblank_off(crtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (new_crtc_state->event && !new_crtc_state->active) {
		drm_crtc_send_vblank_event(crtc, new_crtc_state->event);
		new_crtc_state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

static bool dc_crtc_get_scanout_position(struct drm_crtc *crtc,
					 bool in_vblank_irq,
					 int *vpos, int *hpos,
					 ktime_t *stime, ktime_t *etime,
					 const struct drm_display_mode *mode)
{
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);
	int vdisplay = mode->crtc_vdisplay;
	int vtotal = mode->crtc_vtotal;
	bool reliable;
	int line;
	int idx;

	if (stime)
		*stime = ktime_get();

	if (!drm_dev_enter(crtc->dev, &idx)) {
		reliable = false;
		*vpos = 0;
		*hpos = 0;
		goto out;
	}

	/* line index starts with 0 for the first active output line */
	line = dc_fg_get_line_index(dc_crtc->fg);

	if (line < vdisplay)
		/* active scanout area - positive */
		*vpos = line + 1;
	else
		/* inside vblank - negative */
		*vpos = line - (vtotal - 1);

	*hpos = 0;

	reliable = true;

	drm_dev_exit(idx);
out:
	if (etime)
		*etime = ktime_get();

	return reliable;
}

static const struct drm_crtc_helper_funcs dc_helper_funcs = {
	.mode_valid		= dc_crtc_mode_valid,
	.atomic_check		= dc_crtc_atomic_check,
	.atomic_begin		= dc_crtc_atomic_begin,
	.atomic_flush		= dc_crtc_atomic_flush,
	.atomic_enable		= dc_crtc_atomic_enable,
	.atomic_disable		= dc_crtc_atomic_disable,
	.get_scanout_position	= dc_crtc_get_scanout_position,
};

static irqreturn_t dc_crtc_irq_handler_dec_framecomplete(int irq, void *dev_id)
{
	struct dc_crtc *dc_crtc = dev_id;
	struct drm_crtc *crtc = &dc_crtc->base;
	unsigned long flags;

	drm_crtc_handle_vblank(crtc);

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	if (dc_crtc->event) {
		drm_crtc_send_vblank_event(crtc, dc_crtc->event);
		dc_crtc->event = NULL;
		drm_crtc_vblank_put(crtc);
	}
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t
dc_crtc_irq_handler_dec_seqcomplete_done(int irq, void *dev_id)
{
	struct dc_crtc *dc_crtc = dev_id;

	complete(&dc_crtc->dec_seqcomplete_done);

	return IRQ_HANDLED;
}

static irqreturn_t dc_crtc_irq_handler_dec_shdload_done(int irq, void *dev_id)
{
	struct dc_crtc *dc_crtc = dev_id;

	complete(&dc_crtc->dec_shdload_done);

	return IRQ_HANDLED;
}

static irqreturn_t
dc_crtc_irq_handler_ed_cont_shdload_done(int irq, void *dev_id)
{
	struct dc_crtc *dc_crtc = dev_id;

	complete(&dc_crtc->ed_cont_shdload_done);

	return IRQ_HANDLED;
}

static irqreturn_t
dc_crtc_irq_handler_ed_safe_shdload_done(int irq, void *dev_id)
{
	struct dc_crtc *dc_crtc = dev_id;

	complete(&dc_crtc->ed_safe_shdload_done);

	return IRQ_HANDLED;
}

static int dc_crtc_request_irqs(struct drm_device *drm, struct dc_crtc *dc_crtc)
{
	struct {
		struct device *dev;
		unsigned int irq;
		irqreturn_t (*irq_handler)(int irq, void *dev_id);
	} irqs[DC_CRTC_IRQS] = {
		{
			dc_crtc->de->dev,
			dc_crtc->irq_dec_framecomplete,
			dc_crtc_irq_handler_dec_framecomplete,
		}, {
			dc_crtc->de->dev,
			dc_crtc->irq_dec_seqcomplete,
			dc_crtc_irq_handler_dec_seqcomplete_done,
		}, {
			dc_crtc->de->dev,
			dc_crtc->irq_dec_shdload,
			dc_crtc_irq_handler_dec_shdload_done,
		}, {
			dc_crtc->ed_cont->dev,
			dc_crtc->irq_ed_cont_shdload,
			dc_crtc_irq_handler_ed_cont_shdload_done,
		}, {
			dc_crtc->ed_safe->dev,
			dc_crtc->irq_ed_safe_shdload,
			dc_crtc_irq_handler_ed_safe_shdload_done,
		},
	};
	int i, ret;

	for (i = 0; i < DC_CRTC_IRQS; i++) {
		struct dc_crtc_irq *irq = &dc_crtc->irqs[i];

		ret = devm_request_irq(irqs[i].dev, irqs[i].irq,
				       irqs[i].irq_handler, IRQF_NO_AUTOEN,
				       dev_name(irqs[i].dev), dc_crtc);
		if (ret) {
			dev_err(irqs[i].dev, "failed to request irq(%u): %d\n",
				irqs[i].irq, ret);
			return ret;
		}

		irq->dc_crtc = dc_crtc;
		irq->irq = irqs[i].irq;
	}

	return 0;
}

int dc_crtc_init(struct dc_drm_device *dc_drm, int crtc_index)
{
	struct dc_crtc *dc_crtc = &dc_drm->dc_crtc[crtc_index];
	struct drm_device *drm = &dc_drm->base;
	struct dc_de *de = dc_drm->de[crtc_index];
	struct dc_pe *pe = dc_drm->pe;
	struct dc_plane *dc_primary;
	int ret;

	dc_crtc->de = de;

	init_completion(&dc_crtc->dec_seqcomplete_done);
	init_completion(&dc_crtc->dec_shdload_done);
	init_completion(&dc_crtc->ed_cont_shdload_done);
	init_completion(&dc_crtc->ed_safe_shdload_done);

	dc_crtc->cf_cont = pe->cf_cont[crtc_index];
	dc_crtc->cf_safe = pe->cf_safe[crtc_index];
	dc_crtc->ed_cont = pe->ed_cont[crtc_index];
	dc_crtc->ed_safe = pe->ed_safe[crtc_index];
	dc_crtc->fg = de->fg;

	dc_crtc->irq_dec_framecomplete = de->irq_framecomplete;
	dc_crtc->irq_dec_seqcomplete = de->irq_seqcomplete;
	dc_crtc->irq_dec_shdload = de->irq_shdload;
	dc_crtc->irq_ed_safe_shdload = dc_crtc->ed_safe->irq_shdload;
	dc_crtc->irq_ed_cont_shdload = dc_crtc->ed_cont->irq_shdload;

	dc_primary = &dc_drm->dc_primary[crtc_index];
	ret = dc_plane_init(dc_drm, dc_primary);
	if (ret) {
		dev_err(de->dev, "failed to initialize primary plane: %d\n",
			ret);
		return ret;
	}

	drm_crtc_helper_add(&dc_crtc->base, &dc_helper_funcs);

	ret = drm_crtc_init_with_planes(drm, &dc_crtc->base, &dc_primary->base,
					NULL, &dc_crtc_funcs, NULL);
	if (ret)
		dev_err(de->dev, "failed to add CRTC: %d\n", ret);

	return ret;
}

int dc_crtc_post_init(struct dc_drm_device *dc_drm, int crtc_index)
{
	struct dc_crtc *dc_crtc = &dc_drm->dc_crtc[crtc_index];
	struct drm_device *drm = &dc_drm->base;

	return dc_crtc_request_irqs(drm, dc_crtc);
}
