/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * Common Header for S3C64XX machines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_S3C64XX_COMMON_H
#define __ARCH_ARM_MACH_S3C64XX_COMMON_H

void s3c64xx_init_irq(u32 vic0, u32 vic1);
void s3c64xx_init_io(struct map_desc *mach_desc, int size);

void s3c64xx_register_clocks(unsigned long xtal, unsigned armclk_limit);
void s3c64xx_setup_clocks(void);

void s3c64xx_restart(char mode, const char *cmd);
void s3c64xx_init_late(void);

#ifdef CONFIG_CPU_S3C6400

extern  int s3c6400_init(void);
extern void s3c6400_init_irq(void);
extern void s3c6400_map_io(void);
extern void s3c6400_init_clocks(int xtal);

#else
#define s3c6400_init_clocks NULL
#define s3c6400_map_io NULL
#define s3c6400_init NULL
#endif

#ifdef CONFIG_CPU_S3C6410

extern  int s3c6410_init(void);
extern void s3c6410_init_irq(void);
extern void s3c6410_map_io(void);
extern void s3c6410_init_clocks(int xtal);

#else
#define s3c6410_init_clocks NULL
#define s3c6410_map_io NULL
#define s3c6410_init NULL
#endif

#ifdef CONFIG_PM
int __init s3c64xx_pm_late_initcall(void);
#else
static inline int s3c64xx_pm_late_initcall(void) { return 0; }
#endif

#endif /* __ARCH_ARM_MACH_S3C64XX_COMMON_H */
