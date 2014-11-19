/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1994 - 2000  Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */

#ifndef __SIGNAL_COMMON_H
#define __SIGNAL_COMMON_H

/* #define DEBUG_SIG */

#ifdef DEBUG_SIG
#  define DEBUGP(fmt, args...) printk("%s: " fmt, __func__, ##args)
#else
#  define DEBUGP(fmt, args...)
#endif

/*
 * Determine which stack to use..
 */
extern void __user *get_sigframe(struct ksignal *ksig, struct pt_regs *regs,
				 size_t frame_size);
/* Check and clear pending FPU exceptions in saved CSR */
extern int fpcsr_pending(unsigned int __user *fpcsr);

/* Make sure we will not lose FPU ownership */
#ifdef CONFIG_PREEMPT
#define lock_fpu_owner()	preempt_disable()
#define unlock_fpu_owner()	preempt_enable()
#else
#define lock_fpu_owner()	pagefault_disable()
#define unlock_fpu_owner()	pagefault_enable()
#endif

#endif	/* __SIGNAL_COMMON_H */
