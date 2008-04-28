	/* SunOS's execv() call only specifies the argv argument, the
	 * environment settings are the same as the calling processes.
	 */
sys_execve:
	sethi	%hi(sparc_execve), %g1
	ba,pt	%xcc, execve_merge
	 or	%g1, %lo(sparc_execve), %g1

#ifdef CONFIG_COMPAT
sunos_execv:
	stx	%g0, [%sp + PTREGS_OFF + PT_V9_I2]
sys32_execve:
	sethi	%hi(sparc32_execve), %g1
	or	%g1, %lo(sparc32_execve), %g1
#endif

execve_merge:
	flushw
	jmpl	%g1, %g0
	 add	%sp, PTREGS_OFF, %o0

	.align	32
sys_pipe:
	ba,pt	%xcc, sparc_pipe
	 add	%sp, PTREGS_OFF, %o0
sys_nis_syscall:
	ba,pt	%xcc, c_sys_nis_syscall
	 add	%sp, PTREGS_OFF, %o0
sys_memory_ordering:
	ba,pt	%xcc, sparc_memory_ordering
	 add	%sp, PTREGS_OFF, %o1
sys_sigaltstack:
	ba,pt	%xcc, do_sigaltstack
	 add	%i6, STACK_BIAS, %o2
#ifdef CONFIG_COMPAT
sys32_sigstack:
	ba,pt	%xcc, do_sys32_sigstack
	 mov	%i6, %o2
sys32_sigaltstack:
	ba,pt	%xcc, do_sys32_sigaltstack
	 mov	%i6, %o2
#endif
	.align	32
#ifdef CONFIG_COMPAT
sys32_sigreturn:
	add	%sp, PTREGS_OFF, %o0
	call	do_sigreturn32
	 add	%o7, 1f-.-4, %o7
	nop
#endif
sys_rt_sigreturn:
	add	%sp, PTREGS_OFF, %o0
	call	do_rt_sigreturn
	 add	%o7, 1f-.-4, %o7
	nop
#ifdef CONFIG_COMPAT
sys32_rt_sigreturn:
	add	%sp, PTREGS_OFF, %o0
	call	do_rt_sigreturn32
	 add	%o7, 1f-.-4, %o7
	nop
#endif
	.align	32
1:	ldx	[%g6 + TI_FLAGS], %l5
	andcc	%l5, (_TIF_SYSCALL_TRACE|_TIF_SECCOMP|_TIF_SYSCALL_AUDIT), %g0
	be,pt	%icc, rtrap
	 nop
	add	%sp, PTREGS_OFF, %o0
	call	syscall_trace
	 mov	1, %o1
	ba,pt	%xcc, rtrap
	 nop

	/* This is how fork() was meant to be done, 8 instruction entry.
	 *
	 * I questioned the following code briefly, let me clear things
	 * up so you must not reason on it like I did.
	 *
	 * Know the fork_kpsr etc. we use in the sparc32 port?  We don't
	 * need it here because the only piece of window state we copy to
	 * the child is the CWP register.  Even if the parent sleeps,
	 * we are safe because we stuck it into pt_regs of the parent
	 * so it will not change.
	 *
	 * XXX This raises the question, whether we can do the same on
	 * XXX sparc32 to get rid of fork_kpsr _and_ fork_kwim.  The
	 * XXX answer is yes.  We stick fork_kpsr in UREG_G0 and
	 * XXX fork_kwim in UREG_G1 (global registers are considered
	 * XXX volatile across a system call in the sparc ABI I think
	 * XXX if it isn't we can use regs->y instead, anyone who depends
	 * XXX upon the Y register being preserved across a fork deserves
	 * XXX to lose).
	 *
	 * In fact we should take advantage of that fact for other things
	 * during system calls...
	 */
	.align	32
sys_vfork: /* Under Linux, vfork and fork are just special cases of clone. */
	sethi	%hi(0x4000 | 0x0100 | SIGCHLD), %o0
	or	%o0, %lo(0x4000 | 0x0100 | SIGCHLD), %o0
	ba,pt	%xcc, sys_clone
sys_fork:
	 clr	%o1
	mov	SIGCHLD, %o0
sys_clone:
	flushw
	movrz	%o1, %fp, %o1
	mov	0, %o3
	ba,pt	%xcc, sparc_do_fork
	 add	%sp, PTREGS_OFF, %o2

	.globl	ret_from_syscall
ret_from_syscall:
	/* Clear current_thread_info()->new_child, and
	 * check performance counter stuff too.
	 */
	stb	%g0, [%g6 + TI_NEW_CHILD]
	ldx	[%g6 + TI_FLAGS], %l0
	call	schedule_tail
	 mov	%g7, %o0
	andcc	%l0, _TIF_PERFCTR, %g0
	be,pt	%icc, 1f
	 nop
	ldx	[%g6 + TI_PCR], %o7
	wr	%g0, %o7, %pcr

	/* Blackbird errata workaround.  See commentary in
	 * smp.c:smp_percpu_timer_interrupt() for more
	 * information.
	 */
	ba,pt	%xcc, 99f
	 nop

	.align	64
99:	wr	%g0, %g0, %pic
	rd	%pic, %g0

1:	ba,pt	%xcc, ret_sys_call
	 ldx	[%sp + PTREGS_OFF + PT_V9_I0], %o0

	.globl	sparc_exit
	.type	sparc_exit,#function
sparc_exit:
	rdpr	%pstate, %g2
	wrpr	%g2, PSTATE_IE, %pstate
	rdpr	%otherwin, %g1
	rdpr	%cansave, %g3
	add	%g3, %g1, %g3
	wrpr	%g3, 0x0, %cansave
	wrpr	%g0, 0x0, %otherwin
	wrpr	%g2, 0x0, %pstate
	ba,pt	%xcc, sys_exit
	 stb	%g0, [%g6 + TI_WSAVED]
	.size	sparc_exit,.-sparc_exit

linux_sparc_ni_syscall:
	sethi	%hi(sys_ni_syscall), %l7
	ba,pt	%xcc, 4f
	 or	%l7, %lo(sys_ni_syscall), %l7

linux_syscall_trace32:
	add	%sp, PTREGS_OFF, %o0
	call	syscall_trace
	 clr	%o1
	srl	%i0, 0, %o0
	srl	%i4, 0, %o4
	srl	%i1, 0, %o1
	srl	%i2, 0, %o2
	ba,pt	%xcc, 2f
	 srl	%i3, 0, %o3

linux_syscall_trace:
	add	%sp, PTREGS_OFF, %o0
	call	syscall_trace
	 clr	%o1
	mov	%i0, %o0
	mov	%i1, %o1
	mov	%i2, %o2
	mov	%i3, %o3
	b,pt	%xcc, 2f
	 mov	%i4, %o4


	/* Linux 32-bit system calls enter here... */
	.align	32
	.globl	linux_sparc_syscall32
linux_sparc_syscall32:
	/* Direct access to user regs, much faster. */
	cmp	%g1, NR_SYSCALLS			! IEU1	Group
	bgeu,pn	%xcc, linux_sparc_ni_syscall		! CTI
	 srl	%i0, 0, %o0				! IEU0
	sll	%g1, 2, %l4				! IEU0	Group
	srl	%i4, 0, %o4				! IEU1
	lduw	[%l7 + %l4], %l7			! Load
	srl	%i1, 0, %o1				! IEU0	Group
	ldx	[%g6 + TI_FLAGS], %l0		! Load

	srl	%i5, 0, %o5				! IEU1
	srl	%i2, 0, %o2				! IEU0	Group
	andcc	%l0, (_TIF_SYSCALL_TRACE|_TIF_SECCOMP|_TIF_SYSCALL_AUDIT), %g0
	bne,pn	%icc, linux_syscall_trace32		! CTI
	 mov	%i0, %l5				! IEU1
	call	%l7					! CTI	Group brk forced
	 srl	%i3, 0, %o3				! IEU0
	ba,a,pt	%xcc, 3f

	/* Linux native system calls enter here... */
	.align	32
	.globl	linux_sparc_syscall
linux_sparc_syscall:
	/* Direct access to user regs, much faster. */
	cmp	%g1, NR_SYSCALLS			! IEU1	Group
	bgeu,pn	%xcc, linux_sparc_ni_syscall		! CTI
	 mov	%i0, %o0				! IEU0
	sll	%g1, 2, %l4				! IEU0	Group
	mov	%i1, %o1				! IEU1
	lduw	[%l7 + %l4], %l7			! Load
4:	mov	%i2, %o2				! IEU0	Group
	ldx	[%g6 + TI_FLAGS], %l0		! Load

	mov	%i3, %o3				! IEU1
	mov	%i4, %o4				! IEU0	Group
	andcc	%l0, (_TIF_SYSCALL_TRACE|_TIF_SECCOMP|_TIF_SYSCALL_AUDIT), %g0
	bne,pn	%icc, linux_syscall_trace		! CTI	Group
	 mov	%i0, %l5				! IEU0
2:	call	%l7					! CTI	Group brk forced
	 mov	%i5, %o5				! IEU0
	nop

3:	stx	%o0, [%sp + PTREGS_OFF + PT_V9_I0]
ret_sys_call:
	ldx	[%sp + PTREGS_OFF + PT_V9_TSTATE], %g3
	ldx	[%sp + PTREGS_OFF + PT_V9_TNPC], %l1 ! pc = npc
	sra	%o0, 0, %o0
	mov	%ulo(TSTATE_XCARRY | TSTATE_ICARRY), %g2
	sllx	%g2, 32, %g2

	/* Check if force_successful_syscall_return()
	 * was invoked.
	 */
	ldub	[%g6 + TI_SYS_NOERROR], %l2
	brnz,a,pn %l2, 80f
	 stb	%g0, [%g6 + TI_SYS_NOERROR]

	cmp	%o0, -ERESTART_RESTARTBLOCK
	bgeu,pn	%xcc, 1f
	 andcc	%l0, (_TIF_SYSCALL_TRACE|_TIF_SECCOMP|_TIF_SYSCALL_AUDIT), %l6
80:
	/* System call success, clear Carry condition code. */
	andn	%g3, %g2, %g3
	stx	%g3, [%sp + PTREGS_OFF + PT_V9_TSTATE]	
	bne,pn	%icc, linux_syscall_trace2
	 add	%l1, 0x4, %l2			! npc = npc+4
	stx	%l1, [%sp + PTREGS_OFF + PT_V9_TPC]
	ba,pt	%xcc, rtrap
	 stx	%l2, [%sp + PTREGS_OFF + PT_V9_TNPC]

1:
	/* System call failure, set Carry condition code.
	 * Also, get abs(errno) to return to the process.
	 */
	andcc	%l0, (_TIF_SYSCALL_TRACE|_TIF_SECCOMP|_TIF_SYSCALL_AUDIT), %l6	
	sub	%g0, %o0, %o0
	or	%g3, %g2, %g3
	stx	%o0, [%sp + PTREGS_OFF + PT_V9_I0]
	stx	%g3, [%sp + PTREGS_OFF + PT_V9_TSTATE]
	bne,pn	%icc, linux_syscall_trace2
	 add	%l1, 0x4, %l2			! npc = npc+4
	stx	%l1, [%sp + PTREGS_OFF + PT_V9_TPC]

	b,pt	%xcc, rtrap
	 stx	%l2, [%sp + PTREGS_OFF + PT_V9_TNPC]
linux_syscall_trace2:
	add	%sp, PTREGS_OFF, %o0
	call	syscall_trace
	 mov	1, %o1
	stx	%l1, [%sp + PTREGS_OFF + PT_V9_TPC]
	ba,pt	%xcc, rtrap
	 stx	%l2, [%sp + PTREGS_OFF + PT_V9_TNPC]
