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
#include <engine/fifo/priv.h>
#include <engine/fifo/cgrp.h>
#include <engine/fifo/chan.h>
#include <engine/fifo/chid.h>
#include <engine/fifo/runl.h>

#include <core/gpuobj.h>
#include <subdev/gsp.h>
#include <subdev/mmu.h>
#include <subdev/vfn.h>
#include <engine/gr.h>

#include <rm/engine.h>

#include <nvhw/drf.h>

#include "nvrm/fifo.h"
#include "nvrm/engine.h"

static u32
r535_chan_doorbell_handle(struct nvkm_chan *chan)
{
	struct nvkm_gsp *gsp = chan->rm.object.client->gsp;

	return gsp->rm->gpu->fifo.chan.doorbell_handle(chan);
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
r535_chan_alloc(struct nvkm_gsp_device *device, u32 handle, u32 nv2080_engine_type, u8 runq,
		bool priv, int chid, u64 inst_addr, u64 userd_addr, u64 mthdbuf_addr,
		struct nvkm_vmm *vmm, u64 gpfifo_offset, u32 gpfifo_length,
		struct nvkm_gsp_object *chan)
{
	struct nvkm_gsp *gsp = device->object.client->gsp;
	struct nvkm_fifo *fifo = gsp->subdev.device->fifo;
	const int userd_p = chid / CHID_PER_USERD;
	const int userd_i = chid % CHID_PER_USERD;
	NV_CHANNELGPFIFO_ALLOCATION_PARAMETERS *args;

	args = nvkm_gsp_rm_alloc_get(&device->object, handle,
				     fifo->func->chan.user.oclass, sizeof(*args), chan);
	if (WARN_ON(IS_ERR(args)))
		return PTR_ERR(args);

	args->gpFifoOffset = gpfifo_offset;
	args->gpFifoEntries = gpfifo_length / 8;

	args->flags  = NVDEF(NVOS04, FLAGS, CHANNEL_TYPE, PHYSICAL);
	args->flags |= NVDEF(NVOS04, FLAGS, VPR, FALSE);
	args->flags |= NVDEF(NVOS04, FLAGS, CHANNEL_SKIP_MAP_REFCOUNTING, FALSE);
	args->flags |= NVVAL(NVOS04, FLAGS, GROUP_CHANNEL_RUNQUEUE, runq);
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

	args->hVASpace = vmm->rm.object.handle;
	args->engineType = nv2080_engine_type;

	args->instanceMem.base = inst_addr;
	args->instanceMem.size = fifo->func->chan.func->inst->size;
	args->instanceMem.addressSpace = 2;
	args->instanceMem.cacheAttrib = 1;

	args->userdMem.base = userd_addr;
	args->userdMem.size = fifo->func->chan.func->userd->size;
	args->userdMem.addressSpace = 2;
	args->userdMem.cacheAttrib = 1;

	args->ramfcMem.base = inst_addr;
	args->ramfcMem.size = 0x200;
	args->ramfcMem.addressSpace = 2;
	args->ramfcMem.cacheAttrib = 1;

	args->mthdbufMem.base = mthdbuf_addr;
	args->mthdbufMem.size = fifo->rm.mthdbuf_size;
	args->mthdbufMem.addressSpace = 1;
	args->mthdbufMem.cacheAttrib = 0;

	if (!priv)
		args->internalFlags = NVDEF(NV_KERNELCHANNEL, ALLOC_INTERNALFLAGS, PRIVILEGE, USER);
	else
		args->internalFlags = NVDEF(NV_KERNELCHANNEL, ALLOC_INTERNALFLAGS, PRIVILEGE, ADMIN);
	args->internalFlags |= NVDEF(NV_KERNELCHANNEL, ALLOC_INTERNALFLAGS, ERROR_NOTIFIER_TYPE, NONE);
	args->internalFlags |= NVDEF(NV_KERNELCHANNEL, ALLOC_INTERNALFLAGS, ECC_ERROR_NOTIFIER_TYPE, NONE);

	return nvkm_gsp_rm_alloc_wr(chan, args);
}

static int
r535_chan_ramfc_write(struct nvkm_chan *chan, u64 offset, u64 length, u32 devm, bool priv)
{
	struct nvkm_fifo *fifo = chan->cgrp->runl->fifo;
	struct nvkm_engn *engn;
	struct nvkm_device *device = fifo->engine.subdev.device;
	const struct nvkm_rm_api *rmapi = device->gsp->rm->api;
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

	ret = rmapi->fifo->chan.alloc(&chan->vmm->rm.device, NVKM_RM_CHAN(chan->id),
				      eT, chan->runq, priv, chan->id, chan->inst->addr,
				      nvkm_memory_addr(chan->userd.mem) + chan->userd.base,
				      chan->rm.mthdbuf.addr, chan->vmm, offset, length,
				      &chan->rm.object);
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

static const struct nvkm_chan_func
r535_chan = {
	.inst = &gf100_chan_inst,
	.userd = &gv100_chan_userd,
	.ramfc = &r535_chan_ramfc,
	.start = r535_chan_start,
	.stop = r535_chan_stop,
	.doorbell_handle = r535_chan_doorbell_handle,
};

static int
r535_engn_nonstall(struct nvkm_engn *engn)
{
	struct nvkm_subdev *subdev = &engn->engine->subdev;
	int ret;

	ret = nvkm_gsp_intr_nonstall(subdev->device->gsp, subdev->type, subdev->inst);
	WARN_ON(ret == -ENOENT);
	return ret;
}

static const struct nvkm_engn_func
r535_engn_ce = {
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
r535_engn_gr = {
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

void
r535_fifo_rc_chid(struct nvkm_fifo *fifo, int chid)
{
	struct nvkm_chan *chan;
	unsigned long flags;

	chan = nvkm_chan_get_chid(&fifo->engine, chid, &flags);
	if (!chan) {
		nvkm_error(&fifo->engine.subdev, "rc: chid %d not found!\n", chid);
		return;
	}

	nvkm_chan_error(chan, false);
	nvkm_chan_put(&chan, flags);
}

static int
r535_fifo_rc_triggered(void *priv, u32 fn, void *repv, u32 repc)
{
	rpc_rc_triggered_v17_02 *msg = repv;
	struct nvkm_gsp *gsp = priv;

	if (WARN_ON(repc < sizeof(*msg)))
		return -EINVAL;

	nvkm_error(&gsp->subdev, "rc: engn:%08x chid:%d type:%d scope:%d part:%d\n",
		   msg->nv2080EngineType, msg->chid, msg->exceptType, msg->scope,
		   msg->partitionAttributionId);

	r535_fifo_rc_chid(gsp->subdev.device->fifo, msg->chid);
	return 0;
}

static int
r535_fifo_xlat_rm_engine_type(u32 rm, enum nvkm_subdev_type *ptype, int *p2080)
{
#define RM_ENGINE_TYPE(RM,NVKM,INST)              \
	RM_ENGINE_TYPE_##RM:                      \
		*ptype = NVKM_ENGINE_##NVKM;      \
		*p2080 = NV2080_ENGINE_TYPE_##RM; \
		return INST

	switch (rm) {
	case RM_ENGINE_TYPE(    GR0,    GR, 0);
	case RM_ENGINE_TYPE(  COPY0,    CE, 0);
	case RM_ENGINE_TYPE(  COPY1,    CE, 1);
	case RM_ENGINE_TYPE(  COPY2,    CE, 2);
	case RM_ENGINE_TYPE(  COPY3,    CE, 3);
	case RM_ENGINE_TYPE(  COPY4,    CE, 4);
	case RM_ENGINE_TYPE(  COPY5,    CE, 5);
	case RM_ENGINE_TYPE(  COPY6,    CE, 6);
	case RM_ENGINE_TYPE(  COPY7,    CE, 7);
	case RM_ENGINE_TYPE(  COPY8,    CE, 8);
	case RM_ENGINE_TYPE(  COPY9,    CE, 9);
	case RM_ENGINE_TYPE( NVDEC0, NVDEC, 0);
	case RM_ENGINE_TYPE( NVDEC1, NVDEC, 1);
	case RM_ENGINE_TYPE( NVDEC2, NVDEC, 2);
	case RM_ENGINE_TYPE( NVDEC3, NVDEC, 3);
	case RM_ENGINE_TYPE( NVDEC4, NVDEC, 4);
	case RM_ENGINE_TYPE( NVDEC5, NVDEC, 5);
	case RM_ENGINE_TYPE( NVDEC6, NVDEC, 6);
	case RM_ENGINE_TYPE( NVDEC7, NVDEC, 7);
	case RM_ENGINE_TYPE( NVENC0, NVENC, 0);
	case RM_ENGINE_TYPE( NVENC1, NVENC, 1);
	case RM_ENGINE_TYPE( NVENC2, NVENC, 2);
	case RM_ENGINE_TYPE(NVJPEG0, NVJPG, 0);
	case RM_ENGINE_TYPE(NVJPEG1, NVJPG, 1);
	case RM_ENGINE_TYPE(NVJPEG2, NVJPG, 2);
	case RM_ENGINE_TYPE(NVJPEG3, NVJPG, 3);
	case RM_ENGINE_TYPE(NVJPEG4, NVJPG, 4);
	case RM_ENGINE_TYPE(NVJPEG5, NVJPG, 5);
	case RM_ENGINE_TYPE(NVJPEG6, NVJPG, 6);
	case RM_ENGINE_TYPE(NVJPEG7, NVJPG, 7);
	case RM_ENGINE_TYPE(     SW,    SW, 0);
	case RM_ENGINE_TYPE(   SEC2,  SEC2, 0);
	case RM_ENGINE_TYPE(    OFA,   OFA, 0);
	default:
		return -EINVAL;
	}
#undef RM_ENGINE_TYPE
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
	struct nvkm_device *device = subdev->device;
	struct nvkm_gsp *gsp = device->gsp;
	struct nvkm_rm *rm = gsp->rm;
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;
	u32 chids = 2048;
	u32 first = rm->api->fifo->rsvd_chids;
	u32 count = chids - first;
	int ret;
	NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_PARAMS *ctrl;

	if ((ret = nvkm_chid_new(&nvkm_chan_event, subdev, chids, first, count, &fifo->cgid)) ||
	    (ret = nvkm_chid_new(&nvkm_chan_event, subdev, chids, first, count, &fifo->chid)))
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

		inst = rm->api->fifo->xlat_rm_engine_type(rmid, &type, &nv2080);
		if (inst < 0) {
			nvkm_warn(subdev, "RM_ENGINE_TYPE 0x%x\n", rmid);
			nvkm_runl_del(runl);
			continue;
		}

		/* Skip SW engine - there's currently no support for NV SW classes. */
		if (type == NVKM_ENGINE_SW)
			continue;

		/* Skip lone GRCEs (ones not paired with GR on a runlist), as they
		 * don't appear to function as async copy engines.
		 */
		if (type == NVKM_ENGINE_CE &&
		     rm->gpu->ce.grce_mask &&
		    (rm->gpu->ce.grce_mask(device) & BIT(inst)) &&
		    !nvkm_runl_find_engn(engn, runl, engn->engine->subdev.type == NVKM_ENGINE_GR)) {
			RUNL_DEBUG(runl, "skip LCE %d - GRCE without GR", inst);
			nvkm_runl_del(runl);
			continue;
		}

		ret = nvkm_rm_engine_new(gsp->rm, type, inst);
		if (ret) {
			nvkm_runl_del(runl);
			continue;
		}

		engn = NULL;

		switch (type) {
		case NVKM_ENGINE_CE:
			engn = nvkm_runl_add(runl, nv2080, &r535_engn_ce, type, inst);
			break;
		case NVKM_ENGINE_GR:
			engn = nvkm_runl_add(runl, nv2080, &r535_engn_gr, type, inst);
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

	return rm->api->fifo->ectx_size(fifo);
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
	const struct nvkm_rm_gpu *gpu = device->gsp->rm->gpu;
	struct nvkm_fifo_func *rm;

	if (!(rm = kzalloc(sizeof(*rm), GFP_KERNEL)))
		return -ENOMEM;

	rm->dtor = r535_fifo_dtor;
	rm->runl_ctor = r535_fifo_runl_ctor;
	rm->runl = &r535_runl;
	rm->chan.user.oclass = gpu->fifo.chan.class;
	rm->chan.func = &r535_chan;
	rm->nonstall = &ga100_fifo_nonstall;
	rm->nonstall_ctor = ga100_fifo_nonstall_ctor;
	rm->nonstall_dtor = ga100_fifo_nonstall_dtor;

	return nvkm_fifo_new_(rm, device, type, inst, pfifo);
}

const struct nvkm_rm_api_fifo
r535_fifo = {
	.xlat_rm_engine_type = r535_fifo_xlat_rm_engine_type,
	.ectx_size = r535_fifo_ectx_size,
	.rc_triggered = r535_fifo_rc_triggered,
	.chan = {
		.alloc = r535_chan_alloc,
	},
};
