/*
 * Copyright 2010 IBM Corp, Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *
 * Generic idle routine for Book3E processors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/threads.h>
#include <asm/reg.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ppc-opcode.h>
#include <asm/processor.h>
#include <asm/thread_info.h>

/* 64-bit version only for now */
#ifdef CONFIG_PPC64

_GLOBAL(book3e_idle)
	/* Save LR for later */
	mflr	r0
	std	r0,16(r1)

	/* Hard disable interrupts */
	wrteei	0

	/* Now check if an interrupt came in while we were soft disabled
	 * since we may otherwise lose it (doorbells etc...). We know
	 * that since PACAHARDIRQEN will have been cleared in that case.
	 */
	lbz	r3,PACAHARDIRQEN(r13)
	cmpwi	cr0,r3,0
	beqlr

	/* Now we are going to mark ourselves as soft and hard enables in
	 * order to be able to take interrupts while asleep. We inform lockdep
	 * of that. We don't actually turn interrupts on just yet tho.
	 */
#ifdef CONFIG_TRACE_IRQFLAGS
	stdu    r1,-128(r1)
	bl	.trace_hardirqs_on
#endif
	li	r0,1
	stb	r0,PACASOFTIRQEN(r13)
	stb	r0,PACAHARDIRQEN(r13)
	
	/* Interrupts will make use return to LR, so get something we want
	 * in there
	 */
	bl	1f

	/* Hard disable interrupts again */
	wrteei	0

	/* Mark them off again in the PACA as well */
	li	r0,0
	stb	r0,PACASOFTIRQEN(r13)
	stb	r0,PACAHARDIRQEN(r13)

	/* Tell lockdep about it */
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	.trace_hardirqs_off
	addi    r1,r1,128
#endif
	ld	r0,16(r1)
	mtlr	r0
	blr

1:	/* Let's set the _TLF_NAPPING flag so interrupts make us return
	 * to the right spot
	*/
	clrrdi	r11,r1,THREAD_SHIFT
	ld	r10,TI_LOCAL_FLAGS(r11)
	ori	r10,r10,_TLF_NAPPING
	std	r10,TI_LOCAL_FLAGS(r11)

	/* We can now re-enable hard interrupts and go to sleep */
	wrteei	1
1:	PPC_WAIT(0)
	b	1b

#endif /* CONFIG_PPC64 */
