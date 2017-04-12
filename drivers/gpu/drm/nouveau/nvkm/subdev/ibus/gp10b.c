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
#include <subdev/ibus.h>

#include "priv.h"

static int
gp10b_ibus_init(struct nvkm_subdev *ibus)
{
	struct nvkm_device *device = ibus->device;

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
gp10b_ibus = {
	.init = gp10b_ibus_init,
	.intr = gk104_ibus_intr,
};

int
gp10b_ibus_new(struct nvkm_device *device, int index,
	       struct nvkm_subdev **pibus)
{
	struct nvkm_subdev *ibus;
	if (!(ibus = *pibus = kzalloc(sizeof(*ibus), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_subdev_ctor(&gp10b_ibus, device, index, ibus);
	return 0;
}
