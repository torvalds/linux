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

#include <core/client.h>
#include <core/device.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

struct nv04_disp_priv {
	struct nvkm_disp base;
};

static int
nv04_disp_scanoutpos(struct nvkm_object *object, struct nv04_disp_priv *priv,
		     void *data, u32 size, int head)
{
	const u32 hoff = head * 0x2000;
	union {
		struct nv04_disp_scanoutpos_v0 v0;
	} *args = data;
	u32 line;
	int ret;

	nv_ioctl(object, "disp scanoutpos size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "disp scanoutpos vers %d\n", args->v0.version);
		args->v0.vblanks = nv_rd32(priv, 0x680800 + hoff) & 0xffff;
		args->v0.vtotal  = nv_rd32(priv, 0x680804 + hoff) & 0xffff;
		args->v0.vblanke = args->v0.vtotal - 1;

		args->v0.hblanks = nv_rd32(priv, 0x680820 + hoff) & 0xffff;
		args->v0.htotal  = nv_rd32(priv, 0x680824 + hoff) & 0xffff;
		args->v0.hblanke = args->v0.htotal - 1;

		/*
		 * If output is vga instead of digital then vtotal/htotal is
		 * invalid so we have to give up and trigger the timestamping
		 * fallback in the drm core.
		 */
		if (!args->v0.vtotal || !args->v0.htotal)
			return -ENOTSUPP;

		args->v0.time[0] = ktime_to_ns(ktime_get());
		line = nv_rd32(priv, 0x600868 + hoff);
		args->v0.time[1] = ktime_to_ns(ktime_get());
		args->v0.hline = (line & 0xffff0000) >> 16;
		args->v0.vline = (line & 0x0000ffff);
	} else
		return ret;

	return 0;
}

static int
nv04_disp_mthd(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	union {
		struct nv04_disp_mthd_v0 v0;
	} *args = data;
	struct nv04_disp_priv *priv = (void *)object->engine;
	int head, ret;

	nv_ioctl(object, "disp mthd size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, true)) {
		nv_ioctl(object, "disp mthd vers %d mthd %02x head %d\n",
			 args->v0.version, args->v0.method, args->v0.head);
		mthd = args->v0.method;
		head = args->v0.head;
	} else
		return ret;

	if (head < 0 || head >= 2)
		return -ENXIO;

	switch (mthd) {
	case NV04_DISP_SCANOUTPOS:
		return nv04_disp_scanoutpos(object, priv, data, size, head);
	default:
		break;
	}

	return -EINVAL;
}

static struct nvkm_ofuncs
nv04_disp_ofuncs = {
	.ctor = _nvkm_object_ctor,
	.dtor = nvkm_object_destroy,
	.init = nvkm_object_init,
	.fini = nvkm_object_fini,
	.mthd = nv04_disp_mthd,
	.ntfy = nvkm_disp_ntfy,
};

static struct nvkm_oclass
nv04_disp_sclass[] = {
	{ NV04_DISP, &nv04_disp_ofuncs },
	{},
};

/*******************************************************************************
 * Display engine implementation
 ******************************************************************************/

static void
nv04_disp_vblank_init(struct nvkm_event *event, int type, int head)
{
	struct nvkm_disp *disp = container_of(event, typeof(*disp), vblank);
	nv_wr32(disp, 0x600140 + (head * 0x2000) , 0x00000001);
}

static void
nv04_disp_vblank_fini(struct nvkm_event *event, int type, int head)
{
	struct nvkm_disp *disp = container_of(event, typeof(*disp), vblank);
	nv_wr32(disp, 0x600140 + (head * 0x2000) , 0x00000000);
}

static const struct nvkm_event_func
nv04_disp_vblank_func = {
	.ctor = nvkm_disp_vblank_ctor,
	.init = nv04_disp_vblank_init,
	.fini = nv04_disp_vblank_fini,
};

static void
nv04_disp_intr(struct nvkm_subdev *subdev)
{
	struct nv04_disp_priv *priv = (void *)subdev;
	u32 crtc0 = nv_rd32(priv, 0x600100);
	u32 crtc1 = nv_rd32(priv, 0x602100);
	u32 pvideo;

	if (crtc0 & 0x00000001) {
		nvkm_disp_vblank(&priv->base, 0);
		nv_wr32(priv, 0x600100, 0x00000001);
	}

	if (crtc1 & 0x00000001) {
		nvkm_disp_vblank(&priv->base, 1);
		nv_wr32(priv, 0x602100, 0x00000001);
	}

	if (nv_device(priv)->chipset >= 0x10 &&
	    nv_device(priv)->chipset <= 0x40) {
		pvideo = nv_rd32(priv, 0x8100);
		if (pvideo & ~0x11)
			nv_info(priv, "PVIDEO intr: %08x\n", pvideo);
		nv_wr32(priv, 0x8100, pvideo);
	}
}

static int
nv04_disp_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nv04_disp_priv *priv;
	int ret;

	ret = nvkm_disp_create(parent, engine, oclass, 2, "DISPLAY",
			       "display", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_engine(priv)->sclass = nv04_disp_sclass;
	nv_subdev(priv)->intr = nv04_disp_intr;
	return 0;
}

struct nvkm_oclass *
nv04_disp_oclass = &(struct nvkm_disp_impl) {
	.base.handle = NV_ENGINE(DISP, 0x04),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_disp_ctor,
		.dtor = _nvkm_disp_dtor,
		.init = _nvkm_disp_init,
		.fini = _nvkm_disp_fini,
	},
	.vblank = &nv04_disp_vblank_func,
}.base;
