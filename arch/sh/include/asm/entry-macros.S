! entry.S macro define
	
	.macro	cli
	stc	sr, r0
	or	#0xf0, r0
	ldc	r0, sr
	.endm

	.macro	sti
	mov	#0xfffffff0, r11
	extu.b	r11, r11
	not	r11, r11
	stc	sr, r10
	and	r11, r10
#ifdef CONFIG_CPU_HAS_SR_RB
	stc	k_g_imask, r11
	or	r11, r10
#endif
	ldc	r10, sr
	.endm

	.macro	get_current_thread_info, ti, tmp
#ifdef CONFIG_CPU_HAS_SR_RB
	stc	r7_bank, \ti
#else
	mov	#((THREAD_SIZE - 1) >> 10) ^ 0xff, \tmp
	shll8	\tmp
	shll2	\tmp
	mov	r15, \ti
	and	\tmp, \ti
#endif	
	.endm

#ifdef CONFIG_TRACE_IRQFLAGS

	.macro	TRACE_IRQS_ON
	mov.l	r0, @-r15
	mov.l	r1, @-r15
	mov.l	r2, @-r15
	mov.l	r3, @-r15
	mov.l	r4, @-r15
	mov.l	r5, @-r15
	mov.l	r6, @-r15
	mov.l	r7, @-r15

	mov.l   7834f, r0
	jsr	@r0
	 nop

	mov.l	@r15+, r7
	mov.l	@r15+, r6
	mov.l	@r15+, r5
	mov.l	@r15+, r4
	mov.l	@r15+, r3
	mov.l	@r15+, r2
	mov.l	@r15+, r1
	mov.l	@r15+, r0
	mov.l	7834f, r0

	bra	7835f
	 nop
	.balign	4
7834:	.long	trace_hardirqs_on
7835:
	.endm
	.macro	TRACE_IRQS_OFF

	mov.l	r0, @-r15
	mov.l	r1, @-r15
	mov.l	r2, @-r15
	mov.l	r3, @-r15
	mov.l	r4, @-r15
	mov.l	r5, @-r15
	mov.l	r6, @-r15
	mov.l	r7, @-r15

	mov.l	7834f, r0
	jsr	@r0
	 nop

	mov.l	@r15+, r7
	mov.l	@r15+, r6
	mov.l	@r15+, r5
	mov.l	@r15+, r4
	mov.l	@r15+, r3
	mov.l	@r15+, r2
	mov.l	@r15+, r1
	mov.l	@r15+, r0
	mov.l	7834f, r0

	bra	7835f
	 nop
	.balign	4
7834:	.long	trace_hardirqs_off
7835:
	.endm

#else
	.macro	TRACE_IRQS_ON
	.endm

	.macro	TRACE_IRQS_OFF
	.endm
#endif

#if defined(CONFIG_CPU_SH2A) || defined(CONFIG_CPU_SH4)
# define PREF(x)	pref	@x
#else
# define PREF(x)	nop
#endif

	/*
	 * Macro for use within assembly. Because the DWARF unwinder
	 * needs to use the frame register to unwind the stack, we
	 * need to setup r14 with the value of the stack pointer as
	 * the return address is usually on the stack somewhere.
	 */
	.macro	setup_frame_reg
#ifdef CONFIG_DWARF_UNWINDER
	mov	r15, r14
#endif
	.endm
