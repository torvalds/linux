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

#include <core/client.h>

#include <nvif/cl5070.h>
#include <nvif/unpack.h>

int
gf119_hdmi_ctrl(NV50_DISP_MTHD_V1)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	const u32 hoff = (head * 0x800);
	union {
		struct nv50_disp_sor_hdmi_pwr_v0 v0;
	} *args = data;
	struct packed_hdmi_infoframe avi_infoframe;
	struct packed_hdmi_infoframe vendor_infoframe;
	u32 ctrl;
	int ret = -ENOSYS;

	nvif_ioctl(object, "disp sor hdmi ctrl size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		nvif_ioctl(object, "disp sor hdmi ctrl vers %d state %d "
				   "max_ac_packet %d rekey %d\n",
			   args->v0.version, args->v0.state,
			   args->v0.max_ac_packet, args->v0.rekey);
		if (args->v0.max_ac_packet > 0x1f || args->v0.rekey > 0x7f)
			return -EINVAL;
		ctrl  = 0x40000000 * !!args->v0.state;
		ctrl |= args->v0.max_ac_packet << 16;
		ctrl |= args->v0.rekey;
	} else
		return ret;

	if ((args->v0.avi_infoframe_length
	     + args->v0.vendor_infoframe_length) > size)
		return -ENOSYS;
	else if ((args->v0.avi_infoframe_length
		    + args->v0.vendor_infoframe_length) < size)
		return -E2BIG;

	pack_hdmi_infoframe(&avi_infoframe,
			    data,
			    args->v0.avi_infoframe_length);

	pack_hdmi_infoframe(&vendor_infoframe,
			    data + args->v0.avi_infoframe_length,
			    args->v0.vendor_infoframe_length);

	if (!(ctrl & 0x40000000)) {
		nvkm_mask(device, 0x616798 + hoff, 0x40000000, 0x00000000);
		nvkm_mask(device, 0x616730 + hoff, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x6167a4 + hoff, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x616714 + hoff, 0x00000001, 0x00000000);
		return 0;
	}

	/* AVI InfoFrame */
	nvkm_mask(device, 0x616714 + hoff, 0x00000001, 0x00000000);
	if (args->v0.avi_infoframe_length) {
		nvkm_wr32(device, 0x61671c + hoff, avi_infoframe.header);
		nvkm_wr32(device, 0x616720 + hoff, avi_infoframe.subpack0_low);
		nvkm_wr32(device, 0x616724 + hoff, avi_infoframe.subpack0_high);
		nvkm_wr32(device, 0x616728 + hoff, avi_infoframe.subpack1_low);
		nvkm_wr32(device, 0x61672c + hoff, avi_infoframe.subpack1_high);
		nvkm_mask(device, 0x616714 + hoff, 0x00000001, 0x00000001);
	}

	/* GENERIC(?) / Vendor InfoFrame? */
	nvkm_mask(device, 0x616730 + hoff, 0x00010001, 0x00010000);
	if (args->v0.vendor_infoframe_length) {
		/*
		 * These appear to be the audio infoframe registers,
		 * but no other set of infoframe registers has yet
		 * been found.
		 */
		nvkm_wr32(device, 0x616738 + hoff, vendor_infoframe.header);
		nvkm_wr32(device, 0x61673c + hoff, vendor_infoframe.subpack0_low);
		nvkm_wr32(device, 0x616740 + hoff, vendor_infoframe.subpack0_high);
		/* Is there a second (or further?) set of subpack registers here? */
		nvkm_mask(device, 0x616730 + hoff, 0x00000001, 0x00000001);
	}

	/* ??? InfoFrame? */
	nvkm_mask(device, 0x6167a4 + hoff, 0x00000001, 0x00000000);
	nvkm_wr32(device, 0x6167ac + hoff, 0x00000010);
	nvkm_mask(device, 0x6167a4 + hoff, 0x00000001, 0x00000001);

	/* HDMI_CTRL */
	nvkm_mask(device, 0x616798 + hoff, 0x401f007f, ctrl);
	return 0;
}
