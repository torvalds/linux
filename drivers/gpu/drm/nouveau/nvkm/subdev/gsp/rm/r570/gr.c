/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <rm/gr.h>

#include <subdev/mmu.h>
#include <engine/fifo.h>
#include <engine/fifo/chid.h>
#include <engine/gr/priv.h>

#include "nvrm/gr.h"
#include "nvrm/engine.h"

int
r570_gr_tpc_mask(struct nvkm_gsp *gsp, int gpc, u32 *pmask)
{
	NV2080_CTRL_GPU_GET_FERMI_TPC_INFO_PARAMS *ctrl;
	int ret;

	ctrl = nvkm_gsp_rm_ctrl_get(&gsp->internal.device.subdevice,
				    NV2080_CTRL_CMD_GPU_GET_FERMI_TPC_INFO, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->gpcId = gpc;

	ret = nvkm_gsp_rm_ctrl_push(&gsp->internal.device.subdevice, &ctrl, sizeof(*ctrl));
	if (ret)
		return ret;

	*pmask = ctrl->tpcMask;

	nvkm_gsp_rm_ctrl_done(&gsp->internal.device.subdevice, ctrl);
	return 0;
}

int
r570_gr_gpc_mask(struct nvkm_gsp *gsp, u32 *pmask)
{
	NV2080_CTRL_GPU_GET_FERMI_GPC_INFO_PARAMS *ctrl;

	ctrl = nvkm_gsp_rm_ctrl_rd(&gsp->internal.device.subdevice,
				   NV2080_CTRL_CMD_GPU_GET_FERMI_GPC_INFO, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	*pmask = ctrl->gpcMask;

	nvkm_gsp_rm_ctrl_done(&gsp->internal.device.subdevice, ctrl);
	return 0;
}

static int
r570_gr_scrubber_ctrl(struct r535_gr *gr, bool teardown)
{
	NV2080_CTRL_INTERNAL_GR_INIT_BUG4208224_WAR_PARAMS *ctrl;

	ctrl = nvkm_gsp_rm_ctrl_get(&gr->scrubber.vmm->rm.device.subdevice,
				    NV2080_CTRL_CMD_INTERNAL_KGR_INIT_BUG4208224_WAR,
				    sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->bTeardown = teardown;

	return nvkm_gsp_rm_ctrl_wr(&gr->scrubber.vmm->rm.device.subdevice, ctrl);
}

static void
r570_gr_scrubber_fini(struct r535_gr *gr)
{
	/* Teardown scrubber channel on RM. */
	if (gr->scrubber.enabled) {
		WARN_ON(r570_gr_scrubber_ctrl(gr, true));
		gr->scrubber.enabled = false;
	}

	/* Free scrubber channel. */
	nvkm_gsp_rm_free(&gr->scrubber.threed);
	nvkm_gsp_rm_free(&gr->scrubber.chan);

	for (int i = 0; i < gr->ctxbuf_nr; i++) {
		nvkm_vmm_put(gr->scrubber.vmm, &gr->scrubber.ctxbuf.vma[i]);
		nvkm_memory_unref(&gr->scrubber.ctxbuf.mem[i]);
	}

	nvkm_vmm_unref(&gr->scrubber.vmm);
	nvkm_memory_unref(&gr->scrubber.inst);
}

static int
r570_gr_scrubber_init(struct r535_gr *gr)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_gsp *gsp = device->gsp;
	struct nvkm_rm *rm = gsp->rm;
	int ret;

	/* Scrubber channel only required on TU10x. */
	switch (device->chipset) {
	case 0x162:
	case 0x164:
	case 0x166:
		break;
	default:
		return 0;
	}

	if (gr->scrubber.chid < 0) {
		gr->scrubber.chid = nvkm_chid_get(device->fifo->chid, NULL);
		if (gr->scrubber.chid < 0)
			return gr->scrubber.chid;
	}

	/* Allocate scrubber channel. */
	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
			      0x2000 + device->fifo->rm.mthdbuf_size, 0, true,
			      &gr->scrubber.inst);
	if (ret)
		goto done;

	ret = nvkm_vmm_new(device, 0x1000, 0, NULL, 0, NULL, "grScrubberVmm",
			   &gr->scrubber.vmm);
	if (ret)
		goto done;

	ret = r535_mmu_vaspace_new(gr->scrubber.vmm, KGRAPHICS_SCRUBBER_HANDLE_VAS, false);
	if (ret)
		goto done;

	ret = rm->api->fifo->chan.alloc(&gr->scrubber.vmm->rm.device, KGRAPHICS_SCRUBBER_HANDLE_CHANNEL,
					NV2080_ENGINE_TYPE_GR0, 0, false, gr->scrubber.chid,
					nvkm_memory_addr(gr->scrubber.inst),
					nvkm_memory_addr(gr->scrubber.inst) + 0x1000,
					nvkm_memory_addr(gr->scrubber.inst) + 0x2000,
					gr->scrubber.vmm, 0, 0x1000, &gr->scrubber.chan);
	if (ret)
		goto done;

	ret = r535_gr_promote_ctx(gr, false, gr->scrubber.vmm, gr->scrubber.ctxbuf.mem,
				  gr->scrubber.ctxbuf.vma, &gr->scrubber.chan);
	if (ret)
		goto done;

	ret = nvkm_gsp_rm_alloc(&gr->scrubber.chan, KGRAPHICS_SCRUBBER_HANDLE_3DOBJ,
				rm->gpu->gr.class.threed, 0, &gr->scrubber.threed);
	if (ret)
		goto done;

	/* Initialise scrubber channel on RM. */
	ret = r570_gr_scrubber_ctrl(gr, false);
	if (ret)
		goto done;

	gr->scrubber.enabled = true;

done:
	if (ret)
		r570_gr_scrubber_fini(gr);

	return ret;
}

static int
r570_gr_get_ctxbufs_info(struct r535_gr *gr)
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

const struct nvkm_rm_api_gr
r570_gr = {
	.get_ctxbufs_info = r570_gr_get_ctxbufs_info,
	.scrubber.init = r570_gr_scrubber_init,
	.scrubber.fini = r570_gr_scrubber_fini,
};
