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
#include <core/engine.h>

int
nvkm_object_mthd(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	if (likely(object->func->mthd))
		return object->func->mthd(object, mthd, data, size);
	return -ENODEV;
}

int
nvkm_object_ntfy(struct nvkm_object *object, u32 mthd,
		 struct nvkm_event **pevent)
{
	if (likely(object->func->ntfy))
		return object->func->ntfy(object, mthd, pevent);
	return -ENODEV;
}

int
nvkm_object_map(struct nvkm_object *object, u64 *addr, u32 *size)
{
	if (likely(object->func->map))
		return object->func->map(object, addr, size);
	return -ENODEV;
}

int
nvkm_object_rd08(struct nvkm_object *object, u64 addr, u8 *data)
{
	if (likely(object->func->rd08))
		return object->func->rd08(object, addr, data);
	return -ENODEV;
}

int
nvkm_object_rd16(struct nvkm_object *object, u64 addr, u16 *data)
{
	if (likely(object->func->rd16))
		return object->func->rd16(object, addr, data);
	return -ENODEV;
}

int
nvkm_object_rd32(struct nvkm_object *object, u64 addr, u32 *data)
{
	if (likely(object->func->rd32))
		return object->func->rd32(object, addr, data);
	return -ENODEV;
}

int
nvkm_object_wr08(struct nvkm_object *object, u64 addr, u8 data)
{
	if (likely(object->func->wr08))
		return object->func->wr08(object, addr, data);
	return -ENODEV;
}

int
nvkm_object_wr16(struct nvkm_object *object, u64 addr, u16 data)
{
	if (likely(object->func->wr16))
		return object->func->wr16(object, addr, data);
	return -ENODEV;
}

int
nvkm_object_wr32(struct nvkm_object *object, u64 addr, u32 data)
{
	if (likely(object->func->wr32))
		return object->func->wr32(object, addr, data);
	return -ENODEV;
}

int
nvkm_object_bind(struct nvkm_object *object, struct nvkm_gpuobj *gpuobj,
		 int align, struct nvkm_gpuobj **pgpuobj)
{
	if (object->func->bind)
		return object->func->bind(object, gpuobj, align, pgpuobj);
	return -ENODEV;
}

int
nvkm_object_fini(struct nvkm_object *object, bool suspend)
{
	if (object->func->fini)
		return object->func->fini(object, suspend);
	return 0;
}

int
nvkm_object_init(struct nvkm_object *object)
{
	if (object->func->init)
		return object->func->init(object);
	return 0;
}

static void
nvkm_object_del(struct nvkm_object **pobject)
{
	struct nvkm_object *object = *pobject;

	if (object && !WARN_ON(!object->func)) {
		if (object->func->dtor)
			*pobject = object->func->dtor(object);
		nvkm_engine_unref(&object->engine);
		kfree(*pobject);
		*pobject = NULL;
	}
}

void
nvkm_object_ctor(const struct nvkm_object_func *func,
		 const struct nvkm_oclass *oclass, struct nvkm_object *object)
{
	object->func = func;
	object->client = oclass->client;
	object->engine = nvkm_engine_ref(oclass->engine);
	object->oclass = oclass->base.oclass;
	object->handle = oclass->handle;
	object->parent = oclass->parent;
	atomic_set(&object->refcount, 1);
	atomic_set(&object->usecount, 0);
}

int
nvkm_object_new_(const struct nvkm_object_func *func,
		 const struct nvkm_oclass *oclass, void *data, u32 size,
		 struct nvkm_object **pobject)
{
	if (size == 0) {
		if (!(*pobject = kzalloc(sizeof(**pobject), GFP_KERNEL)))
			return -ENOMEM;
		nvkm_object_ctor(func, oclass, *pobject);
		return 0;
	}
	return -ENOSYS;
}

static const struct nvkm_object_func
nvkm_object_func = {
};

int
nvkm_object_new(const struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	const struct nvkm_object_func *func =
		oclass->base.func ? oclass->base.func : &nvkm_object_func;
	return nvkm_object_new_(func, oclass, data, size, pobject);
}

void
nvkm_object_ref(struct nvkm_object *obj, struct nvkm_object **ref)
{
	if (obj) {
		atomic_inc(&obj->refcount);
	}

	if (*ref) {
		int dead = atomic_dec_and_test(&(*ref)->refcount);
		if (dead)
			nvkm_object_del(ref);
	}

	*ref = obj;
}

int
nvkm_object_inc(struct nvkm_object *object)
{
	int ref = atomic_add_return(1, &object->usecount);
	int ret;

	if (ref != 1)
		return 0;

	if (object->parent) {
		ret = nvkm_object_inc(object->parent);
		if (ret)
			goto fail_parent;
	}

	ret = nvkm_object_init(object);
	atomic_set(&object->usecount, 1);
	if (ret)
		goto fail_self;

	return 0;

fail_self:
	if (object->parent)
		 nvkm_object_dec(object->parent, false);
fail_parent:
	atomic_dec(&object->usecount);
	return ret;
}

static int
nvkm_object_decf(struct nvkm_object *object)
{
	nvkm_object_fini(object, false);
	atomic_set(&object->usecount, 0);

	if (object->parent)
		nvkm_object_dec(object->parent, false);

	return 0;
}

static int
nvkm_object_decs(struct nvkm_object *object)
{
	int ret;

	ret = nvkm_object_fini(object, true);
	atomic_set(&object->usecount, 0);
	if (ret)
		return ret;

	if (object->parent) {
		ret = nvkm_object_dec(object->parent, true);
		if (ret)
			goto fail_parent;
	}

	return 0;

fail_parent:
	nvkm_object_init(object);

	return ret;
}

int
nvkm_object_dec(struct nvkm_object *object, bool suspend)
{
	int ref = atomic_add_return(-1, &object->usecount);
	int ret;

	if (ref == 0) {
		if (suspend)
			ret = nvkm_object_decs(object);
		else
			ret = nvkm_object_decf(object);

		if (ret) {
			atomic_inc(&object->usecount);
			return ret;
		}
	}

	return 0;
}
