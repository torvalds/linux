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
#include "nv50.h"
#include "head.h"
#include "ior.h"
#include "channv50.h"
#include "rootnv50.h"

#include <core/gpuobj.h>
#include <subdev/timer.h>

int
tu102_disp_init(struct nv50_disp *disp)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	struct nvkm_head *head;
	int i, j;
	u32 tmp;

	/* Claim ownership of display. */
	if (nvkm_rd32(device, 0x6254e8) & 0x00000002) {
		nvkm_mask(device, 0x6254e8, 0x00000001, 0x00000000);
		if (nvkm_msec(device, 2000,
			if (!(nvkm_rd32(device, 0x6254e8) & 0x00000002))
				break;
		) < 0)
			return -EBUSY;
	}

	/* Lock pin capabilities. */
	tmp = 0x00000021; /*XXX*/
	nvkm_wr32(device, 0x640008, tmp);

	/* SOR capabilities. */
	for (i = 0; i < disp->sor.nr; i++) {
		tmp = nvkm_rd32(device, 0x61c000 + (i * 0x800));
		nvkm_mask(device, 0x640000, 0x00000100 << i, 0x00000100 << i);
		nvkm_wr32(device, 0x640144 + (i * 0x08), tmp);
	}

	/* Head capabilities. */
	list_for_each_entry(head, &disp->base.head, head) {
		const int id = head->id;

		/* RG. */
		tmp = nvkm_rd32(device, 0x616300 + (id * 0x800));
		nvkm_wr32(device, 0x640048 + (id * 0x020), tmp);

		/* POSTCOMP. */
		for (j = 0; j < 5 * 4; j += 4) {
			tmp = nvkm_rd32(device, 0x616140 + (id * 0x800) + j);
			nvkm_wr32(device, 0x640680 + (id * 0x20) + j, tmp);
		}
	}

	/* Window capabilities. */
	for (i = 0; i < disp->wndw.nr; i++) {
		nvkm_mask(device, 0x640004, 1 << i, 1 << i);
		for (j = 0; j < 6 * 4; j += 4) {
			tmp = nvkm_rd32(device, 0x630100 + (i * 0x800) + j);
			nvkm_mask(device, 0x640780 + (i * 0x20) + j, 0xffffffff, tmp);
		}
		nvkm_mask(device, 0x64000c, 0x00000100, 0x00000100);
	}

	/* IHUB capabilities. */
	for (i = 0; i < 3; i++) {
		tmp = nvkm_rd32(device, 0x62e000 + (i * 0x04));
		nvkm_wr32(device, 0x640010 + (i * 0x04), tmp);
	}

	nvkm_mask(device, 0x610078, 0x00000001, 0x00000001);

	/* Setup instance memory. */
	switch (nvkm_memory_target(disp->inst->memory)) {
	case NVKM_MEM_TARGET_VRAM: tmp = 0x00000001; break;
	case NVKM_MEM_TARGET_NCOH: tmp = 0x00000002; break;
	case NVKM_MEM_TARGET_HOST: tmp = 0x00000003; break;
	default:
		break;
	}
	nvkm_wr32(device, 0x610010, 0x00000008 | tmp);
	nvkm_wr32(device, 0x610014, disp->inst->addr >> 16);

	/* CTRL_DISP: AWAKEN, ERROR, SUPERVISOR[1-3]. */
	nvkm_wr32(device, 0x611cf0, 0x00000187); /* MSK. */
	nvkm_wr32(device, 0x611db0, 0x00000187); /* EN. */

	/* EXC_OTHER: CURSn, CORE. */
	nvkm_wr32(device, 0x611cec, disp->head.mask << 16 |
				    0x00000001); /* MSK. */
	nvkm_wr32(device, 0x611dac, 0x00000000); /* EN. */

	/* EXC_WINIM. */
	nvkm_wr32(device, 0x611ce8, disp->wndw.mask); /* MSK. */
	nvkm_wr32(device, 0x611da8, 0x00000000); /* EN. */

	/* EXC_WIN. */
	nvkm_wr32(device, 0x611ce4, disp->wndw.mask); /* MSK. */
	nvkm_wr32(device, 0x611da4, 0x00000000); /* EN. */

	/* HEAD_TIMING(n): VBLANK. */
	list_for_each_entry(head, &disp->base.head, head) {
		const u32 hoff = head->id * 4;
		nvkm_wr32(device, 0x611cc0 + hoff, 0x00000004); /* MSK. */
		nvkm_wr32(device, 0x611d80 + hoff, 0x00000000); /* EN. */
	}

	/* OR. */
	nvkm_wr32(device, 0x611cf4, 0x00000000); /* MSK. */
	nvkm_wr32(device, 0x611db4, 0x00000000); /* EN. */
	return 0;
}

static const struct nv50_disp_func
tu102_disp = {
	.init = tu102_disp_init,
	.fini = gv100_disp_fini,
	.intr = gv100_disp_intr,
	.uevent = &gv100_disp_chan_uevent,
	.super = gv100_disp_super,
	.root = &tu102_disp_root_oclass,
	.wndw = { .cnt = gv100_disp_wndw_cnt },
	.head = { .cnt = gv100_head_cnt, .new = gv100_head_new },
	.sor = { .cnt = gv100_sor_cnt, .new = tu102_sor_new },
	.ramht_size = 0x2000,
};

int
tu102_disp_new(struct nvkm_device *device, int index, struct nvkm_disp **pdisp)
{
	return nv50_disp_new_(&tu102_disp, device, index, pdisp);
}
