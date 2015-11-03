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
#include "outp.h"

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/i2c.h>

void
nvkm_output_fini(struct nvkm_output *outp)
{
	if (outp->func->fini)
		outp->func->fini(outp);
}

void
nvkm_output_init(struct nvkm_output *outp)
{
	if (outp->func->init)
		outp->func->init(outp);
}

void
nvkm_output_del(struct nvkm_output **poutp)
{
	struct nvkm_output *outp = *poutp;
	if (outp && !WARN_ON(!outp->func)) {
		if (outp->func->dtor)
			*poutp = outp->func->dtor(outp);
		kfree(*poutp);
		*poutp = NULL;
	}
}

void
nvkm_output_ctor(const struct nvkm_output_func *func, struct nvkm_disp *disp,
		 int index, struct dcb_output *dcbE, struct nvkm_output *outp)
{
	struct nvkm_i2c *i2c = disp->engine.subdev.device->i2c;

	outp->func = func;
	outp->disp = disp;
	outp->index = index;
	outp->info = *dcbE;
	outp->i2c = nvkm_i2c_bus_find(i2c, dcbE->i2c_index);
	outp->or = ffs(outp->info.or) - 1;

	OUTP_DBG(outp, "type %02x loc %d or %d link %d con %x "
		       "edid %x bus %d head %x",
		 outp->info.type, outp->info.location, outp->info.or,
		 outp->info.type >= 2 ? outp->info.sorconf.link : 0,
		 outp->info.connector, outp->info.i2c_index,
		 outp->info.bus, outp->info.heads);
}

int
nvkm_output_new_(const struct nvkm_output_func *func,
		 struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
		 struct nvkm_output **poutp)
{
	if (!(*poutp = kzalloc(sizeof(**poutp), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_output_ctor(func, disp, index, dcbE, *poutp);
	return 0;
}
