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
nvkm_object_rd08(struct nvkm_object *object, u64 addr, u8 *data)
{
	const struct nvkm_oclass *oclass = object->oclass;
	if (oclass->ofuncs && oclass->ofuncs->rd08) {
		*data = oclass->ofuncs->rd08(object, addr);
		return 0;
	}
	*data = 0x00;
	return -ENODEV;
}

int
nvkm_object_rd16(struct nvkm_object *object, u64 addr, u16 *data)
{
	const struct nvkm_oclass *oclass = object->oclass;
	if (oclass->ofuncs && oclass->ofuncs->rd16) {
		*data = oclass->ofuncs->rd16(object, addr);
		return 0;
	}
	*data = 0x0000;
	return -ENODEV;
}

int
nvkm_object_rd32(struct nvkm_object *object, u64 addr, u32 *data)
{
	const struct nvkm_oclass *oclass = object->oclass;
	if (oclass->ofuncs && oclass->ofuncs->rd32) {
		*data = oclass->ofuncs->rd32(object, addr);
		return 0;
	}
	*data = 0x0000;
	return -ENODEV;
}

int
nvkm_object_wr08(struct nvkm_object *object, u64 addr, u8 data)
{
	const struct nvkm_oclass *oclass = object->oclass;
	if (oclass->ofuncs && oclass->ofuncs->wr08) {
		oclass->ofuncs->wr08(object, addr, data);
		return 0;
	}
	return -ENODEV;
}

int
nvkm_object_wr16(struct nvkm_object *object, u64 addr, u16 data)
{
	const struct nvkm_oclass *oclass = object->oclass;
	if (oclass->ofuncs && oclass->ofuncs->wr16) {
		oclass->ofuncs->wr16(object, addr, data);
		return 0;
	}
	return -ENODEV;
}

int
nvkm_object_wr32(struct nvkm_object *object, u64 addr, u32 data)
{
	const struct nvkm_oclass *oclass = object->oclass;
	if (oclass->ofuncs && oclass->ofuncs->wr32) {
		oclass->ofuncs->wr32(object, addr, data);
		return 0;
	}
	return -ENODEV;
}

int
nvkm_object_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, u32 pclass,
		    int size, void **pobject)
{
	struct nvkm_object *object;

	object = *pobject = kzalloc(size, GFP_KERNEL);
	if (!object)
		return -ENOMEM;

	nvkm_object_ref(parent, &object->parent);
	nvkm_object_ref(engine, (struct nvkm_object **)&object->engine);
	object->oclass = oclass;
	object->pclass = pclass;
	atomic_set(&object->refcount, 1);
	atomic_set(&object->usecount, 0);

#ifdef NVKM_OBJECT_MAGIC
	object->_magic = NVKM_OBJECT_MAGIC;
#endif
	return 0;
}

int
_nvkm_object_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	if (size != 0)
		return -ENOSYS;
	return nvkm_object_create(parent, engine, oclass, 0, pobject);
}

void
nvkm_object_destroy(struct nvkm_object *object)
{
	nvkm_object_ref(NULL, (struct nvkm_object **)&object->engine);
	nvkm_object_ref(NULL, &object->parent);
	kfree(object);
}

int
_nvkm_object_init(struct nvkm_object *object)
{
	return 0;
}

int
_nvkm_object_fini(struct nvkm_object *object, bool suspend)
{
	return 0;
}

struct nvkm_ofuncs
nvkm_object_ofuncs = {
	.ctor = _nvkm_object_ctor,
	.dtor = nvkm_object_destroy,
	.init = _nvkm_object_init,
	.fini = _nvkm_object_fini,
};

int
nvkm_object_old(struct nvkm_object *parent, struct nvkm_object *engine,
		 struct nvkm_oclass *oclass, void *data, u32 size,
		 struct nvkm_object **pobject)
{
	struct nvkm_ofuncs *ofuncs = oclass->ofuncs;
	struct nvkm_object *object = NULL;
	int ret;

	ret = ofuncs->ctor(parent, engine, oclass, data, size, &object);
	*pobject = object;
	if (ret < 0) {
		if (object) {
			ofuncs->dtor(object);
			*pobject = NULL;
		}

		return ret;
	}

	if (ret == 0) {
		atomic_set(&object->refcount, 1);
	}

	return 0;
}

static void
nvkm_object_dtor(struct nvkm_object *object)
{
	nv_ofuncs(object)->dtor(object);
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
			nvkm_object_dtor(*ref);
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

	if (object->engine) {
		mutex_lock(&nv_subdev(object->engine)->mutex);
		ret = nvkm_object_inc(&object->engine->subdev.object);
		mutex_unlock(&nv_subdev(object->engine)->mutex);
		if (ret)
			goto fail_engine;
	}

	ret = nv_ofuncs(object)->init(object);
	atomic_set(&object->usecount, 1);
	if (ret)
		goto fail_self;

	return 0;

fail_self:
	if (object->engine) {
		mutex_lock(&nv_subdev(object->engine)->mutex);
		nvkm_object_dec(&object->engine->subdev.object, false);
		mutex_unlock(&nv_subdev(object->engine)->mutex);
	}
fail_engine:
	if (object->parent)
		 nvkm_object_dec(object->parent, false);
fail_parent:
	atomic_dec(&object->usecount);
	return ret;
}

static int
nvkm_object_decf(struct nvkm_object *object)
{
	nv_ofuncs(object)->fini(object, false);
	atomic_set(&object->usecount, 0);

	if (object->engine) {
		mutex_lock(&nv_subdev(object->engine)->mutex);
		nvkm_object_dec(&object->engine->subdev.object, false);
		mutex_unlock(&nv_subdev(object->engine)->mutex);
	}

	if (object->parent)
		nvkm_object_dec(object->parent, false);

	return 0;
}

static int
nvkm_object_decs(struct nvkm_object *object)
{
	int ret;

	ret = nv_ofuncs(object)->fini(object, true);
	atomic_set(&object->usecount, 0);
	if (ret)
		return ret;

	if (object->engine) {
		mutex_lock(&nv_subdev(object->engine)->mutex);
		ret = nvkm_object_dec(&object->engine->subdev.object, true);
		mutex_unlock(&nv_subdev(object->engine)->mutex);
		if (ret)
			goto fail_engine;
	}

	if (object->parent) {
		ret = nvkm_object_dec(object->parent, true);
		if (ret)
			goto fail_parent;
	}

	return 0;

fail_parent:
	if (object->engine) {
		mutex_lock(&nv_subdev(object->engine)->mutex);
		nvkm_object_inc(&object->engine->subdev.object);
		mutex_unlock(&nv_subdev(object->engine)->mutex);
	}

fail_engine:
	nv_ofuncs(object)->init(object);

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
