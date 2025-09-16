/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

static const struct nvkm_gsp_func
gb202_gsp = {
	.flcn = &ga102_gsp_flcn,

	.sig_section = ".fwsignature_gb20x",

	.dtor = r535_gsp_dtor,
	.oneinit = gh100_gsp_oneinit,
	.init = gh100_gsp_init,
	.fini = gh100_gsp_fini,

	.rm.gpu = &gb20x_gpu,
};

static struct nvkm_gsp_fwif
gb202_gsps[] = {
	{ 0, gh100_gsp_load, &gb202_gsp, &r570_rm_gb20x, "570.144" },
	{}
};

int
gb202_gsp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_gsp **pgsp)
{
	return nvkm_gsp_new_(gb202_gsps, device, type, inst, pgsp);
}

NVKM_GSP_FIRMWARE_FMC(gb202, 570.144);
NVKM_GSP_FIRMWARE_FMC(gb203, 570.144);
NVKM_GSP_FIRMWARE_FMC(gb205, 570.144);
NVKM_GSP_FIRMWARE_FMC(gb206, 570.144);
NVKM_GSP_FIRMWARE_FMC(gb207, 570.144);
