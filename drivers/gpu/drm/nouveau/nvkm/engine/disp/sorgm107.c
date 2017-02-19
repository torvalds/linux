/*
 * Copyright 2016 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "nv50.h"
#include "outpdp.h"

int
gm107_sor_dp_pattern(struct nvkm_output_dp *outp, int pattern)
{
	struct nvkm_device *device = outp->base.disp->engine.subdev.device;
	const u32 soff = outp->base.or * 0x800;
	const u32 data = 0x01010101 * pattern;
	if (outp->base.info.sorconf.link & 1)
		nvkm_mask(device, 0x61c110 + soff, 0x0f0f0f0f, data);
	else
		nvkm_mask(device, 0x61c12c + soff, 0x0f0f0f0f, data);
	return 0;
}

static const struct nvkm_output_dp_func
gm107_sor_dp_func = {
	.pattern = gm107_sor_dp_pattern,
	.lnk_pwr = g94_sor_dp_lnk_pwr,
	.lnk_ctl = gf119_sor_dp_lnk_ctl,
	.drv_ctl = gf119_sor_dp_drv_ctl,
	.vcpi = gf119_sor_dp_vcpi,
};

int
gm107_sor_dp_new(struct nvkm_disp *disp, int index,
		 struct dcb_output *dcbE, struct nvkm_output **poutp)
{
	return nvkm_output_dp_new_(&gm107_sor_dp_func, disp, index, dcbE, poutp);
}
