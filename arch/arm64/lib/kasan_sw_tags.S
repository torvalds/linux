/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Google LLC
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Report a tag mismatch detected by tag-based KASAN.
 *
 * A compiler-generated thunk calls this with a non-AAPCS calling
 * convention. Upon entry to this function, registers are as follows:
 *
 * x0:         fault address (see below for restore)
 * x1:         fault description (see below for restore)
 * x2 to x15:  callee-saved
 * x16 to x17: safe to clobber
 * x18 to x30: callee-saved
 * sp:         pre-decremented by 256 bytes (see below for restore)
 *
 * The caller has decremented the SP by 256 bytes, and created a
 * structure on the stack as follows:
 *
 * sp + 0..15:    x0 and x1 to be restored
 * sp + 16..231:  free for use
 * sp + 232..247: x29 and x30 (same as in GPRs)
 * sp + 248..255: free for use
 *
 * Note that this is not a struct pt_regs.
 *
 * To call a regular AAPCS function we must save x2 to x15 (which we can
 * store in the gaps), and create a frame record (for which we can use
 * x29 and x30 spilled by the caller as those match the GPRs).
 *
 * The caller expects x0 and x1 to be restored from the structure, and
 * for the structure to be removed from the stack (i.e. the SP must be
 * incremented by 256 prior to return).
 */
SYM_CODE_START(__hwasan_tag_mismatch)
#ifdef BTI_C
	BTI_C
#endif
	add	x29, sp, #232
	stp	x2, x3, [sp, #8 * 2]
	stp	x4, x5, [sp, #8 * 4]
	stp	x6, x7, [sp, #8 * 6]
	stp	x8, x9, [sp, #8 * 8]
	stp	x10, x11, [sp, #8 * 10]
	stp	x12, x13, [sp, #8 * 12]
	stp	x14, x15, [sp, #8 * 14]
#ifndef CONFIG_SHADOW_CALL_STACK
	str	x18, [sp, #8 * 18]
#endif

	mov	x2, x30
	bl	kasan_tag_mismatch

	ldp	x0, x1, [sp]
	ldp	x2, x3, [sp, #8 * 2]
	ldp	x4, x5, [sp, #8 * 4]
	ldp	x6, x7, [sp, #8 * 6]
	ldp	x8, x9, [sp, #8 * 8]
	ldp	x10, x11, [sp, #8 * 10]
	ldp	x12, x13, [sp, #8 * 12]
	ldp	x14, x15, [sp, #8 * 14]
#ifndef CONFIG_SHADOW_CALL_STACK
	ldr	x18, [sp, #8 * 18]
#endif
	ldp	x29, x30, [sp, #8 * 29]

	/* remove the structure from the stack */
	add	sp, sp, #256
	ret
SYM_CODE_END(__hwasan_tag_mismatch)
EXPORT_SYMBOL(__hwasan_tag_mismatch)
