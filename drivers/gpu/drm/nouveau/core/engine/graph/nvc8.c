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

struct nouveau_oclass
nvc8_graph_sclass[] = {
	{ 0x902d, &nouveau_object_ofuncs },
	{ 0x9039, &nouveau_object_ofuncs },
	{ FERMI_A, &nvc0_fermi_ofuncs, nvc0_graph_9097_omthds },
	{ FERMI_B, &nvc0_fermi_ofuncs, nvc0_graph_9097_omthds },
	{ FERMI_C, &nvc0_fermi_ofuncs, nvc0_graph_9097_omthds },
	{ FERMI_COMPUTE_A, &nouveau_object_ofuncs, nvc0_graph_90c0_omthds },
	{}
};

/*******************************************************************************
 * PGRAPH register lists
 ******************************************************************************/

static const struct nvc0_graph_init
nvc8_graph_init_sm_0[] = {
	{ 0x419e00,   1, 0x04, 0x00000000 },
	{ 0x419ea0,   1, 0x04, 0x00000000 },
	{ 0x419ea4,   1, 0x04, 0x00000100 },
	{ 0x419ea8,   1, 0x04, 0x00001100 },
	{ 0x419eac,   1, 0x04, 0x11100f02 },
	{ 0x419eb0,   1, 0x04, 0x00000003 },
	{ 0x419eb4,   4, 0x04, 0x00000000 },
	{ 0x419ec8,   1, 0x04, 0x06060618 },
	{ 0x419ed0,   1, 0x04, 0x0eff0e38 },
	{ 0x419ed4,   1, 0x04, 0x011104f1 },
	{ 0x419edc,   1, 0x04, 0x00000000 },
	{ 0x419f00,   1, 0x04, 0x00000000 },
	{ 0x419f2c,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_pack
nvc8_graph_pack_mmio[] = {
	{ nvc0_graph_init_main_0 },
	{ nvc0_graph_init_fe_0 },
	{ nvc0_graph_init_pri_0 },
	{ nvc0_graph_init_rstr2d_0 },
	{ nvc0_graph_init_pd_0 },
	{ nvc0_graph_init_ds_0 },
	{ nvc0_graph_init_scc_0 },
	{ nvc0_graph_init_prop_0 },
	{ nvc0_graph_init_gpc_unk_0 },
	{ nvc0_graph_init_setup_0 },
	{ nvc0_graph_init_crstr_0 },
	{ nvc1_graph_init_setup_1 },
	{ nvc0_graph_init_zcull_0 },
	{ nvc0_graph_init_gpm_0 },
	{ nvc0_graph_init_gpc_unk_1 },
	{ nvc0_graph_init_gcc_0 },
	{ nvc0_graph_init_tpccs_0 },
	{ nvc0_graph_init_tex_0 },
	{ nvc0_graph_init_pe_0 },
	{ nvc0_graph_init_l1c_0 },
	{ nvc0_graph_init_wwdx_0 },
	{ nvc0_graph_init_tpccs_1 },
	{ nvc0_graph_init_mpc_0 },
	{ nvc8_graph_init_sm_0 },
	{ nvc0_graph_init_be_0 },
	{ nvc0_graph_init_fe_1 },
	{ nvc0_graph_init_pe_1 },
	{}
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

struct nouveau_oclass *
nvc8_graph_oclass = &(struct nvc0_graph_oclass) {
	.base.handle = NV_ENGINE(GR, 0xc8),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_ctor,
		.dtor = nvc0_graph_dtor,
		.init = nvc0_graph_init,
		.fini = _nouveau_graph_fini,
	},
	.cclass = &nvc8_grctx_oclass,
	.sclass = nvc8_graph_sclass,
	.mmio = nvc8_graph_pack_mmio,
	.fecs.ucode = &nvc0_graph_fecs_ucode,
	.gpccs.ucode = &nvc0_graph_gpccs_ucode,
}.base;
