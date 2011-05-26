/*
 * linux/arch/unicore32/include/asm/fpstate.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __UNICORE_FPSTATE_H__
#define __UNICORE_FPSTATE_H__

#ifndef __ASSEMBLY__

#define FP_REGS_NUMBER		33

struct fp_state {
	unsigned int regs[FP_REGS_NUMBER];
} __attribute__((aligned(8)));

#endif

#endif
