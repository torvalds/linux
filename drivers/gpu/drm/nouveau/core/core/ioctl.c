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

#include <core/object.h>
#include <core/parent.h>
#include <core/handle.h>
#include <core/namedb.h>
#include <core/client.h>
#include <core/device.h>
#include <core/ioctl.h>
#include <core/event.h>

#include <nvif/unpack.h>
#include <nvif/ioctl.h>

static int
nvkm_ioctl_nop(struct nouveau_handle *handle, void *data, u32 size)
{
	struct nouveau_object *object = handle->object;
	union {
		struct nvif_ioctl_nop none;
	} *args = data;
	int ret;

	nv_ioctl(object, "nop size %d\n", size);
	if (nvif_unvers(args->none)) {
		nv_ioctl(object, "nop\n");
	}

	return ret;
}

static int
nvkm_ioctl_sclass(struct nouveau_handle *handle, void *data, u32 size)
{
	struct nouveau_object *object = handle->object;
	union {
		struct nvif_ioctl_sclass_v0 v0;
	} *args = data;
	int ret;

	if (!nv_iclass(object, NV_PARENT_CLASS)) {
		nv_debug(object, "cannot have children (sclass)\n");
		return -ENODEV;
	}

	nv_ioctl(object, "sclass size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, true)) {
		nv_ioctl(object, "sclass vers %d count %d\n",
			 args->v0.version, args->v0.count);
		if (size == args->v0.count * sizeof(args->v0.oclass[0])) {
			ret = nouveau_parent_lclass(object, args->v0.oclass,
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
nvkm_ioctl_new(struct nouveau_handle *parent, void *data, u32 size)
{
	union {
		struct nvif_ioctl_new_v0 v0;
	} *args = data;
	struct nouveau_client *client = nouveau_client(parent->object);
	struct nouveau_object *engctx = NULL;
	struct nouveau_object *object = NULL;
	struct nouveau_object *engine;
	struct nouveau_oclass *oclass;
	struct nouveau_handle *handle;
	u32 _handle, _oclass;
	int ret;

	nv_ioctl(client, "new size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, true)) {
		_handle = args->v0.handle;
		_oclass = args->v0.oclass;
	} else
		return ret;

	nv_ioctl(client, "new vers %d handle %08x class %08x "
			 "route %02x token %llx\n",
		args->v0.version, _handle, _oclass,
		args->v0.route, args->v0.token);

	if (!nv_iclass(parent->object, NV_PARENT_CLASS)) {
		nv_debug(parent->object, "cannot have children (ctor)\n");
		ret = -ENODEV;
		goto fail_class;
	}

	/* check that parent supports the requested subclass */
	ret = nouveau_parent_sclass(parent->object, _oclass, &engine, &oclass);
	if (ret) {
		nv_debug(parent->object, "illegal class 0x%04x\n", _oclass);
		goto fail_class;
	}

	/* make sure engine init has been completed *before* any objects
	 * it controls are created - the constructors may depend on
	 * state calculated at init (ie. default context construction)
	 */
	if (engine) {
		ret = nouveau_object_inc(engine);
		if (ret)
			goto fail_class;
	}

	/* if engine requires it, create a context object to insert
	 * between the parent and its children (eg. PGRAPH context)
	 */
	if (engine && nv_engine(engine)->cclass) {
		ret = nouveau_object_ctor(parent->object, engine,
					  nv_engine(engine)->cclass,
					  data, size, &engctx);
		if (ret)
			goto fail_engctx;
	} else {
		nouveau_object_ref(parent->object, &engctx);
	}

	/* finally, create new object and bind it to its handle */
	ret = nouveau_object_ctor(engctx, engine, oclass, data, size, &object);
	client->data = object;
	if (ret)
		goto fail_ctor;

	ret = nouveau_object_inc(object);
	if (ret)
		goto fail_init;

	ret = nouveau_handle_create(parent->object, parent->name,
				    _handle, object, &handle);
	if (ret)
		goto fail_handle;

	ret = nouveau_handle_init(handle);
	handle->route = args->v0.route;
	handle->token = args->v0.token;
	if (ret)
		nouveau_handle_destroy(handle);

fail_handle:
	nouveau_object_dec(object, false);
fail_init:
	nouveau_object_ref(NULL, &object);
fail_ctor:
	nouveau_object_ref(NULL, &engctx);
fail_engctx:
	if (engine)
		nouveau_object_dec(engine, false);
fail_class:
	return ret;
}

static int
nvkm_ioctl_del(struct nouveau_handle *handle, void *data, u32 size)
{
	struct nouveau_object *object = handle->object;
	union {
		struct nvif_ioctl_del none;
	} *args = data;
	int ret;

	nv_ioctl(object, "delete size %d\n", size);
	if (nvif_unvers(args->none)) {
		nv_ioctl(object, "delete\n");
		nouveau_handle_fini(handle, false);
		nouveau_handle_destroy(handle);
	}

	return ret;
}

static int
nvkm_ioctl_mthd(struct nouveau_handle *handle, void *data, u32 size)
{
	struct nouveau_object *object = handle->object;
	struct nouveau_ofuncs *ofuncs = object->oclass->ofuncs;
	union {
		struct nvif_ioctl_mthd_v0 v0;
	} *args = data;
	int ret;

	nv_ioctl(object, "mthd size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, true)) {
		nv_ioctl(object, "mthd vers %d mthd %02x\n",
			 args->v0.version, args->v0.method);
		if (ret = -ENODEV, ofuncs->mthd)
			ret = ofuncs->mthd(object, args->v0.method, data, size);
	}

	return ret;
}


static int
nvkm_ioctl_rd(struct nouveau_handle *handle, void *data, u32 size)
{
	struct nouveau_object *object = handle->object;
	struct nouveau_ofuncs *ofuncs = object->oclass->ofuncs;
	union {
		struct nvif_ioctl_rd_v0 v0;
	} *args = data;
	int ret;

	nv_ioctl(object, "rd size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "rd vers %d size %d addr %016llx\n",
			args->v0.version, args->v0.size, args->v0.addr);
		switch (args->v0.size) {
		case 1:
			if (ret = -ENODEV, ofuncs->rd08) {
				args->v0.data = nv_ro08(object, args->v0.addr);
				ret = 0;
			}
			break;
		case 2:
			if (ret = -ENODEV, ofuncs->rd16) {
				args->v0.data = nv_ro16(object, args->v0.addr);
				ret = 0;
			}
			break;
		case 4:
			if (ret = -ENODEV, ofuncs->rd32) {
				args->v0.data = nv_ro32(object, args->v0.addr);
				ret = 0;
			}
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}

static int
nvkm_ioctl_wr(struct nouveau_handle *handle, void *data, u32 size)
{
	struct nouveau_object *object = handle->object;
	struct nouveau_ofuncs *ofuncs = object->oclass->ofuncs;
	union {
		struct nvif_ioctl_wr_v0 v0;
	} *args = data;
	int ret;

	nv_ioctl(object, "wr size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "wr vers %d size %d addr %016llx data %08x\n",
			 args->v0.version, args->v0.size, args->v0.addr,
			 args->v0.data);
		switch (args->v0.size) {
		case 1:
			if (ret = -ENODEV, ofuncs->wr08) {
				nv_wo08(object, args->v0.addr, args->v0.data);
				ret = 0;
			}
			break;
		case 2:
			if (ret = -ENODEV, ofuncs->wr16) {
				nv_wo16(object, args->v0.addr, args->v0.data);
				ret = 0;
			}
			break;
		case 4:
			if (ret = -ENODEV, ofuncs->wr32) {
				nv_wo32(object, args->v0.addr, args->v0.data);
				ret = 0;
			}
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}

static int
nvkm_ioctl_map(struct nouveau_handle *handle, void *data, u32 size)
{
	struct nouveau_object *object = handle->object;
	struct nouveau_ofuncs *ofuncs = object->oclass->ofuncs;
	union {
		struct nvif_ioctl_map_v0 v0;
	} *args = data;
	int ret;

	nv_ioctl(object, "map size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "map vers %d\n", args->v0.version);
		if (ret = -ENODEV, ofuncs->map) {
			ret = ofuncs->map(object, &args->v0.handle,
						  &args->v0.length);
		}
	}

	return ret;
}

static int
nvkm_ioctl_unmap(struct nouveau_handle *handle, void *data, u32 size)
{
	struct nouveau_object *object = handle->object;
	union {
		struct nvif_ioctl_unmap none;
	} *args = data;
	int ret;

	nv_ioctl(object, "unmap size %d\n", size);
	if (nvif_unvers(args->none)) {
		nv_ioctl(object, "unmap\n");
	}

	return ret;
}

static int
nvkm_ioctl_ntfy_new(struct nouveau_handle *handle, void *data, u32 size)
{
	struct nouveau_object *object = handle->object;
	struct nouveau_ofuncs *ofuncs = object->oclass->ofuncs;
	union {
		struct nvif_ioctl_ntfy_new_v0 v0;
	} *args = data;
	struct nvkm_event *event;
	int ret;

	nv_ioctl(object, "ntfy new size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, true)) {
		nv_ioctl(object, "ntfy new vers %d event %02x\n",
			 args->v0.version, args->v0.event);
		if (ret = -ENODEV, ofuncs->ntfy)
			ret = ofuncs->ntfy(object, args->v0.event, &event);
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
nvkm_ioctl_ntfy_del(struct nouveau_handle *handle, void *data, u32 size)
{
	struct nouveau_client *client = nouveau_client(handle->object);
	struct nouveau_object *object = handle->object;
	union {
		struct nvif_ioctl_ntfy_del_v0 v0;
	} *args = data;
	int ret;

	nv_ioctl(object, "ntfy del size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "ntfy del vers %d index %d\n",
			 args->v0.version, args->v0.index);
		ret = nvkm_client_notify_del(client, args->v0.index);
	}

	return ret;
}

static int
nvkm_ioctl_ntfy_get(struct nouveau_handle *handle, void *data, u32 size)
{
	struct nouveau_client *client = nouveau_client(handle->object);
	struct nouveau_object *object = handle->object;
	union {
		struct nvif_ioctl_ntfy_get_v0 v0;
	} *args = data;
	int ret;

	nv_ioctl(object, "ntfy get size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "ntfy get vers %d index %d\n",
			 args->v0.version, args->v0.index);
		ret = nvkm_client_notify_get(client, args->v0.index);
	}

	return ret;
}

static int
nvkm_ioctl_ntfy_put(struct nouveau_handle *handle, void *data, u32 size)
{
	struct nouveau_client *client = nouveau_client(handle->object);
	struct nouveau_object *object = handle->object;
	union {
		struct nvif_ioctl_ntfy_put_v0 v0;
	} *args = data;
	int ret;

	nv_ioctl(object, "ntfy put size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "ntfy put vers %d index %d\n",
			 args->v0.version, args->v0.index);
		ret = nvkm_client_notify_put(client, args->v0.index);
	}

	return ret;
}

static struct {
	int version;
	int (*func)(struct nouveau_handle *, void *, u32);
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
nvkm_ioctl_path(struct nouveau_handle *parent, u32 type, u32 nr,
		  u32 *path, void *data, u32 size,
		  u8 owner, u8 *route, u64 *token)
{
	struct nouveau_handle *handle = parent;
	struct nouveau_namedb *namedb;
	struct nouveau_object *object;
	int ret;

	while ((object = parent->object), nr--) {
		nv_ioctl(object, "path 0x%08x\n", path[nr]);
		if (!nv_iclass(object, NV_PARENT_CLASS)) {
			nv_debug(object, "cannot have children (path)\n");
			return -EINVAL;
		}

		if (!(namedb = (void *)nv_pclass(object, NV_NAMEDB_CLASS)) ||
		    !(handle = nouveau_namedb_get(namedb, path[nr]))) {
			nv_debug(object, "handle 0x%08x not found\n", path[nr]);
			return -ENOENT;
		}
		nouveau_namedb_put(handle);
		parent = handle;
	}

	if (owner != NVIF_IOCTL_V0_OWNER_ANY &&
	    owner != handle->route) {
		nv_ioctl(object, "object route != owner\n");
		return -EACCES;
	}
	*route = handle->route;
	*token = handle->token;

	if (ret = -EINVAL, type < ARRAY_SIZE(nvkm_ioctl_v0)) {
		if (nvkm_ioctl_v0[type].version == 0) {
			ret = nvkm_ioctl_v0[type].func(handle, data, size);
		}
	}

	return ret;
}

int
nvkm_ioctl(struct nouveau_client *client, bool supervisor,
	   void *data, u32 size, void **hack)
{
	union {
		struct nvif_ioctl_v0 v0;
	} *args = data;
	int ret;

	client->super = supervisor;
	nv_ioctl(client, "size %d\n", size);

	if (nvif_unpack(args->v0, 0, 0, true)) {
		nv_ioctl(client, "vers %d type %02x path %d owner %02x\n",
			 args->v0.version, args->v0.type, args->v0.path_nr,
			 args->v0.owner);
		ret = nvkm_ioctl_path(client->root, args->v0.type,
				      args->v0.path_nr, args->v0.path,
				      data, size, args->v0.owner,
				     &args->v0.route, &args->v0.token);
	}

	nv_ioctl(client, "return %d\n", ret);
	if (hack) {
		*hack = client->data;
		client->data = NULL;
	}
	client->super = false;
	return ret;
}
