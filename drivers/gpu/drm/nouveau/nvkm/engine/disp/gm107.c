/*
 * Copyright 2012 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */
#include "priv.h"
#include "chan.h"
#include "head.h"
#include "ior.h"

#include <nvif/class.h>

void
gm107_sor_dp_pattern(struct nvkm_ior *sor, int pattern)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	u32 mask = 0x1f1f1f1f, data;

	switch (pattern) {
	case 0: data = 0x10101010; break;
	case 1: data = 0x01010101; break;
	case 2: data = 0x02020202; break;
	case 3: data = 0x03030303; break;
	case 4: data = 0x1b1b1b1b; break;
	default:
		WARN_ON(1);
		return;
	}

	if (sor->asy.link & 1)
		nvkm_mask(device, 0x61c110 + soff, mask, data);
	else
		nvkm_mask(device, 0x61c12c + soff, mask, data);
}

static const struct nvkm_ior_func_dp
gm107_sor_dp = {
	.lanes = { 0, 1, 2, 3 },
	.links = gf119_sor_dp_links,
	.power = g94_sor_dp_power,
	.pattern = gm107_sor_dp_pattern,
	.drive = gf119_sor_dp_drive,
	.vcpi = gf119_sor_dp_vcpi,
	.audio = gf119_sor_dp_audio,
	.audio_sym = gf119_sor_dp_audio_sym,
	.watermark = gf119_sor_dp_watermark,
};

static const struct nvkm_ior_func
gm107_sor = {
	.state = gf119_sor_state,
	.power = nv50_sor_power,
	.clock = gf119_sor_clock,
	.hdmi = {
		.ctrl = gk104_sor_hdmi_ctrl,
	},
	.dp = &gm107_sor_dp,
	.hda = &gf119_sor_hda,
};

static int
gm107_sor_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&gm107_sor, disp, SOR, id, true);
}

static const struct nvkm_disp_func
gm107_disp = {
	.oneinit = nv50_disp_oneinit,
	.init = gf119_disp_init,
	.fini = gf119_disp_fini,
	.intr = gf119_disp_intr,
	.intr_error = gf119_disp_intr_error,
	.super = gf119_disp_super,
	.uevent = &gf119_disp_chan_uevent,
	.head = { .cnt = gf119_head_cnt, .new = gf119_head_new },
	.dac = { .cnt = gf119_dac_cnt, .new = gf119_dac_new },
	.sor = { .cnt = gf119_sor_cnt, .new = gm107_sor_new },
	.root = { 0,0,GM107_DISP },
	.user = {
		{{0,0,GK104_DISP_CURSOR             }, nvkm_disp_chan_new, &gf119_disp_curs },
		{{0,0,GK104_DISP_OVERLAY            }, nvkm_disp_chan_new, &gf119_disp_oimm },
		{{0,0,GK110_DISP_BASE_CHANNEL_DMA   }, nvkm_disp_chan_new, &gf119_disp_base },
		{{0,0,GM107_DISP_CORE_CHANNEL_DMA   }, nvkm_disp_core_new, &gk104_disp_core },
		{{0,0,GK104_DISP_OVERLAY_CONTROL_DMA}, nvkm_disp_chan_new, &gk104_disp_ovly },
		{}
	},
};

int
gm107_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	return nvkm_disp_new_(&gm107_disp, device, type, inst, pdisp);
}
