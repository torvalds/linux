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
#include "priv.h"
#include "pad.h"

#include <subdev/gsp.h>

static void
gm200_aux_autodpcd(struct nvkm_i2c *i2c, int aux, bool enable)
{
	nvkm_mask(i2c->subdev.device, 0x00d968 + (aux * 0x50), 0x00010000, enable << 16);
}

static const struct nvkm_i2c_func
gm200_i2c = {
	.pad_x_new = gf119_i2c_pad_x_new,
	.pad_s_new = gm200_i2c_pad_s_new,
	.aux = 8,
	.aux_stat = gk104_aux_stat,
	.aux_mask = gk104_aux_mask,
	.aux_autodpcd = gm200_aux_autodpcd,
};

int
gm200_i2c_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_i2c **pi2c)
{
	if (nvkm_gsp_rm(device->gsp))
		return -ENODEV;

	return nvkm_i2c_new_(&gm200_i2c, device, type, inst, pi2c);
}
