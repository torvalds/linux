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
#include <nvif/fifo.h>

static int
nvif_fifo_runlists(struct nvif_device *device)
{
	struct nvif_object *object = &device->object;
	struct {
		struct nv_device_info_v1 m;
		struct {
			struct nv_device_info_v1_data runlists;
			struct nv_device_info_v1_data runlist[64];
		} v;
	} *a;
	int ret, i;

	if (device->runlist)
		return 0;

	if (!(a = kmalloc(sizeof(*a), GFP_KERNEL)))
		return -ENOMEM;
	a->m.version = 1;
	a->m.count = sizeof(a->v) / sizeof(a->v.runlists);
	a->v.runlists.mthd = NV_DEVICE_FIFO_RUNLISTS;
	for (i = 0; i < ARRAY_SIZE(a->v.runlist); i++)
		a->v.runlist[i].mthd = NV_DEVICE_FIFO_RUNLIST_ENGINES(i);

	ret = nvif_object_mthd(object, NV_DEVICE_V0_INFO, a, sizeof(*a));
	if (ret)
		goto done;

	device->runlists = fls64(a->v.runlists.data);
	device->runlist = kcalloc(device->runlists, sizeof(*device->runlist),
				  GFP_KERNEL);
	if (!device->runlist) {
		ret = -ENOMEM;
		goto done;
	}

	for (i = 0; i < device->runlists; i++) {
		if (a->v.runlists.data & BIT_ULL(i))
			device->runlist[i].engines = a->v.runlist[i].data;
	}

done:
	kfree(a);
	return ret;
}

u64
nvif_fifo_runlist(struct nvif_device *device, u64 engine)
{
	struct nvif_object *object = &device->object;
	struct {
		struct nv_device_info_v1 m;
		struct {
			struct nv_device_info_v1_data engine;
		} v;
	} a = {
		.m.version = 1,
		.m.count = sizeof(a.v) / sizeof(a.v.engine),
		.v.engine.mthd = engine,
	};
	u64 runm = 0;
	int ret, i;

	if ((ret = nvif_fifo_runlists(device)))
		return runm;

	ret = nvif_object_mthd(object, NV_DEVICE_V0_INFO, &a, sizeof(a));
	if (ret == 0) {
		for (i = 0; i < device->runlists; i++) {
			if (device->runlist[i].engines & a.v.engine.data)
				runm |= BIT_ULL(i);
		}
	}

	return runm;
}
