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
nv108_graph_sclass[] = {
	{ 0x902d, &nouveau_object_ofuncs },
	{ 0xa140, &nouveau_object_ofuncs },
	{ KEPLER_B, &nvc0_fermi_ofuncs },
	{ 0xa1c0, &nouveau_object_ofuncs },
	{}
};

/*******************************************************************************
 * PGRAPH register lists
 ******************************************************************************/

static const struct nvc0_graph_init
nv108_graph_init_main_0[] = {
	{ 0x400080,   1, 0x04, 0x003083c2 },
	{ 0x400088,   1, 0x04, 0x0001bfe7 },
	{ 0x40008c,   1, 0x04, 0x00000000 },
	{ 0x400090,   1, 0x04, 0x00000030 },
	{ 0x40013c,   1, 0x04, 0x003901f7 },
	{ 0x400140,   1, 0x04, 0x00000100 },
	{ 0x400144,   1, 0x04, 0x00000000 },
	{ 0x400148,   1, 0x04, 0x00000110 },
	{ 0x400138,   1, 0x04, 0x00000000 },
	{ 0x400130,   2, 0x04, 0x00000000 },
	{ 0x400124,   1, 0x04, 0x00000002 },
	{}
};

static const struct nvc0_graph_init
nv108_graph_init_ds_0[] = {
	{ 0x405844,   1, 0x04, 0x00ffffff },
	{ 0x405850,   1, 0x04, 0x00000000 },
	{ 0x405900,   1, 0x04, 0x00000000 },
	{ 0x405908,   1, 0x04, 0x00000000 },
	{ 0x405928,   2, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nv108_graph_init_gpc_unk_0[] = {
	{ 0x418604,   1, 0x04, 0x00000000 },
	{ 0x418680,   1, 0x04, 0x00000000 },
	{ 0x418714,   1, 0x04, 0x00000000 },
	{ 0x418384,   2, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
nv108_graph_init_setup_1[] = {
	{ 0x4188c8,   2, 0x04, 0x00000000 },
	{ 0x4188d0,   1, 0x04, 0x00010000 },
	{ 0x4188d4,   1, 0x04, 0x00000201 },
	{}
};

static const struct nvc0_graph_init
nv108_graph_init_tex_0[] = {
	{ 0x419ab0,   1, 0x04, 0x00000000 },
	{ 0x419ac8,   1, 0x04, 0x00000000 },
	{ 0x419ab8,   1, 0x04, 0x000000e7 },
	{ 0x419abc,   2, 0x04, 0x00000000 },
	{ 0x419ab4,   1, 0x04, 0x00000000 },
	{ 0x419aa8,   2, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
nv108_graph_init_l1c_0[] = {
	{ 0x419c98,   1, 0x04, 0x00000000 },
	{ 0x419ca8,   1, 0x04, 0x00000000 },
	{ 0x419cb0,   1, 0x04, 0x01000000 },
	{ 0x419cb4,   1, 0x04, 0x00000000 },
	{ 0x419cb8,   1, 0x04, 0x00b08bea },
	{ 0x419c84,   1, 0x04, 0x00010384 },
	{ 0x419cbc,   1, 0x04, 0x281b3646 },
	{ 0x419cc0,   2, 0x04, 0x00000000 },
	{ 0x419c80,   1, 0x04, 0x00000230 },
	{ 0x419ccc,   2, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_pack
nv108_graph_pack_mmio[] = {
	{ nv108_graph_init_main_0 },
	{ nvf0_graph_init_fe_0 },
	{ nvc0_graph_init_pri_0 },
	{ nvc0_graph_init_rstr2d_0 },
	{ nvd9_graph_init_pd_0 },
	{ nv108_graph_init_ds_0 },
	{ nvc0_graph_init_scc_0 },
	{ nvf0_graph_init_sked_0 },
	{ nvf0_graph_init_cwd_0 },
	{ nvd9_graph_init_prop_0 },
	{ nv108_graph_init_gpc_unk_0 },
	{ nvc0_graph_init_setup_0 },
	{ nvc0_graph_init_crstr_0 },
	{ nv108_graph_init_setup_1 },
	{ nvc0_graph_init_zcull_0 },
	{ nvd9_graph_init_gpm_0 },
	{ nvf0_graph_init_gpc_unk_1 },
	{ nvc0_graph_init_gcc_0 },
	{ nve4_graph_init_tpccs_0 },
	{ nv108_graph_init_tex_0 },
	{ nve4_graph_init_pe_0 },
	{ nv108_graph_init_l1c_0 },
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

static int
nv108_graph_fini(struct nouveau_object *object, bool suspend)
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

#include "fuc/hubnv108.fuc5.h"

static struct nvc0_graph_ucode
nv108_graph_fecs_ucode = {
	.code.data = nv108_grhub_code,
	.code.size = sizeof(nv108_grhub_code),
	.data.data = nv108_grhub_data,
	.data.size = sizeof(nv108_grhub_data),
};

#include "fuc/gpcnv108.fuc5.h"

static struct nvc0_graph_ucode
nv108_graph_gpccs_ucode = {
	.code.data = nv108_grgpc_code,
	.code.size = sizeof(nv108_grgpc_code),
	.data.data = nv108_grgpc_data,
	.data.size = sizeof(nv108_grgpc_data),
};

struct nouveau_oclass *
nv108_graph_oclass = &(struct nvc0_graph_oclass) {
	.base.handle = NV_ENGINE(GR, 0x08),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_ctor,
		.dtor = nvc0_graph_dtor,
		.init = nve4_graph_init,
		.fini = nv108_graph_fini,
	},
	.cclass = &nv108_grctx_oclass,
	.sclass =  nv108_graph_sclass,
	.mmio = nv108_graph_pack_mmio,
	.fecs.ucode = &nv108_graph_fecs_ucode,
	.gpccs.ucode = &nv108_graph_gpccs_ucode,
	.ppc_nr = 1,
}.base;
