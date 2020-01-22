/*
 * Copyright 2015 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include <core/memory.h>
#include <core/mm.h>
#include <subdev/fb.h>
#include <subdev/instmem.h>

void
nvkm_memory_tags_put(struct nvkm_memory *memory, struct nvkm_device *device,
		     struct nvkm_tags **ptags)
{
	struct nvkm_fb *fb = device->fb;
	struct nvkm_tags *tags = *ptags;
	if (tags) {
		mutex_lock(&fb->subdev.mutex);
		if (refcount_dec_and_test(&tags->refcount)) {
			nvkm_mm_free(&fb->tags, &tags->mn);
			kfree(memory->tags);
			memory->tags = NULL;
		}
		mutex_unlock(&fb->subdev.mutex);
		*ptags = NULL;
	}
}

int
nvkm_memory_tags_get(struct nvkm_memory *memory, struct nvkm_device *device,
		     u32 nr, void (*clr)(struct nvkm_device *, u32, u32),
		     struct nvkm_tags **ptags)
{
	struct nvkm_fb *fb = device->fb;
	struct nvkm_tags *tags;

	mutex_lock(&fb->subdev.mutex);
	if ((tags = memory->tags)) {
		/* If comptags exist for the memory, but a different amount
		 * than requested, the buffer is being mapped with settings
		 * that are incompatible with existing mappings.
		 */
		if (tags->mn && tags->mn->length != nr) {
			mutex_unlock(&fb->subdev.mutex);
			return -EINVAL;
		}

		refcount_inc(&tags->refcount);
		mutex_unlock(&fb->subdev.mutex);
		*ptags = tags;
		return 0;
	}

	if (!(tags = kmalloc(sizeof(*tags), GFP_KERNEL))) {
		mutex_unlock(&fb->subdev.mutex);
		return -ENOMEM;
	}

	if (!nvkm_mm_head(&fb->tags, 0, 1, nr, nr, 1, &tags->mn)) {
		if (clr)
			clr(device, tags->mn->offset, tags->mn->length);
	} else {
		/* Failure to allocate HW comptags is not an error, the
		 * caller should fall back to an uncompressed map.
		 *
		 * As memory can be mapped in multiple places, we still
		 * need to track the allocation failure and ensure that
		 * any additional mappings remain uncompressed.
		 *
		 * This is handled by returning an empty nvkm_tags.
		 */
		tags->mn = NULL;
	}

	refcount_set(&tags->refcount, 1);
	*ptags = memory->tags = tags;
	mutex_unlock(&fb->subdev.mutex);
	return 0;
}

void
nvkm_memory_ctor(const struct nvkm_memory_func *func,
		 struct nvkm_memory *memory)
{
	memory->func = func;
	kref_init(&memory->kref);
}

static void
nvkm_memory_del(struct kref *kref)
{
	struct nvkm_memory *memory = container_of(kref, typeof(*memory), kref);
	if (!WARN_ON(!memory->func)) {
		if (memory->func->dtor)
			memory = memory->func->dtor(memory);
		kfree(memory);
	}
}

void
nvkm_memory_unref(struct nvkm_memory **pmemory)
{
	struct nvkm_memory *memory = *pmemory;
	if (memory) {
		kref_put(&memory->kref, nvkm_memory_del);
		*pmemory = NULL;
	}
}

struct nvkm_memory *
nvkm_memory_ref(struct nvkm_memory *memory)
{
	if (memory)
		kref_get(&memory->kref);
	return memory;
}

int
nvkm_memory_new(struct nvkm_device *device, enum nvkm_memory_target target,
		u64 size, u32 align, bool zero,
		struct nvkm_memory **pmemory)
{
	struct nvkm_instmem *imem = device->imem;
	struct nvkm_memory *memory;
	int ret = -ENOSYS;

	if (unlikely(target != NVKM_MEM_TARGET_INST || !imem))
		return -ENOSYS;

	ret = nvkm_instobj_new(imem, size, align, zero, &memory);
	if (ret)
		return ret;

	*pmemory = memory;
	return 0;
}
