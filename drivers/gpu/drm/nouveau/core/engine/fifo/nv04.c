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
#include <nvif/unpack.h>
#include <nvif/class.h>
#include <core/engctx.h>
#include <core/namedb.h>
#include <core/handle.h>
#include <core/ramht.h>
#include <core/event.h>

#include <subdev/instmem.h>
#include <subdev/instmem/nv04.h>
#include <subdev/timer.h>
#include <subdev/fb.h>

#include <engine/fifo.h>

#include "nv04.h"

static struct ramfc_desc
nv04_ramfc[] = {
	{ 32,  0, 0x00,  0, NV04_PFIFO_CACHE1_DMA_PUT },
	{ 32,  0, 0x04,  0, NV04_PFIFO_CACHE1_DMA_GET },
	{ 16,  0, 0x08,  0, NV04_PFIFO_CACHE1_DMA_INSTANCE },
	{ 16, 16, 0x08,  0, NV04_PFIFO_CACHE1_DMA_DCOUNT },
	{ 32,  0, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_STATE },
	{ 32,  0, 0x10,  0, NV04_PFIFO_CACHE1_DMA_FETCH },
	{ 32,  0, 0x14,  0, NV04_PFIFO_CACHE1_ENGINE },
	{ 32,  0, 0x18,  0, NV04_PFIFO_CACHE1_PULL1 },
	{}
};

/*******************************************************************************
 * FIFO channel objects
 ******************************************************************************/

int
nv04_fifo_object_attach(struct nouveau_object *parent,
			struct nouveau_object *object, u32 handle)
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
		context |= 0x00010000;
		break;
	case NVDEV_ENGINE_MPEG:
		context |= 0x00020000;
		break;
	default:
		return -EINVAL;
	}

	context |= 0x80000000; /* valid */
	context |= chid << 24;

	mutex_lock(&nv_subdev(priv)->mutex);
	ret = nouveau_ramht_insert(priv->ramht, chid, handle, context);
	mutex_unlock(&nv_subdev(priv)->mutex);
	return ret;
}

void
nv04_fifo_object_detach(struct nouveau_object *parent, int cookie)
{
	struct nv04_fifo_priv *priv = (void *)parent->engine;
	mutex_lock(&nv_subdev(priv)->mutex);
	nouveau_ramht_remove(priv->ramht, cookie);
	mutex_unlock(&nv_subdev(priv)->mutex);
}

int
nv04_fifo_context_attach(struct nouveau_object *parent,
			 struct nouveau_object *object)
{
	nv_engctx(object)->addr = nouveau_fifo_chan(parent)->chid;
	return 0;
}

static int
nv04_fifo_chan_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
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

	ret = nouveau_fifo_channel_create(parent, engine, oclass, 0, 0x800000,
					  0x10000, args->v0.pushbuf,
					  (1ULL << NVDEV_ENGINE_DMAOBJ) |
					  (1ULL << NVDEV_ENGINE_SW) |
					  (1ULL << NVDEV_ENGINE_GR), &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	args->v0.chid = chan->base.chid;

	nv_parent(chan)->object_attach = nv04_fifo_object_attach;
	nv_parent(chan)->object_detach = nv04_fifo_object_detach;
	nv_parent(chan)->context_attach = nv04_fifo_context_attach;
	chan->ramfc = chan->base.chid * 32;

	nv_wo32(priv->ramfc, chan->ramfc + 0x00, args->v0.offset);
	nv_wo32(priv->ramfc, chan->ramfc + 0x04, args->v0.offset);
	nv_wo32(priv->ramfc, chan->ramfc + 0x08, chan->base.pushgpu->addr >> 4);
	nv_wo32(priv->ramfc, chan->ramfc + 0x10,
			     NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
			     NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
#ifdef __BIG_ENDIAN
			     NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
			     NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8);
	return 0;
}

void
nv04_fifo_chan_dtor(struct nouveau_object *object)
{
	struct nv04_fifo_priv *priv = (void *)object->engine;
	struct nv04_fifo_chan *chan = (void *)object;
	struct ramfc_desc *c = priv->ramfc_desc;

	do {
		nv_wo32(priv->ramfc, chan->ramfc + c->ctxp, 0x00000000);
	} while ((++c)->bits);

	nouveau_fifo_channel_destroy(&chan->base);
}

int
nv04_fifo_chan_init(struct nouveau_object *object)
{
	struct nv04_fifo_priv *priv = (void *)object->engine;
	struct nv04_fifo_chan *chan = (void *)object;
	u32 mask = 1 << chan->base.chid;
	unsigned long flags;
	int ret;

	ret = nouveau_fifo_channel_init(&chan->base);
	if (ret)
		return ret;

	spin_lock_irqsave(&priv->base.lock, flags);
	nv_mask(priv, NV04_PFIFO_MODE, mask, mask);
	spin_unlock_irqrestore(&priv->base.lock, flags);
	return 0;
}

int
nv04_fifo_chan_fini(struct nouveau_object *object, bool suspend)
{
	struct nv04_fifo_priv *priv = (void *)object->engine;
	struct nv04_fifo_chan *chan = (void *)object;
	struct nouveau_gpuobj *fctx = priv->ramfc;
	struct ramfc_desc *c;
	unsigned long flags;
	u32 data = chan->ramfc;
	u32 chid;

	/* prevent fifo context switches */
	spin_lock_irqsave(&priv->base.lock, flags);
	nv_wr32(priv, NV03_PFIFO_CACHES, 0);

	/* if this channel is active, replace it with a null context */
	chid = nv_rd32(priv, NV03_PFIFO_CACHE1_PUSH1) & priv->base.max;
	if (chid == chan->base.chid) {
		nv_mask(priv, NV04_PFIFO_CACHE1_DMA_PUSH, 0x00000001, 0);
		nv_wr32(priv, NV03_PFIFO_CACHE1_PUSH0, 0);
		nv_mask(priv, NV04_PFIFO_CACHE1_PULL0, 0x00000001, 0);

		c = priv->ramfc_desc;
		do {
			u32 rm = ((1ULL << c->bits) - 1) << c->regs;
			u32 cm = ((1ULL << c->bits) - 1) << c->ctxs;
			u32 rv = (nv_rd32(priv, c->regp) &  rm) >> c->regs;
			u32 cv = (nv_ro32(fctx, c->ctxp + data) & ~cm);
			nv_wo32(fctx, c->ctxp + data, cv | (rv << c->ctxs));
		} while ((++c)->bits);

		c = priv->ramfc_desc;
		do {
			nv_wr32(priv, c->regp, 0x00000000);
		} while ((++c)->bits);

		nv_wr32(priv, NV03_PFIFO_CACHE1_GET, 0);
		nv_wr32(priv, NV03_PFIFO_CACHE1_PUT, 0);
		nv_wr32(priv, NV03_PFIFO_CACHE1_PUSH1, priv->base.max);
		nv_wr32(priv, NV03_PFIFO_CACHE1_PUSH0, 1);
		nv_wr32(priv, NV04_PFIFO_CACHE1_PULL0, 1);
	}

	/* restore normal operation, after disabling dma mode */
	nv_mask(priv, NV04_PFIFO_MODE, 1 << chan->base.chid, 0);
	nv_wr32(priv, NV03_PFIFO_CACHES, 1);
	spin_unlock_irqrestore(&priv->base.lock, flags);

	return nouveau_fifo_channel_fini(&chan->base, suspend);
}

static struct nouveau_ofuncs
nv04_fifo_ofuncs = {
	.ctor = nv04_fifo_chan_ctor,
	.dtor = nv04_fifo_chan_dtor,
	.init = nv04_fifo_chan_init,
	.fini = nv04_fifo_chan_fini,
	.map  = _nouveau_fifo_channel_map,
	.rd32 = _nouveau_fifo_channel_rd32,
	.wr32 = _nouveau_fifo_channel_wr32,
	.ntfy = _nouveau_fifo_channel_ntfy
};

static struct nouveau_oclass
nv04_fifo_sclass[] = {
	{ NV03_CHANNEL_DMA, &nv04_fifo_ofuncs },
	{}
};

/*******************************************************************************
 * FIFO context - basically just the instmem reserved for the channel
 ******************************************************************************/

int
nv04_fifo_context_ctor(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, void *data, u32 size,
		       struct nouveau_object **pobject)
{
	struct nv04_fifo_base *base;
	int ret;

	ret = nouveau_fifo_context_create(parent, engine, oclass, NULL, 0x1000,
				          0x1000, NVOBJ_FLAG_HEAP, &base);
	*pobject = nv_object(base);
	if (ret)
		return ret;

	return 0;
}

static struct nouveau_oclass
nv04_fifo_cclass = {
	.handle = NV_ENGCTX(FIFO, 0x04),
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

void
nv04_fifo_pause(struct nouveau_fifo *pfifo, unsigned long *pflags)
__acquires(priv->base.lock)
{
	struct nv04_fifo_priv *priv = (void *)pfifo;
	unsigned long flags;

	spin_lock_irqsave(&priv->base.lock, flags);
	*pflags = flags;

	nv_wr32(priv, NV03_PFIFO_CACHES, 0x00000000);
	nv_mask(priv, NV04_PFIFO_CACHE1_PULL0, 0x00000001, 0x00000000);

	/* in some cases the puller may be left in an inconsistent state
	 * if you try to stop it while it's busy translating handles.
	 * sometimes you get a CACHE_ERROR, sometimes it just fails
	 * silently; sending incorrect instance offsets to PGRAPH after
	 * it's started up again.
	 *
	 * to avoid this, we invalidate the most recently calculated
	 * instance.
	 */
	if (!nv_wait(priv, NV04_PFIFO_CACHE1_PULL0,
			   NV04_PFIFO_CACHE1_PULL0_HASH_BUSY, 0x00000000))
		nv_warn(priv, "timeout idling puller\n");

	if (nv_rd32(priv, NV04_PFIFO_CACHE1_PULL0) &
			  NV04_PFIFO_CACHE1_PULL0_HASH_FAILED)
		nv_wr32(priv, NV03_PFIFO_INTR_0, NV_PFIFO_INTR_CACHE_ERROR);

	nv_wr32(priv, NV04_PFIFO_CACHE1_HASH, 0x00000000);
}

void
nv04_fifo_start(struct nouveau_fifo *pfifo, unsigned long *pflags)
__releases(priv->base.lock)
{
	struct nv04_fifo_priv *priv = (void *)pfifo;
	unsigned long flags = *pflags;

	nv_mask(priv, NV04_PFIFO_CACHE1_PULL0, 0x00000001, 0x00000001);
	nv_wr32(priv, NV03_PFIFO_CACHES, 0x00000001);

	spin_unlock_irqrestore(&priv->base.lock, flags);
}

static const char *
nv_dma_state_err(u32 state)
{
	static const char * const desc[] = {
		"NONE", "CALL_SUBR_ACTIVE", "INVALID_MTHD", "RET_SUBR_INACTIVE",
		"INVALID_CMD", "IB_EMPTY"/* NV50+ */, "MEM_FAULT", "UNK"
	};
	return desc[(state >> 29) & 0x7];
}

static bool
nv04_fifo_swmthd(struct nv04_fifo_priv *priv, u32 chid, u32 addr, u32 data)
{
	struct nv04_fifo_chan *chan = NULL;
	struct nouveau_handle *bind;
	const int subc = (addr >> 13) & 0x7;
	const int mthd = addr & 0x1ffc;
	bool handled = false;
	unsigned long flags;
	u32 engine;

	spin_lock_irqsave(&priv->base.lock, flags);
	if (likely(chid >= priv->base.min && chid <= priv->base.max))
		chan = (void *)priv->base.channel[chid];
	if (unlikely(!chan))
		goto out;

	switch (mthd) {
	case 0x0000:
		bind = nouveau_namedb_get(nv_namedb(chan), data);
		if (unlikely(!bind))
			break;

		if (nv_engidx(bind->object->engine) == NVDEV_ENGINE_SW) {
			engine = 0x0000000f << (subc * 4);
			chan->subc[subc] = data;
			handled = true;

			nv_mask(priv, NV04_PFIFO_CACHE1_ENGINE, engine, 0);
		}

		nouveau_namedb_put(bind);
		break;
	default:
		engine = nv_rd32(priv, NV04_PFIFO_CACHE1_ENGINE);
		if (unlikely(((engine >> (subc * 4)) & 0xf) != 0))
			break;

		bind = nouveau_namedb_get(nv_namedb(chan), chan->subc[subc]);
		if (likely(bind)) {
			if (!nv_call(bind->object, mthd, data))
				handled = true;
			nouveau_namedb_put(bind);
		}
		break;
	}

out:
	spin_unlock_irqrestore(&priv->base.lock, flags);
	return handled;
}

static void
nv04_fifo_cache_error(struct nouveau_device *device,
		struct nv04_fifo_priv *priv, u32 chid, u32 get)
{
	u32 mthd, data;
	int ptr;

	/* NV_PFIFO_CACHE1_GET actually goes to 0xffc before wrapping on my
	 * G80 chips, but CACHE1 isn't big enough for this much data.. Tests
	 * show that it wraps around to the start at GET=0x800.. No clue as to
	 * why..
	 */
	ptr = (get & 0x7ff) >> 2;

	if (device->card_type < NV_40) {
		mthd = nv_rd32(priv, NV04_PFIFO_CACHE1_METHOD(ptr));
		data = nv_rd32(priv, NV04_PFIFO_CACHE1_DATA(ptr));
	} else {
		mthd = nv_rd32(priv, NV40_PFIFO_CACHE1_METHOD(ptr));
		data = nv_rd32(priv, NV40_PFIFO_CACHE1_DATA(ptr));
	}

	if (!nv04_fifo_swmthd(priv, chid, mthd, data)) {
		const char *client_name =
			nouveau_client_name_for_fifo_chid(&priv->base, chid);
		nv_error(priv,
			 "CACHE_ERROR - ch %d [%s] subc %d mthd 0x%04x data 0x%08x\n",
			 chid, client_name, (mthd >> 13) & 7, mthd & 0x1ffc,
			 data);
	}

	nv_wr32(priv, NV04_PFIFO_CACHE1_DMA_PUSH, 0);
	nv_wr32(priv, NV03_PFIFO_INTR_0, NV_PFIFO_INTR_CACHE_ERROR);

	nv_wr32(priv, NV03_PFIFO_CACHE1_PUSH0,
		nv_rd32(priv, NV03_PFIFO_CACHE1_PUSH0) & ~1);
	nv_wr32(priv, NV03_PFIFO_CACHE1_GET, get + 4);
	nv_wr32(priv, NV03_PFIFO_CACHE1_PUSH0,
		nv_rd32(priv, NV03_PFIFO_CACHE1_PUSH0) | 1);
	nv_wr32(priv, NV04_PFIFO_CACHE1_HASH, 0);

	nv_wr32(priv, NV04_PFIFO_CACHE1_DMA_PUSH,
		nv_rd32(priv, NV04_PFIFO_CACHE1_DMA_PUSH) | 1);
	nv_wr32(priv, NV04_PFIFO_CACHE1_PULL0, 1);
}

static void
nv04_fifo_dma_pusher(struct nouveau_device *device, struct nv04_fifo_priv *priv,
		u32 chid)
{
	const char *client_name;
	u32 dma_get = nv_rd32(priv, 0x003244);
	u32 dma_put = nv_rd32(priv, 0x003240);
	u32 push = nv_rd32(priv, 0x003220);
	u32 state = nv_rd32(priv, 0x003228);

	client_name = nouveau_client_name_for_fifo_chid(&priv->base, chid);

	if (device->card_type == NV_50) {
		u32 ho_get = nv_rd32(priv, 0x003328);
		u32 ho_put = nv_rd32(priv, 0x003320);
		u32 ib_get = nv_rd32(priv, 0x003334);
		u32 ib_put = nv_rd32(priv, 0x003330);

		nv_error(priv,
			 "DMA_PUSHER - ch %d [%s] get 0x%02x%08x put 0x%02x%08x ib_get 0x%08x ib_put 0x%08x state 0x%08x (err: %s) push 0x%08x\n",
			 chid, client_name, ho_get, dma_get, ho_put, dma_put,
			 ib_get, ib_put, state, nv_dma_state_err(state), push);

		/* METHOD_COUNT, in DMA_STATE on earlier chipsets */
		nv_wr32(priv, 0x003364, 0x00000000);
		if (dma_get != dma_put || ho_get != ho_put) {
			nv_wr32(priv, 0x003244, dma_put);
			nv_wr32(priv, 0x003328, ho_put);
		} else
		if (ib_get != ib_put)
			nv_wr32(priv, 0x003334, ib_put);
	} else {
		nv_error(priv,
			 "DMA_PUSHER - ch %d [%s] get 0x%08x put 0x%08x state 0x%08x (err: %s) push 0x%08x\n",
			 chid, client_name, dma_get, dma_put, state,
			 nv_dma_state_err(state), push);

		if (dma_get != dma_put)
			nv_wr32(priv, 0x003244, dma_put);
	}

	nv_wr32(priv, 0x003228, 0x00000000);
	nv_wr32(priv, 0x003220, 0x00000001);
	nv_wr32(priv, 0x002100, NV_PFIFO_INTR_DMA_PUSHER);
}

void
nv04_fifo_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_device *device = nv_device(subdev);
	struct nv04_fifo_priv *priv = (void *)subdev;
	uint32_t status, reassign;
	int cnt = 0;

	reassign = nv_rd32(priv, NV03_PFIFO_CACHES) & 1;
	while ((status = nv_rd32(priv, NV03_PFIFO_INTR_0)) && (cnt++ < 100)) {
		uint32_t chid, get;

		nv_wr32(priv, NV03_PFIFO_CACHES, 0);

		chid = nv_rd32(priv, NV03_PFIFO_CACHE1_PUSH1) & priv->base.max;
		get  = nv_rd32(priv, NV03_PFIFO_CACHE1_GET);

		if (status & NV_PFIFO_INTR_CACHE_ERROR) {
			nv04_fifo_cache_error(device, priv, chid, get);
			status &= ~NV_PFIFO_INTR_CACHE_ERROR;
		}

		if (status & NV_PFIFO_INTR_DMA_PUSHER) {
			nv04_fifo_dma_pusher(device, priv, chid);
			status &= ~NV_PFIFO_INTR_DMA_PUSHER;
		}

		if (status & NV_PFIFO_INTR_SEMAPHORE) {
			uint32_t sem;

			status &= ~NV_PFIFO_INTR_SEMAPHORE;
			nv_wr32(priv, NV03_PFIFO_INTR_0,
				NV_PFIFO_INTR_SEMAPHORE);

			sem = nv_rd32(priv, NV10_PFIFO_CACHE1_SEMAPHORE);
			nv_wr32(priv, NV10_PFIFO_CACHE1_SEMAPHORE, sem | 0x1);

			nv_wr32(priv, NV03_PFIFO_CACHE1_GET, get + 4);
			nv_wr32(priv, NV04_PFIFO_CACHE1_PULL0, 1);
		}

		if (device->card_type == NV_50) {
			if (status & 0x00000010) {
				status &= ~0x00000010;
				nv_wr32(priv, 0x002100, 0x00000010);
			}

			if (status & 0x40000000) {
				nv_wr32(priv, 0x002100, 0x40000000);
				nouveau_fifo_uevent(&priv->base);
				status &= ~0x40000000;
			}
		}

		if (status) {
			nv_warn(priv, "unknown intr 0x%08x, ch %d\n",
				status, chid);
			nv_wr32(priv, NV03_PFIFO_INTR_0, status);
			status = 0;
		}

		nv_wr32(priv, NV03_PFIFO_CACHES, reassign);
	}

	if (status) {
		nv_error(priv, "still angry after %d spins, halt\n", cnt);
		nv_wr32(priv, 0x002140, 0);
		nv_wr32(priv, 0x000140, 0);
	}

	nv_wr32(priv, 0x000100, 0x00000100);
}

static int
nv04_fifo_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv04_instmem_priv *imem = nv04_instmem(parent);
	struct nv04_fifo_priv *priv;
	int ret;

	ret = nouveau_fifo_create(parent, engine, oclass, 0, 15, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nouveau_ramht_ref(imem->ramht, &priv->ramht);
	nouveau_gpuobj_ref(imem->ramro, &priv->ramro);
	nouveau_gpuobj_ref(imem->ramfc, &priv->ramfc);

	nv_subdev(priv)->unit = 0x00000100;
	nv_subdev(priv)->intr = nv04_fifo_intr;
	nv_engine(priv)->cclass = &nv04_fifo_cclass;
	nv_engine(priv)->sclass = nv04_fifo_sclass;
	priv->base.pause = nv04_fifo_pause;
	priv->base.start = nv04_fifo_start;
	priv->ramfc_desc = nv04_ramfc;
	return 0;
}

void
nv04_fifo_dtor(struct nouveau_object *object)
{
	struct nv04_fifo_priv *priv = (void *)object;
	nouveau_gpuobj_ref(NULL, &priv->ramfc);
	nouveau_gpuobj_ref(NULL, &priv->ramro);
	nouveau_ramht_ref(NULL, &priv->ramht);
	nouveau_fifo_destroy(&priv->base);
}

int
nv04_fifo_init(struct nouveau_object *object)
{
	struct nv04_fifo_priv *priv = (void *)object;
	int ret;

	ret = nouveau_fifo_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, NV04_PFIFO_DELAY_0, 0x000000ff);
	nv_wr32(priv, NV04_PFIFO_DMA_TIMESLICE, 0x0101ffff);

	nv_wr32(priv, NV03_PFIFO_RAMHT, (0x03 << 24) /* search 128 */ |
				       ((priv->ramht->bits - 9) << 16) |
				        (priv->ramht->base.addr >> 8));
	nv_wr32(priv, NV03_PFIFO_RAMRO, priv->ramro->addr >> 8);
	nv_wr32(priv, NV03_PFIFO_RAMFC, priv->ramfc->addr >> 8);

	nv_wr32(priv, NV03_PFIFO_CACHE1_PUSH1, priv->base.max);

	nv_wr32(priv, NV03_PFIFO_INTR_0, 0xffffffff);
	nv_wr32(priv, NV03_PFIFO_INTR_EN_0, 0xffffffff);

	nv_wr32(priv, NV03_PFIFO_CACHE1_PUSH0, 1);
	nv_wr32(priv, NV04_PFIFO_CACHE1_PULL0, 1);
	nv_wr32(priv, NV03_PFIFO_CACHES, 1);
	return 0;
}

struct nouveau_oclass *
nv04_fifo_oclass = &(struct nouveau_oclass) {
	.handle = NV_ENGINE(FIFO, 0x04),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_fifo_ctor,
		.dtor = nv04_fifo_dtor,
		.init = nv04_fifo_init,
		.fini = _nouveau_fifo_fini,
	},
};
