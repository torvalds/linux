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
#include <nvif/push006c.h>

#include <nvif/class.h>
#include <nvif/cl0002.h>
#include <nvif/cl006b.h>
#include <nvif/cl506f.h>
#include <nvif/cl906f.h>
#include <nvif/cla06f.h>
#include <nvif/clc36f.h>
#include <nvif/ioctl.h>

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_bo.h"
#include "nouveau_chan.h"
#include "nouveau_fence.h"
#include "nouveau_abi16.h"
#include "nouveau_vmm.h"
#include "nouveau_svm.h"

MODULE_PARM_DESC(vram_pushbuf, "Create DMA push buffers in VRAM");
int nouveau_vram_pushbuf;
module_param_named(vram_pushbuf, nouveau_vram_pushbuf, int, 0400);

static int
nouveau_channel_killed(struct nvif_notify *ntfy)
{
	struct nouveau_channel *chan = container_of(ntfy, typeof(*chan), kill);
	struct nouveau_cli *cli = (void *)chan->user.client;
	NV_PRINTK(warn, cli, "channel %d killed!\n", chan->chid);
	atomic_set(&chan->killed, 1);
	if (chan->fence)
		nouveau_fence_context_kill(chan->fence, -ENODEV);
	return NVIF_NOTIFY_DROP;
}

int
nouveau_channel_idle(struct nouveau_channel *chan)
{
	if (likely(chan && chan->fence && !atomic_read(&chan->killed))) {
		struct nouveau_cli *cli = (void *)chan->user.client;
		struct nouveau_fence *fence = NULL;
		int ret;

		ret = nouveau_fence_new(chan, false, &fence);
		if (!ret) {
			ret = nouveau_fence_wait(fence, false, false);
			nouveau_fence_unref(&fence);
		}

		if (ret) {
			NV_PRINTK(err, cli, "failed to idle channel %d [%s]\n",
				  chan->chid, nvxx_client(&cli->base)->name);
			return ret;
		}
	}
	return 0;
}

void
nouveau_channel_del(struct nouveau_channel **pchan)
{
	struct nouveau_channel *chan = *pchan;
	if (chan) {
		struct nouveau_cli *cli = (void *)chan->user.client;
		bool super;

		if (cli) {
			super = cli->base.super;
			cli->base.super = true;
		}

		if (chan->fence)
			nouveau_fence(chan->drm)->context_del(chan);

		if (cli)
			nouveau_svmm_part(chan->vmm->svmm, chan->inst);

		nvif_object_dtor(&chan->nvsw);
		nvif_object_dtor(&chan->gart);
		nvif_object_dtor(&chan->vram);
		nvif_notify_dtor(&chan->kill);
		nvif_object_dtor(&chan->user);
		nvif_object_dtor(&chan->push.ctxdma);
		nouveau_vma_del(&chan->push.vma);
		nouveau_bo_unmap(chan->push.buffer);
		if (chan->push.buffer && chan->push.buffer->bo.pin_count)
			nouveau_bo_unpin(chan->push.buffer);
		nouveau_bo_ref(NULL, &chan->push.buffer);
		kfree(chan);

		if (cli)
			cli->base.super = super;
	}
	*pchan = NULL;
}

static void
nouveau_channel_kick(struct nvif_push *push)
{
	struct nouveau_channel *chan = container_of(push, typeof(*chan), chan._push);
	chan->dma.cur = chan->dma.cur + (chan->chan._push.cur - chan->chan._push.bgn);
	FIRE_RING(chan);
	chan->chan._push.bgn = chan->chan._push.cur;
}

static int
nouveau_channel_wait(struct nvif_push *push, u32 size)
{
	struct nouveau_channel *chan = container_of(push, typeof(*chan), chan._push);
	int ret;
	chan->dma.cur = chan->dma.cur + (chan->chan._push.cur - chan->chan._push.bgn);
	ret = RING_SPACE(chan, size);
	if (ret == 0) {
		chan->chan._push.bgn = chan->chan._push.mem.object.map.ptr;
		chan->chan._push.bgn = chan->chan._push.bgn + chan->dma.cur;
		chan->chan._push.cur = chan->chan._push.bgn;
		chan->chan._push.end = chan->chan._push.bgn + size;
	}
	return ret;
}

static int
nouveau_channel_prep(struct nouveau_drm *drm, struct nvif_device *device,
		     u32 size, struct nouveau_channel **pchan)
{
	struct nouveau_cli *cli = (void *)device->object.client;
	struct nv_dma_v0 args = {};
	struct nouveau_channel *chan;
	u32 target;
	int ret;

	chan = *pchan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	chan->device = device;
	chan->drm = drm;
	chan->vmm = cli->svm.cli ? &cli->svm : &cli->vmm;
	atomic_set(&chan->killed, 0);

	/* allocate memory for dma push buffer */
	target = NOUVEAU_GEM_DOMAIN_GART | NOUVEAU_GEM_DOMAIN_COHERENT;
	if (nouveau_vram_pushbuf)
		target = NOUVEAU_GEM_DOMAIN_VRAM;

	ret = nouveau_bo_new(cli, size, 0, target, 0, 0, NULL, NULL,
			    &chan->push.buffer);
	if (ret == 0) {
		ret = nouveau_bo_pin(chan->push.buffer, target, false);
		if (ret == 0)
			ret = nouveau_bo_map(chan->push.buffer);
	}

	if (ret) {
		nouveau_channel_del(pchan);
		return ret;
	}

	chan->chan._push.mem.object.parent = cli->base.object.parent;
	chan->chan._push.mem.object.client = &cli->base;
	chan->chan._push.mem.object.name = "chanPush";
	chan->chan._push.mem.object.map.ptr = chan->push.buffer->kmap.virtual;
	chan->chan._push.wait = nouveau_channel_wait;
	chan->chan._push.kick = nouveau_channel_kick;
	chan->chan.push = &chan->chan._push;

	/* create dma object covering the *entire* memory space that the
	 * pushbuf lives in, this is because the GEM code requires that
	 * we be able to call out to other (indirect) push buffers
	 */
	chan->push.addr = chan->push.buffer->offset;

	if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
		ret = nouveau_vma_new(chan->push.buffer, chan->vmm,
				      &chan->push.vma);
		if (ret) {
			nouveau_channel_del(pchan);
			return ret;
		}

		chan->push.addr = chan->push.vma->addr;

		if (device->info.family >= NV_DEVICE_INFO_V0_FERMI)
			return 0;

		args.target = NV_DMA_V0_TARGET_VM;
		args.access = NV_DMA_V0_ACCESS_VM;
		args.start = 0;
		args.limit = chan->vmm->vmm.limit - 1;
	} else
	if (chan->push.buffer->bo.mem.mem_type == TTM_PL_VRAM) {
		if (device->info.family == NV_DEVICE_INFO_V0_TNT) {
			/* nv04 vram pushbuf hack, retarget to its location in
			 * the framebuffer bar rather than direct vram access..
			 * nfi why this exists, it came from the -nv ddx.
			 */
			args.target = NV_DMA_V0_TARGET_PCI;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = nvxx_device(device)->func->
				resource_addr(nvxx_device(device), 1);
			args.limit = args.start + device->info.ram_user - 1;
		} else {
			args.target = NV_DMA_V0_TARGET_VRAM;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = 0;
			args.limit = device->info.ram_user - 1;
		}
	} else {
		if (chan->drm->agp.bridge) {
			args.target = NV_DMA_V0_TARGET_AGP;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = chan->drm->agp.base;
			args.limit = chan->drm->agp.base +
				     chan->drm->agp.size - 1;
		} else {
			args.target = NV_DMA_V0_TARGET_VM;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = 0;
			args.limit = chan->vmm->vmm.limit - 1;
		}
	}

	ret = nvif_object_ctor(&device->object, "abi16PushCtxDma", 0,
			       NV_DMA_FROM_MEMORY, &args, sizeof(args),
			       &chan->push.ctxdma);
	if (ret) {
		nouveau_channel_del(pchan);
		return ret;
	}

	return 0;
}

static int
nouveau_channel_ind(struct nouveau_drm *drm, struct nvif_device *device,
		    u64 runlist, bool priv, struct nouveau_channel **pchan)
{
	static const u16 oclasses[] = { TURING_CHANNEL_GPFIFO_A,
					VOLTA_CHANNEL_GPFIFO_A,
					PASCAL_CHANNEL_GPFIFO_A,
					MAXWELL_CHANNEL_GPFIFO_A,
					KEPLER_CHANNEL_GPFIFO_B,
					KEPLER_CHANNEL_GPFIFO_A,
					FERMI_CHANNEL_GPFIFO,
					G82_CHANNEL_GPFIFO,
					NV50_CHANNEL_GPFIFO,
					0 };
	const u16 *oclass = oclasses;
	union {
		struct nv50_channel_gpfifo_v0 nv50;
		struct fermi_channel_gpfifo_v0 fermi;
		struct kepler_channel_gpfifo_a_v0 kepler;
		struct volta_channel_gpfifo_a_v0 volta;
	} args;
	struct nouveau_channel *chan;
	u32 size;
	int ret;

	/* allocate dma push buffer */
	ret = nouveau_channel_prep(drm, device, 0x12000, &chan);
	*pchan = chan;
	if (ret)
		return ret;

	/* create channel object */
	do {
		if (oclass[0] >= VOLTA_CHANNEL_GPFIFO_A) {
			args.volta.version = 0;
			args.volta.ilength = 0x02000;
			args.volta.ioffset = 0x10000 + chan->push.addr;
			args.volta.runlist = runlist;
			args.volta.vmm = nvif_handle(&chan->vmm->vmm.object);
			args.volta.priv = priv;
			size = sizeof(args.volta);
		} else
		if (oclass[0] >= KEPLER_CHANNEL_GPFIFO_A) {
			args.kepler.version = 0;
			args.kepler.ilength = 0x02000;
			args.kepler.ioffset = 0x10000 + chan->push.addr;
			args.kepler.runlist = runlist;
			args.kepler.vmm = nvif_handle(&chan->vmm->vmm.object);
			args.kepler.priv = priv;
			size = sizeof(args.kepler);
		} else
		if (oclass[0] >= FERMI_CHANNEL_GPFIFO) {
			args.fermi.version = 0;
			args.fermi.ilength = 0x02000;
			args.fermi.ioffset = 0x10000 + chan->push.addr;
			args.fermi.vmm = nvif_handle(&chan->vmm->vmm.object);
			size = sizeof(args.fermi);
		} else {
			args.nv50.version = 0;
			args.nv50.ilength = 0x02000;
			args.nv50.ioffset = 0x10000 + chan->push.addr;
			args.nv50.pushbuf = nvif_handle(&chan->push.ctxdma);
			args.nv50.vmm = nvif_handle(&chan->vmm->vmm.object);
			size = sizeof(args.nv50);
		}

		ret = nvif_object_ctor(&device->object, "abi16ChanUser", 0,
				       *oclass++, &args, size, &chan->user);
		if (ret == 0) {
			if (chan->user.oclass >= VOLTA_CHANNEL_GPFIFO_A) {
				chan->chid = args.volta.chid;
				chan->inst = args.volta.inst;
				chan->token = args.volta.token;
			} else
			if (chan->user.oclass >= KEPLER_CHANNEL_GPFIFO_A) {
				chan->chid = args.kepler.chid;
				chan->inst = args.kepler.inst;
			} else
			if (chan->user.oclass >= FERMI_CHANNEL_GPFIFO) {
				chan->chid = args.fermi.chid;
			} else {
				chan->chid = args.nv50.chid;
			}
			return ret;
		}
	} while (*oclass);

	nouveau_channel_del(pchan);
	return ret;
}

static int
nouveau_channel_dma(struct nouveau_drm *drm, struct nvif_device *device,
		    struct nouveau_channel **pchan)
{
	static const u16 oclasses[] = { NV40_CHANNEL_DMA,
					NV17_CHANNEL_DMA,
					NV10_CHANNEL_DMA,
					NV03_CHANNEL_DMA,
					0 };
	const u16 *oclass = oclasses;
	struct nv03_channel_dma_v0 args;
	struct nouveau_channel *chan;
	int ret;

	/* allocate dma push buffer */
	ret = nouveau_channel_prep(drm, device, 0x10000, &chan);
	*pchan = chan;
	if (ret)
		return ret;

	/* create channel object */
	args.version = 0;
	args.pushbuf = nvif_handle(&chan->push.ctxdma);
	args.offset = chan->push.addr;

	do {
		ret = nvif_object_ctor(&device->object, "abi16ChanUser", 0,
				       *oclass++, &args, sizeof(args),
				       &chan->user);
		if (ret == 0) {
			chan->chid = args.chid;
			return ret;
		}
	} while (ret && *oclass);

	nouveau_channel_del(pchan);
	return ret;
}

static int
nouveau_channel_init(struct nouveau_channel *chan, u32 vram, u32 gart)
{
	struct nvif_device *device = chan->device;
	struct nouveau_drm *drm = chan->drm;
	struct nv_dma_v0 args = {};
	int ret, i;

	nvif_object_map(&chan->user, NULL, 0);

	if (chan->user.oclass >= FERMI_CHANNEL_GPFIFO) {
		ret = nvif_notify_ctor(&chan->user, "abi16ChanKilled",
				       nouveau_channel_killed,
				       true, NV906F_V0_NTFY_KILLED,
				       NULL, 0, 0, &chan->kill);
		if (ret == 0)
			ret = nvif_notify_get(&chan->kill);
		if (ret) {
			NV_ERROR(drm, "Failed to request channel kill "
				      "notification: %d\n", ret);
			return ret;
		}
	}

	/* allocate dma objects to cover all allowed vram, and gart */
	if (device->info.family < NV_DEVICE_INFO_V0_FERMI) {
		if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
			args.target = NV_DMA_V0_TARGET_VM;
			args.access = NV_DMA_V0_ACCESS_VM;
			args.start = 0;
			args.limit = chan->vmm->vmm.limit - 1;
		} else {
			args.target = NV_DMA_V0_TARGET_VRAM;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = 0;
			args.limit = device->info.ram_user - 1;
		}

		ret = nvif_object_ctor(&chan->user, "abi16ChanVramCtxDma", vram,
				       NV_DMA_IN_MEMORY, &args, sizeof(args),
				       &chan->vram);
		if (ret)
			return ret;

		if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
			args.target = NV_DMA_V0_TARGET_VM;
			args.access = NV_DMA_V0_ACCESS_VM;
			args.start = 0;
			args.limit = chan->vmm->vmm.limit - 1;
		} else
		if (chan->drm->agp.bridge) {
			args.target = NV_DMA_V0_TARGET_AGP;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = chan->drm->agp.base;
			args.limit = chan->drm->agp.base +
				     chan->drm->agp.size - 1;
		} else {
			args.target = NV_DMA_V0_TARGET_VM;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = 0;
			args.limit = chan->vmm->vmm.limit - 1;
		}

		ret = nvif_object_ctor(&chan->user, "abi16ChanGartCtxDma", gart,
				       NV_DMA_IN_MEMORY, &args, sizeof(args),
				       &chan->gart);
		if (ret)
			return ret;
	}

	/* initialise dma tracking parameters */
	switch (chan->user.oclass & 0x00ff) {
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

	ret = PUSH_WAIT(chan->chan.push, NOUVEAU_DMA_SKIPS);
	if (ret)
		return ret;

	for (i = 0; i < NOUVEAU_DMA_SKIPS; i++)
		PUSH_DATA(chan->chan.push, 0x00000000);

	/* allocate software object class (used for fences on <= nv05) */
	if (device->info.family < NV_DEVICE_INFO_V0_CELSIUS) {
		ret = nvif_object_ctor(&chan->user, "abi16NvswFence", 0x006e,
				       NVIF_CLASS_SW_NV04,
				       NULL, 0, &chan->nvsw);
		if (ret)
			return ret;

		ret = PUSH_WAIT(chan->chan.push, 2);
		if (ret)
			return ret;

		PUSH_NVSQ(chan->chan.push, NV_SW, 0x0000, chan->nvsw.handle);
		PUSH_KICK(chan->chan.push);
	}

	/* initialise synchronisation */
	return nouveau_fence(chan->drm)->context_new(chan);
}

int
nouveau_channel_new(struct nouveau_drm *drm, struct nvif_device *device,
		    u32 arg0, u32 arg1, bool priv,
		    struct nouveau_channel **pchan)
{
	struct nouveau_cli *cli = (void *)device->object.client;
	bool super;
	int ret;

	/* hack until fencenv50 is fixed, and agp access relaxed */
	super = cli->base.super;
	cli->base.super = true;

	ret = nouveau_channel_ind(drm, device, arg0, priv, pchan);
	if (ret) {
		NV_PRINTK(dbg, cli, "ib channel create, %d\n", ret);
		ret = nouveau_channel_dma(drm, device, pchan);
		if (ret) {
			NV_PRINTK(dbg, cli, "dma channel create, %d\n", ret);
			goto done;
		}
	}

	ret = nouveau_channel_init(*pchan, arg0, arg1);
	if (ret) {
		NV_PRINTK(err, cli, "channel failed to initialise, %d\n", ret);
		nouveau_channel_del(pchan);
		goto done;
	}

	ret = nouveau_svmm_join((*pchan)->vmm->svmm, (*pchan)->inst);
	if (ret)
		nouveau_channel_del(pchan);

done:
	cli->base.super = super;
	return ret;
}

int
nouveau_channels_init(struct nouveau_drm *drm)
{
	struct {
		struct nv_device_info_v1 m;
		struct {
			struct nv_device_info_v1_data channels;
		} v;
	} args = {
		.m.version = 1,
		.m.count = sizeof(args.v) / sizeof(args.v.channels),
		.v.channels.mthd = NV_DEVICE_HOST_CHANNELS,
	};
	struct nvif_object *device = &drm->client.device.object;
	int ret;

	ret = nvif_object_mthd(device, NV_DEVICE_V0_INFO, &args, sizeof(args));
	if (ret || args.v.channels.mthd == NV_DEVICE_INFO_INVALID)
		return -ENODEV;

	drm->chan.nr = args.v.channels.data;
	drm->chan.context_base = dma_fence_context_alloc(drm->chan.nr);
	return 0;
}
