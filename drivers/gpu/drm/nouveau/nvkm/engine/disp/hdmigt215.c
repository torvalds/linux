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
#include "nv50.h"
#include "outp.h"

#include <core/client.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

int
gt215_hdmi_ctrl(NV50_DISP_MTHD_V1)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	const u32 soff = outp->or * 0x800;
	union {
		struct nv50_disp_sor_hdmi_pwr_v0 v0;
	} *args = data;
	u32 ctrl;
	int ret;

	nvif_ioctl(object, "disp sor hdmi ctrl size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(object, "disp sor hdmi ctrl vers %d state %d "
				   "max_ac_packet %d rekey %d\n",
			   args->v0.version, args->v0.state,
			   args->v0.max_ac_packet, args->v0.rekey);
		if (args->v0.max_ac_packet > 0x1f || args->v0.rekey > 0x7f)
			return -EINVAL;
		ctrl  = 0x40000000 * !!args->v0.state;
		ctrl |= args->v0.max_ac_packet << 16;
		ctrl |= args->v0.rekey;
		ctrl |= 0x1f000000; /* ??? */
	} else
		return ret;

	if (!(ctrl & 0x40000000)) {
		nvkm_mask(device, 0x61c5a4 + soff, 0x40000000, 0x00000000);
		nvkm_mask(device, 0x61c520 + soff, 0x00000001, 0x00000000);
		nvkm_mask(device, 0x61c500 + soff, 0x00000001, 0x00000000);
		return 0;
	}

	/* AVI InfoFrame */
	nvkm_mask(device, 0x61c520 + soff, 0x00000001, 0x00000000);
	nvkm_wr32(device, 0x61c528 + soff, 0x000d0282);
	nvkm_wr32(device, 0x61c52c + soff, 0x0000006f);
	nvkm_wr32(device, 0x61c530 + soff, 0x00000000);
	nvkm_wr32(device, 0x61c534 + soff, 0x00000000);
	nvkm_wr32(device, 0x61c538 + soff, 0x00000000);
	nvkm_mask(device, 0x61c520 + soff, 0x00000001, 0x00000001);

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
	return 0;
}
