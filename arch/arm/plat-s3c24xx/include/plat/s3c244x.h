/* linux/arch/arm/plat-s3c24xx/include/plat/s3c244x.h
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for S3C2440 and S3C2442 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#if defined(CONFIG_CPU_S3C2440) || defined(CONFIG_CPU_S3C2442)

extern void s3c244x_map_io(void);

extern void s3c244x_init_uarts(struct s3c2410_uartcfg *cfg, int no);

extern void s3c244x_init_clocks(int xtal);

#else
#define s3c244x_init_clocks NULL
#define s3c244x_init_uarts NULL
#define s3c244x_map_io NULL
#endif

#ifdef CONFIG_CPU_S3C2440
extern  int s3c2440_init(void);
#else
#define s3c2440_init NULL
#endif

#ifdef CONFIG_CPU_S3C2442
extern  int s3c2442_init(void);
#else
#define s3c2442_init NULL
#endif
