/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 */

#ifndef _ASM_NIOS2_REGISTERS_H
#define _ASM_NIOS2_REGISTERS_H

#ifndef __ASSEMBLER__
#include <asm/cpuinfo.h>
#endif

/* control register numbers */
#define CTL_FSTATUS	0
#define CTL_ESTATUS	1
#define CTL_BSTATUS	2
#define CTL_IENABLE	3
#define CTL_IPENDING	4
#define CTL_CPUID	5
#define CTL_RSV1	6
#define CTL_EXCEPTION	7
#define CTL_PTEADDR	8
#define CTL_TLBACC	9
#define CTL_TLBMISC	10
#define CTL_RSV2	11
#define CTL_BADADDR	12
#define CTL_CONFIG	13
#define CTL_MPUBASE	14
#define CTL_MPUACC	15

/* access control registers using GCC builtins */
#define RDCTL(r)	__builtin_rdctl(r)
#define WRCTL(r, v)	__builtin_wrctl(r, v)

/* status register bits */
#define STATUS_PIE	(1 << 0)	/* processor interrupt enable */
#define STATUS_U	(1 << 1)	/* user mode */
#define STATUS_EH	(1 << 2)	/* Exception mode */

/* estatus register bits */
#define ESTATUS_EPIE	(1 << 0)	/* processor interrupt enable */
#define ESTATUS_EU	(1 << 1)	/* user mode */
#define ESTATUS_EH	(1 << 2)	/* Exception mode */

/* tlbmisc register bits */
#define TLBMISC_PID_SHIFT	4
#ifndef __ASSEMBLER__
#define TLBMISC_PID_MASK	((1UL << cpuinfo.tlb_pid_num_bits) - 1)
#endif
#define TLBMISC_WAY_MASK	0xf
#define TLBMISC_WAY_SHIFT	20

#define TLBMISC_PID	(TLBMISC_PID_MASK << TLBMISC_PID_SHIFT)	/* TLB PID */
#define TLBMISC_WE	(1 << 18)	/* TLB write enable */
#define TLBMISC_RD	(1 << 19)	/* TLB read */
#define TLBMISC_WAY	(TLBMISC_WAY_MASK << TLBMISC_WAY_SHIFT) /* TLB way */

#endif /* _ASM_NIOS2_REGISTERS_H */
