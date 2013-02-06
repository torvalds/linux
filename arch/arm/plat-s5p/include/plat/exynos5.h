/* linux/arch/arm/plat-s5p/include/plat/exynos5.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Header file for exynos5 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* Common init code for EXYNOS5 related SoCs */

struct s3c2410_uartcfg;

extern void exynos_common_init_uarts(struct s3c2410_uartcfg *cfg, int no);
extern void exynos5_register_clocks(void);
extern void exynos5_setup_clocks(void);

#if defined(CONFIG_CPU_EXYNOS5210) || defined(CONFIG_CPU_EXYNOS5250)

extern  int exynos5_init(void);
extern void exynos5_init_irq(void);
extern void exynos5_map_io(void);
extern void exynos5_init_clocks(int xtal);
extern struct sys_timer exynos4_timer;

#define exynos5_init_uarts exynos_common_init_uarts

#else
#define exynos5_init_clocks NULL
#define exynos5_init_uarts NULL
#define exynos5_map_io NULL
#define exynos5_init NULL
#endif
