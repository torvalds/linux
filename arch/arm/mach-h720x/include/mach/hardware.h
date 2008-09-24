/*
 * arch/arm/mach-h720x/include/mach/hardware.h
 *
 * Copyright (C) 2000 Jungjun Kim, Hynix Semiconductor Inc.
 *           (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *           (C) 2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *
 * This file contains the hardware definitions of the h720x processors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Do not add implementations specific defines here. This files contains
 * only defines of the onchip peripherals. Add those defines to boards.h,
 * which is included by this file.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#define IOCLK (3686400L)

/* Onchip peripherals */

#define IO_VIRT			0xf0000000	/* IO peripherals */
#define IO_PHYS			0x80000000
#define IO_SIZE			0x00050000

#ifdef CONFIG_CPU_H7202
#include "h7202-regs.h"
#elif defined CONFIG_CPU_H7201
#include "h7201-regs.h"
#else
#error machine definition mismatch
#endif

/* Macro to access the CPU IO */
#define CPU_IO(x) (*(volatile u32*)(x))

/* Macro to access general purpose regs (base, offset) */
#define CPU_REG(x,y) CPU_IO(x+y)

/* Macro to access irq related regs */
#define IRQ_REG(x) CPU_REG(IRQC_VIRT,x)

/* CPU registers */
/* general purpose I/O */
#define GPIO_VIRT(x)		(IO_VIRT + 0x23000 + ((x)<<5))
#define GPIO_A_VIRT		(GPIO_VIRT(0))
#define GPIO_B_VIRT		(GPIO_VIRT(1))
#define GPIO_C_VIRT		(GPIO_VIRT(2))
#define GPIO_D_VIRT		(GPIO_VIRT(3))
#define GPIO_E_VIRT		(GPIO_VIRT(4))
#define GPIO_AMULSEL		(GPIO_VIRT(0) + 0xA4)

#define AMULSEL_USIN2	(1<<5)
#define AMULSEL_USOUT2	(1<<6)
#define AMULSEL_USIN3	(1<<13)
#define AMULSEL_USOUT3	(1<<14)
#define AMULSEL_IRDIN	(1<<15)
#define AMULSEL_IRDOUT	(1<<7)

/* Register offsets general purpose I/O */
#define GPIO_DATA		0x00
#define GPIO_DIR		0x04
#define GPIO_MASK		0x08
#define GPIO_STAT		0x0C
#define GPIO_EDGE		0x10
#define GPIO_CLR		0x14
#define GPIO_POL		0x18
#define GPIO_EN			0x1C

/*interrupt controller */
#define IRQC_VIRT		(IO_VIRT + 0x24000)
/* register offset interrupt controller */
#define IRQC_IER		0x00
#define IRQC_ISR		0x04

/* timer unit */
#define TIMER_VIRT		(IO_VIRT + 0x25000)
/* Register offsets timer unit */
#define TM0_PERIOD   		0x00
#define TM0_COUNT    		0x08
#define TM0_CTRL     		0x10
#define TM1_PERIOD   		0x20
#define TM1_COUNT    		0x28
#define TM1_CTRL     		0x30
#define TM2_PERIOD   		0x40
#define TM2_COUNT    		0x48
#define TM2_CTRL     		0x50
#define TIMER_TOPCTRL		0x60
#define TIMER_TOPSTAT		0x64
#define T64_COUNTL		0x80
#define T64_COUNTH		0x84
#define T64_CTRL		0x88
#define T64_BASEL		0x94
#define T64_BASEH		0x98
/* Bitmaks timer unit TOPSTAT reg */
#define TSTAT_T0INT		0x1
#define TSTAT_T1INT		0x2
#define TSTAT_T2INT		0x4
#define TSTAT_T3INT		0x8
/* Bit description of TMx_CTRL register */
#define TM_START  		0x1
#define TM_REPEAT 		0x2
#define TM_RESET  		0x4
/* Bit description of TIMER_CTRL register */
#define ENABLE_TM0_INTR  	0x1
#define ENABLE_TM1_INTR  	0x2
#define ENABLE_TM2_INTR  	0x4
#define TIMER_ENABLE_BIT 	0x8
#define ENABLE_TIMER64   	0x10
#define ENABLE_TIMER64_INT	0x20

/* PMU & PLL */
#define PMU_BASE 		(IO_VIRT + 0x1000)
#define PMU_MODE		0x00
#define PMU_STAT   		0x20
#define PMU_PLL_CTRL 		0x28

/* PMU Mode bits */
#define PMU_MODE_SLOW		0x00
#define PMU_MODE_RUN		0x01
#define PMU_MODE_IDLE		0x02
#define PMU_MODE_SLEEP		0x03
#define PMU_MODE_INIT		0x04
#define PMU_MODE_DEEPSLEEP	0x07
#define PMU_MODE_WAKEUP		0x08

/* PMU ... */
#define PLL_2_EN		0x8000
#define PLL_1_EN		0x4000
#define PLL_3_MUTE		0x0080

/* Control bits for PMU/ PLL */
#define PMU_WARMRESET		0x00010000
#define PLL_CTRL_MASK23		0x000080ff

/* LCD Controller */
#define LCD_BASE 		(IO_VIRT + 0x10000)
#define LCD_CTRL 		0x00
#define LCD_STATUS		0x04
#define LCD_STATUS_M		0x08
#define LCD_INTERRUPT		0x0C
#define LCD_DBAR		0x10
#define LCD_DCAR		0x14
#define LCD_TIMING0 		0x20
#define LCD_TIMING1 		0x24
#define LCD_TIMING2 		0x28
#define LCD_TEST		0x40

/* LCD Control Bits */
#define LCD_CTRL_LCD_ENABLE   	0x00000001
/* Bits per pixel */
#define LCD_CTRL_LCD_BPP_MASK 	0x00000006
#define LCD_CTRL_LCD_4BPP    	0x00000000
#define LCD_CTRL_LCD_8BPP    	0x00000002
#define LCD_CTRL_LCD_16BPP   	0x00000004
#define LCD_CTRL_LCD_BW		0x00000008
#define LCD_CTRL_LCD_TFT	0x00000010
#define LCD_CTRL_BGR		0x00001000
#define LCD_CTRL_LCD_VCOMP	0x00080000
#define LCD_CTRL_LCD_MONO8	0x00200000
#define LCD_CTRL_LCD_PWR	0x00400000
#define LCD_CTRL_LCD_BLE	0x00800000
#define LCD_CTRL_LDBUSEN	0x01000000

/* Palette */
#define LCD_PALETTE_BASE 	(IO_VIRT + 0x10400)

/* Serial ports */
#define SERIAL0_OFS		0x20000
#define SERIAL0_VIRT 		(IO_VIRT + SERIAL0_OFS)
#define SERIAL0_BASE		(IO_PHYS + SERIAL0_OFS)

#define SERIAL1_OFS		0x21000
#define SERIAL1_VIRT 		(IO_VIRT + SERIAL1_OFS)
#define SERIAL1_BASE		(IO_PHYS + SERIAL1_OFS)

#define SERIAL_ENABLE		0x30
#define SERIAL_ENABLE_EN	(1<<0)

/* General defines to pacify gcc */
#define PCIO_BASE 		(0)	/* for inb, outb and friends */
#define PCIO_VIRT		PCIO_BASE

#define __ASM_ARCH_HARDWARE_INCMACH_H
#include "boards.h"
#undef __ASM_ARCH_HARDWARE_INCMACH_H

#endif				/* __ASM_ARCH_HARDWARE_H */
