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

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_ramht.h"
#include "nouveau_software.h"

struct nvc0_software_priv {
	struct nouveau_software_priv base;
};

struct nvc0_software_chan {
	struct nouveau_software_chan base;
};

static int
nvc0_software_context_new(struct nouveau_channel *chan, int engine)
{
	struct nvc0_software_chan *pch;

	pch = kzalloc(sizeof(*pch), GFP_KERNEL);
	if (!pch)
		return -ENOMEM;

	nouveau_software_context_new(&pch->base);
	chan->engctx[engine] = pch;
	return 0;
}

static void
nvc0_software_context_del(struct nouveau_channel *chan, int engine)
{
	struct nvc0_software_chan *pch = chan->engctx[engine];
	chan->engctx[engine] = NULL;
	kfree(pch);
}

static int
nvc0_software_object_new(struct nouveau_channel *chan, int engine,
			 u32 handle, u16 class)
{
	return 0;
}

static int
nvc0_software_init(struct drm_device *dev, int engine)
{
	return 0;
}

static int
nvc0_software_fini(struct drm_device *dev, int engine, bool suspend)
{
	return 0;
}

static void
nvc0_software_destroy(struct drm_device *dev, int engine)
{
	struct nvc0_software_priv *psw = nv_engine(dev, engine);

	NVOBJ_ENGINE_DEL(dev, SW);
	kfree(psw);
}

int
nvc0_software_create(struct drm_device *dev)
{
	struct nvc0_software_priv *psw = kzalloc(sizeof(*psw), GFP_KERNEL);
	if (!psw)
		return -ENOMEM;

	psw->base.base.destroy = nvc0_software_destroy;
	psw->base.base.init = nvc0_software_init;
	psw->base.base.fini = nvc0_software_fini;
	psw->base.base.context_new = nvc0_software_context_new;
	psw->base.base.context_del = nvc0_software_context_del;
	psw->base.base.object_new = nvc0_software_object_new;
	nouveau_software_create(&psw->base);

	NVOBJ_ENGINE_ADD(dev, SW, &psw->base.base);
	NVOBJ_CLASS(dev, 0x906e, SW);
	return 0;
}
