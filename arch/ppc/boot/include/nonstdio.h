/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This is sort of a catchall for I/O related functions.  Stuff that
 * wouldn't be in 'stdio.h' normally is here, and it's 'nonstdio.h'
 * for a reason.  -- Tom
 */
typedef int FILE;
extern FILE *stdin, *stdout;
#define NULL ((void *)0)
#define EOF (-1)
#define fopen(n, m) NULL
#define fflush(f) 0
#define fclose(f) 0
#define perror(s) printf("%s: no files!\n", (s))

extern int getc(void);
extern int printf(const char *format, ...);
extern int sprintf(char *str, const char *format, ...);
extern int tstc(void);
extern void exit(void);
extern void outb(int port, unsigned char val);
extern void putc(const char c);
extern void puthex(unsigned long val);
extern void puts(const char *);
extern void udelay(long delay);
extern unsigned char inb(int port);
extern void board_isa_init(void);
extern void ISA_init(unsigned long base);
