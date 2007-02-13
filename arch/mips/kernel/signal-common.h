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
#  define DEBUGP(fmt, args...) printk("%s: " fmt, __FUNCTION__ , ##args)
#else
#  define DEBUGP(fmt, args...)
#endif

/*
 * Horribly complicated - with the bloody RM9000 workarounds enabled
 * the signal trampolines is moving to the end of the structure so we can
 * increase the alignment without breaking software compatibility.
 */
#if ICACHE_REFILLS_WORKAROUND_WAR == 0

struct sigframe {
	u32 sf_ass[4];		/* argument save space for o32 */
	u32 sf_code[2];		/* signal trampoline */
	struct sigcontext sf_sc;
	sigset_t sf_mask;
};

#else  /* ICACHE_REFILLS_WORKAROUND_WAR */

struct sigframe {
	u32 sf_ass[4];			/* argument save space for o32 */
	u32 sf_pad[2];
	struct sigcontext sf_sc;	/* hw context */
	sigset_t sf_mask;
	u32 sf_code[8] ____cacheline_aligned;	/* signal trampoline */
};

#endif	/* !ICACHE_REFILLS_WORKAROUND_WAR */

/*
 * Determine which stack to use..
 */
extern void __user *get_sigframe(struct k_sigaction *ka, struct pt_regs *regs,
				 size_t frame_size);
/*
 * install trampoline code to get back from the sig handler
 */
extern int install_sigtramp(unsigned int __user *tramp, unsigned int syscall);

#endif	/* __SIGNAL_COMMON_H */
