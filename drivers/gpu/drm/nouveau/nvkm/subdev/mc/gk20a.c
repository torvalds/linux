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
#include "priv.h"

static const struct nvkm_mc_func
gk20a_mc = {
	.init = nv50_mc_init,
	.intr = gk104_mc_intr,
	.intr_unarm = gf100_mc_intr_unarm,
	.intr_rearm = gf100_mc_intr_rearm,
	.intr_mask = gf100_mc_intr_mask,
	.intr_stat = gf100_mc_intr_stat,
	.reset = gk104_mc_reset,
};

int
gk20a_mc_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_mc **pmc)
{
	return nvkm_mc_new_(&gk20a_mc, device, type, inst, pmc);
}
