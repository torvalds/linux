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
	struct nvkm_device *device = disp->base.engine.subdev.device;
	const u32 doff = outp->or * 0x800;
	union {
		struct nv50_disp_dac_pwr_v0 v0;
	} *args = data;
	u32 stat;
	int ret;

	nvif_ioctl(object, "disp dac pwr size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(object, "disp dac pwr vers %d state %d data %d "
				   "vsync %d hsync %d\n",
			   args->v0.version, args->v0.state, args->v0.data,
			   args->v0.vsync, args->v0.hsync);
		stat  = 0x00000040 * !args->v0.state;
		stat |= 0x00000010 * !args->v0.data;
		stat |= 0x00000004 * !args->v0.vsync;
		stat |= 0x00000001 * !args->v0.hsync;
	} else
		return ret;

	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61a004 + doff) & 0x80000000))
			break;
	);
	nvkm_mask(device, 0x61a004 + doff, 0xc000007f, 0x80000000 | stat);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61a004 + doff) & 0x80000000))
			break;
	);
	return 0;
}

int
nv50_dac_sense(NV50_DISP_MTHD_V1)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	union {
		struct nv50_disp_dac_load_v0 v0;
	} *args = data;
	const u32 doff = outp->or * 0x800;
	u32 loadval;
	int ret;

	nvif_ioctl(object, "disp dac load size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(object, "disp dac load vers %d data %08x\n",
			   args->v0.version, args->v0.data);
		if (args->v0.data & 0xfff00000)
			return -EINVAL;
		loadval = args->v0.data;
	} else
		return ret;

	nvkm_mask(device, 0x61a004 + doff, 0x807f0000, 0x80150000);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61a004 + doff) & 0x80000000))
			break;
	);

	nvkm_wr32(device, 0x61a00c + doff, 0x00100000 | loadval);
	mdelay(9);
	udelay(500);
	loadval = nvkm_mask(device, 0x61a00c + doff, 0xffffffff, 0x00000000);

	nvkm_mask(device, 0x61a004 + doff, 0x807f0000, 0x80550000);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61a004 + doff) & 0x80000000))
			break;
	);

	nvkm_debug(subdev, "DAC%d sense: %08x\n", outp->or, loadval);
	if (!(loadval & 0x80000000))
		return -ETIMEDOUT;

	args->v0.load = (loadval & 0x38000000) >> 27;
	return 0;
}

static const struct nvkm_output_func
nv50_dac_output_func = {
};

int
nv50_dac_output_new(struct nvkm_disp *disp, int index,
		    struct dcb_output *dcbE, struct nvkm_output **poutp)
{
	return nvkm_output_new_(&nv50_dac_output_func, disp,
				index, dcbE, poutp);
}
