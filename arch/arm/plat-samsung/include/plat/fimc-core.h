/*
 * arch/arm/plat-samsung/include/plat/fimc-core.h
 *
 * Copyright 2010 Samsung Electronics Co., Ltd.
 *	Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * Samsung camera interface driver core functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_PLAT_FIMC_CORE_H
#define __ASM_PLAT_FIMC_CORE_H __FILE__

/*
 * These functions are only for use with the core support code, such as
 * the CPU-specific initialization code.
 */

/* Re-define device name to differentiate the subsystem in various SoCs. */
static inline void s3c_fimc_setname(int id, char *name)
{
	switch (id) {
#ifdef CONFIG_S5P_DEV_FIMC0
	case 0:
		s5p_device_fimc0.name = name;
		break;
#endif
#ifdef CONFIG_S5P_DEV_FIMC1
	case 1:
		s5p_device_fimc1.name = name;
		break;
#endif
#ifdef CONFIG_S5P_DEV_FIMC2
	case 2:
		s5p_device_fimc2.name = name;
		break;
#endif
#ifdef CONFIG_S5P_DEV_FIMC3
	case 3:
		s5p_device_fimc3.name = name;
		break;
#endif
	}
}

#endif /* __ASM_PLAT_FIMC_CORE_H */
