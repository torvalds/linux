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

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static struct nouveau_oclass
nve4_graph_sclass[] = {
	{ 0x902d, &nouveau_object_ofuncs },
	{ 0xa040, &nouveau_object_ofuncs },
	{ 0xa097, &nouveau_object_ofuncs },
	{ 0xa0c0, &nouveau_object_ofuncs },
	{}
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

struct nvc0_graph_init
nve4_graph_init_regs[] = {
	{ 0x400080,   1, 0x04, 0x003083c2 },
	{ 0x400088,   1, 0x04, 0x0001ffe7 },
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

static struct nvc0_graph_init
nve4_graph_init_unk58xx[] = {
	{ 0x405844,   1, 0x04, 0x00ffffff },
	{ 0x405850,   1, 0x04, 0x00000000 },
	{ 0x405900,   1, 0x04, 0x0000ff34 },
	{ 0x405908,   1, 0x04, 0x00000000 },
	{ 0x405928,   1, 0x04, 0x00000000 },
	{ 0x40592c,   1, 0x04, 0x00000000 },
	{}
};

static struct nvc0_graph_init
nve4_graph_init_unk70xx[] = {
	{ 0x407010,   1, 0x04, 0x00000000 },
	{}
};

struct nvc0_graph_init
nve4_graph_init_unk5bxx[] = {
	{ 0x405b50,   1, 0x04, 0x00000000 },
	{}
};

static struct nvc0_graph_init
nve4_graph_init_gpc[] = {
	{ 0x418408,   1, 0x04, 0x00000000 },
	{ 0x4184a0,   1, 0x04, 0x00000000 },
	{ 0x4184a4,   2, 0x04, 0x00000000 },
	{ 0x418604,   1, 0x04, 0x00000000 },
	{ 0x418680,   1, 0x04, 0x00000000 },
	{ 0x418714,   1, 0x04, 0x00000000 },
	{ 0x418384,   1, 0x04, 0x00000000 },
	{ 0x418814,   3, 0x04, 0x00000000 },
	{ 0x418b04,   1, 0x04, 0x00000000 },
	{ 0x4188c8,   2, 0x04, 0x00000000 },
	{ 0x4188d0,   1, 0x04, 0x00010000 },
	{ 0x4188d4,   1, 0x04, 0x00000001 },
	{ 0x418910,   1, 0x04, 0x00010001 },
	{ 0x418914,   1, 0x04, 0x00000301 },
	{ 0x418918,   1, 0x04, 0x00800000 },
	{ 0x418980,   1, 0x04, 0x77777770 },
	{ 0x418984,   3, 0x04, 0x77777777 },
	{ 0x418c04,   1, 0x04, 0x00000000 },
	{ 0x418c64,   1, 0x04, 0x00000000 },
	{ 0x418c68,   1, 0x04, 0x00000000 },
	{ 0x418c88,   1, 0x04, 0x00000000 },
	{ 0x418cb4,   2, 0x04, 0x00000000 },
	{ 0x418d00,   1, 0x04, 0x00000000 },
	{ 0x418d28,   1, 0x04, 0x00000000 },
	{ 0x418d2c,   1, 0x04, 0x00000000 },
	{ 0x418f00,   1, 0x04, 0x00000000 },
	{ 0x418f08,   1, 0x04, 0x00000000 },
	{ 0x418f20,   2, 0x04, 0x00000000 },
	{ 0x418e00,   1, 0x04, 0x00000060 },
	{ 0x418e08,   1, 0x04, 0x00000000 },
	{ 0x418e1c,   1, 0x04, 0x00000000 },
	{ 0x418e20,   1, 0x04, 0x00000000 },
	{ 0x41900c,   1, 0x04, 0x00000000 },
	{ 0x419018,   1, 0x04, 0x00000000 },
	{}
};

static struct nvc0_graph_init
nve4_graph_init_tpc[] = {
	{ 0x419d0c,   1, 0x04, 0x00000000 },
	{ 0x419d10,   1, 0x04, 0x00000014 },
	{ 0x419ab0,   1, 0x04, 0x00000000 },
	{ 0x419ac8,   1, 0x04, 0x00000000 },
	{ 0x419ab8,   1, 0x04, 0x000000e7 },
	{ 0x419abc,   2, 0x04, 0x00000000 },
	{ 0x419ab4,   1, 0x04, 0x00000000 },
	{ 0x41980c,   1, 0x04, 0x00000010 },
	{ 0x419844,   1, 0x04, 0x00000000 },
	{ 0x419850,   1, 0x04, 0x00000004 },
	{ 0x419854,   2, 0x04, 0x00000000 },
	{ 0x419c98,   1, 0x04, 0x00000000 },
	{ 0x419ca8,   1, 0x04, 0x00000000 },
	{ 0x419cb0,   1, 0x04, 0x01000000 },
	{ 0x419cb4,   1, 0x04, 0x00000000 },
	{ 0x419cb8,   1, 0x04, 0x00b08bea },
	{ 0x419c84,   1, 0x04, 0x00010384 },
	{ 0x419cbc,   1, 0x04, 0x28137646 },
	{ 0x419cc0,   2, 0x04, 0x00000000 },
	{ 0x419c80,   1, 0x04, 0x00020232 },
	{ 0x419c0c,   1, 0x04, 0x00000000 },
	{ 0x419e00,   1, 0x04, 0x00000000 },
	{ 0x419ea0,   1, 0x04, 0x00000000 },
	{ 0x419ee4,   1, 0x04, 0x00000000 },
	{ 0x419ea4,   1, 0x04, 0x00000100 },
	{ 0x419ea8,   1, 0x04, 0x00000000 },
	{ 0x419eb4,   1, 0x04, 0x00000000 },
	{ 0x419eb8,   3, 0x04, 0x00000000 },
	{ 0x419edc,   1, 0x04, 0x00000000 },
	{ 0x419f00,   1, 0x04, 0x00000000 },
	{ 0x419f74,   1, 0x04, 0x00000555 },
	{}
};

struct nvc0_graph_init
nve4_graph_init_unk[] = {
	{ 0x41be04,   1, 0x04, 0x00000000 },
	{ 0x41be08,   1, 0x04, 0x00000004 },
	{ 0x41be0c,   1, 0x04, 0x00000000 },
	{ 0x41be10,   1, 0x04, 0x003b8bc7 },
	{ 0x41be14,   2, 0x04, 0x00000000 },
	{ 0x41bfd4,   1, 0x04, 0x00800000 },
	{ 0x41bfdc,   1, 0x04, 0x00000000 },
	{ 0x41bff8,   1, 0x04, 0x00000000 },
	{ 0x41bffc,   1, 0x04, 0x00000000 },
	{ 0x41becc,   1, 0x04, 0x00000000 },
	{ 0x41bee8,   1, 0x04, 0x00000000 },
	{ 0x41beec,   1, 0x04, 0x00000000 },
	{}
};

struct nvc0_graph_init
nve4_graph_init_unk88xx[] = {
	{ 0x40880c,   1, 0x04, 0x00000000 },
	{ 0x408850,   1, 0x04, 0x00000004 },
	{ 0x408910,   9, 0x04, 0x00000000 },
	{ 0x408950,   1, 0x04, 0x00000000 },
	{ 0x408954,   1, 0x04, 0x0000ffff },
	{ 0x408958,   1, 0x04, 0x00000034 },
	{ 0x408984,   1, 0x04, 0x00000000 },
	{ 0x408988,   1, 0x04, 0x08040201 },
	{ 0x40898c,   1, 0x04, 0x80402010 },
	{}
};

int
nve4_graph_init(struct nouveau_object *object)
{
	struct nvc0_graph_oclass *oclass = (void *)object->oclass;
	struct nvc0_graph_priv *priv = (void *)object;
	const u32 magicgpc918 = DIV_ROUND_UP(0x00800000, priv->tpc_total);
	u32 data[TPC_MAX / 8] = {};
	u8  tpcnr[GPC_MAX];
	int gpc, tpc, rop;
	int ret, i;

	ret = nouveau_graph_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, GPC_BCAST(0x0880), 0x00000000);
	nv_wr32(priv, GPC_BCAST(0x08a4), 0x00000000);
	nv_wr32(priv, GPC_BCAST(0x0888), 0x00000000);
	nv_wr32(priv, GPC_BCAST(0x088c), 0x00000000);
	nv_wr32(priv, GPC_BCAST(0x0890), 0x00000000);
	nv_wr32(priv, GPC_BCAST(0x0894), 0x00000000);
	nv_wr32(priv, GPC_BCAST(0x08b4), priv->unk4188b4->addr >> 8);
	nv_wr32(priv, GPC_BCAST(0x08b8), priv->unk4188b8->addr >> 8);

	for (i = 0; oclass->mmio[i]; i++)
		nvc0_graph_mmio(priv, oclass->mmio[i]);

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

	nv_wr32(priv, 0x409ffc, 0x00000000);
	nv_wr32(priv, 0x409c14, 0x00003e3e);
	nv_wr32(priv, 0x409c24, 0x000f0001);
	nv_wr32(priv, 0x404000, 0xc0000000);
	nv_wr32(priv, 0x404600, 0xc0000000);
	nv_wr32(priv, 0x408030, 0xc0000000);
	nv_wr32(priv, 0x404490, 0xc0000000);
	nv_wr32(priv, 0x406018, 0xc0000000);
	nv_wr32(priv, 0x407020, 0x40000000);
	nv_wr32(priv, 0x405840, 0xc0000000);
	nv_wr32(priv, 0x405844, 0x00ffffff);
	nv_mask(priv, 0x419cc0, 0x00000008, 0x00000008);
	nv_mask(priv, 0x419eb4, 0x00001000, 0x00001000);

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		nv_wr32(priv, GPC_UNIT(gpc, 0x3038), 0xc0000000);
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
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x644), 0x001ffffe);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x64c), 0x0000000f);
		}
		nv_wr32(priv, GPC_UNIT(gpc, 0x2c90), 0xffffffff);
		nv_wr32(priv, GPC_UNIT(gpc, 0x2c94), 0xffffffff);
	}

	for (rop = 0; rop < priv->rop_nr; rop++) {
		nv_wr32(priv, ROP_UNIT(rop, 0x144), 0xc0000000);
		nv_wr32(priv, ROP_UNIT(rop, 0x070), 0xc0000000);
		nv_wr32(priv, ROP_UNIT(rop, 0x204), 0xffffffff);
		nv_wr32(priv, ROP_UNIT(rop, 0x208), 0xffffffff);
	}

	nv_wr32(priv, 0x400108, 0xffffffff);
	nv_wr32(priv, 0x400138, 0xffffffff);
	nv_wr32(priv, 0x400118, 0xffffffff);
	nv_wr32(priv, 0x400130, 0xffffffff);
	nv_wr32(priv, 0x40011c, 0xffffffff);
	nv_wr32(priv, 0x400134, 0xffffffff);

	nv_wr32(priv, 0x400054, 0x34ce3464);
	return nvc0_graph_init_ctxctl(priv);
}

static struct nvc0_graph_init *
nve4_graph_init_mmio[] = {
	nve4_graph_init_regs,
	nvc0_graph_init_unk40xx,
	nvc0_graph_init_unk44xx,
	nvc0_graph_init_unk78xx,
	nvc0_graph_init_unk60xx,
	nvd9_graph_init_unk64xx,
	nve4_graph_init_unk58xx,
	nvc0_graph_init_unk80xx,
	nve4_graph_init_unk70xx,
	nve4_graph_init_unk5bxx,
	nve4_graph_init_gpc,
	nve4_graph_init_tpc,
	nve4_graph_init_unk,
	nve4_graph_init_unk88xx,
	NULL
};

#include "fuc/hubnve0.fuc.h"

static struct nvc0_graph_ucode
nve4_graph_fecs_ucode = {
	.code.data = nve0_grhub_code,
	.code.size = sizeof(nve0_grhub_code),
	.data.data = nve0_grhub_data,
	.data.size = sizeof(nve0_grhub_data),
};

#include "fuc/gpcnve0.fuc.h"

static struct nvc0_graph_ucode
nve4_graph_gpccs_ucode = {
	.code.data = nve0_grgpc_code,
	.code.size = sizeof(nve0_grgpc_code),
	.data.data = nve0_grgpc_data,
	.data.size = sizeof(nve0_grgpc_data),
};

struct nouveau_oclass *
nve4_graph_oclass = &(struct nvc0_graph_oclass) {
	.base.handle = NV_ENGINE(GR, 0xe4),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_ctor,
		.dtor = nvc0_graph_dtor,
		.init = nve4_graph_init,
		.fini = _nouveau_graph_fini,
	},
	.cclass = &nve4_grctx_oclass,
	.sclass = nve4_graph_sclass,
	.mmio = nve4_graph_init_mmio,
	.fecs.ucode = &nve4_graph_fecs_ucode,
	.gpccs.ucode = &nve4_graph_gpccs_ucode,
}.base;
