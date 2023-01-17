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
#include "priv.h"
#include "chan.h"
#include "hdmi.h"
#include "head.h"
#include "ior.h"
#include "outp.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <core/ramht.h>
#include <subdev/timer.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

static void
gv100_sor_hda_device_entry(struct nvkm_ior *ior, int head)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 hoff = 0x800 * head;

	nvkm_mask(device, 0x616528 + hoff, 0x00000070, head << 4);
}

const struct nvkm_ior_func_hda
gv100_sor_hda = {
	.hpd = gf119_sor_hda_hpd,
	.eld = gf119_sor_hda_eld,
	.device_entry = gv100_sor_hda_device_entry,
};

void
gv100_sor_dp_watermark(struct nvkm_ior *sor, int head, u8 watermark)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 hoff = head * 0x800;

	nvkm_mask(device, 0x616550 + hoff, 0x0c00003f, 0x08000000 | watermark);
}

void
gv100_sor_dp_audio_sym(struct nvkm_ior *sor, int head, u16 h, u32 v)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 hoff = head * 0x800;

	nvkm_mask(device, 0x616568 + hoff, 0x0000ffff, h);
	nvkm_mask(device, 0x61656c + hoff, 0x00ffffff, v);
}

void
gv100_sor_dp_audio(struct nvkm_ior *sor, int head, bool enable)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 hoff = 0x800 * head;
	const u32 data = 0x80000000 | (0x00000001 * enable);
	const u32 mask = 0x8000000d;

	nvkm_mask(device, 0x616560 + hoff, mask, data);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x616560 + hoff) & 0x80000000))
			break;
	);
}

static const struct nvkm_ior_func_dp
gv100_sor_dp = {
	.lanes = { 0, 1, 2, 3 },
	.links = gf119_sor_dp_links,
	.power = g94_sor_dp_power,
	.pattern = gm107_sor_dp_pattern,
	.drive = gm200_sor_dp_drive,
	.audio = gv100_sor_dp_audio,
	.audio_sym = gv100_sor_dp_audio_sym,
	.watermark = gv100_sor_dp_watermark,
};

static void
gv100_sor_hdmi_infoframe_vsi(struct nvkm_ior *ior, int head, void *data, u32 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	struct packed_hdmi_infoframe vsi;
	const u32 hoff = head * 0x400;

	pack_hdmi_infoframe(&vsi, data, size);

	nvkm_mask(device, 0x6f0100 + hoff, 0x00010001, 0x00000000);
	if (!size)
		return;

	nvkm_wr32(device, 0x6f0108 + hoff, vsi.header);
	nvkm_wr32(device, 0x6f010c + hoff, vsi.subpack0_low);
	nvkm_wr32(device, 0x6f0110 + hoff, vsi.subpack0_high);
	nvkm_wr32(device, 0x6f0114 + hoff, 0x00000000);
	nvkm_wr32(device, 0x6f0118 + hoff, 0x00000000);
	nvkm_wr32(device, 0x6f011c + hoff, 0x00000000);
	nvkm_wr32(device, 0x6f0120 + hoff, 0x00000000);
	nvkm_wr32(device, 0x6f0124 + hoff, 0x00000000);
	nvkm_mask(device, 0x6f0100 + hoff, 0x00000001, 0x00000001);
}

static void
gv100_sor_hdmi_infoframe_avi(struct nvkm_ior *ior, int head, void *data, u32 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	struct packed_hdmi_infoframe avi;
	const u32 hoff = head * 0x400;

	pack_hdmi_infoframe(&avi, data, size);

	nvkm_mask(device, 0x6f0000 + hoff, 0x00000001, 0x00000000);
	if (!size)
		return;

	nvkm_wr32(device, 0x6f0008 + hoff, avi.header);
	nvkm_wr32(device, 0x6f000c + hoff, avi.subpack0_low);
	nvkm_wr32(device, 0x6f0010 + hoff, avi.subpack0_high);
	nvkm_wr32(device, 0x6f0014 + hoff, avi.subpack1_low);
	nvkm_wr32(device, 0x6f0018 + hoff, avi.subpack1_high);

	nvkm_mask(device, 0x6f0000 + hoff, 0x00000001, 0x00000001);
}

static void
gv100_sor_hdmi_ctrl(struct nvkm_ior *ior, int head, bool enable, u8 max_ac_packet, u8 rekey)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 ctrl = 0x40000000 * enable |
			 max_ac_packet << 16 |
			 rekey;
	const u32 hoff = head * 0x800;
	const u32 hdmi = head * 0x400;

	if (!(ctrl & 0x40000000)) {
		nvkm_mask(device, 0x6165c0 + hoff, 0x40000000, 0x00000000);
		nvkm_mask(device, 0x6f0100 + hdmi, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x6f00c0 + hdmi, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x6f0000 + hdmi, 0x00000001, 0x00000000);
		return;
	}

	/* General Control (GCP). */
	nvkm_mask(device, 0x6f00c0 + hdmi, 0x00000001, 0x00000000);
	nvkm_wr32(device, 0x6f00cc + hdmi, 0x00000010);
	nvkm_mask(device, 0x6f00c0 + hdmi, 0x00000001, 0x00000001);

	/* Audio Clock Regeneration (ACR). */
	nvkm_wr32(device, 0x6f0080 + hdmi, 0x82000000);

	/* NV_PDISP_SF_HDMI_CTRL. */
	nvkm_mask(device, 0x6165c0 + hoff, 0x401f007f, ctrl);
}

const struct nvkm_ior_func_hdmi
gv100_sor_hdmi = {
	.ctrl = gv100_sor_hdmi_ctrl,
	.scdc = gm200_sor_hdmi_scdc,
	.infoframe_avi = gv100_sor_hdmi_infoframe_avi,
	.infoframe_vsi = gv100_sor_hdmi_infoframe_vsi,
};

void
gv100_sor_state(struct nvkm_ior *sor, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 coff = (state == &sor->arm) * 0x8000 + sor->id * 0x20;
	u32 ctrl = nvkm_rd32(device, 0x680300 + coff);

	state->proto_evo = (ctrl & 0x00000f00) >> 8;
	switch (state->proto_evo) {
	case 0: state->proto = LVDS; state->link = 1; break;
	case 1: state->proto = TMDS; state->link = 1; break;
	case 2: state->proto = TMDS; state->link = 2; break;
	case 5: state->proto = TMDS; state->link = 3; break;
	case 8: state->proto =   DP; state->link = 1; break;
	case 9: state->proto =   DP; state->link = 2; break;
	default:
		state->proto = UNKNOWN;
		break;
	}

	state->head = ctrl & 0x000000ff;
}

static const struct nvkm_ior_func
gv100_sor = {
	.route = {
		.get = gm200_sor_route_get,
		.set = gm200_sor_route_set,
	},
	.state = gv100_sor_state,
	.power = nv50_sor_power,
	.clock = gf119_sor_clock,
	.hdmi = &gv100_sor_hdmi,
	.dp = &gv100_sor_dp,
	.hda = &gv100_sor_hda,
};

static int
gv100_sor_new(struct nvkm_disp *disp, int id)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	u32 hda;

	if (!((hda = nvkm_rd32(device, 0x08a15c)) & 0x40000000))
		hda = nvkm_rd32(device, 0x118fb0) >> 8;

	return nvkm_ior_new_(&gv100_sor, disp, SOR, id, hda & BIT(id));
}

int
gv100_sor_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;

	*pmask = (nvkm_rd32(device, 0x610060) & 0x0000ff00) >> 8;
	return (nvkm_rd32(device, 0x610074) & 0x00000f00) >> 8;
}

static void
gv100_head_vblank_put(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	nvkm_mask(device, 0x611d80 + (head->id * 4), 0x00000004, 0x00000000);
}

static void
gv100_head_vblank_get(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	nvkm_mask(device, 0x611d80 + (head->id * 4), 0x00000004, 0x00000004);
}

static void
gv100_head_rgpos(struct nvkm_head *head, u16 *hline, u16 *vline)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = head->id * 0x800;
	/* vline read locks hline. */
	*vline = nvkm_rd32(device, 0x616330 + hoff) & 0x0000ffff;
	*hline = nvkm_rd32(device, 0x616334 + hoff) & 0x0000ffff;
}

static void
gv100_head_state(struct nvkm_head *head, struct nvkm_head_state *state)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = (state == &head->arm) * 0x8000 + head->id * 0x400;
	u32 data;

	data = nvkm_rd32(device, 0x682064 + hoff);
	state->vtotal = (data & 0xffff0000) >> 16;
	state->htotal = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x682068 + hoff);
	state->vsynce = (data & 0xffff0000) >> 16;
	state->hsynce = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x68206c + hoff);
	state->vblanke = (data & 0xffff0000) >> 16;
	state->hblanke = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x682070 + hoff);
	state->vblanks = (data & 0xffff0000) >> 16;
	state->hblanks = (data & 0x0000ffff);
	state->hz = nvkm_rd32(device, 0x68200c + hoff);

	data = nvkm_rd32(device, 0x682004 + hoff);
	switch ((data & 0x000000f0) >> 4) {
	case 5: state->or.depth = 30; break;
	case 4: state->or.depth = 24; break;
	case 1: state->or.depth = 18; break;
	default:
		state->or.depth = 18;
		WARN_ON(1);
		break;
	}
}

static const struct nvkm_head_func
gv100_head = {
	.state = gv100_head_state,
	.rgpos = gv100_head_rgpos,
	.rgclk = gf119_head_rgclk,
	.vblank_get = gv100_head_vblank_get,
	.vblank_put = gv100_head_vblank_put,
};

int
gv100_head_new(struct nvkm_disp *disp, int id)
{
	struct nvkm_device *device = disp->engine.subdev.device;

	if (!(nvkm_rd32(device, 0x610060) & (0x00000001 << id)))
		return 0;

	return nvkm_head_new_(&gv100_head, disp, id);
}

int
gv100_head_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;

	*pmask = nvkm_rd32(device, 0x610060) & 0x000000ff;
	return nvkm_rd32(device, 0x610074) & 0x0000000f;
}

const struct nvkm_event_func
gv100_disp_chan_uevent = {
};

u64
gv100_disp_chan_user(struct nvkm_disp_chan *chan, u64 *psize)
{
	*psize = 0x1000;
	return 0x690000 + ((chan->chid.user - 1) * 0x1000);
}

static int
gv100_disp_dmac_idle(struct nvkm_disp_chan *chan)
{
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	const u32 soff = (chan->chid.ctrl - 1) * 0x04;
	nvkm_msec(device, 2000,
		u32 stat = nvkm_rd32(device, 0x610664 + soff);
		if ((stat & 0x000f0000) == 0x00040000)
			return 0;
	);
	return -EBUSY;
}

int
gv100_disp_dmac_bind(struct nvkm_disp_chan *chan,
		     struct nvkm_object *object, u32 handle)
{
	return nvkm_ramht_insert(chan->disp->ramht, object, chan->chid.user, -9, handle,
				 chan->chid.user << 25 | 0x00000040);
}

void
gv100_disp_dmac_fini(struct nvkm_disp_chan *chan)
{
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	const u32 uoff = (chan->chid.ctrl - 1) * 0x1000;
	const u32 coff = chan->chid.ctrl * 0x04;
	nvkm_mask(device, 0x6104e0 + coff, 0x00000010, 0x00000000);
	gv100_disp_dmac_idle(chan);
	nvkm_mask(device, 0x6104e0 + coff, 0x00000002, 0x00000000);
	chan->suspend_put = nvkm_rd32(device, 0x690000 + uoff);
}

int
gv100_disp_dmac_init(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	const u32 uoff = (chan->chid.ctrl - 1) * 0x1000;
	const u32 poff = chan->chid.ctrl * 0x10;
	const u32 coff = chan->chid.ctrl * 0x04;

	nvkm_wr32(device, 0x610b24 + poff, lower_32_bits(chan->push));
	nvkm_wr32(device, 0x610b20 + poff, upper_32_bits(chan->push));
	nvkm_wr32(device, 0x610b28 + poff, 0x00000001);
	nvkm_wr32(device, 0x610b2c + poff, 0x00000040);

	nvkm_mask(device, 0x6104e0 + coff, 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x690000 + uoff, chan->suspend_put);
	nvkm_wr32(device, 0x6104e0 + coff, 0x00000013);
	return gv100_disp_dmac_idle(chan);
}

static void
gv100_disp_wimm_intr(struct nvkm_disp_chan *chan, bool en)
{
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	const u32 mask = 0x00000001 << chan->head;
	const u32 data = en ? mask : 0;
	nvkm_mask(device, 0x611da8, mask, data);
}

static const struct nvkm_disp_chan_func
gv100_disp_wimm_func = {
	.push = nv50_disp_dmac_push,
	.init = gv100_disp_dmac_init,
	.fini = gv100_disp_dmac_fini,
	.intr = gv100_disp_wimm_intr,
	.user = gv100_disp_chan_user,
};

const struct nvkm_disp_chan_user
gv100_disp_wimm = {
	.func = &gv100_disp_wimm_func,
	.ctrl = 33,
	.user = 33,
};

static const struct nvkm_disp_mthd_list
gv100_disp_wndw_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0200, 0x690200 },
		{ 0x020c, 0x69020c },
		{ 0x0210, 0x690210 },
		{ 0x0214, 0x690214 },
		{ 0x0218, 0x690218 },
		{ 0x021c, 0x69021c },
		{ 0x0220, 0x690220 },
		{ 0x0224, 0x690224 },
		{ 0x0228, 0x690228 },
		{ 0x022c, 0x69022c },
		{ 0x0230, 0x690230 },
		{ 0x0234, 0x690234 },
		{ 0x0238, 0x690238 },
		{ 0x0240, 0x690240 },
		{ 0x0244, 0x690244 },
		{ 0x0248, 0x690248 },
		{ 0x024c, 0x69024c },
		{ 0x0250, 0x690250 },
		{ 0x0254, 0x690254 },
		{ 0x0260, 0x690260 },
		{ 0x0264, 0x690264 },
		{ 0x0268, 0x690268 },
		{ 0x026c, 0x69026c },
		{ 0x0270, 0x690270 },
		{ 0x0274, 0x690274 },
		{ 0x0280, 0x690280 },
		{ 0x0284, 0x690284 },
		{ 0x0288, 0x690288 },
		{ 0x028c, 0x69028c },
		{ 0x0290, 0x690290 },
		{ 0x0298, 0x690298 },
		{ 0x029c, 0x69029c },
		{ 0x02a0, 0x6902a0 },
		{ 0x02a4, 0x6902a4 },
		{ 0x02a8, 0x6902a8 },
		{ 0x02ac, 0x6902ac },
		{ 0x02b0, 0x6902b0 },
		{ 0x02b4, 0x6902b4 },
		{ 0x02b8, 0x6902b8 },
		{ 0x02bc, 0x6902bc },
		{ 0x02c0, 0x6902c0 },
		{ 0x02c4, 0x6902c4 },
		{ 0x02c8, 0x6902c8 },
		{ 0x02cc, 0x6902cc },
		{ 0x02d0, 0x6902d0 },
		{ 0x02d4, 0x6902d4 },
		{ 0x02d8, 0x6902d8 },
		{ 0x02dc, 0x6902dc },
		{ 0x02e0, 0x6902e0 },
		{ 0x02e4, 0x6902e4 },
		{ 0x02e8, 0x6902e8 },
		{ 0x02ec, 0x6902ec },
		{ 0x02f0, 0x6902f0 },
		{ 0x02f4, 0x6902f4 },
		{ 0x02f8, 0x6902f8 },
		{ 0x02fc, 0x6902fc },
		{ 0x0300, 0x690300 },
		{ 0x0304, 0x690304 },
		{ 0x0308, 0x690308 },
		{ 0x0310, 0x690310 },
		{ 0x0314, 0x690314 },
		{ 0x0318, 0x690318 },
		{ 0x031c, 0x69031c },
		{ 0x0320, 0x690320 },
		{ 0x0324, 0x690324 },
		{ 0x0328, 0x690328 },
		{ 0x032c, 0x69032c },
		{ 0x033c, 0x69033c },
		{ 0x0340, 0x690340 },
		{ 0x0344, 0x690344 },
		{ 0x0348, 0x690348 },
		{ 0x034c, 0x69034c },
		{ 0x0350, 0x690350 },
		{ 0x0354, 0x690354 },
		{ 0x0358, 0x690358 },
		{ 0x0364, 0x690364 },
		{ 0x0368, 0x690368 },
		{ 0x036c, 0x69036c },
		{ 0x0370, 0x690370 },
		{ 0x0374, 0x690374 },
		{ 0x0380, 0x690380 },
		{}
	}
};

static const struct nvkm_disp_chan_mthd
gv100_disp_wndw_mthd = {
	.name = "Window",
	.addr = 0x001000,
	.prev = 0x000800,
	.data = {
		{ "Global", 1, &gv100_disp_wndw_mthd_base },
		{}
	}
};

static void
gv100_disp_wndw_intr(struct nvkm_disp_chan *chan, bool en)
{
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	const u32 mask = 0x00000001 << chan->head;
	const u32 data = en ? mask : 0;
	nvkm_mask(device, 0x611da4, mask, data);
}

static const struct nvkm_disp_chan_func
gv100_disp_wndw_func = {
	.push = nv50_disp_dmac_push,
	.init = gv100_disp_dmac_init,
	.fini = gv100_disp_dmac_fini,
	.intr = gv100_disp_wndw_intr,
	.user = gv100_disp_chan_user,
	.bind = gv100_disp_dmac_bind,
};

const struct nvkm_disp_chan_user
gv100_disp_wndw = {
	.func = &gv100_disp_wndw_func,
	.ctrl = 1,
	.user = 1,
	.mthd = &gv100_disp_wndw_mthd,
};

int
gv100_disp_wndw_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;

	*pmask = nvkm_rd32(device, 0x610064);
	return (nvkm_rd32(device, 0x610074) & 0x03f00000) >> 20;
}

static int
gv100_disp_curs_idle(struct nvkm_disp_chan *chan)
{
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	const u32 soff = (chan->chid.ctrl - 1) * 0x04;
	nvkm_msec(device, 2000,
		u32 stat = nvkm_rd32(device, 0x610664 + soff);
		if ((stat & 0x00070000) == 0x00040000)
			return 0;
	);
	return -EBUSY;
}

static void
gv100_disp_curs_intr(struct nvkm_disp_chan *chan, bool en)
{
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	const u32 mask = 0x00010000 << chan->head;
	const u32 data = en ? mask : 0;
	nvkm_mask(device, 0x611dac, mask, data);
}

static void
gv100_disp_curs_fini(struct nvkm_disp_chan *chan)
{
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	const u32 hoff = chan->chid.ctrl * 4;
	nvkm_mask(device, 0x6104e0 + hoff, 0x00000010, 0x00000010);
	gv100_disp_curs_idle(chan);
	nvkm_mask(device, 0x6104e0 + hoff, 0x00000001, 0x00000000);
}

static int
gv100_disp_curs_init(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	nvkm_wr32(device, 0x6104e0 + chan->chid.ctrl * 4, 0x00000001);
	return gv100_disp_curs_idle(chan);
}

static const struct nvkm_disp_chan_func
gv100_disp_curs_func = {
	.init = gv100_disp_curs_init,
	.fini = gv100_disp_curs_fini,
	.intr = gv100_disp_curs_intr,
	.user = gv100_disp_chan_user,
};

const struct nvkm_disp_chan_user
gv100_disp_curs = {
	.func = &gv100_disp_curs_func,
	.ctrl = 73,
	.user = 73,
};

const struct nvkm_disp_mthd_list
gv100_disp_core_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0200, 0x680200 },
		{ 0x0208, 0x680208 },
		{ 0x020c, 0x68020c },
		{ 0x0210, 0x680210 },
		{ 0x0214, 0x680214 },
		{ 0x0218, 0x680218 },
		{ 0x021c, 0x68021c },
		{}
	}
};

static const struct nvkm_disp_mthd_list
gv100_disp_core_mthd_sor = {
	.mthd = 0x0020,
	.addr = 0x000020,
	.data = {
		{ 0x0300, 0x680300 },
		{ 0x0304, 0x680304 },
		{ 0x0308, 0x680308 },
		{ 0x030c, 0x68030c },
		{}
	}
};

static const struct nvkm_disp_mthd_list
gv100_disp_core_mthd_wndw = {
	.mthd = 0x0080,
	.addr = 0x000080,
	.data = {
		{ 0x1000, 0x681000 },
		{ 0x1004, 0x681004 },
		{ 0x1008, 0x681008 },
		{ 0x100c, 0x68100c },
		{ 0x1010, 0x681010 },
		{}
	}
};

static const struct nvkm_disp_mthd_list
gv100_disp_core_mthd_head = {
	.mthd = 0x0400,
	.addr = 0x000400,
	.data = {
		{ 0x2000, 0x682000 },
		{ 0x2004, 0x682004 },
		{ 0x2008, 0x682008 },
		{ 0x200c, 0x68200c },
		{ 0x2014, 0x682014 },
		{ 0x2018, 0x682018 },
		{ 0x201c, 0x68201c },
		{ 0x2020, 0x682020 },
		{ 0x2028, 0x682028 },
		{ 0x202c, 0x68202c },
		{ 0x2030, 0x682030 },
		{ 0x2038, 0x682038 },
		{ 0x203c, 0x68203c },
		{ 0x2048, 0x682048 },
		{ 0x204c, 0x68204c },
		{ 0x2050, 0x682050 },
		{ 0x2054, 0x682054 },
		{ 0x2058, 0x682058 },
		{ 0x205c, 0x68205c },
		{ 0x2060, 0x682060 },
		{ 0x2064, 0x682064 },
		{ 0x2068, 0x682068 },
		{ 0x206c, 0x68206c },
		{ 0x2070, 0x682070 },
		{ 0x2074, 0x682074 },
		{ 0x2078, 0x682078 },
		{ 0x207c, 0x68207c },
		{ 0x2080, 0x682080 },
		{ 0x2088, 0x682088 },
		{ 0x2090, 0x682090 },
		{ 0x209c, 0x68209c },
		{ 0x20a0, 0x6820a0 },
		{ 0x20a4, 0x6820a4 },
		{ 0x20a8, 0x6820a8 },
		{ 0x20ac, 0x6820ac },
		{ 0x2180, 0x682180 },
		{ 0x2184, 0x682184 },
		{ 0x218c, 0x68218c },
		{ 0x2194, 0x682194 },
		{ 0x2198, 0x682198 },
		{ 0x219c, 0x68219c },
		{ 0x21a0, 0x6821a0 },
		{ 0x21a4, 0x6821a4 },
		{ 0x2214, 0x682214 },
		{ 0x2218, 0x682218 },
		{}
	}
};

static const struct nvkm_disp_chan_mthd
gv100_disp_core_mthd = {
	.name = "Core",
	.addr = 0x000000,
	.prev = 0x008000,
	.data = {
		{ "Global", 1, &gv100_disp_core_mthd_base },
		{    "SOR", 4, &gv100_disp_core_mthd_sor  },
		{ "WINDOW", 8, &gv100_disp_core_mthd_wndw },
		{   "HEAD", 4, &gv100_disp_core_mthd_head },
		{}
	}
};

static int
gv100_disp_core_idle(struct nvkm_disp_chan *chan)
{
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	nvkm_msec(device, 2000,
		u32 stat = nvkm_rd32(device, 0x610630);
		if ((stat & 0x001f0000) == 0x000b0000)
			return 0;
	);
	return -EBUSY;
}

static u64
gv100_disp_core_user(struct nvkm_disp_chan *chan, u64 *psize)
{
	*psize = 0x10000;
	return 0x680000;
}

static void
gv100_disp_core_intr(struct nvkm_disp_chan *chan, bool en)
{
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	const u32 mask = 0x00000001;
	const u32 data = en ? mask : 0;
	nvkm_mask(device, 0x611dac, mask, data);
}

static void
gv100_disp_core_fini(struct nvkm_disp_chan *chan)
{
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	nvkm_mask(device, 0x6104e0, 0x00000010, 0x00000000);
	gv100_disp_core_idle(chan);
	nvkm_mask(device, 0x6104e0, 0x00000002, 0x00000000);
	chan->suspend_put = nvkm_rd32(device, 0x680000);
}

static int
gv100_disp_core_init(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;

	nvkm_wr32(device, 0x610b24, lower_32_bits(chan->push));
	nvkm_wr32(device, 0x610b20, upper_32_bits(chan->push));
	nvkm_wr32(device, 0x610b28, 0x00000001);
	nvkm_wr32(device, 0x610b2c, 0x00000040);

	nvkm_mask(device, 0x6104e0, 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x680000, chan->suspend_put);
	nvkm_wr32(device, 0x6104e0, 0x00000013);
	return gv100_disp_core_idle(chan);
}

static const struct nvkm_disp_chan_func
gv100_disp_core_func = {
	.push = nv50_disp_dmac_push,
	.init = gv100_disp_core_init,
	.fini = gv100_disp_core_fini,
	.intr = gv100_disp_core_intr,
	.user = gv100_disp_core_user,
	.bind = gv100_disp_dmac_bind,
};

const struct nvkm_disp_chan_user
gv100_disp_core = {
	.func = &gv100_disp_core_func,
	.ctrl = 0,
	.user = 0,
	.mthd = &gv100_disp_core_mthd,
};

#define gv100_disp_caps(p) container_of((p), struct gv100_disp_caps, object)

struct gv100_disp_caps {
	struct nvkm_object object;
	struct nvkm_disp *disp;
};

static int
gv100_disp_caps_map(struct nvkm_object *object, void *argv, u32 argc,
		    enum nvkm_object_map *type, u64 *addr, u64 *size)
{
	struct gv100_disp_caps *caps = gv100_disp_caps(object);
	struct nvkm_device *device = caps->disp->engine.subdev.device;
	*type = NVKM_OBJECT_MAP_IO;
	*addr = 0x640000 + device->func->resource_addr(device, 0);
	*size = 0x1000;
	return 0;
}

static const struct nvkm_object_func
gv100_disp_caps = {
	.map = gv100_disp_caps_map,
};

int
gv100_disp_caps_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = nvkm_udisp(oclass->parent);
	struct gv100_disp_caps *caps;

	if (!(caps = kzalloc(sizeof(*caps), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &caps->object;

	nvkm_object_ctor(&gv100_disp_caps, oclass, &caps->object);
	caps->disp = disp;
	return 0;
}

void
gv100_disp_super(struct work_struct *work)
{
	struct nvkm_disp *disp = container_of(work, struct nvkm_disp, super.work);
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_head *head;
	u32 stat, mask[4];

	mutex_lock(&disp->super.mutex);
	stat = nvkm_rd32(device, 0x6107a8);

	nvkm_debug(subdev, "supervisor %d: %08x\n", ffs(disp->super.pending), stat);
	list_for_each_entry(head, &disp->heads, head) {
		mask[head->id] = nvkm_rd32(device, 0x6107ac + (head->id * 4));
		HEAD_DBG(head, "%08x", mask[head->id]);
	}

	if (disp->super.pending & 0x00000001) {
		nv50_disp_chan_mthd(disp->chan[0], NV_DBG_DEBUG);
		nv50_disp_super_1(disp);
		list_for_each_entry(head, &disp->heads, head) {
			if (!(mask[head->id] & 0x00001000))
				continue;
			nv50_disp_super_1_0(disp, head);
		}
	} else
	if (disp->super.pending & 0x00000002) {
		list_for_each_entry(head, &disp->heads, head) {
			if (!(mask[head->id] & 0x00001000))
				continue;
			nv50_disp_super_2_0(disp, head);
		}
		nvkm_outp_route(disp);
		list_for_each_entry(head, &disp->heads, head) {
			if (!(mask[head->id] & 0x00010000))
				continue;
			nv50_disp_super_2_1(disp, head);
		}
		list_for_each_entry(head, &disp->heads, head) {
			if (!(mask[head->id] & 0x00001000))
				continue;
			nv50_disp_super_2_2(disp, head);
		}
	} else
	if (disp->super.pending & 0x00000004) {
		list_for_each_entry(head, &disp->heads, head) {
			if (!(mask[head->id] & 0x00001000))
				continue;
			nv50_disp_super_3_0(disp, head);
		}
	}

	list_for_each_entry(head, &disp->heads, head)
		nvkm_wr32(device, 0x6107ac + (head->id * 4), 0x00000000);

	nvkm_wr32(device, 0x6107a8, 0x80000000);
	mutex_unlock(&disp->super.mutex);
}

static void
gv100_disp_exception(struct nvkm_disp *disp, int chid)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x611020 + (chid * 12));
	u32 type = (stat & 0x00007000) >> 12;
	u32 mthd = (stat & 0x00000fff) << 2;
	const struct nvkm_enum *reason =
		nvkm_enum_find(nv50_disp_intr_error_type, type);

	/*TODO: Suspect 33->41 are for WRBK channel exceptions, but we
	 *      don't support those currently.
	 *
	 *      CORE+WIN CHIDs map directly to the FE_EXCEPT() slots.
	 */
	if (chid <= 32) {
		u32 data = nvkm_rd32(device, 0x611024 + (chid * 12));
		u32 code = nvkm_rd32(device, 0x611028 + (chid * 12));
		nvkm_error(subdev, "chid %d stat %08x reason %d [%s] "
				   "mthd %04x data %08x code %08x\n",
			   chid, stat, type, reason ? reason->name : "",
			   mthd, data, code);
	} else {
		nvkm_error(subdev, "chid %d stat %08x reason %d [%s] "
				   "mthd %04x\n",
			   chid, stat, type, reason ? reason->name : "", mthd);
	}

	if (chid < ARRAY_SIZE(disp->chan) && disp->chan[chid]) {
		switch (mthd) {
		case 0x0200:
			nv50_disp_chan_mthd(disp->chan[chid], NV_DBG_ERROR);
			break;
		default:
			break;
		}
	}

	nvkm_wr32(device, 0x611020 + (chid * 12), 0x90000000);
}

static void
gv100_disp_intr_ctrl_disp(struct nvkm_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x611c30);

	if (stat & 0x00000007) {
		disp->super.pending = (stat & 0x00000007);
		queue_work(disp->super.wq, &disp->super.work);
		nvkm_wr32(device, 0x611860, disp->super.pending);
		stat &= ~0x00000007;
	}

	/*TODO: I would guess this is VBIOS_RELEASE, however, NFI how to
	 *      ACK it, nor does RM appear to bother.
	 */
	if (stat & 0x00000008)
		stat &= ~0x00000008;

	if (stat & 0x00000080) {
		u32 error = nvkm_mask(device, 0x611848, 0x00000000, 0x00000000);
		nvkm_warn(subdev, "error %08x\n", error);
		stat &= ~0x00000080;
	}

	if (stat & 0x00000100) {
		unsigned long wndws = nvkm_rd32(device, 0x611858);
		unsigned long other = nvkm_rd32(device, 0x61185c);
		int wndw;

		nvkm_wr32(device, 0x611858, wndws);
		nvkm_wr32(device, 0x61185c, other);

		/* AWAKEN_OTHER_CORE. */
		if (other & 0x00000001)
			nv50_disp_chan_uevent_send(disp, 0);

		/* AWAKEN_WIN_CH(n). */
		for_each_set_bit(wndw, &wndws, disp->wndw.nr) {
			nv50_disp_chan_uevent_send(disp, 1 + wndw);
		}
	}

	if (stat)
		nvkm_warn(subdev, "ctrl %08x\n", stat);
}

static void
gv100_disp_intr_exc_other(struct nvkm_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x611854);
	unsigned long mask;
	int head;

	if (stat & 0x00000001) {
		nvkm_wr32(device, 0x611854, 0x00000001);
		gv100_disp_exception(disp, 0);
		stat &= ~0x00000001;
	}

	if ((mask = (stat & 0x00ff0000) >> 16)) {
		for_each_set_bit(head, &mask, disp->wndw.nr) {
			nvkm_wr32(device, 0x611854, 0x00010000 << head);
			gv100_disp_exception(disp, 73 + head);
			stat &= ~(0x00010000 << head);
		}
	}

	if (stat) {
		nvkm_warn(subdev, "exception %08x\n", stat);
		nvkm_wr32(device, 0x611854, stat);
	}
}

static void
gv100_disp_intr_exc_winim(struct nvkm_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	unsigned long stat = nvkm_rd32(device, 0x611850);
	int wndw;

	for_each_set_bit(wndw, &stat, disp->wndw.nr) {
		nvkm_wr32(device, 0x611850, BIT(wndw));
		gv100_disp_exception(disp, 33 + wndw);
		stat &= ~BIT(wndw);
	}

	if (stat) {
		nvkm_warn(subdev, "wimm %08x\n", (u32)stat);
		nvkm_wr32(device, 0x611850, stat);
	}
}

static void
gv100_disp_intr_exc_win(struct nvkm_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	unsigned long stat = nvkm_rd32(device, 0x61184c);
	int wndw;

	for_each_set_bit(wndw, &stat, disp->wndw.nr) {
		nvkm_wr32(device, 0x61184c, BIT(wndw));
		gv100_disp_exception(disp, 1 + wndw);
		stat &= ~BIT(wndw);
	}

	if (stat) {
		nvkm_warn(subdev, "wndw %08x\n", (u32)stat);
		nvkm_wr32(device, 0x61184c, stat);
	}
}

static void
gv100_disp_intr_head_timing(struct nvkm_disp *disp, int head)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x611800 + (head * 0x04));

	/* LAST_DATA, LOADV. */
	if (stat & 0x00000003) {
		nvkm_wr32(device, 0x611800 + (head * 0x04), stat & 0x00000003);
		stat &= ~0x00000003;
	}

	if (stat & 0x00000004) {
		nvkm_disp_vblank(disp, head);
		nvkm_wr32(device, 0x611800 + (head * 0x04), 0x00000004);
		stat &= ~0x00000004;
	}

	if (stat) {
		nvkm_warn(subdev, "head %08x\n", stat);
		nvkm_wr32(device, 0x611800 + (head * 0x04), stat);
	}
}

void
gv100_disp_intr(struct nvkm_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x611ec0);
	unsigned long mask;
	int head;

	if ((mask = (stat & 0x000000ff))) {
		for_each_set_bit(head, &mask, 8) {
			gv100_disp_intr_head_timing(disp, head);
			stat &= ~BIT(head);
		}
	}

	if (stat & 0x00000200) {
		gv100_disp_intr_exc_win(disp);
		stat &= ~0x00000200;
	}

	if (stat & 0x00000400) {
		gv100_disp_intr_exc_winim(disp);
		stat &= ~0x00000400;
	}

	if (stat & 0x00000800) {
		gv100_disp_intr_exc_other(disp);
		stat &= ~0x00000800;
	}

	if (stat & 0x00001000) {
		gv100_disp_intr_ctrl_disp(disp);
		stat &= ~0x00001000;
	}

	if (stat)
		nvkm_warn(subdev, "intr %08x\n", stat);
}

void
gv100_disp_fini(struct nvkm_disp *disp)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	nvkm_wr32(device, 0x611db0, 0x00000000);
}

static int
gv100_disp_init(struct nvkm_disp *disp)
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
	tmp = nvkm_rd32(device, 0x610068);
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
		for (j = 0; j < 6 * 4; j += 4) {
			tmp = nvkm_rd32(device, 0x616100 + (id * 0x800) + j);
			nvkm_wr32(device, 0x640030 + (id * 0x20) + j, tmp);
		}
	}

	/* Window capabilities. */
	for (i = 0; i < disp->wndw.nr; i++) {
		nvkm_mask(device, 0x640004, 1 << i, 1 << i);
		for (j = 0; j < 6 * 4; j += 4) {
			tmp = nvkm_rd32(device, 0x630050 + (i * 0x800) + j);
			nvkm_wr32(device, 0x6401e4 + (i * 0x20) + j, tmp);
		}
	}

	/* IHUB capabilities. */
	for (i = 0; i < 4; i++) {
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
gv100_disp = {
	.oneinit = nv50_disp_oneinit,
	.init = gv100_disp_init,
	.fini = gv100_disp_fini,
	.intr = gv100_disp_intr,
	.super = gv100_disp_super,
	.uevent = &gv100_disp_chan_uevent,
	.wndw = { .cnt = gv100_disp_wndw_cnt },
	.head = { .cnt = gv100_head_cnt, .new = gv100_head_new },
	.sor = { .cnt = gv100_sor_cnt, .new = gv100_sor_new },
	.ramht_size = 0x2000,
	.root = {  0, 0,GV100_DISP },
	.user = {
		{{-1,-1,GV100_DISP_CAPS                  }, gv100_disp_caps_new },
		{{ 0, 0,GV100_DISP_CURSOR                },  nvkm_disp_chan_new, &gv100_disp_curs },
		{{ 0, 0,GV100_DISP_WINDOW_IMM_CHANNEL_DMA},  nvkm_disp_wndw_new, &gv100_disp_wimm },
		{{ 0, 0,GV100_DISP_CORE_CHANNEL_DMA      },  nvkm_disp_core_new, &gv100_disp_core },
		{{ 0, 0,GV100_DISP_WINDOW_CHANNEL_DMA    },  nvkm_disp_wndw_new, &gv100_disp_wndw },
		{}
	},
};

int
gv100_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	return nvkm_disp_new_(&gv100_disp, device, type, inst, pdisp);
}
