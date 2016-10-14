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
#include "ctxgf100.h"

#include <subdev/fb.h>
#include <subdev/mc.h>

/*******************************************************************************
 * PGRAPH context register lists
 ******************************************************************************/

static const struct gf100_gr_init
gf117_grctx_init_ds_0[] = {
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

static const struct gf100_gr_init
gf117_grctx_init_pd_0[] = {
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

static const struct gf100_gr_pack
gf117_grctx_pack_hub[] = {
	{ gf100_grctx_init_main_0 },
	{ gf119_grctx_init_fe_0 },
	{ gf100_grctx_init_pri_0 },
	{ gf100_grctx_init_memfmt_0 },
	{ gf117_grctx_init_ds_0 },
	{ gf117_grctx_init_pd_0 },
	{ gf100_grctx_init_rstr2d_0 },
	{ gf100_grctx_init_scc_0 },
	{ gf119_grctx_init_be_0 },
	{}
};

static const struct gf100_gr_init
gf117_grctx_init_setup_0[] = {
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

static const struct gf100_gr_pack
gf117_grctx_pack_gpc[] = {
	{ gf100_grctx_init_gpc_unk_0 },
	{ gf119_grctx_init_prop_0 },
	{ gf119_grctx_init_gpc_unk_1 },
	{ gf117_grctx_init_setup_0 },
	{ gf100_grctx_init_zcull_0 },
	{ gf119_grctx_init_crstr_0 },
	{ gf108_grctx_init_gpm_0 },
	{ gf100_grctx_init_gcc_0 },
	{}
};

const struct gf100_gr_init
gf117_grctx_init_pe_0[] = {
	{ 0x419848,   1, 0x04, 0x00000000 },
	{ 0x419864,   1, 0x04, 0x00000129 },
	{ 0x419888,   1, 0x04, 0x00000000 },
	{}
};

static const struct gf100_gr_init
gf117_grctx_init_tex_0[] = {
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

static const struct gf100_gr_init
gf117_grctx_init_mpc_0[] = {
	{ 0x419c00,   1, 0x04, 0x0000000a },
	{ 0x419c04,   1, 0x04, 0x00000006 },
	{ 0x419c08,   1, 0x04, 0x00000002 },
	{ 0x419c20,   1, 0x04, 0x00000000 },
	{ 0x419c24,   1, 0x04, 0x00084210 },
	{ 0x419c28,   1, 0x04, 0x3efbefbe },
	{}
};

static const struct gf100_gr_pack
gf117_grctx_pack_tpc[] = {
	{ gf117_grctx_init_pe_0 },
	{ gf117_grctx_init_tex_0 },
	{ gf117_grctx_init_mpc_0 },
	{ gf104_grctx_init_l1c_0 },
	{ gf119_grctx_init_sm_0 },
	{}
};

static const struct gf100_gr_init
gf117_grctx_init_pes_0[] = {
	{ 0x41be24,   1, 0x04, 0x00000002 },
	{}
};

static const struct gf100_gr_init
gf117_grctx_init_cbm_0[] = {
	{ 0x41bec0,   1, 0x04, 0x12180000 },
	{ 0x41bec4,   1, 0x04, 0x00003fff },
	{ 0x41bee4,   1, 0x04, 0x03240218 },
	{}
};

const struct gf100_gr_init
gf117_grctx_init_wwdx_0[] = {
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

static const struct gf100_gr_pack
gf117_grctx_pack_ppc[] = {
	{ gf117_grctx_init_pes_0 },
	{ gf117_grctx_init_cbm_0 },
	{ gf117_grctx_init_wwdx_0 },
	{}
};

/*******************************************************************************
 * PGRAPH context implementation
 ******************************************************************************/

void
gf117_grctx_generate_attrib(struct gf100_grctx *info)
{
	struct gf100_gr *gr = info->gr;
	const struct gf100_grctx_func *grctx = gr->func->grctx;
	const u32  alpha = grctx->alpha_nr;
	const u32   beta = grctx->attrib_nr;
	const u32   size = 0x20 * (grctx->attrib_nr_max + grctx->alpha_nr_max);
	const u32 access = NV_MEM_ACCESS_RW;
	const int s = 12;
	const int b = mmio_vram(info, size * gr->tpc_total, (1 << s), access);
	const int timeslice_mode = 1;
	const int max_batches = 0xffff;
	u32 bo = 0;
	u32 ao = bo + grctx->attrib_nr_max * gr->tpc_total;
	int gpc, ppc;

	mmio_refn(info, 0x418810, 0x80000000, s, b);
	mmio_refn(info, 0x419848, 0x10000000, s, b);
	mmio_wr32(info, 0x405830, (beta << 16) | alpha);
	mmio_wr32(info, 0x4064c4, ((alpha / 4) << 16) | max_batches);

	for (gpc = 0; gpc < gr->gpc_nr; gpc++) {
		for (ppc = 0; ppc < gr->ppc_nr[gpc]; ppc++) {
			const u32 a = alpha * gr->ppc_tpc_nr[gpc][ppc];
			const u32 b =  beta * gr->ppc_tpc_nr[gpc][ppc];
			const u32 t = timeslice_mode;
			const u32 o = PPC_UNIT(gpc, ppc, 0);
			if (!(gr->ppc_mask[gpc] & (1 << ppc)))
				continue;
			mmio_skip(info, o + 0xc0, (t << 28) | (b << 16) | ++bo);
			mmio_wr32(info, o + 0xc0, (t << 28) | (b << 16) | --bo);
			bo += grctx->attrib_nr_max * gr->ppc_tpc_nr[gpc][ppc];
			mmio_wr32(info, o + 0xe4, (a << 16) | ao);
			ao += grctx->alpha_nr_max * gr->ppc_tpc_nr[gpc][ppc];
		}
	}
}

void
gf117_grctx_generate_main(struct gf100_gr *gr, struct gf100_grctx *info)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const struct gf100_grctx_func *grctx = gr->func->grctx;
	u32 idle_timeout;
	int i;

	nvkm_mc_unk260(device, 0);

	gf100_gr_mmio(gr, grctx->hub);
	gf100_gr_mmio(gr, grctx->gpc);
	gf100_gr_mmio(gr, grctx->zcull);
	gf100_gr_mmio(gr, grctx->tpc);
	gf100_gr_mmio(gr, grctx->ppc);

	idle_timeout = nvkm_mask(device, 0x404154, 0xffffffff, 0x00000000);

	grctx->bundle(info);
	grctx->pagepool(info);
	grctx->attrib(info);
	grctx->unkn(gr);

	gf100_grctx_generate_tpcid(gr);
	gf100_grctx_generate_r406028(gr);
	gf100_grctx_generate_r4060a8(gr);
	gk104_grctx_generate_r418bb8(gr);
	gf100_grctx_generate_r406800(gr);

	for (i = 0; i < 8; i++)
		nvkm_wr32(device, 0x4064d0 + (i * 0x04), 0x00000000);

	gf100_gr_icmd(gr, grctx->icmd);
	nvkm_wr32(device, 0x404154, idle_timeout);
	gf100_gr_mthd(gr, grctx->mthd);
	nvkm_mc_unk260(device, 1);
}

const struct gf100_grctx_func
gf117_grctx = {
	.main  = gf117_grctx_generate_main,
	.unkn  = gk104_grctx_generate_unkn,
	.hub   = gf117_grctx_pack_hub,
	.gpc   = gf117_grctx_pack_gpc,
	.zcull = gf100_grctx_pack_zcull,
	.tpc   = gf117_grctx_pack_tpc,
	.ppc   = gf117_grctx_pack_ppc,
	.icmd  = gf119_grctx_pack_icmd,
	.mthd  = gf119_grctx_pack_mthd,
	.bundle = gf100_grctx_generate_bundle,
	.bundle_size = 0x1800,
	.pagepool = gf100_grctx_generate_pagepool,
	.pagepool_size = 0x8000,
	.attrib = gf117_grctx_generate_attrib,
	.attrib_nr_max = 0x324,
	.attrib_nr = 0x218,
	.alpha_nr_max = 0x7ff,
	.alpha_nr = 0x324,
};
