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
gp102_grctx_generate_attrib(struct gf100_grctx *info)
{
	struct gf100_gr *gr = info->gr;
	const struct gf100_grctx_func *grctx = gr->func->grctx;
	const u32  alpha = grctx->alpha_nr;
	const u32 attrib = grctx->attrib_nr;
	const u32 pertpc = 0x20 * (grctx->attrib_nr_max + grctx->alpha_nr_max);
	const u32   size = roundup(gr->tpc_total * pertpc, 0x80);
	const int s = 12;
	const int b = mmio_vram(info, size, (1 << s), false);
	const int max_batches = 0xffff;
	u32 ao = 0;
	u32 bo = ao + grctx->alpha_nr_max * gr->tpc_total;
	int gpc, ppc, n = 0;

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
			const u32 bs = attrib * gr->ppc_tpc_nr[gpc][ppc];
			const u32 u = 0x418ea0 + (n * 0x04);
			const u32 o = PPC_UNIT(gpc, ppc, 0);
			const u32 p = GPC_UNIT(gpc, 0xc44 + (ppc * 4));
			if (!(gr->ppc_mask[gpc] & (1 << ppc)))
				continue;
			mmio_wr32(info, o + 0xc0, bs);
			mmio_wr32(info, p, bs);
			mmio_wr32(info, o + 0xf4, bo);
			mmio_wr32(info, o + 0xf0, bs);
			bo += grctx->attrib_nr_max * gr->ppc_tpc_nr[gpc][ppc];
			mmio_wr32(info, o + 0xe4, as);
			mmio_wr32(info, o + 0xf8, ao);
			ao += grctx->alpha_nr_max * gr->ppc_tpc_nr[gpc][ppc];
			mmio_wr32(info, u, bs);
		}
	}

	mmio_wr32(info, 0x4181e4, 0x00000100);
	mmio_wr32(info, 0x41befc, 0x00000100);
}

const struct gf100_grctx_func
gp102_grctx = {
	.main = gp100_grctx_generate_main,
	.unkn = gk104_grctx_generate_unkn,
	.bundle = gm107_grctx_generate_bundle,
	.bundle_size = 0x3000,
	.bundle_min_gpm_fifo_depth = 0x180,
	.bundle_token_limit = 0x900,
	.pagepool = gp100_grctx_generate_pagepool,
	.pagepool_size = 0x20000,
	.attrib = gp102_grctx_generate_attrib,
	.attrib_nr_max = 0x5d4,
	.attrib_nr = 0x320,
	.alpha_nr_max = 0xc00,
	.alpha_nr = 0x800,
};
