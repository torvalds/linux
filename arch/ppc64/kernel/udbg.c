/*
 * polling mode stateless debugging stuff, originally for NS16550 Serial Ports
 *
 * c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#define WANT_PPCDBG_TAB /* Only defined here */
#include <linux/config.h>
#include <linux/types.h>
#include <asm/ppcdebug.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/prom.h>

void udbg_puts(const char *s)
{
	if (ppc_md.udbg_putc) {
		char c;

		if (s && *s != '\0') {
			while ((c = *s++) != '\0')
				ppc_md.udbg_putc(c);
		}
	}
#if 0
	else {
		printk("%s", s);
	}
#endif
}

int udbg_write(const char *s, int n)
{
	int remain = n;
	char c;

	if (!ppc_md.udbg_putc)
		return 0;

	if (s && *s != '\0') {
		while (((c = *s++) != '\0') && (remain-- > 0)) {
			ppc_md.udbg_putc(c);
		}
	}

	return n - remain;
}

int udbg_read(char *buf, int buflen)
{
	char c, *p = buf;
	int i;

	if (!ppc_md.udbg_getc)
		return 0;

	for (i = 0; i < buflen; ++i) {
		do {
			c = ppc_md.udbg_getc();
		} while (c == 0x11 || c == 0x13);
		if (c == 0)
			break;
		*p++ = c;
	}

	return i;
}

void udbg_console_write(struct console *con, const char *s, unsigned int n)
{
	udbg_write(s, n);
}

#define UDBG_BUFSIZE 256
void udbg_printf(const char *fmt, ...)
{
	unsigned char buf[UDBG_BUFSIZE];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buf, UDBG_BUFSIZE, fmt, args);
	udbg_puts(buf);
	va_end(args);
}

/* Special print used by PPCDBG() macro */
void udbg_ppcdbg(unsigned long debug_flags, const char *fmt, ...)
{
	unsigned long active_debugs = debug_flags & ppc64_debug_switch;

	if (active_debugs) {
		va_list ap;
		unsigned char buf[UDBG_BUFSIZE];
		unsigned long i, len = 0;

		for (i=0; i < PPCDBG_NUM_FLAGS; i++) {
			if (((1U << i) & active_debugs) && 
			    trace_names[i]) {
				len += strlen(trace_names[i]); 
				udbg_puts(trace_names[i]);
				break;
			}
		}

		snprintf(buf, UDBG_BUFSIZE, " [%s]: ", current->comm);
		len += strlen(buf); 
		udbg_puts(buf);

		while (len < 18) {
			udbg_puts(" ");
			len++;
		}

		va_start(ap, fmt);
		vsnprintf(buf, UDBG_BUFSIZE, fmt, ap);
		udbg_puts(buf);
		va_end(ap);
	}
}

unsigned long udbg_ifdebug(unsigned long flags)
{
	return (flags & ppc64_debug_switch);
}
