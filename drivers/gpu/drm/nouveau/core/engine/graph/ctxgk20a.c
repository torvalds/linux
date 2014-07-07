/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
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

#include "ctxnvc0.h"

static const struct nvc0_graph_pack
gk20a_grctx_pack_mthd[] = {
	{ nve4_grctx_init_a097_0, 0xa297 },
	{ nvc0_grctx_init_902d_0, 0x902d },
	{}
};

struct nouveau_oclass *
gk20a_grctx_oclass = &(struct nvc0_grctx_oclass) {
	.base.handle = NV_ENGCTX(GR, 0xea),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_context_ctor,
		.dtor = nvc0_graph_context_dtor,
		.init = _nouveau_graph_context_init,
		.fini = _nouveau_graph_context_fini,
		.rd32 = _nouveau_graph_context_rd32,
		.wr32 = _nouveau_graph_context_wr32,
	},
	.main  = nve4_grctx_generate_main,
	.mods  = nve4_grctx_generate_mods,
	.unkn  = nve4_grctx_generate_unkn,
	.hub   = nve4_grctx_pack_hub,
	.gpc   = nve4_grctx_pack_gpc,
	.zcull = nvc0_grctx_pack_zcull,
	.tpc   = nve4_grctx_pack_tpc,
	.ppc   = nve4_grctx_pack_ppc,
	.icmd  = nve4_grctx_pack_icmd,
	.mthd  = gk20a_grctx_pack_mthd,
}.base;
