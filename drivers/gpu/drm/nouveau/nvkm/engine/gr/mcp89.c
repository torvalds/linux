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

#include <nvif/class.h>

static const struct nvkm_gr_func
mcp89_gr = {
	.init = nv50_gr_init,
	.intr = nv50_gr_intr,
	.chan_new = nv50_gr_chan_new,
	.tlb_flush = g84_gr_tlb_flush,
	.units = nv50_gr_units,
	.sclass = {
		{ -1, -1, NV_NULL_CLASS, &nv50_gr_object },
		{ -1, -1, NV50_TWOD, &nv50_gr_object },
		{ -1, -1, NV50_MEMORY_TO_MEMORY_FORMAT, &nv50_gr_object },
		{ -1, -1, NV50_COMPUTE, &nv50_gr_object },
		{ -1, -1, GT214_COMPUTE, &nv50_gr_object },
		{ -1, -1, GT21A_TESLA, &nv50_gr_object },
		{}
	}
};

int
mcp89_gr_new(struct nvkm_device *device, int index, struct nvkm_gr **pgr)
{
	return nv50_gr_new_(&mcp89_gr, device, index, pgr);
}
