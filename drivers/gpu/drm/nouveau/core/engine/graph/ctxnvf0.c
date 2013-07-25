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

static struct nvc0_graph_init
nvf0_grctx_init_unk40xx[] = {
	{ 0x404004,   8, 0x04, 0x00000000 },
	{ 0x404024,   1, 0x04, 0x0000e000 },
	{ 0x404028,   8, 0x04, 0x00000000 },
	{ 0x4040a8,   8, 0x04, 0x00000000 },
	{ 0x4040c8,   1, 0x04, 0xf800008f },
	{ 0x4040d0,   6, 0x04, 0x00000000 },
	{ 0x4040e8,   1, 0x04, 0x00001000 },
	{ 0x4040f8,   1, 0x04, 0x00000000 },
	{ 0x404100,  10, 0x04, 0x00000000 },
	{ 0x404130,   2, 0x04, 0x00000000 },
	{ 0x404138,   1, 0x04, 0x20000040 },
	{ 0x404150,   1, 0x04, 0x0000002e },
	{ 0x404154,   1, 0x04, 0x00000400 },
	{ 0x404158,   1, 0x04, 0x00000200 },
	{ 0x404164,   1, 0x04, 0x00000055 },
	{ 0x40417c,   2, 0x04, 0x00000000 },
	{ 0x4041a0,   4, 0x04, 0x00000000 },
	{ 0x404200,   1, 0x04, 0x0000a197 },
	{ 0x404204,   1, 0x04, 0x0000a1c0 },
	{ 0x404208,   1, 0x04, 0x0000a140 },
	{ 0x40420c,   1, 0x04, 0x0000902d },
	{}
};

static struct nvc0_graph_init
nvf0_grctx_init_unk44xx[] = {
	{ 0x404404,  12, 0x04, 0x00000000 },
	{ 0x404438,   1, 0x04, 0x00000000 },
	{ 0x404460,   2, 0x04, 0x00000000 },
	{ 0x404468,   1, 0x04, 0x00ffffff },
	{ 0x40446c,   1, 0x04, 0x00000000 },
	{ 0x404480,   1, 0x04, 0x00000001 },
	{ 0x404498,   1, 0x04, 0x00000001 },
	{}
};

static struct nvc0_graph_init
nvf0_grctx_init_unk5bxx[] = {
	{ 0x405b00,   1, 0x04, 0x00000000 },
	{ 0x405b10,   1, 0x04, 0x00001000 },
	{ 0x405b20,   1, 0x04, 0x04000000 },
	{}
};

static struct nvc0_graph_init
nvf0_grctx_init_unk60xx[] = {
	{ 0x406020,   1, 0x04, 0x034103c1 },
	{ 0x406028,   4, 0x04, 0x00000001 },
	{}
};

static struct nvc0_graph_init
nvf0_grctx_init_unk64xx[] = {
	{ 0x4064a8,   1, 0x04, 0x00000000 },
	{ 0x4064ac,   1, 0x04, 0x00003fff },
	{ 0x4064b0,   3, 0x04, 0x00000000 },
	{ 0x4064c0,   1, 0x04, 0x802000f0 },
	{ 0x4064c4,   1, 0x04, 0x0192ffff },
	{ 0x4064c8,   1, 0x04, 0x018007c0 },
	{ 0x4064cc,   9, 0x04, 0x00000000 },
	{ 0x4064fc,   1, 0x04, 0x0000022a },
	{}
};

static struct nvc0_graph_init
nvf0_grctx_init_unk88xx[] = {
	{ 0x408800,   1, 0x04, 0x12802a3c },
	{ 0x408804,   1, 0x04, 0x00000040 },
	{ 0x408808,   1, 0x04, 0x1003e005 },
	{ 0x408840,   1, 0x04, 0x0000000b },
	{ 0x408900,   1, 0x04, 0x3080b801 },
	{ 0x408904,   1, 0x04, 0x62000001 },
	{ 0x408908,   1, 0x04, 0x00c8102f },
	{ 0x408980,   1, 0x04, 0x0000011d },
	{}
};

static struct nvc0_graph_init
nvf0_grctx_init_gpc_0[] = {
	{ 0x418380,   1, 0x04, 0x00000016 },
	{ 0x418400,   1, 0x04, 0x38004e00 },
	{ 0x418404,   1, 0x04, 0x71e0ffff },
	{ 0x41840c,   1, 0x04, 0x00001008 },
	{ 0x418410,   1, 0x04, 0x0fff0fff },
	{ 0x418414,   1, 0x04, 0x02200fff },
	{ 0x418450,   6, 0x04, 0x00000000 },
	{ 0x418468,   1, 0x04, 0x00000001 },
	{ 0x41846c,   2, 0x04, 0x00000000 },
	{ 0x418600,   1, 0x04, 0x0000001f },
	{ 0x418684,   1, 0x04, 0x0000000f },
	{ 0x418700,   1, 0x04, 0x00000002 },
	{ 0x418704,   1, 0x04, 0x00000080 },
	{ 0x418708,   3, 0x04, 0x00000000 },
	{ 0x418800,   1, 0x04, 0x7006860a },
	{ 0x418808,   1, 0x04, 0x00000000 },
	{ 0x41880c,   1, 0x04, 0x00000030 },
	{ 0x418810,   1, 0x04, 0x00000000 },
	{ 0x418828,   1, 0x04, 0x00000044 },
	{ 0x418830,   1, 0x04, 0x10000001 },
	{ 0x4188d8,   1, 0x04, 0x00000008 },
	{ 0x4188e0,   1, 0x04, 0x01000000 },
	{ 0x4188e8,   5, 0x04, 0x00000000 },
	{ 0x4188fc,   1, 0x04, 0x20100018 },
	{ 0x41891c,   1, 0x04, 0x00ff00ff },
	{ 0x418924,   1, 0x04, 0x00000000 },
	{ 0x418928,   1, 0x04, 0x00ffff00 },
	{ 0x41892c,   1, 0x04, 0x0000ff00 },
	{ 0x418b00,   1, 0x04, 0x00000006 },
	{ 0x418b08,   1, 0x04, 0x0a418820 },
	{ 0x418b0c,   1, 0x04, 0x062080e6 },
	{ 0x418b10,   1, 0x04, 0x020398a4 },
	{ 0x418b14,   1, 0x04, 0x0e629062 },
	{ 0x418b18,   1, 0x04, 0x0a418820 },
	{ 0x418b1c,   1, 0x04, 0x000000e6 },
	{ 0x418bb8,   1, 0x04, 0x00000103 },
	{ 0x418c08,   1, 0x04, 0x00000001 },
	{ 0x418c10,   8, 0x04, 0x00000000 },
	{ 0x418c40,   1, 0x04, 0xffffffff },
	{ 0x418c6c,   1, 0x04, 0x00000001 },
	{ 0x418c80,   1, 0x04, 0x20200004 },
	{ 0x418c8c,   1, 0x04, 0x00000001 },
	{ 0x418d24,   1, 0x04, 0x00000000 },
	{ 0x419000,   1, 0x04, 0x00000780 },
	{ 0x419004,   2, 0x04, 0x00000000 },
	{ 0x419014,   1, 0x04, 0x00000004 },
	{}
};

static struct nvc0_graph_init
nvf0_grctx_init_tpc[] = {
	{ 0x419848,   1, 0x04, 0x00000000 },
	{ 0x419864,   1, 0x04, 0x00000129 },
	{ 0x419888,   1, 0x04, 0x00000000 },
	{ 0x419a00,   1, 0x04, 0x000000f0 },
	{ 0x419a04,   1, 0x04, 0x00000001 },
	{ 0x419a08,   1, 0x04, 0x00000021 },
	{ 0x419a0c,   1, 0x04, 0x00020000 },
	{ 0x419a10,   1, 0x04, 0x00000000 },
	{ 0x419a14,   1, 0x04, 0x00000200 },
	{ 0x419a1c,   1, 0x04, 0x0000c000 },
	{ 0x419a20,   1, 0x04, 0x00020800 },
	{ 0x419a30,   1, 0x04, 0x00000001 },
	{ 0x419ac4,   1, 0x04, 0x0037f440 },
	{ 0x419c00,   1, 0x04, 0x0000001a },
	{ 0x419c04,   1, 0x04, 0x80000006 },
	{ 0x419c08,   1, 0x04, 0x00000002 },
	{ 0x419c20,   1, 0x04, 0x00000000 },
	{ 0x419c24,   1, 0x04, 0x00084210 },
	{ 0x419c28,   1, 0x04, 0x3efbefbe },
	{ 0x419ce8,   1, 0x04, 0x00000000 },
	{ 0x419cf4,   1, 0x04, 0x00000203 },
	{ 0x419e04,   1, 0x04, 0x00000000 },
	{ 0x419e08,   1, 0x04, 0x0000001d },
	{ 0x419e0c,   1, 0x04, 0x00000000 },
	{ 0x419e10,   1, 0x04, 0x00001c02 },
	{ 0x419e44,   1, 0x04, 0x0013eff2 },
	{ 0x419e48,   1, 0x04, 0x00000000 },
	{ 0x419e4c,   1, 0x04, 0x0000007f },
	{ 0x419e50,   2, 0x04, 0x00000000 },
	{ 0x419e58,   1, 0x04, 0x00000001 },
	{ 0x419e5c,   3, 0x04, 0x00000000 },
	{ 0x419e68,   1, 0x04, 0x00000002 },
	{ 0x419e6c,  12, 0x04, 0x00000000 },
	{ 0x419eac,   1, 0x04, 0x00001fcf },
	{ 0x419eb0,   1, 0x04, 0x0db00da0 },
	{ 0x419eb8,   1, 0x04, 0x00000000 },
	{ 0x419ec8,   1, 0x04, 0x0001304f },
	{ 0x419f30,   4, 0x04, 0x00000000 },
	{ 0x419f40,   1, 0x04, 0x00000018 },
	{ 0x419f44,   3, 0x04, 0x00000000 },
	{ 0x419f58,   1, 0x04, 0x00000000 },
	{ 0x419f70,   1, 0x04, 0x00007300 },
	{ 0x419f78,   1, 0x04, 0x000000eb },
	{ 0x419f7c,   1, 0x04, 0x00000404 },
	{}
};

static struct nvc0_graph_init
nvf0_grctx_init_unk[] = {
	{ 0x41be24,   1, 0x04, 0x00000006 },
	{ 0x41bec0,   1, 0x04, 0x10000000 },
	{ 0x41bec4,   1, 0x04, 0x00037f7f },
	{ 0x41bee4,   1, 0x04, 0x00000000 },
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

static void
nvf0_grctx_generate_mods(struct nvc0_graph_priv *priv, struct nvc0_grctx *info)
{
	u32 magic[GPC_MAX][4];
	u32 offset;
	int gpc;

	mmio_data(0x003000, 0x0100, NV_MEM_ACCESS_RW | NV_MEM_ACCESS_SYS);
	mmio_data(0x008000, 0x0100, NV_MEM_ACCESS_RW | NV_MEM_ACCESS_SYS);
	mmio_data(0x060000, 0x1000, NV_MEM_ACCESS_RW);
	mmio_list(0x40800c, 0x00000000,  8, 1);
	mmio_list(0x408010, 0x80000000,  0, 0);
	mmio_list(0x419004, 0x00000000,  8, 1);
	mmio_list(0x419008, 0x00000000,  0, 0);
	mmio_list(0x4064cc, 0x80000000,  0, 0);
	mmio_list(0x408004, 0x00000000,  8, 0);
	mmio_list(0x408008, 0x80000030,  0, 0);
	mmio_list(0x418808, 0x00000000,  8, 0);
	mmio_list(0x41880c, 0x80000030,  0, 0);
	mmio_list(0x4064c8, 0x01800600,  0, 0);
	mmio_list(0x418810, 0x80000000, 12, 2);
	mmio_list(0x419848, 0x10000000, 12, 2);

	mmio_list(0x405830, 0x02180648,  0, 0);
	mmio_list(0x4064c4, 0x0192ffff,  0, 0);

	for (gpc = 0, offset = 0; gpc < priv->gpc_nr; gpc++) {
		u16 magic0 = 0x0218 * (priv->tpc_nr[gpc] - 1);
		u16 magic1 = 0x0648 * (priv->tpc_nr[gpc] - 1);
		u16 magic2 = 0x0218;
		u16 magic3 = 0x0648;
		magic[gpc][0]  = 0x10000000 | (magic0 << 16) | offset;
		magic[gpc][1]  = 0x00000000 | (magic1 << 16);
		offset += 0x0324 * (priv->tpc_nr[gpc] - 1);;
		magic[gpc][2]  = 0x10000000 | (magic2 << 16) | offset;
		magic[gpc][3]  = 0x00000000 | (magic3 << 16);
		offset += 0x0324;
	}

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		mmio_list(GPC_UNIT(gpc, 0x30c0), magic[gpc][0], 0, 0);
		mmio_list(GPC_UNIT(gpc, 0x30e4), magic[gpc][1] | offset, 0, 0);
		offset += 0x07ff * (priv->tpc_nr[gpc] - 1);
		mmio_list(GPC_UNIT(gpc, 0x32c0), magic[gpc][2], 0, 0);
		mmio_list(GPC_UNIT(gpc, 0x32e4), magic[gpc][3] | offset, 0, 0);
		offset += 0x07ff;
	}

	mmio_list(0x17e91c, 0x06060609, 0, 0);
	mmio_list(0x17e920, 0x00090a05, 0, 0);
}

static struct nvc0_graph_init *
nvf0_grctx_init_hub[] = {
	nvc0_grctx_init_base,
	nvf0_grctx_init_unk40xx,
	nvf0_grctx_init_unk44xx,
	nve4_grctx_init_unk46xx,
	nve4_grctx_init_unk47xx,
	nve4_grctx_init_unk58xx,
	nvf0_grctx_init_unk5bxx,
	nvf0_grctx_init_unk60xx,
	nvf0_grctx_init_unk64xx,
	nve4_grctx_init_unk80xx,
	nvf0_grctx_init_unk88xx,
	nvd9_grctx_init_rop,
	NULL
};

struct nvc0_graph_init *
nvf0_grctx_init_gpc[] = {
	nvf0_grctx_init_gpc_0,
	nvc0_grctx_init_gpc_1,
	nvf0_grctx_init_tpc,
	nvf0_grctx_init_unk,
	NULL
};

static struct nvc0_graph_mthd
nvf0_grctx_init_mthd[] = {
	{ 0xa197, nvc1_grctx_init_9097, },
	{ 0x902d, nvc0_grctx_init_902d, },
	{ 0x902d, nvc0_grctx_init_mthd_magic, },
	{}
};

struct nouveau_oclass *
nvf0_grctx_oclass = &(struct nvc0_grctx_oclass) {
	.base.handle = NV_ENGCTX(GR, 0xf0),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_context_ctor,
		.dtor = nvc0_graph_context_dtor,
		.init = _nouveau_graph_context_init,
		.fini = _nouveau_graph_context_fini,
		.rd32 = _nouveau_graph_context_rd32,
		.wr32 = _nouveau_graph_context_wr32,
	},
	.main = nve4_grctx_generate_main,
	.mods = nvf0_grctx_generate_mods,
	.unkn = nve4_grctx_generate_unkn,
	.hub  = nvf0_grctx_init_hub,
	.gpc  = nvf0_grctx_init_gpc,
	.icmd = nvc0_grctx_init_icmd,
	.mthd = nvf0_grctx_init_mthd,
}.base;
