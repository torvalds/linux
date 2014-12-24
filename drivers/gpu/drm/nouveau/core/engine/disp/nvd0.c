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

#include <subdev/timer.h>
#include <subdev/fb.h>
#include <subdev/clock.h>

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/disp.h>
#include <subdev/bios/init.h>
#include <subdev/bios/pll.h>

#include "nv50.h"

/*******************************************************************************
 * EVO DMA channel base class
 ******************************************************************************/

static int
nvd0_disp_dmac_object_attach(struct nouveau_object *parent,
			     struct nouveau_object *object, u32 name)
{
	struct nv50_disp_base *base = (void *)parent->parent;
	struct nv50_disp_chan *chan = (void *)parent;
	u32 addr = nv_gpuobj(object)->node->offset;
	u32 data = (chan->chid << 27) | (addr << 9) | 0x00000001;
	return nouveau_ramht_insert(base->ramht, chan->chid, name, data);
}

static void
nvd0_disp_dmac_object_detach(struct nouveau_object *parent, int cookie)
{
	struct nv50_disp_base *base = (void *)parent->parent;
	nouveau_ramht_remove(base->ramht, cookie);
}

static int
nvd0_disp_dmac_init(struct nouveau_object *object)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_dmac *dmac = (void *)object;
	int chid = dmac->base.chid;
	int ret;

	ret = nv50_disp_chan_init(&dmac->base);
	if (ret)
		return ret;

	/* enable error reporting */
	nv_mask(priv, 0x610090, 0x00000001 << chid, 0x00000001 << chid);
	nv_mask(priv, 0x6100a0, 0x00000001 << chid, 0x00000001 << chid);

	/* initialise channel for dma command submission */
	nv_wr32(priv, 0x610494 + (chid * 0x0010), dmac->push);
	nv_wr32(priv, 0x610498 + (chid * 0x0010), 0x00010000);
	nv_wr32(priv, 0x61049c + (chid * 0x0010), 0x00000001);
	nv_mask(priv, 0x610490 + (chid * 0x0010), 0x00000010, 0x00000010);
	nv_wr32(priv, 0x640000 + (chid * 0x1000), 0x00000000);
	nv_wr32(priv, 0x610490 + (chid * 0x0010), 0x00000013);

	/* wait for it to go inactive */
	if (!nv_wait(priv, 0x610490 + (chid * 0x10), 0x80000000, 0x00000000)) {
		nv_error(dmac, "init: 0x%08x\n",
			 nv_rd32(priv, 0x610490 + (chid * 0x10)));
		return -EBUSY;
	}

	return 0;
}

static int
nvd0_disp_dmac_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_dmac *dmac = (void *)object;
	int chid = dmac->base.chid;

	/* deactivate channel */
	nv_mask(priv, 0x610490 + (chid * 0x0010), 0x00001010, 0x00001000);
	nv_mask(priv, 0x610490 + (chid * 0x0010), 0x00000003, 0x00000000);
	if (!nv_wait(priv, 0x610490 + (chid * 0x10), 0x001e0000, 0x00000000)) {
		nv_error(dmac, "fini: 0x%08x\n",
			 nv_rd32(priv, 0x610490 + (chid * 0x10)));
		if (suspend)
			return -EBUSY;
	}

	/* disable error reporting */
	nv_mask(priv, 0x610090, 0x00000001 << chid, 0x00000000);
	nv_mask(priv, 0x6100a0, 0x00000001 << chid, 0x00000000);

	return nv50_disp_chan_fini(&dmac->base, suspend);
}

/*******************************************************************************
 * EVO master channel object
 ******************************************************************************/

static int
nvd0_disp_mast_ctor(struct nouveau_object *parent,
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

	nv_parent(mast)->object_attach = nvd0_disp_dmac_object_attach;
	nv_parent(mast)->object_detach = nvd0_disp_dmac_object_detach;
	return 0;
}

static int
nvd0_disp_mast_init(struct nouveau_object *object)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_dmac *mast = (void *)object;
	int ret;

	ret = nv50_disp_chan_init(&mast->base);
	if (ret)
		return ret;

	/* enable error reporting */
	nv_mask(priv, 0x610090, 0x00000001, 0x00000001);
	nv_mask(priv, 0x6100a0, 0x00000001, 0x00000001);

	/* initialise channel for dma command submission */
	nv_wr32(priv, 0x610494, mast->push);
	nv_wr32(priv, 0x610498, 0x00010000);
	nv_wr32(priv, 0x61049c, 0x00000001);
	nv_mask(priv, 0x610490, 0x00000010, 0x00000010);
	nv_wr32(priv, 0x640000, 0x00000000);
	nv_wr32(priv, 0x610490, 0x01000013);

	/* wait for it to go inactive */
	if (!nv_wait(priv, 0x610490, 0x80000000, 0x00000000)) {
		nv_error(mast, "init: 0x%08x\n", nv_rd32(priv, 0x610490));
		return -EBUSY;
	}

	return 0;
}

static int
nvd0_disp_mast_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_dmac *mast = (void *)object;

	/* deactivate channel */
	nv_mask(priv, 0x610490, 0x00000010, 0x00000000);
	nv_mask(priv, 0x610490, 0x00000003, 0x00000000);
	if (!nv_wait(priv, 0x610490, 0x001e0000, 0x00000000)) {
		nv_error(mast, "fini: 0x%08x\n", nv_rd32(priv, 0x610490));
		if (suspend)
			return -EBUSY;
	}

	/* disable error reporting */
	nv_mask(priv, 0x610090, 0x00000001, 0x00000000);
	nv_mask(priv, 0x6100a0, 0x00000001, 0x00000000);

	return nv50_disp_chan_fini(&mast->base, suspend);
}

struct nouveau_ofuncs
nvd0_disp_mast_ofuncs = {
	.ctor = nvd0_disp_mast_ctor,
	.dtor = nv50_disp_dmac_dtor,
	.init = nvd0_disp_mast_init,
	.fini = nvd0_disp_mast_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * EVO sync channel objects
 ******************************************************************************/

static int
nvd0_disp_sync_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv50_display_sync_class *args = data;
	struct nv50_disp_priv *priv = (void *)engine;
	struct nv50_disp_dmac *dmac;
	int ret;

	if (size < sizeof(*args) || args->head >= priv->head.nr)
		return -EINVAL;

	ret = nv50_disp_dmac_create_(parent, engine, oclass, args->pushbuf,
				     1 + args->head, sizeof(*dmac),
				     (void **)&dmac);
	*pobject = nv_object(dmac);
	if (ret)
		return ret;

	nv_parent(dmac)->object_attach = nvd0_disp_dmac_object_attach;
	nv_parent(dmac)->object_detach = nvd0_disp_dmac_object_detach;
	return 0;
}

struct nouveau_ofuncs
nvd0_disp_sync_ofuncs = {
	.ctor = nvd0_disp_sync_ctor,
	.dtor = nv50_disp_dmac_dtor,
	.init = nvd0_disp_dmac_init,
	.fini = nvd0_disp_dmac_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * EVO overlay channel objects
 ******************************************************************************/

static int
nvd0_disp_ovly_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv50_display_ovly_class *args = data;
	struct nv50_disp_priv *priv = (void *)engine;
	struct nv50_disp_dmac *dmac;
	int ret;

	if (size < sizeof(*args) || args->head >= priv->head.nr)
		return -EINVAL;

	ret = nv50_disp_dmac_create_(parent, engine, oclass, args->pushbuf,
				     5 + args->head, sizeof(*dmac),
				     (void **)&dmac);
	*pobject = nv_object(dmac);
	if (ret)
		return ret;

	nv_parent(dmac)->object_attach = nvd0_disp_dmac_object_attach;
	nv_parent(dmac)->object_detach = nvd0_disp_dmac_object_detach;
	return 0;
}

struct nouveau_ofuncs
nvd0_disp_ovly_ofuncs = {
	.ctor = nvd0_disp_ovly_ctor,
	.dtor = nv50_disp_dmac_dtor,
	.init = nvd0_disp_dmac_init,
	.fini = nvd0_disp_dmac_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * EVO PIO channel base class
 ******************************************************************************/

static int
nvd0_disp_pioc_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, int chid,
		       int length, void **pobject)
{
	return nv50_disp_chan_create_(parent, engine, oclass, chid,
				      length, pobject);
}

static void
nvd0_disp_pioc_dtor(struct nouveau_object *object)
{
	struct nv50_disp_pioc *pioc = (void *)object;
	nv50_disp_chan_destroy(&pioc->base);
}

static int
nvd0_disp_pioc_init(struct nouveau_object *object)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_pioc *pioc = (void *)object;
	int chid = pioc->base.chid;
	int ret;

	ret = nv50_disp_chan_init(&pioc->base);
	if (ret)
		return ret;

	/* enable error reporting */
	nv_mask(priv, 0x610090, 0x00000001 << chid, 0x00000001 << chid);
	nv_mask(priv, 0x6100a0, 0x00000001 << chid, 0x00000001 << chid);

	/* activate channel */
	nv_wr32(priv, 0x610490 + (chid * 0x10), 0x00000001);
	if (!nv_wait(priv, 0x610490 + (chid * 0x10), 0x00030000, 0x00010000)) {
		nv_error(pioc, "init: 0x%08x\n",
			 nv_rd32(priv, 0x610490 + (chid * 0x10)));
		return -EBUSY;
	}

	return 0;
}

static int
nvd0_disp_pioc_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_pioc *pioc = (void *)object;
	int chid = pioc->base.chid;

	nv_mask(priv, 0x610490 + (chid * 0x10), 0x00000001, 0x00000000);
	if (!nv_wait(priv, 0x610490 + (chid * 0x10), 0x00030000, 0x00000000)) {
		nv_error(pioc, "timeout: 0x%08x\n",
			 nv_rd32(priv, 0x610490 + (chid * 0x10)));
		if (suspend)
			return -EBUSY;
	}

	/* disable error reporting */
	nv_mask(priv, 0x610090, 0x00000001 << chid, 0x00000000);
	nv_mask(priv, 0x6100a0, 0x00000001 << chid, 0x00000000);

	return nv50_disp_chan_fini(&pioc->base, suspend);
}

/*******************************************************************************
 * EVO immediate overlay channel objects
 ******************************************************************************/

static int
nvd0_disp_oimm_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv50_display_oimm_class *args = data;
	struct nv50_disp_priv *priv = (void *)engine;
	struct nv50_disp_pioc *pioc;
	int ret;

	if (size < sizeof(*args) || args->head >= priv->head.nr)
		return -EINVAL;

	ret = nvd0_disp_pioc_create_(parent, engine, oclass, 9 + args->head,
				     sizeof(*pioc), (void **)&pioc);
	*pobject = nv_object(pioc);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_ofuncs
nvd0_disp_oimm_ofuncs = {
	.ctor = nvd0_disp_oimm_ctor,
	.dtor = nvd0_disp_pioc_dtor,
	.init = nvd0_disp_pioc_init,
	.fini = nvd0_disp_pioc_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * EVO cursor channel objects
 ******************************************************************************/

static int
nvd0_disp_curs_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv50_display_curs_class *args = data;
	struct nv50_disp_priv *priv = (void *)engine;
	struct nv50_disp_pioc *pioc;
	int ret;

	if (size < sizeof(*args) || args->head >= priv->head.nr)
		return -EINVAL;

	ret = nvd0_disp_pioc_create_(parent, engine, oclass, 13 + args->head,
				     sizeof(*pioc), (void **)&pioc);
	*pobject = nv_object(pioc);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_ofuncs
nvd0_disp_curs_ofuncs = {
	.ctor = nvd0_disp_curs_ctor,
	.dtor = nvd0_disp_pioc_dtor,
	.init = nvd0_disp_pioc_init,
	.fini = nvd0_disp_pioc_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
};

/*******************************************************************************
 * Base display object
 ******************************************************************************/

static void
nvd0_disp_base_vblank_enable(struct nouveau_event *event, int head)
{
	nv_mask(event->priv, 0x6100c0 + (head * 0x800), 0x00000001, 0x00000001);
}

static void
nvd0_disp_base_vblank_disable(struct nouveau_event *event, int head)
{
	nv_mask(event->priv, 0x6100c0 + (head * 0x800), 0x00000001, 0x00000000);
}

static int
nvd0_disp_base_ctor(struct nouveau_object *parent,
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
	priv->base.vblank->enable = nvd0_disp_base_vblank_enable;
	priv->base.vblank->disable = nvd0_disp_base_vblank_disable;

	return nouveau_ramht_new(nv_object(base), nv_object(base), 0x1000, 0,
				&base->ramht);
}

static void
nvd0_disp_base_dtor(struct nouveau_object *object)
{
	struct nv50_disp_base *base = (void *)object;
	nouveau_ramht_ref(NULL, &base->ramht);
	nouveau_parent_destroy(&base->base);
}

static int
nvd0_disp_base_init(struct nouveau_object *object)
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
	 * something similar.
	 */

	/* ... CRTC caps */
	for (i = 0; i < priv->head.nr; i++) {
		tmp = nv_rd32(priv, 0x616104 + (i * 0x800));
		nv_wr32(priv, 0x6101b4 + (i * 0x800), tmp);
		tmp = nv_rd32(priv, 0x616108 + (i * 0x800));
		nv_wr32(priv, 0x6101b8 + (i * 0x800), tmp);
		tmp = nv_rd32(priv, 0x61610c + (i * 0x800));
		nv_wr32(priv, 0x6101bc + (i * 0x800), tmp);
	}

	/* ... DAC caps */
	for (i = 0; i < priv->dac.nr; i++) {
		tmp = nv_rd32(priv, 0x61a000 + (i * 0x800));
		nv_wr32(priv, 0x6101c0 + (i * 0x800), tmp);
	}

	/* ... SOR caps */
	for (i = 0; i < priv->sor.nr; i++) {
		tmp = nv_rd32(priv, 0x61c000 + (i * 0x800));
		nv_wr32(priv, 0x6301c4 + (i * 0x800), tmp);
	}

	/* steal display away from vbios, or something like that */
	if (nv_rd32(priv, 0x6100ac) & 0x00000100) {
		nv_wr32(priv, 0x6100ac, 0x00000100);
		nv_mask(priv, 0x6194e8, 0x00000001, 0x00000000);
		if (!nv_wait(priv, 0x6194e8, 0x00000002, 0x00000000)) {
			nv_error(priv, "timeout acquiring display\n");
			return -EBUSY;
		}
	}

	/* point at display engine memory area (hash table, objects) */
	nv_wr32(priv, 0x610010, (nv_gpuobj(object->parent)->addr >> 8) | 9);

	/* enable supervisor interrupts, disable everything else */
	nv_wr32(priv, 0x610090, 0x00000000);
	nv_wr32(priv, 0x6100a0, 0x00000000);
	nv_wr32(priv, 0x6100b0, 0x00000307);

	return 0;
}

static int
nvd0_disp_base_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nv50_disp_base *base = (void *)object;

	/* disable all interrupts */
	nv_wr32(priv, 0x6100b0, 0x00000000);

	return nouveau_parent_fini(&base->base, suspend);
}

struct nouveau_ofuncs
nvd0_disp_base_ofuncs = {
	.ctor = nvd0_disp_base_ctor,
	.dtor = nvd0_disp_base_dtor,
	.init = nvd0_disp_base_init,
	.fini = nvd0_disp_base_fini,
};

static struct nouveau_oclass
nvd0_disp_base_oclass[] = {
	{ NVD0_DISP_CLASS, &nvd0_disp_base_ofuncs, nva3_disp_base_omthds },
	{}
};

static struct nouveau_oclass
nvd0_disp_sclass[] = {
	{ NVD0_DISP_MAST_CLASS, &nvd0_disp_mast_ofuncs },
	{ NVD0_DISP_SYNC_CLASS, &nvd0_disp_sync_ofuncs },
	{ NVD0_DISP_OVLY_CLASS, &nvd0_disp_ovly_ofuncs },
	{ NVD0_DISP_OIMM_CLASS, &nvd0_disp_oimm_ofuncs },
	{ NVD0_DISP_CURS_CLASS, &nvd0_disp_curs_ofuncs },
	{}
};

/*******************************************************************************
 * Display engine implementation
 ******************************************************************************/

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
	} else {
		outp -= 4;
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
		dcb->sorconf.link = mask;
	}

	mask  = 0x00c0 & (mask << 6);
	mask |= 0x0001 << outp;
	mask |= 0x0100 << head;

	data = dcb_outp_match(bios, type, mask, ver, hdr, dcb);
	if (!data)
		return 0x0000;

	return nvbios_outp_match(bios, type, mask, ver, hdr, cnt, len, info);
}

static bool
exec_script(struct nv50_disp_priv *priv, int head, int id)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_outp info;
	struct dcb_output dcb;
	u8  ver, hdr, cnt, len;
	u32 ctrl = 0x00000000;
	u16 data;
	int outp;

	for (outp = 0; !(ctrl & (1 << head)) && outp < 8; outp++) {
		ctrl = nv_rd32(priv, 0x640180 + (outp * 0x20));
		if (ctrl & (1 << head))
			break;
	}

	if (outp == 8)
		return false;

	data = exec_lookup(priv, head, outp, ctrl, &dcb, &ver, &hdr, &cnt, &len, &info);
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
exec_clkcmp(struct nv50_disp_priv *priv, int head, int id,
	    u32 pclk, struct dcb_output *dcb)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_outp info1;
	struct nvbios_ocfg info2;
	u8  ver, hdr, cnt, len;
	u32 ctrl = 0x00000000;
	u32 data, conf = ~0;
	int outp;

	for (outp = 0; !(ctrl & (1 << head)) && outp < 8; outp++) {
		ctrl = nv_rd32(priv, 0x660180 + (outp * 0x20));
		if (ctrl & (1 << head))
			break;
	}

	if (outp == 8)
		return conf;

	data = exec_lookup(priv, head, outp, ctrl, dcb, &ver, &hdr, &cnt, &len, &info1);
	if (data == 0x0000)
		return conf;

	switch (dcb->type) {
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

	data = nvbios_ocfg_match(bios, data, conf, &ver, &hdr, &cnt, &len, &info2);
	if (data && id < 0xff) {
		data = nvbios_oclk_match(bios, info2.clkcmp[id], pclk);
		if (data) {
			struct nvbios_init init = {
				.subdev = nv_subdev(priv),
				.bios = bios,
				.offset = data,
				.outp = dcb,
				.crtc = head,
				.execute = 1,
			};

			nvbios_exec(&init);
		}
	}

	return conf;
}

static void
nvd0_disp_intr_unk1_0(struct nv50_disp_priv *priv, int head)
{
	exec_script(priv, head, 1);
}

static void
nvd0_disp_intr_unk2_0(struct nv50_disp_priv *priv, int head)
{
	exec_script(priv, head, 2);
}

static void
nvd0_disp_intr_unk2_1(struct nv50_disp_priv *priv, int head)
{
	struct nouveau_clock *clk = nouveau_clock(priv);
	u32 pclk = nv_rd32(priv, 0x660450 + (head * 0x300)) / 1000;
	if (pclk)
		clk->pll_set(clk, PLL_VPLL0 + head, pclk);
	nv_wr32(priv, 0x612200 + (head * 0x800), 0x00000000);
}

static void
nvd0_disp_intr_unk2_2_tu(struct nv50_disp_priv *priv, int head,
			 struct dcb_output *outp)
{
	const int or = ffs(outp->or) - 1;
	const u32 ctrl = nv_rd32(priv, 0x660200 + (or   * 0x020));
	const u32 conf = nv_rd32(priv, 0x660404 + (head * 0x300));
	const u32 pclk = nv_rd32(priv, 0x660450 + (head * 0x300)) / 1000;
	const u32 link = ((ctrl & 0xf00) == 0x800) ? 0 : 1;
	const u32 hoff = (head * 0x800);
	const u32 soff = (  or * 0x800);
	const u32 loff = (link * 0x080) + soff;
	const u32 symbol = 100000;
	const u32 TU = 64;
	u32 dpctrl = nv_rd32(priv, 0x61c10c + loff) & 0x000f0000;
	u32 clksor = nv_rd32(priv, 0x612300 + soff);
	u32 datarate, link_nr, link_bw, bits;
	u64 ratio, value;

	if      ((conf & 0x3c0) == 0x180) bits = 30;
	else if ((conf & 0x3c0) == 0x140) bits = 24;
	else                              bits = 18;
	datarate = (pclk * bits) / 8;

	if      (dpctrl > 0x00030000) link_nr = 4;
	else if (dpctrl > 0x00010000) link_nr = 2;
	else			      link_nr = 1;

	link_bw  = (clksor & 0x007c0000) >> 18;
	link_bw *= 27000;

	ratio  = datarate;
	ratio *= symbol;
	do_div(ratio, link_nr * link_bw);

	value  = (symbol - ratio) * TU;
	value *= ratio;
	do_div(value, symbol);
	do_div(value, symbol);

	value += 5;
	value |= 0x08000000;

	nv_wr32(priv, 0x616610 + hoff, value);
}

static void
nvd0_disp_intr_unk2_2(struct nv50_disp_priv *priv, int head)
{
	struct dcb_output outp;
	u32 pclk = nv_rd32(priv, 0x660450 + (head * 0x300)) / 1000;
	u32 conf = exec_clkcmp(priv, head, 0xff, pclk, &outp);
	if (conf != ~0) {
		u32 addr, data;

		if (outp.type == DCB_OUTPUT_DP) {
			u32 sync = nv_rd32(priv, 0x660404 + (head * 0x300));
			switch ((sync & 0x000003c0) >> 6) {
			case 6: pclk = pclk * 30 / 8; break;
			case 5: pclk = pclk * 24 / 8; break;
			case 2:
			default:
				pclk = pclk * 18 / 8;
				break;
			}

			nouveau_dp_train(&priv->base, priv->sor.dp,
					 &outp, head, pclk);
		}

		exec_clkcmp(priv, head, 0, pclk, &outp);

		if (outp.type == DCB_OUTPUT_ANALOG) {
			addr = 0x612280 + (ffs(outp.or) - 1) * 0x800;
			data = 0x00000000;
		} else {
			if (outp.type == DCB_OUTPUT_DP)
				nvd0_disp_intr_unk2_2_tu(priv, head, &outp);
			addr = 0x612300 + (ffs(outp.or) - 1) * 0x800;
			data = (conf & 0x0100) ? 0x00000101 : 0x00000000;
		}

		nv_mask(priv, addr, 0x00000707, data);
	}
}

static void
nvd0_disp_intr_unk4_0(struct nv50_disp_priv *priv, int head)
{
	struct dcb_output outp;
	u32 pclk = nv_rd32(priv, 0x660450 + (head * 0x300)) / 1000;
	exec_clkcmp(priv, head, 1, pclk, &outp);
}

void
nvd0_disp_intr_supervisor(struct work_struct *work)
{
	struct nv50_disp_priv *priv =
		container_of(work, struct nv50_disp_priv, supervisor);
	u32 mask[4];
	int head;

	nv_debug(priv, "supervisor %08x\n", priv->super);
	for (head = 0; head < priv->head.nr; head++) {
		mask[head] = nv_rd32(priv, 0x6101d4 + (head * 0x800));
		nv_debug(priv, "head %d: 0x%08x\n", head, mask[head]);
	}

	if (priv->super & 0x00000001) {
		for (head = 0; head < priv->head.nr; head++) {
			if (!(mask[head] & 0x00001000))
				continue;
			nvd0_disp_intr_unk1_0(priv, head);
		}
	} else
	if (priv->super & 0x00000002) {
		for (head = 0; head < priv->head.nr; head++) {
			if (!(mask[head] & 0x00001000))
				continue;
			nvd0_disp_intr_unk2_0(priv, head);
		}
		for (head = 0; head < priv->head.nr; head++) {
			if (!(mask[head] & 0x00010000))
				continue;
			nvd0_disp_intr_unk2_1(priv, head);
		}
		for (head = 0; head < priv->head.nr; head++) {
			if (!(mask[head] & 0x00001000))
				continue;
			nvd0_disp_intr_unk2_2(priv, head);
		}
	} else
	if (priv->super & 0x00000004) {
		for (head = 0; head < priv->head.nr; head++) {
			if (!(mask[head] & 0x00001000))
				continue;
			nvd0_disp_intr_unk4_0(priv, head);
		}
	}

	for (head = 0; head < priv->head.nr; head++)
		nv_wr32(priv, 0x6101d4 + (head * 0x800), 0x00000000);
	nv_wr32(priv, 0x6101d0, 0x80000000);
}

void
nvd0_disp_intr(struct nouveau_subdev *subdev)
{
	struct nv50_disp_priv *priv = (void *)subdev;
	u32 intr = nv_rd32(priv, 0x610088);
	int i;

	if (intr & 0x00000001) {
		u32 stat = nv_rd32(priv, 0x61008c);
		nv_wr32(priv, 0x61008c, stat);
		intr &= ~0x00000001;
	}

	if (intr & 0x00000002) {
		u32 stat = nv_rd32(priv, 0x61009c);
		int chid = ffs(stat) - 1;
		if (chid >= 0) {
			u32 mthd = nv_rd32(priv, 0x6101f0 + (chid * 12));
			u32 data = nv_rd32(priv, 0x6101f4 + (chid * 12));
			u32 unkn = nv_rd32(priv, 0x6101f8 + (chid * 12));

			nv_error(priv, "chid %d mthd 0x%04x data 0x%08x "
				       "0x%08x 0x%08x\n",
				 chid, (mthd & 0x0000ffc), data, mthd, unkn);
			nv_wr32(priv, 0x61009c, (1 << chid));
			nv_wr32(priv, 0x6101f0 + (chid * 12), 0x90000000);
		}

		intr &= ~0x00000002;
	}

	if (intr & 0x00100000) {
		u32 stat = nv_rd32(priv, 0x6100ac);
		if (stat & 0x00000007) {
			priv->super = (stat & 0x00000007);
			schedule_work(&priv->supervisor);
			nv_wr32(priv, 0x6100ac, priv->super);
			stat &= ~0x00000007;
		}

		if (stat) {
			nv_info(priv, "unknown intr24 0x%08x\n", stat);
			nv_wr32(priv, 0x6100ac, stat);
		}

		intr &= ~0x00100000;
	}

	for (i = 0; i < priv->head.nr; i++) {
		u32 mask = 0x01000000 << i;
		if (mask & intr) {
			u32 stat = nv_rd32(priv, 0x6100bc + (i * 0x800));
			if (stat & 0x00000001)
				nouveau_event_trigger(priv->base.vblank, i);
			nv_mask(priv, 0x6100bc + (i * 0x800), 0, 0);
			nv_rd32(priv, 0x6100c0 + (i * 0x800));
		}
	}
}

static int
nvd0_disp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv50_disp_priv *priv;
	int heads = nv_rd32(parent, 0x022448);
	int ret;

	ret = nouveau_disp_create(parent, engine, oclass, heads,
				  "PDISP", "display", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_engine(priv)->sclass = nvd0_disp_base_oclass;
	nv_engine(priv)->cclass = &nv50_disp_cclass;
	nv_subdev(priv)->intr = nvd0_disp_intr;
	INIT_WORK(&priv->supervisor, nvd0_disp_intr_supervisor);
	priv->sclass = nvd0_disp_sclass;
	priv->head.nr = heads;
	priv->dac.nr = 3;
	priv->sor.nr = 4;
	priv->dac.power = nv50_dac_power;
	priv->dac.sense = nv50_dac_sense;
	priv->sor.power = nv50_sor_power;
	priv->sor.hda_eld = nvd0_hda_eld;
	priv->sor.hdmi = nvd0_hdmi_ctrl;
	priv->sor.dp = &nvd0_sor_dp_func;
	return 0;
}

struct nouveau_oclass
nvd0_disp_oclass = {
	.handle = NV_ENGINE(DISP, 0x90),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvd0_disp_ctor,
		.dtor = _nouveau_disp_dtor,
		.init = _nouveau_disp_init,
		.fini = _nouveau_disp_fini,
	},
};
