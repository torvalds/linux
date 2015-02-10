/*
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_UCONTEXT_H
#define _ASM_NIOS2_UCONTEXT_H

typedef int greg_t;
#define NGREG 32
typedef greg_t gregset_t[NGREG];

struct mcontext {
	int version;
	gregset_t gregs;
};

#define MCONTEXT_VERSION 2

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct mcontext	  uc_mcontext;
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

#endif
