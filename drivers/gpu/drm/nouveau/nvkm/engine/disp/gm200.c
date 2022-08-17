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
#include "hdmi.h"
#include "head.h"
#include "ior.h"
#include "outp.h"

#include <nvif/class.h>

void
gm200_sor_dp_drive(struct nvkm_ior *sor, int ln, int pc, int dc, int pe, int pu)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32  loff = nv50_sor_link(sor);
	const u32 shift = sor->func->dp->lanes[ln] * 8;
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

const struct nvkm_ior_func_dp
gm200_sor_dp = {
	.lanes = { 0, 1, 2, 3 },
	.links = gf119_sor_dp_links,
	.power = g94_sor_dp_power,
	.pattern = gm107_sor_dp_pattern,
	.drive = gm200_sor_dp_drive,
	.vcpi = gf119_sor_dp_vcpi,
	.audio = gf119_sor_dp_audio,
	.audio_sym = gf119_sor_dp_audio_sym,
	.watermark = gf119_sor_dp_watermark,
};

void
gm200_sor_hdmi_scdc(struct nvkm_ior *ior, u8 scdc)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(ior);
	const u32 ctrl = scdc & 0x3;

	nvkm_mask(device, 0x61c5bc + soff, 0x00000003, ctrl);

	ior->tmds.high_speed = !!(scdc & 0x2);
}

void
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

int
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
		.ctrl = gk104_sor_hdmi_ctrl,
		.scdc = gm200_sor_hdmi_scdc,
	},
	.dp = &gm200_sor_dp,
	.hda = &gf119_sor_hda,
};

static int
gm200_sor_new(struct nvkm_disp *disp, int id)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	u32 hda;

	if (!((hda = nvkm_rd32(device, 0x08a15c)) & 0x40000000))
		hda = nvkm_rd32(device, 0x101034);

	return nvkm_ior_new_(&gm200_sor, disp, SOR, id, hda & BIT(id));
}

static const struct nvkm_disp_func
gm200_disp = {
	.oneinit = nv50_disp_oneinit,
	.init = gf119_disp_init,
	.fini = gf119_disp_fini,
	.intr = gf119_disp_intr,
	.intr_error = gf119_disp_intr_error,
	.super = gf119_disp_super,
	.uevent = &gf119_disp_chan_uevent,
	.head = { .cnt = gf119_head_cnt, .new = gf119_head_new },
	.dac = { .cnt = gf119_dac_cnt, .new = gf119_dac_new },
	.sor = { .cnt = gf119_sor_cnt, .new = gm200_sor_new },
	.root = { 0,0,GM200_DISP },
	.user = {
		{{0,0,GK104_DISP_CURSOR             }, nvkm_disp_chan_new, &gf119_disp_curs },
		{{0,0,GK104_DISP_OVERLAY            }, nvkm_disp_chan_new, &gf119_disp_oimm },
		{{0,0,GK110_DISP_BASE_CHANNEL_DMA   }, nvkm_disp_chan_new, &gf119_disp_base },
		{{0,0,GM200_DISP_CORE_CHANNEL_DMA   }, nvkm_disp_core_new, &gk104_disp_core },
		{{0,0,GK104_DISP_OVERLAY_CONTROL_DMA}, nvkm_disp_chan_new, &gk104_disp_ovly },
		{}
	},
};

int
gm200_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	return nvkm_disp_new_(&gm200_disp, device, type, inst, pdisp);
}
