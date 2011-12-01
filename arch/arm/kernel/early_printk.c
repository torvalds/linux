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


void sw_put_char(u8 val)
{
	while ((SW_UART0_USR & 0x2) == 0);
		SW_UART0_THR = val;
}

void sw_put_string(char *buf, int n)
{
	int len = n;
	int i;

	for (i=0; i<len; i++) {
		sw_put_char(buf[i]);
	}
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

