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
 */
#include <core/ramht.h>
#include <core/engine.h>
#include <core/object.h>

static u32
nvkm_ramht_hash(struct nvkm_ramht *ramht, int chid, u32 handle)
{
	u32 hash = 0;

	while (handle) {
		hash ^= (handle & ((1 << ramht->bits) - 1));
		handle >>= ramht->bits;
	}

	hash ^= chid << (ramht->bits - 4);
	return hash;
}

struct nvkm_gpuobj *
nvkm_ramht_search(struct nvkm_ramht *ramht, int chid, u32 handle)
{
	u32 co, ho;

	co = ho = nvkm_ramht_hash(ramht, chid, handle);
	do {
		if (ramht->data[co].chid == chid) {
			if (ramht->data[co].handle == handle)
				return ramht->data[co].inst;
		}

		if (++co >= ramht->size)
			co = 0;
	} while (co != ho);

	return NULL;
}

static int
nvkm_ramht_update(struct nvkm_ramht *ramht, int co, struct nvkm_object *object,
		  int chid, int addr, u32 handle, u32 context)
{
	struct nvkm_ramht_data *data = &ramht->data[co];
	u64 inst = 0x00000040; /* just non-zero for <=g8x fifo ramht */
	int ret;

	nvkm_gpuobj_del(&data->inst);
	data->chid = chid;
	data->handle = handle;

	if (object) {
		ret = nvkm_object_bind(object, ramht->parent, 16, &data->inst);
		if (ret) {
			if (ret != -ENODEV) {
				data->chid = -1;
				return ret;
			}
			data->inst = NULL;
		}

		if (data->inst) {
			if (ramht->device->card_type >= NV_50)
				inst = data->inst->node->offset;
			else
				inst = data->inst->addr;
		}

		if (addr < 0) context |= inst << -addr;
		else          context |= inst >>  addr;
	}

	nvkm_kmap(ramht->gpuobj);
	nvkm_wo32(ramht->gpuobj, (co << 3) + 0, handle);
	nvkm_wo32(ramht->gpuobj, (co << 3) + 4, context);
	nvkm_done(ramht->gpuobj);
	return co + 1;
}

void
nvkm_ramht_remove(struct nvkm_ramht *ramht, int cookie)
{
	if (--cookie >= 0)
		nvkm_ramht_update(ramht, cookie, NULL, -1, 0, 0, 0);
}

int
nvkm_ramht_insert(struct nvkm_ramht *ramht, struct nvkm_object *object,
		  int chid, int addr, u32 handle, u32 context)
{
	u32 co, ho;

	if (nvkm_ramht_search(ramht, chid, handle))
		return -EEXIST;

	co = ho = nvkm_ramht_hash(ramht, chid, handle);
	do {
		if (ramht->data[co].chid < 0) {
			return nvkm_ramht_update(ramht, co, object, chid,
						 addr, handle, context);
		}

		if (++co >= ramht->size)
			co = 0;
	} while (co != ho);

	return -ENOSPC;
}

void
nvkm_ramht_del(struct nvkm_ramht **pramht)
{
	struct nvkm_ramht *ramht = *pramht;
	if (ramht) {
		nvkm_gpuobj_del(&ramht->gpuobj);
		vfree(*pramht);
		*pramht = NULL;
	}
}

int
nvkm_ramht_new(struct nvkm_device *device, u32 size, u32 align,
	       struct nvkm_gpuobj *parent, struct nvkm_ramht **pramht)
{
	struct nvkm_ramht *ramht;
	int ret, i;

	if (!(ramht = *pramht = vzalloc(sizeof(*ramht) +
					(size >> 3) * sizeof(*ramht->data))))
		return -ENOMEM;

	ramht->device = device;
	ramht->parent = parent;
	ramht->size = size >> 3;
	ramht->bits = order_base_2(ramht->size);
	for (i = 0; i < ramht->size; i++)
		ramht->data[i].chid = -1;

	ret = nvkm_gpuobj_new(ramht->device, size, align, true,
			      ramht->parent, &ramht->gpuobj);
	if (ret)
		nvkm_ramht_del(pramht);
	return ret;
}
