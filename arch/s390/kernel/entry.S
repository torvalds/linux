/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    S390 low-level entry points.
 *
 *    Copyright IBM Corp. 1999, 2012
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *		 Hartmut Penner (hp@de.ibm.com),
 *		 Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/asm-extable.h>
#include <asm/alternative-asm.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/dwarf.h>
#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>
#include <asm/page.h>
#include <asm/sigp.h>
#include <asm/irq.h>
#include <asm/vx-insn.h>
#include <asm/setup.h>
#include <asm/nmi.h>
#include <asm/export.h>
#include <asm/nospec-insn.h>

STACK_SHIFT = PAGE_SHIFT + THREAD_SIZE_ORDER
STACK_SIZE  = 1 << STACK_SHIFT
STACK_INIT = STACK_SIZE - STACK_FRAME_OVERHEAD - __PT_SIZE

_LPP_OFFSET	= __LC_LPP

	.macro STBEAR address
	ALTERNATIVE "nop", ".insn s,0xb2010000,\address", 193
	.endm

	.macro LBEAR address
	ALTERNATIVE "nop", ".insn s,0xb2000000,\address", 193
	.endm

	.macro LPSWEY address,lpswe
	ALTERNATIVE "b \lpswe; nopr", ".insn siy,0xeb0000000071,\address,0", 193
	.endm

	.macro MBEAR reg
	ALTERNATIVE "brcl 0,0", __stringify(mvc __PT_LAST_BREAK(8,\reg),__LC_LAST_BREAK), 193
	.endm

	.macro	CHECK_STACK savearea
#ifdef CONFIG_CHECK_STACK
	tml	%r15,STACK_SIZE - CONFIG_STACK_GUARD
	lghi	%r14,\savearea
	jz	stack_overflow
#endif
	.endm

	.macro	CHECK_VMAP_STACK savearea,oklabel
#ifdef CONFIG_VMAP_STACK
	lgr	%r14,%r15
	nill	%r14,0x10000 - STACK_SIZE
	oill	%r14,STACK_INIT
	clg	%r14,__LC_KERNEL_STACK
	je	\oklabel
	clg	%r14,__LC_ASYNC_STACK
	je	\oklabel
	clg	%r14,__LC_MCCK_STACK
	je	\oklabel
	clg	%r14,__LC_NODAT_STACK
	je	\oklabel
	clg	%r14,__LC_RESTART_STACK
	je	\oklabel
	lghi	%r14,\savearea
	j	stack_overflow
#else
	j	\oklabel
#endif
	.endm

	/*
	 * The TSTMSK macro generates a test-under-mask instruction by
	 * calculating the memory offset for the specified mask value.
	 * Mask value can be any constant.  The macro shifts the mask
	 * value to calculate the memory offset for the test-under-mask
	 * instruction.
	 */
	.macro TSTMSK addr, mask, size=8, bytepos=0
		.if (\bytepos < \size) && (\mask >> 8)
			.if (\mask & 0xff)
				.error "Mask exceeds byte boundary"
			.endif
			TSTMSK \addr, "(\mask >> 8)", \size, "(\bytepos + 1)"
			.exitm
		.endif
		.ifeq \mask
			.error "Mask must not be zero"
		.endif
		off = \size - \bytepos - 1
		tm	off+\addr, \mask
	.endm

	.macro BPOFF
	ALTERNATIVE "nop", ".insn rrf,0xb2e80000,0,0,12,0", 82
	.endm

	.macro BPON
	ALTERNATIVE "nop", ".insn rrf,0xb2e80000,0,0,13,0", 82
	.endm

	.macro BPENTER tif_ptr,tif_mask
	ALTERNATIVE "TSTMSK \tif_ptr,\tif_mask; jz .+8; .insn rrf,0xb2e80000,0,0,13,0", \
		    "j .+12; nop; nop", 82
	.endm

	.macro BPEXIT tif_ptr,tif_mask
	TSTMSK	\tif_ptr,\tif_mask
	ALTERNATIVE "jz .+8;  .insn rrf,0xb2e80000,0,0,12,0", \
		    "jnz .+8; .insn rrf,0xb2e80000,0,0,13,0", 82
	.endm

	/*
	 * The CHKSTG macro jumps to the provided label in case the
	 * machine check interruption code reports one of unrecoverable
	 * storage errors:
	 * - Storage error uncorrected
	 * - Storage key error uncorrected
	 * - Storage degradation with Failing-storage-address validity
	 */
	.macro CHKSTG errlabel
	TSTMSK	__LC_MCCK_CODE,(MCCK_CODE_STG_ERROR|MCCK_CODE_STG_KEY_ERROR)
	jnz	\errlabel
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_STG_DEGRAD
	jz	.Loklabel\@
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_STG_FAIL_ADDR
	jnz	\errlabel
.Loklabel\@:
	.endm

#if IS_ENABLED(CONFIG_KVM)
	/*
	 * The OUTSIDE macro jumps to the provided label in case the value
	 * in the provided register is outside of the provided range. The
	 * macro is useful for checking whether a PSW stored in a register
	 * pair points inside or outside of a block of instructions.
	 * @reg: register to check
	 * @start: start of the range
	 * @end: end of the range
	 * @outside_label: jump here if @reg is outside of [@start..@end)
	 */
	.macro OUTSIDE reg,start,end,outside_label
	lgr	%r14,\reg
	larl	%r13,\start
	slgr	%r14,%r13
#ifdef CONFIG_AS_IS_LLVM
	clgfrl	%r14,.Lrange_size\@
#else
	clgfi	%r14,\end - \start
#endif
	jhe	\outside_label
#ifdef CONFIG_AS_IS_LLVM
	.section .rodata, "a"
	.align 4
.Lrange_size\@:
	.long	\end - \start
	.previous
#endif
	.endm

	.macro SIEEXIT
	lg	%r9,__SF_SIE_CONTROL(%r15)	# get control block pointer
	ni	__SIE_PROG0C+3(%r9),0xfe	# no longer in SIE
	lctlg	%c1,%c1,__LC_KERNEL_ASCE	# load primary asce
	larl	%r9,sie_exit			# skip forward to sie_exit
	.endm
#endif

	GEN_BR_THUNK %r14

	.section .kprobes.text, "ax"
.Ldummy:
	/*
	 * This nop exists only in order to avoid that __bpon starts at
	 * the beginning of the kprobes text section. In that case we would
	 * have several symbols at the same address. E.g. objdump would take
	 * an arbitrary symbol name when disassembling this code.
	 * With the added nop in between the __bpon symbol is unique
	 * again.
	 */
	nop	0

ENTRY(__bpon)
	.globl __bpon
	BPON
	BR_EX	%r14
ENDPROC(__bpon)

/*
 * Scheduler resume function, called by switch_to
 *  gpr2 = (task_struct *) prev
 *  gpr3 = (task_struct *) next
 * Returns:
 *  gpr2 = prev
 */
ENTRY(__switch_to)
	stmg	%r6,%r15,__SF_GPRS(%r15)	# store gprs of prev task
	lghi	%r4,__TASK_stack
	lghi	%r1,__TASK_thread
	llill	%r5,STACK_INIT
	stg	%r15,__THREAD_ksp(%r1,%r2)	# store kernel stack of prev
	lg	%r15,0(%r4,%r3)			# start of kernel stack of next
	agr	%r15,%r5			# end of kernel stack of next
	stg	%r3,__LC_CURRENT		# store task struct of next
	stg	%r15,__LC_KERNEL_STACK		# store end of kernel stack
	lg	%r15,__THREAD_ksp(%r1,%r3)	# load kernel stack of next
	aghi	%r3,__TASK_pid
	mvc	__LC_CURRENT_PID(4,%r0),0(%r3)	# store pid of next
	lmg	%r6,%r15,__SF_GPRS(%r15)	# load gprs of next task
	ALTERNATIVE "nop", "lpp _LPP_OFFSET", 40
	BR_EX	%r14
ENDPROC(__switch_to)

#if IS_ENABLED(CONFIG_KVM)
/*
 * sie64a calling convention:
 * %r2 pointer to sie control block
 * %r3 guest register save area
 */
ENTRY(sie64a)
	stmg	%r6,%r14,__SF_GPRS(%r15)	# save kernel registers
	lg	%r12,__LC_CURRENT
	stg	%r2,__SF_SIE_CONTROL(%r15)	# save control block pointer
	stg	%r3,__SF_SIE_SAVEAREA(%r15)	# save guest register save area
	xc	__SF_SIE_REASON(8,%r15),__SF_SIE_REASON(%r15) # reason code = 0
	mvc	__SF_SIE_FLAGS(8,%r15),__TI_flags(%r12) # copy thread flags
	lmg	%r0,%r13,0(%r3)			# load guest gprs 0-13
	lg	%r14,__LC_GMAP			# get gmap pointer
	ltgr	%r14,%r14
	jz	.Lsie_gmap
	lctlg	%c1,%c1,__GMAP_ASCE(%r14)	# load primary asce
.Lsie_gmap:
	lg	%r14,__SF_SIE_CONTROL(%r15)	# get control block pointer
	oi	__SIE_PROG0C+3(%r14),1		# we are going into SIE now
	tm	__SIE_PROG20+3(%r14),3		# last exit...
	jnz	.Lsie_skip
	TSTMSK	__LC_CPU_FLAGS,_CIF_FPU
	jo	.Lsie_skip			# exit if fp/vx regs changed
	BPEXIT	__SF_SIE_FLAGS(%r15),(_TIF_ISOLATE_BP|_TIF_ISOLATE_BP_GUEST)
.Lsie_entry:
	sie	0(%r14)
# Let the next instruction be NOP to avoid triggering a machine check
# and handling it in a guest as result of the instruction execution.
	nopr	7
.Lsie_leave:
	BPOFF
	BPENTER	__SF_SIE_FLAGS(%r15),(_TIF_ISOLATE_BP|_TIF_ISOLATE_BP_GUEST)
.Lsie_skip:
	ni	__SIE_PROG0C+3(%r14),0xfe	# no longer in SIE
	lctlg	%c1,%c1,__LC_KERNEL_ASCE	# load primary asce
.Lsie_done:
# some program checks are suppressing. C code (e.g. do_protection_exception)
# will rewind the PSW by the ILC, which is often 4 bytes in case of SIE. There
# are some corner cases (e.g. runtime instrumentation) where ILC is unpredictable.
# Other instructions between sie64a and .Lsie_done should not cause program
# interrupts. So lets use 3 nops as a landing pad for all possible rewinds.
.Lrewind_pad6:
	nopr	7
.Lrewind_pad4:
	nopr	7
.Lrewind_pad2:
	nopr	7
	.globl sie_exit
sie_exit:
	lg	%r14,__SF_SIE_SAVEAREA(%r15)	# load guest register save area
	stmg	%r0,%r13,0(%r14)		# save guest gprs 0-13
	xgr	%r0,%r0				# clear guest registers to
	xgr	%r1,%r1				# prevent speculative use
	xgr	%r3,%r3
	xgr	%r4,%r4
	xgr	%r5,%r5
	lmg	%r6,%r14,__SF_GPRS(%r15)	# restore kernel registers
	lg	%r2,__SF_SIE_REASON(%r15)	# return exit reason code
	BR_EX	%r14
.Lsie_fault:
	lghi	%r14,-EFAULT
	stg	%r14,__SF_SIE_REASON(%r15)	# set exit reason code
	j	sie_exit

	EX_TABLE(.Lrewind_pad6,.Lsie_fault)
	EX_TABLE(.Lrewind_pad4,.Lsie_fault)
	EX_TABLE(.Lrewind_pad2,.Lsie_fault)
	EX_TABLE(sie_exit,.Lsie_fault)
ENDPROC(sie64a)
EXPORT_SYMBOL(sie64a)
EXPORT_SYMBOL(sie_exit)
#endif

/*
 * SVC interrupt handler routine. System calls are synchronous events and
 * are entered with interrupts disabled.
 */

ENTRY(system_call)
	stpt	__LC_SYS_ENTER_TIMER
	stmg	%r8,%r15,__LC_SAVE_AREA_SYNC
	BPOFF
	lghi	%r14,0
.Lsysc_per:
	STBEAR	__LC_LAST_BREAK
	lctlg	%c1,%c1,__LC_KERNEL_ASCE
	lg	%r12,__LC_CURRENT
	lg	%r15,__LC_KERNEL_STACK
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	stmg	%r0,%r7,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	BPENTER __TI_flags(%r12),_TIF_ISOLATE_BP
	# clear user controlled register to prevent speculative use
	xgr	%r0,%r0
	xgr	%r1,%r1
	xgr	%r4,%r4
	xgr	%r5,%r5
	xgr	%r6,%r6
	xgr	%r7,%r7
	xgr	%r8,%r8
	xgr	%r9,%r9
	xgr	%r10,%r10
	xgr	%r11,%r11
	la	%r2,STACK_FRAME_OVERHEAD(%r15)	# pointer to pt_regs
	mvc	__PT_R8(64,%r2),__LC_SAVE_AREA_SYNC
	MBEAR	%r2
	lgr	%r3,%r14
	brasl	%r14,__do_syscall
	lctlg	%c1,%c1,__LC_USER_ASCE
	mvc	__LC_RETURN_PSW(16),STACK_FRAME_OVERHEAD+__PT_PSW(%r15)
	BPEXIT __TI_flags(%r12),_TIF_ISOLATE_BP
	LBEAR	STACK_FRAME_OVERHEAD+__PT_LAST_BREAK(%r15)
	lmg	%r0,%r15,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	stpt	__LC_EXIT_TIMER
	LPSWEY	__LC_RETURN_PSW,__LC_RETURN_LPSWE
ENDPROC(system_call)

#
# a new process exits the kernel with ret_from_fork
#
ENTRY(ret_from_fork)
	lgr	%r3,%r11
	brasl	%r14,__ret_from_fork
	lctlg	%c1,%c1,__LC_USER_ASCE
	mvc	__LC_RETURN_PSW(16),STACK_FRAME_OVERHEAD+__PT_PSW(%r15)
	BPEXIT __TI_flags(%r12),_TIF_ISOLATE_BP
	LBEAR	STACK_FRAME_OVERHEAD+__PT_LAST_BREAK(%r15)
	lmg	%r0,%r15,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	stpt	__LC_EXIT_TIMER
	LPSWEY	__LC_RETURN_PSW,__LC_RETURN_LPSWE
ENDPROC(ret_from_fork)

/*
 * Program check handler routine
 */

ENTRY(pgm_check_handler)
	stpt	__LC_SYS_ENTER_TIMER
	BPOFF
	stmg	%r8,%r15,__LC_SAVE_AREA_SYNC
	lg	%r12,__LC_CURRENT
	lghi	%r10,0
	lmg	%r8,%r9,__LC_PGM_OLD_PSW
	tmhh	%r8,0x0001		# coming from user space?
	jno	.Lpgm_skip_asce
	lctlg	%c1,%c1,__LC_KERNEL_ASCE
	j	3f			# -> fault in user space
.Lpgm_skip_asce:
#if IS_ENABLED(CONFIG_KVM)
	# cleanup critical section for program checks in sie64a
	OUTSIDE	%r9,.Lsie_gmap,.Lsie_done,1f
	SIEEXIT
	lghi	%r10,_PIF_GUEST_FAULT
#endif
1:	tmhh	%r8,0x4000		# PER bit set in old PSW ?
	jnz	2f			# -> enabled, can't be a double fault
	tm	__LC_PGM_ILC+3,0x80	# check for per exception
	jnz	.Lpgm_svcper		# -> single stepped svc
2:	CHECK_STACK __LC_SAVE_AREA_SYNC
	aghi	%r15,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	# CHECK_VMAP_STACK branches to stack_overflow or 4f
	CHECK_VMAP_STACK __LC_SAVE_AREA_SYNC,4f
3:	BPENTER __TI_flags(%r12),_TIF_ISOLATE_BP
	lg	%r15,__LC_KERNEL_STACK
4:	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	stg	%r10,__PT_FLAGS(%r11)
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	stmg	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_SYNC
	mvc	__PT_LAST_BREAK(8,%r11),__LC_PGM_LAST_BREAK
	stmg	%r8,%r9,__PT_PSW(%r11)

	# clear user controlled registers to prevent speculative use
	xgr	%r0,%r0
	xgr	%r1,%r1
	xgr	%r3,%r3
	xgr	%r4,%r4
	xgr	%r5,%r5
	xgr	%r6,%r6
	xgr	%r7,%r7
	lgr	%r2,%r11
	brasl	%r14,__do_pgm_check
	tmhh	%r8,0x0001		# returning to user space?
	jno	.Lpgm_exit_kernel
	lctlg	%c1,%c1,__LC_USER_ASCE
	BPEXIT __TI_flags(%r12),_TIF_ISOLATE_BP
	stpt	__LC_EXIT_TIMER
.Lpgm_exit_kernel:
	mvc	__LC_RETURN_PSW(16),STACK_FRAME_OVERHEAD+__PT_PSW(%r15)
	LBEAR	STACK_FRAME_OVERHEAD+__PT_LAST_BREAK(%r15)
	lmg	%r0,%r15,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	LPSWEY	__LC_RETURN_PSW,__LC_RETURN_LPSWE

#
# single stepped system call
#
.Lpgm_svcper:
	mvc	__LC_RETURN_PSW(8),__LC_SVC_NEW_PSW
	larl	%r14,.Lsysc_per
	stg	%r14,__LC_RETURN_PSW+8
	lghi	%r14,1
	LBEAR	__LC_PGM_LAST_BREAK
	LPSWEY	__LC_RETURN_PSW,__LC_RETURN_LPSWE # branch to .Lsysc_per
ENDPROC(pgm_check_handler)

/*
 * Interrupt handler macro used for external and IO interrupts.
 */
.macro INT_HANDLER name,lc_old_psw,handler
ENTRY(\name)
	stckf	__LC_INT_CLOCK
	stpt	__LC_SYS_ENTER_TIMER
	STBEAR	__LC_LAST_BREAK
	BPOFF
	stmg	%r8,%r15,__LC_SAVE_AREA_ASYNC
	lg	%r12,__LC_CURRENT
	lmg	%r8,%r9,\lc_old_psw
	tmhh	%r8,0x0001			# interrupting from user ?
	jnz	1f
#if IS_ENABLED(CONFIG_KVM)
	OUTSIDE	%r9,.Lsie_gmap,.Lsie_done,0f
	BPENTER	__SF_SIE_FLAGS(%r15),(_TIF_ISOLATE_BP|_TIF_ISOLATE_BP_GUEST)
	SIEEXIT
#endif
0:	CHECK_STACK __LC_SAVE_AREA_ASYNC
	aghi	%r15,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	j	2f
1:	BPENTER __TI_flags(%r12),_TIF_ISOLATE_BP
	lctlg	%c1,%c1,__LC_KERNEL_ASCE
	lg	%r15,__LC_KERNEL_STACK
2:	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	stmg	%r0,%r7,__PT_R0(%r11)
	# clear user controlled registers to prevent speculative use
	xgr	%r0,%r0
	xgr	%r1,%r1
	xgr	%r3,%r3
	xgr	%r4,%r4
	xgr	%r5,%r5
	xgr	%r6,%r6
	xgr	%r7,%r7
	xgr	%r10,%r10
	xc	__PT_FLAGS(8,%r11),__PT_FLAGS(%r11)
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_ASYNC
	MBEAR	%r11
	stmg	%r8,%r9,__PT_PSW(%r11)
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,\handler
	mvc	__LC_RETURN_PSW(16),__PT_PSW(%r11)
	tmhh	%r8,0x0001		# returning to user ?
	jno	2f
	lctlg	%c1,%c1,__LC_USER_ASCE
	BPEXIT	__TI_flags(%r12),_TIF_ISOLATE_BP
	stpt	__LC_EXIT_TIMER
2:	LBEAR	__PT_LAST_BREAK(%r11)
	lmg	%r0,%r15,__PT_R0(%r11)
	LPSWEY	__LC_RETURN_PSW,__LC_RETURN_LPSWE
ENDPROC(\name)
.endm

INT_HANDLER ext_int_handler,__LC_EXT_OLD_PSW,do_ext_irq
INT_HANDLER io_int_handler,__LC_IO_OLD_PSW,do_io_irq

/*
 * Load idle PSW.
 */
ENTRY(psw_idle)
	stg	%r14,(__SF_GPRS+8*8)(%r15)
	stg	%r3,__SF_EMPTY(%r15)
	larl	%r1,psw_idle_exit
	stg	%r1,__SF_EMPTY+8(%r15)
	larl	%r1,smp_cpu_mtid
	llgf	%r1,0(%r1)
	ltgr	%r1,%r1
	jz	.Lpsw_idle_stcctm
	.insn	rsy,0xeb0000000017,%r1,5,__MT_CYCLES_ENTER(%r2)
.Lpsw_idle_stcctm:
	oi	__LC_CPU_FLAGS+7,_CIF_ENABLED_WAIT
	BPON
	stckf	__CLOCK_IDLE_ENTER(%r2)
	stpt	__TIMER_IDLE_ENTER(%r2)
	lpswe	__SF_EMPTY(%r15)
.globl psw_idle_exit
psw_idle_exit:
	BR_EX	%r14
ENDPROC(psw_idle)

/*
 * Machine check handler routines
 */
ENTRY(mcck_int_handler)
	stckf	__LC_MCCK_CLOCK
	BPOFF
	la	%r1,4095		# validate r1
	spt	__LC_CPU_TIMER_SAVE_AREA-4095(%r1)	# validate cpu timer
	LBEAR	__LC_LAST_BREAK_SAVE_AREA-4095(%r1)		# validate bear
	lmg	%r0,%r15,__LC_GPREGS_SAVE_AREA-4095(%r1)# validate gprs
	lg	%r12,__LC_CURRENT
	lmg	%r8,%r9,__LC_MCK_OLD_PSW
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_SYSTEM_DAMAGE
	jo	.Lmcck_panic		# yes -> rest of mcck code invalid
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_CR_VALID
	jno	.Lmcck_panic		# control registers invalid -> panic
	la	%r14,4095
	lctlg	%c0,%c15,__LC_CREGS_SAVE_AREA-4095(%r14) # validate ctl regs
	ptlb
	lghi	%r14,__LC_CPU_TIMER_SAVE_AREA
	mvc	__LC_MCCK_ENTER_TIMER(8),0(%r14)
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_CPU_TIMER_VALID
	jo	3f
	la	%r14,__LC_SYS_ENTER_TIMER
	clc	0(8,%r14),__LC_EXIT_TIMER
	jl	1f
	la	%r14,__LC_EXIT_TIMER
1:	clc	0(8,%r14),__LC_LAST_UPDATE_TIMER
	jl	2f
	la	%r14,__LC_LAST_UPDATE_TIMER
2:	spt	0(%r14)
	mvc	__LC_MCCK_ENTER_TIMER(8),0(%r14)
3:	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_PSW_MWP_VALID
	jno	.Lmcck_panic
	tmhh	%r8,0x0001		# interrupting from user ?
	jnz	6f
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_PSW_IA_VALID
	jno	.Lmcck_panic
#if IS_ENABLED(CONFIG_KVM)
	OUTSIDE	%r9,.Lsie_gmap,.Lsie_done,6f
	OUTSIDE	%r9,.Lsie_entry,.Lsie_leave,4f
	oi	__LC_CPU_FLAGS+7, _CIF_MCCK_GUEST
	j	5f
4:	CHKSTG	.Lmcck_panic
5:	larl	%r14,.Lstosm_tmp
	stosm	0(%r14),0x04		# turn dat on, keep irqs off
	BPENTER	__SF_SIE_FLAGS(%r15),(_TIF_ISOLATE_BP|_TIF_ISOLATE_BP_GUEST)
	SIEEXIT
	j	.Lmcck_stack
#endif
6:	CHKSTG	.Lmcck_panic
	larl	%r14,.Lstosm_tmp
	stosm	0(%r14),0x04		# turn dat on, keep irqs off
	tmhh	%r8,0x0001		# interrupting from user ?
	jz	.Lmcck_stack
	BPENTER __TI_flags(%r12),_TIF_ISOLATE_BP
.Lmcck_stack:
	lg	%r15,__LC_MCCK_STACK
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	stctg	%c1,%c1,__PT_CR1(%r11)
	lctlg	%c1,%c1,__LC_KERNEL_ASCE
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lghi	%r14,__LC_GPREGS_SAVE_AREA+64
	stmg	%r0,%r7,__PT_R0(%r11)
	# clear user controlled registers to prevent speculative use
	xgr	%r0,%r0
	xgr	%r1,%r1
	xgr	%r3,%r3
	xgr	%r4,%r4
	xgr	%r5,%r5
	xgr	%r6,%r6
	xgr	%r7,%r7
	xgr	%r10,%r10
	mvc	__PT_R8(64,%r11),0(%r14)
	stmg	%r8,%r9,__PT_PSW(%r11)
	xc	__PT_FLAGS(8,%r11),__PT_FLAGS(%r11)
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,s390_do_machine_check
	cghi	%r2,0
	je	.Lmcck_return
	lg	%r1,__LC_KERNEL_STACK	# switch to kernel stack
	mvc	STACK_FRAME_OVERHEAD(__PT_SIZE,%r1),0(%r11)
	xc	__SF_BACKCHAIN(8,%r1),__SF_BACKCHAIN(%r1)
	la	%r11,STACK_FRAME_OVERHEAD(%r1)
	lgr	%r2,%r11
	lgr	%r15,%r1
	brasl	%r14,s390_handle_mcck
.Lmcck_return:
	lctlg	%c1,%c1,__PT_CR1(%r11)
	lmg	%r0,%r10,__PT_R0(%r11)
	mvc	__LC_RETURN_MCCK_PSW(16),__PT_PSW(%r11) # move return PSW
	tm	__LC_RETURN_MCCK_PSW+1,0x01 # returning to user ?
	jno	0f
	BPEXIT	__TI_flags(%r12),_TIF_ISOLATE_BP
	stpt	__LC_EXIT_TIMER
0:	ALTERNATIVE "nop", __stringify(lghi %r12,__LC_LAST_BREAK_SAVE_AREA),193
	LBEAR	0(%r12)
	lmg	%r11,%r15,__PT_R11(%r11)
	LPSWEY	__LC_RETURN_MCCK_PSW,__LC_RETURN_MCCK_LPSWE

.Lmcck_panic:
	/*
	 * Iterate over all possible CPU addresses in the range 0..0xffff
	 * and stop each CPU using signal processor. Use compare and swap
	 * to allow just one CPU-stopper and prevent concurrent CPUs from
	 * stopping each other while leaving the others running.
	 */
	lhi	%r5,0
	lhi	%r6,1
	larl	%r7,.Lstop_lock
	cs	%r5,%r6,0(%r7)		# single CPU-stopper only
	jnz	4f
	larl	%r7,.Lthis_cpu
	stap	0(%r7)			# this CPU address
	lh	%r4,0(%r7)
	nilh	%r4,0
	lhi	%r0,1
	sll	%r0,16			# CPU counter
	lhi	%r3,0			# next CPU address
0:	cr	%r3,%r4
	je	2f
1:	sigp	%r1,%r3,SIGP_STOP	# stop next CPU
	brc	SIGP_CC_BUSY,1b
2:	ahi	%r3,1
	brct	%r0,0b
3:	sigp	%r1,%r4,SIGP_STOP	# stop this CPU
	brc	SIGP_CC_BUSY,3b
4:	j	4b
ENDPROC(mcck_int_handler)

ENTRY(restart_int_handler)
	ALTERNATIVE "nop", "lpp _LPP_OFFSET", 40
	stg	%r15,__LC_SAVE_AREA_RESTART
	TSTMSK	__LC_RESTART_FLAGS,RESTART_FLAG_CTLREGS,4
	jz	0f
	la	%r15,4095
	lctlg	%c0,%c15,__LC_CREGS_SAVE_AREA-4095(%r15)
0:	larl	%r15,.Lstosm_tmp
	stosm	0(%r15),0x04			# turn dat on, keep irqs off
	lg	%r15,__LC_RESTART_STACK
	xc	STACK_FRAME_OVERHEAD(__PT_SIZE,%r15),STACK_FRAME_OVERHEAD(%r15)
	stmg	%r0,%r14,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	mvc	STACK_FRAME_OVERHEAD+__PT_R15(8,%r15),__LC_SAVE_AREA_RESTART
	mvc	STACK_FRAME_OVERHEAD+__PT_PSW(16,%r15),__LC_RST_OLD_PSW
	xc	0(STACK_FRAME_OVERHEAD,%r15),0(%r15)
	lg	%r1,__LC_RESTART_FN		# load fn, parm & source cpu
	lg	%r2,__LC_RESTART_DATA
	lgf	%r3,__LC_RESTART_SOURCE
	ltgr	%r3,%r3				# test source cpu address
	jm	1f				# negative -> skip source stop
0:	sigp	%r4,%r3,SIGP_SENSE		# sigp sense to source cpu
	brc	10,0b				# wait for status stored
1:	basr	%r14,%r1			# call function
	stap	__SF_EMPTY(%r15)		# store cpu address
	llgh	%r3,__SF_EMPTY(%r15)
2:	sigp	%r4,%r3,SIGP_STOP		# sigp stop to current cpu
	brc	2,2b
3:	j	3b
ENDPROC(restart_int_handler)

	.section .kprobes.text, "ax"

#if defined(CONFIG_CHECK_STACK) || defined(CONFIG_VMAP_STACK)
/*
 * The synchronous or the asynchronous stack overflowed. We are dead.
 * No need to properly save the registers, we are going to panic anyway.
 * Setup a pt_regs so that show_trace can provide a good call trace.
 */
ENTRY(stack_overflow)
	lg	%r15,__LC_NODAT_STACK	# change to panic stack
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	stmg	%r0,%r7,__PT_R0(%r11)
	stmg	%r8,%r9,__PT_PSW(%r11)
	mvc	__PT_R8(64,%r11),0(%r14)
	stg	%r10,__PT_ORIG_GPR2(%r11) # store last break to orig_gpr2
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lgr	%r2,%r11		# pass pointer to pt_regs
	jg	kernel_stack_overflow
ENDPROC(stack_overflow)
#endif

	.section .data, "aw"
		.align	4
.Lstop_lock:	.long	0
.Lthis_cpu:	.short	0
.Lstosm_tmp:	.byte	0
	.section .rodata, "a"
	.balign	8
#define SYSCALL(esame,emu)	.quad __s390x_ ## esame
	.globl	sys_call_table
sys_call_table:
#include "asm/syscall_table.h"
#undef SYSCALL

#ifdef CONFIG_COMPAT

#define SYSCALL(esame,emu)	.quad __s390_ ## emu
	.globl	sys_call_table_emu
sys_call_table_emu:
#include "asm/syscall_table.h"
#undef SYSCALL
#endif
