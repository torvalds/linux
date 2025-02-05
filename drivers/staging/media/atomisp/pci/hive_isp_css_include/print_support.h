/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __PRINT_SUPPORT_H_INCLUDED__
#define __PRINT_SUPPORT_H_INCLUDED__

#include <linux/stdarg.h>

extern int (*sh_css_printf)(const char *fmt, va_list args);
/* depends on host supplied print function in ia_css_init() */
static inline  __printf(1, 2) void ia_css_print(const char *fmt, ...)
{
	va_list ap;

	if (sh_css_printf) {
		va_start(ap, fmt);
		sh_css_printf(fmt, ap);
		va_end(ap);
	}
}

/* Start adding support for bxt tracing functions for poc. From
 * bxt_sandbox/support/print_support.h. */
/* TODO: support these macros in userspace. */
#define PWARN(format, ...) ia_css_print("warning: ", ##__VA_ARGS__)
#define PRINT(format, ...) ia_css_print(format, ##__VA_ARGS__)
#define PERROR(format, ...) ia_css_print("error: " format, ##__VA_ARGS__)
#define PDEBUG(format, ...) ia_css_print("debug: " format, ##__VA_ARGS__)

#endif /* __PRINT_SUPPORT_H_INCLUDED__ */
