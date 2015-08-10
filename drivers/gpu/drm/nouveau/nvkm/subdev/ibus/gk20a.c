/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <subdev/ibus.h>
#include <subdev/timer.h>

struct gk20a_ibus_priv {
	struct nvkm_ibus base;
};

static void
gk20a_ibus_init_priv_ring(struct gk20a_ibus_priv *priv)
{
	nv_mask(priv, 0x137250, 0x3f, 0);

	nv_mask(priv, 0x000200, 0x20, 0);
	usleep_range(20, 30);
	nv_mask(priv, 0x000200, 0x20, 0x20);

	nv_wr32(priv, 0x12004c, 0x4);
	nv_wr32(priv, 0x122204, 0x2);
	nv_rd32(priv, 0x122204);

	/*
	 * Bug: increase clock timeout to avoid operation failure at high
	 * gpcclk rate.
	 */
	nv_wr32(priv, 0x122354, 0x800);
	nv_wr32(priv, 0x128328, 0x800);
	nv_wr32(priv, 0x124320, 0x800);
}

static void
gk20a_ibus_intr(struct nvkm_subdev *subdev)
{
	struct gk20a_ibus_priv *priv = (void *)subdev;
	u32 status0 = nv_rd32(priv, 0x120058);

	if (status0 & 0x7) {
		nv_debug(priv, "resetting priv ring\n");
		gk20a_ibus_init_priv_ring(priv);
	}

	/* Acknowledge interrupt */
	nv_mask(priv, 0x12004c, 0x2, 0x2);

	if (!nv_wait(subdev, 0x12004c, 0x3f, 0x00))
		nv_warn(priv, "timeout waiting for ringmaster ack\n");
}

static int
gk20a_ibus_init(struct nvkm_object *object)
{
	struct gk20a_ibus_priv *priv = (void *)object;
	int ret;

	ret = _nvkm_ibus_init(object);
	if (ret)
		return ret;

	gk20a_ibus_init_priv_ring(priv);

	return 0;
}

static int
gk20a_ibus_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	struct gk20a_ibus_priv *priv;
	int ret;

	ret = nvkm_ibus_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->intr = gk20a_ibus_intr;
	return 0;
}

struct nvkm_oclass
gk20a_ibus_oclass = {
	.handle = NV_SUBDEV(IBUS, 0xea),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gk20a_ibus_ctor,
		.dtor = _nvkm_ibus_dtor,
		.init = gk20a_ibus_init,
		.fini = _nvkm_ibus_fini,
	},
};
