/*
 * Copyright 2013 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "nv50.h"

static const struct nvkm_clk_func
g84_clk = {
	.read = nv50_clk_read,
	.calc = nv50_clk_calc,
	.prog = nv50_clk_prog,
	.tidy = nv50_clk_tidy,
	.domains = {
		{ nv_clk_src_crystal, 0xff },
		{ nv_clk_src_href   , 0xff },
		{ nv_clk_src_core   , 0xff, 0, "core", 1000 },
		{ nv_clk_src_shader , 0xff, 0, "shader", 1000 },
		{ nv_clk_src_mem    , 0xff, 0, "memory", 1000 },
		{ nv_clk_src_vdec   , 0xff },
		{ nv_clk_src_max }
	}
};

int
g84_clk_new(struct nvkm_device *device, int index, struct nvkm_clk **pclk)
{
	return nv50_clk_new_(&g84_clk, device, index,
			     (device->chipset == 0xa0), pclk);
}
