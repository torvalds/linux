/*
 * drivers/gpu/drm/omapdrm/omap_irq.c
 *
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "omap_drv.h"

static DEFINE_SPINLOCK(list_lock);

/* call with list_lock and dispc runtime held */
static void omap_irq_update(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_drm_irq *irq;
	uint32_t irqmask = priv->irq_mask;

	assert_spin_locked(&list_lock);

	list_for_each_entry(irq, &priv->irq_list, node)
		irqmask |= irq->irqmask;

	DBG("irqmask=%08x", irqmask);

	dispc_write_irqenable(irqmask);
	dispc_read_irqenable();        /* flush posted write */
}

static void omap_irq_register(struct drm_device *dev, struct omap_drm_irq *irq)
{
	struct omap_drm_private *priv = dev->dev_private;
	unsigned long flags;

	dispc_runtime_get();
	spin_lock_irqsave(&list_lock, flags);

	if (!WARN_ON(irq->registered)) {
		irq->registered = true;
		list_add(&irq->node, &priv->irq_list);
		omap_irq_update(dev);
	}

	spin_unlock_irqrestore(&list_lock, flags);
	dispc_runtime_put();
}

static void omap_irq_unregister(struct drm_device *dev,
				struct omap_drm_irq *irq)
{
	unsigned long flags;

	dispc_runtime_get();
	spin_lock_irqsave(&list_lock, flags);

	if (!WARN_ON(!irq->registered)) {
		irq->registered = false;
		list_del(&irq->node);
		omap_irq_update(dev);
	}

	spin_unlock_irqrestore(&list_lock, flags);
	dispc_runtime_put();
}

struct omap_irq_wait {
	struct omap_drm_irq irq;
	int count;
};

static DECLARE_WAIT_QUEUE_HEAD(wait_event);

static void wait_irq(struct omap_drm_irq *irq, uint32_t irqstatus)
{
	struct omap_irq_wait *wait =
			container_of(irq, struct omap_irq_wait, irq);
	wait->count--;
	wake_up_all(&wait_event);
}

struct omap_irq_wait * omap_irq_wait_init(struct drm_device *dev,
		uint32_t irqmask, int count)
{
	struct omap_irq_wait *wait = kzalloc(sizeof(*wait), GFP_KERNEL);
	wait->irq.irq = wait_irq;
	wait->irq.irqmask = irqmask;
	wait->count = count;
	omap_irq_register(dev, &wait->irq);
	return wait;
}

int omap_irq_wait(struct drm_device *dev, struct omap_irq_wait *wait,
		unsigned long timeout)
{
	int ret = wait_event_timeout(wait_event, (wait->count <= 0), timeout);
	omap_irq_unregister(dev, &wait->irq);
	kfree(wait);
	if (ret == 0)
		return -1;
	return 0;
}

/**
 * enable_vblank - enable vblank interrupt events
 * @dev: DRM device
 * @pipe: which irq to enable
 *
 * Enable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 *
 * RETURNS
 * Zero on success, appropriate errno if the given @crtc's vblank
 * interrupt cannot be enabled.
 */
int omap_irq_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc = priv->crtcs[pipe];
	unsigned long flags;

	DBG("dev=%p, crtc=%u", dev, pipe);

	spin_lock_irqsave(&list_lock, flags);
	priv->irq_mask |= pipe2vbl(crtc);
	omap_irq_update(dev);
	spin_unlock_irqrestore(&list_lock, flags);

	return 0;
}

/**
 * disable_vblank - disable vblank interrupt events
 * @dev: DRM device
 * @pipe: which irq to enable
 *
 * Disable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 */
void omap_irq_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc = priv->crtcs[pipe];
	unsigned long flags;

	DBG("dev=%p, crtc=%u", dev, pipe);

	spin_lock_irqsave(&list_lock, flags);
	priv->irq_mask &= ~pipe2vbl(crtc);
	omap_irq_update(dev);
	spin_unlock_irqrestore(&list_lock, flags);
}

static void omap_irq_fifo_underflow(struct omap_drm_private *priv,
				    u32 irqstatus)
{
	static DEFINE_RATELIMIT_STATE(_rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	static const struct {
		const char *name;
		u32 mask;
	} sources[] = {
		{ "gfx", DISPC_IRQ_GFX_FIFO_UNDERFLOW },
		{ "vid1", DISPC_IRQ_VID1_FIFO_UNDERFLOW },
		{ "vid2", DISPC_IRQ_VID2_FIFO_UNDERFLOW },
		{ "vid3", DISPC_IRQ_VID3_FIFO_UNDERFLOW },
	};

	const u32 mask = DISPC_IRQ_GFX_FIFO_UNDERFLOW
		       | DISPC_IRQ_VID1_FIFO_UNDERFLOW
		       | DISPC_IRQ_VID2_FIFO_UNDERFLOW
		       | DISPC_IRQ_VID3_FIFO_UNDERFLOW;
	unsigned int i;

	spin_lock(&list_lock);
	irqstatus &= priv->irq_mask & mask;
	spin_unlock(&list_lock);

	if (!irqstatus)
		return;

	if (!__ratelimit(&_rs))
		return;

	DRM_ERROR("FIFO underflow on ");

	for (i = 0; i < ARRAY_SIZE(sources); ++i) {
		if (sources[i].mask & irqstatus)
			pr_cont("%s ", sources[i].name);
	}

	pr_cont("(0x%08x)\n", irqstatus);
}

static void omap_irq_ocp_error_handler(u32 irqstatus)
{
	if (!(irqstatus & DISPC_IRQ_OCP_ERR))
		return;

	DRM_ERROR("OCP error\n");
}

static irqreturn_t omap_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_drm_irq *handler, *n;
	unsigned long flags;
	unsigned int id;
	u32 irqstatus;

	irqstatus = dispc_read_irqstatus();
	dispc_clear_irqstatus(irqstatus);
	dispc_read_irqstatus();        /* flush posted write */

	VERB("irqs: %08x", irqstatus);

	for (id = 0; id < priv->num_crtcs; id++) {
		struct drm_crtc *crtc = priv->crtcs[id];
		enum omap_channel channel = omap_crtc_channel(crtc);

		if (irqstatus & pipe2vbl(crtc)) {
			drm_handle_vblank(dev, id);
			omap_crtc_vblank_irq(crtc);
		}

		if (irqstatus & dispc_mgr_get_sync_lost_irq(channel))
			omap_crtc_error_irq(crtc, irqstatus);
	}

	omap_irq_ocp_error_handler(irqstatus);
	omap_irq_fifo_underflow(priv, irqstatus);

	spin_lock_irqsave(&list_lock, flags);
	list_for_each_entry_safe(handler, n, &priv->irq_list, node) {
		if (handler->irqmask & irqstatus) {
			spin_unlock_irqrestore(&list_lock, flags);
			handler->irq(handler, handler->irqmask & irqstatus);
			spin_lock_irqsave(&list_lock, flags);
		}
	}
	spin_unlock_irqrestore(&list_lock, flags);

	return IRQ_HANDLED;
}

static const u32 omap_underflow_irqs[] = {
	[OMAP_DSS_GFX] = DISPC_IRQ_GFX_FIFO_UNDERFLOW,
	[OMAP_DSS_VIDEO1] = DISPC_IRQ_VID1_FIFO_UNDERFLOW,
	[OMAP_DSS_VIDEO2] = DISPC_IRQ_VID2_FIFO_UNDERFLOW,
	[OMAP_DSS_VIDEO3] = DISPC_IRQ_VID3_FIFO_UNDERFLOW,
};

/*
 * We need a special version, instead of just using drm_irq_install(),
 * because we need to register the irq via omapdss.  Once omapdss and
 * omapdrm are merged together we can assign the dispc hwmod data to
 * ourselves and drop these and just use drm_irq_{install,uninstall}()
 */

int omap_drm_irq_install(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	unsigned int num_mgrs = dss_feat_get_num_mgrs();
	unsigned int max_planes;
	unsigned int i;
	int ret;

	INIT_LIST_HEAD(&priv->irq_list);

	priv->irq_mask = DISPC_IRQ_OCP_ERR;

	max_planes = min(ARRAY_SIZE(priv->planes),
			 ARRAY_SIZE(omap_underflow_irqs));
	for (i = 0; i < max_planes; ++i) {
		if (priv->planes[i])
			priv->irq_mask |= omap_underflow_irqs[i];
	}

	for (i = 0; i < num_mgrs; ++i)
		priv->irq_mask |= dispc_mgr_get_sync_lost_irq(i);

	dispc_runtime_get();
	dispc_clear_irqstatus(0xffffffff);
	dispc_runtime_put();

	ret = dispc_request_irq(omap_irq_handler, dev);
	if (ret < 0)
		return ret;

	dev->irq_enabled = true;

	return 0;
}

void omap_drm_irq_uninstall(struct drm_device *dev)
{
	unsigned long irqflags;
	int i;

	if (!dev->irq_enabled)
		return;

	dev->irq_enabled = false;

	/* Wake up any waiters so they don't hang. */
	if (dev->num_crtcs) {
		spin_lock_irqsave(&dev->vbl_lock, irqflags);
		for (i = 0; i < dev->num_crtcs; i++) {
			wake_up(&dev->vblank[i].queue);
			dev->vblank[i].enabled = false;
			dev->vblank[i].last =
				dev->driver->get_vblank_counter(dev, i);
		}
		spin_unlock_irqrestore(&dev->vbl_lock, irqflags);
	}

	dispc_free_irq(dev);
}
