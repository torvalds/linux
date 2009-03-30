/* arch/arm/mach-s3c2410/include/mach/gpio-nrs.h
 *
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - GPIO bank numbering
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define S3C2410_GPIONO(bank,offset) ((bank) + (offset))

#define S3C2410_GPIO_BANKA   (32*0)
#define S3C2410_GPIO_BANKB   (32*1)
#define S3C2410_GPIO_BANKC   (32*2)
#define S3C2410_GPIO_BANKD   (32*3)
#define S3C2410_GPIO_BANKE   (32*4)
#define S3C2410_GPIO_BANKF   (32*5)
#define S3C2410_GPIO_BANKG   (32*6)
#define S3C2410_GPIO_BANKH   (32*7)
