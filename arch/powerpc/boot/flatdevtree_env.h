/*
 * This file adds the header file glue so that the shared files
 * flatdevicetree.[ch] can compile and work in the powerpc bootwrapper.
 *
 * strncmp & strchr copied from <file:lib/strings.c>
 * Copyright (C) 1991, 1992  Linus Torvalds
 *
 * Maintained by: Mark A. Greer <mgreer@mvista.com>
 */
#ifndef _PPC_BOOT_FLATDEVTREE_ENV_H_
#define _PPC_BOOT_FLATDEVTREE_ENV_H_

#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "string.h"
#include "stdio.h"
#include "ops.h"

#define be16_to_cpu(x)		(x)
#define cpu_to_be16(x)		(x)
#define be32_to_cpu(x)		(x)
#define cpu_to_be32(x)		(x)
#define be64_to_cpu(x)		(x)
#define cpu_to_be64(x)		(x)

static inline int strncmp(const char *cs, const char *ct, size_t count)
{
	signed char __res = 0;

	while (count) {
		if ((__res = *cs - *ct++) != 0 || !*cs++)
			break;
		count--;
	}
	return __res;
}

static inline char *strchr(const char *s, int c)
{
	for (; *s != (char)c; ++s)
		if (*s == '\0')
			return NULL;
	return (char *)s;
}

#endif /* _PPC_BOOT_FLATDEVTREE_ENV_H_ */
