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

static void omap_irq_error_handler(struct omap_drm_irq *irq,
		uint32_t irqstatus)
{
	DRM_ERROR("errors: %08x\n", irqstatus);
}

/* call with list_lock and dispc runtime held */
static void omap_irq_update(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_drm_irq *irq;
	uint32_t irqmask = priv->vblank_mask;

	BUG_ON(!spin_is_locked(&list_lock));

	list_for_each_entry(irq, &priv->irq_list, node)
		irqmask |= irq->irqmask;

	DBG("irqmask=%08x", irqmask);

	dispc_write_irqenable(irqmask);
	dispc_read_irqenable();        /* flush posted write */
}

void omap_irq_register(struct drm_device *dev, struct omap_drm_irq *irq)
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

void omap_irq_unregister(struct drm_device *dev, struct omap_drm_irq *irq)
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
 * @crtc: which irq to enable
 *
 * Enable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 *
 * RETURNS
 * Zero on success, appropriate errno if the given @crtc's vblank
 * interrupt cannot be enabled.
 */
int omap_irq_enable_vblank(struct drm_device *dev, int crtc_id)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc = priv->crtcs[crtc_id];
	unsigned long flags;

	DBG("dev=%p, crtc=%d", dev, crtc_id);

	dispc_runtime_get();
	spin_lock_irqsave(&list_lock, flags);
	priv->vblank_mask |= pipe2vbl(crtc);
	omap_irq_update(dev);
	spin_unlock_irqrestore(&list_lock, flags);
	dispc_runtime_put();

	return 0;
}

/**
 * disable_vblank - disable vblank interrupt events
 * @dev: DRM device
 * @crtc: which irq to enable
 *
 * Disable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 */
void omap_irq_disable_vblank(struct drm_device *dev, int crtc_id)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc = priv->crtcs[crtc_id];
	unsigned long flags;

	DBG("dev=%p, crtc=%d", dev, crtc_id);

	dispc_runtime_get();
	spin_lock_irqsave(&list_lock, flags);
	priv->vblank_mask &= ~pipe2vbl(crtc);
	omap_irq_update(dev);
	spin_unlock_irqrestore(&list_lock, flags);
	dispc_runtime_put();
}

irqreturn_t omap_irq_handler(DRM_IRQ_ARGS)
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

		if (irqstatus & pipe2vbl(crtc))
			drm_handle_vblank(dev, id);
	}

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

void omap_irq_preinstall(struct drm_device *dev)
{
	DBG("dev=%p", dev);
	dispc_runtime_get();
	dispc_clear_irqstatus(0xffffffff);
	dispc_runtime_put();
}

int omap_irq_postinstall(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_drm_irq *error_handler = &priv->error_handler;

	DBG("dev=%p", dev);

	INIT_LIST_HEAD(&priv->irq_list);

	error_handler->irq = omap_irq_error_handler;
	error_handler->irqmask = DISPC_IRQ_OCP_ERR;

	/* for now ignore DISPC_IRQ_SYNC_LOST_DIGIT.. really I think
	 * we just need to ignore it while enabling tv-out
	 */
	error_handler->irqmask &= ~DISPC_IRQ_SYNC_LOST_DIGIT;

	omap_irq_register(dev, error_handler);

	return 0;
}

void omap_irq_uninstall(struct drm_device *dev)
{
	DBG("dev=%p", dev);
	// TODO prolly need to call drm_irq_uninstall() somewhere too
}

/*
 * We need a special version, instead of just using drm_irq_install(),
 * because we need to register the irq via omapdss.  Once omapdss and
 * omapdrm are merged together we can assign the dispc hwmod data to
 * ourselves and drop these and just use drm_irq_{install,uninstall}()
 */

int omap_drm_irq_install(struct drm_device *dev)
{
	int ret;

	mutex_lock(&dev->struct_mutex);

	if (dev->irq_enabled) {
		mutex_unlock(&dev->struct_mutex);
		return -EBUSY;
	}
	dev->irq_enabled = 1;
	mutex_unlock(&dev->struct_mutex);

	/* Before installing handler */
	if (dev->driver->irq_preinstall)
		dev->driver->irq_preinstall(dev);

	ret = dispc_request_irq(dev->driver->irq_handler, dev);

	if (ret < 0) {
		mutex_lock(&dev->struct_mutex);
		dev->irq_enabled = 0;
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	/* After installing handler */
	if (dev->driver->irq_postinstall)
		ret = dev->driver->irq_postinstall(dev);

	if (ret < 0) {
		mutex_lock(&dev->struct_mutex);
		dev->irq_enabled = 0;
		mutex_unlock(&dev->struct_mutex);
		dispc_free_irq(dev);
	}

	return ret;
}

int omap_drm_irq_uninstall(struct drm_device *dev)
{
	unsigned long irqflags;
	int irq_enabled, i;

	mutex_lock(&dev->struct_mutex);
	irq_enabled = dev->irq_enabled;
	dev->irq_enabled = 0;
	mutex_unlock(&dev->struct_mutex);

	/*
	 * Wake up any waiters so they don't hang.
	 */
	if (dev->num_crtcs) {
		spin_lock_irqsave(&dev->vbl_lock, irqflags);
		for (i = 0; i < dev->num_crtcs; i++) {
			DRM_WAKEUP(&dev->vblank[i].queue);
			dev->vblank[i].enabled = false;
			dev->vblank[i].last =
				dev->driver->get_vblank_counter(dev, i);
		}
		spin_unlock_irqrestore(&dev->vbl_lock, irqflags);
	}

	if (!irq_enabled)
		return -EINVAL;

	if (dev->driver->irq_uninstall)
		dev->driver->irq_uninstall(dev);

	dispc_free_irq(dev);

	return 0;
}
