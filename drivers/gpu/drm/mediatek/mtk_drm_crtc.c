// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk-cmdq.h>
#include <linux/soc/mediatek/mtk-mmsys.h>
#include <linux/soc/mediatek/mtk-mutex.h>

#include <asm/barrier.h>
#include <soc/mediatek/smi.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "mtk_drm_drv.h"
#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_gem.h"
#include "mtk_drm_plane.h"

/*
 * struct mtk_drm_crtc - MediaTek specific crtc structure.
 * @base: crtc object.
 * @enabled: records whether crtc_enable succeeded
 * @planes: array of 4 drm_plane structures, one for each overlay plane
 * @pending_planes: whether any plane has pending changes to be applied
 * @mmsys_dev: pointer to the mmsys device for configuration registers
 * @mutex: handle to one of the ten disp_mutex streams
 * @ddp_comp_nr: number of components in ddp_comp
 * @ddp_comp: array of pointers the mtk_ddp_comp structures used by this crtc
 *
 * TODO: Needs update: this header is missing a bunch of member descriptions.
 */
struct mtk_drm_crtc {
	struct drm_crtc			base;
	bool				enabled;

	bool				pending_needs_vblank;
	struct drm_pending_vblank_event	*event;

	struct drm_plane		*planes;
	unsigned int			layer_nr;
	bool				pending_planes;
	bool				pending_async_planes;

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	struct cmdq_client		*cmdq_client;
	u32				cmdq_event;
#endif

	struct device			*mmsys_dev;
	struct mtk_mutex		*mutex;
	unsigned int			ddp_comp_nr;
	struct mtk_ddp_comp		**ddp_comp;

	/* lock for display hardware access */
	struct mutex			hw_lock;
	bool				config_updating;
};

struct mtk_crtc_state {
	struct drm_crtc_state		base;

	bool				pending_config;
	unsigned int			pending_width;
	unsigned int			pending_height;
	unsigned int			pending_vrefresh;
};

static inline struct mtk_drm_crtc *to_mtk_crtc(struct drm_crtc *c)
{
	return container_of(c, struct mtk_drm_crtc, base);
}

static inline struct mtk_crtc_state *to_mtk_crtc_state(struct drm_crtc_state *s)
{
	return container_of(s, struct mtk_crtc_state, base);
}

static void mtk_drm_crtc_finish_page_flip(struct mtk_drm_crtc *mtk_crtc)
{
	struct drm_crtc *crtc = &mtk_crtc->base;
	unsigned long flags;

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	drm_crtc_send_vblank_event(crtc, mtk_crtc->event);
	drm_crtc_vblank_put(crtc);
	mtk_crtc->event = NULL;
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}

static void mtk_drm_finish_page_flip(struct mtk_drm_crtc *mtk_crtc)
{
	drm_crtc_handle_vblank(&mtk_crtc->base);
	if (!mtk_crtc->config_updating && mtk_crtc->pending_needs_vblank) {
		mtk_drm_crtc_finish_page_flip(mtk_crtc);
		mtk_crtc->pending_needs_vblank = false;
	}
}

static void mtk_drm_crtc_destroy(struct drm_crtc *crtc)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);

	mtk_mutex_put(mtk_crtc->mutex);

	drm_crtc_cleanup(crtc);
}

static void mtk_drm_crtc_reset(struct drm_crtc *crtc)
{
	struct mtk_crtc_state *state;

	if (crtc->state)
		__drm_atomic_helper_crtc_destroy_state(crtc->state);

	kfree(to_mtk_crtc_state(crtc->state));
	crtc->state = NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_crtc_reset(crtc, &state->base);
}

static struct drm_crtc_state *mtk_drm_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct mtk_crtc_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &state->base);

	WARN_ON(state->base.crtc != crtc);
	state->base.crtc = crtc;

	return &state->base;
}

static void mtk_drm_crtc_destroy_state(struct drm_crtc *crtc,
				       struct drm_crtc_state *state)
{
	__drm_atomic_helper_crtc_destroy_state(state);
	kfree(to_mtk_crtc_state(state));
}

static bool mtk_drm_crtc_mode_fixup(struct drm_crtc *crtc,
				    const struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode)
{
	/* Nothing to do here, but this callback is mandatory. */
	return true;
}

static void mtk_drm_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct mtk_crtc_state *state = to_mtk_crtc_state(crtc->state);

	state->pending_width = crtc->mode.hdisplay;
	state->pending_height = crtc->mode.vdisplay;
	state->pending_vrefresh = drm_mode_vrefresh(&crtc->mode);
	wmb();	/* Make sure the above parameters are set before update */
	state->pending_config = true;
}

static int mtk_crtc_ddp_clk_enable(struct mtk_drm_crtc *mtk_crtc)
{
	int ret;
	int i;

	for (i = 0; i < mtk_crtc->ddp_comp_nr; i++) {
		ret = mtk_ddp_comp_clk_enable(mtk_crtc->ddp_comp[i]);
		if (ret) {
			DRM_ERROR("Failed to enable clock %d: %d\n", i, ret);
			goto err;
		}
	}

	return 0;
err:
	while (--i >= 0)
		mtk_ddp_comp_clk_disable(mtk_crtc->ddp_comp[i]);
	return ret;
}

static void mtk_crtc_ddp_clk_disable(struct mtk_drm_crtc *mtk_crtc)
{
	int i;

	for (i = 0; i < mtk_crtc->ddp_comp_nr; i++)
		mtk_ddp_comp_clk_disable(mtk_crtc->ddp_comp[i]);
}

static
struct mtk_ddp_comp *mtk_drm_ddp_comp_for_plane(struct drm_crtc *crtc,
						struct drm_plane *plane,
						unsigned int *local_layer)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_ddp_comp *comp;
	int i, count = 0;
	unsigned int local_index = plane - mtk_crtc->planes;

	for (i = 0; i < mtk_crtc->ddp_comp_nr; i++) {
		comp = mtk_crtc->ddp_comp[i];
		if (local_index < (count + mtk_ddp_comp_layer_nr(comp))) {
			*local_layer = local_index - count;
			return comp;
		}
		count += mtk_ddp_comp_layer_nr(comp);
	}

	WARN(1, "Failed to find component for plane %d\n", plane->index);
	return NULL;
}

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
static void ddp_cmdq_cb(struct cmdq_cb_data data)
{
	cmdq_pkt_destroy(data.data);
}
#endif

static int mtk_crtc_ddp_hw_init(struct mtk_drm_crtc *mtk_crtc)
{
	struct drm_crtc *crtc = &mtk_crtc->base;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_connector_list_iter conn_iter;
	unsigned int width, height, vrefresh, bpc = MTK_MAX_BPC;
	int ret;
	int i;

	if (WARN_ON(!crtc->state))
		return -EINVAL;

	width = crtc->state->adjusted_mode.hdisplay;
	height = crtc->state->adjusted_mode.vdisplay;
	vrefresh = drm_mode_vrefresh(&crtc->state->adjusted_mode);

	drm_for_each_encoder(encoder, crtc->dev) {
		if (encoder->crtc != crtc)
			continue;

		drm_connector_list_iter_begin(crtc->dev, &conn_iter);
		drm_for_each_connector_iter(connector, &conn_iter) {
			if (connector->encoder != encoder)
				continue;
			if (connector->display_info.bpc != 0 &&
			    bpc > connector->display_info.bpc)
				bpc = connector->display_info.bpc;
		}
		drm_connector_list_iter_end(&conn_iter);
	}

	ret = pm_runtime_resume_and_get(crtc->dev->dev);
	if (ret < 0) {
		DRM_ERROR("Failed to enable power domain: %d\n", ret);
		return ret;
	}

	ret = mtk_mutex_prepare(mtk_crtc->mutex);
	if (ret < 0) {
		DRM_ERROR("Failed to enable mutex clock: %d\n", ret);
		goto err_pm_runtime_put;
	}

	ret = mtk_crtc_ddp_clk_enable(mtk_crtc);
	if (ret < 0) {
		DRM_ERROR("Failed to enable component clocks: %d\n", ret);
		goto err_mutex_unprepare;
	}

	for (i = 0; i < mtk_crtc->ddp_comp_nr - 1; i++) {
		mtk_mmsys_ddp_connect(mtk_crtc->mmsys_dev,
				      mtk_crtc->ddp_comp[i]->id,
				      mtk_crtc->ddp_comp[i + 1]->id);
		mtk_mutex_add_comp(mtk_crtc->mutex,
					mtk_crtc->ddp_comp[i]->id);
	}
	mtk_mutex_add_comp(mtk_crtc->mutex, mtk_crtc->ddp_comp[i]->id);
	mtk_mutex_enable(mtk_crtc->mutex);

	for (i = 0; i < mtk_crtc->ddp_comp_nr; i++) {
		struct mtk_ddp_comp *comp = mtk_crtc->ddp_comp[i];

		if (i == 1)
			mtk_ddp_comp_bgclr_in_on(comp);

		mtk_ddp_comp_config(comp, width, height, vrefresh, bpc, NULL);
		mtk_ddp_comp_start(comp);
	}

	/* Initially configure all planes */
	for (i = 0; i < mtk_crtc->layer_nr; i++) {
		struct drm_plane *plane = &mtk_crtc->planes[i];
		struct mtk_plane_state *plane_state;
		struct mtk_ddp_comp *comp;
		unsigned int local_layer;

		plane_state = to_mtk_plane_state(plane->state);
		comp = mtk_drm_ddp_comp_for_plane(crtc, plane, &local_layer);
		if (comp)
			mtk_ddp_comp_layer_config(comp, local_layer,
						  plane_state, NULL);
	}

	return 0;

err_mutex_unprepare:
	mtk_mutex_unprepare(mtk_crtc->mutex);
err_pm_runtime_put:
	pm_runtime_put(crtc->dev->dev);
	return ret;
}

static void mtk_crtc_ddp_hw_fini(struct mtk_drm_crtc *mtk_crtc)
{
	struct drm_device *drm = mtk_crtc->base.dev;
	struct drm_crtc *crtc = &mtk_crtc->base;
	int i;

	for (i = 0; i < mtk_crtc->ddp_comp_nr; i++) {
		mtk_ddp_comp_stop(mtk_crtc->ddp_comp[i]);
		if (i == 1)
			mtk_ddp_comp_bgclr_in_off(mtk_crtc->ddp_comp[i]);
	}

	for (i = 0; i < mtk_crtc->ddp_comp_nr; i++)
		mtk_mutex_remove_comp(mtk_crtc->mutex,
					   mtk_crtc->ddp_comp[i]->id);
	mtk_mutex_disable(mtk_crtc->mutex);
	for (i = 0; i < mtk_crtc->ddp_comp_nr - 1; i++) {
		mtk_mmsys_ddp_disconnect(mtk_crtc->mmsys_dev,
					 mtk_crtc->ddp_comp[i]->id,
					 mtk_crtc->ddp_comp[i + 1]->id);
		mtk_mutex_remove_comp(mtk_crtc->mutex,
					   mtk_crtc->ddp_comp[i]->id);
	}
	mtk_mutex_remove_comp(mtk_crtc->mutex, mtk_crtc->ddp_comp[i]->id);
	mtk_crtc_ddp_clk_disable(mtk_crtc);
	mtk_mutex_unprepare(mtk_crtc->mutex);

	pm_runtime_put(drm->dev);

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static void mtk_crtc_ddp_config(struct drm_crtc *crtc,
				struct cmdq_pkt *cmdq_handle)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_crtc_state *state = to_mtk_crtc_state(mtk_crtc->base.state);
	struct mtk_ddp_comp *comp = mtk_crtc->ddp_comp[0];
	unsigned int i;
	unsigned int local_layer;

	/*
	 * TODO: instead of updating the registers here, we should prepare
	 * working registers in atomic_commit and let the hardware command
	 * queue update module registers on vblank.
	 */
	if (state->pending_config) {
		mtk_ddp_comp_config(comp, state->pending_width,
				    state->pending_height,
				    state->pending_vrefresh, 0,
				    cmdq_handle);

		state->pending_config = false;
	}

	if (mtk_crtc->pending_planes) {
		for (i = 0; i < mtk_crtc->layer_nr; i++) {
			struct drm_plane *plane = &mtk_crtc->planes[i];
			struct mtk_plane_state *plane_state;

			plane_state = to_mtk_plane_state(plane->state);

			if (!plane_state->pending.config)
				continue;

			comp = mtk_drm_ddp_comp_for_plane(crtc, plane,
							  &local_layer);

			if (comp)
				mtk_ddp_comp_layer_config(comp, local_layer,
							  plane_state,
							  cmdq_handle);
			plane_state->pending.config = false;
		}
		mtk_crtc->pending_planes = false;
	}

	if (mtk_crtc->pending_async_planes) {
		for (i = 0; i < mtk_crtc->layer_nr; i++) {
			struct drm_plane *plane = &mtk_crtc->planes[i];
			struct mtk_plane_state *plane_state;

			plane_state = to_mtk_plane_state(plane->state);

			if (!plane_state->pending.async_config)
				continue;

			comp = mtk_drm_ddp_comp_for_plane(crtc, plane,
							  &local_layer);

			if (comp)
				mtk_ddp_comp_layer_config(comp, local_layer,
							  plane_state,
							  cmdq_handle);
			plane_state->pending.async_config = false;
		}
		mtk_crtc->pending_async_planes = false;
	}
}

static void mtk_drm_crtc_update_config(struct mtk_drm_crtc *mtk_crtc,
				       bool needs_vblank)
{
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	struct cmdq_pkt *cmdq_handle;
#endif
	struct drm_crtc *crtc = &mtk_crtc->base;
	struct mtk_drm_private *priv = crtc->dev->dev_private;
	unsigned int pending_planes = 0, pending_async_planes = 0;
	int i;

	mutex_lock(&mtk_crtc->hw_lock);
	mtk_crtc->config_updating = true;
	if (needs_vblank)
		mtk_crtc->pending_needs_vblank = true;

	for (i = 0; i < mtk_crtc->layer_nr; i++) {
		struct drm_plane *plane = &mtk_crtc->planes[i];
		struct mtk_plane_state *plane_state;

		plane_state = to_mtk_plane_state(plane->state);
		if (plane_state->pending.dirty) {
			plane_state->pending.config = true;
			plane_state->pending.dirty = false;
			pending_planes |= BIT(i);
		} else if (plane_state->pending.async_dirty) {
			plane_state->pending.async_config = true;
			plane_state->pending.async_dirty = false;
			pending_async_planes |= BIT(i);
		}
	}
	if (pending_planes)
		mtk_crtc->pending_planes = true;
	if (pending_async_planes)
		mtk_crtc->pending_async_planes = true;

	if (priv->data->shadow_register) {
		mtk_mutex_acquire(mtk_crtc->mutex);
		mtk_crtc_ddp_config(crtc, NULL);
		mtk_mutex_release(mtk_crtc->mutex);
	}
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (mtk_crtc->cmdq_client) {
		mbox_flush(mtk_crtc->cmdq_client->chan, 2000);
		cmdq_handle = cmdq_pkt_create(mtk_crtc->cmdq_client, PAGE_SIZE);
		cmdq_pkt_clear_event(cmdq_handle, mtk_crtc->cmdq_event);
		cmdq_pkt_wfe(cmdq_handle, mtk_crtc->cmdq_event, false);
		mtk_crtc_ddp_config(crtc, cmdq_handle);
		cmdq_pkt_finalize(cmdq_handle);
		cmdq_pkt_flush_async(cmdq_handle, ddp_cmdq_cb, cmdq_handle);
	}
#endif
	mtk_crtc->config_updating = false;
	mutex_unlock(&mtk_crtc->hw_lock);
}

static void mtk_crtc_ddp_irq(void *data)
{
	struct drm_crtc *crtc = data;
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_drm_private *priv = crtc->dev->dev_private;

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (!priv->data->shadow_register && !mtk_crtc->cmdq_client)
#else
	if (!priv->data->shadow_register)
#endif
		mtk_crtc_ddp_config(crtc, NULL);

	mtk_drm_finish_page_flip(mtk_crtc);
}

static int mtk_drm_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_ddp_comp *comp = mtk_crtc->ddp_comp[0];

	mtk_ddp_comp_enable_vblank(comp, mtk_crtc_ddp_irq, &mtk_crtc->base);

	return 0;
}

static void mtk_drm_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_ddp_comp *comp = mtk_crtc->ddp_comp[0];

	mtk_ddp_comp_disable_vblank(comp);
}

int mtk_drm_crtc_plane_check(struct drm_crtc *crtc, struct drm_plane *plane,
			     struct mtk_plane_state *state)
{
	unsigned int local_layer;
	struct mtk_ddp_comp *comp;

	comp = mtk_drm_ddp_comp_for_plane(crtc, plane, &local_layer);
	if (comp)
		return mtk_ddp_comp_layer_check(comp, local_layer, state);
	return 0;
}

void mtk_drm_crtc_async_update(struct drm_crtc *crtc, struct drm_plane *plane,
			       struct drm_atomic_state *state)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	const struct drm_plane_helper_funcs *plane_helper_funcs =
			plane->helper_private;

	if (!mtk_crtc->enabled)
		return;

	plane_helper_funcs->atomic_update(plane, state);
	mtk_drm_crtc_update_config(mtk_crtc, false);
}

static void mtk_drm_crtc_atomic_enable(struct drm_crtc *crtc,
				       struct drm_atomic_state *state)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_ddp_comp *comp = mtk_crtc->ddp_comp[0];
	int ret;

	DRM_DEBUG_DRIVER("%s %d\n", __func__, crtc->base.id);

	ret = mtk_smi_larb_get(comp->larb_dev);
	if (ret) {
		DRM_ERROR("Failed to get larb: %d\n", ret);
		return;
	}

	ret = mtk_crtc_ddp_hw_init(mtk_crtc);
	if (ret) {
		mtk_smi_larb_put(comp->larb_dev);
		return;
	}

	drm_crtc_vblank_on(crtc);
	mtk_crtc->enabled = true;
}

static void mtk_drm_crtc_atomic_disable(struct drm_crtc *crtc,
					struct drm_atomic_state *state)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_ddp_comp *comp = mtk_crtc->ddp_comp[0];
	int i;

	DRM_DEBUG_DRIVER("%s %d\n", __func__, crtc->base.id);
	if (!mtk_crtc->enabled)
		return;

	/* Set all pending plane state to disabled */
	for (i = 0; i < mtk_crtc->layer_nr; i++) {
		struct drm_plane *plane = &mtk_crtc->planes[i];
		struct mtk_plane_state *plane_state;

		plane_state = to_mtk_plane_state(plane->state);
		plane_state->pending.enable = false;
		plane_state->pending.config = true;
	}
	mtk_crtc->pending_planes = true;

	mtk_drm_crtc_update_config(mtk_crtc, false);
	/* Wait for planes to be disabled */
	drm_crtc_wait_one_vblank(crtc);

	drm_crtc_vblank_off(crtc);
	mtk_crtc_ddp_hw_fini(mtk_crtc);
	mtk_smi_larb_put(comp->larb_dev);

	mtk_crtc->enabled = false;
}

static void mtk_drm_crtc_atomic_begin(struct drm_crtc *crtc,
				      struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state,
									  crtc);
	struct mtk_crtc_state *mtk_crtc_state = to_mtk_crtc_state(crtc_state);
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);

	if (mtk_crtc->event && mtk_crtc_state->base.event)
		DRM_ERROR("new event while there is still a pending event\n");

	if (mtk_crtc_state->base.event) {
		mtk_crtc_state->base.event->pipe = drm_crtc_index(crtc);
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		mtk_crtc->event = mtk_crtc_state->base.event;
		mtk_crtc_state->base.event = NULL;
	}
}

static void mtk_drm_crtc_atomic_flush(struct drm_crtc *crtc,
				      struct drm_atomic_state *state)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	int i;

	if (crtc->state->color_mgmt_changed)
		for (i = 0; i < mtk_crtc->ddp_comp_nr; i++) {
			mtk_ddp_gamma_set(mtk_crtc->ddp_comp[i], crtc->state);
			mtk_ddp_ctm_set(mtk_crtc->ddp_comp[i], crtc->state);
		}
	mtk_drm_crtc_update_config(mtk_crtc, !!mtk_crtc->event);
}

static const struct drm_crtc_funcs mtk_crtc_funcs = {
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.destroy		= mtk_drm_crtc_destroy,
	.reset			= mtk_drm_crtc_reset,
	.atomic_duplicate_state	= mtk_drm_crtc_duplicate_state,
	.atomic_destroy_state	= mtk_drm_crtc_destroy_state,
	.enable_vblank		= mtk_drm_crtc_enable_vblank,
	.disable_vblank		= mtk_drm_crtc_disable_vblank,
};

static const struct drm_crtc_helper_funcs mtk_crtc_helper_funcs = {
	.mode_fixup	= mtk_drm_crtc_mode_fixup,
	.mode_set_nofb	= mtk_drm_crtc_mode_set_nofb,
	.atomic_begin	= mtk_drm_crtc_atomic_begin,
	.atomic_flush	= mtk_drm_crtc_atomic_flush,
	.atomic_enable	= mtk_drm_crtc_atomic_enable,
	.atomic_disable	= mtk_drm_crtc_atomic_disable,
};

static int mtk_drm_crtc_init(struct drm_device *drm,
			     struct mtk_drm_crtc *mtk_crtc,
			     unsigned int pipe)
{
	struct drm_plane *primary = NULL;
	struct drm_plane *cursor = NULL;
	int i, ret;

	for (i = 0; i < mtk_crtc->layer_nr; i++) {
		if (mtk_crtc->planes[i].type == DRM_PLANE_TYPE_PRIMARY)
			primary = &mtk_crtc->planes[i];
		else if (mtk_crtc->planes[i].type == DRM_PLANE_TYPE_CURSOR)
			cursor = &mtk_crtc->planes[i];
	}

	ret = drm_crtc_init_with_planes(drm, &mtk_crtc->base, primary, cursor,
					&mtk_crtc_funcs, NULL);
	if (ret)
		goto err_cleanup_crtc;

	drm_crtc_helper_add(&mtk_crtc->base, &mtk_crtc_helper_funcs);

	return 0;

err_cleanup_crtc:
	drm_crtc_cleanup(&mtk_crtc->base);
	return ret;
}

static int mtk_drm_crtc_num_comp_planes(struct mtk_drm_crtc *mtk_crtc,
					int comp_idx)
{
	struct mtk_ddp_comp *comp;

	if (comp_idx > 1)
		return 0;

	comp = mtk_crtc->ddp_comp[comp_idx];
	if (!comp->funcs)
		return 0;

	if (comp_idx == 1 && !comp->funcs->bgclr_in_on)
		return 0;

	return mtk_ddp_comp_layer_nr(comp);
}

static inline
enum drm_plane_type mtk_drm_crtc_plane_type(unsigned int plane_idx,
					    unsigned int num_planes)
{
	if (plane_idx == 0)
		return DRM_PLANE_TYPE_PRIMARY;
	else if (plane_idx == (num_planes - 1))
		return DRM_PLANE_TYPE_CURSOR;
	else
		return DRM_PLANE_TYPE_OVERLAY;

}

static int mtk_drm_crtc_init_comp_planes(struct drm_device *drm_dev,
					 struct mtk_drm_crtc *mtk_crtc,
					 int comp_idx, int pipe)
{
	int num_planes = mtk_drm_crtc_num_comp_planes(mtk_crtc, comp_idx);
	struct mtk_ddp_comp *comp = mtk_crtc->ddp_comp[comp_idx];
	int i, ret;

	for (i = 0; i < num_planes; i++) {
		ret = mtk_plane_init(drm_dev,
				&mtk_crtc->planes[mtk_crtc->layer_nr],
				BIT(pipe),
				mtk_drm_crtc_plane_type(mtk_crtc->layer_nr,
							num_planes),
				mtk_ddp_comp_supported_rotations(comp));
		if (ret)
			return ret;

		mtk_crtc->layer_nr++;
	}
	return 0;
}

int mtk_drm_crtc_create(struct drm_device *drm_dev,
			const enum mtk_ddp_comp_id *path, unsigned int path_len)
{
	struct mtk_drm_private *priv = drm_dev->dev_private;
	struct device *dev = drm_dev->dev;
	struct mtk_drm_crtc *mtk_crtc;
	unsigned int num_comp_planes = 0;
	int pipe = priv->num_pipes;
	int ret;
	int i;
	bool has_ctm = false;
	uint gamma_lut_size = 0;

	if (!path)
		return 0;

	for (i = 0; i < path_len; i++) {
		enum mtk_ddp_comp_id comp_id = path[i];
		struct device_node *node;

		node = priv->comp_node[comp_id];
		if (!node) {
			dev_info(dev,
				 "Not creating crtc %d because component %d is disabled or missing\n",
				 pipe, comp_id);
			return 0;
		}
	}

	mtk_crtc = devm_kzalloc(dev, sizeof(*mtk_crtc), GFP_KERNEL);
	if (!mtk_crtc)
		return -ENOMEM;

	mtk_crtc->mmsys_dev = priv->mmsys_dev;
	mtk_crtc->ddp_comp_nr = path_len;
	mtk_crtc->ddp_comp = devm_kmalloc_array(dev, mtk_crtc->ddp_comp_nr,
						sizeof(*mtk_crtc->ddp_comp),
						GFP_KERNEL);
	if (!mtk_crtc->ddp_comp)
		return -ENOMEM;

	mtk_crtc->mutex = mtk_mutex_get(priv->mutex_dev);
	if (IS_ERR(mtk_crtc->mutex)) {
		ret = PTR_ERR(mtk_crtc->mutex);
		dev_err(dev, "Failed to get mutex: %d\n", ret);
		return ret;
	}

	for (i = 0; i < mtk_crtc->ddp_comp_nr; i++) {
		enum mtk_ddp_comp_id comp_id = path[i];
		struct mtk_ddp_comp *comp;
		struct device_node *node;

		node = priv->comp_node[comp_id];
		comp = &priv->ddp_comp[comp_id];
		if (!comp) {
			dev_err(dev, "Component %pOF not initialized\n", node);
			ret = -ENODEV;
			return ret;
		}

		mtk_crtc->ddp_comp[i] = comp;

		if (comp->funcs) {
			if (comp->funcs->gamma_set)
				gamma_lut_size = MTK_LUT_SIZE;

			if (comp->funcs->ctm_set)
				has_ctm = true;
		}
	}

	for (i = 0; i < mtk_crtc->ddp_comp_nr; i++)
		num_comp_planes += mtk_drm_crtc_num_comp_planes(mtk_crtc, i);

	mtk_crtc->planes = devm_kcalloc(dev, num_comp_planes,
					sizeof(struct drm_plane), GFP_KERNEL);

	for (i = 0; i < mtk_crtc->ddp_comp_nr; i++) {
		ret = mtk_drm_crtc_init_comp_planes(drm_dev, mtk_crtc, i,
						    pipe);
		if (ret)
			return ret;
	}

	ret = mtk_drm_crtc_init(drm_dev, mtk_crtc, pipe);
	if (ret < 0)
		return ret;

	if (gamma_lut_size)
		drm_mode_crtc_set_gamma_size(&mtk_crtc->base, gamma_lut_size);
	drm_crtc_enable_color_mgmt(&mtk_crtc->base, 0, has_ctm, gamma_lut_size);
	priv->num_pipes++;
	mutex_init(&mtk_crtc->hw_lock);

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	mtk_crtc->cmdq_client =
			cmdq_mbox_create(mtk_crtc->mmsys_dev,
					 drm_crtc_index(&mtk_crtc->base));
	if (IS_ERR(mtk_crtc->cmdq_client)) {
		dev_dbg(dev, "mtk_crtc %d failed to create mailbox client, writing register by CPU now\n",
			drm_crtc_index(&mtk_crtc->base));
		mtk_crtc->cmdq_client = NULL;
	}

	if (mtk_crtc->cmdq_client) {
		ret = of_property_read_u32_index(priv->mutex_node,
						 "mediatek,gce-events",
						 drm_crtc_index(&mtk_crtc->base),
						 &mtk_crtc->cmdq_event);
		if (ret) {
			dev_dbg(dev, "mtk_crtc %d failed to get mediatek,gce-events property\n",
				drm_crtc_index(&mtk_crtc->base));
			cmdq_mbox_destroy(mtk_crtc->cmdq_client);
			mtk_crtc->cmdq_client = NULL;
		}
	}
#endif
	return 0;
}
