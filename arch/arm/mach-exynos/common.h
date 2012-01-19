/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Header for EXYNOS machines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_EXYNOS_COMMON_H
#define __ARCH_ARM_MACH_EXYNOS_COMMON_H

void exynos_init_io(struct map_desc *mach_desc, int size);
void exynos4_init_irq(void);

void exynos4_register_clocks(void);
void exynos4_setup_clocks(void);

void exynos4210_register_clocks(void);
void exynos4212_register_clocks(void);

void exynos4_restart(char mode, const char *cmd);

extern struct sys_timer exynos4_timer;

#ifdef CONFIG_ARCH_EXYNOS
extern  int exynos_init(void);
extern void exynos4_map_io(void);
extern void exynos4_init_clocks(int xtal);
extern void exynos4_init_uarts(struct s3c2410_uartcfg *cfg, int no);

#else
#define exynos4_init_clocks NULL
#define exynos4_init_uarts NULL
#define exynos4_map_io NULL
#define exynos_init NULL
#endif

#endif /* __ARCH_ARM_MACH_EXYNOS_COMMON_H */
