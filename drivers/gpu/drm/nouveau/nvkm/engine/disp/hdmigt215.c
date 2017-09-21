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
gt215_hdmi_ctrl(struct nvkm_ior *ior, int head, bool enable, u8 max_ac_packet,
		u8 rekey, u8 *avi, u8 avi_size, u8 *vendor, u8 vendor_size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 ctrl = 0x40000000 * enable |
			 0x1f000000 /* ??? */ |
			 max_ac_packet << 16 |
			 rekey;
	const u32 soff = nv50_ior_base(ior);
	struct packed_hdmi_infoframe avi_infoframe;
	struct packed_hdmi_infoframe vendor_infoframe;

	pack_hdmi_infoframe(&avi_infoframe, avi, avi_size);
	pack_hdmi_infoframe(&vendor_infoframe, vendor, vendor_size);

	if (!(ctrl & 0x40000000)) {
		nvkm_mask(device, 0x61c5a4 + soff, 0x40000000, 0x00000000);
		nvkm_mask(device, 0x61c53c + soff, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x61c520 + soff, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x61c500 + soff, 0x00000001, 0x00000000);
		return;
	}

	/* AVI InfoFrame */
	nvkm_mask(device, 0x61c520 + soff, 0x00000001, 0x00000000);
	if (avi_size) {
		nvkm_wr32(device, 0x61c528 + soff, avi_infoframe.header);
		nvkm_wr32(device, 0x61c52c + soff, avi_infoframe.subpack0_low);
		nvkm_wr32(device, 0x61c530 + soff, avi_infoframe.subpack0_high);
		nvkm_wr32(device, 0x61c534 + soff, avi_infoframe.subpack1_low);
		nvkm_wr32(device, 0x61c538 + soff, avi_infoframe.subpack1_high);
		nvkm_mask(device, 0x61c520 + soff, 0x00000001, 0x00000001);
	}

	/* Audio InfoFrame */
	nvkm_mask(device, 0x61c500 + soff, 0x00000001, 0x00000000);
	nvkm_wr32(device, 0x61c508 + soff, 0x000a0184);
	nvkm_wr32(device, 0x61c50c + soff, 0x00000071);
	nvkm_wr32(device, 0x61c510 + soff, 0x00000000);
	nvkm_mask(device, 0x61c500 + soff, 0x00000001, 0x00000001);

	/* Vendor InfoFrame */
	nvkm_mask(device, 0x61c53c + soff, 0x00010001, 0x00010000);
	if (vendor_size) {
		nvkm_wr32(device, 0x61c544 + soff, vendor_infoframe.header);
		nvkm_wr32(device, 0x61c548 + soff, vendor_infoframe.subpack0_low);
		nvkm_wr32(device, 0x61c54c + soff, vendor_infoframe.subpack0_high);
		/* Is there a second (or up to fourth?) set of subpack registers here? */
		/* nvkm_wr32(device, 0x61c550 + soff, vendor_infoframe.subpack1_low); */
		/* nvkm_wr32(device, 0x61c554 + soff, vendor_infoframe.subpack1_high); */
		nvkm_mask(device, 0x61c53c + soff, 0x00010001, 0x00010001);
	}

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
