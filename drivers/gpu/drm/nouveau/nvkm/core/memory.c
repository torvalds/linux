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
#include <subdev/instmem.h>

void
nvkm_memory_ctor(const struct nvkm_memory_func *func,
		 struct nvkm_memory *memory)
{
	memory->func = func;
}

void
nvkm_memory_del(struct nvkm_memory **pmemory)
{
	struct nvkm_memory *memory = *pmemory;
	if (memory && !WARN_ON(!memory->func)) {
		if (memory->func->dtor)
			*pmemory = memory->func->dtor(memory);
		kfree(*pmemory);
		*pmemory = NULL;
	}
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
