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

#include <subdev/vm.h>
#include <subdev/bar.h>
#include <subdev/timer.h>

#include <engine/mpeg.h>

struct nv50_mpeg_priv {
	struct nouveau_mpeg base;
};

struct nv50_mpeg_chan {
	struct nouveau_mpeg_chan base;
};

/*******************************************************************************
 * MPEG object classes
 ******************************************************************************/

static int
nv50_mpeg_object_ctor(struct nouveau_object *parent,
		      struct nouveau_object *engine,
		      struct nouveau_oclass *oclass, void *data, u32 size,
		      struct nouveau_object **pobject)
{
	struct nouveau_gpuobj *obj;
	int ret;

	ret = nouveau_gpuobj_create(parent, engine, oclass, 0, parent,
				    16, 16, 0, &obj);
	*pobject = nv_object(obj);
	if (ret)
		return ret;

	nv_wo32(obj, 0x00, nv_mclass(obj));
	nv_wo32(obj, 0x04, 0x00000000);
	nv_wo32(obj, 0x08, 0x00000000);
	nv_wo32(obj, 0x0c, 0x00000000);
	return 0;
}

struct nouveau_ofuncs
nv50_mpeg_ofuncs = {
	.ctor = nv50_mpeg_object_ctor,
	.dtor = _nouveau_gpuobj_dtor,
	.init = _nouveau_gpuobj_init,
	.fini = _nouveau_gpuobj_fini,
	.rd32 = _nouveau_gpuobj_rd32,
	.wr32 = _nouveau_gpuobj_wr32,
};

static struct nouveau_oclass
nv50_mpeg_sclass[] = {
	{ 0x3174, &nv50_mpeg_ofuncs },
	{}
};

/*******************************************************************************
 * PMPEG context
 ******************************************************************************/

int
nv50_mpeg_context_ctor(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, void *data, u32 size,
		       struct nouveau_object **pobject)
{
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nv50_mpeg_chan *chan;
	int ret;

	ret = nouveau_mpeg_context_create(parent, engine, oclass, NULL, 128 * 4,
					  0, NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	nv_wo32(chan, 0x0070, 0x00801ec1);
	nv_wo32(chan, 0x007c, 0x0000037c);
	bar->flush(bar);
	return 0;
}

static struct nouveau_oclass
nv50_mpeg_cclass = {
	.handle = NV_ENGCTX(MPEG, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_mpeg_context_ctor,
		.dtor = _nouveau_mpeg_context_dtor,
		.init = _nouveau_mpeg_context_init,
		.fini = _nouveau_mpeg_context_fini,
		.rd32 = _nouveau_mpeg_context_rd32,
		.wr32 = _nouveau_mpeg_context_wr32,
	},
};

/*******************************************************************************
 * PMPEG engine/subdev functions
 ******************************************************************************/

void
nv50_mpeg_intr(struct nouveau_subdev *subdev)
{
	struct nv50_mpeg_priv *priv = (void *)subdev;
	u32 stat = nv_rd32(priv, 0x00b100);
	u32 type = nv_rd32(priv, 0x00b230);
	u32 mthd = nv_rd32(priv, 0x00b234);
	u32 data = nv_rd32(priv, 0x00b238);
	u32 show = stat;

	if (stat & 0x01000000) {
		/* happens on initial binding of the object */
		if (type == 0x00000020 && mthd == 0x0000) {
			nv_wr32(priv, 0x00b308, 0x00000100);
			show &= ~0x01000000;
		}
	}

	if (show) {
		nv_info(priv, "0x%08x 0x%08x 0x%08x 0x%08x\n",
			stat, type, mthd, data);
	}

	nv_wr32(priv, 0x00b100, stat);
	nv_wr32(priv, 0x00b230, 0x00000001);
}

static void
nv50_vpe_intr(struct nouveau_subdev *subdev)
{
	struct nv50_mpeg_priv *priv = (void *)subdev;

	if (nv_rd32(priv, 0x00b100))
		nv50_mpeg_intr(subdev);

	if (nv_rd32(priv, 0x00b800)) {
		u32 stat = nv_rd32(priv, 0x00b800);
		nv_info(priv, "PMSRCH: 0x%08x\n", stat);
		nv_wr32(priv, 0xb800, stat);
	}
}

static int
nv50_mpeg_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv50_mpeg_priv *priv;
	int ret;

	ret = nouveau_mpeg_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00400002;
	nv_subdev(priv)->intr = nv50_vpe_intr;
	nv_engine(priv)->cclass = &nv50_mpeg_cclass;
	nv_engine(priv)->sclass = nv50_mpeg_sclass;
	return 0;
}

int
nv50_mpeg_init(struct nouveau_object *object)
{
	struct nv50_mpeg_priv *priv = (void *)object;
	int ret;

	ret = nouveau_mpeg_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x00b32c, 0x00000000);
	nv_wr32(priv, 0x00b314, 0x00000100);
	nv_wr32(priv, 0x00b0e0, 0x0000001a);

	nv_wr32(priv, 0x00b220, 0x00000044);
	nv_wr32(priv, 0x00b300, 0x00801ec1);
	nv_wr32(priv, 0x00b390, 0x00000000);
	nv_wr32(priv, 0x00b394, 0x00000000);
	nv_wr32(priv, 0x00b398, 0x00000000);
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
nv50_mpeg_oclass = {
	.handle = NV_ENGINE(MPEG, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_mpeg_ctor,
		.dtor = _nouveau_mpeg_dtor,
		.init = nv50_mpeg_init,
		.fini = _nouveau_mpeg_fini,
	},
};
