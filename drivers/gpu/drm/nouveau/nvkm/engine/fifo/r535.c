/*
 * Copyright 2023 Red Hat Inc.
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
 */
#include "priv.h"
#include "cgrp.h"
#include "chan.h"
#include "chid.h"
#include "runl.h"

#include <core/gpuobj.h>
#include <subdev/gsp.h>
#include <subdev/mmu.h>
#include <subdev/vfn.h>
#include <engine/gr.h>

#include <nvhw/drf.h>

#include <nvrm/nvtypes.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/alloc/alloc_channel.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/class/cl2080_notification.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080ce.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080fifo.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080gpu.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080internal.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrla06f/ctrla06fgpfifo.h>
#include <nvrm/535.113.01/nvidia/generated/g_kernel_channel_nvoc.h>
#include <nvrm/535.113.01/nvidia/generated/g_kernel_fifo_nvoc.h>
#include <nvrm/535.113.01/nvidia/inc/kernel/gpu/gpu_engine_type.h>

static u32
r535_chan_doorbell_handle(struct nvkm_chan *chan)
{
	return (chan->cgrp->runl->id << 16) | chan->id;
}

static void
r535_chan_stop(struct nvkm_chan *chan)
{
}

static void
r535_chan_start(struct nvkm_chan *chan)
{
}

static void
r535_chan_ramfc_clear(struct nvkm_chan *chan)
{
	struct nvkm_fifo *fifo = chan->cgrp->runl->fifo;

	nvkm_gsp_rm_free(&chan->rm.object);

	dma_free_coherent(fifo->engine.subdev.device->dev, fifo->rm.mthdbuf_size,
			  chan->rm.mthdbuf.ptr, chan->rm.mthdbuf.addr);

	nvkm_cgrp_vctx_put(chan->cgrp, &chan->rm.grctx);
}

#define CHID_PER_USERD 8

static int
r535_chan_ramfc_write(struct nvkm_chan *chan, u64 offset, u64 length, u32 devm, bool priv)
{
	struct nvkm_fifo *fifo = chan->cgrp->runl->fifo;
	struct nvkm_engn *engn;
	struct nvkm_device *device = fifo->engine.subdev.device;
	NV_CHANNELGPFIFO_ALLOCATION_PARAMETERS *args;
	const int userd_p = chan->id / CHID_PER_USERD;
	const int userd_i = chan->id % CHID_PER_USERD;
	u32 eT = ~0;
	int ret;

	if (unlikely(device->gr && !device->gr->engine.subdev.oneinit)) {
		ret = nvkm_subdev_oneinit(&device->gr->engine.subdev);
		if (ret)
			return ret;
	}

	nvkm_runl_foreach_engn(engn, chan->cgrp->runl) {
		eT = engn->id;
		break;
	}

	if (WARN_ON(eT == ~0))
		return -EINVAL;

	chan->rm.mthdbuf.ptr = dma_alloc_coherent(fifo->engine.subdev.device->dev,
						  fifo->rm.mthdbuf_size,
						  &chan->rm.mthdbuf.addr, GFP_KERNEL);
	if (!chan->rm.mthdbuf.ptr)
		return -ENOMEM;

	args = nvkm_gsp_rm_alloc_get(&chan->vmm->rm.device.object, 0xf1f00000 | chan->id,
				     fifo->func->chan.user.oclass, sizeof(*args),
				     &chan->rm.object);
	if (WARN_ON(IS_ERR(args)))
		return PTR_ERR(args);

	args->gpFifoOffset = offset;
	args->gpFifoEntries = length / 8;

	args->flags  = NVDEF(NVOS04, FLAGS, CHANNEL_TYPE, PHYSICAL);
	args->flags |= NVDEF(NVOS04, FLAGS, VPR, FALSE);
	args->flags |= NVDEF(NVOS04, FLAGS, CHANNEL_SKIP_MAP_REFCOUNTING, FALSE);
	args->flags |= NVVAL(NVOS04, FLAGS, GROUP_CHANNEL_RUNQUEUE, chan->runq);
	if (!priv)
		args->flags |= NVDEF(NVOS04, FLAGS, PRIVILEGED_CHANNEL, FALSE);
	else
		args->flags |= NVDEF(NVOS04, FLAGS, PRIVILEGED_CHANNEL, TRUE);
	args->flags |= NVDEF(NVOS04, FLAGS, DELAY_CHANNEL_SCHEDULING, FALSE);
	args->flags |= NVDEF(NVOS04, FLAGS, CHANNEL_DENY_PHYSICAL_MODE_CE, FALSE);

	args->flags |= NVVAL(NVOS04, FLAGS, CHANNEL_USERD_INDEX_VALUE, userd_i);
	args->flags |= NVDEF(NVOS04, FLAGS, CHANNEL_USERD_INDEX_FIXED, FALSE);
	args->flags |= NVVAL(NVOS04, FLAGS, CHANNEL_USERD_INDEX_PAGE_VALUE, userd_p);
	args->flags |= NVDEF(NVOS04, FLAGS, CHANNEL_USERD_INDEX_PAGE_FIXED, TRUE);

	args->flags |= NVDEF(NVOS04, FLAGS, CHANNEL_DENY_AUTH_LEVEL_PRIV, FALSE);
	args->flags |= NVDEF(NVOS04, FLAGS, CHANNEL_SKIP_SCRUBBER, FALSE);
	args->flags |= NVDEF(NVOS04, FLAGS, CHANNEL_CLIENT_MAP_FIFO, FALSE);
	args->flags |= NVDEF(NVOS04, FLAGS, SET_EVICT_LAST_CE_PREFETCH_CHANNEL, FALSE);
	args->flags |= NVDEF(NVOS04, FLAGS, CHANNEL_VGPU_PLUGIN_CONTEXT, FALSE);
	args->flags |= NVDEF(NVOS04, FLAGS, CHANNEL_PBDMA_ACQUIRE_TIMEOUT, FALSE);
	args->flags |= NVDEF(NVOS04, FLAGS, GROUP_CHANNEL_THREAD, DEFAULT);
	args->flags |= NVDEF(NVOS04, FLAGS, MAP_CHANNEL, FALSE);
	args->flags |= NVDEF(NVOS04, FLAGS, SKIP_CTXBUFFER_ALLOC, FALSE);

	args->hVASpace = chan->vmm->rm.object.handle;
	args->engineType = eT;

	args->instanceMem.base = chan->inst->addr;
	args->instanceMem.size = chan->inst->size;
	args->instanceMem.addressSpace = 2;
	args->instanceMem.cacheAttrib = 1;

	args->userdMem.base = nvkm_memory_addr(chan->userd.mem) + chan->userd.base;
	args->userdMem.size = fifo->func->chan.func->userd->size;
	args->userdMem.addressSpace = 2;
	args->userdMem.cacheAttrib = 1;

	args->ramfcMem.base = chan->inst->addr + 0;
	args->ramfcMem.size = 0x200;
	args->ramfcMem.addressSpace = 2;
	args->ramfcMem.cacheAttrib = 1;

	args->mthdbufMem.base = chan->rm.mthdbuf.addr;
	args->mthdbufMem.size = fifo->rm.mthdbuf_size;
	args->mthdbufMem.addressSpace = 1;
	args->mthdbufMem.cacheAttrib = 0;

	if (!priv)
		args->internalFlags = NVDEF(NV_KERNELCHANNEL, ALLOC_INTERNALFLAGS, PRIVILEGE, USER);
	else
		args->internalFlags = NVDEF(NV_KERNELCHANNEL, ALLOC_INTERNALFLAGS, PRIVILEGE, ADMIN);
	args->internalFlags |= NVDEF(NV_KERNELCHANNEL, ALLOC_INTERNALFLAGS, ERROR_NOTIFIER_TYPE, NONE);
	args->internalFlags |= NVDEF(NV_KERNELCHANNEL, ALLOC_INTERNALFLAGS, ECC_ERROR_NOTIFIER_TYPE, NONE);

	ret = nvkm_gsp_rm_alloc_wr(&chan->rm.object, args);
	if (ret)
		return ret;

	if (1) {
		NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS *ctrl;

		if (1) {
			NVA06F_CTRL_BIND_PARAMS *ctrl;

			ctrl = nvkm_gsp_rm_ctrl_get(&chan->rm.object,
						    NVA06F_CTRL_CMD_BIND, sizeof(*ctrl));
			if (WARN_ON(IS_ERR(ctrl)))
				return PTR_ERR(ctrl);

			ctrl->engineType = eT;

			ret = nvkm_gsp_rm_ctrl_wr(&chan->rm.object, ctrl);
			if (ret)
				return ret;
		}

		ctrl = nvkm_gsp_rm_ctrl_get(&chan->rm.object,
					    NVA06F_CTRL_CMD_GPFIFO_SCHEDULE, sizeof(*ctrl));
		if (WARN_ON(IS_ERR(ctrl)))
			return PTR_ERR(ctrl);

		ctrl->bEnable = 1;
		ret = nvkm_gsp_rm_ctrl_wr(&chan->rm.object, ctrl);
	}

	return ret;
}

static const struct nvkm_chan_func_ramfc
r535_chan_ramfc = {
	.write = r535_chan_ramfc_write,
	.clear = r535_chan_ramfc_clear,
	.devm = 0xfff,
	.priv = true,
};

struct r535_chan_userd {
	struct nvkm_memory *mem;
	struct nvkm_memory *map;
	int chid;
	u32 used;

	struct list_head head;
} *userd;

static void
r535_chan_id_put(struct nvkm_chan *chan)
{
	struct nvkm_runl *runl = chan->cgrp->runl;
	struct nvkm_fifo *fifo = runl->fifo;
	struct r535_chan_userd *userd;

	mutex_lock(&fifo->userd.mutex);
	list_for_each_entry(userd, &fifo->userd.list, head) {
		if (userd->map == chan->userd.mem) {
			u32 chid = chan->userd.base / chan->func->userd->size;

			userd->used &= ~BIT(chid);
			if (!userd->used) {
				nvkm_memory_unref(&userd->map);
				nvkm_memory_unref(&userd->mem);
				nvkm_chid_put(runl->chid, userd->chid, &chan->cgrp->lock);
				list_del(&userd->head);
			}

			break;
		}
	}
	mutex_unlock(&fifo->userd.mutex);

}

static int
r535_chan_id_get_locked(struct nvkm_chan *chan, struct nvkm_memory *muserd, u64 ouserd)
{
	const u32 userd_size = CHID_PER_USERD * chan->func->userd->size;
	struct nvkm_runl *runl = chan->cgrp->runl;
	struct nvkm_fifo *fifo = runl->fifo;
	struct r535_chan_userd *userd;
	u32 chid;
	int ret;

	if (ouserd + chan->func->userd->size >= userd_size ||
	    (ouserd & (chan->func->userd->size - 1))) {
		RUNL_DEBUG(runl, "ouserd %llx", ouserd);
		return -EINVAL;
	}

	chid = div_u64(ouserd, chan->func->userd->size);

	list_for_each_entry(userd, &fifo->userd.list, head) {
		if (userd->mem == muserd) {
			if (userd->used & BIT(chid))
				return -EBUSY;
			break;
		}
	}

	if (&userd->head == &fifo->userd.list) {
		if (nvkm_memory_size(muserd) < userd_size) {
			RUNL_DEBUG(runl, "userd too small");
			return -EINVAL;
		}

		userd = kzalloc(sizeof(*userd), GFP_KERNEL);
		if (!userd)
			return -ENOMEM;

		userd->chid = nvkm_chid_get(runl->chid, chan);
		if (userd->chid < 0) {
			ret = userd->chid;
			kfree(userd);
			return ret;
		}

		userd->mem = nvkm_memory_ref(muserd);

		ret = nvkm_memory_kmap(userd->mem, &userd->map);
		if (ret) {
			nvkm_chid_put(runl->chid, userd->chid, &chan->cgrp->lock);
			kfree(userd);
			return ret;
		}


		list_add(&userd->head, &fifo->userd.list);
	}

	userd->used |= BIT(chid);

	chan->userd.mem = nvkm_memory_ref(userd->map);
	chan->userd.base = ouserd;

	return (userd->chid * CHID_PER_USERD) + chid;
}

static int
r535_chan_id_get(struct nvkm_chan *chan, struct nvkm_memory *muserd, u64 ouserd)
{
	struct nvkm_fifo *fifo = chan->cgrp->runl->fifo;
	int ret;

	mutex_lock(&fifo->userd.mutex);
	ret = r535_chan_id_get_locked(chan, muserd, ouserd);
	mutex_unlock(&fifo->userd.mutex);
	return ret;
}

static const struct nvkm_chan_func
r535_chan = {
	.id_get = r535_chan_id_get,
	.id_put = r535_chan_id_put,
	.inst = &gf100_chan_inst,
	.userd = &gv100_chan_userd,
	.ramfc = &r535_chan_ramfc,
	.start = r535_chan_start,
	.stop = r535_chan_stop,
	.doorbell_handle = r535_chan_doorbell_handle,
};

static const struct nvkm_cgrp_func
r535_cgrp = {
};

static int
r535_engn_nonstall(struct nvkm_engn *engn)
{
	struct nvkm_subdev *subdev = &engn->engine->subdev;
	int ret;

	ret = nvkm_gsp_intr_nonstall(subdev->device->gsp, subdev->type, subdev->inst);
	WARN_ON(ret < 0);
	return ret;
}

static const struct nvkm_engn_func
r535_ce = {
	.nonstall = r535_engn_nonstall,
};

static int
r535_gr_ctor(struct nvkm_engn *engn, struct nvkm_vctx *vctx, struct nvkm_chan *chan)
{
	/* RM requires GR context buffers to remain mapped until after the
	 * channel has been destroyed (as opposed to after the last gr obj
	 * has been deleted).
	 *
	 * Take an extra ref here, which will be released once the channel
	 * object has been deleted.
	 */
	refcount_inc(&vctx->refs);
	chan->rm.grctx = vctx;
	return 0;
}

static const struct nvkm_engn_func
r535_gr = {
	.nonstall = r535_engn_nonstall,
	.ctor2 = r535_gr_ctor,
};

static int
r535_flcn_bind(struct nvkm_engn *engn, struct nvkm_vctx *vctx, struct nvkm_chan *chan)
{
	struct nvkm_gsp_client *client = &chan->vmm->rm.client;
	NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS *ctrl;

	ctrl = nvkm_gsp_rm_ctrl_get(&chan->vmm->rm.device.subdevice,
				    NV2080_CTRL_CMD_GPU_PROMOTE_CTX, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->hClient = client->object.handle;
	ctrl->hObject = chan->rm.object.handle;
	ctrl->hChanClient = client->object.handle;
	ctrl->virtAddress = vctx->vma->addr;
	ctrl->size = vctx->inst->size;
	ctrl->engineType = engn->id;
	ctrl->ChID = chan->id;

	return nvkm_gsp_rm_ctrl_wr(&chan->vmm->rm.device.subdevice, ctrl);
}

static int
r535_flcn_ctor(struct nvkm_engn *engn, struct nvkm_vctx *vctx, struct nvkm_chan *chan)
{
	int ret;

	if (WARN_ON(!engn->rm.size))
		return -EINVAL;

	ret = nvkm_gpuobj_new(engn->engine->subdev.device, engn->rm.size, 0, true, NULL,
			      &vctx->inst);
	if (ret)
		return ret;

	ret = nvkm_vmm_get(vctx->vmm, 12, vctx->inst->size, &vctx->vma);
	if (ret)
		return ret;

	ret = nvkm_memory_map(vctx->inst, 0, vctx->vmm, vctx->vma, NULL, 0);
	if (ret)
		return ret;

	return r535_flcn_bind(engn, vctx, chan);
}

static const struct nvkm_engn_func
r535_flcn = {
	.nonstall = r535_engn_nonstall,
	.ctor2 = r535_flcn_ctor,
};

static void
r535_runl_allow(struct nvkm_runl *runl, u32 engm)
{
}

static void
r535_runl_block(struct nvkm_runl *runl, u32 engm)
{
}

static const struct nvkm_runl_func
r535_runl = {
	.block = r535_runl_block,
	.allow = r535_runl_allow,
};

static int
r535_fifo_2080_type(enum nvkm_subdev_type type, int inst)
{
	switch (type) {
	case NVKM_ENGINE_GR: return NV2080_ENGINE_TYPE_GR0;
	case NVKM_ENGINE_CE: return NV2080_ENGINE_TYPE_COPY0 + inst;
	case NVKM_ENGINE_SEC2: return NV2080_ENGINE_TYPE_SEC2;
	case NVKM_ENGINE_NVDEC: return NV2080_ENGINE_TYPE_NVDEC0 + inst;
	case NVKM_ENGINE_NVENC: return NV2080_ENGINE_TYPE_NVENC0 + inst;
	case NVKM_ENGINE_NVJPG: return NV2080_ENGINE_TYPE_NVJPEG0 + inst;
	case NVKM_ENGINE_OFA: return NV2080_ENGINE_TYPE_OFA;
	case NVKM_ENGINE_SW: return NV2080_ENGINE_TYPE_SW;
	default:
		break;
	}

	WARN_ON(1);
	return -EINVAL;
}

static int
r535_fifo_engn_type(RM_ENGINE_TYPE rm, enum nvkm_subdev_type *ptype)
{
	switch (rm) {
	case RM_ENGINE_TYPE_GR0:
		*ptype = NVKM_ENGINE_GR;
		return 0;
	case RM_ENGINE_TYPE_COPY0...RM_ENGINE_TYPE_COPY9:
		*ptype = NVKM_ENGINE_CE;
		return rm - RM_ENGINE_TYPE_COPY0;
	case RM_ENGINE_TYPE_NVDEC0...RM_ENGINE_TYPE_NVDEC7:
		*ptype = NVKM_ENGINE_NVDEC;
		return rm - RM_ENGINE_TYPE_NVDEC0;
	case RM_ENGINE_TYPE_NVENC0...RM_ENGINE_TYPE_NVENC2:
		*ptype = NVKM_ENGINE_NVENC;
		return rm - RM_ENGINE_TYPE_NVENC0;
	case RM_ENGINE_TYPE_SW:
		*ptype = NVKM_ENGINE_SW;
		return 0;
	case RM_ENGINE_TYPE_SEC2:
		*ptype = NVKM_ENGINE_SEC2;
		return 0;
	case RM_ENGINE_TYPE_NVJPEG0...RM_ENGINE_TYPE_NVJPEG7:
		*ptype = NVKM_ENGINE_NVJPG;
		return rm - RM_ENGINE_TYPE_NVJPEG0;
	case RM_ENGINE_TYPE_OFA:
		*ptype = NVKM_ENGINE_OFA;
		return 0;
	default:
		return -EINVAL;
	}
}

static int
r535_fifo_ectx_size(struct nvkm_fifo *fifo)
{
	NV2080_CTRL_INTERNAL_GET_CONSTRUCTED_FALCON_INFO_PARAMS *ctrl;
	struct nvkm_gsp *gsp = fifo->engine.subdev.device->gsp;
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;

	ctrl = nvkm_gsp_rm_ctrl_rd(&gsp->internal.device.subdevice,
				   NV2080_CTRL_CMD_INTERNAL_GET_CONSTRUCTED_FALCON_INFO,
				   sizeof(*ctrl));
	if (WARN_ON(IS_ERR(ctrl)))
		return PTR_ERR(ctrl);

	for (int i = 0; i < ctrl->numConstructedFalcons; i++) {
		nvkm_runl_foreach(runl, fifo) {
			nvkm_runl_foreach_engn(engn, runl) {
				if (engn->rm.desc == ctrl->constructedFalconsTable[i].engDesc) {
					engn->rm.size =
						ctrl->constructedFalconsTable[i].ctxBufferSize;
					break;
				}
			}
		}
	}

	nvkm_gsp_rm_ctrl_done(&gsp->internal.device.subdevice, ctrl);
	return 0;
}

static int
r535_fifo_runl_ctor(struct nvkm_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_gsp *gsp = subdev->device->gsp;
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;
	u32 cgids = 2048;
	u32 chids = 2048;
	int ret;
	NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_PARAMS *ctrl;

	if ((ret = nvkm_chid_new(&nvkm_chan_event, subdev, cgids, 0, cgids, &fifo->cgid)) ||
	    (ret = nvkm_chid_new(&nvkm_chan_event, subdev, chids, 0, chids, &fifo->chid)))
		return ret;

	ctrl = nvkm_gsp_rm_ctrl_rd(&gsp->internal.device.subdevice,
				   NV2080_CTRL_CMD_FIFO_GET_DEVICE_INFO_TABLE, sizeof(*ctrl));
	if (WARN_ON(IS_ERR(ctrl)))
		return PTR_ERR(ctrl);

	for (int i = 0; i < ctrl->numEntries; i++) {
		const u32 addr = ctrl->entries[i].engineData[ENGINE_INFO_TYPE_RUNLIST_PRI_BASE];
		const u32 id = ctrl->entries[i].engineData[ENGINE_INFO_TYPE_RUNLIST];

		runl = nvkm_runl_get(fifo, id, addr);
		if (!runl) {
			runl = nvkm_runl_new(fifo, id, addr, 0);
			if (WARN_ON(IS_ERR(runl)))
				continue;
		}
	}

	for (int i = 0; i < ctrl->numEntries; i++) {
		const u32 addr = ctrl->entries[i].engineData[ENGINE_INFO_TYPE_RUNLIST_PRI_BASE];
		const u32 rmid = ctrl->entries[i].engineData[ENGINE_INFO_TYPE_RM_ENGINE_TYPE];
		const u32 id = ctrl->entries[i].engineData[ENGINE_INFO_TYPE_RUNLIST];
		enum nvkm_subdev_type type;
		int inst, nv2080;

		runl = nvkm_runl_get(fifo, id, addr);
		if (!runl)
			continue;

		inst = r535_fifo_engn_type(rmid, &type);
		if (inst < 0) {
			nvkm_warn(subdev, "RM_ENGINE_TYPE 0x%x\n", rmid);
			nvkm_runl_del(runl);
			continue;
		}

		nv2080 = r535_fifo_2080_type(type, inst);
		if (nv2080 < 0) {
			nvkm_runl_del(runl);
			continue;
		}

		switch (type) {
		case NVKM_ENGINE_CE:
			engn = nvkm_runl_add(runl, nv2080, &r535_ce, type, inst);
			break;
		case NVKM_ENGINE_GR:
			engn = nvkm_runl_add(runl, nv2080, &r535_gr, type, inst);
			break;
		case NVKM_ENGINE_NVDEC:
		case NVKM_ENGINE_NVENC:
		case NVKM_ENGINE_NVJPG:
		case NVKM_ENGINE_OFA:
			engn = nvkm_runl_add(runl, nv2080, &r535_flcn, type, inst);
			break;
		case NVKM_ENGINE_SW:
			continue;
		default:
			engn = NULL;
			break;
		}

		if (!engn) {
			nvkm_runl_del(runl);
			continue;
		}

		engn->rm.desc = ctrl->entries[i].engineData[ENGINE_INFO_TYPE_ENG_DESC];
	}

	nvkm_gsp_rm_ctrl_done(&gsp->internal.device.subdevice, ctrl);

	{
		NV2080_CTRL_CE_GET_FAULT_METHOD_BUFFER_SIZE_PARAMS *ctrl;

		ctrl = nvkm_gsp_rm_ctrl_rd(&gsp->internal.device.subdevice,
					   NV2080_CTRL_CMD_CE_GET_FAULT_METHOD_BUFFER_SIZE,
					   sizeof(*ctrl));
		if (IS_ERR(ctrl))
			return PTR_ERR(ctrl);

		fifo->rm.mthdbuf_size = ctrl->size;

		nvkm_gsp_rm_ctrl_done(&gsp->internal.device.subdevice, ctrl);
	}

	return r535_fifo_ectx_size(fifo);
}

static void
r535_fifo_dtor(struct nvkm_fifo *fifo)
{
	kfree(fifo->func);
}

int
r535_fifo_new(const struct nvkm_fifo_func *hw, struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, struct nvkm_fifo **pfifo)
{
	struct nvkm_fifo_func *rm;

	if (!(rm = kzalloc(sizeof(*rm), GFP_KERNEL)))
		return -ENOMEM;

	rm->dtor = r535_fifo_dtor;
	rm->runl_ctor = r535_fifo_runl_ctor;
	rm->runl = &r535_runl;
	rm->cgrp = hw->cgrp;
	rm->cgrp.func = &r535_cgrp;
	rm->chan = hw->chan;
	rm->chan.func = &r535_chan;
	rm->nonstall = &ga100_fifo_nonstall;
	rm->nonstall_ctor = ga100_fifo_nonstall_ctor;

	return nvkm_fifo_new_(rm, device, type, inst, pfifo);
}
