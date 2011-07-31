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

#include <arch/abi.h>

/*
 * struct sigcontext has the same shape as struct pt_regs,
 * but is simplified since we know the fault is from userspace.
 */
struct sigcontext {
	uint_reg_t gregs[53];	/* General-purpose registers.  */
	uint_reg_t tp;		/* Aliases gregs[TREG_TP].  */
	uint_reg_t sp;		/* Aliases gregs[TREG_SP].  */
	uint_reg_t lr;		/* Aliases gregs[TREG_LR].  */
	uint_reg_t pc;		/* Program counter.  */
	uint_reg_t ics;		/* In Interrupt Critical Section?  */
	uint_reg_t faultnum;	/* Fault number.  */
	uint_reg_t pad[5];
};

#endif /* _ASM_TILE_SIGCONTEXT_H */
