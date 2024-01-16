/*
 * Copyright 2019 Red Hat Inc.
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
 */
#include "priv.h"
#include <subdev/acr.h>

MODULE_FIRMWARE("nvidia/gp108/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp108/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp108/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/gv100/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gv100/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gv100/sec2/sig.bin");

static const struct nvkm_sec2_fwif
gp108_sec2_fwif[] = {
	{ 0, gp102_sec2_load, &gp102_sec2, &gp102_sec2_acr_1 },
	{}
};

int
gp108_sec2_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_sec2 **psec2)
{
	return nvkm_sec2_new_(gp108_sec2_fwif, device, type, inst, 0, psec2);
}
