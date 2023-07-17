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

#include <core/firmware.h>
#include <subdev/acr.h>
#include <subdev/timer.h>
#include <subdev/vfn.h>

#include <nvfw/flcn.h>

#include <nvif/class.h>

static void
ga102_gr_zbc_clear_color(struct gf100_gr *gr, int zbc)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	u32 invalid[] = { 0, 0, 0, 0 }, *color;

	if (gr->zbc_color[zbc].format)
		color = gr->zbc_color[zbc].l2;
	else
		color = invalid;

	nvkm_mask(device, 0x41bcb4, 0x0000001f, zbc);
	nvkm_wr32(device, 0x41bcec, color[0]);
	nvkm_wr32(device, 0x41bcf0, color[1]);
	nvkm_wr32(device, 0x41bcf4, color[2]);
	nvkm_wr32(device, 0x41bcf8, color[3]);
}

static const struct gf100_gr_func_zbc
ga102_gr_zbc = {
	.clear_color = ga102_gr_zbc_clear_color,
	.clear_depth = gp100_gr_zbc_clear_depth,
	.stencil_get = gp102_gr_zbc_stencil_get,
	.clear_stencil = gp102_gr_zbc_clear_stencil,
};

static void
ga102_gr_gpccs_reset(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;

	nvkm_wr32(device, 0x41a610, 0x00000000);
	nvkm_msec(device, 1, NVKM_DELAY);
	nvkm_wr32(device, 0x41a610, 0x00000001);
}

static const struct nvkm_acr_lsf_func
ga102_gr_gpccs_acr = {
	.flags = NVKM_ACR_LSF_FORCE_PRIV_LOAD,
	.bl_entry = 0x3400,
	.bld_size = sizeof(struct flcn_bl_dmem_desc_v2),
	.bld_write = gp108_gr_acr_bld_write,
	.bld_patch = gp108_gr_acr_bld_patch,
};

static void
ga102_gr_fecs_reset(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;

	nvkm_wr32(device, 0x409614, 0x00000010);
	nvkm_wr32(device, 0x41a614, 0x00000020);
	nvkm_usec(device, 10, NVKM_DELAY);
	nvkm_wr32(device, 0x409614, 0x00000110);
	nvkm_wr32(device, 0x41a614, 0x00000a20);
	nvkm_usec(device, 10, NVKM_DELAY);
	nvkm_rd32(device, 0x409614);
	nvkm_rd32(device, 0x41a614);
}

static const struct nvkm_acr_lsf_func
ga102_gr_fecs_acr = {
	.bl_entry = 0x7e00,
	.bld_size = sizeof(struct flcn_bl_dmem_desc_v2),
	.bld_write = gp108_gr_acr_bld_write,
	.bld_patch = gp108_gr_acr_bld_patch,
};

static void
ga102_gr_init_rop_exceptions(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;

	nvkm_wr32(device, 0x41bcbc, 0x40000000);
	nvkm_wr32(device, 0x41bc38, 0x40000000);
	nvkm_wr32(device, 0x41ac94, nvkm_rd32(device, 0x502c94));
}

static void
ga102_gr_init_40a790(struct gf100_gr *gr)
{
	nvkm_wr32(gr->base.engine.subdev.device, 0x40a790, 0xc0000000);
}

static void
ga102_gr_init_gpc_mmu(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;

	nvkm_wr32(device, 0x418880, nvkm_rd32(device, 0x100c80) & 0xf8001fff);
	nvkm_wr32(device, 0x418894, 0x00000000);

	nvkm_wr32(device, 0x4188b4, nvkm_rd32(device, 0x100cc8));
	nvkm_wr32(device, 0x4188b8, nvkm_rd32(device, 0x100ccc));
	nvkm_wr32(device, 0x4188b0, nvkm_rd32(device, 0x100cc4));
}

static struct nvkm_intr *
ga102_gr_oneinit_intr(struct gf100_gr *gr, enum nvkm_intr_type *pvector)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;

	*pvector = nvkm_rd32(device, 0x400154) & 0x00000fff;
	return &device->vfn->intr;
}

static int
ga102_gr_nonstall(struct gf100_gr *gr)
{
	return nvkm_rd32(gr->base.engine.subdev.device, 0x400160) & 0x00000fff;
}

static const struct gf100_gr_func
ga102_gr = {
	.nonstall = ga102_gr_nonstall,
	.oneinit_intr = ga102_gr_oneinit_intr,
	.oneinit_tiles = gm200_gr_oneinit_tiles,
	.oneinit_sm_id = gv100_gr_oneinit_sm_id,
	.init = gf100_gr_init,
	.init_419bd8 = gv100_gr_init_419bd8,
	.init_gpc_mmu = ga102_gr_init_gpc_mmu,
	.init_vsc_stream_master = gk104_gr_init_vsc_stream_master,
	.init_zcull = tu102_gr_init_zcull,
	.init_num_active_ltcs = gf100_gr_init_num_active_ltcs,
	.init_swdx_pes_mask = gp102_gr_init_swdx_pes_mask,
	.init_fs = tu102_gr_init_fs,
	.init_fecs_exceptions = tu102_gr_init_fecs_exceptions,
	.init_40a790 = ga102_gr_init_40a790,
	.init_ds_hww_esr_2 = gm200_gr_init_ds_hww_esr_2,
	.init_sked_hww_esr = gk104_gr_init_sked_hww_esr,
	.init_ppc_exceptions = gk104_gr_init_ppc_exceptions,
	.init_504430 = gv100_gr_init_504430,
	.init_shader_exceptions = gv100_gr_init_shader_exceptions,
	.init_rop_exceptions = ga102_gr_init_rop_exceptions,
	.init_4188a4 = gv100_gr_init_4188a4,
	.trap_mp = gv100_gr_trap_mp,
	.fecs.reset = ga102_gr_fecs_reset,
	.gpccs.reset = ga102_gr_gpccs_reset,
	.rops = gm200_gr_rops,
	.gpc_nr = 7,
	.tpc_nr = 6,
	.ppc_nr = 3,
	.grctx = &ga102_grctx,
	.zbc = &ga102_gr_zbc,
	.sclass = {
		{ -1, -1, FERMI_TWOD_A },
		{ -1, -1, KEPLER_INLINE_TO_MEMORY_B },
		{ -1, -1, AMPERE_B, &gf100_fermi },
		{ -1, -1, AMPERE_COMPUTE_B },
		{}
	}
};

MODULE_FIRMWARE("nvidia/ga102/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/ga102/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/ga102/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/ga102/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/ga102/gr/NET_img.bin");

MODULE_FIRMWARE("nvidia/ga103/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/ga103/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/ga103/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/ga103/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/ga103/gr/NET_img.bin");

MODULE_FIRMWARE("nvidia/ga104/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/ga104/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/ga104/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/ga104/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/ga104/gr/NET_img.bin");

MODULE_FIRMWARE("nvidia/ga106/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/ga106/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/ga106/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/ga106/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/ga106/gr/NET_img.bin");

MODULE_FIRMWARE("nvidia/ga107/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/ga107/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/ga107/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/ga107/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/ga107/gr/NET_img.bin");

struct netlist_region {
	u32 region_id;
	u32 data_size;
	u32 data_offset;
};

struct netlist_image_header {
	u32 version;
	u32 regions;
};

struct netlist_image {
	struct netlist_image_header header;
	struct netlist_region regions[];
};

struct netlist_av64 {
	u32 addr;
	u32 data_hi;
	u32 data_lo;
};

static int
ga102_gr_av64_to_init(struct nvkm_blob *blob, struct gf100_gr_pack **ppack)
{
	struct gf100_gr_init *init;
	struct gf100_gr_pack *pack;
	int nent;
	int i;

	nent = (blob->size / sizeof(struct netlist_av64));
	pack = vzalloc((sizeof(*pack) * 2) + (sizeof(*init) * (nent + 1)));
	if (!pack)
		return -ENOMEM;

	init = (void *)(pack + 2);
	pack[0].init = init;
	pack[0].type = 64;

	for (i = 0; i < nent; i++) {
		struct gf100_gr_init *ent = &init[i];
		struct netlist_av64 *av = &((struct netlist_av64 *)blob->data)[i];

		ent->addr = av->addr;
		ent->data = ((u64)av->data_hi << 32) | av->data_lo;
		ent->count = 1;
		ent->pitch = 1;
	}

	*ppack = pack;
	return 0;
}

static int
ga102_gr_load(struct gf100_gr *gr, int ver, const struct gf100_gr_fwif *fwif)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	const struct firmware *fw;
	const struct netlist_image *net;
	const struct netlist_region *fecs_inst = NULL;
	const struct netlist_region *fecs_data = NULL;
	const struct netlist_region *gpccs_inst = NULL;
	const struct netlist_region *gpccs_data = NULL;
	int ret, i;

	ret = nvkm_firmware_get(subdev, "gr/NET_img", 0, &fw);
	if (ret)
		return ret;

	net = (const void *)fw->data;
	nvkm_debug(subdev, "netlist version %d, %d regions\n",
		   net->header.version, net->header.regions);

	for (i = 0; i < net->header.regions; i++) {
		const struct netlist_region *reg = &net->regions[i];
		struct nvkm_blob blob = {
			.data = (void *)fw->data + reg->data_offset,
			.size = reg->data_size,
		};

		nvkm_debug(subdev, "\t%2d: %08x %08x\n",
			   reg->region_id, reg->data_offset, reg->data_size);

		switch (reg->region_id) {
		case  0: fecs_data = reg; break;
		case  1: fecs_inst = reg; break;
		case  2: gpccs_data = reg; break;
		case  3: gpccs_inst = reg; break;
		case  4: gk20a_gr_av_to_init(&blob, &gr->bundle); break;
		case  5: gk20a_gr_aiv_to_init(&blob, &gr->sw_ctx); break;
		case  7: gk20a_gr_av_to_method(&blob, &gr->method); break;
		case 28: tu102_gr_av_to_init_veid(&blob, &gr->bundle_veid); break;
		case 34: ga102_gr_av64_to_init(&blob, &gr->bundle64); break;
		case 48: gk20a_gr_av_to_init(&blob, &gr->sw_nonctx1); break;
		case 49: gk20a_gr_av_to_init(&blob, &gr->sw_nonctx2); break;
		case 50: gk20a_gr_av_to_init(&blob, &gr->sw_nonctx3); break;
		case 51: gk20a_gr_av_to_init(&blob, &gr->sw_nonctx4); break;
		default:
			break;
		}
	}

	ret = nvkm_acr_lsfw_load_bl_sig_net(subdev, &gr->fecs.falcon, NVKM_ACR_LSF_FECS,
					    "gr/fecs_", ver, fwif->fecs,
					    fw->data + fecs_inst->data_offset,
						       fecs_inst->data_size,
					    fw->data + fecs_data->data_offset,
						       fecs_data->data_size);
	if (ret)
		return ret;

	ret = nvkm_acr_lsfw_load_bl_sig_net(subdev, &gr->gpccs.falcon, NVKM_ACR_LSF_GPCCS,
					    "gr/gpccs_", ver, fwif->gpccs,
					    fw->data + gpccs_inst->data_offset,
						       gpccs_inst->data_size,
					    fw->data + gpccs_data->data_offset,
						       gpccs_data->data_size);
	if (ret)
		return ret;

	gr->firmware = true;

	nvkm_firmware_put(fw);
	return 0;
}

static const struct gf100_gr_fwif
ga102_gr_fwif[] = {
	{  0, ga102_gr_load, &ga102_gr, &ga102_gr_fecs_acr, &ga102_gr_gpccs_acr },
	{ -1, gm200_gr_nofw },
	{}
};

int
ga102_gr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_gr **pgr)
{
	return gf100_gr_new_(ga102_gr_fwif, device, type, inst, pgr);
}
