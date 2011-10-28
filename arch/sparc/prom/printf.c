/*
 * printf.c:  Internal prom library printf facility.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (c) 2002 Pete Zaitcev (zaitcev@yahoo.com)
 *
 * We used to warn all over the code: DO NOT USE prom_printf(),
 * and yet people do. Anton's banking code was outputting banks
 * with prom_printf for most of the 2.4 lifetime. Since an effective
 * stick is not available, we deployed a carrot: an early printk
 * through PROM by means of -p boot option. This ought to fix it.
 * USE printk; if you need, deploy -p.
 */

#include <linux/kernel.h>
#include <linux/compiler.h>

#include <asm/openprom.h>
#include <asm/oplib.h>

static char ppbuf[1024];

void notrace prom_write(const char *buf, unsigned int n)
{
	char ch;

	while (n != 0) {
		--n;
		if ((ch = *buf++) == '\n')
			prom_putchar('\r');
		prom_putchar(ch);
	}
}

void notrace prom_printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vscnprintf(ppbuf, sizeof(ppbuf), fmt, args);
	va_end(args);

	prom_write(ppbuf, i);
}
