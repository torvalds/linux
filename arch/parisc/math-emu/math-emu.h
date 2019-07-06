/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *
 * Floating-point emulation code
 *  Copyright (C) 2001 Hewlett-Packard (Paul Bame) <bame@debian.org>
 */
#ifndef _PARISC_MATH_EMU_H
#define _PARISC_MATH_EMU_H

#include <asm/ptrace.h>
extern int handle_fpe(struct pt_regs *regs);

#endif
