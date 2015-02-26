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

/*******************************************************************************
 * software object classes
 ******************************************************************************/

static int
gf100_sw_mthd_vblsem_offset(struct nvkm_object *object, u32 mthd,
			    void *args, u32 size)
{
	struct nv50_sw_chan *chan = (void *)nv_engctx(object->parent);
	u64 data = *(u32 *)args;
	if (mthd == 0x0400) {
		chan->vblank.offset &= 0x00ffffffffULL;
		chan->vblank.offset |= data << 32;
	} else {
		chan->vblank.offset &= 0xff00000000ULL;
		chan->vblank.offset |= data;
	}
	return 0;
}

static int
gf100_sw_mthd_mp_control(struct nvkm_object *object, u32 mthd,
			 void *args, u32 size)
{
	struct nv50_sw_chan *chan = (void *)nv_engctx(object->parent);
	struct nv50_sw_priv *priv = (void *)nv_object(chan)->engine;
	u32 data = *(u32 *)args;

	switch (mthd) {
	case 0x600:
		nv_wr32(priv, 0x419e00, data); /* MP.PM_UNK000 */
		break;
	case 0x644:
		if (data & ~0x1ffffe)
			return -EINVAL;
		nv_wr32(priv, 0x419e44, data); /* MP.TRAP_WARP_ERROR_EN */
		break;
	case 0x6ac:
		nv_wr32(priv, 0x419eac, data); /* MP.PM_UNK0AC */
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static struct nvkm_omthds
gf100_sw_omthds[] = {
	{ 0x0400, 0x0400, gf100_sw_mthd_vblsem_offset },
	{ 0x0404, 0x0404, gf100_sw_mthd_vblsem_offset },
	{ 0x0408, 0x0408, nv50_sw_mthd_vblsem_value },
	{ 0x040c, 0x040c, nv50_sw_mthd_vblsem_release },
	{ 0x0500, 0x0500, nv50_sw_mthd_flip },
	{ 0x0600, 0x0600, gf100_sw_mthd_mp_control },
	{ 0x0644, 0x0644, gf100_sw_mthd_mp_control },
	{ 0x06ac, 0x06ac, gf100_sw_mthd_mp_control },
	{}
};

static struct nvkm_oclass
gf100_sw_sclass[] = {
	{ 0x906e, &nvkm_object_ofuncs, gf100_sw_omthds },
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
	struct nv50_sw_priv *priv = (void *)nv_object(chan)->engine;
	struct nvkm_bar *bar = nvkm_bar(priv);

	nv_wr32(priv, 0x001718, 0x80000000 | chan->vblank.channel);
	bar->flush(bar);
	nv_wr32(priv, 0x06000c, upper_32_bits(chan->vblank.offset));
	nv_wr32(priv, 0x060010, lower_32_bits(chan->vblank.offset));
	nv_wr32(priv, 0x060014, chan->vblank.value);

	return NVKM_NOTIFY_DROP;
}

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
