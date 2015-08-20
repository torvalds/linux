/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "ctxgf100.h"

static void
gm20b_grctx_generate_r406028(struct gf100_gr *gr)
{
	u32 tpc_per_gpc = 0;
	int i;

	for (i = 0; i < gr->gpc_nr; i++)
		tpc_per_gpc |= gr->tpc_nr[i] << (4 * i);

	nv_wr32(gr, 0x406028, tpc_per_gpc);
	nv_wr32(gr, 0x405870, tpc_per_gpc);
}

static void
gm20b_grctx_generate_main(struct gf100_gr *gr, struct gf100_grctx *info)
{
	struct gf100_grctx_oclass *oclass = (void *)nv_engine(gr)->cclass;
	int idle_timeout_save;
	int i, tmp;

	gf100_gr_mmio(gr, gr->fuc_sw_ctx);

	gf100_gr_wait_idle(gr);

	idle_timeout_save = nv_rd32(gr, 0x404154);
	nv_wr32(gr, 0x404154, 0x00000000);

	oclass->attrib(info);

	oclass->unkn(gr);

	gm204_grctx_generate_tpcid(gr);
	gm20b_grctx_generate_r406028(gr);
	gk104_grctx_generate_r418bb8(gr);

	for (i = 0; i < 8; i++)
		nv_wr32(gr, 0x4064d0 + (i * 0x04), 0x00000000);

	nv_wr32(gr, 0x405b00, (gr->tpc_total << 8) | gr->gpc_nr);

	gk104_grctx_generate_rop_active_fbps(gr);
	nv_wr32(gr, 0x408908, nv_rd32(gr, 0x410108) | 0x80000000);

	for (tmp = 0, i = 0; i < gr->gpc_nr; i++)
		tmp |= ((1 << gr->tpc_nr[i]) - 1) << (i * 4);
	nv_wr32(gr, 0x4041c4, tmp);

	gm204_grctx_generate_405b60(gr);

	gf100_gr_wait_idle(gr);

	nv_wr32(gr, 0x404154, idle_timeout_save);
	gf100_gr_wait_idle(gr);

	gf100_gr_mthd(gr, gr->fuc_method);
	gf100_gr_wait_idle(gr);

	gf100_gr_icmd(gr, gr->fuc_bundle);
	oclass->pagepool(info);
	oclass->bundle(info);
}

struct nvkm_oclass *
gm20b_grctx_oclass = &(struct gf100_grctx_oclass) {
	.base.handle = NV_ENGCTX(GR, 0x2b),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gf100_gr_context_ctor,
		.dtor = gf100_gr_context_dtor,
		.init = _nvkm_gr_context_init,
		.fini = _nvkm_gr_context_fini,
		.rd32 = _nvkm_gr_context_rd32,
		.wr32 = _nvkm_gr_context_wr32,
	},
	.main  = gm20b_grctx_generate_main,
	.unkn  = gk104_grctx_generate_unkn,
	.bundle = gm107_grctx_generate_bundle,
	.bundle_size = 0x1800,
	.bundle_min_gpm_fifo_depth = 0x182,
	.bundle_token_limit = 0x1c0,
	.pagepool = gm107_grctx_generate_pagepool,
	.pagepool_size = 0x8000,
	.attrib = gm107_grctx_generate_attrib,
	.attrib_nr_max = 0x600,
	.attrib_nr = 0x400,
	.alpha_nr_max = 0xc00,
	.alpha_nr = 0x800,
}.base;
