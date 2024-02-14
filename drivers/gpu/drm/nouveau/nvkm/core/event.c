/*
 * Copyright 2013-2014 Red Hat Inc.
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
 */
#include <core/event.h>
#include <core/notify.h>

void
nvkm_event_put(struct nvkm_event *event, u32 types, int index)
{
	assert_spin_locked(&event->refs_lock);
	while (types) {
		int type = __ffs(types); types &= ~(1 << type);
		if (--event->refs[index * event->types_nr + type] == 0) {
			if (event->func->fini)
				event->func->fini(event, 1 << type, index);
		}
	}
}

void
nvkm_event_get(struct nvkm_event *event, u32 types, int index)
{
	assert_spin_locked(&event->refs_lock);
	while (types) {
		int type = __ffs(types); types &= ~(1 << type);
		if (++event->refs[index * event->types_nr + type] == 1) {
			if (event->func->init)
				event->func->init(event, 1 << type, index);
		}
	}
}

void
nvkm_event_send(struct nvkm_event *event, u32 types, int index,
		void *data, u32 size)
{
	struct nvkm_notify *notify;
	unsigned long flags;

	if (!event->refs || WARN_ON(index >= event->index_nr))
		return;

	spin_lock_irqsave(&event->list_lock, flags);
	list_for_each_entry(notify, &event->list, head) {
		if (notify->index == index && (notify->types & types)) {
			if (event->func->send) {
				event->func->send(data, size, notify);
				continue;
			}
			nvkm_notify_send(notify, data, size);
		}
	}
	spin_unlock_irqrestore(&event->list_lock, flags);
}

void
nvkm_event_fini(struct nvkm_event *event)
{
	if (event->refs) {
		kfree(event->refs);
		event->refs = NULL;
	}
}

int
nvkm_event_init(const struct nvkm_event_func *func, int types_nr, int index_nr,
		struct nvkm_event *event)
{
	event->refs = kzalloc(array3_size(index_nr, types_nr,
					  sizeof(*event->refs)),
			      GFP_KERNEL);
	if (!event->refs)
		return -ENOMEM;

	event->func = func;
	event->types_nr = types_nr;
	event->index_nr = index_nr;
	spin_lock_init(&event->refs_lock);
	spin_lock_init(&event->list_lock);
	INIT_LIST_HEAD(&event->list);
	return 0;
}
