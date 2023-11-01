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

#include <nvif/class.h>

void
gk104_sor_hdmi_infoframe_vsi(struct nvkm_ior *ior, int head, void *data, u32 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	struct packed_hdmi_infoframe vsi;
	const u32 hoff = head * 0x400;

	pack_hdmi_infoframe(&vsi, data, size);

	/* GENERIC(?) / Vendor InfoFrame? */
	nvkm_mask(device, 0x690100 + hoff, 0x00010001, 0x00000000);
	if (!size)
		return;

	nvkm_wr32(device, 0x690108 + hoff, vsi.header);
	nvkm_wr32(device, 0x69010c + hoff, vsi.subpack0_low);
	nvkm_wr32(device, 0x690110 + hoff, vsi.subpack0_high);
	/* Is there a second (or further?) set of subpack registers here? */
	nvkm_mask(device, 0x690100 + hoff, 0x00000001, 0x00000001);
}

void
gk104_sor_hdmi_infoframe_avi(struct nvkm_ior *ior, int head, void *data, u32 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	struct packed_hdmi_infoframe avi;
	const u32 hoff = head * 0x400;

	pack_hdmi_infoframe(&avi, data, size);

	/* AVI InfoFrame */
	nvkm_mask(device, 0x690000 + hoff, 0x00000001, 0x00000000);
	if (!size)
		return;

	nvkm_wr32(device, 0x690008 + hoff, avi.header);
	nvkm_wr32(device, 0x69000c + hoff, avi.subpack0_low);
	nvkm_wr32(device, 0x690010 + hoff, avi.subpack0_high);
	nvkm_wr32(device, 0x690014 + hoff, avi.subpack1_low);
	nvkm_wr32(device, 0x690018 + hoff, avi.subpack1_high);

	nvkm_mask(device, 0x690000 + hoff, 0x00000001, 0x00000001);
}

void
gk104_sor_hdmi_ctrl(struct nvkm_ior *ior, int head, bool enable, u8 max_ac_packet, u8 rekey)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 ctrl = 0x40000000 * enable |
			 max_ac_packet << 16 |
			 rekey;
	const u32 hoff = head * 0x800;
	const u32 hdmi = head * 0x400;

	if (!(ctrl & 0x40000000)) {
		nvkm_mask(device, 0x616798 + hoff, 0x40000000, 0x00000000);
		nvkm_mask(device, 0x690100 + hdmi, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x6900c0 + hdmi, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x690000 + hdmi, 0x00000001, 0x00000000);
		return;
	}

	/* ??? InfoFrame? */
	nvkm_mask(device, 0x6900c0 + hdmi, 0x00000001, 0x00000000);
	nvkm_wr32(device, 0x6900cc + hdmi, 0x00000010);
	nvkm_mask(device, 0x6900c0 + hdmi, 0x00000001, 0x00000001);

	/* ??? */
	nvkm_wr32(device, 0x690080 + hdmi, 0x82000000);

	/* HDMI_CTRL */
	nvkm_mask(device, 0x616798 + hoff, 0x401f007f, ctrl);
}

const struct nvkm_ior_func_hdmi
gk104_sor_hdmi = {
	.ctrl = gk104_sor_hdmi_ctrl,
	.infoframe_avi = gk104_sor_hdmi_infoframe_avi,
	.infoframe_vsi = gk104_sor_hdmi_infoframe_vsi,
};

static const struct nvkm_ior_func
gk104_sor = {
	.state = gf119_sor_state,
	.power = nv50_sor_power,
	.clock = gf119_sor_clock,
	.bl = &gt215_sor_bl,
	.hdmi = &gk104_sor_hdmi,
	.dp = &gf119_sor_dp,
	.hda = &gf119_sor_hda,
};

int
gk104_sor_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&gk104_sor, disp, SOR, id, true);
}

static const struct nvkm_disp_mthd_list
gk104_disp_ovly_mthd_base = {
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
		{ 0x00c4, 0x6650c4 },
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
		{ 0x0404, 0x665404 },
		{ 0x0408, 0x665408 },
		{ 0x040c, 0x66540c },
		{ 0x0410, 0x665410 },
		{}
	}
};

const struct nvkm_disp_chan_mthd
gk104_disp_ovly_mthd = {
	.name = "Overlay",
	.addr = 0x001000,
	.prev = -0x020000,
	.data = {
		{ "Global", 1, &gk104_disp_ovly_mthd_base },
		{}
	}
};

const struct nvkm_disp_chan_user
gk104_disp_ovly = {
	.func = &gf119_disp_dmac_func,
	.ctrl = 5,
	.user = 5,
	.mthd = &gk104_disp_ovly_mthd,
};

static const struct nvkm_disp_mthd_list
gk104_disp_core_mthd_head = {
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
		{ 0x047c, 0x66047c },
		{ 0x0480, 0x660480 },
		{ 0x0484, 0x660484 },
		{ 0x0488, 0x660488 },
		{ 0x048c, 0x66048c },
		{ 0x0490, 0x660490 },
		{ 0x0494, 0x660494 },
		{ 0x0498, 0x660498 },
		{ 0x04a0, 0x6604a0 },
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
		{ 0x0520, 0x660520 },
		{ 0x0524, 0x660524 },
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

const struct nvkm_disp_chan_mthd
gk104_disp_core_mthd = {
	.name = "Core",
	.addr = 0x000000,
	.prev = -0x020000,
	.data = {
		{ "Global", 1, &gf119_disp_core_mthd_base },
		{    "DAC", 3, &gf119_disp_core_mthd_dac  },
		{    "SOR", 8, &gf119_disp_core_mthd_sor  },
		{   "PIOR", 4, &gf119_disp_core_mthd_pior },
		{   "HEAD", 4, &gk104_disp_core_mthd_head },
		{}
	}
};

const struct nvkm_disp_chan_user
gk104_disp_core = {
	.func = &gf119_disp_core_func,
	.ctrl = 0,
	.user = 0,
	.mthd = &gk104_disp_core_mthd,
};

static const struct nvkm_disp_func
gk104_disp = {
	.oneinit = nv50_disp_oneinit,
	.init = gf119_disp_init,
	.fini = gf119_disp_fini,
	.intr = gf119_disp_intr,
	.intr_error = gf119_disp_intr_error,
	.super = gf119_disp_super,
	.uevent = &gf119_disp_chan_uevent,
	.head = { .cnt = gf119_head_cnt, .new = gf119_head_new },
	.dac = { .cnt = gf119_dac_cnt, .new = gf119_dac_new },
	.sor = { .cnt = gf119_sor_cnt, .new = gk104_sor_new },
	.root = { 0,0,GK104_DISP },
	.user = {
		{{0,0,GK104_DISP_CURSOR             }, nvkm_disp_chan_new, &gf119_disp_curs },
		{{0,0,GK104_DISP_OVERLAY            }, nvkm_disp_chan_new, &gf119_disp_oimm },
		{{0,0,GK104_DISP_BASE_CHANNEL_DMA   }, nvkm_disp_chan_new, &gf119_disp_base },
		{{0,0,GK104_DISP_CORE_CHANNEL_DMA   }, nvkm_disp_core_new, &gk104_disp_core },
		{{0,0,GK104_DISP_OVERLAY_CONTROL_DMA}, nvkm_disp_chan_new, &gk104_disp_ovly },
		{}
	},
};

int
gk104_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	return nvkm_disp_new_(&gk104_disp, device, type, inst, pdisp);
}
