/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <arch/chip.h>


/*
 * This file shares the implementation of the userspace memcpy and
 * the kernel's memcpy, copy_to_user and copy_from_user.
 */

#include <linux/linkage.h>

/* On TILE64, we wrap these functions via arch/tile/lib/memcpy_tile64.c */
#if !CHIP_HAS_COHERENT_LOCAL_CACHE()
#define memcpy __memcpy_asm
#define __copy_to_user_inatomic __copy_to_user_inatomic_asm
#define __copy_from_user_inatomic __copy_from_user_inatomic_asm
#define __copy_from_user_zeroing __copy_from_user_zeroing_asm
#endif

#define IS_MEMCPY	  0
#define IS_COPY_FROM_USER  1
#define IS_COPY_FROM_USER_ZEROING  2
#define IS_COPY_TO_USER   -1

	.section .text.memcpy_common, "ax"
	.align 64

/* Use this to preface each bundle that can cause an exception so
 * the kernel can clean up properly. The special cleanup code should
 * not use these, since it knows what it is doing.
 */
#define EX \
	.pushsection __ex_table, "a"; \
	.word 9f, memcpy_common_fixup; \
	.popsection; \
	9


/* __copy_from_user_inatomic takes the kernel target address in r0,
 * the user source in r1, and the bytes to copy in r2.
 * It returns the number of uncopiable bytes (hopefully zero) in r0.
 */
ENTRY(__copy_from_user_inatomic)
.type __copy_from_user_inatomic, @function
	FEEDBACK_ENTER_EXPLICIT(__copy_from_user_inatomic, \
	  .text.memcpy_common, \
	  .Lend_memcpy_common - __copy_from_user_inatomic)
	{ movei r29, IS_COPY_FROM_USER; j memcpy_common }
	.size __copy_from_user_inatomic, . - __copy_from_user_inatomic

/* __copy_from_user_zeroing is like __copy_from_user_inatomic, but
 * any uncopiable bytes are zeroed in the target.
 */
ENTRY(__copy_from_user_zeroing)
.type __copy_from_user_zeroing, @function
	FEEDBACK_REENTER(__copy_from_user_inatomic)
	{ movei r29, IS_COPY_FROM_USER_ZEROING; j memcpy_common }
	.size __copy_from_user_zeroing, . - __copy_from_user_zeroing

/* __copy_to_user_inatomic takes the user target address in r0,
 * the kernel source in r1, and the bytes to copy in r2.
 * It returns the number of uncopiable bytes (hopefully zero) in r0.
 */
ENTRY(__copy_to_user_inatomic)
.type __copy_to_user_inatomic, @function
	FEEDBACK_REENTER(__copy_from_user_inatomic)
	{ movei r29, IS_COPY_TO_USER; j memcpy_common }
	.size __copy_to_user_inatomic, . - __copy_to_user_inatomic

ENTRY(memcpy)
.type memcpy, @function
	FEEDBACK_REENTER(__copy_from_user_inatomic)
	{ movei r29, IS_MEMCPY }
	.size memcpy, . - memcpy
	/* Fall through */

	.type memcpy_common, @function
memcpy_common:
	/* On entry, r29 holds one of the IS_* macro values from above. */


	/* r0 is the dest, r1 is the source, r2 is the size. */

	/* Save aside original dest so we can return it at the end. */
	{ sw sp, lr; move r23, r0; or r4, r0, r1 }

	/* Check for an empty size. */
	{ bz r2, .Ldone; andi r4, r4, 3 }

	/* Save aside original values in case of a fault. */
	{ move r24, r1; move r25, r2 }
	move r27, lr

	/* Check for an unaligned source or dest. */
	{ bnz r4, .Lcopy_unaligned_maybe_many; addli r4, r2, -256 }

.Lcheck_aligned_copy_size:
	/* If we are copying < 256 bytes, branch to simple case. */
	{ blzt r4, .Lcopy_8_check; slti_u r8, r2, 8 }

	/* Copying >= 256 bytes, so jump to complex prefetching loop. */
	{ andi r6, r1, 63; j .Lcopy_many }

/*
 *
 * Aligned 4 byte at a time copy loop
 *
 */

.Lcopy_8_loop:
	/* Copy two words at a time to hide load latency. */
EX:	{ lw r3, r1; addi r1, r1, 4; slti_u r8, r2, 16 }
EX:	{ lw r4, r1; addi r1, r1, 4 }
EX:	{ sw r0, r3; addi r0, r0, 4; addi r2, r2, -4 }
EX:	{ sw r0, r4; addi r0, r0, 4; addi r2, r2, -4 }
.Lcopy_8_check:
	{ bzt r8, .Lcopy_8_loop; slti_u r4, r2, 4 }

	/* Copy odd leftover word, if any. */
	{ bnzt r4, .Lcheck_odd_stragglers }
EX:	{ lw r3, r1; addi r1, r1, 4 }
EX:	{ sw r0, r3; addi r0, r0, 4; addi r2, r2, -4 }

.Lcheck_odd_stragglers:
	{ bnz r2, .Lcopy_unaligned_few }

.Ldone:
	/* For memcpy return original dest address, else zero. */
	{ mz r0, r29, r23; jrp lr }


/*
 *
 * Prefetching multiple cache line copy handler (for large transfers).
 *
 */

	/* Copy words until r1 is cache-line-aligned. */
.Lalign_loop:
EX:	{ lw r3, r1; addi r1, r1, 4 }
	{ andi r6, r1, 63 }
EX:	{ sw r0, r3; addi r0, r0, 4; addi r2, r2, -4 }
.Lcopy_many:
	{ bnzt r6, .Lalign_loop; addi r9, r0, 63 }

	{ addi r3, r1, 60; andi r9, r9, -64 }

#if CHIP_HAS_WH64()
	/* No need to prefetch dst, we'll just do the wh64
	 * right before we copy a line.
	 */
#endif

EX:	{ lw r5, r3; addi r3, r3, 64; movei r4, 1 }
	/* Intentionally stall for a few cycles to leave L2 cache alone. */
	{ bnzt zero, .; move r27, lr }
EX:	{ lw r6, r3; addi r3, r3, 64 }
	/* Intentionally stall for a few cycles to leave L2 cache alone. */
	{ bnzt zero, . }
EX:	{ lw r7, r3; addi r3, r3, 64 }
#if !CHIP_HAS_WH64()
	/* Prefetch the dest */
	/* Intentionally stall for a few cycles to leave L2 cache alone. */
	{ bnzt zero, . }
	/* Use a real load to cause a TLB miss if necessary.  We aren't using
	 * r28, so this should be fine.
	 */
EX:	{ lw r28, r9; addi r9, r9, 64 }
	/* Intentionally stall for a few cycles to leave L2 cache alone. */
	{ bnzt zero, . }
	{ prefetch r9; addi r9, r9, 64 }
	/* Intentionally stall for a few cycles to leave L2 cache alone. */
	{ bnzt zero, . }
	{ prefetch r9; addi r9, r9, 64 }
#endif
	/* Intentionally stall for a few cycles to leave L2 cache alone. */
	{ bz zero, .Lbig_loop2 }

	/* On entry to this loop:
	 * - r0 points to the start of dst line 0
	 * - r1 points to start of src line 0
	 * - r2 >= (256 - 60), only the first time the loop trips.
	 * - r3 contains r1 + 128 + 60    [pointer to end of source line 2]
	 *   This is our prefetch address. When we get near the end
	 *   rather than prefetching off the end this is changed to point
	 *   to some "safe" recently loaded address.
	 * - r5 contains *(r1 + 60)       [i.e. last word of source line 0]
	 * - r6 contains *(r1 + 64 + 60)  [i.e. last word of source line 1]
	 * - r9 contains ((r0 + 63) & -64)
	 *     [start of next dst cache line.]
	 */

.Lbig_loop:
	{ jal .Lcopy_line2; add r15, r1, r2 }

.Lbig_loop2:
	/* Copy line 0, first stalling until r5 is ready. */
EX:	{ move r12, r5; lw r16, r1 }
	{ bz r4, .Lcopy_8_check; slti_u r8, r2, 8 }
	/* Prefetch several lines ahead. */
EX:	{ lw r5, r3; addi r3, r3, 64 }
	{ jal .Lcopy_line }

	/* Copy line 1, first stalling until r6 is ready. */
EX:	{ move r12, r6; lw r16, r1 }
	{ bz r4, .Lcopy_8_check; slti_u r8, r2, 8 }
	/* Prefetch several lines ahead. */
EX:	{ lw r6, r3; addi r3, r3, 64 }
	{ jal .Lcopy_line }

	/* Copy line 2, first stalling until r7 is ready. */
EX:	{ move r12, r7; lw r16, r1 }
	{ bz r4, .Lcopy_8_check; slti_u r8, r2, 8 }
	/* Prefetch several lines ahead. */
EX:	{ lw r7, r3; addi r3, r3, 64 }
	/* Use up a caches-busy cycle by jumping back to the top of the
	 * loop. Might as well get it out of the way now.
	 */
	{ j .Lbig_loop }


	/* On entry:
	 * - r0 points to the destination line.
	 * - r1 points to the source line.
	 * - r3 is the next prefetch address.
	 * - r9 holds the last address used for wh64.
	 * - r12 = WORD_15
	 * - r16 = WORD_0.
	 * - r17 == r1 + 16.
	 * - r27 holds saved lr to restore.
	 *
	 * On exit:
	 * - r0 is incremented by 64.
	 * - r1 is incremented by 64, unless that would point to a word
	 *   beyond the end of the source array, in which case it is redirected
	 *   to point to an arbitrary word already in the cache.
	 * - r2 is decremented by 64.
	 * - r3 is unchanged, unless it points to a word beyond the
	 *   end of the source array, in which case it is redirected
	 *   to point to an arbitrary word already in the cache.
	 *   Redirecting is OK since if we are that close to the end
	 *   of the array we will not come back to this subroutine
	 *   and use the contents of the prefetched address.
	 * - r4 is nonzero iff r2 >= 64.
	 * - r9 is incremented by 64, unless it points beyond the
	 *   end of the last full destination cache line, in which
	 *   case it is redirected to a "safe address" that can be
	 *   clobbered (sp - 64)
	 * - lr contains the value in r27.
	 */

/* r26 unused */

.Lcopy_line:
	/* TODO: when r3 goes past the end, we would like to redirect it
	 * to prefetch the last partial cache line (if any) just once, for the
	 * benefit of the final cleanup loop. But we don't want to
	 * prefetch that line more than once, or subsequent prefetches
	 * will go into the RTF. But then .Lbig_loop should unconditionally
	 * branch to top of loop to execute final prefetch, and its
	 * nop should become a conditional branch.
	 */

	/* We need two non-memory cycles here to cover the resources
	 * used by the loads initiated by the caller.
	 */
	{ add r15, r1, r2 }
.Lcopy_line2:
	{ slt_u r13, r3, r15; addi r17, r1, 16 }

	/* NOTE: this will stall for one cycle as L1 is busy. */

	/* Fill second L1D line. */
EX:	{ lw r17, r17; addi r1, r1, 48; mvz r3, r13, r1 } /* r17 = WORD_4 */

#if CHIP_HAS_WH64()
	/* Prepare destination line for writing. */
EX:	{ wh64 r9; addi r9, r9, 64 }
#else
	/* Prefetch dest line */
	{ prefetch r9; addi r9, r9, 64 }
#endif
	/* Load seven words that are L1D hits to cover wh64 L2 usage. */

	/* Load the three remaining words from the last L1D line, which
	 * we know has already filled the L1D.
	 */
EX:	{ lw r4, r1;  addi r1, r1, 4;   addi r20, r1, 16 }   /* r4 = WORD_12 */
EX:	{ lw r8, r1;  addi r1, r1, 4;   slt_u r13, r20, r15 }/* r8 = WORD_13 */
EX:	{ lw r11, r1; addi r1, r1, -52; mvz r20, r13, r1 }  /* r11 = WORD_14 */

	/* Load the three remaining words from the first L1D line, first
	 * stalling until it has filled by "looking at" r16.
	 */
EX:	{ lw r13, r1; addi r1, r1, 4; move zero, r16 }   /* r13 = WORD_1 */
EX:	{ lw r14, r1; addi r1, r1, 4 }                   /* r14 = WORD_2 */
EX:	{ lw r15, r1; addi r1, r1, 8; addi r10, r0, 60 } /* r15 = WORD_3 */

	/* Load second word from the second L1D line, first
	 * stalling until it has filled by "looking at" r17.
	 */
EX:	{ lw r19, r1; addi r1, r1, 4; move zero, r17 }  /* r19 = WORD_5 */

	/* Store last word to the destination line, potentially dirtying it
	 * for the first time, which keeps the L2 busy for two cycles.
	 */
EX:	{ sw r10, r12 }                                 /* store(WORD_15) */

	/* Use two L1D hits to cover the sw L2 access above. */
EX:	{ lw r10, r1; addi r1, r1, 4 }                  /* r10 = WORD_6 */
EX:	{ lw r12, r1; addi r1, r1, 4 }                  /* r12 = WORD_7 */

	/* Fill third L1D line. */
EX:	{ lw r18, r1; addi r1, r1, 4 }                  /* r18 = WORD_8 */

	/* Store first L1D line. */
EX:	{ sw r0, r16; addi r0, r0, 4; add r16, r0, r2 } /* store(WORD_0) */
EX:	{ sw r0, r13; addi r0, r0, 4; andi r16, r16, -64 } /* store(WORD_1) */
EX:	{ sw r0, r14; addi r0, r0, 4; slt_u r16, r9, r16 } /* store(WORD_2) */
#if CHIP_HAS_WH64()
EX:	{ sw r0, r15; addi r0, r0, 4; addi r13, sp, -64 } /* store(WORD_3) */
#else
	/* Back up the r9 to a cache line we are already storing to
	 * if it gets past the end of the dest vector.  Strictly speaking,
	 * we don't need to back up to the start of a cache line, but it's free
	 * and tidy, so why not?
	 */
EX:	{ sw r0, r15; addi r0, r0, 4; andi r13, r0, -64 } /* store(WORD_3) */
#endif
	/* Store second L1D line. */
EX:	{ sw r0, r17; addi r0, r0, 4; mvz r9, r16, r13 }/* store(WORD_4) */
EX:	{ sw r0, r19; addi r0, r0, 4 }                  /* store(WORD_5) */
EX:	{ sw r0, r10; addi r0, r0, 4 }                  /* store(WORD_6) */
EX:	{ sw r0, r12; addi r0, r0, 4 }                  /* store(WORD_7) */

EX:	{ lw r13, r1; addi r1, r1, 4; move zero, r18 }  /* r13 = WORD_9 */
EX:	{ lw r14, r1; addi r1, r1, 4 }                  /* r14 = WORD_10 */
EX:	{ lw r15, r1; move r1, r20   }                  /* r15 = WORD_11 */

	/* Store third L1D line. */
EX:	{ sw r0, r18; addi r0, r0, 4 }                  /* store(WORD_8) */
EX:	{ sw r0, r13; addi r0, r0, 4 }                  /* store(WORD_9) */
EX:	{ sw r0, r14; addi r0, r0, 4 }                  /* store(WORD_10) */
EX:	{ sw r0, r15; addi r0, r0, 4 }                  /* store(WORD_11) */

	/* Store rest of fourth L1D line. */
EX:	{ sw r0, r4;  addi r0, r0, 4 }                  /* store(WORD_12) */
	{
EX:	sw r0, r8                                       /* store(WORD_13) */
	addi r0, r0, 4
	/* Will r2 be > 64 after we subtract 64 below? */
	shri r4, r2, 7
	}
	{
EX:	sw r0, r11                                      /* store(WORD_14) */
	addi r0, r0, 8
	/* Record 64 bytes successfully copied. */
	addi r2, r2, -64
	}

	{ jrp lr; move lr, r27 }

	/* Convey to the backtrace library that the stack frame is size
	 * zero, and the real return address is on the stack rather than
	 * in 'lr'.
	 */
	{ info 8 }

	.align 64
.Lcopy_unaligned_maybe_many:
	/* Skip the setup overhead if we aren't copying many bytes. */
	{ slti_u r8, r2, 20; sub r4, zero, r0 }
	{ bnzt r8, .Lcopy_unaligned_few; andi r4, r4, 3 }
	{ bz r4, .Ldest_is_word_aligned; add r18, r1, r2 }

/*
 *
 * unaligned 4 byte at a time copy handler.
 *
 */

	/* Copy single bytes until r0 == 0 mod 4, so we can store words. */
.Lalign_dest_loop:
EX:	{ lb_u r3, r1; addi r1, r1, 1; addi r4, r4, -1 }
EX:	{ sb r0, r3;   addi r0, r0, 1; addi r2, r2, -1 }
	{ bnzt r4, .Lalign_dest_loop; andi r3, r1, 3 }

	/* If source and dest are now *both* aligned, do an aligned copy. */
	{ bz r3, .Lcheck_aligned_copy_size; addli r4, r2, -256 }

.Ldest_is_word_aligned:

#if CHIP_HAS_DWORD_ALIGN()
EX:	{ andi r8, r0, 63; lwadd_na r6, r1, 4}
	{ slti_u r9, r2, 64; bz r8, .Ldest_is_L2_line_aligned }

	/* This copies unaligned words until either there are fewer
	 * than 4 bytes left to copy, or until the destination pointer
	 * is cache-aligned, whichever comes first.
	 *
	 * On entry:
	 * - r0 is the next store address.
	 * - r1 points 4 bytes past the load address corresponding to r0.
	 * - r2 >= 4
	 * - r6 is the next aligned word loaded.
	 */
.Lcopy_unaligned_src_words:
EX:	{ lwadd_na r7, r1, 4; slti_u r8, r2, 4 + 4 }
	/* stall */
	{ dword_align r6, r7, r1; slti_u r9, r2, 64 + 4 }
EX:	{ swadd r0, r6, 4; addi r2, r2, -4 }
	{ bnz r8, .Lcleanup_unaligned_words; andi r8, r0, 63 }
	{ bnzt r8, .Lcopy_unaligned_src_words; move r6, r7 }

	/* On entry:
	 * - r0 is the next store address.
	 * - r1 points 4 bytes past the load address corresponding to r0.
	 * - r2 >= 4 (# of bytes left to store).
	 * - r6 is the next aligned src word value.
	 * - r9 = (r2 < 64U).
	 * - r18 points one byte past the end of source memory.
	 */
.Ldest_is_L2_line_aligned:

	{
	/* Not a full cache line remains. */
	bnz r9, .Lcleanup_unaligned_words
	move r7, r6
	}

	/* r2 >= 64 */

	/* Kick off two prefetches, but don't go past the end. */
	{ addi r3, r1, 63 - 4; addi r8, r1, 64 + 63 - 4 }
	{ prefetch r3; move r3, r8; slt_u r8, r8, r18 }
	{ mvz r3, r8, r1; addi r8, r3, 64 }
	{ prefetch r3; move r3, r8; slt_u r8, r8, r18 }
	{ mvz r3, r8, r1; movei r17, 0 }

.Lcopy_unaligned_line:
	/* Prefetch another line. */
	{ prefetch r3; addi r15, r1, 60; addi r3, r3, 64 }
	/* Fire off a load of the last word we are about to copy. */
EX:	{ lw_na r15, r15; slt_u r8, r3, r18 }

EX:	{ mvz r3, r8, r1; wh64 r0 }

	/* This loop runs twice.
	 *
	 * On entry:
	 * - r17 is even before the first iteration, and odd before
	 *   the second.  It is incremented inside the loop.  Encountering
	 *   an even value at the end of the loop makes it stop.
	 */
.Lcopy_half_an_unaligned_line:
EX:	{
	/* Stall until the last byte is ready. In the steady state this
	 * guarantees all words to load below will be in the L2 cache, which
	 * avoids shunting the loads to the RTF.
	 */
	move zero, r15
	lwadd_na r7, r1, 16
	}
EX:	{ lwadd_na r11, r1, 12 }
EX:	{ lwadd_na r14, r1, -24 }
EX:	{ lwadd_na r8, r1, 4 }
EX:	{ lwadd_na r9, r1, 4 }
EX:	{
	lwadd_na r10, r1, 8
	/* r16 = (r2 < 64), after we subtract 32 from r2 below. */
	slti_u r16, r2, 64 + 32
	}
EX:	{ lwadd_na r12, r1, 4; addi r17, r17, 1 }
EX:	{ lwadd_na r13, r1, 8; dword_align r6, r7, r1 }
EX:	{ swadd r0, r6,  4; dword_align r7,  r8,  r1 }
EX:	{ swadd r0, r7,  4; dword_align r8,  r9,  r1 }
EX:	{ swadd r0, r8,  4; dword_align r9,  r10, r1 }
EX:	{ swadd r0, r9,  4; dword_align r10, r11, r1 }
EX:	{ swadd r0, r10, 4; dword_align r11, r12, r1 }
EX:	{ swadd r0, r11, 4; dword_align r12, r13, r1 }
EX:	{ swadd r0, r12, 4; dword_align r13, r14, r1 }
EX:	{ swadd r0, r13, 4; addi r2, r2, -32 }
	{ move r6, r14; bbst r17, .Lcopy_half_an_unaligned_line }

	{ bzt r16, .Lcopy_unaligned_line; move r7, r6 }

	/* On entry:
	 * - r0 is the next store address.
	 * - r1 points 4 bytes past the load address corresponding to r0.
	 * - r2 >= 0 (# of bytes left to store).
	 * - r7 is the next aligned src word value.
	 */
.Lcleanup_unaligned_words:
	/* Handle any trailing bytes. */
	{ bz r2, .Lcopy_unaligned_done; slti_u r8, r2, 4 }
	{ bzt r8, .Lcopy_unaligned_src_words; move r6, r7 }

	/* Move r1 back to the point where it corresponds to r0. */
	{ addi r1, r1, -4 }

#else /* !CHIP_HAS_DWORD_ALIGN() */

	/* Compute right/left shift counts and load initial source words. */
	{ andi r5, r1, -4; andi r3, r1, 3 }
EX:	{ lw r6, r5; addi r5, r5, 4; shli r3, r3, 3 }
EX:	{ lw r7, r5; addi r5, r5, 4; sub r4, zero, r3 }

	/* Load and store one word at a time, using shifts and ORs
	 * to correct for the misaligned src.
	 */
.Lcopy_unaligned_src_loop:
	{ shr r6, r6, r3; shl r8, r7, r4 }
EX:	{ lw r7, r5; or r8, r8, r6; move r6, r7 }
EX:	{ sw r0, r8; addi r0, r0, 4; addi r2, r2, -4 }
	{ addi r5, r5, 4; slti_u r8, r2, 8 }
	{ bzt r8, .Lcopy_unaligned_src_loop; addi r1, r1, 4 }

	{ bz r2, .Lcopy_unaligned_done }
#endif /* !CHIP_HAS_DWORD_ALIGN() */

	/* Fall through */

/*
 *
 * 1 byte at a time copy handler.
 *
 */

.Lcopy_unaligned_few:
EX:	{ lb_u r3, r1; addi r1, r1, 1 }
EX:	{ sb r0, r3;   addi r0, r0, 1; addi r2, r2, -1 }
	{ bnzt r2, .Lcopy_unaligned_few }

.Lcopy_unaligned_done:

	/* For memcpy return original dest address, else zero. */
	{ mz r0, r29, r23; jrp lr }

.Lend_memcpy_common:
	.size memcpy_common, .Lend_memcpy_common - memcpy_common

	.section .fixup,"ax"
memcpy_common_fixup:
	.type memcpy_common_fixup, @function

	/* Skip any bytes we already successfully copied.
	 * r2 (num remaining) is correct, but r0 (dst) and r1 (src)
	 * may not be quite right because of unrolling and prefetching.
	 * So we need to recompute their values as the address just
	 * after the last byte we are sure was successfully loaded and
	 * then stored.
	 */

	/* Determine how many bytes we successfully copied. */
	{ sub r3, r25, r2 }

	/* Add this to the original r0 and r1 to get their new values. */
	{ add r0, r23, r3; add r1, r24, r3 }

	{ bzt r29, memcpy_fixup_loop }
	{ blzt r29, copy_to_user_fixup_loop }

copy_from_user_fixup_loop:
	/* Try copying the rest one byte at a time, expecting a load fault. */
.Lcfu:	{ lb_u r3, r1; addi r1, r1, 1 }
	{ sb r0, r3; addi r0, r0, 1; addi r2, r2, -1 }
	{ bnzt r2, copy_from_user_fixup_loop }

.Lcopy_from_user_fixup_zero_remainder:
	{ bbs r29, 2f }  /* low bit set means IS_COPY_FROM_USER */
	/* byte-at-a-time loop faulted, so zero the rest. */
	{ move r3, r2; bz r2, 2f /* should be impossible, but handle it. */ }
1:      { sb r0, zero; addi r0, r0, 1; addi r3, r3, -1 }
	{ bnzt r3, 1b }
2:	move lr, r27
	{ move r0, r2; jrp lr }

copy_to_user_fixup_loop:
	/* Try copying the rest one byte at a time, expecting a store fault. */
	{ lb_u r3, r1; addi r1, r1, 1 }
.Lctu:	{ sb r0, r3; addi r0, r0, 1; addi r2, r2, -1 }
	{ bnzt r2, copy_to_user_fixup_loop }
.Lcopy_to_user_fixup_done:
	move lr, r27
	{ move r0, r2; jrp lr }

memcpy_fixup_loop:
	/* Try copying the rest one byte at a time. We expect a disastrous
	 * fault to happen since we are in fixup code, but let it happen.
	 */
	{ lb_u r3, r1; addi r1, r1, 1 }
	{ sb r0, r3; addi r0, r0, 1; addi r2, r2, -1 }
	{ bnzt r2, memcpy_fixup_loop }
	/* This should be unreachable, we should have faulted again.
	 * But be paranoid and handle it in case some interrupt changed
	 * the TLB or something.
	 */
	move lr, r27
	{ move r0, r23; jrp lr }

	.size memcpy_common_fixup, . - memcpy_common_fixup

	.section __ex_table,"a"
	.word .Lcfu, .Lcopy_from_user_fixup_zero_remainder
	.word .Lctu, .Lcopy_to_user_fixup_done
