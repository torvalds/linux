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
#include <nvif/user.h>
#include <nvif/device.h>

#include <nvif/class.h>

void
nvif_user_fini(struct nvif_device *device)
{
	if (device->user.func) {
		nvif_object_fini(&device->user.object);
		device->user.func = NULL;
	}
}

int
nvif_user_init(struct nvif_device *device)
{
	struct {
		s32 oclass;
		int version;
		const struct nvif_user_func *func;
	} users[] = {
		{ VOLTA_USERMODE_A, -1, &nvif_userc361 },
		{}
	};
	int cid, ret;

	if (device->user.func)
		return 0;

	cid = nvif_mclass(&device->object, users);
	if (cid < 0)
		return cid;

	ret = nvif_object_init(&device->object, 0, users[cid].oclass, NULL, 0,
			       &device->user.object);
	if (ret)
		return ret;

	nvif_object_map(&device->user.object, NULL, 0);
	device->user.func = users[cid].func;
	return 0;
}
