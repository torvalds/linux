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
#include <core/namedb.h>
#include <core/handle.h>
#include <core/client.h>
#include <core/engctx.h>

#include <subdev/vm.h>

int
nouveau_engctx_create_(struct nouveau_object *parent,
		       struct nouveau_object *engobj,
		       struct nouveau_oclass *oclass,
		       struct nouveau_object *pargpu,
		       u32 size, u32 align, u32 flags,
		       int length, void **pobject)
{
	struct nouveau_client *client = nouveau_client(parent);
	struct nouveau_engine *engine = nv_engine(engobj);
	struct nouveau_subdev *subdev = nv_subdev(engine);
	struct nouveau_engctx *engctx;
	struct nouveau_object *ctxpar;
	int ret;

	/* use existing context for the engine if one is available */
	mutex_lock(&subdev->mutex);
	list_for_each_entry(engctx, &engine->contexts, head) {
		ctxpar = nv_pclass(nv_object(engctx), NV_PARENT_CLASS);
		if (ctxpar == parent) {
			atomic_inc(&nv_object(engctx)->refcount);
			*pobject = engctx;
			mutex_unlock(&subdev->mutex);
			return 1;
		}
	}
	mutex_unlock(&subdev->mutex);

	if (size) {
		ret = nouveau_gpuobj_create_(parent, engobj, oclass,
					     NV_ENGCTX_CLASS,
					     pargpu, size, align, flags,
					     length, pobject);
	} else {
		ret = nouveau_object_create_(parent, engobj, oclass,
					     NV_ENGCTX_CLASS, length, pobject);
	}

	engctx = *pobject;
	if (engctx && client->vm)
		atomic_inc(&client->vm->engref[nv_engidx(engobj)]);
	if (ret)
		return ret;

	list_add(&engctx->head, &engine->contexts);
	return 0;
}

void
nouveau_engctx_destroy(struct nouveau_engctx *engctx)
{
	struct nouveau_object *engine = nv_object(engctx)->engine;
	struct nouveau_client *client = nouveau_client(engctx);

	nouveau_gpuobj_unmap(&engctx->vma);
	list_del(&engctx->head);
	if (client->vm)
		atomic_dec(&client->vm->engref[nv_engidx(engine)]);

	if (engctx->base.size)
		nouveau_gpuobj_destroy(&engctx->base);
	else
		nouveau_object_destroy(&engctx->base.base);
}

int
nouveau_engctx_init(struct nouveau_engctx *engctx)
{
	struct nouveau_object *object = nv_object(engctx);
	struct nouveau_subdev *subdev = nv_subdev(object->engine);
	struct nouveau_object *parent;
	struct nouveau_subdev *pardev;
	int ret;

	ret = nouveau_gpuobj_init(&engctx->base);
	if (ret)
		return ret;

	parent = nv_pclass(object->parent, NV_PARENT_CLASS);
	pardev = nv_subdev(parent->engine);
	if (nv_parent(parent)->context_attach) {
		mutex_lock(&pardev->mutex);
		ret = nv_parent(parent)->context_attach(parent, object);
		mutex_unlock(&pardev->mutex);
	}

	if (ret) {
		nv_error(parent, "failed to attach %s context, %d\n",
			 subdev->name, ret);
		return ret;
	}

	nv_debug(parent, "attached %s context\n", subdev->name);
	return 0;
}

int
nouveau_engctx_fini(struct nouveau_engctx *engctx, bool suspend)
{
	struct nouveau_object *object = nv_object(engctx);
	struct nouveau_subdev *subdev = nv_subdev(object->engine);
	struct nouveau_object *parent;
	struct nouveau_subdev *pardev;
	int ret = 0;

	parent = nv_pclass(object->parent, NV_PARENT_CLASS);
	pardev = nv_subdev(parent->engine);
	if (nv_parent(parent)->context_detach) {
		mutex_lock(&pardev->mutex);
		ret = nv_parent(parent)->context_detach(parent, suspend, object);
		mutex_unlock(&pardev->mutex);
	}

	if (ret) {
		nv_error(parent, "failed to detach %s context, %d\n",
			 subdev->name, ret);
		return ret;
	}

	nv_debug(parent, "detached %s context\n", subdev->name);
	return nouveau_gpuobj_fini(&engctx->base, suspend);
}

void
_nouveau_engctx_dtor(struct nouveau_object *object)
{
	nouveau_engctx_destroy(nv_engctx(object));
}

int
_nouveau_engctx_init(struct nouveau_object *object)
{
	return nouveau_engctx_init(nv_engctx(object));
}


int
_nouveau_engctx_fini(struct nouveau_object *object, bool suspend)
{
	return nouveau_engctx_fini(nv_engctx(object), suspend);
}

struct nouveau_object *
nouveau_engctx_lookup(struct nouveau_engine *engine, u64 addr)
{
	struct nouveau_engctx *engctx;

	list_for_each_entry(engctx, &engine->contexts, head) {
		if (engctx->base.size &&
		    nv_gpuobj(engctx)->addr == addr)
			return nv_object(engctx);
	}

	return NULL;
}

struct nouveau_handle *
nouveau_engctx_lookup_class(struct nouveau_engine *engine, u64 addr, u16 oclass)
{
	struct nouveau_object *engctx = nouveau_engctx_lookup(engine, addr);
	struct nouveau_namedb *namedb;

	if (engctx && (namedb = (void *)nv_pclass(engctx, NV_NAMEDB_CLASS)))
		return nouveau_namedb_get_class(namedb, oclass);

	return NULL;
}

struct nouveau_handle *
nouveau_engctx_lookup_vinst(struct nouveau_engine *engine, u64 addr, u64 vinst)
{
	struct nouveau_object *engctx = nouveau_engctx_lookup(engine, addr);
	struct nouveau_namedb *namedb;

	if (engctx && (namedb = (void *)nv_pclass(engctx, NV_NAMEDB_CLASS)))
		return nouveau_namedb_get_vinst(namedb, vinst);

	return NULL;
}

struct nouveau_handle *
nouveau_engctx_lookup_cinst(struct nouveau_engine *engine, u64 addr, u32 cinst)
{
	struct nouveau_object *engctx = nouveau_engctx_lookup(engine, addr);
	struct nouveau_namedb *namedb;

	if (engctx && (namedb = (void *)nv_pclass(engctx, NV_NAMEDB_CLASS)))
		return nouveau_namedb_get_cinst(namedb, cinst);

	return NULL;
}

void
nouveau_engctx_handle_put(struct nouveau_handle *handle)
{
	if (handle)
		nouveau_namedb_put(handle);
}
