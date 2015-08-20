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
#define nv04_sw_chan(p) container_of((p), struct nv04_sw_chan, base)
#include "priv.h"
#include "chan.h"
#include "nvsw.h"

#include <nvif/class.h>
#include <nvif/ioctl.h>
#include <nvif/unpack.h>

struct nv04_sw_chan {
	struct nvkm_sw_chan base;
	atomic_t ref;
};

/*******************************************************************************
 * software object classes
 ******************************************************************************/

static int
nv04_sw_mthd_get_ref(struct nvkm_object *object, void *data, u32 size)
{
	struct nv04_sw_chan *chan = (void *)object->parent;
	union {
		struct nv04_nvsw_get_ref_v0 v0;
	} *args = data;
	int ret;

	if (nvif_unpack(args->v0, 0, 0, false)) {
		args->v0.ref = atomic_read(&chan->ref);
	}

	return ret;
}

static int
nv04_sw_mthd(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	switch (mthd) {
	case NV04_NVSW_GET_REF:
		return nv04_sw_mthd_get_ref(object, data, size);
	default:
		break;
	}
	return -EINVAL;
}

static struct nvkm_ofuncs
nv04_sw_ofuncs = {
	.ctor = nvkm_nvsw_ctor,
	.dtor = nvkm_object_destroy,
	.init = _nvkm_object_init,
	.fini = _nvkm_object_fini,
	.mthd = nv04_sw_mthd,
};

static struct nvkm_oclass
nv04_sw_sclass[] = {
	{ NVIF_IOCTL_NEW_V0_SW_NV04, &nv04_sw_ofuncs },
	{}
};

/*******************************************************************************
 * software context
 ******************************************************************************/

static bool
nv04_sw_chan_mthd(struct nvkm_sw_chan *base, int subc, u32 mthd, u32 data)
{
	struct nv04_sw_chan *chan = nv04_sw_chan(base);

	switch (mthd) {
	case 0x0150:
		atomic_set(&chan->ref, data);
		return true;
	default:
		break;
	}

	return false;
}

static const struct nvkm_sw_chan_func
nv04_sw_chan_func = {
	.mthd = nv04_sw_chan_mthd,
};

static int
nv04_sw_context_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, void *data, u32 size,
		     struct nvkm_object **pobject)
{
	struct nv04_sw_chan *chan;
	int ret;

	ret = nvkm_sw_context_create(&nv04_sw_chan_func,
				     parent, engine, oclass, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	atomic_set(&chan->ref, 0);
	return 0;
}

static struct nvkm_oclass
nv04_sw_cclass = {
	.handle = NV_ENGCTX(SW, 0x04),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_sw_context_ctor,
		.dtor = _nvkm_sw_context_dtor,
		.init = _nvkm_sw_context_init,
		.fini = _nvkm_sw_context_fini,
	},
};

/*******************************************************************************
 * software engine/subdev functions
 ******************************************************************************/

void
nv04_sw_intr(struct nvkm_subdev *subdev)
{
	nvkm_mask(subdev->device, 0x000100, 0x80000000, 0x00000000);
}

static int
nv04_sw_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	     struct nvkm_oclass *oclass, void *data, u32 size,
	     struct nvkm_object **pobject)
{
	struct nvkm_sw *sw;
	int ret;

	ret = nvkm_sw_create(parent, engine, oclass, &sw);
	*pobject = nv_object(sw);
	if (ret)
		return ret;

	nv_engine(sw)->cclass = &nv04_sw_cclass;
	nv_engine(sw)->sclass = nv04_sw_sclass;
	nv_subdev(sw)->intr = nv04_sw_intr;
	return 0;
}

struct nvkm_oclass *
nv04_sw_oclass = &(struct nvkm_oclass) {
	.handle = NV_ENGINE(SW, 0x04),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_sw_ctor,
		.dtor = _nvkm_sw_dtor,
		.init = _nvkm_sw_init,
		.fini = _nvkm_sw_fini,
	},
};
