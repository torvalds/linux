/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg: May 2011
 *  -We had half-optimised memset/memcpy, got better versions of those
 *  -Added memcmp, strchr, strcpy, strcmp, strlen
 *
 * Amit Bhor: Codito Technologies 2004
 */

#ifndef _ASM_ARC_STRING_H
#define _ASM_ARC_STRING_H

#include <linux/types.h>

#define __HAVE_ARCH_MEMSET
#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMCMP
#define __HAVE_ARCH_STRCHR
#define __HAVE_ARCH_STRCPY
#define __HAVE_ARCH_STRCMP
#define __HAVE_ARCH_STRLEN

extern void *memset(void *ptr, int, __kernel_size_t);
extern void *memcpy(void *, const void *, __kernel_size_t);
extern void memzero(void *ptr, __kernel_size_t n);
extern int memcmp(const void *, const void *, __kernel_size_t);
extern char *strchr(const char *s, int c);
extern char *strcpy(char *dest, const char *src);
extern int strcmp(const char *cs, const char *ct);
extern __kernel_size_t strlen(const char *);

#endif /* _ASM_ARC_STRING_H */
