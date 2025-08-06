/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

#include <nvhw/drf.h>
#include <nvhw/ref/gb202/dev_therm.h>

static int
gb202_fsp_wait_secure_boot(struct nvkm_fsp *fsp)
{
	struct nvkm_device *device = fsp->subdev.device;
	unsigned timeout_ms = 4000;

	do {
		u32 status = NVKM_RD32(device, NV_THERM, I2CS_SCRATCH, FSP_BOOT_COMPLETE_STATUS);

		if (status == NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE_STATUS_SUCCESS)
			return 0;

		usleep_range(1000, 2000);
	} while (timeout_ms--);

	return -ETIMEDOUT;
}

static const struct nvkm_fsp_func
gb202_fsp = {
	.wait_secure_boot = gb202_fsp_wait_secure_boot,
	.cot = {
		.version = 2,
		.size_hash = 48,
		.size_pkey = 97,
		.size_sig = 96,
		.boot_gsp_fmc = gh100_fsp_boot_gsp_fmc,
	},
};

int
gb202_fsp_new(struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, struct nvkm_fsp **pfsp)
{
	return nvkm_fsp_new_(&gb202_fsp, device, type, inst, pfsp);
}
