/*
 * arch/v850/kernel/mach.h -- Machine-dependent functions used by v850 port
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_MACH_H__
#define __V850_MACH_H__

#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/seq_file.h>

#include <asm/ptrace.h>
#include <asm/entry.h>
#include <asm/clinkage.h>

void mach_setup (char **cmdline);
void mach_gettimeofday (struct timespec *tv);
void mach_sched_init (struct irqaction *timer_action);
void mach_get_physical_ram (unsigned long *ram_start, unsigned long *ram_len);
void mach_init_irqs (void);

/* If defined, is called very early in the kernel initialization.  The
   stack pointer is valid, but very little has been initialized (e.g.,
   bss is not zeroed yet) when this is called, so care must taken.  */
void mach_early_init (void);

/* If defined, called after the bootmem allocator has been initialized,
   to allow the platform-dependent code to reserve any areas of RAM that
   the kernel shouldn't touch.  */
void mach_reserve_bootmem (void) __attribute__ ((__weak__));

/* Called with each timer tick, if non-zero.  */
extern void (*mach_tick) (void);

/* The following establishes aliases for various mach_ functions to the
   name by which the rest of the kernel calls them.  These statements
   should only have an effect in the file that defines the actual functions. */
#define MACH_ALIAS(to, from)						      \
   asm (".global " macrology_stringify (C_SYMBOL_NAME (to)) ";"		      \
	macrology_stringify (C_SYMBOL_NAME (to))			      \
	" = " macrology_stringify (C_SYMBOL_NAME (from)))
/* e.g.: MACH_ALIAS (kernel_name,	arch_spec_name);  */

#endif /* __V850_MACH_H__ */
