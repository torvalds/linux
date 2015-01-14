/*
 * Copyright 2012 Nouveau Community
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
 * Authors: Martin Peres <martin.peres@labri.fr>
 *          Ben Skeggs
 */
#include "nv04.h"

static void
gf100_bus_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_bus *pbus = nvkm_bus(subdev);
	u32 stat = nv_rd32(pbus, 0x001100) & nv_rd32(pbus, 0x001140);

	if (stat & 0x0000000e) {
		u32 addr = nv_rd32(pbus, 0x009084);
		u32 data = nv_rd32(pbus, 0x009088);

		nv_error(pbus, "MMIO %s of 0x%08x FAULT at 0x%06x [ %s%s%s]\n",
			 (addr & 0x00000002) ? "write" : "read", data,
			 (addr & 0x00fffffc),
			 (stat & 0x00000002) ? "!ENGINE " : "",
			 (stat & 0x00000004) ? "IBUS " : "",
			 (stat & 0x00000008) ? "TIMEOUT " : "");

		nv_wr32(pbus, 0x009084, 0x00000000);
		nv_wr32(pbus, 0x001100, (stat & 0x0000000e));
		stat &= ~0x0000000e;
	}

	if (stat) {
		nv_error(pbus, "unknown intr 0x%08x\n", stat);
		nv_mask(pbus, 0x001140, stat, 0x00000000);
	}
}

static int
gf100_bus_init(struct nvkm_object *object)
{
	struct nv04_bus_priv *priv = (void *)object;
	int ret;

	ret = nvkm_bus_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x001100, 0xffffffff);
	nv_wr32(priv, 0x001140, 0x0000000e);
	return 0;
}

struct nvkm_oclass *
gf100_bus_oclass = &(struct nv04_bus_impl) {
	.base.handle = NV_SUBDEV(BUS, 0xc0),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_bus_ctor,
		.dtor = _nvkm_bus_dtor,
		.init = gf100_bus_init,
		.fini = _nvkm_bus_fini,
	},
	.intr = gf100_bus_intr,
}.base;
