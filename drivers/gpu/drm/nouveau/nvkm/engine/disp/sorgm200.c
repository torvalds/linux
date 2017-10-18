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
#include "ior.h"

static void
gm200_sor_dp_drive(struct nvkm_ior *sor, int ln, int pc, int dc, int pe, int pu)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32  loff = nv50_sor_link(sor);
	const u32 shift = sor->func->dp.lanes[ln] * 8;
	u32 data[4];

	pu &= 0x0f;

	data[0] = nvkm_rd32(device, 0x61c118 + loff) & ~(0x000000ff << shift);
	data[1] = nvkm_rd32(device, 0x61c120 + loff) & ~(0x000000ff << shift);
	data[2] = nvkm_rd32(device, 0x61c130 + loff);
	if ((data[2] & 0x00000f00) < (pu << 8) || ln == 0)
		data[2] = (data[2] & ~0x00000f00) | (pu << 8);
	nvkm_wr32(device, 0x61c118 + loff, data[0] | (dc << shift));
	nvkm_wr32(device, 0x61c120 + loff, data[1] | (pe << shift));
	nvkm_wr32(device, 0x61c130 + loff, data[2]);
	data[3] = nvkm_rd32(device, 0x61c13c + loff) & ~(0x000000ff << shift);
	nvkm_wr32(device, 0x61c13c + loff, data[3] | (pc << shift));
}

static void
gm200_sor_route_set(struct nvkm_outp *outp, struct nvkm_ior *ior)
{
	struct nvkm_device *device = outp->disp->engine.subdev.device;
	const u32 moff = __ffs(outp->info.or) * 0x100;
	const u32  sor = ior ? ior->id + 1 : 0;
	u32 link = ior ? (ior->asy.link == 2) : 0;

	if (outp->info.sorconf.link & 1) {
		nvkm_mask(device, 0x612308 + moff, 0x0000001f, link << 4 | sor);
		link++;
	}

	if (outp->info.sorconf.link & 2)
		nvkm_mask(device, 0x612388 + moff, 0x0000001f, link << 4 | sor);
}

static int
gm200_sor_route_get(struct nvkm_outp *outp, int *link)
{
	struct nvkm_device *device = outp->disp->engine.subdev.device;
	const int sublinks = outp->info.sorconf.link;
	int lnk[2], sor[2], m, s;

	for (*link = 0, m = __ffs(outp->info.or) * 2, s = 0; s < 2; m++, s++) {
		if (sublinks & BIT(s)) {
			u32 data = nvkm_rd32(device, 0x612308 + (m * 0x80));
			lnk[s] = (data & 0x00000010) >> 4;
			sor[s] = (data & 0x0000000f);
			if (!sor[s])
				return -1;
			*link |= lnk[s];
		}
	}

	if (sublinks == 3) {
		if (sor[0] != sor[1] || WARN_ON(lnk[0] || !lnk[1]))
			return -1;
	}

	return ((sublinks & 1) ? sor[0] : sor[1]) - 1;
}

static const struct nvkm_ior_func
gm200_sor = {
	.route = {
		.get = gm200_sor_route_get,
		.set = gm200_sor_route_set,
	},
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
		.drive = gm200_sor_dp_drive,
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
gm200_sor_new(struct nvkm_disp *disp, int id)
{
	return gf119_sor_new_(&gm200_sor, disp, id);
}
