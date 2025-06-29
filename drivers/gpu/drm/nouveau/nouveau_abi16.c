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
#include <nvif/fifo.h>
#include <nvif/ioctl.h>
#include <nvif/class.h>
#include <nvif/cl0002.h>
#include <nvif/unpack.h>

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_exec.h"
#include "nouveau_gem.h"
#include "nouveau_chan.h"
#include "nouveau_abi16.h"
#include "nouveau_vmm.h"
#include "nouveau_sched.h"

static struct nouveau_abi16 *
nouveau_abi16(struct drm_file *file_priv)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	if (!cli->abi16) {
		struct nouveau_abi16 *abi16;
		cli->abi16 = abi16 = kzalloc(sizeof(*abi16), GFP_KERNEL);
		if (cli->abi16) {
			abi16->cli = cli;
			INIT_LIST_HEAD(&abi16->channels);
			INIT_LIST_HEAD(&abi16->objects);
		}
	}
	return cli->abi16;
}

struct nouveau_abi16 *
nouveau_abi16_get(struct drm_file *file_priv)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	mutex_lock(&cli->mutex);
	if (nouveau_abi16(file_priv))
		return cli->abi16;
	mutex_unlock(&cli->mutex);
	return NULL;
}

int
nouveau_abi16_put(struct nouveau_abi16 *abi16, int ret)
{
	struct nouveau_cli *cli = abi16->cli;
	mutex_unlock(&cli->mutex);
	return ret;
}

/* Tracks objects created via the DRM_NOUVEAU_NVIF ioctl.
 *
 * The only two types of object that userspace ever allocated via this
 * interface are 'device', in order to retrieve basic device info, and
 * 'engine objects', which instantiate HW classes on a channel.
 *
 * The remainder of what used to be available via DRM_NOUVEAU_NVIF has
 * been removed, but these object types need to be tracked to maintain
 * compatibility with userspace.
 */
struct nouveau_abi16_obj {
	enum nouveau_abi16_obj_type {
		DEVICE,
		ENGOBJ,
	} type;
	u64 object;

	struct nvif_object engobj;

	struct list_head head; /* protected by nouveau_abi16.cli.mutex */
};

static struct nouveau_abi16_obj *
nouveau_abi16_obj_find(struct nouveau_abi16 *abi16, u64 object)
{
	struct nouveau_abi16_obj *obj;

	list_for_each_entry(obj, &abi16->objects, head) {
		if (obj->object == object)
			return obj;
	}

	return NULL;
}

static void
nouveau_abi16_obj_del(struct nouveau_abi16_obj *obj)
{
	list_del(&obj->head);
	kfree(obj);
}

static struct nouveau_abi16_obj *
nouveau_abi16_obj_new(struct nouveau_abi16 *abi16, enum nouveau_abi16_obj_type type, u64 object)
{
	struct nouveau_abi16_obj *obj;

	obj = nouveau_abi16_obj_find(abi16, object);
	if (obj)
		return ERR_PTR(-EEXIST);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	obj->type = type;
	obj->object = object;
	list_add_tail(&obj->head, &abi16->objects);
	return obj;
}

s32
nouveau_abi16_swclass(struct nouveau_drm *drm)
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
nouveau_abi16_ntfy_fini(struct nouveau_abi16_chan *chan,
			struct nouveau_abi16_ntfy *ntfy)
{
	nvif_object_dtor(&ntfy->object);
	nvkm_mm_free(&chan->heap, &ntfy->node);
	list_del(&ntfy->head);
	kfree(ntfy);
}

static void
nouveau_abi16_chan_fini(struct nouveau_abi16 *abi16,
			struct nouveau_abi16_chan *chan)
{
	struct nouveau_abi16_ntfy *ntfy, *temp;

	/* Cancel all jobs from the entity's queue. */
	if (chan->sched)
		drm_sched_entity_fini(&chan->sched->entity);

	if (chan->chan)
		nouveau_channel_idle(chan->chan);

	if (chan->sched)
		nouveau_sched_destroy(&chan->sched);

	/* cleanup notifier state */
	list_for_each_entry_safe(ntfy, temp, &chan->notifiers, head) {
		nouveau_abi16_ntfy_fini(chan, ntfy);
	}

	if (chan->ntfy) {
		nouveau_vma_del(&chan->ntfy_vma);
		nouveau_bo_unpin(chan->ntfy);
		drm_gem_object_put(&chan->ntfy->bo.base);
	}

	if (chan->heap.block_size)
		nvkm_mm_fini(&chan->heap);

	/* destroy channel object, all children will be killed too */
	if (chan->chan) {
		nvif_object_dtor(&chan->ce);
		nouveau_channel_del(&chan->chan);
	}

	list_del(&chan->head);
	kfree(chan);
}

void
nouveau_abi16_fini(struct nouveau_abi16 *abi16)
{
	struct nouveau_cli *cli = abi16->cli;
	struct nouveau_abi16_chan *chan, *temp;
	struct nouveau_abi16_obj *obj, *tmp;

	/* cleanup objects */
	list_for_each_entry_safe(obj, tmp, &abi16->objects, head) {
		nouveau_abi16_obj_del(obj);
	}

	/* cleanup channels */
	list_for_each_entry_safe(chan, temp, &abi16->channels, head) {
		nouveau_abi16_chan_fini(abi16, chan);
	}

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
nouveau_abi16_ioctl_getparam(ABI16_IOCTL_ARGS)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvif_device *device = &drm->client.device;
	struct nvkm_device *nvkm_device = nvxx_device(drm);
	struct nvkm_gr *gr = nvxx_gr(drm);
	struct drm_nouveau_getparam *getparam = data;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	switch (getparam->param) {
	case NOUVEAU_GETPARAM_CHIPSET_ID:
		getparam->value = device->info.chipset;
		break;
	case NOUVEAU_GETPARAM_PCI_VENDOR:
		if (device->info.platform != NV_DEVICE_INFO_V0_SOC)
			getparam->value = pdev->vendor;
		else
			getparam->value = 0;
		break;
	case NOUVEAU_GETPARAM_PCI_DEVICE:
		if (device->info.platform != NV_DEVICE_INFO_V0_SOC)
			getparam->value = pdev->device;
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
	case NOUVEAU_GETPARAM_EXEC_PUSH_MAX: {
		int ib_max = getparam_dma_ib_max(device);

		getparam->value = nouveau_exec_push_max_from_ib_max(ib_max);
		break;
	}
	case NOUVEAU_GETPARAM_VRAM_BAR_SIZE:
		getparam->value = nvkm_device->func->resource_size(nvkm_device, NVKM_BAR1_FB);
		break;
	case NOUVEAU_GETPARAM_VRAM_USED: {
		struct ttm_resource_manager *vram_mgr = ttm_manager_type(&drm->ttm.bdev, TTM_PL_VRAM);
		getparam->value = (u64)ttm_resource_manager_usage(vram_mgr);
		break;
	}
	case NOUVEAU_GETPARAM_HAS_VMA_TILEMODE:
		getparam->value = 1;
		break;
	default:
		NV_PRINTK(dbg, cli, "unknown parameter %lld\n", getparam->param);
		return -EINVAL;
	}

	return 0;
}

int
nouveau_abi16_ioctl_channel_alloc(ABI16_IOCTL_ARGS)
{
	struct drm_nouveau_channel_alloc *init = data;
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_abi16 *abi16 = nouveau_abi16_get(file_priv);
	struct nouveau_abi16_chan *chan;
	struct nvif_device *device = &cli->device;
	u64 engine, runm;
	int ret;

	if (unlikely(!abi16))
		return -ENOMEM;

	if (!drm->channel)
		return nouveau_abi16_put(abi16, -ENODEV);

	/* If uvmm wasn't initialized until now disable it completely to prevent
	 * userspace from mixing up UAPIs.
	 *
	 * The client lock is already acquired by nouveau_abi16_get().
	 */
	__nouveau_cli_disable_uvmm_noinit(cli);

	engine = NV_DEVICE_HOST_RUNLIST_ENGINES_GR;

	/* hack to allow channel engine type specification on kepler */
	if (device->info.family >= NV_DEVICE_INFO_V0_KEPLER) {
		if (init->fb_ctxdma_handle == ~0) {
			switch (init->tt_ctxdma_handle) {
			case NOUVEAU_FIFO_ENGINE_GR:
				engine = NV_DEVICE_HOST_RUNLIST_ENGINES_GR;
				break;
			case NOUVEAU_FIFO_ENGINE_VP:
				engine = NV_DEVICE_HOST_RUNLIST_ENGINES_MSPDEC;
				break;
			case NOUVEAU_FIFO_ENGINE_PPP:
				engine = NV_DEVICE_HOST_RUNLIST_ENGINES_MSPPP;
				break;
			case NOUVEAU_FIFO_ENGINE_BSP:
				engine = NV_DEVICE_HOST_RUNLIST_ENGINES_MSVLD;
				break;
			case NOUVEAU_FIFO_ENGINE_CE:
				engine = NV_DEVICE_HOST_RUNLIST_ENGINES_CE;
				break;
			default:
				return nouveau_abi16_put(abi16, -ENOSYS);
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
		return nouveau_abi16_put(abi16, -EINVAL);

	/* allocate "abi16 channel" data and make up a handle for it */
	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return nouveau_abi16_put(abi16, -ENOMEM);

	INIT_LIST_HEAD(&chan->notifiers);
	list_add(&chan->head, &abi16->channels);

	/* create channel object and initialise dma and fence management */
	ret = nouveau_channel_new(cli, false, runm, init->fb_ctxdma_handle,
				  init->tt_ctxdma_handle, &chan->chan);
	if (ret)
		goto done;

	/* If we're not using the VM_BIND uAPI, we don't need a scheduler.
	 *
	 * The client lock is already acquired by nouveau_abi16_get().
	 */
	if (nouveau_cli_uvmm(cli)) {
		ret = nouveau_sched_create(&chan->sched, drm, drm->sched_wq,
					   chan->chan->chan.gpfifo.max);
		if (ret)
			goto done;
	}

	init->channel = chan->chan->chid;

	if (device->info.family >= NV_DEVICE_INFO_V0_TESLA)
		init->pushbuf_domains = NOUVEAU_GEM_DOMAIN_VRAM |
					NOUVEAU_GEM_DOMAIN_GART;
	else
	if (chan->chan->push.buffer->bo.resource->mem_type == TTM_PL_VRAM)
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

	/* Workaround "nvc0" gallium driver using classes it doesn't allocate on
	 * Kepler and above.  NVKM no longer always sets CE_CTX_VALID as part of
	 * channel init, now we know what that stuff actually is.
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
	ret = nouveau_gem_new(cli, PAGE_SIZE, 0, NOUVEAU_GEM_DOMAIN_GART,
			      0, 0, &chan->ntfy);
	if (ret == 0)
		ret = nouveau_bo_pin(chan->ntfy, NOUVEAU_GEM_DOMAIN_GART,
				     false);
	if (ret)
		goto done;

	if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
		ret = nouveau_vma_new(chan->ntfy, chan->chan->vmm,
				      &chan->ntfy_vma);
		if (ret)
			goto done;
	}

	ret = drm_gem_handle_create(file_priv, &chan->ntfy->bo.base,
				    &init->notifier_handle);
	if (ret)
		goto done;

	ret = nvkm_mm_init(&chan->heap, 0, 0, PAGE_SIZE, 1);
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
		if (chan->chan->chid == channel)
			return chan;
	}

	return NULL;
}

int
nouveau_abi16_ioctl_channel_free(ABI16_IOCTL_ARGS)
{
	struct drm_nouveau_channel_free *req = data;
	struct nouveau_abi16 *abi16 = nouveau_abi16_get(file_priv);
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
	struct nouveau_abi16 *abi16 = nouveau_abi16_get(file_priv);
	struct nouveau_abi16_chan *chan;
	struct nouveau_abi16_ntfy *ntfy;
	struct nvif_sclass *sclass;
	s32 oclass = 0;
	int ret, i;

	if (unlikely(!abi16))
		return -ENOMEM;

	if (init->handle == ~0)
		return nouveau_abi16_put(abi16, -EINVAL);

	chan = nouveau_abi16_chan(abi16, init->channel);
	if (!chan)
		return nouveau_abi16_put(abi16, -ENOENT);

	ret = nvif_object_sclass_get(&chan->chan->user, &sclass);
	if (ret < 0)
		return nouveau_abi16_put(abi16, ret);

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
		return nouveau_abi16_put(abi16, -EINVAL);

	ntfy = kzalloc(sizeof(*ntfy), GFP_KERNEL);
	if (!ntfy)
		return nouveau_abi16_put(abi16, -ENOMEM);

	list_add(&ntfy->head, &chan->notifiers);

	ret = nvif_object_ctor(&chan->chan->user, "abi16EngObj", init->handle,
			       oclass, NULL, 0, &ntfy->object);

	if (ret)
		nouveau_abi16_ntfy_fini(chan, ntfy);
	return nouveau_abi16_put(abi16, ret);
}

int
nouveau_abi16_ioctl_notifierobj_alloc(ABI16_IOCTL_ARGS)
{
	struct drm_nouveau_notifierobj_alloc *info = data;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_abi16 *abi16 = nouveau_abi16_get(file_priv);
	struct nouveau_abi16_chan *chan;
	struct nouveau_abi16_ntfy *ntfy;
	struct nvif_device *device;
	struct nv_dma_v0 args = {};
	int ret;

	if (unlikely(!abi16))
		return -ENOMEM;
	device = &abi16->cli->device;

	/* completely unnecessary for these chipsets... */
	if (unlikely(device->info.family >= NV_DEVICE_INFO_V0_FERMI))
		return nouveau_abi16_put(abi16, -EINVAL);

	chan = nouveau_abi16_chan(abi16, info->channel);
	if (!chan)
		return nouveau_abi16_put(abi16, -ENOENT);

	ntfy = kzalloc(sizeof(*ntfy), GFP_KERNEL);
	if (!ntfy)
		return nouveau_abi16_put(abi16, -ENOMEM);

	list_add(&ntfy->head, &chan->notifiers);

	ret = nvkm_mm_head(&chan->heap, 0, 1, info->size, info->size, 1,
			   &ntfy->node);
	if (ret)
		goto done;

	args.start = ntfy->node->offset;
	args.limit = ntfy->node->offset + ntfy->node->length - 1;
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

	ret = nvif_object_ctor(&chan->chan->user, "abi16Ntfy", info->handle,
			       NV_DMA_IN_MEMORY, &args, sizeof(args),
			       &ntfy->object);
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
	struct nouveau_abi16 *abi16 = nouveau_abi16_get(file_priv);
	struct nouveau_abi16_chan *chan;
	struct nouveau_abi16_ntfy *ntfy;
	int ret = -ENOENT;

	if (unlikely(!abi16))
		return -ENOMEM;

	chan = nouveau_abi16_chan(abi16, fini->channel);
	if (!chan)
		return nouveau_abi16_put(abi16, -EINVAL);

	/* synchronize with the user channel and destroy the gpu object */
	nouveau_channel_idle(chan->chan);

	list_for_each_entry(ntfy, &chan->notifiers, head) {
		if (ntfy->object.handle == fini->handle) {
			nouveau_abi16_ntfy_fini(chan, ntfy);
			ret = 0;
			break;
		}
	}

	return nouveau_abi16_put(abi16, ret);
}

static int
nouveau_abi16_ioctl_mthd(struct nouveau_abi16 *abi16, struct nvif_ioctl_v0 *ioctl, u32 argc)
{
	struct nouveau_cli *cli = abi16->cli;
	struct nvif_ioctl_mthd_v0 *args;
	struct nouveau_abi16_obj *obj;
	struct nv_device_info_v0 *info;

	if (ioctl->route || argc < sizeof(*args))
		return -EINVAL;
	args = (void *)ioctl->data;
	argc -= sizeof(*args);

	obj = nouveau_abi16_obj_find(abi16, ioctl->object);
	if (!obj || obj->type != DEVICE)
		return -EINVAL;

	if (args->method != NV_DEVICE_V0_INFO ||
	    argc != sizeof(*info))
		return -EINVAL;

	info = (void *)args->data;
	if (info->version != 0x00)
		return -EINVAL;

	info = &cli->device.info;
	memcpy(args->data, info, sizeof(*info));
	return 0;
}

static int
nouveau_abi16_ioctl_del(struct nouveau_abi16 *abi16, struct nvif_ioctl_v0 *ioctl, u32 argc)
{
	struct nouveau_abi16_obj *obj;

	if (ioctl->route || argc)
		return -EINVAL;

	obj = nouveau_abi16_obj_find(abi16, ioctl->object);
	if (obj) {
		if (obj->type == ENGOBJ)
			nvif_object_dtor(&obj->engobj);
		nouveau_abi16_obj_del(obj);
	}

	return 0;
}

static int
nouveau_abi16_ioctl_new(struct nouveau_abi16 *abi16, struct nvif_ioctl_v0 *ioctl, u32 argc)
{
	struct nvif_ioctl_new_v0 *args;
	struct nouveau_abi16_chan *chan;
	struct nouveau_abi16_obj *obj;
	int ret;

	if (argc < sizeof(*args))
		return -EINVAL;
	args = (void *)ioctl->data;
	argc -= sizeof(*args);

	if (args->version != 0)
		return -EINVAL;

	if (!ioctl->route) {
		if (ioctl->object || args->oclass != NV_DEVICE)
			return -EINVAL;

		obj = nouveau_abi16_obj_new(abi16, DEVICE, args->object);
		if (IS_ERR(obj))
			return PTR_ERR(obj);

		return 0;
	}

	chan = nouveau_abi16_chan(abi16, ioctl->token);
	if (!chan)
		return -EINVAL;

	obj = nouveau_abi16_obj_new(abi16, ENGOBJ, args->object);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	ret = nvif_object_ctor(&chan->chan->user, "abi16EngObj", args->handle, args->oclass,
			       NULL, 0, &obj->engobj);
	if (ret)
		nouveau_abi16_obj_del(obj);

	return ret;
}

static int
nouveau_abi16_ioctl_sclass(struct nouveau_abi16 *abi16, struct nvif_ioctl_v0 *ioctl, u32 argc)
{
	struct nvif_ioctl_sclass_v0 *args;
	struct nouveau_abi16_chan *chan;
	struct nvif_sclass *sclass;
	int ret;

	if (!ioctl->route || argc < sizeof(*args))
		return -EINVAL;
	args = (void *)ioctl->data;
	argc -= sizeof(*args);

	if (argc != args->count * sizeof(args->oclass[0]))
		return -EINVAL;

	chan = nouveau_abi16_chan(abi16, ioctl->token);
	if (!chan)
		return -EINVAL;

	ret = nvif_object_sclass_get(&chan->chan->user, &sclass);
	if (ret < 0)
		return ret;

	for (int i = 0; i < min_t(u8, args->count, ret); i++) {
		args->oclass[i].oclass = sclass[i].oclass;
		args->oclass[i].minver = sclass[i].minver;
		args->oclass[i].maxver = sclass[i].maxver;
	}
	args->count = ret;

	nvif_object_sclass_put(&sclass);
	return 0;
}

int
nouveau_abi16_ioctl(struct drm_file *filp, void __user *user, u32 size)
{
	struct nvif_ioctl_v0 *ioctl;
	struct nouveau_abi16 *abi16;
	u32 argc = size;
	int ret;

	if (argc < sizeof(*ioctl))
		return -EINVAL;
	argc -= sizeof(*ioctl);

	ioctl = kmalloc(size, GFP_KERNEL);
	if (!ioctl)
		return -ENOMEM;

	ret = -EFAULT;
	if (copy_from_user(ioctl, user, size))
		goto done_free;

	if (ioctl->version != 0x00 ||
	    (ioctl->route && ioctl->route != 0xff)) {
		ret = -EINVAL;
		goto done_free;
	}

	abi16 = nouveau_abi16_get(filp);
	if (unlikely(!abi16)) {
		ret = -ENOMEM;
		goto done_free;
	}

	switch (ioctl->type) {
	case NVIF_IOCTL_V0_SCLASS: ret = nouveau_abi16_ioctl_sclass(abi16, ioctl, argc); break;
	case NVIF_IOCTL_V0_NEW   : ret = nouveau_abi16_ioctl_new   (abi16, ioctl, argc); break;
	case NVIF_IOCTL_V0_DEL   : ret = nouveau_abi16_ioctl_del   (abi16, ioctl, argc); break;
	case NVIF_IOCTL_V0_MTHD  : ret = nouveau_abi16_ioctl_mthd  (abi16, ioctl, argc); break;
	default:
		ret = -EINVAL;
		break;
	}

	nouveau_abi16_put(abi16, 0);

	if (ret == 0) {
		if (copy_to_user(user, ioctl, size))
			ret = -EFAULT;
	}

done_free:
	kfree(ioctl);
	return ret;
}
