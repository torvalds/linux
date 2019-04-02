/* SPDX-License-Identifier: GPL-2.0 */
/*
 * kde.h:  Defines and definitions for deging the Linux kernel
 *            under various kernel degers.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_KDE_H
#define _SPARC_KDE_H

#include <asm/openprom.h>
#include <asm/vaddrs.h>

/* Breakpoints are enter through trap table entry 126.  So in sparc assembly
 * if you want to drop into the deger you do:
 *
 * t DE_BP_TRAP
 */

#define DE_BP_TRAP     126

#ifndef __ASSEMBLY__
/* The de vector is passed in %o1 at boot time.  It is a pointer to
 * a structure in the degers address space.  Here is its format.
 */

typedef unsigned int (*deger_funct)(void);

struct kernel_de {
	/* First the entry point into the deger.  You jump here
	 * to give control over to the deger.
	 */
	unsigned long kde_entry;
	unsigned long kde_trapme;   /* Figure out later... */
	/* The following is the number of pages that the deger has
	 * taken from to total pool.
	 */
	unsigned long *kde_stolen_pages;
	/* Ok, after you remap yourself and/or change the trap table
	 * from what you were left with at boot time you have to call
	 * this synchronization function so the deger can check out
	 * what you have done.
	 */
	deger_funct teach_deger;
}; /* I think that is it... */

extern struct kernel_de *linux_dbvec;

/* Use this macro in C-code to enter the deger. */
static inline void sp_enter_deger(void)
{
	__asm__ __volatile__("jmpl %0, %%o7\n\t"
			     "nop\n\t" : :
			     "r" (linux_dbvec) : "o7", "memory");
}

#define SP_ENTER_DEGER do { \
	     if((linux_dbvec!=0) && ((*(short *)linux_dbvec)!=-1)) \
	       sp_enter_deger(); \
		       } while(0)

enum die_val {
	DIE_UNUSED,
	DIE_OOPS,
};

#endif /* !(__ASSEMBLY__) */

/* Some nice offset defines for assembler code. */
#define KDE_ENTRY_OFF    0x0
#define KDE_DUNNO_OFF    0x4
#define KDE_DUNNO2_OFF   0x8
#define KDE_TEACH_OFF    0xc

#endif /* !(_SPARC_KDE_H) */
