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
#include <engine/mpeg.h>

#include <core/client.h>
#include <core/handle.h>
#include <engine/fifo.h>

struct nv44_mpeg_priv {
	struct nvkm_mpeg base;
};

struct nv44_mpeg_chan {
	struct nvkm_mpeg_chan base;
};

/*******************************************************************************
 * PMPEG context
 ******************************************************************************/

static int
nv44_mpeg_context_ctor(struct nvkm_object *parent,
		       struct nvkm_object *engine,
		       struct nvkm_oclass *oclass, void *data, u32 size,
		       struct nvkm_object **pobject)
{
	struct nv44_mpeg_chan *chan;
	int ret;

	ret = nvkm_mpeg_context_create(parent, engine, oclass, NULL, 264 * 4,
				       16, NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	nv_wo32(&chan->base.base, 0x78, 0x02001ec1);
	return 0;
}

static int
nv44_mpeg_context_fini(struct nvkm_object *object, bool suspend)
{

	struct nv44_mpeg_priv *priv = (void *)object->engine;
	struct nv44_mpeg_chan *chan = (void *)object;
	u32 inst = 0x80000000 | nv_gpuobj(chan)->addr >> 4;

	nv_mask(priv, 0x00b32c, 0x00000001, 0x00000000);
	if (nv_rd32(priv, 0x00b318) == inst)
		nv_mask(priv, 0x00b318, 0x80000000, 0x00000000);
	nv_mask(priv, 0x00b32c, 0x00000001, 0x00000001);
	return 0;
}

static struct nvkm_oclass
nv44_mpeg_cclass = {
	.handle = NV_ENGCTX(MPEG, 0x44),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv44_mpeg_context_ctor,
		.dtor = _nvkm_mpeg_context_dtor,
		.init = _nvkm_mpeg_context_init,
		.fini = nv44_mpeg_context_fini,
		.rd32 = _nvkm_mpeg_context_rd32,
		.wr32 = _nvkm_mpeg_context_wr32,
	},
};

/*******************************************************************************
 * PMPEG engine/subdev functions
 ******************************************************************************/

static void
nv44_mpeg_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_fifo *pfifo = nvkm_fifo(subdev);
	struct nvkm_engine *engine = nv_engine(subdev);
	struct nvkm_object *engctx;
	struct nvkm_handle *handle;
	struct nv44_mpeg_priv *priv = (void *)subdev;
	u32 inst = nv_rd32(priv, 0x00b318) & 0x000fffff;
	u32 stat = nv_rd32(priv, 0x00b100);
	u32 type = nv_rd32(priv, 0x00b230);
	u32 mthd = nv_rd32(priv, 0x00b234);
	u32 data = nv_rd32(priv, 0x00b238);
	u32 show = stat;
	int chid;

	engctx = nvkm_engctx_get(engine, inst);
	chid   = pfifo->chid(pfifo, engctx);

	if (stat & 0x01000000) {
		/* happens on initial binding of the object */
		if (type == 0x00000020 && mthd == 0x0000) {
			nv_mask(priv, 0x00b308, 0x00000000, 0x00000000);
			show &= ~0x01000000;
		}

		if (type == 0x00000010) {
			handle = nvkm_handle_get_class(engctx, 0x3174);
			if (handle && !nv_call(handle->object, mthd, data))
				show &= ~0x01000000;
			nvkm_handle_put(handle);
		}
	}

	nv_wr32(priv, 0x00b100, stat);
	nv_wr32(priv, 0x00b230, 0x00000001);

	if (show) {
		nv_error(priv,
			 "ch %d [0x%08x %s] 0x%08x 0x%08x 0x%08x 0x%08x\n",
			 chid, inst << 4, nvkm_client_name(engctx), stat,
			 type, mthd, data);
	}

	nvkm_engctx_put(engctx);
}

static void
nv44_mpeg_me_intr(struct nvkm_subdev *subdev)
{
	struct nv44_mpeg_priv *priv = (void *)subdev;
	u32 stat;

	if ((stat = nv_rd32(priv, 0x00b100)))
		nv44_mpeg_intr(subdev);

	if ((stat = nv_rd32(priv, 0x00b800))) {
		nv_error(priv, "PMSRCH 0x%08x\n", stat);
		nv_wr32(priv, 0x00b800, stat);
	}
}

static int
nv44_mpeg_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nv44_mpeg_priv *priv;
	int ret;

	ret = nvkm_mpeg_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000002;
	nv_subdev(priv)->intr = nv44_mpeg_me_intr;
	nv_engine(priv)->cclass = &nv44_mpeg_cclass;
	nv_engine(priv)->sclass = nv40_mpeg_sclass;
	nv_engine(priv)->tile_prog = nv31_mpeg_tile_prog;
	return 0;
}

struct nvkm_oclass
nv44_mpeg_oclass = {
	.handle = NV_ENGINE(MPEG, 0x44),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv44_mpeg_ctor,
		.dtor = _nvkm_mpeg_dtor,
		.init = nv31_mpeg_init,
		.fini = _nvkm_mpeg_fini,
	},
};
