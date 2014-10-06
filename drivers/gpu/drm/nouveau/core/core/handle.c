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
#include <core/handle.h>
#include <core/client.h>

#define hprintk(h,l,f,a...) do {                                               \
	struct nouveau_client *c = nouveau_client((h)->object);                \
	struct nouveau_handle *p = (h)->parent; u32 n = p ? p->name : ~0;      \
	nv_printk((c), l, "0x%08x:0x%08x "f, n, (h)->name, ##a);               \
} while(0)

int
nouveau_handle_init(struct nouveau_handle *handle)
{
	struct nouveau_handle *item;
	int ret;

	hprintk(handle, TRACE, "init running\n");
	ret = nouveau_object_inc(handle->object);
	if (ret)
		return ret;

	hprintk(handle, TRACE, "init children\n");
	list_for_each_entry(item, &handle->tree, head) {
		ret = nouveau_handle_init(item);
		if (ret)
			goto fail;
	}

	hprintk(handle, TRACE, "init completed\n");
	return 0;
fail:
	hprintk(handle, ERROR, "init failed with %d\n", ret);
	list_for_each_entry_continue_reverse(item, &handle->tree, head) {
		nouveau_handle_fini(item, false);
	}

	nouveau_object_dec(handle->object, false);
	return ret;
}

int
nouveau_handle_fini(struct nouveau_handle *handle, bool suspend)
{
	static char *name[2] = { "fini", "suspend" };
	struct nouveau_handle *item;
	int ret;

	hprintk(handle, TRACE, "%s children\n", name[suspend]);
	list_for_each_entry(item, &handle->tree, head) {
		ret = nouveau_handle_fini(item, suspend);
		if (ret && suspend)
			goto fail;
	}

	hprintk(handle, TRACE, "%s running\n", name[suspend]);
	if (handle->object) {
		ret = nouveau_object_dec(handle->object, suspend);
		if (ret && suspend)
			goto fail;
	}

	hprintk(handle, TRACE, "%s completed\n", name[suspend]);
	return 0;
fail:
	hprintk(handle, ERROR, "%s failed with %d\n", name[suspend], ret);
	list_for_each_entry_continue_reverse(item, &handle->tree, head) {
		int rret = nouveau_handle_init(item);
		if (rret)
			hprintk(handle, FATAL, "failed to restart, %d\n", rret);
	}

	return ret;
}

int
nouveau_handle_create(struct nouveau_object *parent, u32 _parent, u32 _handle,
		      struct nouveau_object *object,
		      struct nouveau_handle **phandle)
{
	struct nouveau_object *namedb;
	struct nouveau_handle *handle;
	int ret;

	namedb = parent;
	while (!nv_iclass(namedb, NV_NAMEDB_CLASS))
		namedb = namedb->parent;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	INIT_LIST_HEAD(&handle->head);
	INIT_LIST_HEAD(&handle->tree);
	handle->name = _handle;
	handle->priv = ~0;

	ret = nouveau_namedb_insert(nv_namedb(namedb), _handle, object, handle);
	if (ret) {
		kfree(handle);
		return ret;
	}

	if (nv_parent(parent)->object_attach) {
		ret = nv_parent(parent)->object_attach(parent, object, _handle);
		if (ret < 0) {
			nouveau_handle_destroy(handle);
			return ret;
		}

		handle->priv = ret;
	}

	if (object != namedb) {
		while (!nv_iclass(namedb, NV_CLIENT_CLASS))
			namedb = namedb->parent;

		handle->parent = nouveau_namedb_get(nv_namedb(namedb), _parent);
		if (handle->parent) {
			list_add(&handle->head, &handle->parent->tree);
			nouveau_namedb_put(handle->parent);
		}
	}

	hprintk(handle, TRACE, "created\n");
	*phandle = handle;
	return 0;
}

void
nouveau_handle_destroy(struct nouveau_handle *handle)
{
	struct nouveau_handle *item, *temp;

	hprintk(handle, TRACE, "destroy running\n");
	list_for_each_entry_safe(item, temp, &handle->tree, head) {
		nouveau_handle_destroy(item);
	}
	list_del(&handle->head);

	if (handle->priv != ~0) {
		struct nouveau_object *parent = handle->parent->object;
		nv_parent(parent)->object_detach(parent, handle->priv);
	}

	hprintk(handle, TRACE, "destroy completed\n");
	nouveau_namedb_remove(handle);
	kfree(handle);
}

struct nouveau_object *
nouveau_handle_ref(struct nouveau_object *parent, u32 name)
{
	struct nouveau_object *object = NULL;
	struct nouveau_handle *handle;

	while (!nv_iclass(parent, NV_NAMEDB_CLASS))
		parent = parent->parent;

	handle = nouveau_namedb_get(nv_namedb(parent), name);
	if (handle) {
		nouveau_object_ref(handle->object, &object);
		nouveau_namedb_put(handle);
	}

	return object;
}

struct nouveau_handle *
nouveau_handle_get_class(struct nouveau_object *engctx, u16 oclass)
{
	struct nouveau_namedb *namedb;
	if (engctx && (namedb = (void *)nv_pclass(engctx, NV_NAMEDB_CLASS)))
		return nouveau_namedb_get_class(namedb, oclass);
	return NULL;
}

struct nouveau_handle *
nouveau_handle_get_vinst(struct nouveau_object *engctx, u64 vinst)
{
	struct nouveau_namedb *namedb;
	if (engctx && (namedb = (void *)nv_pclass(engctx, NV_NAMEDB_CLASS)))
		return nouveau_namedb_get_vinst(namedb, vinst);
	return NULL;
}

struct nouveau_handle *
nouveau_handle_get_cinst(struct nouveau_object *engctx, u32 cinst)
{
	struct nouveau_namedb *namedb;
	if (engctx && (namedb = (void *)nv_pclass(engctx, NV_NAMEDB_CLASS)))
		return nouveau_namedb_get_cinst(namedb, cinst);
	return NULL;
}

void
nouveau_handle_put(struct nouveau_handle *handle)
{
	if (handle)
		nouveau_namedb_put(handle);
}

int
nouveau_handle_new(struct nouveau_object *client, u32 _parent, u32 _handle,
		   u16 _oclass, void *data, u32 size,
		   struct nouveau_object **pobject)
{
	struct nouveau_object *parent = NULL;
	struct nouveau_object *engctx = NULL;
	struct nouveau_object *object = NULL;
	struct nouveau_object *engine;
	struct nouveau_oclass *oclass;
	struct nouveau_handle *handle;
	int ret;

	/* lookup parent object and ensure it *is* a parent */
	parent = nouveau_handle_ref(client, _parent);
	if (!parent) {
		nv_error(client, "parent 0x%08x not found\n", _parent);
		return -ENOENT;
	}

	if (!nv_iclass(parent, NV_PARENT_CLASS)) {
		nv_error(parent, "cannot have children\n");
		ret = -EINVAL;
		goto fail_class;
	}

	/* check that parent supports the requested subclass */
	ret = nouveau_parent_sclass(parent, _oclass, &engine, &oclass);
	if (ret) {
		nv_debug(parent, "illegal class 0x%04x\n", _oclass);
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
		ret = nouveau_object_ctor(parent, engine,
					  nv_engine(engine)->cclass,
					  data, size, &engctx);
		if (ret)
			goto fail_engctx;
	} else {
		nouveau_object_ref(parent, &engctx);
	}

	/* finally, create new object and bind it to its handle */
	ret = nouveau_object_ctor(engctx, engine, oclass, data, size, &object);
	*pobject = object;
	if (ret)
		goto fail_ctor;

	ret = nouveau_object_inc(object);
	if (ret)
		goto fail_init;

	ret = nouveau_handle_create(parent, _parent, _handle, object, &handle);
	if (ret)
		goto fail_handle;

	ret = nouveau_handle_init(handle);
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
	nouveau_object_ref(NULL, &parent);
	return ret;
}

int
nouveau_handle_del(struct nouveau_object *client, u32 _parent, u32 _handle)
{
	struct nouveau_object *parent = NULL;
	struct nouveau_object *namedb = NULL;
	struct nouveau_handle *handle = NULL;

	parent = nouveau_handle_ref(client, _parent);
	if (!parent)
		return -ENOENT;

	namedb = nv_pclass(parent, NV_NAMEDB_CLASS);
	if (namedb) {
		handle = nouveau_namedb_get(nv_namedb(namedb), _handle);
		if (handle) {
			nouveau_namedb_put(handle);
			nouveau_handle_fini(handle, false);
			nouveau_handle_destroy(handle);
		}
	}

	nouveau_object_ref(NULL, &parent);
	return handle ? 0 : -EINVAL;
}
