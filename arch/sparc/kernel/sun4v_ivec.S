/* SPDX-License-Identifier: GPL-2.0 */
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

	/* Get &trap_block[smp_processor_id()] into %g4.  */
	ldxa	[%g0] ASI_SCRATCHPAD, %g4
	sub	%g4, TRAP_PER_CPU_FAULT_INFO, %g4

	/* Get smp_processor_id() into %g3 */
	sethi	%hi(trap_block), %g5
	or	%g5, %lo(trap_block), %g5
	sub	%g4, %g5, %g3
	srlx	%g3, TRAP_BLOCK_SZ_SHIFT, %g3

	/* Increment cpu_mondo_counter[smp_processor_id()] */
	sethi	%hi(cpu_mondo_counter), %g5
	or	%g5, %lo(cpu_mondo_counter), %g5
	sllx	%g3, 3, %g3
	add	%g5, %g3, %g5
	ldx	[%g5], %g3
	add	%g3, 1, %g3
	stx	%g3, [%g5]

	/* Get CPU mondo queue base phys address into %g7.  */
	ldx	[%g4 + TRAP_PER_CPU_CPU_MONDO_PA], %g7

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
	lduw	[%g4 + TRAP_PER_CPU_CPU_MONDO_QMASK], %g4
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

	/* Get &trap_block[smp_processor_id()] into %g4.  */
	ldxa	[%g0] ASI_SCRATCHPAD, %g4
	sub	%g4, TRAP_PER_CPU_FAULT_INFO, %g4

	/* Get DEV mondo queue base phys address into %g5.  */
	ldx	[%g4 + TRAP_PER_CPU_DEV_MONDO_PA], %g5

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
	lduw	[%g4 + TRAP_PER_CPU_DEV_MONDO_QMASK], %g4
	and	%g2, %g4, %g2

	mov	INTRQ_DEVICE_MONDO_HEAD, %g4
	stxa	%g2, [%g4] ASI_QUEUE
	membar	#Sync

	TRAP_LOAD_IRQ_WORK_PA(%g1, %g4)

	/* For VIRQs, cookie is encoded as ~bucket_phys_addr  */
	brlz,pt %g3, 1f
	 xnor	%g3, %g0, %g4

	/* Get __pa(&ivector_table[IVEC]) into %g4.  */
	sethi	%hi(ivector_table_pa), %g4
	ldx	[%g4 + %lo(ivector_table_pa)], %g4
	sllx	%g3, 4, %g3
	add	%g4, %g3, %g4

1:	ldx	[%g1], %g2
	stxa	%g2, [%g4] ASI_PHYS_USE_EC
	stx	%g4, [%g1]

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

	lduw	[%g3 + TRAP_PER_CPU_RESUM_QMASK], %g4

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
	and	%g2, %g4, %g2

	mov	INTRQ_RESUM_MONDO_HEAD, %g4
	stxa	%g2, [%g4] ASI_QUEUE
	membar	#Sync

	/* Disable interrupts and save register state so we can call
	 * C code.  The etrap handling will leave %g4 in %l4 for us
	 * when it's done.
	 */
	rdpr	%pil, %g2
	wrpr	%g0, PIL_NORMAL_MAX, %pil
	mov	%g1, %g4
	ba,pt	%xcc, etrap_irq
	 rd	%pc, %g7
#ifdef CONFIG_TRACE_IRQFLAGS
	call		trace_hardirqs_off
	 nop
#endif
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
	wrpr	%g0, PIL_NORMAL_MAX, %pil
	ba,pt	%xcc, etrap_irq
	 rd	%pc, %g7
#ifdef CONFIG_TRACE_IRQFLAGS
	call		trace_hardirqs_off
	 nop
#endif
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

	lduw	[%g3 + TRAP_PER_CPU_NONRESUM_QMASK], %g4

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
	and	%g2, %g4, %g2

	mov	INTRQ_NONRESUM_MONDO_HEAD, %g4
	stxa	%g2, [%g4] ASI_QUEUE
	membar	#Sync

	/* Disable interrupts and save register state so we can call
	 * C code.  The etrap handling will leave %g4 in %l4 for us
	 * when it's done.
	 */
	rdpr	%pil, %g2
	wrpr	%g0, PIL_NORMAL_MAX, %pil
	mov	%g1, %g4
	ba,pt	%xcc, etrap_irq
	 rd	%pc, %g7
#ifdef CONFIG_TRACE_IRQFLAGS
	call		trace_hardirqs_off
	 nop
#endif
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
	wrpr	%g0, PIL_NORMAL_MAX, %pil
	ba,pt	%xcc, etrap_irq
	 rd	%pc, %g7
#ifdef CONFIG_TRACE_IRQFLAGS
	call		trace_hardirqs_off
	 nop
#endif
	call	sun4v_nonresum_overflow
	 add	%sp, PTREGS_OFF, %o0

	ba,pt	%xcc, rtrap_irq
	 nop
