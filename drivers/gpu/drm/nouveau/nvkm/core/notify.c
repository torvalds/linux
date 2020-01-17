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
 * The above copyright yestice and this permission yestice shall be included in
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
#include <core/yestify.h>
#include <core/event.h>

static inline void
nvkm_yestify_put_locked(struct nvkm_yestify *yestify)
{
	if (yestify->block++ == 0)
		nvkm_event_put(yestify->event, yestify->types, yestify->index);
}

void
nvkm_yestify_put(struct nvkm_yestify *yestify)
{
	struct nvkm_event *event = yestify->event;
	unsigned long flags;
	if (likely(event) &&
	    test_and_clear_bit(NVKM_NOTIFY_USER, &yestify->flags)) {
		spin_lock_irqsave(&event->refs_lock, flags);
		nvkm_yestify_put_locked(yestify);
		spin_unlock_irqrestore(&event->refs_lock, flags);
		if (test_bit(NVKM_NOTIFY_WORK, &yestify->flags))
			flush_work(&yestify->work);
	}
}

static inline void
nvkm_yestify_get_locked(struct nvkm_yestify *yestify)
{
	if (--yestify->block == 0)
		nvkm_event_get(yestify->event, yestify->types, yestify->index);
}

void
nvkm_yestify_get(struct nvkm_yestify *yestify)
{
	struct nvkm_event *event = yestify->event;
	unsigned long flags;
	if (likely(event) &&
	    !test_and_set_bit(NVKM_NOTIFY_USER, &yestify->flags)) {
		spin_lock_irqsave(&event->refs_lock, flags);
		nvkm_yestify_get_locked(yestify);
		spin_unlock_irqrestore(&event->refs_lock, flags);
	}
}

static inline void
nvkm_yestify_func(struct nvkm_yestify *yestify)
{
	struct nvkm_event *event = yestify->event;
	int ret = yestify->func(yestify);
	unsigned long flags;
	if ((ret == NVKM_NOTIFY_KEEP) ||
	    !test_and_clear_bit(NVKM_NOTIFY_USER, &yestify->flags)) {
		spin_lock_irqsave(&event->refs_lock, flags);
		nvkm_yestify_get_locked(yestify);
		spin_unlock_irqrestore(&event->refs_lock, flags);
	}
}

static void
nvkm_yestify_work(struct work_struct *work)
{
	struct nvkm_yestify *yestify = container_of(work, typeof(*yestify), work);
	nvkm_yestify_func(yestify);
}

void
nvkm_yestify_send(struct nvkm_yestify *yestify, void *data, u32 size)
{
	struct nvkm_event *event = yestify->event;
	unsigned long flags;

	assert_spin_locked(&event->list_lock);
	BUG_ON(size != yestify->size);

	spin_lock_irqsave(&event->refs_lock, flags);
	if (yestify->block) {
		spin_unlock_irqrestore(&event->refs_lock, flags);
		return;
	}
	nvkm_yestify_put_locked(yestify);
	spin_unlock_irqrestore(&event->refs_lock, flags);

	if (test_bit(NVKM_NOTIFY_WORK, &yestify->flags)) {
		memcpy((void *)yestify->data, data, size);
		schedule_work(&yestify->work);
	} else {
		yestify->data = data;
		nvkm_yestify_func(yestify);
		yestify->data = NULL;
	}
}

void
nvkm_yestify_fini(struct nvkm_yestify *yestify)
{
	unsigned long flags;
	if (yestify->event) {
		nvkm_yestify_put(yestify);
		spin_lock_irqsave(&yestify->event->list_lock, flags);
		list_del(&yestify->head);
		spin_unlock_irqrestore(&yestify->event->list_lock, flags);
		kfree((void *)yestify->data);
		yestify->event = NULL;
	}
}

int
nvkm_yestify_init(struct nvkm_object *object, struct nvkm_event *event,
		 int (*func)(struct nvkm_yestify *), bool work,
		 void *data, u32 size, u32 reply,
		 struct nvkm_yestify *yestify)
{
	unsigned long flags;
	int ret = -ENODEV;
	if ((yestify->event = event), event->refs) {
		ret = event->func->ctor(object, data, size, yestify);
		if (ret == 0 && (ret = -EINVAL, yestify->size == reply)) {
			yestify->flags = 0;
			yestify->block = 1;
			yestify->func = func;
			yestify->data = NULL;
			if (ret = 0, work) {
				INIT_WORK(&yestify->work, nvkm_yestify_work);
				set_bit(NVKM_NOTIFY_WORK, &yestify->flags);
				yestify->data = kmalloc(reply, GFP_KERNEL);
				if (!yestify->data)
					ret = -ENOMEM;
			}
		}
		if (ret == 0) {
			spin_lock_irqsave(&event->list_lock, flags);
			list_add_tail(&yestify->head, &event->list);
			spin_unlock_irqrestore(&event->list_lock, flags);
		}
	}
	if (ret)
		yestify->event = NULL;
	return ret;
}
