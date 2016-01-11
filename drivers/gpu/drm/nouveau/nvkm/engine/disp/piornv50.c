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
#include "outpdp.h"
#include "nv50.h"

#include <core/client.h>
#include <subdev/i2c.h>
#include <subdev/timer.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

int
nv50_pior_power(NV50_DISP_MTHD_V1)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	const u32 soff = outp->or * 0x800;
	union {
		struct nv50_disp_pior_pwr_v0 v0;
	} *args = data;
	u32 ctrl, type;
	int ret;

	nvif_ioctl(object, "disp pior pwr size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(object, "disp pior pwr vers %d state %d type %x\n",
			   args->v0.version, args->v0.state, args->v0.type);
		if (args->v0.type > 0x0f)
			return -EINVAL;
		ctrl = !!args->v0.state;
		type = args->v0.type;
	} else
		return ret;

	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61e004 + soff) & 0x80000000))
			break;
	);
	nvkm_mask(device, 0x61e004 + soff, 0x80000101, 0x80000000 | ctrl);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61e004 + soff) & 0x80000000))
			break;
	);
	disp->pior.type[outp->or] = type;
	return 0;
}

/******************************************************************************
 * TMDS
 *****************************************************************************/
static const struct nvkm_output_func
nv50_pior_output_func = {
};

int
nv50_pior_output_new(struct nvkm_disp *disp, int index,
		     struct dcb_output *dcbE, struct nvkm_output **poutp)
{
	return nvkm_output_new_(&nv50_pior_output_func, disp,
				index, dcbE, poutp);
}

/******************************************************************************
 * DisplayPort
 *****************************************************************************/
static int
nv50_pior_output_dp_pattern(struct nvkm_output_dp *outp, int pattern)
{
	return 0;
}

static int
nv50_pior_output_dp_lnk_pwr(struct nvkm_output_dp *outp, int nr)
{
	return 0;
}

static int
nv50_pior_output_dp_lnk_ctl(struct nvkm_output_dp *outp,
			    int nr, int bw, bool ef)
{
	int ret = nvkm_i2c_aux_lnk_ctl(outp->aux, nr, bw, ef);
	if (ret)
		return ret;
	return 1;
}

static const struct nvkm_output_dp_func
nv50_pior_output_dp_func = {
	.pattern = nv50_pior_output_dp_pattern,
	.lnk_pwr = nv50_pior_output_dp_lnk_pwr,
	.lnk_ctl = nv50_pior_output_dp_lnk_ctl,
};

int
nv50_pior_dp_new(struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
		 struct nvkm_output **poutp)
{
	struct nvkm_i2c *i2c = disp->engine.subdev.device->i2c;
	struct nvkm_i2c_aux *aux =
		nvkm_i2c_aux_find(i2c, NVKM_I2C_AUX_EXT(dcbE->extdev));
	struct nvkm_output_dp *outp;

	if (!(outp = kzalloc(sizeof(*outp), GFP_KERNEL)))
		return -ENOMEM;
	*poutp = &outp->base;

	return nvkm_output_dp_ctor(&nv50_pior_output_dp_func, disp,
				   index, dcbE, aux, outp);
}
