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
#include <subdev/timer.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

int
gt215_hda_eld(NV50_DISP_MTHD_V1)
{
	union {
		struct nv50_disp_sor_hda_eld_v0 v0;
	} *args = data;
	const u32 soff = outp->or * 0x800;
	int ret, i;

	nv_ioctl(object, "disp sor hda eld size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, true)) {
		nv_ioctl(object, "disp sor hda eld vers %d\n", args->v0.version);
		if (size > 0x60)
			return -E2BIG;
	} else
		return ret;

	if (size && args->v0.data[0]) {
		if (outp->info.type == DCB_OUTPUT_DP) {
			nv_mask(priv, 0x61c1e0 + soff, 0x8000000d, 0x80000001);
			nv_wait(priv, 0x61c1e0 + soff, 0x80000000, 0x00000000);
		}
		for (i = 0; i < size; i++)
			nv_wr32(priv, 0x61c440 + soff, (i << 8) | args->v0.data[0]);
		for (; i < 0x60; i++)
			nv_wr32(priv, 0x61c440 + soff, (i << 8));
		nv_mask(priv, 0x61c448 + soff, 0x80000003, 0x80000003);
	} else {
		if (outp->info.type == DCB_OUTPUT_DP) {
			nv_mask(priv, 0x61c1e0 + soff, 0x80000001, 0x80000000);
			nv_wait(priv, 0x61c1e0 + soff, 0x80000000, 0x00000000);
		}
		nv_mask(priv, 0x61c448 + soff, 0x80000003, 0x80000000 | !!size);
	}

	return 0;
}
