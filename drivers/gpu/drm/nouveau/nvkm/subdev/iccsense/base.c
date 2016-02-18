/*
 * Copyright 2015 Martin Peres
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
 * Authors: Martin Peres
 */
#include "priv.h"

struct nvkm_subdev_func iccsense_func = { 0 };

void
nvkm_iccsense_ctor(struct nvkm_device *device, int index,
		   struct nvkm_iccsense *iccsense)
{
	nvkm_subdev_ctor(&iccsense_func, device, index, 0, &iccsense->subdev);
}

int
nvkm_iccsense_new_(struct nvkm_device *device, int index,
		   struct nvkm_iccsense **iccsense)
{
	if (!(*iccsense = kzalloc(sizeof(**iccsense), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_iccsense_ctor(device, index, *iccsense);
	return 0;
}
