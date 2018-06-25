/*
 * Copyright 2017 Red Hat Inc.
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
#include "ior.h"

#include <subdev/timer.h>

void
gt215_sor_dp_audio(struct nvkm_ior *sor, int head, bool enable)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	const u32 data = 0x80000000 | (0x00000001 * enable);
	const u32 mask = 0x8000000d;
	nvkm_mask(device, 0x61c1e0 + soff, mask, data);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61c1e0 + soff) & 0x80000000))
			break;
	);
}

static const struct nvkm_ior_func
gt215_sor = {
	.state = g94_sor_state,
	.power = nv50_sor_power,
	.clock = nv50_sor_clock,
	.hdmi = {
		.ctrl = gt215_hdmi_ctrl,
	},
	.dp = {
		.lanes = { 2, 1, 0, 3 },
		.links = g94_sor_dp_links,
		.power = g94_sor_dp_power,
		.pattern = g94_sor_dp_pattern,
		.drive = g94_sor_dp_drive,
		.audio = gt215_sor_dp_audio,
		.audio_sym = g94_sor_dp_audio_sym,
		.activesym = g94_sor_dp_activesym,
		.watermark = g94_sor_dp_watermark,
	},
	.hda = {
		.hpd = gt215_hda_hpd,
		.eld = gt215_hda_eld,
	},
};

int
gt215_sor_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&gt215_sor, disp, SOR, id);
}
