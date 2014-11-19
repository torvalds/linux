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
#include <core/client.h>
#include <core/parent.h>
#include <core/handle.h>
#include <core/enum.h>
#include <nvif/unpack.h>
#include <nvif/class.h>

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

static int
nv50_disp_chan_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, int head,
		       int length, void **pobject)
{
	const struct nv50_disp_chan_impl *impl = (void *)oclass->ofuncs;
	struct nv50_disp_base *base = (void *)parent;
	struct nv50_disp_chan *chan;
	int chid = impl->chid + head;
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

	nv_parent(chan)->object_attach = impl->attach;
	nv_parent(chan)->object_detach = impl->detach;
	return 0;
}

static void
nv50_disp_chan_destroy(struct nv50_disp_chan *chan)
{
	struct nv50_disp_base *base = (void *)nv_object(chan)->parent;
	base->chan &= ~(1 << chan->chid);
	nouveau_namedb_destroy(&chan->base);
}

int
nv50_disp_chan_map(struct nouveau_object *object, u64 *addr, u32 *size)
{
	struct nv50_disp_chan *chan = (void *)object;
	*addr = nv_device_resource_start(nv_device(object), 0) +
		0x640000 + (chan->chid * 0x1000);
	*size = 0x001000;
	return 0;
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

static int
nv50_disp_dmac_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, u32 pushbuf, int head,
		       int length, void **pobject)
{
	struct nv50_disp_dmac *dmac;
	int ret;

	ret = nv50_disp_chan_create_(parent, engine, oclass, head,
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

static void
nv50_disp_mthd_list(struct nv50_disp_priv *priv, int debug, u32 base, int c,
		    const struct nv50_disp_mthd_list *list, int inst)
{
	struct nouveau_object *disp = nv_object(priv);
	int i;

	for (i = 0; list->data[i].mthd; i++) {
		if (list->data[i].addr) {
			u32 next = nv_rd32(priv, list->data[i].addr + base + 0);
			u32 prev = nv_rd32(priv, list->data[i].addr + base + c);
			u32 mthd = list->data[i].mthd + (list->mthd * inst);
			const char *name = list->data[i].name;
			char mods[16];

			if (prev != next)
				snprintf(mods, sizeof(mods), "-> 0x%08x", next);
			else
				snprintf(mods, sizeof(mods), "%13c", ' ');

			nv_printk_(disp, debug, "\t0x%04x: 0x%08x %s%s%s\n",
				   mthd, prev, mods, name ? " // " : "",
				   name ? name : "");
		}
	}
}

void
nv50_disp_mthd_chan(struct nv50_disp_priv *priv, int debug, int head,
		    const struct nv50_disp_mthd_chan *chan)
{
	struct nouveau_object *disp = nv_object(priv);
	const struct nv50_disp_impl *impl = (void *)disp->oclass;
	const struct nv50_disp_mthd_list *list;
	int i, j;

	if (debug > nv_subdev(priv)->debug)
		return;

	for (i = 0; (list = chan->data[i].mthd) != NULL; i++) {
		u32 base = head * chan->addr;
		for (j = 0; j < chan->data[i].nr; j++, base += list->addr) {
			const char *cname = chan->name;
			const char *sname = "";
			char cname_[16], sname_[16];

			if (chan->addr) {
				snprintf(cname_, sizeof(cname_), "%s %d",
					 chan->name, head);
				cname = cname_;
			}

			if (chan->data[i].nr > 1) {
				snprintf(sname_, sizeof(sname_), " - %s %d",
					 chan->data[i].name, j);
				sname = sname_;
			}

			nv_printk_(disp, debug, "%s%s:\n", cname, sname);
			nv50_disp_mthd_list(priv, debug, base, impl->mthd.prev,
					    list, j);
		}
	}
}

const struct nv50_disp_mthd_list
nv50_disp_mast_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x610bb8 },
		{ 0x0088, 0x610b9c },
		{ 0x008c, 0x000000 },
		{}
	}
};

static const struct nv50_disp_mthd_list
nv50_disp_mast_mthd_dac = {
	.mthd = 0x0080,
	.addr = 0x000008,
	.data = {
		{ 0x0400, 0x610b58 },
		{ 0x0404, 0x610bdc },
		{ 0x0420, 0x610828 },
		{}
	}
};

const struct nv50_disp_mthd_list
nv50_disp_mast_mthd_sor = {
	.mthd = 0x0040,
	.addr = 0x000008,
	.data = {
		{ 0x0600, 0x610b70 },
		{}
	}
};

const struct nv50_disp_mthd_list
nv50_disp_mast_mthd_pior = {
	.mthd = 0x0040,
	.addr = 0x000008,
	.data = {
		{ 0x0700, 0x610b80 },
		{}
	}
};

static const struct nv50_disp_mthd_list
nv50_disp_mast_mthd_head = {
	.mthd = 0x0400,
	.addr = 0x000540,
	.data = {
		{ 0x0800, 0x610ad8 },
		{ 0x0804, 0x610ad0 },
		{ 0x0808, 0x610a48 },
		{ 0x080c, 0x610a78 },
		{ 0x0810, 0x610ac0 },
		{ 0x0814, 0x610af8 },
		{ 0x0818, 0x610b00 },
		{ 0x081c, 0x610ae8 },
		{ 0x0820, 0x610af0 },
		{ 0x0824, 0x610b08 },
		{ 0x0828, 0x610b10 },
		{ 0x082c, 0x610a68 },
		{ 0x0830, 0x610a60 },
		{ 0x0834, 0x000000 },
		{ 0x0838, 0x610a40 },
		{ 0x0840, 0x610a24 },
		{ 0x0844, 0x610a2c },
		{ 0x0848, 0x610aa8 },
		{ 0x084c, 0x610ab0 },
		{ 0x0860, 0x610a84 },
		{ 0x0864, 0x610a90 },
		{ 0x0868, 0x610b18 },
		{ 0x086c, 0x610b20 },
		{ 0x0870, 0x610ac8 },
		{ 0x0874, 0x610a38 },
		{ 0x0880, 0x610a58 },
		{ 0x0884, 0x610a9c },
		{ 0x08a0, 0x610a70 },
		{ 0x08a4, 0x610a50 },
		{ 0x08a8, 0x610ae0 },
		{ 0x08c0, 0x610b28 },
		{ 0x08c4, 0x610b30 },
		{ 0x08c8, 0x610b40 },
		{ 0x08d4, 0x610b38 },
		{ 0x08d8, 0x610b48 },
		{ 0x08dc, 0x610b50 },
		{ 0x0900, 0x610a18 },
		{ 0x0904, 0x610ab8 },
		{}
	}
};

static const struct nv50_disp_mthd_chan
nv50_disp_mast_mthd_chan = {
	.name = "Core",
	.addr = 0x000000,
	.data = {
		{ "Global", 1, &nv50_disp_mast_mthd_base },
		{    "DAC", 3, &nv50_disp_mast_mthd_dac  },
		{    "SOR", 2, &nv50_disp_mast_mthd_sor  },
		{   "PIOR", 3, &nv50_disp_mast_mthd_pior },
		{   "HEAD", 2, &nv50_disp_mast_mthd_head },
		{}
	}
};

int
nv50_disp_mast_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	union {
		struct nv50_disp_core_channel_dma_v0 v0;
	} *args = data;
	struct nv50_disp_dmac *mast;
	int ret;

	nv_ioctl(parent, "create disp core channel dma size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create disp core channel dma vers %d "
				 "pushbuf %08x\n",
			 args->v0.version, args->v0.pushbuf);
	} else
		return ret;

	ret = nv50_disp_dmac_create_(parent, engine, oclass, args->v0.pushbuf,
				     0, sizeof(*mast), (void **)&mast);
	*pobject = nv_object(mast);
	if (ret)
		return ret;

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

struct nv50_disp_chan_impl
nv50_disp_mast_ofuncs = {
	.base.ctor = nv50_disp_mast_ctor,
	.base.dtor = nv50_disp_dmac_dtor,
	.base.init = nv50_disp_mast_init,
	.base.fini = nv50_disp_mast_fini,
	.base.map  = nv50_disp_chan_map,
	.base.rd32 = nv50_disp_chan_rd32,
	.base.wr32 = nv50_disp_chan_wr32,
	.chid = 0,
	.attach = nv50_disp_dmac_object_attach,
	.detach = nv50_disp_dmac_object_detach,
};

/*******************************************************************************
 * EVO sync channel objects
 ******************************************************************************/

static const struct nv50_disp_mthd_list
nv50_disp_sync_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x0008c4 },
		{ 0x0088, 0x0008d0 },
		{ 0x008c, 0x0008dc },
		{ 0x0090, 0x0008e4 },
		{ 0x0094, 0x610884 },
		{ 0x00a0, 0x6108a0 },
		{ 0x00a4, 0x610878 },
		{ 0x00c0, 0x61086c },
		{ 0x00e0, 0x610858 },
		{ 0x00e4, 0x610860 },
		{ 0x00e8, 0x6108ac },
		{ 0x00ec, 0x6108b4 },
		{ 0x0100, 0x610894 },
		{ 0x0110, 0x6108bc },
		{ 0x0114, 0x61088c },
		{}
	}
};

const struct nv50_disp_mthd_list
nv50_disp_sync_mthd_image = {
	.mthd = 0x0400,
	.addr = 0x000000,
	.data = {
		{ 0x0800, 0x6108f0 },
		{ 0x0804, 0x6108fc },
		{ 0x0808, 0x61090c },
		{ 0x080c, 0x610914 },
		{ 0x0810, 0x610904 },
		{}
	}
};

static const struct nv50_disp_mthd_chan
nv50_disp_sync_mthd_chan = {
	.name = "Base",
	.addr = 0x000540,
	.data = {
		{ "Global", 1, &nv50_disp_sync_mthd_base },
		{  "Image", 2, &nv50_disp_sync_mthd_image },
		{}
	}
};

int
nv50_disp_sync_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	union {
		struct nv50_disp_base_channel_dma_v0 v0;
	} *args = data;
	struct nv50_disp_priv *priv = (void *)engine;
	struct nv50_disp_dmac *dmac;
	int ret;

	nv_ioctl(parent, "create disp base channel dma size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create disp base channel dma vers %d "
				 "pushbuf %08x head %d\n",
			 args->v0.version, args->v0.pushbuf, args->v0.head);
		if (args->v0.head > priv->head.nr)
			return -EINVAL;
	} else
		return ret;

	ret = nv50_disp_dmac_create_(parent, engine, oclass, args->v0.pushbuf,
				     args->v0.head, sizeof(*dmac),
				     (void **)&dmac);
	*pobject = nv_object(dmac);
	if (ret)
		return ret;

	return 0;
}

struct nv50_disp_chan_impl
nv50_disp_sync_ofuncs = {
	.base.ctor = nv50_disp_sync_ctor,
	.base.dtor = nv50_disp_dmac_dtor,
	.base.init = nv50_disp_dmac_init,
	.base.fini = nv50_disp_dmac_fini,
	.base.map  = nv50_disp_chan_map,
	.base.rd32 = nv50_disp_chan_rd32,
	.base.wr32 = nv50_disp_chan_wr32,
	.chid = 1,
	.attach = nv50_disp_dmac_object_attach,
	.detach = nv50_disp_dmac_object_detach,
};

/*******************************************************************************
 * EVO overlay channel objects
 ******************************************************************************/

const struct nv50_disp_mthd_list
nv50_disp_ovly_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x0009a0 },
		{ 0x0088, 0x0009c0 },
		{ 0x008c, 0x0009c8 },
		{ 0x0090, 0x6109b4 },
		{ 0x0094, 0x610970 },
		{ 0x00a0, 0x610998 },
		{ 0x00a4, 0x610964 },
		{ 0x00c0, 0x610958 },
		{ 0x00e0, 0x6109a8 },
		{ 0x00e4, 0x6109d0 },
		{ 0x00e8, 0x6109d8 },
		{ 0x0100, 0x61094c },
		{ 0x0104, 0x610984 },
		{ 0x0108, 0x61098c },
		{ 0x0800, 0x6109f8 },
		{ 0x0808, 0x610a08 },
		{ 0x080c, 0x610a10 },
		{ 0x0810, 0x610a00 },
		{}
	}
};

static const struct nv50_disp_mthd_chan
nv50_disp_ovly_mthd_chan = {
	.name = "Overlay",
	.addr = 0x000540,
	.data = {
		{ "Global", 1, &nv50_disp_ovly_mthd_base },
		{}
	}
};

int
nv50_disp_ovly_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	union {
		struct nv50_disp_overlay_channel_dma_v0 v0;
	} *args = data;
	struct nv50_disp_priv *priv = (void *)engine;
	struct nv50_disp_dmac *dmac;
	int ret;

	nv_ioctl(parent, "create disp overlay channel dma size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create disp overlay channel dma vers %d "
				 "pushbuf %08x head %d\n",
			 args->v0.version, args->v0.pushbuf, args->v0.head);
		if (args->v0.head > priv->head.nr)
			return -EINVAL;
	} else
		return ret;

	ret = nv50_disp_dmac_create_(parent, engine, oclass, args->v0.pushbuf,
				     args->v0.head, sizeof(*dmac),
				     (void **)&dmac);
	*pobject = nv_object(dmac);
	if (ret)
		return ret;

	return 0;
}

struct nv50_disp_chan_impl
nv50_disp_ovly_ofuncs = {
	.base.ctor = nv50_disp_ovly_ctor,
	.base.dtor = nv50_disp_dmac_dtor,
	.base.init = nv50_disp_dmac_init,
	.base.fini = nv50_disp_dmac_fini,
	.base.map  = nv50_disp_chan_map,
	.base.rd32 = nv50_disp_chan_rd32,
	.base.wr32 = nv50_disp_chan_wr32,
	.chid = 3,
	.attach = nv50_disp_dmac_object_attach,
	.detach = nv50_disp_dmac_object_detach,
};

/*******************************************************************************
 * EVO PIO channel base class
 ******************************************************************************/

static int
nv50_disp_pioc_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, int head,
		       int length, void **pobject)
{
	return nv50_disp_chan_create_(parent, engine, oclass, head,
				      length, pobject);
}

void
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

int
nv50_disp_oimm_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	union {
		struct nv50_disp_overlay_v0 v0;
	} *args = data;
	struct nv50_disp_priv *priv = (void *)engine;
	struct nv50_disp_pioc *pioc;
	int ret;

	nv_ioctl(parent, "create disp overlay size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create disp overlay vers %d head %d\n",
			 args->v0.version, args->v0.head);
		if (args->v0.head > priv->head.nr)
			return -EINVAL;
	} else
		return ret;

	ret = nv50_disp_pioc_create_(parent, engine, oclass, args->v0.head,
				     sizeof(*pioc), (void **)&pioc);
	*pobject = nv_object(pioc);
	if (ret)
		return ret;

	return 0;
}

struct nv50_disp_chan_impl
nv50_disp_oimm_ofuncs = {
	.base.ctor = nv50_disp_oimm_ctor,
	.base.dtor = nv50_disp_pioc_dtor,
	.base.init = nv50_disp_pioc_init,
	.base.fini = nv50_disp_pioc_fini,
	.base.map  = nv50_disp_chan_map,
	.base.rd32 = nv50_disp_chan_rd32,
	.base.wr32 = nv50_disp_chan_wr32,
	.chid = 5,
};

/*******************************************************************************
 * EVO cursor channel objects
 ******************************************************************************/

int
nv50_disp_curs_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	union {
		struct nv50_disp_cursor_v0 v0;
	} *args = data;
	struct nv50_disp_priv *priv = (void *)engine;
	struct nv50_disp_pioc *pioc;
	int ret;

	nv_ioctl(parent, "create disp cursor size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create disp cursor vers %d head %d\n",
			 args->v0.version, args->v0.head);
		if (args->v0.head > priv->head.nr)
			return -EINVAL;
	} else
		return ret;

	ret = nv50_disp_pioc_create_(parent, engine, oclass, args->v0.head,
				     sizeof(*pioc), (void **)&pioc);
	*pobject = nv_object(pioc);
	if (ret)
		return ret;

	return 0;
}

struct nv50_disp_chan_impl
nv50_disp_curs_ofuncs = {
	.base.ctor = nv50_disp_curs_ctor,
	.base.dtor = nv50_disp_pioc_dtor,
	.base.init = nv50_disp_pioc_init,
	.base.fini = nv50_disp_pioc_fini,
	.base.map  = nv50_disp_chan_map,
	.base.rd32 = nv50_disp_chan_rd32,
	.base.wr32 = nv50_disp_chan_wr32,
	.chid = 7,
};

/*******************************************************************************
 * Base display object
 ******************************************************************************/

int
nv50_disp_base_scanoutpos(NV50_DISP_MTHD_V0)
{
	const u32 blanke = nv_rd32(priv, 0x610aec + (head * 0x540));
	const u32 blanks = nv_rd32(priv, 0x610af4 + (head * 0x540));
	const u32 total  = nv_rd32(priv, 0x610afc + (head * 0x540));
	union {
		struct nv04_disp_scanoutpos_v0 v0;
	} *args = data;
	int ret;

	nv_ioctl(object, "disp scanoutpos size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "disp scanoutpos vers %d\n", args->v0.version);
		args->v0.vblanke = (blanke & 0xffff0000) >> 16;
		args->v0.hblanke = (blanke & 0x0000ffff);
		args->v0.vblanks = (blanks & 0xffff0000) >> 16;
		args->v0.hblanks = (blanks & 0x0000ffff);
		args->v0.vtotal  = ( total & 0xffff0000) >> 16;
		args->v0.htotal  = ( total & 0x0000ffff);
		args->v0.time[0] = ktime_to_ns(ktime_get());
		args->v0.vline = /* vline read locks hline */
			nv_rd32(priv, 0x616340 + (head * 0x800)) & 0xffff;
		args->v0.time[1] = ktime_to_ns(ktime_get());
		args->v0.hline =
			nv_rd32(priv, 0x616344 + (head * 0x800)) & 0xffff;
	} else
		return ret;

	return 0;
}

int
nv50_disp_base_mthd(struct nouveau_object *object, u32 mthd,
		    void *data, u32 size)
{
	const struct nv50_disp_impl *impl = (void *)nv_oclass(object->engine);
	union {
		struct nv50_disp_mthd_v0 v0;
		struct nv50_disp_mthd_v1 v1;
	} *args = data;
	struct nv50_disp_priv *priv = (void *)object->engine;
	struct nvkm_output *outp = NULL;
	struct nvkm_output *temp;
	u16 type, mask = 0;
	int head, ret;

	if (mthd != NV50_DISP_MTHD)
		return -EINVAL;

	nv_ioctl(object, "disp mthd size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, true)) {
		nv_ioctl(object, "disp mthd vers %d mthd %02x head %d\n",
			 args->v0.version, args->v0.method, args->v0.head);
		mthd = args->v0.method;
		head = args->v0.head;
	} else
	if (nvif_unpack(args->v1, 1, 1, true)) {
		nv_ioctl(object, "disp mthd vers %d mthd %02x "
				 "type %04x mask %04x\n",
			 args->v1.version, args->v1.method,
			 args->v1.hasht, args->v1.hashm);
		mthd = args->v1.method;
		type = args->v1.hasht;
		mask = args->v1.hashm;
		head = ffs((mask >> 8) & 0x0f) - 1;
	} else
		return ret;

	if (head < 0 || head >= priv->head.nr)
		return -ENXIO;

	if (mask) {
		list_for_each_entry(temp, &priv->base.outp, head) {
			if ((temp->info.hasht         == type) &&
			    (temp->info.hashm & mask) == mask) {
				outp = temp;
				break;
			}
		}
		if (outp == NULL)
			return -ENXIO;
	}

	switch (mthd) {
	case NV50_DISP_SCANOUTPOS:
		return impl->head.scanoutpos(object, priv, data, size, head);
	default:
		break;
	}

	switch (mthd * !!outp) {
	case NV50_DISP_MTHD_V1_DAC_PWR:
		return priv->dac.power(object, priv, data, size, head, outp);
	case NV50_DISP_MTHD_V1_DAC_LOAD:
		return priv->dac.sense(object, priv, data, size, head, outp);
	case NV50_DISP_MTHD_V1_SOR_PWR:
		return priv->sor.power(object, priv, data, size, head, outp);
	case NV50_DISP_MTHD_V1_SOR_HDA_ELD:
		if (!priv->sor.hda_eld)
			return -ENODEV;
		return priv->sor.hda_eld(object, priv, data, size, head, outp);
	case NV50_DISP_MTHD_V1_SOR_HDMI_PWR:
		if (!priv->sor.hdmi)
			return -ENODEV;
		return priv->sor.hdmi(object, priv, data, size, head, outp);
	case NV50_DISP_MTHD_V1_SOR_LVDS_SCRIPT: {
		union {
			struct nv50_disp_sor_lvds_script_v0 v0;
		} *args = data;
		nv_ioctl(object, "disp sor lvds script size %d\n", size);
		if (nvif_unpack(args->v0, 0, 0, false)) {
			nv_ioctl(object, "disp sor lvds script "
					 "vers %d name %04x\n",
				 args->v0.version, args->v0.script);
			priv->sor.lvdsconf = args->v0.script;
			return 0;
		} else
			return ret;
	}
		break;
	case NV50_DISP_MTHD_V1_SOR_DP_PWR: {
		struct nvkm_output_dp *outpdp = (void *)outp;
		union {
			struct nv50_disp_sor_dp_pwr_v0 v0;
		} *args = data;
		nv_ioctl(object, "disp sor dp pwr size %d\n", size);
		if (nvif_unpack(args->v0, 0, 0, false)) {
			nv_ioctl(object, "disp sor dp pwr vers %d state %d\n",
				 args->v0.version, args->v0.state);
			if (args->v0.state == 0) {
				nvkm_notify_put(&outpdp->irq);
				((struct nvkm_output_dp_impl *)nv_oclass(outp))
					->lnk_pwr(outpdp, 0);
				atomic_set(&outpdp->lt.done, 0);
				return 0;
			} else
			if (args->v0.state != 0) {
				nvkm_output_dp_train(&outpdp->base, 0, true);
				return 0;
			}
		} else
			return ret;
	}
		break;
	case NV50_DISP_MTHD_V1_PIOR_PWR:
		if (!priv->pior.power)
			return -ENODEV;
		return priv->pior.power(object, priv, data, size, head, outp);
	default:
		break;
	}

	return -EINVAL;
}

int
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

	return nouveau_ramht_new(nv_object(base), nv_object(base), 0x1000, 0,
				&base->ramht);
}

void
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
	.mthd = nv50_disp_base_mthd,
	.ntfy = nouveau_disp_ntfy,
};

static struct nouveau_oclass
nv50_disp_base_oclass[] = {
	{ NV50_DISP, &nv50_disp_base_ofuncs },
	{}
};

static struct nouveau_oclass
nv50_disp_sclass[] = {
	{ NV50_DISP_CORE_CHANNEL_DMA, &nv50_disp_mast_ofuncs.base },
	{ NV50_DISP_BASE_CHANNEL_DMA, &nv50_disp_sync_ofuncs.base },
	{ NV50_DISP_OVERLAY_CHANNEL_DMA, &nv50_disp_ovly_ofuncs.base },
	{ NV50_DISP_OVERLAY, &nv50_disp_oimm_ofuncs.base },
	{ NV50_DISP_CURSOR, &nv50_disp_curs_ofuncs.base },
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
	if (nv_mclass(parent) != NV_DEVICE) {
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
nv50_disp_vblank_fini(struct nvkm_event *event, int type, int head)
{
	struct nouveau_disp *disp = container_of(event, typeof(*disp), vblank);
	nv_mask(disp, 0x61002c, (4 << head), 0);
}

static void
nv50_disp_vblank_init(struct nvkm_event *event, int type, int head)
{
	struct nouveau_disp *disp = container_of(event, typeof(*disp), vblank);
	nv_mask(disp, 0x61002c, (4 << head), (4 << head));
}

const struct nvkm_event_func
nv50_disp_vblank_func = {
	.ctor = nouveau_disp_vblank_ctor,
	.init = nv50_disp_vblank_init,
	.fini = nv50_disp_vblank_fini,
};

static const struct nouveau_enum
nv50_disp_intr_error_type[] = {
	{ 3, "ILLEGAL_MTHD" },
	{ 4, "INVALID_VALUE" },
	{ 5, "INVALID_STATE" },
	{ 7, "INVALID_HANDLE" },
	{}
};

static const struct nouveau_enum
nv50_disp_intr_error_code[] = {
	{ 0x00, "" },
	{}
};

static void
nv50_disp_intr_error(struct nv50_disp_priv *priv, int chid)
{
	struct nv50_disp_impl *impl = (void *)nv_object(priv)->oclass;
	u32 data = nv_rd32(priv, 0x610084 + (chid * 0x08));
	u32 addr = nv_rd32(priv, 0x610080 + (chid * 0x08));
	u32 code = (addr & 0x00ff0000) >> 16;
	u32 type = (addr & 0x00007000) >> 12;
	u32 mthd = (addr & 0x00000ffc);
	const struct nouveau_enum *ec, *et;
	char ecunk[6], etunk[6];

	et = nouveau_enum_find(nv50_disp_intr_error_type, type);
	if (!et)
		snprintf(etunk, sizeof(etunk), "UNK%02X", type);

	ec = nouveau_enum_find(nv50_disp_intr_error_code, code);
	if (!ec)
		snprintf(ecunk, sizeof(ecunk), "UNK%02X", code);

	nv_error(priv, "%s [%s] chid %d mthd 0x%04x data 0x%08x\n",
		 et ? et->name : etunk, ec ? ec->name : ecunk,
		 chid, mthd, data);

	if (chid == 0) {
		switch (mthd) {
		case 0x0080:
			nv50_disp_mthd_chan(priv, NV_DBG_ERROR, chid - 0,
					    impl->mthd.core);
			break;
		default:
			break;
		}
	} else
	if (chid <= 2) {
		switch (mthd) {
		case 0x0080:
			nv50_disp_mthd_chan(priv, NV_DBG_ERROR, chid - 1,
					    impl->mthd.base);
			break;
		default:
			break;
		}
	} else
	if (chid <= 4) {
		switch (mthd) {
		case 0x0080:
			nv50_disp_mthd_chan(priv, NV_DBG_ERROR, chid - 3,
					    impl->mthd.ovly);
			break;
		default:
			break;
		}
	}

	nv_wr32(priv, 0x610020, 0x00010000 << chid);
	nv_wr32(priv, 0x610080 + (chid * 0x08), 0x90000000);
}

static struct nvkm_output *
exec_lookup(struct nv50_disp_priv *priv, int head, int or, u32 ctrl,
	    u32 *data, u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
	    struct nvbios_outp *info)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvkm_output *outp;
	u16 mask, type;

	if (or < 4) {
		type = DCB_OUTPUT_ANALOG;
		mask = 0;
	} else
	if (or < 8) {
		switch (ctrl & 0x00000f00) {
		case 0x00000000: type = DCB_OUTPUT_LVDS; mask = 1; break;
		case 0x00000100: type = DCB_OUTPUT_TMDS; mask = 1; break;
		case 0x00000200: type = DCB_OUTPUT_TMDS; mask = 2; break;
		case 0x00000500: type = DCB_OUTPUT_TMDS; mask = 3; break;
		case 0x00000800: type = DCB_OUTPUT_DP; mask = 1; break;
		case 0x00000900: type = DCB_OUTPUT_DP; mask = 2; break;
		default:
			nv_error(priv, "unknown SOR mc 0x%08x\n", ctrl);
			return NULL;
		}
		or  -= 4;
	} else {
		or   = or - 8;
		type = 0x0010;
		mask = 0;
		switch (ctrl & 0x00000f00) {
		case 0x00000000: type |= priv->pior.type[or]; break;
		default:
			nv_error(priv, "unknown PIOR mc 0x%08x\n", ctrl);
			return NULL;
		}
	}

	mask  = 0x00c0 & (mask << 6);
	mask |= 0x0001 << or;
	mask |= 0x0100 << head;

	list_for_each_entry(outp, &priv->base.outp, head) {
		if ((outp->info.hasht & 0xff) == type &&
		    (outp->info.hashm & mask) == mask) {
			*data = nvbios_outp_match(bios, outp->info.hasht,
							outp->info.hashm,
						  ver, hdr, cnt, len, info);
			if (!*data)
				return NULL;
			return outp;
		}
	}

	return NULL;
}

static struct nvkm_output *
exec_script(struct nv50_disp_priv *priv, int head, int id)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvkm_output *outp;
	struct nvbios_outp info;
	u8  ver, hdr, cnt, len;
	u32 data, ctrl = 0;
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
		return NULL;
	i--;

	outp = exec_lookup(priv, head, i, ctrl, &data, &ver, &hdr, &cnt, &len, &info);
	if (outp) {
		struct nvbios_init init = {
			.subdev = nv_subdev(priv),
			.bios = bios,
			.offset = info.script[id],
			.outp = &outp->info,
			.crtc = head,
			.execute = 1,
		};

		nvbios_exec(&init);
	}

	return outp;
}

static struct nvkm_output *
exec_clkcmp(struct nv50_disp_priv *priv, int head, int id, u32 pclk, u32 *conf)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvkm_output *outp;
	struct nvbios_outp info1;
	struct nvbios_ocfg info2;
	u8  ver, hdr, cnt, len;
	u32 data, ctrl = 0;
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
		return NULL;
	i--;

	outp = exec_lookup(priv, head, i, ctrl, &data, &ver, &hdr, &cnt, &len, &info1);
	if (!outp)
		return NULL;

	if (outp->info.location == 0) {
		switch (outp->info.type) {
		case DCB_OUTPUT_TMDS:
			*conf = (ctrl & 0x00000f00) >> 8;
			if (pclk >= 165000)
				*conf |= 0x0100;
			break;
		case DCB_OUTPUT_LVDS:
			*conf = priv->sor.lvdsconf;
			break;
		case DCB_OUTPUT_DP:
			*conf = (ctrl & 0x00000f00) >> 8;
			break;
		case DCB_OUTPUT_ANALOG:
		default:
			*conf = 0x00ff;
			break;
		}
	} else {
		*conf = (ctrl & 0x00000f00) >> 8;
		pclk = pclk / 2;
	}

	data = nvbios_ocfg_match(bios, data, *conf, &ver, &hdr, &cnt, &len, &info2);
	if (data && id < 0xff) {
		data = nvbios_oclk_match(bios, info2.clkcmp[id], pclk);
		if (data) {
			struct nvbios_init init = {
				.subdev = nv_subdev(priv),
				.bios = bios,
				.offset = data,
				.outp = &outp->info,
				.crtc = head,
				.execute = 1,
			};

			nvbios_exec(&init);
		}
	}

	return outp;
}

static void
nv50_disp_intr_unk10_0(struct nv50_disp_priv *priv, int head)
{
	exec_script(priv, head, 1);
}

static void
nv50_disp_intr_unk20_0(struct nv50_disp_priv *priv, int head)
{
	struct nvkm_output *outp = exec_script(priv, head, 2);

	/* the binary driver does this outside of the supervisor handling
	 * (after the third supervisor from a detach).  we (currently?)
	 * allow both detach/attach to happen in the same set of
	 * supervisor interrupts, so it would make sense to execute this
	 * (full power down?) script after all the detach phases of the
	 * supervisor handling.  like with training if needed from the
	 * second supervisor, nvidia doesn't do this, so who knows if it's
	 * entirely safe, but it does appear to work..
	 *
	 * without this script being run, on some configurations i've
	 * seen, switching from DP to TMDS on a DP connector may result
	 * in a blank screen (SOR_PWR off/on can restore it)
	 */
	if (outp && outp->info.type == DCB_OUTPUT_DP) {
		struct nvkm_output_dp *outpdp = (void *)outp;
		struct nvbios_init init = {
			.subdev = nv_subdev(priv),
			.bios = nouveau_bios(priv),
			.outp = &outp->info,
			.crtc = head,
			.offset = outpdp->info.script[4],
			.execute = 1,
		};

		nvbios_exec(&init);
		atomic_set(&outpdp->lt.done, 0);
	}
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
	u32 link_nr, link_bw, bits;

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
	do_div(link_ratio, link_bw);

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
	do_div(unk, symbol);
	do_div(unk, symbol);
	unk += 6;

	nv_mask(priv, 0x61c10c + loff, 0x000001fc, bestTU << 2);
	nv_mask(priv, 0x61c128 + loff, 0x010f7f3f, bestVTUa << 24 |
						   bestVTUf << 16 |
						   bestVTUi << 8 | unk);
}

static void
nv50_disp_intr_unk20_2(struct nv50_disp_priv *priv, int head)
{
	struct nvkm_output *outp;
	u32 pclk = nv_rd32(priv, 0x610ad0 + (head * 0x540)) & 0x3fffff;
	u32 hval, hreg = 0x614200 + (head * 0x800);
	u32 oval, oreg;
	u32 mask, conf;

	outp = exec_clkcmp(priv, head, 0xff, pclk, &conf);
	if (!outp)
		return;

	/* we allow both encoder attach and detach operations to occur
	 * within a single supervisor (ie. modeset) sequence.  the
	 * encoder detach scripts quite often switch off power to the
	 * lanes, which requires the link to be re-trained.
	 *
	 * this is not generally an issue as the sink "must" (heh)
	 * signal an irq when it's lost sync so the driver can
	 * re-train.
	 *
	 * however, on some boards, if one does not configure at least
	 * the gpu side of the link *before* attaching, then various
	 * things can go horribly wrong (PDISP disappearing from mmio,
	 * third supervisor never happens, etc).
	 *
	 * the solution is simply to retrain here, if necessary.  last
	 * i checked, the binary driver userspace does not appear to
	 * trigger this situation (it forces an UPDATE between steps).
	 */
	if (outp->info.type == DCB_OUTPUT_DP) {
		u32 soff = (ffs(outp->info.or) - 1) * 0x08;
		u32 ctrl, datarate;

		if (outp->info.location == 0) {
			ctrl = nv_rd32(priv, 0x610794 + soff);
			soff = 1;
		} else {
			ctrl = nv_rd32(priv, 0x610b80 + soff);
			soff = 2;
		}

		switch ((ctrl & 0x000f0000) >> 16) {
		case 6: datarate = pclk * 30; break;
		case 5: datarate = pclk * 24; break;
		case 2:
		default:
			datarate = pclk * 18;
			break;
		}

		if (nvkm_output_dp_train(outp, datarate / soff, true))
			ERR("link not trained before attach\n");
	}

	exec_clkcmp(priv, head, 0, pclk, &conf);

	if (!outp->info.location && outp->info.type == DCB_OUTPUT_ANALOG) {
		oreg = 0x614280 + (ffs(outp->info.or) - 1) * 0x800;
		oval = 0x00000000;
		hval = 0x00000000;
		mask = 0xffffffff;
	} else
	if (!outp->info.location) {
		if (outp->info.type == DCB_OUTPUT_DP)
			nv50_disp_intr_unk20_2_dp(priv, &outp->info, pclk);
		oreg = 0x614300 + (ffs(outp->info.or) - 1) * 0x800;
		oval = (conf & 0x0100) ? 0x00000101 : 0x00000000;
		hval = 0x00000000;
		mask = 0x00000707;
	} else {
		oreg = 0x614380 + (ffs(outp->info.or) - 1) * 0x800;
		oval = 0x00000001;
		hval = 0x00000001;
		mask = 0x00000707;
	}

	nv_mask(priv, hreg, 0x0000000f, hval);
	nv_mask(priv, oreg, mask, oval);
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
	struct dcb_output match;
	u8  ver, hdr;

	if (dcb_outp_match(bios, DCB_OUTPUT_DP, mask, &ver, &hdr, &match))
		nv_mask(priv, 0x61c10c + loff, 0x00000001, 0x00000000);
}

static void
nv50_disp_intr_unk40_0(struct nv50_disp_priv *priv, int head)
{
	struct nvkm_output *outp;
	u32 pclk = nv_rd32(priv, 0x610ad0 + (head * 0x540)) & 0x3fffff;
	u32 conf;

	outp = exec_clkcmp(priv, head, 1, pclk, &conf);
	if (!outp)
		return;

	if (outp->info.location == 0 && outp->info.type == DCB_OUTPUT_TMDS)
		nv50_disp_intr_unk40_0_tmds(priv, &outp->info);
}

void
nv50_disp_intr_supervisor(struct work_struct *work)
{
	struct nv50_disp_priv *priv =
		container_of(work, struct nv50_disp_priv, supervisor);
	struct nv50_disp_impl *impl = (void *)nv_object(priv)->oclass;
	u32 super = nv_rd32(priv, 0x610030);
	int head;

	nv_debug(priv, "supervisor 0x%08x 0x%08x\n", priv->super, super);

	if (priv->super & 0x00000010) {
		nv50_disp_mthd_chan(priv, NV_DBG_DEBUG, 0, impl->mthd.core);
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

	while (intr0 & 0x001f0000) {
		u32 chid = __ffs(intr0 & 0x001f0000) - 16;
		nv50_disp_intr_error(priv, chid);
		intr0 &= ~(0x00010000 << chid);
	}

	if (intr1 & 0x00000004) {
		nouveau_disp_vblank(&priv->base, 0);
		nv_wr32(priv, 0x610024, 0x00000004);
		intr1 &= ~0x00000004;
	}

	if (intr1 & 0x00000008) {
		nouveau_disp_vblank(&priv->base, 1);
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
	return 0;
}

struct nouveau_oclass *
nv50_disp_outp_sclass[] = {
	&nv50_pior_dp_impl.base.base,
	NULL
};

struct nouveau_oclass *
nv50_disp_oclass = &(struct nv50_disp_impl) {
	.base.base.handle = NV_ENGINE(DISP, 0x50),
	.base.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_disp_ctor,
		.dtor = _nouveau_disp_dtor,
		.init = _nouveau_disp_init,
		.fini = _nouveau_disp_fini,
	},
	.base.vblank = &nv50_disp_vblank_func,
	.base.outp =  nv50_disp_outp_sclass,
	.mthd.core = &nv50_disp_mast_mthd_chan,
	.mthd.base = &nv50_disp_sync_mthd_chan,
	.mthd.ovly = &nv50_disp_ovly_mthd_chan,
	.mthd.prev = 0x000004,
	.head.scanoutpos = nv50_disp_base_scanoutpos,
}.base.base;
