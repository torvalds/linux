/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

static const struct nvkm_gsp_func
gb100_gsp = {
	.flcn = &ga102_gsp_flcn,

	.sig_section = ".fwsignature_gb10x",

	.dtor = r535_gsp_dtor,
	.oneinit = gh100_gsp_oneinit,
	.init = gh100_gsp_init,
	.fini = gh100_gsp_fini,

	.rm.gpu = &gb10x_gpu,
};

static struct nvkm_gsp_fwif
gb100_gsps[] = {
	{ 0, gh100_gsp_load, &gb100_gsp, &r570_rm_gb10x, "570.144" },
	{}
};

int
gb100_gsp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_gsp **pgsp)
{
	return nvkm_gsp_new_(gb100_gsps, device, type, inst, pgsp);
}

NVKM_GSP_FIRMWARE_FMC(gb100, 570.144);
NVKM_GSP_FIRMWARE_FMC(gb102, 570.144);
