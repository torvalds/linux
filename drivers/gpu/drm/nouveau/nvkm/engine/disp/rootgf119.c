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
#include "rootnv50.h"
#include "dmacnv50.h"

#include <core/client.h>
#include <core/ramht.h>
#include <subdev/timer.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

int
gf119_disp_root_scanoutpos(NV50_DISP_MTHD_V0)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	const u32 total  = nvkm_rd32(device, 0x640414 + (head * 0x300));
	const u32 blanke = nvkm_rd32(device, 0x64041c + (head * 0x300));
	const u32 blanks = nvkm_rd32(device, 0x640420 + (head * 0x300));
	union {
		struct nv04_disp_scanoutpos_v0 v0;
	} *args = data;
	int ret;

	nvif_ioctl(object, "disp scanoutpos size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(object, "disp scanoutpos vers %d\n",
			   args->v0.version);
		args->v0.vblanke = (blanke & 0xffff0000) >> 16;
		args->v0.hblanke = (blanke & 0x0000ffff);
		args->v0.vblanks = (blanks & 0xffff0000) >> 16;
		args->v0.hblanks = (blanks & 0x0000ffff);
		args->v0.vtotal  = ( total & 0xffff0000) >> 16;
		args->v0.htotal  = ( total & 0x0000ffff);
		args->v0.time[0] = ktime_to_ns(ktime_get());
		args->v0.vline = /* vline read locks hline */
			nvkm_rd32(device, 0x616340 + (head * 0x800)) & 0xffff;
		args->v0.time[1] = ktime_to_ns(ktime_get());
		args->v0.hline =
			nvkm_rd32(device, 0x616344 + (head * 0x800)) & 0xffff;
	} else
		return ret;

	return 0;
}

void
gf119_disp_root_fini(struct nv50_disp_root *root)
{
	struct nvkm_device *device = root->disp->base.engine.subdev.device;
	/* disable all interrupts */
	nvkm_wr32(device, 0x6100b0, 0x00000000);
}

int
gf119_disp_root_init(struct nv50_disp_root *root)
{
	struct nv50_disp *disp = root->disp;
	struct nvkm_device *device = disp->base.engine.subdev.device;
	u32 tmp;
	int i;

	/* The below segments of code copying values from one register to
	 * another appear to inform EVO of the display capabilities or
	 * something similar.
	 */

	/* ... CRTC caps */
	for (i = 0; i < disp->base.head.nr; i++) {
		tmp = nvkm_rd32(device, 0x616104 + (i * 0x800));
		nvkm_wr32(device, 0x6101b4 + (i * 0x800), tmp);
		tmp = nvkm_rd32(device, 0x616108 + (i * 0x800));
		nvkm_wr32(device, 0x6101b8 + (i * 0x800), tmp);
		tmp = nvkm_rd32(device, 0x61610c + (i * 0x800));
		nvkm_wr32(device, 0x6101bc + (i * 0x800), tmp);
	}

	/* ... DAC caps */
	for (i = 0; i < disp->func->dac.nr; i++) {
		tmp = nvkm_rd32(device, 0x61a000 + (i * 0x800));
		nvkm_wr32(device, 0x6101c0 + (i * 0x800), tmp);
	}

	/* ... SOR caps */
	for (i = 0; i < disp->func->sor.nr; i++) {
		tmp = nvkm_rd32(device, 0x61c000 + (i * 0x800));
		nvkm_wr32(device, 0x6301c4 + (i * 0x800), tmp);
	}

	/* steal display away from vbios, or something like that */
	if (nvkm_rd32(device, 0x6100ac) & 0x00000100) {
		nvkm_wr32(device, 0x6100ac, 0x00000100);
		nvkm_mask(device, 0x6194e8, 0x00000001, 0x00000000);
		if (nvkm_msec(device, 2000,
			if (!(nvkm_rd32(device, 0x6194e8) & 0x00000002))
				break;
		) < 0)
			return -EBUSY;
	}

	/* point at display engine memory area (hash table, objects) */
	nvkm_wr32(device, 0x610010, (root->instmem->addr >> 8) | 9);

	/* enable supervisor interrupts, disable everything else */
	nvkm_wr32(device, 0x610090, 0x00000000);
	nvkm_wr32(device, 0x6100a0, 0x00000000);
	nvkm_wr32(device, 0x6100b0, 0x00000307);

	/* disable underflow reporting, preventing an intermittent issue
	 * on some gk104 boards where the production vbios left this
	 * setting enabled by default.
	 *
	 * ftp://download.nvidia.com/open-gpu-doc/gk104-disable-underflow-reporting/1/gk104-disable-underflow-reporting.txt
	 */
	for (i = 0; i < disp->base.head.nr; i++)
		nvkm_mask(device, 0x616308 + (i * 0x800), 0x00000111, 0x00000010);

	return 0;
}

static const struct nv50_disp_root_func
gf119_disp_root = {
	.init = gf119_disp_root_init,
	.fini = gf119_disp_root_fini,
	.dmac = {
		&gf119_disp_core_oclass,
		&gf119_disp_base_oclass,
		&gf119_disp_ovly_oclass,
	},
	.pioc = {
		&gf119_disp_oimm_oclass,
		&gf119_disp_curs_oclass,
	},
};

static int
gf119_disp_root_new(struct nvkm_disp *disp, const struct nvkm_oclass *oclass,
		    void *data, u32 size, struct nvkm_object **pobject)
{
	return nv50_disp_root_new_(&gf119_disp_root, disp, oclass,
				   data, size, pobject);
}

const struct nvkm_disp_oclass
gf119_disp_root_oclass = {
	.base.oclass = GF110_DISP,
	.base.minver = -1,
	.base.maxver = -1,
	.ctor = gf119_disp_root_new,
};
