/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Macro used to simplify coding multi-line assembler.
 * Some of the bit test macro can simplify down to one line
 * depending on the mask value.
 *
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * All rights reserved.
 */
#ifndef _ASM_NIOS2_ASMMACROS_H
#define _ASM_NIOS2_ASMMACROS_H
/*
 * ANDs reg2 with mask and places the result in reg1.
 *
 * You cannnot use the same register for reg1 & reg2.
 */

.macro ANDI32	reg1, reg2, mask
.if \mask & 0xffff
	.if \mask & 0xffff0000
		movhi	\reg1, %hi(\mask)
		movui	\reg1, %lo(\mask)
		and	\reg1, \reg1, \reg2
	.else
		andi	\reg1, \reg2, %lo(\mask)
	.endif
.else
	andhi	\reg1, \reg2, %hi(\mask)
.endif
.endm

/*
 * ORs reg2 with mask and places the result in reg1.
 *
 * It is safe to use the same register for reg1 & reg2.
 */

.macro ORI32	reg1, reg2, mask
.if \mask & 0xffff
	.if \mask & 0xffff0000
		orhi	\reg1, \reg2, %hi(\mask)
		ori	\reg1, \reg2, %lo(\mask)
	.else
		ori	\reg1, \reg2, %lo(\mask)
	.endif
.else
	orhi	\reg1, \reg2, %hi(\mask)
.endif
.endm

/*
 * XORs reg2 with mask and places the result in reg1.
 *
 * It is safe to use the same register for reg1 & reg2.
 */

.macro XORI32	reg1, reg2, mask
.if \mask & 0xffff
	.if \mask & 0xffff0000
		xorhi	\reg1, \reg2, %hi(\mask)
		xori	\reg1, \reg1, %lo(\mask)
	.else
		xori	\reg1, \reg2, %lo(\mask)
	.endif
.else
	xorhi	\reg1, \reg2, %hi(\mask)
.endif
.endm

/*
 * This is a support macro for BTBZ & BTBNZ.  It checks
 * the bit to make sure it is valid 32 value.
 *
 * It is safe to use the same register for reg1 & reg2.
 */

.macro BT	reg1, reg2, bit
.if \bit > 31
	.err
.else
	.if \bit < 16
		andi	\reg1, \reg2, (1 << \bit)
	.else
		andhi	\reg1, \reg2, (1 << (\bit - 16))
	.endif
.endif
.endm

/*
 * Tests the bit in reg2 and branches to label if the
 * bit is zero.  The result of the bit test is stored in reg1.
 *
 * It is safe to use the same register for reg1 & reg2.
 */

.macro BTBZ	reg1, reg2, bit, label
	BT	\reg1, \reg2, \bit
	beq	\reg1, r0, \label
.endm

/*
 * Tests the bit in reg2 and branches to label if the
 * bit is non-zero.  The result of the bit test is stored in reg1.
 *
 * It is safe to use the same register for reg1 & reg2.
 */

.macro BTBNZ	reg1, reg2, bit, label
	BT	\reg1, \reg2, \bit
	bne	\reg1, r0, \label
.endm

/*
 * Tests the bit in reg2 and then compliments the bit in reg2.
 * The result of the bit test is stored in reg1.
 *
 * It is NOT safe to use the same register for reg1 & reg2.
 */

.macro BTC	reg1, reg2, bit
.if \bit > 31
	.err
.else
	.if \bit < 16
		andi	\reg1, \reg2, (1 << \bit)
		xori	\reg2, \reg2, (1 << \bit)
	.else
		andhi	\reg1, \reg2, (1 << (\bit - 16))
		xorhi	\reg2, \reg2, (1 << (\bit - 16))
	.endif
.endif
.endm

/*
 * Tests the bit in reg2 and then sets the bit in reg2.
 * The result of the bit test is stored in reg1.
 *
 * It is NOT safe to use the same register for reg1 & reg2.
 */

.macro BTS	reg1, reg2, bit
.if \bit > 31
	.err
.else
	.if \bit < 16
		andi	\reg1, \reg2, (1 << \bit)
		ori	\reg2, \reg2, (1 << \bit)
	.else
		andhi	\reg1, \reg2, (1 << (\bit - 16))
		orhi	\reg2, \reg2, (1 << (\bit - 16))
	.endif
.endif
.endm

/*
 * Tests the bit in reg2 and then resets the bit in reg2.
 * The result of the bit test is stored in reg1.
 *
 * It is NOT safe to use the same register for reg1 & reg2.
 */

.macro BTR	reg1, reg2, bit
.if \bit > 31
	.err
.else
	.if \bit < 16
		andi	\reg1, \reg2, (1 << \bit)
		andi	\reg2, \reg2, %lo(~(1 << \bit))
	.else
		andhi	\reg1, \reg2, (1 << (\bit - 16))
		andhi	\reg2, \reg2, %lo(~(1 << (\bit - 16)))
	.endif
.endif
.endm

/*
 * Tests the bit in reg2 and then compliments the bit in reg2.
 * The result of the bit test is stored in reg1.  If the
 * original bit was zero it branches to label.
 *
 * It is NOT safe to use the same register for reg1 & reg2.
 */

.macro BTCBZ	reg1, reg2, bit, label
	BTC	\reg1, \reg2, \bit
	beq	\reg1, r0, \label
.endm

/*
 * Tests the bit in reg2 and then compliments the bit in reg2.
 * The result of the bit test is stored in reg1.  If the
 * original bit was non-zero it branches to label.
 *
 * It is NOT safe to use the same register for reg1 & reg2.
 */

.macro BTCBNZ	reg1, reg2, bit, label
	BTC	\reg1, \reg2, \bit
	bne	\reg1, r0, \label
.endm

/*
 * Tests the bit in reg2 and then sets the bit in reg2.
 * The result of the bit test is stored in reg1.  If the
 * original bit was zero it branches to label.
 *
 * It is NOT safe to use the same register for reg1 & reg2.
 */

.macro BTSBZ	reg1, reg2, bit, label
	BTS	\reg1, \reg2, \bit
	beq	\reg1, r0, \label
.endm

/*
 * Tests the bit in reg2 and then sets the bit in reg2.
 * The result of the bit test is stored in reg1.  If the
 * original bit was non-zero it branches to label.
 *
 * It is NOT safe to use the same register for reg1 & reg2.
 */

.macro BTSBNZ	reg1, reg2, bit, label
	BTS	\reg1, \reg2, \bit
	bne	\reg1, r0, \label
.endm

/*
 * Tests the bit in reg2 and then resets the bit in reg2.
 * The result of the bit test is stored in reg1.  If the
 * original bit was zero it branches to label.
 *
 * It is NOT safe to use the same register for reg1 & reg2.
 */

.macro BTRBZ	reg1, reg2, bit, label
	BTR	\reg1, \reg2, \bit
	bne	\reg1, r0, \label
.endm

/*
 * Tests the bit in reg2 and then resets the bit in reg2.
 * The result of the bit test is stored in reg1.  If the
 * original bit was non-zero it branches to label.
 *
 * It is NOT safe to use the same register for reg1 & reg2.
 */

.macro BTRBNZ	reg1, reg2, bit, label
	BTR	\reg1, \reg2, \bit
	bne	\reg1, r0, \label
.endm

/*
 * Tests the bits in mask against reg2 stores the result in reg1.
 * If the all the bits in the mask are zero it branches to label.
 *
 * It is safe to use the same register for reg1 & reg2.
 */

.macro TSTBZ	reg1, reg2, mask, label
	ANDI32	\reg1, \reg2, \mask
	beq	\reg1, r0, \label
.endm

/*
 * Tests the bits in mask against reg2 stores the result in reg1.
 * If the any of the bits in the mask are 1 it branches to label.
 *
 * It is safe to use the same register for reg1 & reg2.
 */

.macro TSTBNZ	reg1, reg2, mask, label
	ANDI32	\reg1, \reg2, \mask
	bne	\reg1, r0, \label
.endm

/*
 * Pushes reg onto the stack.
 */

.macro PUSH	reg
	addi	sp, sp, -4
	stw	\reg, 0(sp)
.endm

/*
 * Pops the top of the stack into reg.
 */

.macro POP	reg
	ldw	\reg, 0(sp)
	addi	sp, sp, 4
.endm


#endif /* _ASM_NIOS2_ASMMACROS_H */
