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
#include <core/handle.h>
#include <core/client.h>

#define hprintk(h,l,f,a...) do {                                               \
	struct nvkm_client *c = nvkm_client((h)->object);                      \
	struct nvkm_handle *p = (h)->parent; u32 n = p ? p->name : ~0;         \
	nv_printk((c), l, "0x%08x:0x%08x "f, n, (h)->name, ##a);               \
} while(0)

int
nvkm_handle_init(struct nvkm_handle *handle)
{
	struct nvkm_handle *item;
	int ret;

	hprintk(handle, TRACE, "init running\n");
	ret = nvkm_object_inc(handle->object);
	if (ret)
		return ret;

	hprintk(handle, TRACE, "init children\n");
	list_for_each_entry(item, &handle->tree, head) {
		ret = nvkm_handle_init(item);
		if (ret)
			goto fail;
	}

	hprintk(handle, TRACE, "init completed\n");
	return 0;
fail:
	hprintk(handle, ERROR, "init failed with %d\n", ret);
	list_for_each_entry_continue_reverse(item, &handle->tree, head) {
		nvkm_handle_fini(item, false);
	}

	nvkm_object_dec(handle->object, false);
	return ret;
}

int
nvkm_handle_fini(struct nvkm_handle *handle, bool suspend)
{
	static char *name[2] = { "fini", "suspend" };
	struct nvkm_handle *item;
	int ret;

	hprintk(handle, TRACE, "%s children\n", name[suspend]);
	list_for_each_entry(item, &handle->tree, head) {
		ret = nvkm_handle_fini(item, suspend);
		if (ret && suspend)
			goto fail;
	}

	hprintk(handle, TRACE, "%s running\n", name[suspend]);
	if (handle->object) {
		ret = nvkm_object_dec(handle->object, suspend);
		if (ret && suspend)
			goto fail;
	}

	hprintk(handle, TRACE, "%s completed\n", name[suspend]);
	return 0;
fail:
	hprintk(handle, ERROR, "%s failed with %d\n", name[suspend], ret);
	list_for_each_entry_continue_reverse(item, &handle->tree, head) {
		int rret = nvkm_handle_init(item);
		if (rret)
			hprintk(handle, FATAL, "failed to restart, %d\n", rret);
	}

	return ret;
}

int
nvkm_handle_create(struct nvkm_object *parent, u32 _parent, u32 _handle,
		   struct nvkm_object *object, struct nvkm_handle **phandle)
{
	struct nvkm_object *namedb;
	struct nvkm_handle *handle;
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

	ret = nvkm_namedb_insert(nv_namedb(namedb), _handle, object, handle);
	if (ret) {
		kfree(handle);
		return ret;
	}

	if (nv_parent(parent)->object_attach) {
		ret = nv_parent(parent)->object_attach(parent, object, _handle);
		if (ret < 0) {
			nvkm_handle_destroy(handle);
			return ret;
		}

		handle->priv = ret;
	}

	if (object != namedb) {
		while (!nv_iclass(namedb, NV_CLIENT_CLASS))
			namedb = namedb->parent;

		handle->parent = nvkm_namedb_get(nv_namedb(namedb), _parent);
		if (handle->parent) {
			list_add(&handle->head, &handle->parent->tree);
			nvkm_namedb_put(handle->parent);
		}
	}

	hprintk(handle, TRACE, "created\n");
	*phandle = handle;
	return 0;
}

void
nvkm_handle_destroy(struct nvkm_handle *handle)
{
	struct nvkm_handle *item, *temp;

	hprintk(handle, TRACE, "destroy running\n");
	list_for_each_entry_safe(item, temp, &handle->tree, head) {
		nvkm_handle_destroy(item);
	}
	list_del(&handle->head);

	if (handle->priv != ~0) {
		struct nvkm_object *parent = handle->parent->object;
		nv_parent(parent)->object_detach(parent, handle->priv);
	}

	hprintk(handle, TRACE, "destroy completed\n");
	nvkm_namedb_remove(handle);
	kfree(handle);
}

struct nvkm_object *
nvkm_handle_ref(struct nvkm_object *parent, u32 name)
{
	struct nvkm_object *object = NULL;
	struct nvkm_handle *handle;

	while (!nv_iclass(parent, NV_NAMEDB_CLASS))
		parent = parent->parent;

	handle = nvkm_namedb_get(nv_namedb(parent), name);
	if (handle) {
		nvkm_object_ref(handle->object, &object);
		nvkm_namedb_put(handle);
	}

	return object;
}

struct nvkm_handle *
nvkm_handle_get_class(struct nvkm_object *engctx, u16 oclass)
{
	struct nvkm_namedb *namedb;
	if (engctx && (namedb = (void *)nv_pclass(engctx, NV_NAMEDB_CLASS)))
		return nvkm_namedb_get_class(namedb, oclass);
	return NULL;
}

struct nvkm_handle *
nvkm_handle_get_vinst(struct nvkm_object *engctx, u64 vinst)
{
	struct nvkm_namedb *namedb;
	if (engctx && (namedb = (void *)nv_pclass(engctx, NV_NAMEDB_CLASS)))
		return nvkm_namedb_get_vinst(namedb, vinst);
	return NULL;
}

struct nvkm_handle *
nvkm_handle_get_cinst(struct nvkm_object *engctx, u32 cinst)
{
	struct nvkm_namedb *namedb;
	if (engctx && (namedb = (void *)nv_pclass(engctx, NV_NAMEDB_CLASS)))
		return nvkm_namedb_get_cinst(namedb, cinst);
	return NULL;
}

void
nvkm_handle_put(struct nvkm_handle *handle)
{
	if (handle)
		nvkm_namedb_put(handle);
}
