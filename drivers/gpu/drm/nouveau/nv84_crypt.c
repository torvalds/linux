/*
 * Copyright 2010 Red Hat Inc.
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

int
nv84_crypt_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramin = chan->ramin;
	int ret;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	ret = nouveau_gpuobj_new(dev, chan, 256, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC | NVOBJ_FLAG_ZERO_FREE,
				 &chan->crypt_ctx);
	if (ret)
		return ret;

	nv_wo32(ramin, 0xa0, 0x00190000);
	nv_wo32(ramin, 0xa4, chan->crypt_ctx->vinst + 0xff);
	nv_wo32(ramin, 0xa8, chan->crypt_ctx->vinst);
	nv_wo32(ramin, 0xac, 0);
	nv_wo32(ramin, 0xb0, 0);
	nv_wo32(ramin, 0xb4, 0);

	dev_priv->engine.instmem.flush(dev);
	return 0;
}

void
nv84_crypt_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	u32 inst;

	if (!chan->ramin)
		return;

	inst  = (chan->ramin->vinst >> 12);
	inst |= 0x80000000;

	/* mark context as invalid if still on the hardware, not
	 * doing this causes issues the next time PCRYPT is used,
	 * unsurprisingly :)
	 */
	nv_wr32(dev, 0x10200c, 0x00000000);
	if (nv_rd32(dev, 0x102188) == inst)
		nv_mask(dev, 0x102188, 0x80000000, 0x00000000);
	if (nv_rd32(dev, 0x10218c) == inst)
		nv_mask(dev, 0x10218c, 0x80000000, 0x00000000);
	nv_wr32(dev, 0x10200c, 0x00000010);

	nouveau_gpuobj_ref(NULL, &chan->crypt_ctx);
}

void
nv84_crypt_tlb_flush(struct drm_device *dev)
{
	nv50_vm_flush(dev, 0x0a);
}

int
nv84_crypt_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_crypt_engine *pcrypt = &dev_priv->engine.crypt;

	if (!pcrypt->registered) {
		NVOBJ_CLASS(dev, 0x74c1, CRYPT);
		pcrypt->registered = true;
	}

	nv_mask(dev, 0x000200, 0x00004000, 0x00000000);
	nv_mask(dev, 0x000200, 0x00004000, 0x00004000);
	nv_wr32(dev, 0x102130, 0xffffffff);
	nv_wr32(dev, 0x102140, 0xffffffbf);
	nv_wr32(dev, 0x10200c, 0x00000010);
	return 0;
}

void
nv84_crypt_fini(struct drm_device *dev)
{
	nv_wr32(dev, 0x102140, 0x00000000);
}
