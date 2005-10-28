/*
 * Written by Cort Dougan to replace the version originally used
 * by Paul Mackerras, which came from NetBSD and thus had copyright
 * conflicts with Linux.
 *
 * This file makes liberal use of the standard linux utility
 * routines to reduce the size of the binary.  We assume we can
 * trust some parts of Linux inside the debugger.
 *   -- Cort (cort@cs.nmt.edu)
 *
 * Copyright (C) 1999 Cort Dougan.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <stdarg.h>
#include "nonstdio.h"

extern int xmon_write(void *, void *, int);

void xmon_vfprintf(void *f, const char *fmt, va_list ap)
{
	static char xmon_buf[2048];
	int n;

	n = vsprintf(xmon_buf, fmt, ap);
	xmon_write(f, xmon_buf, n);
}

void xmon_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	xmon_vfprintf(stdout, fmt, ap);
	va_end(ap);
}
EXPORT_SYMBOL(xmon_printf);

void xmon_fprintf(void *f, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	xmon_vfprintf(f, fmt, ap);
	va_end(ap);
}

