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

static void
g84_sor_hdmi_infoframe_vsi(struct nvkm_ior *ior, int head, void *data, u32 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	struct packed_hdmi_infoframe vsi;
	const u32 hoff = head * 0x800;

	nvkm_mask(device, 0x61653c + hoff, 0x00010001, 0x00010000);
	if (!size)
		return;

	pack_hdmi_infoframe(&vsi, data, size);

	nvkm_wr32(device, 0x616544 + hoff, vsi.header);
	nvkm_wr32(device, 0x616548 + hoff, vsi.subpack0_low);
	nvkm_wr32(device, 0x61654c + hoff, vsi.subpack0_high);
	/* Is there a second (or up to fourth?) set of subpack registers here? */
	/* nvkm_wr32(device, 0x616550 + hoff, vsi.subpack1_low); */
	/* nvkm_wr32(device, 0x616554 + hoff, vsi.subpack1_high); */

	nvkm_mask(device, 0x61653c + hoff, 0x00010001, 0x00010001);
}

static void
g84_sor_hdmi_infoframe_avi(struct nvkm_ior *ior, int head, void *data, u32 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	struct packed_hdmi_infoframe avi;
	const u32 hoff = head * 0x800;

	pack_hdmi_infoframe(&avi, data, size);

	nvkm_mask(device, 0x616520 + hoff, 0x00000001, 0x00000000);
	if (!size)
		return;

	nvkm_wr32(device, 0x616528 + hoff, avi.header);
	nvkm_wr32(device, 0x61652c + hoff, avi.subpack0_low);
	nvkm_wr32(device, 0x616530 + hoff, avi.subpack0_high);
	nvkm_wr32(device, 0x616534 + hoff, avi.subpack1_low);
	nvkm_wr32(device, 0x616538 + hoff, avi.subpack1_high);

	nvkm_mask(device, 0x616520 + hoff, 0x00000001, 0x00000001);
}


static void
g84_sor_hdmi_ctrl(struct nvkm_ior *ior, int head, bool enable, u8 max_ac_packet, u8 rekey)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 ctrl = 0x40000000 * enable |
			 0x1f000000 /* ??? */ |
			 max_ac_packet << 16 |
			 rekey;
	const u32 hoff = head * 0x800;

	if (!(ctrl & 0x40000000)) {
		nvkm_mask(device, 0x6165a4 + hoff, 0x40000000, 0x00000000);
		nvkm_mask(device, 0x616500 + hoff, 0x00000001, 0x00000000);
		return;
	}

	/* Audio InfoFrame */
	nvkm_mask(device, 0x616500 + hoff, 0x00000001, 0x00000000);
	nvkm_wr32(device, 0x616508 + hoff, 0x000a0184);
	nvkm_wr32(device, 0x61650c + hoff, 0x00000071);
	nvkm_wr32(device, 0x616510 + hoff, 0x00000000);
	nvkm_mask(device, 0x616500 + hoff, 0x00000001, 0x00000001);


	nvkm_mask(device, 0x6165d0 + hoff, 0x00070001, 0x00010001); /* SPARE, HW_CTS */
	nvkm_mask(device, 0x616568 + hoff, 0x00010101, 0x00000000); /* ACR_CTRL, ?? */
	nvkm_mask(device, 0x616578 + hoff, 0x80000000, 0x80000000); /* ACR_0441_ENABLE */

	/* ??? */
	nvkm_mask(device, 0x61733c, 0x00100000, 0x00100000); /* RESETF */
	nvkm_mask(device, 0x61733c, 0x10000000, 0x10000000); /* LOOKUP_EN */
	nvkm_mask(device, 0x61733c, 0x00100000, 0x00000000); /* !RESETF */

	/* HDMI_CTRL */
	nvkm_mask(device, 0x6165a4 + hoff, 0x5f1f007f, ctrl);
}

const struct nvkm_ior_func_hdmi
g84_sor_hdmi = {
	.ctrl = g84_sor_hdmi_ctrl,
	.infoframe_avi = g84_sor_hdmi_infoframe_avi,
	.infoframe_vsi = g84_sor_hdmi_infoframe_vsi,
};

static const struct nvkm_ior_func
g84_sor = {
	.state = nv50_sor_state,
	.power = nv50_sor_power,
	.clock = nv50_sor_clock,
	.hdmi = &g84_sor_hdmi,
};

int
g84_sor_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&g84_sor, disp, SOR, id, false);
}

static const struct nvkm_disp_mthd_list
g84_disp_ovly_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x6109a0 },
		{ 0x0088, 0x6109c0 },
		{ 0x008c, 0x6109c8 },
		{ 0x0090, 0x6109b4 },
		{ 0x0094, 0x610970 },
		{ 0x00a0, 0x610998 },
		{ 0x00a4, 0x610964 },
		{ 0x00c0, 0x610958 },
		{ 0x00e0, 0x6109a8 },
		{ 0x00e4, 0x6109d0 },
		{ 0x00e8, 0x6109d8 },
		{ 0x0100, 0x61094c },
		{ 0x0104, 0x610984 },
		{ 0x0108, 0x61098c },
		{ 0x0800, 0x6109f8 },
		{ 0x0808, 0x610a08 },
		{ 0x080c, 0x610a10 },
		{ 0x0810, 0x610a00 },
		{}
	}
};

static const struct nvkm_disp_chan_mthd
g84_disp_ovly_mthd = {
	.name = "Overlay",
	.addr = 0x000540,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &g84_disp_ovly_mthd_base },
		{}
	}
};

const struct nvkm_disp_chan_user
g84_disp_ovly = {
	.func = &nv50_disp_dmac_func,
	.ctrl = 3,
	.user = 3,
	.mthd = &g84_disp_ovly_mthd,
};

static const struct nvkm_disp_mthd_list
g84_disp_base_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x0008c4 },
		{ 0x0088, 0x0008d0 },
		{ 0x008c, 0x0008dc },
		{ 0x0090, 0x0008e4 },
		{ 0x0094, 0x610884 },
		{ 0x00a0, 0x6108a0 },
		{ 0x00a4, 0x610878 },
		{ 0x00c0, 0x61086c },
		{ 0x00c4, 0x610800 },
		{ 0x00c8, 0x61080c },
		{ 0x00cc, 0x610818 },
		{ 0x00e0, 0x610858 },
		{ 0x00e4, 0x610860 },
		{ 0x00e8, 0x6108ac },
		{ 0x00ec, 0x6108b4 },
		{ 0x00fc, 0x610824 },
		{ 0x0100, 0x610894 },
		{ 0x0104, 0x61082c },
		{ 0x0110, 0x6108bc },
		{ 0x0114, 0x61088c },
		{}
	}
};

static const struct nvkm_disp_chan_mthd
g84_disp_base_mthd = {
	.name = "Base",
	.addr = 0x000540,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &g84_disp_base_mthd_base },
		{  "Image", 2, &nv50_disp_base_mthd_image },
		{}
	}
};

const struct nvkm_disp_chan_user
g84_disp_base = {
	.func = &nv50_disp_dmac_func,
	.ctrl = 1,
	.user = 1,
	.mthd = &g84_disp_base_mthd,
};

const struct nvkm_disp_mthd_list
g84_disp_core_mthd_dac = {
	.mthd = 0x0080,
	.addr = 0x000008,
	.data = {
		{ 0x0400, 0x610b58 },
		{ 0x0404, 0x610bdc },
		{ 0x0420, 0x610bc4 },
		{}
	}
};

const struct nvkm_disp_mthd_list
g84_disp_core_mthd_head = {
	.mthd = 0x0400,
	.addr = 0x000540,
	.data = {
		{ 0x0800, 0x610ad8 },
		{ 0x0804, 0x610ad0 },
		{ 0x0808, 0x610a48 },
		{ 0x080c, 0x610a78 },
		{ 0x0810, 0x610ac0 },
		{ 0x0814, 0x610af8 },
		{ 0x0818, 0x610b00 },
		{ 0x081c, 0x610ae8 },
		{ 0x0820, 0x610af0 },
		{ 0x0824, 0x610b08 },
		{ 0x0828, 0x610b10 },
		{ 0x082c, 0x610a68 },
		{ 0x0830, 0x610a60 },
		{ 0x0834, 0x000000 },
		{ 0x0838, 0x610a40 },
		{ 0x0840, 0x610a24 },
		{ 0x0844, 0x610a2c },
		{ 0x0848, 0x610aa8 },
		{ 0x084c, 0x610ab0 },
		{ 0x085c, 0x610c5c },
		{ 0x0860, 0x610a84 },
		{ 0x0864, 0x610a90 },
		{ 0x0868, 0x610b18 },
		{ 0x086c, 0x610b20 },
		{ 0x0870, 0x610ac8 },
		{ 0x0874, 0x610a38 },
		{ 0x0878, 0x610c50 },
		{ 0x0880, 0x610a58 },
		{ 0x0884, 0x610a9c },
		{ 0x089c, 0x610c68 },
		{ 0x08a0, 0x610a70 },
		{ 0x08a4, 0x610a50 },
		{ 0x08a8, 0x610ae0 },
		{ 0x08c0, 0x610b28 },
		{ 0x08c4, 0x610b30 },
		{ 0x08c8, 0x610b40 },
		{ 0x08d4, 0x610b38 },
		{ 0x08d8, 0x610b48 },
		{ 0x08dc, 0x610b50 },
		{ 0x0900, 0x610a18 },
		{ 0x0904, 0x610ab8 },
		{ 0x0910, 0x610c70 },
		{ 0x0914, 0x610c78 },
		{}
	}
};

const struct nvkm_disp_chan_mthd
g84_disp_core_mthd = {
	.name = "Core",
	.addr = 0x000000,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &nv50_disp_core_mthd_base },
		{    "DAC", 3, &g84_disp_core_mthd_dac  },
		{    "SOR", 2, &nv50_disp_core_mthd_sor  },
		{   "PIOR", 3, &nv50_disp_core_mthd_pior },
		{   "HEAD", 2, &g84_disp_core_mthd_head },
		{}
	}
};

const struct nvkm_disp_chan_user
g84_disp_core = {
	.func = &nv50_disp_core_func,
	.ctrl = 0,
	.user = 0,
	.mthd = &g84_disp_core_mthd,
};

static const struct nvkm_disp_func
g84_disp = {
	.oneinit = nv50_disp_oneinit,
	.init = nv50_disp_init,
	.fini = nv50_disp_fini,
	.intr = nv50_disp_intr,
	.super = nv50_disp_super,
	.uevent = &nv50_disp_chan_uevent,
	.head = { .cnt = nv50_head_cnt, .new = nv50_head_new },
	.dac = { .cnt = nv50_dac_cnt, .new = nv50_dac_new },
	.sor = { .cnt = nv50_sor_cnt, .new = g84_sor_new },
	.pior = { .cnt = nv50_pior_cnt, .new = nv50_pior_new },
	.root = { 0,0,G82_DISP },
	.user = {
		{{0,0,G82_DISP_CURSOR             }, nvkm_disp_chan_new, &nv50_disp_curs },
		{{0,0,G82_DISP_OVERLAY            }, nvkm_disp_chan_new, &nv50_disp_oimm },
		{{0,0,G82_DISP_BASE_CHANNEL_DMA   }, nvkm_disp_chan_new, & g84_disp_base },
		{{0,0,G82_DISP_CORE_CHANNEL_DMA   }, nvkm_disp_core_new, & g84_disp_core },
		{{0,0,G82_DISP_OVERLAY_CHANNEL_DMA}, nvkm_disp_chan_new, & g84_disp_ovly },
		{}
	},
};

int
g84_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	     struct nvkm_disp **pdisp)
{
	return nvkm_disp_new_(&g84_disp, device, type, inst, pdisp);
}
