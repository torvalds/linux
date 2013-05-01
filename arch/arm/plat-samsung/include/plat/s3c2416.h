/* linux/arch/arm/plat-samsung/include/plat/s3c2416.h
 *
 * Copyright (c) 2009 Yauhen Kharuzhy <jekhor@gmail.com>
 *
 * Header file for s3c2416 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifdef CONFIG_CPU_S3C2416

struct s3c2410_uartcfg;

extern  int s3c2416_init(void);

extern void s3c2416_map_io(void);

extern void s3c2416_init_uarts(struct s3c2410_uartcfg *cfg, int no);

extern void s3c2416_init_clocks(int xtal);

extern  int s3c2416_baseclk_add(void);

extern void s3c2416_restart(char mode, const char *cmd);

extern void s3c2416_init_irq(void);
extern struct syscore_ops s3c2416_irq_syscore_ops;

#else
#define s3c2416_init_clocks NULL
#define s3c2416_init_uarts NULL
#define s3c2416_map_io NULL
#define s3c2416_init NULL
#define s3c2416_restart NULL
#endif
