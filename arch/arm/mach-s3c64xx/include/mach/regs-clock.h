/* arch/arm/plat-s3c64xx/include/plat/regs-clock.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C64XX clock register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_REGS_CLOCK_H
#define __PLAT_REGS_CLOCK_H __FILE__

/*
 * FIXME: Remove remaining definitions
 */

#define S3C_CLKREG(x)		(S3C_VA_SYS + (x))

#define S3C_PCLK_GATE		S3C_CLKREG(0x34)
#define S3C6410_CLK_SRC2	S3C_CLKREG(0x10C)
#define S3C_MEM_SYS_CFG		S3C_CLKREG(0x120)

/* PCLK GATE Registers */
#define S3C_CLKCON_PCLK_UART3		(1<<4)
#define S3C_CLKCON_PCLK_UART2		(1<<3)
#define S3C_CLKCON_PCLK_UART1		(1<<2)
#define S3C_CLKCON_PCLK_UART0		(1<<1)

/* MEM_SYS_CFG */
#define MEM_SYS_CFG_INDEP_CF		0x4000
#define MEM_SYS_CFG_EBI_FIX_PRI_CFCON	0x30

#endif /* _PLAT_REGS_CLOCK_H */
