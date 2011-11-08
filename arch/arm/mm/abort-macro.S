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

	.macro	do_thumb_abort, fsr, pc, psr, tmp
	tst	\psr, #PSR_T_BIT
	beq	not_thumb
	ldrh	\tmp, [\pc]			@ Read aborted Thumb instruction
	and	\tmp, \tmp, # 0xfe00		@ Mask opcode field
	cmp	\tmp, # 0x5600			@ Is it ldrsb?
	orreq	\tmp, \tmp, #1 << 11		@ Set L-bit if yes
	tst	\tmp, #1 << 11			@ L = 0 -> write
	orreq	\fsr, \fsr, #1 << 11		@ yes.
	b	do_DataAbort
not_thumb:
	.endm

/*
 * We check for the following instruction encoding for LDRD.
 *
 * [27:25] == 000
 *   [7:4] == 1101
 *    [20] == 0
 */
	.macro	do_ldrd_abort, tmp, insn
	tst	\insn, #0x0e100000		@ [27:25,20] == 0
	bne	not_ldrd
	and	\tmp, \insn, #0x000000f0	@ [7:4] == 1101
	cmp	\tmp, #0x000000d0
	beq	do_DataAbort
not_ldrd:
	.endm

