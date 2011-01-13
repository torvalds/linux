/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI ARCS firmware interface library for the Linux kernel.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 2001, 2002 Ralf Baechle (ralf@gnu.org)
 */
#ifndef _ASM_SGIALIB_H
#define _ASM_SGIALIB_H

#include <asm/sgiarcs.h>

extern struct linux_romvec *romvec;
extern int prom_argc;

extern LONG *_prom_argv, *_prom_envp;

/* A 32-bit ARC PROM pass arguments and environment as 32-bit pointer.
   These macros take care of sign extension.  */
#define prom_argv(index) ((char *) (long) _prom_argv[(index)])
#define prom_argc(index) ((char *) (long) _prom_argc[(index)])

extern int prom_flags;

#define PROM_FLAG_ARCS			1
#define PROM_FLAG_USE_AS_CONSOLE	2
#define PROM_FLAG_DONT_FREE_TEMP	4

/* Simple char-by-char console I/O. */
extern void prom_putchar(char c);
extern char prom_getchar(void);

/* Get next memory descriptor after CURR, returns first descriptor
 * in chain is CURR is NULL.
 */
extern struct linux_mdesc *prom_getmdesc(struct linux_mdesc *curr);
#define PROM_NULL_MDESC   ((struct linux_mdesc *) 0)

/* Called by prom_init to setup the physical memory pmemblock
 * array.
 */
extern void prom_meminit(void);

/* PROM device tree library routines. */
#define PROM_NULL_COMPONENT ((pcomponent *) 0)

/* Get sibling component of THIS. */
extern pcomponent *ArcGetPeer(pcomponent *this);

/* Get child component of THIS. */
extern pcomponent *ArcGetChild(pcomponent *this);

/* This is called at prom_init time to identify the
 * ARC architecture we are running on
 */
extern void prom_identify_arch(void);

/* Environment variable routines. */
extern PCHAR ArcGetEnvironmentVariable(PCHAR name);
extern LONG ArcSetEnvironmentVariable(PCHAR name, PCHAR value);

/* ARCS command line parsing. */
extern void prom_init_cmdline(void);

/* File operations. */
extern LONG ArcRead(ULONG fd, PVOID buf, ULONG num, PULONG cnt);
extern LONG ArcWrite(ULONG fd, PVOID buf, ULONG num, PULONG cnt);

/* Misc. routines. */
extern VOID ArcReboot(VOID) __attribute__((noreturn));
extern VOID ArcEnterInteractiveMode(VOID) __attribute__((noreturn));
extern VOID ArcFlushAllCaches(VOID);
extern DISPLAY_STATUS *ArcGetDisplayStatus(ULONG FileID);

#endif /* _ASM_SGIALIB_H */
