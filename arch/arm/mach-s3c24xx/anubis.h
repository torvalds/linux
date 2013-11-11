/*
 * Copyright (c) 2005 Simtec Electronics
 *	http://www.simtec.co.uk/products/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * ANUBIS - CPLD control constants
 * ANUBIS - IRQ Number definitions
 * ANUBIS - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __MACH_S3C24XX_ANUBIS_H
#define __MACH_S3C24XX_ANUBIS_H __FILE__

/* CTRL2 - NAND WP control, IDE Reset assert/check */

#define ANUBIS_CTRL1_NANDSEL		(0x3)

/* IDREG - revision */

#define ANUBIS_IDREG_REVMASK		(0x7)

/* irq */

#define ANUBIS_IRQ_IDE0			IRQ_EINT2
#define ANUBIS_IRQ_IDE1			IRQ_EINT3
#define ANUBIS_IRQ_ASIX			IRQ_EINT1

/* map */

/* start peripherals off after the S3C2410 */

#define ANUBIS_IOADDR(x)		(S3C2410_ADDR((x) + 0x01800000))

#define ANUBIS_PA_CPLD			(S3C2410_CS1 | (1<<26))

/* we put the CPLD registers next, to get them out of the way */

#define ANUBIS_VA_CTRL1			ANUBIS_IOADDR(0x00000000)
#define ANUBIS_PA_CTRL1			ANUBIS_PA_CPLD

#define ANUBIS_VA_IDREG			ANUBIS_IOADDR(0x00300000)
#define ANUBIS_PA_IDREG			(ANUBIS_PA_CPLD + (3 << 23))

#define ANUBIS_IDEPRI			ANUBIS_IOADDR(0x01000000)
#define ANUBIS_IDEPRIAUX		ANUBIS_IOADDR(0x01100000)
#define ANUBIS_IDESEC			ANUBIS_IOADDR(0x01200000)
#define ANUBIS_IDESECAUX		ANUBIS_IOADDR(0x01300000)

#endif /* __MACH_S3C24XX_ANUBIS_H */
