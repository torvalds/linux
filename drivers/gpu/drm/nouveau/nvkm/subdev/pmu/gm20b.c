/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "priv.h"

#include <core/msgqueue.h>
#include <subdev/acr.h>

#include <nvfw/pmu.h>

static int
gm20b_pmu_acr_bootstrap_falcon_cb(void *priv, struct nv_falcon_msg *hdr)
{
	struct nv_pmu_acr_bootstrap_falcon_msg *msg =
		container_of(hdr, typeof(*msg), msg.hdr);
	return msg->falcon_id;
}

int
gm20b_pmu_acr_bootstrap_falcon(struct nvkm_falcon *falcon,
			       enum nvkm_acr_lsf_id id)
{
	struct nvkm_pmu *pmu = container_of(falcon, typeof(*pmu), falcon);
	struct nv_pmu_acr_bootstrap_falcon_cmd cmd = {
		.cmd.hdr.unit_id = NV_PMU_UNIT_ACR,
		.cmd.hdr.size = sizeof(cmd),
		.cmd.cmd_type = NV_PMU_ACR_CMD_BOOTSTRAP_FALCON,
		.flags = NV_PMU_ACR_BOOTSTRAP_FALCON_FLAGS_RESET_YES,
		.falcon_id = id,
	};
	int ret;

	ret = nvkm_falcon_cmdq_send(pmu->hpq, &cmd.cmd.hdr,
				    gm20b_pmu_acr_bootstrap_falcon_cb,
				    &pmu->subdev, msecs_to_jiffies(1000));
	if (ret >= 0 && ret != cmd.falcon_id)
		ret = -EIO;
	return ret;
}

static const struct nvkm_acr_lsf_func
gm20b_pmu_acr = {
	.bootstrap_falcon = gm20b_pmu_acr_bootstrap_falcon,
};

static int
gm20b_pmu_acr_init_wpr_callback(void *priv, struct nv_falcon_msg *hdr)
{
	struct nv_pmu_acr_init_wpr_region_msg *msg =
		container_of(hdr, typeof(*msg), msg.hdr);
	struct nvkm_pmu *pmu = priv;
	struct nvkm_subdev *subdev = &pmu->subdev;

	if (msg->error_code) {
		nvkm_error(subdev, "ACR WPR init failure: %d\n",
			   msg->error_code);
		return -EINVAL;
	}

	nvkm_debug(subdev, "ACR WPR init complete\n");
	complete_all(&pmu->wpr_ready);
	return 0;
}

static int
gm20b_pmu_acr_init_wpr(struct nvkm_pmu *pmu)
{
	struct nv_pmu_acr_init_wpr_region_cmd cmd = {
		.cmd.hdr.unit_id = NV_PMU_UNIT_ACR,
		.cmd.hdr.size = sizeof(cmd),
		.cmd.cmd_type = NV_PMU_ACR_CMD_INIT_WPR_REGION,
		.region_id = 1,
		.wpr_offset = 0,
	};

	return nvkm_falcon_cmdq_send(pmu->hpq, &cmd.cmd.hdr,
				     gm20b_pmu_acr_init_wpr_callback, pmu, 0);
}

int
gm20b_pmu_initmsg(struct nvkm_pmu *pmu)
{
	struct nv_pmu_init_msg msg;
	int ret;

	ret = nvkm_falcon_msgq_recv_initmsg(pmu->msgq, &msg, sizeof(msg));
	if (ret)
		return ret;

	if (msg.hdr.unit_id != NV_PMU_UNIT_INIT ||
	    msg.msg_type != NV_PMU_INIT_MSG_INIT)
		return -EINVAL;

	nvkm_falcon_cmdq_init(pmu->hpq, msg.queue_info[0].index,
					msg.queue_info[0].offset,
					msg.queue_info[0].size);
	nvkm_falcon_cmdq_init(pmu->lpq, msg.queue_info[1].index,
					msg.queue_info[1].offset,
					msg.queue_info[1].size);
	nvkm_falcon_msgq_init(pmu->msgq, msg.queue_info[4].index,
					 msg.queue_info[4].offset,
					 msg.queue_info[4].size);
	return gm20b_pmu_acr_init_wpr(pmu);
}

void
gm20b_pmu_recv(struct nvkm_pmu *pmu)
{
	if (!pmu->initmsg_received) {
		int ret = pmu->func->initmsg(pmu);
		if (ret) {
			nvkm_error(&pmu->subdev,
				   "error parsing init message: %d\n", ret);
			return;
		}

		pmu->initmsg_received = true;
	}

	nvkm_falcon_msgq_recv(pmu->msgq);
}

static const struct nvkm_pmu_func
gm20b_pmu = {
	.flcn = &gt215_pmu_flcn,
	.enabled = gf100_pmu_enabled,
	.intr = gt215_pmu_intr,
	.recv = gm20b_pmu_recv,
	.initmsg = gm20b_pmu_initmsg,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
MODULE_FIRMWARE("nvidia/gm20b/pmu/desc.bin");
MODULE_FIRMWARE("nvidia/gm20b/pmu/image.bin");
MODULE_FIRMWARE("nvidia/gm20b/pmu/sig.bin");
#endif

int
gm20b_pmu_load(struct nvkm_pmu *pmu, int ver, const struct nvkm_pmu_fwif *fwif)
{
	return nvkm_acr_lsfw_load_sig_image_desc(&pmu->subdev, &pmu->falcon,
						 NVKM_ACR_LSF_PMU, "pmu/",
						 ver, fwif->acr);
}

static const struct nvkm_pmu_fwif
gm20b_pmu_fwif[] = {
	{ 0, gm20b_pmu_load, &gm20b_pmu, &gm20b_pmu_acr },
	{}
};

int
gm20b_pmu_new(struct nvkm_device *device, int index, struct nvkm_pmu **ppmu)
{
	return nvkm_pmu_new_(gm20b_pmu_fwif, device, index, ppmu);
}
