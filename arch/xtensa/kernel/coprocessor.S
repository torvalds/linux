/*
 * arch/xtensa/kernel/coprocessor.S
 *
 * Xtensa processor configuration-specific table of coprocessor and
 * other custom register layout information.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 - 2007 Tensilica Inc.
 */


#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/processor.h>
#include <asm/coprocessor.h>
#include <asm/thread_info.h>
#include <asm/asm-uaccess.h>
#include <asm/unistd.h>
#include <asm/ptrace.h>
#include <asm/current.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/signal.h>
#include <asm/tlbflush.h>

#if XTENSA_HAVE_COPROCESSORS

/*
 * Macros for lazy context switch. 
 */

#define SAVE_CP_REGS(x)							\
	.align 4;							\
	.Lsave_cp_regs_cp##x:						\
	.if XTENSA_HAVE_COPROCESSOR(x);					\
		xchal_cp##x##_store a2 a4 a5 a6 a7;			\
	.endif;								\
	jx	a0

#define SAVE_CP_REGS_TAB(x)						\
	.if XTENSA_HAVE_COPROCESSOR(x);					\
		.long .Lsave_cp_regs_cp##x - .Lsave_cp_regs_jump_table;	\
	.else;								\
		.long 0;						\
	.endif;								\
	.long THREAD_XTREGS_CP##x


#define LOAD_CP_REGS(x)							\
	.align 4;							\
	.Lload_cp_regs_cp##x:						\
	.if XTENSA_HAVE_COPROCESSOR(x);					\
		xchal_cp##x##_load a2 a4 a5 a6 a7;			\
	.endif;								\
	jx	a0

#define LOAD_CP_REGS_TAB(x)						\
	.if XTENSA_HAVE_COPROCESSOR(x);					\
		.long .Lload_cp_regs_cp##x - .Lload_cp_regs_jump_table; \
	.else;								\
		.long 0;						\
	.endif;								\
	.long THREAD_XTREGS_CP##x

	SAVE_CP_REGS(0)
	SAVE_CP_REGS(1)
	SAVE_CP_REGS(2)
	SAVE_CP_REGS(3)
	SAVE_CP_REGS(4)
	SAVE_CP_REGS(5)
	SAVE_CP_REGS(6)
	SAVE_CP_REGS(7)

	LOAD_CP_REGS(0)
	LOAD_CP_REGS(1)
	LOAD_CP_REGS(2)
	LOAD_CP_REGS(3)
	LOAD_CP_REGS(4)
	LOAD_CP_REGS(5)
	LOAD_CP_REGS(6)
	LOAD_CP_REGS(7)

	.align 4
.Lsave_cp_regs_jump_table:
	SAVE_CP_REGS_TAB(0)
	SAVE_CP_REGS_TAB(1)
	SAVE_CP_REGS_TAB(2)
	SAVE_CP_REGS_TAB(3)
	SAVE_CP_REGS_TAB(4)
	SAVE_CP_REGS_TAB(5)
	SAVE_CP_REGS_TAB(6)
	SAVE_CP_REGS_TAB(7)

.Lload_cp_regs_jump_table:
	LOAD_CP_REGS_TAB(0)
	LOAD_CP_REGS_TAB(1)
	LOAD_CP_REGS_TAB(2)
	LOAD_CP_REGS_TAB(3)
	LOAD_CP_REGS_TAB(4)
	LOAD_CP_REGS_TAB(5)
	LOAD_CP_REGS_TAB(6)
	LOAD_CP_REGS_TAB(7)

/*
 * coprocessor_save(buffer, index) 
 *                    a2      a3
 * coprocessor_load(buffer, index)
 *                    a2      a3
 *
 * Save or load coprocessor registers for coprocessor 'index'. 
 * The register values are saved to or loaded from them 'buffer' address.
 *
 * Note that these functions don't update the coprocessor_owner information!
 *
 */

ENTRY(coprocessor_save)

	entry	a1, 32
	s32i	a0, a1, 0
	movi	a0, .Lsave_cp_regs_jump_table
	addx8	a3, a3, a0
	l32i	a3, a3, 0
	beqz	a3, 1f
	add	a0, a0, a3
	callx0	a0
1:	l32i	a0, a1, 0
	retw

ENDPROC(coprocessor_save)

ENTRY(coprocessor_load)

	entry	a1, 32
	s32i	a0, a1, 0
	movi	a0, .Lload_cp_regs_jump_table
	addx4	a3, a3, a0
	l32i	a3, a3, 0
	beqz	a3, 1f
	add	a0, a0, a3
	callx0	a0
1:	l32i	a0, a1, 0
	retw

ENDPROC(coprocessor_load)

/*
 * coprocessor_flush(struct task_info*, index)
 *                             a2        a3
 * coprocessor_restore(struct task_info*, index)
 *                              a2         a3
 *
 * Save or load coprocessor registers for coprocessor 'index'. 
 * The register values are saved to or loaded from the coprocessor area 
 * inside the task_info structure.
 *
 * Note that these functions don't update the coprocessor_owner information!
 *
 */


ENTRY(coprocessor_flush)

	entry	a1, 32
	s32i	a0, a1, 0
	movi	a0, .Lsave_cp_regs_jump_table
	addx8	a3, a3, a0
	l32i	a4, a3, 4
	l32i	a3, a3, 0
	add	a2, a2, a4
	beqz	a3, 1f
	add	a0, a0, a3
	callx0	a0
1:	l32i	a0, a1, 0
	retw

ENDPROC(coprocessor_flush)

ENTRY(coprocessor_restore)
	entry	a1, 32
	s32i	a0, a1, 0
	movi	a0, .Lload_cp_regs_jump_table
	addx4	a3, a3, a0
	l32i	a4, a3, 4
	l32i	a3, a3, 0
	add	a2, a2, a4
	beqz	a3, 1f
	add	a0, a0, a3
	callx0	a0
1:	l32i	a0, a1, 0
	retw

ENDPROC(coprocessor_restore)

/*
 * Entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	a3
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	dispatch table
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 */

ENTRY(fast_coprocessor_double)

	wsr	a0, excsave1
	movi	a0, unrecoverable_exception
	callx0	a0

ENDPROC(fast_coprocessor_double)

ENTRY(fast_coprocessor)

	/* Save remaining registers a1-a3 and SAR */

	s32i	a3, a2, PT_AREG3
	rsr	a3, sar
	s32i	a1, a2, PT_AREG1
	s32i	a3, a2, PT_SAR
	mov	a1, a2
	rsr	a2, depc
	s32i	a2, a1, PT_AREG2

	/*
	 * The hal macros require up to 4 temporary registers. We use a3..a6.
	 */

	s32i	a4, a1, PT_AREG4
	s32i	a5, a1, PT_AREG5
	s32i	a6, a1, PT_AREG6

	/* Find coprocessor number. Subtract first CP EXCCAUSE from EXCCAUSE */

	rsr	a3, exccause
	addi	a3, a3, -EXCCAUSE_COPROCESSOR0_DISABLED

	/* Set corresponding CPENABLE bit -> (sar:cp-index, a3: 1<<cp-index)*/

	ssl	a3			# SAR: 32 - coprocessor_number
	movi	a2, 1
	rsr	a0, cpenable
	sll	a2, a2
	or	a0, a0, a2
	wsr	a0, cpenable
	rsync

	/* Retrieve previous owner. (a3 still holds CP number) */

	movi	a0, coprocessor_owner	# list of owners
	addx4	a0, a3, a0		# entry for CP
	l32i	a4, a0, 0

	beqz	a4, 1f			# skip 'save' if no previous owner

	/* Disable coprocessor for previous owner. (a2 = 1 << CP number) */

	l32i	a5, a4, THREAD_CPENABLE
	xor	a5, a5, a2		# (1 << cp-id) still in a2
	s32i	a5, a4, THREAD_CPENABLE

	/*
	 * Get context save area and 'call' save routine. 
	 * (a4 still holds previous owner (thread_info), a3 CP number)
	 */

	movi	a5, .Lsave_cp_regs_jump_table
	movi	a0, 2f			# a0: 'return' address
	addx8	a3, a3, a5		# a3: coprocessor number
	l32i	a2, a3, 4		# a2: xtregs offset
	l32i	a3, a3, 0		# a3: jump offset
	add	a2, a2, a4
	add	a4, a3, a5		# a4: address of save routine
	jx	a4

	/* Note that only a0 and a1 were preserved. */

2:	rsr	a3, exccause
	addi	a3, a3, -EXCCAUSE_COPROCESSOR0_DISABLED
	movi	a0, coprocessor_owner
	addx4	a0, a3, a0

	/* Set new 'owner' (a0 points to the CP owner, a3 contains the CP nr) */

1:	GET_THREAD_INFO (a4, a1)
	s32i	a4, a0, 0

	/* Get context save area and 'call' load routine. */

	movi	a5, .Lload_cp_regs_jump_table
	movi	a0, 1f
	addx8	a3, a3, a5
	l32i	a2, a3, 4		# a2: xtregs offset
	l32i	a3, a3, 0		# a3: jump offset
	add	a2, a2, a4
	add	a4, a3, a5
	jx	a4

	/* Restore all registers and return from exception handler. */

1:	l32i	a6, a1, PT_AREG6
	l32i	a5, a1, PT_AREG5
	l32i	a4, a1, PT_AREG4

	l32i	a0, a1, PT_SAR
	l32i	a3, a1, PT_AREG3
	l32i	a2, a1, PT_AREG2
	wsr	a0, sar
	l32i	a0, a1, PT_AREG0
	l32i	a1, a1, PT_AREG1

	rfe

ENDPROC(fast_coprocessor)

	.data

ENTRY(coprocessor_owner)

	.fill XCHAL_CP_MAX, 4, 0

END(coprocessor_owner)

#endif /* XTENSA_HAVE_COPROCESSORS */
