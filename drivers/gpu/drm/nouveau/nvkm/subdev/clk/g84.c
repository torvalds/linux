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

static struct nvkm_domain
g84_domains[] = {
	{ nv_clk_src_crystal, 0xff },
	{ nv_clk_src_href   , 0xff },
	{ nv_clk_src_core   , 0xff, 0, "core", 1000 },
	{ nv_clk_src_shader , 0xff, 0, "shader", 1000 },
	{ nv_clk_src_mem    , 0xff, 0, "memory", 1000 },
	{ nv_clk_src_vdec   , 0xff },
	{ nv_clk_src_max }
};

struct nvkm_oclass *
g84_clk_oclass = &(struct nv50_clk_oclass) {
	.base.handle = NV_SUBDEV(CLK, 0x84),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_clk_ctor,
		.dtor = _nvkm_clk_dtor,
		.init = _nvkm_clk_init,
		.fini = _nvkm_clk_fini,
	},
	.domains = g84_domains,
}.base;
