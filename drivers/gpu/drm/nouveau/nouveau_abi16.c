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
 */

#include <nvif/client.h>
#include <nvif/driver.h>
#include <nvif/ioctl.h>
#include <nvif/class.h>

#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nouveau_gem.h"
#include "nouveau_chan.h"
#include "nouveau_abi16.h"

struct nouveau_abi16 *
nouveau_abi16_get(struct drm_file *file_priv, struct drm_device *dev)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	mutex_lock(&cli->mutex);
	if (!cli->abi16) {
		struct nouveau_abi16 *abi16;
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
			if (nvif_device_init(&cli->base.base, NULL,
					     NOUVEAU_ABI16_DEVICE, NV_DEVICE,
					     &args, sizeof(args),
					     &abi16->device) == 0)
				return cli->abi16;

			kfree(cli->abi16);
			cli->abi16 = NULL;
		}

		mutex_unlock(&cli->mutex);
	}
	return cli->abi16;
}

int
nouveau_abi16_put(struct nouveau_abi16 *abi16, int ret)
{
	struct nouveau_cli *cli = (void *)nvif_client(&abi16->device.base);
	mutex_unlock(&cli->mutex);
	return ret;
}

u16
nouveau_abi16_swclass(struct nouveau_drm *drm)
{
	switch (drm->device.info.family) {
	case NV_DEVICE_INFO_V0_TNT:
		return 0x006e;
	case NV_DEVICE_INFO_V0_CELSIUS:
	case NV_DEVICE_INFO_V0_KELVIN:
	case NV_DEVICE_INFO_V0_RANKINE:
	case NV_DEVICE_INFO_V0_CURIE:
		return 0x016e;
	case NV_DEVICE_INFO_V0_TESLA:
		return 0x506e;
	case NV_DEVICE_INFO_V0_FERMI:
	case NV_DEVICE_INFO_V0_KEPLER:
	case NV_DEVICE_INFO_V0_MAXWELL:
		return 0x906e;
	}

	return 0x0000;
}

static void
nouveau_abi16_ntfy_fini(struct nouveau_abi16_chan *chan,
			struct nouveau_abi16_ntfy *ntfy)
{
	nvkm_mm_free(&chan->heap, &ntfy->node);
	list_del(&ntfy->head);
	kfree(ntfy);
}

static void
nouveau_abi16_chan_fini(struct nouveau_abi16 *abi16,
			struct nouveau_abi16_chan *chan)
{
	struct nouveau_abi16_ntfy *ntfy, *temp;

	/* wait for all activity to stop before releasing notify object, which
	 * may be still in use */
	if (chan->chan && chan->ntfy)
		nouveau_channel_idle(chan->chan);

	/* cleanup notifier state */
	list_for_each_entry_safe(ntfy, temp, &chan->notifiers, head) {
		nouveau_abi16_ntfy_fini(chan, ntfy);
	}

	if (chan->ntfy) {
		nouveau_bo_vma_del(chan->ntfy, &chan->ntfy_vma);
		nouveau_bo_unpin(chan->ntfy);
		drm_gem_object_unreference_unlocked(&chan->ntfy->gem);
	}

	if (chan->heap.block_size)
		nvkm_mm_fini(&chan->heap);

	/* destroy channel object, all children will be killed too */
	if (chan->chan) {
		abi16->handles &= ~(1ULL << (chan->chan->object->handle & 0xffff));
		nouveau_channel_del(&chan->chan);
	}

	list_del(&chan->head);
	kfree(chan);
}

void
nouveau_abi16_fini(struct nouveau_abi16 *abi16)
{
	struct nouveau_cli *cli = (void *)nvif_client(&abi16->device.base);
	struct nouveau_abi16_chan *chan, *temp;

	/* cleanup channels */
	list_for_each_entry_safe(chan, temp, &abi16->channels, head) {
		nouveau_abi16_chan_fini(abi16, chan);
	}

	/* destroy the device object */
	nvif_device_fini(&abi16->device);

	kfree(cli->abi16);
	cli->abi16 = NULL;
}

int
nouveau_abi16_ioctl_getparam(ABI16_IOCTL_ARGS)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvif_device *device = &drm->device;
	struct nvkm_timer *ptimer = nvxx_timer(device);
	struct nvkm_gr *gr = nvxx_gr(device);
	struct drm_nouveau_getparam *getparam = data;

	switch (getparam->param) {
	case NOUVEAU_GETPARAM_CHIPSET_ID:
		getparam->value = device->info.chipset;
		break;
	case NOUVEAU_GETPARAM_PCI_VENDOR:
		if (nv_device_is_pci(nvxx_device(device)))
			getparam->value = dev->pdev->vendor;
		else
			getparam->value = 0;
		break;
	case NOUVEAU_GETPARAM_PCI_DEVICE:
		if (nv_device_is_pci(nvxx_device(device)))
			getparam->value = dev->pdev->device;
		else
			getparam->value = 0;
		break;
	case NOUVEAU_GETPARAM_BUS_TYPE:
		if (!nv_device_is_pci(nvxx_device(device)))
			getparam->value = 3;
		else
		if (drm_pci_device_is_agp(dev))
			getparam->value = 0;
		else
		if (!pci_is_pcie(dev->pdev))
			getparam->value = 1;
		else
			getparam->value = 2;
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
		getparam->value = ptimer->read(ptimer);
		break;
	case NOUVEAU_GETPARAM_HAS_BO_USAGE:
		getparam->value = 1;
		break;
	case NOUVEAU_GETPARAM_HAS_PAGEFLIP:
		getparam->value = 1;
		break;
	case NOUVEAU_GETPARAM_GRAPH_UNITS:
		getparam->value = gr->units ? gr->units(gr) : 0;
		break;
	default:
		NV_PRINTK(debug, cli, "unknown parameter %lld\n", getparam->param);
		return -EINVAL;
	}

	return 0;
}

int
nouveau_abi16_ioctl_setparam(ABI16_IOCTL_ARGS)
{
	return -EINVAL;
}

int
nouveau_abi16_ioctl_channel_alloc(ABI16_IOCTL_ARGS)
{
	struct drm_nouveau_channel_alloc *init = data;
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_abi16 *abi16 = nouveau_abi16_get(file_priv, dev);
	struct nouveau_abi16_chan *chan;
	struct nvif_device *device;
	int ret;

	if (unlikely(!abi16))
		return -ENOMEM;

	if (!drm->channel)
		return nouveau_abi16_put(abi16, -ENODEV);

	device = &abi16->device;

	/* hack to allow channel engine type specification on kepler */
	if (device->info.family >= NV_DEVICE_INFO_V0_KEPLER) {
		if (init->fb_ctxdma_handle != ~0)
			init->fb_ctxdma_handle = KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_GR;
		else
			init->fb_ctxdma_handle = init->tt_ctxdma_handle;

		/* allow flips to be executed if this is a graphics channel */
		init->tt_ctxdma_handle = 0;
		if (init->fb_ctxdma_handle == KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_GR)
			init->tt_ctxdma_handle = 1;
	}

	if (init->fb_ctxdma_handle == ~0 || init->tt_ctxdma_handle == ~0)
		return nouveau_abi16_put(abi16, -EINVAL);

	/* allocate "abi16 channel" data and make up a handle for it */
	init->channel = __ffs64(~abi16->handles);
	if (~abi16->handles == 0)
		return nouveau_abi16_put(abi16, -ENOSPC);

	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return nouveau_abi16_put(abi16, -ENOMEM);

	INIT_LIST_HEAD(&chan->notifiers);
	list_add(&chan->head, &abi16->channels);
	abi16->handles |= (1ULL << init->channel);

	/* create channel object and initialise dma and fence management */
	ret = nouveau_channel_new(drm, device,
				  NOUVEAU_ABI16_CHAN(init->channel),
				  init->fb_ctxdma_handle,
				  init->tt_ctxdma_handle, &chan->chan);
	if (ret)
		goto done;

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
	ret = nouveau_gem_new(dev, PAGE_SIZE, 0, NOUVEAU_GEM_DOMAIN_GART,
			      0, 0, &chan->ntfy);
	if (ret == 0)
		ret = nouveau_bo_pin(chan->ntfy, TTM_PL_FLAG_TT, false);
	if (ret)
		goto done;

	if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
		ret = nouveau_bo_vma_add(chan->ntfy, cli->vm,
					&chan->ntfy_vma);
		if (ret)
			goto done;
	}

	ret = drm_gem_handle_create(file_priv, &chan->ntfy->gem,
				    &init->notifier_handle);
	if (ret)
		goto done;

	ret = nvkm_mm_init(&chan->heap, 0, PAGE_SIZE, 1);
done:
	if (ret)
		nouveau_abi16_chan_fini(abi16, chan);
	return nouveau_abi16_put(abi16, ret);
}

static struct nouveau_abi16_chan *
nouveau_abi16_chan(struct nouveau_abi16 *abi16, int channel)
{
	struct nouveau_abi16_chan *chan;

	list_for_each_entry(chan, &abi16->channels, head) {
		if (chan->chan->object->handle == NOUVEAU_ABI16_CHAN(channel))
			return chan;
	}

	return NULL;
}

int
nouveau_abi16_ioctl_channel_free(ABI16_IOCTL_ARGS)
{
	struct drm_nouveau_channel_free *req = data;
	struct nouveau_abi16 *abi16 = nouveau_abi16_get(file_priv, dev);
	struct nouveau_abi16_chan *chan;

	if (unlikely(!abi16))
		return -ENOMEM;

	chan = nouveau_abi16_chan(abi16, req->channel);
	if (!chan)
		return nouveau_abi16_put(abi16, -ENOENT);
	nouveau_abi16_chan_fini(abi16, chan);
	return nouveau_abi16_put(abi16, 0);
}

int
nouveau_abi16_ioctl_grobj_alloc(ABI16_IOCTL_ARGS)
{
	struct drm_nouveau_grobj_alloc *init = data;
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_new_v0 new;
	} args = {
		.ioctl.owner = NVIF_IOCTL_V0_OWNER_ANY,
		.ioctl.type = NVIF_IOCTL_V0_NEW,
		.ioctl.path_nr = 3,
		.ioctl.path[2] = NOUVEAU_ABI16_CLIENT,
		.ioctl.path[1] = NOUVEAU_ABI16_DEVICE,
		.ioctl.path[0] = NOUVEAU_ABI16_CHAN(init->channel),
		.new.route = NVDRM_OBJECT_ABI16,
		.new.handle = init->handle,
		.new.oclass = init->class,
	};
	struct nouveau_abi16 *abi16 = nouveau_abi16_get(file_priv, dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvif_client *client;
	int ret;

	if (unlikely(!abi16))
		return -ENOMEM;

	if (init->handle == ~0)
		return nouveau_abi16_put(abi16, -EINVAL);
	client = nvif_client(nvif_object(&abi16->device));

	/* compatibility with userspace that assumes 506e for all chipsets */
	if (init->class == 0x506e) {
		init->class = nouveau_abi16_swclass(drm);
		if (init->class == 0x906e)
			return nouveau_abi16_put(abi16, 0);
	}

	ret = nvif_client_ioctl(client, &args, sizeof(args));
	return nouveau_abi16_put(abi16, ret);
}

int
nouveau_abi16_ioctl_notifierobj_alloc(ABI16_IOCTL_ARGS)
{
	struct drm_nouveau_notifierobj_alloc *info = data;
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_new_v0 new;
		struct nv_dma_v0 ctxdma;
	} args = {
		.ioctl.owner = NVIF_IOCTL_V0_OWNER_ANY,
		.ioctl.type = NVIF_IOCTL_V0_NEW,
		.ioctl.path_nr = 3,
		.ioctl.path[2] = NOUVEAU_ABI16_CLIENT,
		.ioctl.path[1] = NOUVEAU_ABI16_DEVICE,
		.ioctl.path[0] = NOUVEAU_ABI16_CHAN(info->channel),
		.new.route = NVDRM_OBJECT_ABI16,
		.new.handle = info->handle,
		.new.oclass = NV_DMA_IN_MEMORY,
	};
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_abi16 *abi16 = nouveau_abi16_get(file_priv, dev);
	struct nouveau_abi16_chan *chan;
	struct nouveau_abi16_ntfy *ntfy;
	struct nvif_device *device = &abi16->device;
	struct nvif_client *client;
	int ret;

	if (unlikely(!abi16))
		return -ENOMEM;

	/* completely unnecessary for these chipsets... */
	if (unlikely(device->info.family >= NV_DEVICE_INFO_V0_FERMI))
		return nouveau_abi16_put(abi16, -EINVAL);
	client = nvif_client(nvif_object(&abi16->device));

	chan = nouveau_abi16_chan(abi16, info->channel);
	if (!chan)
		return nouveau_abi16_put(abi16, -ENOENT);

	ntfy = kzalloc(sizeof(*ntfy), GFP_KERNEL);
	if (!ntfy)
		return nouveau_abi16_put(abi16, -ENOMEM);

	list_add(&ntfy->head, &chan->notifiers);
	ntfy->handle = info->handle;

	ret = nvkm_mm_head(&chan->heap, 0, 1, info->size, info->size, 1,
			   &ntfy->node);
	if (ret)
		goto done;

	args.ctxdma.start = ntfy->node->offset;
	args.ctxdma.limit = ntfy->node->offset + ntfy->node->length - 1;
	if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
		args.ctxdma.target = NV_DMA_V0_TARGET_VM;
		args.ctxdma.access = NV_DMA_V0_ACCESS_VM;
		args.ctxdma.start += chan->ntfy_vma.offset;
		args.ctxdma.limit += chan->ntfy_vma.offset;
	} else
	if (drm->agp.stat == ENABLED) {
		args.ctxdma.target = NV_DMA_V0_TARGET_AGP;
		args.ctxdma.access = NV_DMA_V0_ACCESS_RDWR;
		args.ctxdma.start += drm->agp.base + chan->ntfy->bo.offset;
		args.ctxdma.limit += drm->agp.base + chan->ntfy->bo.offset;
		client->super = true;
	} else {
		args.ctxdma.target = NV_DMA_V0_TARGET_VM;
		args.ctxdma.access = NV_DMA_V0_ACCESS_RDWR;
		args.ctxdma.start += chan->ntfy->bo.offset;
		args.ctxdma.limit += chan->ntfy->bo.offset;
	}

	ret = nvif_client_ioctl(client, &args, sizeof(args));
	client->super = false;
	if (ret)
		goto done;

	info->offset = ntfy->node->offset;

done:
	if (ret)
		nouveau_abi16_ntfy_fini(chan, ntfy);
	return nouveau_abi16_put(abi16, ret);
}

int
nouveau_abi16_ioctl_gpuobj_free(ABI16_IOCTL_ARGS)
{
	struct drm_nouveau_gpuobj_free *fini = data;
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_del del;
	} args = {
		.ioctl.owner = NVDRM_OBJECT_ABI16,
		.ioctl.type = NVIF_IOCTL_V0_DEL,
		.ioctl.path_nr = 4,
		.ioctl.path[3] = NOUVEAU_ABI16_CLIENT,
		.ioctl.path[2] = NOUVEAU_ABI16_DEVICE,
		.ioctl.path[1] = NOUVEAU_ABI16_CHAN(fini->channel),
		.ioctl.path[0] = fini->handle,
	};
	struct nouveau_abi16 *abi16 = nouveau_abi16_get(file_priv, dev);
	struct nouveau_abi16_chan *chan;
	struct nouveau_abi16_ntfy *ntfy;
	struct nvif_client *client;
	int ret;

	if (unlikely(!abi16))
		return -ENOMEM;

	chan = nouveau_abi16_chan(abi16, fini->channel);
	if (!chan)
		return nouveau_abi16_put(abi16, -ENOENT);
	client = nvif_client(nvif_object(&abi16->device));

	/* synchronize with the user channel and destroy the gpu object */
	nouveau_channel_idle(chan->chan);

	ret = nvif_client_ioctl(client, &args, sizeof(args));
	if (ret)
		return nouveau_abi16_put(abi16, ret);

	/* cleanup extra state if this object was a notifier */
	list_for_each_entry(ntfy, &chan->notifiers, head) {
		if (ntfy->handle == fini->handle) {
			nvkm_mm_free(&chan->heap, &ntfy->node);
			list_del(&ntfy->head);
			break;
		}
	}

	return nouveau_abi16_put(abi16, 0);
}
