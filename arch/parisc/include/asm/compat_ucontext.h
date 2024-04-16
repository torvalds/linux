/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_COMPAT_UCONTEXT_H
#define _ASM_PARISC_COMPAT_UCONTEXT_H

#include <linux/compat.h>

/* 32-bit ucontext as seen from an 64-bit kernel */
struct compat_ucontext {
	compat_uint_t uc_flags;
	compat_uptr_t uc_link;
	compat_stack_t uc_stack;	/* struct compat_sigaltstack (12 bytes)*/	
	/* FIXME: Pad out to get uc_mcontext to start at an 8-byte aligned boundary */
	compat_uint_t pad[1];
	struct compat_sigcontext uc_mcontext;
	compat_sigset_t uc_sigmask;	/* mask last for extensibility */
};

#endif /* !_ASM_PARISC_COMPAT_UCONTEXT_H */
