/* linux/arch/arm/mach-exynos/dev-ion.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/ion.h>
#include <linux/slab.h>
#include <mach/exynos-ion.h>

struct platform_device exynos_device_ion = {
	.name		= "ion-exynos",
	.id		= -1,
};

void __init exynos_ion_set_platdata(void)
{
	struct ion_platform_data *pdata;
	pdata = kzalloc(sizeof(struct ion_platform_data)
			+ 5 * sizeof(struct ion_platform_heap), GFP_KERNEL);
	if (pdata) {
		pdata->nr = 5;
		pdata->heaps[0].type = ION_HEAP_TYPE_SYSTEM;
		pdata->heaps[0].name = "ion_noncontig_heap";
		pdata->heaps[0].id = ION_HEAP_TYPE_SYSTEM;
		pdata->heaps[1].type = ION_HEAP_TYPE_SYSTEM_CONTIG;
		pdata->heaps[1].name = "ion_contig_heap";
		pdata->heaps[1].id = ION_HEAP_TYPE_SYSTEM_CONTIG;
		pdata->heaps[2].type = ION_HEAP_TYPE_EXYNOS;
		pdata->heaps[2].name = "exynos_noncontig_heap";
		pdata->heaps[2].id = ION_HEAP_TYPE_EXYNOS;
		pdata->heaps[3].type = ION_HEAP_TYPE_EXYNOS_CONTIG;
		pdata->heaps[3].name = "exynos_contig_heap";
		pdata->heaps[3].id = ION_HEAP_TYPE_EXYNOS_CONTIG;
		pdata->heaps[4].type = ION_HEAP_TYPE_EXYNOS_USER;
		pdata->heaps[4].name = "exynos_user_heap";
		pdata->heaps[4].id = ION_HEAP_TYPE_EXYNOS_USER;
		exynos_device_ion.dev.platform_data = pdata;
	}
}
