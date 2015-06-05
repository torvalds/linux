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
#include "mdp_kms.h"


struct mdp_irq_wait {
	struct mdp_irq irq;
	int count;
};

static DECLARE_WAIT_QUEUE_HEAD(wait_event);

static DEFINE_SPINLOCK(list_lock);

static void update_irq(struct mdp_kms *mdp_kms)
{
	struct mdp_irq *irq;
	uint32_t irqmask = mdp_kms->vblank_mask;

	assert_spin_locked(&list_lock);

	list_for_each_entry(irq, &mdp_kms->irq_list, node)
		irqmask |= irq->irqmask;

	mdp_kms->funcs->set_irqmask(mdp_kms, irqmask);
}

/* if an mdp_irq's irqmask has changed, such as when mdp5 crtc<->encoder
 * link changes, this must be called to figure out the new global irqmask
 */
void mdp_irq_update(struct mdp_kms *mdp_kms)
{
	unsigned long flags;
	spin_lock_irqsave(&list_lock, flags);
	update_irq(mdp_kms);
	spin_unlock_irqrestore(&list_lock, flags);
}

void mdp_dispatch_irqs(struct mdp_kms *mdp_kms, uint32_t status)
{
	struct mdp_irq *handler, *n;
	unsigned long flags;

	spin_lock_irqsave(&list_lock, flags);
	mdp_kms->in_irq = true;
	list_for_each_entry_safe(handler, n, &mdp_kms->irq_list, node) {
		if (handler->irqmask & status) {
			spin_unlock_irqrestore(&list_lock, flags);
			handler->irq(handler, handler->irqmask & status);
			spin_lock_irqsave(&list_lock, flags);
		}
	}
	mdp_kms->in_irq = false;
	update_irq(mdp_kms);
	spin_unlock_irqrestore(&list_lock, flags);

}

void mdp_update_vblank_mask(struct mdp_kms *mdp_kms, uint32_t mask, bool enable)
{
	unsigned long flags;

	spin_lock_irqsave(&list_lock, flags);
	if (enable)
		mdp_kms->vblank_mask |= mask;
	else
		mdp_kms->vblank_mask &= ~mask;
	update_irq(mdp_kms);
	spin_unlock_irqrestore(&list_lock, flags);
}

static void wait_irq(struct mdp_irq *irq, uint32_t irqstatus)
{
	struct mdp_irq_wait *wait =
			container_of(irq, struct mdp_irq_wait, irq);
	wait->count--;
	wake_up_all(&wait_event);
}

void mdp_irq_wait(struct mdp_kms *mdp_kms, uint32_t irqmask)
{
	struct mdp_irq_wait wait = {
		.irq = {
			.irq = wait_irq,
			.irqmask = irqmask,
		},
		.count = 1,
	};
	mdp_irq_register(mdp_kms, &wait.irq);
	wait_event_timeout(wait_event, (wait.count <= 0),
			msecs_to_jiffies(100));
	mdp_irq_unregister(mdp_kms, &wait.irq);
}

void mdp_irq_register(struct mdp_kms *mdp_kms, struct mdp_irq *irq)
{
	unsigned long flags;
	bool needs_update = false;

	spin_lock_irqsave(&list_lock, flags);

	if (!irq->registered) {
		irq->registered = true;
		list_add(&irq->node, &mdp_kms->irq_list);
		needs_update = !mdp_kms->in_irq;
	}

	spin_unlock_irqrestore(&list_lock, flags);

	if (needs_update)
		mdp_irq_update(mdp_kms);
}

void mdp_irq_unregister(struct mdp_kms *mdp_kms, struct mdp_irq *irq)
{
	unsigned long flags;
	bool needs_update = false;

	spin_lock_irqsave(&list_lock, flags);

	if (irq->registered) {
		irq->registered = false;
		list_del(&irq->node);
		needs_update = !mdp_kms->in_irq;
	}

	spin_unlock_irqrestore(&list_lock, flags);

	if (needs_update)
		mdp_irq_update(mdp_kms);
}
