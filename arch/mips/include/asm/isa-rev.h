/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 MIPS Tech, LLC
 * Author: Matt Redfearn <matt.redfearn@mips.com>
 */

#ifndef __MIPS_ASM_ISA_REV_H__
#define __MIPS_ASM_ISA_REV_H__

/*
 * The ISA revision level. This is 0 for MIPS I to V and N for
 * MIPS{32,64}rN.
 */

/* If the compiler has defined __mips_isa_rev, believe it. */
#ifdef __mips_isa_rev
#define MIPS_ISA_REV __mips_isa_rev
#else
/* The compiler hasn't defined the isa rev so assume it's MIPS I - V (0) */
#define MIPS_ISA_REV 0
#endif


#endif /* __MIPS_ASM_ISA_REV_H__ */
