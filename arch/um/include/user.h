/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __USER_H__
#define __USER_H__

/*
 * The usual definition - copied here because the kernel provides its own,
 * fancier, type-safe, definition.  Using that one would require
 * copying too much infrastructure for my taste, so userspace files
 * get less checking than kernel files.
 */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

extern void panic(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern int printk(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern void schedule(void);
extern int in_aton(char *str);
extern int open_gdb_chan(void);
/* These use size_t, however unsigned long is correct on both i386 and x86_64. */
extern unsigned long strlcpy(char *, const char *, unsigned long);
extern unsigned long strlcat(char *, const char *, unsigned long);

#endif
