#include <linux/linkage.h>
#include <asm/assembler.h>
/*
 * Function: v4t_late_abort
 *
 * Params  : r2 = address of aborted instruction
 *         : r3 = saved SPSR
 *
 * Returns : r0 = address of abort
 *	   : r1 = FSR, bit 11 = write
 *	   : r2-r8 = corrupted
 *	   : r9 = preserved
 *	   : sp = pointer to registers
 *
 * Purpose : obtain information about current aborted instruction.
 * Note: we read user space.  This means we might cause a data
 * abort here if the I-TLB and D-TLB aren't seeing the same
 * picture.  Unfortunately, this does happen.  We live with it.
 */
ENTRY(v4t_late_abort)
	tst	r3, #PSR_T_BIT			@ check for thumb mode
#ifdef CONFIG_CPU_CP15_MMU
	mrc	p15, 0, r1, c5, c0, 0		@ get FSR
	mrc	p15, 0, r0, c6, c0, 0		@ get FAR
	bic	r1, r1, #1 << 11 | 1 << 10	@ clear bits 11 and 10 of FSR
#else
	mov	r0, #0				@ clear r0, r1 (no FSR/FAR)
	mov	r1, #0
#endif
	bne	.data_thumb_abort
	ldr	r8, [r2]			@ read arm instruction
	tst	r8, #1 << 20			@ L = 1 -> write?
	orreq	r1, r1, #1 << 11		@ yes.
	and	r7, r8, #15 << 24
	add	pc, pc, r7, lsr #22		@ Now branch to the relevant processing routine
	nop

/* 0 */	b	.data_arm_lateldrhpost		@ ldrh	rd, [rn], #m/rm
/* 1 */	b	.data_arm_lateldrhpre		@ ldrh	rd, [rn, #m/rm]
/* 2 */	b	.data_unknown
/* 3 */	b	.data_unknown
/* 4 */	b	.data_arm_lateldrpostconst	@ ldr	rd, [rn], #m
/* 5 */	b	.data_arm_lateldrpreconst	@ ldr	rd, [rn, #m] 
/* 6 */	b	.data_arm_lateldrpostreg	@ ldr	rd, [rn], rm
/* 7 */	b	.data_arm_lateldrprereg		@ ldr	rd, [rn, rm]
/* 8 */	b	.data_arm_ldmstm		@ ldm*a	rn, <rlist>
/* 9 */	b	.data_arm_ldmstm		@ ldm*b	rn, <rlist>
/* a */	b	.data_unknown
/* b */	b	.data_unknown
/* c */	mov	pc, lr				@ ldc	rd, [rn], #m	@ Same as ldr	rd, [rn], #m
/* d */	mov	pc, lr				@ ldc	rd, [rn, #m]
/* e */	b	.data_unknown
/* f */
.data_unknown:	@ Part of jumptable
	mov	r0, r2
	mov	r1, r8
	mov	r2, sp
	bl	baddataabort
	b	ret_from_exception

.data_arm_ldmstm:
	tst	r8, #1 << 21			@ check writeback bit
	moveq	pc, lr				@ no writeback -> no fixup
	mov	r7, #0x11
	orr	r7, r7, #0x1100
	and	r6, r8, r7
	and	r2, r8, r7, lsl #1
	add	r6, r6, r2, lsr #1
	and	r2, r8, r7, lsl #2
	add	r6, r6, r2, lsr #2
	and	r2, r8, r7, lsl #3
	add	r6, r6, r2, lsr #3
	add	r6, r6, r6, lsr #8
	add	r6, r6, r6, lsr #4
	and	r6, r6, #15			@ r6 = no. of registers to transfer.
	and	r5, r8, #15 << 16		@ Extract 'n' from instruction
	ldr	r7, [sp, r5, lsr #14]		@ Get register 'Rn'
	tst	r8, #1 << 23			@ Check U bit
	subne	r7, r7, r6, lsl #2		@ Undo increment
	addeq	r7, r7, r6, lsl #2		@ Undo decrement
	str	r7, [sp, r5, lsr #14]		@ Put register 'Rn'
	mov	pc, lr

.data_arm_lateldrhpre:
	tst	r8, #1 << 21			@ Check writeback bit
	moveq	pc, lr				@ No writeback -> no fixup
.data_arm_lateldrhpost:
	and	r5, r8, #0x00f			@ get Rm / low nibble of immediate value
	tst	r8, #1 << 22			@ if (immediate offset)
	andne	r6, r8, #0xf00			@ { immediate high nibble
	orrne	r6, r5, r6, lsr #4		@   combine nibbles } else
	ldreq	r6, [sp, r5, lsl #2]		@ { load Rm value }
.data_arm_apply_r6_and_rn:
	and	r5, r8, #15 << 16		@ Extract 'n' from instruction
	ldr	r7, [sp, r5, lsr #14]		@ Get register 'Rn'
	tst	r8, #1 << 23			@ Check U bit
	subne	r7, r7, r6			@ Undo incrmenet
	addeq	r7, r7, r6			@ Undo decrement
	str	r7, [sp, r5, lsr #14]		@ Put register 'Rn'
	mov	pc, lr

.data_arm_lateldrpreconst:
	tst	r8, #1 << 21			@ check writeback bit
	moveq	pc, lr				@ no writeback -> no fixup
.data_arm_lateldrpostconst:
	movs	r2, r8, lsl #20			@ Get offset
	moveq	pc, lr				@ zero -> no fixup
	and	r5, r8, #15 << 16		@ Extract 'n' from instruction
	ldr	r7, [sp, r5, lsr #14]		@ Get register 'Rn'
	tst	r8, #1 << 23			@ Check U bit
	subne	r7, r7, r2, lsr #20		@ Undo increment
	addeq	r7, r7, r2, lsr #20		@ Undo decrement
	str	r7, [sp, r5, lsr #14]		@ Put register 'Rn'
	mov	pc, lr

.data_arm_lateldrprereg:
	tst	r8, #1 << 21			@ check writeback bit
	moveq	pc, lr				@ no writeback -> no fixup
.data_arm_lateldrpostreg:
	and	r7, r8, #15			@ Extract 'm' from instruction
	ldr	r6, [sp, r7, lsl #2]		@ Get register 'Rm'
	mov	r5, r8, lsr #7			@ get shift count
	ands	r5, r5, #31
	and	r7, r8, #0x70			@ get shift type
	orreq	r7, r7, #8			@ shift count = 0
	add	pc, pc, r7
	nop

	mov	r6, r6, lsl r5			@ 0: LSL #!0
	b	.data_arm_apply_r6_and_rn
	b	.data_arm_apply_r6_and_rn	@ 1: LSL #0
	nop
	b	.data_unknown			@ 2: MUL?
	nop
	b	.data_unknown			@ 3: MUL?
	nop
	mov	r6, r6, lsr r5			@ 4: LSR #!0
	b	.data_arm_apply_r6_and_rn
	mov	r6, r6, lsr #32			@ 5: LSR #32
	b	.data_arm_apply_r6_and_rn
	b	.data_unknown			@ 6: MUL?
	nop
	b	.data_unknown			@ 7: MUL?
	nop
	mov	r6, r6, asr r5			@ 8: ASR #!0
	b	.data_arm_apply_r6_and_rn
	mov	r6, r6, asr #32			@ 9: ASR #32
	b	.data_arm_apply_r6_and_rn
	b	.data_unknown			@ A: MUL?
	nop
	b	.data_unknown			@ B: MUL?
	nop
	mov	r6, r6, ror r5			@ C: ROR #!0
	b	.data_arm_apply_r6_and_rn
	mov	r6, r6, rrx			@ D: RRX
	b	.data_arm_apply_r6_and_rn
	b	.data_unknown			@ E: MUL?
	nop
	b	.data_unknown			@ F: MUL?

.data_thumb_abort:
	ldrh	r8, [r2]			@ read instruction
	tst	r8, #1 << 11			@ L = 1 -> write?
	orreq	r1, r1, #1 << 8			@ yes
	and	r7, r8, #15 << 12
	add	pc, pc, r7, lsr #10		@ lookup in table
	nop

/* 0 */	b	.data_unknown
/* 1 */	b	.data_unknown
/* 2 */	b	.data_unknown
/* 3 */	b	.data_unknown
/* 4 */	b	.data_unknown
/* 5 */	b	.data_thumb_reg
/* 6 */	mov	pc, lr
/* 7 */	mov	pc, lr
/* 8 */	mov	pc, lr
/* 9 */	mov	pc, lr
/* A */	b	.data_unknown
/* B */	b	.data_thumb_pushpop
/* C */	b	.data_thumb_ldmstm
/* D */	b	.data_unknown
/* E */	b	.data_unknown
/* F */	b	.data_unknown

.data_thumb_reg:
	tst	r8, #1 << 9
	moveq	pc, lr
	tst	r8, #1 << 10			@ If 'S' (signed) bit is set
	movne	r1, #0				@ it must be a load instr
	mov	pc, lr

.data_thumb_pushpop:
	tst	r8, #1 << 10
	beq	.data_unknown
	and	r6, r8, #0x55			@ hweight8(r8) + R bit
	and	r2, r8, #0xaa
	add	r6, r6, r2, lsr #1
	and	r2, r6, #0xcc
	and	r6, r6, #0x33
	add	r6, r6, r2, lsr #2
	movs	r7, r8, lsr #9			@ C = r8 bit 8 (R bit)
	adc	r6, r6, r6, lsr #4		@ high + low nibble + R bit
	and	r6, r6, #15			@ number of regs to transfer
	ldr	r7, [sp, #13 << 2]
	tst	r8, #1 << 11
	addeq	r7, r7, r6, lsl #2		@ increment SP if PUSH
	subne	r7, r7, r6, lsl #2		@ decrement SP if POP
	str	r7, [sp, #13 << 2]
	mov	pc, lr

.data_thumb_ldmstm:
	and	r6, r8, #0x55			@ hweight8(r8)
	and	r2, r8, #0xaa
	add	r6, r6, r2, lsr #1
	and	r2, r6, #0xcc
	and	r6, r6, #0x33
	add	r6, r6, r2, lsr #2
	add	r6, r6, r6, lsr #4
	and	r5, r8, #7 << 8
	ldr	r7, [sp, r5, lsr #6]
	and	r6, r6, #15			@ number of regs to transfer
	sub	r7, r7, r6, lsl #2		@ always decrement
	str	r7, [sp, r5, lsr #6]
	mov	pc, lr
