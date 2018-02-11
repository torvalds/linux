// SPDX-License-Identifier: GPL-2.0
/*
 *	arch/alpha/lib/srm_printk.c
 */

#include <linux/kernel.h>
#include <asm/console.h>

long
srm_printk(const char *fmt, ...)
{
	static char buf[1024];
	va_list args;
	long len, num_lf;
	char *src, *dst;

	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);

	/* count number of linefeeds in string: */

	num_lf = 0;
	for (src = buf; *src; ++src) {
		if (*src == '\n') {
			++num_lf;
		}
	}

	if (num_lf) {
		/* expand each linefeed into carriage-return/linefeed: */
		for (dst = src + num_lf; src >= buf; ) {
			if (*src == '\n') {
				*dst-- = '\r';
			}
			*dst-- = *src--;
		}
	}

	srm_puts(buf, num_lf+len);	
        return len;
}
