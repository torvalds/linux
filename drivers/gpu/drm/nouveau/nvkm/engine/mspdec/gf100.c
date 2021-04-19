/*
 * Copyright 2012 Maarten Lankhorst
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
 * Authors: Maarten Lankhorst
 */
#include "priv.h"

#include <nvif/class.h>

void
gf100_mspdec_init(struct nvkm_falcon *mspdec)
{
	struct nvkm_device *device = mspdec->engine.subdev.device;
	nvkm_wr32(device, 0x085010, 0x0000fff2);
	nvkm_wr32(device, 0x08501c, 0x0000fff2);
}

static const struct nvkm_falcon_func
gf100_mspdec = {
	.init = gf100_mspdec_init,
	.sclass = {
		{ -1, -1, GF100_MSPDEC },
		{}
	}
};

int
gf100_mspdec_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		 struct nvkm_engine **pengine)
{
	return nvkm_mspdec_new_(&gf100_mspdec, device, type, inst, pengine);
}
