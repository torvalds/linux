/*
 * Copyright 2014 Red Hat Inc.
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
#include "pad.h"
#include "aux.h"
#include "bus.h"

static const struct nvkm_i2c_pad_func
gf119_i2c_pad_s_func = {
	.bus_new_4 = gf119_i2c_bus_new,
	.aux_new_6 = g94_i2c_aux_new,
	.mode = g94_i2c_pad_mode,
};

int
gf119_i2c_pad_s_new(struct nvkm_i2c *i2c, int id, struct nvkm_i2c_pad **ppad)
{
	return nvkm_i2c_pad_new_(&gf119_i2c_pad_s_func, i2c, id, ppad);
}

static const struct nvkm_i2c_pad_func
gf119_i2c_pad_x_func = {
	.bus_new_4 = gf119_i2c_bus_new,
	.aux_new_6 = g94_i2c_aux_new,
};

int
gf119_i2c_pad_x_new(struct nvkm_i2c *i2c, int id, struct nvkm_i2c_pad **ppad)
{
	return nvkm_i2c_pad_new_(&gf119_i2c_pad_x_func, i2c, id, ppad);
}
