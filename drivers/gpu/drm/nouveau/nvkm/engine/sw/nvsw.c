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
#define nvkm_nvsw(p) container_of((p), struct nvkm_nvsw, object)
#include "nvsw.h"
#include "chan.h"

#include <nvif/class.h>

struct nvkm_nvsw {
	struct nvkm_object object;
	struct nvkm_sw_chan *chan;
};

static int
nvkm_nvsw_ntfy(struct nvkm_object *base, u32 mthd, struct nvkm_event **pevent)
{
	struct nvkm_nvsw *nvsw = nvkm_nvsw(base);
	switch (mthd) {
	case NVSW_NTFY_UEVENT:
		*pevent = &nvsw->chan->event;
		return 0;
	default:
		break;
	}
	return -EINVAL;
}

int
nvkm_nvsw_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_sw_chan *chan = (void *)parent;
	struct nvkm_nvsw *nvsw;
	int ret;

	ret = nvkm_object_create(parent, engine, oclass, 0, &nvsw);
	*pobject = &nvsw->object;
	if (ret)
		return ret;

	nvsw->chan = chan;
	return 0;
}

struct nvkm_ofuncs
nvkm_nvsw_ofuncs = {
	.ctor = nvkm_nvsw_ctor,
	.dtor = nvkm_object_destroy,
	.init = _nvkm_object_init,
	.fini = _nvkm_object_fini,
	.ntfy = nvkm_nvsw_ntfy,
};
