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
#include <subdev/privring.h>
#include <subdev/timer.h>

static void
gk20a_privring_init_privring_ring(struct nvkm_subdev *privring)
{
	struct nvkm_device *device = privring->device;
	nvkm_mask(device, 0x137250, 0x3f, 0);

	nvkm_mask(device, 0x000200, 0x20, 0);
	udelay(20);
	nvkm_mask(device, 0x000200, 0x20, 0x20);

	nvkm_wr32(device, 0x12004c, 0x4);
	nvkm_wr32(device, 0x122204, 0x2);
	nvkm_rd32(device, 0x122204);

	/*
	 * Bug: increase clock timeout to avoid operation failure at high
	 * gpcclk rate.
	 */
	nvkm_wr32(device, 0x122354, 0x800);
	nvkm_wr32(device, 0x128328, 0x800);
	nvkm_wr32(device, 0x124320, 0x800);
}

static void
gk20a_privring_intr(struct nvkm_subdev *privring)
{
	struct nvkm_device *device = privring->device;
	u32 status0 = nvkm_rd32(device, 0x120058);

	if (status0 & 0x7) {
		nvkm_debug(privring, "resetting privring ring\n");
		gk20a_privring_init_privring_ring(privring);
	}

	/* Acknowledge interrupt */
	nvkm_mask(device, 0x12004c, 0x2, 0x2);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x12004c) & 0x0000003f))
			break;
	);
}

static int
gk20a_privring_init(struct nvkm_subdev *privring)
{
	gk20a_privring_init_privring_ring(privring);
	return 0;
}

static const struct nvkm_subdev_func
gk20a_privring = {
	.init = gk20a_privring_init,
	.intr = gk20a_privring_intr,
};

int
gk20a_privring_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		   struct nvkm_subdev **pprivring)
{
	return nvkm_subdev_new_(&gk20a_privring, device, type, inst, pprivring);
}
