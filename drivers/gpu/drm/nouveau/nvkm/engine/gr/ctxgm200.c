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

/*******************************************************************************
 * PGRAPH context implementation
 ******************************************************************************/

void
gm200_grctx_generate_r419a3c(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_mask(device, 0x419a3c, 0x00000014, 0x00000000);
}

static void
gm200_grctx_generate_r418e94(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_mask(device, 0x418e94, 0xffffffff, 0xc4230000);
	nvkm_mask(device, 0x418e4c, 0xffffffff, 0x70000000);
}

void
gm200_grctx_generate_smid_config(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const u32 dist_nr = DIV_ROUND_UP(gr->tpc_total, 4);
	u32 dist[TPC_MAX / 4] = {};
	u32 gpcs[GPC_MAX] = {};
	u8  sm, i;

	for (sm = 0; sm < gr->sm_nr; sm++) {
		const u8 gpc = gr->sm[sm].gpc;
		const u8 tpc = gr->sm[sm].tpc;
		dist[sm / 4] |= ((gpc << 4) | tpc) << ((sm % 4) * 8);
		gpcs[gpc] |= sm << (tpc * 8);
	}

	for (i = 0; i < dist_nr; i++)
		nvkm_wr32(device, 0x405b60 + (i * 4), dist[i]);
	for (i = 0; i < gr->gpc_nr; i++)
		nvkm_wr32(device, 0x405ba0 + (i * 4), gpcs[i]);
}

void
gm200_grctx_generate_tpc_mask(struct gf100_gr *gr)
{
	u32 tmp, i;
	for (tmp = 0, i = 0; i < gr->gpc_nr; i++)
		tmp |= ((1 << gr->tpc_nr[i]) - 1) << (i * gr->func->tpc_nr);
	nvkm_wr32(gr->base.engine.subdev.device, 0x4041c4, tmp);
}

void
gm200_grctx_generate_r406500(struct gf100_gr *gr)
{
	nvkm_wr32(gr->base.engine.subdev.device, 0x406500, 0x00000000);
}

void
gm200_grctx_generate_dist_skip_table(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	u32 data[8] = {};
	int gpc, ppc, i;

	for (gpc = 0; gpc < gr->gpc_nr; gpc++) {
		for (ppc = 0; ppc < gr->ppc_nr[gpc]; ppc++) {
			u8 ppc_tpcs = gr->ppc_tpc_nr[gpc][ppc];
			u8 ppc_tpcm = gr->ppc_tpc_mask[gpc][ppc];
			while (ppc_tpcs-- > gr->ppc_tpc_min)
				ppc_tpcm &= ppc_tpcm - 1;
			ppc_tpcm ^= gr->ppc_tpc_mask[gpc][ppc];
			((u8 *)data)[gpc] |= ppc_tpcm;
		}
	}

	for (i = 0; i < ARRAY_SIZE(data); i++)
		nvkm_wr32(device, 0x4064d0 + (i * 0x04), data[i]);
}

const struct gf100_grctx_func
gm200_grctx = {
	.main  = gf100_grctx_generate_main,
	.unkn  = gk104_grctx_generate_unkn,
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
	.sm_id = gm107_grctx_generate_sm_id,
	.rop_mapping = gf117_grctx_generate_rop_mapping,
	.dist_skip_table = gm200_grctx_generate_dist_skip_table,
	.r406500 = gm200_grctx_generate_r406500,
	.gpc_tpc_nr = gk104_grctx_generate_gpc_tpc_nr,
	.tpc_mask = gm200_grctx_generate_tpc_mask,
	.smid_config = gm200_grctx_generate_smid_config,
	.r418e94 = gm200_grctx_generate_r418e94,
	.r419a3c = gm200_grctx_generate_r419a3c,
};
