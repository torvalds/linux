/*
 * The ARM LDRD and Thumb LDRSB instructions use bit 20/11 (ARM/Thumb)
 * differently than every other instruction, so it is set to 0 (write)
 * even though the instructions are read instructions. This means that
 * during an abort the instructions will be treated as a write and the
 * handler will raise a signal from unwriteable locations if they
 * fault. We have to specifically check for these instructions
 * from the abort handlers to treat them properly.
 *
 */

	.macro	do_thumb_abort
	tst	r3, #PSR_T_BIT
	beq	not_thumb
	ldrh	r3, [r2]			@ Read aborted Thumb instruction
	and	r3, r3, # 0xfe00		@ Mask opcode field
	cmp	r3, # 0x5600			@ Is it ldrsb?
	orreq	r3, r3, #1 << 11		@ Set L-bit if yes
	tst	r3, #1 << 11			@ L = 0 -> write
	orreq	r1, r1, #1 << 11		@ yes.
	mov	pc, lr
not_thumb:
	.endm

/*
 * We check for the following insturction encoding for LDRD.
 *
 * [27:25] == 0
 *   [7:4] == 1101
 *    [20] == 0
 */
 	.macro	do_ldrd_abort
 	tst	r3, #0x0e000000			@ [27:25] == 0
	bne	not_ldrd
	and	r2, r3, #0x000000f0		@ [7:4] == 1101
	cmp	r2, #0x000000d0
	bne	not_ldrd
	tst	r3, #1 << 20			@ [20] == 0
	moveq	pc, lr
not_ldrd:
	.endm

