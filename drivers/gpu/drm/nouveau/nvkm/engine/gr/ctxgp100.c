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
	mmio_wr32(info, 0x408010, 0x80000000);
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
			if (!(gr->ppc_mask[gpc] & (1 << ppc)))
				continue;
			mmio_wr32(info, o + 0xc0, bs);
			mmio_wr32(info, o + 0xf4, bo);
			mmio_wr32(info, o + 0xf0, bs);
			bo += grctx->attrib_nr_max * gr->ppc_tpc_nr[gpc][ppc];
			mmio_wr32(info, o + 0xe4, as);
			mmio_wr32(info, o + 0xf8, ao);
			ao += grctx->alpha_nr_max * gr->ppc_tpc_nr[gpc][ppc];
			mmio_wr32(info, u, bs);
		}
	}

	mmio_wr32(info, 0x418eec, 0x00000000);
	mmio_wr32(info, 0x41befc, 0x00000000);
}

static void
gp100_grctx_generate_405b60(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const u32 dist_nr = DIV_ROUND_UP(gr->tpc_total, 4);
	u32 dist[TPC_MAX / 4] = {};
	u32 gpcs[GPC_MAX * 2] = {};
	u8  tpcnr[GPC_MAX];
	int tpc, gpc, i;

	memcpy(tpcnr, gr->tpc_nr, sizeof(gr->tpc_nr));

	/* won't result in the same distribution as the binary driver where
	 * some of the gpcs have more tpcs than others, but this shall do
	 * for the moment.  the code for earlier gpus has this issue too.
	 */
	for (gpc = -1, i = 0; i < gr->tpc_total; i++) {
		do {
			gpc = (gpc + 1) % gr->gpc_nr;
		} while(!tpcnr[gpc]);
		tpc = gr->tpc_nr[gpc] - tpcnr[gpc]--;

		dist[i / 4] |= ((gpc << 4) | tpc) << ((i % 4) * 8);
		gpcs[gpc + (gr->gpc_nr * (tpc / 4))] |= i << (tpc * 8);
	}

	for (i = 0; i < dist_nr; i++)
		nvkm_wr32(device, 0x405b60 + (i * 4), dist[i]);
	for (i = 0; i < gr->gpc_nr * 2; i++)
		nvkm_wr32(device, 0x405ba0 + (i * 4), gpcs[i]);
}

void
gp100_grctx_generate_main(struct gf100_gr *gr, struct gf100_grctx *info)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const struct gf100_grctx_func *grctx = gr->func->grctx;
	u32 idle_timeout, tmp;
	int i;

	gf100_gr_mmio(gr, gr->fuc_sw_ctx);

	idle_timeout = nvkm_mask(device, 0x404154, 0xffffffff, 0x00000000);

	grctx->pagepool(info);
	grctx->bundle(info);
	grctx->attrib(info);
	grctx->unkn(gr);

	gm200_grctx_generate_tpcid(gr);
	gf100_grctx_generate_r406028(gr);
	gk104_grctx_generate_r418bb8(gr);

	for (i = 0; i < 8; i++)
		nvkm_wr32(device, 0x4064d0 + (i * 0x04), 0x00000000);
	nvkm_wr32(device, 0x406500, 0x00000000);

	nvkm_wr32(device, 0x405b00, (gr->tpc_total << 8) | gr->gpc_nr);

	for (tmp = 0, i = 0; i < gr->gpc_nr; i++)
		tmp |= ((1 << gr->tpc_nr[i]) - 1) << (i * 5);
	nvkm_wr32(device, 0x4041c4, tmp);

	gp100_grctx_generate_405b60(gr);

	gf100_gr_icmd(gr, gr->fuc_bundle);
	nvkm_wr32(device, 0x404154, idle_timeout);
	gf100_gr_mthd(gr, gr->fuc_method);
}

const struct gf100_grctx_func
gp100_grctx = {
	.main  = gp100_grctx_generate_main,
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
};
