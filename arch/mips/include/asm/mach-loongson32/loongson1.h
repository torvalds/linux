/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * Register mappings for Loongson 1
 */

#ifndef __ASM_MACH_LOONGSON32_LOONGSON1_H
#define __ASM_MACH_LOONGSON32_LOONGSON1_H

#if defined(CONFIG_LOONGSON1_LS1B)
#define DEFAULT_MEMSIZE			64	/* If no memsize provided */
#elif defined(CONFIG_LOONGSON1_LS1C)
#define DEFAULT_MEMSIZE			32
#endif

/* Loongson 1 Register Bases */
#define LS1X_MUX_BASE			0x1fd00420
#define LS1X_INTC_BASE			0x1fd01040
#define LS1X_GPIO0_BASE			0x1fd010c0
#define LS1X_GPIO1_BASE			0x1fd010c4
#define LS1X_DMAC_BASE			0x1fd01160
#define LS1X_CBUS_BASE			0x1fd011c0
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
#define LS1X_PWM0_BASE			0x1fe5c000
#define LS1X_PWM1_BASE			0x1fe5c010
#define LS1X_PWM2_BASE			0x1fe5c020
#define LS1X_PWM3_BASE			0x1fe5c030
#define LS1X_WDT_BASE			0x1fe5c060
#define LS1X_RTC_BASE			0x1fe64000
#define LS1X_AC97_BASE			0x1fe74000
#define LS1X_NAND_BASE			0x1fe78000
#define LS1X_CLK_BASE			0x1fe78030

#include <regs-clk.h>
#include <regs-mux.h>
#include <regs-rtc.h>
#include <regs-wdt.h>

#endif /* __ASM_MACH_LOONGSON32_LOONGSON1_H */
