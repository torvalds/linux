/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Quadword loads and stores
 * for use in instruction emulation.
 *
 * Copyright 2017 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */

#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/ppc-opcode.h>
#include <asm/reg.h>
#include <asm/asm-offsets.h>
#include <linux/errno.h>

/* do_lq(unsigned long ea, unsigned long *regs) */
_GLOBAL(do_lq)
1:	lq	r6, 0(r3)
	std	r6, 0(r4)
	std	r7, 8(r4)
	li	r3, 0
	blr
2:	li	r3, -EFAULT
	blr
	EX_TABLE(1b, 2b)

/* do_stq(unsigned long ea, unsigned long val0, unsigned long val1) */
_GLOBAL(do_stq)
1:	stq	r4, 0(r3)
	li	r3, 0
	blr
2:	li	r3, -EFAULT
	blr
	EX_TABLE(1b, 2b)

/* do_lqarx(unsigned long ea, unsigned long *regs) */
_GLOBAL(do_lqarx)
1:	PPC_LQARX(6, 0, 3, 0)
	std	r6, 0(r4)
	std	r7, 8(r4)
	li	r3, 0
	blr
2:	li	r3, -EFAULT
	blr
	EX_TABLE(1b, 2b)

/* do_stqcx(unsigned long ea, unsigned long val0, unsigned long val1,
	    unsigned int *crp) */

_GLOBAL(do_stqcx)
1:	PPC_STQCX(4, 0, 3)
	mfcr	r5
	stw	r5, 0(r6)
	li	r3, 0
	blr
2:	li	r3, -EFAULT
	blr
	EX_TABLE(1b, 2b)
