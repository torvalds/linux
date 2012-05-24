/* arch/arm/mach-s3c2410/include/mach/regs-s3c2412.h
 *
 * Copyright 2007 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2412 specific register definitions
*/

#ifndef __ASM_ARCH_REGS_S3C2412_H
#define __ASM_ARCH_REGS_S3C2412_H "s3c2412"

#define S3C2412_SWRST		(S3C24XX_VA_CLKPWR + 0x30)
#define S3C2412_SWRST_RESET	(0x533C2412)

/* see regs-power.h for the other registers in the power block. */

#endif	/* __ASM_ARCH_REGS_S3C2412_H */

