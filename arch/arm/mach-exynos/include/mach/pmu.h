/* linux/arch/arm/mach-exynos/include/mach/pmu.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4210 - PMU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_PMU_H
#define __ASM_ARCH_PMU_H __FILE__

#include <linux/cpu.h>

static inline void exynos4_reset_assert_ctrl(unsigned int on)
{
	unsigned int i;
	unsigned int core_option;

	for (i = 0 ; i < num_possible_cpus(); i++) {
		core_option = __raw_readl(S5P_ARM_CORE_OPTION(i));
		core_option &= ~S5P_USE_DELAYED_RESET_ASSERTION;
		core_option |= (on << S5P_USE_DELAYED_RESET_OFFSET);
		__raw_writel(core_option, S5P_ARM_CORE_OPTION(i));
	}
}

static inline int exynos4_is_c2c_use(void)
{
	unsigned int ret;

	ret = __raw_readl(S5P_C2C_CTRL);

	return ret;
}

enum sys_powerdown {
	SYS_AFTR,
	SYS_LPA,
	SYS_SLEEP,
	NUM_SYS_POWERDOWN,
};

struct exynos4_pmu_conf {
	void __iomem *reg;
	unsigned long val[NUM_SYS_POWERDOWN];
};

enum c2c_pwr_mode {
	MIN_LATENCY,
	SHORT_LATENCY,
	MAX_LATENCY,
};

struct exynos4_c2c_pmu_conf {
	void __iomem *reg;
	unsigned long val;
};

/* external function for exynos4 series */
extern void exynos4_sys_powerdown_conf(enum sys_powerdown mode);
extern int exynos4_enter_lp(unsigned long *saveblk, long);
extern void exynos4_idle_resume(void);
extern void exynos4_c2c_request_pwr_mode(enum c2c_pwr_mode mode);

/* external function for exynos5 series */
extern void exynos5_sys_powerdown_conf(enum sys_powerdown mode);
extern int exynos5_enter_lp(unsigned long *saveblk, long);
extern void exynos5_idle_resume(void);

#endif /* __ASM_ARCH_PMU_H */
