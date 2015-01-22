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
#include "nv04.h"

#include <core/client.h>
#include <core/device.h>
#include <core/engctx.h>
#include <core/ramht.h>
#include <subdev/fb.h>
#include <subdev/instmem/nv04.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

static struct ramfc_desc
nv40_ramfc[] = {
	{ 32,  0, 0x00,  0, NV04_PFIFO_CACHE1_DMA_PUT },
	{ 32,  0, 0x04,  0, NV04_PFIFO_CACHE1_DMA_GET },
	{ 32,  0, 0x08,  0, NV10_PFIFO_CACHE1_REF_CNT },
	{ 32,  0, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_INSTANCE },
	{ 32,  0, 0x10,  0, NV04_PFIFO_CACHE1_DMA_DCOUNT },
	{ 32,  0, 0x14,  0, NV04_PFIFO_CACHE1_DMA_STATE },
	{ 28,  0, 0x18,  0, NV04_PFIFO_CACHE1_DMA_FETCH },
	{  2, 28, 0x18, 28, 0x002058 },
	{ 32,  0, 0x1c,  0, NV04_PFIFO_CACHE1_ENGINE },
	{ 32,  0, 0x20,  0, NV04_PFIFO_CACHE1_PULL1 },
	{ 32,  0, 0x24,  0, NV10_PFIFO_CACHE1_ACQUIRE_VALUE },
	{ 32,  0, 0x28,  0, NV10_PFIFO_CACHE1_ACQUIRE_TIMESTAMP },
	{ 32,  0, 0x2c,  0, NV10_PFIFO_CACHE1_ACQUIRE_TIMEOUT },
	{ 32,  0, 0x30,  0, NV10_PFIFO_CACHE1_SEMAPHORE },
	{ 32,  0, 0x34,  0, NV10_PFIFO_CACHE1_DMA_SUBROUTINE },
	{ 32,  0, 0x38,  0, NV40_PFIFO_GRCTX_INSTANCE },
	{ 17,  0, 0x3c,  0, NV04_PFIFO_DMA_TIMESLICE },
	{ 32,  0, 0x40,  0, 0x0032e4 },
	{ 32,  0, 0x44,  0, 0x0032e8 },
	{ 32,  0, 0x4c,  0, 0x002088 },
	{ 32,  0, 0x50,  0, 0x003300 },
	{ 32,  0, 0x54,  0, 0x00330c },
	{}
};

/*******************************************************************************
 * FIFO channel objects
 ******************************************************************************/

static int
nv40_fifo_object_attach(struct nvkm_object *parent,
			struct nvkm_object *object, u32 handle)
{
	struct nv04_fifo_priv *priv = (void *)parent->engine;
	struct nv04_fifo_chan *chan = (void *)parent;
	u32 context, chid = chan->base.chid;
	int ret;

	if (nv_iclass(object, NV_GPUOBJ_CLASS))
		context = nv_gpuobj(object)->addr >> 4;
	else
		context = 0x00000004; /* just non-zero */

	switch (nv_engidx(object->engine)) {
	case NVDEV_ENGINE_DMAOBJ:
	case NVDEV_ENGINE_SW:
		context |= 0x00000000;
		break;
	case NVDEV_ENGINE_GR:
		context |= 0x00100000;
		break;
	case NVDEV_ENGINE_MPEG:
		context |= 0x00200000;
		break;
	default:
		return -EINVAL;
	}

	context |= chid << 23;

	mutex_lock(&nv_subdev(priv)->mutex);
	ret = nvkm_ramht_insert(priv->ramht, chid, handle, context);
	mutex_unlock(&nv_subdev(priv)->mutex);
	return ret;
}

static int
nv40_fifo_context_attach(struct nvkm_object *parent, struct nvkm_object *engctx)
{
	struct nv04_fifo_priv *priv = (void *)parent->engine;
	struct nv04_fifo_chan *chan = (void *)parent;
	unsigned long flags;
	u32 reg, ctx;

	switch (nv_engidx(engctx->engine)) {
	case NVDEV_ENGINE_SW:
		return 0;
	case NVDEV_ENGINE_GR:
		reg = 0x32e0;
		ctx = 0x38;
		break;
	case NVDEV_ENGINE_MPEG:
		reg = 0x330c;
		ctx = 0x54;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&priv->base.lock, flags);
	nv_engctx(engctx)->addr = nv_gpuobj(engctx)->addr >> 4;
	nv_mask(priv, 0x002500, 0x00000001, 0x00000000);

	if ((nv_rd32(priv, 0x003204) & priv->base.max) == chan->base.chid)
		nv_wr32(priv, reg, nv_engctx(engctx)->addr);
	nv_wo32(priv->ramfc, chan->ramfc + ctx, nv_engctx(engctx)->addr);

	nv_mask(priv, 0x002500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&priv->base.lock, flags);
	return 0;
}

static int
nv40_fifo_context_detach(struct nvkm_object *parent, bool suspend,
			 struct nvkm_object *engctx)
{
	struct nv04_fifo_priv *priv = (void *)parent->engine;
	struct nv04_fifo_chan *chan = (void *)parent;
	unsigned long flags;
	u32 reg, ctx;

	switch (nv_engidx(engctx->engine)) {
	case NVDEV_ENGINE_SW:
		return 0;
	case NVDEV_ENGINE_GR:
		reg = 0x32e0;
		ctx = 0x38;
		break;
	case NVDEV_ENGINE_MPEG:
		reg = 0x330c;
		ctx = 0x54;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&priv->base.lock, flags);
	nv_mask(priv, 0x002500, 0x00000001, 0x00000000);

	if ((nv_rd32(priv, 0x003204) & priv->base.max) == chan->base.chid)
		nv_wr32(priv, reg, 0x00000000);
	nv_wo32(priv->ramfc, chan->ramfc + ctx, 0x00000000);

	nv_mask(priv, 0x002500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&priv->base.lock, flags);
	return 0;
}

static int
nv40_fifo_chan_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, void *data, u32 size,
		    struct nvkm_object **pobject)
{
	union {
		struct nv03_channel_dma_v0 v0;
	} *args = data;
	struct nv04_fifo_priv *priv = (void *)engine;
	struct nv04_fifo_chan *chan;
	int ret;

	nv_ioctl(parent, "create channel dma size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create channel dma vers %d pushbuf %08x "
				 "offset %016llx\n", args->v0.version,
			 args->v0.pushbuf, args->v0.offset);
	} else
		return ret;

	ret = nvkm_fifo_channel_create(parent, engine, oclass, 0, 0xc00000,
				       0x1000, args->v0.pushbuf,
				       (1ULL << NVDEV_ENGINE_DMAOBJ) |
				       (1ULL << NVDEV_ENGINE_SW) |
				       (1ULL << NVDEV_ENGINE_GR) |
				       (1ULL << NVDEV_ENGINE_MPEG), &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	args->v0.chid = chan->base.chid;

	nv_parent(chan)->context_attach = nv40_fifo_context_attach;
	nv_parent(chan)->context_detach = nv40_fifo_context_detach;
	nv_parent(chan)->object_attach = nv40_fifo_object_attach;
	nv_parent(chan)->object_detach = nv04_fifo_object_detach;
	chan->ramfc = chan->base.chid * 128;

	nv_wo32(priv->ramfc, chan->ramfc + 0x00, args->v0.offset);
	nv_wo32(priv->ramfc, chan->ramfc + 0x04, args->v0.offset);
	nv_wo32(priv->ramfc, chan->ramfc + 0x0c, chan->base.pushgpu->addr >> 4);
	nv_wo32(priv->ramfc, chan->ramfc + 0x18, 0x30000000 |
			     NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
			     NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
#ifdef __BIG_ENDIAN
			     NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
			     NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8);
	nv_wo32(priv->ramfc, chan->ramfc + 0x3c, 0x0001ffff);
	return 0;
}

static struct nvkm_ofuncs
nv40_fifo_ofuncs = {
	.ctor = nv40_fifo_chan_ctor,
	.dtor = nv04_fifo_chan_dtor,
	.init = nv04_fifo_chan_init,
	.fini = nv04_fifo_chan_fini,
	.map  = _nvkm_fifo_channel_map,
	.rd32 = _nvkm_fifo_channel_rd32,
	.wr32 = _nvkm_fifo_channel_wr32,
	.ntfy = _nvkm_fifo_channel_ntfy
};

static struct nvkm_oclass
nv40_fifo_sclass[] = {
	{ NV40_CHANNEL_DMA, &nv40_fifo_ofuncs },
	{}
};

/*******************************************************************************
 * FIFO context - basically just the instmem reserved for the channel
 ******************************************************************************/

static struct nvkm_oclass
nv40_fifo_cclass = {
	.handle = NV_ENGCTX(FIFO, 0x40),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_fifo_context_ctor,
		.dtor = _nvkm_fifo_context_dtor,
		.init = _nvkm_fifo_context_init,
		.fini = _nvkm_fifo_context_fini,
		.rd32 = _nvkm_fifo_context_rd32,
		.wr32 = _nvkm_fifo_context_wr32,
	},
};

/*******************************************************************************
 * PFIFO engine
 ******************************************************************************/

static int
nv40_fifo_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nv04_instmem_priv *imem = nv04_instmem(parent);
	struct nv04_fifo_priv *priv;
	int ret;

	ret = nvkm_fifo_create(parent, engine, oclass, 0, 31, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nvkm_ramht_ref(imem->ramht, &priv->ramht);
	nvkm_gpuobj_ref(imem->ramro, &priv->ramro);
	nvkm_gpuobj_ref(imem->ramfc, &priv->ramfc);

	nv_subdev(priv)->unit = 0x00000100;
	nv_subdev(priv)->intr = nv04_fifo_intr;
	nv_engine(priv)->cclass = &nv40_fifo_cclass;
	nv_engine(priv)->sclass = nv40_fifo_sclass;
	priv->base.pause = nv04_fifo_pause;
	priv->base.start = nv04_fifo_start;
	priv->ramfc_desc = nv40_ramfc;
	return 0;
}

static int
nv40_fifo_init(struct nvkm_object *object)
{
	struct nv04_fifo_priv *priv = (void *)object;
	struct nvkm_fb *pfb = nvkm_fb(object);
	int ret;

	ret = nvkm_fifo_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x002040, 0x000000ff);
	nv_wr32(priv, 0x002044, 0x2101ffff);
	nv_wr32(priv, 0x002058, 0x00000001);

	nv_wr32(priv, NV03_PFIFO_RAMHT, (0x03 << 24) /* search 128 */ |
				       ((priv->ramht->bits - 9) << 16) |
				        (priv->ramht->gpuobj.addr >> 8));
	nv_wr32(priv, NV03_PFIFO_RAMRO, priv->ramro->addr >> 8);

	switch (nv_device(priv)->chipset) {
	case 0x47:
	case 0x49:
	case 0x4b:
		nv_wr32(priv, 0x002230, 0x00000001);
	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x45:
	case 0x48:
		nv_wr32(priv, 0x002220, 0x00030002);
		break;
	default:
		nv_wr32(priv, 0x002230, 0x00000000);
		nv_wr32(priv, 0x002220, ((pfb->ram->size - 512 * 1024 +
					 priv->ramfc->addr) >> 16) |
					0x00030000);
		break;
	}

	nv_wr32(priv, NV03_PFIFO_CACHE1_PUSH1, priv->base.max);

	nv_wr32(priv, NV03_PFIFO_INTR_0, 0xffffffff);
	nv_wr32(priv, NV03_PFIFO_INTR_EN_0, 0xffffffff);

	nv_wr32(priv, NV03_PFIFO_CACHE1_PUSH0, 1);
	nv_wr32(priv, NV04_PFIFO_CACHE1_PULL0, 1);
	nv_wr32(priv, NV03_PFIFO_CACHES, 1);
	return 0;
}

struct nvkm_oclass *
nv40_fifo_oclass = &(struct nvkm_oclass) {
	.handle = NV_ENGINE(FIFO, 0x40),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv40_fifo_ctor,
		.dtor = nv04_fifo_dtor,
		.init = nv40_fifo_init,
		.fini = _nvkm_fifo_fini,
	},
};
