/*
 * Copyright 2018 Red Hat Inc.
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
 */
#include "ctxgf100.h"

/*******************************************************************************
 * PGRAPH context implementation
 ******************************************************************************/

const struct gf100_gr_init
gv100_grctx_init_sw_veid_bundle_init_0[] = {
	{ 0x00001000, 64, 0x00100000, 0x00000008 },
	{ 0x00000941, 64, 0x00100000, 0x00000000 },
	{ 0x0000097e, 64, 0x00100000, 0x00000000 },
	{ 0x0000097f, 64, 0x00100000, 0x00000100 },
	{ 0x0000035c, 64, 0x00100000, 0x00000000 },
	{ 0x0000035d, 64, 0x00100000, 0x00000000 },
	{ 0x00000a08, 64, 0x00100000, 0x00000000 },
	{ 0x00000a09, 64, 0x00100000, 0x00000000 },
	{ 0x00000a0a, 64, 0x00100000, 0x00000000 },
	{ 0x00000352, 64, 0x00100000, 0x00000000 },
	{ 0x00000353, 64, 0x00100000, 0x00000000 },
	{ 0x00000358, 64, 0x00100000, 0x00000000 },
	{ 0x00000359, 64, 0x00100000, 0x00000000 },
	{ 0x00000370, 64, 0x00100000, 0x00000000 },
	{ 0x00000371, 64, 0x00100000, 0x00000000 },
	{ 0x00000372, 64, 0x00100000, 0x000fffff },
	{ 0x00000366, 64, 0x00100000, 0x00000000 },
	{ 0x00000367, 64, 0x00100000, 0x00000000 },
	{ 0x00000368, 64, 0x00100000, 0x00000fff },
	{ 0x00000623, 64, 0x00100000, 0x00000000 },
	{ 0x00000624, 64, 0x00100000, 0x00000000 },
	{ 0x0001e100,  1, 0x00000001, 0x02000001 },
	{}
};

static const struct gf100_gr_pack
gv100_grctx_pack_sw_veid_bundle_init[] = {
	{ gv100_grctx_init_sw_veid_bundle_init_0 },
	{}
};

void
gv100_grctx_generate_attrib(struct gf100_grctx *info)
{
	struct gf100_gr *gr = info->gr;
	const struct gf100_grctx_func *grctx = gr->func->grctx;
	const u32  alpha = grctx->alpha_nr;
	const u32 attrib = grctx->attrib_nr;
	const u32   gfxp = grctx->gfxp_nr;
	const int s = 12;
	u32 size = grctx->alpha_nr_max * gr->tpc_total;
	u32 ao = 0;
	u32 bo = ao + size;
	int gpc, ppc, b, n = 0;

	for (gpc = 0; gpc < gr->gpc_nr; gpc++)
		size += grctx->gfxp_nr * gr->ppc_nr[gpc] * gr->ppc_tpc_max;
	size = ((size * 0x20) + 127) & ~127;
	b = mmio_vram(info, size, (1 << s), false);

	mmio_refn(info, 0x418810, 0x80000000, s, b);
	mmio_refn(info, 0x419848, 0x10000000, s, b);
	mmio_refn(info, 0x419c2c, 0x10000000, s, b);
	mmio_refn(info, 0x419e00, 0x00000000, s, b);
	mmio_wr32(info, 0x419e04, 0x80000000 | size >> 7);
	mmio_wr32(info, 0x405830, attrib);
	mmio_wr32(info, 0x40585c, alpha);

	for (gpc = 0; gpc < gr->gpc_nr; gpc++) {
		for (ppc = 0; ppc < gr->ppc_nr[gpc]; ppc++, n++) {
			const u32 as =  alpha * gr->ppc_tpc_nr[gpc][ppc];
			const u32 bs = attrib * gr->ppc_tpc_max;
			const u32 gs =   gfxp * gr->ppc_tpc_max;
			const u32 u = 0x418ea0 + (n * 0x04);
			const u32 o = PPC_UNIT(gpc, ppc, 0);
			if (!(gr->ppc_mask[gpc] & (1 << ppc)))
				continue;
			mmio_wr32(info, o + 0xc0, gs);
			mmio_wr32(info, o + 0xf4, bo);
			mmio_wr32(info, o + 0xf0, bs);
			bo += gs;
			mmio_wr32(info, o + 0xe4, as);
			mmio_wr32(info, o + 0xf8, ao);
			ao += grctx->alpha_nr_max * gr->ppc_tpc_nr[gpc][ppc];
			mmio_wr32(info, u, bs);
		}
	}

	mmio_wr32(info, 0x4181e4, 0x00000100);
	mmio_wr32(info, 0x41befc, 0x00000100);
}

void
gv100_grctx_generate_rop_mapping(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	u32 data;
	int i, j;

	/* Pack tile map into register format. */
	nvkm_wr32(device, 0x418bb8, (gr->tpc_total << 8) |
				     gr->screen_tile_row_offset);
	for (i = 0; i < 11; i++) {
		for (data = 0, j = 0; j < 6; j++)
			data |= (gr->tile[i * 6 + j] & 0x1f) << (j * 5);
		nvkm_wr32(device, 0x418b08 + (i * 4), data);
		nvkm_wr32(device, 0x41bf00 + (i * 4), data);
		nvkm_wr32(device, 0x40780c + (i * 4), data);
	}

	/* GPC_BROADCAST.TP_BROADCAST */
	nvkm_wr32(device, 0x41bfd0, (gr->tpc_total << 8) |
				     gr->screen_tile_row_offset);
	for (i = 0, j = 1; i < 5; i++, j += 4) {
		u8 v19 = (1 << (j + 0)) % gr->tpc_total;
		u8 v20 = (1 << (j + 1)) % gr->tpc_total;
		u8 v21 = (1 << (j + 2)) % gr->tpc_total;
		u8 v22 = (1 << (j + 3)) % gr->tpc_total;
		nvkm_wr32(device, 0x41bfb0 + (i * 4), (v22 << 24) |
						      (v21 << 16) |
						      (v20 <<  8) |
						       v19);
	}

	/* UNK78xx */
	nvkm_wr32(device, 0x4078bc, (gr->tpc_total << 8) |
				     gr->screen_tile_row_offset);
}

void
gv100_grctx_generate_r400088(struct gf100_gr *gr, bool on)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_mask(device, 0x400088, 0x00060000, on ? 0x00060000 : 0x00000000);
}

static void
gv100_grctx_generate_sm_id(struct gf100_gr *gr, int gpc, int tpc, int sm)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x608), sm);
	nvkm_wr32(device, GPC_UNIT(gpc, 0x0c10 + tpc * 4), sm);
	nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x088), sm);
}

void
gv100_grctx_generate_unkn(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_mask(device, 0x41980c, 0x00000010, 0x00000010);
	nvkm_mask(device, 0x41be08, 0x00000004, 0x00000004);
	nvkm_mask(device, 0x4064c0, 0x80000000, 0x80000000);
	nvkm_mask(device, 0x405800, 0x08000000, 0x08000000);
	nvkm_mask(device, 0x419c00, 0x00000008, 0x00000008);
}

void
gv100_grctx_unkn88c(struct gf100_gr *gr, bool on)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const u32 mask = 0x00000010, data = on ? mask : 0x00000000;
	nvkm_mask(device, 0x40988c, mask, data);
	nvkm_rd32(device, 0x40988c);
	nvkm_mask(device, 0x41a88c, mask, data);
	nvkm_rd32(device, 0x41a88c);
	nvkm_mask(device, 0x408a14, mask, data);
	nvkm_rd32(device, 0x408a14);
}

const struct gf100_grctx_func
gv100_grctx = {
	.unkn88c = gv100_grctx_unkn88c,
	.main = gf100_grctx_generate_main,
	.unkn = gv100_grctx_generate_unkn,
	.sw_veid_bundle_init = gv100_grctx_pack_sw_veid_bundle_init,
	.bundle = gm107_grctx_generate_bundle,
	.bundle_size = 0x3000,
	.bundle_min_gpm_fifo_depth = 0x180,
	.bundle_token_limit = 0x1680,
	.pagepool = gp100_grctx_generate_pagepool,
	.pagepool_size = 0x20000,
	.attrib = gv100_grctx_generate_attrib,
	.attrib_nr_max = 0x6c0,
	.attrib_nr = 0x480,
	.alpha_nr_max = 0xc00,
	.alpha_nr = 0x800,
	.gfxp_nr = 0xd10,
	.sm_id = gv100_grctx_generate_sm_id,
	.rop_mapping = gv100_grctx_generate_rop_mapping,
	.dist_skip_table = gm200_grctx_generate_dist_skip_table,
	.r406500 = gm200_grctx_generate_r406500,
	.gpc_tpc_nr = gk104_grctx_generate_gpc_tpc_nr,
	.smid_config = gp100_grctx_generate_smid_config,
	.r400088 = gv100_grctx_generate_r400088,
};
