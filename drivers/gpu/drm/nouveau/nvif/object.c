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

#include <nvif/object.h>
#include <nvif/client.h>
#include <nvif/driver.h>
#include <nvif/ioctl.h>

int
nvif_object_ioctl(struct nvif_object *object, void *data, u32 size, void **hack)
{
	struct nvif_client *client = nvif_client(object);
	union {
		struct nvif_ioctl_v0 v0;
	} *args = data;

	if (size >= sizeof(*args) && args->v0.version == 0) {
		args->v0.owner = NVIF_IOCTL_V0_OWNER_ANY;
		args->v0.path_nr = 0;
		while (args->v0.path_nr < ARRAY_SIZE(args->v0.path)) {
			args->v0.path[args->v0.path_nr++] = object->handle;
			if (object->parent == object)
				break;
			object = object->parent;
		}
	} else
		return -ENOSYS;

	return client->driver->ioctl(client->base.priv, client->super, data, size, hack);
}

int
nvif_object_sclass(struct nvif_object *object, u32 *oclass, int count)
{
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_sclass_v0 sclass;
	} *args;
	u32 size = count * sizeof(args->sclass.oclass[0]);
	int ret;

	if (!(args = kmalloc(sizeof(*args) + size, GFP_KERNEL)))
		return -ENOMEM;
	args->ioctl.version = 0;
	args->ioctl.type = NVIF_IOCTL_V0_SCLASS;
	args->sclass.version = 0;
	args->sclass.count = count;

	memcpy(args->sclass.oclass, oclass, size);
	ret = nvif_object_ioctl(object, args, sizeof(*args) + size, NULL);
	ret = ret ? ret : args->sclass.count;
	memcpy(oclass, args->sclass.oclass, size);
	kfree(args);
	return ret;
}

u32
nvif_object_rd(struct nvif_object *object, int size, u64 addr)
{
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_rd_v0 rd;
	} args = {
		.ioctl.type = NVIF_IOCTL_V0_RD,
		.rd.size = size,
		.rd.addr = addr,
	};
	int ret = nvif_object_ioctl(object, &args, sizeof(args), NULL);
	if (ret) {
		/*XXX: warn? */
		return 0;
	}
	return args.rd.data;
}

void
nvif_object_wr(struct nvif_object *object, int size, u64 addr, u32 data)
{
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_wr_v0 wr;
	} args = {
		.ioctl.type = NVIF_IOCTL_V0_WR,
		.wr.size = size,
		.wr.addr = addr,
		.wr.data = data,
	};
	int ret = nvif_object_ioctl(object, &args, sizeof(args), NULL);
	if (ret) {
		/*XXX: warn? */
	}
}

int
nvif_object_mthd(struct nvif_object *object, u32 mthd, void *data, u32 size)
{
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_mthd_v0 mthd;
	} *args;
	u8 stack[128];
	int ret;

	if (sizeof(*args) + size > sizeof(stack)) {
		if (!(args = kmalloc(sizeof(*args) + size, GFP_KERNEL)))
			return -ENOMEM;
	} else {
		args = (void *)stack;
	}
	args->ioctl.version = 0;
	args->ioctl.type = NVIF_IOCTL_V0_MTHD;
	args->mthd.version = 0;
	args->mthd.method = mthd;

	memcpy(args->mthd.data, data, size);
	ret = nvif_object_ioctl(object, args, sizeof(*args) + size, NULL);
	memcpy(data, args->mthd.data, size);
	if (args != (void *)stack)
		kfree(args);
	return ret;
}

void
nvif_object_unmap(struct nvif_object *object)
{
	if (object->map.size) {
		struct nvif_client *client = nvif_client(object);
		struct {
			struct nvif_ioctl_v0 ioctl;
			struct nvif_ioctl_unmap unmap;
		} args = {
			.ioctl.type = NVIF_IOCTL_V0_UNMAP,
		};

		if (object->map.ptr) {
			client->driver->unmap(client, object->map.ptr,
						      object->map.size);
			object->map.ptr = NULL;
		}

		nvif_object_ioctl(object, &args, sizeof(args), NULL);
		object->map.size = 0;
	}
}

int
nvif_object_map(struct nvif_object *object)
{
	struct nvif_client *client = nvif_client(object);
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_map_v0 map;
	} args = {
		.ioctl.type = NVIF_IOCTL_V0_MAP,
	};
	int ret = nvif_object_ioctl(object, &args, sizeof(args), NULL);
	if (ret == 0) {
		object->map.size = args.map.length;
		object->map.ptr = client->driver->map(client, args.map.handle,
						      object->map.size);
		if (ret = -ENOMEM, object->map.ptr)
			return 0;
		nvif_object_unmap(object);
	}
	return ret;
}

struct ctor {
	struct nvif_ioctl_v0 ioctl;
	struct nvif_ioctl_new_v0 new;
};

void
nvif_object_fini(struct nvif_object *object)
{
	struct ctor *ctor = container_of(object->data, typeof(*ctor), new.data);
	if (object->parent) {
		struct {
			struct nvif_ioctl_v0 ioctl;
			struct nvif_ioctl_del del;
		} args = {
			.ioctl.type = NVIF_IOCTL_V0_DEL,
		};

		nvif_object_unmap(object);
		nvif_object_ioctl(object, &args, sizeof(args), NULL);
		if (object->data) {
			object->size = 0;
			object->data = NULL;
			kfree(ctor);
		}
		nvif_object_ref(NULL, &object->parent);
	}
}

int
nvif_object_init(struct nvif_object *parent, void (*dtor)(struct nvif_object *),
		 u32 handle, u32 oclass, void *data, u32 size,
		 struct nvif_object *object)
{
	struct ctor *ctor;
	int ret = 0;

	object->parent = NULL;
	object->object = object;
	nvif_object_ref(parent, &object->parent);
	kref_init(&object->refcount);
	object->handle = handle;
	object->oclass = oclass;
	object->data = NULL;
	object->size = 0;
	object->dtor = dtor;
	object->map.ptr = NULL;
	object->map.size = 0;

	if (object->parent) {
		if (!(ctor = kmalloc(sizeof(*ctor) + size, GFP_KERNEL))) {
			nvif_object_fini(object);
			return -ENOMEM;
		}
		object->data = ctor->new.data;
		object->size = size;
		memcpy(object->data, data, size);

		ctor->ioctl.version = 0;
		ctor->ioctl.type = NVIF_IOCTL_V0_NEW;
		ctor->new.version = 0;
		ctor->new.route = NVIF_IOCTL_V0_ROUTE_NVIF;
		ctor->new.token = (unsigned long)(void *)object;
		ctor->new.handle = handle;
		ctor->new.oclass = oclass;

		ret = nvif_object_ioctl(parent, ctor, sizeof(*ctor) +
					object->size, &object->priv);
	}

	if (ret)
		nvif_object_fini(object);
	return ret;
}

static void
nvif_object_del(struct nvif_object *object)
{
	nvif_object_fini(object);
	kfree(object);
}

int
nvif_object_new(struct nvif_object *parent, u32 handle, u32 oclass,
		void *data, u32 size, struct nvif_object **pobject)
{
	struct nvif_object *object = kzalloc(sizeof(*object), GFP_KERNEL);
	if (object) {
		int ret = nvif_object_init(parent, nvif_object_del, handle,
					   oclass, data, size, object);
		if (ret) {
			kfree(object);
			object = NULL;
		}
		*pobject = object;
		return ret;
	}
	return -ENOMEM;
}

static void
nvif_object_put(struct kref *kref)
{
	struct nvif_object *object =
		container_of(kref, typeof(*object), refcount);
	object->dtor(object);
}

void
nvif_object_ref(struct nvif_object *object, struct nvif_object **pobject)
{
	if (object)
		kref_get(&object->refcount);
	if (*pobject)
		kref_put(&(*pobject)->refcount, nvif_object_put);
	*pobject = object;
}
