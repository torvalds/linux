/*
 *  arch/s390/kernel/entry.S
 *    S390 low-level entry points.
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Hartmut Penner (hp@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <linux/config.h>
#include <asm/cache.h>
#include <asm/lowcore.h>
#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/offsets.h>
#include <asm/unistd.h>
#include <asm/page.h>

/*
 * Stack layout for the system_call stack entry.
 * The first few entries are identical to the user_regs_struct.
 */
SP_PTREGS    =  STACK_FRAME_OVERHEAD
SP_ARGS      =  STACK_FRAME_OVERHEAD + __PT_ARGS
SP_PSW       =  STACK_FRAME_OVERHEAD + __PT_PSW
SP_R0        =  STACK_FRAME_OVERHEAD + __PT_GPRS
SP_R1        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 4
SP_R2        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 8
SP_R3        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 12
SP_R4        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 16
SP_R5        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 20
SP_R6        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 24
SP_R7        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 28
SP_R8        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 32
SP_R9        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 36
SP_R10       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 40
SP_R11       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 44
SP_R12       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 48
SP_R13       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 52
SP_R14       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 56
SP_R15       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 60
SP_ORIG_R2   =  STACK_FRAME_OVERHEAD + __PT_ORIG_GPR2
SP_ILC       =  STACK_FRAME_OVERHEAD + __PT_ILC
SP_TRAP      =  STACK_FRAME_OVERHEAD + __PT_TRAP
SP_SIZE      =  STACK_FRAME_OVERHEAD + __PT_SIZE

_TIF_WORK_SVC = (_TIF_SIGPENDING | _TIF_NEED_RESCHED | \
		 _TIF_RESTART_SVC | _TIF_SINGLE_STEP )
_TIF_WORK_INT = (_TIF_SIGPENDING | _TIF_NEED_RESCHED)

STACK_SHIFT = PAGE_SHIFT + THREAD_ORDER
STACK_SIZE  = 1 << STACK_SHIFT

#define BASED(name) name-system_call(%r13)

/*
 * Register usage in interrupt handlers:
 *    R9  - pointer to current task structure
 *    R13 - pointer to literal pool
 *    R14 - return register for function calls
 *    R15 - kernel stack pointer
 */

	.macro  STORE_TIMER lc_offset
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	stpt	\lc_offset
#endif
	.endm

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	.macro  UPDATE_VTIME lc_from,lc_to,lc_sum
	lm	%r10,%r11,\lc_from
	sl	%r10,\lc_to
	sl	%r11,\lc_to+4
	bc	3,BASED(0f)
	sl	%r10,BASED(.Lc_1)
0:	al	%r10,\lc_sum
	al	%r11,\lc_sum+4
	bc	12,BASED(1f)
	al	%r10,BASED(.Lc_1)
1:	stm	%r10,%r11,\lc_sum
	.endm
#endif

	.macro	SAVE_ALL_BASE savearea
	stm	%r12,%r15,\savearea
	l	%r13,__LC_SVC_NEW_PSW+4	# load &system_call to %r13
	.endm

	.macro	SAVE_ALL psworg,savearea,sync
	la	%r12,\psworg
	.if	\sync
	tm	\psworg+1,0x01		# test problem state bit
	bz	BASED(2f)		# skip stack setup save
	l	%r15,__LC_KERNEL_STACK	# problem state -> load ksp
	.else
	tm	\psworg+1,0x01		# test problem state bit
	bnz	BASED(1f)		# from user -> load async stack
	clc	\psworg+4(4),BASED(.Lcritical_end)
	bhe	BASED(0f)
	clc	\psworg+4(4),BASED(.Lcritical_start)
	bl	BASED(0f)
	l	%r14,BASED(.Lcleanup_critical)
	basr	%r14,%r14
	tm	0(%r12),0x01		# retest problem state after cleanup
	bnz	BASED(1f)
0:	l	%r14,__LC_ASYNC_STACK	# are we already on the async stack ?
	slr	%r14,%r15
	sra	%r14,STACK_SHIFT
	be	BASED(2f)
1:	l	%r15,__LC_ASYNC_STACK
	.endif
#ifdef CONFIG_CHECK_STACK
	b	BASED(3f)
2:	tml	%r15,STACK_SIZE - CONFIG_STACK_GUARD
	bz	BASED(stack_overflow)
3:
#endif
2:	s	%r15,BASED(.Lc_spsize)	# make room for registers & psw
	mvc	SP_PSW(8,%r15),0(%r12)	# move user PSW to stack
	la	%r12,\psworg
	st	%r2,SP_ORIG_R2(%r15)	# store original content of gpr 2
	icm	%r12,12,__LC_SVC_ILC
	stm	%r0,%r11,SP_R0(%r15)	# store gprs %r0-%r11 to kernel stack
	st	%r12,SP_ILC(%r15)
	mvc	SP_R12(16,%r15),\savearea # move %r12-%r15 to stack
	la	%r12,0
	st	%r12,__SF_BACKCHAIN(%r15)	# clear back chain
	.endm

	.macro  RESTORE_ALL sync
	mvc	__LC_RETURN_PSW(8),SP_PSW(%r15) # move user PSW to lowcore
	.if !\sync
	ni	__LC_RETURN_PSW+1,0xfd	# clear wait state bit
	.endif
	lm	%r0,%r15,SP_R0(%r15)	# load gprs 0-15 of user
	STORE_TIMER __LC_EXIT_TIMER
	lpsw	__LC_RETURN_PSW		# back to caller
	.endm

/*
 * Scheduler resume function, called by switch_to
 *  gpr2 = (task_struct *) prev
 *  gpr3 = (task_struct *) next
 * Returns:
 *  gpr2 = prev
 */
        .globl  __switch_to
__switch_to:
        basr    %r1,0
__switch_to_base:
	tm	__THREAD_per(%r3),0xe8		# new process is using per ?
	bz	__switch_to_noper-__switch_to_base(%r1)	# if not we're fine
        stctl   %c9,%c11,__SF_EMPTY(%r15)	# We are using per stuff
        clc     __THREAD_per(12,%r3),__SF_EMPTY(%r15)
        be      __switch_to_noper-__switch_to_base(%r1)	# we got away w/o bashing TLB's
        lctl    %c9,%c11,__THREAD_per(%r3)	# Nope we didn't
__switch_to_noper:
        stm     %r6,%r15,__SF_GPRS(%r15)# store __switch_to registers of prev task
	st	%r15,__THREAD_ksp(%r2)	# store kernel stack to prev->tss.ksp
	l	%r15,__THREAD_ksp(%r3)	# load kernel stack from next->tss.ksp
	lm	%r6,%r15,__SF_GPRS(%r15)# load __switch_to registers of next task
	st	%r3,__LC_CURRENT	# __LC_CURRENT = current task struct
	lctl	%c4,%c4,__TASK_pid(%r3) # load pid to control reg. 4
	l	%r3,__THREAD_info(%r3)  # load thread_info from task struct
	st	%r3,__LC_THREAD_INFO
	ahi	%r3,STACK_SIZE
	st	%r3,__LC_KERNEL_STACK	# __LC_KERNEL_STACK = new kernel stack
	br	%r14

__critical_start:
/*
 * SVC interrupt handler routine. System calls are synchronous events and
 * are executed with interrupts enabled.
 */

	.globl  system_call
system_call:
	STORE_TIMER __LC_SYNC_ENTER_TIMER
sysc_saveall:
	SAVE_ALL_BASE __LC_SAVE_AREA
        SAVE_ALL __LC_SVC_OLD_PSW,__LC_SAVE_AREA,1
	lh	%r7,0x8a	  # get svc number from lowcore
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
sysc_vtime:
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	bz	BASED(sysc_do_svc)
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_SYNC_ENTER_TIMER,__LC_USER_TIMER
sysc_stime:
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
sysc_update:
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_SYNC_ENTER_TIMER
#endif
sysc_do_svc:
	l	%r9,__LC_THREAD_INFO	# load pointer to thread_info struct
	sla	%r7,2             # *4 and test for svc 0
	bnz	BASED(sysc_nr_ok) # svc number > 0
	# svc 0: system call number in %r1
	cl	%r1,BASED(.Lnr_syscalls)
	bnl	BASED(sysc_nr_ok)
	lr	%r7,%r1           # copy svc number to %r7
	sla	%r7,2             # *4
sysc_nr_ok:
	mvc	SP_ARGS(4,%r15),SP_R7(%r15)
sysc_do_restart:
	tm	__TI_flags+3(%r9),(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT)
        l       %r8,sys_call_table-system_call(%r7,%r13) # get system call addr.
        bnz     BASED(sysc_tracesys)
        basr    %r14,%r8          # call sys_xxxx
        st      %r2,SP_R2(%r15)   # store return value (change R2 on stack)
                                  # ATTENTION: check sys_execve_glue before
                                  # changing anything here !!

sysc_return:
	tm	SP_PSW+1(%r15),0x01	# returning to user ?
	bno	BASED(sysc_leave)
	tm	__TI_flags+3(%r9),_TIF_WORK_SVC
	bnz	BASED(sysc_work)  # there is work to do (signals etc.)
sysc_leave:
        RESTORE_ALL 1

#
# recheck if there is more work to do
#
sysc_work_loop:
	tm	__TI_flags+3(%r9),_TIF_WORK_SVC
	bz	BASED(sysc_leave)      # there is no work to do
#
# One of the work bits is on. Find out which one.
#
sysc_work:
	tm	__TI_flags+3(%r9),_TIF_NEED_RESCHED
	bo	BASED(sysc_reschedule)
	tm	__TI_flags+3(%r9),_TIF_SIGPENDING
	bo	BASED(sysc_sigpending)
	tm	__TI_flags+3(%r9),_TIF_RESTART_SVC
	bo	BASED(sysc_restart)
	tm	__TI_flags+3(%r9),_TIF_SINGLE_STEP
	bo	BASED(sysc_singlestep)
	b	BASED(sysc_leave)

#
# _TIF_NEED_RESCHED is set, call schedule
#	
sysc_reschedule:        
        l       %r1,BASED(.Lschedule)
	la      %r14,BASED(sysc_work_loop)
	br      %r1		       # call scheduler

#
# _TIF_SIGPENDING is set, call do_signal
#
sysc_sigpending:     
	ni	__TI_flags+3(%r9),255-_TIF_SINGLE_STEP # clear TIF_SINGLE_STEP
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        sr      %r3,%r3                # clear *oldset
        l       %r1,BASED(.Ldo_signal)
	basr	%r14,%r1               # call do_signal
	tm	__TI_flags+3(%r9),_TIF_RESTART_SVC
	bo	BASED(sysc_restart)
	tm	__TI_flags+3(%r9),_TIF_SINGLE_STEP
	bo	BASED(sysc_singlestep)
	b	BASED(sysc_leave)      # out of here, do NOT recheck

#
# _TIF_RESTART_SVC is set, set up registers and restart svc
#
sysc_restart:
	ni	__TI_flags+3(%r9),255-_TIF_RESTART_SVC # clear TIF_RESTART_SVC
	l	%r7,SP_R2(%r15)        # load new svc number
	sla	%r7,2
	mvc	SP_R2(4,%r15),SP_ORIG_R2(%r15) # restore first argument
	lm	%r2,%r6,SP_R2(%r15)    # load svc arguments
	b	BASED(sysc_do_restart) # restart svc

#
# _TIF_SINGLE_STEP is set, call do_single_step
#
sysc_singlestep:
	ni	__TI_flags+3(%r9),255-_TIF_SINGLE_STEP # clear TIF_SINGLE_STEP
	mvi	SP_TRAP+1(%r15),0x28	# set trap indication to pgm check
	la	%r2,SP_PTREGS(%r15)	# address of register-save area
	l	%r1,BASED(.Lhandle_per)	# load adr. of per handler
	la	%r14,BASED(sysc_return)	# load adr. of system return
	br	%r1			# branch to do_single_step

__critical_end:

#
# call trace before and after sys_call
#
sysc_tracesys:
        l       %r1,BASED(.Ltrace)
	la	%r2,SP_PTREGS(%r15)    # load pt_regs
	la	%r3,0
	srl	%r7,2
	st	%r7,SP_R2(%r15)
	basr	%r14,%r1
	clc	SP_R2(4,%r15),BASED(.Lnr_syscalls)
	bnl	BASED(sysc_tracenogo)
	l	%r7,SP_R2(%r15)        # strace might have changed the 
	sll	%r7,2                  #  system call
	l	%r8,sys_call_table-system_call(%r7,%r13)
sysc_tracego:
	lm	%r3,%r6,SP_R3(%r15)
	l	%r2,SP_ORIG_R2(%r15)
	basr	%r14,%r8          # call sys_xxx
	st	%r2,SP_R2(%r15)   # store return value
sysc_tracenogo:
	tm	__TI_flags+3(%r9),(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT)
        bz      BASED(sysc_return)
	l	%r1,BASED(.Ltrace)
	la	%r2,SP_PTREGS(%r15)    # load pt_regs
	la	%r3,1
	la	%r14,BASED(sysc_return)
	br	%r1

#
# a new process exits the kernel with ret_from_fork
#
        .globl  ret_from_fork
ret_from_fork:
	l	%r13,__LC_SVC_NEW_PSW+4
	l	%r9,__LC_THREAD_INFO	# load pointer to thread_info struct
	tm	SP_PSW+1(%r15),0x01	# forking a kernel thread ?
	bo	BASED(0f)
	st	%r15,SP_R15(%r15)	# store stack pointer for new kthread
0:	l       %r1,BASED(.Lschedtail)
	basr    %r14,%r1
        stosm   __SF_EMPTY(%r15),0x03     # reenable interrupts
	b	BASED(sysc_return)

#
# clone, fork, vfork, exec and sigreturn need glue,
# because they all expect pt_regs as parameter,
# but are called with different parameter.
# return-address is set up above
#
sys_clone_glue: 
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        l       %r1,BASED(.Lclone)
        br      %r1                   # branch to sys_clone

sys_fork_glue:  
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        l       %r1,BASED(.Lfork)
        br      %r1                   # branch to sys_fork

sys_vfork_glue: 
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        l       %r1,BASED(.Lvfork)
        br      %r1                   # branch to sys_vfork

sys_execve_glue:        
        la      %r2,SP_PTREGS(%r15)   # load pt_regs
        l       %r1,BASED(.Lexecve)
	lr      %r12,%r14             # save return address
        basr    %r14,%r1              # call sys_execve
        ltr     %r2,%r2               # check if execve failed
        bnz     0(%r12)               # it did fail -> store result in gpr2
        b       4(%r12)               # SKIP ST 2,SP_R2(15) after BASR 14,8
                                      # in system_call/sysc_tracesys

sys_sigreturn_glue:     
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as parameter
        l       %r1,BASED(.Lsigreturn)
        br      %r1                   # branch to sys_sigreturn

sys_rt_sigreturn_glue:     
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as parameter
        l       %r1,BASED(.Lrt_sigreturn)
        br      %r1                   # branch to sys_sigreturn

#
# sigsuspend and rt_sigsuspend need pt_regs as an additional
# parameter and they have to skip the store of %r2 into the
# user register %r2 because the return value was set in 
# sigsuspend and rt_sigsuspend already and must not be overwritten!
#

sys_sigsuspend_glue:    
        lr      %r5,%r4               # move mask back
        lr      %r4,%r3               # move history1 parameter
        lr      %r3,%r2               # move history0 parameter
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as first parameter
        l       %r1,BASED(.Lsigsuspend)
	la      %r14,4(%r14)          # skip store of return value
        br      %r1                   # branch to sys_sigsuspend

sys_rt_sigsuspend_glue: 
        lr      %r4,%r3               # move sigsetsize parameter
        lr      %r3,%r2               # move unewset parameter
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as first parameter
        l       %r1,BASED(.Lrt_sigsuspend)
	la      %r14,4(%r14)          # skip store of return value
        br      %r1                   # branch to sys_rt_sigsuspend

sys_sigaltstack_glue:
        la      %r4,SP_PTREGS(%r15)   # load pt_regs as parameter
        l       %r1,BASED(.Lsigaltstack)
        br      %r1                   # branch to sys_sigreturn


/*
 * Program check handler routine
 */

        .globl  pgm_check_handler
pgm_check_handler:
/*
 * First we need to check for a special case:
 * Single stepping an instruction that disables the PER event mask will
 * cause a PER event AFTER the mask has been set. Example: SVC or LPSW.
 * For a single stepped SVC the program check handler gets control after
 * the SVC new PSW has been loaded. But we want to execute the SVC first and
 * then handle the PER event. Therefore we update the SVC old PSW to point
 * to the pgm_check_handler and branch to the SVC handler after we checked
 * if we have to load the kernel stack register.
 * For every other possible cause for PER event without the PER mask set
 * we just ignore the PER event (FIXME: is there anything we have to do
 * for LPSW?).
 */
	STORE_TIMER __LC_SYNC_ENTER_TIMER
	SAVE_ALL_BASE __LC_SAVE_AREA
        tm      __LC_PGM_INT_CODE+1,0x80 # check whether we got a per exception
        bnz     BASED(pgm_per)           # got per exception -> special case
	SAVE_ALL __LC_PGM_OLD_PSW,__LC_SAVE_AREA,1
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	bz	BASED(pgm_no_vtime)
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_SYNC_ENTER_TIMER,__LC_USER_TIMER
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_SYNC_ENTER_TIMER
pgm_no_vtime:
#endif
	l	%r9,__LC_THREAD_INFO	# load pointer to thread_info struct
        l       %r3,__LC_PGM_ILC         # load program interruption code
	la	%r8,0x7f
	nr	%r8,%r3
pgm_do_call:
        l       %r7,BASED(.Ljump_table)
        sll     %r8,2
        l       %r7,0(%r8,%r7)		 # load address of handler routine
        la      %r2,SP_PTREGS(%r15)	 # address of register-save area
	la      %r14,BASED(sysc_return)
	br      %r7			 # branch to interrupt-handler

#
# handle per exception
#
pgm_per:
        tm      __LC_PGM_OLD_PSW,0x40    # test if per event recording is on
        bnz     BASED(pgm_per_std)       # ok, normal per event from user space
# ok its one of the special cases, now we need to find out which one
        clc     __LC_PGM_OLD_PSW(8),__LC_SVC_NEW_PSW
        be      BASED(pgm_svcper)
# no interesting special case, ignore PER event
        lm      %r12,%r15,__LC_SAVE_AREA
	lpsw    0x28

#
# Normal per exception
#
pgm_per_std:
	SAVE_ALL __LC_PGM_OLD_PSW,__LC_SAVE_AREA,1
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	bz	BASED(pgm_no_vtime2)
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_SYNC_ENTER_TIMER,__LC_USER_TIMER
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_SYNC_ENTER_TIMER
pgm_no_vtime2:
#endif
	l	%r9,__LC_THREAD_INFO	# load pointer to thread_info struct
	l	%r1,__TI_task(%r9)
	mvc	__THREAD_per+__PER_atmid(2,%r1),__LC_PER_ATMID
	mvc	__THREAD_per+__PER_address(4,%r1),__LC_PER_ADDRESS
	mvc	__THREAD_per+__PER_access_id(1,%r1),__LC_PER_ACCESS_ID
	oi	__TI_flags+3(%r9),_TIF_SINGLE_STEP # set TIF_SINGLE_STEP
	l	%r3,__LC_PGM_ILC	 # load program interruption code
	la	%r8,0x7f
	nr	%r8,%r3                  # clear per-event-bit and ilc
	be	BASED(sysc_return)       # only per or per+check ?
	b	BASED(pgm_do_call)

#
# it was a single stepped SVC that is causing all the trouble
#
pgm_svcper:
	SAVE_ALL __LC_SVC_OLD_PSW,__LC_SAVE_AREA,1
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	bz	BASED(pgm_no_vtime3)
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_SYNC_ENTER_TIMER,__LC_USER_TIMER
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_SYNC_ENTER_TIMER
pgm_no_vtime3:
#endif
	lh	%r7,0x8a		# get svc number from lowcore
	l	%r9,__LC_THREAD_INFO	# load pointer to thread_info struct
	l	%r1,__TI_task(%r9)
	mvc	__THREAD_per+__PER_atmid(2,%r1),__LC_PER_ATMID
	mvc	__THREAD_per+__PER_address(4,%r1),__LC_PER_ADDRESS
	mvc	__THREAD_per+__PER_access_id(1,%r1),__LC_PER_ACCESS_ID
	oi	__TI_flags+3(%r9),_TIF_SINGLE_STEP # set TIF_SINGLE_STEP
	stosm	__SF_EMPTY(%r15),0x03	# reenable interrupts
	b	BASED(sysc_do_svc)

/*
 * IO interrupt handler routine
 */

        .globl io_int_handler
io_int_handler:
	STORE_TIMER __LC_ASYNC_ENTER_TIMER
	stck	__LC_INT_CLOCK
	SAVE_ALL_BASE __LC_SAVE_AREA+16
        SAVE_ALL __LC_IO_OLD_PSW,__LC_SAVE_AREA+16,0
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	bz	BASED(io_no_vtime)
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_ASYNC_ENTER_TIMER,__LC_USER_TIMER
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_ASYNC_ENTER_TIMER
io_no_vtime:
#endif
	l	%r9,__LC_THREAD_INFO	# load pointer to thread_info struct
        l       %r1,BASED(.Ldo_IRQ)        # load address of do_IRQ
        la      %r2,SP_PTREGS(%r15) # address of register-save area
        basr    %r14,%r1          # branch to standard irq handler

io_return:
        tm      SP_PSW+1(%r15),0x01    # returning to user ?
#ifdef CONFIG_PREEMPT
	bno     BASED(io_preempt)      # no -> check for preemptive scheduling
#else
        bno     BASED(io_leave)        # no-> skip resched & signal
#endif
	tm	__TI_flags+3(%r9),_TIF_WORK_INT
	bnz	BASED(io_work)         # there is work to do (signals etc.)
io_leave:
        RESTORE_ALL 0

#ifdef CONFIG_PREEMPT
io_preempt:
	icm	%r0,15,__TI_precount(%r9)
	bnz     BASED(io_leave)
	l	%r1,SP_R15(%r15)
	s	%r1,BASED(.Lc_spsize)
	mvc	SP_PTREGS(__PT_SIZE,%r1),SP_PTREGS(%r15)
        xc      __SF_BACKCHAIN(4,%r1),__SF_BACKCHAIN(%r1) # clear back chain
	lr	%r15,%r1
io_resume_loop:
	tm	__TI_flags+3(%r9),_TIF_NEED_RESCHED
	bno	BASED(io_leave)
	mvc     __TI_precount(4,%r9),BASED(.Lc_pactive)
        stosm   __SF_EMPTY(%r15),0x03  # reenable interrupts
        l       %r1,BASED(.Lschedule)
	basr	%r14,%r1	       # call schedule
        stnsm   __SF_EMPTY(%r15),0xfc  # disable I/O and ext. interrupts
	xc      __TI_precount(4,%r9),__TI_precount(%r9)
	b	BASED(io_resume_loop)
#endif

#
# switch to kernel stack, then check the TIF bits
#
io_work:
	l	%r1,__LC_KERNEL_STACK
	s	%r1,BASED(.Lc_spsize)
	mvc	SP_PTREGS(__PT_SIZE,%r1),SP_PTREGS(%r15)
        xc      __SF_BACKCHAIN(4,%r1),__SF_BACKCHAIN(%r1) # clear back chain
	lr	%r15,%r1
#
# One of the work bits is on. Find out which one.
# Checked are: _TIF_SIGPENDING and _TIF_NEED_RESCHED
#
io_work_loop:
	tm	__TI_flags+3(%r9),_TIF_NEED_RESCHED
	bo	BASED(io_reschedule)
	tm	__TI_flags+3(%r9),_TIF_SIGPENDING
	bo	BASED(io_sigpending)
	b	BASED(io_leave)

#
# _TIF_NEED_RESCHED is set, call schedule
#	
io_reschedule:        
        l       %r1,BASED(.Lschedule)
        stosm   __SF_EMPTY(%r15),0x03  # reenable interrupts
	basr    %r14,%r1	       # call scheduler
        stnsm   __SF_EMPTY(%r15),0xfc  # disable I/O and ext. interrupts
	tm	__TI_flags+3(%r9),_TIF_WORK_INT
	bz	BASED(io_leave)        # there is no work to do
	b	BASED(io_work_loop)

#
# _TIF_SIGPENDING is set, call do_signal
#
io_sigpending:     
        stosm   __SF_EMPTY(%r15),0x03  # reenable interrupts
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        sr      %r3,%r3                # clear *oldset
        l       %r1,BASED(.Ldo_signal)
	basr    %r14,%r1	       # call do_signal
        stnsm   __SF_EMPTY(%r15),0xfc  # disable I/O and ext. interrupts
	b	BASED(io_leave)        # out of here, do NOT recheck

/*
 * External interrupt handler routine
 */

        .globl  ext_int_handler
ext_int_handler:
	STORE_TIMER __LC_ASYNC_ENTER_TIMER
	stck	__LC_INT_CLOCK
	SAVE_ALL_BASE __LC_SAVE_AREA+16
        SAVE_ALL __LC_EXT_OLD_PSW,__LC_SAVE_AREA+16,0
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	bz	BASED(ext_no_vtime)
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_ASYNC_ENTER_TIMER,__LC_USER_TIMER
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_ASYNC_ENTER_TIMER
ext_no_vtime:
#endif
	l	%r9,__LC_THREAD_INFO	# load pointer to thread_info struct
	la	%r2,SP_PTREGS(%r15)    # address of register-save area
	lh	%r3,__LC_EXT_INT_CODE  # get interruption code
	l	%r1,BASED(.Ldo_extint)
	basr	%r14,%r1
	b	BASED(io_return)

/*
 * Machine check handler routines
 */

        .globl mcck_int_handler
mcck_int_handler:
	STORE_TIMER __LC_ASYNC_ENTER_TIMER
	SAVE_ALL_BASE __LC_SAVE_AREA+32
        SAVE_ALL __LC_MCK_OLD_PSW,__LC_SAVE_AREA+32,0
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	bz	BASED(mcck_no_vtime)
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_ASYNC_ENTER_TIMER,__LC_USER_TIMER
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_ASYNC_ENTER_TIMER
mcck_no_vtime:
#endif
	l       %r1,BASED(.Ls390_mcck)
	basr    %r14,%r1	  # call machine check handler
mcck_return:
        RESTORE_ALL 0

#ifdef CONFIG_SMP
/*
 * Restart interruption handler, kick starter for additional CPUs
 */
        .globl restart_int_handler
restart_int_handler:
        l       %r15,__LC_SAVE_AREA+60 # load ksp
        lctl    %c0,%c15,__LC_CREGS_SAVE_AREA # get new ctl regs
        lam     %a0,%a15,__LC_AREGS_SAVE_AREA
        lm      %r6,%r15,__SF_GPRS(%r15) # load registers from clone
        stosm   __SF_EMPTY(%r15),0x04    # now we can turn dat on
        basr    %r14,0
        l       %r14,restart_addr-.(%r14)
        br      %r14                   # branch to start_secondary
restart_addr:
        .long   start_secondary
#else
/*
 * If we do not run with SMP enabled, let the new CPU crash ...
 */
        .globl restart_int_handler
restart_int_handler:
        basr    %r1,0
restart_base:
        lpsw    restart_crash-restart_base(%r1)
        .align 8
restart_crash:
        .long  0x000a0000,0x00000000
restart_go:
#endif

#ifdef CONFIG_CHECK_STACK
/*
 * The synchronous or the asynchronous stack overflowed. We are dead.
 * No need to properly save the registers, we are going to panic anyway.
 * Setup a pt_regs so that show_trace can provide a good call trace.
 */
stack_overflow:
	l	%r15,__LC_PANIC_STACK	# change to panic stack
	sl	%r15,BASED(.Lc_spsize)
	mvc	SP_PSW(8,%r15),0(%r12)	# move user PSW to stack
	stm	%r0,%r11,SP_R0(%r15)	# store gprs %r0-%r11 to kernel stack
	la	%r1,__LC_SAVE_AREA
	ch	%r12,BASED(.L0x020)	# old psw addr == __LC_SVC_OLD_PSW ?
	be	BASED(0f)
	ch	%r12,BASED(.L0x028)	# old psw addr == __LC_PGM_OLD_PSW ?
	be	BASED(0f)
	la	%r1,__LC_SAVE_AREA+16
0:	mvc	SP_R12(16,%r15),0(%r1)	# move %r12-%r15 to stack
        xc      __SF_BACKCHAIN(4,%r15),__SF_BACKCHAIN(%r15) # clear back chain
	l	%r1,BASED(1f)		# branch to kernel_stack_overflow
        la      %r2,SP_PTREGS(%r15)	# load pt_regs
	br	%r1
1:	.long  kernel_stack_overflow
#endif

cleanup_table_system_call:
	.long	system_call + 0x80000000, sysc_do_svc + 0x80000000
cleanup_table_sysc_return:
	.long	sysc_return + 0x80000000, sysc_leave + 0x80000000
cleanup_table_sysc_leave:
	.long	sysc_leave + 0x80000000, sysc_work_loop + 0x80000000
cleanup_table_sysc_work_loop:
	.long	sysc_work_loop + 0x80000000, sysc_reschedule + 0x80000000

cleanup_critical:
	clc	4(4,%r12),BASED(cleanup_table_system_call)
	bl	BASED(0f)
	clc	4(4,%r12),BASED(cleanup_table_system_call+4)
	bl	BASED(cleanup_system_call)
0:
	clc	4(4,%r12),BASED(cleanup_table_sysc_return)
	bl	BASED(0f)
	clc	4(4,%r12),BASED(cleanup_table_sysc_return+4)
	bl	BASED(cleanup_sysc_return)
0:
	clc	4(4,%r12),BASED(cleanup_table_sysc_leave)
	bl	BASED(0f)
	clc	4(4,%r12),BASED(cleanup_table_sysc_leave+4)
	bl	BASED(cleanup_sysc_leave)
0:
	clc	4(4,%r12),BASED(cleanup_table_sysc_work_loop)
	bl	BASED(0f)
	clc	4(4,%r12),BASED(cleanup_table_sysc_work_loop+4)
	bl	BASED(cleanup_sysc_leave)
0:
	br	%r14

cleanup_system_call:
	mvc	__LC_RETURN_PSW(8),0(%r12)
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	clc	__LC_RETURN_PSW+4(4),BASED(cleanup_system_call_insn+4)
	bh	BASED(0f)
	mvc	__LC_SYNC_ENTER_TIMER(8),__LC_ASYNC_ENTER_TIMER
0:	clc	__LC_RETURN_PSW+4(4),BASED(cleanup_system_call_insn+8)
	bhe	BASED(cleanup_vtime)
#endif
	clc	__LC_RETURN_PSW+4(4),BASED(cleanup_system_call_insn)
	bh	BASED(0f)
	mvc	__LC_SAVE_AREA(16),__LC_SAVE_AREA+16
0:	st	%r13,__LC_SAVE_AREA+20
	SAVE_ALL __LC_SVC_OLD_PSW,__LC_SAVE_AREA,1
	st	%r15,__LC_SAVE_AREA+28
	lh	%r7,0x8a
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
cleanup_vtime:
	clc	__LC_RETURN_PSW+4(4),BASED(cleanup_system_call_insn+12)
	bhe	BASED(cleanup_stime)
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	bz	BASED(cleanup_novtime)
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_SYNC_ENTER_TIMER,__LC_USER_TIMER
cleanup_stime:
	clc	__LC_RETURN_PSW+4(4),BASED(cleanup_system_call_insn+16)
	bh	BASED(cleanup_update)
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
cleanup_update:
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_SYNC_ENTER_TIMER
cleanup_novtime:
#endif
	mvc	__LC_RETURN_PSW+4(4),BASED(cleanup_table_system_call+4)
	la	%r12,__LC_RETURN_PSW
	br	%r14
cleanup_system_call_insn:
	.long	sysc_saveall + 0x80000000
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	.long   system_call + 0x80000000
	.long   sysc_vtime + 0x80000000
	.long   sysc_stime + 0x80000000
	.long   sysc_update + 0x80000000
#endif

cleanup_sysc_return:
	mvc	__LC_RETURN_PSW(4),0(%r12)
	mvc	__LC_RETURN_PSW+4(4),BASED(cleanup_table_sysc_return)
	la	%r12,__LC_RETURN_PSW
	br	%r14

cleanup_sysc_leave:
	clc	4(4,%r12),BASED(cleanup_sysc_leave_insn)
	be	BASED(0f)
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	mvc	__LC_EXIT_TIMER(8),__LC_ASYNC_ENTER_TIMER
	clc	4(4,%r12),BASED(cleanup_sysc_leave_insn+4)
	be	BASED(0f)
#endif
	mvc	__LC_RETURN_PSW(8),SP_PSW(%r15)
	mvc	__LC_SAVE_AREA+16(16),SP_R12(%r15)
	lm	%r0,%r11,SP_R0(%r15)
	l	%r15,SP_R15(%r15)
0:	la	%r12,__LC_RETURN_PSW
	br	%r14
cleanup_sysc_leave_insn:
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	.long	sysc_leave + 14 + 0x80000000
#endif
	.long	sysc_leave + 10 + 0x80000000

/*
 * Integer constants
 */
               .align 4
.Lc_spsize:    .long  SP_SIZE
.Lc_overhead:  .long  STACK_FRAME_OVERHEAD
.Lc_pactive:   .long  PREEMPT_ACTIVE
.Lnr_syscalls: .long  NR_syscalls
.L0x018:       .short 0x018
.L0x020:       .short 0x020
.L0x028:       .short 0x028
.L0x030:       .short 0x030
.L0x038:       .short 0x038
.Lc_1:         .long  1

/*
 * Symbol constants
 */
.Ls390_mcck:   .long  s390_do_machine_check
.Ldo_IRQ:      .long  do_IRQ
.Ldo_extint:   .long  do_extint
.Ldo_signal:   .long  do_signal
.Lhandle_per:  .long  do_single_step
.Ljump_table:  .long  pgm_check_table
.Lschedule:    .long  schedule
.Lclone:       .long  sys_clone
.Lexecve:      .long  sys_execve
.Lfork:        .long  sys_fork
.Lrt_sigreturn:.long  sys_rt_sigreturn
.Lrt_sigsuspend:
               .long  sys_rt_sigsuspend
.Lsigreturn:   .long  sys_sigreturn
.Lsigsuspend:  .long  sys_sigsuspend
.Lsigaltstack: .long  sys_sigaltstack
.Ltrace:       .long  syscall_trace
.Lvfork:       .long  sys_vfork
.Lschedtail:   .long  schedule_tail

.Lcritical_start:
               .long  __critical_start + 0x80000000
.Lcritical_end:
               .long  __critical_end + 0x80000000
.Lcleanup_critical:
               .long  cleanup_critical

#define SYSCALL(esa,esame,emu)	.long esa
	.globl  sys_call_table
sys_call_table:
#include "syscalls.S"
#undef SYSCALL

