/* linux/arch/arm/plat-samsung/include/plat/fiq.h
 *
 * Copyright (c) 2009 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for S3C24XX CPU FIQ support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

extern int s3c24xx_set_fiq(unsigned int irq, bool on);
