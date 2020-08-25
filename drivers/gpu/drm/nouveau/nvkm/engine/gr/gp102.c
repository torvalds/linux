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

static void
gp102_gr_zbc_clear_stencil(struct gf100_gr *gr, int zbc)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const int znum =  zbc - 1;
	const u32 zoff = znum * 4;

	if (gr->zbc_stencil[zbc].format)
		nvkm_wr32(device, 0x41815c + zoff, gr->zbc_stencil[zbc].ds);
	nvkm_mask(device, 0x418198 + ((znum / 4) * 4),
			  0x0000007f << ((znum % 4) * 7),
			  gr->zbc_stencil[zbc].format << ((znum % 4) * 7));
}

static int
gp102_gr_zbc_stencil_get(struct gf100_gr *gr, int format,
			 const u32 ds, const u32 l2)
{
	struct nvkm_ltc *ltc = gr->base.engine.subdev.device->ltc;
	int zbc = -ENOSPC, i;

	for (i = ltc->zbc_min; i <= ltc->zbc_max; i++) {
		if (gr->zbc_stencil[i].format) {
			if (gr->zbc_stencil[i].format != format)
				continue;
			if (gr->zbc_stencil[i].ds != ds)
				continue;
			if (gr->zbc_stencil[i].l2 != l2) {
				WARN_ON(1);
				return -EINVAL;
			}
			return i;
		} else {
			zbc = (zbc < 0) ? i : zbc;
		}
	}

	if (zbc < 0)
		return zbc;

	gr->zbc_stencil[zbc].format = format;
	gr->zbc_stencil[zbc].ds = ds;
	gr->zbc_stencil[zbc].l2 = l2;
	nvkm_ltc_zbc_stencil_get(ltc, zbc, l2);
	gr->func->zbc->clear_stencil(gr, zbc);
	return zbc;
}

const struct gf100_gr_func_zbc
gp102_gr_zbc = {
	.clear_color = gp100_gr_zbc_clear_color,
	.clear_depth = gp100_gr_zbc_clear_depth,
	.stencil_get = gp102_gr_zbc_stencil_get,
	.clear_stencil = gp102_gr_zbc_clear_stencil,
};

void
gp102_gr_init_swdx_pes_mask(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	u32 mask = 0, data, gpc;

	for (gpc = 0; gpc < gr->gpc_nr; gpc++) {
		data = nvkm_rd32(device, GPC_UNIT(gpc, 0x0c50)) & 0x0000000f;
		mask |= data << (gpc * 4);
	}

	nvkm_wr32(device, 0x4181d0, mask);
}

static const struct gf100_gr_func
gp102_gr = {
	.oneinit_tiles = gm200_gr_oneinit_tiles,
	.oneinit_sm_id = gm200_gr_oneinit_sm_id,
	.init = gf100_gr_init,
	.init_gpc_mmu = gm200_gr_init_gpc_mmu,
	.init_vsc_stream_master = gk104_gr_init_vsc_stream_master,
	.init_zcull = gf117_gr_init_zcull,
	.init_num_active_ltcs = gm200_gr_init_num_active_ltcs,
	.init_rop_active_fbps = gp100_gr_init_rop_active_fbps,
	.init_swdx_pes_mask = gp102_gr_init_swdx_pes_mask,
	.init_fecs_exceptions = gp100_gr_init_fecs_exceptions,
	.init_ds_hww_esr_2 = gm200_gr_init_ds_hww_esr_2,
	.init_sked_hww_esr = gk104_gr_init_sked_hww_esr,
	.init_419cc0 = gf100_gr_init_419cc0,
	.init_ppc_exceptions = gk104_gr_init_ppc_exceptions,
	.init_tex_hww_esr = gf100_gr_init_tex_hww_esr,
	.init_504430 = gm107_gr_init_504430,
	.init_shader_exceptions = gp100_gr_init_shader_exceptions,
	.trap_mp = gf100_gr_trap_mp,
	.rops = gm200_gr_rops,
	.gpc_nr = 6,
	.tpc_nr = 5,
	.ppc_nr = 3,
	.grctx = &gp102_grctx,
	.zbc = &gp102_gr_zbc,
	.sclass = {
		{ -1, -1, FERMI_TWOD_A },
		{ -1, -1, KEPLER_INLINE_TO_MEMORY_B },
		{ -1, -1, PASCAL_B, &gf100_fermi },
		{ -1, -1, PASCAL_COMPUTE_B },
		{}
	}
};

MODULE_FIRMWARE("nvidia/gp102/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/sw_method_init.bin");

static const struct gf100_gr_fwif
gp102_gr_fwif[] = {
	{  0, gm200_gr_load, &gp102_gr, &gm200_gr_fecs_acr, &gm200_gr_gpccs_acr },
	{ -1, gm200_gr_nofw },
	{}
};

int
gp102_gr_new(struct nvkm_device *device, int index, struct nvkm_gr **pgr)
{
	return gf100_gr_new_(gp102_gr_fwif, device, index, pgr);
}
