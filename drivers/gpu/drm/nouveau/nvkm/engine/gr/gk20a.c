/*
 * Copyright (c) 2014-2015, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "gf100.h"
#include "ctxgf100.h"

#include <subdev/timer.h>

#include <nvif/class.h>

struct gk20a_fw_av
{
	u32 addr;
	u32 data;
};

int
gk20a_gr_av_to_init(struct gf100_gr *gr, const char *fw_name,
		    struct gf100_gr_pack **ppack)
{
	struct gf100_gr_fuc fuc;
	struct gf100_gr_init *init;
	struct gf100_gr_pack *pack;
	int nent;
	int ret;
	int i;

	ret = gf100_gr_ctor_fw(gr, fw_name, &fuc);
	if (ret)
		return ret;

	nent = (fuc.size / sizeof(struct gk20a_fw_av));
	pack = vzalloc((sizeof(*pack) * 2) + (sizeof(*init) * (nent + 1)));
	if (!pack) {
		ret = -ENOMEM;
		goto end;
	}

	init = (void *)(pack + 2);
	pack[0].init = init;

	for (i = 0; i < nent; i++) {
		struct gf100_gr_init *ent = &init[i];
		struct gk20a_fw_av *av = &((struct gk20a_fw_av *)fuc.data)[i];

		ent->addr = av->addr;
		ent->data = av->data;
		ent->count = 1;
		ent->pitch = 1;
	}

	*ppack = pack;

end:
	gf100_gr_dtor_fw(&fuc);
	return ret;
}

struct gk20a_fw_aiv
{
	u32 addr;
	u32 index;
	u32 data;
};

int
gk20a_gr_aiv_to_init(struct gf100_gr *gr, const char *fw_name,
		     struct gf100_gr_pack **ppack)
{
	struct gf100_gr_fuc fuc;
	struct gf100_gr_init *init;
	struct gf100_gr_pack *pack;
	int nent;
	int ret;
	int i;

	ret = gf100_gr_ctor_fw(gr, fw_name, &fuc);
	if (ret)
		return ret;

	nent = (fuc.size / sizeof(struct gk20a_fw_aiv));
	pack = vzalloc((sizeof(*pack) * 2) + (sizeof(*init) * (nent + 1)));
	if (!pack) {
		ret = -ENOMEM;
		goto end;
	}

	init = (void *)(pack + 2);
	pack[0].init = init;

	for (i = 0; i < nent; i++) {
		struct gf100_gr_init *ent = &init[i];
		struct gk20a_fw_aiv *av = &((struct gk20a_fw_aiv *)fuc.data)[i];

		ent->addr = av->addr;
		ent->data = av->data;
		ent->count = 1;
		ent->pitch = 1;
	}

	*ppack = pack;

end:
	gf100_gr_dtor_fw(&fuc);
	return ret;
}

int
gk20a_gr_av_to_method(struct gf100_gr *gr, const char *fw_name,
		      struct gf100_gr_pack **ppack)
{
	struct gf100_gr_fuc fuc;
	struct gf100_gr_init *init;
	struct gf100_gr_pack *pack;
	/* We don't suppose we will initialize more than 16 classes here... */
	static const unsigned int max_classes = 16;
	u32 classidx = 0, prevclass = 0;
	int nent;
	int ret;
	int i;

	ret = gf100_gr_ctor_fw(gr, fw_name, &fuc);
	if (ret)
		return ret;

	nent = (fuc.size / sizeof(struct gk20a_fw_av));

	pack = vzalloc((sizeof(*pack) * (max_classes + 1)) +
		       (sizeof(*init) * (nent + max_classes + 1)));
	if (!pack) {
		ret = -ENOMEM;
		goto end;
	}

	init = (void *)(pack + max_classes + 1);

	for (i = 0; i < nent; i++, init++) {
		struct gk20a_fw_av *av = &((struct gk20a_fw_av *)fuc.data)[i];
		u32 class = av->addr & 0xffff;
		u32 addr = (av->addr & 0xffff0000) >> 14;

		if (prevclass != class) {
			if (prevclass) /* Add terminator to the method list. */
				init++;
			pack[classidx].init = init;
			pack[classidx].type = class;
			prevclass = class;
			if (++classidx >= max_classes) {
				vfree(pack);
				ret = -ENOSPC;
				goto end;
			}
		}

		init->addr = addr;
		init->data = av->data;
		init->count = 1;
		init->pitch = 1;
	}

	*ppack = pack;

end:
	gf100_gr_dtor_fw(&fuc);
	return ret;
}

static int
gk20a_gr_wait_mem_scrubbing(struct gf100_gr *gr)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;

	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x40910c) & 0x00000006))
			break;
	) < 0) {
		nvkm_error(subdev, "FECS mem scrubbing timeout\n");
		return -ETIMEDOUT;
	}

	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x41a10c) & 0x00000006))
			break;
	) < 0) {
		nvkm_error(subdev, "GPCCS mem scrubbing timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void
gk20a_gr_set_hww_esr_report_mask(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	nvkm_wr32(device, 0x419e44, 0x1ffffe);
	nvkm_wr32(device, 0x419e4c, 0x7f);
}

int
gk20a_gr_init(struct gf100_gr *gr)
{
	struct nvkm_device *device = gr->base.engine.subdev.device;
	int ret;

	/* Clear SCC RAM */
	nvkm_wr32(device, 0x40802c, 0x1);

	gf100_gr_mmio(gr, gr->fuc_sw_nonctx);

	ret = gk20a_gr_wait_mem_scrubbing(gr);
	if (ret)
		return ret;

	ret = gf100_gr_wait_idle(gr);
	if (ret)
		return ret;

	/* MMU debug buffer */
	if (gr->func->init_gpc_mmu)
		gr->func->init_gpc_mmu(gr);

	/* Set the PE as stream master */
	nvkm_mask(device, 0x503018, 0x1, 0x1);

	/* Zcull init */
	gr->func->init_zcull(gr);

	gr->func->init_rop_active_fbps(gr);

	/* Enable FIFO access */
	nvkm_wr32(device, 0x400500, 0x00010001);

	/* Enable interrupts */
	nvkm_wr32(device, 0x400100, 0xffffffff);
	nvkm_wr32(device, 0x40013c, 0xffffffff);

	/* Enable FECS error interrupts */
	nvkm_wr32(device, 0x409c24, 0x000f0000);

	/* Enable hardware warning exceptions */
	nvkm_wr32(device, 0x404000, 0xc0000000);
	nvkm_wr32(device, 0x404600, 0xc0000000);

	if (gr->func->set_hww_esr_report_mask)
		gr->func->set_hww_esr_report_mask(gr);

	/* Enable TPC exceptions per GPC */
	nvkm_wr32(device, 0x419d0c, 0x2);
	nvkm_wr32(device, 0x41ac94, (((1 << gr->tpc_total) - 1) & 0xff) << 16);

	/* Reset and enable all exceptions */
	nvkm_wr32(device, 0x400108, 0xffffffff);
	nvkm_wr32(device, 0x400138, 0xffffffff);
	nvkm_wr32(device, 0x400118, 0xffffffff);
	nvkm_wr32(device, 0x400130, 0xffffffff);
	nvkm_wr32(device, 0x40011c, 0xffffffff);
	nvkm_wr32(device, 0x400134, 0xffffffff);

	gf100_gr_zbc_init(gr);

	return gf100_gr_init_ctxctl(gr);
}

static const struct gf100_gr_func
gk20a_gr = {
	.oneinit_tiles = gf100_gr_oneinit_tiles,
	.oneinit_sm_id = gf100_gr_oneinit_sm_id,
	.init = gk20a_gr_init,
	.init_zcull = gf117_gr_init_zcull,
	.init_rop_active_fbps = gk104_gr_init_rop_active_fbps,
	.trap_mp = gf100_gr_trap_mp,
	.set_hww_esr_report_mask = gk20a_gr_set_hww_esr_report_mask,
	.rops = gf100_gr_rops,
	.ppc_nr = 1,
	.grctx = &gk20a_grctx,
	.zbc = &gf100_gr_zbc,
	.sclass = {
		{ -1, -1, FERMI_TWOD_A },
		{ -1, -1, KEPLER_INLINE_TO_MEMORY_A },
		{ -1, -1, KEPLER_C, &gf100_fermi },
		{ -1, -1, KEPLER_COMPUTE_A },
		{}
	}
};

int
gk20a_gr_new(struct nvkm_device *device, int index, struct nvkm_gr **pgr)
{
	struct gf100_gr *gr;
	int ret;

	if (!(gr = kzalloc(sizeof(*gr), GFP_KERNEL)))
		return -ENOMEM;
	*pgr = &gr->base;

	ret = gf100_gr_ctor(&gk20a_gr, device, index, gr);
	if (ret)
		return ret;

	if (gf100_gr_ctor_fw(gr, "fecs_inst", &gr->fuc409c) ||
	    gf100_gr_ctor_fw(gr, "fecs_data", &gr->fuc409d) ||
	    gf100_gr_ctor_fw(gr, "gpccs_inst", &gr->fuc41ac) ||
	    gf100_gr_ctor_fw(gr, "gpccs_data", &gr->fuc41ad))
		return -ENODEV;

	ret = gk20a_gr_av_to_init(gr, "sw_nonctx", &gr->fuc_sw_nonctx);
	if (ret)
		return ret;

	ret = gk20a_gr_aiv_to_init(gr, "sw_ctx", &gr->fuc_sw_ctx);
	if (ret)
		return ret;

	ret = gk20a_gr_av_to_init(gr, "sw_bundle_init", &gr->fuc_bundle);
	if (ret)
		return ret;

	ret = gk20a_gr_av_to_method(gr, "sw_method_init", &gr->fuc_method);
	if (ret)
		return ret;

	return 0;
}
