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

void
gp100_grctx_generate_pagepool(struct gf100_grctx *info)
{
	const struct gf100_grctx_func *grctx = info->gr->func->grctx;
	const int s = 8;
	const int b = mmio_vram(info, grctx->pagepool_size, (1 << s), true);
	mmio_refn(info, 0x40800c, 0x00000000, s, b);
	mmio_wr32(info, 0x408010, 0x8007d800);
	mmio_refn(info, 0x419004, 0x00000000, s, b);
	mmio_wr32(info, 0x419008, 0x00000000);
}

static void
gp100_grctx_generate_attrib(struct gf100_grctx *info)
{
	struct gf100_gr *gr = info->gr;
	const struct gf100_grctx_func *grctx = gr->func->grctx;
	const u32  alpha = grctx->alpha_nr;
	const u32 attrib = grctx->attrib_nr;
	const int s = 12;
	const int max_batches = 0xffff;
	u32 size = grctx->alpha_nr_max * gr->tpc_total;
	u32 ao = 0;
	u32 bo = ao + size;
	int gpc, ppc, b, n = 0;

	for (gpc = 0; gpc < gr->gpc_nr; gpc++)
		size += grctx->attrib_nr_max * gr->ppc_nr[gpc] * gr->ppc_tpc_max;
	size = ((size * 0x20) + 128) & ~127;
	b = mmio_vram(info, size, (1 << s), false);

	mmio_refn(info, 0x418810, 0x80000000, s, b);
	mmio_refn(info, 0x419848, 0x10000000, s, b);
	mmio_refn(info, 0x419c2c, 0x10000000, s, b);
	mmio_refn(info, 0x419b00, 0x00000000, s, b);
	mmio_wr32(info, 0x419b04, 0x80000000 | size >> 7);
	mmio_wr32(info, 0x405830, attrib);
	mmio_wr32(info, 0x40585c, alpha);
	mmio_wr32(info, 0x4064c4, ((alpha / 4) << 16) | max_batches);

	for (gpc = 0; gpc < gr->gpc_nr; gpc++) {
		for (ppc = 0; ppc < gr->ppc_nr[gpc]; ppc++, n++) {
			const u32 as =  alpha * gr->ppc_tpc_nr[gpc][ppc];
			const u32 bs = attrib * gr->ppc_tpc_max;
			const u32 u = 0x418ea0 + (n * 0x04);
			const u32 o = PPC_UNIT(gpc, ppc, 0);
			if (!(gr->ppc_mask[gpc] & (1 << ppc)))
				continue;
			mmio_wr32(info, o + 0xc0, bs);
			mmio_wr32(info, o + 0xf4, bo);
			mmio_wr32(info, o + 0xf0, bs);
			bo += grctx->attrib_nr_max * gr->ppc_tpc_max;
			mmio_wr32(info, o + 0xe4, as);
			mmio_wr32(info, o + 0xf8, ao);
			ao += grctx->alpha_nr_max * gr->ppc_tpc_nr[gpc][ppc];
			mmio_wr32(info, u, bs);
		}
	}

	mmio_wr32(info, 0x418eec, 0x00000000);
	mmio_wr32(info, 0x41befc, 0x00000000);
}

void
gp100_grctx_generate_smid_config(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const u32 dist_nr = DIV_ROUND_UP(gr->tpc_total, 4);
	u32 dist[TPC_MAX / 4] = {}, gpcs[16] = {};
	u8  sm, i;

	for (sm = 0; sm < gr->sm_nr; sm++) {
		const u8 gpc = gr->sm[sm].gpc;
		const u8 tpc = gr->sm[sm].tpc;
		dist[sm / 4] |= ((gpc << 4) | tpc) << ((sm % 4) * 8);
		gpcs[gpc + (gr->func->gpc_nr * (tpc / 4))] |= sm << ((tpc % 4) * 8);
	}

	for (i = 0; i < dist_nr; i++)
		nvkm_wr32(device, 0x405b60 + (i * 4), dist[i]);
	for (i = 0; i < ARRAY_SIZE(gpcs); i++)
		nvkm_wr32(device, 0x405ba0 + (i * 4), gpcs[i]);
}

const struct gf100_grctx_func
gp100_grctx = {
	.main  = gf100_grctx_generate_main,
	.unkn  = gk104_grctx_generate_unkn,
	.bundle = gm107_grctx_generate_bundle,
	.bundle_size = 0x3000,
	.bundle_min_gpm_fifo_depth = 0x180,
	.bundle_token_limit = 0x1080,
	.pagepool = gp100_grctx_generate_pagepool,
	.pagepool_size = 0x20000,
	.attrib = gp100_grctx_generate_attrib,
	.attrib_nr_max = 0x660,
	.attrib_nr = 0x440,
	.alpha_nr_max = 0xc00,
	.alpha_nr = 0x800,
	.sm_id = gm107_grctx_generate_sm_id,
	.rop_mapping = gf117_grctx_generate_rop_mapping,
	.dist_skip_table = gm200_grctx_generate_dist_skip_table,
	.r406500 = gm200_grctx_generate_r406500,
	.gpc_tpc_nr = gk104_grctx_generate_gpc_tpc_nr,
	.tpc_mask = gm200_grctx_generate_tpc_mask,
	.smid_config = gp100_grctx_generate_smid_config,
	.r419a3c = gm200_grctx_generate_r419a3c,
};
