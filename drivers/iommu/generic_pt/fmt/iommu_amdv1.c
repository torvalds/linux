// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 */
#define PT_FMT amdv1
#define PT_SUPPORTED_FEATURES                                          \
	(BIT(PT_FEAT_FULL_VA) | BIT(PT_FEAT_DYNAMIC_TOP) |             \
	 BIT(PT_FEAT_FLUSH_RANGE) | BIT(PT_FEAT_FLUSH_RANGE_NO_GAPS) | \
	 BIT(PT_FEAT_AMDV1_ENCRYPT_TABLES) |                           \
	 BIT(PT_FEAT_AMDV1_FORCE_COHERENCE))
#define PT_FORCE_ENABLED_FEATURES                                       \
	(BIT(PT_FEAT_DYNAMIC_TOP) | BIT(PT_FEAT_AMDV1_ENCRYPT_TABLES) | \
	 BIT(PT_FEAT_AMDV1_FORCE_COHERENCE))

#include "iommu_template.h"
