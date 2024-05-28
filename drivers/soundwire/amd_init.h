/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __AMD_INIT_H
#define __AMD_INIT_H

#include <linux/soundwire/sdw_amd.h>

int amd_sdw_manager_start(struct amd_sdw_manager *amd_manager);

static inline void amd_updatel(void __iomem *mmio, int offset, u32 mask, u32 val)
{
	u32 tmp;

	tmp = readl(mmio + offset);
	tmp = (tmp & ~mask) | val;
	writel(tmp, mmio + offset);
}
#endif
