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

const struct nv50_disp_mthd_list
g84_disp_core_mthd_dac = {
	.mthd = 0x0080,
	.addr = 0x000008,
	.data = {
		{ 0x0400, 0x610b58 },
		{ 0x0404, 0x610bdc },
		{ 0x0420, 0x610bc4 },
		{}
	}
};

const struct nv50_disp_mthd_list
g84_disp_core_mthd_head = {
	.mthd = 0x0400,
	.addr = 0x000540,
	.data = {
		{ 0x0800, 0x610ad8 },
		{ 0x0804, 0x610ad0 },
		{ 0x0808, 0x610a48 },
		{ 0x080c, 0x610a78 },
		{ 0x0810, 0x610ac0 },
		{ 0x0814, 0x610af8 },
		{ 0x0818, 0x610b00 },
		{ 0x081c, 0x610ae8 },
		{ 0x0820, 0x610af0 },
		{ 0x0824, 0x610b08 },
		{ 0x0828, 0x610b10 },
		{ 0x082c, 0x610a68 },
		{ 0x0830, 0x610a60 },
		{ 0x0834, 0x000000 },
		{ 0x0838, 0x610a40 },
		{ 0x0840, 0x610a24 },
		{ 0x0844, 0x610a2c },
		{ 0x0848, 0x610aa8 },
		{ 0x084c, 0x610ab0 },
		{ 0x085c, 0x610c5c },
		{ 0x0860, 0x610a84 },
		{ 0x0864, 0x610a90 },
		{ 0x0868, 0x610b18 },
		{ 0x086c, 0x610b20 },
		{ 0x0870, 0x610ac8 },
		{ 0x0874, 0x610a38 },
		{ 0x0878, 0x610c50 },
		{ 0x0880, 0x610a58 },
		{ 0x0884, 0x610a9c },
		{ 0x089c, 0x610c68 },
		{ 0x08a0, 0x610a70 },
		{ 0x08a4, 0x610a50 },
		{ 0x08a8, 0x610ae0 },
		{ 0x08c0, 0x610b28 },
		{ 0x08c4, 0x610b30 },
		{ 0x08c8, 0x610b40 },
		{ 0x08d4, 0x610b38 },
		{ 0x08d8, 0x610b48 },
		{ 0x08dc, 0x610b50 },
		{ 0x0900, 0x610a18 },
		{ 0x0904, 0x610ab8 },
		{ 0x0910, 0x610c70 },
		{ 0x0914, 0x610c78 },
		{}
	}
};

const struct nv50_disp_chan_mthd
g84_disp_core_chan_mthd = {
	.name = "Core",
	.addr = 0x000000,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &nv50_disp_core_mthd_base },
		{    "DAC", 3, &g84_disp_core_mthd_dac  },
		{    "SOR", 2, &nv50_disp_core_mthd_sor  },
		{   "PIOR", 3, &nv50_disp_core_mthd_pior },
		{   "HEAD", 2, &g84_disp_core_mthd_head },
		{}
	}
};

const struct nv50_disp_dmac_oclass
g84_disp_core_oclass = {
	.base.oclass = G82_DISP_CORE_CHANNEL_DMA,
	.base.minver = 0,
	.base.maxver = 0,
	.ctor = nv50_disp_core_new,
	.func = &nv50_disp_core_func,
	.mthd = &g84_disp_core_chan_mthd,
	.chid = 0,
};
