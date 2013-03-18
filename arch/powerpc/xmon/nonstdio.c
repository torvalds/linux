/*
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/string.h>
#include <asm/udbg.h>
#include <asm/time.h>
#include "nonstdio.h"


static int xmon_write(const void *ptr, int nb)
{
	return udbg_write(ptr, nb);
}

static int xmon_readchar(void)
{
	if (udbg_getc)
		return udbg_getc();
	return -1;
}

int xmon_putchar(int c)
{
	char ch = c;

	if (c == '\n')
		xmon_putchar('\r');
	return xmon_write(&ch, 1) == 1? c: -1;
}

static char line[256];
static char *lineptr;
static int lineleft;

static int xmon_getchar(void)
{
	int c;

	if (lineleft == 0) {
		lineptr = line;
		for (;;) {
			c = xmon_readchar();
			if (c == -1 || c == 4)
				break;
			if (c == '\r' || c == '\n') {
				*lineptr++ = '\n';
				xmon_putchar('\n');
				break;
			}
			switch (c) {
			case 0177:
			case '\b':
				if (lineptr > line) {
					xmon_putchar('\b');
					xmon_putchar(' ');
					xmon_putchar('\b');
					--lineptr;
				}
				break;
			case 'U' & 0x1F:
				while (lineptr > line) {
					xmon_putchar('\b');
					xmon_putchar(' ');
					xmon_putchar('\b');
					--lineptr;
				}
				break;
			default:
				if (lineptr >= &line[sizeof(line) - 1])
					xmon_putchar('\a');
				else {
					xmon_putchar(c);
					*lineptr++ = c;
				}
			}
		}
		lineleft = lineptr - line;
		lineptr = line;
	}
	if (lineleft == 0)
		return -1;
	--lineleft;
	return *lineptr++;
}

char *xmon_gets(char *str, int nb)
{
	char *p;
	int c;

	for (p = str; p < str + nb - 1; ) {
		c = xmon_getchar();
		if (c == -1) {
			if (p == str)
				return NULL;
			break;
		}
		*p++ = c;
		if (c == '\n')
			break;
	}
	*p = 0;
	return str;
}

void xmon_printf(const char *format, ...)
{
	va_list args;
	static char xmon_outbuf[1024];
	int rc, n;

	va_start(args, format);
	n = vsnprintf(xmon_outbuf, sizeof(xmon_outbuf), format, args);
	va_end(args);

	rc = xmon_write(xmon_outbuf, n);

	if (n && rc == 0) {
		/* No udbg hooks, fallback to printk() - dangerous */
		printk(xmon_outbuf);
	}
}

void xmon_puts(const char *str)
{
	xmon_write(str, strlen(str));
}
