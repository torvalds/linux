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
nv50_dac_power(NV50_DISP_MTHD_V1)
{
	const u32 doff = outp->or * 0x800;
	union {
		struct nv50_disp_dac_pwr_v0 v0;
	} *args = data;
	u32 stat;
	int ret;

	nv_ioctl(object, "disp dac pwr size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "disp dac pwr vers %d state %d data %d "
				 "vsync %d hsync %d\n",
			 args->v0.version, args->v0.state, args->v0.data,
			 args->v0.vsync, args->v0.hsync);
		stat  = 0x00000040 * !args->v0.state;
		stat |= 0x00000010 * !args->v0.data;
		stat |= 0x00000004 * !args->v0.vsync;
		stat |= 0x00000001 * !args->v0.hsync;
	} else
		return ret;

	nv_wait(priv, 0x61a004 + doff, 0x80000000, 0x00000000);
	nv_mask(priv, 0x61a004 + doff, 0xc000007f, 0x80000000 | stat);
	nv_wait(priv, 0x61a004 + doff, 0x80000000, 0x00000000);
	return 0;
}

int
nv50_dac_sense(NV50_DISP_MTHD_V1)
{
	union {
		struct nv50_disp_dac_load_v0 v0;
	} *args = data;
	const u32 doff = outp->or * 0x800;
	u32 loadval;
	int ret;

	nv_ioctl(object, "disp dac load size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(object, "disp dac load vers %d data %08x\n",
			 args->v0.version, args->v0.data);
		if (args->v0.data & 0xfff00000)
			return -EINVAL;
		loadval = args->v0.data;
	} else
		return ret;

	nv_mask(priv, 0x61a004 + doff, 0x807f0000, 0x80150000);
	nv_wait(priv, 0x61a004 + doff, 0x80000000, 0x00000000);

	nv_wr32(priv, 0x61a00c + doff, 0x00100000 | loadval);
	mdelay(9);
	udelay(500);
	loadval = nv_mask(priv, 0x61a00c + doff, 0xffffffff, 0x00000000);

	nv_mask(priv, 0x61a004 + doff, 0x807f0000, 0x80550000);
	nv_wait(priv, 0x61a004 + doff, 0x80000000, 0x00000000);

	nv_debug(priv, "DAC%d sense: 0x%08x\n", outp->or, loadval);
	if (!(loadval & 0x80000000))
		return -ETIMEDOUT;

	args->v0.load = (loadval & 0x38000000) >> 27;
	return 0;
}
