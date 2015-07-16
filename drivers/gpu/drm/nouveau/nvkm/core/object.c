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

#ifdef NVKM_OBJECT_MAGIC
static struct list_head _objlist = LIST_HEAD_INIT(_objlist);
static DEFINE_SPINLOCK(_objlist_lock);
#endif

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
	object->oclass->handle |= pclass;
	atomic_set(&object->refcount, 1);
	atomic_set(&object->usecount, 0);

#ifdef NVKM_OBJECT_MAGIC
	object->_magic = NVKM_OBJECT_MAGIC;
	spin_lock(&_objlist_lock);
	list_add(&object->list, &_objlist);
	spin_unlock(&_objlist_lock);
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
#ifdef NVKM_OBJECT_MAGIC
	spin_lock(&_objlist_lock);
	list_del(&object->list);
	spin_unlock(&_objlist_lock);
#endif
	nvkm_object_ref(NULL, (struct nvkm_object **)&object->engine);
	nvkm_object_ref(NULL, &object->parent);
	kfree(object);
}

int
nvkm_object_init(struct nvkm_object *object)
{
	return 0;
}

int
nvkm_object_fini(struct nvkm_object *object, bool suspend)
{
	return 0;
}

struct nvkm_ofuncs
nvkm_object_ofuncs = {
	.ctor = _nvkm_object_ctor,
	.dtor = nvkm_object_destroy,
	.init = nvkm_object_init,
	.fini = nvkm_object_fini,
};

int
nvkm_object_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		 struct nvkm_oclass *oclass, void *data, u32 size,
		 struct nvkm_object **pobject)
{
	struct nvkm_ofuncs *ofuncs = oclass->ofuncs;
	struct nvkm_object *object = NULL;
	int ret;

	ret = ofuncs->ctor(parent, engine, oclass, data, size, &object);
	*pobject = object;
	if (ret < 0) {
		if (ret != -ENODEV) {
			nv_error(parent, "failed to create 0x%08x, %d\n",
				 oclass->handle, ret);
		}

		if (object) {
			ofuncs->dtor(object);
			*pobject = NULL;
		}

		return ret;
	}

	if (ret == 0) {
		nv_trace(object, "created\n");
		atomic_set(&object->refcount, 1);
	}

	return 0;
}

static void
nvkm_object_dtor(struct nvkm_object *object)
{
	nv_trace(object, "destroying\n");
	nv_ofuncs(object)->dtor(object);
}

void
nvkm_object_ref(struct nvkm_object *obj, struct nvkm_object **ref)
{
	if (obj) {
		atomic_inc(&obj->refcount);
		nv_trace(obj, "inc() == %d\n", atomic_read(&obj->refcount));
	}

	if (*ref) {
		int dead = atomic_dec_and_test(&(*ref)->refcount);
		nv_trace(*ref, "dec() == %d\n", atomic_read(&(*ref)->refcount));
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

	nv_trace(object, "use(+1) == %d\n", atomic_read(&object->usecount));
	if (ref != 1)
		return 0;

	nv_trace(object, "initialising...\n");
	if (object->parent) {
		ret = nvkm_object_inc(object->parent);
		if (ret) {
			nv_error(object, "parent failed, %d\n", ret);
			goto fail_parent;
		}
	}

	if (object->engine) {
		mutex_lock(&nv_subdev(object->engine)->mutex);
		ret = nvkm_object_inc(&object->engine->subdev.object);
		mutex_unlock(&nv_subdev(object->engine)->mutex);
		if (ret) {
			nv_error(object, "engine failed, %d\n", ret);
			goto fail_engine;
		}
	}

	ret = nv_ofuncs(object)->init(object);
	atomic_set(&object->usecount, 1);
	if (ret) {
		nv_error(object, "init failed, %d\n", ret);
		goto fail_self;
	}

	nv_trace(object, "initialised\n");
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
	int ret;

	nv_trace(object, "stopping...\n");

	ret = nv_ofuncs(object)->fini(object, false);
	atomic_set(&object->usecount, 0);
	if (ret)
		nv_warn(object, "failed fini, %d\n", ret);

	if (object->engine) {
		mutex_lock(&nv_subdev(object->engine)->mutex);
		nvkm_object_dec(&object->engine->subdev.object, false);
		mutex_unlock(&nv_subdev(object->engine)->mutex);
	}

	if (object->parent)
		nvkm_object_dec(object->parent, false);

	nv_trace(object, "stopped\n");
	return 0;
}

static int
nvkm_object_decs(struct nvkm_object *object)
{
	int ret, rret;

	nv_trace(object, "suspending...\n");

	ret = nv_ofuncs(object)->fini(object, true);
	atomic_set(&object->usecount, 0);
	if (ret) {
		nv_error(object, "failed suspend, %d\n", ret);
		return ret;
	}

	if (object->engine) {
		mutex_lock(&nv_subdev(object->engine)->mutex);
		ret = nvkm_object_dec(&object->engine->subdev.object, true);
		mutex_unlock(&nv_subdev(object->engine)->mutex);
		if (ret) {
			nv_warn(object, "engine failed suspend, %d\n", ret);
			goto fail_engine;
		}
	}

	if (object->parent) {
		ret = nvkm_object_dec(object->parent, true);
		if (ret) {
			nv_warn(object, "parent failed suspend, %d\n", ret);
			goto fail_parent;
		}
	}

	nv_trace(object, "suspended\n");
	return 0;

fail_parent:
	if (object->engine) {
		mutex_lock(&nv_subdev(object->engine)->mutex);
		rret = nvkm_object_inc(&object->engine->subdev.object);
		mutex_unlock(&nv_subdev(object->engine)->mutex);
		if (rret)
			nv_fatal(object, "engine failed to reinit, %d\n", rret);
	}

fail_engine:
	rret = nv_ofuncs(object)->init(object);
	if (rret)
		nv_fatal(object, "failed to reinit, %d\n", rret);

	return ret;
}

int
nvkm_object_dec(struct nvkm_object *object, bool suspend)
{
	int ref = atomic_add_return(-1, &object->usecount);
	int ret;

	nv_trace(object, "use(-1) == %d\n", atomic_read(&object->usecount));

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

void
nvkm_object_debug(void)
{
#ifdef NVKM_OBJECT_MAGIC
	struct nvkm_object *object;
	if (!list_empty(&_objlist)) {
		nv_fatal(NULL, "*******************************************\n");
		nv_fatal(NULL, "* AIIIII! object(s) still exist!!!\n");
		nv_fatal(NULL, "*******************************************\n");
		list_for_each_entry(object, &_objlist, list) {
			nv_fatal(object, "%p/%p/%d/%d\n",
				 object->parent, object->engine,
				 atomic_read(&object->refcount),
				 atomic_read(&object->usecount));
		}
	}
#endif
}
