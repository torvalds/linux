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

#include <core/client.h>
#include <core/enum.h>
#include <core/engctx.h>
#include <core/object.h>

#include <subdev/bios.h>

#include "nv50.h"

int
nv50_fb_memtype[0x80] = {
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 0, 0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 0, 0,
	0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 2, 2, 2, 2,
	1, 0, 2, 0, 1, 0, 2, 0, 1, 1, 2, 2, 1, 1, 0, 0
};

bool
nv50_fb_memtype_valid(struct nouveau_fb *pfb, u32 memtype)
{
	return nv50_fb_memtype[(memtype & 0xff00) >> 8] != 0;
}

static const struct nouveau_enum vm_dispatch_subclients[] = {
	{ 0x00000000, "GRCTX", NULL },
	{ 0x00000001, "NOTIFY", NULL },
	{ 0x00000002, "QUERY", NULL },
	{ 0x00000003, "COND", NULL },
	{ 0x00000004, "M2M_IN", NULL },
	{ 0x00000005, "M2M_OUT", NULL },
	{ 0x00000006, "M2M_NOTIFY", NULL },
	{}
};

static const struct nouveau_enum vm_ccache_subclients[] = {
	{ 0x00000000, "CB", NULL },
	{ 0x00000001, "TIC", NULL },
	{ 0x00000002, "TSC", NULL },
	{}
};

static const struct nouveau_enum vm_prop_subclients[] = {
	{ 0x00000000, "RT0", NULL },
	{ 0x00000001, "RT1", NULL },
	{ 0x00000002, "RT2", NULL },
	{ 0x00000003, "RT3", NULL },
	{ 0x00000004, "RT4", NULL },
	{ 0x00000005, "RT5", NULL },
	{ 0x00000006, "RT6", NULL },
	{ 0x00000007, "RT7", NULL },
	{ 0x00000008, "ZETA", NULL },
	{ 0x00000009, "LOCAL", NULL },
	{ 0x0000000a, "GLOBAL", NULL },
	{ 0x0000000b, "STACK", NULL },
	{ 0x0000000c, "DST2D", NULL },
	{}
};

static const struct nouveau_enum vm_pfifo_subclients[] = {
	{ 0x00000000, "PUSHBUF", NULL },
	{ 0x00000001, "SEMAPHORE", NULL },
	{}
};

static const struct nouveau_enum vm_bar_subclients[] = {
	{ 0x00000000, "FB", NULL },
	{ 0x00000001, "IN", NULL },
	{}
};

static const struct nouveau_enum vm_client[] = {
	{ 0x00000000, "STRMOUT", NULL },
	{ 0x00000003, "DISPATCH", vm_dispatch_subclients },
	{ 0x00000004, "PFIFO_WRITE", NULL },
	{ 0x00000005, "CCACHE", vm_ccache_subclients },
	{ 0x00000006, "PPPP", NULL },
	{ 0x00000007, "CLIPID", NULL },
	{ 0x00000008, "PFIFO_READ", NULL },
	{ 0x00000009, "VFETCH", NULL },
	{ 0x0000000a, "TEXTURE", NULL },
	{ 0x0000000b, "PROP", vm_prop_subclients },
	{ 0x0000000c, "PVP", NULL },
	{ 0x0000000d, "PBSP", NULL },
	{ 0x0000000e, "PCRYPT", NULL },
	{ 0x0000000f, "PCOUNTER", NULL },
	{ 0x00000011, "PDAEMON", NULL },
	{}
};

static const struct nouveau_enum vm_engine[] = {
	{ 0x00000000, "PGRAPH", NULL, NVDEV_ENGINE_GR },
	{ 0x00000001, "PVP", NULL, NVDEV_ENGINE_VP },
	{ 0x00000004, "PEEPHOLE", NULL },
	{ 0x00000005, "PFIFO", vm_pfifo_subclients, NVDEV_ENGINE_FIFO },
	{ 0x00000006, "BAR", vm_bar_subclients },
	{ 0x00000008, "PPPP", NULL, NVDEV_ENGINE_PPP },
	{ 0x00000008, "PMPEG", NULL, NVDEV_ENGINE_MPEG },
	{ 0x00000009, "PBSP", NULL, NVDEV_ENGINE_BSP },
	{ 0x0000000a, "PCRYPT", NULL, NVDEV_ENGINE_CRYPT },
	{ 0x0000000b, "PCOUNTER", NULL },
	{ 0x0000000c, "SEMAPHORE_BG", NULL },
	{ 0x0000000d, "PCOPY", NULL, NVDEV_ENGINE_COPY0 },
	{ 0x0000000e, "PDAEMON", NULL },
	{}
};

static const struct nouveau_enum vm_fault[] = {
	{ 0x00000000, "PT_NOT_PRESENT", NULL },
	{ 0x00000001, "PT_TOO_SHORT", NULL },
	{ 0x00000002, "PAGE_NOT_PRESENT", NULL },
	{ 0x00000003, "PAGE_SYSTEM_ONLY", NULL },
	{ 0x00000004, "PAGE_READ_ONLY", NULL },
	{ 0x00000006, "NULL_DMAOBJ", NULL },
	{ 0x00000007, "WRONG_MEMTYPE", NULL },
	{ 0x0000000b, "VRAM_LIMIT", NULL },
	{ 0x0000000f, "DMAOBJ_LIMIT", NULL },
	{}
};

static void
nv50_fb_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_device *device = nv_device(subdev);
	struct nouveau_engine *engine;
	struct nv50_fb_priv *priv = (void *)subdev;
	const struct nouveau_enum *en, *cl;
	struct nouveau_object *engctx = NULL;
	u32 trap[6], idx, chan;
	u8 st0, st1, st2, st3;
	int i;

	idx = nv_rd32(priv, 0x100c90);
	if (!(idx & 0x80000000))
		return;
	idx &= 0x00ffffff;

	for (i = 0; i < 6; i++) {
		nv_wr32(priv, 0x100c90, idx | i << 24);
		trap[i] = nv_rd32(priv, 0x100c94);
	}
	nv_wr32(priv, 0x100c90, idx | 0x80000000);

	/* decode status bits into something more useful */
	if (device->chipset  < 0xa3 ||
	    device->chipset == 0xaa || device->chipset == 0xac) {
		st0 = (trap[0] & 0x0000000f) >> 0;
		st1 = (trap[0] & 0x000000f0) >> 4;
		st2 = (trap[0] & 0x00000f00) >> 8;
		st3 = (trap[0] & 0x0000f000) >> 12;
	} else {
		st0 = (trap[0] & 0x000000ff) >> 0;
		st1 = (trap[0] & 0x0000ff00) >> 8;
		st2 = (trap[0] & 0x00ff0000) >> 16;
		st3 = (trap[0] & 0xff000000) >> 24;
	}
	chan = (trap[2] << 16) | trap[1];

	en = nouveau_enum_find(vm_engine, st0);

	if (en && en->data2) {
		const struct nouveau_enum *orig_en = en;
		while (en->name && en->value == st0 && en->data2) {
			engine = nouveau_engine(subdev, en->data2);
			if (engine) {
				engctx = nouveau_engctx_get(engine, chan);
				if (engctx)
					break;
			}
			en++;
		}
		if (!engctx)
			en = orig_en;
	}

	nv_error(priv, "trapped %s at 0x%02x%04x%04x on channel 0x%08x [%s] ",
		 (trap[5] & 0x00000100) ? "read" : "write",
		 trap[5] & 0xff, trap[4] & 0xffff, trap[3] & 0xffff, chan,
		 nouveau_client_name(engctx));

	nouveau_engctx_put(engctx);

	if (en)
		pr_cont("%s/", en->name);
	else
		pr_cont("%02x/", st0);

	cl = nouveau_enum_find(vm_client, st2);
	if (cl)
		pr_cont("%s/", cl->name);
	else
		pr_cont("%02x/", st2);

	if      (cl && cl->data) cl = nouveau_enum_find(cl->data, st3);
	else if (en && en->data) cl = nouveau_enum_find(en->data, st3);
	else                     cl = NULL;
	if (cl)
		pr_cont("%s", cl->name);
	else
		pr_cont("%02x", st3);

	pr_cont(" reason: ");
	en = nouveau_enum_find(vm_fault, st1);
	if (en)
		pr_cont("%s\n", en->name);
	else
		pr_cont("0x%08x\n", st1);
}

int
nv50_fb_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	     struct nouveau_oclass *oclass, void *data, u32 size,
	     struct nouveau_object **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct nv50_fb_priv *priv;
	int ret;

	ret = nouveau_fb_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->r100c08_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (priv->r100c08_page) {
		priv->r100c08 = nv_device_map_page(device, priv->r100c08_page);
		if (!priv->r100c08)
			nv_warn(priv, "failed 0x100c08 page map\n");
	} else {
		nv_warn(priv, "failed 0x100c08 page alloc\n");
	}

	nv_subdev(priv)->intr = nv50_fb_intr;
	return 0;
}

void
nv50_fb_dtor(struct nouveau_object *object)
{
	struct nouveau_device *device = nv_device(object);
	struct nv50_fb_priv *priv = (void *)object;

	if (priv->r100c08_page) {
		nv_device_unmap_page(device, priv->r100c08);
		__free_page(priv->r100c08_page);
	}

	nouveau_fb_destroy(&priv->base);
}

int
nv50_fb_init(struct nouveau_object *object)
{
	struct nv50_fb_impl *impl = (void *)object->oclass;
	struct nv50_fb_priv *priv = (void *)object;
	int ret;

	ret = nouveau_fb_init(&priv->base);
	if (ret)
		return ret;

	/* Not a clue what this is exactly.  Without pointing it at a
	 * scratch page, VRAM->GART blits with M2MF (as in DDX DFS)
	 * cause IOMMU "read from address 0" errors (rh#561267)
	 */
	nv_wr32(priv, 0x100c08, priv->r100c08 >> 8);

	/* This is needed to get meaningful information from 100c90
	 * on traps. No idea what these values mean exactly. */
	nv_wr32(priv, 0x100c90, impl->trap);
	return 0;
}

struct nouveau_oclass *
nv50_fb_oclass = &(struct nv50_fb_impl) {
	.base.base.handle = NV_SUBDEV(FB, 0x50),
	.base.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_fb_ctor,
		.dtor = nv50_fb_dtor,
		.init = nv50_fb_init,
		.fini = _nouveau_fb_fini,
	},
	.base.memtype = nv50_fb_memtype_valid,
	.base.ram = &nv50_ram_oclass,
	.trap = 0x000707ff,
}.base.base;
