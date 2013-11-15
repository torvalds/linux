/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
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


#include "msm_drv.h"
#include "mdp4_kms.h"


struct mdp4_irq_wait {
	struct mdp4_irq irq;
	int count;
};

static DECLARE_WAIT_QUEUE_HEAD(wait_event);

static DEFINE_SPINLOCK(list_lock);

static void update_irq(struct mdp4_kms *mdp4_kms)
{
	struct mdp4_irq *irq;
	uint32_t irqmask = mdp4_kms->vblank_mask;

	BUG_ON(!spin_is_locked(&list_lock));

	list_for_each_entry(irq, &mdp4_kms->irq_list, node)
		irqmask |= irq->irqmask;

	mdp4_write(mdp4_kms, REG_MDP4_INTR_ENABLE, irqmask);
}

static void update_irq_unlocked(struct mdp4_kms *mdp4_kms)
{
	unsigned long flags;
	spin_lock_irqsave(&list_lock, flags);
	update_irq(mdp4_kms);
	spin_unlock_irqrestore(&list_lock, flags);
}

static void mdp4_irq_error_handler(struct mdp4_irq *irq, uint32_t irqstatus)
{
	DRM_ERROR("errors: %08x\n", irqstatus);
}

void mdp4_irq_preinstall(struct msm_kms *kms)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(kms);
	mdp4_write(mdp4_kms, REG_MDP4_INTR_CLEAR, 0xffffffff);
}

int mdp4_irq_postinstall(struct msm_kms *kms)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(kms);
	struct mdp4_irq *error_handler = &mdp4_kms->error_handler;

	INIT_LIST_HEAD(&mdp4_kms->irq_list);

	error_handler->irq = mdp4_irq_error_handler;
	error_handler->irqmask = MDP4_IRQ_PRIMARY_INTF_UDERRUN |
			MDP4_IRQ_EXTERNAL_INTF_UDERRUN;

	mdp4_irq_register(mdp4_kms, error_handler);

	return 0;
}

void mdp4_irq_uninstall(struct msm_kms *kms)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(kms);
	mdp4_write(mdp4_kms, REG_MDP4_INTR_ENABLE, 0x00000000);
}

irqreturn_t mdp4_irq(struct msm_kms *kms)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(kms);
	struct drm_device *dev = mdp4_kms->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct mdp4_irq *handler, *n;
	unsigned long flags;
	unsigned int id;
	uint32_t status;

	status = mdp4_read(mdp4_kms, REG_MDP4_INTR_STATUS);
	mdp4_write(mdp4_kms, REG_MDP4_INTR_CLEAR, status);

	VERB("status=%08x", status);

	for (id = 0; id < priv->num_crtcs; id++)
		if (status & mdp4_crtc_vblank(priv->crtcs[id]))
			drm_handle_vblank(dev, id);

	spin_lock_irqsave(&list_lock, flags);
	mdp4_kms->in_irq = true;
	list_for_each_entry_safe(handler, n, &mdp4_kms->irq_list, node) {
		if (handler->irqmask & status) {
			spin_unlock_irqrestore(&list_lock, flags);
			handler->irq(handler, handler->irqmask & status);
			spin_lock_irqsave(&list_lock, flags);
		}
	}
	mdp4_kms->in_irq = false;
	update_irq(mdp4_kms);
	spin_unlock_irqrestore(&list_lock, flags);

	return IRQ_HANDLED;
}

int mdp4_enable_vblank(struct msm_kms *kms, struct drm_crtc *crtc)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(kms);
	unsigned long flags;

	spin_lock_irqsave(&list_lock, flags);
	mdp4_kms->vblank_mask |= mdp4_crtc_vblank(crtc);
	update_irq(mdp4_kms);
	spin_unlock_irqrestore(&list_lock, flags);

	return 0;
}

void mdp4_disable_vblank(struct msm_kms *kms, struct drm_crtc *crtc)
{
	struct mdp4_kms *mdp4_kms = to_mdp4_kms(kms);
	unsigned long flags;

	spin_lock_irqsave(&list_lock, flags);
	mdp4_kms->vblank_mask &= ~mdp4_crtc_vblank(crtc);
	update_irq(mdp4_kms);
	spin_unlock_irqrestore(&list_lock, flags);
}

static void wait_irq(struct mdp4_irq *irq, uint32_t irqstatus)
{
	struct mdp4_irq_wait *wait =
			container_of(irq, struct mdp4_irq_wait, irq);
	wait->count--;
	wake_up_all(&wait_event);
}

void mdp4_irq_wait(struct mdp4_kms *mdp4_kms, uint32_t irqmask)
{
	struct mdp4_irq_wait wait = {
		.irq = {
			.irq = wait_irq,
			.irqmask = irqmask,
		},
		.count = 1,
	};
	mdp4_irq_register(mdp4_kms, &wait.irq);
	wait_event(wait_event, (wait.count <= 0));
	mdp4_irq_unregister(mdp4_kms, &wait.irq);
}

void mdp4_irq_register(struct mdp4_kms *mdp4_kms, struct mdp4_irq *irq)
{
	unsigned long flags;
	bool needs_update = false;

	spin_lock_irqsave(&list_lock, flags);

	if (!irq->registered) {
		irq->registered = true;
		list_add(&irq->node, &mdp4_kms->irq_list);
		needs_update = !mdp4_kms->in_irq;
	}

	spin_unlock_irqrestore(&list_lock, flags);

	if (needs_update)
		update_irq_unlocked(mdp4_kms);
}

void mdp4_irq_unregister(struct mdp4_kms *mdp4_kms, struct mdp4_irq *irq)
{
	unsigned long flags;
	bool needs_update = false;

	spin_lock_irqsave(&list_lock, flags);

	if (irq->registered) {
		irq->registered = false;
		list_del(&irq->node);
		needs_update = !mdp4_kms->in_irq;
	}

	spin_unlock_irqrestore(&list_lock, flags);

	if (needs_update)
		update_irq_unlocked(mdp4_kms);
}
