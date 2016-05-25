/*
 * Copyright 2012 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */
#include <core/client.h>
#include <core/device.h>
#include <core/notify.h>
#include <core/option.h>

#include <nvif/class.h>
#include <nvif/event.h>
#include <nvif/if0000.h>
#include <nvif/unpack.h>

static const struct nvkm_sclass
nvkm_uclient_sclass = {
	.oclass = NVIF_CLASS_CLIENT,
};

struct nvkm_client_notify {
	struct nvkm_client *client;
	struct nvkm_notify n;
	u8 version;
	u8 size;
	union {
		struct nvif_notify_rep_v0 v0;
	} rep;
};

static int
nvkm_client_notify(struct nvkm_notify *n)
{
	struct nvkm_client_notify *notify = container_of(n, typeof(*notify), n);
	struct nvkm_client *client = notify->client;
	return client->ntfy(&notify->rep, notify->size, n->data, n->size);
}

int
nvkm_client_notify_put(struct nvkm_client *client, int index)
{
	if (index < ARRAY_SIZE(client->notify)) {
		if (client->notify[index]) {
			nvkm_notify_put(&client->notify[index]->n);
			return 0;
		}
	}
	return -ENOENT;
}

int
nvkm_client_notify_get(struct nvkm_client *client, int index)
{
	if (index < ARRAY_SIZE(client->notify)) {
		if (client->notify[index]) {
			nvkm_notify_get(&client->notify[index]->n);
			return 0;
		}
	}
	return -ENOENT;
}

int
nvkm_client_notify_del(struct nvkm_client *client, int index)
{
	if (index < ARRAY_SIZE(client->notify)) {
		if (client->notify[index]) {
			nvkm_notify_fini(&client->notify[index]->n);
			kfree(client->notify[index]);
			client->notify[index] = NULL;
			return 0;
		}
	}
	return -ENOENT;
}

int
nvkm_client_notify_new(struct nvkm_object *object,
		       struct nvkm_event *event, void *data, u32 size)
{
	struct nvkm_client *client = object->client;
	struct nvkm_client_notify *notify;
	union {
		struct nvif_notify_req_v0 v0;
	} *req = data;
	u8  index, reply;
	int ret = -ENOSYS;

	for (index = 0; index < ARRAY_SIZE(client->notify); index++) {
		if (!client->notify[index])
			break;
	}

	if (index == ARRAY_SIZE(client->notify))
		return -ENOSPC;

	notify = kzalloc(sizeof(*notify), GFP_KERNEL);
	if (!notify)
		return -ENOMEM;

	nvif_ioctl(object, "notify new size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, req->v0, 0, 0, true))) {
		nvif_ioctl(object, "notify new vers %d reply %d route %02x "
				   "token %llx\n", req->v0.version,
			   req->v0.reply, req->v0.route, req->v0.token);
		notify->version = req->v0.version;
		notify->size = sizeof(notify->rep.v0);
		notify->rep.v0.version = req->v0.version;
		notify->rep.v0.route = req->v0.route;
		notify->rep.v0.token = req->v0.token;
		reply = req->v0.reply;
	}

	if (ret == 0) {
		ret = nvkm_notify_init(object, event, nvkm_client_notify,
				       false, data, size, reply, &notify->n);
		if (ret == 0) {
			client->notify[index] = notify;
			notify->client = client;
			return index;
		}
	}

	kfree(notify);
	return ret;
}

static int
nvkm_client_mthd_devlist(struct nvkm_client *client, void *data, u32 size)
{
	union {
		struct nvif_client_devlist_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(&client->object, "client devlist size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		nvif_ioctl(&client->object, "client devlist vers %d count %d\n",
			   args->v0.version, args->v0.count);
		if (size == sizeof(args->v0.device[0]) * args->v0.count) {
			ret = nvkm_device_list(args->v0.device, args->v0.count);
			if (ret >= 0) {
				args->v0.count = ret;
				ret = 0;
			}
		} else {
			ret = -EINVAL;
		}
	}

	return ret;
}

static int
nvkm_client_mthd(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	struct nvkm_client *client = nvkm_client(object);
	switch (mthd) {
	case NVIF_CLIENT_V0_DEVLIST:
		return nvkm_client_mthd_devlist(client, data, size);
	default:
		break;
	}
	return -EINVAL;
}

static int
nvkm_client_child_new(const struct nvkm_oclass *oclass,
		      void *data, u32 size, struct nvkm_object **pobject)
{
	return oclass->base.ctor(oclass, data, size, pobject);
}

static int
nvkm_client_child_get(struct nvkm_object *object, int index,
		      struct nvkm_oclass *oclass)
{
	const struct nvkm_sclass *sclass;

	switch (index) {
	case 0: sclass = &nvkm_udevice_sclass; break;
	default:
		return -EINVAL;
	}

	oclass->ctor = nvkm_client_child_new;
	oclass->base = *sclass;
	return 0;
}

static int
nvkm_client_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_client *client = nvkm_client(object);
	const char *name[2] = { "fini", "suspend" };
	int i;
	nvif_debug(object, "%s notify\n", name[suspend]);
	for (i = 0; i < ARRAY_SIZE(client->notify); i++)
		nvkm_client_notify_put(client, i);
	return 0;
}

static void *
nvkm_client_dtor(struct nvkm_object *object)
{
	struct nvkm_client *client = nvkm_client(object);
	int i;
	for (i = 0; i < ARRAY_SIZE(client->notify); i++)
		nvkm_client_notify_del(client, i);
	return client;
}

static const struct nvkm_object_func
nvkm_client = {
	.dtor = nvkm_client_dtor,
	.fini = nvkm_client_fini,
	.mthd = nvkm_client_mthd,
	.sclass = nvkm_client_child_get,
};

int
nvkm_client_new(const char *name, u64 device, const char *cfg,
		const char *dbg,
		int (*ntfy)(const void *, u32, const void *, u32),
		struct nvkm_client **pclient)
{
	struct nvkm_oclass oclass = { .base = nvkm_uclient_sclass };
	struct nvkm_client *client;

	if (!(client = *pclient = kzalloc(sizeof(*client), GFP_KERNEL)))
		return -ENOMEM;
	oclass.client = client;

	nvkm_object_ctor(&nvkm_client, &oclass, &client->object);
	snprintf(client->name, sizeof(client->name), "%s", name);
	client->device = device;
	client->debug = nvkm_dbgopt(dbg, "CLIENT");
	client->objroot = RB_ROOT;
	client->ntfy = ntfy;
	return 0;
}
