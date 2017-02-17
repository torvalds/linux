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
#include <core/engine.h>

struct nvkm_object *
nvkm_object_search(struct nvkm_client *client, u64 handle,
		   const struct nvkm_object_func *func)
{
	struct nvkm_object *object;

	if (handle) {
		struct rb_node *node = client->objroot.rb_node;
		while (node) {
			object = rb_entry(node, typeof(*object), node);
			if (handle < object->object)
				node = node->rb_left;
			else
			if (handle > object->object)
				node = node->rb_right;
			else
				goto done;
		}
		return ERR_PTR(-ENOENT);
	} else {
		object = &client->object;
	}

done:
	if (unlikely(func && object->func != func))
		return ERR_PTR(-EINVAL);
	return object;
}

void
nvkm_object_remove(struct nvkm_object *object)
{
	if (!RB_EMPTY_NODE(&object->node))
		rb_erase(&object->node, &object->client->objroot);
}

bool
nvkm_object_insert(struct nvkm_object *object)
{
	struct rb_node **ptr = &object->client->objroot.rb_node;
	struct rb_node *parent = NULL;

	while (*ptr) {
		struct nvkm_object *this = rb_entry(*ptr, typeof(*this), node);
		parent = *ptr;
		if (object->object < this->object)
			ptr = &parent->rb_left;
		else
		if (object->object > this->object)
			ptr = &parent->rb_right;
		else
			return false;
	}

	rb_link_node(&object->node, parent, ptr);
	rb_insert_color(&object->node, &object->client->objroot);
	return true;
}

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
	const char *action = suspend ? "suspend" : "fini";
	struct nvkm_object *child;
	s64 time;
	int ret;

	nvif_debug(object, "%s children...\n", action);
	time = ktime_to_us(ktime_get());
	list_for_each_entry(child, &object->tree, head) {
		ret = nvkm_object_fini(child, suspend);
		if (ret && suspend)
			goto fail_child;
	}

	nvif_debug(object, "%s running...\n", action);
	if (object->func->fini) {
		ret = object->func->fini(object, suspend);
		if (ret) {
			nvif_error(object, "%s failed with %d\n", action, ret);
			if (suspend)
				goto fail;
		}
	}

	time = ktime_to_us(ktime_get()) - time;
	nvif_debug(object, "%s completed in %lldus\n", action, time);
	return 0;

fail:
	if (object->func->init) {
		int rret = object->func->init(object);
		if (rret)
			nvif_fatal(object, "failed to restart, %d\n", rret);
	}
fail_child:
	list_for_each_entry_continue_reverse(child, &object->tree, head) {
		nvkm_object_init(child);
	}
	return ret;
}

int
nvkm_object_init(struct nvkm_object *object)
{
	struct nvkm_object *child;
	s64 time;
	int ret;

	nvif_debug(object, "init running...\n");
	time = ktime_to_us(ktime_get());
	if (object->func->init) {
		ret = object->func->init(object);
		if (ret)
			goto fail;
	}

	nvif_debug(object, "init children...\n");
	list_for_each_entry(child, &object->tree, head) {
		ret = nvkm_object_init(child);
		if (ret)
			goto fail_child;
	}

	time = ktime_to_us(ktime_get()) - time;
	nvif_debug(object, "init completed in %lldus\n", time);
	return 0;

fail_child:
	list_for_each_entry_continue_reverse(child, &object->tree, head)
		nvkm_object_fini(child, false);
fail:
	nvif_error(object, "init failed with %d\n", ret);
	if (object->func->fini)
		object->func->fini(object, false);
	return ret;
}

void *
nvkm_object_dtor(struct nvkm_object *object)
{
	struct nvkm_object *child, *ctemp;
	void *data = object;
	s64 time;

	nvif_debug(object, "destroy children...\n");
	time = ktime_to_us(ktime_get());
	list_for_each_entry_safe(child, ctemp, &object->tree, head) {
		nvkm_object_del(&child);
	}

	nvif_debug(object, "destroy running...\n");
	if (object->func->dtor)
		data = object->func->dtor(object);
	nvkm_engine_unref(&object->engine);
	time = ktime_to_us(ktime_get()) - time;
	nvif_debug(object, "destroy completed in %lldus...\n", time);
	return data;
}

void
nvkm_object_del(struct nvkm_object **pobject)
{
	struct nvkm_object *object = *pobject;
	if (object && !WARN_ON(!object->func)) {
		*pobject = nvkm_object_dtor(object);
		nvkm_object_remove(object);
		list_del(&object->head);
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
	object->route  = oclass->route;
	object->token  = oclass->token;
	object->object = oclass->object;
	INIT_LIST_HEAD(&object->head);
	INIT_LIST_HEAD(&object->tree);
	RB_CLEAR_NODE(&object->node);
	WARN_ON(oclass->engine && !object->engine);
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
