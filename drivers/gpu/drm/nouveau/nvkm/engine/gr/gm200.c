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

static void
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

int
gm200_gr_init(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	const u32 magicgpc918 = DIV_ROUND_UP(0x00800000, gr->tpc_total);
	u32 data[TPC_MAX / 8] = {};
	u8  tpcnr[GPC_MAX];
	int gpc, tpc, ppc, rop;
	int i;

	gr->func->init_gpc_mmu(gr);

	gf100_gr_mmio(gr, gr->fuc_sw_nonctx);

	gm107_gr_init_bios(gr);

	nvkm_wr32(device, GPC_UNIT(0, 0x3018), 0x00000001);

	memset(data, 0x00, sizeof(data));
	memcpy(tpcnr, gr->tpc_nr, sizeof(gr->tpc_nr));
	for (i = 0, gpc = -1; i < gr->tpc_total; i++) {
		do {
			gpc = (gpc + 1) % gr->gpc_nr;
		} while (!tpcnr[gpc]);
		tpc = gr->tpc_nr[gpc] - tpcnr[gpc]--;

		data[i / 8] |= tpc << ((i % 8) * 4);
	}

	nvkm_wr32(device, GPC_BCAST(0x0980), data[0]);
	nvkm_wr32(device, GPC_BCAST(0x0984), data[1]);
	nvkm_wr32(device, GPC_BCAST(0x0988), data[2]);
	nvkm_wr32(device, GPC_BCAST(0x098c), data[3]);

	for (gpc = 0; gpc < gr->gpc_nr; gpc++) {
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0914),
			  gr->screen_tile_row_offset << 8 | gr->tpc_nr[gpc]);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0910), 0x00040000 |
							 gr->tpc_total);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0918), magicgpc918);
	}

	nvkm_wr32(device, GPC_BCAST(0x3fd4), magicgpc918);
	nvkm_wr32(device, GPC_BCAST(0x08ac), nvkm_rd32(device, 0x100800));
	nvkm_wr32(device, GPC_BCAST(0x033c), nvkm_rd32(device, 0x100804));

	gr->func->init_rop_active_fbps(gr);

	nvkm_wr32(device, 0x400500, 0x00010001);
	nvkm_wr32(device, 0x400100, 0xffffffff);
	nvkm_wr32(device, 0x40013c, 0xffffffff);
	nvkm_wr32(device, 0x400124, 0x00000002);
	nvkm_wr32(device, 0x409c24, 0x000e0000);
	nvkm_wr32(device, 0x405848, 0xc0000000);
	nvkm_wr32(device, 0x40584c, 0x00000001);
	nvkm_wr32(device, 0x404000, 0xc0000000);
	nvkm_wr32(device, 0x404600, 0xc0000000);
	nvkm_wr32(device, 0x408030, 0xc0000000);
	nvkm_wr32(device, 0x404490, 0xc0000000);
	nvkm_wr32(device, 0x406018, 0xc0000000);
	nvkm_wr32(device, 0x407020, 0x40000000);
	nvkm_wr32(device, 0x405840, 0xc0000000);
	nvkm_wr32(device, 0x405844, 0x00ffffff);
	nvkm_mask(device, 0x419cc0, 0x00000008, 0x00000008);

	for (gpc = 0; gpc < gr->gpc_nr; gpc++) {
		for (ppc = 0; ppc < gr->ppc_nr[gpc]; ppc++)
			nvkm_wr32(device, PPC_UNIT(gpc, ppc, 0x038), 0xc0000000);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0420), 0xc0000000);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0900), 0xc0000000);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x1028), 0xc0000000);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x0824), 0xc0000000);
		for (tpc = 0; tpc < gr->tpc_nr[gpc]; tpc++) {
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x508), 0xffffffff);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x50c), 0xffffffff);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x224), 0xc0000000);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x48c), 0xc0000000);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x084), 0xc0000000);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x430), 0xc0000000);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x644), 0x00dffffe);
			nvkm_wr32(device, TPC_UNIT(gpc, tpc, 0x64c), 0x00000005);
		}
		nvkm_wr32(device, GPC_UNIT(gpc, 0x2c90), 0xffffffff);
		nvkm_wr32(device, GPC_UNIT(gpc, 0x2c94), 0xffffffff);
	}

	for (rop = 0; rop < gr->rop_nr; rop++) {
		nvkm_wr32(device, ROP_UNIT(rop, 0x144), 0x40000000);
		nvkm_wr32(device, ROP_UNIT(rop, 0x070), 0x40000000);
		nvkm_wr32(device, ROP_UNIT(rop, 0x204), 0xffffffff);
		nvkm_wr32(device, ROP_UNIT(rop, 0x208), 0xffffffff);
	}

	nvkm_wr32(device, 0x400108, 0xffffffff);
	nvkm_wr32(device, 0x400138, 0xffffffff);
	nvkm_wr32(device, 0x400118, 0xffffffff);
	nvkm_wr32(device, 0x400130, 0xffffffff);
	nvkm_wr32(device, 0x40011c, 0xffffffff);
	nvkm_wr32(device, 0x400134, 0xffffffff);

	nvkm_wr32(device, 0x400054, 0x2c350f63);

	gf100_gr_zbc_init(gr);

	return gf100_gr_init_ctxctl(gr);
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
	.init = gm200_gr_init,
	.init_gpc_mmu = gm200_gr_init_gpc_mmu,
	.init_rop_active_fbps = gm200_gr_init_rop_active_fbps,
	.rops = gm200_gr_rops,
	.ppc_nr = 2,
	.grctx = &gm200_grctx,
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
