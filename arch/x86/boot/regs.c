// SPDX-License-Identifier: GPL-2.0-or-later
/* -----------------------------------------------------------------------
 *
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 * ----------------------------------------------------------------------- */

/*
 * Simple helper function for initializing a register set.
 *
 * Note that this sets EFLAGS_CF in the input register set; this
 * makes it easier to catch functions which do nothing but don't
 * explicitly set CF.
 */

#include "boot.h"
#include "string.h"

void initregs(struct biosregs *reg)
{
	memset(reg, 0, sizeof(*reg));
	reg->eflags |= X86_EFLAGS_CF;
	reg->ds = ds();
	reg->es = ds();
	reg->fs = fs();
	reg->gs = gs();
}
