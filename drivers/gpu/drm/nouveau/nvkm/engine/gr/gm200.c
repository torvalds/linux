/*
 * Copyright 2015 Red Hat Inc.
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
#include "gf100.h"
#include "ctxgf100.h"

#include <core/firmware.h>
#include <subdev/acr.h>

#include <nvfw/flcn.h>

#include <nvif/class.h>

int
gm200_gr_nofw(struct gf100_gr *gr, int ver, const struct gf100_gr_fwif *fwif)
{
	nvkm_warn(&gr->base.engine.subdev, "firmware unavailable\n");
	return -ENODEV;
}

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

static void
gm200_gr_acr_bld_patch(struct nvkm_acr *acr, u32 bld, s64 adjust)
{
	struct flcn_bl_dmem_desc_v1 hdr;
	nvkm_robj(acr->wpr, bld, &hdr, sizeof(hdr));
	hdr.code_dma_base = hdr.code_dma_base + adjust;
	hdr.data_dma_base = hdr.data_dma_base + adjust;
	nvkm_wobj(acr->wpr, bld, &hdr, sizeof(hdr));
	flcn_bl_dmem_desc_v1_dump(&acr->subdev, &hdr);
}

static void
gm200_gr_acr_bld_write(struct nvkm_acr *acr, u32 bld,
		       struct nvkm_acr_lsfw *lsfw)
{
	const u64 base = lsfw->offset.img + lsfw->app_start_offset;
	const u64 code = base + lsfw->app_resident_code_offset;
	const u64 data = base + lsfw->app_resident_data_offset;
	const struct flcn_bl_dmem_desc_v1 hdr = {
		.ctx_dma = FALCON_DMAIDX_UCODE,
		.code_dma_base = code,
		.non_sec_code_off = lsfw->app_resident_code_offset,
		.non_sec_code_size = lsfw->app_resident_code_size,
		.code_entry_point = lsfw->app_imem_entry,
		.data_dma_base = data,
		.data_size = lsfw->app_resident_data_size,
	};

	nvkm_wobj(acr->wpr, bld, &hdr, sizeof(hdr));
}

const struct nvkm_acr_lsf_func
gm200_gr_gpccs_acr = {
	.flags = NVKM_ACR_LSF_FORCE_PRIV_LOAD,
	.bld_size = sizeof(struct flcn_bl_dmem_desc_v1),
	.bld_write = gm200_gr_acr_bld_write,
	.bld_patch = gm200_gr_acr_bld_patch,
};

const struct nvkm_acr_lsf_func
gm200_gr_fecs_acr = {
	.bld_size = sizeof(struct flcn_bl_dmem_desc_v1),
	.bld_write = gm200_gr_acr_bld_write,
	.bld_patch = gm200_gr_acr_bld_patch,
};

int
gm200_gr_rops(struct gf100_gr *gr)
{
	return nvkm_rd32(gr->base.engine.subdev.device, 0x12006c);
}

void
gm200_gr_init_ds_hww_esr_2(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_wr32(device, 0x405848, 0xc0000000);
	nvkm_mask(device, 0x40584c, 0x00000001, 0x00000001);
}

void
gm200_gr_init_num_active_ltcs(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_wr32(device, GPC_BCAST(0x08ac), nvkm_rd32(device, 0x100800));
	nvkm_wr32(device, GPC_BCAST(0x033c), nvkm_rd32(device, 0x100804));
}

void
gm200_gr_init_gpc_mmu(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;

	nvkm_wr32(device, 0x418880, nvkm_rd32(device, 0x100c80) & 0xf0001fff);
	nvkm_wr32(device, 0x418890, 0x00000000);
	nvkm_wr32(device, 0x418894, 0x00000000);

	nvkm_wr32(device, 0x4188b4, nvkm_rd32(device, 0x100cc8));
	nvkm_wr32(device, 0x4188b8, nvkm_rd32(device, 0x100ccc));
	nvkm_wr32(device, 0x4188b0, nvkm_rd32(device, 0x100cc4));
}

static void
gm200_gr_init_rop_active_fbps(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const u32 fbp_count = nvkm_rd32(device, 0x12006c);
	nvkm_mask(device, 0x408850, 0x0000000f, fbp_count); /* zrop */
	nvkm_mask(device, 0x408958, 0x0000000f, fbp_count); /* crop */
}

static u8
gm200_gr_tile_map_6_24[] = {
	0, 1, 2, 3, 4, 5, 3, 4, 5, 0, 1, 2, 0, 1, 2, 3, 4, 5, 3, 4, 5, 0, 1, 2,
};

static u8
gm200_gr_tile_map_4_16[] = {
	0, 1, 2, 3, 2, 3, 0, 1, 3, 0, 1, 2, 1, 2, 3, 0,
};

static u8
gm200_gr_tile_map_2_8[] = {
	0, 1, 1, 0, 0, 1, 1, 0,
};

int
gm200_gr_oneinit_sm_id(struct gf100_gr *gr)
{
	/*XXX: There's a different algorithm here I've not yet figured out. */
	return gf100_gr_oneinit_sm_id(gr);
}

void
gm200_gr_oneinit_tiles(struct gf100_gr *gr)
{
	/*XXX: Not sure what this is about.  The algorithm from NVGPU
	 *     seems to work for all boards I tried from earlier (and
	 *     later) GPUs except in these specific configurations.
	 *
	 *     Let's just hardcode them for now.
	 */
	if (gr->gpc_nr == 2 && gr->tpc_total == 8) {
		memcpy(gr->tile, gm200_gr_tile_map_2_8, gr->tpc_total);
		gr->screen_tile_row_offset = 1;
	} else
	if (gr->gpc_nr == 4 && gr->tpc_total == 16) {
		memcpy(gr->tile, gm200_gr_tile_map_4_16, gr->tpc_total);
		gr->screen_tile_row_offset = 4;
	} else
	if (gr->gpc_nr == 6 && gr->tpc_total == 24) {
		memcpy(gr->tile, gm200_gr_tile_map_6_24, gr->tpc_total);
		gr->screen_tile_row_offset = 5;
	} else {
		gf100_gr_oneinit_tiles(gr);
	}
}

static const struct gf100_gr_func
gm200_gr = {
	.oneinit_tiles = gm200_gr_oneinit_tiles,
	.oneinit_sm_id = gm200_gr_oneinit_sm_id,
	.init = gf100_gr_init,
	.init_gpc_mmu = gm200_gr_init_gpc_mmu,
	.init_bios = gm107_gr_init_bios,
	.init_vsc_stream_master = gk104_gr_init_vsc_stream_master,
	.init_zcull = gf117_gr_init_zcull,
	.init_num_active_ltcs = gm200_gr_init_num_active_ltcs,
	.init_rop_active_fbps = gm200_gr_init_rop_active_fbps,
	.init_fecs_exceptions = gf100_gr_init_fecs_exceptions,
	.init_ds_hww_esr_2 = gm200_gr_init_ds_hww_esr_2,
	.init_sked_hww_esr = gk104_gr_init_sked_hww_esr,
	.init_419cc0 = gf100_gr_init_419cc0,
	.init_ppc_exceptions = gk104_gr_init_ppc_exceptions,
	.init_tex_hww_esr = gf100_gr_init_tex_hww_esr,
	.init_504430 = gm107_gr_init_504430,
	.init_shader_exceptions = gm107_gr_init_shader_exceptions,
	.init_rop_exceptions = gf100_gr_init_rop_exceptions,
	.init_exception2 = gf100_gr_init_exception2,
	.init_400054 = gm107_gr_init_400054,
	.trap_mp = gf100_gr_trap_mp,
	.fecs.reset = gf100_gr_fecs_reset,
	.rops = gm200_gr_rops,
	.tpc_nr = 4,
	.ppc_nr = 2,
	.grctx = &gm200_grctx,
	.zbc = &gf100_gr_zbc,
	.sclass = {
		{ -1, -1, FERMI_TWOD_A },
		{ -1, -1, KEPLER_INLINE_TO_MEMORY_B },
		{ -1, -1, MAXWELL_B, &gf100_fermi },
		{ -1, -1, MAXWELL_COMPUTE_B },
		{}
	}
};

int
gm200_gr_load(struct gf100_gr *gr, int ver, const struct gf100_gr_fwif *fwif)
{
	int ret;

	ret = nvkm_acr_lsfw_load_bl_inst_data_sig(&gr->base.engine.subdev,
						  &gr->fecs.falcon,
						  NVKM_ACR_LSF_FECS,
						  "gr/fecs_", ver, fwif->fecs);
	if (ret)
		return ret;

	ret = nvkm_acr_lsfw_load_bl_inst_data_sig(&gr->base.engine.subdev,
						  &gr->gpccs.falcon,
						  NVKM_ACR_LSF_GPCCS,
						  "gr/gpccs_", ver,
						  fwif->gpccs);
	if (ret)
		return ret;

	gr->firmware = true;

	return gk20a_gr_load_sw(gr, "gr/", ver);
}

MODULE_FIRMWARE("nvidia/gm200/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/sw_method_init.bin");

MODULE_FIRMWARE("nvidia/gm204/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/sw_method_init.bin");

MODULE_FIRMWARE("nvidia/gm206/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/sw_method_init.bin");

static const struct gf100_gr_fwif
gm200_gr_fwif[] = {
	{  0, gm200_gr_load, &gm200_gr, &gm200_gr_fecs_acr, &gm200_gr_gpccs_acr },
	{ -1, gm200_gr_nofw },
	{}
};

int
gm200_gr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_gr **pgr)
{
	return gf100_gr_new_(gm200_gr_fwif, device, type, inst, pgr);
}
