/*
 * Copyright 2018 Red Hat Inc.
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
#include <nvif/disp.h>
#include <nvif/device.h>

#include <nvif/class.h>

void
nvif_disp_dtor(struct nvif_disp *disp)
{
	nvif_object_dtor(&disp->object);
}

int
nvif_disp_ctor(struct nvif_device *device, const char *name, s32 oclass,
	       struct nvif_disp *disp)
{
	static const struct nvif_mclass disps[] = {
		{ TU102_DISP, -1 },
		{ GV100_DISP, -1 },
		{ GP102_DISP, -1 },
		{ GP100_DISP, -1 },
		{ GM200_DISP, -1 },
		{ GM107_DISP, -1 },
		{ GK110_DISP, -1 },
		{ GK104_DISP, -1 },
		{ GF110_DISP, -1 },
		{ GT214_DISP, -1 },
		{ GT206_DISP, -1 },
		{ GT200_DISP, -1 },
		{   G82_DISP, -1 },
		{  NV50_DISP, -1 },
		{  NV04_DISP, -1 },
		{}
	};
	int cid = nvif_sclass(&device->object, disps, oclass);
	disp->object.client = NULL;
	if (cid < 0)
		return cid;

	return nvif_object_ctor(&device->object, name ? name : "nvifDisp", 0,
				disps[cid].oclass, NULL, 0, &disp->object);
}
