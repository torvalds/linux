/* sun4v_ivec.S: Sun4v interrupt vector handling.
 *
 * Copyright (C) 2006 <davem@davemloft.net>
 */

#include <asm/cpudata.h>
#include <asm/intr_queue.h>
#include <asm/pil.h>

	.text
	.align	32

sun4v_cpu_mondo:
	/* Head offset in %g2, tail offset in %g4.
	 * If they are the same, no work.
	 */
	mov	INTRQ_CPU_MONDO_HEAD, %g2
	ldxa	[%g2] ASI_QUEUE, %g2
	mov	INTRQ_CPU_MONDO_TAIL, %g4
	ldxa	[%g4] ASI_QUEUE, %g4
	cmp	%g2, %g4
	be,pn	%xcc, sun4v_cpu_mondo_queue_empty
	 nop

	/* Get &trap_block[smp_processor_id()] into %g3.  */
	ldxa	[%g0] ASI_SCRATCHPAD, %g3
	sub	%g3, TRAP_PER_CPU_FAULT_INFO, %g3

	/* Get CPU mondo queue base phys address into %g7.  */
	ldx	[%g3 + TRAP_PER_CPU_CPU_MONDO_PA], %g7

	/* Now get the cross-call arguments and handler PC, same
	 * layout as sun4u:
	 *
	 * 1st 64-bit word: low half is 32-bit PC, put into %g3 and jmpl to it
	 *                  high half is context arg to MMU flushes, into %g5
	 * 2nd 64-bit word: 64-bit arg, load into %g1
	 * 3rd 64-bit word: 64-bit arg, load into %g7
	 */
	ldxa	[%g7 + %g2] ASI_PHYS_USE_EC, %g3
	add	%g2, 0x8, %g2
	srlx	%g3, 32, %g5
	ldxa	[%g7 + %g2] ASI_PHYS_USE_EC, %g1
	add	%g2, 0x8, %g2
	srl	%g3, 0, %g3
	ldxa	[%g7 + %g2] ASI_PHYS_USE_EC, %g7
	add	%g2, 0x40 - 0x8 - 0x8, %g2

	/* Update queue head pointer.  */
	sethi	%hi(8192 - 1), %g4
	or	%g4, %lo(8192 - 1), %g4
	and	%g2, %g4, %g2

	mov	INTRQ_CPU_MONDO_HEAD, %g4
	stxa	%g2, [%g4] ASI_QUEUE
	membar	#Sync

	jmpl	%g3, %g0
	 nop

sun4v_cpu_mondo_queue_empty:
	retry

sun4v_dev_mondo:
	/* Head offset in %g2, tail offset in %g4.  */
	mov	INTRQ_DEVICE_MONDO_HEAD, %g2
	ldxa	[%g2] ASI_QUEUE, %g2
	mov	INTRQ_DEVICE_MONDO_TAIL, %g4
	ldxa	[%g4] ASI_QUEUE, %g4
	cmp	%g2, %g4
	be,pn	%xcc, sun4v_dev_mondo_queue_empty
	 nop

	/* Get &trap_block[smp_processor_id()] into %g3.  */
	ldxa	[%g0] ASI_SCRATCHPAD, %g3
	sub	%g3, TRAP_PER_CPU_FAULT_INFO, %g3

	/* Get DEV mondo queue base phys address into %g5.  */
	ldx	[%g3 + TRAP_PER_CPU_DEV_MONDO_PA], %g5

	/* Load IVEC into %g3.  */
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	add	%g2, 0x40, %g2

	/* XXX There can be a full 64-byte block of data here.
	 * XXX This is how we can get at MSI vector data.
	 * XXX Current we do not capture this, but when we do we'll
	 * XXX need to add a 64-byte storage area in the struct ino_bucket
	 * XXX or the struct irq_desc.
	 */

	/* Update queue head pointer, this frees up some registers.  */
	sethi	%hi(8192 - 1), %g4
	or	%g4, %lo(8192 - 1), %g4
	and	%g2, %g4, %g2

	mov	INTRQ_DEVICE_MONDO_HEAD, %g4
	stxa	%g2, [%g4] ASI_QUEUE
	membar	#Sync

	/* Get &__irq_work[smp_processor_id()] into %g1.  */
	TRAP_LOAD_IRQ_WORK(%g1, %g4)

	/* Get &ivector_table[IVEC] into %g4.  */
	sethi	%hi(ivector_table), %g4
	sllx	%g3, 3, %g3
	or	%g4, %lo(ivector_table), %g4
	add	%g4, %g3, %g4

	/* Insert ivector_table[] entry into __irq_work[] queue.  */
	lduw	[%g1], %g2		/* g2 = irq_work(cpu) */
	stw	%g2, [%g4 + 0x00]	/* bucket->irq_chain = g2 */
	stw	%g4, [%g1]		/* irq_work(cpu) = bucket */

	/* Signal the interrupt by setting (1 << pil) in %softint.  */
	wr	%g0, 1 << PIL_DEVICE_IRQ, %set_softint

sun4v_dev_mondo_queue_empty:
	retry

sun4v_res_mondo:
	/* Head offset in %g2, tail offset in %g4.  */
	mov	INTRQ_RESUM_MONDO_HEAD, %g2
	ldxa	[%g2] ASI_QUEUE, %g2
	mov	INTRQ_RESUM_MONDO_TAIL, %g4
	ldxa	[%g4] ASI_QUEUE, %g4
	cmp	%g2, %g4
	be,pn	%xcc, sun4v_res_mondo_queue_empty
	 nop

	/* Get &trap_block[smp_processor_id()] into %g3.  */
	ldxa	[%g0] ASI_SCRATCHPAD, %g3
	sub	%g3, TRAP_PER_CPU_FAULT_INFO, %g3

	/* Get RES mondo queue base phys address into %g5.  */
	ldx	[%g3 + TRAP_PER_CPU_RESUM_MONDO_PA], %g5

	/* Get RES kernel buffer base phys address into %g7.  */
	ldx	[%g3 + TRAP_PER_CPU_RESUM_KBUF_PA], %g7

	/* If the first word is non-zero, queue is full.  */
	ldxa	[%g7 + %g2] ASI_PHYS_USE_EC, %g1
	brnz,pn	%g1, sun4v_res_mondo_queue_full
	 nop

	/* Remember this entry's offset in %g1.  */
	mov	%g2, %g1

	/* Copy 64-byte queue entry into kernel buffer.  */
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2

	/* Update queue head pointer.  */
	sethi	%hi(8192 - 1), %g4
	or	%g4, %lo(8192 - 1), %g4
	and	%g2, %g4, %g2

	mov	INTRQ_RESUM_MONDO_HEAD, %g4
	stxa	%g2, [%g4] ASI_QUEUE
	membar	#Sync

	/* Disable interrupts and save register state so we can call
	 * C code.  The etrap handling will leave %g4 in %l4 for us
	 * when it's done.
	 */
	rdpr	%pil, %g2
	wrpr	%g0, 15, %pil
	mov	%g1, %g4
	ba,pt	%xcc, etrap_irq
	 rd	%pc, %g7

	/* Log the event.  */
	add	%sp, PTREGS_OFF, %o0
	call	sun4v_resum_error
	 mov	%l4, %o1

	/* Return from trap.  */
	ba,pt	%xcc, rtrap_irq
	 nop

sun4v_res_mondo_queue_empty:
	retry

sun4v_res_mondo_queue_full:
	/* The queue is full, consolidate our damage by setting
	 * the head equal to the tail.  We'll just trap again otherwise.
	 * Call C code to log the event.
	 */
	mov	INTRQ_RESUM_MONDO_HEAD, %g2
	stxa	%g4, [%g2] ASI_QUEUE
	membar	#Sync

	rdpr	%pil, %g2
	wrpr	%g0, 15, %pil
	ba,pt	%xcc, etrap_irq
	 rd	%pc, %g7

	call	sun4v_resum_overflow
	 add	%sp, PTREGS_OFF, %o0

	ba,pt	%xcc, rtrap_irq
	 nop

sun4v_nonres_mondo:
	/* Head offset in %g2, tail offset in %g4.  */
	mov	INTRQ_NONRESUM_MONDO_HEAD, %g2
	ldxa	[%g2] ASI_QUEUE, %g2
	mov	INTRQ_NONRESUM_MONDO_TAIL, %g4
	ldxa	[%g4] ASI_QUEUE, %g4
	cmp	%g2, %g4
	be,pn	%xcc, sun4v_nonres_mondo_queue_empty
	 nop

	/* Get &trap_block[smp_processor_id()] into %g3.  */
	ldxa	[%g0] ASI_SCRATCHPAD, %g3
	sub	%g3, TRAP_PER_CPU_FAULT_INFO, %g3

	/* Get RES mondo queue base phys address into %g5.  */
	ldx	[%g3 + TRAP_PER_CPU_NONRESUM_MONDO_PA], %g5

	/* Get RES kernel buffer base phys address into %g7.  */
	ldx	[%g3 + TRAP_PER_CPU_NONRESUM_KBUF_PA], %g7

	/* If the first word is non-zero, queue is full.  */
	ldxa	[%g7 + %g2] ASI_PHYS_USE_EC, %g1
	brnz,pn	%g1, sun4v_nonres_mondo_queue_full
	 nop

	/* Remember this entry's offset in %g1.  */
	mov	%g2, %g1

	/* Copy 64-byte queue entry into kernel buffer.  */
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2
	ldxa	[%g5 + %g2] ASI_PHYS_USE_EC, %g3
	stxa	%g3, [%g7 + %g2] ASI_PHYS_USE_EC
	add	%g2, 0x08, %g2

	/* Update queue head pointer.  */
	sethi	%hi(8192 - 1), %g4
	or	%g4, %lo(8192 - 1), %g4
	and	%g2, %g4, %g2

	mov	INTRQ_NONRESUM_MONDO_HEAD, %g4
	stxa	%g2, [%g4] ASI_QUEUE
	membar	#Sync

	/* Disable interrupts and save register state so we can call
	 * C code.  The etrap handling will leave %g4 in %l4 for us
	 * when it's done.
	 */
	rdpr	%pil, %g2
	wrpr	%g0, 15, %pil
	mov	%g1, %g4
	ba,pt	%xcc, etrap_irq
	 rd	%pc, %g7

	/* Log the event.  */
	add	%sp, PTREGS_OFF, %o0
	call	sun4v_nonresum_error
	 mov	%l4, %o1

	/* Return from trap.  */
	ba,pt	%xcc, rtrap_irq
	 nop

sun4v_nonres_mondo_queue_empty:
	retry

sun4v_nonres_mondo_queue_full:
	/* The queue is full, consolidate our damage by setting
	 * the head equal to the tail.  We'll just trap again otherwise.
	 * Call C code to log the event.
	 */
	mov	INTRQ_NONRESUM_MONDO_HEAD, %g2
	stxa	%g4, [%g2] ASI_QUEUE
	membar	#Sync

	rdpr	%pil, %g2
	wrpr	%g0, 15, %pil
	ba,pt	%xcc, etrap_irq
	 rd	%pc, %g7

	call	sun4v_nonresum_overflow
	 add	%sp, PTREGS_OFF, %o0

	ba,pt	%xcc, rtrap_irq
	 nop
