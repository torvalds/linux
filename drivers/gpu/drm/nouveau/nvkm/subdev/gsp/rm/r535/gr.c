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
#include <rm/gr.h>

#include <core/memory.h>
#include <subdev/gsp.h>
#include <subdev/mmu/vmm.h>
#include <engine/fifo/priv.h>
#include <engine/gr/priv.h>

#include <nvif/if900d.h>

#include <nvhw/drf.h>

#include "nvrm/gr.h"
#include "nvrm/vmm.h"

#define r535_gr(p) container_of((p), struct r535_gr, base)

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

int
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

int
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

u64
r535_gr_units(struct nvkm_gr *gr)
{
	struct nvkm_gsp *gsp = gr->engine.subdev.device->gsp;

	return (gsp->gr.tpcs << 8) | gsp->gr.gpcs;
}

void
r535_gr_get_ctxbuf_info(struct r535_gr *gr, int i,
			struct NV2080_CTRL_INTERNAL_ENGINE_CONTEXT_BUFFER_INFO *info)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
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
	u32 size = info->size;
	u8 align, page;
	int id;

	for (id = 0; id < ARRAY_SIZE(map); id++) {
		if (map[id].id0 == i)
			break;
	}

	nvkm_debug(subdev, "%02x: size:0x%08x %s\n", i,
		   size, (id < ARRAY_SIZE(map)) ? "*" : "");
	if (id >= ARRAY_SIZE(map))
		return;

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
		return;

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
			return;

		gr->ctxbuf[gr->ctxbuf_nr] = gr->ctxbuf[gr->ctxbuf_nr - 1];
		gr->ctxbuf[gr->ctxbuf_nr].bufferId =
			NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_UNRESTRICTED_PRIV_ACCESS_MAP;
		gr->ctxbuf_nr++;
	}
}

static int
r535_gr_get_ctxbufs_info(struct r535_gr *gr)
{
	NV2080_CTRL_INTERNAL_STATIC_GR_GET_CONTEXT_BUFFERS_INFO_PARAMS *info;
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_gsp *gsp = subdev->device->gsp;

	info = nvkm_gsp_rm_ctrl_rd(&gsp->internal.device.subdevice,
				   NV2080_CTRL_CMD_INTERNAL_STATIC_KGR_GET_CONTEXT_BUFFERS_INFO,
				   sizeof(*info));
	if (WARN_ON(IS_ERR(info)))
		return PTR_ERR(info);

	for (int i = 0; i < ARRAY_SIZE(info->engineContextBuffersInfo[0].engine); i++)
		r535_gr_get_ctxbuf_info(gr, i, &info->engineContextBuffersInfo[0].engine[i]);

	nvkm_gsp_rm_ctrl_done(&gsp->internal.device.subdevice, info);
	return 0;
}

int
r535_gr_oneinit(struct nvkm_gr *base)
{
	struct r535_gr *gr = container_of(base, typeof(*gr), base);
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_gsp *gsp = device->gsp;
	struct nvkm_rm *rm = gsp->rm;
	struct {
		struct nvkm_memory *inst;
		struct nvkm_vmm *vmm;
		struct nvkm_gsp_object chan;
		struct nvkm_vma *vma[R515_GR_MAX_CTXBUFS];
	} golden = {};
	struct nvkm_gsp_object threed;
	int ret;

	/* Allocate a channel to use for golden context init. */
	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x12000, 0, true, &golden.inst);
	if (ret)
		goto done;

	ret = nvkm_vmm_new(device, 0x1000, 0, NULL, 0, NULL, "grGoldenVmm", &golden.vmm);
	if (ret)
		goto done;

	ret = r535_mmu_vaspace_new(golden.vmm, NVKM_RM_VASPACE, false);
	if (ret)
		goto done;

	ret = rm->api->fifo->chan.alloc(&golden.vmm->rm.device, NVKM_RM_CHAN(0),
					1, 0, true, rm->api->fifo->rsvd_chids,
					nvkm_memory_addr(golden.inst),
					nvkm_memory_addr(golden.inst) + 0x1000,
					nvkm_memory_addr(golden.inst) + 0x2000,
					golden.vmm, 0, 0x1000, &golden.chan);
	if (ret)
		goto done;

	/* Fetch context buffer info from RM and allocate each of them here to use
	 * during golden context init (or later as a global context buffer).
	 *
	 * Also build the information that'll be used to create channel contexts.
	 */
	ret = rm->api->gr->get_ctxbufs_info(gr);
	if (ret)
		goto done;

	/* Promote golden context to RM. */
	ret = r535_gr_promote_ctx(gr, true, golden.vmm, gr->ctxbuf_mem, golden.vma, &golden.chan);
	if (ret)
		goto done;

	/* Allocate 3D class on channel to trigger golden context init in RM. */
	ret = nvkm_gsp_rm_alloc(&golden.chan, NVKM_RM_THREED, rm->gpu->gr.class.threed, 0, &threed);
	if (ret)
		goto done;

	/* There's no need to keep the golden channel around, as RM caches the context. */
	nvkm_gsp_rm_free(&threed);
done:
	nvkm_gsp_rm_free(&golden.chan);
	for (int i = gr->ctxbuf_nr - 1; i >= 0; i--)
		nvkm_vmm_put(golden.vmm, &golden.vma[i]);
	nvkm_vmm_unref(&golden.vmm);
	nvkm_memory_unref(&golden.inst);
	return ret;

}

void *
r535_gr_dtor(struct nvkm_gr *base)
{
	struct r535_gr *gr = r535_gr(base);

	while (gr->ctxbuf_nr)
		nvkm_memory_unref(&gr->ctxbuf_mem[--gr->ctxbuf_nr]);

	kfree(gr->base.func);
	return gr;
}

const struct nvkm_rm_api_gr
r535_gr = {
	.get_ctxbufs_info = r535_gr_get_ctxbufs_info,
};
