/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

static const struct nvkm_fsp_func
gb100_fsp = {
	.wait_secure_boot = gh100_fsp_wait_secure_boot,
	.cot = {
		.version = 2,
		.size_hash = 48,
		.size_pkey = 97,
		.size_sig = 96,
		.boot_gsp_fmc = gh100_fsp_boot_gsp_fmc,
	},
};

int
gb100_fsp_new(struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, struct nvkm_fsp **pfsp)
{
	return nvkm_fsp_new_(&gb100_fsp, device, type, inst, pfsp);
}
