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
#include "nv04.h"

#include <core/client.h>
#include <core/engctx.h>
#include <core/ramht.h>
#include <subdev/bar.h>
#include <subdev/mmu.h>
#include <subdev/timer.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

/*******************************************************************************
 * FIFO channel objects
 ******************************************************************************/

static void
nv50_fifo_playlist_update_locked(struct nv50_fifo_priv *priv)
{
	struct nvkm_bar *bar = nvkm_bar(priv);
	struct nvkm_gpuobj *cur;
	int i, p;

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
}

void
nv50_fifo_playlist_update(struct nv50_fifo_priv *priv)
{
	mutex_lock(&nv_subdev(priv)->mutex);
	nv50_fifo_playlist_update_locked(priv);
	mutex_unlock(&nv_subdev(priv)->mutex);
}

static int
nv50_fifo_context_attach(struct nvkm_object *parent, struct nvkm_object *object)
{
	struct nvkm_bar *bar = nvkm_bar(parent);
	struct nv50_fifo_base *base = (void *)parent->parent;
	struct nvkm_gpuobj *ectx = (void *)object;
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
nv50_fifo_context_detach(struct nvkm_object *parent, bool suspend,
			 struct nvkm_object *object)
{
	struct nvkm_bar *bar = nvkm_bar(parent);
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
			 chan->base.chid, nvkm_client_name(chan));
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
nv50_fifo_object_attach(struct nvkm_object *parent,
			struct nvkm_object *object, u32 handle)
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

	return nvkm_ramht_insert(chan->ramht, 0, handle, context);
}

void
nv50_fifo_object_detach(struct nvkm_object *parent, int cookie)
{
	struct nv50_fifo_chan *chan = (void *)parent;
	nvkm_ramht_remove(chan->ramht, cookie);
}

static int
nv50_fifo_chan_ctor_dma(struct nvkm_object *parent, struct nvkm_object *engine,
			struct nvkm_oclass *oclass, void *data, u32 size,
			struct nvkm_object **pobject)
{
	union {
		struct nv03_channel_dma_v0 v0;
	} *args = data;
	struct nvkm_bar *bar = nvkm_bar(parent);
	struct nv50_fifo_base *base = (void *)parent;
	struct nv50_fifo_chan *chan;
	int ret;

	nv_ioctl(parent, "create channel dma size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create channel dma vers %d pushbuf %08x "
				 "offset %016llx\n", args->v0.version,
			 args->v0.pushbuf, args->v0.offset);
	} else
		return ret;

	ret = nvkm_fifo_channel_create(parent, engine, oclass, 0, 0xc00000,
				       0x2000, args->v0.pushbuf,
				       (1ULL << NVDEV_ENGINE_DMAOBJ) |
				       (1ULL << NVDEV_ENGINE_SW) |
				       (1ULL << NVDEV_ENGINE_GR) |
				       (1ULL << NVDEV_ENGINE_MPEG), &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	args->v0.chid = chan->base.chid;

	nv_parent(chan)->context_attach = nv50_fifo_context_attach;
	nv_parent(chan)->context_detach = nv50_fifo_context_detach;
	nv_parent(chan)->object_attach = nv50_fifo_object_attach;
	nv_parent(chan)->object_detach = nv50_fifo_object_detach;

	ret = nvkm_ramht_new(nv_object(chan), nv_object(chan), 0x8000, 16,
			     &chan->ramht);
	if (ret)
		return ret;

	nv_wo32(base->ramfc, 0x08, lower_32_bits(args->v0.offset));
	nv_wo32(base->ramfc, 0x0c, upper_32_bits(args->v0.offset));
	nv_wo32(base->ramfc, 0x10, lower_32_bits(args->v0.offset));
	nv_wo32(base->ramfc, 0x14, upper_32_bits(args->v0.offset));
	nv_wo32(base->ramfc, 0x3c, 0x003f6078);
	nv_wo32(base->ramfc, 0x44, 0x01003fff);
	nv_wo32(base->ramfc, 0x48, chan->base.pushgpu->node->offset >> 4);
	nv_wo32(base->ramfc, 0x4c, 0xffffffff);
	nv_wo32(base->ramfc, 0x60, 0x7fffffff);
	nv_wo32(base->ramfc, 0x78, 0x00000000);
	nv_wo32(base->ramfc, 0x7c, 0x30000001);
	nv_wo32(base->ramfc, 0x80, ((chan->ramht->bits - 9) << 27) |
				   (4 << 24) /* SEARCH_FULL */ |
				   (chan->ramht->gpuobj.node->offset >> 4));
	bar->flush(bar);
	return 0;
}

static int
nv50_fifo_chan_ctor_ind(struct nvkm_object *parent, struct nvkm_object *engine,
			struct nvkm_oclass *oclass, void *data, u32 size,
			struct nvkm_object **pobject)
{
	union {
		struct nv50_channel_gpfifo_v0 v0;
	} *args = data;
	struct nvkm_bar *bar = nvkm_bar(parent);
	struct nv50_fifo_base *base = (void *)parent;
	struct nv50_fifo_chan *chan;
	u64 ioffset, ilength;
	int ret;

	nv_ioctl(parent, "create channel gpfifo size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create channel gpfifo vers %d pushbuf %08x "
				 "ioffset %016llx ilength %08x\n",
			 args->v0.version, args->v0.pushbuf, args->v0.ioffset,
			 args->v0.ilength);
	} else
		return ret;

	ret = nvkm_fifo_channel_create(parent, engine, oclass, 0, 0xc00000,
				       0x2000, args->v0.pushbuf,
				       (1ULL << NVDEV_ENGINE_DMAOBJ) |
				       (1ULL << NVDEV_ENGINE_SW) |
				       (1ULL << NVDEV_ENGINE_GR) |
				       (1ULL << NVDEV_ENGINE_MPEG), &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	args->v0.chid = chan->base.chid;

	nv_parent(chan)->context_attach = nv50_fifo_context_attach;
	nv_parent(chan)->context_detach = nv50_fifo_context_detach;
	nv_parent(chan)->object_attach = nv50_fifo_object_attach;
	nv_parent(chan)->object_detach = nv50_fifo_object_detach;

	ret = nvkm_ramht_new(nv_object(chan), nv_object(chan), 0x8000, 16,
			     &chan->ramht);
	if (ret)
		return ret;

	ioffset = args->v0.ioffset;
	ilength = order_base_2(args->v0.ilength / 8);

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
				   (chan->ramht->gpuobj.node->offset >> 4));
	bar->flush(bar);
	return 0;
}

void
nv50_fifo_chan_dtor(struct nvkm_object *object)
{
	struct nv50_fifo_chan *chan = (void *)object;
	nvkm_ramht_ref(NULL, &chan->ramht);
	nvkm_fifo_channel_destroy(&chan->base);
}

static int
nv50_fifo_chan_init(struct nvkm_object *object)
{
	struct nv50_fifo_priv *priv = (void *)object->engine;
	struct nv50_fifo_base *base = (void *)object->parent;
	struct nv50_fifo_chan *chan = (void *)object;
	struct nvkm_gpuobj *ramfc = base->ramfc;
	u32 chid = chan->base.chid;
	int ret;

	ret = nvkm_fifo_channel_init(&chan->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x002600 + (chid * 4), 0x80000000 | ramfc->addr >> 12);
	nv50_fifo_playlist_update(priv);
	return 0;
}

int
nv50_fifo_chan_fini(struct nvkm_object *object, bool suspend)
{
	struct nv50_fifo_priv *priv = (void *)object->engine;
	struct nv50_fifo_chan *chan = (void *)object;
	u32 chid = chan->base.chid;

	/* remove channel from playlist, fifo will unload context */
	nv_mask(priv, 0x002600 + (chid * 4), 0x80000000, 0x00000000);
	nv50_fifo_playlist_update(priv);
	nv_wr32(priv, 0x002600 + (chid * 4), 0x00000000);

	return nvkm_fifo_channel_fini(&chan->base, suspend);
}

static struct nvkm_ofuncs
nv50_fifo_ofuncs_dma = {
	.ctor = nv50_fifo_chan_ctor_dma,
	.dtor = nv50_fifo_chan_dtor,
	.init = nv50_fifo_chan_init,
	.fini = nv50_fifo_chan_fini,
	.map  = _nvkm_fifo_channel_map,
	.rd32 = _nvkm_fifo_channel_rd32,
	.wr32 = _nvkm_fifo_channel_wr32,
	.ntfy = _nvkm_fifo_channel_ntfy
};

static struct nvkm_ofuncs
nv50_fifo_ofuncs_ind = {
	.ctor = nv50_fifo_chan_ctor_ind,
	.dtor = nv50_fifo_chan_dtor,
	.init = nv50_fifo_chan_init,
	.fini = nv50_fifo_chan_fini,
	.map  = _nvkm_fifo_channel_map,
	.rd32 = _nvkm_fifo_channel_rd32,
	.wr32 = _nvkm_fifo_channel_wr32,
	.ntfy = _nvkm_fifo_channel_ntfy
};

static struct nvkm_oclass
nv50_fifo_sclass[] = {
	{ NV50_CHANNEL_DMA, &nv50_fifo_ofuncs_dma },
	{ NV50_CHANNEL_GPFIFO, &nv50_fifo_ofuncs_ind },
	{}
};

/*******************************************************************************
 * FIFO context - basically just the instmem reserved for the channel
 ******************************************************************************/

static int
nv50_fifo_context_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		       struct nvkm_oclass *oclass, void *data, u32 size,
		       struct nvkm_object **pobject)
{
	struct nv50_fifo_base *base;
	int ret;

	ret = nvkm_fifo_context_create(parent, engine, oclass, NULL, 0x10000,
				       0x1000, NVOBJ_FLAG_HEAP, &base);
	*pobject = nv_object(base);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(base), nv_object(base), 0x0200,
			      0x1000, NVOBJ_FLAG_ZERO_ALLOC, &base->ramfc);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(base), nv_object(base), 0x1200, 0,
			      NVOBJ_FLAG_ZERO_ALLOC, &base->eng);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(base), nv_object(base), 0x4000, 0, 0,
			      &base->pgd);
	if (ret)
		return ret;

	ret = nvkm_vm_ref(nvkm_client(parent)->vm, &base->vm, base->pgd);
	if (ret)
		return ret;

	return 0;
}

void
nv50_fifo_context_dtor(struct nvkm_object *object)
{
	struct nv50_fifo_base *base = (void *)object;
	nvkm_vm_ref(NULL, &base->vm, base->pgd);
	nvkm_gpuobj_ref(NULL, &base->pgd);
	nvkm_gpuobj_ref(NULL, &base->eng);
	nvkm_gpuobj_ref(NULL, &base->ramfc);
	nvkm_gpuobj_ref(NULL, &base->cache);
	nvkm_fifo_context_destroy(&base->base);
}

static struct nvkm_oclass
nv50_fifo_cclass = {
	.handle = NV_ENGCTX(FIFO, 0x50),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_fifo_context_ctor,
		.dtor = nv50_fifo_context_dtor,
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
nv50_fifo_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nv50_fifo_priv *priv;
	int ret;

	ret = nvkm_fifo_create(parent, engine, oclass, 1, 127, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(priv), NULL, 128 * 4, 0x1000, 0,
			      &priv->playlist[0]);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(priv), NULL, 128 * 4, 0x1000, 0,
			      &priv->playlist[1]);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000100;
	nv_subdev(priv)->intr = nv04_fifo_intr;
	nv_engine(priv)->cclass = &nv50_fifo_cclass;
	nv_engine(priv)->sclass = nv50_fifo_sclass;
	priv->base.pause = nv04_fifo_pause;
	priv->base.start = nv04_fifo_start;
	return 0;
}

void
nv50_fifo_dtor(struct nvkm_object *object)
{
	struct nv50_fifo_priv *priv = (void *)object;

	nvkm_gpuobj_ref(NULL, &priv->playlist[1]);
	nvkm_gpuobj_ref(NULL, &priv->playlist[0]);

	nvkm_fifo_destroy(&priv->base);
}

int
nv50_fifo_init(struct nvkm_object *object)
{
	struct nv50_fifo_priv *priv = (void *)object;
	int ret, i;

	ret = nvkm_fifo_init(&priv->base);
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
	nv50_fifo_playlist_update_locked(priv);

	nv_wr32(priv, 0x003200, 0x00000001);
	nv_wr32(priv, 0x003250, 0x00000001);
	nv_wr32(priv, 0x002500, 0x00000001);
	return 0;
}

struct nvkm_oclass *
nv50_fifo_oclass = &(struct nvkm_oclass) {
	.handle = NV_ENGINE(FIFO, 0x50),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_fifo_ctor,
		.dtor = nv50_fifo_dtor,
		.init = nv50_fifo_init,
		.fini = _nvkm_fifo_fini,
	},
};
