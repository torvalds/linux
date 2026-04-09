// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES
 */
#define PT_FMT riscv
#define PT_FMT_VARIANT 64
#define PT_SUPPORTED_FEATURES                                  \
	(BIT(PT_FEAT_SIGN_EXTEND) | BIT(PT_FEAT_FLUSH_RANGE) | \
	 BIT(PT_FEAT_RISCV_SVNAPOT_64K))

#include "iommu_template.h"
