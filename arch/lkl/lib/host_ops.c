// SPDX-License-Identifier: GPL-2.0

#include <asm/host_ops.h>
#include <linux/stdarg.h>
#include <linux/sprintf.h>

static int lkl_vprintf(const char *fmt, va_list args)
{
	int n;
	char *buffer;
	va_list copy;

	if (!lkl_ops->print)
		return 0;

	va_copy(copy, args);
	n = vsnprintf(NULL, 0, fmt, copy);
	va_end(copy);

	buffer = lkl_ops->mem_alloc(n + 1);
	if (!buffer)
		return -1;

	vsnprintf(buffer, n + 1, fmt, args);

	lkl_ops->print(buffer, n);
	lkl_ops->mem_free(buffer);

	return n;
}

int lkl_printf(const char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);
	n = lkl_vprintf(fmt, args);
	va_end(args);

	return n;
}

void lkl_bug(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	lkl_vprintf(fmt, args);
	va_end(args);

	lkl_ops->panic();
}
