/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __KERN_H__
#define __KERN_H__

/* These are all user-mode things which are convenient to call directly
 * from kernel code and for which writing a wrapper is too much of a pain.
 * The regular include files can't be included because this file is included
 * only into kernel code, and user-space includes conflict with kernel
 * includes.
 */

extern int errno;

extern int clone(int (*proc)(void *), void *sp, int flags, void *data);
extern int sleep(int);
extern int printf(const char *fmt, ...);
extern char *strerror(int errnum);
extern char *ptsname(int __fd);
extern int munmap(void *, int);
extern void *sbrk(int increment);
extern void *malloc(int size);
extern void perror(char *err);
extern int kill(int pid, int sig);
extern int getuid(void);
extern int getgid(void);
extern int pause(void);
extern int write(int, const void *, int);
extern int exit(int);
extern int close(int);
extern int read(unsigned int, char *, int);
extern int pipe(int *);
extern int sched_yield(void);
extern int ptrace(int op, int pid, long addr, long data);

#endif

