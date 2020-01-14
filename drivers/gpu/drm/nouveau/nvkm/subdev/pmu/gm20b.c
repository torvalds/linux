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

static const struct nvkm_acr_lsf_func
gm20b_pmu_acr = {
};

void
gm20b_pmu_recv(struct nvkm_pmu *pmu)
{
	if (!pmu->queue) {
		nvkm_warn(&pmu->subdev,
			  "recv function called while no firmware set!\n");
		return;
	}

	nvkm_msgqueue_recv(pmu->queue);
}

static const struct nvkm_pmu_func
gm20b_pmu = {
	.flcn = &gt215_pmu_flcn,
	.enabled = gf100_pmu_enabled,
	.intr = gt215_pmu_intr,
	.recv = gm20b_pmu_recv,
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
