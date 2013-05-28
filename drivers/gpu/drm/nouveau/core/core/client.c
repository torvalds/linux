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

#include <engine/device.h>

static void
nouveau_client_dtor(struct nouveau_object *object)
{
	struct nouveau_client *client = (void *)object;
	nouveau_object_ref(NULL, &client->device);
	nouveau_handle_destroy(client->root);
	nouveau_namedb_destroy(&client->base);
}

static struct nouveau_oclass
nouveau_client_oclass = {
	.ofuncs = &(struct nouveau_ofuncs) {
		.dtor = nouveau_client_dtor,
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
	int ret;

	nv_debug(client, "%s running\n", name[suspend]);
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
