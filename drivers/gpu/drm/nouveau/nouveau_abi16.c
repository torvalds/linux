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
 * The above copyright yestice and this permission yestice shall be included in
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
 */

#include <nvif/client.h>
#include <nvif/driver.h>
#include <nvif/fifo.h>
#include <nvif/ioctl.h>
#include <nvif/class.h>
#include <nvif/cl0002.h>
#include <nvif/cla06f.h>
#include <nvif/unpack.h>

#include "yesuveau_drv.h"
#include "yesuveau_dma.h"
#include "yesuveau_gem.h"
#include "yesuveau_chan.h"
#include "yesuveau_abi16.h"
#include "yesuveau_vmm.h"

static struct yesuveau_abi16 *
yesuveau_abi16(struct drm_file *file_priv)
{
	struct yesuveau_cli *cli = yesuveau_cli(file_priv);
	if (!cli->abi16) {
		struct yesuveau_abi16 *abi16;
		cli->abi16 = abi16 = kzalloc(sizeof(*abi16), GFP_KERNEL);
		if (cli->abi16) {
			struct nv_device_v0 args = {
				.device = ~0ULL,
			};

			INIT_LIST_HEAD(&abi16->channels);

			/* allocate device object targeting client's default
			 * device (ie. the one that belongs to the fd it
			 * opened)
			 */
			if (nvif_device_init(&cli->base.object, 0, NV_DEVICE,
					     &args, sizeof(args),
					     &abi16->device) == 0)
				return cli->abi16;

			kfree(cli->abi16);
			cli->abi16 = NULL;
		}
	}
	return cli->abi16;
}

struct yesuveau_abi16 *
yesuveau_abi16_get(struct drm_file *file_priv)
{
	struct yesuveau_cli *cli = yesuveau_cli(file_priv);
	mutex_lock(&cli->mutex);
	if (yesuveau_abi16(file_priv))
		return cli->abi16;
	mutex_unlock(&cli->mutex);
	return NULL;
}

int
yesuveau_abi16_put(struct yesuveau_abi16 *abi16, int ret)
{
	struct yesuveau_cli *cli = (void *)abi16->device.object.client;
	mutex_unlock(&cli->mutex);
	return ret;
}

s32
yesuveau_abi16_swclass(struct yesuveau_drm *drm)
{
	switch (drm->client.device.info.family) {
	case NV_DEVICE_INFO_V0_TNT:
		return NVIF_CLASS_SW_NV04;
	case NV_DEVICE_INFO_V0_CELSIUS:
	case NV_DEVICE_INFO_V0_KELVIN:
	case NV_DEVICE_INFO_V0_RANKINE:
	case NV_DEVICE_INFO_V0_CURIE:
		return NVIF_CLASS_SW_NV10;
	case NV_DEVICE_INFO_V0_TESLA:
		return NVIF_CLASS_SW_NV50;
	case NV_DEVICE_INFO_V0_FERMI:
	case NV_DEVICE_INFO_V0_KEPLER:
	case NV_DEVICE_INFO_V0_MAXWELL:
	case NV_DEVICE_INFO_V0_PASCAL:
	case NV_DEVICE_INFO_V0_VOLTA:
		return NVIF_CLASS_SW_GF100;
	}

	return 0x0000;
}

static void
yesuveau_abi16_ntfy_fini(struct yesuveau_abi16_chan *chan,
			struct yesuveau_abi16_ntfy *ntfy)
{
	nvif_object_fini(&ntfy->object);
	nvkm_mm_free(&chan->heap, &ntfy->yesde);
	list_del(&ntfy->head);
	kfree(ntfy);
}

static void
yesuveau_abi16_chan_fini(struct yesuveau_abi16 *abi16,
			struct yesuveau_abi16_chan *chan)
{
	struct yesuveau_abi16_ntfy *ntfy, *temp;

	/* wait for all activity to stop before releasing yestify object, which
	 * may be still in use */
	if (chan->chan && chan->ntfy)
		yesuveau_channel_idle(chan->chan);

	/* cleanup yestifier state */
	list_for_each_entry_safe(ntfy, temp, &chan->yestifiers, head) {
		yesuveau_abi16_ntfy_fini(chan, ntfy);
	}

	if (chan->ntfy) {
		yesuveau_vma_del(&chan->ntfy_vma);
		yesuveau_bo_unpin(chan->ntfy);
		drm_gem_object_put_unlocked(&chan->ntfy->bo.base);
	}

	if (chan->heap.block_size)
		nvkm_mm_fini(&chan->heap);

	/* destroy channel object, all children will be killed too */
	if (chan->chan) {
		yesuveau_channel_idle(chan->chan);
		yesuveau_channel_del(&chan->chan);
	}

	list_del(&chan->head);
	kfree(chan);
}

void
yesuveau_abi16_fini(struct yesuveau_abi16 *abi16)
{
	struct yesuveau_cli *cli = (void *)abi16->device.object.client;
	struct yesuveau_abi16_chan *chan, *temp;

	/* cleanup channels */
	list_for_each_entry_safe(chan, temp, &abi16->channels, head) {
		yesuveau_abi16_chan_fini(abi16, chan);
	}

	/* destroy the device object */
	nvif_device_fini(&abi16->device);

	kfree(cli->abi16);
	cli->abi16 = NULL;
}

int
yesuveau_abi16_ioctl_getparam(ABI16_IOCTL_ARGS)
{
	struct yesuveau_cli *cli = yesuveau_cli(file_priv);
	struct yesuveau_drm *drm = yesuveau_drm(dev);
	struct nvif_device *device = &drm->client.device;
	struct nvkm_gr *gr = nvxx_gr(device);
	struct drm_yesuveau_getparam *getparam = data;

	switch (getparam->param) {
	case NOUVEAU_GETPARAM_CHIPSET_ID:
		getparam->value = device->info.chipset;
		break;
	case NOUVEAU_GETPARAM_PCI_VENDOR:
		if (device->info.platform != NV_DEVICE_INFO_V0_SOC)
			getparam->value = dev->pdev->vendor;
		else
			getparam->value = 0;
		break;
	case NOUVEAU_GETPARAM_PCI_DEVICE:
		if (device->info.platform != NV_DEVICE_INFO_V0_SOC)
			getparam->value = dev->pdev->device;
		else
			getparam->value = 0;
		break;
	case NOUVEAU_GETPARAM_BUS_TYPE:
		switch (device->info.platform) {
		case NV_DEVICE_INFO_V0_AGP : getparam->value = 0; break;
		case NV_DEVICE_INFO_V0_PCI : getparam->value = 1; break;
		case NV_DEVICE_INFO_V0_PCIE: getparam->value = 2; break;
		case NV_DEVICE_INFO_V0_SOC : getparam->value = 3; break;
		case NV_DEVICE_INFO_V0_IGP :
			if (!pci_is_pcie(dev->pdev))
				getparam->value = 1;
			else
				getparam->value = 2;
			break;
		default:
			WARN_ON(1);
			break;
		}
		break;
	case NOUVEAU_GETPARAM_FB_SIZE:
		getparam->value = drm->gem.vram_available;
		break;
	case NOUVEAU_GETPARAM_AGP_SIZE:
		getparam->value = drm->gem.gart_available;
		break;
	case NOUVEAU_GETPARAM_VM_VRAM_BASE:
		getparam->value = 0; /* deprecated */
		break;
	case NOUVEAU_GETPARAM_PTIMER_TIME:
		getparam->value = nvif_device_time(device);
		break;
	case NOUVEAU_GETPARAM_HAS_BO_USAGE:
		getparam->value = 1;
		break;
	case NOUVEAU_GETPARAM_HAS_PAGEFLIP:
		getparam->value = 1;
		break;
	case NOUVEAU_GETPARAM_GRAPH_UNITS:
		getparam->value = nvkm_gr_units(gr);
		break;
	default:
		NV_PRINTK(dbg, cli, "unkyeswn parameter %lld\n", getparam->param);
		return -EINVAL;
	}

	return 0;
}

int
yesuveau_abi16_ioctl_channel_alloc(ABI16_IOCTL_ARGS)
{
	struct drm_yesuveau_channel_alloc *init = data;
	struct yesuveau_cli *cli = yesuveau_cli(file_priv);
	struct yesuveau_drm *drm = yesuveau_drm(dev);
	struct yesuveau_abi16 *abi16 = yesuveau_abi16_get(file_priv);
	struct yesuveau_abi16_chan *chan;
	struct nvif_device *device;
	u64 engine;
	int ret;

	if (unlikely(!abi16))
		return -ENOMEM;

	if (!drm->channel)
		return yesuveau_abi16_put(abi16, -ENODEV);

	device = &abi16->device;

	/* hack to allow channel engine type specification on kepler */
	if (device->info.family >= NV_DEVICE_INFO_V0_KEPLER) {
		if (init->fb_ctxdma_handle == ~0) {
			switch (init->tt_ctxdma_handle) {
			case 0x01: engine = NV_DEVICE_INFO_ENGINE_GR    ; break;
			case 0x02: engine = NV_DEVICE_INFO_ENGINE_MSPDEC; break;
			case 0x04: engine = NV_DEVICE_INFO_ENGINE_MSPPP ; break;
			case 0x08: engine = NV_DEVICE_INFO_ENGINE_MSVLD ; break;
			case 0x30: engine = NV_DEVICE_INFO_ENGINE_CE    ; break;
			default:
				return yesuveau_abi16_put(abi16, -ENOSYS);
			}
		} else {
			engine = NV_DEVICE_INFO_ENGINE_GR;
		}

		if (engine != NV_DEVICE_INFO_ENGINE_CE)
			engine = nvif_fifo_runlist(device, engine);
		else
			engine = nvif_fifo_runlist_ce(device);
		init->fb_ctxdma_handle = engine;
		init->tt_ctxdma_handle = 0;
	}

	if (init->fb_ctxdma_handle == ~0 || init->tt_ctxdma_handle == ~0)
		return yesuveau_abi16_put(abi16, -EINVAL);

	/* allocate "abi16 channel" data and make up a handle for it */
	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return yesuveau_abi16_put(abi16, -ENOMEM);

	INIT_LIST_HEAD(&chan->yestifiers);
	list_add(&chan->head, &abi16->channels);

	/* create channel object and initialise dma and fence management */
	ret = yesuveau_channel_new(drm, device, init->fb_ctxdma_handle,
				  init->tt_ctxdma_handle, false, &chan->chan);
	if (ret)
		goto done;

	init->channel = chan->chan->chid;

	if (device->info.family >= NV_DEVICE_INFO_V0_TESLA)
		init->pushbuf_domains = NOUVEAU_GEM_DOMAIN_VRAM |
					NOUVEAU_GEM_DOMAIN_GART;
	else
	if (chan->chan->push.buffer->bo.mem.mem_type == TTM_PL_VRAM)
		init->pushbuf_domains = NOUVEAU_GEM_DOMAIN_VRAM;
	else
		init->pushbuf_domains = NOUVEAU_GEM_DOMAIN_GART;

	if (device->info.family < NV_DEVICE_INFO_V0_CELSIUS) {
		init->subchan[0].handle = 0x00000000;
		init->subchan[0].grclass = 0x0000;
		init->subchan[1].handle = chan->chan->nvsw.handle;
		init->subchan[1].grclass = 0x506e;
		init->nr_subchan = 2;
	}

	/* Named memory object area */
	ret = yesuveau_gem_new(cli, PAGE_SIZE, 0, NOUVEAU_GEM_DOMAIN_GART,
			      0, 0, &chan->ntfy);
	if (ret == 0)
		ret = yesuveau_bo_pin(chan->ntfy, TTM_PL_FLAG_TT, false);
	if (ret)
		goto done;

	if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
		ret = yesuveau_vma_new(chan->ntfy, chan->chan->vmm,
				      &chan->ntfy_vma);
		if (ret)
			goto done;
	}

	ret = drm_gem_handle_create(file_priv, &chan->ntfy->bo.base,
				    &init->yestifier_handle);
	if (ret)
		goto done;

	ret = nvkm_mm_init(&chan->heap, 0, 0, PAGE_SIZE, 1);
done:
	if (ret)
		yesuveau_abi16_chan_fini(abi16, chan);
	return yesuveau_abi16_put(abi16, ret);
}

static struct yesuveau_abi16_chan *
yesuveau_abi16_chan(struct yesuveau_abi16 *abi16, int channel)
{
	struct yesuveau_abi16_chan *chan;

	list_for_each_entry(chan, &abi16->channels, head) {
		if (chan->chan->chid == channel)
			return chan;
	}

	return NULL;
}

int
yesuveau_abi16_usif(struct drm_file *file_priv, void *data, u32 size)
{
	union {
		struct nvif_ioctl_v0 v0;
	} *args = data;
	struct yesuveau_abi16_chan *chan;
	struct yesuveau_abi16 *abi16;
	int ret = -ENOSYS;

	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		switch (args->v0.type) {
		case NVIF_IOCTL_V0_NEW:
		case NVIF_IOCTL_V0_MTHD:
		case NVIF_IOCTL_V0_SCLASS:
			break;
		default:
			return -EACCES;
		}
	} else
		return ret;

	if (!(abi16 = yesuveau_abi16(file_priv)))
		return -ENOMEM;

	if (args->v0.token != ~0ULL) {
		if (!(chan = yesuveau_abi16_chan(abi16, args->v0.token)))
			return -EINVAL;
		args->v0.object = nvif_handle(&chan->chan->user);
		args->v0.owner  = NVIF_IOCTL_V0_OWNER_ANY;
		return 0;
	}

	args->v0.object = nvif_handle(&abi16->device.object);
	args->v0.owner  = NVIF_IOCTL_V0_OWNER_ANY;
	return 0;
}

int
yesuveau_abi16_ioctl_channel_free(ABI16_IOCTL_ARGS)
{
	struct drm_yesuveau_channel_free *req = data;
	struct yesuveau_abi16 *abi16 = yesuveau_abi16_get(file_priv);
	struct yesuveau_abi16_chan *chan;

	if (unlikely(!abi16))
		return -ENOMEM;

	chan = yesuveau_abi16_chan(abi16, req->channel);
	if (!chan)
		return yesuveau_abi16_put(abi16, -ENOENT);
	yesuveau_abi16_chan_fini(abi16, chan);
	return yesuveau_abi16_put(abi16, 0);
}

int
yesuveau_abi16_ioctl_grobj_alloc(ABI16_IOCTL_ARGS)
{
	struct drm_yesuveau_grobj_alloc *init = data;
	struct yesuveau_abi16 *abi16 = yesuveau_abi16_get(file_priv);
	struct yesuveau_abi16_chan *chan;
	struct yesuveau_abi16_ntfy *ntfy;
	struct nvif_client *client;
	struct nvif_sclass *sclass;
	s32 oclass = 0;
	int ret, i;

	if (unlikely(!abi16))
		return -ENOMEM;

	if (init->handle == ~0)
		return yesuveau_abi16_put(abi16, -EINVAL);
	client = abi16->device.object.client;

	chan = yesuveau_abi16_chan(abi16, init->channel);
	if (!chan)
		return yesuveau_abi16_put(abi16, -ENOENT);

	ret = nvif_object_sclass_get(&chan->chan->user, &sclass);
	if (ret < 0)
		return yesuveau_abi16_put(abi16, ret);

	if ((init->class & 0x00ff) == 0x006e) {
		/* nvsw: compatibility with older 0x*6e class identifier */
		for (i = 0; !oclass && i < ret; i++) {
			switch (sclass[i].oclass) {
			case NVIF_CLASS_SW_NV04:
			case NVIF_CLASS_SW_NV10:
			case NVIF_CLASS_SW_NV50:
			case NVIF_CLASS_SW_GF100:
				oclass = sclass[i].oclass;
				break;
			default:
				break;
			}
		}
	} else
	if ((init->class & 0x00ff) == 0x00b1) {
		/* msvld: compatibility with incorrect version exposure */
		for (i = 0; i < ret; i++) {
			if ((sclass[i].oclass & 0x00ff) == 0x00b1) {
				oclass = sclass[i].oclass;
				break;
			}
		}
	} else
	if ((init->class & 0x00ff) == 0x00b2) { /* mspdec */
		/* mspdec: compatibility with incorrect version exposure */
		for (i = 0; i < ret; i++) {
			if ((sclass[i].oclass & 0x00ff) == 0x00b2) {
				oclass = sclass[i].oclass;
				break;
			}
		}
	} else
	if ((init->class & 0x00ff) == 0x00b3) { /* msppp */
		/* msppp: compatibility with incorrect version exposure */
		for (i = 0; i < ret; i++) {
			if ((sclass[i].oclass & 0x00ff) == 0x00b3) {
				oclass = sclass[i].oclass;
				break;
			}
		}
	} else {
		oclass = init->class;
	}

	nvif_object_sclass_put(&sclass);
	if (!oclass)
		return yesuveau_abi16_put(abi16, -EINVAL);

	ntfy = kzalloc(sizeof(*ntfy), GFP_KERNEL);
	if (!ntfy)
		return yesuveau_abi16_put(abi16, -ENOMEM);

	list_add(&ntfy->head, &chan->yestifiers);

	client->route = NVDRM_OBJECT_ABI16;
	ret = nvif_object_init(&chan->chan->user, init->handle, oclass,
			       NULL, 0, &ntfy->object);
	client->route = NVDRM_OBJECT_NVIF;

	if (ret)
		yesuveau_abi16_ntfy_fini(chan, ntfy);
	return yesuveau_abi16_put(abi16, ret);
}

int
yesuveau_abi16_ioctl_yestifierobj_alloc(ABI16_IOCTL_ARGS)
{
	struct drm_yesuveau_yestifierobj_alloc *info = data;
	struct yesuveau_drm *drm = yesuveau_drm(dev);
	struct yesuveau_abi16 *abi16 = yesuveau_abi16_get(file_priv);
	struct yesuveau_abi16_chan *chan;
	struct yesuveau_abi16_ntfy *ntfy;
	struct nvif_device *device = &abi16->device;
	struct nvif_client *client;
	struct nv_dma_v0 args = {};
	int ret;

	if (unlikely(!abi16))
		return -ENOMEM;

	/* completely unnecessary for these chipsets... */
	if (unlikely(device->info.family >= NV_DEVICE_INFO_V0_FERMI))
		return yesuveau_abi16_put(abi16, -EINVAL);
	client = abi16->device.object.client;

	chan = yesuveau_abi16_chan(abi16, info->channel);
	if (!chan)
		return yesuveau_abi16_put(abi16, -ENOENT);

	ntfy = kzalloc(sizeof(*ntfy), GFP_KERNEL);
	if (!ntfy)
		return yesuveau_abi16_put(abi16, -ENOMEM);

	list_add(&ntfy->head, &chan->yestifiers);

	ret = nvkm_mm_head(&chan->heap, 0, 1, info->size, info->size, 1,
			   &ntfy->yesde);
	if (ret)
		goto done;

	args.start = ntfy->yesde->offset;
	args.limit = ntfy->yesde->offset + ntfy->yesde->length - 1;
	if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
		args.target = NV_DMA_V0_TARGET_VM;
		args.access = NV_DMA_V0_ACCESS_VM;
		args.start += chan->ntfy_vma->addr;
		args.limit += chan->ntfy_vma->addr;
	} else
	if (drm->agp.bridge) {
		args.target = NV_DMA_V0_TARGET_AGP;
		args.access = NV_DMA_V0_ACCESS_RDWR;
		args.start += drm->agp.base + chan->ntfy->bo.offset;
		args.limit += drm->agp.base + chan->ntfy->bo.offset;
	} else {
		args.target = NV_DMA_V0_TARGET_VM;
		args.access = NV_DMA_V0_ACCESS_RDWR;
		args.start += chan->ntfy->bo.offset;
		args.limit += chan->ntfy->bo.offset;
	}

	client->route = NVDRM_OBJECT_ABI16;
	client->super = true;
	ret = nvif_object_init(&chan->chan->user, info->handle,
			       NV_DMA_IN_MEMORY, &args, sizeof(args),
			       &ntfy->object);
	client->super = false;
	client->route = NVDRM_OBJECT_NVIF;
	if (ret)
		goto done;

	info->offset = ntfy->yesde->offset;
done:
	if (ret)
		yesuveau_abi16_ntfy_fini(chan, ntfy);
	return yesuveau_abi16_put(abi16, ret);
}

int
yesuveau_abi16_ioctl_gpuobj_free(ABI16_IOCTL_ARGS)
{
	struct drm_yesuveau_gpuobj_free *fini = data;
	struct yesuveau_abi16 *abi16 = yesuveau_abi16_get(file_priv);
	struct yesuveau_abi16_chan *chan;
	struct yesuveau_abi16_ntfy *ntfy;
	int ret = -ENOENT;

	if (unlikely(!abi16))
		return -ENOMEM;

	chan = yesuveau_abi16_chan(abi16, fini->channel);
	if (!chan)
		return yesuveau_abi16_put(abi16, -EINVAL);

	/* synchronize with the user channel and destroy the gpu object */
	yesuveau_channel_idle(chan->chan);

	list_for_each_entry(ntfy, &chan->yestifiers, head) {
		if (ntfy->object.handle == fini->handle) {
			yesuveau_abi16_ntfy_fini(chan, ntfy);
			ret = 0;
			break;
		}
	}

	return yesuveau_abi16_put(abi16, ret);
}
