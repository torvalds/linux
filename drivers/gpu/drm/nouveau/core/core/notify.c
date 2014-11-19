/*
 * Copyright 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */

#include <core/client.h>
#include <core/event.h>
#include <core/notify.h>

#include <nvif/unpack.h>
#include <nvif/event.h>

static inline void
nvkm_notify_put_locked(struct nvkm_notify *notify)
{
	if (notify->block++ == 0)
		nvkm_event_put(notify->event, notify->types, notify->index);
}

void
nvkm_notify_put(struct nvkm_notify *notify)
{
	struct nvkm_event *event = notify->event;
	unsigned long flags;
	if (likely(event) &&
	    test_and_clear_bit(NVKM_NOTIFY_USER, &notify->flags)) {
		spin_lock_irqsave(&event->refs_lock, flags);
		nvkm_notify_put_locked(notify);
		spin_unlock_irqrestore(&event->refs_lock, flags);
		if (test_bit(NVKM_NOTIFY_WORK, &notify->flags))
			flush_work(&notify->work);
	}
}

static inline void
nvkm_notify_get_locked(struct nvkm_notify *notify)
{
	if (--notify->block == 0)
		nvkm_event_get(notify->event, notify->types, notify->index);
}

void
nvkm_notify_get(struct nvkm_notify *notify)
{
	struct nvkm_event *event = notify->event;
	unsigned long flags;
	if (likely(event) &&
	    !test_and_set_bit(NVKM_NOTIFY_USER, &notify->flags)) {
		spin_lock_irqsave(&event->refs_lock, flags);
		nvkm_notify_get_locked(notify);
		spin_unlock_irqrestore(&event->refs_lock, flags);
	}
}

static inline void
nvkm_notify_func(struct nvkm_notify *notify)
{
	struct nvkm_event *event = notify->event;
	int ret = notify->func(notify);
	unsigned long flags;
	if ((ret == NVKM_NOTIFY_KEEP) ||
	    !test_and_clear_bit(NVKM_NOTIFY_USER, &notify->flags)) {
		spin_lock_irqsave(&event->refs_lock, flags);
		nvkm_notify_get_locked(notify);
		spin_unlock_irqrestore(&event->refs_lock, flags);
	}
}

static void
nvkm_notify_work(struct work_struct *work)
{
	struct nvkm_notify *notify = container_of(work, typeof(*notify), work);
	nvkm_notify_func(notify);
}

void
nvkm_notify_send(struct nvkm_notify *notify, void *data, u32 size)
{
	struct nvkm_event *event = notify->event;
	unsigned long flags;

	BUG_ON(!spin_is_locked(&event->list_lock));
	BUG_ON(size != notify->size);

	spin_lock_irqsave(&event->refs_lock, flags);
	if (notify->block) {
		spin_unlock_irqrestore(&event->refs_lock, flags);
		return;
	}
	nvkm_notify_put_locked(notify);
	spin_unlock_irqrestore(&event->refs_lock, flags);

	if (test_bit(NVKM_NOTIFY_WORK, &notify->flags)) {
		memcpy((void *)notify->data, data, size);
		schedule_work(&notify->work);
	} else {
		notify->data = data;
		nvkm_notify_func(notify);
		notify->data = NULL;
	}
}

void
nvkm_notify_fini(struct nvkm_notify *notify)
{
	unsigned long flags;
	if (notify->event) {
		nvkm_notify_put(notify);
		spin_lock_irqsave(&notify->event->list_lock, flags);
		list_del(&notify->head);
		spin_unlock_irqrestore(&notify->event->list_lock, flags);
		kfree((void *)notify->data);
		notify->event = NULL;
	}
}

int
nvkm_notify_init(struct nvkm_event *event, int (*func)(struct nvkm_notify *),
		 bool work, void *data, u32 size, u32 reply,
		 struct nvkm_notify *notify)
{
	unsigned long flags;
	int ret = -ENODEV;
	if ((notify->event = event), event->refs) {
		ret = event->func->ctor(data, size, notify);
		if (ret == 0 && (ret = -EINVAL, notify->size == reply)) {
			notify->flags = 0;
			notify->block = 1;
			notify->func = func;
			notify->data = NULL;
			if (ret = 0, work) {
				INIT_WORK(&notify->work, nvkm_notify_work);
				set_bit(NVKM_NOTIFY_WORK, &notify->flags);
				notify->data = kmalloc(reply, GFP_KERNEL);
				if (!notify->data)
					ret = -ENOMEM;
			}
		}
		if (ret == 0) {
			spin_lock_irqsave(&event->list_lock, flags);
			list_add_tail(&notify->head, &event->list);
			spin_unlock_irqrestore(&event->list_lock, flags);
		}
	}
	if (ret)
		notify->event = NULL;
	return ret;
}
