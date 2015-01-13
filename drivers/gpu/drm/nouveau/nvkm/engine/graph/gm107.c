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

#include <subdev/bios.h>
#include <subdev/bios/P0260.h>

#include "nvc0.h"
#include "ctxnvc0.h"

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static struct nouveau_oclass
gm107_graph_sclass[] = {
	{ 0x902d, &nouveau_object_ofuncs },
	{ 0xa140, &nouveau_object_ofuncs },
	{ MAXWELL_A, &nvc0_fermi_ofuncs, nvc0_graph_9097_omthds },
	{ MAXWELL_COMPUTE_A, &nouveau_object_ofuncs, nvc0_graph_90c0_omthds },
	{}
};

/*******************************************************************************
 * PGRAPH register lists
 ******************************************************************************/

static const struct nvc0_graph_init
gm107_graph_init_main_0[] = {
	{ 0x400080,   1, 0x04, 0x003003c2 },
	{ 0x400088,   1, 0x04, 0x0001bfe7 },
	{ 0x40008c,   1, 0x04, 0x00060000 },
	{ 0x400090,   1, 0x04, 0x00000030 },
	{ 0x40013c,   1, 0x04, 0x003901f3 },
	{ 0x400140,   1, 0x04, 0x00000100 },
	{ 0x400144,   1, 0x04, 0x00000000 },
	{ 0x400148,   1, 0x04, 0x00000110 },
	{ 0x400138,   1, 0x04, 0x00000000 },
	{ 0x400130,   2, 0x04, 0x00000000 },
	{ 0x400124,   1, 0x04, 0x00000002 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_ds_0[] = {
	{ 0x405844,   1, 0x04, 0x00ffffff },
	{ 0x405850,   1, 0x04, 0x00000000 },
	{ 0x405900,   1, 0x04, 0x00000000 },
	{ 0x405908,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_scc_0[] = {
	{ 0x40803c,   1, 0x04, 0x00000010 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_sked_0[] = {
	{ 0x407010,   1, 0x04, 0x00000000 },
	{ 0x407040,   1, 0x04, 0x40440424 },
	{ 0x407048,   1, 0x04, 0x0000000a },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_prop_0[] = {
	{ 0x418408,   1, 0x04, 0x00000000 },
	{ 0x4184a0,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_setup_1[] = {
	{ 0x4188c8,   2, 0x04, 0x00000000 },
	{ 0x4188d0,   1, 0x04, 0x00010000 },
	{ 0x4188d4,   1, 0x04, 0x00010201 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_zcull_0[] = {
	{ 0x418910,   1, 0x04, 0x00010001 },
	{ 0x418914,   1, 0x04, 0x00000301 },
	{ 0x418918,   1, 0x04, 0x00800000 },
	{ 0x418930,   2, 0x04, 0x00000000 },
	{ 0x418980,   1, 0x04, 0x77777770 },
	{ 0x418984,   3, 0x04, 0x77777777 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_gpc_unk_1[] = {
	{ 0x418d00,   1, 0x04, 0x00000000 },
	{ 0x418f00,   1, 0x04, 0x00000400 },
	{ 0x418f08,   1, 0x04, 0x00000000 },
	{ 0x418e08,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_tpccs_0[] = {
	{ 0x419dc4,   1, 0x04, 0x00000000 },
	{ 0x419dc8,   1, 0x04, 0x00000501 },
	{ 0x419dd0,   1, 0x04, 0x00000000 },
	{ 0x419dd4,   1, 0x04, 0x00000100 },
	{ 0x419dd8,   1, 0x04, 0x00000001 },
	{ 0x419ddc,   1, 0x04, 0x00000002 },
	{ 0x419de0,   1, 0x04, 0x00000001 },
	{ 0x419d0c,   1, 0x04, 0x00000000 },
	{ 0x419d10,   1, 0x04, 0x00000014 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_tex_0[] = {
	{ 0x419ab0,   1, 0x04, 0x00000000 },
	{ 0x419ab8,   1, 0x04, 0x000000e7 },
	{ 0x419abc,   1, 0x04, 0x00000000 },
	{ 0x419acc,   1, 0x04, 0x000000ff },
	{ 0x419ac0,   1, 0x04, 0x00000000 },
	{ 0x419aa8,   2, 0x04, 0x00000000 },
	{ 0x419ad0,   2, 0x04, 0x00000000 },
	{ 0x419ae0,   2, 0x04, 0x00000000 },
	{ 0x419af0,   4, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_pe_0[] = {
	{ 0x419900,   1, 0x04, 0x000000ff },
	{ 0x41980c,   1, 0x04, 0x00000010 },
	{ 0x419844,   1, 0x04, 0x00000000 },
	{ 0x419838,   1, 0x04, 0x000000ff },
	{ 0x419850,   1, 0x04, 0x00000004 },
	{ 0x419854,   2, 0x04, 0x00000000 },
	{ 0x419894,   3, 0x04, 0x00100401 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_l1c_0[] = {
	{ 0x419c98,   1, 0x04, 0x00000000 },
	{ 0x419cc0,   2, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_sm_0[] = {
	{ 0x419e30,   1, 0x04, 0x000000ff },
	{ 0x419e00,   1, 0x04, 0x00000000 },
	{ 0x419ea0,   1, 0x04, 0x00000000 },
	{ 0x419ee4,   1, 0x04, 0x00000000 },
	{ 0x419ea4,   1, 0x04, 0x00000100 },
	{ 0x419ea8,   1, 0x04, 0x01000000 },
	{ 0x419ee8,   1, 0x04, 0x00000091 },
	{ 0x419eb4,   1, 0x04, 0x00000000 },
	{ 0x419ebc,   2, 0x04, 0x00000000 },
	{ 0x419edc,   1, 0x04, 0x000c1810 },
	{ 0x419ed8,   1, 0x04, 0x00000000 },
	{ 0x419ee0,   1, 0x04, 0x00000000 },
	{ 0x419f74,   1, 0x04, 0x00005155 },
	{ 0x419f80,   4, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_l1c_1[] = {
	{ 0x419ccc,   2, 0x04, 0x00000000 },
	{ 0x419c80,   1, 0x04, 0x3f006022 },
	{ 0x419c88,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_pes_0[] = {
	{ 0x41be50,   1, 0x04, 0x000000ff },
	{ 0x41be04,   1, 0x04, 0x00000000 },
	{ 0x41be08,   1, 0x04, 0x00000004 },
	{ 0x41be0c,   1, 0x04, 0x00000008 },
	{ 0x41be10,   1, 0x04, 0x0e3b8bc7 },
	{ 0x41be14,   2, 0x04, 0x00000000 },
	{ 0x41be3c,   5, 0x04, 0x00100401 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_wwdx_0[] = {
	{ 0x41bfd4,   1, 0x04, 0x00800000 },
	{ 0x41bfdc,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_cbm_0[] = {
	{ 0x41becc,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_be_0[] = {
	{ 0x408890,   1, 0x04, 0x000000ff },
	{ 0x40880c,   1, 0x04, 0x00000000 },
	{ 0x408850,   1, 0x04, 0x00000004 },
	{ 0x408878,   1, 0x04, 0x00c81603 },
	{ 0x40887c,   1, 0x04, 0x80543432 },
	{ 0x408880,   1, 0x04, 0x0010581e },
	{ 0x408884,   1, 0x04, 0x00001205 },
	{ 0x408974,   1, 0x04, 0x000000ff },
	{ 0x408910,   9, 0x04, 0x00000000 },
	{ 0x408950,   1, 0x04, 0x00000000 },
	{ 0x408954,   1, 0x04, 0x0000ffff },
	{ 0x408958,   1, 0x04, 0x00000034 },
	{ 0x40895c,   1, 0x04, 0x8531a003 },
	{ 0x408960,   1, 0x04, 0x0561985a },
	{ 0x408964,   1, 0x04, 0x04e15c4f },
	{ 0x408968,   1, 0x04, 0x02808833 },
	{ 0x40896c,   1, 0x04, 0x01f02438 },
	{ 0x408970,   1, 0x04, 0x00012c00 },
	{ 0x408984,   1, 0x04, 0x00000000 },
	{ 0x408988,   1, 0x04, 0x08040201 },
	{ 0x40898c,   1, 0x04, 0x80402010 },
	{}
};

static const struct nvc0_graph_init
gm107_graph_init_sm_1[] = {
	{ 0x419e5c,   1, 0x04, 0x00000000 },
	{ 0x419e58,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_pack
gm107_graph_pack_mmio[] = {
	{ gm107_graph_init_main_0 },
	{ nvf0_graph_init_fe_0 },
	{ nvc0_graph_init_pri_0 },
	{ nvc0_graph_init_rstr2d_0 },
	{ nvc0_graph_init_pd_0 },
	{ gm107_graph_init_ds_0 },
	{ gm107_graph_init_scc_0 },
	{ gm107_graph_init_sked_0 },
	{ nvf0_graph_init_cwd_0 },
	{ gm107_graph_init_prop_0 },
	{ nv108_graph_init_gpc_unk_0 },
	{ nvc0_graph_init_setup_0 },
	{ nvc0_graph_init_crstr_0 },
	{ gm107_graph_init_setup_1 },
	{ gm107_graph_init_zcull_0 },
	{ nvc0_graph_init_gpm_0 },
	{ gm107_graph_init_gpc_unk_1 },
	{ nvc0_graph_init_gcc_0 },
	{ gm107_graph_init_tpccs_0 },
	{ gm107_graph_init_tex_0 },
	{ gm107_graph_init_pe_0 },
	{ gm107_graph_init_l1c_0 },
	{ nvc0_graph_init_mpc_0 },
	{ gm107_graph_init_sm_0 },
	{ gm107_graph_init_l1c_1 },
	{ gm107_graph_init_pes_0 },
	{ gm107_graph_init_wwdx_0 },
	{ gm107_graph_init_cbm_0 },
	{ gm107_graph_init_be_0 },
	{ gm107_graph_init_sm_1 },
	{}
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

static void
gm107_graph_init_bios(struct nvc0_graph_priv *priv)
{
	static const struct {
		u32 ctrl;
		u32 data;
	} regs[] = {
		{ 0x419ed8, 0x419ee0 },
		{ 0x419ad0, 0x419ad4 },
		{ 0x419ae0, 0x419ae4 },
		{ 0x419af0, 0x419af4 },
		{ 0x419af8, 0x419afc },
	};
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_P0260E infoE;
	struct nvbios_P0260X infoX;
	int E = -1, X;
	u8 ver, hdr;

	while (nvbios_P0260Ep(bios, ++E, &ver, &hdr, &infoE)) {
		if (X = -1, E < ARRAY_SIZE(regs)) {
			nv_wr32(priv, regs[E].ctrl, infoE.data);
			while (nvbios_P0260Xp(bios, ++X, &ver, &hdr, &infoX))
				nv_wr32(priv, regs[E].data, infoX.data);
		}
	}
}

int
gm107_graph_init(struct nouveau_object *object)
{
	struct nvc0_graph_oclass *oclass = (void *)object->oclass;
	struct nvc0_graph_priv *priv = (void *)object;
	const u32 magicgpc918 = DIV_ROUND_UP(0x00800000, priv->tpc_total);
	u32 data[TPC_MAX / 8] = {};
	u8  tpcnr[GPC_MAX];
	int gpc, tpc, ppc, rop;
	int ret, i;

	ret = nouveau_graph_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, GPC_BCAST(0x0880), 0x00000000);
	nv_wr32(priv, GPC_BCAST(0x0890), 0x00000000);
	nv_wr32(priv, GPC_BCAST(0x0894), 0x00000000);
	nv_wr32(priv, GPC_BCAST(0x08b4), priv->unk4188b4->addr >> 8);
	nv_wr32(priv, GPC_BCAST(0x08b8), priv->unk4188b8->addr >> 8);

	nvc0_graph_mmio(priv, oclass->mmio);

	gm107_graph_init_bios(priv);

	nv_wr32(priv, GPC_UNIT(0, 0x3018), 0x00000001);

	memset(data, 0x00, sizeof(data));
	memcpy(tpcnr, priv->tpc_nr, sizeof(priv->tpc_nr));
	for (i = 0, gpc = -1; i < priv->tpc_total; i++) {
		do {
			gpc = (gpc + 1) % priv->gpc_nr;
		} while (!tpcnr[gpc]);
		tpc = priv->tpc_nr[gpc] - tpcnr[gpc]--;

		data[i / 8] |= tpc << ((i % 8) * 4);
	}

	nv_wr32(priv, GPC_BCAST(0x0980), data[0]);
	nv_wr32(priv, GPC_BCAST(0x0984), data[1]);
	nv_wr32(priv, GPC_BCAST(0x0988), data[2]);
	nv_wr32(priv, GPC_BCAST(0x098c), data[3]);

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		nv_wr32(priv, GPC_UNIT(gpc, 0x0914),
			priv->magic_not_rop_nr << 8 | priv->tpc_nr[gpc]);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0910), 0x00040000 |
			priv->tpc_total);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0918), magicgpc918);
	}

	nv_wr32(priv, GPC_BCAST(0x3fd4), magicgpc918);
	nv_wr32(priv, GPC_BCAST(0x08ac), nv_rd32(priv, 0x100800));

	nv_wr32(priv, 0x400500, 0x00010001);

	nv_wr32(priv, 0x400100, 0xffffffff);
	nv_wr32(priv, 0x40013c, 0xffffffff);
	nv_wr32(priv, 0x400124, 0x00000002);
	nv_wr32(priv, 0x409c24, 0x000e0000);

	nv_wr32(priv, 0x404000, 0xc0000000);
	nv_wr32(priv, 0x404600, 0xc0000000);
	nv_wr32(priv, 0x408030, 0xc0000000);
	nv_wr32(priv, 0x404490, 0xc0000000);
	nv_wr32(priv, 0x406018, 0xc0000000);
	nv_wr32(priv, 0x407020, 0x40000000);
	nv_wr32(priv, 0x405840, 0xc0000000);
	nv_wr32(priv, 0x405844, 0x00ffffff);
	nv_mask(priv, 0x419cc0, 0x00000008, 0x00000008);

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		for (ppc = 0; ppc < 2 /* priv->ppc_nr[gpc] */; ppc++)
			nv_wr32(priv, PPC_UNIT(gpc, ppc, 0x038), 0xc0000000);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0420), 0xc0000000);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0900), 0xc0000000);
		nv_wr32(priv, GPC_UNIT(gpc, 0x1028), 0xc0000000);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0824), 0xc0000000);
		for (tpc = 0; tpc < priv->tpc_nr[gpc]; tpc++) {
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x508), 0xffffffff);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x50c), 0xffffffff);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x224), 0xc0000000);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x48c), 0xc0000000);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x084), 0xc0000000);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x430), 0xc0000000);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x644), 0x00dffffe);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x64c), 0x00000005);
		}
		nv_wr32(priv, GPC_UNIT(gpc, 0x2c90), 0xffffffff);
		nv_wr32(priv, GPC_UNIT(gpc, 0x2c94), 0xffffffff);
	}

	for (rop = 0; rop < priv->rop_nr; rop++) {
		nv_wr32(priv, ROP_UNIT(rop, 0x144), 0x40000000);
		nv_wr32(priv, ROP_UNIT(rop, 0x070), 0x40000000);
		nv_wr32(priv, ROP_UNIT(rop, 0x204), 0xffffffff);
		nv_wr32(priv, ROP_UNIT(rop, 0x208), 0xffffffff);
	}

	nv_wr32(priv, 0x400108, 0xffffffff);
	nv_wr32(priv, 0x400138, 0xffffffff);
	nv_wr32(priv, 0x400118, 0xffffffff);
	nv_wr32(priv, 0x400130, 0xffffffff);
	nv_wr32(priv, 0x40011c, 0xffffffff);
	nv_wr32(priv, 0x400134, 0xffffffff);

	nv_wr32(priv, 0x400054, 0x2c350f63);

	nvc0_graph_zbc_init(priv);

	return nvc0_graph_init_ctxctl(priv);
}

#include "fuc/hubgm107.fuc5.h"

static struct nvc0_graph_ucode
gm107_graph_fecs_ucode = {
	.code.data = gm107_grhub_code,
	.code.size = sizeof(gm107_grhub_code),
	.data.data = gm107_grhub_data,
	.data.size = sizeof(gm107_grhub_data),
};

#include "fuc/gpcgm107.fuc5.h"

static struct nvc0_graph_ucode
gm107_graph_gpccs_ucode = {
	.code.data = gm107_grgpc_code,
	.code.size = sizeof(gm107_grgpc_code),
	.data.data = gm107_grgpc_data,
	.data.size = sizeof(gm107_grgpc_data),
};

struct nouveau_oclass *
gm107_graph_oclass = &(struct nvc0_graph_oclass) {
	.base.handle = NV_ENGINE(GR, 0x07),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_ctor,
		.dtor = nvc0_graph_dtor,
		.init = gm107_graph_init,
		.fini = _nouveau_graph_fini,
	},
	.cclass = &gm107_grctx_oclass,
	.sclass =  gm107_graph_sclass,
	.mmio = gm107_graph_pack_mmio,
	.fecs.ucode = 0 ? &gm107_graph_fecs_ucode : NULL,
	.gpccs.ucode = &gm107_graph_gpccs_ucode,
	.ppc_nr = 2,
}.base;
