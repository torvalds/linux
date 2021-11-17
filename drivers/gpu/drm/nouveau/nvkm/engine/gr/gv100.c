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
#include "gf100.h"
#include "ctxgf100.h"

#include <nvif/class.h>

static void
gv100_gr_trap_sm(struct gf100_gr *gr, int gpc, int tpc, int sm)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 werr = nvkm_rd32(device, TPC_UNIT(gpc, tpc, 0x730 + (sm * 0x80)));
	u32 gerr = nvkm_rd32(device, TPC_UNIT(gpc, tpc, 0x734 + (sm * 0x80)));
	const struct nvkm_enum *warp;
	char glob[128];

	nvkm_snprintbf(glob, sizeof(glob), gf100_mp_global_error, gerr);
	warp = nvkm_enum_find(gf100_mp_warp_error, werr & 0xffff);

	nvkm_error(subdev, "GPC%i/TPC%i/SM%d trap: "
			   "global %08x [%s] warp %04x [%s]\n",
		   gpc, tpc, sm, gerr, glob, werr, warp ? warp->name : "");

	nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x730 + sm * 0x80), 0x00000000);
	nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x734 + sm * 0x80), gerr);
}

void
gv100_gr_trap_mp(struct gf100_gr *gr, int gpc, int tpc)
{
	gv100_gr_trap_sm(gr, gpc, tpc, 0);
	gv100_gr_trap_sm(gr, gpc, tpc, 1);
}

static void
gv100_gr_init_4188a4(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_mask(device, 0x4188a4, 0x03000000, 0x03000000);
}

void
gv100_gr_init_shader_exceptions(struct gf100_gr *gr, int gpc, int tpc)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	int sm;
	for (sm = 0; sm < 0x100; sm += 0x80) {
		nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x728 + sm), 0x0085eb64);
		nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x610), 0x00000001);
		nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x72c + sm), 0x00000004);
	}
}

void
gv100_gr_init_504430(struct gf100_gr *gr, int gpc, int tpc)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x430), 0x403f0000);
}

void
gv100_gr_init_419bd8(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_mask(device, 0x419bd8, 0x00000700, 0x00000000);
}

static const struct gf100_gr_func
gv100_gr = {
	.oneinit_tiles = gm200_gr_oneinit_tiles,
	.oneinit_sm_id = gm200_gr_oneinit_sm_id,
	.init = gf100_gr_init,
	.init_419bd8 = gv100_gr_init_419bd8,
	.init_gpc_mmu = gm200_gr_init_gpc_mmu,
	.init_vsc_stream_master = gk104_gr_init_vsc_stream_master,
	.init_zcull = gf117_gr_init_zcull,
	.init_num_active_ltcs = gm200_gr_init_num_active_ltcs,
	.init_rop_active_fbps = gp100_gr_init_rop_active_fbps,
	.init_swdx_pes_mask = gp102_gr_init_swdx_pes_mask,
	.init_fecs_exceptions = gp100_gr_init_fecs_exceptions,
	.init_ds_hww_esr_2 = gm200_gr_init_ds_hww_esr_2,
	.init_sked_hww_esr = gk104_gr_init_sked_hww_esr,
	.init_ppc_exceptions = gk104_gr_init_ppc_exceptions,
	.init_504430 = gv100_gr_init_504430,
	.init_shader_exceptions = gv100_gr_init_shader_exceptions,
	.init_4188a4 = gv100_gr_init_4188a4,
	.trap_mp = gv100_gr_trap_mp,
	.rops = gm200_gr_rops,
	.gpc_nr = 6,
	.tpc_nr = 5,
	.ppc_nr = 3,
	.grctx = &gv100_grctx,
	.zbc = &gp102_gr_zbc,
	.sclass = {
		{ -1, -1, FERMI_TWOD_A },
		{ -1, -1, KEPLER_INLINE_TO_MEMORY_B },
		{ -1, -1, VOLTA_A, &gf100_fermi },
		{ -1, -1, VOLTA_COMPUTE_A },
		{}
	}
};

MODULE_FIRMWARE("nvidia/gv100/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/sw_method_init.bin");

static const struct gf100_gr_fwif
gv100_gr_fwif[] = {
	{  0, gm200_gr_load, &gv100_gr, &gp108_gr_fecs_acr, &gp108_gr_gpccs_acr },
	{ -1, gm200_gr_nofw },
	{}
};

int
gv100_gr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_gr **pgr)
{
	return gf100_gr_new_(gv100_gr_fwif, device, type, inst, pgr);
}
