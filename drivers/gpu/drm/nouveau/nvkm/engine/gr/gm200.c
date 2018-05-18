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

#include <subdev/secboot.h>

#include <nvif/class.h>

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

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

void
gm200_gr_oneinit_sm_id(struct gf100_gr *gr)
{
	/*XXX: There's a different algorithm here I've not yet figured out. */
	gf100_gr_oneinit_sm_id(gr);
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

int
gm200_gr_new_(const struct gf100_gr_func *func, struct nvkm_device *device,
	      int index, struct nvkm_gr **pgr)
{
	struct gf100_gr *gr;
	int ret;

	if (!(gr = kzalloc(sizeof(*gr), GFP_KERNEL)))
		return -ENOMEM;
	*pgr = &gr->base;

	ret = gf100_gr_ctor(func, device, index, gr);
	if (ret)
		return ret;

	/* Load firmwares for non-secure falcons */
	if (!nvkm_secboot_is_managed(device->secboot,
				     NVKM_SECBOOT_FALCON_FECS)) {
		if ((ret = gf100_gr_ctor_fw(gr, "gr/fecs_inst", &gr->fuc409c)) ||
		    (ret = gf100_gr_ctor_fw(gr, "gr/fecs_data", &gr->fuc409d)))
			return ret;
	}
	if (!nvkm_secboot_is_managed(device->secboot,
				     NVKM_SECBOOT_FALCON_GPCCS)) {
		if ((ret = gf100_gr_ctor_fw(gr, "gr/gpccs_inst", &gr->fuc41ac)) ||
		    (ret = gf100_gr_ctor_fw(gr, "gr/gpccs_data", &gr->fuc41ad)))
			return ret;
	}

	if ((ret = gk20a_gr_av_to_init(gr, "gr/sw_nonctx", &gr->fuc_sw_nonctx)) ||
	    (ret = gk20a_gr_aiv_to_init(gr, "gr/sw_ctx", &gr->fuc_sw_ctx)) ||
	    (ret = gk20a_gr_av_to_init(gr, "gr/sw_bundle_init", &gr->fuc_bundle)) ||
	    (ret = gk20a_gr_av_to_method(gr, "gr/sw_method_init", &gr->fuc_method)))
		return ret;

	return 0;
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
	.init_400054 = gm107_gr_init_400054,
	.trap_mp = gf100_gr_trap_mp,
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
gm200_gr_new(struct nvkm_device *device, int index, struct nvkm_gr **pgr)
{
	return gm200_gr_new_(&gm200_gr, device, index, pgr);
}
