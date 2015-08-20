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

static void
gk20a_ibus_init_ibus_ring(struct nvkm_ibus *ibus)
{
	nv_mask(ibus, 0x137250, 0x3f, 0);

	nv_mask(ibus, 0x000200, 0x20, 0);
	usleep_range(20, 30);
	nv_mask(ibus, 0x000200, 0x20, 0x20);

	nv_wr32(ibus, 0x12004c, 0x4);
	nv_wr32(ibus, 0x122204, 0x2);
	nv_rd32(ibus, 0x122204);

	/*
	 * Bug: increase clock timeout to avoid operation failure at high
	 * gpcclk rate.
	 */
	nv_wr32(ibus, 0x122354, 0x800);
	nv_wr32(ibus, 0x128328, 0x800);
	nv_wr32(ibus, 0x124320, 0x800);
}

static void
gk20a_ibus_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_ibus *ibus = (void *)subdev;
	u32 status0 = nv_rd32(ibus, 0x120058);

	if (status0 & 0x7) {
		nv_debug(ibus, "resetting ibus ring\n");
		gk20a_ibus_init_ibus_ring(ibus);
	}

	/* Acknowledge interrupt */
	nv_mask(ibus, 0x12004c, 0x2, 0x2);

	if (!nv_wait(subdev, 0x12004c, 0x3f, 0x00))
		nv_warn(ibus, "timeout waiting for ringmaster ack\n");
}

static int
gk20a_ibus_init(struct nvkm_object *object)
{
	struct nvkm_ibus *ibus = (void *)object;
	int ret;

	ret = _nvkm_ibus_init(object);
	if (ret)
		return ret;

	gk20a_ibus_init_ibus_ring(ibus);

	return 0;
}

static int
gk20a_ibus_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	struct nvkm_ibus *ibus;
	int ret;

	ret = nvkm_ibus_create(parent, engine, oclass, &ibus);
	*pobject = nv_object(ibus);
	if (ret)
		return ret;

	nv_subdev(ibus)->intr = gk20a_ibus_intr;
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
