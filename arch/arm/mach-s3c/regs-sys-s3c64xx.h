/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C64XX system register definitions
*/

#ifndef __MACH_S3C64XX_REGS_SYS_H
#define __MACH_S3C64XX_REGS_SYS_H __FILE__

#define S3C_SYSREG(x)			(S3C_VA_SYS + (x))

#define S3C64XX_AHB_CON0		S3C_SYSREG(0x100)
#define S3C64XX_AHB_CON1		S3C_SYSREG(0x104)
#define S3C64XX_AHB_CON2		S3C_SYSREG(0x108)

#define S3C64XX_SDMA_SEL		S3C_SYSREG(0x110)

#define S3C64XX_OTHERS			S3C_SYSREG(0x900)

#define S3C64XX_OTHERS_USBMASK		(1 << 16)
#define S3C64XX_OTHERS_SYNCMUXSEL	(1 << 6)

#endif /* __MACH_S3C64XX_REGS_SYS_H */
