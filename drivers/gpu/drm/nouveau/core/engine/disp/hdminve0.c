/*
 * Copyright 2014 Red Hat Inc.
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

#include <core/client.h>
#include <nvif/unpack.h>
#include <nvif/class.h>

#include "nv50.h"

int
nve0_hdmi_ctrl(NV50_DISP_MTHD_V1)
{
	const u32 hoff = (head * 0x800);
	const u32 hdmi = (head * 0x400);
	union {
		struct nv50_disp_sor_hdmi_pwr_v0 v0;
	} *args = data;
	u32 ctrl;
	int ret;

	nv_ioctl(object, "disp sor hdmi ctrl size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "disp sor hdmi ctrl vers %d state %d "
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

	if (!(ctrl & 0x40000000)) {
		nv_mask(priv, 0x616798 + hoff, 0x40000000, 0x00000000);
		nv_mask(priv, 0x6900c0 + hdmi, 0x00000001, 0x00000000);
		nv_mask(priv, 0x690000 + hdmi, 0x00000001, 0x00000000);
		return 0;
	}

	/* AVI InfoFrame */
	nv_mask(priv, 0x690000 + hdmi, 0x00000001, 0x00000000);
	nv_wr32(priv, 0x690008 + hdmi, 0x000d0282);
	nv_wr32(priv, 0x69000c + hdmi, 0x0000006f);
	nv_wr32(priv, 0x690010 + hdmi, 0x00000000);
	nv_wr32(priv, 0x690014 + hdmi, 0x00000000);
	nv_wr32(priv, 0x690018 + hdmi, 0x00000000);
	nv_mask(priv, 0x690000 + hdmi, 0x00000001, 0x00000001);

	/* ??? InfoFrame? */
	nv_mask(priv, 0x6900c0 + hdmi, 0x00000001, 0x00000000);
	nv_wr32(priv, 0x6900cc + hdmi, 0x00000010);
	nv_mask(priv, 0x6900c0 + hdmi, 0x00000001, 0x00000001);

	/* ??? */
	nv_wr32(priv, 0x690080 + hdmi, 0x82000000);

	/* HDMI_CTRL */
	nv_mask(priv, 0x616798 + hoff, 0x401f007f, ctrl);
	return 0;
}
