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

#include <core/object.h>
#include <core/parent.h>
#include <core/handle.h>
#include <core/class.h>

#include <engine/disp.h>

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/disp.h>
#include <subdev/bios/init.h>
#include <subdev/bios/pll.h>
#include <subdev/devinit.h>
#include <subdev/timer.h>
#include <subdev/fb.h>

#include "nv50.h"

/*******************************************************************************
 * EVO channel base class
 ******************************************************************************/

int
nv50_disp_chan_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, int chid,
		       int length, void **pobject)
{
	struct nv50_disp_base *base = (void *)parent;
	struct nv50_disp_chan *chan;
	int ret;

	if (base->chan & (1 << chid))
		return -EBUSY;
	base->chan |= (1 << chid);

	ret = nouveau_namedb_create_(parent, engine, oclass, 0, NULL,
				     (1ULL << NVDEV_ENGINE_DMAOBJ),
				     length, pobject);
	chan = *pobject;
	if (ret)
		return ret;

	chan->chid = chid;
	return 0;
}

void
nv50_disp_chan_destroy(struct nv50_disp_chan *chan)
{
	struct nv50_disp_base *base = (void *)nv_object(chan)->parent;
	base->chan &= ~(1 << chan->chid);
	nouveau_namedb_destroy(&chan->base);
}

u32
nv50_disp_chan_rd32(struct nouveau_object *object, u64 addr)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_chan *chan = (void *)object;
	return nv_rd32(priv, 0x640000 + (chan->chid * 0x1000) + addr);
}

void
nv50_disp_chan_wr32(struct nouveau_object *object, u64 addr, u32 data)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_chan *chan = (void *)object;
	nv_wr32(priv, 0x640000 + (chan->chid * 0x1000) + addr, data);
}

/*******************************************************************************
 * EVO DMA channel base class
 ******************************************************************************/

static int
nv50_disp_dmac_object_attach(struct nouveau_object *parent,
			     struct nouveau_object *object, u32 name)
{
	struct nv50_disp_base *base = (void *)parent->parent;
	struct nv50_disp_chan *chan = (void *)parent;
	u32 addr = nv_gpuobj(object)->node->offset;
	u32 chid = chan->chid;
	u32 data = (chid << 28) | (addr << 10) | chid;
	return nouveau_ramht_insert(base->ramht, chid, name, data);
}

static void
nv50_disp_dmac_object_detach(struct nouveau_object *parent, int cookie)
{
	struct nv50_disp_base *base = (void *)parent->parent;
	nouveau_ramht_remove(base->ramht, cookie);
}

int
nv50_disp_dmac_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, u32 pushbuf, int chid,
		       int length, void **pobject)
{
	struct nv50_disp_dmac *dmac;
	int ret;

	ret = nv50_disp_chan_create_(parent, engine, oclass, chid,
				     length, pobject);
	dmac = *pobject;
	if (ret)
		return ret;

	dmac->pushdma = (void *)nouveau_handle_ref(parent, pushbuf);
	if (!dmac->pushdma)
		return -ENOENT;

	switch (nv_mclass(dmac->pushdma)) {
	case 0x0002:
	case 0x003d:
		if (dmac->pushdma->limit - dmac->pushdma->start != 0xfff)
			return -EINVAL;

		switch (dmac->pushdma->target) {
		case NV_MEM_TARGET_VRAM:
			dmac->push = 0x00000000 | dmac->pushdma->start >> 8;
			break;
		case NV_MEM_TARGET_PCI_NOSNOOP:
			dmac->push = 0x00000003 | dmac->pushdma->start >> 8;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void
nv50_disp_dmac_dtor(struct nouveau_object *object)
{
	struct nv50_disp_dmac *dmac = (void *)object;
	nouveau_object_ref(NULL, (struct nouveau_object **)&dmac->pushdma);
	nv50_disp_chan_destroy(&dmac->base);
}

static int
nv50_disp_dmac_init(struct nouveau_object *object)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_dmac *dmac = (void *)object;
	int chid = dmac->base.chid;
	int ret;

	ret = nv50_disp_chan_init(&dmac->base);
	if (ret)
		return ret;

	/* enable error reporting */
	nv_mask(priv, 0x610028, 0x00010001 << chid, 0x00010001 << chid);

	/* initialise channel for dma command submission */
	nv_wr32(priv, 0x610204 + (chid * 0x0010), dmac->push);
	nv_wr32(priv, 0x610208 + (chid * 0x0010), 0x00010000);
	nv_wr32(priv, 0x61020c + (chid * 0x0010), chid);
	nv_mask(priv, 0x610200 + (chid * 0x0010), 0x00000010, 0x00000010);
	nv_wr32(priv, 0x640000 + (chid * 0x1000), 0x00000000);
	nv_wr32(priv, 0x610200 + (chid * 0x0010), 0x00000013);

	/* wait for it to go inactive */
	if (!nv_wait(priv, 0x610200 + (chid * 0x10), 0x80000000, 0x00000000)) {
		nv_error(dmac, "init timeout, 0x%08x\n",
			 nv_rd32(priv, 0x610200 + (chid * 0x10)));
		return -EBUSY;
	}

	return 0;
}

static int
nv50_disp_dmac_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_dmac *dmac = (void *)object;
	int chid = dmac->base.chid;

	/* deactivate channel */
	nv_mask(priv, 0x610200 + (chid * 0x0010), 0x00001010, 0x00001000);
	nv_mask(priv, 0x610200 + (chid * 0x0010), 0x00000003, 0x00000000);
	if (!nv_wait(priv, 0x610200 + (chid * 0x10), 0x001e0000, 0x00000000)) {
		nv_error(dmac, "fini timeout, 0x%08x\n",
			 nv_rd32(priv, 0x610200 + (chid * 0x10)));
		if (suspend)
			return -EBUSY;
	}

	/* disable error reporting */
	nv_mask(priv, 0x610028, 0x00010001 << chid, 0x00000000 << chid);

	return nv50_disp_chan_fini(&dmac->base, suspend);
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
	struct nv50_display_mast_class *args = data;
	struct nv50_disp_dmac *mast;
	int ret;

	if (size < sizeof(*args))
		return -EINVAL;

	ret = nv50_disp_dmac_create_(parent, engine, oclass, args->pushbuf,
				     0, sizeof(*mast), (void **)&mast);
	*pobject = nv_object(mast);
	if (ret)
		return ret;

	nv_parent(mast)->object_attach = nv50_disp_dmac_object_attach;
	nv_parent(mast)->object_detach = nv50_disp_dmac_object_detach;
	return 0;
}

static int
nv50_disp_mast_init(struct nouveau_object *object)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_dmac *mast = (void *)object;
	int ret;

	ret = nv50_disp_chan_init(&mast->base);
	if (ret)
		return ret;

	/* enable error reporting */
	nv_mask(priv, 0x610028, 0x00010001, 0x00010001);

	/* attempt to unstick channel from some unknown state */
	if ((nv_rd32(priv, 0x610200) & 0x009f0000) == 0x00020000)
		nv_mask(priv, 0x610200, 0x00800000, 0x00800000);
	if ((nv_rd32(priv, 0x610200) & 0x003f0000) == 0x00030000)
		nv_mask(priv, 0x610200, 0x00600000, 0x00600000);

	/* initialise channel for dma command submission */
	nv_wr32(priv, 0x610204, mast->push);
	nv_wr32(priv, 0x610208, 0x00010000);
	nv_wr32(priv, 0x61020c, 0x00000000);
	nv_mask(priv, 0x610200, 0x00000010, 0x00000010);
	nv_wr32(priv, 0x640000, 0x00000000);
	nv_wr32(priv, 0x610200, 0x01000013);

	/* wait for it to go inactive */
	if (!nv_wait(priv, 0x610200, 0x80000000, 0x00000000)) {
		nv_error(mast, "init: 0x%08x\n", nv_rd32(priv, 0x610200));
		return -EBUSY;
	}

	return 0;
}

static int
nv50_disp_mast_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_dmac *mast = (void *)object;

	/* deactivate channel */
	nv_mask(priv, 0x610200, 0x00000010, 0x00000000);
	nv_mask(priv, 0x610200, 0x00000003, 0x00000000);
	if (!nv_wait(priv, 0x610200, 0x001e0000, 0x00000000)) {
		nv_error(mast, "fini: 0x%08x\n", nv_rd32(priv, 0x610200));
		if (suspend)
			return -EBUSY;
	}

	/* disable error reporting */
	nv_mask(priv, 0x610028, 0x00010001, 0x00000000);

	return nv50_disp_chan_fini(&mast->base, suspend);
}

struct nouveau_ofuncs
nv50_disp_mast_ofuncs = {
	.ctor = nv50_disp_mast_ctor,
	.dtor = nv50_disp_dmac_dtor,
	.init = nv50_disp_mast_init,
	.fini = nv50_disp_mast_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * EVO sync channel objects
 ******************************************************************************/

static int
nv50_disp_sync_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv50_display_sync_class *args = data;
	struct nv50_disp_dmac *dmac;
	int ret;

	if (size < sizeof(*args) || args->head > 1)
		return -EINVAL;

	ret = nv50_disp_dmac_create_(parent, engine, oclass, args->pushbuf,
				     1 + args->head, sizeof(*dmac),
				     (void **)&dmac);
	*pobject = nv_object(dmac);
	if (ret)
		return ret;

	nv_parent(dmac)->object_attach = nv50_disp_dmac_object_attach;
	nv_parent(dmac)->object_detach = nv50_disp_dmac_object_detach;
	return 0;
}

struct nouveau_ofuncs
nv50_disp_sync_ofuncs = {
	.ctor = nv50_disp_sync_ctor,
	.dtor = nv50_disp_dmac_dtor,
	.init = nv50_disp_dmac_init,
	.fini = nv50_disp_dmac_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * EVO overlay channel objects
 ******************************************************************************/

static int
nv50_disp_ovly_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv50_display_ovly_class *args = data;
	struct nv50_disp_dmac *dmac;
	int ret;

	if (size < sizeof(*args) || args->head > 1)
		return -EINVAL;

	ret = nv50_disp_dmac_create_(parent, engine, oclass, args->pushbuf,
				     3 + args->head, sizeof(*dmac),
				     (void **)&dmac);
	*pobject = nv_object(dmac);
	if (ret)
		return ret;

	nv_parent(dmac)->object_attach = nv50_disp_dmac_object_attach;
	nv_parent(dmac)->object_detach = nv50_disp_dmac_object_detach;
	return 0;
}

struct nouveau_ofuncs
nv50_disp_ovly_ofuncs = {
	.ctor = nv50_disp_ovly_ctor,
	.dtor = nv50_disp_dmac_dtor,
	.init = nv50_disp_dmac_init,
	.fini = nv50_disp_dmac_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * EVO PIO channel base class
 ******************************************************************************/

static int
nv50_disp_pioc_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, int chid,
		       int length, void **pobject)
{
	return nv50_disp_chan_create_(parent, engine, oclass, chid,
				      length, pobject);
}

static void
nv50_disp_pioc_dtor(struct nouveau_object *object)
{
	struct nv50_disp_pioc *pioc = (void *)object;
	nv50_disp_chan_destroy(&pioc->base);
}

static int
nv50_disp_pioc_init(struct nouveau_object *object)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_pioc *pioc = (void *)object;
	int chid = pioc->base.chid;
	int ret;

	ret = nv50_disp_chan_init(&pioc->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x610200 + (chid * 0x10), 0x00002000);
	if (!nv_wait(priv, 0x610200 + (chid * 0x10), 0x00000000, 0x00000000)) {
		nv_error(pioc, "timeout0: 0x%08x\n",
			 nv_rd32(priv, 0x610200 + (chid * 0x10)));
		return -EBUSY;
	}

	nv_wr32(priv, 0x610200 + (chid * 0x10), 0x00000001);
	if (!nv_wait(priv, 0x610200 + (chid * 0x10), 0x00030000, 0x00010000)) {
		nv_error(pioc, "timeout1: 0x%08x\n",
			 nv_rd32(priv, 0x610200 + (chid * 0x10)));
		return -EBUSY;
	}

	return 0;
}

static int
nv50_disp_pioc_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_pioc *pioc = (void *)object;
	int chid = pioc->base.chid;

	nv_mask(priv, 0x610200 + (chid * 0x10), 0x00000001, 0x00000000);
	if (!nv_wait(priv, 0x610200 + (chid * 0x10), 0x00030000, 0x00000000)) {
		nv_error(pioc, "timeout: 0x%08x\n",
			 nv_rd32(priv, 0x610200 + (chid * 0x10)));
		if (suspend)
			return -EBUSY;
	}

	return nv50_disp_chan_fini(&pioc->base, suspend);
}

/*******************************************************************************
 * EVO immediate overlay channel objects
 ******************************************************************************/

static int
nv50_disp_oimm_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv50_display_oimm_class *args = data;
	struct nv50_disp_pioc *pioc;
	int ret;

	if (size < sizeof(*args) || args->head > 1)
		return -EINVAL;

	ret = nv50_disp_pioc_create_(parent, engine, oclass, 5 + args->head,
				     sizeof(*pioc), (void **)&pioc);
	*pobject = nv_object(pioc);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_ofuncs
nv50_disp_oimm_ofuncs = {
	.ctor = nv50_disp_oimm_ctor,
	.dtor = nv50_disp_pioc_dtor,
	.init = nv50_disp_pioc_init,
	.fini = nv50_disp_pioc_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * EVO cursor channel objects
 ******************************************************************************/

static int
nv50_disp_curs_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv50_display_curs_class *args = data;
	struct nv50_disp_pioc *pioc;
	int ret;

	if (size < sizeof(*args) || args->head > 1)
		return -EINVAL;

	ret = nv50_disp_pioc_create_(parent, engine, oclass, 7 + args->head,
				     sizeof(*pioc), (void **)&pioc);
	*pobject = nv_object(pioc);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_ofuncs
nv50_disp_curs_ofuncs = {
	.ctor = nv50_disp_curs_ctor,
	.dtor = nv50_disp_pioc_dtor,
	.init = nv50_disp_pioc_init,
	.fini = nv50_disp_pioc_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * Base display object
 ******************************************************************************/

int
nv50_disp_base_scanoutpos(struct nouveau_object *object, u32 mthd,
			  void *data, u32 size)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv04_display_scanoutpos *args = data;
	const int head = (mthd & NV50_DISP_MTHD_HEAD);
	u32 blanke, blanks, total;

	if (size < sizeof(*args) || head >= priv->head.nr)
		return -EINVAL;
	blanke = nv_rd32(priv, 0x610aec + (head * 0x540));
	blanks = nv_rd32(priv, 0x610af4 + (head * 0x540));
	total  = nv_rd32(priv, 0x610afc + (head * 0x540));

	args->vblanke = (blanke & 0xffff0000) >> 16;
	args->hblanke = (blanke & 0x0000ffff);
	args->vblanks = (blanks & 0xffff0000) >> 16;
	args->hblanks = (blanks & 0x0000ffff);
	args->vtotal  = ( total & 0xffff0000) >> 16;
	args->htotal  = ( total & 0x0000ffff);

	args->time[0] = ktime_to_ns(ktime_get());
	args->vline   = nv_rd32(priv, 0x616340 + (head * 0x800)) & 0xffff;
	args->time[1] = ktime_to_ns(ktime_get()); /* vline read locks hline */
	args->hline   = nv_rd32(priv, 0x616344 + (head * 0x800)) & 0xffff;
	return 0;
}

static void
nv50_disp_base_vblank_enable(struct nouveau_event *event, int head)
{
	nv_mask(event->priv, 0x61002c, (4 << head), (4 << head));
}

static void
nv50_disp_base_vblank_disable(struct nouveau_event *event, int head)
{
	nv_mask(event->priv, 0x61002c, (4 << head), 0);
}

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

	priv->base.vblank->priv = priv;
	priv->base.vblank->enable = nv50_disp_base_vblank_enable;
	priv->base.vblank->disable = nv50_disp_base_vblank_disable;
	return nouveau_ramht_new(nv_object(base), nv_object(base), 0x1000, 0,
				&base->ramht);
}

static void
nv50_disp_base_dtor(struct nouveau_object *object)
{
	struct nv50_disp_base *base = (void *)object;
	nouveau_ramht_ref(NULL, &base->ramht);
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

	/* ... PIOR caps */
	for (i = 0; i < priv->pior.nr; i++) {
		tmp = nv_rd32(priv, 0x61e000 + (i * 0x800));
		nv_wr32(priv, 0x6101f0 + (i * 0x04), tmp);
	}

	/* steal display away from vbios, or something like that */
	if (nv_rd32(priv, 0x610024) & 0x00000100) {
		nv_wr32(priv, 0x610024, 0x00000100);
		nv_mask(priv, 0x6194e8, 0x00000001, 0x00000000);
		if (!nv_wait(priv, 0x6194e8, 0x00000002, 0x00000000)) {
			nv_error(priv, "timeout acquiring display\n");
			return -EBUSY;
		}
	}

	/* point at display engine memory area (hash table, objects) */
	nv_wr32(priv, 0x610010, (nv_gpuobj(base->ramht)->addr >> 8) | 9);

	/* enable supervisor interrupts, disable everything else */
	nv_wr32(priv, 0x61002c, 0x00000370);
	nv_wr32(priv, 0x610028, 0x00000000);
	return 0;
}

static int
nv50_disp_base_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_base *base = (void *)object;

	/* disable all interrupts */
	nv_wr32(priv, 0x610024, 0x00000000);
	nv_wr32(priv, 0x610020, 0x00000000);

	return nouveau_parent_fini(&base->base, suspend);
}

struct nouveau_ofuncs
nv50_disp_base_ofuncs = {
	.ctor = nv50_disp_base_ctor,
	.dtor = nv50_disp_base_dtor,
	.init = nv50_disp_base_init,
	.fini = nv50_disp_base_fini,
};

static struct nouveau_omthds
nv50_disp_base_omthds[] = {
	{ HEAD_MTHD(NV50_DISP_SCANOUTPOS)     , nv50_disp_base_scanoutpos },
	{ SOR_MTHD(NV50_DISP_SOR_PWR)         , nv50_sor_mthd },
	{ SOR_MTHD(NV50_DISP_SOR_LVDS_SCRIPT) , nv50_sor_mthd },
	{ DAC_MTHD(NV50_DISP_DAC_PWR)         , nv50_dac_mthd },
	{ DAC_MTHD(NV50_DISP_DAC_LOAD)        , nv50_dac_mthd },
	{ PIOR_MTHD(NV50_DISP_PIOR_PWR)       , nv50_pior_mthd },
	{ PIOR_MTHD(NV50_DISP_PIOR_TMDS_PWR)  , nv50_pior_mthd },
	{ PIOR_MTHD(NV50_DISP_PIOR_DP_PWR)    , nv50_pior_mthd },
	{},
};

static struct nouveau_oclass
nv50_disp_base_oclass[] = {
	{ NV50_DISP_CLASS, &nv50_disp_base_ofuncs, nv50_disp_base_omthds },
	{}
};

static struct nouveau_oclass
nv50_disp_sclass[] = {
	{ NV50_DISP_MAST_CLASS, &nv50_disp_mast_ofuncs },
	{ NV50_DISP_SYNC_CLASS, &nv50_disp_sync_ofuncs },
	{ NV50_DISP_OVLY_CLASS, &nv50_disp_ovly_ofuncs },
	{ NV50_DISP_OIMM_CLASS, &nv50_disp_oimm_ofuncs },
	{ NV50_DISP_CURS_CLASS, &nv50_disp_curs_ofuncs },
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
	struct nv50_disp_priv *priv = (void *)engine;
	struct nouveau_engctx *ectx;
	int ret = -EBUSY;

	/* no context needed for channel objects... */
	if (nv_mclass(parent) != NV_DEVICE_CLASS) {
		atomic_inc(&parent->refcount);
		*pobject = parent;
		return 1;
	}

	/* allocate display hardware to client */
	mutex_lock(&nv_subdev(priv)->mutex);
	if (list_empty(&nv_engine(priv)->contexts)) {
		ret = nouveau_engctx_create(parent, engine, oclass, NULL,
					    0x10000, 0x10000,
					    NVOBJ_FLAG_HEAP, &ectx);
		*pobject = nv_object(ectx);
	}
	mutex_unlock(&nv_subdev(priv)->mutex);
	return ret;
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
nv50_disp_intr_error(struct nv50_disp_priv *priv)
{
	u32 channels = (nv_rd32(priv, 0x610020) & 0x001f0000) >> 16;
	u32 addr, data;
	int chid;

	for (chid = 0; chid < 5; chid++) {
		if (!(channels & (1 << chid)))
			continue;

		nv_wr32(priv, 0x610020, 0x00010000 << chid);
		addr = nv_rd32(priv, 0x610080 + (chid * 0x08));
		data = nv_rd32(priv, 0x610084 + (chid * 0x08));
		nv_wr32(priv, 0x610080 + (chid * 0x08), 0x90000000);

		nv_error(priv, "chid %d mthd 0x%04x data 0x%08x 0x%08x\n",
			 chid, addr & 0xffc, data, addr);
	}
}

static u16
exec_lookup(struct nv50_disp_priv *priv, int head, int outp, u32 ctrl,
	    struct dcb_output *dcb, u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
	    struct nvbios_outp *info)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	u16 mask, type, data;

	if (outp < 4) {
		type = DCB_OUTPUT_ANALOG;
		mask = 0;
	} else
	if (outp < 8) {
		switch (ctrl & 0x00000f00) {
		case 0x00000000: type = DCB_OUTPUT_LVDS; mask = 1; break;
		case 0x00000100: type = DCB_OUTPUT_TMDS; mask = 1; break;
		case 0x00000200: type = DCB_OUTPUT_TMDS; mask = 2; break;
		case 0x00000500: type = DCB_OUTPUT_TMDS; mask = 3; break;
		case 0x00000800: type = DCB_OUTPUT_DP; mask = 1; break;
		case 0x00000900: type = DCB_OUTPUT_DP; mask = 2; break;
		default:
			nv_error(priv, "unknown SOR mc 0x%08x\n", ctrl);
			return 0x0000;
		}
		outp -= 4;
	} else {
		outp = outp - 8;
		type = 0x0010;
		mask = 0;
		switch (ctrl & 0x00000f00) {
		case 0x00000000: type |= priv->pior.type[outp]; break;
		default:
			nv_error(priv, "unknown PIOR mc 0x%08x\n", ctrl);
			return 0x0000;
		}
	}

	mask  = 0x00c0 & (mask << 6);
	mask |= 0x0001 << outp;
	mask |= 0x0100 << head;

	data = dcb_outp_match(bios, type, mask, ver, hdr, dcb);
	if (!data)
		return 0x0000;

	/* off-chip encoders require matching the exact encoder type */
	if (dcb->location != 0)
		type |= dcb->extdev << 8;

	return nvbios_outp_match(bios, type, mask, ver, hdr, cnt, len, info);
}

static bool
exec_script(struct nv50_disp_priv *priv, int head, int id)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_outp info;
	struct dcb_output dcb;
	u8  ver, hdr, cnt, len;
	u16 data;
	u32 ctrl = 0x00000000;
	u32 reg;
	int i;

	/* DAC */
	for (i = 0; !(ctrl & (1 << head)) && i < priv->dac.nr; i++)
		ctrl = nv_rd32(priv, 0x610b5c + (i * 8));

	/* SOR */
	if (!(ctrl & (1 << head))) {
		if (nv_device(priv)->chipset  < 0x90 ||
		    nv_device(priv)->chipset == 0x92 ||
		    nv_device(priv)->chipset == 0xa0) {
			reg = 0x610b74;
		} else {
			reg = 0x610798;
		}
		for (i = 0; !(ctrl & (1 << head)) && i < priv->sor.nr; i++)
			ctrl = nv_rd32(priv, reg + (i * 8));
		i += 4;
	}

	/* PIOR */
	if (!(ctrl & (1 << head))) {
		for (i = 0; !(ctrl & (1 << head)) && i < priv->pior.nr; i++)
			ctrl = nv_rd32(priv, 0x610b84 + (i * 8));
		i += 8;
	}

	if (!(ctrl & (1 << head)))
		return false;
	i--;

	data = exec_lookup(priv, head, i, ctrl, &dcb, &ver, &hdr, &cnt, &len, &info);
	if (data) {
		struct nvbios_init init = {
			.subdev = nv_subdev(priv),
			.bios = bios,
			.offset = info.script[id],
			.outp = &dcb,
			.crtc = head,
			.execute = 1,
		};

		return nvbios_exec(&init) == 0;
	}

	return false;
}

static u32
exec_clkcmp(struct nv50_disp_priv *priv, int head, int id, u32 pclk,
	    struct dcb_output *outp)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_outp info1;
	struct nvbios_ocfg info2;
	u8  ver, hdr, cnt, len;
	u32 ctrl = 0x00000000;
	u32 data, conf = ~0;
	u32 reg;
	int i;

	/* DAC */
	for (i = 0; !(ctrl & (1 << head)) && i < priv->dac.nr; i++)
		ctrl = nv_rd32(priv, 0x610b58 + (i * 8));

	/* SOR */
	if (!(ctrl & (1 << head))) {
		if (nv_device(priv)->chipset  < 0x90 ||
		    nv_device(priv)->chipset == 0x92 ||
		    nv_device(priv)->chipset == 0xa0) {
			reg = 0x610b70;
		} else {
			reg = 0x610794;
		}
		for (i = 0; !(ctrl & (1 << head)) && i < priv->sor.nr; i++)
			ctrl = nv_rd32(priv, reg + (i * 8));
		i += 4;
	}

	/* PIOR */
	if (!(ctrl & (1 << head))) {
		for (i = 0; !(ctrl & (1 << head)) && i < priv->pior.nr; i++)
			ctrl = nv_rd32(priv, 0x610b80 + (i * 8));
		i += 8;
	}

	if (!(ctrl & (1 << head)))
		return conf;
	i--;

	data = exec_lookup(priv, head, i, ctrl, outp, &ver, &hdr, &cnt, &len, &info1);
	if (!data)
		return conf;

	if (outp->location == 0) {
		switch (outp->type) {
		case DCB_OUTPUT_TMDS:
			conf = (ctrl & 0x00000f00) >> 8;
			if (pclk >= 165000)
				conf |= 0x0100;
			break;
		case DCB_OUTPUT_LVDS:
			conf = priv->sor.lvdsconf;
			break;
		case DCB_OUTPUT_DP:
			conf = (ctrl & 0x00000f00) >> 8;
			break;
		case DCB_OUTPUT_ANALOG:
		default:
			conf = 0x00ff;
			break;
		}
	} else {
		conf = (ctrl & 0x00000f00) >> 8;
		pclk = pclk / 2;
	}

	data = nvbios_ocfg_match(bios, data, conf, &ver, &hdr, &cnt, &len, &info2);
	if (data && id < 0xff) {
		data = nvbios_oclk_match(bios, info2.clkcmp[id], pclk);
		if (data) {
			struct nvbios_init init = {
				.subdev = nv_subdev(priv),
				.bios = bios,
				.offset = data,
				.outp = outp,
				.crtc = head,
				.execute = 1,
			};

			nvbios_exec(&init);
		}
	}

	return conf;
}

static void
nv50_disp_intr_unk10_0(struct nv50_disp_priv *priv, int head)
{
	exec_script(priv, head, 1);
}

static void
nv50_disp_intr_unk20_0(struct nv50_disp_priv *priv, int head)
{
	exec_script(priv, head, 2);
}

static void
nv50_disp_intr_unk20_1(struct nv50_disp_priv *priv, int head)
{
	struct nouveau_devinit *devinit = nouveau_devinit(priv);
	u32 pclk = nv_rd32(priv, 0x610ad0 + (head * 0x540)) & 0x3fffff;
	if (pclk)
		devinit->pll_set(devinit, PLL_VPLL0 + head, pclk);
}

static void
nv50_disp_intr_unk20_2_dp(struct nv50_disp_priv *priv,
			  struct dcb_output *outp, u32 pclk)
{
	const int link = !(outp->sorconf.link & 1);
	const int   or = ffs(outp->or) - 1;
	const u32 soff = (  or * 0x800);
	const u32 loff = (link * 0x080) + soff;
	const u32 ctrl = nv_rd32(priv, 0x610794 + (or * 8));
	const u32 symbol = 100000;
	u32 dpctrl = nv_rd32(priv, 0x61c10c + loff) & 0x0000f0000;
	u32 clksor = nv_rd32(priv, 0x614300 + soff);
	int bestTU = 0, bestVTUi = 0, bestVTUf = 0, bestVTUa = 0;
	int TU, VTUi, VTUf, VTUa;
	u64 link_data_rate, link_ratio, unk;
	u32 best_diff = 64 * symbol;
	u32 link_nr, link_bw, bits, r;

	/* calculate packed data rate for each lane */
	if      (dpctrl > 0x00030000) link_nr = 4;
	else if (dpctrl > 0x00010000) link_nr = 2;
	else			      link_nr = 1;

	if (clksor & 0x000c0000)
		link_bw = 270000;
	else
		link_bw = 162000;

	if      ((ctrl & 0xf0000) == 0x60000) bits = 30;
	else if ((ctrl & 0xf0000) == 0x50000) bits = 24;
	else                                  bits = 18;

	link_data_rate = (pclk * bits / 8) / link_nr;

	/* calculate ratio of packed data rate to link symbol rate */
	link_ratio = link_data_rate * symbol;
	r = do_div(link_ratio, link_bw);

	for (TU = 64; TU >= 32; TU--) {
		/* calculate average number of valid symbols in each TU */
		u32 tu_valid = link_ratio * TU;
		u32 calc, diff;

		/* find a hw representation for the fraction.. */
		VTUi = tu_valid / symbol;
		calc = VTUi * symbol;
		diff = tu_valid - calc;
		if (diff) {
			if (diff >= (symbol / 2)) {
				VTUf = symbol / (symbol - diff);
				if (symbol - (VTUf * diff))
					VTUf++;

				if (VTUf <= 15) {
					VTUa  = 1;
					calc += symbol - (symbol / VTUf);
				} else {
					VTUa  = 0;
					VTUf  = 1;
					calc += symbol;
				}
			} else {
				VTUa  = 0;
				VTUf  = min((int)(symbol / diff), 15);
				calc += symbol / VTUf;
			}

			diff = calc - tu_valid;
		} else {
			/* no remainder, but the hw doesn't like the fractional
			 * part to be zero.  decrement the integer part and
			 * have the fraction add a whole symbol back
			 */
			VTUa = 0;
			VTUf = 1;
			VTUi--;
		}

		if (diff < best_diff) {
			best_diff = diff;
			bestTU = TU;
			bestVTUa = VTUa;
			bestVTUf = VTUf;
			bestVTUi = VTUi;
			if (diff == 0)
				break;
		}
	}

	if (!bestTU) {
		nv_error(priv, "unable to find suitable dp config\n");
		return;
	}

	/* XXX close to vbios numbers, but not right */
	unk  = (symbol - link_ratio) * bestTU;
	unk *= link_ratio;
	r = do_div(unk, symbol);
	r = do_div(unk, symbol);
	unk += 6;

	nv_mask(priv, 0x61c10c + loff, 0x000001fc, bestTU << 2);
	nv_mask(priv, 0x61c128 + loff, 0x010f7f3f, bestVTUa << 24 |
						   bestVTUf << 16 |
						   bestVTUi << 8 | unk);
}

static void
nv50_disp_intr_unk20_2(struct nv50_disp_priv *priv, int head)
{
	struct dcb_output outp;
	u32 pclk = nv_rd32(priv, 0x610ad0 + (head * 0x540)) & 0x3fffff;
	u32 hval, hreg = 0x614200 + (head * 0x800);
	u32 oval, oreg;
	u32 mask;
	u32 conf = exec_clkcmp(priv, head, 0xff, pclk, &outp);
	if (conf != ~0) {
		if (outp.location == 0 && outp.type == DCB_OUTPUT_DP) {
			u32 soff = (ffs(outp.or) - 1) * 0x08;
			u32 ctrl = nv_rd32(priv, 0x610798 + soff);
			u32 datarate;

			switch ((ctrl & 0x000f0000) >> 16) {
			case 6: datarate = pclk * 30 / 8; break;
			case 5: datarate = pclk * 24 / 8; break;
			case 2:
			default:
				datarate = pclk * 18 / 8;
				break;
			}

			nouveau_dp_train(&priv->base, priv->sor.dp,
					 &outp, head, datarate);
		}

		exec_clkcmp(priv, head, 0, pclk, &outp);

		if (!outp.location && outp.type == DCB_OUTPUT_ANALOG) {
			oreg = 0x614280 + (ffs(outp.or) - 1) * 0x800;
			oval = 0x00000000;
			hval = 0x00000000;
			mask = 0xffffffff;
		} else
		if (!outp.location) {
			if (outp.type == DCB_OUTPUT_DP)
				nv50_disp_intr_unk20_2_dp(priv, &outp, pclk);
			oreg = 0x614300 + (ffs(outp.or) - 1) * 0x800;
			oval = (conf & 0x0100) ? 0x00000101 : 0x00000000;
			hval = 0x00000000;
			mask = 0x00000707;
		} else {
			oreg = 0x614380 + (ffs(outp.or) - 1) * 0x800;
			oval = 0x00000001;
			hval = 0x00000001;
			mask = 0x00000707;
		}

		nv_mask(priv, hreg, 0x0000000f, hval);
		nv_mask(priv, oreg, mask, oval);
	}
}

/* If programming a TMDS output on a SOR that can also be configured for
 * DisplayPort, make sure NV50_SOR_DP_CTRL_ENABLE is forced off.
 *
 * It looks like the VBIOS TMDS scripts make an attempt at this, however,
 * the VBIOS scripts on at least one board I have only switch it off on
 * link 0, causing a blank display if the output has previously been
 * programmed for DisplayPort.
 */
static void
nv50_disp_intr_unk40_0_tmds(struct nv50_disp_priv *priv, struct dcb_output *outp)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	const int link = !(outp->sorconf.link & 1);
	const int   or = ffs(outp->or) - 1;
	const u32 loff = (or * 0x800) + (link * 0x80);
	const u16 mask = (outp->sorconf.link << 6) | outp->or;
	u8  ver, hdr;

	if (dcb_outp_match(bios, DCB_OUTPUT_DP, mask, &ver, &hdr, outp))
		nv_mask(priv, 0x61c10c + loff, 0x00000001, 0x00000000);
}

static void
nv50_disp_intr_unk40_0(struct nv50_disp_priv *priv, int head)
{
	struct dcb_output outp;
	u32 pclk = nv_rd32(priv, 0x610ad0 + (head * 0x540)) & 0x3fffff;
	if (exec_clkcmp(priv, head, 1, pclk, &outp) != ~0) {
		if (outp.location == 0 && outp.type == DCB_OUTPUT_TMDS)
			nv50_disp_intr_unk40_0_tmds(priv, &outp);
		else
		if (outp.location == 1 && outp.type == DCB_OUTPUT_DP) {
			u32 soff = (ffs(outp.or) - 1) * 0x08;
			u32 ctrl = nv_rd32(priv, 0x610b84 + soff);
			u32 datarate;

			switch ((ctrl & 0x000f0000) >> 16) {
			case 6: datarate = pclk * 30 / 8; break;
			case 5: datarate = pclk * 24 / 8; break;
			case 2:
			default:
				datarate = pclk * 18 / 8;
				break;
			}

			nouveau_dp_train(&priv->base, priv->pior.dp,
					 &outp, head, datarate);
		}
	}
}

void
nv50_disp_intr_supervisor(struct work_struct *work)
{
	struct nv50_disp_priv *priv =
		container_of(work, struct nv50_disp_priv, supervisor);
	u32 super = nv_rd32(priv, 0x610030);
	int head;

	nv_debug(priv, "supervisor 0x%08x 0x%08x\n", priv->super, super);

	if (priv->super & 0x00000010) {
		for (head = 0; head < priv->head.nr; head++) {
			if (!(super & (0x00000020 << head)))
				continue;
			if (!(super & (0x00000080 << head)))
				continue;
			nv50_disp_intr_unk10_0(priv, head);
		}
	} else
	if (priv->super & 0x00000020) {
		for (head = 0; head < priv->head.nr; head++) {
			if (!(super & (0x00000080 << head)))
				continue;
			nv50_disp_intr_unk20_0(priv, head);
		}
		for (head = 0; head < priv->head.nr; head++) {
			if (!(super & (0x00000200 << head)))
				continue;
			nv50_disp_intr_unk20_1(priv, head);
		}
		for (head = 0; head < priv->head.nr; head++) {
			if (!(super & (0x00000080 << head)))
				continue;
			nv50_disp_intr_unk20_2(priv, head);
		}
	} else
	if (priv->super & 0x00000040) {
		for (head = 0; head < priv->head.nr; head++) {
			if (!(super & (0x00000080 << head)))
				continue;
			nv50_disp_intr_unk40_0(priv, head);
		}
	}

	nv_wr32(priv, 0x610030, 0x80000000);
}

void
nv50_disp_intr(struct nouveau_subdev *subdev)
{
	struct nv50_disp_priv *priv = (void *)subdev;
	u32 intr0 = nv_rd32(priv, 0x610020);
	u32 intr1 = nv_rd32(priv, 0x610024);

	if (intr0 & 0x001f0000) {
		nv50_disp_intr_error(priv);
		intr0 &= ~0x001f0000;
	}

	if (intr1 & 0x00000004) {
		nouveau_event_trigger(priv->base.vblank, 0);
		nv_wr32(priv, 0x610024, 0x00000004);
		intr1 &= ~0x00000004;
	}

	if (intr1 & 0x00000008) {
		nouveau_event_trigger(priv->base.vblank, 1);
		nv_wr32(priv, 0x610024, 0x00000008);
		intr1 &= ~0x00000008;
	}

	if (intr1 & 0x00000070) {
		priv->super = (intr1 & 0x00000070);
		schedule_work(&priv->supervisor);
		nv_wr32(priv, 0x610024, priv->super);
		intr1 &= ~0x00000070;
	}
}

static int
nv50_disp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv50_disp_priv *priv;
	int ret;

	ret = nouveau_disp_create(parent, engine, oclass, 2, "PDISP",
				  "display", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_engine(priv)->sclass = nv50_disp_base_oclass;
	nv_engine(priv)->cclass = &nv50_disp_cclass;
	nv_subdev(priv)->intr = nv50_disp_intr;
	INIT_WORK(&priv->supervisor, nv50_disp_intr_supervisor);
	priv->sclass = nv50_disp_sclass;
	priv->head.nr = 2;
	priv->dac.nr = 3;
	priv->sor.nr = 2;
	priv->pior.nr = 3;
	priv->dac.power = nv50_dac_power;
	priv->dac.sense = nv50_dac_sense;
	priv->sor.power = nv50_sor_power;
	priv->pior.power = nv50_pior_power;
	priv->pior.dp = &nv50_pior_dp_func;
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
