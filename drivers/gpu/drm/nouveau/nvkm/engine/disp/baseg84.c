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
#include "dmacnv50.h"
#include "rootnv50.h"

#include <nvif/class.h>

static const struct nv50_disp_mthd_list
g84_disp_base_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x0008c4 },
		{ 0x0088, 0x0008d0 },
		{ 0x008c, 0x0008dc },
		{ 0x0090, 0x0008e4 },
		{ 0x0094, 0x610884 },
		{ 0x00a0, 0x6108a0 },
		{ 0x00a4, 0x610878 },
		{ 0x00c0, 0x61086c },
		{ 0x00c4, 0x610800 },
		{ 0x00c8, 0x61080c },
		{ 0x00cc, 0x610818 },
		{ 0x00e0, 0x610858 },
		{ 0x00e4, 0x610860 },
		{ 0x00e8, 0x6108ac },
		{ 0x00ec, 0x6108b4 },
		{ 0x00fc, 0x610824 },
		{ 0x0100, 0x610894 },
		{ 0x0104, 0x61082c },
		{ 0x0110, 0x6108bc },
		{ 0x0114, 0x61088c },
		{}
	}
};

const struct nv50_disp_chan_mthd
g84_disp_base_chan_mthd = {
	.name = "Base",
	.addr = 0x000540,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &g84_disp_base_mthd_base },
		{  "Image", 2, &nv50_disp_base_mthd_image },
		{}
	}
};

const struct nv50_disp_dmac_oclass
g84_disp_base_oclass = {
	.base.oclass = G82_DISP_BASE_CHANNEL_DMA,
	.base.minver = 0,
	.base.maxver = 0,
	.ctor = nv50_disp_base_new,
	.func = &nv50_disp_dmac_func,
	.mthd = &g84_disp_base_chan_mthd,
	.chid = 1,
};
