/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 */

#ifndef __ARCH_ARM_MACH_S3C24XX_S3C2412_H
#define __ARCH_ARM_MACH_S3C24XX_S3C2412_H __FILE__

#define S3C2412_MEMREG(x)		(S3C24XX_VA_MEMCTRL + (x))
#define S3C2412_EBIREG(x)		(S3C2412_VA_EBI + (x))

#define S3C2412_SSMCREG(x)		(S3C2412_VA_SSMC + (x))
#define S3C2412_SSMC(x, o)		(S3C2412_SSMCREG((x * 0x20) + (o)))

#define S3C2412_REFRESH			S3C2412_MEMREG(0x10)

#define S3C2412_EBI_BANKCFG		S3C2412_EBIREG(0x4)

#define S3C2412_SSMC_BANK(x)		S3C2412_SSMC(x, 0x0)

#endif /* __ARCH_ARM_MACH_S3C24XX_S3C2412_H */
