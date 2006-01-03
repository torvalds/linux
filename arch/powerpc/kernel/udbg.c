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
#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/console.h>
#include <asm/processor.h>

void (*udbg_putc)(unsigned char c);
unsigned char (*udbg_getc)(void);
int (*udbg_getc_poll)(void);

/* udbg library, used by xmon et al */
void udbg_puts(const char *s)
{
	if (udbg_putc) {
		char c;

		if (s && *s != '\0') {
			while ((c = *s++) != '\0')
				udbg_putc(c);
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

	if (!udbg_putc)
		return 0;

	if (s && *s != '\0') {
		while (((c = *s++) != '\0') && (remain-- > 0)) {
			udbg_putc(c);
		}
	}

	return n - remain;
}

int udbg_read(char *buf, int buflen)
{
	char c, *p = buf;
	int i;

	if (!udbg_getc)
		return 0;

	for (i = 0; i < buflen; ++i) {
		do {
			c = udbg_getc();
		} while (c == 0x11 || c == 0x13);
		if (c == 0)
			break;
		*p++ = c;
	}

	return i;
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

/*
 * Early boot console based on udbg
 */
static void udbg_console_write(struct console *con, const char *s,
		unsigned int n)
{
	udbg_write(s, n);
}

static struct console udbg_console = {
	.name	= "udbg",
	.write	= udbg_console_write,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};

static int early_console_initialized;

void __init disable_early_printk(void)
{
	if (!early_console_initialized)
		return;
	unregister_console(&udbg_console);
	early_console_initialized = 0;
}

/* called by setup_system */
void register_early_udbg_console(void)
{
	early_console_initialized = 1;
	register_console(&udbg_console);
}

#if 0   /* if you want to use this as a regular output console */
console_initcall(register_udbg_console);
#endif
