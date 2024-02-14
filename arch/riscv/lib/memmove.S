/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Michael T. Kloos <michael@michaelkloos.com>
 */

#include <linux/linkage.h>
#include <asm/asm.h>

SYM_FUNC_START(__memmove)
	/*
	 * Returns
	 *   a0 - dest
	 *
	 * Parameters
	 *   a0 - Inclusive first byte of dest
	 *   a1 - Inclusive first byte of src
	 *   a2 - Length of copy n
	 *
	 * Because the return matches the parameter register a0,
	 * we will not clobber or modify that register.
	 *
	 * Note: This currently only works on little-endian.
	 * To port to big-endian, reverse the direction of shifts
	 * in the 2 misaligned fixup copy loops.
	 */

	/* Return if nothing to do */
	beq a0, a1, .Lreturn_from_memmove
	beqz a2, .Lreturn_from_memmove

	/*
	 * Register Uses
	 *      Forward Copy: a1 - Index counter of src
	 *      Reverse Copy: a4 - Index counter of src
	 *      Forward Copy: t3 - Index counter of dest
	 *      Reverse Copy: t4 - Index counter of dest
	 *   Both Copy Modes: t5 - Inclusive first multibyte/aligned of dest
	 *   Both Copy Modes: t6 - Non-Inclusive last multibyte/aligned of dest
	 *   Both Copy Modes: t0 - Link / Temporary for load-store
	 *   Both Copy Modes: t1 - Temporary for load-store
	 *   Both Copy Modes: t2 - Temporary for load-store
	 *   Both Copy Modes: a5 - dest to src alignment offset
	 *   Both Copy Modes: a6 - Shift ammount
	 *   Both Copy Modes: a7 - Inverse Shift ammount
	 *   Both Copy Modes: a2 - Alternate breakpoint for unrolled loops
	 */

	/*
	 * Solve for some register values now.
	 * Byte copy does not need t5 or t6.
	 */
	mv   t3, a0
	add  t4, a0, a2
	add  a4, a1, a2

	/*
	 * Byte copy if copying less than (2 * SZREG) bytes. This can
	 * cause problems with the bulk copy implementation and is
	 * small enough not to bother.
	 */
	andi t0, a2, -(2 * SZREG)
	beqz t0, .Lbyte_copy

	/*
	 * Now solve for t5 and t6.
	 */
	andi t5, t3, -SZREG
	andi t6, t4, -SZREG
	/*
	 * If dest(Register t3) rounded down to the nearest naturally
	 * aligned SZREG address, does not equal dest, then add SZREG
	 * to find the low-bound of SZREG alignment in the dest memory
	 * region.  Note that this could overshoot the dest memory
	 * region if n is less than SZREG.  This is one reason why
	 * we always byte copy if n is less than SZREG.
	 * Otherwise, dest is already naturally aligned to SZREG.
	 */
	beq  t5, t3, 1f
		addi t5, t5, SZREG
	1:

	/*
	 * If the dest and src are co-aligned to SZREG, then there is
	 * no need for the full rigmarole of a full misaligned fixup copy.
	 * Instead, do a simpler co-aligned copy.
	 */
	xor  t0, a0, a1
	andi t1, t0, (SZREG - 1)
	beqz t1, .Lcoaligned_copy
	/* Fall through to misaligned fixup copy */

.Lmisaligned_fixup_copy:
	bltu a1, a0, .Lmisaligned_fixup_copy_reverse

.Lmisaligned_fixup_copy_forward:
	jal  t0, .Lbyte_copy_until_aligned_forward

	andi a5, a1, (SZREG - 1) /* Find the alignment offset of src (a1) */
	slli a6, a5, 3 /* Multiply by 8 to convert that to bits to shift */
	sub  a5, a1, t3 /* Find the difference between src and dest */
	andi a1, a1, -SZREG /* Align the src pointer */
	addi a2, t6, SZREG /* The other breakpoint for the unrolled loop*/

	/*
	 * Compute The Inverse Shift
	 * a7 = XLEN - a6 = XLEN + -a6
	 * 2s complement negation to find the negative: -a6 = ~a6 + 1
	 * Add that to XLEN.  XLEN = SZREG * 8.
	 */
	not  a7, a6
	addi a7, a7, (SZREG * 8 + 1)

	/*
	 * Fix Misalignment Copy Loop - Forward
	 * load_val0 = load_ptr[0];
	 * do {
	 * 	load_val1 = load_ptr[1];
	 * 	store_ptr += 2;
	 * 	store_ptr[0 - 2] = (load_val0 >> {a6}) | (load_val1 << {a7});
	 *
	 * 	if (store_ptr == {a2})
	 * 		break;
	 *
	 * 	load_val0 = load_ptr[2];
	 * 	load_ptr += 2;
	 * 	store_ptr[1 - 2] = (load_val1 >> {a6}) | (load_val0 << {a7});
	 *
	 * } while (store_ptr != store_ptr_end);
	 * store_ptr = store_ptr_end;
	 */

	REG_L t0, (0 * SZREG)(a1)
	1:
	REG_L t1, (1 * SZREG)(a1)
	addi  t3, t3, (2 * SZREG)
	srl   t0, t0, a6
	sll   t2, t1, a7
	or    t2, t0, t2
	REG_S t2, ((0 * SZREG) - (2 * SZREG))(t3)

	beq   t3, a2, 2f

	REG_L t0, (2 * SZREG)(a1)
	addi  a1, a1, (2 * SZREG)
	srl   t1, t1, a6
	sll   t2, t0, a7
	or    t2, t1, t2
	REG_S t2, ((1 * SZREG) - (2 * SZREG))(t3)

	bne   t3, t6, 1b
	2:
	mv    t3, t6 /* Fix the dest pointer in case the loop was broken */

	add  a1, t3, a5 /* Restore the src pointer */
	j .Lbyte_copy_forward /* Copy any remaining bytes */

.Lmisaligned_fixup_copy_reverse:
	jal  t0, .Lbyte_copy_until_aligned_reverse

	andi a5, a4, (SZREG - 1) /* Find the alignment offset of src (a4) */
	slli a6, a5, 3 /* Multiply by 8 to convert that to bits to shift */
	sub  a5, a4, t4 /* Find the difference between src and dest */
	andi a4, a4, -SZREG /* Align the src pointer */
	addi a2, t5, -SZREG /* The other breakpoint for the unrolled loop*/

	/*
	 * Compute The Inverse Shift
	 * a7 = XLEN - a6 = XLEN + -a6
	 * 2s complement negation to find the negative: -a6 = ~a6 + 1
	 * Add that to XLEN.  XLEN = SZREG * 8.
	 */
	not  a7, a6
	addi a7, a7, (SZREG * 8 + 1)

	/*
	 * Fix Misalignment Copy Loop - Reverse
	 * load_val1 = load_ptr[0];
	 * do {
	 * 	load_val0 = load_ptr[-1];
	 * 	store_ptr -= 2;
	 * 	store_ptr[1] = (load_val0 >> {a6}) | (load_val1 << {a7});
	 *
	 * 	if (store_ptr == {a2})
	 * 		break;
	 *
	 * 	load_val1 = load_ptr[-2];
	 * 	load_ptr -= 2;
	 * 	store_ptr[0] = (load_val1 >> {a6}) | (load_val0 << {a7});
	 *
	 * } while (store_ptr != store_ptr_end);
	 * store_ptr = store_ptr_end;
	 */

	REG_L t1, ( 0 * SZREG)(a4)
	1:
	REG_L t0, (-1 * SZREG)(a4)
	addi  t4, t4, (-2 * SZREG)
	sll   t1, t1, a7
	srl   t2, t0, a6
	or    t2, t1, t2
	REG_S t2, ( 1 * SZREG)(t4)

	beq   t4, a2, 2f

	REG_L t1, (-2 * SZREG)(a4)
	addi  a4, a4, (-2 * SZREG)
	sll   t0, t0, a7
	srl   t2, t1, a6
	or    t2, t0, t2
	REG_S t2, ( 0 * SZREG)(t4)

	bne   t4, t5, 1b
	2:
	mv    t4, t5 /* Fix the dest pointer in case the loop was broken */

	add  a4, t4, a5 /* Restore the src pointer */
	j .Lbyte_copy_reverse /* Copy any remaining bytes */

/*
 * Simple copy loops for SZREG co-aligned memory locations.
 * These also make calls to do byte copies for any unaligned
 * data at their terminations.
 */
.Lcoaligned_copy:
	bltu a1, a0, .Lcoaligned_copy_reverse

.Lcoaligned_copy_forward:
	jal t0, .Lbyte_copy_until_aligned_forward

	1:
	REG_L t1, ( 0 * SZREG)(a1)
	addi  a1, a1, SZREG
	addi  t3, t3, SZREG
	REG_S t1, (-1 * SZREG)(t3)
	bne   t3, t6, 1b

	j .Lbyte_copy_forward /* Copy any remaining bytes */

.Lcoaligned_copy_reverse:
	jal t0, .Lbyte_copy_until_aligned_reverse

	1:
	REG_L t1, (-1 * SZREG)(a4)
	addi  a4, a4, -SZREG
	addi  t4, t4, -SZREG
	REG_S t1, ( 0 * SZREG)(t4)
	bne   t4, t5, 1b

	j .Lbyte_copy_reverse /* Copy any remaining bytes */

/*
 * These are basically sub-functions within the function.  They
 * are used to byte copy until the dest pointer is in alignment.
 * At which point, a bulk copy method can be used by the
 * calling code.  These work on the same registers as the bulk
 * copy loops.  Therefore, the register values can be picked
 * up from where they were left and we avoid code duplication
 * without any overhead except the call in and return jumps.
 */
.Lbyte_copy_until_aligned_forward:
	beq  t3, t5, 2f
	1:
	lb   t1,  0(a1)
	addi a1, a1, 1
	addi t3, t3, 1
	sb   t1, -1(t3)
	bne  t3, t5, 1b
	2:
	jalr zero, 0x0(t0) /* Return to multibyte copy loop */

.Lbyte_copy_until_aligned_reverse:
	beq  t4, t6, 2f
	1:
	lb   t1, -1(a4)
	addi a4, a4, -1
	addi t4, t4, -1
	sb   t1,  0(t4)
	bne  t4, t6, 1b
	2:
	jalr zero, 0x0(t0) /* Return to multibyte copy loop */

/*
 * Simple byte copy loops.
 * These will byte copy until they reach the end of data to copy.
 * At that point, they will call to return from memmove.
 */
.Lbyte_copy:
	bltu a1, a0, .Lbyte_copy_reverse

.Lbyte_copy_forward:
	beq  t3, t4, 2f
	1:
	lb   t1,  0(a1)
	addi a1, a1, 1
	addi t3, t3, 1
	sb   t1, -1(t3)
	bne  t3, t4, 1b
	2:
	ret

.Lbyte_copy_reverse:
	beq  t4, t3, 2f
	1:
	lb   t1, -1(a4)
	addi a4, a4, -1
	addi t4, t4, -1
	sb   t1,  0(t4)
	bne  t4, t3, 1b
	2:

.Lreturn_from_memmove:
	ret

SYM_FUNC_END(__memmove)
SYM_FUNC_ALIAS_WEAK(memmove, __memmove)
SYM_FUNC_ALIAS(__pi_memmove, __memmove)
SYM_FUNC_ALIAS(__pi___memmove, __memmove)
