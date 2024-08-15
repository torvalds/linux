/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __MACH_TEGRA_IRAMMAP_H
#define __MACH_TEGRA_IRAMMAP_H

#include <linux/sizes.h>

/* The first 1K of IRAM is permanently reserved for the CPU reset handler */
#define TEGRA_IRAM_RESET_HANDLER_OFFSET	0
#define TEGRA_IRAM_RESET_HANDLER_SIZE	SZ_1K

/*
 * This area is used for LPx resume vector, only while LPx power state is
 * active. At other times, the AVP may use this area for arbitrary purposes
 */
#define TEGRA_IRAM_LPx_RESUME_AREA	(TEGRA_IRAM_BASE + SZ_4K)

#endif
