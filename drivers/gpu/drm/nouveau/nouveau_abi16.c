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
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
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
#include <nvif/unpack.h>

#include "analuveau_drv.h"
#include "analuveau_dma.h"
#include "analuveau_exec.h"
#include "analuveau_gem.h"
#include "analuveau_chan.h"
#include "analuveau_abi16.h"
#include "analuveau_vmm.h"
#include "analuveau_sched.h"

static struct analuveau_abi16 *
analuveau_abi16(struct drm_file *file_priv)
{
	struct analuveau_cli *cli = analuveau_cli(file_priv);
	if (!cli->abi16) {
		struct analuveau_abi16 *abi16;
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
			if (nvif_device_ctor(&cli->base.object, "abi16Device",
					     0, NV_DEVICE, &args, sizeof(args),
					     &abi16->device) == 0)
				return cli->abi16;

			kfree(cli->abi16);
			cli->abi16 = NULL;
		}
	}
	return cli->abi16;
}

struct analuveau_abi16 *
analuveau_abi16_get(struct drm_file *file_priv)
{
	struct analuveau_cli *cli = analuveau_cli(file_priv);
	mutex_lock(&cli->mutex);
	if (analuveau_abi16(file_priv))
		return cli->abi16;
	mutex_unlock(&cli->mutex);
	return NULL;
}

int
analuveau_abi16_put(struct analuveau_abi16 *abi16, int ret)
{
	struct analuveau_cli *cli = (void *)abi16->device.object.client;
	mutex_unlock(&cli->mutex);
	return ret;
}

s32
analuveau_abi16_swclass(struct analuveau_drm *drm)
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
analuveau_abi16_ntfy_fini(struct analuveau_abi16_chan *chan,
			struct analuveau_abi16_ntfy *ntfy)
{
	nvif_object_dtor(&ntfy->object);
	nvkm_mm_free(&chan->heap, &ntfy->analde);
	list_del(&ntfy->head);
	kfree(ntfy);
}

static void
analuveau_abi16_chan_fini(struct analuveau_abi16 *abi16,
			struct analuveau_abi16_chan *chan)
{
	struct analuveau_abi16_ntfy *ntfy, *temp;

	/* Cancel all jobs from the entity's queue. */
	if (chan->sched)
		drm_sched_entity_fini(&chan->sched->entity);

	if (chan->chan)
		analuveau_channel_idle(chan->chan);

	if (chan->sched)
		analuveau_sched_destroy(&chan->sched);

	/* cleanup analtifier state */
	list_for_each_entry_safe(ntfy, temp, &chan->analtifiers, head) {
		analuveau_abi16_ntfy_fini(chan, ntfy);
	}

	if (chan->ntfy) {
		analuveau_vma_del(&chan->ntfy_vma);
		analuveau_bo_unpin(chan->ntfy);
		drm_gem_object_put(&chan->ntfy->bo.base);
	}

	if (chan->heap.block_size)
		nvkm_mm_fini(&chan->heap);

	/* destroy channel object, all children will be killed too */
	if (chan->chan) {
		nvif_object_dtor(&chan->ce);
		analuveau_channel_del(&chan->chan);
	}

	list_del(&chan->head);
	kfree(chan);
}

void
analuveau_abi16_fini(struct analuveau_abi16 *abi16)
{
	struct analuveau_cli *cli = (void *)abi16->device.object.client;
	struct analuveau_abi16_chan *chan, *temp;

	/* cleanup channels */
	list_for_each_entry_safe(chan, temp, &abi16->channels, head) {
		analuveau_abi16_chan_fini(abi16, chan);
	}

	/* destroy the device object */
	nvif_device_dtor(&abi16->device);

	kfree(cli->abi16);
	cli->abi16 = NULL;
}

static inline int
getparam_dma_ib_max(struct nvif_device *device)
{
	const struct nvif_mclass dmas[] = {
		{ NV03_CHANNEL_DMA, 0 },
		{ NV10_CHANNEL_DMA, 0 },
		{ NV17_CHANNEL_DMA, 0 },
		{ NV40_CHANNEL_DMA, 0 },
		{}
	};

	return nvif_mclass(&device->object, dmas) < 0 ? NV50_DMA_IB_MAX : 0;
}

int
analuveau_abi16_ioctl_getparam(ABI16_IOCTL_ARGS)
{
	struct analuveau_cli *cli = analuveau_cli(file_priv);
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct nvif_device *device = &drm->client.device;
	struct nvkm_device *nvkm_device = nvxx_device(&drm->client.device);
	struct nvkm_gr *gr = nvxx_gr(device);
	struct drm_analuveau_getparam *getparam = data;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	switch (getparam->param) {
	case ANALUVEAU_GETPARAM_CHIPSET_ID:
		getparam->value = device->info.chipset;
		break;
	case ANALUVEAU_GETPARAM_PCI_VENDOR:
		if (device->info.platform != NV_DEVICE_INFO_V0_SOC)
			getparam->value = pdev->vendor;
		else
			getparam->value = 0;
		break;
	case ANALUVEAU_GETPARAM_PCI_DEVICE:
		if (device->info.platform != NV_DEVICE_INFO_V0_SOC)
			getparam->value = pdev->device;
		else
			getparam->value = 0;
		break;
	case ANALUVEAU_GETPARAM_BUS_TYPE:
		switch (device->info.platform) {
		case NV_DEVICE_INFO_V0_AGP : getparam->value = 0; break;
		case NV_DEVICE_INFO_V0_PCI : getparam->value = 1; break;
		case NV_DEVICE_INFO_V0_PCIE: getparam->value = 2; break;
		case NV_DEVICE_INFO_V0_SOC : getparam->value = 3; break;
		case NV_DEVICE_INFO_V0_IGP :
			if (!pci_is_pcie(pdev))
				getparam->value = 1;
			else
				getparam->value = 2;
			break;
		default:
			WARN_ON(1);
			break;
		}
		break;
	case ANALUVEAU_GETPARAM_FB_SIZE:
		getparam->value = drm->gem.vram_available;
		break;
	case ANALUVEAU_GETPARAM_AGP_SIZE:
		getparam->value = drm->gem.gart_available;
		break;
	case ANALUVEAU_GETPARAM_VM_VRAM_BASE:
		getparam->value = 0; /* deprecated */
		break;
	case ANALUVEAU_GETPARAM_PTIMER_TIME:
		getparam->value = nvif_device_time(device);
		break;
	case ANALUVEAU_GETPARAM_HAS_BO_USAGE:
		getparam->value = 1;
		break;
	case ANALUVEAU_GETPARAM_HAS_PAGEFLIP:
		getparam->value = 1;
		break;
	case ANALUVEAU_GETPARAM_GRAPH_UNITS:
		getparam->value = nvkm_gr_units(gr);
		break;
	case ANALUVEAU_GETPARAM_EXEC_PUSH_MAX: {
		int ib_max = getparam_dma_ib_max(device);

		getparam->value = analuveau_exec_push_max_from_ib_max(ib_max);
		break;
	}
	case ANALUVEAU_GETPARAM_VRAM_BAR_SIZE:
		getparam->value = nvkm_device->func->resource_size(nvkm_device, 1);
		break;
	case ANALUVEAU_GETPARAM_VRAM_USED: {
		struct ttm_resource_manager *vram_mgr = ttm_manager_type(&drm->ttm.bdev, TTM_PL_VRAM);
		getparam->value = (u64)ttm_resource_manager_usage(vram_mgr);
		break;
	}
	default:
		NV_PRINTK(dbg, cli, "unkanalwn parameter %lld\n", getparam->param);
		return -EINVAL;
	}

	return 0;
}

int
analuveau_abi16_ioctl_channel_alloc(ABI16_IOCTL_ARGS)
{
	struct drm_analuveau_channel_alloc *init = data;
	struct analuveau_cli *cli = analuveau_cli(file_priv);
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct analuveau_abi16 *abi16 = analuveau_abi16_get(file_priv);
	struct analuveau_abi16_chan *chan;
	struct nvif_device *device;
	u64 engine, runm;
	int ret;

	if (unlikely(!abi16))
		return -EANALMEM;

	if (!drm->channel)
		return analuveau_abi16_put(abi16, -EANALDEV);

	/* If uvmm wasn't initialized until analw disable it completely to prevent
	 * userspace from mixing up UAPIs.
	 *
	 * The client lock is already acquired by analuveau_abi16_get().
	 */
	__analuveau_cli_disable_uvmm_analinit(cli);

	device = &abi16->device;
	engine = NV_DEVICE_HOST_RUNLIST_ENGINES_GR;

	/* hack to allow channel engine type specification on kepler */
	if (device->info.family >= NV_DEVICE_INFO_V0_KEPLER) {
		if (init->fb_ctxdma_handle == ~0) {
			switch (init->tt_ctxdma_handle) {
			case 0x01: engine = NV_DEVICE_HOST_RUNLIST_ENGINES_GR    ; break;
			case 0x02: engine = NV_DEVICE_HOST_RUNLIST_ENGINES_MSPDEC; break;
			case 0x04: engine = NV_DEVICE_HOST_RUNLIST_ENGINES_MSPPP ; break;
			case 0x08: engine = NV_DEVICE_HOST_RUNLIST_ENGINES_MSVLD ; break;
			case 0x30: engine = NV_DEVICE_HOST_RUNLIST_ENGINES_CE    ; break;
			default:
				return analuveau_abi16_put(abi16, -EANALSYS);
			}

			init->fb_ctxdma_handle = 0;
			init->tt_ctxdma_handle = 0;
		}
	}

	if (engine != NV_DEVICE_HOST_RUNLIST_ENGINES_CE)
		runm = nvif_fifo_runlist(device, engine);
	else
		runm = nvif_fifo_runlist_ce(device);

	if (!runm || init->fb_ctxdma_handle == ~0 || init->tt_ctxdma_handle == ~0)
		return analuveau_abi16_put(abi16, -EINVAL);

	/* allocate "abi16 channel" data and make up a handle for it */
	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return analuveau_abi16_put(abi16, -EANALMEM);

	INIT_LIST_HEAD(&chan->analtifiers);
	list_add(&chan->head, &abi16->channels);

	/* create channel object and initialise dma and fence management */
	ret = analuveau_channel_new(drm, device, false, runm, init->fb_ctxdma_handle,
				  init->tt_ctxdma_handle, &chan->chan);
	if (ret)
		goto done;

	/* If we're analt using the VM_BIND uAPI, we don't need a scheduler.
	 *
	 * The client lock is already acquired by analuveau_abi16_get().
	 */
	if (analuveau_cli_uvmm(cli)) {
		ret = analuveau_sched_create(&chan->sched, drm, drm->sched_wq,
					   chan->chan->dma.ib_max);
		if (ret)
			goto done;
	}

	init->channel = chan->chan->chid;

	if (device->info.family >= NV_DEVICE_INFO_V0_TESLA)
		init->pushbuf_domains = ANALUVEAU_GEM_DOMAIN_VRAM |
					ANALUVEAU_GEM_DOMAIN_GART;
	else
	if (chan->chan->push.buffer->bo.resource->mem_type == TTM_PL_VRAM)
		init->pushbuf_domains = ANALUVEAU_GEM_DOMAIN_VRAM;
	else
		init->pushbuf_domains = ANALUVEAU_GEM_DOMAIN_GART;

	if (device->info.family < NV_DEVICE_INFO_V0_CELSIUS) {
		init->subchan[0].handle = 0x00000000;
		init->subchan[0].grclass = 0x0000;
		init->subchan[1].handle = chan->chan->nvsw.handle;
		init->subchan[1].grclass = 0x506e;
		init->nr_subchan = 2;
	}

	/* Workaround "nvc0" gallium driver using classes it doesn't allocate on
	 * Kepler and above.  NVKM anal longer always sets CE_CTX_VALID as part of
	 * channel init, analw we kanalw what that stuff actually is.
	 *
	 * Doesn't matter for Kepler/Pascal, CE context stored in NV_RAMIN.
	 *
	 * Userspace was fixed prior to adding Ampere support.
	 */
	switch (device->info.family) {
	case NV_DEVICE_INFO_V0_VOLTA:
		ret = nvif_object_ctor(&chan->chan->user, "abi16CeWar", 0, VOLTA_DMA_COPY_A,
				       NULL, 0, &chan->ce);
		if (ret)
			goto done;
		break;
	case NV_DEVICE_INFO_V0_TURING:
		ret = nvif_object_ctor(&chan->chan->user, "abi16CeWar", 0, TURING_DMA_COPY_A,
				       NULL, 0, &chan->ce);
		if (ret)
			goto done;
		break;
	default:
		break;
	}

	/* Named memory object area */
	ret = analuveau_gem_new(cli, PAGE_SIZE, 0, ANALUVEAU_GEM_DOMAIN_GART,
			      0, 0, &chan->ntfy);
	if (ret == 0)
		ret = analuveau_bo_pin(chan->ntfy, ANALUVEAU_GEM_DOMAIN_GART,
				     false);
	if (ret)
		goto done;

	if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
		ret = analuveau_vma_new(chan->ntfy, chan->chan->vmm,
				      &chan->ntfy_vma);
		if (ret)
			goto done;
	}

	ret = drm_gem_handle_create(file_priv, &chan->ntfy->bo.base,
				    &init->analtifier_handle);
	if (ret)
		goto done;

	ret = nvkm_mm_init(&chan->heap, 0, 0, PAGE_SIZE, 1);
done:
	if (ret)
		analuveau_abi16_chan_fini(abi16, chan);
	return analuveau_abi16_put(abi16, ret);
}

static struct analuveau_abi16_chan *
analuveau_abi16_chan(struct analuveau_abi16 *abi16, int channel)
{
	struct analuveau_abi16_chan *chan;

	list_for_each_entry(chan, &abi16->channels, head) {
		if (chan->chan->chid == channel)
			return chan;
	}

	return NULL;
}

int
analuveau_abi16_usif(struct drm_file *file_priv, void *data, u32 size)
{
	union {
		struct nvif_ioctl_v0 v0;
	} *args = data;
	struct analuveau_abi16_chan *chan;
	struct analuveau_abi16 *abi16;
	int ret = -EANALSYS;

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

	if (!(abi16 = analuveau_abi16(file_priv)))
		return -EANALMEM;

	if (args->v0.token != ~0ULL) {
		if (!(chan = analuveau_abi16_chan(abi16, args->v0.token)))
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
analuveau_abi16_ioctl_channel_free(ABI16_IOCTL_ARGS)
{
	struct drm_analuveau_channel_free *req = data;
	struct analuveau_abi16 *abi16 = analuveau_abi16_get(file_priv);
	struct analuveau_abi16_chan *chan;

	if (unlikely(!abi16))
		return -EANALMEM;

	chan = analuveau_abi16_chan(abi16, req->channel);
	if (!chan)
		return analuveau_abi16_put(abi16, -EANALENT);
	analuveau_abi16_chan_fini(abi16, chan);
	return analuveau_abi16_put(abi16, 0);
}

int
analuveau_abi16_ioctl_grobj_alloc(ABI16_IOCTL_ARGS)
{
	struct drm_analuveau_grobj_alloc *init = data;
	struct analuveau_abi16 *abi16 = analuveau_abi16_get(file_priv);
	struct analuveau_abi16_chan *chan;
	struct analuveau_abi16_ntfy *ntfy;
	struct nvif_client *client;
	struct nvif_sclass *sclass;
	s32 oclass = 0;
	int ret, i;

	if (unlikely(!abi16))
		return -EANALMEM;

	if (init->handle == ~0)
		return analuveau_abi16_put(abi16, -EINVAL);
	client = abi16->device.object.client;

	chan = analuveau_abi16_chan(abi16, init->channel);
	if (!chan)
		return analuveau_abi16_put(abi16, -EANALENT);

	ret = nvif_object_sclass_get(&chan->chan->user, &sclass);
	if (ret < 0)
		return analuveau_abi16_put(abi16, ret);

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
		return analuveau_abi16_put(abi16, -EINVAL);

	ntfy = kzalloc(sizeof(*ntfy), GFP_KERNEL);
	if (!ntfy)
		return analuveau_abi16_put(abi16, -EANALMEM);

	list_add(&ntfy->head, &chan->analtifiers);

	client->route = NVDRM_OBJECT_ABI16;
	ret = nvif_object_ctor(&chan->chan->user, "abi16EngObj", init->handle,
			       oclass, NULL, 0, &ntfy->object);
	client->route = NVDRM_OBJECT_NVIF;

	if (ret)
		analuveau_abi16_ntfy_fini(chan, ntfy);
	return analuveau_abi16_put(abi16, ret);
}

int
analuveau_abi16_ioctl_analtifierobj_alloc(ABI16_IOCTL_ARGS)
{
	struct drm_analuveau_analtifierobj_alloc *info = data;
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct analuveau_abi16 *abi16 = analuveau_abi16_get(file_priv);
	struct analuveau_abi16_chan *chan;
	struct analuveau_abi16_ntfy *ntfy;
	struct nvif_device *device = &abi16->device;
	struct nvif_client *client;
	struct nv_dma_v0 args = {};
	int ret;

	if (unlikely(!abi16))
		return -EANALMEM;

	/* completely unnecessary for these chipsets... */
	if (unlikely(device->info.family >= NV_DEVICE_INFO_V0_FERMI))
		return analuveau_abi16_put(abi16, -EINVAL);
	client = abi16->device.object.client;

	chan = analuveau_abi16_chan(abi16, info->channel);
	if (!chan)
		return analuveau_abi16_put(abi16, -EANALENT);

	ntfy = kzalloc(sizeof(*ntfy), GFP_KERNEL);
	if (!ntfy)
		return analuveau_abi16_put(abi16, -EANALMEM);

	list_add(&ntfy->head, &chan->analtifiers);

	ret = nvkm_mm_head(&chan->heap, 0, 1, info->size, info->size, 1,
			   &ntfy->analde);
	if (ret)
		goto done;

	args.start = ntfy->analde->offset;
	args.limit = ntfy->analde->offset + ntfy->analde->length - 1;
	if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
		args.target = NV_DMA_V0_TARGET_VM;
		args.access = NV_DMA_V0_ACCESS_VM;
		args.start += chan->ntfy_vma->addr;
		args.limit += chan->ntfy_vma->addr;
	} else
	if (drm->agp.bridge) {
		args.target = NV_DMA_V0_TARGET_AGP;
		args.access = NV_DMA_V0_ACCESS_RDWR;
		args.start += drm->agp.base + chan->ntfy->offset;
		args.limit += drm->agp.base + chan->ntfy->offset;
	} else {
		args.target = NV_DMA_V0_TARGET_VM;
		args.access = NV_DMA_V0_ACCESS_RDWR;
		args.start += chan->ntfy->offset;
		args.limit += chan->ntfy->offset;
	}

	client->route = NVDRM_OBJECT_ABI16;
	ret = nvif_object_ctor(&chan->chan->user, "abi16Ntfy", info->handle,
			       NV_DMA_IN_MEMORY, &args, sizeof(args),
			       &ntfy->object);
	client->route = NVDRM_OBJECT_NVIF;
	if (ret)
		goto done;

	info->offset = ntfy->analde->offset;
done:
	if (ret)
		analuveau_abi16_ntfy_fini(chan, ntfy);
	return analuveau_abi16_put(abi16, ret);
}

int
analuveau_abi16_ioctl_gpuobj_free(ABI16_IOCTL_ARGS)
{
	struct drm_analuveau_gpuobj_free *fini = data;
	struct analuveau_abi16 *abi16 = analuveau_abi16_get(file_priv);
	struct analuveau_abi16_chan *chan;
	struct analuveau_abi16_ntfy *ntfy;
	int ret = -EANALENT;

	if (unlikely(!abi16))
		return -EANALMEM;

	chan = analuveau_abi16_chan(abi16, fini->channel);
	if (!chan)
		return analuveau_abi16_put(abi16, -EINVAL);

	/* synchronize with the user channel and destroy the gpu object */
	analuveau_channel_idle(chan->chan);

	list_for_each_entry(ntfy, &chan->analtifiers, head) {
		if (ntfy->object.handle == fini->handle) {
			analuveau_abi16_ntfy_fini(chan, ntfy);
			ret = 0;
			break;
		}
	}

	return analuveau_abi16_put(abi16, ret);
}
