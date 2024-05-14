/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 
 *    Copyright (C) 2001 Matthew Wilcox <willy at parisc-linux.org>
 *    Copyright (C) 2003 Carlos O'Donell <carlos at parisc-linux.org>
 */
#ifndef _PARISC64_KERNEL_SIGNAL32_H
#define _PARISC64_KERNEL_SIGNAL32_H

#include <linux/compat.h>

/* 32-bit ucontext as seen from an 64-bit kernel */
struct compat_ucontext {
        compat_uint_t uc_flags;
        compat_uptr_t uc_link;
        compat_stack_t uc_stack;        /* struct compat_sigaltstack (12 bytes)*/
        /* FIXME: Pad out to get uc_mcontext to start at an 8-byte aligned boundary */
        compat_uint_t pad[1];
        struct compat_sigcontext uc_mcontext;
        compat_sigset_t uc_sigmask;     /* mask last for extensibility */
};

/* ELF32 signal handling */

/* In a deft move of uber-hackery, we decide to carry the top half of all
 * 64-bit registers in a non-portable, non-ABI, hidden structure.
 * Userspace can read the hidden structure if it *wants* but is never
 * guaranteed to be in the same place. In fact the uc_sigmask from the
 * ucontext_t structure may push the hidden register file downards
 */
struct compat_regfile {
        /* Upper half of all the 64-bit registers that were truncated
           on a copy to a 32-bit userspace */
        compat_int_t rf_gr[32];
        compat_int_t rf_iasq[2];
        compat_int_t rf_iaoq[2];
        compat_int_t rf_sar;
};

struct compat_rt_sigframe {
	unsigned int tramp[2]; /* holds original return address */
	compat_siginfo_t info;
	struct compat_ucontext uc;
	/* Hidden location of truncated registers, *must* be last. */
	struct compat_regfile regs;
};

/*
 * The 32-bit ABI wants at least 48 bytes for a function call frame:
 * 16 bytes for arg0-arg3, and 32 bytes for magic (the only part of
 * which Linux/parisc uses is sp-20 for the saved return pointer...)
 * Then, the stack pointer must be rounded to a cache line (64 bytes).
 */
#define SIGFRAME32              64
#define FUNCTIONCALLFRAME32     48
#define PARISC_RT_SIGFRAME_SIZE32 (((sizeof(struct compat_rt_sigframe) + FUNCTIONCALLFRAME32) + SIGFRAME32) & -SIGFRAME32)

long restore_sigcontext32(struct compat_sigcontext __user *sc, 
		struct compat_regfile __user *rf,
		struct pt_regs *regs);
long setup_sigcontext32(struct compat_sigcontext __user *sc, 
		struct compat_regfile __user *rf,
		struct pt_regs *regs, int in_syscall);

#endif
