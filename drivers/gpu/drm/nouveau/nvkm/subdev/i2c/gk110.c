/*
 * Copyright 2021 Red Hat Inc.
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
#include "priv.h"
#include "pad.h"

static void
gk110_aux_autodpcd(struct nvkm_i2c *i2c, int aux, bool enable)
{
	nvkm_mask(i2c->subdev.device, 0x00e4f8 + (aux * 0x50), 0x00010000, enable << 16);
}

static const struct nvkm_i2c_func
gk110_i2c = {
	.pad_x_new = gf119_i2c_pad_x_new,
	.pad_s_new = gf119_i2c_pad_s_new,
	.aux = 4,
	.aux_stat = gk104_aux_stat,
	.aux_mask = gk104_aux_mask,
	.aux_autodpcd = gk110_aux_autodpcd,
};

int
gk110_i2c_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_i2c **pi2c)
{
	return nvkm_i2c_new_(&gk110_i2c, device, type, inst, pi2c);
}
