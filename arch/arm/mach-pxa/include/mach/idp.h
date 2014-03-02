/*
 *  arch/arm/mach-pxa/include/mach/idp.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (c) 2001 Cliff Brake, Accelent Systems Inc.
 *
 * 2001-09-13: Cliff Brake <cbrake@accelent.com>
 *             Initial code
 *
 * 2005-02-15: Cliff Brake <cliff.brake@gmail.com>
 *             <http://www.vibren.com> <http://bec-systems.com>
 *             Changes for 2.6 kernel.
 */


/*
 * Note: this file must be safe to include in assembly files
 *
 * Support for the Vibren PXA255 IDP requires rev04 or later
 * IDP hardware.
 */

#include "irqs.h" /* PXA_GPIO_TO_IRQ */

#define IDP_FLASH_PHYS		(PXA_CS0_PHYS)
#define IDP_ALT_FLASH_PHYS	(PXA_CS1_PHYS)
#define IDP_MEDIAQ_PHYS		(PXA_CS3_PHYS)
#define IDP_IDE_PHYS		(PXA_CS5_PHYS + 0x03000000)
#define IDP_ETH_PHYS		(PXA_CS5_PHYS + 0x03400000)
#define IDP_COREVOLT_PHYS	(PXA_CS5_PHYS + 0x03800000)
#define IDP_CPLD_PHYS		(PXA_CS5_PHYS + 0x03C00000)


/*
 * virtual memory map
 */

#define IDP_COREVOLT_VIRT	(0xf0000000)
#define IDP_COREVOLT_SIZE	(1*1024*1024)

#define IDP_CPLD_VIRT		(IDP_COREVOLT_VIRT + IDP_COREVOLT_SIZE)
#define IDP_CPLD_SIZE		(1*1024*1024)

#if (IDP_CPLD_VIRT + IDP_CPLD_SIZE) > 0xfc000000
#error Your custom IO space is getting a bit large !!
#endif

#define CPLD_P2V(x)		((x) - IDP_CPLD_PHYS + IDP_CPLD_VIRT)
#define CPLD_V2P(x)		((x) - IDP_CPLD_VIRT + IDP_CPLD_PHYS)

#ifndef __ASSEMBLY__
#  define __CPLD_REG(x)		(*((volatile unsigned long *)CPLD_P2V(x)))
#else
#  define __CPLD_REG(x)		CPLD_P2V(x)
#endif

/* board level registers in the CPLD: (offsets from CPLD_VIRT) */

#define _IDP_CPLD_REV			(IDP_CPLD_PHYS + 0x00)
#define _IDP_CPLD_PERIPH_PWR		(IDP_CPLD_PHYS + 0x04)
#define _IDP_CPLD_LED_CONTROL		(IDP_CPLD_PHYS + 0x08)
#define _IDP_CPLD_KB_COL_HIGH		(IDP_CPLD_PHYS + 0x0C)
#define _IDP_CPLD_KB_COL_LOW		(IDP_CPLD_PHYS + 0x10)
#define _IDP_CPLD_PCCARD_EN		(IDP_CPLD_PHYS + 0x14)
#define _IDP_CPLD_GPIOH_DIR		(IDP_CPLD_PHYS + 0x18)
#define _IDP_CPLD_GPIOH_VALUE		(IDP_CPLD_PHYS + 0x1C)
#define _IDP_CPLD_GPIOL_DIR		(IDP_CPLD_PHYS + 0x20)
#define _IDP_CPLD_GPIOL_VALUE		(IDP_CPLD_PHYS + 0x24)
#define _IDP_CPLD_PCCARD_PWR		(IDP_CPLD_PHYS + 0x28)
#define _IDP_CPLD_MISC_CTRL		(IDP_CPLD_PHYS + 0x2C)
#define _IDP_CPLD_LCD			(IDP_CPLD_PHYS + 0x30)
#define _IDP_CPLD_FLASH_WE		(IDP_CPLD_PHYS + 0x34)

#define _IDP_CPLD_KB_ROW		(IDP_CPLD_PHYS + 0x50)
#define _IDP_CPLD_PCCARD0_STATUS	(IDP_CPLD_PHYS + 0x54)
#define _IDP_CPLD_PCCARD1_STATUS	(IDP_CPLD_PHYS + 0x58)
#define _IDP_CPLD_MISC_STATUS		(IDP_CPLD_PHYS + 0x5C)

/* FPGA register virtual addresses */

#define IDP_CPLD_REV			__CPLD_REG(_IDP_CPLD_REV)
#define IDP_CPLD_PERIPH_PWR		__CPLD_REG(_IDP_CPLD_PERIPH_PWR)
#define IDP_CPLD_LED_CONTROL		__CPLD_REG(_IDP_CPLD_LED_CONTROL)
#define IDP_CPLD_KB_COL_HIGH		__CPLD_REG(_IDP_CPLD_KB_COL_HIGH)
#define IDP_CPLD_KB_COL_LOW		__CPLD_REG(_IDP_CPLD_KB_COL_LOW)
#define IDP_CPLD_PCCARD_EN		__CPLD_REG(_IDP_CPLD_PCCARD_EN)
#define IDP_CPLD_GPIOH_DIR		__CPLD_REG(_IDP_CPLD_GPIOH_DIR)
#define IDP_CPLD_GPIOH_VALUE		__CPLD_REG(_IDP_CPLD_GPIOH_VALUE)
#define IDP_CPLD_GPIOL_DIR		__CPLD_REG(_IDP_CPLD_GPIOL_DIR)
#define IDP_CPLD_GPIOL_VALUE		__CPLD_REG(_IDP_CPLD_GPIOL_VALUE)
#define IDP_CPLD_PCCARD_PWR		__CPLD_REG(_IDP_CPLD_PCCARD_PWR)
#define IDP_CPLD_MISC_CTRL		__CPLD_REG(_IDP_CPLD_MISC_CTRL)
#define IDP_CPLD_LCD			__CPLD_REG(_IDP_CPLD_LCD)
#define IDP_CPLD_FLASH_WE		__CPLD_REG(_IDP_CPLD_FLASH_WE)

#define IDP_CPLD_KB_ROW		        __CPLD_REG(_IDP_CPLD_KB_ROW)
#define IDP_CPLD_PCCARD0_STATUS	        __CPLD_REG(_IDP_CPLD_PCCARD0_STATUS)
#define IDP_CPLD_PCCARD1_STATUS	        __CPLD_REG(_IDP_CPLD_PCCARD1_STATUS)
#define IDP_CPLD_MISC_STATUS		__CPLD_REG(_IDP_CPLD_MISC_STATUS)


/*
 * Bit masks for various registers
 */

// IDP_CPLD_PCCARD_PWR
#define PCC0_PWR0	(1 << 0)
#define PCC0_PWR1	(1 << 1)
#define PCC0_PWR2	(1 << 2)
#define PCC0_PWR3	(1 << 3)
#define PCC1_PWR0	(1 << 4)
#define PCC1_PWR1	(1 << 5)
#define PCC1_PWR2	(1 << 6)
#define PCC1_PWR3	(1 << 7)

// IDP_CPLD_PCCARD_EN
#define PCC0_RESET	(1 << 6)
#define PCC1_RESET	(1 << 7)
#define PCC0_ENABLE	(1 << 0)
#define PCC1_ENABLE	(1 << 1)

// IDP_CPLD_PCCARDx_STATUS
#define _PCC_WRPROT	(1 << 7) // 7-4 read as low true
#define _PCC_RESET	(1 << 6)
#define _PCC_IRQ	(1 << 5)
#define _PCC_INPACK	(1 << 4)
#define PCC_BVD2	(1 << 3)
#define PCC_BVD1	(1 << 2)
#define PCC_VS2		(1 << 1)
#define PCC_VS1		(1 << 0)

/* A listing of interrupts used by external hardware devices */

#define TOUCH_PANEL_IRQ			PXA_GPIO_TO_IRQ(5)
#define IDE_IRQ				PXA_GPIO_TO_IRQ(21)

#define TOUCH_PANEL_IRQ_EDGE		IRQ_TYPE_EDGE_FALLING

#define ETHERNET_IRQ			PXA_GPIO_TO_IRQ(4)
#define ETHERNET_IRQ_EDGE		IRQ_TYPE_EDGE_RISING

#define IDE_IRQ_EDGE			IRQ_TYPE_EDGE_RISING

#define PCMCIA_S0_CD_VALID		PXA_GPIO_TO_IRQ(7)
#define PCMCIA_S0_CD_VALID_EDGE		IRQ_TYPE_EDGE_BOTH

#define PCMCIA_S1_CD_VALID		PXA_GPIO_TO_IRQ(8)
#define PCMCIA_S1_CD_VALID_EDGE		IRQ_TYPE_EDGE_BOTH

#define PCMCIA_S0_RDYINT		PXA_GPIO_TO_IRQ(19)
#define PCMCIA_S1_RDYINT		PXA_GPIO_TO_IRQ(22)


/*
 * Macros for LED Driver
 */

/* leds 0 = ON */
#define IDP_HB_LED	(1<<5)
#define IDP_BUSY_LED	(1<<6)

#define IDP_LEDS_MASK	(IDP_HB_LED | IDP_BUSY_LED)

/*
 * macros for MTD driver
 */

#define FLASH_WRITE_PROTECT_DISABLE()	((IDP_CPLD_FLASH_WE) &= ~(0x1))
#define FLASH_WRITE_PROTECT_ENABLE()	((IDP_CPLD_FLASH_WE) |= (0x1))

/*
 * macros for matrix keyboard driver
 */

#define KEYBD_MATRIX_NUMBER_INPUTS	7
#define KEYBD_MATRIX_NUMBER_OUTPUTS	14

#define KEYBD_MATRIX_INVERT_OUTPUT_LOGIC	FALSE
#define KEYBD_MATRIX_INVERT_INPUT_LOGIC		FALSE

#define KEYBD_MATRIX_SETTLING_TIME_US			100
#define KEYBD_MATRIX_KEYSTATE_DEBOUNCE_CONSTANT		2

#define KEYBD_MATRIX_SET_OUTPUTS(outputs) \
{\
	IDP_CPLD_KB_COL_LOW = outputs;\
	IDP_CPLD_KB_COL_HIGH = outputs >> 7;\
}

#define KEYBD_MATRIX_GET_INPUTS(inputs) \
{\
	inputs = (IDP_CPLD_KB_ROW & 0x7f);\
}


