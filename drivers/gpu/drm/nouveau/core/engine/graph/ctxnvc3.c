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

#include "nvc0.h"

static struct nvc0_graph_init
nvc3_grctx_init_tpc[] = {
	{ 0x419818,   1, 0x04, 0x00000000 },
	{ 0x41983c,   1, 0x04, 0x00038bc7 },
	{ 0x419848,   1, 0x04, 0x00000000 },
	{ 0x419864,   1, 0x04, 0x0000012a },
	{ 0x419888,   1, 0x04, 0x00000000 },
	{ 0x419a00,   1, 0x04, 0x000001f0 },
	{ 0x419a04,   1, 0x04, 0x00000001 },
	{ 0x419a08,   1, 0x04, 0x00000023 },
	{ 0x419a0c,   1, 0x04, 0x00020000 },
	{ 0x419a10,   1, 0x04, 0x00000000 },
	{ 0x419a14,   1, 0x04, 0x00000200 },
	{ 0x419a1c,   1, 0x04, 0x00000000 },
	{ 0x419a20,   1, 0x04, 0x00000800 },
	{ 0x419ac4,   1, 0x04, 0x0007f440 },
	{ 0x419b00,   1, 0x04, 0x0a418820 },
	{ 0x419b04,   1, 0x04, 0x062080e6 },
	{ 0x419b08,   1, 0x04, 0x020398a4 },
	{ 0x419b0c,   1, 0x04, 0x0e629062 },
	{ 0x419b10,   1, 0x04, 0x0a418820 },
	{ 0x419b14,   1, 0x04, 0x000000e6 },
	{ 0x419bd0,   1, 0x04, 0x00900103 },
	{ 0x419be0,   1, 0x04, 0x00000001 },
	{ 0x419be4,   1, 0x04, 0x00000000 },
	{ 0x419c00,   1, 0x04, 0x00000002 },
	{ 0x419c04,   1, 0x04, 0x00000006 },
	{ 0x419c08,   1, 0x04, 0x00000002 },
	{ 0x419c20,   1, 0x04, 0x00000000 },
	{ 0x419cb0,   1, 0x04, 0x00020048 },
	{ 0x419ce8,   1, 0x04, 0x00000000 },
	{ 0x419cf4,   1, 0x04, 0x00000183 },
	{ 0x419d20,   1, 0x04, 0x02180000 },
	{ 0x419d24,   1, 0x04, 0x00001fff },
	{ 0x419e04,   3, 0x04, 0x00000000 },
	{ 0x419e10,   1, 0x04, 0x00000002 },
	{ 0x419e44,   1, 0x04, 0x001beff2 },
	{ 0x419e48,   1, 0x04, 0x00000000 },
	{ 0x419e4c,   1, 0x04, 0x0000000f },
	{ 0x419e50,  17, 0x04, 0x00000000 },
	{ 0x419e98,   1, 0x04, 0x00000000 },
	{ 0x419ee0,   1, 0x04, 0x00011110 },
	{ 0x419f30,  11, 0x04, 0x00000000 },
	{}
};

struct nvc0_graph_init *
nvc3_grctx_init_gpc[] = {
	nvc0_grctx_init_gpc_0,
	nvc0_grctx_init_gpc_1,
	nvc3_grctx_init_tpc,
	NULL
};

struct nouveau_oclass *
nvc3_grctx_oclass = &(struct nvc0_grctx_oclass) {
	.base.handle = NV_ENGCTX(GR, 0xc3),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_context_ctor,
		.dtor = nvc0_graph_context_dtor,
		.init = _nouveau_graph_context_init,
		.fini = _nouveau_graph_context_fini,
		.rd32 = _nouveau_graph_context_rd32,
		.wr32 = _nouveau_graph_context_wr32,
	},
	.main = nvc0_grctx_generate_main,
	.mods = nvc0_grctx_generate_mods,
	.unkn = nvc0_grctx_generate_unkn,
	.hub  = nvc0_grctx_init_hub,
	.gpc  = nvc3_grctx_init_gpc,
	.icmd = nvc0_grctx_init_icmd,
	.mthd = nvc0_grctx_init_mthd,
}.base;
