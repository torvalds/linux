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

#include <subdev/timer.h>

#include <nvif/class.h>

static void
gt215_sor_hda_eld(struct nvkm_ior *ior, int head, u8 *data, u8 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 soff = ior->id * 0x800;
	int i;

	for (i = 0; i < size; i++)
		nvkm_wr32(device, 0x61c440 + soff, (i << 8) | data[i]);
	for (; i < 0x60; i++)
		nvkm_wr32(device, 0x61c440 + soff, (i << 8));
	nvkm_mask(device, 0x61c448 + soff, 0x80000002, 0x80000002);
}

static void
gt215_sor_hda_hpd(struct nvkm_ior *ior, int head, bool present)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	u32 data = 0x80000000;
	u32 mask = 0x80000001;
	if (present)
		data |= 0x00000001;
	else
		mask |= 0x00000002;
	nvkm_mask(device, 0x61c448 + ior->id * 0x800, mask, data);
}

const struct nvkm_ior_func_hda
gt215_sor_hda = {
	.hpd = gt215_sor_hda_hpd,
	.eld = gt215_sor_hda_eld,
};

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

static const struct nvkm_ior_func_dp
gt215_sor_dp = {
	.lanes = { 2, 1, 0, 3 },
	.links = g94_sor_dp_links,
	.power = g94_sor_dp_power,
	.pattern = g94_sor_dp_pattern,
	.drive = g94_sor_dp_drive,
	.audio = gt215_sor_dp_audio,
	.audio_sym = g94_sor_dp_audio_sym,
	.activesym = g94_sor_dp_activesym,
	.watermark = g94_sor_dp_watermark,
};

static void
gt215_sor_hdmi_infoframe_vsi(struct nvkm_ior *ior, int head, void *data, u32 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	struct packed_hdmi_infoframe vsi;
	const u32 soff = nv50_ior_base(ior);

	pack_hdmi_infoframe(&vsi, data, size);

	nvkm_mask(device, 0x61c53c + soff, 0x00010001, 0x00010000);
	if (!size)
		return;

	nvkm_wr32(device, 0x61c544 + soff, vsi.header);
	nvkm_wr32(device, 0x61c548 + soff, vsi.subpack0_low);
	nvkm_wr32(device, 0x61c54c + soff, vsi.subpack0_high);
	/* Is there a second (or up to fourth?) set of subpack registers here? */
	/* nvkm_wr32(device, 0x61c550 + soff, vsi.subpack1_low); */
	/* nvkm_wr32(device, 0x61c554 + soff, vsi.subpack1_high); */

	nvkm_mask(device, 0x61c53c + soff, 0x00010001, 0x00010001);
}

static void
gt215_sor_hdmi_infoframe_avi(struct nvkm_ior *ior, int head, void *data, u32 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	struct packed_hdmi_infoframe avi;
	const u32 soff = nv50_ior_base(ior);

	pack_hdmi_infoframe(&avi, data, size);

	nvkm_mask(device, 0x61c520 + soff, 0x00000001, 0x00000000);
	if (!size)
		return;

	nvkm_wr32(device, 0x61c528 + soff, avi.header);
	nvkm_wr32(device, 0x61c52c + soff, avi.subpack0_low);
	nvkm_wr32(device, 0x61c530 + soff, avi.subpack0_high);
	nvkm_wr32(device, 0x61c534 + soff, avi.subpack1_low);
	nvkm_wr32(device, 0x61c538 + soff, avi.subpack1_high);

	nvkm_mask(device, 0x61c520 + soff, 0x00000001, 0x00000001);
}

static void
gt215_sor_hdmi_ctrl(struct nvkm_ior *ior, int head, bool enable, u8 max_ac_packet, u8 rekey)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 ctrl = 0x40000000 * enable |
			 0x1f000000 /* ??? */ |
			 max_ac_packet << 16 |
			 rekey;
	const u32 soff = nv50_ior_base(ior);

	if (!(ctrl & 0x40000000)) {
		nvkm_mask(device, 0x61c5a4 + soff, 0x40000000, 0x00000000);
		nvkm_mask(device, 0x61c53c + soff, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x61c520 + soff, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x61c500 + soff, 0x00000001, 0x00000000);
		return;
	}

	/* Audio InfoFrame */
	nvkm_mask(device, 0x61c500 + soff, 0x00000001, 0x00000000);
	nvkm_wr32(device, 0x61c508 + soff, 0x000a0184);
	nvkm_wr32(device, 0x61c50c + soff, 0x00000071);
	nvkm_wr32(device, 0x61c510 + soff, 0x00000000);
	nvkm_mask(device, 0x61c500 + soff, 0x00000001, 0x00000001);

	nvkm_mask(device, 0x61c5d0 + soff, 0x00070001, 0x00010001); /* SPARE, HW_CTS */
	nvkm_mask(device, 0x61c568 + soff, 0x00010101, 0x00000000); /* ACR_CTRL, ?? */
	nvkm_mask(device, 0x61c578 + soff, 0x80000000, 0x80000000); /* ACR_0441_ENABLE */

	/* ??? */
	nvkm_mask(device, 0x61733c, 0x00100000, 0x00100000); /* RESETF */
	nvkm_mask(device, 0x61733c, 0x10000000, 0x10000000); /* LOOKUP_EN */
	nvkm_mask(device, 0x61733c, 0x00100000, 0x00000000); /* !RESETF */

	/* HDMI_CTRL */
	nvkm_mask(device, 0x61c5a4 + soff, 0x5f1f007f, ctrl);
}

const struct nvkm_ior_func_hdmi
gt215_sor_hdmi = {
	.ctrl = gt215_sor_hdmi_ctrl,
	.infoframe_avi = gt215_sor_hdmi_infoframe_avi,
	.infoframe_vsi = gt215_sor_hdmi_infoframe_vsi,
};

static int
gt215_sor_bl_set(struct nvkm_ior *ior, int lvl)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(ior);
	u32 div, val;

	div = nvkm_rd32(device, 0x61c080 + soff);
	val = (lvl * div) / 100;
	if (div)
		nvkm_wr32(device, 0x61c084 + soff, 0xc0000000 | val);

	return 0;
}

static int
gt215_sor_bl_get(struct nvkm_ior *ior)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(ior);
	u32 div, val;

	div  = nvkm_rd32(device, 0x61c080 + soff);
	val  = nvkm_rd32(device, 0x61c084 + soff);
	val &= 0x00ffffff;
	if (div && div >= val)
		return ((val * 100) + (div / 2)) / div;

	return 100;
}

const struct nvkm_ior_func_bl
gt215_sor_bl = {
	.get = gt215_sor_bl_get,
	.set = gt215_sor_bl_set,
};

static const struct nvkm_ior_func
gt215_sor = {
	.state = g94_sor_state,
	.power = nv50_sor_power,
	.clock = nv50_sor_clock,
	.bl = &gt215_sor_bl,
	.hdmi = &gt215_sor_hdmi,
	.dp = &gt215_sor_dp,
	.hda = &gt215_sor_hda,
};

static int
gt215_sor_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&gt215_sor, disp, SOR, id, true);
}

static const struct nvkm_disp_func
gt215_disp = {
	.oneinit = nv50_disp_oneinit,
	.init = nv50_disp_init,
	.fini = nv50_disp_fini,
	.intr = nv50_disp_intr,
	.super = nv50_disp_super,
	.uevent = &nv50_disp_chan_uevent,
	.head = { .cnt = nv50_head_cnt, .new = nv50_head_new },
	.dac = { .cnt = nv50_dac_cnt, .new = nv50_dac_new },
	.sor = { .cnt = g94_sor_cnt, .new = gt215_sor_new },
	.pior = { .cnt = nv50_pior_cnt, .new = nv50_pior_new },
	.root = { 0,0,GT214_DISP },
	.user = {
		{{0,0,GT214_DISP_CURSOR             }, nvkm_disp_chan_new, & nv50_disp_curs },
		{{0,0,GT214_DISP_OVERLAY            }, nvkm_disp_chan_new, & nv50_disp_oimm },
		{{0,0,GT214_DISP_BASE_CHANNEL_DMA   }, nvkm_disp_chan_new, &  g84_disp_base },
		{{0,0,GT214_DISP_CORE_CHANNEL_DMA   }, nvkm_disp_core_new, &  g94_disp_core },
		{{0,0,GT214_DISP_OVERLAY_CHANNEL_DMA}, nvkm_disp_chan_new, &  g84_disp_ovly },
		{}
	},
};

int
gt215_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	return nvkm_disp_new_(&gt215_disp, device, type, inst, pdisp);
}
