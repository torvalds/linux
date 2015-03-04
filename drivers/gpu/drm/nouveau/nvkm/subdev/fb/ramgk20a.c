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

struct gk20a_mem {
	struct nvkm_mem base;
	void *cpuaddr;
	dma_addr_t handle;
};
#define to_gk20a_mem(m) container_of(m, struct gk20a_mem, base)

static void
gk20a_ram_put(struct nvkm_fb *pfb, struct nvkm_mem **pmem)
{
	struct device *dev = nv_device_base(nv_device(pfb));
	struct gk20a_mem *mem = to_gk20a_mem(*pmem);

	*pmem = NULL;
	if (unlikely(mem == NULL))
		return;

	if (likely(mem->cpuaddr))
		dma_free_coherent(dev, mem->base.size << PAGE_SHIFT,
				  mem->cpuaddr, mem->handle);

	kfree(mem->base.pages);
	kfree(mem);
}

static int
gk20a_ram_get(struct nvkm_fb *pfb, u64 size, u32 align, u32 ncmin,
	     u32 memtype, struct nvkm_mem **pmem)
{
	struct device *dev = nv_device_base(nv_device(pfb));
	struct gk20a_mem *mem;
	u32 type = memtype & 0xff;
	u32 npages, order;
	int i;

	nv_debug(pfb, "%s: size: %llx align: %x, ncmin: %x\n", __func__, size,
		 align, ncmin);

	npages = size >> PAGE_SHIFT;
	if (npages == 0)
		npages = 1;

	if (align == 0)
		align = PAGE_SIZE;
	align >>= PAGE_SHIFT;

	/* round alignment to the next power of 2, if needed */
	order = fls(align);
	if ((align & (align - 1)) == 0)
		order--;
	align = BIT(order);

	/* ensure returned address is correctly aligned */
	npages = max(align, npages);

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	mem->base.size = npages;
	mem->base.memtype = type;

	mem->base.pages = kzalloc(sizeof(dma_addr_t) * npages, GFP_KERNEL);
	if (!mem->base.pages) {
		kfree(mem);
		return -ENOMEM;
	}

	*pmem = &mem->base;

	mem->cpuaddr = dma_alloc_coherent(dev, npages << PAGE_SHIFT,
					  &mem->handle, GFP_KERNEL);
	if (!mem->cpuaddr) {
		nv_error(pfb, "%s: cannot allocate memory!\n", __func__);
		gk20a_ram_put(pfb, pmem);
		return -ENOMEM;
	}

	align <<= PAGE_SHIFT;

	/* alignment check */
	if (unlikely(mem->handle & (align - 1)))
		nv_warn(pfb, "memory not aligned as requested: %pad (0x%x)\n",
			&mem->handle, align);

	nv_debug(pfb, "alloc size: 0x%x, align: 0x%x, paddr: %pad, vaddr: %p\n",
		 npages << PAGE_SHIFT, align, &mem->handle, mem->cpuaddr);

	for (i = 0; i < npages; i++)
		mem->base.pages[i] = mem->handle + (PAGE_SIZE * i);

	mem->base.offset = (u64)mem->base.pages[0];
	return 0;
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
