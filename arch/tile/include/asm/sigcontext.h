/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_SIGCONTEXT_H
#define _ASM_TILE_SIGCONTEXT_H

/* NOTE: we can't include <linux/ptrace.h> due to #include dependencies. */
#include <asm/ptrace.h>

/* Must track <sys/ucontext.h> */

struct sigcontext {
	struct pt_regs regs;
};

#endif /* _ASM_TILE_SIGCONTEXT_H */
