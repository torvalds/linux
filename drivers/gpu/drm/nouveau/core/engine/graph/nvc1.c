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
 * Graphics object classes
 ******************************************************************************/

static struct nouveau_oclass
nvc1_graph_sclass[] = {
	{ 0x902d, &nouveau_object_ofuncs },
	{ 0x9039, &nouveau_object_ofuncs },
	{ FERMI_A, &nvc0_fermi_ofuncs, nvc0_graph_9097_omthds },
	{ FERMI_B, &nvc0_fermi_ofuncs, nvc0_graph_9097_omthds },
	{ FERMI_COMPUTE_A, &nouveau_object_ofuncs, nvc0_graph_90c0_omthds },
	{}
};

/*******************************************************************************
 * PGRAPH register lists
 ******************************************************************************/

const struct nvc0_graph_init
nvc1_graph_init_gpc_unk_0[] = {
	{ 0x418604,   1, 0x04, 0x00000000 },
	{ 0x418680,   1, 0x04, 0x00000000 },
	{ 0x418714,   1, 0x04, 0x00000000 },
	{ 0x418384,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc1_graph_init_setup_1[] = {
	{ 0x4188c8,   2, 0x04, 0x00000000 },
	{ 0x4188d0,   1, 0x04, 0x00010000 },
	{ 0x4188d4,   1, 0x04, 0x00000001 },
	{}
};

static const struct nvc0_graph_init
nvc1_graph_init_gpc_unk_1[] = {
	{ 0x418d00,   1, 0x04, 0x00000000 },
	{ 0x418f08,   1, 0x04, 0x00000000 },
	{ 0x418e00,   1, 0x04, 0x00000003 },
	{ 0x418e08,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
nvc1_graph_init_pe_0[] = {
	{ 0x41980c,   1, 0x04, 0x00000010 },
	{ 0x419810,   1, 0x04, 0x00000000 },
	{ 0x419814,   1, 0x04, 0x00000004 },
	{ 0x419844,   1, 0x04, 0x00000000 },
	{ 0x41984c,   1, 0x04, 0x00005bc5 },
	{ 0x419850,   4, 0x04, 0x00000000 },
	{ 0x419880,   1, 0x04, 0x00000002 },
	{}
};

static const struct nvc0_graph_pack
nvc1_graph_pack_mmio[] = {
	{ nvc0_graph_init_main_0 },
	{ nvc0_graph_init_fe_0 },
	{ nvc0_graph_init_pri_0 },
	{ nvc0_graph_init_rstr2d_0 },
	{ nvc0_graph_init_pd_0 },
	{ nvc4_graph_init_ds_0 },
	{ nvc0_graph_init_scc_0 },
	{ nvc0_graph_init_prop_0 },
	{ nvc1_graph_init_gpc_unk_0 },
	{ nvc0_graph_init_setup_0 },
	{ nvc0_graph_init_crstr_0 },
	{ nvc1_graph_init_setup_1 },
	{ nvc0_graph_init_zcull_0 },
	{ nvc0_graph_init_gpm_0 },
	{ nvc1_graph_init_gpc_unk_1 },
	{ nvc0_graph_init_gcc_0 },
	{ nvc0_graph_init_tpccs_0 },
	{ nvc4_graph_init_tex_0 },
	{ nvc1_graph_init_pe_0 },
	{ nvc0_graph_init_l1c_0 },
	{ nvc0_graph_init_wwdx_0 },
	{ nvc0_graph_init_tpccs_1 },
	{ nvc0_graph_init_mpc_0 },
	{ nvc4_graph_init_sm_0 },
	{ nvc0_graph_init_be_0 },
	{ nvc0_graph_init_fe_1 },
	{}
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

struct nouveau_oclass *
nvc1_graph_oclass = &(struct nvc0_graph_oclass) {
	.base.handle = NV_ENGINE(GR, 0xc1),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_ctor,
		.dtor = nvc0_graph_dtor,
		.init = nvc0_graph_init,
		.fini = _nouveau_graph_fini,
	},
	.cclass = &nvc1_grctx_oclass,
	.sclass = nvc1_graph_sclass,
	.mmio = nvc1_graph_pack_mmio,
	.fecs.ucode = &nvc0_graph_fecs_ucode,
	.gpccs.ucode = &nvc0_graph_gpccs_ucode,
}.base;
