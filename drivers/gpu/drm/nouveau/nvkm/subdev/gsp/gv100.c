/*
 * Copyright 2019 Red Hat Inc.
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
 */
#include <subdev/gsp.h>
#include <subdev/top.h>
#include <engine/falcon.h>

static int
gv100_gsp_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_gsp *gsp = nvkm_gsp(subdev);

	gsp->addr = nvkm_top_addr(subdev->device, subdev->index);
	if (!gsp->addr)
		return -EINVAL;

	return nvkm_falcon_v1_new(subdev, "GSP", gsp->addr, &gsp->falcon);
}

static void *
gv100_gsp_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_gsp *gsp = nvkm_gsp(subdev);
	nvkm_falcon_del(&gsp->falcon);
	return gsp;
}

static const struct nvkm_subdev_func
gv100_gsp = {
	.dtor = gv100_gsp_dtor,
	.oneinit = gv100_gsp_oneinit,
};

int
gv100_gsp_new(struct nvkm_device *device, int index, struct nvkm_gsp **pgsp)
{
	struct nvkm_gsp *gsp;

	if (!(gsp = *pgsp = kzalloc(sizeof(*gsp), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_subdev_ctor(&gv100_gsp, device, index, &gsp->subdev);
	return 0;
}
