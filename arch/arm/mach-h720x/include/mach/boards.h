/*
 * arch/arm/mach-h720x/include/mach/boards.h
 *
 * Copyright (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *           (C) 2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *
 * This file contains the board specific defines for various devices
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_HARDWARE_INCMACH_H
#error Do not include this file directly. Include asm/hardware.h instead !
#endif

/* Hynix H7202 developer board specific device defines */
#ifdef CONFIG_ARCH_H7202

/* FLASH */
#define H720X_FLASH_VIRT	0xd0000000
#define H720X_FLASH_PHYS	0x00000000
#define H720X_FLASH_SIZE	0x02000000

/* onboard LAN controller */
# define ETH0_PHYS		0x08000000

/* Touch screen defines */
/* GPIO Port */
#define PEN_GPIO		GPIO_B_VIRT
/* Bitmask for pen down interrupt */
#define PEN_INT_BIT		(1<<7)
/* Bitmask for pen up interrupt */
#define PEN_ENA_BIT		(1<<6)
/* pen up interrupt */
#define IRQ_PEN			IRQ_MUX_GPIOB(7)

#endif

/* Hynix H7201 developer board specific device defines */
#if defined (CONFIG_ARCH_H7201)
/* ROM DISK SPACE */
#define ROM_DISK_BASE           0xc1800000
#define ROM_DISK_START          0x41800000
#define ROM_DISK_SIZE           0x00700000

/* SRAM DISK SPACE */
#define SRAM_DISK_BASE          0xf1000000
#define SRAM_DISK_START         0x04000000
#define SRAM_DISK_SIZE          0x00400000
#endif

