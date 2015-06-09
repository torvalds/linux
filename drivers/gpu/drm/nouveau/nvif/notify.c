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

#include <nvif/client.h>
#include <nvif/driver.h>
#include <nvif/notify.h>
#include <nvif/object.h>
#include <nvif/ioctl.h>
#include <nvif/event.h>

static inline int
nvif_notify_put_(struct nvif_notify *notify)
{
	struct nvif_object *object = notify->object;
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_ntfy_put_v0 ntfy;
	} args = {
		.ioctl.type = NVIF_IOCTL_V0_NTFY_PUT,
		.ntfy.index = notify->index,
	};

	if (atomic_inc_return(&notify->putcnt) != 1)
		return 0;

	return nvif_object_ioctl(object, &args, sizeof(args), NULL);
}

int
nvif_notify_put(struct nvif_notify *notify)
{
	if (likely(notify->object) &&
	    test_and_clear_bit(NVIF_NOTIFY_USER, &notify->flags)) {
		int ret = nvif_notify_put_(notify);
		if (test_bit(NVIF_NOTIFY_WORK, &notify->flags))
			flush_work(&notify->work);
		return ret;
	}
	return 0;
}

static inline int
nvif_notify_get_(struct nvif_notify *notify)
{
	struct nvif_object *object = notify->object;
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_ntfy_get_v0 ntfy;
	} args = {
		.ioctl.type = NVIF_IOCTL_V0_NTFY_GET,
		.ntfy.index = notify->index,
	};

	if (atomic_dec_return(&notify->putcnt) != 0)
		return 0;

	return nvif_object_ioctl(object, &args, sizeof(args), NULL);
}

int
nvif_notify_get(struct nvif_notify *notify)
{
	if (likely(notify->object) &&
	    !test_and_set_bit(NVIF_NOTIFY_USER, &notify->flags))
		return nvif_notify_get_(notify);
	return 0;
}

static inline int
nvif_notify_func(struct nvif_notify *notify, bool keep)
{
	int ret = notify->func(notify);
	if (ret == NVIF_NOTIFY_KEEP ||
	    !test_and_clear_bit(NVIF_NOTIFY_USER, &notify->flags)) {
		if (!keep)
			atomic_dec(&notify->putcnt);
		else
			nvif_notify_get_(notify);
	}
	return ret;
}

static void
nvif_notify_work(struct work_struct *work)
{
	struct nvif_notify *notify = container_of(work, typeof(*notify), work);
	nvif_notify_func(notify, true);
}

int
nvif_notify(const void *header, u32 length, const void *data, u32 size)
{
	struct nvif_notify *notify = NULL;
	const union {
		struct nvif_notify_rep_v0 v0;
	} *args = header;
	int ret = NVIF_NOTIFY_DROP;

	if (length == sizeof(args->v0) && args->v0.version == 0) {
		if (WARN_ON(args->v0.route))
			return NVIF_NOTIFY_DROP;
		notify = (void *)(unsigned long)args->v0.token;
	}

	if (!WARN_ON(notify == NULL)) {
		struct nvif_client *client = nvif_client(notify->object);
		if (!WARN_ON(notify->size != size)) {
			atomic_inc(&notify->putcnt);
			if (test_bit(NVIF_NOTIFY_WORK, &notify->flags)) {
				memcpy((void *)notify->data, data, size);
				schedule_work(&notify->work);
				return NVIF_NOTIFY_DROP;
			}
			notify->data = data;
			ret = nvif_notify_func(notify, client->driver->keep);
			notify->data = NULL;
		}
	}

	return ret;
}

int
nvif_notify_fini(struct nvif_notify *notify)
{
	struct nvif_object *object = notify->object;
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_ntfy_del_v0 ntfy;
	} args = {
		.ioctl.type = NVIF_IOCTL_V0_NTFY_DEL,
		.ntfy.index = notify->index,
	};
	int ret = nvif_notify_put(notify);
	if (ret >= 0 && object) {
		ret = nvif_object_ioctl(object, &args, sizeof(args), NULL);
		if (ret == 0) {
			nvif_object_ref(NULL, &notify->object);
			kfree((void *)notify->data);
		}
	}
	return ret;
}

int
nvif_notify_init(struct nvif_object *object, void (*dtor)(struct nvif_notify *),
		 int (*func)(struct nvif_notify *), bool work, u8 event,
		 void *data, u32 size, u32 reply, struct nvif_notify *notify)
{
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_ntfy_new_v0 ntfy;
		struct nvif_notify_req_v0 req;
	} *args;
	int ret = -ENOMEM;

	notify->object = NULL;
	nvif_object_ref(object, &notify->object);
	notify->flags = 0;
	atomic_set(&notify->putcnt, 1);
	notify->dtor = dtor;
	notify->func = func;
	notify->data = NULL;
	notify->size = reply;
	if (work) {
		INIT_WORK(&notify->work, nvif_notify_work);
		set_bit(NVIF_NOTIFY_WORK, &notify->flags);
		notify->data = kmalloc(notify->size, GFP_KERNEL);
		if (!notify->data)
			goto done;
	}

	if (!(args = kmalloc(sizeof(*args) + size, GFP_KERNEL)))
		goto done;
	args->ioctl.version = 0;
	args->ioctl.type = NVIF_IOCTL_V0_NTFY_NEW;
	args->ntfy.version = 0;
	args->ntfy.event = event;
	args->req.version = 0;
	args->req.reply = notify->size;
	args->req.route = 0;
	args->req.token = (unsigned long)(void *)notify;

	memcpy(args->req.data, data, size);
	ret = nvif_object_ioctl(object, args, sizeof(*args) + size, NULL);
	notify->index = args->ntfy.index;
	kfree(args);
done:
	if (ret)
		nvif_notify_fini(notify);
	return ret;
}

static void
nvif_notify_del(struct nvif_notify *notify)
{
	nvif_notify_fini(notify);
	kfree(notify);
}

void
nvif_notify_ref(struct nvif_notify *notify, struct nvif_notify **pnotify)
{
	BUG_ON(notify != NULL);
	if (*pnotify)
		(*pnotify)->dtor(*pnotify);
	*pnotify = notify;
}

int
nvif_notify_new(struct nvif_object *object, int (*func)(struct nvif_notify *),
		bool work, u8 type, void *data, u32 size, u32 reply,
		struct nvif_notify **pnotify)
{
	struct nvif_notify *notify = kzalloc(sizeof(*notify), GFP_KERNEL);
	if (notify) {
		int ret = nvif_notify_init(object, nvif_notify_del, func, work,
					   type, data, size, reply, notify);
		if (ret) {
			kfree(notify);
			notify = NULL;
		}
		*pnotify = notify;
		return ret;
	}
	return -ENOMEM;
}
