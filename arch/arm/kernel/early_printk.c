/*
 *  linux/arch/arm/kernel/early_printk.c
 *
 *  Copyright (C) 2009 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/init.h>
#include <mach/platform.h>

void sw_put_char0(u8 val)
{
#ifdef CONFIG_SUN5I_A13
	while ((SW_UART1_USR & 0x2) == 0);
		SW_UART1_THR = val;
#else
	while ((SW_UART0_USR & 0x2) == 0);
		SW_UART0_THR = val;
#endif
}

void sw_put_char1(u8 val)
{
	while ((SW_UART1_USR & 0x2) == 0);
		SW_UART1_THR = val;
}

static void (*sw_put_char[2])(u8 val) = {
	sw_put_char0,
	sw_put_char1
};

void sw_put_string(char *buf, int n)
{
	int len = n;
	int i;
	static int dbg_uart = 0;
	static int first = 1;

	if (first) {
		if (SW_UART0_LSR)
			dbg_uart = 0;
		else if (SW_UART1_LSR)
			dbg_uart = 1;
		else
			return;
		first = 0;
	}
	for (i=0; i<len; i++)
		(sw_put_char[dbg_uart])(buf[i]);
}

asmlinkage void early_printk(const char *fmt, ...)
{
	char buf[512];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = vscnprintf(buf, sizeof(buf), fmt, ap);
	sw_put_string(buf, n);
	va_end(ap);
}

