/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Header for S5P64X0 machines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_S5P64X0_COMMON_H
#define __ARCH_ARM_MACH_S5P64X0_COMMON_H

void s5p6440_init_irq(void);
void s5p6450_init_irq(void);
void s5p64x0_init_io(struct map_desc *mach_desc, int size);

void s5p6440_register_clocks(void);
void s5p6440_setup_clocks(void);

void s5p6450_register_clocks(void);
void s5p6450_setup_clocks(void);

void s5p64x0_restart(char mode, const char *cmd);

#ifdef CONFIG_CPU_S5P6440

extern  int s5p64x0_init(void);
extern void s5p6440_map_io(void);
extern void s5p6440_init_clocks(int xtal);

extern void s5p6440_init_uarts(struct s3c2410_uartcfg *cfg, int no);

#else
#define s5p6440_init_clocks NULL
#define s5p6440_init_uarts NULL
#define s5p6440_map_io NULL
#define s5p64x0_init NULL
#endif

#ifdef CONFIG_CPU_S5P6450

extern  int s5p64x0_init(void);
extern void s5p6450_map_io(void);
extern void s5p6450_init_clocks(int xtal);

extern void s5p6450_init_uarts(struct s3c2410_uartcfg *cfg, int no);

#else
#define s5p6450_init_clocks NULL
#define s5p6450_init_uarts NULL
#define s5p6450_map_io NULL
#define s5p64x0_init NULL
#endif

#endif /* __ARCH_ARM_MACH_S5P64X0_COMMON_H */
