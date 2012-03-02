/* arch/arm/mach-s3c2410/include/mach/regs-s3c2416-mem.h
 *
 * Copyright (c) 2009 Yauhen Kharuzhy <jekhor@gmail.com>,
 *	as part of OpenInkpot project
 * Copyright (c) 2009 Promwad Innovation Company
 *	Yauhen Kharuzhy <yauhen.kharuzhy@promwad.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2416 memory register definitions
*/

#ifndef __ASM_ARM_REGS_S3C2416_MEM
#define __ASM_ARM_REGS_S3C2416_MEM

#ifndef S3C2416_MEMREG
#define S3C2416_MEMREG(x) (S3C24XX_VA_MEMCTRL + (x))
#endif

#define S3C2416_BANKCFG			S3C2416_MEMREG(0x00)
#define S3C2416_BANKCON1		S3C2416_MEMREG(0x04)
#define S3C2416_BANKCON2		S3C2416_MEMREG(0x08)
#define S3C2416_BANKCON3		S3C2416_MEMREG(0x0C)

#define S3C2416_REFRESH			S3C2416_MEMREG(0x10)
#define S3C2416_TIMEOUT			S3C2416_MEMREG(0x14)

#endif /*  __ASM_ARM_REGS_S3C2416_MEM */
