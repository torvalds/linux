/*
 * Copyright 2011 Red Hat Inc.
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
#include "nouveau_util.h"
#include "nouveau_vm.h"
#include "nouveau_ramht.h"

struct nv98_ppp_engine {
	struct nouveau_exec_engine base;
};

static int
nv98_ppp_fini(struct drm_device *dev, int engine, bool suspend)
{
	if (!(nv_rd32(dev, 0x000200) & 0x00000002))
		return 0;

	nv_mask(dev, 0x000200, 0x00000002, 0x00000000);
	return 0;
}

static int
nv98_ppp_init(struct drm_device *dev, int engine)
{
	nv_mask(dev, 0x000200, 0x00000002, 0x00000000);
	nv_mask(dev, 0x000200, 0x00000002, 0x00000002);
	return 0;
}

static void
nv98_ppp_destroy(struct drm_device *dev, int engine)
{
	struct nv98_ppp_engine *pppp = nv_engine(dev, engine);

	NVOBJ_ENGINE_DEL(dev, PPP);

	kfree(pppp);
}

int
nv98_ppp_create(struct drm_device *dev)
{
	struct nv98_ppp_engine *pppp;

	pppp = kzalloc(sizeof(*pppp), GFP_KERNEL);
	if (!pppp)
		return -ENOMEM;

	pppp->base.destroy = nv98_ppp_destroy;
	pppp->base.init = nv98_ppp_init;
	pppp->base.fini = nv98_ppp_fini;

	NVOBJ_ENGINE_ADD(dev, PPP, &pppp->base);
	return 0;
}
