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
#include "chan.h"
#include "priv.h"
#include "head.h"
#include "ior.h"

#include <core/gpuobj.h>
#include <subdev/gsp.h>
#include <subdev/timer.h>

#include <nvif/class.h>

void
tu102_sor_dp_vcpi(struct nvkm_ior *sor, int head, u8 slot, u8 slot_nr, u16 pbn, u16 aligned)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 hoff = head * 0x800;

	nvkm_mask(device, 0x61657c + hoff, 0xffffffff, (aligned << 16) | pbn);
	nvkm_mask(device, 0x616578 + hoff, 0x00003f3f, (slot_nr << 8) | slot);
}

static int
tu102_sor_dp_links(struct nvkm_ior *sor, struct nvkm_i2c_aux *aux)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	const u32 loff = nv50_sor_link(sor);
	u32 dpctrl = 0x00000000;
	u32 clksor = 0x00000000;

	clksor |= sor->dp.bw << 18;
	dpctrl |= ((1 << sor->dp.nr) - 1) << 16;
	if (sor->dp.mst)
		dpctrl |= 0x40000000;
	if (sor->dp.ef)
		dpctrl |= 0x00004000;

	nvkm_mask(device, 0x612300 + soff, 0x007c0000, clksor);

	/*XXX*/
	nvkm_msec(device, 40, NVKM_DELAY);
	nvkm_mask(device, 0x612300 + soff, 0x00030000, 0x00010000);
	nvkm_mask(device, 0x61c10c + loff, 0x00000003, 0x00000001);

	nvkm_mask(device, 0x61c10c + loff, 0x401f4000, dpctrl);
	return 0;
}

static const struct nvkm_ior_func_dp
tu102_sor_dp = {
	.lanes = { 0, 1, 2, 3 },
	.links = tu102_sor_dp_links,
	.power = g94_sor_dp_power,
	.pattern = gm107_sor_dp_pattern,
	.drive = gm200_sor_dp_drive,
	.vcpi = tu102_sor_dp_vcpi,
	.audio = gv100_sor_dp_audio,
	.audio_sym = gv100_sor_dp_audio_sym,
	.watermark = gv100_sor_dp_watermark,
};

static const struct nvkm_ior_func
tu102_sor = {
	.route = {
		.get = gm200_sor_route_get,
		.set = gm200_sor_route_set,
	},
	.state = gv100_sor_state,
	.power = nv50_sor_power,
	.clock = gf119_sor_clock,
	.bl = &gt215_sor_bl,
	.hdmi = &gv100_sor_hdmi,
	.dp = &tu102_sor_dp,
	.hda = &gv100_sor_hda,
};

static int
tu102_sor_new(struct nvkm_disp *disp, int id)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	u32 hda = nvkm_rd32(device, 0x08a15c);

	return nvkm_ior_new_(&tu102_sor, disp, SOR, id, hda & BIT(id));
}

int
tu102_disp_init(struct nvkm_disp *disp)
{
	struct nvkm_device *device = disp->engine.subdev.device;
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
	list_for_each_entry(head, &disp->heads, head) {
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
	list_for_each_entry(head, &disp->heads, head) {
		const u32 hoff = head->id * 4;
		nvkm_wr32(device, 0x611cc0 + hoff, 0x00000004); /* MSK. */
		nvkm_wr32(device, 0x611d80 + hoff, 0x00000000); /* EN. */
	}

	/* OR. */
	nvkm_wr32(device, 0x611cf4, 0x00000000); /* MSK. */
	nvkm_wr32(device, 0x611db4, 0x00000000); /* EN. */
	return 0;
}

static const struct nvkm_disp_func
tu102_disp = {
	.oneinit = nv50_disp_oneinit,
	.init = tu102_disp_init,
	.fini = gv100_disp_fini,
	.intr = gv100_disp_intr,
	.super = gv100_disp_super,
	.uevent = &gv100_disp_chan_uevent,
	.wndw = { .cnt = gv100_disp_wndw_cnt },
	.head = { .cnt = gv100_head_cnt, .new = gv100_head_new },
	.sor = { .cnt = gv100_sor_cnt, .new = tu102_sor_new },
	.ramht_size = 0x2000,
	.root = {  0, 0,TU102_DISP },
	.user = {
		{{-1,-1,GV100_DISP_CAPS                  }, gv100_disp_caps_new },
		{{ 0, 0,TU102_DISP_CURSOR                },  nvkm_disp_chan_new, &gv100_disp_curs },
		{{ 0, 0,TU102_DISP_WINDOW_IMM_CHANNEL_DMA},  nvkm_disp_wndw_new, &gv100_disp_wimm },
		{{ 0, 0,TU102_DISP_CORE_CHANNEL_DMA      },  nvkm_disp_core_new, &gv100_disp_core },
		{{ 0, 0,TU102_DISP_WINDOW_CHANNEL_DMA    },  nvkm_disp_wndw_new, &gv100_disp_wndw },
		{}
	},
};

int
tu102_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	if (nvkm_gsp_rm(device->gsp))
		return r535_disp_new(&tu102_disp, device, type, inst, pdisp);

	return nvkm_disp_new_(&tu102_disp, device, type, inst, pdisp);
}
