/*
 * arch/arm/mach-h720x/include/mach/h7201-regs.h
 *
 * Copyright (C) 2000 Jungjun Kim, Hynix Semiconductor Inc.
 *           (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *           (C) 2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *           (C) 2004 Sascha Hauer    <s.hauer@pengutronix.de>
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

#define SERIAL2_VIRT 		(IO_VIRT + 0x50100)
#define SERIAL3_VIRT 		(IO_VIRT + 0x50200)

/*
 * PCMCIA
 */
#define PCMCIA0_ATT_BASE        0xe5000000
#define PCMCIA0_ATT_SIZE        0x00200000
#define PCMCIA0_ATT_START       0x20000000
#define PCMCIA0_MEM_BASE        0xe5200000
#define PCMCIA0_MEM_SIZE        0x00200000
#define PCMCIA0_MEM_START       0x24000000
#define PCMCIA0_IO_BASE         0xe5400000
#define PCMCIA0_IO_SIZE         0x00200000
#define PCMCIA0_IO_START        0x28000000

#define PCMCIA1_ATT_BASE        0xe5600000
#define PCMCIA1_ATT_SIZE        0x00200000
#define PCMCIA1_ATT_START       0x30000000
#define PCMCIA1_MEM_BASE        0xe5800000
#define PCMCIA1_MEM_SIZE        0x00200000
#define PCMCIA1_MEM_START       0x34000000
#define PCMCIA1_IO_BASE         0xe5a00000
#define PCMCIA1_IO_SIZE         0x00200000
#define PCMCIA1_IO_START        0x38000000

#define PRIME3C_BASE            0xf0050000
#define PRIME3C_SIZE            0x00001000
#define PRIME3C_START           0x10000000

/* VGA Controller */
#define VGA_RAMBASE 		0x50
#define VGA_TIMING0 		0x60
#define VGA_TIMING1 		0x64
#define VGA_TIMING2 		0x68
#define VGA_TIMING3 		0x6c

#define LCD_CTRL_VGA_ENABLE   	0x00000100
#define LCD_CTRL_VGA_BPP_MASK 	0x00000600
#define LCD_CTRL_VGA_4BPP    	0x00000000
#define LCD_CTRL_VGA_8BPP    	0x00000200
#define LCD_CTRL_VGA_16BPP   	0x00000300
#define LCD_CTRL_SHARE_DMA    	0x00000800
#define LCD_CTRL_VDE          	0x00100000
#define LCD_CTRL_LPE          	0x00400000	/* LCD Power enable */
#define LCD_CTRL_BLE          	0x00800000	/* LCD backlight enable */

#define VGA_PALETTE_BASE	(IO_VIRT + 0x10800)
