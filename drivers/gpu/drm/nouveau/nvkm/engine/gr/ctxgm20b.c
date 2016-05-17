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
	struct nvkm_device *device = gr->base.engine.subdev.device;
	u32 tpc_per_gpc = 0;
	int i;

	for (i = 0; i < gr->gpc_nr; i++)
		tpc_per_gpc |= gr->tpc_nr[i] << (4 * i);

	nvkm_wr32(device, 0x406028, tpc_per_gpc);
	nvkm_wr32(device, 0x405870, tpc_per_gpc);
}

static void
gm20b_grctx_generate_main(struct gf100_gr *gr, struct gf100_grctx *info)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const struct gf100_grctx_func *grctx = gr->func->grctx;
	int idle_timeout_save;
	int i, tmp;

	gf100_gr_mmio(gr, gr->fuc_sw_ctx);

	gf100_gr_wait_idle(gr);

	idle_timeout_save = nvkm_rd32(device, 0x404154);
	nvkm_wr32(device, 0x404154, 0x00000000);

	grctx->attrib(info);

	grctx->unkn(gr);

	gm200_grctx_generate_tpcid(gr);
	gm20b_grctx_generate_r406028(gr);
	gk104_grctx_generate_r418bb8(gr);

	for (i = 0; i < 8; i++)
		nvkm_wr32(device, 0x4064d0 + (i * 0x04), 0x00000000);

	nvkm_wr32(device, 0x405b00, (gr->tpc_total << 8) | gr->gpc_nr);

	gk104_grctx_generate_rop_active_fbps(gr);
	nvkm_wr32(device, 0x408908, nvkm_rd32(device, 0x410108) | 0x80000000);

	for (tmp = 0, i = 0; i < gr->gpc_nr; i++)
		tmp |= ((1 << gr->tpc_nr[i]) - 1) << (i * 4);
	nvkm_wr32(device, 0x4041c4, tmp);

	gm200_grctx_generate_405b60(gr);

	gf100_gr_wait_idle(gr);

	nvkm_wr32(device, 0x404154, idle_timeout_save);
	gf100_gr_wait_idle(gr);

	gf100_gr_mthd(gr, gr->fuc_method);
	gf100_gr_wait_idle(gr);

	gf100_gr_icmd(gr, gr->fuc_bundle);
	grctx->pagepool(info);
	grctx->bundle(info);
}

const struct gf100_grctx_func
gm20b_grctx = {
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
};
