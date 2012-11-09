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

#include <engine/software.h>
#include <engine/disp.h>

#include <subdev/timer.h>
#include <subdev/fb.h>
#include <subdev/bar.h>

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

	if (size < sizeof(*data) || args->head > 1)
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

	if (size < sizeof(*data) || args->head > 1)
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

	return nouveau_ramht_new(parent, parent, 0x1000, 0, &base->ramht);
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

	/* ... EXT caps */
	for (i = 0; i < 3; i++) {
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
	{ SOR_MTHD(NV50_DISP_SOR_PWR)         , nv50_sor_mthd },
	{ SOR_MTHD(NV50_DISP_SOR_LVDS_SCRIPT) , nv50_sor_mthd },
	{ DAC_MTHD(NV50_DISP_DAC_PWR)         , nv50_dac_mthd },
	{ DAC_MTHD(NV50_DISP_DAC_LOAD)        , nv50_dac_mthd },
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
		return 0;
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
	priv->dac.power = nv50_dac_power;
	priv->dac.sense = nv50_dac_sense;
	priv->sor.power = nv50_sor_power;

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
