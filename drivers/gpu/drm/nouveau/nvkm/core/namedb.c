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
#include <core/namedb.h>
#include <core/gpuobj.h>
#include <core/handle.h>

static struct nvkm_handle *
nvkm_namedb_lookup(struct nvkm_namedb *namedb, u32 name)
{
	struct nvkm_handle *handle;

	list_for_each_entry(handle, &namedb->list, node) {
		if (handle->name == name)
			return handle;
	}

	return NULL;
}

static struct nvkm_handle *
nvkm_namedb_lookup_class(struct nvkm_namedb *namedb, u16 oclass)
{
	struct nvkm_handle *handle;

	list_for_each_entry(handle, &namedb->list, node) {
		if (nv_mclass(handle->object) == oclass)
			return handle;
	}

	return NULL;
}

static struct nvkm_handle *
nvkm_namedb_lookup_vinst(struct nvkm_namedb *namedb, u64 vinst)
{
	struct nvkm_handle *handle;

	list_for_each_entry(handle, &namedb->list, node) {
		if (nv_iclass(handle->object, NV_GPUOBJ_CLASS)) {
			if (nv_gpuobj(handle->object)->addr == vinst)
				return handle;
		}
	}

	return NULL;
}

static struct nvkm_handle *
nvkm_namedb_lookup_cinst(struct nvkm_namedb *namedb, u32 cinst)
{
	struct nvkm_handle *handle;

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
nvkm_namedb_insert(struct nvkm_namedb *namedb, u32 name,
		   struct nvkm_object *object,
		   struct nvkm_handle *handle)
{
	int ret = -EEXIST;
	write_lock_irq(&namedb->lock);
	if (!nvkm_namedb_lookup(namedb, name)) {
		nvkm_object_ref(object, &handle->object);
		handle->namedb = namedb;
		list_add(&handle->node, &namedb->list);
		ret = 0;
	}
	write_unlock_irq(&namedb->lock);
	return ret;
}

void
nvkm_namedb_remove(struct nvkm_handle *handle)
{
	struct nvkm_namedb *namedb = handle->namedb;
	struct nvkm_object *object = handle->object;
	write_lock_irq(&namedb->lock);
	list_del(&handle->node);
	write_unlock_irq(&namedb->lock);
	nvkm_object_ref(NULL, &object);
}

struct nvkm_handle *
nvkm_namedb_get(struct nvkm_namedb *namedb, u32 name)
{
	struct nvkm_handle *handle;
	read_lock(&namedb->lock);
	handle = nvkm_namedb_lookup(namedb, name);
	if (handle == NULL)
		read_unlock(&namedb->lock);
	return handle;
}

struct nvkm_handle *
nvkm_namedb_get_class(struct nvkm_namedb *namedb, u16 oclass)
{
	struct nvkm_handle *handle;
	read_lock(&namedb->lock);
	handle = nvkm_namedb_lookup_class(namedb, oclass);
	if (handle == NULL)
		read_unlock(&namedb->lock);
	return handle;
}

struct nvkm_handle *
nvkm_namedb_get_vinst(struct nvkm_namedb *namedb, u64 vinst)
{
	struct nvkm_handle *handle;
	read_lock(&namedb->lock);
	handle = nvkm_namedb_lookup_vinst(namedb, vinst);
	if (handle == NULL)
		read_unlock(&namedb->lock);
	return handle;
}

struct nvkm_handle *
nvkm_namedb_get_cinst(struct nvkm_namedb *namedb, u32 cinst)
{
	struct nvkm_handle *handle;
	read_lock(&namedb->lock);
	handle = nvkm_namedb_lookup_cinst(namedb, cinst);
	if (handle == NULL)
		read_unlock(&namedb->lock);
	return handle;
}

void
nvkm_namedb_put(struct nvkm_handle *handle)
{
	if (handle)
		read_unlock(&handle->namedb->lock);
}

int
nvkm_namedb_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, u32 pclass,
		    struct nvkm_oclass *sclass, u64 engcls,
		    int length, void **pobject)
{
	struct nvkm_namedb *namedb;
	int ret;

	ret = nvkm_parent_create_(parent, engine, oclass, pclass |
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
_nvkm_namedb_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	struct nvkm_namedb *object;
	int ret;

	ret = nvkm_namedb_create(parent, engine, oclass, 0, NULL, 0, &object);
	*pobject = nv_object(object);
	if (ret)
		return ret;

	return 0;
}
