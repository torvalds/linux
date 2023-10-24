/*
 * Copyright 2019 Red Hat Inc.
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

void
tu102_gr_init_fecs_exceptions(struct gf100_gr *gr)
{
	nvkm_wr32(gr->base.engine.subdev.device, 0x409c24, 0x006e0003);
}

void
tu102_gr_init_fs(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	int sm;

	gp100_grctx_generate_smid_config(gr);
	gk104_grctx_generate_gpc_tpc_nr(gr);

	for (sm = 0; sm < gr->sm_nr; sm++) {
		int tpc = gv100_gr_nonpes_aware_tpc(gr, gr->sm[sm].gpc, gr->sm[sm].tpc);

		nvkm_wr32(device, GPC_UNIT(gr->sm[sm].gpc, 0x0c10 + tpc * 4), sm);
	}

	gm200_grctx_generate_dist_skip_table(gr);
	gf100_gr_init_num_tpc_per_gpc(gr, true, true);
}

void
tu102_gr_init_zcull(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const u32 magicgpc918 = DIV_ROUND_UP(0x00800000, gr->tpc_total);
	const u8 tile_nr = gr->func->gpc_nr * gr->func->tpc_nr;
	u8 bank[GPC_MAX] = {}, gpc, i, j;
	u32 data;

	for (i = 0; i < tile_nr; i += 8) {
		for (data = 0, j = 0; j < 8 && i + j < gr->tpc_total; j++) {
			data |= bank[gr->tile[i + j]] << (j * 4);
			bank[gr->tile[i + j]]++;
		}
		nvkm_wr32(device, GPC_BCAST(0x0980 + ((i / 8) * 4)), data);
	}

	for (gpc = 0; gpc < gr->gpc_nr; gpc++) {
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0914),
			  gr->screen_tile_row_offset << 8 | gr->tpc_nr[gpc]);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0910), 0x00040000 |
							 gr->tpc_total);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0918), magicgpc918);
	}

	nvkm_wr32(device, GPC_BCAST(0x3fd4), magicgpc918);
}

static void
tu102_gr_init_gpc_mmu(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;

	nvkm_wr32(device, 0x418880, nvkm_rd32(device, 0x100c80) & 0xf8001fff);
	nvkm_wr32(device, 0x418890, 0x00000000);
	nvkm_wr32(device, 0x418894, 0x00000000);

	nvkm_wr32(device, 0x4188b4, nvkm_rd32(device, 0x100cc8));
	nvkm_wr32(device, 0x4188b8, nvkm_rd32(device, 0x100ccc));
	nvkm_wr32(device, 0x4188b0, nvkm_rd32(device, 0x100cc4));
}

static const struct gf100_gr_func
tu102_gr = {
	.oneinit_tiles = gm200_gr_oneinit_tiles,
	.oneinit_sm_id = gv100_gr_oneinit_sm_id,
	.init = gf100_gr_init,
	.init_419bd8 = gv100_gr_init_419bd8,
	.init_gpc_mmu = tu102_gr_init_gpc_mmu,
	.init_vsc_stream_master = gk104_gr_init_vsc_stream_master,
	.init_zcull = tu102_gr_init_zcull,
	.init_num_active_ltcs = gf100_gr_init_num_active_ltcs,
	.init_rop_active_fbps = gp100_gr_init_rop_active_fbps,
	.init_swdx_pes_mask = gp102_gr_init_swdx_pes_mask,
	.init_fs = tu102_gr_init_fs,
	.init_fecs_exceptions = tu102_gr_init_fecs_exceptions,
	.init_ds_hww_esr_2 = gm200_gr_init_ds_hww_esr_2,
	.init_sked_hww_esr = gk104_gr_init_sked_hww_esr,
	.init_ppc_exceptions = gk104_gr_init_ppc_exceptions,
	.init_504430 = gv100_gr_init_504430,
	.init_shader_exceptions = gv100_gr_init_shader_exceptions,
	.init_rop_exceptions = gf100_gr_init_rop_exceptions,
	.init_exception2 = gf100_gr_init_exception2,
	.init_4188a4 = gv100_gr_init_4188a4,
	.trap_mp = gv100_gr_trap_mp,
	.fecs.reset = gf100_gr_fecs_reset,
	.rops = gm200_gr_rops,
	.gpc_nr = 6,
	.tpc_nr = 6,
	.ppc_nr = 3,
	.grctx = &tu102_grctx,
	.zbc = &gp102_gr_zbc,
	.sclass = {
		{ -1, -1, FERMI_TWOD_A },
		{ -1, -1, KEPLER_INLINE_TO_MEMORY_B },
		{ -1, -1, TURING_A, &gf100_fermi },
		{ -1, -1, TURING_COMPUTE_A },
		{}
	}
};

MODULE_FIRMWARE("nvidia/tu102/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/tu102/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/tu102/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/tu102/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/tu102/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/tu102/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/tu102/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/tu102/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/tu102/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/tu102/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/tu102/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/tu102/gr/sw_method_init.bin");
MODULE_FIRMWARE("nvidia/tu102/gr/sw_veid_bundle_init.bin");

MODULE_FIRMWARE("nvidia/tu104/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/tu104/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/tu104/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/tu104/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/tu104/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/tu104/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/tu104/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/tu104/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/tu104/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/tu104/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/tu104/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/tu104/gr/sw_method_init.bin");
MODULE_FIRMWARE("nvidia/tu104/gr/sw_veid_bundle_init.bin");

MODULE_FIRMWARE("nvidia/tu106/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/tu106/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/tu106/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/tu106/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/tu106/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/tu106/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/tu106/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/tu106/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/tu106/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/tu106/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/tu106/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/tu106/gr/sw_method_init.bin");
MODULE_FIRMWARE("nvidia/tu106/gr/sw_veid_bundle_init.bin");

MODULE_FIRMWARE("nvidia/tu117/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/tu117/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/tu117/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/tu117/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/tu117/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/tu117/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/tu117/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/tu117/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/tu117/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/tu117/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/tu117/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/tu117/gr/sw_method_init.bin");
MODULE_FIRMWARE("nvidia/tu117/gr/sw_veid_bundle_init.bin");

MODULE_FIRMWARE("nvidia/tu116/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/tu116/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/tu116/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/tu116/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/tu116/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/tu116/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/tu116/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/tu116/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/tu116/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/tu116/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/tu116/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/tu116/gr/sw_method_init.bin");
MODULE_FIRMWARE("nvidia/tu116/gr/sw_veid_bundle_init.bin");

int
tu102_gr_av_to_init_veid(struct nvkm_blob *blob, struct gf100_gr_pack **ppack)
{
	return gk20a_gr_av_to_init_(blob, 64, 0x00100000, ppack);
}

static const struct gf100_gr_fwif
tu102_gr_fwif[] = {
	{  0, gm200_gr_load, &tu102_gr, &gp108_gr_fecs_acr, &gp108_gr_gpccs_acr },
	{ -1, gm200_gr_nofw },
	{}
};

int
tu102_gr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_gr **pgr)
{
	return gf100_gr_new_(tu102_gr_fwif, device, type, inst, pgr);
}
