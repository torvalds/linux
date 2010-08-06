/* linux/arch/arm/plat-s5p/include/plat/s5pv310.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Header file for s5pv310 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* Common init code for S5PV310 related SoCs */

extern void s5pv310_common_init_uarts(struct s3c2410_uartcfg *cfg, int no);
extern void s5pv310_register_clocks(void);
extern void s5pv310_setup_clocks(void);

#ifdef CONFIG_CPU_S5PV310

extern  int s5pv310_init(void);
extern void s5pv310_init_irq(void);
extern void s5pv310_map_io(void);
extern void s5pv310_init_clocks(int xtal);
extern struct sys_timer s5pv310_timer;

#define s5pv310_init_uarts s5pv310_common_init_uarts

#else
#define s5pv310_init_clocks NULL
#define s5pv310_init_uarts NULL
#define s5pv310_map_io NULL
#define s5pv310_init NULL
#endif
