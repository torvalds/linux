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

#include "nv50_display.h"

struct nv50_software_priv {
	struct nouveau_software_priv base;
};

struct nv50_software_chan {
	struct nouveau_software_chan base;
	struct {
		struct nouveau_gpuobj *object;
	} vblank;
};

static int
mthd_dma_vblsem(struct nouveau_channel *chan, u32 class, u32 mthd, u32 data)
{
	struct nv50_software_chan *pch = chan->engctx[NVOBJ_ENGINE_SW];
	struct nouveau_gpuobj *gpuobj;

	gpuobj = nouveau_ramht_find(chan, data);
	if (!gpuobj)
		return -ENOENT;

	if (nouveau_notifier_offset(gpuobj, NULL))
		return -EINVAL;

	pch->vblank.object = gpuobj;
	pch->base.vblank.offset = ~0;
	return 0;
}

static int
mthd_vblsem_offset(struct nouveau_channel *chan, u32 class, u32 mthd, u32 data)
{
	struct nv50_software_chan *pch = chan->engctx[NVOBJ_ENGINE_SW];

	if (nouveau_notifier_offset(pch->vblank.object, &data))
		return -ERANGE;

	pch->base.vblank.offset = data >> 2;
	return 0;
}

static int
mthd_vblsem_value(struct nouveau_channel *chan, u32 class, u32 mthd, u32 data)
{
	struct nv50_software_chan *pch = chan->engctx[NVOBJ_ENGINE_SW];
	pch->base.vblank.value = data;
	return 0;
}

static int
mthd_vblsem_release(struct nouveau_channel *chan, u32 class, u32 mthd, u32 data)
{
	struct nv50_software_priv *psw = nv_engine(chan->dev, NVOBJ_ENGINE_SW);
	struct nv50_software_chan *pch = chan->engctx[NVOBJ_ENGINE_SW];
	struct drm_device *dev = chan->dev;

	if (!pch->vblank.object || pch->base.vblank.offset == ~0 || data > 1)
		return -EINVAL;

	drm_vblank_get(dev, data);

	pch->base.vblank.head = data;
	list_add(&pch->base.vblank.list, &psw->base.vblank);
	return 0;
}

static int
mthd_flip(struct nouveau_channel *chan, u32 class, u32 mthd, u32 data)
{
	nouveau_finish_page_flip(chan, NULL);
	return 0;
}

static int
nv50_software_context_new(struct nouveau_channel *chan, int engine)
{
	struct nv50_software_priv *psw = nv_engine(chan->dev, NVOBJ_ENGINE_SW);
	struct nv50_display *pdisp = nv50_display(chan->dev);
	struct nv50_software_chan *pch;
	int ret = 0, i;

	pch = kzalloc(sizeof(*pch), GFP_KERNEL);
	if (!pch)
		return -ENOMEM;

	nouveau_software_context_new(&pch->base);
	pch->base.vblank.bo = chan->notifier_bo;
	chan->engctx[engine] = pch;

	/* dma objects for display sync channel semaphore blocks */
	for (i = 0; i < chan->dev->mode_config.num_crtc; i++) {
		struct nv50_display_crtc *dispc = &pdisp->crtc[i];
		struct nouveau_gpuobj *obj = NULL;

		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     dispc->sem.bo->bo.offset, 0x1000,
					     NV_MEM_ACCESS_RW,
					     NV_MEM_TARGET_VRAM, &obj);
		if (ret)
			break;

		ret = nouveau_ramht_insert(chan, NvEvoSema0 + i, obj);
		nouveau_gpuobj_ref(NULL, &obj);
	}

	if (ret)
		psw->base.base.context_del(chan, engine);
	return ret;
}

static void
nv50_software_context_del(struct nouveau_channel *chan, int engine)
{
	struct nv50_software_chan *pch = chan->engctx[engine];
	chan->engctx[engine] = NULL;
	kfree(pch);
}

static int
nv50_software_object_new(struct nouveau_channel *chan, int engine,
			 u32 handle, u16 class)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(dev, chan, 16, 16, 0, &obj);
	if (ret)
		return ret;
	obj->engine = 0;
	obj->class  = class;

	ret = nouveau_ramht_insert(chan, handle, obj);
	nouveau_gpuobj_ref(NULL, &obj);
	return ret;
}

static int
nv50_software_init(struct drm_device *dev, int engine)
{
	return 0;
}

static int
nv50_software_fini(struct drm_device *dev, int engine, bool suspend)
{
	return 0;
}

static void
nv50_software_destroy(struct drm_device *dev, int engine)
{
	struct nv50_software_priv *psw = nv_engine(dev, engine);

	NVOBJ_ENGINE_DEL(dev, SW);
	kfree(psw);
}

int
nv50_software_create(struct drm_device *dev)
{
	struct nv50_software_priv *psw = kzalloc(sizeof(*psw), GFP_KERNEL);
	if (!psw)
		return -ENOMEM;

	psw->base.base.destroy = nv50_software_destroy;
	psw->base.base.init = nv50_software_init;
	psw->base.base.fini = nv50_software_fini;
	psw->base.base.context_new = nv50_software_context_new;
	psw->base.base.context_del = nv50_software_context_del;
	psw->base.base.object_new = nv50_software_object_new;
	nouveau_software_create(&psw->base);

	NVOBJ_ENGINE_ADD(dev, SW, &psw->base.base);
	NVOBJ_CLASS(dev, 0x506e, SW);
	NVOBJ_MTHD (dev, 0x506e, 0x018c, mthd_dma_vblsem);
	NVOBJ_MTHD (dev, 0x506e, 0x0400, mthd_vblsem_offset);
	NVOBJ_MTHD (dev, 0x506e, 0x0404, mthd_vblsem_value);
	NVOBJ_MTHD (dev, 0x506e, 0x0408, mthd_vblsem_release);
	NVOBJ_MTHD (dev, 0x506e, 0x0500, mthd_flip);
	return 0;
}
