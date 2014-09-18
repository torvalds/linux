/*
 * Copyright 2013 Red Hat Inc.
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

#include "nvc0.h"
#include "ctxnvc0.h"

/*******************************************************************************
 * PGRAPH register lists
 ******************************************************************************/

static const struct nvc0_graph_init
nvd7_graph_init_pe_0[] = {
	{ 0x41980c,   1, 0x04, 0x00000010 },
	{ 0x419844,   1, 0x04, 0x00000000 },
	{ 0x41984c,   1, 0x04, 0x00005bc8 },
	{ 0x419850,   3, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvd7_graph_init_pes_0[] = {
	{ 0x41be04,   1, 0x04, 0x00000000 },
	{ 0x41be08,   1, 0x04, 0x00000004 },
	{ 0x41be0c,   1, 0x04, 0x00000000 },
	{ 0x41be10,   1, 0x04, 0x003b8bc7 },
	{ 0x41be14,   2, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvd7_graph_init_wwdx_0[] = {
	{ 0x41bfd4,   1, 0x04, 0x00800000 },
	{ 0x41bfdc,   1, 0x04, 0x00000000 },
	{ 0x41bff8,   2, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvd7_graph_init_cbm_0[] = {
	{ 0x41becc,   1, 0x04, 0x00000000 },
	{ 0x41bee8,   2, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_pack
nvd7_graph_pack_mmio[] = {
	{ nvc0_graph_init_main_0 },
	{ nvc0_graph_init_fe_0 },
	{ nvc0_graph_init_pri_0 },
	{ nvc0_graph_init_rstr2d_0 },
	{ nvd9_graph_init_pd_0 },
	{ nvd9_graph_init_ds_0 },
	{ nvc0_graph_init_scc_0 },
	{ nvd9_graph_init_prop_0 },
	{ nvc1_graph_init_gpc_unk_0 },
	{ nvc0_graph_init_setup_0 },
	{ nvc0_graph_init_crstr_0 },
	{ nvc1_graph_init_setup_1 },
	{ nvc0_graph_init_zcull_0 },
	{ nvd9_graph_init_gpm_0 },
	{ nvd9_graph_init_gpc_unk_1 },
	{ nvc0_graph_init_gcc_0 },
	{ nvc0_graph_init_tpccs_0 },
	{ nvd9_graph_init_tex_0 },
	{ nvd7_graph_init_pe_0 },
	{ nvc0_graph_init_l1c_0 },
	{ nvc0_graph_init_mpc_0 },
	{ nvd9_graph_init_sm_0 },
	{ nvd7_graph_init_pes_0 },
	{ nvd7_graph_init_wwdx_0 },
	{ nvd7_graph_init_cbm_0 },
	{ nvc0_graph_init_be_0 },
	{ nvd9_graph_init_fe_1 },
	{}
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

#include "fuc/hubnvd7.fuc.h"

struct nvc0_graph_ucode
nvd7_graph_fecs_ucode = {
	.code.data = nvd7_grhub_code,
	.code.size = sizeof(nvd7_grhub_code),
	.data.data = nvd7_grhub_data,
	.data.size = sizeof(nvd7_grhub_data),
};

#include "fuc/gpcnvd7.fuc.h"

struct nvc0_graph_ucode
nvd7_graph_gpccs_ucode = {
	.code.data = nvd7_grgpc_code,
	.code.size = sizeof(nvd7_grgpc_code),
	.data.data = nvd7_grgpc_data,
	.data.size = sizeof(nvd7_grgpc_data),
};

struct nouveau_oclass *
nvd7_graph_oclass = &(struct nvc0_graph_oclass) {
	.base.handle = NV_ENGINE(GR, 0xd7),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_ctor,
		.dtor = nvc0_graph_dtor,
		.init = nvc0_graph_init,
		.fini = _nouveau_graph_fini,
	},
	.cclass = &nvd7_grctx_oclass,
	.sclass = nvc8_graph_sclass,
	.mmio = nvd7_graph_pack_mmio,
	.fecs.ucode = &nvd7_graph_fecs_ucode,
	.gpccs.ucode = &nvd7_graph_gpccs_ucode,
	.ppc_nr = 1,
}.base;
