#include <asm/asmmacro.h>
#include <asm/ia32.h>
#include <asm/asm-offsets.h>
#include <asm/signal.h>
#include <asm/thread_info.h>

#include "../kernel/minstate.h"

	/*
	 * execve() is special because in case of success, we need to
	 * setup a null register window frame (in case an IA-32 process
	 * is exec'ing an IA-64 program).
	 */
ENTRY(ia32_execve)
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(3)
	alloc loc1=ar.pfs,3,2,4,0
	mov loc0=rp
	.body
	zxt4 out0=in0			// filename
	;;				// stop bit between alloc and call
	zxt4 out1=in1			// argv
	zxt4 out2=in2			// envp
	add out3=16,sp			// regs
	br.call.sptk.few rp=sys32_execve
1:	cmp.ge p6,p0=r8,r0
	mov ar.pfs=loc1			// restore ar.pfs
	;;
(p6)	mov ar.pfs=r0			// clear ar.pfs in case of success
	sxt4 r8=r8			// return 64-bit result
	mov rp=loc0
	br.ret.sptk.few rp
END(ia32_execve)

ENTRY(ia32_clone)
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(5)
	alloc r16=ar.pfs,5,2,6,0
	DO_SAVE_SWITCH_STACK
	mov loc0=rp
	mov loc1=r16				// save ar.pfs across do_fork
	.body
	zxt4 out1=in1				// newsp
	mov out3=16				// stacksize (compensates for 16-byte scratch area)
	adds out2=IA64_SWITCH_STACK_SIZE+16,sp	// out2 = &regs
	mov out0=in0				// out0 = clone_flags
	zxt4 out4=in2				// out4 = parent_tidptr
	zxt4 out5=in4				// out5 = child_tidptr
	br.call.sptk.many rp=do_fork
.ret0:	.restore sp
	adds sp=IA64_SWITCH_STACK_SIZE,sp	// pop the switch stack
	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(ia32_clone)

GLOBAL_ENTRY(ia32_ret_from_clone)
	PT_REGS_UNWIND_INFO(0)
{	/*
	 * Some versions of gas generate bad unwind info if the first instruction of a
	 * procedure doesn't go into the first slot of a bundle.  This is a workaround.
	 */
	nop.m 0
	nop.i 0
	/*
	 * We need to call schedule_tail() to complete the scheduling process.
	 * Called by ia64_switch_to after do_fork()->copy_thread().  r8 contains the
	 * address of the previously executing task.
	 */
	br.call.sptk.many rp=ia64_invoke_schedule_tail
}
.ret1:
	adds r2=TI_FLAGS+IA64_TASK_SIZE,r13
	;;
	ld4 r2=[r2]
	;;
	mov r8=0
	and r2=_TIF_SYSCALL_TRACEAUDIT,r2
	;;
	cmp.ne p6,p0=r2,r0
(p6)	br.cond.spnt .ia32_strace_check_retval
	;;					// prevent RAW on r8
END(ia32_ret_from_clone)
	// fall thrugh
GLOBAL_ENTRY(ia32_ret_from_syscall)
	PT_REGS_UNWIND_INFO(0)

	cmp.ge p6,p7=r8,r0                      // syscall executed successfully?
	adds r2=IA64_PT_REGS_R8_OFFSET+16,sp    // r2 = &pt_regs.r8
	;;
	alloc r3=ar.pfs,0,0,0,0			// drop the syscall argument frame
	st8 [r2]=r8                             // store return value in slot for r8
	br.cond.sptk.many ia64_leave_kernel
END(ia32_ret_from_syscall)

	//
	// Invoke a system call, but do some tracing before and after the call.
	// We MUST preserve the current register frame throughout this routine
	// because some system calls (such as ia64_execve) directly
	// manipulate ar.pfs.
	//
	// Input:
	//	r8 = syscall number
	//	b6 = syscall entry point
	//
GLOBAL_ENTRY(ia32_trace_syscall)
	PT_REGS_UNWIND_INFO(0)
	mov r3=-38
	adds r2=IA64_PT_REGS_R8_OFFSET+16,sp
	;;
	st8 [r2]=r3				// initialize return code to -ENOSYS
	br.call.sptk.few rp=syscall_trace_enter	// give parent a chance to catch syscall args
	cmp.lt p6,p0=r8,r0			// check tracehook
	adds r2=IA64_PT_REGS_R8_OFFSET+16,sp	// r2 = &pt_regs.r8
	;;
(p6)	st8.spill [r2]=r8			// store return value in slot for r8
(p6)	br.spnt.few .ret4
.ret2:	// Need to reload arguments (they may be changed by the tracing process)
	adds r2=IA64_PT_REGS_R1_OFFSET+16,sp	// r2 = &pt_regs.r1
	adds r3=IA64_PT_REGS_R13_OFFSET+16,sp	// r3 = &pt_regs.r13
	mov r15=IA32_NR_syscalls
	;;
	ld4 r8=[r2],IA64_PT_REGS_R9_OFFSET-IA64_PT_REGS_R1_OFFSET
	movl r16=ia32_syscall_table
	;;
	ld4 r33=[r2],8				// r9 == ecx
	ld4 r37=[r3],16				// r13 == ebp
	cmp.ltu.unc p6,p7=r8,r15
	;;
	ld4 r34=[r2],8				// r10 == edx
	ld4 r36=[r3],8				// r15 == edi
(p6)	shladd r16=r8,3,r16	// force ni_syscall if not valid syscall number
	;;
	ld8 r16=[r16]
	;;
	ld4 r32=[r2],8				// r11 == ebx
	mov b6=r16
	ld4 r35=[r3],8				// r14 == esi
	br.call.sptk.few rp=b6			// do the syscall
.ia32_strace_check_retval:
	cmp.lt p6,p0=r8,r0			// syscall failed?
	adds r2=IA64_PT_REGS_R8_OFFSET+16,sp	// r2 = &pt_regs.r8
	;;
	st8.spill [r2]=r8			// store return value in slot for r8
	br.call.sptk.few rp=syscall_trace_leave	// give parent a chance to catch return value
.ret4:	alloc r2=ar.pfs,0,0,0,0			// drop the syscall argument frame
	br.cond.sptk.many ia64_leave_kernel
END(ia32_trace_syscall)

GLOBAL_ENTRY(sys32_vfork)
	alloc r16=ar.pfs,2,2,4,0;;
	mov out0=IA64_CLONE_VFORK|IA64_CLONE_VM|SIGCHLD	// out0 = clone_flags
	br.cond.sptk.few .fork1			// do the work
END(sys32_vfork)

GLOBAL_ENTRY(sys32_fork)
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(2)
	alloc r16=ar.pfs,2,2,4,0
	mov out0=SIGCHLD			// out0 = clone_flags
	;;
.fork1:
	mov loc0=rp
	mov loc1=r16				// save ar.pfs across do_fork
	DO_SAVE_SWITCH_STACK

	.body

	mov out1=0
	mov out3=0
	adds out2=IA64_SWITCH_STACK_SIZE+16,sp	// out2 = &regs
	br.call.sptk.few rp=do_fork
.ret5:	.restore sp
	adds sp=IA64_SWITCH_STACK_SIZE,sp	// pop the switch stack
	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(sys32_fork)

	.rodata
	.align 8
	.globl ia32_syscall_table
ia32_syscall_table:
	data8 sys_ni_syscall	  /* 0	-  old "setup(" system call*/
	data8 sys_exit
	data8 sys32_fork
	data8 sys_read
	data8 sys_write
	data8 compat_sys_open	  /* 5 */
	data8 sys_close
	data8 sys32_waitpid
	data8 sys_creat
	data8 sys_link
	data8 sys_unlink	  /* 10 */
	data8 ia32_execve
	data8 sys_chdir
	data8 compat_sys_time
	data8 sys_mknod
	data8 sys_chmod		  /* 15 */
	data8 sys_lchown	/* 16-bit version */
	data8 sys_ni_syscall	  /* old break syscall holder */
	data8 sys_ni_syscall
	data8 sys32_lseek
	data8 sys_getpid	  /* 20 */
	data8 compat_sys_mount
	data8 sys_oldumount
	data8 sys_setuid	/* 16-bit version */
	data8 sys_getuid	/* 16-bit version */
	data8 compat_sys_stime    /* 25 */
	data8 compat_sys_ptrace
	data8 sys32_alarm
	data8 sys_ni_syscall
	data8 sys_pause
	data8 compat_sys_utime	  /* 30 */
	data8 sys_ni_syscall	  /* old stty syscall holder */
	data8 sys_ni_syscall	  /* old gtty syscall holder */
	data8 sys_access
	data8 sys_nice
	data8 sys_ni_syscall	  /* 35 */	  /* old ftime syscall holder */
	data8 sys_sync
	data8 sys_kill
	data8 sys_rename
	data8 sys_mkdir
	data8 sys_rmdir		  /* 40 */
	data8 sys_dup
	data8 sys_ia64_pipe
	data8 compat_sys_times
	data8 sys_ni_syscall	  /* old prof syscall holder */
	data8 sys32_brk		  /* 45 */
	data8 sys_setgid	/* 16-bit version */
	data8 sys_getgid	/* 16-bit version */
	data8 sys32_signal
	data8 sys_geteuid	/* 16-bit version */
	data8 sys_getegid	/* 16-bit version */	  /* 50 */
	data8 sys_acct
	data8 sys_umount	  /* recycled never used phys( */
	data8 sys_ni_syscall	  /* old lock syscall holder */
	data8 compat_sys_ioctl
	data8 compat_sys_fcntl	  /* 55 */
	data8 sys_ni_syscall	  /* old mpx syscall holder */
	data8 sys_setpgid
	data8 sys_ni_syscall	  /* old ulimit syscall holder */
	data8 sys_ni_syscall
	data8 sys_umask		  /* 60 */
	data8 sys_chroot
	data8 sys_ustat
	data8 sys_dup2
	data8 sys_getppid
	data8 sys_getpgrp	  /* 65 */
	data8 sys_setsid
	data8 sys32_sigaction
	data8 sys_ni_syscall
	data8 sys_ni_syscall
	data8 sys_setreuid	/* 16-bit version */	  /* 70 */
	data8 sys_setregid	/* 16-bit version */
	data8 sys32_sigsuspend
	data8 compat_sys_sigpending
	data8 sys_sethostname
	data8 compat_sys_setrlimit	  /* 75 */
	data8 compat_sys_old_getrlimit
	data8 compat_sys_getrusage
	data8 compat_sys_gettimeofday
	data8 compat_sys_settimeofday
	data8 sys32_getgroups16	  /* 80 */
	data8 sys32_setgroups16
	data8 sys32_old_select
	data8 sys_symlink
	data8 sys_ni_syscall
	data8 sys_readlink	  /* 85 */
	data8 sys_uselib
	data8 sys_swapon
	data8 sys_reboot
	data8 compat_sys_old_readdir
	data8 sys32_mmap	  /* 90 */
	data8 sys32_munmap
	data8 sys_truncate
	data8 sys_ftruncate
	data8 sys_fchmod
	data8 sys_fchown	/* 16-bit version */	  /* 95 */
	data8 sys_getpriority
	data8 sys_setpriority
	data8 sys_ni_syscall	  /* old profil syscall holder */
	data8 compat_sys_statfs
	data8 compat_sys_fstatfs	  /* 100 */
	data8 sys_ni_syscall	/* ioperm */
	data8 compat_sys_socketcall
	data8 sys_syslog
	data8 compat_sys_setitimer
	data8 compat_sys_getitimer	  /* 105 */
	data8 compat_sys_newstat
	data8 compat_sys_newlstat
	data8 compat_sys_newfstat
	data8 sys_ni_syscall
	data8 sys_ni_syscall	/* iopl */	/* 110 */
	data8 sys_vhangup
	data8 sys_ni_syscall		/* used to be sys_idle */
	data8 sys_ni_syscall
	data8 compat_sys_wait4
	data8 sys_swapoff	  /* 115 */
	data8 compat_sys_sysinfo
	data8 sys32_ipc
	data8 sys_fsync
	data8 sys32_sigreturn
	data8 ia32_clone	  /* 120 */
	data8 sys_setdomainname
	data8 sys32_newuname
	data8 sys32_modify_ldt
	data8 compat_sys_adjtimex
	data8 sys32_mprotect	  /* 125 */
	data8 compat_sys_sigprocmask
	data8 sys_ni_syscall	/* create_module */
	data8 sys_ni_syscall	/* init_module */
	data8 sys_ni_syscall	/* delete_module */
	data8 sys_ni_syscall	/* get_kernel_syms */  /* 130 */
	data8 sys32_quotactl
	data8 sys_getpgid
	data8 sys_fchdir
	data8 sys_ni_syscall	/* sys_bdflush */
	data8 sys_sysfs		/* 135 */
	data8 sys32_personality
	data8 sys_ni_syscall	  /* for afs_syscall */
	data8 sys_setfsuid	/* 16-bit version */
	data8 sys_setfsgid	/* 16-bit version */
	data8 sys_llseek	  /* 140 */
	data8 compat_sys_getdents
	data8 compat_sys_select
	data8 sys_flock
	data8 sys32_msync
	data8 compat_sys_readv	  /* 145 */
	data8 compat_sys_writev
	data8 sys_getsid
	data8 sys_fdatasync
	data8 sys32_sysctl
	data8 sys_mlock		  /* 150 */
	data8 sys_munlock
	data8 sys_mlockall
	data8 sys_munlockall
	data8 sys_sched_setparam
	data8 sys_sched_getparam  /* 155 */
	data8 sys_sched_setscheduler
	data8 sys_sched_getscheduler
	data8 sys_sched_yield
	data8 sys_sched_get_priority_max
	data8 sys_sched_get_priority_min	 /* 160 */
	data8 sys32_sched_rr_get_interval
	data8 compat_sys_nanosleep
	data8 sys32_mremap
	data8 sys_setresuid	/* 16-bit version */
	data8 sys32_getresuid16	/* 16-bit version */	  /* 165 */
	data8 sys_ni_syscall	/* vm86 */
	data8 sys_ni_syscall	/* sys_query_module */
	data8 sys_poll
	data8 sys_ni_syscall	/* nfsservctl */
	data8 sys_setresgid	  /* 170 */
	data8 sys32_getresgid16
	data8 sys_prctl
	data8 sys32_rt_sigreturn
	data8 sys32_rt_sigaction
	data8 sys32_rt_sigprocmask /* 175 */
	data8 sys_rt_sigpending
	data8 compat_sys_rt_sigtimedwait
	data8 sys32_rt_sigqueueinfo
	data8 compat_sys_rt_sigsuspend
	data8 sys32_pread	  /* 180 */
	data8 sys32_pwrite
	data8 sys_chown	/* 16-bit version */
	data8 sys_getcwd
	data8 sys_capget
	data8 sys_capset	  /* 185 */
	data8 sys32_sigaltstack
	data8 sys32_sendfile
	data8 sys_ni_syscall		  /* streams1 */
	data8 sys_ni_syscall		  /* streams2 */
	data8 sys32_vfork	  /* 190 */
	data8 compat_sys_getrlimit
	data8 sys32_mmap2
	data8 sys32_truncate64
	data8 sys32_ftruncate64
	data8 sys32_stat64	  /* 195 */
	data8 sys32_lstat64
	data8 sys32_fstat64
	data8 sys_lchown
	data8 sys_getuid
	data8 sys_getgid	  /* 200 */
	data8 sys_geteuid
	data8 sys_getegid
	data8 sys_setreuid
	data8 sys_setregid
	data8 sys_getgroups	  /* 205 */
	data8 sys_setgroups
	data8 sys_fchown
	data8 sys_setresuid
	data8 sys_getresuid
	data8 sys_setresgid	  /* 210 */
	data8 sys_getresgid
	data8 sys_chown
	data8 sys_setuid
	data8 sys_setgid
	data8 sys_setfsuid	  /* 215 */
	data8 sys_setfsgid
	data8 sys_pivot_root
	data8 sys_mincore
	data8 sys_madvise
	data8 compat_sys_getdents64	  /* 220 */
	data8 compat_sys_fcntl64
	data8 sys_ni_syscall		/* reserved for TUX */
	data8 sys_ni_syscall		/* reserved for Security */
	data8 sys_gettid
	data8 sys_readahead	  /* 225 */
 	data8 sys_setxattr
 	data8 sys_lsetxattr
 	data8 sys_fsetxattr
 	data8 sys_getxattr
 	data8 sys_lgetxattr	/* 230 */
 	data8 sys_fgetxattr
 	data8 sys_listxattr
 	data8 sys_llistxattr
 	data8 sys_flistxattr
 	data8 sys_removexattr	/* 235 */
 	data8 sys_lremovexattr
 	data8 sys_fremovexattr
	data8 sys_tkill
 	data8 sys_sendfile64
	data8 compat_sys_futex	/* 240 */
	data8 compat_sys_sched_setaffinity
	data8 compat_sys_sched_getaffinity
	data8 sys32_set_thread_area
	data8 sys32_get_thread_area
 	data8 compat_sys_io_setup	/* 245 */
 	data8 sys_io_destroy
 	data8 compat_sys_io_getevents
 	data8 compat_sys_io_submit
 	data8 sys_io_cancel
 	data8 sys_fadvise64	/* 250 */
	data8 sys_ni_syscall
	data8 sys_exit_group
 	data8 sys_lookup_dcookie
	data8 sys_epoll_create
	data8 sys32_epoll_ctl	/* 255 */
	data8 sys32_epoll_wait
	data8 sys_remap_file_pages
	data8 sys_set_tid_address
 	data8 compat_sys_timer_create
 	data8 compat_sys_timer_settime	/* 260 */
 	data8 compat_sys_timer_gettime
 	data8 sys_timer_getoverrun
 	data8 sys_timer_delete
 	data8 compat_sys_clock_settime
 	data8 compat_sys_clock_gettime /* 265 */
 	data8 compat_sys_clock_getres
 	data8 compat_sys_clock_nanosleep
	data8 compat_sys_statfs64
	data8 compat_sys_fstatfs64
 	data8 sys_tgkill	/* 270 */
 	data8 compat_sys_utimes
 	data8 sys32_fadvise64_64
 	data8 sys_ni_syscall
  	data8 sys_ni_syscall
 	data8 sys_ni_syscall	/* 275 */
  	data8 sys_ni_syscall
  	data8 compat_sys_mq_open
  	data8 sys_mq_unlink
  	data8 compat_sys_mq_timedsend
  	data8 compat_sys_mq_timedreceive	/* 280 */
  	data8 compat_sys_mq_notify
  	data8 compat_sys_mq_getsetattr
	data8 sys_ni_syscall		/* reserved for kexec */
	data8 compat_sys_waitid

	// guard against failures to increase IA32_NR_syscalls
	.org ia32_syscall_table + 8*IA32_NR_syscalls
