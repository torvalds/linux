/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <rm/rm.h>

#include <subdev/mmu.h>
#include <engine/fifo/priv.h>
#include <engine/fifo/chan.h>
#include <engine/fifo/runl.h>

#include "nvhw/drf.h"

#include "nvrm/fifo.h"
#include "nvrm/engine.h"

#define CHID_PER_USERD 8

static int
r570_chan_alloc(struct nvkm_gsp_device *device, u32 handle, u32 nv2080_engine_type, u8 runq,
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
r570_fifo_rc_triggered(void *priv, u32 fn, void *repv, u32 repc)
{
	rpc_rc_triggered_v17_02 *msg = repv;
	struct nvkm_gsp *gsp = priv;

	if (WARN_ON(repc < sizeof(*msg)))
		return -EINVAL;

	nvkm_error(&gsp->subdev, "rc engn:%08x chid:%d gfid:%d level:%d type:%d scope:%d part:%d "
				 "fault_addr:%08x%08x fault_type:%08x\n",
		   msg->nv2080EngineType, msg->chid, msg->gfid, msg->exceptLevel, msg->exceptType,
		   msg->scope, msg->partitionAttributionId,
		   msg->mmuFaultAddrHi, msg->mmuFaultAddrLo, msg->mmuFaultType);

	r535_fifo_rc_chid(gsp->subdev.device->fifo, msg->chid);
	return 0;
}

static int
r570_fifo_ectx_size(struct nvkm_fifo *fifo)
{
	NV2080_CTRL_GPU_GET_CONSTRUCTED_FALCON_INFO_PARAMS *ctrl;
	struct nvkm_gsp *gsp = fifo->engine.subdev.device->gsp;
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;

	ctrl = nvkm_gsp_rm_ctrl_rd(&gsp->internal.device.subdevice,
				   NV2080_CTRL_CMD_GPU_GET_CONSTRUCTED_FALCON_INFO,
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
r570_fifo_xlat_rm_engine_type(u32 rm, enum nvkm_subdev_type *ptype, int *p2080)
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
	case RM_ENGINE_TYPE( COPY10,    CE, 10);
	case RM_ENGINE_TYPE( COPY11,    CE, 11);
	case RM_ENGINE_TYPE( COPY12,    CE, 12);
	case RM_ENGINE_TYPE( COPY13,    CE, 13);
	case RM_ENGINE_TYPE( COPY14,    CE, 14);
	case RM_ENGINE_TYPE( COPY15,    CE, 15);
	case RM_ENGINE_TYPE( COPY16,    CE, 16);
	case RM_ENGINE_TYPE( COPY17,    CE, 17);
	case RM_ENGINE_TYPE( COPY18,    CE, 18);
	case RM_ENGINE_TYPE( COPY19,    CE, 19);
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
	case RM_ENGINE_TYPE( NVENC3, NVENC, 3);
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
	case RM_ENGINE_TYPE(   OFA0,   OFA, 0);
	case RM_ENGINE_TYPE(   OFA1,   OFA, 1);
	default:
		return -EINVAL;
	}
#undef RM_ENGINE_TYPE
}

const struct nvkm_rm_api_fifo
r570_fifo = {
	.xlat_rm_engine_type = r570_fifo_xlat_rm_engine_type,
	.ectx_size = r570_fifo_ectx_size,
	.rsvd_chids = 1,
	.rc_triggered = r570_fifo_rc_triggered,
	.chan = {
		.alloc = r570_chan_alloc,
	},
};
