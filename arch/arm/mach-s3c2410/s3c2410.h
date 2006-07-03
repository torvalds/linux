/* arch/arm/mach-s3c2410/s3c2410.h
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
 * Modifications:
 *     18-Aug-2004 BJD  Created initial version
 *     20-Aug-2004 BJD  Added s3c2410_board struct
 *     04-Sep-2004 BJD  Added s3c2410_init_uarts() call
 *     17-Oct-2004 BJD  Moved board out to cpu
 *     04-Jan-2005 BJD  Changed uart init
 *     10-Jan-2005 BJD  Removed timer to cpu.h, moved 2410 specific bits here
 *     14-Jan-2005 BJD  Added s3c2410_init_clocks call
*/

#ifdef CONFIG_CPU_S3C2410

extern  int s3c2410_init(void);

extern void s3c2410_map_io(struct map_desc *mach_desc, int size);

extern void s3c2410_init_uarts(struct s3c2410_uartcfg *cfg, int no);

extern void s3c2410_init_clocks(int xtal);

extern  int s3c2410_baseclk_add(void);

#else
#define s3c2410_init_clocks NULL
#define s3c2410_init_uarts NULL
#define s3c2410_map_io NULL
#define s3c2410_init NULL
#endif
