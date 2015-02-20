/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "priv.h"

#include <core/device.h>

static void
gk20a_ram_put(struct nvkm_fb *pfb, struct nvkm_mem **pmem)
{
	BUG();
}

static int
gk20a_ram_get(struct nvkm_fb *pfb, u64 size, u32 align, u32 ncmin,
	     u32 memtype, struct nvkm_mem **pmem)
{
	BUG();
}

static int
gk20a_ram_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 datasize,
	       struct nvkm_object **pobject)
{
	struct nvkm_ram *ram;
	int ret;

	ret = nvkm_ram_create(parent, engine, oclass, &ram);
	*pobject = nv_object(ram);
	if (ret)
		return ret;
	ram->type = NV_MEM_TYPE_STOLEN;
	ram->size = get_num_physpages() << PAGE_SHIFT;

	ram->get = gk20a_ram_get;
	ram->put = gk20a_ram_put;
	return 0;
}

struct nvkm_oclass
gk20a_ram_oclass = {
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gk20a_ram_ctor,
		.dtor = _nvkm_ram_dtor,
		.init = _nvkm_ram_init,
		.fini = _nvkm_ram_fini,
	},
};
