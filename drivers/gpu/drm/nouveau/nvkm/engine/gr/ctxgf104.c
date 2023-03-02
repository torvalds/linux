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
#include "ctxgf100.h"

/*******************************************************************************
 * PGRAPH context register lists
 ******************************************************************************/

const struct gf100_gr_init
gf104_grctx_init_tex_0[] = {
	{ 0x419a00,   1, 0x04, 0x000001f0 },
	{ 0x419a04,   1, 0x04, 0x00000001 },
	{ 0x419a08,   1, 0x04, 0x00000023 },
	{ 0x419a0c,   1, 0x04, 0x00020000 },
	{ 0x419a10,   1, 0x04, 0x00000000 },
	{ 0x419a14,   1, 0x04, 0x00000200 },
	{ 0x419a1c,   1, 0x04, 0x00000000 },
	{ 0x419a20,   1, 0x04, 0x00000800 },
	{ 0x419ac4,   1, 0x04, 0x0007f440 },
	{}
};

const struct gf100_gr_init
gf104_grctx_init_l1c_0[] = {
	{ 0x419cb0,   1, 0x04, 0x00020048 },
	{ 0x419ce8,   1, 0x04, 0x00000000 },
	{ 0x419cf4,   1, 0x04, 0x00000183 },
	{}
};

const struct gf100_gr_init
gf104_grctx_init_sm_0[] = {
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

static const struct gf100_gr_pack
gf104_grctx_pack_tpc[] = {
	{ gf100_grctx_init_pe_0 },
	{ gf104_grctx_init_tex_0 },
	{ gf100_grctx_init_wwdx_0 },
	{ gf100_grctx_init_mpc_0 },
	{ gf104_grctx_init_l1c_0 },
	{ gf100_grctx_init_tpccs_0 },
	{ gf104_grctx_init_sm_0 },
	{}
};

/*******************************************************************************
 * PGRAPH context implementation
 ******************************************************************************/

const struct gf100_grctx_func
gf104_grctx = {
	.main  = gf100_grctx_generate_main,
	.unkn  = gf100_grctx_generate_unkn,
	.hub   = gf100_grctx_pack_hub,
	.gpc_0 = gf100_grctx_pack_gpc_0,
	.gpc_1 = gf100_grctx_pack_gpc_1,
	.zcull = gf100_grctx_pack_zcull,
	.tpc   = gf104_grctx_pack_tpc,
	.icmd  = gf100_grctx_pack_icmd,
	.mthd  = gf100_grctx_pack_mthd,
	.bundle = gf100_grctx_generate_bundle,
	.bundle_size = 0x1800,
	.pagepool = gf100_grctx_generate_pagepool,
	.pagepool_size = 0x8000,
	.attrib_cb_size = gf100_grctx_generate_attrib_cb_size,
	.attrib_cb = gf100_grctx_generate_attrib_cb,
	.attrib = gf100_grctx_generate_attrib,
	.attrib_nr_max = 0x324,
	.attrib_nr = 0x218,
	.sm_id = gf100_grctx_generate_sm_id,
	.tpc_nr = gf100_grctx_generate_tpc_nr,
	.r4060a8 = gf100_grctx_generate_r4060a8,
	.rop_mapping = gf100_grctx_generate_rop_mapping,
	.alpha_beta_tables = gf100_grctx_generate_alpha_beta_tables,
	.max_ways_evict = gf100_grctx_generate_max_ways_evict,
	.r419cb8 = gf100_grctx_generate_r419cb8,
};
