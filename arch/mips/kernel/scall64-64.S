/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2000, 01, 02 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/irqflags.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/asm-offsets.h>
#include <asm/sysmips.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>
#include <asm/war.h>

#ifndef CONFIG_BINFMT_ELF32
/* Neither O32 nor N32, so define handle_sys here */
#define handle_sys64 handle_sys
#endif

	.align  5
NESTED(handle_sys64, PT_SIZE, sp)
#if !defined(CONFIG_MIPS32_O32) && !defined(CONFIG_MIPS32_N32)
	/*
	 * When 32-bit compatibility is configured scall_o32.S
	 * already did this.
	 */
	.set	noat
	SAVE_SOME
	TRACE_IRQS_ON_RELOAD
	STI
	.set	at
#endif

	dsubu	t0, v0, __NR_64_Linux	# check syscall number
	sltiu	t0, t0,	__NR_64_Linux_syscalls + 1
#if !defined(CONFIG_MIPS32_O32) && !defined(CONFIG_MIPS32_N32)
	ld	t1, PT_EPC(sp)		# skip syscall on return
	daddiu	t1, 4			# skip to next instruction
	sd	t1, PT_EPC(sp)
#endif
	beqz	t0, illegal_syscall

	dsll	t0, v0, 3		# offset into table
	ld	t2, (sys_call_table - (__NR_64_Linux * 8))(t0)
					# syscall routine

	sd	a3, PT_R26(sp)		# save a3 for syscall restarting

	li	t1, _TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT
	LONG_L	t0, TI_FLAGS($28)	# syscall tracing enabled?
	and	t0, t1, t0
	bnez	t0, syscall_trace_entry

	jalr	t2			# Do The Real Thing (TM)

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sd	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	dnegu	v0			# error
	sd	v0, PT_R0(sp)		# set flag for syscall
					# restarting
1:	sd	v0, PT_R2(sp)		# result

n64_syscall_exit:
	local_irq_disable		# make sure need_resched and
					# signals dont change between
					# sampling and return
	LONG_L	a2, TI_FLAGS($28)	# current->work
	li	t0, _TIF_ALLWORK_MASK
	and	t0, a2, t0
	bnez	t0, n64_syscall_exit_work

	j	restore_partial

n64_syscall_exit_work:
	j	syscall_exit_work_partial

/* ------------------------------------------------------------------------ */

syscall_trace_entry:
	SAVE_STATIC
	move	s0, t2
	move	a0, sp
	li	a1, 0
	jal	do_syscall_trace

	move	t0, s0
	RESTORE_STATIC
	ld	a0, PT_R4(sp)		# Restore argument registers
	ld	a1, PT_R5(sp)
	ld	a2, PT_R6(sp)
	ld	a3, PT_R7(sp)
	ld	a4, PT_R8(sp)
	ld	a5, PT_R9(sp)
	jalr	t0

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sd	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	dnegu	v0			# error
	sd	v0, PT_R0(sp)		# set flag for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

	j	syscall_exit

illegal_syscall:
	/* This also isn't a 64-bit syscall, throw an error.  */
	li	v0, ENOSYS			# error
	sd	v0, PT_R2(sp)
	li	t0, 1				# set error flag
	sd	t0, PT_R7(sp)
	j	n64_syscall_exit
	END(handle_sys64)

	LEAF(mips_atomic_set)
	andi	v0, a1, 3			# must be word aligned
	bnez	v0, bad_alignment

	LONG_L	v1, TI_ADDR_LIMIT($28)		# in legal address range?
	LONG_ADDIU	a0, a1, 4
	or	a0, a0, a1
	and	a0, a0, v1
	bltz	a0, bad_address

#ifdef CONFIG_CPU_HAS_LLSC
	/* Ok, this is the ll/sc case.  World is sane :-)  */
1:	ll	v0, (a1)
	move	a0, a2
2:	sc	a0, (a1)
#if R10000_LLSC_WAR
	beqzl	a0, 1b
#else
	beqz	a0, 1b
#endif

	.section __ex_table,"a"
	PTR	1b, bad_stack
	PTR	2b, bad_stack
	.previous
#else
	sw	a1, 16(sp)
	sw	a2, 20(sp)

	move	a0, sp
	move	a2, a1
	li	a1, 1
	jal	do_page_fault

	lw	a1, 16(sp)
	lw	a2, 20(sp)

	/*
	 * At this point the page should be readable and writable unless
	 * there was no more memory available.
	 */
1:	lw	v0, (a1)
2:	sw	a2, (a1)

	.section __ex_table,"a"
	PTR	1b, no_mem
	PTR	2b, no_mem
	.previous
#endif

	sd	zero, PT_R7(sp)		# success
	sd	v0, PT_R2(sp)		# result

	j	n64_syscall_exit	# continue like a normal syscall

no_mem:	li	v0, -ENOMEM
	jr	ra

bad_address:
	li	v0, -EFAULT
	jr	ra

bad_alignment:
	li	v0, -EINVAL
	jr	ra
	END(mips_atomic_set)

	LEAF(sys_sysmips)
	beq	a0, MIPS_ATOMIC_SET, mips_atomic_set
	j	_sys_sysmips
	END(sys_sysmips)

	.align	3
sys_call_table:
	PTR	sys_read			/* 5000 */
	PTR	sys_write
	PTR	sys_open
	PTR	sys_close
	PTR	sys_newstat
	PTR	sys_newfstat			/* 5005 */
	PTR	sys_newlstat
	PTR	sys_poll
	PTR	sys_lseek
	PTR	sys_mips_mmap
	PTR	sys_mprotect			/* 5010 */
	PTR	sys_munmap
	PTR	sys_brk
	PTR	sys_rt_sigaction
	PTR	sys_rt_sigprocmask
	PTR	sys_ioctl			/* 5015 */
	PTR	sys_pread64
	PTR	sys_pwrite64
	PTR	sys_readv
	PTR	sys_writev
	PTR	sys_access			/* 5020 */
	PTR	sysm_pipe
	PTR	sys_select
	PTR	sys_sched_yield
	PTR	sys_mremap
	PTR	sys_msync			/* 5025 */
	PTR	sys_mincore
	PTR	sys_madvise
	PTR	sys_shmget
	PTR	sys_shmat
	PTR	sys_shmctl			/* 5030 */
	PTR	sys_dup
	PTR	sys_dup2
	PTR	sys_pause
	PTR	sys_nanosleep
	PTR	sys_getitimer			/* 5035 */
	PTR	sys_setitimer
	PTR	sys_alarm
	PTR	sys_getpid
	PTR	sys_sendfile64
	PTR	sys_socket			/* 5040 */
	PTR	sys_connect
	PTR	sys_accept
	PTR	sys_sendto
	PTR	sys_recvfrom
	PTR	sys_sendmsg			/* 5045 */
	PTR	sys_recvmsg
	PTR	sys_shutdown
	PTR	sys_bind
	PTR	sys_listen
	PTR	sys_getsockname			/* 5050 */
	PTR	sys_getpeername
	PTR	sys_socketpair
	PTR	sys_setsockopt
	PTR	sys_getsockopt
	PTR	sys_clone			/* 5055 */
	PTR	sys_fork
	PTR	sys_execve
	PTR	sys_exit
	PTR	sys_wait4
	PTR	sys_kill			/* 5060 */
	PTR	sys_newuname
	PTR	sys_semget
	PTR	sys_semop
	PTR	sys_semctl
	PTR	sys_shmdt			/* 5065 */
	PTR	sys_msgget
	PTR	sys_msgsnd
	PTR	sys_msgrcv
	PTR	sys_msgctl
	PTR	sys_fcntl			/* 5070 */
	PTR	sys_flock
	PTR	sys_fsync
	PTR	sys_fdatasync
	PTR	sys_truncate
	PTR	sys_ftruncate			/* 5075 */
	PTR	sys_getdents
	PTR	sys_getcwd
	PTR	sys_chdir
	PTR	sys_fchdir
	PTR	sys_rename			/* 5080 */
	PTR	sys_mkdir
	PTR	sys_rmdir
	PTR	sys_creat
	PTR	sys_link
	PTR	sys_unlink			/* 5085 */
	PTR	sys_symlink
	PTR	sys_readlink
	PTR	sys_chmod
	PTR	sys_fchmod
	PTR	sys_chown			/* 5090 */
	PTR	sys_fchown
	PTR	sys_lchown
	PTR	sys_umask
	PTR	sys_gettimeofday
	PTR	sys_getrlimit			/* 5095 */
	PTR	sys_getrusage
	PTR	sys_sysinfo
	PTR	sys_times
	PTR	sys_ptrace
	PTR	sys_getuid			/* 5100 */
	PTR	sys_syslog
	PTR	sys_getgid
	PTR	sys_setuid
	PTR	sys_setgid
	PTR	sys_geteuid			/* 5105 */
	PTR	sys_getegid
	PTR	sys_setpgid
	PTR	sys_getppid
	PTR	sys_getpgrp
	PTR	sys_setsid			/* 5110 */
	PTR	sys_setreuid
	PTR	sys_setregid
	PTR	sys_getgroups
	PTR	sys_setgroups
	PTR	sys_setresuid			/* 5115 */
	PTR	sys_getresuid
	PTR	sys_setresgid
	PTR	sys_getresgid
	PTR	sys_getpgid
	PTR	sys_setfsuid			/* 5120 */
	PTR	sys_setfsgid
	PTR	sys_getsid
	PTR	sys_capget
	PTR	sys_capset
	PTR	sys_rt_sigpending		/* 5125 */
	PTR	sys_rt_sigtimedwait
	PTR	sys_rt_sigqueueinfo
	PTR	sys_rt_sigsuspend
	PTR	sys_sigaltstack
	PTR	sys_utime			/* 5130 */
	PTR	sys_mknod
	PTR	sys_personality
	PTR	sys_ustat
	PTR	sys_statfs
	PTR	sys_fstatfs			/* 5135 */
	PTR	sys_sysfs
	PTR	sys_getpriority
	PTR	sys_setpriority
	PTR	sys_sched_setparam
	PTR	sys_sched_getparam		/* 5140 */
	PTR	sys_sched_setscheduler
	PTR	sys_sched_getscheduler
	PTR	sys_sched_get_priority_max
	PTR	sys_sched_get_priority_min
	PTR	sys_sched_rr_get_interval	/* 5145 */
	PTR	sys_mlock
	PTR	sys_munlock
	PTR	sys_mlockall
	PTR	sys_munlockall
	PTR	sys_vhangup			/* 5150 */
	PTR	sys_pivot_root
	PTR	sys_sysctl
	PTR	sys_prctl
	PTR	sys_adjtimex
	PTR	sys_setrlimit			/* 5155 */
	PTR	sys_chroot
	PTR	sys_sync
	PTR	sys_acct
	PTR	sys_settimeofday
	PTR	sys_mount			/* 5160 */
	PTR	sys_umount
	PTR	sys_swapon
	PTR	sys_swapoff
	PTR	sys_reboot
	PTR	sys_sethostname			/* 5165 */
	PTR	sys_setdomainname
	PTR	sys_ni_syscall			/* was create_module */
	PTR	sys_init_module
	PTR	sys_delete_module
	PTR	sys_ni_syscall			/* 5170, was get_kernel_syms */
	PTR	sys_ni_syscall			/* was query_module */
	PTR	sys_quotactl
	PTR	sys_nfsservctl
	PTR	sys_ni_syscall			/* res. for getpmsg */
	PTR	sys_ni_syscall			/* 5175  for putpmsg */
	PTR	sys_ni_syscall			/* res. for afs_syscall */
	PTR	sys_ni_syscall			/* res. for security */
	PTR	sys_gettid
	PTR	sys_readahead
	PTR	sys_setxattr			/* 5180 */
	PTR	sys_lsetxattr
	PTR	sys_fsetxattr
	PTR	sys_getxattr
	PTR	sys_lgetxattr
	PTR	sys_fgetxattr			/* 5185 */
	PTR	sys_listxattr
	PTR	sys_llistxattr
	PTR	sys_flistxattr
	PTR	sys_removexattr
	PTR	sys_lremovexattr		/* 5190 */
	PTR	sys_fremovexattr
	PTR	sys_tkill
	PTR	sys_ni_syscall
	PTR	sys_futex
	PTR	sys_sched_setaffinity		/* 5195 */
	PTR	sys_sched_getaffinity
	PTR	sys_cacheflush
	PTR	sys_cachectl
	PTR	sys_sysmips
	PTR	sys_io_setup			/* 5200 */
	PTR	sys_io_destroy
	PTR	sys_io_getevents
	PTR	sys_io_submit
	PTR	sys_io_cancel
	PTR	sys_exit_group			/* 5205 */
	PTR	sys_lookup_dcookie
	PTR	sys_epoll_create
	PTR	sys_epoll_ctl
	PTR	sys_epoll_wait
	PTR	sys_remap_file_pages		/* 5210 */
	PTR	sys_rt_sigreturn
	PTR	sys_set_tid_address
	PTR	sys_restart_syscall
	PTR	sys_semtimedop
	PTR	sys_fadvise64_64		/* 5215 */
	PTR	sys_timer_create
	PTR	sys_timer_settime
	PTR	sys_timer_gettime
	PTR	sys_timer_getoverrun
	PTR	sys_timer_delete		/* 5220 */
	PTR	sys_clock_settime
	PTR	sys_clock_gettime
	PTR	sys_clock_getres
	PTR	sys_clock_nanosleep
	PTR	sys_tgkill			/* 5225 */
	PTR	sys_utimes
	PTR	sys_mbind
	PTR	sys_ni_syscall			/* sys_get_mempolicy */
	PTR	sys_ni_syscall			/* sys_set_mempolicy */
	PTR	sys_mq_open			/* 5230 */
	PTR	sys_mq_unlink
	PTR	sys_mq_timedsend
	PTR	sys_mq_timedreceive
	PTR	sys_mq_notify
	PTR	sys_mq_getsetattr		/* 5235 */
	PTR	sys_ni_syscall			/* sys_vserver */
	PTR	sys_waitid
	PTR	sys_ni_syscall			/* available, was setaltroot */
	PTR	sys_add_key
	PTR	sys_request_key			/* 5240 */
	PTR	sys_keyctl
	PTR	sys_set_thread_area
	PTR	sys_inotify_init
	PTR	sys_inotify_add_watch
	PTR	sys_inotify_rm_watch		/* 5245 */
	PTR	sys_migrate_pages
	PTR	sys_openat
	PTR	sys_mkdirat
	PTR	sys_mknodat
	PTR	sys_fchownat			/* 5250 */
	PTR	sys_futimesat
	PTR	sys_newfstatat
	PTR	sys_unlinkat
	PTR	sys_renameat
	PTR	sys_linkat			/* 5255 */
	PTR	sys_symlinkat
	PTR	sys_readlinkat
	PTR	sys_fchmodat
	PTR	sys_faccessat
	PTR	sys_pselect6			/* 5260 */
	PTR	sys_ppoll
	PTR	sys_unshare
	PTR	sys_splice
	PTR	sys_sync_file_range
	PTR	sys_tee				/* 5265 */
	PTR	sys_vmsplice
	PTR	sys_move_pages
	PTR	sys_set_robust_list
	PTR	sys_get_robust_list
	PTR	sys_kexec_load			/* 5270 */
	PTR	sys_getcpu
	PTR	sys_epoll_pwait
	PTR	sys_ioprio_set
	PTR	sys_ioprio_get
	PTR	sys_utimensat			/* 5275 */
	PTR	sys_signalfd
	PTR	sys_ni_syscall
	PTR	sys_eventfd
	PTR	sys_fallocate
	PTR	sys_timerfd_create		/* 5280 */
	PTR	sys_timerfd_gettime
	PTR	sys_timerfd_settime
	PTR	sys_signalfd4
	PTR	sys_eventfd2
	PTR	sys_epoll_create1		/* 5285 */
	PTR	sys_dup3
	PTR	sys_pipe2
	PTR	sys_inotify_init1
	PTR	sys_preadv
	PTR	sys_pwritev			/* 5390 */
	.size	sys_call_table,.-sys_call_table
