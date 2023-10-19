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
#include <core/oproxy.h>

static int
nvkm_oproxy_mthd(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	return nvkm_object_mthd(nvkm_oproxy(object)->object, mthd, data, size);
}

static int
nvkm_oproxy_ntfy(struct nvkm_object *object, u32 mthd,
		 struct nvkm_event **pevent)
{
	return nvkm_object_ntfy(nvkm_oproxy(object)->object, mthd, pevent);
}

static int
nvkm_oproxy_map(struct nvkm_object *object, void *argv, u32 argc,
		enum nvkm_object_map *type, u64 *addr, u64 *size)
{
	struct nvkm_oproxy *oproxy = nvkm_oproxy(object);
	return nvkm_object_map(oproxy->object, argv, argc, type, addr, size);
}

static int
nvkm_oproxy_unmap(struct nvkm_object *object)
{
	return nvkm_object_unmap(nvkm_oproxy(object)->object);
}

static int
nvkm_oproxy_rd08(struct nvkm_object *object, u64 addr, u8 *data)
{
	return nvkm_object_rd08(nvkm_oproxy(object)->object, addr, data);
}

static int
nvkm_oproxy_rd16(struct nvkm_object *object, u64 addr, u16 *data)
{
	return nvkm_object_rd16(nvkm_oproxy(object)->object, addr, data);
}

static int
nvkm_oproxy_rd32(struct nvkm_object *object, u64 addr, u32 *data)
{
	return nvkm_object_rd32(nvkm_oproxy(object)->object, addr, data);
}

static int
nvkm_oproxy_wr08(struct nvkm_object *object, u64 addr, u8 data)
{
	return nvkm_object_wr08(nvkm_oproxy(object)->object, addr, data);
}

static int
nvkm_oproxy_wr16(struct nvkm_object *object, u64 addr, u16 data)
{
	return nvkm_object_wr16(nvkm_oproxy(object)->object, addr, data);
}

static int
nvkm_oproxy_wr32(struct nvkm_object *object, u64 addr, u32 data)
{
	return nvkm_object_wr32(nvkm_oproxy(object)->object, addr, data);
}

static int
nvkm_oproxy_bind(struct nvkm_object *object, struct nvkm_gpuobj *parent,
		 int align, struct nvkm_gpuobj **pgpuobj)
{
	return nvkm_object_bind(nvkm_oproxy(object)->object,
				parent, align, pgpuobj);
}

static int
nvkm_oproxy_sclass(struct nvkm_object *object, int index,
		   struct nvkm_oclass *oclass)
{
	struct nvkm_oproxy *oproxy = nvkm_oproxy(object);
	oclass->parent = oproxy->object;
	if (!oproxy->object->func->sclass)
		return -ENODEV;
	return oproxy->object->func->sclass(oproxy->object, index, oclass);
}

static int
nvkm_oproxy_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_oproxy *oproxy = nvkm_oproxy(object);
	int ret;

	if (oproxy->func->fini[0]) {
		ret = oproxy->func->fini[0](oproxy, suspend);
		if (ret && suspend)
			return ret;
	}

	if (oproxy->object->func->fini) {
		ret = oproxy->object->func->fini(oproxy->object, suspend);
		if (ret && suspend)
			return ret;
	}

	if (oproxy->func->fini[1]) {
		ret = oproxy->func->fini[1](oproxy, suspend);
		if (ret && suspend)
			return ret;
	}

	return 0;
}

static int
nvkm_oproxy_init(struct nvkm_object *object)
{
	struct nvkm_oproxy *oproxy = nvkm_oproxy(object);
	int ret;

	if (oproxy->func->init[0]) {
		ret = oproxy->func->init[0](oproxy);
		if (ret)
			return ret;
	}

	if (oproxy->object->func->init) {
		ret = oproxy->object->func->init(oproxy->object);
		if (ret)
			return ret;
	}

	if (oproxy->func->init[1]) {
		ret = oproxy->func->init[1](oproxy);
		if (ret)
			return ret;
	}

	return 0;
}

static void *
nvkm_oproxy_dtor(struct nvkm_object *object)
{
	struct nvkm_oproxy *oproxy = nvkm_oproxy(object);
	if (oproxy->func->dtor[0])
		oproxy->func->dtor[0](oproxy);
	nvkm_object_del(&oproxy->object);
	if (oproxy->func->dtor[1])
		oproxy->func->dtor[1](oproxy);
	return oproxy;
}

static const struct nvkm_object_func
nvkm_oproxy_func = {
	.dtor = nvkm_oproxy_dtor,
	.init = nvkm_oproxy_init,
	.fini = nvkm_oproxy_fini,
	.mthd = nvkm_oproxy_mthd,
	.ntfy = nvkm_oproxy_ntfy,
	.map = nvkm_oproxy_map,
	.unmap = nvkm_oproxy_unmap,
	.rd08 = nvkm_oproxy_rd08,
	.rd16 = nvkm_oproxy_rd16,
	.rd32 = nvkm_oproxy_rd32,
	.wr08 = nvkm_oproxy_wr08,
	.wr16 = nvkm_oproxy_wr16,
	.wr32 = nvkm_oproxy_wr32,
	.bind = nvkm_oproxy_bind,
	.sclass = nvkm_oproxy_sclass,
};

void
nvkm_oproxy_ctor(const struct nvkm_oproxy_func *func,
		 const struct nvkm_oclass *oclass, struct nvkm_oproxy *oproxy)
{
	nvkm_object_ctor(&nvkm_oproxy_func, oclass, &oproxy->base);
	oproxy->func = func;
}

int
nvkm_oproxy_new_(const struct nvkm_oproxy_func *func,
		 const struct nvkm_oclass *oclass, struct nvkm_oproxy **poproxy)
{
	if (!(*poproxy = kzalloc(sizeof(**poproxy), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_oproxy_ctor(func, oclass, *poproxy);
	return 0;
}
