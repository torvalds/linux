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

#include "ctxnvc0.h"

/*******************************************************************************
 * PGRAPH context register lists
 ******************************************************************************/

static const struct nvc0_graph_init
nvd7_grctx_init_ds_0[] = {
	{ 0x405800,   1, 0x04, 0x0f8000bf },
	{ 0x405830,   1, 0x04, 0x02180324 },
	{ 0x405834,   1, 0x04, 0x08000000 },
	{ 0x405838,   1, 0x04, 0x00000000 },
	{ 0x405854,   1, 0x04, 0x00000000 },
	{ 0x405870,   4, 0x04, 0x00000001 },
	{ 0x405a00,   2, 0x04, 0x00000000 },
	{ 0x405a18,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
nvd7_grctx_init_pd_0[] = {
	{ 0x406020,   1, 0x04, 0x000103c1 },
	{ 0x406028,   4, 0x04, 0x00000001 },
	{ 0x4064a8,   1, 0x04, 0x00000000 },
	{ 0x4064ac,   1, 0x04, 0x00003fff },
	{ 0x4064b4,   3, 0x04, 0x00000000 },
	{ 0x4064c0,   1, 0x04, 0x801a0078 },
	{ 0x4064c4,   1, 0x04, 0x00c9ffff },
	{ 0x4064d0,   8, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_pack
nvd7_grctx_pack_hub[] = {
	{ nvc0_grctx_init_main_0 },
	{ nvd9_grctx_init_fe_0 },
	{ nvc0_grctx_init_pri_0 },
	{ nvc0_grctx_init_memfmt_0 },
	{ nvd7_grctx_init_ds_0 },
	{ nvd7_grctx_init_pd_0 },
	{ nvc0_grctx_init_rstr2d_0 },
	{ nvc0_grctx_init_scc_0 },
	{ nvd9_grctx_init_be_0 },
	{}
};

static const struct nvc0_graph_init
nvd7_grctx_init_setup_0[] = {
	{ 0x418800,   1, 0x04, 0x7006860a },
	{ 0x418808,   3, 0x04, 0x00000000 },
	{ 0x418828,   1, 0x04, 0x00008442 },
	{ 0x418830,   1, 0x04, 0x10000001 },
	{ 0x4188d8,   1, 0x04, 0x00000008 },
	{ 0x4188e0,   1, 0x04, 0x01000000 },
	{ 0x4188e8,   5, 0x04, 0x00000000 },
	{ 0x4188fc,   1, 0x04, 0x20100018 },
	{}
};

static const struct nvc0_graph_pack
nvd7_grctx_pack_gpc[] = {
	{ nvc0_grctx_init_gpc_unk_0 },
	{ nvd9_grctx_init_prop_0 },
	{ nvd9_grctx_init_gpc_unk_1 },
	{ nvd7_grctx_init_setup_0 },
	{ nvc0_grctx_init_zcull_0 },
	{ nvd9_grctx_init_crstr_0 },
	{ nvc1_grctx_init_gpm_0 },
	{ nvc0_grctx_init_gcc_0 },
	{}
};

const struct nvc0_graph_init
nvd7_grctx_init_pe_0[] = {
	{ 0x419848,   1, 0x04, 0x00000000 },
	{ 0x419864,   1, 0x04, 0x00000129 },
	{ 0x419888,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
nvd7_grctx_init_tex_0[] = {
	{ 0x419a00,   1, 0x04, 0x000001f0 },
	{ 0x419a04,   1, 0x04, 0x00000001 },
	{ 0x419a08,   1, 0x04, 0x00000023 },
	{ 0x419a0c,   1, 0x04, 0x00020000 },
	{ 0x419a10,   1, 0x04, 0x00000000 },
	{ 0x419a14,   1, 0x04, 0x00000200 },
	{ 0x419a1c,   1, 0x04, 0x00008000 },
	{ 0x419a20,   1, 0x04, 0x00000800 },
	{ 0x419ac4,   1, 0x04, 0x0017f440 },
	{}
};

static const struct nvc0_graph_init
nvd7_grctx_init_mpc_0[] = {
	{ 0x419c00,   1, 0x04, 0x0000000a },
	{ 0x419c04,   1, 0x04, 0x00000006 },
	{ 0x419c08,   1, 0x04, 0x00000002 },
	{ 0x419c20,   1, 0x04, 0x00000000 },
	{ 0x419c24,   1, 0x04, 0x00084210 },
	{ 0x419c28,   1, 0x04, 0x3efbefbe },
	{}
};

static const struct nvc0_graph_pack
nvd7_grctx_pack_tpc[] = {
	{ nvd7_grctx_init_pe_0 },
	{ nvd7_grctx_init_tex_0 },
	{ nvd7_grctx_init_mpc_0 },
	{ nvc4_grctx_init_l1c_0 },
	{ nvd9_grctx_init_sm_0 },
	{}
};

static const struct nvc0_graph_init
nvd7_grctx_init_pes_0[] = {
	{ 0x41be24,   1, 0x04, 0x00000002 },
	{}
};

static const struct nvc0_graph_init
nvd7_grctx_init_cbm_0[] = {
	{ 0x41bec0,   1, 0x04, 0x12180000 },
	{ 0x41bec4,   1, 0x04, 0x00003fff },
	{ 0x41bee4,   1, 0x04, 0x03240218 },
	{}
};

const struct nvc0_graph_init
nvd7_grctx_init_wwdx_0[] = {
	{ 0x41bf00,   1, 0x04, 0x0a418820 },
	{ 0x41bf04,   1, 0x04, 0x062080e6 },
	{ 0x41bf08,   1, 0x04, 0x020398a4 },
	{ 0x41bf0c,   1, 0x04, 0x0e629062 },
	{ 0x41bf10,   1, 0x04, 0x0a418820 },
	{ 0x41bf14,   1, 0x04, 0x000000e6 },
	{ 0x41bfd0,   1, 0x04, 0x00900103 },
	{ 0x41bfe0,   1, 0x04, 0x00400001 },
	{ 0x41bfe4,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_pack
nvd7_grctx_pack_ppc[] = {
	{ nvd7_grctx_init_pes_0 },
	{ nvd7_grctx_init_cbm_0 },
	{ nvd7_grctx_init_wwdx_0 },
	{}
};

/*******************************************************************************
 * PGRAPH context implementation
 ******************************************************************************/

static void
nvd7_grctx_generate_mods(struct nvc0_graph_priv *priv, struct nvc0_grctx *info)
{
	u32 magic[GPC_MAX][2];
	u32 offset;
	int gpc;

	mmio_data(0x003000, 0x0100, NV_MEM_ACCESS_RW | NV_MEM_ACCESS_SYS);
	mmio_data(0x008000, 0x0100, NV_MEM_ACCESS_RW | NV_MEM_ACCESS_SYS);
	mmio_data(0x060000, 0x1000, NV_MEM_ACCESS_RW);
	mmio_list(0x40800c, 0x00000000,  8, 1);
	mmio_list(0x408010, 0x80000000,  0, 0);
	mmio_list(0x419004, 0x00000000,  8, 1);
	mmio_list(0x419008, 0x00000000,  0, 0);
	mmio_list(0x408004, 0x00000000,  8, 0);
	mmio_list(0x408008, 0x80000018,  0, 0);
	mmio_list(0x418808, 0x00000000,  8, 0);
	mmio_list(0x41880c, 0x80000018,  0, 0);
	mmio_list(0x418810, 0x80000000, 12, 2);
	mmio_list(0x419848, 0x10000000, 12, 2);

	mmio_list(0x405830, 0x02180324,  0, 0);
	mmio_list(0x4064c4, 0x00c9ffff,  0, 0);

	for (gpc = 0, offset = 0; gpc < priv->gpc_nr; gpc++) {
		u16 magic0 = 0x0218 * priv->tpc_nr[gpc];
		u16 magic1 = 0x0324 * priv->tpc_nr[gpc];
		magic[gpc][0]  = 0x10000000 | (magic0 << 16) | offset;
		magic[gpc][1]  = 0x00000000 | (magic1 << 16);
		offset += 0x0324 * priv->tpc_nr[gpc];
	}

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		mmio_list(GPC_UNIT(gpc, 0x30c0), magic[gpc][0], 0, 0);
		mmio_list(GPC_UNIT(gpc, 0x30e4), magic[gpc][1] | offset, 0, 0);
		offset += 0x07ff * priv->tpc_nr[gpc];
	}
	mmio_list(0x17e91c, 0x03060609, 0, 0); /* different from kepler */
}

void
nvd7_grctx_generate_main(struct nvc0_graph_priv *priv, struct nvc0_grctx *info)
{
	struct nvc0_grctx_oclass *oclass = (void *)nv_engine(priv)->cclass;
	int i;

	nv_mask(priv, 0x000260, 0x00000001, 0x00000000);

	nvc0_graph_mmio(priv, oclass->hub);
	nvc0_graph_mmio(priv, oclass->gpc);
	nvc0_graph_mmio(priv, oclass->zcull);
	nvc0_graph_mmio(priv, oclass->tpc);
	nvc0_graph_mmio(priv, oclass->ppc);

	nv_wr32(priv, 0x404154, 0x00000000);

	oclass->mods(priv, info);
	oclass->unkn(priv);

	nvc0_grctx_generate_tpcid(priv);
	nvc0_grctx_generate_r406028(priv);
	nvc0_grctx_generate_r4060a8(priv);
	nve4_grctx_generate_r418bb8(priv);
	nvc0_grctx_generate_r406800(priv);

	for (i = 0; i < 8; i++)
		nv_wr32(priv, 0x4064d0 + (i * 0x04), 0x00000000);

	nvc0_graph_icmd(priv, oclass->icmd);
	nv_wr32(priv, 0x404154, 0x00000400);
	nvc0_graph_mthd(priv, oclass->mthd);
	nv_mask(priv, 0x000260, 0x00000001, 0x00000001);
}

struct nouveau_oclass *
nvd7_grctx_oclass = &(struct nvc0_grctx_oclass) {
	.base.handle = NV_ENGCTX(GR, 0xd7),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_context_ctor,
		.dtor = nvc0_graph_context_dtor,
		.init = _nouveau_graph_context_init,
		.fini = _nouveau_graph_context_fini,
		.rd32 = _nouveau_graph_context_rd32,
		.wr32 = _nouveau_graph_context_wr32,
	},
	.main  = nvd7_grctx_generate_main,
	.mods  = nvd7_grctx_generate_mods,
	.unkn  = nve4_grctx_generate_unkn,
	.hub   = nvd7_grctx_pack_hub,
	.gpc   = nvd7_grctx_pack_gpc,
	.zcull = nvc0_grctx_pack_zcull,
	.tpc   = nvd7_grctx_pack_tpc,
	.ppc   = nvd7_grctx_pack_ppc,
	.icmd  = nvd9_grctx_pack_icmd,
	.mthd  = nvd9_grctx_pack_mthd,
}.base;
