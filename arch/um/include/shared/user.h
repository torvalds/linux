/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __USER_H__
#define __USER_H__

#include <generated/asm-offsets.h>

/*
 * The usual definition - copied here because the kernel provides its own,
 * fancier, type-safe, definition.  Using that one would require
 * copying too much infrastructure for my taste, so userspace files
 * get less checking than kernel files.
 */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* This is to get size_t and NULL */
#ifndef __UM_HOST__
#include <linux/types.h>
#else
#include <stddef.h>
#include <sys/types.h>
#endif

extern void panic(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

/* Requires preincluding include/linux/kern_levels.h */
#define UM_KERN_EMERG	KERN_EMERG
#define UM_KERN_ALERT	KERN_ALERT
#define UM_KERN_CRIT	KERN_CRIT
#define UM_KERN_ERR	KERN_ERR
#define UM_KERN_WARNING	KERN_WARNING
#define UM_KERN_NOTICE	KERN_NOTICE
#define UM_KERN_INFO	KERN_INFO
#define UM_KERN_DEBUG	KERN_DEBUG
#define UM_KERN_CONT	KERN_CONT

#ifdef UML_CONFIG_PRINTK
#define printk(...) _printk(__VA_ARGS__)
extern int _printk(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern void print_hex_dump(const char *level, const char *prefix_str,
			   int prefix_type, int rowsize, int groupsize,
			   const void *buf, size_t len, _Bool ascii);
#else
static inline int printk(const char *fmt, ...)
{
	return 0;
}
static inline void print_hex_dump(const char *level, const char *prefix_str,
				  int prefix_type, int rowsize, int groupsize,
				  const void *buf, size_t len, _Bool ascii)
{
}
#endif

extern int in_aton(char *str);
extern size_t strlcat(char *, const char *, size_t);
extern size_t sized_strscpy(char *, const char *, size_t);
#define strscpy(dst, src)	sized_strscpy(dst, src, sizeof(dst))

/* Copied from linux/compiler-gcc.h since we can't include it directly */
#define barrier() __asm__ __volatile__("": : :"memory")

#endif
