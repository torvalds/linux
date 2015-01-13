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

#include "ctxnvc0.h"

/*******************************************************************************
 * PGRAPH context register lists
 ******************************************************************************/

static const struct nvc0_graph_init
gk110b_grctx_init_sm_0[] = {
	{ 0x419e04,   1, 0x04, 0x00000000 },
	{ 0x419e08,   1, 0x04, 0x0000001d },
	{ 0x419e0c,   1, 0x04, 0x00000000 },
	{ 0x419e10,   1, 0x04, 0x00001c02 },
	{ 0x419e44,   1, 0x04, 0x0013eff2 },
	{ 0x419e48,   1, 0x04, 0x00000000 },
	{ 0x419e4c,   1, 0x04, 0x0000007f },
	{ 0x419e50,   2, 0x04, 0x00000000 },
	{ 0x419e58,   1, 0x04, 0x00000001 },
	{ 0x419e5c,   3, 0x04, 0x00000000 },
	{ 0x419e68,   1, 0x04, 0x00000002 },
	{ 0x419e6c,  12, 0x04, 0x00000000 },
	{ 0x419eac,   1, 0x04, 0x00001f8f },
	{ 0x419eb0,   1, 0x04, 0x0db00d2f },
	{ 0x419eb8,   1, 0x04, 0x00000000 },
	{ 0x419ec8,   1, 0x04, 0x0001304f },
	{ 0x419f30,   4, 0x04, 0x00000000 },
	{ 0x419f40,   1, 0x04, 0x00000018 },
	{ 0x419f44,   3, 0x04, 0x00000000 },
	{ 0x419f58,   1, 0x04, 0x00000000 },
	{ 0x419f70,   1, 0x04, 0x00006300 },
	{ 0x419f78,   1, 0x04, 0x000000eb },
	{ 0x419f7c,   1, 0x04, 0x00000404 },
	{}
};

static const struct nvc0_graph_pack
gk110b_grctx_pack_tpc[] = {
	{ nvd7_grctx_init_pe_0 },
	{ nvf0_grctx_init_tex_0 },
	{ nvf0_grctx_init_mpc_0 },
	{ nvf0_grctx_init_l1c_0 },
	{ gk110b_grctx_init_sm_0 },
	{}
};

/*******************************************************************************
 * PGRAPH context implementation
 ******************************************************************************/

struct nouveau_oclass *
gk110b_grctx_oclass = &(struct nvc0_grctx_oclass) {
	.base.handle = NV_ENGCTX(GR, 0xf1),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_context_ctor,
		.dtor = nvc0_graph_context_dtor,
		.init = _nouveau_graph_context_init,
		.fini = _nouveau_graph_context_fini,
		.rd32 = _nouveau_graph_context_rd32,
		.wr32 = _nouveau_graph_context_wr32,
	},
	.main  = nve4_grctx_generate_main,
	.unkn  = nve4_grctx_generate_unkn,
	.hub   = nvf0_grctx_pack_hub,
	.gpc   = nvf0_grctx_pack_gpc,
	.zcull = nvc0_grctx_pack_zcull,
	.tpc   = gk110b_grctx_pack_tpc,
	.ppc   = nvf0_grctx_pack_ppc,
	.icmd  = nvf0_grctx_pack_icmd,
	.mthd  = nvf0_grctx_pack_mthd,
	.bundle = nve4_grctx_generate_bundle,
	.bundle_size = 0x3000,
	.bundle_min_gpm_fifo_depth = 0x180,
	.bundle_token_limit = 0x600,
	.pagepool = nve4_grctx_generate_pagepool,
	.pagepool_size = 0x8000,
	.attrib = nvd7_grctx_generate_attrib,
	.attrib_nr_max = 0x324,
	.attrib_nr = 0x218,
	.alpha_nr_max = 0x7ff,
	.alpha_nr = 0x648,
}.base;
