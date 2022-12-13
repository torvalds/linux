/*
 * Copyright 2019 Red Hat Inc.
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
 */
#include "ctxgf100.h"

static void
ga102_grctx_generate_sm_id(struct gf100_gr *gr, int gpc, int tpc, int sm)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;

	tpc = gv100_gr_nonpes_aware_tpc(gr, gpc, tpc);

	nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x608), sm);
}

static void
ga102_grctx_generate_unkn(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;

	nvkm_mask(device, 0x41980c, 0x00000010, 0x00000010);
	nvkm_mask(device, 0x41be08, 0x00000004, 0x00000004);
}

static void
ga102_grctx_generate_r419ea8(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;

	nvkm_wr32(device, 0x419ea8, nvkm_rd32(device, 0x504728) | 0x08000000);
}

const struct gf100_grctx_func
ga102_grctx = {
	.main = gf100_grctx_generate_main,
	.unkn = ga102_grctx_generate_unkn,
	.bundle = gm107_grctx_generate_bundle,
	.bundle_size = 0x3000,
	.bundle_min_gpm_fifo_depth = 0x180,
	.bundle_token_limit = 0x1140,
	.pagepool = gp100_grctx_generate_pagepool,
	.pagepool_size = 0x20000,
	.attrib_cb_size = gp102_grctx_generate_attrib_cb_size,
	.attrib_cb = gv100_grctx_generate_attrib_cb,
	.attrib = gv100_grctx_generate_attrib,
	.attrib_nr_max = 0x800,
	.attrib_nr = 0x4a1,
	.alpha_nr_max = 0xc00,
	.alpha_nr = 0x800,
	.unknown_size = 0x80000,
	.unknown = tu102_grctx_generate_unknown,
	.gfxp_nr = 0xd28,
	.sm_id = ga102_grctx_generate_sm_id,
	.skip_pd_num_tpc_per_gpc = true,
	.rop_mapping = gv100_grctx_generate_rop_mapping,
	.r406500 = gm200_grctx_generate_r406500,
	.r400088 = gv100_grctx_generate_r400088,
	.r419ea8 = ga102_grctx_generate_r419ea8,
};
