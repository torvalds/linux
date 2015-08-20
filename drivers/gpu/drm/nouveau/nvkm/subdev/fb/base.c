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
#include "ram.h"

#include <subdev/bios.h>
#include <subdev/bios/M0203.h>

int
nvkm_fb_bios_memtype(struct nvkm_bios *bios)
{
	struct nvkm_subdev *subdev = &bios->subdev;
	struct nvkm_device *device = subdev->device;
	const u8 ramcfg = (nvkm_rd32(device, 0x101000) & 0x0000003c) >> 2;
	struct nvbios_M0203E M0203E;
	u8 ver, hdr;

	if (nvbios_M0203Em(bios, ramcfg, &ver, &hdr, &M0203E)) {
		switch (M0203E.type) {
		case M0203E_TYPE_DDR2 : return NVKM_RAM_TYPE_DDR2;
		case M0203E_TYPE_DDR3 : return NVKM_RAM_TYPE_DDR3;
		case M0203E_TYPE_GDDR3: return NVKM_RAM_TYPE_GDDR3;
		case M0203E_TYPE_GDDR5: return NVKM_RAM_TYPE_GDDR5;
		default:
			nvkm_warn(subdev, "M0203E type %02x\n", M0203E.type);
			return NVKM_RAM_TYPE_UNKNOWN;
		}
	}

	nvkm_warn(subdev, "M0203E not matched!\n");
	return NVKM_RAM_TYPE_UNKNOWN;
}

int
_nvkm_fb_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_fb *fb = (void *)object;
	return nvkm_subdev_fini_old(&fb->subdev, suspend);
}

int
_nvkm_fb_init(struct nvkm_object *object)
{
	struct nvkm_fb *fb = (void *)object;
	int ret, i;

	ret = nvkm_subdev_init_old(&fb->subdev);
	if (ret)
		return ret;

	if (fb->ram)
		nvkm_ram_init(fb->ram);

	for (i = 0; i < fb->tile.regions; i++)
		fb->tile.prog(fb, i, &fb->tile.region[i]);

	return 0;
}

void
_nvkm_fb_dtor(struct nvkm_object *object)
{
	struct nvkm_fb *fb = (void *)object;
	int i;

	for (i = 0; i < fb->tile.regions; i++)
		fb->tile.fini(fb, i, &fb->tile.region[i]);

	nvkm_ram_del(&fb->ram);
	nvkm_subdev_destroy(&fb->subdev);
}

int
nvkm_fb_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, int length, void **pobject)
{
	struct nvkm_fb_impl *impl = (void *)oclass;
	struct nvkm_fb *fb;
	int ret;

	ret = nvkm_subdev_create_(parent, engine, oclass, 0, "PFB", "fb",
				  length, pobject);
	fb = *pobject;
	if (ret)
		return ret;

	fb->memtype_valid = impl->memtype;

	if (!impl->ram_new)
		return 0;

	ret = impl->ram_new(fb, &fb->ram);
	if (ret) {
		nvkm_error(&fb->subdev, "vram init failed, %d\n", ret);
		return ret;
	}

	return 0;
}
