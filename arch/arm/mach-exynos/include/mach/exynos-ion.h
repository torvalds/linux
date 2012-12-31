/* linux/arch/arm/mach-exynos/include/mach/exynos-ion.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __MACH_EXYNOS_ION_H_

struct platform_device;

#ifdef CONFIG_ION_EXYNOS
extern struct platform_device exynos_device_ion;
void exynos_ion_set_platdata(void);
#endif

#endif /* __MACH_S5PV310_ION_H_ */
