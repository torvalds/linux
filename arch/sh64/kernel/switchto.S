/*
 * arch/sh64/kernel/switchto.S
 *
 * sh64 context switch
 *
 * Copyright (C) 2004  Richard Curnow
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
*/

	.section .text..SHmedia32,"ax"
	.little

	.balign 32

	.type sh64_switch_to,@function
	.global sh64_switch_to
	.global __sh64_switch_to_end
sh64_switch_to:

/* Incoming args
   r2 - prev
   r3 - &prev->thread
   r4 - next
   r5 - &next->thread

   Outgoing results
   r2 - last (=prev) : this just stays in r2 throughout

   Want to create a full (struct pt_regs) on the stack to allow backtracing
   functions to work.  However, we only need to populate the callee-save
   register slots in this structure; since we're a function our ancestors must
   have themselves preserved all caller saved state in the stack.  This saves
   some wasted effort since we won't need to look at the values.

   In particular, all caller-save registers are immediately available for
   scratch use.

*/

#define FRAME_SIZE (76*8 + 8)

	movi	FRAME_SIZE, r0
	sub.l	r15, r0, r15
	! Do normal-style register save to support backtrace

	st.l	r15,   0, r18	! save link reg
	st.l	r15,   4, r14	! save fp
	add.l	r15, r63, r14	! setup frame pointer

	! hopefully this looks normal to the backtrace now.

	addi.l	r15,   8, r1    ! base of pt_regs
	addi.l	r1,   24, r0    ! base of pt_regs.regs
	addi.l	r0, (63*8), r8	! base of pt_regs.trregs

	/* Note : to be fixed?
	   struct pt_regs is really designed for holding the state on entry
	   to an exception, i.e. pc,sr,regs etc.  However, for the context
	   switch state, some of this is not required.  But the unwinder takes
	   struct pt_regs * as an arg so we have to build this structure
	   to allow unwinding switched tasks in show_state() */

	st.q	r0, ( 9*8), r9
	st.q	r0, (10*8), r10
	st.q	r0, (11*8), r11
	st.q	r0, (12*8), r12
	st.q	r0, (13*8), r13
	st.q	r0, (14*8), r14 ! for unwind, want to look as though we took a trap at
	! the point where the process is left in suspended animation, i.e. current
	! fp here, not the saved one.
	st.q	r0, (16*8), r16

	st.q	r0, (24*8), r24
	st.q	r0, (25*8), r25
	st.q	r0, (26*8), r26
	st.q	r0, (27*8), r27
	st.q	r0, (28*8), r28
	st.q	r0, (29*8), r29
	st.q	r0, (30*8), r30
	st.q	r0, (31*8), r31
	st.q	r0, (32*8), r32
	st.q	r0, (33*8), r33
	st.q	r0, (34*8), r34
	st.q	r0, (35*8), r35

	st.q	r0, (44*8), r44
	st.q	r0, (45*8), r45
	st.q	r0, (46*8), r46
	st.q	r0, (47*8), r47
	st.q	r0, (48*8), r48
	st.q	r0, (49*8), r49
	st.q	r0, (50*8), r50
	st.q	r0, (51*8), r51
	st.q	r0, (52*8), r52
	st.q	r0, (53*8), r53
	st.q	r0, (54*8), r54
	st.q	r0, (55*8), r55
	st.q	r0, (56*8), r56
	st.q	r0, (57*8), r57
	st.q	r0, (58*8), r58
	st.q	r0, (59*8), r59

	! do this early as pta->gettr has no pipeline forwarding (=> 5 cycle latency)
	! Use a local label to avoid creating a symbol that will confuse the !
	! backtrace
	pta	.Lsave_pc, tr0

	gettr	tr5, r45
	gettr	tr6, r46
	gettr	tr7, r47
	st.q	r8, (5*8), r45
	st.q	r8, (6*8), r46
	st.q	r8, (7*8), r47

	! Now switch context
	gettr	tr0, r9
	st.l	r3, 0, r15	! prev->thread.sp
	st.l	r3, 8, r1	! prev->thread.kregs
	st.l	r3, 4, r9	! prev->thread.pc
	st.q	r1, 0, r9	! save prev->thread.pc into pt_regs->pc

	! Load PC for next task (init value or save_pc later)
	ld.l	r5, 4, r18	! next->thread.pc
	! Switch stacks
	ld.l	r5, 0, r15	! next->thread.sp
	ptabs	r18, tr0

	! Update current
	ld.l	r4, 4, r9	! next->thread_info (2nd element of next task_struct)
	putcon	r9, kcr0	! current = next->thread_info

	! go to save_pc for a reschedule, or the initial thread.pc for a new process
	blink	tr0, r63

	! Restore (when we come back to a previously saved task)
.Lsave_pc:
	addi.l	r15, 32, r0	! r0 = next's regs
	addi.l	r0, (63*8), r8	! r8 = next's tr_regs

	ld.q	r8, (5*8), r45
	ld.q	r8, (6*8), r46
	ld.q	r8, (7*8), r47
	ptabs	r45, tr5
	ptabs	r46, tr6
	ptabs	r47, tr7

	ld.q	r0, ( 9*8), r9
	ld.q	r0, (10*8), r10
	ld.q	r0, (11*8), r11
	ld.q	r0, (12*8), r12
	ld.q	r0, (13*8), r13
	ld.q	r0, (14*8), r14
	ld.q	r0, (16*8), r16

	ld.q	r0, (24*8), r24
	ld.q	r0, (25*8), r25
	ld.q	r0, (26*8), r26
	ld.q	r0, (27*8), r27
	ld.q	r0, (28*8), r28
	ld.q	r0, (29*8), r29
	ld.q	r0, (30*8), r30
	ld.q	r0, (31*8), r31
	ld.q	r0, (32*8), r32
	ld.q	r0, (33*8), r33
	ld.q	r0, (34*8), r34
	ld.q	r0, (35*8), r35

	ld.q	r0, (44*8), r44
	ld.q	r0, (45*8), r45
	ld.q	r0, (46*8), r46
	ld.q	r0, (47*8), r47
	ld.q	r0, (48*8), r48
	ld.q	r0, (49*8), r49
	ld.q	r0, (50*8), r50
	ld.q	r0, (51*8), r51
	ld.q	r0, (52*8), r52
	ld.q	r0, (53*8), r53
	ld.q	r0, (54*8), r54
	ld.q	r0, (55*8), r55
	ld.q	r0, (56*8), r56
	ld.q	r0, (57*8), r57
	ld.q	r0, (58*8), r58
	ld.q	r0, (59*8), r59

	! epilogue
	ld.l	r15, 0, r18
	ld.l	r15, 4, r14
	ptabs	r18, tr0
	movi	FRAME_SIZE, r0
	add	r15, r0, r15
	blink	tr0, r63
__sh64_switch_to_end:
.LFE1:
	.size	sh64_switch_to,.LFE1-sh64_switch_to

