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
gm200_grctx_generate_tpcid(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	int gpc, tpc, id;

	for (tpc = 0, id = 0; tpc < 4; tpc++) {
		for (gpc = 0; gpc < gr->gpc_nr; gpc++) {
			if (tpc < gr->tpc_nr[gpc]) {
				nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x698), id);
				nvkm_wr32(device, GPC_UNIT(gpc, 0x0c10 + tpc * 4), id);
				nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x088), id);
				id++;
			}
		}
	}
}

static void
gm200_grctx_generate_rop_active_fbps(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const u32 fbp_count = nvkm_rd32(device, 0x12006c);
	nvkm_mask(device, 0x408850, 0x0000000f, fbp_count); /* zrop */
	nvkm_mask(device, 0x408958, 0x0000000f, fbp_count); /* crop */
}

void
gm200_grctx_generate_405b60(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const u32 dist_nr = DIV_ROUND_UP(gr->tpc_total, 4);
	u32 dist[TPC_MAX / 4] = {};
	u32 gpcs[GPC_MAX] = {};
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
		gpcs[gpc] |= i << (tpc * 8);
	}

	for (i = 0; i < dist_nr; i++)
		nvkm_wr32(device, 0x405b60 + (i * 4), dist[i]);
	for (i = 0; i < gr->gpc_nr; i++)
		nvkm_wr32(device, 0x405ba0 + (i * 4), gpcs[i]);
}

void
gm200_grctx_generate_main(struct gf100_gr *gr, struct gf100_grctx *info)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const struct gf100_grctx_func *grctx = gr->func->grctx;
	u32 idle_timeout, tmp;
	int i;

	gf100_gr_mmio(gr, gr->fuc_sw_ctx);

	idle_timeout = nvkm_mask(device, 0x404154, 0xffffffff, 0x00000000);

	grctx->bundle(info);
	grctx->pagepool(info);
	grctx->attrib(info);
	grctx->unkn(gr);

	gm200_grctx_generate_tpcid(gr);
	gf100_grctx_generate_r406028(gr);
	gk104_grctx_generate_r418bb8(gr);

	for (i = 0; i < 8; i++)
		nvkm_wr32(device, 0x4064d0 + (i * 0x04), 0x00000000);
	nvkm_wr32(device, 0x406500, 0x00000000);

	nvkm_wr32(device, 0x405b00, (gr->tpc_total << 8) | gr->gpc_nr);

	gm200_grctx_generate_rop_active_fbps(gr);

	for (tmp = 0, i = 0; i < gr->gpc_nr; i++)
		tmp |= ((1 << gr->tpc_nr[i]) - 1) << (i * 4);
	nvkm_wr32(device, 0x4041c4, tmp);

	gm200_grctx_generate_405b60(gr);

	gf100_gr_icmd(gr, gr->fuc_bundle);
	nvkm_wr32(device, 0x404154, idle_timeout);
	gf100_gr_mthd(gr, gr->fuc_method);

	nvkm_mask(device, 0x418e94, 0xffffffff, 0xc4230000);
	nvkm_mask(device, 0x418e4c, 0xffffffff, 0x70000000);
}

const struct gf100_grctx_func
gm200_grctx = {
	.main  = gm200_grctx_generate_main,
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
};
