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

static inline int
nouveau_engctx_exists(struct nouveau_object *parent,
		      struct nouveau_engine *engine, void **pobject)
{
	struct nouveau_engctx *engctx;
	struct nouveau_object *parctx;

	list_for_each_entry(engctx, &engine->contexts, head) {
		parctx = nv_pclass(nv_object(engctx), NV_PARENT_CLASS);
		if (parctx == parent) {
			atomic_inc(&nv_object(engctx)->refcount);
			*pobject = engctx;
			return 1;
		}
	}

	return 0;
}

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
	struct nouveau_object *engctx;
	unsigned long save;
	int ret;

	/* check if this engine already has a context for the parent object,
	 * and reference it instead of creating a new one
	 */
	spin_lock_irqsave(&engine->lock, save);
	ret = nouveau_engctx_exists(parent, engine, pobject);
	spin_unlock_irqrestore(&engine->lock, save);
	if (ret)
		return ret;

	/* create the new context, supports creating both raw objects and
	 * objects backed by instance memory
	 */
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
	if (ret)
		return ret;

	/* must take the lock again and re-check a context doesn't already
	 * exist (in case of a race) - the lock had to be dropped before as
	 * it's not possible to allocate the object with it held.
	 */
	spin_lock_irqsave(&engine->lock, save);
	ret = nouveau_engctx_exists(parent, engine, pobject);
	if (ret) {
		spin_unlock_irqrestore(&engine->lock, save);
		nouveau_object_ref(NULL, &engctx);
		return ret;
	}

	if (client->vm)
		atomic_inc(&client->vm->engref[nv_engidx(engobj)]);
	list_add(&nv_engctx(engctx)->head, &engine->contexts);
	nv_engctx(engctx)->addr = ~0ULL;
	spin_unlock_irqrestore(&engine->lock, save);
	return 0;
}

void
nouveau_engctx_destroy(struct nouveau_engctx *engctx)
{
	struct nouveau_object *engobj = nv_object(engctx)->engine;
	struct nouveau_engine *engine = nv_engine(engobj);
	struct nouveau_client *client = nouveau_client(engctx);
	unsigned long save;

	nouveau_gpuobj_unmap(&engctx->vma);
	spin_lock_irqsave(&engine->lock, save);
	list_del(&engctx->head);
	spin_unlock_irqrestore(&engine->lock, save);

	if (client->vm)
		atomic_dec(&client->vm->engref[nv_engidx(engobj)]);

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
nouveau_engctx_get(struct nouveau_engine *engine, u64 addr)
{
	struct nouveau_engctx *engctx;
	unsigned long flags;

	spin_lock_irqsave(&engine->lock, flags);
	list_for_each_entry(engctx, &engine->contexts, head) {
		if (engctx->addr == addr) {
			engctx->save = flags;
			return nv_object(engctx);
		}
	}
	spin_unlock_irqrestore(&engine->lock, flags);
	return NULL;
}

void
nouveau_engctx_put(struct nouveau_object *object)
{
	if (object) {
		struct nouveau_engine *engine = nv_engine(object->engine);
		struct nouveau_engctx *engctx = nv_engctx(object);
		spin_unlock_irqrestore(&engine->lock, engctx->save);
	}
}
