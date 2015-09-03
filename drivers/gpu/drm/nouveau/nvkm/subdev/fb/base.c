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
#include "priv.h"

#include <subdev/bios.h>
#include <subdev/bios/M0203.h>

int
nvkm_fb_bios_memtype(struct nvkm_bios *bios)
{
	const u8 ramcfg = (nv_rd32(bios, 0x101000) & 0x0000003c) >> 2;
	struct nvbios_M0203E M0203E;
	u8 ver, hdr;

	if (nvbios_M0203Em(bios, ramcfg, &ver, &hdr, &M0203E)) {
		switch (M0203E.type) {
		case M0203E_TYPE_DDR2 : return NV_MEM_TYPE_DDR2;
		case M0203E_TYPE_DDR3 : return NV_MEM_TYPE_DDR3;
		case M0203E_TYPE_GDDR3: return NV_MEM_TYPE_GDDR3;
		case M0203E_TYPE_GDDR5: return NV_MEM_TYPE_GDDR5;
		default:
			nv_warn(bios, "M0203E type %02x\n", M0203E.type);
			return NV_MEM_TYPE_UNKNOWN;
		}
	}

	nv_warn(bios, "M0203E not matched!\n");
	return NV_MEM_TYPE_UNKNOWN;
}

int
_nvkm_fb_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_fb *pfb = (void *)object;
	int ret;

	if (pfb->ram) {
		ret = nv_ofuncs(pfb->ram)->fini(nv_object(pfb->ram), suspend);
		if (ret && suspend)
			return ret;
	}

	return nvkm_subdev_fini(&pfb->base, suspend);
}

int
_nvkm_fb_init(struct nvkm_object *object)
{
	struct nvkm_fb *pfb = (void *)object;
	int ret, i;

	ret = nvkm_subdev_init(&pfb->base);
	if (ret)
		return ret;

	if (pfb->ram) {
		ret = nv_ofuncs(pfb->ram)->init(nv_object(pfb->ram));
		if (ret)
			return ret;
	}

	for (i = 0; i < pfb->tile.regions; i++)
		pfb->tile.prog(pfb, i, &pfb->tile.region[i]);

	return 0;
}

void
_nvkm_fb_dtor(struct nvkm_object *object)
{
	struct nvkm_fb *pfb = (void *)object;
	int i;

	for (i = 0; i < pfb->tile.regions; i++)
		pfb->tile.fini(pfb, i, &pfb->tile.region[i]);
	nvkm_mm_fini(&pfb->tags);

	if (pfb->ram) {
		nvkm_mm_fini(&pfb->vram);
		nvkm_object_ref(NULL, (struct nvkm_object **)&pfb->ram);
	}

	nvkm_subdev_destroy(&pfb->base);
}

int
nvkm_fb_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, int length, void **pobject)
{
	struct nvkm_fb_impl *impl = (void *)oclass;
	static const char *name[] = {
		[NV_MEM_TYPE_UNKNOWN] = "unknown",
		[NV_MEM_TYPE_STOLEN ] = "stolen system memory",
		[NV_MEM_TYPE_SGRAM  ] = "SGRAM",
		[NV_MEM_TYPE_SDRAM  ] = "SDRAM",
		[NV_MEM_TYPE_DDR1   ] = "DDR1",
		[NV_MEM_TYPE_DDR2   ] = "DDR2",
		[NV_MEM_TYPE_DDR3   ] = "DDR3",
		[NV_MEM_TYPE_GDDR2  ] = "GDDR2",
		[NV_MEM_TYPE_GDDR3  ] = "GDDR3",
		[NV_MEM_TYPE_GDDR4  ] = "GDDR4",
		[NV_MEM_TYPE_GDDR5  ] = "GDDR5",
	};
	struct nvkm_object *ram;
	struct nvkm_fb *pfb;
	int ret;

	ret = nvkm_subdev_create_(parent, engine, oclass, 0, "PFB", "fb",
				  length, pobject);
	pfb = *pobject;
	if (ret)
		return ret;

	pfb->memtype_valid = impl->memtype;

	if (!impl->ram)
		return 0;

	ret = nvkm_object_ctor(nv_object(pfb), NULL, impl->ram, NULL, 0, &ram);
	if (ret) {
		nv_fatal(pfb, "error detecting memory configuration!!\n");
		return ret;
	}

	pfb->ram = (void *)ram;

	if (!nvkm_mm_initialised(&pfb->vram)) {
		ret = nvkm_mm_init(&pfb->vram, 0, pfb->ram->size >> 12, 1);
		if (ret)
			return ret;
	}

	if (!nvkm_mm_initialised(&pfb->tags)) {
		ret = nvkm_mm_init(&pfb->tags, 0, pfb->ram->tags ?
				   ++pfb->ram->tags : 0, 1);
		if (ret)
			return ret;
	}

	nv_info(pfb, "RAM type: %s\n", name[pfb->ram->type]);
	nv_info(pfb, "RAM size: %d MiB\n", (int)(pfb->ram->size >> 20));
	nv_info(pfb, "   ZCOMP: %d tags\n", pfb->ram->tags);
	return 0;
}
