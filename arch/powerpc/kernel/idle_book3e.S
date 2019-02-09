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
#include <asm/epapr_hcalls.h>
#include <asm/hw_irq.h>

/* 64-bit version only for now */
#ifdef CONFIG_PPC64

.macro BOOK3E_IDLE name loop
_GLOBAL(\name)
	/* Save LR for later */
	mflr	r0
	std	r0,16(r1)

	/* Hard disable interrupts */
	wrteei	0

	/* Now check if an interrupt came in while we were soft disabled
	 * since we may otherwise lose it (doorbells etc...).
	 */
	lbz	r3,PACAIRQHAPPENED(r13)
	cmpwi	cr0,r3,0
	bne	2f

	/* Now we are going to mark ourselves as soft and hard enabled in
	 * order to be able to take interrupts while asleep. We inform lockdep
	 * of that. We don't actually turn interrupts on just yet tho.
	 */
#ifdef CONFIG_TRACE_IRQFLAGS
	stdu    r1,-128(r1)
	bl	trace_hardirqs_on
	addi    r1,r1,128
#endif
	li	r0,IRQS_ENABLED
	stb	r0,PACAIRQSOFTMASK(r13)
	
	/* Interrupts will make use return to LR, so get something we want
	 * in there
	 */
	bl	1f

	/* And return (interrupts are on) */
	ld	r0,16(r1)
	mtlr	r0
	blr

1:	/* Let's set the _TLF_NAPPING flag so interrupts make us return
	 * to the right spot
	*/
	CURRENT_THREAD_INFO(r11, r1)
	ld	r10,TI_LOCAL_FLAGS(r11)
	ori	r10,r10,_TLF_NAPPING
	std	r10,TI_LOCAL_FLAGS(r11)

	/* We can now re-enable hard interrupts and go to sleep */
	wrteei	1
	\loop

2:
	lbz	r10,PACAIRQHAPPENED(r13)
	ori	r10,r10,PACA_IRQ_HARD_DIS
	stb	r10,PACAIRQHAPPENED(r13)
	blr
.endm

.macro BOOK3E_IDLE_LOOP
1:
	PPC_WAIT(0)
	b	1b
.endm

/* epapr_ev_idle_start below is patched with the proper hcall
   opcodes during kernel initialization */
.macro EPAPR_EV_IDLE_LOOP
idle_loop:
	LOAD_REG_IMMEDIATE(r11, EV_HCALL_TOKEN(EV_IDLE))

.global epapr_ev_idle_start
epapr_ev_idle_start:
	li      r3, -1
	nop
	nop
	nop
	b       idle_loop
.endm

BOOK3E_IDLE epapr_ev_idle EPAPR_EV_IDLE_LOOP

BOOK3E_IDLE book3e_idle BOOK3E_IDLE_LOOP

#endif /* CONFIG_PPC64 */
