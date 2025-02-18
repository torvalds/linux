/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "gpu.h"

#include <nvif/class.h>

const struct nvkm_rm_gpu
ga100_gpu = {
	.usermode.class = AMPERE_USERMODE_A,
};
