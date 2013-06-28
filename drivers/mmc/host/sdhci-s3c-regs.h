/* linux/arch/arm/plat-s3c/include/plat/regs-sdhci.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C Platform - SDHCI (HSMMC) register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_S3C_SDHCI_REGS_H
#define __PLAT_S3C_SDHCI_REGS_H __FILE__

#define S3C_SDHCI_CONTROL2			(0x80)
#define S3C_SDHCI_CONTROL3			(0x84)
#define S3C64XX_SDHCI_CONTROL4			(0x8C)

#define S3C64XX_SDHCI_CTRL2_ENSTAASYNCCLR	(1 << 31)
#define S3C64XX_SDHCI_CTRL2_ENCMDCNFMSK		(1 << 30)
#define S3C_SDHCI_CTRL2_CDINVRXD3		(1 << 29)
#define S3C_SDHCI_CTRL2_SLCARDOUT		(1 << 28)

#define S3C_SDHCI_CTRL2_FLTCLKSEL_MASK		(0xf << 24)
#define S3C_SDHCI_CTRL2_FLTCLKSEL_SHIFT		(24)
#define S3C_SDHCI_CTRL2_FLTCLKSEL(_x)		((_x) << 24)

#define S3C_SDHCI_CTRL2_LVLDAT_MASK		(0xff << 16)
#define S3C_SDHCI_CTRL2_LVLDAT_SHIFT		(16)
#define S3C_SDHCI_CTRL2_LVLDAT(_x)		((_x) << 16)

#define S3C_SDHCI_CTRL2_ENFBCLKTX		(1 << 15)
#define S3C_SDHCI_CTRL2_ENFBCLKRX		(1 << 14)
#define S3C_SDHCI_CTRL2_SDCDSEL			(1 << 13)
#define S3C_SDHCI_CTRL2_SDSIGPC			(1 << 12)
#define S3C_SDHCI_CTRL2_ENBUSYCHKTXSTART	(1 << 11)

#define S3C_SDHCI_CTRL2_DFCNT_MASK		(0x3 << 9)
#define S3C_SDHCI_CTRL2_DFCNT_SHIFT		(9)
#define S3C_SDHCI_CTRL2_DFCNT_NONE		(0x0 << 9)
#define S3C_SDHCI_CTRL2_DFCNT_4SDCLK		(0x1 << 9)
#define S3C_SDHCI_CTRL2_DFCNT_16SDCLK		(0x2 << 9)
#define S3C_SDHCI_CTRL2_DFCNT_64SDCLK		(0x3 << 9)

#define S3C_SDHCI_CTRL2_ENCLKOUTHOLD		(1 << 8)
#define S3C_SDHCI_CTRL2_RWAITMODE		(1 << 7)
#define S3C_SDHCI_CTRL2_DISBUFRD		(1 << 6)
#define S3C_SDHCI_CTRL2_SELBASECLK_MASK		(0x3 << 4)
#define S3C_SDHCI_CTRL2_SELBASECLK_SHIFT	(4)
#define S3C_SDHCI_CTRL2_PWRSYNC			(1 << 3)
#define S3C_SDHCI_CTRL2_ENCLKOUTMSKCON		(1 << 1)
#define S3C_SDHCI_CTRL2_HWINITFIN		(1 << 0)

#define S3C_SDHCI_CTRL3_FCSEL3			(1 << 31)
#define S3C_SDHCI_CTRL3_FCSEL2			(1 << 23)
#define S3C_SDHCI_CTRL3_FCSEL1			(1 << 15)
#define S3C_SDHCI_CTRL3_FCSEL0			(1 << 7)

#define S3C_SDHCI_CTRL3_FIA3_MASK		(0x7f << 24)
#define S3C_SDHCI_CTRL3_FIA3_SHIFT		(24)
#define S3C_SDHCI_CTRL3_FIA3(_x)		((_x) << 24)

#define S3C_SDHCI_CTRL3_FIA2_MASK		(0x7f << 16)
#define S3C_SDHCI_CTRL3_FIA2_SHIFT		(16)
#define S3C_SDHCI_CTRL3_FIA2(_x)		((_x) << 16)

#define S3C_SDHCI_CTRL3_FIA1_MASK		(0x7f << 8)
#define S3C_SDHCI_CTRL3_FIA1_SHIFT		(8)
#define S3C_SDHCI_CTRL3_FIA1(_x)		((_x) << 8)

#define S3C_SDHCI_CTRL3_FIA0_MASK		(0x7f << 0)
#define S3C_SDHCI_CTRL3_FIA0_SHIFT		(0)
#define S3C_SDHCI_CTRL3_FIA0(_x)		((_x) << 0)

#define S3C64XX_SDHCI_CONTROL4_DRIVE_MASK	(0x3 << 16)
#define S3C64XX_SDHCI_CONTROL4_DRIVE_SHIFT	(16)
#define S3C64XX_SDHCI_CONTROL4_DRIVE_2mA	(0x0 << 16)
#define S3C64XX_SDHCI_CONTROL4_DRIVE_4mA	(0x1 << 16)
#define S3C64XX_SDHCI_CONTROL4_DRIVE_7mA	(0x2 << 16)
#define S3C64XX_SDHCI_CONTROL4_DRIVE_9mA	(0x3 << 16)

#define S3C64XX_SDHCI_CONTROL4_BUSY		(1)

#endif /* __PLAT_S3C_SDHCI_REGS_H */
