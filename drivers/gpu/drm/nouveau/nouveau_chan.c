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

#include <core/object.h>
#include <core/client.h>
#include <core/device.h>
#include <core/class.h>

#include <subdev/fb.h>
#include <subdev/vm.h>
#include <subdev/instmem.h>

#include <engine/software.h>

#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nouveau_bo.h"
#include "nouveau_chan.h"
#include "nouveau_fence.h"
#include "nouveau_abi16.h"

MODULE_PARM_DESC(vram_pushbuf, "Create DMA push buffers in VRAM");
static int nouveau_vram_pushbuf;
module_param_named(vram_pushbuf, nouveau_vram_pushbuf, int, 0400);

int
nouveau_channel_idle(struct nouveau_channel *chan)
{
	struct nouveau_cli *cli = chan->cli;
	struct nouveau_fence *fence = NULL;
	int ret;

	ret = nouveau_fence_new(chan, &fence);
	if (!ret) {
		ret = nouveau_fence_wait(fence, false, false);
		nouveau_fence_unref(&fence);
	}

	if (ret)
		NV_ERROR(cli, "failed to idle channel 0x%08x\n", chan->handle);
	return ret;
}

void
nouveau_channel_del(struct nouveau_channel **pchan)
{
	struct nouveau_channel *chan = *pchan;
	if (chan) {
		struct nouveau_object *client = nv_object(chan->cli);
		if (chan->fence) {
			nouveau_channel_idle(chan);
			nouveau_fence(chan->drm)->context_del(chan);
		}
		nouveau_object_del(client, NVDRM_DEVICE, chan->handle);
		nouveau_object_del(client, NVDRM_DEVICE, chan->push.handle);
		nouveau_bo_vma_del(chan->push.buffer, &chan->push.vma);
		nouveau_bo_unmap(chan->push.buffer);
		if (chan->push.buffer && chan->push.buffer->pin_refcnt)
			nouveau_bo_unpin(chan->push.buffer);
		nouveau_bo_ref(NULL, &chan->push.buffer);
		kfree(chan);
	}
	*pchan = NULL;
}

static int
nouveau_channel_prep(struct nouveau_drm *drm, struct nouveau_cli *cli,
		     u32 parent, u32 handle, u32 size,
		     struct nouveau_channel **pchan)
{
	struct nouveau_device *device = nv_device(drm->device);
	struct nouveau_instmem *imem = nouveau_instmem(device);
	struct nouveau_vmmgr *vmm = nouveau_vmmgr(device);
	struct nouveau_fb *pfb = nouveau_fb(device);
	struct nouveau_client *client = &cli->base;
	struct nv_dma_class args = {};
	struct nouveau_channel *chan;
	struct nouveau_object *push;
	u32 target;
	int ret;

	chan = *pchan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	chan->cli = cli;
	chan->drm = drm;
	chan->handle = handle;

	/* allocate memory for dma push buffer */
	target = TTM_PL_FLAG_TT;
	if (nouveau_vram_pushbuf)
		target = TTM_PL_FLAG_VRAM;

	ret = nouveau_bo_new(drm->dev, size, 0, target, 0, 0, NULL,
			    &chan->push.buffer);
	if (ret == 0) {
		ret = nouveau_bo_pin(chan->push.buffer, target);
		if (ret == 0)
			ret = nouveau_bo_map(chan->push.buffer);
	}

	if (ret) {
		nouveau_channel_del(pchan);
		return ret;
	}

	/* create dma object covering the *entire* memory space that the
	 * pushbuf lives in, this is because the GEM code requires that
	 * we be able to call out to other (indirect) push buffers
	 */
	chan->push.vma.offset = chan->push.buffer->bo.offset;
	chan->push.handle = NVDRM_PUSH | (handle & 0xffff);

	if (device->card_type >= NV_50) {
		ret = nouveau_bo_vma_add(chan->push.buffer, client->vm,
					&chan->push.vma);
		if (ret) {
			nouveau_channel_del(pchan);
			return ret;
		}

		args.flags = NV_DMA_TARGET_VM | NV_DMA_ACCESS_VM;
		args.start = 0;
		args.limit = client->vm->vmm->limit - 1;
	} else
	if (chan->push.buffer->bo.mem.mem_type == TTM_PL_VRAM) {
		u64 limit = pfb->ram.size - imem->reserved - 1;
		if (device->card_type == NV_04) {
			/* nv04 vram pushbuf hack, retarget to its location in
			 * the framebuffer bar rather than direct vram access..
			 * nfi why this exists, it came from the -nv ddx.
			 */
			args.flags = NV_DMA_TARGET_PCI | NV_DMA_ACCESS_RDWR;
			args.start = pci_resource_start(device->pdev, 1);
			args.limit = args.start + limit;
		} else {
			args.flags = NV_DMA_TARGET_VRAM | NV_DMA_ACCESS_RDWR;
			args.start = 0;
			args.limit = limit;
		}
	} else {
		if (chan->drm->agp.stat == ENABLED) {
			args.flags = NV_DMA_TARGET_AGP | NV_DMA_ACCESS_RDWR;
			args.start = chan->drm->agp.base;
			args.limit = chan->drm->agp.base +
				     chan->drm->agp.size - 1;
		} else {
			args.flags = NV_DMA_TARGET_VM | NV_DMA_ACCESS_RDWR;
			args.start = 0;
			args.limit = vmm->limit - 1;
		}
	}

	ret = nouveau_object_new(nv_object(chan->cli), parent,
				 chan->push.handle, 0x0002,
				 &args, sizeof(args), &push);
	if (ret) {
		nouveau_channel_del(pchan);
		return ret;
	}

	return 0;
}

static int
nouveau_channel_ind(struct nouveau_drm *drm, struct nouveau_cli *cli,
		    u32 parent, u32 handle, u32 engine,
		    struct nouveau_channel **pchan)
{
	static const u16 oclasses[] = { NVE0_CHANNEL_IND_CLASS,
					NVC0_CHANNEL_IND_CLASS,
					NV84_CHANNEL_IND_CLASS,
					NV50_CHANNEL_IND_CLASS,
					0 };
	const u16 *oclass = oclasses;
	struct nve0_channel_ind_class args;
	struct nouveau_channel *chan;
	int ret;

	/* allocate dma push buffer */
	ret = nouveau_channel_prep(drm, cli, parent, handle, 0x12000, &chan);
	*pchan = chan;
	if (ret)
		return ret;

	/* create channel object */
	args.pushbuf = chan->push.handle;
	args.ioffset = 0x10000 + chan->push.vma.offset;
	args.ilength = 0x02000;
	args.engine  = engine;

	do {
		ret = nouveau_object_new(nv_object(cli), parent, handle,
					 *oclass++, &args, sizeof(args),
					 &chan->object);
		if (ret == 0)
			return ret;
	} while (*oclass);

	nouveau_channel_del(pchan);
	return ret;
}

static int
nouveau_channel_dma(struct nouveau_drm *drm, struct nouveau_cli *cli,
		    u32 parent, u32 handle, struct nouveau_channel **pchan)
{
	static const u16 oclasses[] = { NV40_CHANNEL_DMA_CLASS,
					NV17_CHANNEL_DMA_CLASS,
					NV10_CHANNEL_DMA_CLASS,
					NV03_CHANNEL_DMA_CLASS,
					0 };
	const u16 *oclass = oclasses;
	struct nv03_channel_dma_class args;
	struct nouveau_channel *chan;
	int ret;

	/* allocate dma push buffer */
	ret = nouveau_channel_prep(drm, cli, parent, handle, 0x10000, &chan);
	*pchan = chan;
	if (ret)
		return ret;

	/* create channel object */
	args.pushbuf = chan->push.handle;
	args.offset = chan->push.vma.offset;

	do {
		ret = nouveau_object_new(nv_object(cli), parent, handle,
					 *oclass++, &args, sizeof(args),
					 &chan->object);
		if (ret == 0)
			return ret;
	} while (ret && *oclass);

	nouveau_channel_del(pchan);
	return ret;
}

static int
nouveau_channel_init(struct nouveau_channel *chan, u32 vram, u32 gart)
{
	struct nouveau_client *client = nv_client(chan->cli);
	struct nouveau_device *device = nv_device(chan->drm->device);
	struct nouveau_instmem *imem = nouveau_instmem(device);
	struct nouveau_vmmgr *vmm = nouveau_vmmgr(device);
	struct nouveau_fb *pfb = nouveau_fb(device);
	struct nouveau_software_chan *swch;
	struct nouveau_object *object;
	struct nv_dma_class args = {};
	int ret, i;

	/* allocate dma objects to cover all allowed vram, and gart */
	if (device->card_type < NV_C0) {
		if (device->card_type >= NV_50) {
			args.flags = NV_DMA_TARGET_VM | NV_DMA_ACCESS_VM;
			args.start = 0;
			args.limit = client->vm->vmm->limit - 1;
		} else {
			args.flags = NV_DMA_TARGET_VRAM | NV_DMA_ACCESS_RDWR;
			args.start = 0;
			args.limit = pfb->ram.size - imem->reserved - 1;
		}

		ret = nouveau_object_new(nv_object(client), chan->handle, vram,
					 0x003d, &args, sizeof(args), &object);
		if (ret)
			return ret;

		if (device->card_type >= NV_50) {
			args.flags = NV_DMA_TARGET_VM | NV_DMA_ACCESS_VM;
			args.start = 0;
			args.limit = client->vm->vmm->limit - 1;
		} else
		if (chan->drm->agp.stat == ENABLED) {
			args.flags = NV_DMA_TARGET_AGP | NV_DMA_ACCESS_RDWR;
			args.start = chan->drm->agp.base;
			args.limit = chan->drm->agp.base +
				     chan->drm->agp.size - 1;
		} else {
			args.flags = NV_DMA_TARGET_VM | NV_DMA_ACCESS_RDWR;
			args.start = 0;
			args.limit = vmm->limit - 1;
		}

		ret = nouveau_object_new(nv_object(client), chan->handle, gart,
					 0x003d, &args, sizeof(args), &object);
		if (ret)
			return ret;

		chan->vram = vram;
		chan->gart = gart;
	}

	/* initialise dma tracking parameters */
	switch (nv_hclass(chan->object) & 0x00ff) {
	case 0x006b:
	case 0x006e:
		chan->user_put = 0x40;
		chan->user_get = 0x44;
		chan->dma.max = (0x10000 / 4) - 2;
		break;
	default:
		chan->user_put = 0x40;
		chan->user_get = 0x44;
		chan->user_get_hi = 0x60;
		chan->dma.ib_base =  0x10000 / 4;
		chan->dma.ib_max  = (0x02000 / 8) - 1;
		chan->dma.ib_put  = 0;
		chan->dma.ib_free = chan->dma.ib_max - chan->dma.ib_put;
		chan->dma.max = chan->dma.ib_base;
		break;
	}

	chan->dma.put = 0;
	chan->dma.cur = chan->dma.put;
	chan->dma.free = chan->dma.max - chan->dma.cur;

	ret = RING_SPACE(chan, NOUVEAU_DMA_SKIPS);
	if (ret)
		return ret;

	for (i = 0; i < NOUVEAU_DMA_SKIPS; i++)
		OUT_RING(chan, 0x00000000);

	/* allocate software object class (used for fences on <= nv05, and
	 * to signal flip completion), bind it to a subchannel.
	 */
	if ((device->card_type < NV_E0) || gart /* nve0: want_nvsw */) {
		ret = nouveau_object_new(nv_object(client), chan->handle,
					 NvSw, nouveau_abi16_swclass(chan->drm),
					 NULL, 0, &object);
		if (ret)
			return ret;

		swch = (void *)object->parent;
		swch->flip = nouveau_flip_complete;
		swch->flip_data = chan;
	}

	if (device->card_type < NV_C0) {
		ret = RING_SPACE(chan, 2);
		if (ret)
			return ret;

		BEGIN_NV04(chan, NvSubSw, 0x0000, 1);
		OUT_RING  (chan, NvSw);
		FIRE_RING (chan);
	}

	/* initialise synchronisation */
	return nouveau_fence(chan->drm)->context_new(chan);
}

int
nouveau_channel_new(struct nouveau_drm *drm, struct nouveau_cli *cli,
		    u32 parent, u32 handle, u32 arg0, u32 arg1,
		    struct nouveau_channel **pchan)
{
	int ret;

	ret = nouveau_channel_ind(drm, cli, parent, handle, arg0, pchan);
	if (ret) {
		NV_DEBUG(cli, "ib channel create, %d\n", ret);
		ret = nouveau_channel_dma(drm, cli, parent, handle, pchan);
		if (ret) {
			NV_DEBUG(cli, "dma channel create, %d\n", ret);
			return ret;
		}
	}

	ret = nouveau_channel_init(*pchan, arg0, arg1);
	if (ret) {
		NV_ERROR(cli, "channel failed to initialise, %d\n", ret);
		nouveau_channel_del(pchan);
		return ret;
	}

	return 0;
}
