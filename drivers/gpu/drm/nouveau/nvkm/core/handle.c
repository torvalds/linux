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
	struct nvkm_handle *p = (h)->parent; u32 n = p ? p->name : ~0;         \
	nvif_printk((h)->object, l, INFO, "0x%08x:0x%08x "f, n, (h)->name, ##a);\
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
nvkm_handle_create(struct nvkm_handle *parent, u32 _handle,
		   struct nvkm_object *object, struct nvkm_handle **phandle)
{
	struct nvkm_handle *handle;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	INIT_LIST_HEAD(&handle->head);
	INIT_LIST_HEAD(&handle->tree);
	handle->name = _handle;
	handle->priv = ~0;
	RB_CLEAR_NODE(&handle->rb);
	handle->parent = parent;
	nvkm_object_ref(object, &handle->object);

	if (parent)
		list_add(&handle->head, &handle->parent->tree);

	hprintk(handle, TRACE, "created\n");
	*phandle = handle;
	return 0;
}

void
nvkm_handle_destroy(struct nvkm_handle *handle)
{
	struct nvkm_client *client = nvkm_client(handle->object);
	struct nvkm_handle *item, *temp;

	hprintk(handle, TRACE, "destroy running\n");
	list_for_each_entry_safe(item, temp, &handle->tree, head) {
		nvkm_handle_destroy(item);
	}

	nvkm_client_remove(client, handle);
	list_del(&handle->head);

	hprintk(handle, TRACE, "destroy completed\n");
	nvkm_object_ref(NULL, &handle->object);
	kfree(handle);
}
