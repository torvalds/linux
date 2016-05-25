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
#include <core/ioctl.h>
#include <core/client.h>
#include <core/engine.h>

#include <nvif/unpack.h>
#include <nvif/ioctl.h>

static int
nvkm_ioctl_nop(struct nvkm_client *client,
	       struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_ioctl_nop_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(object, "nop size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(object, "nop vers %lld\n", args->v0.version);
		args->v0.version = NVIF_VERSION_LATEST;
	}

	return ret;
}

static int
nvkm_ioctl_sclass(struct nvkm_client *client,
		  struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_ioctl_sclass_v0 v0;
	} *args = data;
	struct nvkm_oclass oclass;
	int ret = -ENOSYS, i = 0;

	nvif_ioctl(object, "sclass size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		nvif_ioctl(object, "sclass vers %d count %d\n",
			   args->v0.version, args->v0.count);
		if (size != args->v0.count * sizeof(args->v0.oclass[0]))
			return -EINVAL;

		while (object->func->sclass &&
		       object->func->sclass(object, i, &oclass) >= 0) {
			if (i < args->v0.count) {
				args->v0.oclass[i].oclass = oclass.base.oclass;
				args->v0.oclass[i].minver = oclass.base.minver;
				args->v0.oclass[i].maxver = oclass.base.maxver;
			}
			i++;
		}

		args->v0.count = i;
	}

	return ret;
}

static int
nvkm_ioctl_new(struct nvkm_client *client,
	       struct nvkm_object *parent, void *data, u32 size)
{
	union {
		struct nvif_ioctl_new_v0 v0;
	} *args = data;
	struct nvkm_object *object = NULL;
	struct nvkm_oclass oclass;
	int ret = -ENOSYS, i = 0;

	nvif_ioctl(parent, "new size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		nvif_ioctl(parent, "new vers %d handle %08x class %08x "
				   "route %02x token %llx object %016llx\n",
			   args->v0.version, args->v0.handle, args->v0.oclass,
			   args->v0.route, args->v0.token, args->v0.object);
	} else
		return ret;

	if (!parent->func->sclass) {
		nvif_ioctl(parent, "cannot have children\n");
		return -EINVAL;
	}

	do {
		memset(&oclass, 0x00, sizeof(oclass));
		oclass.client = client;
		oclass.handle = args->v0.handle;
		oclass.object = args->v0.object;
		oclass.parent = parent;
		ret = parent->func->sclass(parent, i++, &oclass);
		if (ret)
			return ret;
	} while (oclass.base.oclass != args->v0.oclass);

	if (oclass.engine) {
		oclass.engine = nvkm_engine_ref(oclass.engine);
		if (IS_ERR(oclass.engine))
			return PTR_ERR(oclass.engine);
	}

	ret = oclass.ctor(&oclass, data, size, &object);
	nvkm_engine_unref(&oclass.engine);
	if (ret == 0) {
		ret = nvkm_object_init(object);
		if (ret == 0) {
			list_add(&object->head, &parent->tree);
			object->route = args->v0.route;
			object->token = args->v0.token;
			object->object = args->v0.object;
			if (nvkm_object_insert(object)) {
				client->data = object;
				return 0;
			}
			ret = -EEXIST;
		}
		nvkm_object_fini(object, false);
	}

	nvkm_object_del(&object);
	return ret;
}

static int
nvkm_ioctl_del(struct nvkm_client *client,
	       struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_ioctl_del none;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(object, "delete size %d\n", size);
	if (!(ret = nvif_unvers(ret, &data, &size, args->none))) {
		nvif_ioctl(object, "delete\n");
		nvkm_object_fini(object, false);
		nvkm_object_del(&object);
	}

	return ret;
}

static int
nvkm_ioctl_mthd(struct nvkm_client *client,
		struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_ioctl_mthd_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(object, "mthd size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		nvif_ioctl(object, "mthd vers %d mthd %02x\n",
			   args->v0.version, args->v0.method);
		ret = nvkm_object_mthd(object, args->v0.method, data, size);
	}

	return ret;
}


static int
nvkm_ioctl_rd(struct nvkm_client *client,
	      struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_ioctl_rd_v0 v0;
	} *args = data;
	union {
		u8  b08;
		u16 b16;
		u32 b32;
	} v;
	int ret = -ENOSYS;

	nvif_ioctl(object, "rd size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(object, "rd vers %d size %d addr %016llx\n",
			   args->v0.version, args->v0.size, args->v0.addr);
		switch (args->v0.size) {
		case 1:
			ret = nvkm_object_rd08(object, args->v0.addr, &v.b08);
			args->v0.data = v.b08;
			break;
		case 2:
			ret = nvkm_object_rd16(object, args->v0.addr, &v.b16);
			args->v0.data = v.b16;
			break;
		case 4:
			ret = nvkm_object_rd32(object, args->v0.addr, &v.b32);
			args->v0.data = v.b32;
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}

static int
nvkm_ioctl_wr(struct nvkm_client *client,
	      struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_ioctl_wr_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(object, "wr size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(object,
			   "wr vers %d size %d addr %016llx data %08x\n",
			   args->v0.version, args->v0.size, args->v0.addr,
			   args->v0.data);
	} else
		return ret;

	switch (args->v0.size) {
	case 1: return nvkm_object_wr08(object, args->v0.addr, args->v0.data);
	case 2: return nvkm_object_wr16(object, args->v0.addr, args->v0.data);
	case 4: return nvkm_object_wr32(object, args->v0.addr, args->v0.data);
	default:
		break;
	}

	return -EINVAL;
}

static int
nvkm_ioctl_map(struct nvkm_client *client,
	       struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_ioctl_map_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(object, "map size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(object, "map vers %d\n", args->v0.version);
		ret = nvkm_object_map(object, &args->v0.handle,
					      &args->v0.length);
	}

	return ret;
}

static int
nvkm_ioctl_unmap(struct nvkm_client *client,
		 struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_ioctl_unmap none;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(object, "unmap size %d\n", size);
	if (!(ret = nvif_unvers(ret, &data, &size, args->none))) {
		nvif_ioctl(object, "unmap\n");
	}

	return ret;
}

static int
nvkm_ioctl_ntfy_new(struct nvkm_client *client,
		    struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_ioctl_ntfy_new_v0 v0;
	} *args = data;
	struct nvkm_event *event;
	int ret = -ENOSYS;

	nvif_ioctl(object, "ntfy new size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		nvif_ioctl(object, "ntfy new vers %d event %02x\n",
			   args->v0.version, args->v0.event);
		ret = nvkm_object_ntfy(object, args->v0.event, &event);
		if (ret == 0) {
			ret = nvkm_client_notify_new(object, event, data, size);
			if (ret >= 0) {
				args->v0.index = ret;
				ret = 0;
			}
		}
	}

	return ret;
}

static int
nvkm_ioctl_ntfy_del(struct nvkm_client *client,
		    struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_ioctl_ntfy_del_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(object, "ntfy del size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(object, "ntfy del vers %d index %d\n",
			   args->v0.version, args->v0.index);
		ret = nvkm_client_notify_del(client, args->v0.index);
	}

	return ret;
}

static int
nvkm_ioctl_ntfy_get(struct nvkm_client *client,
		    struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_ioctl_ntfy_get_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(object, "ntfy get size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(object, "ntfy get vers %d index %d\n",
			   args->v0.version, args->v0.index);
		ret = nvkm_client_notify_get(client, args->v0.index);
	}

	return ret;
}

static int
nvkm_ioctl_ntfy_put(struct nvkm_client *client,
		    struct nvkm_object *object, void *data, u32 size)
{
	union {
		struct nvif_ioctl_ntfy_put_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(object, "ntfy put size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(object, "ntfy put vers %d index %d\n",
			   args->v0.version, args->v0.index);
		ret = nvkm_client_notify_put(client, args->v0.index);
	}

	return ret;
}

static struct {
	int version;
	int (*func)(struct nvkm_client *, struct nvkm_object *, void *, u32);
}
nvkm_ioctl_v0[] = {
	{ 0x00, nvkm_ioctl_nop },
	{ 0x00, nvkm_ioctl_sclass },
	{ 0x00, nvkm_ioctl_new },
	{ 0x00, nvkm_ioctl_del },
	{ 0x00, nvkm_ioctl_mthd },
	{ 0x00, nvkm_ioctl_rd },
	{ 0x00, nvkm_ioctl_wr },
	{ 0x00, nvkm_ioctl_map },
	{ 0x00, nvkm_ioctl_unmap },
	{ 0x00, nvkm_ioctl_ntfy_new },
	{ 0x00, nvkm_ioctl_ntfy_del },
	{ 0x00, nvkm_ioctl_ntfy_get },
	{ 0x00, nvkm_ioctl_ntfy_put },
};

static int
nvkm_ioctl_path(struct nvkm_client *client, u64 handle, u32 type,
		void *data, u32 size, u8 owner, u8 *route, u64 *token)
{
	struct nvkm_object *object;
	int ret;

	object = nvkm_object_search(client, handle, NULL);
	if (IS_ERR(object)) {
		nvif_ioctl(&client->object, "object not found\n");
		return PTR_ERR(object);
	}

	if (owner != NVIF_IOCTL_V0_OWNER_ANY && owner != object->route) {
		nvif_ioctl(&client->object, "route != owner\n");
		return -EACCES;
	}
	*route = object->route;
	*token = object->token;

	if (ret = -EINVAL, type < ARRAY_SIZE(nvkm_ioctl_v0)) {
		if (nvkm_ioctl_v0[type].version == 0)
			ret = nvkm_ioctl_v0[type].func(client, object, data, size);
	}

	return ret;
}

int
nvkm_ioctl(struct nvkm_client *client, bool supervisor,
	   void *data, u32 size, void **hack)
{
	struct nvkm_object *object = &client->object;
	union {
		struct nvif_ioctl_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	client->super = supervisor;
	nvif_ioctl(object, "size %d\n", size);

	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		nvif_ioctl(object,
			   "vers %d type %02x object %016llx owner %02x\n",
			   args->v0.version, args->v0.type, args->v0.object,
			   args->v0.owner);
		ret = nvkm_ioctl_path(client, args->v0.object, args->v0.type,
				      data, size, args->v0.owner,
				      &args->v0.route, &args->v0.token);
	}

	nvif_ioctl(object, "return %d\n", ret);
	if (hack) {
		*hack = client->data;
		client->data = NULL;
	}

	client->super = false;
	return ret;
}
