/*
 * Copyright 2016 Red Hat Inc.
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

#include <subdev/fb.h>

/*******************************************************************************
 * PGRAPH context implementation
 ******************************************************************************/

static void
gp102_grctx_generate_r408840(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_mask(device, 0x408840, 0x00000003, 0x00000000);
}

void
gp102_grctx_generate_attrib(struct gf100_gr_chan *chan)
{
	struct gf100_gr *gr = chan->gr;
	const struct gf100_grctx_func *grctx = gr->func->grctx;
	const u32  alpha = grctx->alpha_nr;
	const u32 attrib = grctx->attrib_nr;
	const u32   gfxp = grctx->gfxp_nr;
	const int max_batches = 0xffff;
	u32 size = grctx->alpha_nr_max * gr->tpc_total;
	u32 ao = 0;
	u32 bo = ao + size;
	int gpc, ppc, n = 0;

	gf100_grctx_patch_wr32(chan, 0x405830, attrib);
	gf100_grctx_patch_wr32(chan, 0x40585c, alpha);
	gf100_grctx_patch_wr32(chan, 0x4064c4, ((alpha / 4) << 16) | max_batches);

	for (gpc = 0; gpc < gr->gpc_nr; gpc++) {
		for (ppc = 0; ppc < gr->func->ppc_nr; ppc++, n++) {
			const u32 as =  alpha * gr->ppc_tpc_nr[gpc][ppc];
			const u32 bs = attrib * gr->ppc_tpc_max;
			const u32 gs =   gfxp * gr->ppc_tpc_max;
			const u32 u = 0x418ea0 + (n * 0x04);
			const u32 o = PPC_UNIT(gpc, ppc, 0);
			const u32 p = GPC_UNIT(gpc, 0xc44 + (ppc * 4));

			if (!(gr->ppc_mask[gpc] & (1 << ppc)))
				continue;

			gf100_grctx_patch_wr32(chan, o + 0xc0, gs);
			gf100_grctx_patch_wr32(chan, p, bs);
			gf100_grctx_patch_wr32(chan, o + 0xf4, bo);
			gf100_grctx_patch_wr32(chan, o + 0xf0, bs);
			bo += gs;
			gf100_grctx_patch_wr32(chan, o + 0xe4, as);
			gf100_grctx_patch_wr32(chan, o + 0xf8, ao);
			ao += grctx->alpha_nr_max * gr->ppc_tpc_nr[gpc][ppc];
			gf100_grctx_patch_wr32(chan, u, bs);
		}
	}

	gf100_grctx_patch_wr32(chan, 0x4181e4, 0x00000100);
	gf100_grctx_patch_wr32(chan, 0x41befc, 0x00000100);
}

u32
gp102_grctx_generate_attrib_cb_size(struct gf100_gr *gr)
{
	const struct gf100_grctx_func *grctx = gr->func->grctx;
	u32 size = grctx->alpha_nr_max * gr->tpc_total;
	int gpc;

	for (gpc = 0; gpc < gr->gpc_nr; gpc++)
		size += grctx->gfxp_nr * gr->func->ppc_nr * gr->ppc_tpc_max;

	return ((size * 0x20) + 127) & ~127;
}

const struct gf100_grctx_func
gp102_grctx = {
	.main = gf100_grctx_generate_main,
	.unkn = gk104_grctx_generate_unkn,
	.bundle = gm107_grctx_generate_bundle,
	.bundle_size = 0x3000,
	.bundle_min_gpm_fifo_depth = 0x180,
	.bundle_token_limit = 0x900,
	.pagepool = gp100_grctx_generate_pagepool,
	.pagepool_size = 0x20000,
	.attrib_cb_size = gp102_grctx_generate_attrib_cb_size,
	.attrib_cb = gp100_grctx_generate_attrib_cb,
	.attrib = gp102_grctx_generate_attrib,
	.attrib_nr_max = 0x4b0,
	.attrib_nr = 0x320,
	.alpha_nr_max = 0xc00,
	.alpha_nr = 0x800,
	.gfxp_nr = 0xba8,
	.sm_id = gm107_grctx_generate_sm_id,
	.rop_mapping = gf117_grctx_generate_rop_mapping,
	.dist_skip_table = gm200_grctx_generate_dist_skip_table,
	.r406500 = gm200_grctx_generate_r406500,
	.gpc_tpc_nr = gk104_grctx_generate_gpc_tpc_nr,
	.tpc_mask = gm200_grctx_generate_tpc_mask,
	.smid_config = gp100_grctx_generate_smid_config,
	.r419a3c = gm200_grctx_generate_r419a3c,
	.r408840 = gp102_grctx_generate_r408840,
};
