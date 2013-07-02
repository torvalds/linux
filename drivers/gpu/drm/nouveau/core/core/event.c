/*
 * Copyright 2013 Red Hat Inc.
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

#include <core/os.h>
#include <core/event.h>

static void
nouveau_event_put_locked(struct nouveau_event *event, int index,
			 struct nouveau_eventh *handler)
{
	if (!--event->index[index].refs) {
		if (event->disable)
			event->disable(event, index);
	}
	list_del(&handler->head);
}

void
nouveau_event_put(struct nouveau_event *event, int index,
		  struct nouveau_eventh *handler)
{
	unsigned long flags;

	spin_lock_irqsave(&event->lock, flags);
	if (index < event->index_nr)
		nouveau_event_put_locked(event, index, handler);
	spin_unlock_irqrestore(&event->lock, flags);
}

void
nouveau_event_get(struct nouveau_event *event, int index,
		  struct nouveau_eventh *handler)
{
	unsigned long flags;

	spin_lock_irqsave(&event->lock, flags);
	if (index < event->index_nr) {
		list_add(&handler->head, &event->index[index].list);
		if (!event->index[index].refs++) {
			if (event->enable)
				event->enable(event, index);
		}
	}
	spin_unlock_irqrestore(&event->lock, flags);
}

void
nouveau_event_trigger(struct nouveau_event *event, int index)
{
	struct nouveau_eventh *handler, *temp;
	unsigned long flags;

	if (index >= event->index_nr)
		return;

	spin_lock_irqsave(&event->lock, flags);
	list_for_each_entry_safe(handler, temp, &event->index[index].list, head) {
		if (handler->func(handler, index) == NVKM_EVENT_DROP) {
			nouveau_event_put_locked(event, index, handler);
		}
	}
	spin_unlock_irqrestore(&event->lock, flags);
}

void
nouveau_event_destroy(struct nouveau_event **pevent)
{
	struct nouveau_event *event = *pevent;
	if (event) {
		kfree(event);
		*pevent = NULL;
	}
}

int
nouveau_event_create(int index_nr, struct nouveau_event **pevent)
{
	struct nouveau_event *event;
	int i;

	event = *pevent = kzalloc(sizeof(*event) + index_nr *
				  sizeof(event->index[0]), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	spin_lock_init(&event->lock);
	for (i = 0; i < index_nr; i++)
		INIT_LIST_HEAD(&event->index[i].list);
	event->index_nr = index_nr;
	return 0;
}
