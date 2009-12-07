
#if __LINUX_ARM_ARCH__ >= 6 && defined(CONFIG_CPU_32v6K)
	.macro	bitop, instr
	mov	r2, #1
	and	r3, r0, #7		@ Get bit offset
	add	r1, r1, r0, lsr #3	@ Get byte offset
	mov	r3, r2, lsl r3
1:	ldrexb	r2, [r1]
	\instr	r2, r2, r3
	strexb	r0, r2, [r1]
	cmp	r0, #0
	bne	1b
	mov	pc, lr
	.endm

	.macro	testop, instr, store
	and	r3, r0, #7		@ Get bit offset
	mov	r2, #1
	add	r1, r1, r0, lsr #3	@ Get byte offset
	mov	r3, r2, lsl r3		@ create mask
	smp_dmb
1:	ldrexb	r2, [r1]
	ands	r0, r2, r3		@ save old value of bit
	\instr	r2, r2, r3			@ toggle bit
	strexb	ip, r2, [r1]
	cmp	ip, #0
	bne	1b
	smp_dmb
	cmp	r0, #0
	movne	r0, #1
2:	mov	pc, lr
	.endm
#else
	.macro	bitop, instr
	and	r2, r0, #7
	mov	r3, #1
	mov	r3, r3, lsl r2
	save_and_disable_irqs ip
	ldrb	r2, [r1, r0, lsr #3]
	\instr	r2, r2, r3
	strb	r2, [r1, r0, lsr #3]
	restore_irqs ip
	mov	pc, lr
	.endm

/**
 * testop - implement a test_and_xxx_bit operation.
 * @instr: operational instruction
 * @store: store instruction
 *
 * Note: we can trivially conditionalise the store instruction
 * to avoid dirtying the data cache.
 */
	.macro	testop, instr, store
	add	r1, r1, r0, lsr #3
	and	r3, r0, #7
	mov	r0, #1
	save_and_disable_irqs ip
	ldrb	r2, [r1]
	tst	r2, r0, lsl r3
	\instr	r2, r2, r0, lsl r3
	\store	r2, [r1]
	moveq	r0, #0
	restore_irqs ip
	mov	pc, lr
	.endm
#endif
