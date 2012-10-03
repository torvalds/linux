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

#include <subdev/fb.h>
#include <subdev/timer.h>
#include <subdev/instmem.h>

#include <engine/mpeg.h>
#include <engine/graph/nv40.h>

struct nv40_mpeg_priv {
	struct nouveau_mpeg base;
};

struct nv40_mpeg_chan {
	struct nouveau_mpeg base;
};

/*******************************************************************************
 * PMPEG context
 ******************************************************************************/

static int
nv40_mpeg_context_ctor(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, void *data, u32 size,
		       struct nouveau_object **pobject)
{
	struct nv40_mpeg_chan *chan;
	int ret;

	ret = nouveau_mpeg_context_create(parent, engine, oclass, NULL,
					  264 * 4, 16,
					  NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	return 0;
}

static int
nv40_mpeg_context_fini(struct nouveau_object *object, bool suspend)
{

	struct nv40_mpeg_priv *priv = (void *)object->engine;
	struct nv40_mpeg_chan *chan = (void *)object;
	u32 inst = 0x80000000 | nv_gpuobj(chan)->addr >> 4;

	nv_mask(priv, 0x00b32c, 0x00000001, 0x00000000);
	if (nv_rd32(priv, 0x00b318) == inst)
		nv_mask(priv, 0x00b318, 0x80000000, 0x00000000);
	nv_mask(priv, 0x00b32c, 0x00000001, 0x00000001);
	return 0;
}

static struct nouveau_oclass
nv40_mpeg_cclass = {
	.handle = NV_ENGCTX(MPEG, 0x40),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv40_mpeg_context_ctor,
		.dtor = _nouveau_mpeg_context_dtor,
		.init = _nouveau_mpeg_context_init,
		.fini = nv40_mpeg_context_fini,
		.rd32 = _nouveau_mpeg_context_rd32,
		.wr32 = _nouveau_mpeg_context_wr32,
	},
};

/*******************************************************************************
 * PMPEG engine/subdev functions
 ******************************************************************************/

static void
nv40_mpeg_intr(struct nouveau_subdev *subdev)
{
	struct nv40_mpeg_priv *priv = (void *)subdev;
	u32 stat;

	if ((stat = nv_rd32(priv, 0x00b100)))
		nv31_mpeg_intr(subdev);

	if ((stat = nv_rd32(priv, 0x00b800))) {
		nv_error(priv, "PMSRCH 0x%08x\n", stat);
		nv_wr32(priv, 0x00b800, stat);
	}
}

static int
nv40_mpeg_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv40_mpeg_priv *priv;
	int ret;

	ret = nouveau_mpeg_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000002;
	nv_subdev(priv)->intr = nv40_mpeg_intr;
	nv_engine(priv)->cclass = &nv40_mpeg_cclass;
	nv_engine(priv)->sclass = nv31_mpeg_sclass;
	nv_engine(priv)->tile_prog = nv31_mpeg_tile_prog;
	return 0;
}

struct nouveau_oclass
nv40_mpeg_oclass = {
	.handle = NV_ENGINE(MPEG, 0x40),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv40_mpeg_ctor,
		.dtor = _nouveau_mpeg_dtor,
		.init = nv31_mpeg_init,
		.fini = _nouveau_mpeg_fini,
	},
};
