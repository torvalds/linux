/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/ucontext.h"
 */

#ifndef _ASM_S390_UCONTEXT_H
#define _ASM_S390_UCONTEXT_H

#define UC_EXTENDED	0x00000001

#ifndef __s390x__

struct ucontext_extended {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	_sigregs	  uc_mcontext;
	sigset_t	  uc_sigmask;
	/* Allow for uc_sigmask growth.  Glibc uses a 1024-bit sigset_t.  */
	unsigned char	  __unused[128 - sizeof(sigset_t)];
	unsigned long	  uc_gprs_high[16];
};

#endif

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
