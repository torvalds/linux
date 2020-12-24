/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/linkage.h>
#include <abi/entry.h>

.text

/*
 * int csky_cmpxchg(int oldval, int newval, int *ptr)
 *
 * If *ptr != oldval && return 1,
 * else *ptr = newval return 0.
 */
ENTRY(csky_cmpxchg)
	USPTOKSP

	RD_MEH	a3
	WR_MEH	a3

	mfcr	a3, epc
	addi	a3, TRAP0_SIZE

	subi    sp, 16
	stw     a3, (sp, 0)
	mfcr    a3, epsr
	stw     a3, (sp, 4)
	mfcr	a3, usp
	stw     a3, (sp, 8)

	psrset	ee
#ifdef CONFIG_CPU_HAS_LDSTEX
1:
	ldex	a3, (a2)
	cmpne	a0, a3
	bt16	2f
	mov	a3, a1
	stex	a3, (a2)
	bez	a3, 1b
2:
	sync.is
#else
GLOBAL(csky_cmpxchg_ldw)
	ldw	a3, (a2)
	cmpne	a0, a3
	bt16	3f
GLOBAL(csky_cmpxchg_stw)
	stw	a1, (a2)
3:
#endif
	mvc	a0
	ldw	a3, (sp, 0)
	mtcr	a3, epc
	ldw     a3, (sp, 4)
	mtcr	a3, epsr
	ldw     a3, (sp, 8)
	mtcr	a3, usp
	addi	sp, 16
	KSPTOUSP
	rte
END(csky_cmpxchg)
