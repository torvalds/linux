/*
 * This file contains the light-weight system call handlers (fsyscall-handlers).
 *
 * Copyright (C) 2003 Hewlett-Packard Co
 * 	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 25-Sep-03 davidm	Implement fsys_rt_sigprocmask().
 * 18-Feb-03 louisk	Implement fsys_gettimeofday().
 * 28-Feb-03 davidm	Fixed several bugs in fsys_gettimeofday().  Tuned it some more,
 *			probably broke it along the way... ;-)
 * 13-Jul-04 clameter   Implement fsys_clock_gettime and revise fsys_gettimeofday to make
 *                      it capable of using memory based clocks without falling back to C code.
 */

#include <asm/asmmacro.h>
#include <asm/errno.h>
#include <asm/asm-offsets.h>
#include <asm/percpu.h>
#include <asm/thread_info.h>
#include <asm/sal.h>
#include <asm/signal.h>
#include <asm/system.h>
#include <asm/unistd.h>

#include "entry.h"

/*
 * See Documentation/ia64/fsys.txt for details on fsyscalls.
 *
 * On entry to an fsyscall handler:
 *   r10	= 0 (i.e., defaults to "successful syscall return")
 *   r11	= saved ar.pfs (a user-level value)
 *   r15	= system call number
 *   r16	= "current" task pointer (in normal kernel-mode, this is in r13)
 *   r32-r39	= system call arguments
 *   b6		= return address (a user-level value)
 *   ar.pfs	= previous frame-state (a user-level value)
 *   PSR.be	= cleared to zero (i.e., little-endian byte order is in effect)
 *   all other registers may contain values passed in from user-mode
 *
 * On return from an fsyscall handler:
 *   r11	= saved ar.pfs (as passed into the fsyscall handler)
 *   r15	= system call number (as passed into the fsyscall handler)
 *   r32-r39	= system call arguments (as passed into the fsyscall handler)
 *   b6		= return address (as passed into the fsyscall handler)
 *   ar.pfs	= previous frame-state (as passed into the fsyscall handler)
 */

ENTRY(fsys_ni_syscall)
	.prologue
	.altrp b6
	.body
	mov r8=ENOSYS
	mov r10=-1
	FSYS_RETURN
END(fsys_ni_syscall)

ENTRY(fsys_getpid)
	.prologue
	.altrp b6
	.body
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	;;
	ld4 r9=[r9]
	add r8=IA64_TASK_TGID_OFFSET,r16
	;;
	and r9=TIF_ALLWORK_MASK,r9
	ld4 r8=[r8]				// r8 = current->tgid
	;;
	cmp.ne p8,p0=0,r9
(p8)	br.spnt.many fsys_fallback_syscall
	FSYS_RETURN
END(fsys_getpid)

ENTRY(fsys_getppid)
	.prologue
	.altrp b6
	.body
	add r17=IA64_TASK_GROUP_LEADER_OFFSET,r16
	;;
	ld8 r17=[r17]				// r17 = current->group_leader
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	;;

	ld4 r9=[r9]
	add r17=IA64_TASK_REAL_PARENT_OFFSET,r17 // r17 = &current->group_leader->real_parent
	;;
	and r9=TIF_ALLWORK_MASK,r9

1:	ld8 r18=[r17]				// r18 = current->group_leader->real_parent
	;;
	cmp.ne p8,p0=0,r9
	add r8=IA64_TASK_TGID_OFFSET,r18	// r8 = &current->group_leader->real_parent->tgid
	;;

	/*
	 * The .acq is needed to ensure that the read of tgid has returned its data before
	 * we re-check "real_parent".
	 */
	ld4.acq r8=[r8]				// r8 = current->group_leader->real_parent->tgid
#ifdef CONFIG_SMP
	/*
	 * Re-read current->group_leader->real_parent.
	 */
	ld8 r19=[r17]				// r19 = current->group_leader->real_parent
(p8)	br.spnt.many fsys_fallback_syscall
	;;
	cmp.ne p6,p0=r18,r19			// did real_parent change?
	mov r19=0			// i must not leak kernel bits...
(p6)	br.cond.spnt.few 1b			// yes -> redo the read of tgid and the check
	;;
	mov r17=0			// i must not leak kernel bits...
	mov r18=0			// i must not leak kernel bits...
#else
	mov r17=0			// i must not leak kernel bits...
	mov r18=0			// i must not leak kernel bits...
	mov r19=0			// i must not leak kernel bits...
#endif
	FSYS_RETURN
END(fsys_getppid)

ENTRY(fsys_set_tid_address)
	.prologue
	.altrp b6
	.body
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	;;
	ld4 r9=[r9]
	tnat.z p6,p7=r32		// check argument register for being NaT
	;;
	and r9=TIF_ALLWORK_MASK,r9
	add r8=IA64_TASK_PID_OFFSET,r16
	add r18=IA64_TASK_CLEAR_CHILD_TID_OFFSET,r16
	;;
	ld4 r8=[r8]
	cmp.ne p8,p0=0,r9
	mov r17=-1
	;;
(p6)	st8 [r18]=r32
(p7)	st8 [r18]=r17
(p8)	br.spnt.many fsys_fallback_syscall
	;;
	mov r17=0			// i must not leak kernel bits...
	mov r18=0			// i must not leak kernel bits...
	FSYS_RETURN
END(fsys_set_tid_address)

/*
 * Ensure that the time interpolator structure is compatible with the asm code
 */
#if IA64_TIME_INTERPOLATOR_SOURCE_OFFSET !=0 || IA64_TIME_INTERPOLATOR_SHIFT_OFFSET != 2 \
	|| IA64_TIME_INTERPOLATOR_JITTER_OFFSET != 3 || IA64_TIME_INTERPOLATOR_NSEC_OFFSET != 4
#error fsys_gettimeofday incompatible with changes to struct time_interpolator
#endif
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_DIVIDE_BY_1000 0x4000
#define CLOCK_ADD_MONOTONIC 0x8000

ENTRY(fsys_gettimeofday)
	.prologue
	.altrp b6
	.body
	mov r31 = r32
	tnat.nz p6,p0 = r33		// guard against NaT argument
(p6)    br.cond.spnt.few .fail_einval
	mov r30 = CLOCK_DIVIDE_BY_1000
	;;
.gettime:
	// Register map
	// Incoming r31 = pointer to address where to place result
	//          r30 = flags determining how time is processed
	// r2,r3 = temp r4-r7 preserved
	// r8 = result nanoseconds
	// r9 = result seconds
	// r10 = temporary storage for clock difference
	// r11 = preserved: saved ar.pfs
	// r12 = preserved: memory stack
	// r13 = preserved: thread pointer
	// r14 = address of mask / mask
	// r15 = preserved: system call number
	// r16 = preserved: current task pointer
	// r17 = wall to monotonic use
	// r18 = time_interpolator->offset
	// r19 = address of wall_to_monotonic
	// r20 = pointer to struct time_interpolator / pointer to time_interpolator->address
	// r21 = shift factor
	// r22 = address of time interpolator->last_counter
	// r23 = address of time_interpolator->last_cycle
	// r24 = adress of time_interpolator->offset
	// r25 = last_cycle value
	// r26 = last_counter value
	// r27 = pointer to xtime
	// r28 = sequence number at the beginning of critcal section
	// r29 = address of seqlock
	// r30 = time processing flags / memory address
	// r31 = pointer to result
	// Predicates
	// p6,p7 short term use
	// p8 = timesource ar.itc
	// p9 = timesource mmio64
	// p10 = timesource mmio32
	// p11 = timesource not to be handled by asm code
	// p12 = memory time source ( = p9 | p10)
	// p13 = do cmpxchg with time_interpolator_last_cycle
	// p14 = Divide by 1000
	// p15 = Add monotonic
	//
	// Note that instructions are optimized for McKinley. McKinley can process two
	// bundles simultaneously and therefore we continuously try to feed the CPU
	// two bundles and then a stop.
	tnat.nz p6,p0 = r31	// branch deferred since it does not fit into bundle structure
	mov pr = r30,0xc000	// Set predicates according to function
	add r2 = TI_FLAGS+IA64_TASK_SIZE,r16
	movl r20 = time_interpolator
	;;
	ld8 r20 = [r20]		// get pointer to time_interpolator structure
	movl r29 = xtime_lock
	ld4 r2 = [r2]		// process work pending flags
	movl r27 = xtime
	;;	// only one bundle here
	ld8 r21 = [r20]		// first quad with control information
	and r2 = TIF_ALLWORK_MASK,r2
(p6)    br.cond.spnt.few .fail_einval	// deferred branch
	;;
	add r10 = IA64_TIME_INTERPOLATOR_ADDRESS_OFFSET,r20
	extr r3 = r21,32,32	// time_interpolator->nsec_per_cyc
	extr r8 = r21,0,16	// time_interpolator->source
	cmp.ne p6, p0 = 0, r2	// Fallback if work is scheduled
(p6)    br.cond.spnt.many fsys_fallback_syscall
	;;
	cmp.eq p8,p12 = 0,r8	// Check for cpu timer
	cmp.eq p9,p0 = 1,r8	// MMIO64 ?
	extr r2 = r21,24,8	// time_interpolator->jitter
	cmp.eq p10,p0 = 2,r8	// MMIO32 ?
	cmp.ltu p11,p0 = 2,r8	// function or other clock
(p11)	br.cond.spnt.many fsys_fallback_syscall
	;;
	setf.sig f7 = r3	// Setup for scaling of counter
(p15)	movl r19 = wall_to_monotonic
(p12)	ld8 r30 = [r10]
	cmp.ne p13,p0 = r2,r0	// need jitter compensation?
	extr r21 = r21,16,8	// shift factor
	;;
.time_redo:
	.pred.rel.mutex p8,p9,p10
	ld4.acq r28 = [r29]	// xtime_lock.sequence. Must come first for locking purposes
(p8)	mov r2 = ar.itc		// CPU_TIMER. 36 clocks latency!!!
	add r22 = IA64_TIME_INTERPOLATOR_LAST_COUNTER_OFFSET,r20
(p9)	ld8 r2 = [r30]		// readq(ti->address). Could also have latency issues..
(p10)	ld4 r2 = [r30]		// readw(ti->address)
(p13)	add r23 = IA64_TIME_INTERPOLATOR_LAST_CYCLE_OFFSET,r20
	;;			// could be removed by moving the last add upward
	ld8 r26 = [r22]		// time_interpolator->last_counter
(p13)	ld8 r25 = [r23]		// time interpolator->last_cycle
	add r24 = IA64_TIME_INTERPOLATOR_OFFSET_OFFSET,r20
(p15)	ld8 r17 = [r19],IA64_TIMESPEC_TV_NSEC_OFFSET
 	ld8 r9 = [r27],IA64_TIMESPEC_TV_NSEC_OFFSET
	add r14 = IA64_TIME_INTERPOLATOR_MASK_OFFSET, r20
	;;
	ld8 r18 = [r24]		// time_interpolator->offset
	ld8 r8 = [r27],-IA64_TIMESPEC_TV_NSEC_OFFSET	// xtime.tv_nsec
(p13)	sub r3 = r25,r2	// Diff needed before comparison (thanks davidm)
	;;
	ld8 r14 = [r14]		// time_interpolator->mask
(p13)	cmp.gt.unc p6,p7 = r3,r0	// check if it is less than last. p6,p7 cleared
	sub r10 = r2,r26	// current_counter - last_counter
	;;
(p6)	sub r10 = r25,r26	// time we got was less than last_cycle
(p7)	mov ar.ccv = r25	// more than last_cycle. Prep for cmpxchg
	;;
	and r10 = r10,r14	// Apply mask
	;;
	setf.sig f8 = r10
	nop.i 123
	;;
(p7)	cmpxchg8.rel r3 = [r23],r2,ar.ccv
EX(.fail_efault, probe.w.fault r31, 3)	// This takes 5 cycles and we have spare time
	xmpy.l f8 = f8,f7	// nsec_per_cyc*(counter-last_counter)
(p15)	add r9 = r9,r17		// Add wall to monotonic.secs to result secs
	;;
(p15)	ld8 r17 = [r19],-IA64_TIMESPEC_TV_NSEC_OFFSET
(p7)	cmp.ne p7,p0 = r25,r3	// if cmpxchg not successful redo
	// simulate tbit.nz.or p7,p0 = r28,0
	and r28 = ~1,r28	// Make sequence even to force retry if odd
	getf.sig r2 = f8
	mf
	add r8 = r8,r18		// Add time interpolator offset
	;;
	ld4 r10 = [r29]		// xtime_lock.sequence
(p15)	add r8 = r8, r17	// Add monotonic.nsecs to nsecs
	shr.u r2 = r2,r21
	;;		// overloaded 3 bundles!
	// End critical section.
	add r8 = r8,r2		// Add xtime.nsecs
	cmp4.ne.or p7,p0 = r28,r10
(p7)	br.cond.dpnt.few .time_redo	// sequence number changed ?
	// Now r8=tv->tv_nsec and r9=tv->tv_sec
	mov r10 = r0
	movl r2 = 1000000000
	add r23 = IA64_TIMESPEC_TV_NSEC_OFFSET, r31
(p14)	movl r3 = 2361183241434822607	// Prep for / 1000 hack
	;;
.time_normalize:
	mov r21 = r8
	cmp.ge p6,p0 = r8,r2
(p14)	shr.u r20 = r8, 3		// We can repeat this if necessary just wasting some time
	;;
(p14)	setf.sig f8 = r20
(p6)	sub r8 = r8,r2
(p6)	add r9 = 1,r9			// two nops before the branch.
(p14)	setf.sig f7 = r3		// Chances for repeats are 1 in 10000 for gettod
(p6)	br.cond.dpnt.few .time_normalize
	;;
	// Divided by 8 though shift. Now divide by 125
	// The compiler was able to do that with a multiply
	// and a shift and we do the same
EX(.fail_efault, probe.w.fault r23, 3)		// This also costs 5 cycles
(p14)	xmpy.hu f8 = f8, f7			// xmpy has 5 cycles latency so use it...
	;;
	mov r8 = r0
(p14)	getf.sig r2 = f8
	;;
(p14)	shr.u r21 = r2, 4
	;;
EX(.fail_efault, st8 [r31] = r9)
EX(.fail_efault, st8 [r23] = r21)
	FSYS_RETURN
.fail_einval:
	mov r8 = EINVAL
	mov r10 = -1
	FSYS_RETURN
.fail_efault:
	mov r8 = EFAULT
	mov r10 = -1
	FSYS_RETURN
END(fsys_gettimeofday)

ENTRY(fsys_clock_gettime)
	.prologue
	.altrp b6
	.body
	cmp4.ltu p6, p0 = CLOCK_MONOTONIC, r32
	// Fallback if this is not CLOCK_REALTIME or CLOCK_MONOTONIC
(p6)	br.spnt.few fsys_fallback_syscall
	mov r31 = r33
	shl r30 = r32,15
	br.many .gettime
END(fsys_clock_gettime)

/*
 * long fsys_rt_sigprocmask (int how, sigset_t *set, sigset_t *oset, size_t sigsetsize).
 */
#if _NSIG_WORDS != 1
# error Sorry, fsys_rt_sigprocmask() needs to be updated for _NSIG_WORDS != 1.
#endif
ENTRY(fsys_rt_sigprocmask)
	.prologue
	.altrp b6
	.body

	add r2=IA64_TASK_BLOCKED_OFFSET,r16
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	cmp4.ltu p6,p0=SIG_SETMASK,r32

	cmp.ne p15,p0=r0,r34			// oset != NULL?
	tnat.nz p8,p0=r34
	add r31=IA64_TASK_SIGHAND_OFFSET,r16
	;;
	ld8 r3=[r2]				// read/prefetch current->blocked
	ld4 r9=[r9]
	tnat.nz.or p6,p0=r35

	cmp.ne.or p6,p0=_NSIG_WORDS*8,r35
	tnat.nz.or p6,p0=r32
(p6)	br.spnt.few .fail_einval		// fail with EINVAL
	;;
#ifdef CONFIG_SMP
	ld8 r31=[r31]				// r31 <- current->sighand
#endif
	and r9=TIF_ALLWORK_MASK,r9
	tnat.nz.or p8,p0=r33
	;;
	cmp.ne p7,p0=0,r9
	cmp.eq p6,p0=r0,r33			// set == NULL?
	add r31=IA64_SIGHAND_SIGLOCK_OFFSET,r31	// r31 <- current->sighand->siglock
(p8)	br.spnt.few .fail_efault		// fail with EFAULT
(p7)	br.spnt.many fsys_fallback_syscall	// got pending kernel work...
(p6)	br.dpnt.many .store_mask		// -> short-circuit to just reading the signal mask

	/* Argh, we actually have to do some work and _update_ the signal mask: */

EX(.fail_efault, probe.r.fault r33, 3)		// verify user has read-access to *set
EX(.fail_efault, ld8 r14=[r33])			// r14 <- *set
	mov r17=(1 << (SIGKILL - 1)) | (1 << (SIGSTOP - 1))
	;;

	rsm psr.i				// mask interrupt delivery
	mov ar.ccv=0
	andcm r14=r14,r17			// filter out SIGKILL & SIGSTOP

#ifdef CONFIG_SMP
	mov r17=1
	;;
	cmpxchg4.acq r18=[r31],r17,ar.ccv	// try to acquire the lock
	mov r8=EINVAL			// default to EINVAL
	;;
	ld8 r3=[r2]			// re-read current->blocked now that we hold the lock
	cmp4.ne p6,p0=r18,r0
(p6)	br.cond.spnt.many .lock_contention
	;;
#else
	ld8 r3=[r2]			// re-read current->blocked now that we hold the lock
	mov r8=EINVAL			// default to EINVAL
#endif
	add r18=IA64_TASK_PENDING_OFFSET+IA64_SIGPENDING_SIGNAL_OFFSET,r16
	add r19=IA64_TASK_SIGNAL_OFFSET,r16
	cmp4.eq p6,p0=SIG_BLOCK,r32
	;;
	ld8 r19=[r19]			// r19 <- current->signal
	cmp4.eq p7,p0=SIG_UNBLOCK,r32
	cmp4.eq p8,p0=SIG_SETMASK,r32
	;;
	ld8 r18=[r18]			// r18 <- current->pending.signal
	.pred.rel.mutex p6,p7,p8
(p6)	or r14=r3,r14			// SIG_BLOCK
(p7)	andcm r14=r3,r14		// SIG_UNBLOCK

(p8)	mov r14=r14			// SIG_SETMASK
(p6)	mov r8=0			// clear error code
	// recalc_sigpending()
	add r17=IA64_SIGNAL_GROUP_STOP_COUNT_OFFSET,r19

	add r19=IA64_SIGNAL_SHARED_PENDING_OFFSET+IA64_SIGPENDING_SIGNAL_OFFSET,r19
	;;
	ld4 r17=[r17]		// r17 <- current->signal->group_stop_count
(p7)	mov r8=0		// clear error code

	ld8 r19=[r19]		// r19 <- current->signal->shared_pending
	;;
	cmp4.gt p6,p7=r17,r0	// p6/p7 <- (current->signal->group_stop_count > 0)?
(p8)	mov r8=0		// clear error code

	or r18=r18,r19		// r18 <- current->pending | current->signal->shared_pending
	;;
	// r18 <- (current->pending | current->signal->shared_pending) & ~current->blocked:
	andcm r18=r18,r14
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	;;

(p7)	cmp.ne.or.andcm p6,p7=r18,r0		// p6/p7 <- signal pending
	mov r19=0					// i must not leak kernel bits...
(p6)	br.cond.dpnt.many .sig_pending
	;;

1:	ld4 r17=[r9]				// r17 <- current->thread_info->flags
	;;
	mov ar.ccv=r17
	and r18=~_TIF_SIGPENDING,r17		// r18 <- r17 & ~(1 << TIF_SIGPENDING)
	;;

	st8 [r2]=r14				// update current->blocked with new mask
	cmpxchg4.acq r8=[r9],r18,ar.ccv		// current->thread_info->flags <- r18
	;;
	cmp.ne p6,p0=r17,r8			// update failed?
(p6)	br.cond.spnt.few 1b			// yes -> retry

#ifdef CONFIG_SMP
	st4.rel [r31]=r0			// release the lock
#endif
	ssm psr.i
	;;

	srlz.d					// ensure psr.i is set again
	mov r18=0					// i must not leak kernel bits...

.store_mask:
EX(.fail_efault, (p15) probe.w.fault r34, 3)	// verify user has write-access to *oset
EX(.fail_efault, (p15) st8 [r34]=r3)
	mov r2=0					// i must not leak kernel bits...
	mov r3=0					// i must not leak kernel bits...
	mov r8=0				// return 0
	mov r9=0					// i must not leak kernel bits...
	mov r14=0					// i must not leak kernel bits...
	mov r17=0					// i must not leak kernel bits...
	mov r31=0					// i must not leak kernel bits...
	FSYS_RETURN

.sig_pending:
#ifdef CONFIG_SMP
	st4.rel [r31]=r0			// release the lock
#endif
	ssm psr.i
	;;
	srlz.d
	br.sptk.many fsys_fallback_syscall	// with signal pending, do the heavy-weight syscall

#ifdef CONFIG_SMP
.lock_contention:
	/* Rather than spinning here, fall back on doing a heavy-weight syscall.  */
	ssm psr.i
	;;
	srlz.d
	br.sptk.many fsys_fallback_syscall
#endif
END(fsys_rt_sigprocmask)

ENTRY(fsys_fallback_syscall)
	.prologue
	.altrp b6
	.body
	/*
	 * We only get here from light-weight syscall handlers.  Thus, we already
	 * know that r15 contains a valid syscall number.  No need to re-check.
	 */
	adds r17=-1024,r15
	movl r14=sys_call_table
	;;
	rsm psr.i
	shladd r18=r17,3,r14
	;;
	ld8 r18=[r18]				// load normal (heavy-weight) syscall entry-point
	mov r29=psr				// read psr (12 cyc load latency)
	mov r27=ar.rsc
	mov r21=ar.fpsr
	mov r26=ar.pfs
END(fsys_fallback_syscall)
	/* FALL THROUGH */
GLOBAL_ENTRY(fsys_bubble_down)
	.prologue
	.altrp b6
	.body
	/*
	 * We get here for syscalls that don't have a lightweight
	 * handler.  For those, we need to bubble down into the kernel
	 * and that requires setting up a minimal pt_regs structure,
	 * and initializing the CPU state more or less as if an
	 * interruption had occurred.  To make syscall-restarts work,
	 * we setup pt_regs such that cr_iip points to the second
	 * instruction in syscall_via_break.  Decrementing the IP
	 * hence will restart the syscall via break and not
	 * decrementing IP will return us to the caller, as usual.
	 * Note that we preserve the value of psr.pp rather than
	 * initializing it from dcr.pp.  This makes it possible to
	 * distinguish fsyscall execution from other privileged
	 * execution.
	 *
	 * On entry:
	 *	- normal fsyscall handler register usage, except
	 *	  that we also have:
	 *	- r18: address of syscall entry point
	 *	- r21: ar.fpsr
	 *	- r26: ar.pfs
	 *	- r27: ar.rsc
	 *	- r29: psr
	 *
	 * We used to clear some PSR bits here but that requires slow
	 * serialization.  Fortuntely, that isn't really necessary.
	 * The rationale is as follows: we used to clear bits
	 * ~PSR_PRESERVED_BITS in PSR.L.  Since
	 * PSR_PRESERVED_BITS==PSR.{UP,MFL,MFH,PK,DT,PP,SP,RT,IC}, we
	 * ended up clearing PSR.{BE,AC,I,DFL,DFH,DI,DB,SI,TB}.
	 * However,
	 *
	 * PSR.BE : already is turned off in __kernel_syscall_via_epc()
	 * PSR.AC : don't care (kernel normally turns PSR.AC on)
	 * PSR.I  : already turned off by the time fsys_bubble_down gets
	 *	    invoked
	 * PSR.DFL: always 0 (kernel never turns it on)
	 * PSR.DFH: don't care --- kernel never touches f32-f127 on its own
	 *	    initiative
	 * PSR.DI : always 0 (kernel never turns it on)
	 * PSR.SI : always 0 (kernel never turns it on)
	 * PSR.DB : don't care --- kernel never enables kernel-level
	 *	    breakpoints
	 * PSR.TB : must be 0 already; if it wasn't zero on entry to
	 *          __kernel_syscall_via_epc, the branch to fsys_bubble_down
	 *          will trigger a taken branch; the taken-trap-handler then
	 *          converts the syscall into a break-based system-call.
	 */
	/*
	 * Reading psr.l gives us only bits 0-31, psr.it, and psr.mc.
	 * The rest we have to synthesize.
	 */
#	define PSR_ONE_BITS		((3 << IA64_PSR_CPL0_BIT)	\
					 | (0x1 << IA64_PSR_RI_BIT)	\
					 | IA64_PSR_BN | IA64_PSR_I)

	invala					// M0|1
	movl r14=ia64_ret_from_syscall		// X

	nop.m 0
	movl r28=__kernel_syscall_via_break	// X	create cr.iip
	;;

	mov r2=r16				// A    get task addr to addl-addressable register
	adds r16=IA64_TASK_THREAD_ON_USTACK_OFFSET,r16 // A
	mov r31=pr				// I0   save pr (2 cyc)
	;;
	st1 [r16]=r0				// M2|3 clear current->thread.on_ustack flag
	addl r22=IA64_RBS_OFFSET,r2		// A    compute base of RBS
	add r3=TI_FLAGS+IA64_TASK_SIZE,r2	// A
	;;
	ld4 r3=[r3]				// M0|1 r3 = current_thread_info()->flags
	lfetch.fault.excl.nt1 [r22]		// M0|1 prefetch register backing-store
	nop.i 0
	;;
	mov ar.rsc=0				// M2   set enforced lazy mode, pl 0, LE, loadrs=0
	nop.m 0
	nop.i 0
	;;
	mov r23=ar.bspstore			// M2 (12 cyc) save ar.bspstore
	mov.m r24=ar.rnat			// M2 (5 cyc) read ar.rnat (dual-issues!)
	nop.i 0
	;;
	mov ar.bspstore=r22			// M2 (6 cyc) switch to kernel RBS
	movl r8=PSR_ONE_BITS			// X
	;;
	mov r25=ar.unat				// M2 (5 cyc) save ar.unat
	mov r19=b6				// I0   save b6 (2 cyc)
	mov r20=r1				// A    save caller's gp in r20
	;;
	or r29=r8,r29				// A    construct cr.ipsr value to save
	mov b6=r18				// I0   copy syscall entry-point to b6 (7 cyc)
	addl r1=IA64_STK_OFFSET-IA64_PT_REGS_SIZE,r2 // A compute base of memory stack

	mov r18=ar.bsp				// M2   save (kernel) ar.bsp (12 cyc)
	cmp.ne pKStk,pUStk=r0,r0		// A    set pKStk <- 0, pUStk <- 1
	br.call.sptk.many b7=ia64_syscall_setup	// B
	;;
	mov ar.rsc=0x3				// M2   set eager mode, pl 0, LE, loadrs=0
	mov rp=r14				// I0   set the real return addr
	and r3=_TIF_SYSCALL_TRACEAUDIT,r3	// A
	;;
	ssm psr.i				// M2   we're on kernel stacks now, reenable irqs
	cmp.eq p8,p0=r3,r0			// A
(p10)	br.cond.spnt.many ia64_ret_from_syscall	// B    return if bad call-frame or r15 is a NaT

	nop.m 0
(p8)	br.call.sptk.many b6=b6			// B    (ignore return address)
	br.cond.spnt ia64_trace_syscall		// B
END(fsys_bubble_down)

	.rodata
	.align 8
	.globl fsyscall_table

	data8 fsys_bubble_down
fsyscall_table:
	data8 fsys_ni_syscall
	data8 0				// exit			// 1025
	data8 0				// read
	data8 0				// write
	data8 0				// open
	data8 0				// close
	data8 0				// creat		// 1030
	data8 0				// link
	data8 0				// unlink
	data8 0				// execve
	data8 0				// chdir
	data8 0				// fchdir		// 1035
	data8 0				// utimes
	data8 0				// mknod
	data8 0				// chmod
	data8 0				// chown
	data8 0				// lseek		// 1040
	data8 fsys_getpid		// getpid
	data8 fsys_getppid		// getppid
	data8 0				// mount
	data8 0				// umount
	data8 0				// setuid		// 1045
	data8 0				// getuid
	data8 0				// geteuid
	data8 0				// ptrace
	data8 0				// access
	data8 0				// sync			// 1050
	data8 0				// fsync
	data8 0				// fdatasync
	data8 0				// kill
	data8 0				// rename
	data8 0				// mkdir		// 1055
	data8 0				// rmdir
	data8 0				// dup
	data8 0				// pipe
	data8 0				// times
	data8 0				// brk			// 1060
	data8 0				// setgid
	data8 0				// getgid
	data8 0				// getegid
	data8 0				// acct
	data8 0				// ioctl		// 1065
	data8 0				// fcntl
	data8 0				// umask
	data8 0				// chroot
	data8 0				// ustat
	data8 0				// dup2			// 1070
	data8 0				// setreuid
	data8 0				// setregid
	data8 0				// getresuid
	data8 0				// setresuid
	data8 0				// getresgid		// 1075
	data8 0				// setresgid
	data8 0				// getgroups
	data8 0				// setgroups
	data8 0				// getpgid
	data8 0				// setpgid		// 1080
	data8 0				// setsid
	data8 0				// getsid
	data8 0				// sethostname
	data8 0				// setrlimit
	data8 0				// getrlimit		// 1085
	data8 0				// getrusage
	data8 fsys_gettimeofday		// gettimeofday
	data8 0				// settimeofday
	data8 0				// select
	data8 0				// poll			// 1090
	data8 0				// symlink
	data8 0				// readlink
	data8 0				// uselib
	data8 0				// swapon
	data8 0				// swapoff		// 1095
	data8 0				// reboot
	data8 0				// truncate
	data8 0				// ftruncate
	data8 0				// fchmod
	data8 0				// fchown		// 1100
	data8 0				// getpriority
	data8 0				// setpriority
	data8 0				// statfs
	data8 0				// fstatfs
	data8 0				// gettid		// 1105
	data8 0				// semget
	data8 0				// semop
	data8 0				// semctl
	data8 0				// msgget
	data8 0				// msgsnd		// 1110
	data8 0				// msgrcv
	data8 0				// msgctl
	data8 0				// shmget
	data8 0				// shmat
	data8 0				// shmdt		// 1115
	data8 0				// shmctl
	data8 0				// syslog
	data8 0				// setitimer
	data8 0				// getitimer
	data8 0					 		// 1120
	data8 0
	data8 0
	data8 0				// vhangup
	data8 0				// lchown
	data8 0				// remap_file_pages	// 1125
	data8 0				// wait4
	data8 0				// sysinfo
	data8 0				// clone
	data8 0				// setdomainname
	data8 0				// newuname		// 1130
	data8 0				// adjtimex
	data8 0
	data8 0				// init_module
	data8 0				// delete_module
	data8 0							// 1135
	data8 0
	data8 0				// quotactl
	data8 0				// bdflush
	data8 0				// sysfs
	data8 0				// personality		// 1140
	data8 0				// afs_syscall
	data8 0				// setfsuid
	data8 0				// setfsgid
	data8 0				// getdents
	data8 0				// flock		// 1145
	data8 0				// readv
	data8 0				// writev
	data8 0				// pread64
	data8 0				// pwrite64
	data8 0				// sysctl		// 1150
	data8 0				// mmap
	data8 0				// munmap
	data8 0				// mlock
	data8 0				// mlockall
	data8 0				// mprotect		// 1155
	data8 0				// mremap
	data8 0				// msync
	data8 0				// munlock
	data8 0				// munlockall
	data8 0				// sched_getparam	// 1160
	data8 0				// sched_setparam
	data8 0				// sched_getscheduler
	data8 0				// sched_setscheduler
	data8 0				// sched_yield
	data8 0				// sched_get_priority_max	// 1165
	data8 0				// sched_get_priority_min
	data8 0				// sched_rr_get_interval
	data8 0				// nanosleep
	data8 0				// nfsservctl
	data8 0				// prctl		// 1170
	data8 0				// getpagesize
	data8 0				// mmap2
	data8 0				// pciconfig_read
	data8 0				// pciconfig_write
	data8 0				// perfmonctl		// 1175
	data8 0				// sigaltstack
	data8 0				// rt_sigaction
	data8 0				// rt_sigpending
	data8 fsys_rt_sigprocmask	// rt_sigprocmask
	data8 0				// rt_sigqueueinfo	// 1180
	data8 0				// rt_sigreturn
	data8 0				// rt_sigsuspend
	data8 0				// rt_sigtimedwait
	data8 0				// getcwd
	data8 0				// capget		// 1185
	data8 0				// capset
	data8 0				// sendfile
	data8 0
	data8 0
	data8 0				// socket		// 1190
	data8 0				// bind
	data8 0				// connect
	data8 0				// listen
	data8 0				// accept
	data8 0				// getsockname		// 1195
	data8 0				// getpeername
	data8 0				// socketpair
	data8 0				// send
	data8 0				// sendto
	data8 0				// recv			// 1200
	data8 0				// recvfrom
	data8 0				// shutdown
	data8 0				// setsockopt
	data8 0				// getsockopt
	data8 0				// sendmsg		// 1205
	data8 0				// recvmsg
	data8 0				// pivot_root
	data8 0				// mincore
	data8 0				// madvise
	data8 0				// newstat		// 1210
	data8 0				// newlstat
	data8 0				// newfstat
	data8 0				// clone2
	data8 0				// getdents64
	data8 0				// getunwind		// 1215
	data8 0				// readahead
	data8 0				// setxattr
	data8 0				// lsetxattr
	data8 0				// fsetxattr
	data8 0				// getxattr		// 1220
	data8 0				// lgetxattr
	data8 0				// fgetxattr
	data8 0				// listxattr
	data8 0				// llistxattr
	data8 0				// flistxattr		// 1225
	data8 0				// removexattr
	data8 0				// lremovexattr
	data8 0				// fremovexattr
	data8 0				// tkill
	data8 0				// futex		// 1230
	data8 0				// sched_setaffinity
	data8 0				// sched_getaffinity
	data8 fsys_set_tid_address	// set_tid_address
	data8 0				// fadvise64_64
	data8 0				// tgkill		// 1235
	data8 0				// exit_group
	data8 0				// lookup_dcookie
	data8 0				// io_setup
	data8 0				// io_destroy
	data8 0				// io_getevents		// 1240
	data8 0				// io_submit
	data8 0				// io_cancel
	data8 0				// epoll_create
	data8 0				// epoll_ctl
	data8 0				// epoll_wait		// 1245
	data8 0				// restart_syscall
	data8 0				// semtimedop
	data8 0				// timer_create
	data8 0				// timer_settime
	data8 0				// timer_gettime 	// 1250
	data8 0				// timer_getoverrun
	data8 0				// timer_delete
	data8 0				// clock_settime
	data8 fsys_clock_gettime	// clock_gettime
	data8 0				// clock_getres		// 1255
	data8 0				// clock_nanosleep
	data8 0				// fstatfs64
	data8 0				// statfs64
	data8 0
	data8 0							// 1260
	data8 0
	data8 0				// mq_open
	data8 0				// mq_unlink
	data8 0				// mq_timedsend
	data8 0				// mq_timedreceive	// 1265
	data8 0				// mq_notify
	data8 0				// mq_getsetattr
	data8 0				// kexec_load
	data8 0
	data8 0							// 1270
	data8 0
	data8 0
	data8 0
	data8 0
	data8 0							// 1275
	data8 0
	data8 0
	data8 0
	data8 0
	data8 0							// 1280

	.org fsyscall_table + 8*NR_syscalls	// guard against failures to increase NR_syscalls
