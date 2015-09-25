/*
 * Copyright 2015 Red Hat Inc.
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
#include "nvsw.h"
#include "chan.h"

#include <nvif/class.h>

static int
nvkm_nvsw_mthd_(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	struct nvkm_nvsw *nvsw = nvkm_nvsw(object);
	if (nvsw->func->mthd)
		return nvsw->func->mthd(nvsw, mthd, data, size);
	return -ENODEV;
}

static int
nvkm_nvsw_ntfy_(struct nvkm_object *object, u32 mthd,
		struct nvkm_event **pevent)
{
	struct nvkm_nvsw *nvsw = nvkm_nvsw(object);
	switch (mthd) {
	case NVSW_NTFY_UEVENT:
		*pevent = &nvsw->chan->event;
		return 0;
	default:
		break;
	}
	return -EINVAL;
}

static const struct nvkm_object_func
nvkm_nvsw_ = {
	.mthd = nvkm_nvsw_mthd_,
	.ntfy = nvkm_nvsw_ntfy_,
};

int
nvkm_nvsw_new_(const struct nvkm_nvsw_func *func, struct nvkm_sw_chan *chan,
	       const struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_nvsw *nvsw;

	if (!(nvsw = kzalloc(sizeof(*nvsw), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &nvsw->object;

	nvkm_object_ctor(&nvkm_nvsw_, oclass, &nvsw->object);
	nvsw->func = func;
	nvsw->chan = chan;
	return 0;
}

static const struct nvkm_nvsw_func
nvkm_nvsw = {
};

int
nvkm_nvsw_new(struct nvkm_sw_chan *chan, const struct nvkm_oclass *oclass,
	      void *data, u32 size, struct nvkm_object **pobject)
{
	return nvkm_nvsw_new_(&nvkm_nvsw, chan, oclass, data, size, pobject);
}
