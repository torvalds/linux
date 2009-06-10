/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_UCONTEXT_H
#define _ASM_MICROBLAZE_UCONTEXT_H

#include <asm/sigcontext.h>

struct ucontext {
	unsigned long		uc_flags;
	struct ucontext		*uc_link;
	stack_t			uc_stack;
	struct sigcontext	uc_mcontext;
	sigset_t		uc_sigmask; /* mask last for extensibility */
};

#endif /* _ASM_MICROBLAZE_UCONTEXT_H */
