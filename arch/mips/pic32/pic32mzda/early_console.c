// SPDX-License-Identifier: GPL-2.0-only
/*
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 */
#include <asm/mach-pic32/pic32.h>
#include <asm/fw/fw.h>
#include <asm/setup.h>

#include "pic32mzda.h"
#include "early_pin.h"

/* Default early console parameters */
#define EARLY_CONSOLE_PORT	1
#define EARLY_CONSOLE_BAUDRATE	115200

#define UART_ENABLE		BIT(15)
#define UART_ENABLE_RX		BIT(12)
#define UART_ENABLE_TX		BIT(10)
#define UART_TX_FULL		BIT(9)

/* UART1(x == 0) - UART6(x == 5) */
#define UART_BASE(x)	((x) * 0x0200)
#define U_MODE(x)	UART_BASE(x)
#define U_STA(x)	(UART_BASE(x) + 0x10)
#define U_TXR(x)	(UART_BASE(x) + 0x20)
#define U_BRG(x)	(UART_BASE(x) + 0x40)

static void __iomem *uart_base;
static char console_port = -1;

static int __init configure_uart_pins(int port)
{
	switch (port) {
	case 1:
		pic32_pps_input(IN_FUNC_U2RX, IN_RPB0);
		pic32_pps_output(OUT_FUNC_U2TX, OUT_RPG9);
		break;
	case 5:
		pic32_pps_input(IN_FUNC_U6RX, IN_RPD0);
		pic32_pps_output(OUT_FUNC_U6TX, OUT_RPB8);
		break;
	default:
		return -1;
	}

	return 0;
}

static void __init configure_uart(char port, int baud)
{
	u32 pbclk;

	pbclk = pic32_get_pbclk(2);

	__raw_writel(0, uart_base + U_MODE(port));
	__raw_writel(((pbclk / baud) / 16) - 1, uart_base + U_BRG(port));
	__raw_writel(UART_ENABLE, uart_base + U_MODE(port));
	__raw_writel(UART_ENABLE_TX | UART_ENABLE_RX,
		     uart_base + PIC32_SET(U_STA(port)));
}

static void __init setup_early_console(char port, int baud)
{
	if (configure_uart_pins(port))
		return;

	console_port = port;
	configure_uart(console_port, baud);
}

static char * __init pic32_getcmdline(void)
{
	/*
	 * arch_mem_init() has not been called yet, so we don't have a real
	 * command line setup if using CONFIG_CMDLINE_BOOL.
	 */
#ifdef CONFIG_CMDLINE_OVERRIDE
	return CONFIG_CMDLINE;
#else
	return fw_getcmdline();
#endif
}

static int __init get_port_from_cmdline(char *arch_cmdline)
{
	char *s;
	int port = -1;

	if (!arch_cmdline || *arch_cmdline == '\0')
		goto _out;

	s = strstr(arch_cmdline, "earlyprintk=");
	if (s) {
		s = strstr(s, "ttyS");
		if (s)
			s += 4;
		else
			goto _out;

		port = (*s) - '0';
	}

_out:
	return port;
}

static int __init get_baud_from_cmdline(char *arch_cmdline)
{
	char *s;
	int baud = -1;

	if (!arch_cmdline || *arch_cmdline == '\0')
		goto _out;

	s = strstr(arch_cmdline, "earlyprintk=");
	if (s) {
		s = strstr(s, "ttyS");
		if (s)
			s += 6;
		else
			goto _out;

		baud = 0;
		while (*s >= '0' && *s <= '9')
			baud = baud * 10 + *s++ - '0';
	}

_out:
	return baud;
}

void __init fw_init_early_console(char port)
{
	char *arch_cmdline = pic32_getcmdline();
	int baud = -1;

	uart_base = ioremap(PIC32_BASE_UART, 0xc00);

	baud = get_baud_from_cmdline(arch_cmdline);
	if (port == -1)
		port = get_port_from_cmdline(arch_cmdline);

	if (port == -1)
		port = EARLY_CONSOLE_PORT;

	if (baud == -1)
		baud = EARLY_CONSOLE_BAUDRATE;

	setup_early_console(port, baud);
}

void prom_putchar(char c)
{
	if (console_port >= 0) {
		while (__raw_readl(
				uart_base + U_STA(console_port)) & UART_TX_FULL)
			;

		__raw_writel(c, uart_base + U_TXR(console_port));
	}
}
