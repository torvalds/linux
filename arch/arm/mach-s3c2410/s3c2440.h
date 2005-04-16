/* arch/arm/mach-s3c2410/s3c2440.h
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for s3c2440 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *	24-Aug-2004 BJD  Start of S3C2440 CPU support
 *	04-Nov-2004 BJD  Added s3c2440_init_uarts()
 *	04-Jan-2005 BJD  Moved uart init to cpu code
 *	10-Jan-2005 BJD  Moved 2440 specific init here
 *	14-Jan-2005 BJD  Split the clock initialisation code
*/

#ifdef CONFIG_CPU_S3C2440

extern  int s3c2440_init(void);

extern void s3c2440_map_io(struct map_desc *mach_desc, int size);

extern void s3c2440_init_uarts(struct s3c2410_uartcfg *cfg, int no);

extern void s3c2440_init_clocks(int xtal);

#else
#define s3c2440_init_clocks NULL
#define s3c2440_init_uarts NULL
#define s3c2440_map_io NULL
#define s3c2440_init NULL
#endif
