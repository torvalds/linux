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

#include <core/os.h>
#include <core/class.h>
#include <core/engctx.h>
#include <core/ramht.h>

#include <subdev/instmem.h>
#include <subdev/instmem/nv04.h>
#include <subdev/fb.h>

#include <engine/fifo.h>

#include "nv04.h"

static struct ramfc_desc
nv10_ramfc[] = {
	{ 32,  0, 0x00,  0, NV04_PFIFO_CACHE1_DMA_PUT },
	{ 32,  0, 0x04,  0, NV04_PFIFO_CACHE1_DMA_GET },
	{ 32,  0, 0x08,  0, NV10_PFIFO_CACHE1_REF_CNT },
	{ 16,  0, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_INSTANCE },
	{ 16, 16, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_DCOUNT },
	{ 32,  0, 0x10,  0, NV04_PFIFO_CACHE1_DMA_STATE },
	{ 32,  0, 0x14,  0, NV04_PFIFO_CACHE1_DMA_FETCH },
	{ 32,  0, 0x18,  0, NV04_PFIFO_CACHE1_ENGINE },
	{ 32,  0, 0x1c,  0, NV04_PFIFO_CACHE1_PULL1 },
	{}
};

/*******************************************************************************
 * FIFO channel objects
 ******************************************************************************/

static int
nv10_fifo_chan_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nv04_fifo_priv *priv = (void *)engine;
	struct nv04_fifo_chan *chan;
	struct nv03_channel_dma_class *args = data;
	int ret;

	if (size < sizeof(*args))
		return -EINVAL;

	ret = nouveau_fifo_channel_create(parent, engine, oclass, 0, 0x800000,
					  0x10000, args->pushbuf,
					  (1 << NVDEV_ENGINE_DMAOBJ) |
					  (1 << NVDEV_ENGINE_SW) |
					  (1 << NVDEV_ENGINE_GR), &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	nv_parent(chan)->object_attach = nv04_fifo_object_attach;
	nv_parent(chan)->object_detach = nv04_fifo_object_detach;
	nv_parent(chan)->context_attach = nv04_fifo_context_attach;
	chan->ramfc = chan->base.chid * 32;

	nv_wo32(priv->ramfc, chan->ramfc + 0x00, args->offset);
	nv_wo32(priv->ramfc, chan->ramfc + 0x04, args->offset);
	nv_wo32(priv->ramfc, chan->ramfc + 0x0c, chan->base.pushgpu->addr >> 4);
	nv_wo32(priv->ramfc, chan->ramfc + 0x14,
			     NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
			     NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
#ifdef __BIG_ENDIAN
			     NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
			     NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8);
	return 0;
}

static struct nouveau_ofuncs
nv10_fifo_ofuncs = {
	.ctor = nv10_fifo_chan_ctor,
	.dtor = nv04_fifo_chan_dtor,
	.init = nv04_fifo_chan_init,
	.fini = nv04_fifo_chan_fini,
	.rd32 = _nouveau_fifo_channel_rd32,
	.wr32 = _nouveau_fifo_channel_wr32,
};

static struct nouveau_oclass
nv10_fifo_sclass[] = {
	{ NV10_CHANNEL_DMA_CLASS, &nv10_fifo_ofuncs },
	{}
};

/*******************************************************************************
 * FIFO context - basically just the instmem reserved for the channel
 ******************************************************************************/

static struct nouveau_oclass
nv10_fifo_cclass = {
	.handle = NV_ENGCTX(FIFO, 0x10),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_fifo_context_ctor,
		.dtor = _nouveau_fifo_context_dtor,
		.init = _nouveau_fifo_context_init,
		.fini = _nouveau_fifo_context_fini,
		.rd32 = _nouveau_fifo_context_rd32,
		.wr32 = _nouveau_fifo_context_wr32,
	},
};

/*******************************************************************************
 * PFIFO engine
 ******************************************************************************/

static int
nv10_fifo_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv04_instmem_priv *imem = nv04_instmem(parent);
	struct nv04_fifo_priv *priv;
	int ret;

	ret = nouveau_fifo_create(parent, engine, oclass, 0, 31, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nouveau_ramht_ref(imem->ramht, &priv->ramht);
	nouveau_gpuobj_ref(imem->ramro, &priv->ramro);
	nouveau_gpuobj_ref(imem->ramfc, &priv->ramfc);

	nv_subdev(priv)->unit = 0x00000100;
	nv_subdev(priv)->intr = nv04_fifo_intr;
	nv_engine(priv)->cclass = &nv10_fifo_cclass;
	nv_engine(priv)->sclass = nv10_fifo_sclass;
	priv->base.pause = nv04_fifo_pause;
	priv->base.start = nv04_fifo_start;
	priv->ramfc_desc = nv10_ramfc;
	return 0;
}

struct nouveau_oclass
nv10_fifo_oclass = {
	.handle = NV_ENGINE(FIFO, 0x10),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv10_fifo_ctor,
		.dtor = nv04_fifo_dtor,
		.init = nv04_fifo_init,
		.fini = _nouveau_fifo_fini,
	},
};
