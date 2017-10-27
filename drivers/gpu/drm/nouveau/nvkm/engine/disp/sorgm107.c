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
#include "ior.h"

void
gm107_sor_dp_pattern(struct nvkm_ior *sor, int pattern)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	const u32 data = 0x01010101 * pattern;
	if (sor->asy.link & 1)
		nvkm_mask(device, 0x61c110 + soff, 0x0f0f0f0f, data);
	else
		nvkm_mask(device, 0x61c12c + soff, 0x0f0f0f0f, data);
}

static const struct nvkm_ior_func
gm107_sor = {
	.state = gf119_sor_state,
	.power = nv50_sor_power,
	.clock = gf119_sor_clock,
	.hdmi = {
		.ctrl = gk104_hdmi_ctrl,
	},
	.dp = {
		.lanes = { 0, 1, 2, 3 },
		.links = gf119_sor_dp_links,
		.power = g94_sor_dp_power,
		.pattern = gm107_sor_dp_pattern,
		.drive = gf119_sor_dp_drive,
		.vcpi = gf119_sor_dp_vcpi,
		.audio = gf119_sor_dp_audio,
		.audio_sym = gf119_sor_dp_audio_sym,
		.watermark = gf119_sor_dp_watermark,
	},
	.hda = {
		.hpd = gf119_hda_hpd,
		.eld = gf119_hda_eld,
	},
};

int
gm107_sor_new(struct nvkm_disp *disp, int id)
{
	return gf119_sor_new_(&gm107_sor, disp, id);
}
