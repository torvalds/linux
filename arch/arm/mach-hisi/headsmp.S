/*
 *  Copyright (c) 2014 Hisilicon Limited.
 *  Copyright (c) 2014 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <linux/init.h>

	__CPUINIT

ENTRY(hisi_secondary_startup)
	bl	v7_invalidate_l1
	b	secondary_startup
