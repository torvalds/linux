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
#include <nvif/if0004.h>
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
nv04_nvsw_mthd_get_ref(struct nvkm_nvsw *nvsw, void *data, u32 size)
{
	struct nv04_sw_chan *chan = nv04_sw_chan(nvsw->chan);
	union {
		struct nv04_nvsw_get_ref_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		args->v0.ref = atomic_read(&chan->ref);
	}

	return ret;
}

static int
nv04_nvsw_mthd(struct nvkm_nvsw *nvsw, u32 mthd, void *data, u32 size)
{
	switch (mthd) {
	case NV04_NVSW_GET_REF:
		return nv04_nvsw_mthd_get_ref(nvsw, data, size);
	default:
		break;
	}
	return -EINVAL;
}

static const struct nvkm_nvsw_func
nv04_nvsw = {
	.mthd = nv04_nvsw_mthd,
};

static int
nv04_nvsw_new(struct nvkm_sw_chan *chan, const struct nvkm_oclass *oclass,
	      void *data, u32 size, struct nvkm_object **pobject)
{
	return nvkm_nvsw_new_(&nv04_nvsw, chan, oclass, data, size, pobject);
}

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
nv04_sw_chan = {
	.mthd = nv04_sw_chan_mthd,
};

static int
nv04_sw_chan_new(struct nvkm_sw *sw, struct nvkm_chan *fifo,
		 const struct nvkm_oclass *oclass, struct nvkm_object **pobject)
{
	struct nv04_sw_chan *chan;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	atomic_set(&chan->ref, 0);
	*pobject = &chan->base.object;

	return nvkm_sw_chan_ctor(&nv04_sw_chan, sw, fifo, oclass, &chan->base);
}

/*******************************************************************************
 * software engine/subdev functions
 ******************************************************************************/

static const struct nvkm_sw_func
nv04_sw = {
	.chan_new = nv04_sw_chan_new,
	.sclass = {
		{ nv04_nvsw_new, { -1, -1, NVIF_CLASS_SW_NV04 } },
		{}
	}
};

int
nv04_sw_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_sw **psw)
{
	return nvkm_sw_new_(&nv04_sw, device, type, inst, psw);
}
