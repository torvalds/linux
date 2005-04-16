#include <stdarg.h>
#include <linux/kernel.h>

extern void prom_putchar(char);

void prom_printf(char *fmt, ...)
{
	va_list args;
	char ppbuf[1024];
	char *bptr;

	va_start(args, fmt);
	vsprintf(ppbuf, fmt, args);

	bptr = ppbuf;

	while (*bptr != 0) {
		if (*bptr == '\n')
			prom_putchar('\r');

		prom_putchar(*bptr++);
	}
	va_end(args);
}
