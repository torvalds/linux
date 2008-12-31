/* linux/include/asm-arm/plat-s3c24xx/s3c2410.h
 *
 * Copyright (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for s3c2410 machine directory
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#ifdef CONFIG_CPU_S3C2410

extern  int s3c2410_init(void);

extern void s3c2410_map_io(void);

extern void s3c2410_init_uarts(struct s3c2410_uartcfg *cfg, int no);

extern void s3c2410_init_clocks(int xtal);

#else
#define s3c2410_init_clocks NULL
#define s3c2410_init_uarts NULL
#define s3c2410_map_io NULL
#define s3c2410_init NULL
#endif

extern int s3c2410_baseclk_add(void);
