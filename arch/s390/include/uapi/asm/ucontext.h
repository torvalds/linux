/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/ucontext.h"
 */

#ifndef _ASM_S390_UCONTEXT_H
#define _ASM_S390_UCONTEXT_H

#define UC_GPRS_HIGH	1	/* uc_mcontext_ext has valid high gprs */
#define UC_VXRS		2	/* uc_mcontext_ext has valid vector regs */

/*
 * The struct ucontext_extended describes how the registers are stored
 * on a rt signal frame. Please note that the structure is not fixed,
 * if new CPU registers are added to the user state the size of the
 * struct ucontext_extended will increase.
 */
struct ucontext_extended {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	_sigregs	  uc_mcontext;
	sigset_t	  uc_sigmask;
	/* Allow for uc_sigmask growth.  Glibc uses a 1024-bit sigset_t.  */
	unsigned char	  __unused[128 - sizeof(sigset_t)];
	_sigregs_ext	  uc_mcontext_ext;
};

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	_sigregs          uc_mcontext;
	sigset_t	  uc_sigmask;
	/* Allow for uc_sigmask growth.  Glibc uses a 1024-bit sigset_t.  */
	unsigned char	  __unused[128 - sizeof(sigset_t)];
};

#endif /* !_ASM_S390_UCONTEXT_H */
