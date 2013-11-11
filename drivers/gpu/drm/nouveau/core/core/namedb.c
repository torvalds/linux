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
#include <core/gpuobj.h>

static struct nouveau_handle *
nouveau_namedb_lookup(struct nouveau_namedb *namedb, u32 name)
{
	struct nouveau_handle *handle;

	list_for_each_entry(handle, &namedb->list, node) {
		if (handle->name == name)
			return handle;
	}

	return NULL;
}

static struct nouveau_handle *
nouveau_namedb_lookup_class(struct nouveau_namedb *namedb, u16 oclass)
{
	struct nouveau_handle *handle;

	list_for_each_entry(handle, &namedb->list, node) {
		if (nv_mclass(handle->object) == oclass)
			return handle;
	}

	return NULL;
}

static struct nouveau_handle *
nouveau_namedb_lookup_vinst(struct nouveau_namedb *namedb, u64 vinst)
{
	struct nouveau_handle *handle;

	list_for_each_entry(handle, &namedb->list, node) {
		if (nv_iclass(handle->object, NV_GPUOBJ_CLASS)) {
			if (nv_gpuobj(handle->object)->addr == vinst)
				return handle;
		}
	}

	return NULL;
}

static struct nouveau_handle *
nouveau_namedb_lookup_cinst(struct nouveau_namedb *namedb, u32 cinst)
{
	struct nouveau_handle *handle;

	list_for_each_entry(handle, &namedb->list, node) {
		if (nv_iclass(handle->object, NV_GPUOBJ_CLASS)) {
			if (nv_gpuobj(handle->object)->node &&
			    nv_gpuobj(handle->object)->node->offset == cinst)
				return handle;
		}
	}

	return NULL;
}

int
nouveau_namedb_insert(struct nouveau_namedb *namedb, u32 name,
		      struct nouveau_object *object,
		      struct nouveau_handle *handle)
{
	int ret = -EEXIST;
	write_lock_irq(&namedb->lock);
	if (!nouveau_namedb_lookup(namedb, name)) {
		nouveau_object_ref(object, &handle->object);
		handle->namedb = namedb;
		list_add(&handle->node, &namedb->list);
		ret = 0;
	}
	write_unlock_irq(&namedb->lock);
	return ret;
}

void
nouveau_namedb_remove(struct nouveau_handle *handle)
{
	struct nouveau_namedb *namedb = handle->namedb;
	struct nouveau_object *object = handle->object;
	write_lock_irq(&namedb->lock);
	list_del(&handle->node);
	write_unlock_irq(&namedb->lock);
	nouveau_object_ref(NULL, &object);
}

struct nouveau_handle *
nouveau_namedb_get(struct nouveau_namedb *namedb, u32 name)
{
	struct nouveau_handle *handle;
	read_lock(&namedb->lock);
	handle = nouveau_namedb_lookup(namedb, name);
	if (handle == NULL)
		read_unlock(&namedb->lock);
	return handle;
}

struct nouveau_handle *
nouveau_namedb_get_class(struct nouveau_namedb *namedb, u16 oclass)
{
	struct nouveau_handle *handle;
	read_lock(&namedb->lock);
	handle = nouveau_namedb_lookup_class(namedb, oclass);
	if (handle == NULL)
		read_unlock(&namedb->lock);
	return handle;
}

struct nouveau_handle *
nouveau_namedb_get_vinst(struct nouveau_namedb *namedb, u64 vinst)
{
	struct nouveau_handle *handle;
	read_lock(&namedb->lock);
	handle = nouveau_namedb_lookup_vinst(namedb, vinst);
	if (handle == NULL)
		read_unlock(&namedb->lock);
	return handle;
}

struct nouveau_handle *
nouveau_namedb_get_cinst(struct nouveau_namedb *namedb, u32 cinst)
{
	struct nouveau_handle *handle;
	read_lock(&namedb->lock);
	handle = nouveau_namedb_lookup_cinst(namedb, cinst);
	if (handle == NULL)
		read_unlock(&namedb->lock);
	return handle;
}

void
nouveau_namedb_put(struct nouveau_handle *handle)
{
	if (handle)
		read_unlock(&handle->namedb->lock);
}

int
nouveau_namedb_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, u32 pclass,
		       struct nouveau_oclass *sclass, u32 engcls,
		       int length, void **pobject)
{
	struct nouveau_namedb *namedb;
	int ret;

	ret = nouveau_parent_create_(parent, engine, oclass, pclass |
				     NV_NAMEDB_CLASS, sclass, engcls,
				     length, pobject);
	namedb = *pobject;
	if (ret)
		return ret;

	rwlock_init(&namedb->lock);
	INIT_LIST_HEAD(&namedb->list);
	return 0;
}

int
_nouveau_namedb_ctor(struct nouveau_object *parent,
		     struct nouveau_object *engine,
		     struct nouveau_oclass *oclass, void *data, u32 size,
		     struct nouveau_object **pobject)
{
	struct nouveau_namedb *object;
	int ret;

	ret = nouveau_namedb_create(parent, engine, oclass, 0, NULL, 0, &object);
	*pobject = nv_object(object);
	if (ret)
		return ret;

	return 0;
}
