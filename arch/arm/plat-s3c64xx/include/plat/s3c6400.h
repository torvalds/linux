/* arch/arm/plat-s3c64xx/include/plat/s3c6400.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * Header file for s3c6400 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* Common init code for S3C6400 related SoCs */

extern void s3c6400_common_init_uarts(struct s3c2410_uartcfg *cfg, int no);
extern void s3c6400_register_clocks(unsigned armclk_divlimit);
extern void s3c6400_setup_clocks(void);

#ifdef CONFIG_CPU_S3C6400

extern  int s3c6400_init(void);
extern void s3c6400_init_irq(void);
extern void s3c6400_map_io(void);
extern void s3c6400_init_clocks(int xtal);

#define s3c6400_init_uarts s3c6400_common_init_uarts

#else
#define s3c6400_init_clocks NULL
#define s3c6400_init_uarts NULL
#define s3c6400_map_io NULL
#define s3c6400_init NULL
#endif

