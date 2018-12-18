/*
 * Copyright (c) 2014-2015, NVIDIA CORPORATION. All rights reserved.
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
#include "gf100.h"

#include <subdev/mc.h>

static void
gk20a_grctx_generate_main(struct gf100_gr *gr, struct gf100_grctx *info)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const struct gf100_grctx_func *grctx = gr->func->grctx;
	u32 idle_timeout;
	int i;

	gf100_gr_mmio(gr, gr->fuc_sw_ctx);

	gf100_gr_wait_idle(gr);

	idle_timeout = nvkm_mask(device, 0x404154, 0xffffffff, 0x00000000);

	grctx->attrib(info);

	grctx->unkn(gr);

	gf100_grctx_generate_floorsweep(gr);

	for (i = 0; i < 8; i++)
		nvkm_wr32(device, 0x4064d0 + (i * 0x04), 0x00000000);

	nvkm_wr32(device, 0x405b00, (gr->tpc_total << 8) | gr->gpc_nr);

	nvkm_mask(device, 0x5044b0, 0x08000000, 0x08000000);

	gf100_gr_wait_idle(gr);

	nvkm_wr32(device, 0x404154, idle_timeout);
	gf100_gr_wait_idle(gr);

	gf100_gr_mthd(gr, gr->fuc_method);
	gf100_gr_wait_idle(gr);

	gf100_gr_icmd(gr, gr->fuc_bundle);
	grctx->pagepool(info);
	grctx->bundle(info);
}

const struct gf100_grctx_func
gk20a_grctx = {
	.main  = gk20a_grctx_generate_main,
	.unkn  = gk104_grctx_generate_unkn,
	.bundle = gk104_grctx_generate_bundle,
	.bundle_size = 0x1800,
	.bundle_min_gpm_fifo_depth = 0x62,
	.bundle_token_limit = 0x100,
	.pagepool = gk104_grctx_generate_pagepool,
	.pagepool_size = 0x8000,
	.attrib = gf117_grctx_generate_attrib,
	.attrib_nr_max = 0x240,
	.attrib_nr = 0x240,
	.alpha_nr_max = 0x648 + (0x648 / 2),
	.alpha_nr = 0x648,
	.sm_id = gf100_grctx_generate_sm_id,
	.tpc_nr = gf100_grctx_generate_tpc_nr,
	.rop_mapping = gf117_grctx_generate_rop_mapping,
	.alpha_beta_tables = gk104_grctx_generate_alpha_beta_tables,
};
