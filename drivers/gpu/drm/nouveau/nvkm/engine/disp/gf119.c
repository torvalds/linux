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

#include <core/ramht.h>
#include <subdev/timer.h>

#include <nvif/class.h>

static void
gf119_sor_hda_device_entry(struct nvkm_ior *ior, int head)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 hoff = 0x800 * head;

	nvkm_mask(device, 0x616548 + hoff, 0x00000070, head << 4);
}

void
gf119_sor_hda_eld(struct nvkm_ior *ior, int head, u8 *data, u8 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 soff = 0x030 * ior->id + (head * 0x04);
	int i;

	for (i = 0; i < size; i++)
		nvkm_wr32(device, 0x10ec00 + soff, (i << 8) | data[i]);
	for (; i < 0x60; i++)
		nvkm_wr32(device, 0x10ec00 + soff, (i << 8));
	nvkm_mask(device, 0x10ec10 + soff, 0x80000002, 0x80000002);
}

void
gf119_sor_hda_hpd(struct nvkm_ior *ior, int head, bool present)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 soff = 0x030 * ior->id + (head * 0x04);
	u32 data = 0x80000000;
	u32 mask = 0x80000001;

	if (present) {
		ior->func->hda->device_entry(ior, head);
		data |= 0x00000001;
	} else {
		mask |= 0x00000002;
	}

	nvkm_mask(device, 0x10ec10 + soff, mask, data);
}

const struct nvkm_ior_func_hda
gf119_sor_hda = {
	.hpd = gf119_sor_hda_hpd,
	.eld = gf119_sor_hda_eld,
	.device_entry = gf119_sor_hda_device_entry,
};

void
gf119_sor_dp_watermark(struct nvkm_ior *sor, int head, u8 watermark)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 hoff = head * 0x800;

	nvkm_mask(device, 0x616610 + hoff, 0x0800003f, 0x08000000 | watermark);
}

void
gf119_sor_dp_audio_sym(struct nvkm_ior *sor, int head, u16 h, u32 v)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 hoff = head * 0x800;

	nvkm_mask(device, 0x616620 + hoff, 0x0000ffff, h);
	nvkm_mask(device, 0x616624 + hoff, 0x00ffffff, v);
}

void
gf119_sor_dp_audio(struct nvkm_ior *sor, int head, bool enable)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 hoff = 0x800 * head;
	const u32 data = 0x80000000 | (0x00000001 * enable);
	const u32 mask = 0x8000000d;

	nvkm_mask(device, 0x616618 + hoff, mask, data);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x616618 + hoff) & 0x80000000))
			break;
	);
}

void
gf119_sor_dp_vcpi(struct nvkm_ior *sor, int head, u8 slot, u8 slot_nr, u16 pbn, u16 aligned)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 hoff = head * 0x800;

	nvkm_mask(device, 0x616588 + hoff, 0x00003f3f, (slot_nr << 8) | slot);
	nvkm_mask(device, 0x61658c + hoff, 0xffffffff, (aligned << 16) | pbn);
}

void
gf119_sor_dp_drive(struct nvkm_ior *sor, int ln, int pc, int dc, int pe, int pu)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32  loff = nv50_sor_link(sor);
	const u32 shift = sor->func->dp->lanes[ln] * 8;
	u32 data[4];

	data[0] = nvkm_rd32(device, 0x61c118 + loff) & ~(0x000000ff << shift);
	data[1] = nvkm_rd32(device, 0x61c120 + loff) & ~(0x000000ff << shift);
	data[2] = nvkm_rd32(device, 0x61c130 + loff);
	if ((data[2] & 0x0000ff00) < (pu << 8) || ln == 0)
		data[2] = (data[2] & ~0x0000ff00) | (pu << 8);

	nvkm_wr32(device, 0x61c118 + loff, data[0] | (dc << shift));
	nvkm_wr32(device, 0x61c120 + loff, data[1] | (pe << shift));
	nvkm_wr32(device, 0x61c130 + loff, data[2]);

	data[3] = nvkm_rd32(device, 0x61c13c + loff) & ~(0x000000ff << shift);
	nvkm_wr32(device, 0x61c13c + loff, data[3] | (pc << shift));
}

static void
gf119_sor_dp_pattern(struct nvkm_ior *sor, int pattern)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	u32 data;

	switch (pattern) {
	case 0: data = 0x10101010; break;
	case 1: data = 0x01010101; break;
	case 2: data = 0x02020202; break;
	case 3: data = 0x03030303; break;
	default:
		WARN_ON(1);
		return;
	}

	nvkm_mask(device, 0x61c110 + soff, 0x1f1f1f1f, data);
}

int
gf119_sor_dp_links(struct nvkm_ior *sor, struct nvkm_i2c_aux *aux)
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
	nvkm_mask(device, 0x61c10c + loff, 0x401f4000, dpctrl);
	return 0;
}

const struct nvkm_ior_func_dp
gf119_sor_dp = {
	.lanes = { 2, 1, 0, 3 },
	.links = gf119_sor_dp_links,
	.power = g94_sor_dp_power,
	.pattern = gf119_sor_dp_pattern,
	.drive = gf119_sor_dp_drive,
	.vcpi = gf119_sor_dp_vcpi,
	.audio = gf119_sor_dp_audio,
	.audio_sym = gf119_sor_dp_audio_sym,
	.watermark = gf119_sor_dp_watermark,
};

static void
gf119_sor_hdmi_infoframe_vsi(struct nvkm_ior *ior, int head, void *data, u32 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	struct packed_hdmi_infoframe vsi;
	const u32 hoff = head * 0x800;

	pack_hdmi_infoframe(&vsi, data, size);

	nvkm_mask(device, 0x616730 + hoff, 0x00010001, 0x00010000);
	if (!size)
		return;

	/*
	 * These appear to be the audio infoframe registers,
	 * but no other set of infoframe registers has yet
	 * been found.
	 */
	nvkm_wr32(device, 0x616738 + hoff, vsi.header);
	nvkm_wr32(device, 0x61673c + hoff, vsi.subpack0_low);
	nvkm_wr32(device, 0x616740 + hoff, vsi.subpack0_high);
	/* Is there a second (or further?) set of subpack registers here? */

	nvkm_mask(device, 0x616730 + hoff, 0x00000001, 0x00000001);
}

static void
gf119_sor_hdmi_infoframe_avi(struct nvkm_ior *ior, int head, void *data, u32 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	struct packed_hdmi_infoframe avi;
	const u32 hoff = head * 0x800;

	pack_hdmi_infoframe(&avi, data, size);

	nvkm_mask(device, 0x616714 + hoff, 0x00000001, 0x00000000);
	if (!size)
		return;

	nvkm_wr32(device, 0x61671c + hoff, avi.header);
	nvkm_wr32(device, 0x616720 + hoff, avi.subpack0_low);
	nvkm_wr32(device, 0x616724 + hoff, avi.subpack0_high);
	nvkm_wr32(device, 0x616728 + hoff, avi.subpack1_low);
	nvkm_wr32(device, 0x61672c + hoff, avi.subpack1_high);

	nvkm_mask(device, 0x616714 + hoff, 0x00000001, 0x00000001);
}

static void
gf119_sor_hdmi_ctrl(struct nvkm_ior *ior, int head, bool enable, u8 max_ac_packet, u8 rekey)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 ctrl = 0x40000000 * enable |
			 max_ac_packet << 16 |
			 rekey;
	const u32 hoff = head * 0x800;

	if (!(ctrl & 0x40000000)) {
		nvkm_mask(device, 0x616798 + hoff, 0x40000000, 0x00000000);
		nvkm_mask(device, 0x616730 + hoff, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x6167a4 + hoff, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x616714 + hoff, 0x00000001, 0x00000000);
		return;
	}

	/* ??? InfoFrame? */
	nvkm_mask(device, 0x6167a4 + hoff, 0x00000001, 0x00000000);
	nvkm_wr32(device, 0x6167ac + hoff, 0x00000010);
	nvkm_mask(device, 0x6167a4 + hoff, 0x00000001, 0x00000001);

	/* HDMI_CTRL */
	nvkm_mask(device, 0x616798 + hoff, 0x401f007f, ctrl);
}

static const struct nvkm_ior_func_hdmi
gf119_sor_hdmi = {
	.ctrl = gf119_sor_hdmi_ctrl,
	.infoframe_avi = gf119_sor_hdmi_infoframe_avi,
	.infoframe_vsi = gf119_sor_hdmi_infoframe_vsi,
};

void
gf119_sor_clock(struct nvkm_ior *sor)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	u32 div1 = sor->asy.link == 3;
	u32 div2 = sor->asy.link == 3;

	if (sor->asy.proto == TMDS) {
		const u32 speed = sor->tmds.high_speed ? 0x14 : 0x0a;
		nvkm_mask(device, 0x612300 + soff, 0x007c0000, speed << 18);
		if (sor->tmds.high_speed)
			div2 = 1;
	}

	nvkm_mask(device, 0x612300 + soff, 0x00000707, (div2 << 8) | div1);
}

void
gf119_sor_state(struct nvkm_ior *sor, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 coff = (state == &sor->asy) * 0x20000 + sor->id * 0x20;
	u32 ctrl = nvkm_rd32(device, 0x640200 + coff);

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

	state->head = ctrl & 0x0000000f;
}

static const struct nvkm_ior_func
gf119_sor = {
	.state = gf119_sor_state,
	.power = nv50_sor_power,
	.clock = gf119_sor_clock,
	.bl = &gt215_sor_bl,
	.hdmi = &gf119_sor_hdmi,
	.dp = &gf119_sor_dp,
	.hda = &gf119_sor_hda,
};

static int
gf119_sor_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&gf119_sor, disp, SOR, id, true);
}

int
gf119_sor_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	*pmask = (nvkm_rd32(device, 0x612004) & 0x0000ff00) >> 8;
	return 8;
}

static void
gf119_dac_clock(struct nvkm_ior *dac)
{
	struct nvkm_device *device = dac->disp->engine.subdev.device;
	const u32 doff = nv50_ior_base(dac);
	nvkm_mask(device, 0x612280 + doff, 0x07070707, 0x00000000);
}

static void
gf119_dac_state(struct nvkm_ior *dac, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = dac->disp->engine.subdev.device;
	const u32 coff = (state == &dac->asy) * 0x20000 + dac->id * 0x20;
	u32 ctrl = nvkm_rd32(device, 0x640180 + coff);

	state->proto_evo = (ctrl & 0x00000f00) >> 8;
	switch (state->proto_evo) {
	case 0: state->proto = CRT; break;
	default:
		state->proto = UNKNOWN;
		break;
	}

	state->head = ctrl & 0x0000000f;
}

static const struct nvkm_ior_func
gf119_dac = {
	.state = gf119_dac_state,
	.power = nv50_dac_power,
	.sense = nv50_dac_sense,
	.clock = gf119_dac_clock,
};

int
gf119_dac_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&gf119_dac, disp, DAC, id, false);
}

int
gf119_dac_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	*pmask = (nvkm_rd32(device, 0x612004) & 0x000000f0) >> 4;
	return 4;
}

static void
gf119_head_vblank_put(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = head->id * 0x800;
	nvkm_mask(device, 0x6100c0 + hoff, 0x00000001, 0x00000000);
}

static void
gf119_head_vblank_get(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = head->id * 0x800;
	nvkm_mask(device, 0x6100c0 + hoff, 0x00000001, 0x00000001);
}

void
gf119_head_rgclk(struct nvkm_head *head, int div)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	nvkm_mask(device, 0x612200 + (head->id * 0x800), 0x0000000f, div);
}

static void
gf119_head_state(struct nvkm_head *head, struct nvkm_head_state *state)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = (state == &head->asy) * 0x20000 + head->id * 0x300;
	u32 data;

	data = nvkm_rd32(device, 0x640414 + hoff);
	state->vtotal = (data & 0xffff0000) >> 16;
	state->htotal = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x640418 + hoff);
	state->vsynce = (data & 0xffff0000) >> 16;
	state->hsynce = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x64041c + hoff);
	state->vblanke = (data & 0xffff0000) >> 16;
	state->hblanke = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x640420 + hoff);
	state->vblanks = (data & 0xffff0000) >> 16;
	state->hblanks = (data & 0x0000ffff);
	state->hz = nvkm_rd32(device, 0x640450 + hoff);

	data = nvkm_rd32(device, 0x640404 + hoff);
	switch ((data & 0x000003c0) >> 6) {
	case 6: state->or.depth = 30; break;
	case 5: state->or.depth = 24; break;
	case 2: state->or.depth = 18; break;
	case 0: state->or.depth = 18; break; /*XXX: "default" */
	default:
		state->or.depth = 18;
		WARN_ON(1);
		break;
	}
}

static const struct nvkm_head_func
gf119_head = {
	.state = gf119_head_state,
	.rgpos = nv50_head_rgpos,
	.rgclk = gf119_head_rgclk,
	.vblank_get = gf119_head_vblank_get,
	.vblank_put = gf119_head_vblank_put,
};

int
gf119_head_new(struct nvkm_disp *disp, int id)
{
	return nvkm_head_new_(&gf119_head, disp, id);
}

int
gf119_head_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	*pmask = nvkm_rd32(device, 0x612004) & 0x0000000f;
	return nvkm_rd32(device, 0x022448);
}

static void
gf119_disp_chan_uevent_fini(struct nvkm_event *event, int type, int index)
{
	struct nvkm_disp *disp = container_of(event, typeof(*disp), uevent);
	struct nvkm_device *device = disp->engine.subdev.device;
	nvkm_mask(device, 0x610090, 0x00000001 << index, 0x00000000 << index);
	nvkm_wr32(device, 0x61008c, 0x00000001 << index);
}

static void
gf119_disp_chan_uevent_init(struct nvkm_event *event, int types, int index)
{
	struct nvkm_disp *disp = container_of(event, typeof(*disp), uevent);
	struct nvkm_device *device = disp->engine.subdev.device;
	nvkm_wr32(device, 0x61008c, 0x00000001 << index);
	nvkm_mask(device, 0x610090, 0x00000001 << index, 0x00000001 << index);
}

const struct nvkm_event_func
gf119_disp_chan_uevent = {
	.init = gf119_disp_chan_uevent_init,
	.fini = gf119_disp_chan_uevent_fini,
};

void
gf119_disp_chan_intr(struct nvkm_disp_chan *chan, bool en)
{
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	const u32 mask = 0x00000001 << chan->chid.user;
	if (!en) {
		nvkm_mask(device, 0x610090, mask, 0x00000000);
		nvkm_mask(device, 0x6100a0, mask, 0x00000000);
	} else {
		nvkm_mask(device, 0x6100a0, mask, mask);
	}
}

static void
gf119_disp_pioc_fini(struct nvkm_disp_chan *chan)
{
	struct nvkm_disp *disp = chan->disp;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	nvkm_mask(device, 0x610490 + (ctrl * 0x10), 0x00000001, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610490 + (ctrl * 0x10)) & 0x00030000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d fini: %08x\n", user,
			   nvkm_rd32(device, 0x610490 + (ctrl * 0x10)));
	}
}

static int
gf119_disp_pioc_init(struct nvkm_disp_chan *chan)
{
	struct nvkm_disp *disp = chan->disp;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	/* activate channel */
	nvkm_wr32(device, 0x610490 + (ctrl * 0x10), 0x00000001);
	if (nvkm_msec(device, 2000,
		u32 tmp = nvkm_rd32(device, 0x610490 + (ctrl * 0x10));
		if ((tmp & 0x00030000) == 0x00010000)
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d init: %08x\n", user,
			   nvkm_rd32(device, 0x610490 + (ctrl * 0x10)));
		return -EBUSY;
	}

	return 0;
}

const struct nvkm_disp_chan_func
gf119_disp_pioc_func = {
	.init = gf119_disp_pioc_init,
	.fini = gf119_disp_pioc_fini,
	.intr = gf119_disp_chan_intr,
	.user = nv50_disp_chan_user,
};

int
gf119_disp_dmac_bind(struct nvkm_disp_chan *chan, struct nvkm_object *object, u32 handle)
{
	return nvkm_ramht_insert(chan->disp->ramht, object, chan->chid.user, -9, handle,
				 chan->chid.user << 27 | 0x00000001);
}

void
gf119_disp_dmac_fini(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	/* deactivate channel */
	nvkm_mask(device, 0x610490 + (ctrl * 0x0010), 0x00001010, 0x00001000);
	nvkm_mask(device, 0x610490 + (ctrl * 0x0010), 0x00000003, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610490 + (ctrl * 0x10)) & 0x001e0000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d fini: %08x\n", user,
			   nvkm_rd32(device, 0x610490 + (ctrl * 0x10)));
	}

	chan->suspend_put = nvkm_rd32(device, 0x640000 + (ctrl * 0x1000));
}

static int
gf119_disp_dmac_init(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	/* initialise channel for dma command submission */
	nvkm_wr32(device, 0x610494 + (ctrl * 0x0010), chan->push);
	nvkm_wr32(device, 0x610498 + (ctrl * 0x0010), 0x00010000);
	nvkm_wr32(device, 0x61049c + (ctrl * 0x0010), 0x00000001);
	nvkm_mask(device, 0x610490 + (ctrl * 0x0010), 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x640000 + (ctrl * 0x1000), chan->suspend_put);
	nvkm_wr32(device, 0x610490 + (ctrl * 0x0010), 0x00000013);

	/* wait for it to go inactive */
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610490 + (ctrl * 0x10)) & 0x80000000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d init: %08x\n", user,
			   nvkm_rd32(device, 0x610490 + (ctrl * 0x10)));
		return -EBUSY;
	}

	return 0;
}

const struct nvkm_disp_chan_func
gf119_disp_dmac_func = {
	.push = nv50_disp_dmac_push,
	.init = gf119_disp_dmac_init,
	.fini = gf119_disp_dmac_fini,
	.intr = gf119_disp_chan_intr,
	.user = nv50_disp_chan_user,
	.bind = gf119_disp_dmac_bind,
};

const struct nvkm_disp_chan_user
gf119_disp_curs = {
	.func = &gf119_disp_pioc_func,
	.ctrl = 13,
	.user = 13,
};

const struct nvkm_disp_chan_user
gf119_disp_oimm = {
	.func = &gf119_disp_pioc_func,
	.ctrl = 9,
	.user = 9,
};

static const struct nvkm_disp_mthd_list
gf119_disp_ovly_mthd_base = {
	.mthd = 0x0000,
	.data = {
		{ 0x0080, 0x665080 },
		{ 0x0084, 0x665084 },
		{ 0x0088, 0x665088 },
		{ 0x008c, 0x66508c },
		{ 0x0090, 0x665090 },
		{ 0x0094, 0x665094 },
		{ 0x00a0, 0x6650a0 },
		{ 0x00a4, 0x6650a4 },
		{ 0x00b0, 0x6650b0 },
		{ 0x00b4, 0x6650b4 },
		{ 0x00b8, 0x6650b8 },
		{ 0x00c0, 0x6650c0 },
		{ 0x00e0, 0x6650e0 },
		{ 0x00e4, 0x6650e4 },
		{ 0x00e8, 0x6650e8 },
		{ 0x0100, 0x665100 },
		{ 0x0104, 0x665104 },
		{ 0x0108, 0x665108 },
		{ 0x010c, 0x66510c },
		{ 0x0110, 0x665110 },
		{ 0x0118, 0x665118 },
		{ 0x011c, 0x66511c },
		{ 0x0120, 0x665120 },
		{ 0x0124, 0x665124 },
		{ 0x0130, 0x665130 },
		{ 0x0134, 0x665134 },
		{ 0x0138, 0x665138 },
		{ 0x013c, 0x66513c },
		{ 0x0140, 0x665140 },
		{ 0x0144, 0x665144 },
		{ 0x0148, 0x665148 },
		{ 0x014c, 0x66514c },
		{ 0x0150, 0x665150 },
		{ 0x0154, 0x665154 },
		{ 0x0158, 0x665158 },
		{ 0x015c, 0x66515c },
		{ 0x0160, 0x665160 },
		{ 0x0164, 0x665164 },
		{ 0x0168, 0x665168 },
		{ 0x016c, 0x66516c },
		{ 0x0400, 0x665400 },
		{ 0x0408, 0x665408 },
		{ 0x040c, 0x66540c },
		{ 0x0410, 0x665410 },
		{}
	}
};

static const struct nvkm_disp_chan_mthd
gf119_disp_ovly_mthd = {
	.name = "Overlay",
	.addr = 0x001000,
	.prev = -0x020000,
	.data = {
		{ "Global", 1, &gf119_disp_ovly_mthd_base },
		{}
	}
};

static const struct nvkm_disp_chan_user
gf119_disp_ovly = {
	.func = &gf119_disp_dmac_func,
	.ctrl = 5,
	.user = 5,
	.mthd = &gf119_disp_ovly_mthd,
};

static const struct nvkm_disp_mthd_list
gf119_disp_base_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x661080 },
		{ 0x0084, 0x661084 },
		{ 0x0088, 0x661088 },
		{ 0x008c, 0x66108c },
		{ 0x0090, 0x661090 },
		{ 0x0094, 0x661094 },
		{ 0x00a0, 0x6610a0 },
		{ 0x00a4, 0x6610a4 },
		{ 0x00c0, 0x6610c0 },
		{ 0x00c4, 0x6610c4 },
		{ 0x00c8, 0x6610c8 },
		{ 0x00cc, 0x6610cc },
		{ 0x00e0, 0x6610e0 },
		{ 0x00e4, 0x6610e4 },
		{ 0x00e8, 0x6610e8 },
		{ 0x00ec, 0x6610ec },
		{ 0x00fc, 0x6610fc },
		{ 0x0100, 0x661100 },
		{ 0x0104, 0x661104 },
		{ 0x0108, 0x661108 },
		{ 0x010c, 0x66110c },
		{ 0x0110, 0x661110 },
		{ 0x0114, 0x661114 },
		{ 0x0118, 0x661118 },
		{ 0x011c, 0x66111c },
		{ 0x0130, 0x661130 },
		{ 0x0134, 0x661134 },
		{ 0x0138, 0x661138 },
		{ 0x013c, 0x66113c },
		{ 0x0140, 0x661140 },
		{ 0x0144, 0x661144 },
		{ 0x0148, 0x661148 },
		{ 0x014c, 0x66114c },
		{ 0x0150, 0x661150 },
		{ 0x0154, 0x661154 },
		{ 0x0158, 0x661158 },
		{ 0x015c, 0x66115c },
		{ 0x0160, 0x661160 },
		{ 0x0164, 0x661164 },
		{ 0x0168, 0x661168 },
		{ 0x016c, 0x66116c },
		{}
	}
};

static const struct nvkm_disp_mthd_list
gf119_disp_base_mthd_image = {
	.mthd = 0x0020,
	.addr = 0x000020,
	.data = {
		{ 0x0400, 0x661400 },
		{ 0x0404, 0x661404 },
		{ 0x0408, 0x661408 },
		{ 0x040c, 0x66140c },
		{ 0x0410, 0x661410 },
		{}
	}
};

const struct nvkm_disp_chan_mthd
gf119_disp_base_mthd = {
	.name = "Base",
	.addr = 0x001000,
	.prev = -0x020000,
	.data = {
		{ "Global", 1, &gf119_disp_base_mthd_base },
		{  "Image", 2, &gf119_disp_base_mthd_image },
		{}
	}
};

const struct nvkm_disp_chan_user
gf119_disp_base = {
	.func = &gf119_disp_dmac_func,
	.ctrl = 1,
	.user = 1,
	.mthd = &gf119_disp_base_mthd,
};

const struct nvkm_disp_mthd_list
gf119_disp_core_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x660080 },
		{ 0x0084, 0x660084 },
		{ 0x0088, 0x660088 },
		{ 0x008c, 0x000000 },
		{}
	}
};

const struct nvkm_disp_mthd_list
gf119_disp_core_mthd_dac = {
	.mthd = 0x0020,
	.addr = 0x000020,
	.data = {
		{ 0x0180, 0x660180 },
		{ 0x0184, 0x660184 },
		{ 0x0188, 0x660188 },
		{ 0x0190, 0x660190 },
		{}
	}
};

const struct nvkm_disp_mthd_list
gf119_disp_core_mthd_sor = {
	.mthd = 0x0020,
	.addr = 0x000020,
	.data = {
		{ 0x0200, 0x660200 },
		{ 0x0204, 0x660204 },
		{ 0x0208, 0x660208 },
		{ 0x0210, 0x660210 },
		{}
	}
};

const struct nvkm_disp_mthd_list
gf119_disp_core_mthd_pior = {
	.mthd = 0x0020,
	.addr = 0x000020,
	.data = {
		{ 0x0300, 0x660300 },
		{ 0x0304, 0x660304 },
		{ 0x0308, 0x660308 },
		{ 0x0310, 0x660310 },
		{}
	}
};

static const struct nvkm_disp_mthd_list
gf119_disp_core_mthd_head = {
	.mthd = 0x0300,
	.addr = 0x000300,
	.data = {
		{ 0x0400, 0x660400 },
		{ 0x0404, 0x660404 },
		{ 0x0408, 0x660408 },
		{ 0x040c, 0x66040c },
		{ 0x0410, 0x660410 },
		{ 0x0414, 0x660414 },
		{ 0x0418, 0x660418 },
		{ 0x041c, 0x66041c },
		{ 0x0420, 0x660420 },
		{ 0x0424, 0x660424 },
		{ 0x0428, 0x660428 },
		{ 0x042c, 0x66042c },
		{ 0x0430, 0x660430 },
		{ 0x0434, 0x660434 },
		{ 0x0438, 0x660438 },
		{ 0x0440, 0x660440 },
		{ 0x0444, 0x660444 },
		{ 0x0448, 0x660448 },
		{ 0x044c, 0x66044c },
		{ 0x0450, 0x660450 },
		{ 0x0454, 0x660454 },
		{ 0x0458, 0x660458 },
		{ 0x045c, 0x66045c },
		{ 0x0460, 0x660460 },
		{ 0x0468, 0x660468 },
		{ 0x046c, 0x66046c },
		{ 0x0470, 0x660470 },
		{ 0x0474, 0x660474 },
		{ 0x0480, 0x660480 },
		{ 0x0484, 0x660484 },
		{ 0x048c, 0x66048c },
		{ 0x0490, 0x660490 },
		{ 0x0494, 0x660494 },
		{ 0x0498, 0x660498 },
		{ 0x04b0, 0x6604b0 },
		{ 0x04b8, 0x6604b8 },
		{ 0x04bc, 0x6604bc },
		{ 0x04c0, 0x6604c0 },
		{ 0x04c4, 0x6604c4 },
		{ 0x04c8, 0x6604c8 },
		{ 0x04d0, 0x6604d0 },
		{ 0x04d4, 0x6604d4 },
		{ 0x04e0, 0x6604e0 },
		{ 0x04e4, 0x6604e4 },
		{ 0x04e8, 0x6604e8 },
		{ 0x04ec, 0x6604ec },
		{ 0x04f0, 0x6604f0 },
		{ 0x04f4, 0x6604f4 },
		{ 0x04f8, 0x6604f8 },
		{ 0x04fc, 0x6604fc },
		{ 0x0500, 0x660500 },
		{ 0x0504, 0x660504 },
		{ 0x0508, 0x660508 },
		{ 0x050c, 0x66050c },
		{ 0x0510, 0x660510 },
		{ 0x0514, 0x660514 },
		{ 0x0518, 0x660518 },
		{ 0x051c, 0x66051c },
		{ 0x052c, 0x66052c },
		{ 0x0530, 0x660530 },
		{ 0x054c, 0x66054c },
		{ 0x0550, 0x660550 },
		{ 0x0554, 0x660554 },
		{ 0x0558, 0x660558 },
		{ 0x055c, 0x66055c },
		{}
	}
};

static const struct nvkm_disp_chan_mthd
gf119_disp_core_mthd = {
	.name = "Core",
	.addr = 0x000000,
	.prev = -0x020000,
	.data = {
		{ "Global", 1, &gf119_disp_core_mthd_base },
		{    "DAC", 3, &gf119_disp_core_mthd_dac  },
		{    "SOR", 8, &gf119_disp_core_mthd_sor  },
		{   "PIOR", 4, &gf119_disp_core_mthd_pior },
		{   "HEAD", 4, &gf119_disp_core_mthd_head },
		{}
	}
};

void
gf119_disp_core_fini(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;

	/* deactivate channel */
	nvkm_mask(device, 0x610490, 0x00000010, 0x00000000);
	nvkm_mask(device, 0x610490, 0x00000003, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610490) & 0x001e0000))
			break;
	) < 0) {
		nvkm_error(subdev, "core fini: %08x\n",
			   nvkm_rd32(device, 0x610490));
	}

	chan->suspend_put = nvkm_rd32(device, 0x640000);
}

static int
gf119_disp_core_init(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;

	/* initialise channel for dma command submission */
	nvkm_wr32(device, 0x610494, chan->push);
	nvkm_wr32(device, 0x610498, 0x00010000);
	nvkm_wr32(device, 0x61049c, 0x00000001);
	nvkm_mask(device, 0x610490, 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x640000, chan->suspend_put);
	nvkm_wr32(device, 0x610490, 0x01000013);

	/* wait for it to go inactive */
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610490) & 0x80000000))
			break;
	) < 0) {
		nvkm_error(subdev, "core init: %08x\n",
			   nvkm_rd32(device, 0x610490));
		return -EBUSY;
	}

	return 0;
}

const struct nvkm_disp_chan_func
gf119_disp_core_func = {
	.push = nv50_disp_dmac_push,
	.init = gf119_disp_core_init,
	.fini = gf119_disp_core_fini,
	.intr = gf119_disp_chan_intr,
	.user = nv50_disp_chan_user,
	.bind = gf119_disp_dmac_bind,
};

static const struct nvkm_disp_chan_user
gf119_disp_core = {
	.func = &gf119_disp_core_func,
	.ctrl = 0,
	.user = 0,
	.mthd = &gf119_disp_core_mthd,
};

void
gf119_disp_super(struct work_struct *work)
{
	struct nvkm_disp *disp = container_of(work, struct nvkm_disp, super.work);
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_head *head;
	u32 mask[4];

	nvkm_debug(subdev, "supervisor %d\n", ffs(disp->super.pending));
	mutex_lock(&disp->super.mutex);

	list_for_each_entry(head, &disp->heads, head) {
		mask[head->id] = nvkm_rd32(device, 0x6101d4 + (head->id * 0x800));
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
		nvkm_wr32(device, 0x6101d4 + (head->id * 0x800), 0x00000000);

	nvkm_wr32(device, 0x6101d0, 0x80000000);
	mutex_unlock(&disp->super.mutex);
}

void
gf119_disp_intr_error(struct nvkm_disp *disp, int chid)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x6101f0 + (chid * 12));
	u32 type = (stat & 0x00007000) >> 12;
	u32 mthd = (stat & 0x00000ffc);
	u32 data = nvkm_rd32(device, 0x6101f4 + (chid * 12));
	u32 code = nvkm_rd32(device, 0x6101f8 + (chid * 12));
	const struct nvkm_enum *reason =
		nvkm_enum_find(nv50_disp_intr_error_type, type);

	nvkm_error(subdev, "chid %d stat %08x reason %d [%s] mthd %04x "
			   "data %08x code %08x\n",
		   chid, stat, type, reason ? reason->name : "",
		   mthd, data, code);

	if (chid < ARRAY_SIZE(disp->chan)) {
		switch (mthd) {
		case 0x0080:
			nv50_disp_chan_mthd(disp->chan[chid], NV_DBG_ERROR);
			break;
		default:
			break;
		}
	}

	nvkm_wr32(device, 0x61009c, (1 << chid));
	nvkm_wr32(device, 0x6101f0 + (chid * 12), 0x90000000);
}

void
gf119_disp_intr(struct nvkm_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_head *head;
	u32 intr = nvkm_rd32(device, 0x610088);

	if (intr & 0x00000001) {
		u32 stat = nvkm_rd32(device, 0x61008c);
		while (stat) {
			int chid = __ffs(stat); stat &= ~(1 << chid);
			nv50_disp_chan_uevent_send(disp, chid);
			nvkm_wr32(device, 0x61008c, 1 << chid);
		}
		intr &= ~0x00000001;
	}

	if (intr & 0x00000002) {
		u32 stat = nvkm_rd32(device, 0x61009c);
		int chid = ffs(stat) - 1;
		if (chid >= 0)
			disp->func->intr_error(disp, chid);
		intr &= ~0x00000002;
	}

	if (intr & 0x00100000) {
		u32 stat = nvkm_rd32(device, 0x6100ac);
		if (stat & 0x00000007) {
			disp->super.pending = (stat & 0x00000007);
			queue_work(disp->super.wq, &disp->super.work);
			nvkm_wr32(device, 0x6100ac, disp->super.pending);
			stat &= ~0x00000007;
		}

		if (stat) {
			nvkm_warn(subdev, "intr24 %08x\n", stat);
			nvkm_wr32(device, 0x6100ac, stat);
		}

		intr &= ~0x00100000;
	}

	list_for_each_entry(head, &disp->heads, head) {
		const u32 hoff = head->id * 0x800;
		u32 mask = 0x01000000 << head->id;
		if (mask & intr) {
			u32 stat = nvkm_rd32(device, 0x6100bc + hoff);
			if (stat & 0x00000001)
				nvkm_disp_vblank(disp, head->id);
			nvkm_mask(device, 0x6100bc + hoff, 0, 0);
			nvkm_rd32(device, 0x6100c0 + hoff);
		}
	}
}

void
gf119_disp_fini(struct nvkm_disp *disp)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	/* disable all interrupts */
	nvkm_wr32(device, 0x6100b0, 0x00000000);
}

int
gf119_disp_init(struct nvkm_disp *disp)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	struct nvkm_head *head;
	u32 tmp;
	int i;

	/* The below segments of code copying values from one register to
	 * another appear to inform EVO of the display capabilities or
	 * something similar.
	 */

	/* ... CRTC caps */
	list_for_each_entry(head, &disp->heads, head) {
		const u32 hoff = head->id * 0x800;
		tmp = nvkm_rd32(device, 0x616104 + hoff);
		nvkm_wr32(device, 0x6101b4 + hoff, tmp);
		tmp = nvkm_rd32(device, 0x616108 + hoff);
		nvkm_wr32(device, 0x6101b8 + hoff, tmp);
		tmp = nvkm_rd32(device, 0x61610c + hoff);
		nvkm_wr32(device, 0x6101bc + hoff, tmp);
	}

	/* ... DAC caps */
	for (i = 0; i < disp->dac.nr; i++) {
		tmp = nvkm_rd32(device, 0x61a000 + (i * 0x800));
		nvkm_wr32(device, 0x6101c0 + (i * 0x800), tmp);
	}

	/* ... SOR caps */
	for (i = 0; i < disp->sor.nr; i++) {
		tmp = nvkm_rd32(device, 0x61c000 + (i * 0x800));
		nvkm_wr32(device, 0x6301c4 + (i * 0x800), tmp);
	}

	/* steal display away from vbios, or something like that */
	if (nvkm_rd32(device, 0x6100ac) & 0x00000100) {
		nvkm_wr32(device, 0x6100ac, 0x00000100);
		nvkm_mask(device, 0x6194e8, 0x00000001, 0x00000000);
		if (nvkm_msec(device, 2000,
			if (!(nvkm_rd32(device, 0x6194e8) & 0x00000002))
				break;
		) < 0)
			return -EBUSY;
	}

	/* point at display engine memory area (hash table, objects) */
	nvkm_wr32(device, 0x610010, (disp->inst->addr >> 8) | 9);

	/* enable supervisor interrupts, disable everything else */
	nvkm_wr32(device, 0x610090, 0x00000000);
	nvkm_wr32(device, 0x6100a0, 0x00000000);
	nvkm_wr32(device, 0x6100b0, 0x00000307);

	/* disable underflow reporting, preventing an intermittent issue
	 * on some gk104 boards where the production vbios left this
	 * setting enabled by default.
	 *
	 * ftp://download.nvidia.com/open-gpu-doc/gk104-disable-underflow-reporting/1/gk104-disable-underflow-reporting.txt
	 */
	list_for_each_entry(head, &disp->heads, head) {
		const u32 hoff = head->id * 0x800;
		nvkm_mask(device, 0x616308 + hoff, 0x00000111, 0x00000010);
	}

	return 0;
}

static const struct nvkm_disp_func
gf119_disp = {
	.oneinit = nv50_disp_oneinit,
	.init = gf119_disp_init,
	.fini = gf119_disp_fini,
	.intr = gf119_disp_intr,
	.intr_error = gf119_disp_intr_error,
	.super = gf119_disp_super,
	.uevent = &gf119_disp_chan_uevent,
	.head = { .cnt = gf119_head_cnt, .new = gf119_head_new },
	.dac = { .cnt = gf119_dac_cnt, .new = gf119_dac_new },
	.sor = { .cnt = gf119_sor_cnt, .new = gf119_sor_new },
	.root = { 0,0,GF110_DISP },
	.user = {
		{{0,0,GF110_DISP_CURSOR             }, nvkm_disp_chan_new, &gf119_disp_curs },
		{{0,0,GF110_DISP_OVERLAY            }, nvkm_disp_chan_new, &gf119_disp_oimm },
		{{0,0,GF110_DISP_BASE_CHANNEL_DMA   }, nvkm_disp_chan_new, &gf119_disp_base },
		{{0,0,GF110_DISP_CORE_CHANNEL_DMA   }, nvkm_disp_core_new, &gf119_disp_core },
		{{0,0,GF110_DISP_OVERLAY_CONTROL_DMA}, nvkm_disp_chan_new, &gf119_disp_ovly },
		{}
	},
};

int
gf119_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	return nvkm_disp_new_(&gf119_disp, device, type, inst, pdisp);
}
