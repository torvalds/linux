/*
 * Copyright 2013 Red Hat Inc.
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
#include "nv50.h"

struct mcp77_ram_priv {
	struct nvkm_ram base;
	u64 poller_base;
};

static int
mcp77_ram_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 datasize,
	       struct nvkm_object **pobject)
{
	u32 rsvd_head = ( 256 * 1024); /* vga memory */
	u32 rsvd_tail = (1024 * 1024); /* vbios etc */
	struct nvkm_fb *pfb = nvkm_fb(parent);
	struct mcp77_ram_priv *priv;
	int ret;

	ret = nvkm_ram_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.type   = NV_MEM_TYPE_STOLEN;
	priv->base.stolen = (u64)nv_rd32(pfb, 0x100e10) << 12;
	priv->base.size   = (u64)nv_rd32(pfb, 0x100e14) << 12;

	rsvd_tail += 0x1000;
	priv->poller_base = priv->base.size - rsvd_tail;

	ret = nvkm_mm_init(&pfb->vram, rsvd_head >> 12,
			   (priv->base.size  - (rsvd_head + rsvd_tail)) >> 12,
			   1);
	if (ret)
		return ret;

	priv->base.get = nv50_ram_get;
	priv->base.put = nv50_ram_put;
	return 0;
}

static int
mcp77_ram_init(struct nvkm_object *object)
{
	struct nvkm_fb *pfb = nvkm_fb(object);
	struct mcp77_ram_priv *priv = (void *)object;
	int ret;
	u64 dniso, hostnb, flush;

	ret = nvkm_ram_init(&priv->base);
	if (ret)
		return ret;

	dniso  = ((priv->base.size - (priv->poller_base + 0x00)) >> 5) - 1;
	hostnb = ((priv->base.size - (priv->poller_base + 0x20)) >> 5) - 1;
	flush  = ((priv->base.size - (priv->poller_base + 0x40)) >> 5) - 1;

	/* Enable NISO poller for various clients and set their associated
	 * read address, only for MCP77/78 and MCP79/7A. (fd#25701)
	 */
	nv_wr32(pfb, 0x100c18, dniso);
	nv_mask(pfb, 0x100c14, 0x00000000, 0x00000001);
	nv_wr32(pfb, 0x100c1c, hostnb);
	nv_mask(pfb, 0x100c14, 0x00000000, 0x00000002);
	nv_wr32(pfb, 0x100c24, flush);
	nv_mask(pfb, 0x100c14, 0x00000000, 0x00010000);
	return 0;
}

struct nvkm_oclass
mcp77_ram_oclass = {
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = mcp77_ram_ctor,
		.dtor = _nvkm_ram_dtor,
		.init = mcp77_ram_init,
		.fini = _nvkm_ram_fini,
	},
};
