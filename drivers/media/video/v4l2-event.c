/*
 * v4l2-event.c
 *
 * V4L2 events.
 *
 * Copyright (C) 2009--2010 Nokia Corporation.
 *
 * Contact: Sakari Ailus <sakari.ailus@maxwell.research.nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ctrls.h>

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/export.h>

static unsigned sev_pos(const struct v4l2_subscribed_event *sev, unsigned idx)
{
	idx += sev->first;
	return idx >= sev->elems ? idx - sev->elems : idx;
}

static int __v4l2_event_dequeue(struct v4l2_fh *fh, struct v4l2_event *event)
{
	struct v4l2_kevent *kev;
	unsigned long flags;

	spin_lock_irqsave(&fh->vdev->fh_lock, flags);

	if (list_empty(&fh->available)) {
		spin_unlock_irqrestore(&fh->vdev->fh_lock, flags);
		return -ENOENT;
	}

	WARN_ON(fh->navailable == 0);

	kev = list_first_entry(&fh->available, struct v4l2_kevent, list);
	list_del(&kev->list);
	fh->navailable--;

	kev->event.pending = fh->navailable;
	*event = kev->event;
	kev->sev->first = sev_pos(kev->sev, 1);
	kev->sev->in_use--;

	spin_unlock_irqrestore(&fh->vdev->fh_lock, flags);

	return 0;
}

int v4l2_event_dequeue(struct v4l2_fh *fh, struct v4l2_event *event,
		       int nonblocking)
{
	int ret;

	if (nonblocking)
		return __v4l2_event_dequeue(fh, event);

	/* Release the vdev lock while waiting */
	if (fh->vdev->lock)
		mutex_unlock(fh->vdev->lock);

	do {
		ret = wait_event_interruptible(fh->wait,
					       fh->navailable != 0);
		if (ret < 0)
			break;

		ret = __v4l2_event_dequeue(fh, event);
	} while (ret == -ENOENT);

	if (fh->vdev->lock)
		mutex_lock(fh->vdev->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_event_dequeue);

/* Caller must hold fh->vdev->fh_lock! */
static struct v4l2_subscribed_event *v4l2_event_subscribed(
		struct v4l2_fh *fh, u32 type, u32 id)
{
	struct v4l2_subscribed_event *sev;

	assert_spin_locked(&fh->vdev->fh_lock);

	list_for_each_entry(sev, &fh->subscribed, list)
		if (sev->type == type && sev->id == id)
			return sev;

	return NULL;
}

static void __v4l2_event_queue_fh(struct v4l2_fh *fh, const struct v4l2_event *ev,
		const struct timespec *ts)
{
	struct v4l2_subscribed_event *sev;
	struct v4l2_kevent *kev;
	bool copy_payload = true;

	/* Are we subscribed? */
	sev = v4l2_event_subscribed(fh, ev->type, ev->id);
	if (sev == NULL)
		return;

	/* Increase event sequence number on fh. */
	fh->sequence++;

	/* Do we have any free events? */
	if (sev->in_use == sev->elems) {
		/* no, remove the oldest one */
		kev = sev->events + sev_pos(sev, 0);
		list_del(&kev->list);
		sev->in_use--;
		sev->first = sev_pos(sev, 1);
		fh->navailable--;
		if (sev->elems == 1) {
			if (sev->replace) {
				sev->replace(&kev->event, ev);
				copy_payload = false;
			}
		} else if (sev->merge) {
			struct v4l2_kevent *second_oldest =
				sev->events + sev_pos(sev, 0);
			sev->merge(&kev->event, &second_oldest->event);
		}
	}

	/* Take one and fill it. */
	kev = sev->events + sev_pos(sev, sev->in_use);
	kev->event.type = ev->type;
	if (copy_payload)
		kev->event.u = ev->u;
	kev->event.id = ev->id;
	kev->event.timestamp = *ts;
	kev->event.sequence = fh->sequence;
	sev->in_use++;
	list_add_tail(&kev->list, &fh->available);

	fh->navailable++;

	wake_up_all(&fh->wait);
}

void v4l2_event_queue(struct video_device *vdev, const struct v4l2_event *ev)
{
	struct v4l2_fh *fh;
	unsigned long flags;
	struct timespec timestamp;

	ktime_get_ts(&timestamp);

	spin_lock_irqsave(&vdev->fh_lock, flags);

	list_for_each_entry(fh, &vdev->fh_list, list)
		__v4l2_event_queue_fh(fh, ev, &timestamp);

	spin_unlock_irqrestore(&vdev->fh_lock, flags);
}
EXPORT_SYMBOL_GPL(v4l2_event_queue);

void v4l2_event_queue_fh(struct v4l2_fh *fh, const struct v4l2_event *ev)
{
	unsigned long flags;
	struct timespec timestamp;

	ktime_get_ts(&timestamp);

	spin_lock_irqsave(&fh->vdev->fh_lock, flags);
	__v4l2_event_queue_fh(fh, ev, &timestamp);
	spin_unlock_irqrestore(&fh->vdev->fh_lock, flags);
}
EXPORT_SYMBOL_GPL(v4l2_event_queue_fh);

int v4l2_event_pending(struct v4l2_fh *fh)
{
	return fh->navailable;
}
EXPORT_SYMBOL_GPL(v4l2_event_pending);

static void ctrls_replace(struct v4l2_event *old, const struct v4l2_event *new)
{
	u32 old_changes = old->u.ctrl.changes;

	old->u.ctrl = new->u.ctrl;
	old->u.ctrl.changes |= old_changes;
}

static void ctrls_merge(const struct v4l2_event *old, struct v4l2_event *new)
{
	new->u.ctrl.changes |= old->u.ctrl.changes;
}

int v4l2_event_subscribe(struct v4l2_fh *fh,
			 struct v4l2_event_subscription *sub, unsigned elems)
{
	struct v4l2_subscribed_event *sev, *found_ev;
	struct v4l2_ctrl *ctrl = NULL;
	unsigned long flags;
	unsigned i;

	if (elems < 1)
		elems = 1;
	if (sub->type == V4L2_EVENT_CTRL) {
		ctrl = v4l2_ctrl_find(fh->ctrl_handler, sub->id);
		if (ctrl == NULL)
			return -EINVAL;
	}

	sev = kzalloc(sizeof(*sev) + sizeof(struct v4l2_kevent) * elems, GFP_KERNEL);
	if (!sev)
		return -ENOMEM;
	for (i = 0; i < elems; i++)
		sev->events[i].sev = sev;
	sev->type = sub->type;
	sev->id = sub->id;
	sev->flags = sub->flags;
	sev->fh = fh;
	sev->elems = elems;
	if (ctrl) {
		sev->replace = ctrls_replace;
		sev->merge = ctrls_merge;
	}

	spin_lock_irqsave(&fh->vdev->fh_lock, flags);
	found_ev = v4l2_event_subscribed(fh, sub->type, sub->id);
	if (!found_ev)
		list_add(&sev->list, &fh->subscribed);
	spin_unlock_irqrestore(&fh->vdev->fh_lock, flags);

	/* v4l2_ctrl_add_event uses a mutex, so do this outside the spin lock */
	if (found_ev)
		kfree(sev);
	else if (ctrl)
		v4l2_ctrl_add_event(ctrl, sev);

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_event_subscribe);

void v4l2_event_unsubscribe_all(struct v4l2_fh *fh)
{
	struct v4l2_event_subscription sub;
	struct v4l2_subscribed_event *sev;
	unsigned long flags;

	do {
		sev = NULL;

		spin_lock_irqsave(&fh->vdev->fh_lock, flags);
		if (!list_empty(&fh->subscribed)) {
			sev = list_first_entry(&fh->subscribed,
					struct v4l2_subscribed_event, list);
			sub.type = sev->type;
			sub.id = sev->id;
		}
		spin_unlock_irqrestore(&fh->vdev->fh_lock, flags);
		if (sev)
			v4l2_event_unsubscribe(fh, &sub);
	} while (sev);
}
EXPORT_SYMBOL_GPL(v4l2_event_unsubscribe_all);

int v4l2_event_unsubscribe(struct v4l2_fh *fh,
			   struct v4l2_event_subscription *sub)
{
	struct v4l2_subscribed_event *sev;
	unsigned long flags;

	if (sub->type == V4L2_EVENT_ALL) {
		v4l2_event_unsubscribe_all(fh);
		return 0;
	}

	spin_lock_irqsave(&fh->vdev->fh_lock, flags);

	sev = v4l2_event_subscribed(fh, sub->type, sub->id);
	if (sev != NULL) {
		list_del(&sev->list);
		sev->fh = NULL;
	}

	spin_unlock_irqrestore(&fh->vdev->fh_lock, flags);
	if (sev && sev->type == V4L2_EVENT_CTRL) {
		struct v4l2_ctrl *ctrl = v4l2_ctrl_find(fh->ctrl_handler, sev->id);

		if (ctrl)
			v4l2_ctrl_del_event(ctrl, sev);
	}

	kfree(sev);

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_event_unsubscribe);
