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

#include <subdev/acr.h>

#include <nvfw/flcn.h>
#include <nvfw/pmu.h>

static int
gp10b_pmu_acr_bootstrap_multiple_falcons_cb(void *priv,
					    struct nvfw_falcon_msg *hdr)
{
	struct nv_pmu_acr_bootstrap_multiple_falcons_msg *msg =
		container_of(hdr, typeof(*msg), msg.hdr);
	return msg->falcon_mask;
}
static int
gp10b_pmu_acr_bootstrap_multiple_falcons(struct nvkm_falcon *falcon, u32 mask)
{
	struct nvkm_pmu *pmu = container_of(falcon, typeof(*pmu), falcon);
	struct nv_pmu_acr_bootstrap_multiple_falcons_cmd cmd = {
		.cmd.hdr.unit_id = NV_PMU_UNIT_ACR,
		.cmd.hdr.size = sizeof(cmd),
		.cmd.cmd_type = NV_PMU_ACR_CMD_BOOTSTRAP_MULTIPLE_FALCONS,
		.flags = NV_PMU_ACR_BOOTSTRAP_MULTIPLE_FALCONS_FLAGS_RESET_YES,
		.falcon_mask = mask,
		.wpr_lo = 0, /*XXX*/
		.wpr_hi = 0, /*XXX*/
	};
	int ret;

	ret = nvkm_falcon_cmdq_send(pmu->hpq, &cmd.cmd.hdr,
				    gp10b_pmu_acr_bootstrap_multiple_falcons_cb,
				    &pmu->subdev, msecs_to_jiffies(1000));
	if (ret >= 0) {
		if (ret != cmd.falcon_mask)
			ret = -EIO;
		else
			ret = 0;
	}

	return ret;
}

static const struct nvkm_acr_lsf_func
gp10b_pmu_acr = {
	.flags = NVKM_ACR_LSF_DMACTL_REQ_CTX,
	.bld_size = sizeof(struct loader_config),
	.bld_write = gm20b_pmu_acr_bld_write,
	.bld_patch = gm20b_pmu_acr_bld_patch,
	.boot = gm20b_pmu_acr_boot,
	.bootstrap_falcons = BIT_ULL(NVKM_ACR_LSF_PMU) |
			     BIT_ULL(NVKM_ACR_LSF_FECS) |
			     BIT_ULL(NVKM_ACR_LSF_GPCCS),
	.bootstrap_falcon = gm20b_pmu_acr_bootstrap_falcon,
	.bootstrap_multiple_falcons = gp10b_pmu_acr_bootstrap_multiple_falcons,
};

static const struct nvkm_pmu_func
gp10b_pmu = {
	.flcn = &gm200_pmu_flcn,
	.enabled = gf100_pmu_enabled,
	.intr = gt215_pmu_intr,
	.recv = gm20b_pmu_recv,
	.initmsg = gm20b_pmu_initmsg,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
MODULE_FIRMWARE("nvidia/gp10b/pmu/desc.bin");
MODULE_FIRMWARE("nvidia/gp10b/pmu/image.bin");
MODULE_FIRMWARE("nvidia/gp10b/pmu/sig.bin");
#endif

static const struct nvkm_pmu_fwif
gp10b_pmu_fwif[] = {
	{  0, gm20b_pmu_load, &gp10b_pmu, &gp10b_pmu_acr },
	{ -1, gm200_pmu_nofw, &gp10b_pmu },
	{}
};

int
gp10b_pmu_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_pmu **ppmu)
{
	return nvkm_pmu_new_(gp10b_pmu_fwif, device, type, inst, ppmu);
}
