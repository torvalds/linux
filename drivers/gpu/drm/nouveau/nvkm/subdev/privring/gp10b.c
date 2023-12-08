/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
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

#include "priv.h"

static int
gp10b_privring_init(struct nvkm_subdev *privring)
{
	struct nvkm_device *device = privring->device;

	nvkm_wr32(device, 0x1200a8, 0x0);

	/* init ring */
	nvkm_wr32(device, 0x12004c, 0x4);
	nvkm_wr32(device, 0x122204, 0x2);
	nvkm_rd32(device, 0x122204);

	/* timeout configuration */
	nvkm_wr32(device, 0x009080, 0x800186a0);

	return 0;
}

static const struct nvkm_subdev_func
gp10b_privring = {
	.init = gp10b_privring_init,
	.intr = gk104_privring_intr,
};

int
gp10b_privring_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		   struct nvkm_subdev **pprivring)
{
	return nvkm_subdev_new_(&gp10b_privring, device, type, inst, pprivring);
}
