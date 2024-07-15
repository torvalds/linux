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
#include "gf100.h"

#include <core/memory.h>
#include <subdev/gsp.h>
#include <subdev/mmu/vmm.h>
#include <engine/fifo/priv.h>

#include <nvif/if900d.h>

#include <nvhw/drf.h>

#include <nvrm/nvtypes.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/alloc/alloc_channel.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrl0080/ctrl0080fifo.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080gpu.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080internal.h>
#include <nvrm/535.113.01/nvidia/generated/g_kernel_channel_nvoc.h>

#define r535_gr(p) container_of((p), struct r535_gr, base)

#define R515_GR_MAX_CTXBUFS 9

struct r535_gr {
	struct nvkm_gr base;

	struct {
		u16 bufferId;
		u32 size;
		u8  page;
		u8  align;
		bool global;
		bool init;
		bool ro;
	} ctxbuf[R515_GR_MAX_CTXBUFS];
	int ctxbuf_nr;

	struct nvkm_memory *ctxbuf_mem[R515_GR_MAX_CTXBUFS];
};

struct r535_gr_chan {
	struct nvkm_object object;
	struct r535_gr *gr;

	struct nvkm_vmm *vmm;
	struct nvkm_chan *chan;

	struct nvkm_memory *mem[R515_GR_MAX_CTXBUFS];
	struct nvkm_vma    *vma[R515_GR_MAX_CTXBUFS];
};

struct r535_gr_obj {
	struct nvkm_object object;
	struct nvkm_gsp_object rm;
};

static void *
r535_gr_obj_dtor(struct nvkm_object *object)
{
	struct r535_gr_obj *obj = container_of(object, typeof(*obj), object);

	nvkm_gsp_rm_free(&obj->rm);
	return obj;
}

static const struct nvkm_object_func
r535_gr_obj = {
	.dtor = r535_gr_obj_dtor,
};

static int
r535_gr_obj_ctor(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		 struct nvkm_object **pobject)
{
	struct r535_gr_chan *chan = container_of(oclass->parent, typeof(*chan), object);
	struct r535_gr_obj *obj;

	if (!(obj = kzalloc(sizeof(*obj), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_object_ctor(&r535_gr_obj, oclass, &obj->object);
	*pobject = &obj->object;

	return nvkm_gsp_rm_alloc(&chan->chan->rm.object, oclass->handle, oclass->base.oclass, 0,
				 &obj->rm);
}

static void *
r535_gr_chan_dtor(struct nvkm_object *object)
{
	struct r535_gr_chan *grc = container_of(object, typeof(*grc), object);
	struct r535_gr *gr = grc->gr;

	for (int i = 0; i < gr->ctxbuf_nr; i++) {
		nvkm_vmm_put(grc->vmm, &grc->vma[i]);
		nvkm_memory_unref(&grc->mem[i]);
	}

	nvkm_vmm_unref(&grc->vmm);
	return grc;
}

static const struct nvkm_object_func
r535_gr_chan = {
	.dtor = r535_gr_chan_dtor,
};

static int
r535_gr_promote_ctx(struct r535_gr *gr, bool golden, struct nvkm_vmm *vmm,
		    struct nvkm_memory **pmem, struct nvkm_vma **pvma,
		    struct nvkm_gsp_object *chan)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS *ctrl;

	ctrl = nvkm_gsp_rm_ctrl_get(&vmm->rm.device.subdevice,
				    NV2080_CTRL_CMD_GPU_PROMOTE_CTX, sizeof(*ctrl));
	if (WARN_ON(IS_ERR(ctrl)))
		return PTR_ERR(ctrl);

	ctrl->engineType = 1;
	ctrl->hChanClient = vmm->rm.client.object.handle;
	ctrl->hObject = chan->handle;

	for (int i = 0; i < gr->ctxbuf_nr; i++) {
		NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY *entry =
			&ctrl->promoteEntry[ctrl->entryCount];
		const bool alloc = golden || !gr->ctxbuf[i].global;
		int ret;

		entry->bufferId = gr->ctxbuf[i].bufferId;
		entry->bInitialize = gr->ctxbuf[i].init && alloc;

		if (alloc) {
			ret = nvkm_memory_new(device, gr->ctxbuf[i].init ?
					      NVKM_MEM_TARGET_INST : NVKM_MEM_TARGET_INST_SR_LOST,
					      gr->ctxbuf[i].size, 1 << gr->ctxbuf[i].page,
					      gr->ctxbuf[i].init, &pmem[i]);
			if (WARN_ON(ret))
				return ret;

			if (gr->ctxbuf[i].bufferId ==
					NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_PRIV_ACCESS_MAP)
				entry->bNonmapped = 1;
		} else {
			if (gr->ctxbuf[i].bufferId ==
				NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_UNRESTRICTED_PRIV_ACCESS_MAP)
				continue;

			pmem[i] = nvkm_memory_ref(gr->ctxbuf_mem[i]);
		}

		if (!entry->bNonmapped) {
			struct gf100_vmm_map_v0 args = {
				.priv = 1,
				.ro   = gr->ctxbuf[i].ro,
			};

			mutex_lock(&vmm->mutex.vmm);
			ret = nvkm_vmm_get_locked(vmm, false, true, false, 0, gr->ctxbuf[i].align,
						  nvkm_memory_size(pmem[i]), &pvma[i]);
			mutex_unlock(&vmm->mutex.vmm);
			if (ret)
				return ret;

			ret = nvkm_memory_map(pmem[i], 0, vmm, pvma[i], &args, sizeof(args));
			if (ret)
				return ret;

			entry->gpuVirtAddr = pvma[i]->addr;
		}

		if (entry->bInitialize) {
			entry->gpuPhysAddr = nvkm_memory_addr(pmem[i]);
			entry->size = gr->ctxbuf[i].size;
			entry->physAttr = 4;
		}

		nvkm_debug(subdev,
			   "promote %02d: pa %016llx/%08x sz %016llx va %016llx init:%d nm:%d\n",
			   entry->bufferId, entry->gpuPhysAddr, entry->physAttr, entry->size,
			   entry->gpuVirtAddr, entry->bInitialize, entry->bNonmapped);

		ctrl->entryCount++;
	}

	return nvkm_gsp_rm_ctrl_wr(&vmm->rm.device.subdevice, ctrl);
}

static int
r535_gr_chan_new(struct nvkm_gr *base, struct nvkm_chan *chan, const struct nvkm_oclass *oclass,
		 struct nvkm_object **pobject)
{
	struct r535_gr *gr = r535_gr(base);
	struct r535_gr_chan *grc;
	int ret;

	if (!(grc = kzalloc(sizeof(*grc), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_object_ctor(&r535_gr_chan, oclass, &grc->object);
	grc->gr = gr;
	grc->vmm = nvkm_vmm_ref(chan->vmm);
	grc->chan = chan;
	*pobject = &grc->object;

	ret = r535_gr_promote_ctx(gr, false, grc->vmm, grc->mem, grc->vma, &chan->rm.object);
	if (ret)
		return ret;

	return 0;
}

static u64
r535_gr_units(struct nvkm_gr *gr)
{
	struct nvkm_gsp *gsp = gr->engine.subdev.device->gsp;

	return (gsp->gr.tpcs << 8) | gsp->gr.gpcs;
}

static int
r535_gr_oneinit(struct nvkm_gr *base)
{
	NV2080_CTRL_INTERNAL_STATIC_GR_GET_CONTEXT_BUFFERS_INFO_PARAMS *info;
	struct r535_gr *gr = container_of(base, typeof(*gr), base);
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_gsp *gsp = device->gsp;
	struct nvkm_mmu *mmu = device->mmu;
	struct {
		struct nvkm_memory *inst;
		struct nvkm_vmm *vmm;
		struct nvkm_gsp_object chan;
		struct nvkm_vma *vma[R515_GR_MAX_CTXBUFS];
	} golden = {};
	int ret;

	/* Allocate a channel to use for golden context init. */
	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x12000, 0, true, &golden.inst);
	if (ret)
		goto done;

	ret = nvkm_vmm_new(device, 0x1000, 0, NULL, 0, NULL, "grGoldenVmm", &golden.vmm);
	if (ret)
		goto done;

	ret = mmu->func->promote_vmm(golden.vmm);
	if (ret)
		goto done;

	{
		NV_CHANNELGPFIFO_ALLOCATION_PARAMETERS *args;

		args = nvkm_gsp_rm_alloc_get(&golden.vmm->rm.device.object, 0xf1f00000,
					     device->fifo->func->chan.user.oclass,
					     sizeof(*args), &golden.chan);
		if (IS_ERR(args)) {
			ret = PTR_ERR(args);
			goto done;
		}

		args->gpFifoOffset = 0;
		args->gpFifoEntries = 0x1000 / 8;
		args->flags =
			NVDEF(NVOS04, FLAGS, CHANNEL_TYPE, PHYSICAL) |
			NVDEF(NVOS04, FLAGS, VPR, FALSE) |
			NVDEF(NVOS04, FLAGS, CHANNEL_SKIP_MAP_REFCOUNTING, FALSE) |
			NVVAL(NVOS04, FLAGS, GROUP_CHANNEL_RUNQUEUE, 0) |
			NVDEF(NVOS04, FLAGS, PRIVILEGED_CHANNEL, TRUE) |
			NVDEF(NVOS04, FLAGS, DELAY_CHANNEL_SCHEDULING, FALSE) |
			NVDEF(NVOS04, FLAGS, CHANNEL_DENY_PHYSICAL_MODE_CE, FALSE) |
			NVVAL(NVOS04, FLAGS, CHANNEL_USERD_INDEX_VALUE, 0) |
			NVDEF(NVOS04, FLAGS, CHANNEL_USERD_INDEX_FIXED, FALSE) |
			NVVAL(NVOS04, FLAGS, CHANNEL_USERD_INDEX_PAGE_VALUE, 0) |
			NVDEF(NVOS04, FLAGS, CHANNEL_USERD_INDEX_PAGE_FIXED, TRUE) |
			NVDEF(NVOS04, FLAGS, CHANNEL_DENY_AUTH_LEVEL_PRIV, FALSE) |
			NVDEF(NVOS04, FLAGS, CHANNEL_SKIP_SCRUBBER, FALSE) |
			NVDEF(NVOS04, FLAGS, CHANNEL_CLIENT_MAP_FIFO, FALSE) |
			NVDEF(NVOS04, FLAGS, SET_EVICT_LAST_CE_PREFETCH_CHANNEL, FALSE) |
			NVDEF(NVOS04, FLAGS, CHANNEL_VGPU_PLUGIN_CONTEXT, FALSE) |
			NVDEF(NVOS04, FLAGS, CHANNEL_PBDMA_ACQUIRE_TIMEOUT, FALSE) |
			NVDEF(NVOS04, FLAGS, GROUP_CHANNEL_THREAD, DEFAULT) |
			NVDEF(NVOS04, FLAGS, MAP_CHANNEL, FALSE) |
			NVDEF(NVOS04, FLAGS, SKIP_CTXBUFFER_ALLOC, FALSE);
		args->hVASpace = golden.vmm->rm.object.handle;
		args->engineType = 1;
		args->instanceMem.base = nvkm_memory_addr(golden.inst);
		args->instanceMem.size = 0x1000;
		args->instanceMem.addressSpace = 2;
		args->instanceMem.cacheAttrib = 1;
		args->ramfcMem.base = nvkm_memory_addr(golden.inst);
		args->ramfcMem.size = 0x200;
		args->ramfcMem.addressSpace = 2;
		args->ramfcMem.cacheAttrib = 1;
		args->userdMem.base = nvkm_memory_addr(golden.inst) + 0x1000;
		args->userdMem.size = 0x200;
		args->userdMem.addressSpace = 2;
		args->userdMem.cacheAttrib = 1;
		args->mthdbufMem.base = nvkm_memory_addr(golden.inst) + 0x2000;
		args->mthdbufMem.size = 0x5000;
		args->mthdbufMem.addressSpace = 2;
		args->mthdbufMem.cacheAttrib = 1;
		args->internalFlags =
			NVDEF(NV_KERNELCHANNEL, ALLOC_INTERNALFLAGS, PRIVILEGE, ADMIN) |
			NVDEF(NV_KERNELCHANNEL, ALLOC_INTERNALFLAGS, ERROR_NOTIFIER_TYPE, NONE) |
			NVDEF(NV_KERNELCHANNEL, ALLOC_INTERNALFLAGS, ECC_ERROR_NOTIFIER_TYPE, NONE);

		ret = nvkm_gsp_rm_alloc_wr(&golden.chan, args);
		if (ret)
			goto done;
	}

	/* Fetch context buffer info from RM and allocate each of them here to use
	 * during golden context init (or later as a global context buffer).
	 *
	 * Also build the information that'll be used to create channel contexts.
	 */
	info = nvkm_gsp_rm_ctrl_rd(&gsp->internal.device.subdevice,
				   NV2080_CTRL_CMD_INTERNAL_STATIC_KGR_GET_CONTEXT_BUFFERS_INFO,
				   sizeof(*info));
	if (WARN_ON(IS_ERR(info))) {
		ret = PTR_ERR(info);
		goto done;
	}

	for (int i = 0; i < ARRAY_SIZE(info->engineContextBuffersInfo[0].engine); i++) {
		static const struct {
			u32     id0; /* NV0080_CTRL_FIFO_GET_ENGINE_CONTEXT_PROPERTIES_ENGINE_ID */
			u32     id1; /* NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID */
			bool global;
			bool   init;
			bool     ro;
		} map[] = {
#define _A(n,N,G,I,R) { .id0 = NV0080_CTRL_FIFO_GET_ENGINE_CONTEXT_PROPERTIES_ENGINE_ID_##n, \
			.id1 = NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_##N, \
			.global = (G), .init = (I), .ro = (R) }
#define _B(N,G,I,R) _A(GRAPHICS_##N, N, (G), (I), (R))
			/*                                       global   init     ro */
			_A(           GRAPHICS,             MAIN, false,  true, false),
			_B(                                PATCH, false,  true, false),
			_A( GRAPHICS_BUNDLE_CB, BUFFER_BUNDLE_CB,  true, false, false),
			_B(                             PAGEPOOL,  true, false, false),
			_B(                         ATTRIBUTE_CB,  true, false, false),
			_B(                        RTV_CB_GLOBAL,  true, false, false),
			_B(                           FECS_EVENT,  true,  true, false),
			_B(                      PRIV_ACCESS_MAP,  true,  true,  true),
#undef _B
#undef _A
		};
		u32 size = info->engineContextBuffersInfo[0].engine[i].size;
		u8 align, page;
		int id;

		for (id = 0; id < ARRAY_SIZE(map); id++) {
			if (map[id].id0 == i)
				break;
		}

		nvkm_debug(subdev, "%02x: size:0x%08x %s\n", i,
			   size, (id < ARRAY_SIZE(map)) ? "*" : "");
		if (id >= ARRAY_SIZE(map))
			continue;

		if (map[id].id1 == NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_MAIN)
			size = ALIGN(size, 0x1000) + 64 * 0x1000; /* per-subctx headers */

		if      (size >= 1 << 21) page = 21;
		else if (size >= 1 << 16) page = 16;
		else			  page = 12;

		if (map[id].id1 == NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_ATTRIBUTE_CB)
			align = order_base_2(size);
		else
			align = page;

		if (WARN_ON(gr->ctxbuf_nr == ARRAY_SIZE(gr->ctxbuf)))
			continue;

		gr->ctxbuf[gr->ctxbuf_nr].bufferId = map[id].id1;
		gr->ctxbuf[gr->ctxbuf_nr].size     = size;
		gr->ctxbuf[gr->ctxbuf_nr].page     = page;
		gr->ctxbuf[gr->ctxbuf_nr].align    = align;
		gr->ctxbuf[gr->ctxbuf_nr].global   = map[id].global;
		gr->ctxbuf[gr->ctxbuf_nr].init     = map[id].init;
		gr->ctxbuf[gr->ctxbuf_nr].ro       = map[id].ro;
		gr->ctxbuf_nr++;

		if (map[id].id1 == NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_PRIV_ACCESS_MAP) {
			if (WARN_ON(gr->ctxbuf_nr == ARRAY_SIZE(gr->ctxbuf)))
				continue;

			gr->ctxbuf[gr->ctxbuf_nr] = gr->ctxbuf[gr->ctxbuf_nr - 1];
			gr->ctxbuf[gr->ctxbuf_nr].bufferId =
				NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_UNRESTRICTED_PRIV_ACCESS_MAP;
			gr->ctxbuf_nr++;
		}
	}

	nvkm_gsp_rm_ctrl_done(&gsp->internal.device.subdevice, info);

	/* Promote golden context to RM. */
	ret = r535_gr_promote_ctx(gr, true, golden.vmm, gr->ctxbuf_mem, golden.vma, &golden.chan);
	if (ret)
		goto done;

	/* Allocate 3D class on channel to trigger golden context init in RM. */
	{
		int i;

		for (i = 0; gr->base.func->sclass[i].ctor; i++) {
			if ((gr->base.func->sclass[i].oclass & 0xff) == 0x97) {
				struct nvkm_gsp_object threed;

				ret = nvkm_gsp_rm_alloc(&golden.chan, 0x97000000,
							gr->base.func->sclass[i].oclass, 0,
							&threed);
				if (ret)
					goto done;

				nvkm_gsp_rm_free(&threed);
				break;
			}
		}

		if (WARN_ON(!gr->base.func->sclass[i].ctor)) {
			ret = -EINVAL;
			goto done;
		}
	}

done:
	nvkm_gsp_rm_free(&golden.chan);
	for (int i = gr->ctxbuf_nr - 1; i >= 0; i--)
		nvkm_vmm_put(golden.vmm, &golden.vma[i]);
	nvkm_vmm_unref(&golden.vmm);
	nvkm_memory_unref(&golden.inst);
	return ret;

}

static void *
r535_gr_dtor(struct nvkm_gr *base)
{
	struct r535_gr *gr = r535_gr(base);

	while (gr->ctxbuf_nr)
		nvkm_memory_unref(&gr->ctxbuf_mem[--gr->ctxbuf_nr]);

	kfree(gr->base.func);
	return gr;
}

int
r535_gr_new(const struct gf100_gr_func *hw,
	    struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_gr **pgr)
{
	struct nvkm_gr_func *rm;
	struct r535_gr *gr;
	int nclass;

	for (nclass = 0; hw->sclass[nclass].oclass; nclass++);

	if (!(rm = kzalloc(sizeof(*rm) + (nclass + 1) * sizeof(rm->sclass[0]), GFP_KERNEL)))
		return -ENOMEM;

	rm->dtor = r535_gr_dtor;
	rm->oneinit = r535_gr_oneinit;
	rm->units = r535_gr_units;
	rm->chan_new = r535_gr_chan_new;

	for (int i = 0; i < nclass; i++) {
		rm->sclass[i].minver = hw->sclass[i].minver;
		rm->sclass[i].maxver = hw->sclass[i].maxver;
		rm->sclass[i].oclass = hw->sclass[i].oclass;
		rm->sclass[i].ctor = r535_gr_obj_ctor;
	}

	if (!(gr = kzalloc(sizeof(*gr), GFP_KERNEL))) {
		kfree(rm);
		return -ENOMEM;
	}

	*pgr = &gr->base;

	return nvkm_gr_ctor(rm, device, type, inst, true, &gr->base);
}
