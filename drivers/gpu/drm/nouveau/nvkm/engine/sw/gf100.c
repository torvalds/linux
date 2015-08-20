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
#include "nv50.h"

#include <subdev/bar.h>
#include <engine/disp.h>

#include <nvif/ioctl.h>

/*******************************************************************************
 * software object classes
 ******************************************************************************/

static struct nvkm_oclass
gf100_sw_sclass[] = {
	{ NVIF_IOCTL_NEW_V0_SW_GF100, &nvkm_nvsw_ofuncs },
	{}
};

/*******************************************************************************
 * software context
 ******************************************************************************/

static int
gf100_sw_vblsem_release(struct nvkm_notify *notify)
{
	struct nv50_sw_chan *chan =
		container_of(notify, typeof(*chan), vblank.notify[notify->index]);
	struct nvkm_sw *sw = (void *)nv_object(chan)->engine;
	struct nvkm_device *device = sw->engine.subdev.device;
	struct nvkm_bar *bar = device->bar;

	nvkm_wr32(device, 0x001718, 0x80000000 | chan->vblank.channel);
	bar->flush(bar);
	nvkm_wr32(device, 0x06000c, upper_32_bits(chan->vblank.offset));
	nvkm_wr32(device, 0x060010, lower_32_bits(chan->vblank.offset));
	nvkm_wr32(device, 0x060014, chan->vblank.value);

	return NVKM_NOTIFY_DROP;
}

static bool
gf100_sw_chan_mthd(struct nvkm_sw_chan *base, int subc, u32 mthd, u32 data)
{
	struct nv50_sw_chan *chan = nv50_sw_chan(base);
	struct nvkm_engine *engine = chan->base.base.gpuobj.object.engine;
	struct nvkm_device *device = engine->subdev.device;
	switch (mthd) {
	case 0x0400:
		chan->vblank.offset &= 0x00ffffffffULL;
		chan->vblank.offset |= (u64)data << 32;
		return true;
	case 0x0404:
		chan->vblank.offset &= 0xff00000000ULL;
		chan->vblank.offset |= data;
		return true;
	case 0x0408:
		chan->vblank.value = data;
		return true;
	case 0x040c:
		if (data < device->disp->vblank.index_nr) {
			nvkm_notify_get(&chan->vblank.notify[data]);
			return true;
		}
		break;
	case 0x600: /* MP.PM_UNK000 */
		nvkm_wr32(device, 0x419e00, data);
		return true;
	case 0x644: /* MP.TRAP_WARP_ERROR_EN */
		if (!(data & ~0x001ffffe)) {
			nvkm_wr32(device, 0x419e44, data);
			return true;
		}
		break;
	case 0x6ac: /* MP.PM_UNK0AC */
		nvkm_wr32(device, 0x419eac, data);
		return true;
	default:
		break;
	}
	return false;
}

static const struct nvkm_sw_chan_func
gf100_sw_chan_func = {
	.mthd = gf100_sw_chan_mthd,
};

static struct nv50_sw_cclass
gf100_sw_cclass = {
	.base.handle = NV_ENGCTX(SW, 0xc0),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_sw_context_ctor,
		.dtor = nv50_sw_context_dtor,
		.init = _nvkm_sw_context_init,
		.fini = _nvkm_sw_context_fini,
	},
	.vblank = gf100_sw_vblsem_release,
	.chan = &gf100_sw_chan_func,
};

/*******************************************************************************
 * software engine/subdev functions
 ******************************************************************************/

struct nvkm_oclass *
gf100_sw_oclass = &(struct nv50_sw_oclass) {
	.base.handle = NV_ENGINE(SW, 0xc0),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_sw_ctor,
		.dtor = _nvkm_sw_dtor,
		.init = _nvkm_sw_init,
		.fini = _nvkm_sw_fini,
	},
	.cclass = &gf100_sw_cclass.base,
	.sclass =  gf100_sw_sclass,
}.base;
