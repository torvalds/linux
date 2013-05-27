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

#include <core/client.h>
#include <core/engctx.h>
#include <core/ramht.h>
#include <core/class.h>
#include <core/math.h>

#include <subdev/timer.h>
#include <subdev/bar.h>

#include <engine/dmaobj.h>
#include <engine/fifo.h>

#include "nv50.h"

/*******************************************************************************
 * FIFO channel objects
 ******************************************************************************/

void
nv50_fifo_playlist_update(struct nv50_fifo_priv *priv)
{
	struct nouveau_bar *bar = nouveau_bar(priv);
	struct nouveau_gpuobj *cur;
	int i, p;

	mutex_lock(&nv_subdev(priv)->mutex);
	cur = priv->playlist[priv->cur_playlist];
	priv->cur_playlist = !priv->cur_playlist;

	for (i = priv->base.min, p = 0; i < priv->base.max; i++) {
		if (nv_rd32(priv, 0x002600 + (i * 4)) & 0x80000000)
			nv_wo32(cur, p++ * 4, i);
	}

	bar->flush(bar);

	nv_wr32(priv, 0x0032f4, cur->addr >> 12);
	nv_wr32(priv, 0x0032ec, p);
	nv_wr32(priv, 0x002500, 0x00000101);
	mutex_unlock(&nv_subdev(priv)->mutex);
}

static int
nv50_fifo_context_attach(struct nouveau_object *parent,
			 struct nouveau_object *object)
{
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nv50_fifo_base *base = (void *)parent->parent;
	struct nouveau_gpuobj *ectx = (void *)object;
	u64 limit = ectx->addr + ectx->size - 1;
	u64 start = ectx->addr;
	u32 addr;

	switch (nv_engidx(object->engine)) {
	case NVDEV_ENGINE_SW   : return 0;
	case NVDEV_ENGINE_GR   : addr = 0x0000; break;
	case NVDEV_ENGINE_MPEG : addr = 0x0060; break;
	default:
		return -EINVAL;
	}

	nv_engctx(ectx)->addr = nv_gpuobj(base)->addr >> 12;
	nv_wo32(base->eng, addr + 0x00, 0x00190000);
	nv_wo32(base->eng, addr + 0x04, lower_32_bits(limit));
	nv_wo32(base->eng, addr + 0x08, lower_32_bits(start));
	nv_wo32(base->eng, addr + 0x0c, upper_32_bits(limit) << 24 |
					upper_32_bits(start));
	nv_wo32(base->eng, addr + 0x10, 0x00000000);
	nv_wo32(base->eng, addr + 0x14, 0x00000000);
	bar->flush(bar);
	return 0;
}

static int
nv50_fifo_context_detach(struct nouveau_object *parent, bool suspend,
			 struct nouveau_object *object)
{
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nv50_fifo_priv *priv = (void *)parent->engine;
	struct nv50_fifo_base *base = (void *)parent->parent;
	struct nv50_fifo_chan *chan = (void *)parent;
	u32 addr, me;
	int ret = 0;

	switch (nv_engidx(object->engine)) {
	case NVDEV_ENGINE_SW   : return 0;
	case NVDEV_ENGINE_GR   : addr = 0x0000; break;
	case NVDEV_ENGINE_MPEG : addr = 0x0060; break;
	default:
		return -EINVAL;
	}

	/* HW bug workaround:
	 *
	 * PFIFO will hang forever if the connected engines don't report
	 * that they've processed the context switch request.
	 *
	 * In order for the kickoff to work, we need to ensure all the
	 * connected engines are in a state where they can answer.
	 *
	 * Newer chipsets don't seem to suffer from this issue, and well,
	 * there's also a "ignore these engines" bitmask reg we can use
	 * if we hit the issue there..
	 */
	me = nv_mask(priv, 0x00b860, 0x00000001, 0x00000001);

	/* do the kickoff... */
	nv_wr32(priv, 0x0032fc, nv_gpuobj(base)->addr >> 12);
	if (!nv_wait_ne(priv, 0x0032fc, 0xffffffff, 0xffffffff)) {
		nv_error(priv, "channel %d [%s] unload timeout\n",
			 chan->base.chid, nouveau_client_name(chan));
		if (suspend)
			ret = -EBUSY;
	}
	nv_wr32(priv, 0x00b860, me);

	if (ret == 0) {
		nv_wo32(base->eng, addr + 0x00, 0x00000000);
		nv_wo32(base->eng, addr + 0x04, 0x00000000);
		nv_wo32(base->eng, addr + 0x08, 0x00000000);
		nv_wo32(base->eng, addr + 0x0c, 0x00000000);
		nv_wo32(base->eng, addr + 0x10, 0x00000000);
		nv_wo32(base->eng, addr + 0x14, 0x00000000);
		bar->flush(bar);
	}

	return ret;
}

static int
nv50_fifo_object_attach(struct nouveau_object *parent,
			struct nouveau_object *object, u32 handle)
{
	struct nv50_fifo_chan *chan = (void *)parent;
	u32 context;

	if (nv_iclass(object, NV_GPUOBJ_CLASS))
		context = nv_gpuobj(object)->node->offset >> 4;
	else
		context = 0x00000004; /* just non-zero */

	switch (nv_engidx(object->engine)) {
	case NVDEV_ENGINE_DMAOBJ:
	case NVDEV_ENGINE_SW    : context |= 0x00000000; break;
	case NVDEV_ENGINE_GR    : context |= 0x00100000; break;
	case NVDEV_ENGINE_MPEG  : context |= 0x00200000; break;
	default:
		return -EINVAL;
	}

	return nouveau_ramht_insert(chan->ramht, 0, handle, context);
}

void
nv50_fifo_object_detach(struct nouveau_object *parent, int cookie)
{
	struct nv50_fifo_chan *chan = (void *)parent;
	nouveau_ramht_remove(chan->ramht, cookie);
}

static int
nv50_fifo_chan_ctor_dma(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nv50_fifo_base *base = (void *)parent;
	struct nv50_fifo_chan *chan;
	struct nv03_channel_dma_class *args = data;
	int ret;

	if (size < sizeof(*args))
		return -EINVAL;

	ret = nouveau_fifo_channel_create(parent, engine, oclass, 0, 0xc00000,
					  0x2000, args->pushbuf,
					  (1ULL << NVDEV_ENGINE_DMAOBJ) |
					  (1ULL << NVDEV_ENGINE_SW) |
					  (1ULL << NVDEV_ENGINE_GR) |
					  (1ULL << NVDEV_ENGINE_MPEG), &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	nv_parent(chan)->context_attach = nv50_fifo_context_attach;
	nv_parent(chan)->context_detach = nv50_fifo_context_detach;
	nv_parent(chan)->object_attach = nv50_fifo_object_attach;
	nv_parent(chan)->object_detach = nv50_fifo_object_detach;

	ret = nouveau_ramht_new(nv_object(chan), nv_object(chan), 0x8000, 16,
				&chan->ramht);
	if (ret)
		return ret;

	nv_wo32(base->ramfc, 0x08, lower_32_bits(args->offset));
	nv_wo32(base->ramfc, 0x0c, upper_32_bits(args->offset));
	nv_wo32(base->ramfc, 0x10, lower_32_bits(args->offset));
	nv_wo32(base->ramfc, 0x14, upper_32_bits(args->offset));
	nv_wo32(base->ramfc, 0x3c, 0x003f6078);
	nv_wo32(base->ramfc, 0x44, 0x01003fff);
	nv_wo32(base->ramfc, 0x48, chan->base.pushgpu->node->offset >> 4);
	nv_wo32(base->ramfc, 0x4c, 0xffffffff);
	nv_wo32(base->ramfc, 0x60, 0x7fffffff);
	nv_wo32(base->ramfc, 0x78, 0x00000000);
	nv_wo32(base->ramfc, 0x7c, 0x30000001);
	nv_wo32(base->ramfc, 0x80, ((chan->ramht->bits - 9) << 27) |
				   (4 << 24) /* SEARCH_FULL */ |
				   (chan->ramht->base.node->offset >> 4));
	bar->flush(bar);
	return 0;
}

static int
nv50_fifo_chan_ctor_ind(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nv50_channel_ind_class *args = data;
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nv50_fifo_base *base = (void *)parent;
	struct nv50_fifo_chan *chan;
	u64 ioffset, ilength;
	int ret;

	if (size < sizeof(*args))
		return -EINVAL;

	ret = nouveau_fifo_channel_create(parent, engine, oclass, 0, 0xc00000,
					  0x2000, args->pushbuf,
					  (1ULL << NVDEV_ENGINE_DMAOBJ) |
					  (1ULL << NVDEV_ENGINE_SW) |
					  (1ULL << NVDEV_ENGINE_GR) |
					  (1ULL << NVDEV_ENGINE_MPEG), &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	nv_parent(chan)->context_attach = nv50_fifo_context_attach;
	nv_parent(chan)->context_detach = nv50_fifo_context_detach;
	nv_parent(chan)->object_attach = nv50_fifo_object_attach;
	nv_parent(chan)->object_detach = nv50_fifo_object_detach;

	ret = nouveau_ramht_new(nv_object(chan), nv_object(chan), 0x8000, 16,
			       &chan->ramht);
	if (ret)
		return ret;

	ioffset = args->ioffset;
	ilength = log2i(args->ilength / 8);

	nv_wo32(base->ramfc, 0x3c, 0x403f6078);
	nv_wo32(base->ramfc, 0x44, 0x01003fff);
	nv_wo32(base->ramfc, 0x48, chan->base.pushgpu->node->offset >> 4);
	nv_wo32(base->ramfc, 0x50, lower_32_bits(ioffset));
	nv_wo32(base->ramfc, 0x54, upper_32_bits(ioffset) | (ilength << 16));
	nv_wo32(base->ramfc, 0x60, 0x7fffffff);
	nv_wo32(base->ramfc, 0x78, 0x00000000);
	nv_wo32(base->ramfc, 0x7c, 0x30000001);
	nv_wo32(base->ramfc, 0x80, ((chan->ramht->bits - 9) << 27) |
				   (4 << 24) /* SEARCH_FULL */ |
				   (chan->ramht->base.node->offset >> 4));
	bar->flush(bar);
	return 0;
}

void
nv50_fifo_chan_dtor(struct nouveau_object *object)
{
	struct nv50_fifo_chan *chan = (void *)object;
	nouveau_ramht_ref(NULL, &chan->ramht);
	nouveau_fifo_channel_destroy(&chan->base);
}

static int
nv50_fifo_chan_init(struct nouveau_object *object)
{
	struct nv50_fifo_priv *priv = (void *)object->engine;
	struct nv50_fifo_base *base = (void *)object->parent;
	struct nv50_fifo_chan *chan = (void *)object;
	struct nouveau_gpuobj *ramfc = base->ramfc;
	u32 chid = chan->base.chid;
	int ret;

	ret = nouveau_fifo_channel_init(&chan->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x002600 + (chid * 4), 0x80000000 | ramfc->addr >> 12);
	nv50_fifo_playlist_update(priv);
	return 0;
}

int
nv50_fifo_chan_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_fifo_priv *priv = (void *)object->engine;
	struct nv50_fifo_chan *chan = (void *)object;
	u32 chid = chan->base.chid;

	/* remove channel from playlist, fifo will unload context */
	nv_mask(priv, 0x002600 + (chid * 4), 0x80000000, 0x00000000);
	nv50_fifo_playlist_update(priv);
	nv_wr32(priv, 0x002600 + (chid * 4), 0x00000000);

	return nouveau_fifo_channel_fini(&chan->base, suspend);
}

static struct nouveau_ofuncs
nv50_fifo_ofuncs_dma = {
	.ctor = nv50_fifo_chan_ctor_dma,
	.dtor = nv50_fifo_chan_dtor,
	.init = nv50_fifo_chan_init,
	.fini = nv50_fifo_chan_fini,
	.rd32 = _nouveau_fifo_channel_rd32,
	.wr32 = _nouveau_fifo_channel_wr32,
};

static struct nouveau_ofuncs
nv50_fifo_ofuncs_ind = {
	.ctor = nv50_fifo_chan_ctor_ind,
	.dtor = nv50_fifo_chan_dtor,
	.init = nv50_fifo_chan_init,
	.fini = nv50_fifo_chan_fini,
	.rd32 = _nouveau_fifo_channel_rd32,
	.wr32 = _nouveau_fifo_channel_wr32,
};

static struct nouveau_oclass
nv50_fifo_sclass[] = {
	{ NV50_CHANNEL_DMA_CLASS, &nv50_fifo_ofuncs_dma },
	{ NV50_CHANNEL_IND_CLASS, &nv50_fifo_ofuncs_ind },
	{}
};

/*******************************************************************************
 * FIFO context - basically just the instmem reserved for the channel
 ******************************************************************************/

static int
nv50_fifo_context_ctor(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, void *data, u32 size,
		       struct nouveau_object **pobject)
{
	struct nv50_fifo_base *base;
	int ret;

	ret = nouveau_fifo_context_create(parent, engine, oclass, NULL, 0x10000,
				          0x1000, NVOBJ_FLAG_HEAP, &base);
	*pobject = nv_object(base);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(base), nv_object(base), 0x0200,
				 0x1000, NVOBJ_FLAG_ZERO_ALLOC, &base->ramfc);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(base), nv_object(base), 0x1200, 0,
				 NVOBJ_FLAG_ZERO_ALLOC, &base->eng);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(base), nv_object(base), 0x4000, 0, 0,
				&base->pgd);
	if (ret)
		return ret;

	ret = nouveau_vm_ref(nouveau_client(parent)->vm, &base->vm, base->pgd);
	if (ret)
		return ret;

	return 0;
}

void
nv50_fifo_context_dtor(struct nouveau_object *object)
{
	struct nv50_fifo_base *base = (void *)object;
	nouveau_vm_ref(NULL, &base->vm, base->pgd);
	nouveau_gpuobj_ref(NULL, &base->pgd);
	nouveau_gpuobj_ref(NULL, &base->eng);
	nouveau_gpuobj_ref(NULL, &base->ramfc);
	nouveau_gpuobj_ref(NULL, &base->cache);
	nouveau_fifo_context_destroy(&base->base);
}

static struct nouveau_oclass
nv50_fifo_cclass = {
	.handle = NV_ENGCTX(FIFO, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_fifo_context_ctor,
		.dtor = nv50_fifo_context_dtor,
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
nv50_fifo_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv50_fifo_priv *priv;
	int ret;

	ret = nouveau_fifo_create(parent, engine, oclass, 1, 127, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 128 * 4, 0x1000, 0,
				&priv->playlist[0]);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 128 * 4, 0x1000, 0,
				&priv->playlist[1]);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000100;
	nv_subdev(priv)->intr = nv04_fifo_intr;
	nv_engine(priv)->cclass = &nv50_fifo_cclass;
	nv_engine(priv)->sclass = nv50_fifo_sclass;
	return 0;
}

void
nv50_fifo_dtor(struct nouveau_object *object)
{
	struct nv50_fifo_priv *priv = (void *)object;

	nouveau_gpuobj_ref(NULL, &priv->playlist[1]);
	nouveau_gpuobj_ref(NULL, &priv->playlist[0]);

	nouveau_fifo_destroy(&priv->base);
}

int
nv50_fifo_init(struct nouveau_object *object)
{
	struct nv50_fifo_priv *priv = (void *)object;
	int ret, i;

	ret = nouveau_fifo_init(&priv->base);
	if (ret)
		return ret;

	nv_mask(priv, 0x000200, 0x00000100, 0x00000000);
	nv_mask(priv, 0x000200, 0x00000100, 0x00000100);
	nv_wr32(priv, 0x00250c, 0x6f3cfc34);
	nv_wr32(priv, 0x002044, 0x01003fff);

	nv_wr32(priv, 0x002100, 0xffffffff);
	nv_wr32(priv, 0x002140, 0xbfffffff);

	for (i = 0; i < 128; i++)
		nv_wr32(priv, 0x002600 + (i * 4), 0x00000000);
	nv50_fifo_playlist_update(priv);

	nv_wr32(priv, 0x003200, 0x00000001);
	nv_wr32(priv, 0x003250, 0x00000001);
	nv_wr32(priv, 0x002500, 0x00000001);
	return 0;
}

struct nouveau_oclass
nv50_fifo_oclass = {
	.handle = NV_ENGINE(FIFO, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_fifo_ctor,
		.dtor = nv50_fifo_dtor,
		.init = nv50_fifo_init,
		.fini = _nouveau_fifo_fini,
	},
};
