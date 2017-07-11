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

#include "nouveau_drm.h"
#include "nouveau_usif.h"
#include "nouveau_abi16.h"

#include <nvif/notify.h>
#include <nvif/unpack.h>
#include <nvif/client.h>
#include <nvif/event.h>
#include <nvif/ioctl.h>

struct usif_notify_p {
	struct drm_pending_event base;
	struct {
		struct drm_event base;
		u8 data[];
	} e;
};

struct usif_notify {
	struct list_head head;
	atomic_t enabled;
	u32 handle;
	u16 reply;
	u8  route;
	u64 token;
	struct usif_notify_p *p;
};

static inline struct usif_notify *
usif_notify_find(struct drm_file *filp, u32 handle)
{
	struct nouveau_cli *cli = nouveau_cli(filp);
	struct usif_notify *ntfy;
	list_for_each_entry(ntfy, &cli->notifys, head) {
		if (ntfy->handle == handle)
			return ntfy;
	}
	return NULL;
}

static inline void
usif_notify_dtor(struct usif_notify *ntfy)
{
	list_del(&ntfy->head);
	kfree(ntfy);
}

int
usif_notify(const void *header, u32 length, const void *data, u32 size)
{
	struct usif_notify *ntfy = NULL;
	const union {
		struct nvif_notify_rep_v0 v0;
	} *rep = header;
	struct drm_device *dev;
	struct drm_file *filp;
	unsigned long flags;

	if (length == sizeof(rep->v0) && rep->v0.version == 0) {
		if (WARN_ON(!(ntfy = (void *)(unsigned long)rep->v0.token)))
			return NVIF_NOTIFY_DROP;
		BUG_ON(rep->v0.route != NVDRM_NOTIFY_USIF);
	} else
	if (WARN_ON(1))
		return NVIF_NOTIFY_DROP;

	if (WARN_ON(!ntfy->p || ntfy->reply != (length + size)))
		return NVIF_NOTIFY_DROP;
	filp = ntfy->p->base.file_priv;
	dev = filp->minor->dev;

	memcpy(&ntfy->p->e.data[0], header, length);
	memcpy(&ntfy->p->e.data[length], data, size);
	switch (rep->v0.version) {
	case 0: {
		struct nvif_notify_rep_v0 *rep = (void *)ntfy->p->e.data;
		rep->route = ntfy->route;
		rep->token = ntfy->token;
	}
		break;
	default:
		BUG_ON(1);
		break;
	}

	spin_lock_irqsave(&dev->event_lock, flags);
	if (!WARN_ON(filp->event_space < ntfy->p->e.base.length)) {
		list_add_tail(&ntfy->p->base.link, &filp->event_list);
		filp->event_space -= ntfy->p->e.base.length;
	}
	wake_up_interruptible(&filp->event_wait);
	spin_unlock_irqrestore(&dev->event_lock, flags);
	atomic_set(&ntfy->enabled, 0);
	return NVIF_NOTIFY_DROP;
}

static int
usif_notify_new(struct drm_file *f, void *data, u32 size, void *argv, u32 argc)
{
	struct nouveau_cli *cli = nouveau_cli(f);
	struct nvif_client *client = &cli->base;
	union {
		struct nvif_ioctl_ntfy_new_v0 v0;
	} *args = data;
	union {
		struct nvif_notify_req_v0 v0;
	} *req;
	struct usif_notify *ntfy;
	int ret;

	if (nvif_unpack(args->v0, 0, 0, true)) {
		if (usif_notify_find(f, args->v0.index))
			return -EEXIST;
	} else
		return ret;
	req = data;

	if (!(ntfy = kmalloc(sizeof(*ntfy), GFP_KERNEL)))
		return -ENOMEM;
	atomic_set(&ntfy->enabled, 0);

	if (nvif_unpack(req->v0, 0, 0, true)) {
		ntfy->reply = sizeof(struct nvif_notify_rep_v0) + req->v0.reply;
		ntfy->route = req->v0.route;
		ntfy->token = req->v0.token;
		req->v0.route = NVDRM_NOTIFY_USIF;
		req->v0.token = (unsigned long)(void *)ntfy;
		ret = nvif_client_ioctl(client, argv, argc);
		req->v0.token = ntfy->token;
		req->v0.route = ntfy->route;
		ntfy->handle = args->v0.index;
	}

	if (ret == 0)
		list_add(&ntfy->head, &cli->notifys);
	if (ret)
		kfree(ntfy);
	return ret;
}

static int
usif_notify_del(struct drm_file *f, void *data, u32 size, void *argv, u32 argc)
{
	struct nouveau_cli *cli = nouveau_cli(f);
	struct nvif_client *client = &cli->base;
	union {
		struct nvif_ioctl_ntfy_del_v0 v0;
	} *args = data;
	struct usif_notify *ntfy;
	int ret;

	if (nvif_unpack(args->v0, 0, 0, true)) {
		if (!(ntfy = usif_notify_find(f, args->v0.index)))
			return -ENOENT;
	} else
		return ret;

	ret = nvif_client_ioctl(client, argv, argc);
	if (ret == 0)
		usif_notify_dtor(ntfy);
	return ret;
}

static int
usif_notify_get(struct drm_file *f, void *data, u32 size, void *argv, u32 argc)
{
	struct nouveau_cli *cli = nouveau_cli(f);
	struct nvif_client *client = &cli->base;
	union {
		struct nvif_ioctl_ntfy_del_v0 v0;
	} *args = data;
	struct usif_notify *ntfy;
	int ret;

	if (nvif_unpack(args->v0, 0, 0, true)) {
		if (!(ntfy = usif_notify_find(f, args->v0.index)))
			return -ENOENT;
	} else
		return ret;

	if (atomic_xchg(&ntfy->enabled, 1))
		return 0;

	ntfy->p = kmalloc(sizeof(*ntfy->p) + ntfy->reply, GFP_KERNEL);
	if (ret = -ENOMEM, !ntfy->p)
		goto done;
	ntfy->p->base.event = &ntfy->p->e.base;
	ntfy->p->base.file_priv = f;
	ntfy->p->base.pid = current->pid;
	ntfy->p->base.destroy =(void(*)(struct drm_pending_event *))kfree;
	ntfy->p->e.base.type = DRM_NOUVEAU_EVENT_NVIF;
	ntfy->p->e.base.length = sizeof(ntfy->p->e.base) + ntfy->reply;

	ret = nvif_client_ioctl(client, argv, argc);
done:
	if (ret) {
		atomic_set(&ntfy->enabled, 0);
		kfree(ntfy->p);
	}
	return ret;
}

static int
usif_notify_put(struct drm_file *f, void *data, u32 size, void *argv, u32 argc)
{
	struct nouveau_cli *cli = nouveau_cli(f);
	struct nvif_client *client = &cli->base;
	union {
		struct nvif_ioctl_ntfy_put_v0 v0;
	} *args = data;
	struct usif_notify *ntfy;
	int ret;

	if (nvif_unpack(args->v0, 0, 0, true)) {
		if (!(ntfy = usif_notify_find(f, args->v0.index)))
			return -ENOENT;
	} else
		return ret;

	ret = nvif_client_ioctl(client, argv, argc);
	if (ret == 0 && atomic_xchg(&ntfy->enabled, 0))
		kfree(ntfy->p);
	return ret;
}

struct usif_object {
	struct list_head head;
	struct list_head ntfy;
	u8  route;
	u64 token;
};

static void
usif_object_dtor(struct usif_object *object)
{
	list_del(&object->head);
	kfree(object);
}

static int
usif_object_new(struct drm_file *f, void *data, u32 size, void *argv, u32 argc)
{
	struct nouveau_cli *cli = nouveau_cli(f);
	struct nvif_client *client = &cli->base;
	union {
		struct nvif_ioctl_new_v0 v0;
	} *args = data;
	struct usif_object *object;
	int ret;

	if (!(object = kmalloc(sizeof(*object), GFP_KERNEL)))
		return -ENOMEM;
	list_add(&object->head, &cli->objects);

	if (nvif_unpack(args->v0, 0, 0, true)) {
		object->route = args->v0.route;
		object->token = args->v0.token;
		args->v0.route = NVDRM_OBJECT_USIF;
		args->v0.token = (unsigned long)(void *)object;
		ret = nvif_client_ioctl(client, argv, argc);
		args->v0.token = object->token;
		args->v0.route = object->route;
	}

	if (ret)
		usif_object_dtor(object);
	return ret;
}

int
usif_ioctl(struct drm_file *filp, void __user *user, u32 argc)
{
	struct nouveau_cli *cli = nouveau_cli(filp);
	struct nvif_client *client = &cli->base;
	void *data = kmalloc(argc, GFP_KERNEL);
	u32   size = argc;
	union {
		struct nvif_ioctl_v0 v0;
	} *argv = data;
	struct usif_object *object;
	u8 owner;
	int ret;

	if (ret = -ENOMEM, !argv)
		goto done;
	if (ret = -EFAULT, copy_from_user(argv, user, size))
		goto done;

	if (nvif_unpack(argv->v0, 0, 0, true)) {
		/* block access to objects not created via this interface */
		owner = argv->v0.owner;
		if (argv->v0.object == 0ULL &&
		    argv->v0.type != NVIF_IOCTL_V0_DEL)
			argv->v0.owner = NVDRM_OBJECT_ANY; /* except client */
		else
			argv->v0.owner = NVDRM_OBJECT_USIF;
	} else
		goto done;

	/* USIF slightly abuses some return-only ioctl members in order
	 * to provide interoperability with the older ABI16 objects
	 */
	mutex_lock(&cli->mutex);
	if (argv->v0.route) {
		if (ret = -EINVAL, argv->v0.route == 0xff)
			ret = nouveau_abi16_usif(filp, argv, argc);
		if (ret) {
			mutex_unlock(&cli->mutex);
			goto done;
		}
	}

	switch (argv->v0.type) {
	case NVIF_IOCTL_V0_NEW:
		ret = usif_object_new(filp, data, size, argv, argc);
		break;
	case NVIF_IOCTL_V0_NTFY_NEW:
		ret = usif_notify_new(filp, data, size, argv, argc);
		break;
	case NVIF_IOCTL_V0_NTFY_DEL:
		ret = usif_notify_del(filp, data, size, argv, argc);
		break;
	case NVIF_IOCTL_V0_NTFY_GET:
		ret = usif_notify_get(filp, data, size, argv, argc);
		break;
	case NVIF_IOCTL_V0_NTFY_PUT:
		ret = usif_notify_put(filp, data, size, argv, argc);
		break;
	default:
		ret = nvif_client_ioctl(client, argv, argc);
		break;
	}
	if (argv->v0.route == NVDRM_OBJECT_USIF) {
		object = (void *)(unsigned long)argv->v0.token;
		argv->v0.route = object->route;
		argv->v0.token = object->token;
		if (ret == 0 && argv->v0.type == NVIF_IOCTL_V0_DEL) {
			list_del(&object->head);
			kfree(object);
		}
	} else {
		argv->v0.route = NVIF_IOCTL_V0_ROUTE_HIDDEN;
		argv->v0.token = 0;
	}
	argv->v0.owner = owner;
	mutex_unlock(&cli->mutex);

	if (copy_to_user(user, argv, argc))
		ret = -EFAULT;
done:
	kfree(argv);
	return ret;
}

void
usif_client_fini(struct nouveau_cli *cli)
{
	struct usif_object *object, *otemp;
	struct usif_notify *notify, *ntemp;

	list_for_each_entry_safe(notify, ntemp, &cli->notifys, head) {
		usif_notify_dtor(notify);
	}

	list_for_each_entry_safe(object, otemp, &cli->objects, head) {
		usif_object_dtor(object);
	}
}

void
usif_client_init(struct nouveau_cli *cli)
{
	INIT_LIST_HEAD(&cli->objects);
	INIT_LIST_HEAD(&cli->notifys);
}
