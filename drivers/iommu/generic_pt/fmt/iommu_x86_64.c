// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 */
#define PT_FMT x86_64
#define PT_SUPPORTED_FEATURES                                  \
	(BIT(PT_FEAT_SIGN_EXTEND) | BIT(PT_FEAT_FLUSH_RANGE) | \
	 BIT(PT_FEAT_FLUSH_RANGE_NO_GAPS) |                    \
	 BIT(PT_FEAT_X86_64_AMD_ENCRYPT_TABLES) | BIT(PT_FEAT_DMA_INCOHERENT))

#include "iommu_template.h"
