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
#include <core/option.h>

#include <nvif/class.h>
#include <nvif/event.h>
#include <nvif/if0000.h>
#include <nvif/unpack.h>

static int
nvkm_uclient_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		 struct nvkm_object **pobject)
{
	union {
		struct nvif_client_v0 v0;
	} *args = argv;
	struct nvkm_client *client;
	int ret = -ENOSYS;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))){
		args->v0.name[sizeof(args->v0.name) - 1] = 0;
		ret = nvkm_client_new(args->v0.name, oclass->client->device, NULL,
				      NULL, oclass->client->event, &client);
		if (ret)
			return ret;
	} else
		return ret;

	client->object.client = oclass->client;
	client->object.handle = oclass->handle;
	client->object.object = oclass->object;
	client->debug = oclass->client->debug;
	*pobject = &client->object;
	return 0;
}

static const struct nvkm_sclass
nvkm_uclient_sclass = {
	.oclass = NVIF_CLASS_CLIENT,
	.minver = 0,
	.maxver = 0,
	.ctor = nvkm_uclient_new,
};

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
	case 0: sclass = &nvkm_uclient_sclass; break;
	case 1: sclass = &nvkm_udevice_sclass; break;
	default:
		return -EINVAL;
	}

	oclass->ctor = nvkm_client_child_new;
	oclass->base = *sclass;
	return 0;
}

static void *
nvkm_client_dtor(struct nvkm_object *object)
{
	return nvkm_client(object);
}

static const struct nvkm_object_func
nvkm_client = {
	.dtor = nvkm_client_dtor,
	.sclass = nvkm_client_child_get,
};

int
nvkm_client_new(const char *name, u64 device, const char *cfg, const char *dbg,
		int (*event)(u64, void *, u32), struct nvkm_client **pclient)
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
	spin_lock_init(&client->obj_lock);
	client->event = event;
	INIT_LIST_HEAD(&client->umem);
	spin_lock_init(&client->lock);
	return 0;
}
