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
#include "hdmi.h"

void
g84_hdmi_ctrl(struct nvkm_ior *ior, int head, bool enable, u8 max_ac_packet,
	      u8 rekey, u8 *avi, u8 avi_size, u8 *vendor, u8 vendor_size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 ctrl = 0x40000000 * enable |
			 0x1f000000 /* ??? */ |
			 max_ac_packet << 16 |
			 rekey;
	const u32 hoff = head * 0x800;
	struct packed_hdmi_infoframe avi_infoframe;
	struct packed_hdmi_infoframe vendor_infoframe;

	pack_hdmi_infoframe(&avi_infoframe, avi, avi_size);
	pack_hdmi_infoframe(&vendor_infoframe, vendor, vendor_size);

	if (!(ctrl & 0x40000000)) {
		nvkm_mask(device, 0x6165a4 + hoff, 0x40000000, 0x00000000);
		nvkm_mask(device, 0x61653c + hoff, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x616520 + hoff, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x616500 + hoff, 0x00000001, 0x00000000);
		return;
	}

	/* AVI InfoFrame */
	nvkm_mask(device, 0x616520 + hoff, 0x00000001, 0x00000000);
	if (avi_size) {
		nvkm_wr32(device, 0x616528 + hoff, avi_infoframe.header);
		nvkm_wr32(device, 0x61652c + hoff, avi_infoframe.subpack0_low);
		nvkm_wr32(device, 0x616530 + hoff, avi_infoframe.subpack0_high);
		nvkm_wr32(device, 0x616534 + hoff, avi_infoframe.subpack1_low);
		nvkm_wr32(device, 0x616538 + hoff, avi_infoframe.subpack1_high);
		nvkm_mask(device, 0x616520 + hoff, 0x00000001, 0x00000001);
	}

	/* Audio InfoFrame */
	nvkm_mask(device, 0x616500 + hoff, 0x00000001, 0x00000000);
	nvkm_wr32(device, 0x616508 + hoff, 0x000a0184);
	nvkm_wr32(device, 0x61650c + hoff, 0x00000071);
	nvkm_wr32(device, 0x616510 + hoff, 0x00000000);
	nvkm_mask(device, 0x616500 + hoff, 0x00000001, 0x00000001);

	/* Vendor InfoFrame */
	nvkm_mask(device, 0x61653c + hoff, 0x00010001, 0x00010000);
	if (vendor_size) {
		nvkm_wr32(device, 0x616544 + hoff, vendor_infoframe.header);
		nvkm_wr32(device, 0x616548 + hoff, vendor_infoframe.subpack0_low);
		nvkm_wr32(device, 0x61654c + hoff, vendor_infoframe.subpack0_high);
		/* Is there a second (or up to fourth?) set of subpack registers here? */
		/* nvkm_wr32(device, 0x616550 + hoff, vendor_infoframe->subpack1_low); */
		/* nvkm_wr32(device, 0x616554 + hoff, vendor_infoframe->subpack1_high); */
		nvkm_mask(device, 0x61653c + hoff, 0x00010001, 0x00010001);
	}

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
