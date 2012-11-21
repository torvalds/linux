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

#include <subdev/bar.h>

#include <engine/software.h>
#include <engine/disp.h>

struct nv50_disp_priv {
	struct nouveau_disp base;
};

static struct nouveau_oclass
nv50_disp_sclass[] = {
	{},
};

static void
nv50_disp_intr_vblank(struct nv50_disp_priv *priv, int crtc)
{
	struct nouveau_bar *bar = nouveau_bar(priv);
	struct nouveau_disp *disp = &priv->base;
	struct nouveau_software_chan *chan, *temp;
	unsigned long flags;

	spin_lock_irqsave(&disp->vblank.lock, flags);
	list_for_each_entry_safe(chan, temp, &disp->vblank.list, vblank.head) {
		if (chan->vblank.crtc != crtc)
			continue;

		if (nv_device(priv)->chipset == 0x50) {
			nv_wr32(priv, 0x001704, chan->vblank.channel);
			nv_wr32(priv, 0x001710, 0x80000000 | chan->vblank.ctxdma);
			bar->flush(bar);
			nv_wr32(priv, 0x001570, chan->vblank.offset);
			nv_wr32(priv, 0x001574, chan->vblank.value);
		} else {
			nv_wr32(priv, 0x001718, 0x80000000 | chan->vblank.channel);
			bar->flush(bar);
			nv_wr32(priv, 0x06000c,
				upper_32_bits(chan->vblank.offset));
			nv_wr32(priv, 0x060010,
				lower_32_bits(chan->vblank.offset));
			nv_wr32(priv, 0x060014, chan->vblank.value);
		}

		list_del(&chan->vblank.head);
		if (disp->vblank.put)
			disp->vblank.put(disp->vblank.data, crtc);
	}
	spin_unlock_irqrestore(&disp->vblank.lock, flags);

	if (disp->vblank.notify)
		disp->vblank.notify(disp->vblank.data, crtc);
}

static void
nv50_disp_intr(struct nouveau_subdev *subdev)
{
	struct nv50_disp_priv *priv = (void *)subdev;
	u32 stat1 = nv_rd32(priv, 0x610024);

	if (stat1 & 0x00000004) {
		nv50_disp_intr_vblank(priv, 0);
		nv_wr32(priv, 0x610024, 0x00000004);
		stat1 &= ~0x00000004;
	}

	if (stat1 & 0x00000008) {
		nv50_disp_intr_vblank(priv, 1);
		nv_wr32(priv, 0x610024, 0x00000008);
		stat1 &= ~0x00000008;
	}

}

static int
nv50_disp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nv50_disp_priv *priv;
	int ret;

	ret = nouveau_disp_create(parent, engine, oclass, "PDISP",
				  "display", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_engine(priv)->sclass = nv50_disp_sclass;
	nv_subdev(priv)->intr = nv50_disp_intr;

	INIT_LIST_HEAD(&priv->base.vblank.list);
	spin_lock_init(&priv->base.vblank.lock);
	return 0;
}

struct nouveau_oclass
nv50_disp_oclass = {
	.handle = NV_ENGINE(DISP, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_disp_ctor,
		.dtor = _nouveau_disp_dtor,
		.init = _nouveau_disp_init,
		.fini = _nouveau_disp_fini,
	},
};
