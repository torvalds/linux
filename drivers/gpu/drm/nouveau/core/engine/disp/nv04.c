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

#include <core/event.h>
#include <core/class.h>

struct nv04_disp_priv {
	struct nouveau_disp base;
};

static int
nv04_disp_scanoutpos(struct nouveau_object *object, u32 mthd,
		     void *data, u32 size)
{
	struct nv04_disp_priv *priv = (void *)object->engine;
	struct nv04_display_scanoutpos *args = data;
	const int head = (mthd & NV04_DISP_MTHD_HEAD);
	u32 line;

	if (size < sizeof(*args))
		return -EINVAL;

	args->vblanks = nv_rd32(priv, 0x680800 + (head * 0x2000)) & 0xffff;
	args->vtotal  = nv_rd32(priv, 0x680804 + (head * 0x2000)) & 0xffff;
	args->vblanke = args->vtotal - 1;

	args->hblanks = nv_rd32(priv, 0x680820 + (head * 0x2000)) & 0xffff;
	args->htotal  = nv_rd32(priv, 0x680824 + (head * 0x2000)) & 0xffff;
	args->hblanke = args->htotal - 1;

	/*
	 * If output is vga instead of digital then vtotal/htotal is invalid
	 * so we have to give up and trigger the timestamping fallback in the
	 * drm core.
	 */
	if (!args->vtotal || !args->htotal)
		return -ENOTSUPP;

	args->time[0] = ktime_to_ns(ktime_get());
	line = nv_rd32(priv, 0x600868 + (head * 0x2000));
	args->time[1] = ktime_to_ns(ktime_get());
	args->hline = (line & 0xffff0000) >> 16;
	args->vline = (line & 0x0000ffff);
	return 0;
}

#define HEAD_MTHD(n) (n), (n) + 0x01

static struct nouveau_omthds
nv04_disp_omthds[] = {
	{ HEAD_MTHD(NV04_DISP_SCANOUTPOS), nv04_disp_scanoutpos },
	{}
};

static struct nouveau_oclass
nv04_disp_sclass[] = {
	{ NV04_DISP_CLASS, &nouveau_object_ofuncs, nv04_disp_omthds },
	{},
};

/*******************************************************************************
 * Display engine implementation
 ******************************************************************************/

static void
nv04_disp_vblank_enable(struct nouveau_event *event, int head)
{
	nv_wr32(event->priv, 0x600140 + (head * 0x2000) , 0x00000001);
}

static void
nv04_disp_vblank_disable(struct nouveau_event *event, int head)
{
	nv_wr32(event->priv, 0x600140 + (head * 0x2000) , 0x00000000);
}

static void
nv04_disp_intr(struct nouveau_subdev *subdev)
{
	struct nv04_disp_priv *priv = (void *)subdev;
	u32 crtc0 = nv_rd32(priv, 0x600100);
	u32 crtc1 = nv_rd32(priv, 0x602100);
	u32 pvideo;

	if (crtc0 & 0x00000001) {
		nouveau_event_trigger(priv->base.vblank, 0);
		nv_wr32(priv, 0x600100, 0x00000001);
	}

	if (crtc1 & 0x00000001) {
		nouveau_event_trigger(priv->base.vblank, 1);
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
nv04_disp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv04_disp_priv *priv;
	int ret;

	ret = nouveau_disp_create(parent, engine, oclass, 2, "DISPLAY",
				  "display", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_engine(priv)->sclass = nv04_disp_sclass;
	nv_subdev(priv)->intr = nv04_disp_intr;
	priv->base.vblank->priv = priv;
	priv->base.vblank->enable = nv04_disp_vblank_enable;
	priv->base.vblank->disable = nv04_disp_vblank_disable;
	return 0;
}

struct nouveau_oclass *
nv04_disp_oclass = &(struct nouveau_disp_impl) {
	.base.handle = NV_ENGINE(DISP, 0x04),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_disp_ctor,
		.dtor = _nouveau_disp_dtor,
		.init = _nouveau_disp_init,
		.fini = _nouveau_disp_fini,
	},
}.base;
