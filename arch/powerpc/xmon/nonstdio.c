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

static bool paginating, paginate_skipping;
static unsigned long paginate_lpp; /* Lines Per Page */
static unsigned long paginate_pos;

void xmon_start_pagination(void)
{
	paginating = true;
	paginate_skipping = false;
	paginate_pos = 0;
}

void xmon_end_pagination(void)
{
	paginating = false;
}

void xmon_set_pagination_lpp(unsigned long lpp)
{
	paginate_lpp = lpp;
}

static int xmon_readchar(void)
{
	if (udbg_getc)
		return udbg_getc();
	return -1;
}

static int xmon_write(const char *ptr, int nb)
{
	int rv = 0;
	const char *p = ptr, *q;
	const char msg[] = "[Hit a key (a:all, q:truncate, any:next page)]";

	if (nb <= 0)
		return rv;

	if (paginating && paginate_skipping)
		return nb;

	if (paginate_lpp) {
		while (paginating && (q = strchr(p, '\n'))) {
			rv += udbg_write(p, q - p + 1);
			p = q + 1;
			paginate_pos++;

			if (paginate_pos >= paginate_lpp) {
				udbg_write(msg, strlen(msg));

				switch (xmon_readchar()) {
				case 'a':
					paginating = false;
					break;
				case 'q':
					paginate_skipping = true;
					break;
				default:
					/* nothing */
					break;
				}

				paginate_pos = 0;
				udbg_write("\r\n", 2);

				if (paginate_skipping)
					return nb;
			}
		}
	}

	return rv + udbg_write(p, nb - (p - ptr));
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
		printk("%s", xmon_outbuf);
	}
}

void xmon_puts(const char *str)
{
	xmon_write(str, strlen(str));
}
