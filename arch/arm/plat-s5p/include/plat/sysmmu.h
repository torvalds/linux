/* linux/arch/arm/plat-s5p/include/plat/sysmmu.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Samsung sysmmu driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_S5P_SYSMMU_H
#define __ASM_PLAT_S5P_SYSMMU_H __FILE__

/* debug macro */
#ifdef CONFIG_S5P_SYSMMU_DEBUG
#define sysmmu_debug(fmt, arg...)	printk(KERN_INFO "[%s] " fmt, __func__, ## arg)
#else
#define sysmmu_debug(fmt, arg...)	do { } while (0)
#endif

#endif /* __ASM_PLAT_S5P_SYSMMU_H */
