/* linux/arch/arm/plat-s5p/include/plat/exynos4.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Header file for exynos4 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* Common init code for EXYNOS4 related SoCs */

extern void exynos4_common_init_uarts(struct s3c2410_uartcfg *cfg, int no);
extern void exynos4_register_clocks(void);
extern void exynos4_setup_clocks(void);

#ifdef CONFIG_CPU_EXYNOS4210

extern  int exynos4_init(void);
extern void exynos4_init_irq(void);
extern void exynos4_map_io(void);
extern void exynos4_init_clocks(int xtal);
extern struct sys_timer exynos4_timer;

#define exynos4_init_uarts exynos4_common_init_uarts

#else
#define exynos4_init_clocks NULL
#define exynos4_init_uarts NULL
#define exynos4_map_io NULL
#define exynos4_init NULL
#endif
