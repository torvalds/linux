/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * Register mappings for Loongson 1
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */


#ifndef __ASM_MACH_LOONGSON1_LOONGSON1_H
#define __ASM_MACH_LOONGSON1_LOONGSON1_H

#define DEFAULT_MEMSIZE			256	/* If no memsize provided */

/* Loongson 1 Register Bases */
#define LS1X_INTC_BASE			0x1fd01040
#define LS1X_EHCI_BASE			0x1fe00000
#define LS1X_OHCI_BASE			0x1fe08000
#define LS1X_GMAC0_BASE			0x1fe10000
#define LS1X_GMAC1_BASE			0x1fe20000

#define LS1X_UART0_BASE			0x1fe40000
#define LS1X_UART1_BASE			0x1fe44000
#define LS1X_UART2_BASE			0x1fe48000
#define LS1X_UART3_BASE			0x1fe4c000
#define LS1X_CAN0_BASE			0x1fe50000
#define LS1X_CAN1_BASE			0x1fe54000
#define LS1X_I2C0_BASE			0x1fe58000
#define LS1X_I2C1_BASE			0x1fe68000
#define LS1X_I2C2_BASE			0x1fe70000
#define LS1X_PWM_BASE			0x1fe5c000
#define LS1X_WDT_BASE			0x1fe5c060
#define LS1X_RTC_BASE			0x1fe64000
#define LS1X_AC97_BASE			0x1fe74000
#define LS1X_NAND_BASE			0x1fe78000
#define LS1X_CLK_BASE			0x1fe78030

#include <regs-clk.h>
#include <regs-wdt.h>

#endif /* __ASM_MACH_LOONGSON1_LOONGSON1_H */
