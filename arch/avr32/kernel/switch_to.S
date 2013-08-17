/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/sysreg.h>

	.text
	.global	__switch_to
	.type	__switch_to, @function

	/* Switch thread context from "prev" to "next", returning "last"
	 *   r12 :	prev
	 *   r11 :	&prev->thread + 1
	 *   r10 :	&next->thread
	 */
__switch_to:
	stm	--r11, r0,r1,r2,r3,r4,r5,r6,r7,sp,lr
	mfsr	r9, SYSREG_SR
	st.w	--r11, r9
	ld.w	r8, r10++
	/*
	 * schedule() may have been called from a mode with a different
	 * set of registers. Make sure we don't lose anything here.
	 */
	pushm	r10,r12
	mtsr	SYSREG_SR, r8
	frs			/* flush the return stack */
	sub	pc, -2		/* flush the pipeline */
	popm	r10,r12
	ldm	r10++, r0,r1,r2,r3,r4,r5,r6,r7,sp,pc
	.size	__switch_to, . - __switch_to
