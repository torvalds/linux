/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/mach-pxa/include/mach/zeus.h
 *
 *  Author:	David Vrabel
 *  Created:	Sept 28, 2005
 *  Copyright:	Arcom Control Systems Ltd.
 *
 *  Maintained by: Marc Zyngier <maz@misterjones.org>
 */

#ifndef _MACH_ZEUS_H
#define _MACH_ZEUS_H

#define ZEUS_NR_IRQS		(IRQ_BOARD_START + 48)

/* Physical addresses */
#define ZEUS_FLASH_PHYS		PXA_CS0_PHYS
#define ZEUS_ETH0_PHYS		PXA_CS1_PHYS
#define ZEUS_ETH1_PHYS		PXA_CS2_PHYS
#define ZEUS_CPLD_PHYS		(PXA_CS4_PHYS+0x2000000)
#define ZEUS_SRAM_PHYS		PXA_CS5_PHYS
#define ZEUS_PC104IO_PHYS	(0x30000000)

#define ZEUS_CPLD_VERSION_PHYS	(ZEUS_CPLD_PHYS + 0x00000000)
#define ZEUS_CPLD_ISA_IRQ_PHYS	(ZEUS_CPLD_PHYS + 0x00800000)
#define ZEUS_CPLD_CONTROL_PHYS	(ZEUS_CPLD_PHYS + 0x01000000)
#define ZEUS_CPLD_EXTWDOG_PHYS	(ZEUS_CPLD_PHYS + 0x01800000)

/* GPIOs */
#define ZEUS_AC97_GPIO		0
#define ZEUS_WAKEUP_GPIO	1
#define ZEUS_UARTA_GPIO		9
#define ZEUS_UARTB_GPIO		10
#define ZEUS_UARTC_GPIO		12
#define ZEUS_UARTD_GPIO		11
#define ZEUS_ETH0_GPIO		14
#define ZEUS_ISA_GPIO		17
#define ZEUS_BKLEN_GPIO		19
#define ZEUS_USB2_PWREN_GPIO	22
#define ZEUS_PTT_GPIO		27
#define ZEUS_CF_CD_GPIO         35
#define ZEUS_MMC_WP_GPIO        52
#define ZEUS_MMC_CD_GPIO        53
#define ZEUS_EXTGPIO_GPIO	91
#define ZEUS_CF_PWEN_GPIO       97
#define ZEUS_CF_RDY_GPIO        99
#define ZEUS_LCD_EN_GPIO	101
#define ZEUS_ETH1_GPIO		113
#define ZEUS_CAN_GPIO		116

#define ZEUS_EXT0_GPIO_BASE	128
#define ZEUS_EXT1_GPIO_BASE	160
#define ZEUS_USER_GPIO_BASE	192

#define ZEUS_EXT0_GPIO(x)	(ZEUS_EXT0_GPIO_BASE + (x))
#define ZEUS_EXT1_GPIO(x)	(ZEUS_EXT1_GPIO_BASE + (x))
#define ZEUS_USER_GPIO(x)	(ZEUS_USER_GPIO_BASE + (x))

#define	ZEUS_CAN_SHDN_GPIO	ZEUS_EXT1_GPIO(2)

/*
 * CPLD registers:
 * Only 4 registers, but spread over a 32MB address space.
 * Be gentle, and remap that over 32kB...
 */

#define ZEUS_CPLD		IOMEM(0xf0000000)
#define ZEUS_CPLD_VERSION	(ZEUS_CPLD + 0x0000)
#define ZEUS_CPLD_ISA_IRQ	(ZEUS_CPLD + 0x1000)
#define ZEUS_CPLD_CONTROL	(ZEUS_CPLD + 0x2000)

/* CPLD register bits */
#define ZEUS_CPLD_CONTROL_CF_RST        0x01

#define ZEUS_PC104IO		IOMEM(0xf1000000)

#define ZEUS_SRAM_SIZE		(256 * 1024)

#endif


