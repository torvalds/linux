/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2005 Simtec Electronics
 *	http://www.simtec.co.uk/products/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * OSIRIS - CPLD control constants
 * OSIRIS - Memory map definitions
 */

#ifndef __MACH_S3C24XX_OSIRIS_H
#define __MACH_S3C24XX_OSIRIS_H __FILE__

/* CTRL0 - NAND WP control */

#define OSIRIS_CTRL0_NANDSEL		(0x3)
#define OSIRIS_CTRL0_BOOT_INT		(1<<3)
#define OSIRIS_CTRL0_PCMCIA		(1<<4)
#define OSIRIS_CTRL0_FIX8		(1<<5)
#define OSIRIS_CTRL0_PCMCIA_nWAIT	(1<<6)
#define OSIRIS_CTRL0_PCMCIA_nIOIS16	(1<<7)

#define OSIRIS_CTRL1_FIX8		(1<<0)

#define OSIRIS_ID_REVMASK		(0x7)

/* start peripherals off after the S3C2410 */

#define OSIRIS_IOADDR(x)	(S3C2410_ADDR((x) + 0x04000000))

#define OSIRIS_PA_CPLD		(S3C2410_CS1 | (1<<26))

/* we put the CPLD registers next, to get them out of the way */

#define OSIRIS_VA_CTRL0		OSIRIS_IOADDR(0x00000000)
#define OSIRIS_PA_CTRL0		(OSIRIS_PA_CPLD)

#define OSIRIS_VA_CTRL1		OSIRIS_IOADDR(0x00100000)
#define OSIRIS_PA_CTRL1		(OSIRIS_PA_CPLD + (1<<23))

#define OSIRIS_VA_CTRL2		OSIRIS_IOADDR(0x00200000)
#define OSIRIS_PA_CTRL2		(OSIRIS_PA_CPLD + (2<<23))

#define OSIRIS_VA_CTRL3		OSIRIS_IOADDR(0x00300000)
#define OSIRIS_PA_CTRL3		(OSIRIS_PA_CPLD + (2<<23))

#define OSIRIS_VA_IDREG		OSIRIS_IOADDR(0x00700000)
#define OSIRIS_PA_IDREG		(OSIRIS_PA_CPLD + (7<<23))

#endif /* __MACH_S3C24XX_OSIRIS_H */
