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

#include <core/object.h>
#include <core/client.h>
#include <core/handle.h>
#include <core/option.h>
#include <nvif/unpack.h>
#include <nvif/class.h>

#include <nvif/unpack.h>
#include <nvif/event.h>

#include <engine/device.h>

struct nvkm_client_notify {
	struct nouveau_client *client;
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
	struct nouveau_client *client = notify->client;
	return client->ntfy(&notify->rep, notify->size, n->data, n->size);
}

int
nvkm_client_notify_put(struct nouveau_client *client, int index)
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
nvkm_client_notify_get(struct nouveau_client *client, int index)
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
nvkm_client_notify_del(struct nouveau_client *client, int index)
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
nvkm_client_notify_new(struct nouveau_object *object,
		       struct nvkm_event *event, void *data, u32 size)
{
	struct nouveau_client *client = nouveau_client(object);
	struct nvkm_client_notify *notify;
	union {
		struct nvif_notify_req_v0 v0;
	} *req = data;
	u8  index, reply;
	int ret;

	for (index = 0; index < ARRAY_SIZE(client->notify); index++) {
		if (!client->notify[index])
			break;
	}

	if (index == ARRAY_SIZE(client->notify))
		return -ENOSPC;

	notify = kzalloc(sizeof(*notify), GFP_KERNEL);
	if (!notify)
		return -ENOMEM;

	nv_ioctl(client, "notify new size %d\n", size);
	if (nvif_unpack(req->v0, 0, 0, true)) {
		nv_ioctl(client, "notify new vers %d reply %d route %02x "
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
nouveau_client_devlist(struct nouveau_object *object, void *data, u32 size)
{
	union {
		struct nv_client_devlist_v0 v0;
	} *args = data;
	int ret;

	nv_ioctl(object, "client devlist size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, true)) {
		nv_ioctl(object, "client devlist vers %d count %d\n",
			 args->v0.version, args->v0.count);
		if (size == sizeof(args->v0.device[0]) * args->v0.count) {
			ret = nouveau_device_list(args->v0.device,
						  args->v0.count);
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
nouveau_client_mthd(struct nouveau_object *object, u32 mthd,
		    void *data, u32 size)
{
	switch (mthd) {
	case NV_CLIENT_DEVLIST:
		return nouveau_client_devlist(object, data, size);
	default:
		break;
	}
	return -EINVAL;
}

static void
nouveau_client_dtor(struct nouveau_object *object)
{
	struct nouveau_client *client = (void *)object;
	int i;
	for (i = 0; i < ARRAY_SIZE(client->notify); i++)
		nvkm_client_notify_del(client, i);
	nouveau_object_ref(NULL, &client->device);
	nouveau_handle_destroy(client->root);
	nouveau_namedb_destroy(&client->base);
}

static struct nouveau_oclass
nouveau_client_oclass = {
	.ofuncs = &(struct nouveau_ofuncs) {
		.dtor = nouveau_client_dtor,
		.mthd = nouveau_client_mthd,
	},
};

int
nouveau_client_create_(const char *name, u64 devname, const char *cfg,
		       const char *dbg, int length, void **pobject)
{
	struct nouveau_object *device;
	struct nouveau_client *client;
	int ret;

	device = (void *)nouveau_device_find(devname);
	if (!device)
		return -ENODEV;

	ret = nouveau_namedb_create_(NULL, NULL, &nouveau_client_oclass,
				     NV_CLIENT_CLASS, NULL,
				     (1ULL << NVDEV_ENGINE_DEVICE),
				     length, pobject);
	client = *pobject;
	if (ret)
		return ret;

	ret = nouveau_handle_create(nv_object(client), ~0, ~0,
				    nv_object(client), &client->root);
	if (ret)
		return ret;

	/* prevent init/fini being called, os in in charge of this */
	atomic_set(&nv_object(client)->usecount, 2);

	nouveau_object_ref(device, &client->device);
	snprintf(client->name, sizeof(client->name), "%s", name);
	client->debug = nouveau_dbgopt(dbg, "CLIENT");
	return 0;
}

int
nouveau_client_init(struct nouveau_client *client)
{
	int ret;
	nv_debug(client, "init running\n");
	ret = nouveau_handle_init(client->root);
	nv_debug(client, "init completed with %d\n", ret);
	return ret;
}

int
nouveau_client_fini(struct nouveau_client *client, bool suspend)
{
	const char *name[2] = { "fini", "suspend" };
	int ret, i;
	nv_debug(client, "%s running\n", name[suspend]);
	nv_debug(client, "%s notify\n", name[suspend]);
	for (i = 0; i < ARRAY_SIZE(client->notify); i++)
		nvkm_client_notify_put(client, i);
	nv_debug(client, "%s object\n", name[suspend]);
	ret = nouveau_handle_fini(client->root, suspend);
	nv_debug(client, "%s completed with %d\n", name[suspend], ret);
	return ret;
}

const char *
nouveau_client_name(void *obj)
{
	const char *client_name = "unknown";
	struct nouveau_client *client = nouveau_client(obj);
	if (client)
		client_name = client->name;
	return client_name;
}
