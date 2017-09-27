/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _UAPI__ASM_UCONTEXT_H
#define _UAPI__ASM_UCONTEXT_H

#include <linux/types.h>

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext	 *uc_link;
	stack_t		  uc_stack;
	sigset_t	  uc_sigmask;
	/* glibc uses a 1024-bit sigset_t */
	__u8		  __unused[1024 / 8 - sizeof(sigset_t)];
	/* last for future expansion */
	struct sigcontext uc_mcontext;
};

#endif /* _UAPI__ASM_UCONTEXT_H */
