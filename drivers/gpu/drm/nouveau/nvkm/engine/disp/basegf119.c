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
#include "channv50.h"

static const struct nv50_disp_mthd_list
gf119_disp_base_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x661080 },
		{ 0x0084, 0x661084 },
		{ 0x0088, 0x661088 },
		{ 0x008c, 0x66108c },
		{ 0x0090, 0x661090 },
		{ 0x0094, 0x661094 },
		{ 0x00a0, 0x6610a0 },
		{ 0x00a4, 0x6610a4 },
		{ 0x00c0, 0x6610c0 },
		{ 0x00c4, 0x6610c4 },
		{ 0x00c8, 0x6610c8 },
		{ 0x00cc, 0x6610cc },
		{ 0x00e0, 0x6610e0 },
		{ 0x00e4, 0x6610e4 },
		{ 0x00e8, 0x6610e8 },
		{ 0x00ec, 0x6610ec },
		{ 0x00fc, 0x6610fc },
		{ 0x0100, 0x661100 },
		{ 0x0104, 0x661104 },
		{ 0x0108, 0x661108 },
		{ 0x010c, 0x66110c },
		{ 0x0110, 0x661110 },
		{ 0x0114, 0x661114 },
		{ 0x0118, 0x661118 },
		{ 0x011c, 0x66111c },
		{ 0x0130, 0x661130 },
		{ 0x0134, 0x661134 },
		{ 0x0138, 0x661138 },
		{ 0x013c, 0x66113c },
		{ 0x0140, 0x661140 },
		{ 0x0144, 0x661144 },
		{ 0x0148, 0x661148 },
		{ 0x014c, 0x66114c },
		{ 0x0150, 0x661150 },
		{ 0x0154, 0x661154 },
		{ 0x0158, 0x661158 },
		{ 0x015c, 0x66115c },
		{ 0x0160, 0x661160 },
		{ 0x0164, 0x661164 },
		{ 0x0168, 0x661168 },
		{ 0x016c, 0x66116c },
		{}
	}
};

static const struct nv50_disp_mthd_list
gf119_disp_base_mthd_image = {
	.mthd = 0x0020,
	.addr = 0x000020,
	.data = {
		{ 0x0400, 0x661400 },
		{ 0x0404, 0x661404 },
		{ 0x0408, 0x661408 },
		{ 0x040c, 0x66140c },
		{ 0x0410, 0x661410 },
		{}
	}
};

const struct nv50_disp_chan_mthd
gf119_disp_base_mthd = {
	.name = "Base",
	.addr = 0x001000,
	.prev = -0x020000,
	.data = {
		{ "Global", 1, &gf119_disp_base_mthd_base },
		{  "Image", 2, &gf119_disp_base_mthd_image },
		{}
	}
};

int
gf119_disp_base_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nv50_disp *disp, struct nvkm_object **pobject)
{
	return nv50_disp_base_new_(&gf119_disp_dmac_func, &gf119_disp_base_mthd,
				   disp, 1, oclass, argv, argc, pobject);
}
