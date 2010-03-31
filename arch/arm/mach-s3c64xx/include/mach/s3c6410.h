/* arch/arm/mach-s3c64xx/include/mach/s3c6410.h
 *
 * Copyright 2008 Openmoko,  Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * Header file for s3c6410 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifdef CONFIG_CPU_S3C6410

extern  int s3c6410_init(void);
extern void s3c6410_init_irq(void);
extern void s3c6410_map_io(void);
extern void s3c6410_init_clocks(int xtal);

#define s3c6410_init_uarts s3c6400_common_init_uarts

#else
#define s3c6410_init_clocks NULL
#define s3c6410_init_uarts NULL
#define s3c6410_map_io NULL
#define s3c6410_init NULL
#endif
