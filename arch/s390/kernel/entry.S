/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    S390 low-level entry points.
 *
 *    Copyright IBM Corp. 1999, 2012
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *		 Hartmut Penner (hp@de.ibm.com),
 *		 Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 */

#include <linux/export.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/asm-extable.h>
#include <asm/alternative.h>
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
#include <asm/fpu-insn.h>
#include <asm/setup.h>
#include <asm/nmi.h>
#include <asm/nospec-insn.h>
#include <asm/lowcore.h>

_LPP_OFFSET	= __LC_LPP

	.macro STBEAR address
	ALTERNATIVE "nop", ".insn s,0xb2010000,\address", ALT_FACILITY(193)
	.endm

	.macro LBEAR address
	ALTERNATIVE "nop", ".insn s,0xb2000000,\address", ALT_FACILITY(193)
	.endm

	.macro LPSWEY address, lpswe
	ALTERNATIVE_2 "b \lpswe;nopr", \
		".insn siy,0xeb0000000071,\address,0", ALT_FACILITY_EARLY(193),		\
		__stringify(.insn siy,0xeb0000000071,LOWCORE_ALT_ADDRESS+\address,0),	\
		ALT_LOWCORE
	.endm

	.macro MBEAR reg, lowcore
	ALTERNATIVE "brcl 0,0", __stringify(mvc __PT_LAST_BREAK(8,\reg),__LC_LAST_BREAK(\lowcore)),\
		ALT_FACILITY(193)
	.endm

	.macro	CHECK_STACK savearea, lowcore
#ifdef CONFIG_CHECK_STACK
	tml	%r15,THREAD_SIZE - CONFIG_STACK_GUARD
	la	%r14,\savearea(\lowcore)
	jz	stack_overflow
#endif
	.endm

	.macro	CHECK_VMAP_STACK savearea, lowcore, oklabel
#ifdef CONFIG_VMAP_STACK
	lgr	%r14,%r15
	nill	%r14,0x10000 - THREAD_SIZE
	oill	%r14,STACK_INIT_OFFSET
	clg	%r14,__LC_KERNEL_STACK(\lowcore)
	je	\oklabel
	clg	%r14,__LC_ASYNC_STACK(\lowcore)
	je	\oklabel
	clg	%r14,__LC_MCCK_STACK(\lowcore)
	je	\oklabel
	clg	%r14,__LC_NODAT_STACK(\lowcore)
	je	\oklabel
	clg	%r14,__LC_RESTART_STACK(\lowcore)
	je	\oklabel
	la	%r14,\savearea(\lowcore)
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
	ALTERNATIVE "nop", ".insn rrf,0xb2e80000,0,0,12,0", ALT_SPEC(82)
	.endm

	.macro BPON
	ALTERNATIVE "nop", ".insn rrf,0xb2e80000,0,0,13,0", ALT_SPEC(82)
	.endm

	.macro BPENTER tif_ptr,tif_mask
	ALTERNATIVE "TSTMSK \tif_ptr,\tif_mask; jz .+8; .insn rrf,0xb2e80000,0,0,13,0", \
		    "j .+12; nop; nop", ALT_SPEC(82)
	.endm

	.macro BPEXIT tif_ptr,tif_mask
	TSTMSK	\tif_ptr,\tif_mask
	ALTERNATIVE "jz .+8;  .insn rrf,0xb2e80000,0,0,12,0", \
		    "jnz .+8; .insn rrf,0xb2e80000,0,0,13,0", ALT_SPEC(82)
	.endm

#if IS_ENABLED(CONFIG_KVM)
	.macro SIEEXIT sie_control,lowcore
	lg	%r9,\sie_control			# get control block pointer
	ni	__SIE_PROG0C+3(%r9),0xfe		# no longer in SIE
	lctlg	%c1,%c1,__LC_KERNEL_ASCE(\lowcore)	# load primary asce
	lg	%r9,__LC_CURRENT(\lowcore)
	mvi	__TI_sie(%r9),0
	larl	%r9,sie_exit			# skip forward to sie_exit
	.endm
#endif

	.macro STACKLEAK_ERASE
#ifdef CONFIG_GCC_PLUGIN_STACKLEAK
	brasl	%r14,stackleak_erase_on_task_stack
#endif
	.endm

	GEN_BR_THUNK %r14

	.section .kprobes.text, "ax"
.Ldummy:
	/*
	 * The following nop exists only in order to avoid that the next
	 * symbol starts at the beginning of the kprobes text section.
	 * In that case there would be several symbols at the same address.
	 * E.g. objdump would take an arbitrary symbol when disassembling
	 * the code.
	 * With the added nop in between this cannot happen.
	 */
	nop	0

/*
 * Scheduler resume function, called by __switch_to
 *  gpr2 = (task_struct *)prev
 *  gpr3 = (task_struct *)next
 * Returns:
 *  gpr2 = prev
 */
SYM_FUNC_START(__switch_to_asm)
	stmg	%r6,%r15,__SF_GPRS(%r15)	# store gprs of prev task
	lghi	%r4,__TASK_stack
	lghi	%r1,__TASK_thread
	llill	%r5,STACK_INIT_OFFSET
	stg	%r15,__THREAD_ksp(%r1,%r2)	# store kernel stack of prev
	lg	%r15,0(%r4,%r3)			# start of kernel stack of next
	agr	%r15,%r5			# end of kernel stack of next
	GET_LC	%r13
	stg	%r3,__LC_CURRENT(%r13)		# store task struct of next
	stg	%r15,__LC_KERNEL_STACK(%r13)	# store end of kernel stack
	lg	%r15,__THREAD_ksp(%r1,%r3)	# load kernel stack of next
	aghi	%r3,__TASK_pid
	mvc	__LC_CURRENT_PID(4,%r13),0(%r3)	# store pid of next
	ALTERNATIVE "nop", "lpp _LPP_OFFSET(%r13)", ALT_FACILITY(40)
	lmg	%r6,%r15,__SF_GPRS(%r15)	# load gprs of next task
	BR_EX	%r14
SYM_FUNC_END(__switch_to_asm)

#if IS_ENABLED(CONFIG_KVM)
/*
 * __sie64a calling convention:
 * %r2 pointer to sie control block phys
 * %r3 pointer to sie control block virt
 * %r4 guest register save area
 * %r5 guest asce
 */
SYM_FUNC_START(__sie64a)
	stmg	%r6,%r14,__SF_GPRS(%r15)	# save kernel registers
	GET_LC	%r13
	lg	%r14,__LC_CURRENT(%r13)
	stg	%r2,__SF_SIE_CONTROL_PHYS(%r15)	# save sie block physical..
	stg	%r3,__SF_SIE_CONTROL(%r15)	# ...and virtual addresses
	stg	%r4,__SF_SIE_SAVEAREA(%r15)	# save guest register save area
	stg	%r5,__SF_SIE_GUEST_ASCE(%r15)	# save guest asce
	xc	__SF_SIE_REASON(8,%r15),__SF_SIE_REASON(%r15) # reason code = 0
	mvc	__SF_SIE_FLAGS(8,%r15),__TI_flags(%r14) # copy thread flags
	lmg	%r0,%r13,0(%r4)			# load guest gprs 0-13
	mvi	__TI_sie(%r14),1
	lctlg	%c1,%c1,__SF_SIE_GUEST_ASCE(%r15) # load primary asce
	lg	%r14,__SF_SIE_CONTROL(%r15)	# get control block pointer
	oi	__SIE_PROG0C+3(%r14),1		# we are going into SIE now
	tm	__SIE_PROG20+3(%r14),3		# last exit...
	jnz	.Lsie_skip
	lg	%r14,__SF_SIE_CONTROL_PHYS(%r15)	# get sie block phys addr
	BPEXIT	__SF_SIE_FLAGS(%r15),_TIF_ISOLATE_BP_GUEST
.Lsie_entry:
	sie	0(%r14)
# Let the next instruction be NOP to avoid triggering a machine check
# and handling it in a guest as result of the instruction execution.
	nopr	7
.Lsie_leave:
	BPOFF
	BPENTER	__SF_SIE_FLAGS(%r15),_TIF_ISOLATE_BP_GUEST
.Lsie_skip:
	lg	%r14,__SF_SIE_CONTROL(%r15)	# get control block pointer
	ni	__SIE_PROG0C+3(%r14),0xfe	# no longer in SIE
	GET_LC	%r14
	lctlg	%c1,%c1,__LC_KERNEL_ASCE(%r14)	# load primary asce
	lg	%r14,__LC_CURRENT(%r14)
	mvi	__TI_sie(%r14),0
# some program checks are suppressing. C code (e.g. do_protection_exception)
# will rewind the PSW by the ILC, which is often 4 bytes in case of SIE. There
# are some corner cases (e.g. runtime instrumentation) where ILC is unpredictable.
# Other instructions between __sie64a and .Lsie_done should not cause program
# interrupts. So lets use 3 nops as a landing pad for all possible rewinds.
.Lrewind_pad6:
	nopr	7
.Lrewind_pad4:
	nopr	7
.Lrewind_pad2:
	nopr	7
SYM_INNER_LABEL(sie_exit, SYM_L_GLOBAL)
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
SYM_FUNC_END(__sie64a)
EXPORT_SYMBOL(__sie64a)
EXPORT_SYMBOL(sie_exit)
#endif

/*
 * SVC interrupt handler routine. System calls are synchronous events and
 * are entered with interrupts disabled.
 */

SYM_CODE_START(system_call)
	STMG_LC	%r8,%r15,__LC_SAVE_AREA_SYNC
	GET_LC	%r13
	stpt	__LC_SYS_ENTER_TIMER(%r13)
	BPOFF
	lghi	%r14,0
.Lsysc_per:
	STBEAR	__LC_LAST_BREAK(%r13)
	lctlg	%c1,%c1,__LC_KERNEL_ASCE(%r13)
	lg	%r15,__LC_KERNEL_STACK(%r13)
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	stmg	%r0,%r7,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
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
	mvc	__PT_R8(64,%r2),__LC_SAVE_AREA_SYNC(%r13)
	MBEAR	%r2,%r13
	lgr	%r3,%r14
	brasl	%r14,__do_syscall
	STACKLEAK_ERASE
	lctlg	%c1,%c1,__LC_USER_ASCE(%r13)
	mvc	__LC_RETURN_PSW(16,%r13),STACK_FRAME_OVERHEAD+__PT_PSW(%r15)
	BPON
	LBEAR	STACK_FRAME_OVERHEAD+__PT_LAST_BREAK(%r15)
	stpt	__LC_EXIT_TIMER(%r13)
	lmg	%r0,%r15,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	LPSWEY	__LC_RETURN_PSW,__LC_RETURN_LPSWE
SYM_CODE_END(system_call)

#
# a new process exits the kernel with ret_from_fork
#
SYM_CODE_START(ret_from_fork)
	lgr	%r3,%r11
	brasl	%r14,__ret_from_fork
	STACKLEAK_ERASE
	GET_LC	%r13
	lctlg	%c1,%c1,__LC_USER_ASCE(%r13)
	mvc	__LC_RETURN_PSW(16,%r13),STACK_FRAME_OVERHEAD+__PT_PSW(%r15)
	BPON
	LBEAR	STACK_FRAME_OVERHEAD+__PT_LAST_BREAK(%r15)
	stpt	__LC_EXIT_TIMER(%r13)
	lmg	%r0,%r15,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	LPSWEY	__LC_RETURN_PSW,__LC_RETURN_LPSWE
SYM_CODE_END(ret_from_fork)

/*
 * Program check handler routine
 */

SYM_CODE_START(pgm_check_handler)
	STMG_LC	%r8,%r15,__LC_SAVE_AREA_SYNC
	GET_LC	%r13
	stpt	__LC_SYS_ENTER_TIMER(%r13)
	BPOFF
	lgr	%r10,%r15
	lmg	%r8,%r9,__LC_PGM_OLD_PSW(%r13)
	tmhh	%r8,0x0001		# coming from user space?
	jno	.Lpgm_skip_asce
	lctlg	%c1,%c1,__LC_KERNEL_ASCE(%r13)
	j	3f			# -> fault in user space
.Lpgm_skip_asce:
1:	tmhh	%r8,0x4000		# PER bit set in old PSW ?
	jnz	2f			# -> enabled, can't be a double fault
	tm	__LC_PGM_ILC+3(%r13),0x80	# check for per exception
	jnz	.Lpgm_svcper		# -> single stepped svc
2:	CHECK_STACK __LC_SAVE_AREA_SYNC,%r13
	aghi	%r15,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	# CHECK_VMAP_STACK branches to stack_overflow or 4f
	CHECK_VMAP_STACK __LC_SAVE_AREA_SYNC,%r13,4f
3:	lg	%r15,__LC_KERNEL_STACK(%r13)
4:	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	xc	__PT_FLAGS(8,%r11),__PT_FLAGS(%r11)
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	stmg	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_SYNC(%r13)
	mvc	__PT_LAST_BREAK(8,%r11),__LC_PGM_LAST_BREAK(%r13)
	stctg	%c1,%c1,__PT_CR1(%r11)
#if IS_ENABLED(CONFIG_KVM)
	ltg	%r12,__LC_GMAP(%r13)
	jz	5f
	clc	__GMAP_ASCE(8,%r12), __PT_CR1(%r11)
	jne	5f
	BPENTER	__SF_SIE_FLAGS(%r10),_TIF_ISOLATE_BP_GUEST
	SIEEXIT __SF_SIE_CONTROL(%r10),%r13
#endif
5:	stmg	%r8,%r9,__PT_PSW(%r11)
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
	STACKLEAK_ERASE
	lctlg	%c1,%c1,__LC_USER_ASCE(%r13)
	BPON
	stpt	__LC_EXIT_TIMER(%r13)
.Lpgm_exit_kernel:
	mvc	__LC_RETURN_PSW(16,%r13),STACK_FRAME_OVERHEAD+__PT_PSW(%r15)
	LBEAR	STACK_FRAME_OVERHEAD+__PT_LAST_BREAK(%r15)
	lmg	%r0,%r15,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	LPSWEY	__LC_RETURN_PSW,__LC_RETURN_LPSWE

#
# single stepped system call
#
.Lpgm_svcper:
	mvc	__LC_RETURN_PSW(8,%r13),__LC_SVC_NEW_PSW(%r13)
	larl	%r14,.Lsysc_per
	stg	%r14,__LC_RETURN_PSW+8(%r13)
	lghi	%r14,1
	LBEAR	__LC_PGM_LAST_BREAK(%r13)
	LPSWEY	__LC_RETURN_PSW,__LC_RETURN_LPSWE # branch to .Lsysc_per
SYM_CODE_END(pgm_check_handler)

/*
 * Interrupt handler macro used for external and IO interrupts.
 */
.macro INT_HANDLER name,lc_old_psw,handler
SYM_CODE_START(\name)
	STMG_LC	%r8,%r15,__LC_SAVE_AREA_ASYNC
	GET_LC	%r13
	stckf	__LC_INT_CLOCK(%r13)
	stpt	__LC_SYS_ENTER_TIMER(%r13)
	STBEAR	__LC_LAST_BREAK(%r13)
	BPOFF
	lmg	%r8,%r9,\lc_old_psw(%r13)
	tmhh	%r8,0x0001			# interrupting from user ?
	jnz	1f
#if IS_ENABLED(CONFIG_KVM)
	lg	%r10,__LC_CURRENT(%r13)
	tm	__TI_sie(%r10),0xff
	jz	0f
	BPENTER	__SF_SIE_FLAGS(%r15),_TIF_ISOLATE_BP_GUEST
	SIEEXIT __SF_SIE_CONTROL(%r15),%r13
#endif
0:	CHECK_STACK __LC_SAVE_AREA_ASYNC,%r13
	aghi	%r15,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	j	2f
1:	lctlg	%c1,%c1,__LC_KERNEL_ASCE(%r13)
	lg	%r15,__LC_KERNEL_STACK(%r13)
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
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_ASYNC(%r13)
	MBEAR	%r11,%r13
	stmg	%r8,%r9,__PT_PSW(%r11)
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,\handler
	mvc	__LC_RETURN_PSW(16,%r13),__PT_PSW(%r11)
	tmhh	%r8,0x0001		# returning to user ?
	jno	2f
	STACKLEAK_ERASE
	lctlg	%c1,%c1,__LC_USER_ASCE(%r13)
	BPON
	stpt	__LC_EXIT_TIMER(%r13)
2:	LBEAR	__PT_LAST_BREAK(%r11)
	lmg	%r0,%r15,__PT_R0(%r11)
	LPSWEY	__LC_RETURN_PSW,__LC_RETURN_LPSWE
SYM_CODE_END(\name)
.endm

INT_HANDLER ext_int_handler,__LC_EXT_OLD_PSW,do_ext_irq
INT_HANDLER io_int_handler,__LC_IO_OLD_PSW,do_io_irq

/*
 * Machine check handler routines
 */
SYM_CODE_START(mcck_int_handler)
	BPOFF
	GET_LC	%r13
	lmg	%r8,%r9,__LC_MCK_OLD_PSW(%r13)
	TSTMSK	__LC_MCCK_CODE(%r13),MCCK_CODE_SYSTEM_DAMAGE
	jo	.Lmcck_panic		# yes -> rest of mcck code invalid
	TSTMSK	__LC_MCCK_CODE(%r13),MCCK_CODE_CR_VALID
	jno	.Lmcck_panic		# control registers invalid -> panic
	ptlb
	lay	%r14,__LC_CPU_TIMER_SAVE_AREA(%r13)
	mvc	__LC_MCCK_ENTER_TIMER(8,%r13),0(%r14)
	TSTMSK	__LC_MCCK_CODE(%r13),MCCK_CODE_CPU_TIMER_VALID
	jo	3f
	la	%r14,__LC_SYS_ENTER_TIMER(%r13)
	clc	0(8,%r14),__LC_EXIT_TIMER(%r13)
	jl	1f
	la	%r14,__LC_EXIT_TIMER(%r13)
1:	clc	0(8,%r14),__LC_LAST_UPDATE_TIMER(%r13)
	jl	2f
	la	%r14,__LC_LAST_UPDATE_TIMER(%r13)
2:	spt	0(%r14)
	mvc	__LC_MCCK_ENTER_TIMER(8,%r13),0(%r14)
3:	TSTMSK	__LC_MCCK_CODE(%r13),MCCK_CODE_PSW_MWP_VALID
	jno	.Lmcck_panic
	tmhh	%r8,0x0001		# interrupting from user ?
	jnz	.Lmcck_user
	TSTMSK	__LC_MCCK_CODE(%r13),MCCK_CODE_PSW_IA_VALID
	jno	.Lmcck_panic
#if IS_ENABLED(CONFIG_KVM)
	lg	%r10,__LC_CURRENT(%r13)
	tm	__TI_sie(%r10),0xff
	jz	.Lmcck_user
	# Need to compare the address instead of __TI_SIE flag.
	# Otherwise there would be a race between setting the flag
	# and entering SIE (or leaving and clearing the flag). This
	# would cause machine checks targeted at the guest to be
	# handled by the host.
	larl	%r14,.Lsie_entry
	clgrjl	%r9,%r14, 4f
	larl	%r14,.Lsie_leave
	clgrjhe	%r9,%r14, 4f
	lg	%r10,__LC_PCPU
	oi	__PCPU_FLAGS+7(%r10), _CIF_MCCK_GUEST
4:	BPENTER	__SF_SIE_FLAGS(%r15),_TIF_ISOLATE_BP_GUEST
	SIEEXIT __SF_SIE_CONTROL(%r15),%r13
#endif
.Lmcck_user:
	lg	%r15,__LC_MCCK_STACK(%r13)
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	stctg	%c1,%c1,__PT_CR1(%r11)
	lctlg	%c1,%c1,__LC_KERNEL_ASCE(%r13)
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lay	%r14,__LC_GPREGS_SAVE_AREA(%r13)
	mvc	__PT_R0(128,%r11),0(%r14)
	# clear user controlled registers to prevent speculative use
	xgr	%r0,%r0
	xgr	%r1,%r1
	xgr	%r3,%r3
	xgr	%r4,%r4
	xgr	%r5,%r5
	xgr	%r6,%r6
	xgr	%r7,%r7
	xgr	%r10,%r10
	stmg	%r8,%r9,__PT_PSW(%r11)
	xc	__PT_FLAGS(8,%r11),__PT_FLAGS(%r11)
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,s390_do_machine_check
	lctlg	%c1,%c1,__PT_CR1(%r11)
	lmg	%r0,%r10,__PT_R0(%r11)
	mvc	__LC_RETURN_MCCK_PSW(16,%r13),__PT_PSW(%r11) # move return PSW
	tm	__LC_RETURN_MCCK_PSW+1(%r13),0x01 # returning to user ?
	jno	0f
	BPON
	stpt	__LC_EXIT_TIMER(%r13)
0:	ALTERNATIVE "brcl 0,0", __stringify(lay %r12,__LC_LAST_BREAK_SAVE_AREA(%r13)),\
		ALT_FACILITY(193)
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
	larl	%r7,stop_lock
	cs	%r5,%r6,0(%r7)		# single CPU-stopper only
	jnz	4f
	larl	%r7,this_cpu
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
SYM_CODE_END(mcck_int_handler)

SYM_CODE_START(restart_int_handler)
	ALTERNATIVE "nop", "lpp _LPP_OFFSET", ALT_FACILITY(40)
	stg	%r15,__LC_SAVE_AREA_RESTART
	TSTMSK	__LC_RESTART_FLAGS,RESTART_FLAG_CTLREGS,4
	jz	0f
	lctlg	%c0,%c15,__LC_CREGS_SAVE_AREA
0:	larl	%r15,daton_psw
	lpswe	0(%r15)				# turn dat on, keep irqs off
.Ldaton:
	GET_LC	%r15
	lg	%r15,__LC_RESTART_STACK(%r15)
	xc	STACK_FRAME_OVERHEAD(__PT_SIZE,%r15),STACK_FRAME_OVERHEAD(%r15)
	stmg	%r0,%r14,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	GET_LC	%r13
	mvc	STACK_FRAME_OVERHEAD+__PT_R15(8,%r15),__LC_SAVE_AREA_RESTART(%r13)
	mvc	STACK_FRAME_OVERHEAD+__PT_PSW(16,%r15),__LC_RST_OLD_PSW(%r13)
	xc	0(STACK_FRAME_OVERHEAD,%r15),0(%r15)
	lg	%r1,__LC_RESTART_FN(%r13)	# load fn, parm & source cpu
	lg	%r2,__LC_RESTART_DATA(%r13)
	lgf	%r3,__LC_RESTART_SOURCE(%r13)
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
SYM_CODE_END(restart_int_handler)

SYM_CODE_START(early_pgm_check_handler)
	STMG_LC %r8,%r15,__LC_SAVE_AREA_SYNC
	GET_LC	%r13
	aghi	%r15,-(STACK_FRAME_OVERHEAD+__PT_SIZE)
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	stmg	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_PSW(16,%r11),__LC_PGM_OLD_PSW(%r13)
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_SYNC(%r13)
	lgr	%r2,%r11
	brasl	%r14,__do_early_pgm_check
	mvc	__LC_RETURN_PSW(16,%r13),STACK_FRAME_OVERHEAD+__PT_PSW(%r15)
	lmg	%r0,%r15,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	LPSWEY	__LC_RETURN_PSW,__LC_RETURN_LPSWE
SYM_CODE_END(early_pgm_check_handler)

	.section .kprobes.text, "ax"

#if defined(CONFIG_CHECK_STACK) || defined(CONFIG_VMAP_STACK)
/*
 * The synchronous or the asynchronous stack overflowed. We are dead.
 * No need to properly save the registers, we are going to panic anyway.
 * Setup a pt_regs so that show_trace can provide a good call trace.
 */
SYM_CODE_START(stack_overflow)
	GET_LC	%r15
	lg	%r15,__LC_NODAT_STACK(%r15) # change to panic stack
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	stmg	%r0,%r7,__PT_R0(%r11)
	stmg	%r8,%r9,__PT_PSW(%r11)
	mvc	__PT_R8(64,%r11),0(%r14)
	stg	%r10,__PT_ORIG_GPR2(%r11) # store last break to orig_gpr2
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lgr	%r2,%r11		# pass pointer to pt_regs
	jg	kernel_stack_overflow
SYM_CODE_END(stack_overflow)
#endif

	.section .data, "aw"
	.balign	4
SYM_DATA_LOCAL(stop_lock,	.long 0)
SYM_DATA_LOCAL(this_cpu,	.short 0)
	.balign	8
SYM_DATA_START_LOCAL(daton_psw)
	.quad	PSW_KERNEL_BITS
	.quad	.Ldaton
SYM_DATA_END(daton_psw)

	.section .rodata, "a"
	.balign	8
#define SYSCALL(esame,emu)	.quad __s390x_ ## esame
SYM_DATA_START(sys_call_table)
#include "asm/syscall_table.h"
SYM_DATA_END(sys_call_table)
#undef SYSCALL

#ifdef CONFIG_COMPAT

#define SYSCALL(esame,emu)	.quad __s390_ ## emu
SYM_DATA_START(sys_call_table_emu)
#include "asm/syscall_table.h"
SYM_DATA_END(sys_call_table_emu)
#undef SYSCALL
#endif
