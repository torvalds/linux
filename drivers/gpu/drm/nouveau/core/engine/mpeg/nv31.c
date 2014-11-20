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
#include <core/os.h>
#include <core/engctx.h>
#include <core/handle.h>

#include <subdev/fb.h>
#include <subdev/timer.h>
#include <subdev/instmem.h>

#include <engine/fifo.h>
#include <engine/mpeg.h>
#include <engine/mpeg/nv31.h>

/*******************************************************************************
 * MPEG object classes
 ******************************************************************************/

static int
nv31_mpeg_object_ctor(struct nouveau_object *parent,
		      struct nouveau_object *engine,
		      struct nouveau_oclass *oclass, void *data, u32 size,
		      struct nouveau_object **pobject)
{
	struct nouveau_gpuobj *obj;
	int ret;

	ret = nouveau_gpuobj_create(parent, engine, oclass, 0, parent,
				    20, 16, 0, &obj);
	*pobject = nv_object(obj);
	if (ret)
		return ret;

	nv_wo32(obj, 0x00, nv_mclass(obj));
	nv_wo32(obj, 0x04, 0x00000000);
	nv_wo32(obj, 0x08, 0x00000000);
	nv_wo32(obj, 0x0c, 0x00000000);
	return 0;
}

static int
nv31_mpeg_mthd_dma(struct nouveau_object *object, u32 mthd, void *arg, u32 len)
{
	struct nouveau_instmem *imem = nouveau_instmem(object);
	struct nv31_mpeg_priv *priv = (void *)object->engine;
	u32 inst = *(u32 *)arg << 4;
	u32 dma0 = nv_ro32(imem, inst + 0);
	u32 dma1 = nv_ro32(imem, inst + 4);
	u32 dma2 = nv_ro32(imem, inst + 8);
	u32 base = (dma2 & 0xfffff000) | (dma0 >> 20);
	u32 size = dma1 + 1;

	/* only allow linear DMA objects */
	if (!(dma0 & 0x00002000))
		return -EINVAL;

	if (mthd == 0x0190) {
		/* DMA_CMD */
		nv_mask(priv, 0x00b300, 0x00010000, (dma0 & 0x00030000) ? 0x00010000 : 0);
		nv_wr32(priv, 0x00b334, base);
		nv_wr32(priv, 0x00b324, size);
	} else
	if (mthd == 0x01a0) {
		/* DMA_DATA */
		nv_mask(priv, 0x00b300, 0x00020000, (dma0 & 0x00030000) ? 0x00020000 : 0);
		nv_wr32(priv, 0x00b360, base);
		nv_wr32(priv, 0x00b364, size);
	} else {
		/* DMA_IMAGE, VRAM only */
		if (dma0 & 0x00030000)
			return -EINVAL;

		nv_wr32(priv, 0x00b370, base);
		nv_wr32(priv, 0x00b374, size);
	}

	return 0;
}

struct nouveau_ofuncs
nv31_mpeg_ofuncs = {
	.ctor = nv31_mpeg_object_ctor,
	.dtor = _nouveau_gpuobj_dtor,
	.init = _nouveau_gpuobj_init,
	.fini = _nouveau_gpuobj_fini,
	.rd32 = _nouveau_gpuobj_rd32,
	.wr32 = _nouveau_gpuobj_wr32,
};

static struct nouveau_omthds
nv31_mpeg_omthds[] = {
	{ 0x0190, 0x0190, nv31_mpeg_mthd_dma },
	{ 0x01a0, 0x01a0, nv31_mpeg_mthd_dma },
	{ 0x01b0, 0x01b0, nv31_mpeg_mthd_dma },
	{}
};

struct nouveau_oclass
nv31_mpeg_sclass[] = {
	{ 0x3174, &nv31_mpeg_ofuncs, nv31_mpeg_omthds },
	{}
};

/*******************************************************************************
 * PMPEG context
 ******************************************************************************/

static int
nv31_mpeg_context_ctor(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, void *data, u32 size,
		       struct nouveau_object **pobject)
{
	struct nv31_mpeg_priv *priv = (void *)engine;
	struct nv31_mpeg_chan *chan;
	unsigned long flags;
	int ret;

	ret = nouveau_object_create(parent, engine, oclass, 0, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	spin_lock_irqsave(&nv_engine(priv)->lock, flags);
	if (priv->chan) {
		spin_unlock_irqrestore(&nv_engine(priv)->lock, flags);
		nouveau_object_destroy(&chan->base);
		*pobject = NULL;
		return -EBUSY;
	}
	priv->chan = chan;
	spin_unlock_irqrestore(&nv_engine(priv)->lock, flags);
	return 0;
}

static void
nv31_mpeg_context_dtor(struct nouveau_object *object)
{
	struct nv31_mpeg_priv *priv = (void *)object->engine;
	struct nv31_mpeg_chan *chan = (void *)object;
	unsigned long flags;

	spin_lock_irqsave(&nv_engine(priv)->lock, flags);
	priv->chan = NULL;
	spin_unlock_irqrestore(&nv_engine(priv)->lock, flags);
	nouveau_object_destroy(&chan->base);
}

struct nouveau_oclass
nv31_mpeg_cclass = {
	.handle = NV_ENGCTX(MPEG, 0x31),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv31_mpeg_context_ctor,
		.dtor = nv31_mpeg_context_dtor,
		.init = nouveau_object_init,
		.fini = nouveau_object_fini,
	},
};

/*******************************************************************************
 * PMPEG engine/subdev functions
 ******************************************************************************/

void
nv31_mpeg_tile_prog(struct nouveau_engine *engine, int i)
{
	struct nouveau_fb_tile *tile = &nouveau_fb(engine)->tile.region[i];
	struct nv31_mpeg_priv *priv = (void *)engine;

	nv_wr32(priv, 0x00b008 + (i * 0x10), tile->pitch);
	nv_wr32(priv, 0x00b004 + (i * 0x10), tile->limit);
	nv_wr32(priv, 0x00b000 + (i * 0x10), tile->addr);
}

void
nv31_mpeg_intr(struct nouveau_subdev *subdev)
{
	struct nv31_mpeg_priv *priv = (void *)subdev;
	struct nouveau_fifo *pfifo = nouveau_fifo(subdev);
	struct nouveau_handle *handle;
	struct nouveau_object *engctx;
	u32 stat = nv_rd32(priv, 0x00b100);
	u32 type = nv_rd32(priv, 0x00b230);
	u32 mthd = nv_rd32(priv, 0x00b234);
	u32 data = nv_rd32(priv, 0x00b238);
	u32 show = stat;
	unsigned long flags;

	spin_lock_irqsave(&nv_engine(priv)->lock, flags);
	engctx = nv_object(priv->chan);

	if (stat & 0x01000000) {
		/* happens on initial binding of the object */
		if (type == 0x00000020 && mthd == 0x0000) {
			nv_mask(priv, 0x00b308, 0x00000000, 0x00000000);
			show &= ~0x01000000;
		}

		if (type == 0x00000010 && engctx) {
			handle = nouveau_handle_get_class(engctx, 0x3174);
			if (handle && !nv_call(handle->object, mthd, data))
				show &= ~0x01000000;
			nouveau_handle_put(handle);
		}
	}

	nv_wr32(priv, 0x00b100, stat);
	nv_wr32(priv, 0x00b230, 0x00000001);

	if (show) {
		nv_error(priv, "ch %d [%s] 0x%08x 0x%08x 0x%08x 0x%08x\n",
			 pfifo->chid(pfifo, engctx),
			 nouveau_client_name(engctx), stat, type, mthd, data);
	}

	spin_unlock_irqrestore(&nv_engine(priv)->lock, flags);
}

static int
nv31_mpeg_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv31_mpeg_priv *priv;
	int ret;

	ret = nouveau_mpeg_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000002;
	nv_subdev(priv)->intr = nv31_mpeg_intr;
	nv_engine(priv)->cclass = &nv31_mpeg_cclass;
	nv_engine(priv)->sclass = nv31_mpeg_sclass;
	nv_engine(priv)->tile_prog = nv31_mpeg_tile_prog;
	return 0;
}

int
nv31_mpeg_init(struct nouveau_object *object)
{
	struct nouveau_engine *engine = nv_engine(object);
	struct nv31_mpeg_priv *priv = (void *)object;
	struct nouveau_fb *pfb = nouveau_fb(object);
	int ret, i;

	ret = nouveau_mpeg_init(&priv->base);
	if (ret)
		return ret;

	/* VPE init */
	nv_wr32(priv, 0x00b0e0, 0x00000020); /* nvidia: rd 0x01, wr 0x20 */
	nv_wr32(priv, 0x00b0e8, 0x00000020); /* nvidia: rd 0x01, wr 0x20 */

	for (i = 0; i < pfb->tile.regions; i++)
		engine->tile_prog(engine, i);

	/* PMPEG init */
	nv_wr32(priv, 0x00b32c, 0x00000000);
	nv_wr32(priv, 0x00b314, 0x00000100);
	nv_wr32(priv, 0x00b220, 0x00000031);
	nv_wr32(priv, 0x00b300, 0x02001ec1);
	nv_mask(priv, 0x00b32c, 0x00000001, 0x00000001);

	nv_wr32(priv, 0x00b100, 0xffffffff);
	nv_wr32(priv, 0x00b140, 0xffffffff);

	if (!nv_wait(priv, 0x00b200, 0x00000001, 0x00000000)) {
		nv_error(priv, "timeout 0x%08x\n", nv_rd32(priv, 0x00b200));
		return -EBUSY;
	}

	return 0;
}

struct nouveau_oclass
nv31_mpeg_oclass = {
	.handle = NV_ENGINE(MPEG, 0x31),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv31_mpeg_ctor,
		.dtor = _nouveau_mpeg_dtor,
		.init = nv31_mpeg_init,
		.fini = _nouveau_mpeg_fini,
	},
};
