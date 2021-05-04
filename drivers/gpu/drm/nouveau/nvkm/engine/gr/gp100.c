/*
 * Copyright 2016 Red Hat Inc.
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

#include <nvif/class.h>

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/
void
gp100_gr_zbc_clear_color(struct gf100_gr *gr, int zbc)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const int znum =  zbc - 1;
	const u32 zoff = znum * 4;

	if (gr->zbc_color[zbc].format) {
		nvkm_wr32(device, 0x418010 + zoff, gr->zbc_color[zbc].ds[0]);
		nvkm_wr32(device, 0x41804c + zoff, gr->zbc_color[zbc].ds[1]);
		nvkm_wr32(device, 0x418088 + zoff, gr->zbc_color[zbc].ds[2]);
		nvkm_wr32(device, 0x4180c4 + zoff, gr->zbc_color[zbc].ds[3]);
	}

	nvkm_mask(device, 0x418100 + ((znum / 4) * 4),
			  0x0000007f << ((znum % 4) * 7),
			  gr->zbc_color[zbc].format << ((znum % 4) * 7));
}

void
gp100_gr_zbc_clear_depth(struct gf100_gr *gr, int zbc)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const int znum =  zbc - 1;
	const u32 zoff = znum * 4;

	if (gr->zbc_depth[zbc].format)
		nvkm_wr32(device, 0x418110 + zoff, gr->zbc_depth[zbc].ds);
	nvkm_mask(device, 0x41814c + ((znum / 4) * 4),
			  0x0000007f << ((znum % 4) * 7),
			  gr->zbc_depth[zbc].format << ((znum % 4) * 7));
}

const struct gf100_gr_func_zbc
gp100_gr_zbc = {
	.clear_color = gp100_gr_zbc_clear_color,
	.clear_depth = gp100_gr_zbc_clear_depth,
};

void
gp100_gr_init_shader_exceptions(struct gf100_gr *gr, int gpc, int tpc)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x644), 0x00dffffe);
	nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x64c), 0x00000105);
}

static void
gp100_gr_init_419c9c(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_mask(device, 0x419c9c, 0x00010000, 0x00010000);
	nvkm_mask(device, 0x419c9c, 0x00020000, 0x00020000);
}

void
gp100_gr_init_fecs_exceptions(struct gf100_gr *gr)
{
	nvkm_wr32(gr->base.engine.subdev.device, 0x409c24, 0x000f0002);
}

void
gp100_gr_init_rop_active_fbps(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	/*XXX: otherwise identical to gm200 aside from mask.. do everywhere? */
	const u32 fbp_count = nvkm_rd32(device, 0x12006c) & 0x0000000f;
	nvkm_mask(device, 0x408850, 0x0000000f, fbp_count); /* zrop */
	nvkm_mask(device, 0x408958, 0x0000000f, fbp_count); /* crop */
}

static const struct gf100_gr_func
gp100_gr = {
	.oneinit_tiles = gm200_gr_oneinit_tiles,
	.oneinit_sm_id = gm200_gr_oneinit_sm_id,
	.init = gf100_gr_init,
	.init_gpc_mmu = gm200_gr_init_gpc_mmu,
	.init_vsc_stream_master = gk104_gr_init_vsc_stream_master,
	.init_zcull = gf117_gr_init_zcull,
	.init_num_active_ltcs = gm200_gr_init_num_active_ltcs,
	.init_rop_active_fbps = gp100_gr_init_rop_active_fbps,
	.init_fecs_exceptions = gp100_gr_init_fecs_exceptions,
	.init_ds_hww_esr_2 = gm200_gr_init_ds_hww_esr_2,
	.init_sked_hww_esr = gk104_gr_init_sked_hww_esr,
	.init_419cc0 = gf100_gr_init_419cc0,
	.init_419c9c = gp100_gr_init_419c9c,
	.init_ppc_exceptions = gk104_gr_init_ppc_exceptions,
	.init_tex_hww_esr = gf100_gr_init_tex_hww_esr,
	.init_504430 = gm107_gr_init_504430,
	.init_shader_exceptions = gp100_gr_init_shader_exceptions,
	.trap_mp = gf100_gr_trap_mp,
	.rops = gm200_gr_rops,
	.gpc_nr = 6,
	.tpc_nr = 5,
	.ppc_nr = 2,
	.grctx = &gp100_grctx,
	.zbc = &gp100_gr_zbc,
	.sclass = {
		{ -1, -1, FERMI_TWOD_A },
		{ -1, -1, KEPLER_INLINE_TO_MEMORY_B },
		{ -1, -1, PASCAL_A, &gf100_fermi },
		{ -1, -1, PASCAL_COMPUTE_A },
		{}
	}
};

MODULE_FIRMWARE("nvidia/gp100/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/sw_method_init.bin");

static const struct gf100_gr_fwif
gp100_gr_fwif[] = {
	{  0, gm200_gr_load, &gp100_gr, &gm200_gr_fecs_acr, &gm200_gr_gpccs_acr },
	{ -1, gm200_gr_nofw },
	{}
};

int
gp100_gr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_gr **pgr)
{
	return gf100_gr_new_(gp100_gr_fwif, device, type, inst, pgr);
}
