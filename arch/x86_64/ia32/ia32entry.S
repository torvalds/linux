/*
 * Compatibility mode system call entry point for x86-64. 
 * 		
 * Copyright 2000-2002 Andi Kleen, SuSE Labs.
 */		 

#include <asm/dwarf2.h>
#include <asm/calling.h>
#include <asm/asm-offsets.h>
#include <asm/current.h>
#include <asm/errno.h>
#include <asm/ia32_unistd.h>	
#include <asm/thread_info.h>	
#include <asm/segment.h>
#include <asm/vsyscall32.h>
#include <linux/linkage.h>

	.macro IA32_ARG_FIXUP noebp=0
	movl	%edi,%r8d
	.if \noebp
	.else
	movl	%ebp,%r9d
	.endif
	xchg	%ecx,%esi
	movl	%ebx,%edi
	movl	%edx,%edx	/* zero extension */
	.endm 

	/* clobbers %eax */	
	.macro  CLEAR_RREGS
	xorl 	%eax,%eax
	movq	%rax,R11(%rsp)
	movq	%rax,R10(%rsp)
	movq	%rax,R9(%rsp)
	movq	%rax,R8(%rsp)
	.endm

	.macro CFI_STARTPROC32 simple
	CFI_STARTPROC	\simple
	CFI_UNDEFINED	r8
	CFI_UNDEFINED	r9
	CFI_UNDEFINED	r10
	CFI_UNDEFINED	r11
	CFI_UNDEFINED	r12
	CFI_UNDEFINED	r13
	CFI_UNDEFINED	r14
	CFI_UNDEFINED	r15
	.endm

/*
 * 32bit SYSENTER instruction entry.
 *
 * Arguments:
 * %eax	System call number.
 * %ebx Arg1
 * %ecx Arg2
 * %edx Arg3
 * %esi Arg4
 * %edi Arg5
 * %ebp user stack
 * 0(%ebp) Arg6	
 * 	
 * Interrupts off.
 *	
 * This is purely a fast path. For anything complicated we use the int 0x80
 * path below.	Set up a complete hardware stack frame to share code
 * with the int 0x80 path.
 */ 	
ENTRY(ia32_sysenter_target)
	CFI_STARTPROC32	simple
	CFI_DEF_CFA	rsp,0
	CFI_REGISTER	rsp,rbp
	swapgs
	movq	%gs:pda_kernelstack, %rsp
	addq	$(PDA_STACKOFFSET),%rsp	
	sti	
 	movl	%ebp,%ebp		/* zero extension */
	pushq	$__USER32_DS
	CFI_ADJUST_CFA_OFFSET 8
	/*CFI_REL_OFFSET ss,0*/
	pushq	%rbp
	CFI_ADJUST_CFA_OFFSET 8
	CFI_REL_OFFSET rsp,0
	pushfq
	CFI_ADJUST_CFA_OFFSET 8
	/*CFI_REL_OFFSET rflags,0*/
	movl	$VSYSCALL32_SYSEXIT, %r10d
	CFI_REGISTER rip,r10
	pushq	$__USER32_CS
	CFI_ADJUST_CFA_OFFSET 8
	/*CFI_REL_OFFSET cs,0*/
	movl	%eax, %eax
	pushq	%r10
	CFI_ADJUST_CFA_OFFSET 8
	CFI_REL_OFFSET rip,0
	pushq	%rax
	CFI_ADJUST_CFA_OFFSET 8
	cld
	SAVE_ARGS 0,0,1
 	/* no need to do an access_ok check here because rbp has been
 	   32bit zero extended */ 
1:	movl	(%rbp),%r9d
 	.section __ex_table,"a"
 	.quad 1b,ia32_badarg
 	.previous	
	GET_THREAD_INFO(%r10)
	orl    $TS_COMPAT,threadinfo_status(%r10)
	testl  $(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT|_TIF_SECCOMP),threadinfo_flags(%r10)
	CFI_REMEMBER_STATE
	jnz  sysenter_tracesys
sysenter_do_call:	
	cmpl	$(IA32_NR_syscalls),%eax
	jae	ia32_badsys
	IA32_ARG_FIXUP 1
	call	*ia32_sys_call_table(,%rax,8)
	movq	%rax,RAX-ARGOFFSET(%rsp)
	GET_THREAD_INFO(%r10)
	cli
	testl	$_TIF_ALLWORK_MASK,threadinfo_flags(%r10)
	jnz	int_ret_from_sys_call
	andl    $~TS_COMPAT,threadinfo_status(%r10)
	/* clear IF, that popfq doesn't enable interrupts early */
	andl  $~0x200,EFLAGS-R11(%rsp) 
	RESTORE_ARGS 1,24,1,1,1,1
	popfq
	CFI_ADJUST_CFA_OFFSET -8
	/*CFI_RESTORE rflags*/
	popq	%rcx				/* User %esp */
	CFI_ADJUST_CFA_OFFSET -8
	CFI_REGISTER rsp,rcx
	movl	$VSYSCALL32_SYSEXIT,%edx	/* User %eip */
	CFI_REGISTER rip,rdx
	swapgs
	sti		/* sti only takes effect after the next instruction */
	/* sysexit */
	.byte	0xf, 0x35

sysenter_tracesys:
	CFI_RESTORE_STATE
	SAVE_REST
	CLEAR_RREGS
	movq	$-ENOSYS,RAX(%rsp)	/* really needed? */
	movq	%rsp,%rdi        /* &pt_regs -> arg1 */
	call	syscall_trace_enter
	LOAD_ARGS ARGOFFSET  /* reload args from stack in case ptrace changed it */
	RESTORE_REST
	movl	%ebp, %ebp
	/* no need to do an access_ok check here because rbp has been
	   32bit zero extended */ 
1:	movl	(%rbp),%r9d
	.section __ex_table,"a"
	.quad 1b,ia32_badarg
	.previous
	jmp	sysenter_do_call
	CFI_ENDPROC

/*
 * 32bit SYSCALL instruction entry.
 *
 * Arguments:
 * %eax	System call number.
 * %ebx Arg1
 * %ecx return EIP 
 * %edx Arg3
 * %esi Arg4
 * %edi Arg5
 * %ebp Arg2    [note: not saved in the stack frame, should not be touched]
 * %esp user stack 
 * 0(%esp) Arg6
 * 	
 * Interrupts off.
 *	
 * This is purely a fast path. For anything complicated we use the int 0x80
 * path below.	Set up a complete hardware stack frame to share code
 * with the int 0x80 path.	
 */ 	
ENTRY(ia32_cstar_target)
	CFI_STARTPROC32	simple
	CFI_DEF_CFA	rsp,0
	CFI_REGISTER	rip,rcx
	/*CFI_REGISTER	rflags,r11*/
	swapgs
	movl	%esp,%r8d
	CFI_REGISTER	rsp,r8
	movq	%gs:pda_kernelstack,%rsp
	sti
	SAVE_ARGS 8,1,1
	movl 	%eax,%eax	/* zero extension */
	movq	%rax,ORIG_RAX-ARGOFFSET(%rsp)
	movq	%rcx,RIP-ARGOFFSET(%rsp)
	CFI_REL_OFFSET rip,RIP-ARGOFFSET
	movq	%rbp,RCX-ARGOFFSET(%rsp) /* this lies slightly to ptrace */
	movl	%ebp,%ecx
	movq	$__USER32_CS,CS-ARGOFFSET(%rsp)
	movq	$__USER32_DS,SS-ARGOFFSET(%rsp)
	movq	%r11,EFLAGS-ARGOFFSET(%rsp)
	/*CFI_REL_OFFSET rflags,EFLAGS-ARGOFFSET*/
	movq	%r8,RSP-ARGOFFSET(%rsp)	
	CFI_REL_OFFSET rsp,RSP-ARGOFFSET
	/* no need to do an access_ok check here because r8 has been
	   32bit zero extended */ 
	/* hardware stack frame is complete now */	
1:	movl	(%r8),%r9d
	.section __ex_table,"a"
	.quad 1b,ia32_badarg
	.previous	
	GET_THREAD_INFO(%r10)
	orl   $TS_COMPAT,threadinfo_status(%r10)
	testl $(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT|_TIF_SECCOMP),threadinfo_flags(%r10)
	CFI_REMEMBER_STATE
	jnz   cstar_tracesys
cstar_do_call:	
	cmpl $IA32_NR_syscalls,%eax
	jae  ia32_badsys
	IA32_ARG_FIXUP 1
	call *ia32_sys_call_table(,%rax,8)
	movq %rax,RAX-ARGOFFSET(%rsp)
	GET_THREAD_INFO(%r10)
	cli
	testl $_TIF_ALLWORK_MASK,threadinfo_flags(%r10)
	jnz  int_ret_from_sys_call
	andl $~TS_COMPAT,threadinfo_status(%r10)
	RESTORE_ARGS 1,-ARG_SKIP,1,1,1
	movl RIP-ARGOFFSET(%rsp),%ecx
	CFI_REGISTER rip,rcx
	movl EFLAGS-ARGOFFSET(%rsp),%r11d	
	/*CFI_REGISTER rflags,r11*/
	movl RSP-ARGOFFSET(%rsp),%esp
	CFI_RESTORE rsp
	swapgs
	sysretl
	
cstar_tracesys:	
	CFI_RESTORE_STATE
	SAVE_REST
	CLEAR_RREGS
	movq $-ENOSYS,RAX(%rsp)	/* really needed? */
	movq %rsp,%rdi        /* &pt_regs -> arg1 */
	call syscall_trace_enter
	LOAD_ARGS ARGOFFSET  /* reload args from stack in case ptrace changed it */
	RESTORE_REST
	movl RSP-ARGOFFSET(%rsp), %r8d
	/* no need to do an access_ok check here because r8 has been
	   32bit zero extended */ 
1:	movl	(%r8),%r9d
	.section __ex_table,"a"
	.quad 1b,ia32_badarg
	.previous
	jmp cstar_do_call
				
ia32_badarg:
	movq $-EFAULT,%rax
	jmp ia32_sysret
	CFI_ENDPROC

/* 
 * Emulated IA32 system calls via int 0x80. 
 *
 * Arguments:	 
 * %eax	System call number.
 * %ebx Arg1
 * %ecx Arg2
 * %edx Arg3
 * %esi Arg4
 * %edi Arg5
 * %ebp Arg6    [note: not saved in the stack frame, should not be touched]
 *
 * Notes:
 * Uses the same stack frame as the x86-64 version.	
 * All registers except %eax must be saved (but ptrace may violate that)
 * Arguments are zero extended. For system calls that want sign extension and
 * take long arguments a wrapper is needed. Most calls can just be called
 * directly.
 * Assumes it is only called from user space and entered with interrupts off.	
 */ 				

ENTRY(ia32_syscall)
	CFI_STARTPROC	simple
	CFI_DEF_CFA	rsp,SS+8-RIP
	/*CFI_REL_OFFSET	ss,SS-RIP*/
	CFI_REL_OFFSET	rsp,RSP-RIP
	/*CFI_REL_OFFSET	rflags,EFLAGS-RIP*/
	/*CFI_REL_OFFSET	cs,CS-RIP*/
	CFI_REL_OFFSET	rip,RIP-RIP
	swapgs	
	sti
	movl %eax,%eax
	pushq %rax
	CFI_ADJUST_CFA_OFFSET 8
	cld
	/* note the registers are not zero extended to the sf.
	   this could be a problem. */
	SAVE_ARGS 0,0,1
	GET_THREAD_INFO(%r10)
	orl   $TS_COMPAT,threadinfo_status(%r10)
	testl $(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT|_TIF_SECCOMP),threadinfo_flags(%r10)
	jnz ia32_tracesys
ia32_do_syscall:	
	cmpl $(IA32_NR_syscalls),%eax
	jae  ia32_badsys
	IA32_ARG_FIXUP
	call *ia32_sys_call_table(,%rax,8) # xxx: rip relative
ia32_sysret:
	movq %rax,RAX-ARGOFFSET(%rsp)
	jmp int_ret_from_sys_call 

ia32_tracesys:			 
	SAVE_REST
	movq $-ENOSYS,RAX(%rsp)	/* really needed? */
	movq %rsp,%rdi        /* &pt_regs -> arg1 */
	call syscall_trace_enter
	LOAD_ARGS ARGOFFSET  /* reload args from stack in case ptrace changed it */
	RESTORE_REST
	jmp ia32_do_syscall

ia32_badsys:
	movq $0,ORIG_RAX-ARGOFFSET(%rsp)
	movq $-ENOSYS,RAX-ARGOFFSET(%rsp)
	jmp int_ret_from_sys_call

ni_syscall:
	movq %rax,%rdi
	jmp  sys32_ni_syscall			

quiet_ni_syscall:
	movq $-ENOSYS,%rax
	ret
	CFI_ENDPROC
	
	.macro PTREGSCALL label, func, arg
	.globl \label
\label:
	leaq \func(%rip),%rax
	leaq -ARGOFFSET+8(%rsp),\arg	/* 8 for return address */
	jmp  ia32_ptregs_common	
	.endm

	CFI_STARTPROC32

	PTREGSCALL stub32_rt_sigreturn, sys32_rt_sigreturn, %rdi
	PTREGSCALL stub32_sigreturn, sys32_sigreturn, %rdi
	PTREGSCALL stub32_sigaltstack, sys32_sigaltstack, %rdx
	PTREGSCALL stub32_sigsuspend, sys32_sigsuspend, %rcx
	PTREGSCALL stub32_execve, sys32_execve, %rcx
	PTREGSCALL stub32_fork, sys_fork, %rdi
	PTREGSCALL stub32_clone, sys32_clone, %rdx
	PTREGSCALL stub32_vfork, sys_vfork, %rdi
	PTREGSCALL stub32_iopl, sys_iopl, %rsi
	PTREGSCALL stub32_rt_sigsuspend, sys_rt_sigsuspend, %rdx

ENTRY(ia32_ptregs_common)
	popq %r11
	CFI_ENDPROC
	CFI_STARTPROC32	simple
	CFI_DEF_CFA	rsp,SS+8-ARGOFFSET
	CFI_REL_OFFSET	rax,RAX-ARGOFFSET
	CFI_REL_OFFSET	rcx,RCX-ARGOFFSET
	CFI_REL_OFFSET	rdx,RDX-ARGOFFSET
	CFI_REL_OFFSET	rsi,RSI-ARGOFFSET
	CFI_REL_OFFSET	rdi,RDI-ARGOFFSET
	CFI_REL_OFFSET	rip,RIP-ARGOFFSET
/*	CFI_REL_OFFSET	cs,CS-ARGOFFSET*/
/*	CFI_REL_OFFSET	rflags,EFLAGS-ARGOFFSET*/
	CFI_REL_OFFSET	rsp,RSP-ARGOFFSET
/*	CFI_REL_OFFSET	ss,SS-ARGOFFSET*/
	SAVE_REST
	call *%rax
	RESTORE_REST
	jmp  ia32_sysret	/* misbalances the return cache */
	CFI_ENDPROC

	.section .rodata,"a"
	.align 8
	.globl ia32_sys_call_table
ia32_sys_call_table:
	.quad sys_restart_syscall
	.quad sys_exit
	.quad stub32_fork
	.quad sys_read
	.quad sys_write
	.quad compat_sys_open		/* 5 */
	.quad sys_close
	.quad sys32_waitpid
	.quad sys_creat
	.quad sys_link
	.quad sys_unlink		/* 10 */
	.quad stub32_execve
	.quad sys_chdir
	.quad compat_sys_time
	.quad sys_mknod
	.quad sys_chmod		/* 15 */
	.quad sys_lchown16
	.quad quiet_ni_syscall			/* old break syscall holder */
	.quad sys_stat
	.quad sys32_lseek
	.quad sys_getpid		/* 20 */
	.quad compat_sys_mount	/* mount  */
	.quad sys_oldumount	/* old_umount  */
	.quad sys_setuid16
	.quad sys_getuid16
	.quad compat_sys_stime	/* stime */		/* 25 */
	.quad sys32_ptrace	/* ptrace */
	.quad sys_alarm
	.quad sys_fstat	/* (old)fstat */
	.quad sys_pause
	.quad compat_sys_utime	/* 30 */
	.quad quiet_ni_syscall	/* old stty syscall holder */
	.quad quiet_ni_syscall	/* old gtty syscall holder */
	.quad sys_access
	.quad sys_nice	
	.quad quiet_ni_syscall	/* 35 */	/* old ftime syscall holder */
	.quad sys_sync
	.quad sys32_kill
	.quad sys_rename
	.quad sys_mkdir
	.quad sys_rmdir		/* 40 */
	.quad sys_dup
	.quad sys32_pipe
	.quad compat_sys_times
	.quad quiet_ni_syscall			/* old prof syscall holder */
	.quad sys_brk		/* 45 */
	.quad sys_setgid16
	.quad sys_getgid16
	.quad sys_signal
	.quad sys_geteuid16
	.quad sys_getegid16	/* 50 */
	.quad sys_acct
	.quad sys_umount			/* new_umount */
	.quad quiet_ni_syscall			/* old lock syscall holder */
	.quad compat_sys_ioctl
	.quad compat_sys_fcntl64		/* 55 */
	.quad quiet_ni_syscall			/* old mpx syscall holder */
	.quad sys_setpgid
	.quad quiet_ni_syscall			/* old ulimit syscall holder */
	.quad sys32_olduname
	.quad sys_umask		/* 60 */
	.quad sys_chroot
	.quad sys32_ustat
	.quad sys_dup2
	.quad sys_getppid
	.quad sys_getpgrp		/* 65 */
	.quad sys_setsid
	.quad sys32_sigaction
	.quad sys_sgetmask
	.quad sys_ssetmask
	.quad sys_setreuid16	/* 70 */
	.quad sys_setregid16
	.quad stub32_sigsuspend
	.quad compat_sys_sigpending
	.quad sys_sethostname
	.quad compat_sys_setrlimit	/* 75 */
	.quad compat_sys_old_getrlimit	/* old_getrlimit */
	.quad compat_sys_getrusage
	.quad sys32_gettimeofday
	.quad sys32_settimeofday
	.quad sys_getgroups16	/* 80 */
	.quad sys_setgroups16
	.quad sys32_old_select
	.quad sys_symlink
	.quad sys_lstat
	.quad sys_readlink		/* 85 */
#ifdef CONFIG_IA32_AOUT
	.quad sys_uselib
#else
	.quad quiet_ni_syscall
#endif
	.quad sys_swapon
	.quad sys_reboot
	.quad compat_sys_old_readdir
	.quad sys32_mmap		/* 90 */
	.quad sys_munmap
	.quad sys_truncate
	.quad sys_ftruncate
	.quad sys_fchmod
	.quad sys_fchown16		/* 95 */
	.quad sys_getpriority
	.quad sys_setpriority
	.quad quiet_ni_syscall			/* old profil syscall holder */
	.quad compat_sys_statfs
	.quad compat_sys_fstatfs		/* 100 */
	.quad sys_ioperm
	.quad compat_sys_socketcall
	.quad sys_syslog
	.quad compat_sys_setitimer
	.quad compat_sys_getitimer	/* 105 */
	.quad compat_sys_newstat
	.quad compat_sys_newlstat
	.quad compat_sys_newfstat
	.quad sys32_uname
	.quad stub32_iopl		/* 110 */
	.quad sys_vhangup
	.quad quiet_ni_syscall	/* old "idle" system call */
	.quad sys32_vm86_warning	/* vm86old */ 
	.quad compat_sys_wait4
	.quad sys_swapoff		/* 115 */
	.quad sys32_sysinfo
	.quad sys32_ipc
	.quad sys_fsync
	.quad stub32_sigreturn
	.quad stub32_clone		/* 120 */
	.quad sys_setdomainname
	.quad sys_uname
	.quad sys_modify_ldt
	.quad compat_sys_adjtimex
	.quad sys32_mprotect		/* 125 */
	.quad compat_sys_sigprocmask
	.quad quiet_ni_syscall		/* create_module */
	.quad sys_init_module
	.quad sys_delete_module
	.quad quiet_ni_syscall		/* 130  get_kernel_syms */
	.quad sys_quotactl
	.quad sys_getpgid
	.quad sys_fchdir
	.quad quiet_ni_syscall	/* bdflush */
	.quad sys_sysfs		/* 135 */
	.quad sys_personality
	.quad quiet_ni_syscall	/* for afs_syscall */
	.quad sys_setfsuid16
	.quad sys_setfsgid16
	.quad sys_llseek		/* 140 */
	.quad compat_sys_getdents
	.quad compat_sys_select
	.quad sys_flock
	.quad sys_msync
	.quad compat_sys_readv		/* 145 */
	.quad compat_sys_writev
	.quad sys_getsid
	.quad sys_fdatasync
	.quad sys32_sysctl	/* sysctl */
	.quad sys_mlock		/* 150 */
	.quad sys_munlock
	.quad sys_mlockall
	.quad sys_munlockall
	.quad sys_sched_setparam
	.quad sys_sched_getparam   /* 155 */
	.quad sys_sched_setscheduler
	.quad sys_sched_getscheduler
	.quad sys_sched_yield
	.quad sys_sched_get_priority_max
	.quad sys_sched_get_priority_min  /* 160 */
	.quad sys_sched_rr_get_interval
	.quad compat_sys_nanosleep
	.quad sys_mremap
	.quad sys_setresuid16
	.quad sys_getresuid16	/* 165 */
	.quad sys32_vm86_warning	/* vm86 */ 
	.quad quiet_ni_syscall	/* query_module */
	.quad sys_poll
	.quad compat_sys_nfsservctl
	.quad sys_setresgid16	/* 170 */
	.quad sys_getresgid16
	.quad sys_prctl
	.quad stub32_rt_sigreturn
	.quad sys32_rt_sigaction
	.quad sys32_rt_sigprocmask	/* 175 */
	.quad sys32_rt_sigpending
	.quad compat_sys_rt_sigtimedwait
	.quad sys32_rt_sigqueueinfo
	.quad stub32_rt_sigsuspend
	.quad sys32_pread		/* 180 */
	.quad sys32_pwrite
	.quad sys_chown16
	.quad sys_getcwd
	.quad sys_capget
	.quad sys_capset
	.quad stub32_sigaltstack
	.quad sys32_sendfile
	.quad quiet_ni_syscall		/* streams1 */
	.quad quiet_ni_syscall		/* streams2 */
	.quad stub32_vfork            /* 190 */
	.quad compat_sys_getrlimit
	.quad sys32_mmap2
	.quad sys32_truncate64
	.quad sys32_ftruncate64
	.quad sys32_stat64		/* 195 */
	.quad sys32_lstat64
	.quad sys32_fstat64
	.quad sys_lchown
	.quad sys_getuid
	.quad sys_getgid		/* 200 */
	.quad sys_geteuid
	.quad sys_getegid
	.quad sys_setreuid
	.quad sys_setregid
	.quad sys_getgroups	/* 205 */
	.quad sys_setgroups
	.quad sys_fchown
	.quad sys_setresuid
	.quad sys_getresuid
	.quad sys_setresgid	/* 210 */
	.quad sys_getresgid
	.quad sys_chown
	.quad sys_setuid
	.quad sys_setgid
	.quad sys_setfsuid		/* 215 */
	.quad sys_setfsgid
	.quad sys_pivot_root
	.quad sys_mincore
	.quad sys_madvise
	.quad compat_sys_getdents64	/* 220 getdents64 */
	.quad compat_sys_fcntl64	
	.quad quiet_ni_syscall		/* tux */
	.quad quiet_ni_syscall    	/* security */
	.quad sys_gettid	
	.quad sys_readahead	/* 225 */
	.quad sys_setxattr
	.quad sys_lsetxattr
	.quad sys_fsetxattr
	.quad sys_getxattr
	.quad sys_lgetxattr	/* 230 */
	.quad sys_fgetxattr
	.quad sys_listxattr
	.quad sys_llistxattr
	.quad sys_flistxattr
	.quad sys_removexattr	/* 235 */
	.quad sys_lremovexattr
	.quad sys_fremovexattr
	.quad sys_tkill
	.quad sys_sendfile64 
	.quad compat_sys_futex		/* 240 */
	.quad compat_sys_sched_setaffinity
	.quad compat_sys_sched_getaffinity
	.quad sys32_set_thread_area
	.quad sys32_get_thread_area
	.quad compat_sys_io_setup	/* 245 */
	.quad sys_io_destroy
	.quad compat_sys_io_getevents
	.quad compat_sys_io_submit
	.quad sys_io_cancel
	.quad sys_fadvise64		/* 250 */
	.quad quiet_ni_syscall 	/* free_huge_pages */
	.quad sys_exit_group
	.quad sys32_lookup_dcookie
	.quad sys_epoll_create
	.quad sys_epoll_ctl		/* 255 */
	.quad sys_epoll_wait
	.quad sys_remap_file_pages
	.quad sys_set_tid_address
	.quad compat_sys_timer_create
	.quad compat_sys_timer_settime	/* 260 */
	.quad compat_sys_timer_gettime
	.quad sys_timer_getoverrun
	.quad sys_timer_delete
	.quad compat_sys_clock_settime
	.quad compat_sys_clock_gettime	/* 265 */
	.quad compat_sys_clock_getres
	.quad compat_sys_clock_nanosleep
	.quad compat_sys_statfs64
	.quad compat_sys_fstatfs64
	.quad sys_tgkill		/* 270 */
	.quad compat_sys_utimes
	.quad sys32_fadvise64_64
	.quad quiet_ni_syscall	/* sys_vserver */
	.quad sys_mbind
	.quad compat_sys_get_mempolicy	/* 275 */
	.quad sys_set_mempolicy
	.quad compat_sys_mq_open
	.quad sys_mq_unlink
	.quad compat_sys_mq_timedsend
	.quad compat_sys_mq_timedreceive	/* 280 */
	.quad compat_sys_mq_notify
	.quad compat_sys_mq_getsetattr
	.quad compat_sys_kexec_load	/* reserved for kexec */
	.quad compat_sys_waitid
	.quad quiet_ni_syscall		/* 285: sys_altroot */
	.quad sys_add_key
	.quad sys_request_key
	.quad sys_keyctl
	.quad sys_ioprio_set
	.quad sys_ioprio_get		/* 290 */
	.quad sys_inotify_init
	.quad sys_inotify_add_watch
	.quad sys_inotify_rm_watch
	.quad sys_migrate_pages
	.quad compat_sys_openat		/* 295 */
	.quad sys_mkdirat
	.quad sys_mknodat
	.quad sys_fchownat
	.quad compat_sys_futimesat
	.quad sys32_fstatat		/* 300 */
	.quad sys_unlinkat
	.quad sys_renameat
	.quad sys_linkat
	.quad sys_symlinkat
	.quad sys_readlinkat		/* 305 */
	.quad sys_fchmodat
	.quad sys_faccessat
	.quad sys_ni_syscall		/* pselect6 for now */
	.quad sys_ni_syscall		/* ppoll for now */
	.quad sys_unshare		/* 310 */
	.quad compat_sys_set_robust_list
	.quad compat_sys_get_robust_list
ia32_syscall_end:		
	.rept IA32_NR_syscalls-(ia32_syscall_end-ia32_sys_call_table)/8
		.quad ni_syscall
	.endr
