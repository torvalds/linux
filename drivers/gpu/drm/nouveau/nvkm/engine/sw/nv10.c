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
#include "priv.h"
#include "chan.h"
#include "nvsw.h"

#include <nvif/class.h>

/*******************************************************************************
 * software context
 ******************************************************************************/

static const struct nvkm_sw_chan_func
nv10_sw_chan = {
};

static int
nv10_sw_chan_new(struct nvkm_sw *sw, struct nvkm_fifo_chan *fifo,
		 const struct nvkm_oclass *oclass, struct nvkm_object **pobject)
{
	struct nvkm_sw_chan *chan;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &chan->object;

	return nvkm_sw_chan_ctor(&nv10_sw_chan, sw, fifo, oclass, chan);
}

/*******************************************************************************
 * software engine/subdev functions
 ******************************************************************************/

static const struct nvkm_sw_func
nv10_sw = {
	.chan_new = nv10_sw_chan_new,
	.sclass = {
		{ nvkm_nvsw_new, { -1, -1, NVIF_CLASS_SW_NV10 } },
		{}
	}
};

int
nv10_sw_new(struct nvkm_device *device, int index, struct nvkm_sw **psw)
{
	return nvkm_sw_new_(&nv10_sw, device, index, psw);
}
