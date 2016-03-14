/*
 * arch/arm/mach-pxa/include/mach/lpd270.h
 *
 * Author:	Lennert Buytenhek
 * Created:	Feb 10, 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_LPD270_H
#define __ASM_ARCH_LPD270_H

#define LPD270_CPLD_PHYS	PXA_CS2_PHYS
#define LPD270_CPLD_VIRT	IOMEM(0xf0000000)
#define LPD270_CPLD_SIZE	0x00100000

#define LPD270_ETH_PHYS		(PXA_CS2_PHYS + 0x01000000)

/* CPLD registers  */
#define LPD270_CPLD_REG(x)	(LPD270_CPLD_VIRT + (x))
#define LPD270_CONTROL		LPD270_CPLD_REG(0x00)
#define LPD270_PERIPHERAL0	LPD270_CPLD_REG(0x04)
#define LPD270_PERIPHERAL1	LPD270_CPLD_REG(0x08)
#define LPD270_CPLD_REVISION	LPD270_CPLD_REG(0x14)
#define LPD270_EEPROM_SPI_ITF	LPD270_CPLD_REG(0x20)
#define LPD270_MODE_PINS	LPD270_CPLD_REG(0x24)
#define LPD270_EGPIO		LPD270_CPLD_REG(0x30)
#define LPD270_INT_MASK		LPD270_CPLD_REG(0x40)
#define LPD270_INT_STATUS	LPD270_CPLD_REG(0x50)

#define LPD270_INT_AC97		(1 << 4)  /* AC'97 CODEC IRQ */
#define LPD270_INT_ETHERNET	(1 << 3)  /* Ethernet controller IRQ */
#define LPD270_INT_USBC		(1 << 2)  /* USB client cable detection IRQ */

#define LPD270_IRQ(x)		(IRQ_BOARD_START + (x))
#define LPD270_USBC_IRQ		LPD270_IRQ(2)
#define LPD270_ETHERNET_IRQ	LPD270_IRQ(3)
#define LPD270_AC97_IRQ		LPD270_IRQ(4)
#define LPD270_NR_IRQS		(IRQ_BOARD_START + 5)

#endif
