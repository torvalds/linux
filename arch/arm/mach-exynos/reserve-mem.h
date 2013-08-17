/* linux/arch/arm/mach-exynos/reserve-mem.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Reserve mem helper functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cma.h>

void __init exynos_cma_region_reserve(struct cma_region *regions,
					struct cma_region *regions_secure,
					struct cma_region *regions_adjacent,
					const char *map);
