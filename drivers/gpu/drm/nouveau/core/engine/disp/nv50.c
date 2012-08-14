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

#include <engine/software.h>
#include <engine/disp.h>

#include "nv50.h"

/*******************************************************************************
 * EVO channel common helpers
 ******************************************************************************/

static u32
nv50_disp_chan_rd32(struct nouveau_object *object, u64 addr)
{
	return 0xdeadcafe;
}

static void
nv50_disp_chan_wr32(struct nouveau_object *object, u64 addr, u32 data)
{
}

/*******************************************************************************
 * EVO master channel object
 ******************************************************************************/

static int
nv50_disp_mast_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv50_disp_chan *chan;
	int ret;

	ret = nouveau_object_create(parent, engine, oclass, 0, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	return 0;
}

static void
nv50_disp_mast_dtor(struct nouveau_object *object)
{
	struct nv50_disp_chan *chan = (void *)object;
	nouveau_object_destroy(&chan->base);
}

static int
nv50_disp_mast_init(struct nouveau_object *object)
{
	struct nv50_disp_chan *chan = (void *)object;
	int ret;

	ret = nouveau_object_init(&chan->base);
	if (ret)
		return ret;

	return 0;
}

static int
nv50_disp_mast_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_disp_chan *chan = (void *)object;
	return nouveau_object_fini(&chan->base, suspend);
}

struct nouveau_ofuncs
nv50_disp_mast_ofuncs = {
	.ctor = nv50_disp_mast_ctor,
	.dtor = nv50_disp_mast_dtor,
	.init = nv50_disp_mast_init,
	.fini = nv50_disp_mast_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * EVO DMA channel objects (sync, overlay)
 ******************************************************************************/

static int
nv50_disp_dmac_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv50_disp_chan *chan;
	int ret;

	ret = nouveau_object_create(parent, engine, oclass, 0, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	return 0;
}

static void
nv50_disp_dmac_dtor(struct nouveau_object *object)
{
	struct nv50_disp_chan *chan = (void *)object;
	nouveau_object_destroy(&chan->base);
}

static int
nv50_disp_dmac_init(struct nouveau_object *object)
{
	struct nv50_disp_chan *chan = (void *)object;
	int ret;

	ret = nouveau_object_init(&chan->base);
	if (ret)
		return ret;

	return 0;
}

static int
nv50_disp_dmac_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_disp_chan *chan = (void *)object;
	return nouveau_object_fini(&chan->base, suspend);
}

struct nouveau_ofuncs
nv50_disp_dmac_ofuncs = {
	.ctor = nv50_disp_dmac_ctor,
	.dtor = nv50_disp_dmac_dtor,
	.init = nv50_disp_dmac_init,
	.fini = nv50_disp_dmac_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * EVO PIO channel objects (cursor, immediate overlay controls)
 ******************************************************************************/

static int
nv50_disp_pioc_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv50_disp_chan *chan;
	int ret;

	ret = nouveau_object_create(parent, engine, oclass, 0, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	return 0;
}

static void
nv50_disp_pioc_dtor(struct nouveau_object *object)
{
	struct nv50_disp_chan *chan = (void *)object;
	nouveau_object_destroy(&chan->base);
}

static int
nv50_disp_pioc_init(struct nouveau_object *object)
{
	struct nv50_disp_chan *chan = (void *)object;
	int ret;

	ret = nouveau_object_init(&chan->base);
	if (ret)
		return ret;

	return 0;
}

static int
nv50_disp_pioc_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_disp_chan *chan = (void *)object;
	return nouveau_object_fini(&chan->base, suspend);
}

struct nouveau_ofuncs
nv50_disp_pioc_ofuncs = {
	.ctor = nv50_disp_pioc_ctor,
	.dtor = nv50_disp_pioc_dtor,
	.init = nv50_disp_pioc_init,
	.fini = nv50_disp_pioc_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * Base display object
 ******************************************************************************/

static int
nv50_disp_base_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv50_disp_priv *priv = (void *)engine;
	struct nv50_disp_base *base;
	int ret;

	ret = nouveau_parent_create(parent, engine, oclass, 0,
				    priv->sclass, 0, &base);
	*pobject = nv_object(base);
	if (ret)
		return ret;

	return 0;
}

static void
nv50_disp_base_dtor(struct nouveau_object *object)
{
	struct nv50_disp_base *base = (void *)object;
	nouveau_parent_destroy(&base->base);
}

static int
nv50_disp_base_init(struct nouveau_object *object)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_base *base = (void *)object;
	int ret, i;
	u32 tmp;

	ret = nouveau_parent_init(&base->base);
	if (ret)
		return ret;

	/* The below segments of code copying values from one register to
	 * another appear to inform EVO of the display capabilities or
	 * something similar.  NFI what the 0x614004 caps are for..
	 */
	tmp = nv_rd32(priv, 0x614004);
	nv_wr32(priv, 0x610184, tmp);

	/* ... CRTC caps */
	for (i = 0; i < priv->head.nr; i++) {
		tmp = nv_rd32(priv, 0x616100 + (i * 0x800));
		nv_wr32(priv, 0x610190 + (i * 0x10), tmp);
		tmp = nv_rd32(priv, 0x616104 + (i * 0x800));
		nv_wr32(priv, 0x610194 + (i * 0x10), tmp);
		tmp = nv_rd32(priv, 0x616108 + (i * 0x800));
		nv_wr32(priv, 0x610198 + (i * 0x10), tmp);
		tmp = nv_rd32(priv, 0x61610c + (i * 0x800));
		nv_wr32(priv, 0x61019c + (i * 0x10), tmp);
	}

	/* ... DAC caps */
	for (i = 0; i < priv->dac.nr; i++) {
		tmp = nv_rd32(priv, 0x61a000 + (i * 0x800));
		nv_wr32(priv, 0x6101d0 + (i * 0x04), tmp);
	}

	/* ... SOR caps */
	for (i = 0; i < priv->sor.nr; i++) {
		tmp = nv_rd32(priv, 0x61c000 + (i * 0x800));
		nv_wr32(priv, 0x6101e0 + (i * 0x04), tmp);
	}

	/* ... EXT caps */
	for (i = 0; i < 3; i++) {
		tmp = nv_rd32(priv, 0x61e000 + (i * 0x800));
		nv_wr32(priv, 0x6101f0 + (i * 0x04), tmp);
	}

	/* intr 100 */
	/* 6194e8 shit */
	/* intr */
	/* set 610010 from engctx */
	/* acquire mast? */
	return 0;
}

static int
nv50_disp_base_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_disp_base *base = (void *)object;
	return nouveau_parent_fini(&base->base, suspend);
}

struct nouveau_ofuncs
nv50_disp_base_ofuncs = {
	.ctor = nv50_disp_base_ctor,
	.dtor = nv50_disp_base_dtor,
	.init = nv50_disp_base_init,
	.fini = nv50_disp_base_fini,
};

static struct nouveau_oclass
nv50_disp_base_oclass[] = {
	{ 0x5070, &nv50_disp_base_ofuncs },
};

static struct nouveau_oclass
nv50_disp_sclass[] = {
	{ 0x507d, &nv50_disp_mast_ofuncs }, /* master */
	{ 0x507c, &nv50_disp_dmac_ofuncs }, /* sync */
	{ 0x507e, &nv50_disp_dmac_ofuncs }, /* overlay */
	{ 0x507b, &nv50_disp_pioc_ofuncs }, /* overlay (pio) */
	{ 0x507a, &nv50_disp_pioc_ofuncs }, /* cursor (pio) */
	{}
};

/*******************************************************************************
 * Display context, tracks instmem allocation and prevents more than one
 * client using the display hardware at any time.
 ******************************************************************************/

static int
nv50_disp_data_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nouveau_engctx *ectx;
	int ret;

	ret = nouveau_engctx_create(parent, engine, oclass, NULL, 0x10000,
				    0x10000, NVOBJ_FLAG_ZERO_ALLOC, &ectx);
	*pobject = nv_object(ectx);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_oclass
nv50_disp_cclass = {
	.handle = NV_ENGCTX(DISP, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_disp_data_ctor,
		.dtor = _nouveau_engctx_dtor,
		.init = _nouveau_engctx_init,
		.fini = _nouveau_engctx_fini,
		.rd32 = _nouveau_engctx_rd32,
		.wr32 = _nouveau_engctx_wr32,
	},
};

/*******************************************************************************
 * Display engine implementation
 ******************************************************************************/

static void
nv50_disp_intr_vblank(struct nv50_disp_priv *priv, int crtc)
{
	struct nouveau_disp *disp = &priv->base;
	struct nouveau_software_chan *chan, *temp;
	unsigned long flags;

	spin_lock_irqsave(&disp->vblank.lock, flags);
	list_for_each_entry_safe(chan, temp, &disp->vblank.list, vblank.head) {
		if (chan->vblank.crtc != crtc)
			continue;

		nv_wr32(priv, 0x001704, chan->vblank.channel);
		nv_wr32(priv, 0x001710, 0x80000000 | chan->vblank.ctxdma);

		if (nv_device(priv)->chipset == 0x50) {
			nv_wr32(priv, 0x001570, chan->vblank.offset);
			nv_wr32(priv, 0x001574, chan->vblank.value);
		} else {
			if (nv_device(priv)->chipset >= 0xc0) {
				nv_wr32(priv, 0x06000c,
					upper_32_bits(chan->vblank.offset));
			}
			nv_wr32(priv, 0x060010, chan->vblank.offset);
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

void
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

	nv_engine(priv)->sclass = nv50_disp_base_oclass;
	nv_engine(priv)->cclass = &nv50_disp_cclass;
	nv_subdev(priv)->intr = nv50_disp_intr;
	priv->sclass = nv50_disp_sclass;
	priv->head.nr = 2;
	priv->dac.nr = 3;
	priv->sor.nr = 2;

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
