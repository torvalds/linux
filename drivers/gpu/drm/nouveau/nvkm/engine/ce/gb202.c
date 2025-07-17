/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

#include <nvhw/drf.h>
#include <nvhw/ref/gb202/dev_ce.h>

u32
gb202_ce_grce_mask(struct nvkm_device *device)
{
	u32 data = nvkm_rd32(device, NV_CE_GRCE_MASK);

	return NVVAL_GET(data, NV_CE, GRCE_MASK, VALUE);
}
