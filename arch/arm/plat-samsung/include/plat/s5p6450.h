/* linux/arch/arm/plat-samsung/include/plat/s5p6450.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Header file for s5p6450 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* Common init code for S5P6450 related SoCs */

extern void s5p6450_register_clocks(void);
extern void s5p6450_setup_clocks(void);

#ifdef CONFIG_CPU_S5P6450

extern  int s5p64x0_init(void);
extern void s5p6450_init_irq(void);
extern void s5p6450_map_io(void);
extern void s5p6450_init_clocks(int xtal);

extern void s5p6450_init_uarts(struct s3c2410_uartcfg *cfg, int no);

#else
#define s5p6450_init_clocks NULL
#define s5p6450_init_uarts NULL
#define s5p6450_map_io NULL
#define s5p64x0_init NULL
#endif

/* S5P6450 timer */

extern struct sys_timer s5p6450_timer;
