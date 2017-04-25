/*
 * Copyright 2016 Karol Herbst
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
 * Authors: Karol Herbst
 */
#include "priv.h"

#include <subdev/fuse.h>

static int
gf100_volt_speedo_read(struct nvkm_volt *volt)
{
	struct nvkm_device *device = volt->subdev.device;
	struct nvkm_fuse *fuse = device->fuse;

	if (!fuse)
		return -EINVAL;

	return nvkm_fuse_read(fuse, 0x1cc);
}

int
gf100_volt_oneinit(struct nvkm_volt *volt)
{
	struct nvkm_subdev *subdev = &volt->subdev;
	if (volt->speedo <= 0)
		nvkm_error(subdev, "couldn't find speedo value, volting not "
			   "possible\n");
	return 0;
}

static const struct nvkm_volt_func
gf100_volt = {
	.oneinit = gf100_volt_oneinit,
	.vid_get = nvkm_voltgpio_get,
	.vid_set = nvkm_voltgpio_set,
	.speedo_read = gf100_volt_speedo_read,
};

int
gf100_volt_new(struct nvkm_device *device, int index, struct nvkm_volt **pvolt)
{
	struct nvkm_volt *volt;
	int ret;

	ret = nvkm_volt_new_(&gf100_volt, device, index, &volt);
	*pvolt = volt;
	if (ret)
		return ret;

	return nvkm_voltgpio_init(volt);
}
