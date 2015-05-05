/*
 * Copyright 2015 Red Hat Inc.
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

static const struct gf100_gr_init
gm206_grctx_init_gpc_unk_1[] = {
	{ 0x418600,   1, 0x04, 0x0000007f },
	{ 0x418684,   1, 0x04, 0x0000001f },
	{ 0x418700,   1, 0x04, 0x00000002 },
	{ 0x418704,   1, 0x04, 0x00000080 },
	{ 0x418708,   1, 0x04, 0x40000000 },
	{ 0x41870c,   2, 0x04, 0x00000000 },
	{ 0x418728,   1, 0x04, 0x00300020 },
	{}
};

static const struct gf100_gr_pack
gm206_grctx_pack_gpc[] = {
	{ gm107_grctx_init_gpc_unk_0 },
	{ gm204_grctx_init_prop_0 },
	{ gm206_grctx_init_gpc_unk_1 },
	{ gm204_grctx_init_setup_0 },
	{ gf100_grctx_init_zcull_0 },
	{ gk208_grctx_init_crstr_0 },
	{ gm204_grctx_init_gpm_0 },
	{ gm204_grctx_init_gpc_unk_2 },
	{ gf100_grctx_init_gcc_0 },
	{}
};

struct nvkm_oclass *
gm206_grctx_oclass = &(struct gf100_grctx_oclass) {
	.base.handle = NV_ENGCTX(GR, 0x26),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gf100_gr_context_ctor,
		.dtor = gf100_gr_context_dtor,
		.init = _nvkm_gr_context_init,
		.fini = _nvkm_gr_context_fini,
		.rd32 = _nvkm_gr_context_rd32,
		.wr32 = _nvkm_gr_context_wr32,
	},
	.main  = gm204_grctx_generate_main,
	.unkn  = gk104_grctx_generate_unkn,
	.hub   = gm204_grctx_pack_hub,
	.gpc   = gm206_grctx_pack_gpc,
	.zcull = gf100_grctx_pack_zcull,
	.tpc   = gm204_grctx_pack_tpc,
	.ppc   = gm204_grctx_pack_ppc,
	.icmd  = gm204_grctx_pack_icmd,
	.mthd  = gm204_grctx_pack_mthd,
	.bundle = gm107_grctx_generate_bundle,
	.bundle_size = 0x3000,
	.bundle_min_gpm_fifo_depth = 0x180,
	.bundle_token_limit = 0x780,
	.pagepool = gm107_grctx_generate_pagepool,
	.pagepool_size = 0x20000,
	.attrib = gm107_grctx_generate_attrib,
	.attrib_nr_max = 0x600,
	.attrib_nr = 0x400,
	.alpha_nr_max = 0x1800,
	.alpha_nr = 0x1000,
}.base;
