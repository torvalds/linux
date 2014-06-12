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
nvf0_graph_sclass[] = {
	{ 0x902d, &nouveau_object_ofuncs },
	{ 0xa140, &nouveau_object_ofuncs },
	{ 0xa197, &nouveau_object_ofuncs },
	{ 0xa1c0, &nouveau_object_ofuncs },
	{}
};

/*******************************************************************************
 * PGRAPH register lists
 ******************************************************************************/

const struct nvc0_graph_init
nvf0_graph_init_fe_0[] = {
	{ 0x40415c,   1, 0x04, 0x00000000 },
	{ 0x404170,   1, 0x04, 0x00000000 },
	{ 0x4041b4,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvf0_graph_init_ds_0[] = {
	{ 0x405844,   1, 0x04, 0x00ffffff },
	{ 0x405850,   1, 0x04, 0x00000000 },
	{ 0x405900,   1, 0x04, 0x0000ff00 },
	{ 0x405908,   1, 0x04, 0x00000000 },
	{ 0x405928,   2, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvf0_graph_init_sked_0[] = {
	{ 0x407010,   1, 0x04, 0x00000000 },
	{ 0x407040,   1, 0x04, 0x80440424 },
	{ 0x407048,   1, 0x04, 0x0000000a },
	{}
};

const struct nvc0_graph_init
nvf0_graph_init_cwd_0[] = {
	{ 0x405b44,   1, 0x04, 0x00000000 },
	{ 0x405b50,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvf0_graph_init_gpc_unk_1[] = {
	{ 0x418d00,   1, 0x04, 0x00000000 },
	{ 0x418d28,   2, 0x04, 0x00000000 },
	{ 0x418f00,   1, 0x04, 0x00000400 },
	{ 0x418f08,   1, 0x04, 0x00000000 },
	{ 0x418f20,   2, 0x04, 0x00000000 },
	{ 0x418e00,   1, 0x04, 0x00000000 },
	{ 0x418e08,   1, 0x04, 0x00000000 },
	{ 0x418e1c,   2, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvf0_graph_init_tex_0[] = {
	{ 0x419ab0,   1, 0x04, 0x00000000 },
	{ 0x419ac8,   1, 0x04, 0x00000000 },
	{ 0x419ab8,   1, 0x04, 0x000000e7 },
	{ 0x419aec,   1, 0x04, 0x00000000 },
	{ 0x419abc,   2, 0x04, 0x00000000 },
	{ 0x419ab4,   1, 0x04, 0x00000000 },
	{ 0x419aa8,   2, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
nvf0_graph_init_l1c_0[] = {
	{ 0x419c98,   1, 0x04, 0x00000000 },
	{ 0x419ca8,   1, 0x04, 0x00000000 },
	{ 0x419cb0,   1, 0x04, 0x01000000 },
	{ 0x419cb4,   1, 0x04, 0x00000000 },
	{ 0x419cb8,   1, 0x04, 0x00b08bea },
	{ 0x419c84,   1, 0x04, 0x00010384 },
	{ 0x419cbc,   1, 0x04, 0x281b3646 },
	{ 0x419cc0,   2, 0x04, 0x00000000 },
	{ 0x419c80,   1, 0x04, 0x00020230 },
	{ 0x419ccc,   2, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvf0_graph_init_sm_0[] = {
	{ 0x419e00,   1, 0x04, 0x00000080 },
	{ 0x419ea0,   1, 0x04, 0x00000000 },
	{ 0x419ee4,   1, 0x04, 0x00000000 },
	{ 0x419ea4,   1, 0x04, 0x00000100 },
	{ 0x419ea8,   1, 0x04, 0x00000000 },
	{ 0x419eb4,   1, 0x04, 0x00000000 },
	{ 0x419ebc,   2, 0x04, 0x00000000 },
	{ 0x419edc,   1, 0x04, 0x00000000 },
	{ 0x419f00,   1, 0x04, 0x00000000 },
	{ 0x419ed0,   1, 0x04, 0x00003234 },
	{ 0x419f74,   1, 0x04, 0x00015555 },
	{ 0x419f80,   4, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_pack
nvf0_graph_pack_mmio[] = {
	{ nve4_graph_init_main_0 },
	{ nvf0_graph_init_fe_0 },
	{ nvc0_graph_init_pri_0 },
	{ nvc0_graph_init_rstr2d_0 },
	{ nvd9_graph_init_pd_0 },
	{ nvf0_graph_init_ds_0 },
	{ nvc0_graph_init_scc_0 },
	{ nvf0_graph_init_sked_0 },
	{ nvf0_graph_init_cwd_0 },
	{ nvd9_graph_init_prop_0 },
	{ nvc1_graph_init_gpc_unk_0 },
	{ nvc0_graph_init_setup_0 },
	{ nvc0_graph_init_crstr_0 },
	{ nvc1_graph_init_setup_1 },
	{ nvc0_graph_init_zcull_0 },
	{ nvd9_graph_init_gpm_0 },
	{ nvf0_graph_init_gpc_unk_1 },
	{ nvc0_graph_init_gcc_0 },
	{ nve4_graph_init_tpccs_0 },
	{ nvf0_graph_init_tex_0 },
	{ nve4_graph_init_pe_0 },
	{ nvf0_graph_init_l1c_0 },
	{ nvc0_graph_init_mpc_0 },
	{ nvf0_graph_init_sm_0 },
	{ nvd7_graph_init_pes_0 },
	{ nvd7_graph_init_wwdx_0 },
	{ nvd7_graph_init_cbm_0 },
	{ nve4_graph_init_be_0 },
	{ nvc0_graph_init_fe_1 },
	{}
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

int
nvf0_graph_fini(struct nouveau_object *object, bool suspend)
{
	struct nvc0_graph_priv *priv = (void *)object;
	static const struct {
		u32 addr;
		u32 data;
	} magic[] = {
		{ 0x020520, 0xfffffffc },
		{ 0x020524, 0xfffffffe },
		{ 0x020524, 0xfffffffc },
		{ 0x020524, 0xfffffff8 },
		{ 0x020524, 0xffffffe0 },
		{ 0x020530, 0xfffffffe },
		{ 0x02052c, 0xfffffffa },
		{ 0x02052c, 0xfffffff0 },
		{ 0x02052c, 0xffffffc0 },
		{ 0x02052c, 0xffffff00 },
		{ 0x02052c, 0xfffffc00 },
		{ 0x02052c, 0xfffcfc00 },
		{ 0x02052c, 0xfff0fc00 },
		{ 0x02052c, 0xff80fc00 },
		{ 0x020528, 0xfffffffe },
		{ 0x020528, 0xfffffffc },
	};
	int i;

	nv_mask(priv, 0x000200, 0x08001000, 0x00000000);
	nv_mask(priv, 0x0206b4, 0x00000000, 0x00000000);
	for (i = 0; i < ARRAY_SIZE(magic); i++) {
		nv_wr32(priv, magic[i].addr, magic[i].data);
		nv_wait(priv, magic[i].addr, 0x80000000, 0x00000000);
	}

	return nouveau_graph_fini(&priv->base, suspend);
}

#include "fuc/hubnvf0.fuc.h"

struct nvc0_graph_ucode
nvf0_graph_fecs_ucode = {
	.code.data = nvf0_grhub_code,
	.code.size = sizeof(nvf0_grhub_code),
	.data.data = nvf0_grhub_data,
	.data.size = sizeof(nvf0_grhub_data),
};

#include "fuc/gpcnvf0.fuc.h"

struct nvc0_graph_ucode
nvf0_graph_gpccs_ucode = {
	.code.data = nvf0_grgpc_code,
	.code.size = sizeof(nvf0_grgpc_code),
	.data.data = nvf0_grgpc_data,
	.data.size = sizeof(nvf0_grgpc_data),
};

struct nouveau_oclass *
nvf0_graph_oclass = &(struct nvc0_graph_oclass) {
	.base.handle = NV_ENGINE(GR, 0xf0),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_ctor,
		.dtor = nvc0_graph_dtor,
		.init = nve4_graph_init,
		.fini = nvf0_graph_fini,
	},
	.cclass = &nvf0_grctx_oclass,
	.sclass =  nvf0_graph_sclass,
	.mmio = nvf0_graph_pack_mmio,
	.fecs.ucode = &nvf0_graph_fecs_ucode,
	.gpccs.ucode = &nvf0_graph_gpccs_ucode,
}.base;
