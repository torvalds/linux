/*
 *  This file contains the hardware definitions for Gemini.
 *
 *  Copyright (C) 2001-2006 Storlink, Corp.
 *  Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __MACH_HARDWARE_H
#define __MACH_HARDWARE_H

/*
 * Memory Map definitions
 */
#ifdef CONFIG_GEMINI_MEM_SWAP
# define GEMINI_DRAM_BASE	0x00000000
# define GEMINI_SRAM_BASE	0x70000000
#else
# define GEMINI_SRAM_BASE	0x00000000
# define GEMINI_DRAM_BASE	0x10000000
#endif
#define GEMINI_FLASH_BASE	0x30000000
#define GEMINI_GLOBAL_BASE	0x40000000
#define GEMINI_WAQTCHDOG_BASE	0x41000000
#define GEMINI_UART_BASE	0x42000000
#define GEMINI_TIMER_BASE	0x43000000
#define GEMINI_LCD_BASE		0x44000000
#define GEMINI_RTC_BASE		0x45000000
#define GEMINI_SATA_BASE	0x46000000
#define GEMINI_LPC_HOST_BASE	0x47000000
#define GEMINI_LPC_IO_BASE	0x47800000
#define GEMINI_INTERRUPT_BASE	0x48000000
/* TODO: Different interrupt controlers when SMP
 * #define GEMINI_INTERRUPT0_BASE	0x48000000
 * #define GEMINI_INTERRUPT1_BASE	0x49000000
 */
#define GEMINI_SSP_CTRL_BASE	0x4A000000
#define GEMINI_POWER_CTRL_BASE	0x4B000000
#define GEMINI_CIR_BASE		0x4C000000
#define GEMINI_GPIO_BASE(x)	(0x4D000000 + (x) * 0x1000000)
#define GEMINI_PCI_IO_BASE	0x50000000
#define GEMINI_PCI_MEM_BASE	0x58000000
#define GEMINI_TOE_BASE		0x60000000
#define GEMINI_GMAC0_BASE	0x6000A000
#define GEMINI_GMAC1_BASE	0x6000E000
#define GEMINI_SECURITY_BASE	0x62000000
#define GEMINI_IDE0_BASE	0x63000000
#define GEMINI_IDE1_BASE	0x63400000
#define GEMINI_RAID_BASE	0x64000000
#define GEMINI_FLASH_CTRL_BASE	0x65000000
#define GEMINI_DRAM_CTRL_BASE	0x66000000
#define GEMINI_GENERAL_DMA_BASE	0x67000000
#define GEMINI_USB0_BASE	0x68000000
#define GEMINI_USB1_BASE	0x69000000
#define GEMINI_BIG_ENDIAN_BASE	0x80000000

#define GEMINI_TIMER1_BASE	GEMINI_TIMER_BASE
#define GEMINI_TIMER2_BASE	(GEMINI_TIMER_BASE + 0x10)
#define GEMINI_TIMER3_BASE	(GEMINI_TIMER_BASE + 0x20)

/*
 * UART Clock when System clk is 150MHz
 */
#define UART_CLK	48000000

/*
 * macro to get at IO space when running virtually
 */
#define IO_ADDRESS(x)	((((x) & 0xFFF00000) >> 4) | ((x) & 0x000FFFFF) | 0xF0000000)

#endif
