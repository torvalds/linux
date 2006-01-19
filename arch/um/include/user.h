/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __USER_H__
#define __USER_H__

extern void panic(const char *fmt, ...);
extern int printk(const char *fmt, ...);
extern void schedule(void);
extern void *um_kmalloc(int size);
extern void *um_kmalloc_atomic(int size);
extern void kfree(void *ptr);
extern int in_aton(char *str);
extern int open_gdb_chan(void);
/* These use size_t, however unsigned long is correct on both i386 and x86_64. */
extern unsigned long strlcpy(char *, const char *, unsigned long);
extern unsigned long strlcat(char *, const char *, unsigned long);
extern void *um_vmalloc(int size);
extern void *um_vmalloc_atomic(int size);
extern void vfree(void *ptr);

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
