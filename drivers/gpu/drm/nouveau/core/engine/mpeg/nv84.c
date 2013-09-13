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

struct nv84_mpeg_priv {
	struct nouveau_mpeg base;
};

struct nv84_mpeg_chan {
	struct nouveau_mpeg_chan base;
};

/*******************************************************************************
 * MPEG object classes
 ******************************************************************************/

static struct nouveau_oclass
nv84_mpeg_sclass[] = {
	{ 0x8274, &nv50_mpeg_ofuncs },
	{}
};

/*******************************************************************************
 * PMPEG context
 ******************************************************************************/

static struct nouveau_oclass
nv84_mpeg_cclass = {
	.handle = NV_ENGCTX(MPEG, 0x84),
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

static int
nv84_mpeg_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv84_mpeg_priv *priv;
	int ret;

	ret = nouveau_mpeg_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000002;
	nv_subdev(priv)->intr = nv50_mpeg_intr;
	nv_engine(priv)->cclass = &nv84_mpeg_cclass;
	nv_engine(priv)->sclass = nv84_mpeg_sclass;
	return 0;
}

struct nouveau_oclass
nv84_mpeg_oclass = {
	.handle = NV_ENGINE(MPEG, 0x84),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv84_mpeg_ctor,
		.dtor = _nouveau_mpeg_dtor,
		.init = nv50_mpeg_init,
		.fini = _nouveau_mpeg_fini,
	},
};
