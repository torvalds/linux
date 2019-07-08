/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vlock.S - simple voting lock implementation for ARM
 *
 * Created by:	Dave Martin, 2012-08-16
 * Copyright:	(C) 2012-2013  Linaro Limited
 *
 * This algorithm is described in more detail in
 * Documentation/arm/vlocks.txt.
 */

#include <linux/linkage.h>
#include "vlock.h"

/* Select different code if voting flags  can fit in a single word. */
#if VLOCK_VOTING_SIZE > 4
#define FEW(x...)
#define MANY(x...) x
#else
#define FEW(x...) x
#define MANY(x...)
#endif

@ voting lock for first-man coordination

.macro voting_begin rbase:req, rcpu:req, rscratch:req
	mov	\rscratch, #1
	strb	\rscratch, [\rbase, \rcpu]
	dmb
.endm

.macro voting_end rbase:req, rcpu:req, rscratch:req
	dmb
	mov	\rscratch, #0
	strb	\rscratch, [\rbase, \rcpu]
	dsb	st
	sev
.endm

/*
 * The vlock structure must reside in Strongly-Ordered or Device memory.
 * This implementation deliberately eliminates most of the barriers which
 * would be required for other memory types, and assumes that independent
 * writes to neighbouring locations within a cacheline do not interfere
 * with one another.
 */

@ r0: lock structure base
@ r1: CPU ID (0-based index within cluster)
ENTRY(vlock_trylock)
	add	r1, r1, #VLOCK_VOTING_OFFSET

	voting_begin	r0, r1, r2

	ldrb	r2, [r0, #VLOCK_OWNER_OFFSET]	@ check whether lock is held
	cmp	r2, #VLOCK_OWNER_NONE
	bne	trylock_fail			@ fail if so

	@ Control dependency implies strb not observable before previous ldrb.

	strb	r1, [r0, #VLOCK_OWNER_OFFSET]	@ submit my vote

	voting_end	r0, r1, r2		@ implies DMB

	@ Wait for the current round of voting to finish:

 MANY(	mov	r3, #VLOCK_VOTING_OFFSET			)
0:
 MANY(	ldr	r2, [r0, r3]					)
 FEW(	ldr	r2, [r0, #VLOCK_VOTING_OFFSET]			)
	cmp	r2, #0
	wfene
	bne	0b
 MANY(	add	r3, r3, #4					)
 MANY(	cmp	r3, #VLOCK_VOTING_OFFSET + VLOCK_VOTING_SIZE	)
 MANY(	bne	0b						)

	@ Check who won:

	dmb
	ldrb	r2, [r0, #VLOCK_OWNER_OFFSET]
	eor	r0, r1, r2			@ zero if I won, else nonzero
	bx	lr

trylock_fail:
	voting_end	r0, r1, r2
	mov	r0, #1				@ nonzero indicates that I lost
	bx	lr
ENDPROC(vlock_trylock)

@ r0: lock structure base
ENTRY(vlock_unlock)
	dmb
	mov	r1, #VLOCK_OWNER_NONE
	strb	r1, [r0, #VLOCK_OWNER_OFFSET]
	dsb	st
	sev
	bx	lr
ENDPROC(vlock_unlock)
