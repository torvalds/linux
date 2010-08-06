/*
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BLACKFIN_STRING_H_
#define _BLACKFIN_STRING_H_

#include <linux/types.h>

#ifdef __KERNEL__		/* only set these up for kernel code */

#define __HAVE_ARCH_STRCPY
extern char *strcpy(char *dest, const char *src);

#define __HAVE_ARCH_STRNCPY
extern char *strncpy(char *dest, const char *src, size_t n);

#define __HAVE_ARCH_STRCMP
extern int strcmp(const char *cs, const char *ct);

#define __HAVE_ARCH_STRNCMP
extern int strncmp(const char *cs, const char *ct, size_t count);

#define __HAVE_ARCH_MEMSET
extern void *memset(void *s, int c, size_t count);
#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *d, const void *s, size_t count);
#define __HAVE_ARCH_MEMCMP
extern int memcmp(const void *, const void *, __kernel_size_t);
#define	__HAVE_ARCH_MEMCHR
extern void *memchr(const void *s, int c, size_t n);
#define	__HAVE_ARCH_MEMMOVE
extern void *memmove(void *dest, const void *src, size_t count);

#endif /*__KERNEL__*/
#endif				/* _BLACKFIN_STRING_H_ */
