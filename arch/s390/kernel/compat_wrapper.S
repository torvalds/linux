/*
*    wrapper for 31 bit compatible system calls.
*
*    Copyright IBM Corp. 2000, 2006
*    Author(s): Gerhard Tonn (ton@de.ibm.com),
*		Thomas Spatzier (tspat@de.ibm.com)
*/

#include <linux/linkage.h>

ENTRY(sys32_exit_wrapper)
	lgfr	%r2,%r2			# int
	jg	sys_exit		# branch to sys_exit

ENTRY(sys32_read_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# char *
	llgfr	%r4,%r4			# size_t
	jg	sys32_read		# branch to sys_read

ENTRY(sys32_write_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# const char *
	llgfr	%r4,%r4			# size_t
	jg	sys32_write		# branch to system call

ENTRY(sys32_close_wrapper)
	llgfr	%r2,%r2			# unsigned int
	jg	sys_close		# branch to system call

ENTRY(sys32_creat_wrapper)
	llgtr	%r2,%r2			# const char *
	lgfr	%r3,%r3			# int
	jg	sys_creat		# branch to system call

ENTRY(sys32_link_wrapper)
	llgtr	%r2,%r2			# const char *
	llgtr	%r3,%r3			# const char *
	jg	sys_link		# branch to system call

ENTRY(sys32_unlink_wrapper)
	llgtr	%r2,%r2			# const char *
	jg	sys_unlink		# branch to system call

ENTRY(sys32_chdir_wrapper)
	llgtr	%r2,%r2			# const char *
	jg	sys_chdir		# branch to system call

ENTRY(sys32_time_wrapper)
	llgtr	%r2,%r2			# int *
	jg	compat_sys_time		# branch to system call

ENTRY(sys32_mknod_wrapper)
	llgtr	%r2,%r2			# const char *
	lgfr	%r3,%r3			# int
	llgfr	%r4,%r4			# dev
	jg	sys_mknod		# branch to system call

ENTRY(sys32_chmod_wrapper)
	llgtr	%r2,%r2			# const char *
	llgfr	%r3,%r3			# mode_t
	jg	sys_chmod		# branch to system call

ENTRY(sys32_lchown16_wrapper)
	llgtr	%r2,%r2			# const char *
	llgfr	%r3,%r3			# __kernel_old_uid_emu31_t
	llgfr	%r4,%r4			# __kernel_old_uid_emu31_t
	jg	sys32_lchown16		# branch to system call

#sys32_getpid_wrapper				# void

ENTRY(sys32_mount_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# char *
	llgtr	%r4,%r4			# char *
	llgfr	%r5,%r5			# unsigned long
	llgtr	%r6,%r6			# void *
	jg	compat_sys_mount	# branch to system call

ENTRY(sys32_oldumount_wrapper)
	llgtr	%r2,%r2			# char *
	jg	sys_oldumount		# branch to system call

ENTRY(sys32_setuid16_wrapper)
	llgfr	%r2,%r2			# __kernel_old_uid_emu31_t
	jg	sys32_setuid16		# branch to system call

#sys32_getuid16_wrapper			# void

ENTRY(sys32_ptrace_wrapper)
	lgfr	%r2,%r2			# long
	lgfr	%r3,%r3			# long
	llgtr	%r4,%r4			# long
	llgfr	%r5,%r5			# long
	jg	compat_sys_ptrace	# branch to system call

ENTRY(sys32_alarm_wrapper)
	llgfr	%r2,%r2			# unsigned int
	jg	sys_alarm		# branch to system call

ENTRY(compat_sys_utime_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# struct compat_utimbuf *
	jg	compat_sys_utime	# branch to system call

ENTRY(sys32_access_wrapper)
	llgtr	%r2,%r2			# const char *
	lgfr	%r3,%r3			# int
	jg	sys_access		# branch to system call

ENTRY(sys32_nice_wrapper)
	lgfr	%r2,%r2			# int
	jg	sys_nice		# branch to system call

#sys32_sync_wrapper			# void

ENTRY(sys32_kill_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	jg	sys_kill		# branch to system call

ENTRY(sys32_rename_wrapper)
	llgtr	%r2,%r2			# const char *
	llgtr	%r3,%r3			# const char *
	jg	sys_rename		# branch to system call

ENTRY(sys32_mkdir_wrapper)
	llgtr	%r2,%r2			# const char *
	lgfr	%r3,%r3			# int
	jg	sys_mkdir		# branch to system call

ENTRY(sys32_rmdir_wrapper)
	llgtr	%r2,%r2			# const char *
	jg	sys_rmdir		# branch to system call

ENTRY(sys32_dup_wrapper)
	llgfr	%r2,%r2			# unsigned int
	jg	sys_dup			# branch to system call

ENTRY(sys32_pipe_wrapper)
	llgtr	%r2,%r2			# u32 *
	jg	sys_pipe		# branch to system call

ENTRY(compat_sys_times_wrapper)
	llgtr	%r2,%r2			# struct compat_tms *
	jg	compat_sys_times	# branch to system call

ENTRY(sys32_brk_wrapper)
	llgtr	%r2,%r2			# unsigned long
	jg	sys_brk			# branch to system call

ENTRY(sys32_setgid16_wrapper)
	llgfr	%r2,%r2			# __kernel_old_gid_emu31_t
	jg	sys32_setgid16		# branch to system call

#sys32_getgid16_wrapper			# void

ENTRY(sys32_signal_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# __sighandler_t
	jg	sys_signal

#sys32_geteuid16_wrapper		# void

#sys32_getegid16_wrapper		# void

ENTRY(sys32_acct_wrapper)
	llgtr	%r2,%r2			# char *
	jg	sys_acct		# branch to system call

ENTRY(sys32_umount_wrapper)
	llgtr	%r2,%r2			# char *
	lgfr	%r3,%r3			# int
	jg	sys_umount		# branch to system call

ENTRY(compat_sys_ioctl_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# unsigned int
	llgfr	%r4,%r4			# unsigned int
	jg	compat_sys_ioctl	# branch to system call

ENTRY(compat_sys_fcntl_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# unsigned int
	llgfr	%r4,%r4			# unsigned long
	jg	compat_sys_fcntl	# branch to system call

ENTRY(sys32_setpgid_wrapper)
	lgfr	%r2,%r2			# pid_t
	lgfr	%r3,%r3			# pid_t
	jg	sys_setpgid		# branch to system call

ENTRY(sys32_umask_wrapper)
	lgfr	%r2,%r2			# int
	jg	sys_umask		# branch to system call

ENTRY(sys32_chroot_wrapper)
	llgtr	%r2,%r2			# char *
	jg	sys_chroot		# branch to system call

ENTRY(sys32_ustat_wrapper)
	llgfr	%r2,%r2			# dev_t
	llgtr	%r3,%r3			# struct ustat *
	jg	compat_sys_ustat

ENTRY(sys32_dup2_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# unsigned int
	jg	sys_dup2		# branch to system call

#sys32_getppid_wrapper			# void

#sys32_getpgrp_wrapper			# void

#sys32_setsid_wrapper			# void

ENTRY(sys32_setreuid16_wrapper)
	llgfr	%r2,%r2			# __kernel_old_uid_emu31_t
	llgfr	%r3,%r3			# __kernel_old_uid_emu31_t
	jg	sys32_setreuid16	# branch to system call

ENTRY(sys32_setregid16_wrapper)
	llgfr	%r2,%r2			# __kernel_old_gid_emu31_t
	llgfr	%r3,%r3			# __kernel_old_gid_emu31_t
	jg	sys32_setregid16	# branch to system call

ENTRY(sys_sigsuspend_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	llgfr	%r4,%r4			# old_sigset_t
	jg	sys_sigsuspend

ENTRY(compat_sys_sigpending_wrapper)
	llgtr	%r2,%r2			# compat_old_sigset_t *
	jg	compat_sys_sigpending	# branch to system call

ENTRY(sys32_sethostname_wrapper)
	llgtr	%r2,%r2			# char *
	lgfr	%r3,%r3			# int
	jg	sys_sethostname		# branch to system call

ENTRY(compat_sys_setrlimit_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# struct rlimit_emu31 *
	jg	compat_sys_setrlimit	# branch to system call

ENTRY(compat_sys_old_getrlimit_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# struct rlimit_emu31 *
	jg	compat_sys_old_getrlimit # branch to system call

ENTRY(compat_sys_getrlimit_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# struct rlimit_emu31 *
	jg	compat_sys_getrlimit	# branch to system call

ENTRY(sys32_mmap2_wrapper)
	llgtr	%r2,%r2			# struct mmap_arg_struct_emu31 *
	jg	sys32_mmap2			# branch to system call

ENTRY(compat_sys_getrusage_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# struct rusage_emu31 *
	jg	compat_sys_getrusage	# branch to system call

ENTRY(compat_sys_gettimeofday_wrapper)
	llgtr	%r2,%r2			# struct timeval_emu31 *
	llgtr	%r3,%r3			# struct timezone *
	jg	compat_sys_gettimeofday	# branch to system call

ENTRY(compat_sys_settimeofday_wrapper)
	llgtr	%r2,%r2			# struct timeval_emu31 *
	llgtr	%r3,%r3			# struct timezone *
	jg	compat_sys_settimeofday	# branch to system call

ENTRY(sys32_getgroups16_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# __kernel_old_gid_emu31_t *
	jg	sys32_getgroups16	# branch to system call

ENTRY(sys32_setgroups16_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# __kernel_old_gid_emu31_t *
	jg	sys32_setgroups16	# branch to system call

ENTRY(sys32_symlink_wrapper)
	llgtr	%r2,%r2			# const char *
	llgtr	%r3,%r3			# const char *
	jg	sys_symlink		# branch to system call

ENTRY(sys32_readlink_wrapper)
	llgtr	%r2,%r2			# const char *
	llgtr	%r3,%r3			# char *
	lgfr	%r4,%r4			# int
	jg	sys_readlink		# branch to system call

ENTRY(sys32_uselib_wrapper)
	llgtr	%r2,%r2			# const char *
	jg	sys_uselib		# branch to system call

ENTRY(sys32_swapon_wrapper)
	llgtr	%r2,%r2			# const char *
	lgfr	%r3,%r3			# int
	jg	sys_swapon		# branch to system call

ENTRY(sys32_reboot_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	llgfr	%r4,%r4			# unsigned int
	llgtr	%r5,%r5			# void *
	jg	sys_reboot		# branch to system call

ENTRY(old32_readdir_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# void *
	llgfr	%r4,%r4			# unsigned int
	jg	compat_sys_old_readdir	# branch to system call

ENTRY(old32_mmap_wrapper)
	llgtr	%r2,%r2			# struct mmap_arg_struct_emu31 *
	jg	old32_mmap		# branch to system call

ENTRY(sys32_munmap_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgfr	%r3,%r3			# size_t
	jg	sys_munmap		# branch to system call

ENTRY(sys32_fchmod_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# mode_t
	jg	sys_fchmod		# branch to system call

ENTRY(sys32_fchown16_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# compat_uid_t
	llgfr	%r4,%r4			# compat_uid_t
	jg	sys32_fchown16		# branch to system call

ENTRY(sys32_getpriority_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	jg	sys_getpriority		# branch to system call

ENTRY(sys32_setpriority_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	lgfr	%r4,%r4			# int
	jg	sys_setpriority		# branch to system call

ENTRY(compat_sys_statfs_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# struct compat_statfs *
	jg	compat_sys_statfs	# branch to system call

ENTRY(compat_sys_fstatfs_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# struct compat_statfs *
	jg	compat_sys_fstatfs	# branch to system call

ENTRY(compat_sys_socketcall_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# u32 *
	jg	compat_sys_socketcall	# branch to system call

ENTRY(sys32_syslog_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# char *
	lgfr	%r4,%r4			# int
	jg	sys_syslog		# branch to system call

ENTRY(compat_sys_newstat_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# struct stat_emu31 *
	jg	compat_sys_newstat	# branch to system call

ENTRY(compat_sys_newlstat_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# struct stat_emu31 *
	jg	compat_sys_newlstat	# branch to system call

ENTRY(compat_sys_newfstat_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# struct stat_emu31 *
	jg	compat_sys_newfstat	# branch to system call

#sys32_vhangup_wrapper			# void

ENTRY(sys32_swapoff_wrapper)
	llgtr	%r2,%r2			# const char *
	jg	sys_swapoff		# branch to system call

ENTRY(compat_sys_sysinfo_wrapper)
	llgtr	%r2,%r2			# struct sysinfo_emu31 *
	jg	compat_sys_sysinfo	# branch to system call

ENTRY(sys32_ipc_wrapper)
	llgfr	%r2,%r2			# uint
	lgfr	%r3,%r3			# int
	lgfr	%r4,%r4			# int
	lgfr	%r5,%r5			# int
	llgfr	%r6,%r6			# u32
	jg	sys32_ipc		# branch to system call

ENTRY(sys32_fsync_wrapper)
	llgfr	%r2,%r2			# unsigned int
	jg	sys_fsync		# branch to system call

#sys32_sigreturn_wrapper		# done in sigreturn_glue

#sys32_clone_wrapper			# done in clone_glue

ENTRY(sys32_setdomainname_wrapper)
	llgtr	%r2,%r2			# char *
	lgfr	%r3,%r3			# int
	jg	sys_setdomainname	# branch to system call

ENTRY(sys32_newuname_wrapper)
	llgtr	%r2,%r2			# struct new_utsname *
	jg	sys_newuname		# branch to system call

ENTRY(compat_sys_adjtimex_wrapper)
	llgtr	%r2,%r2			# struct compat_timex *
	jg	compat_sys_adjtimex	# branch to system call

ENTRY(sys32_mprotect_wrapper)
	llgtr	%r2,%r2			# unsigned long (actually pointer
	llgfr	%r3,%r3			# size_t
	llgfr	%r4,%r4			# unsigned long
	jg	sys_mprotect		# branch to system call

ENTRY(sys_init_module_wrapper)
	llgtr	%r2,%r2			# void *
	llgfr	%r3,%r3			# unsigned long
	llgtr	%r4,%r4			# char *
	jg	sys_init_module		# branch to system call

ENTRY(sys_delete_module_wrapper)
	llgtr	%r2,%r2			# const char *
	llgfr	%r3,%r3			# unsigned int
	jg	sys_delete_module	# branch to system call

ENTRY(sys32_quotactl_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# const char *
	llgfr	%r4,%r4			# qid_t
	llgtr	%r5,%r5			# caddr_t
	jg	sys_quotactl		# branch to system call

ENTRY(sys32_getpgid_wrapper)
	lgfr	%r2,%r2			# pid_t
	jg	sys_getpgid		# branch to system call

ENTRY(sys32_fchdir_wrapper)
	llgfr	%r2,%r2			# unsigned int
	jg	sys_fchdir		# branch to system call

ENTRY(sys32_bdflush_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# long
	jg	sys_bdflush		# branch to system call

ENTRY(sys32_sysfs_wrapper)
	lgfr	%r2,%r2			# int
	llgfr	%r3,%r3			# unsigned long
	llgfr	%r4,%r4			# unsigned long
	jg	sys_sysfs		# branch to system call

ENTRY(sys32_personality_wrapper)
	llgfr	%r2,%r2			# unsigned int
	jg	sys_s390_personality	# branch to system call

ENTRY(sys32_setfsuid16_wrapper)
	llgfr	%r2,%r2			# __kernel_old_uid_emu31_t
	jg	sys32_setfsuid16	# branch to system call

ENTRY(sys32_setfsgid16_wrapper)
	llgfr	%r2,%r2			# __kernel_old_gid_emu31_t
	jg	sys32_setfsgid16	# branch to system call

ENTRY(sys32_llseek_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# unsigned long
	llgfr	%r4,%r4			# unsigned long
	llgtr	%r5,%r5			# loff_t *
	llgfr	%r6,%r6			# unsigned int
	jg	sys_llseek		# branch to system call

ENTRY(sys32_getdents_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# void *
	llgfr	%r4,%r4			# unsigned int
	jg	compat_sys_getdents	# branch to system call

ENTRY(compat_sys_select_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# compat_fd_set *
	llgtr	%r4,%r4			# compat_fd_set *
	llgtr	%r5,%r5			# compat_fd_set *
	llgtr	%r6,%r6			# struct compat_timeval *
	jg	compat_sys_select	# branch to system call

ENTRY(sys32_flock_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# unsigned int
	jg	sys_flock		# branch to system call

ENTRY(sys32_msync_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgfr	%r3,%r3			# size_t
	lgfr	%r4,%r4			# int
	jg	sys_msync		# branch to system call

ENTRY(compat_sys_readv_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const struct compat_iovec *
	llgfr	%r4,%r4			# unsigned long
	jg	compat_sys_readv	# branch to system call

ENTRY(compat_sys_writev_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const struct compat_iovec *
	llgfr	%r4,%r4			# unsigned long
	jg	compat_sys_writev	# branch to system call

ENTRY(sys32_getsid_wrapper)
	lgfr	%r2,%r2			# pid_t
	jg	sys_getsid		# branch to system call

ENTRY(sys32_fdatasync_wrapper)
	llgfr	%r2,%r2			# unsigned int
	jg	sys_fdatasync		# branch to system call

ENTRY(sys32_mlock_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgfr	%r3,%r3			# size_t
	jg	sys_mlock		# branch to system call

ENTRY(sys32_munlock_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgfr	%r3,%r3			# size_t
	jg	sys_munlock		# branch to system call

ENTRY(sys32_mlockall_wrapper)
	lgfr	%r2,%r2			# int
	jg	sys_mlockall		# branch to system call

#sys32_munlockall_wrapper		# void

ENTRY(sys32_sched_setparam_wrapper)
	lgfr	%r2,%r2			# pid_t
	llgtr	%r3,%r3			# struct sched_param *
	jg	sys_sched_setparam	# branch to system call

ENTRY(sys32_sched_getparam_wrapper)
	lgfr	%r2,%r2			# pid_t
	llgtr	%r3,%r3			# struct sched_param *
	jg	sys_sched_getparam	# branch to system call

ENTRY(sys32_sched_setscheduler_wrapper)
	lgfr	%r2,%r2			# pid_t
	lgfr	%r3,%r3			# int
	llgtr	%r4,%r4			# struct sched_param *
	jg	sys_sched_setscheduler	# branch to system call

ENTRY(sys32_sched_getscheduler_wrapper)
	lgfr	%r2,%r2			# pid_t
	jg	sys_sched_getscheduler	# branch to system call

#sys32_sched_yield_wrapper		# void

ENTRY(sys32_sched_get_priority_max_wrapper)
	lgfr	%r2,%r2			# int
	jg	sys_sched_get_priority_max	# branch to system call

ENTRY(sys32_sched_get_priority_min_wrapper)
	lgfr	%r2,%r2			# int
	jg	sys_sched_get_priority_min	# branch to system call

ENTRY(compat_sys_nanosleep_wrapper)
	llgtr	%r2,%r2			# struct compat_timespec *
	llgtr	%r3,%r3			# struct compat_timespec *
	jg	compat_sys_nanosleep		# branch to system call

ENTRY(sys32_mremap_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgfr	%r3,%r3			# unsigned long
	llgfr	%r4,%r4			# unsigned long
	llgfr	%r5,%r5			# unsigned long
	llgfr	%r6,%r6			# unsigned long
	jg	sys_mremap		# branch to system call

ENTRY(sys32_setresuid16_wrapper)
	llgfr	%r2,%r2			# __kernel_old_uid_emu31_t
	llgfr	%r3,%r3			# __kernel_old_uid_emu31_t
	llgfr	%r4,%r4			# __kernel_old_uid_emu31_t
	jg	sys32_setresuid16	# branch to system call

ENTRY(sys32_getresuid16_wrapper)
	llgtr	%r2,%r2			# __kernel_old_uid_emu31_t *
	llgtr	%r3,%r3			# __kernel_old_uid_emu31_t *
	llgtr	%r4,%r4			# __kernel_old_uid_emu31_t *
	jg	sys32_getresuid16	# branch to system call

ENTRY(sys32_poll_wrapper)
	llgtr	%r2,%r2			# struct pollfd *
	llgfr	%r3,%r3			# unsigned int
	lgfr	%r4,%r4			# int
	jg	sys_poll		# branch to system call

ENTRY(sys32_setresgid16_wrapper)
	llgfr	%r2,%r2			# __kernel_old_gid_emu31_t
	llgfr	%r3,%r3			# __kernel_old_gid_emu31_t
	llgfr	%r4,%r4			# __kernel_old_gid_emu31_t
	jg	sys32_setresgid16	# branch to system call

ENTRY(sys32_getresgid16_wrapper)
	llgtr	%r2,%r2			# __kernel_old_gid_emu31_t *
	llgtr	%r3,%r3			# __kernel_old_gid_emu31_t *
	llgtr	%r4,%r4			# __kernel_old_gid_emu31_t *
	jg	sys32_getresgid16	# branch to system call

ENTRY(sys32_prctl_wrapper)
	lgfr	%r2,%r2			# int
	llgfr	%r3,%r3			# unsigned long
	llgfr	%r4,%r4			# unsigned long
	llgfr	%r5,%r5			# unsigned long
	llgfr	%r6,%r6			# unsigned long
	jg	sys_prctl		# branch to system call

#sys32_rt_sigreturn_wrapper		# done in rt_sigreturn_glue

ENTRY(sys32_pread64_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# char *
	llgfr	%r4,%r4			# size_t
	llgfr	%r5,%r5			# u32
	llgfr	%r6,%r6			# u32
	jg	sys32_pread64		# branch to system call

ENTRY(sys32_pwrite64_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# const char *
	llgfr	%r4,%r4			# size_t
	llgfr	%r5,%r5			# u32
	llgfr	%r6,%r6			# u32
	jg	sys32_pwrite64		# branch to system call

ENTRY(sys32_chown16_wrapper)
	llgtr	%r2,%r2			# const char *
	llgfr	%r3,%r3			# __kernel_old_uid_emu31_t
	llgfr	%r4,%r4			# __kernel_old_gid_emu31_t
	jg	sys32_chown16		# branch to system call

ENTRY(sys32_getcwd_wrapper)
	llgtr	%r2,%r2			# char *
	llgfr	%r3,%r3			# unsigned long
	jg	sys_getcwd		# branch to system call

ENTRY(sys32_capget_wrapper)
	llgtr	%r2,%r2			# cap_user_header_t
	llgtr	%r3,%r3			# cap_user_data_t
	jg	sys_capget		# branch to system call

ENTRY(sys32_capset_wrapper)
	llgtr	%r2,%r2			# cap_user_header_t
	llgtr	%r3,%r3			# const cap_user_data_t
	jg	sys_capset		# branch to system call

ENTRY(sys32_sendfile_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	llgtr	%r4,%r4			# __kernel_off_emu31_t *
	llgfr	%r5,%r5			# size_t
	jg	sys32_sendfile		# branch to system call

#sys32_vfork_wrapper			# done in vfork_glue

ENTRY(sys32_truncate64_wrapper)
	llgtr	%r2,%r2			# const char *
	llgfr	%r3,%r3			# unsigned long
	llgfr	%r4,%r4			# unsigned long
	jg	sys32_truncate64	# branch to system call

ENTRY(sys32_ftruncate64_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# unsigned long
	llgfr	%r4,%r4			# unsigned long
	jg	sys32_ftruncate64	# branch to system call

ENTRY(sys32_lchown_wrapper)
	llgtr	%r2,%r2			# const char *
	llgfr	%r3,%r3			# uid_t
	llgfr	%r4,%r4			# gid_t
	jg	sys_lchown		# branch to system call

#sys32_getuid_wrapper			# void
#sys32_getgid_wrapper			# void
#sys32_geteuid_wrapper			# void
#sys32_getegid_wrapper			# void

ENTRY(sys32_setreuid_wrapper)
	llgfr	%r2,%r2			# uid_t
	llgfr	%r3,%r3			# uid_t
	jg	sys_setreuid		# branch to system call

ENTRY(sys32_setregid_wrapper)
	llgfr	%r2,%r2			# gid_t
	llgfr	%r3,%r3			# gid_t
	jg	sys_setregid		# branch to system call

ENTRY(sys32_getgroups_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# gid_t *
	jg	sys_getgroups		# branch to system call

ENTRY(sys32_setgroups_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# gid_t *
	jg	sys_setgroups		# branch to system call

ENTRY(sys32_fchown_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# uid_t
	llgfr	%r4,%r4			# gid_t
	jg	sys_fchown		# branch to system call

ENTRY(sys32_setresuid_wrapper)
	llgfr	%r2,%r2			# uid_t
	llgfr	%r3,%r3			# uid_t
	llgfr	%r4,%r4			# uid_t
	jg	sys_setresuid		# branch to system call

ENTRY(sys32_getresuid_wrapper)
	llgtr	%r2,%r2			# uid_t *
	llgtr	%r3,%r3			# uid_t *
	llgtr	%r4,%r4			# uid_t *
	jg	sys_getresuid		# branch to system call

ENTRY(sys32_setresgid_wrapper)
	llgfr	%r2,%r2			# gid_t
	llgfr	%r3,%r3			# gid_t
	llgfr	%r4,%r4			# gid_t
	jg	sys_setresgid		# branch to system call

ENTRY(sys32_getresgid_wrapper)
	llgtr	%r2,%r2			# gid_t *
	llgtr	%r3,%r3			# gid_t *
	llgtr	%r4,%r4			# gid_t *
	jg	sys_getresgid		# branch to system call

ENTRY(sys32_chown_wrapper)
	llgtr	%r2,%r2			# const char *
	llgfr	%r3,%r3			# uid_t
	llgfr	%r4,%r4			# gid_t
	jg	sys_chown		# branch to system call

ENTRY(sys32_setuid_wrapper)
	llgfr	%r2,%r2			# uid_t
	jg	sys_setuid		# branch to system call

ENTRY(sys32_setgid_wrapper)
	llgfr	%r2,%r2			# gid_t
	jg	sys_setgid		# branch to system call

ENTRY(sys32_setfsuid_wrapper)
	llgfr	%r2,%r2			# uid_t
	jg	sys_setfsuid		# branch to system call

ENTRY(sys32_setfsgid_wrapper)
	llgfr	%r2,%r2			# gid_t
	jg	sys_setfsgid		# branch to system call

ENTRY(sys32_pivot_root_wrapper)
	llgtr	%r2,%r2			# const char *
	llgtr	%r3,%r3			# const char *
	jg	sys_pivot_root		# branch to system call

ENTRY(sys32_mincore_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgfr	%r3,%r3			# size_t
	llgtr	%r4,%r4			# unsigned char *
	jg	sys_mincore		# branch to system call

ENTRY(sys32_madvise_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgfr	%r3,%r3			# size_t
	lgfr	%r4,%r4			# int
	jg	sys_madvise		# branch to system call

ENTRY(sys32_getdents64_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# void *
	llgfr	%r4,%r4			# unsigned int
	jg	sys_getdents64		# branch to system call

ENTRY(compat_sys_fcntl64_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# unsigned int
	llgfr	%r4,%r4			# unsigned long
	jg	compat_sys_fcntl64	# branch to system call

ENTRY(sys32_stat64_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# struct stat64 *
	jg	sys32_stat64		# branch to system call

ENTRY(sys32_lstat64_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# struct stat64 *
	jg	sys32_lstat64		# branch to system call

ENTRY(sys32_stime_wrapper)
	llgtr	%r2,%r2			# long *
	jg	compat_sys_stime	# branch to system call

ENTRY(sys32_sysctl_wrapper)
	llgtr	%r2,%r2 		# struct compat_sysctl_args *
	jg	compat_sys_sysctl

ENTRY(sys32_fstat64_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgtr	%r3,%r3			# struct stat64 *
	jg	sys32_fstat64		# branch to system call

ENTRY(sys32_setxattr_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# char *
	llgtr	%r4,%r4			# void *
	llgfr	%r5,%r5			# size_t
	lgfr	%r6,%r6			# int
	jg	sys_setxattr

ENTRY(sys32_lsetxattr_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# char *
	llgtr	%r4,%r4			# void *
	llgfr	%r5,%r5			# size_t
	lgfr	%r6,%r6			# int
	jg	sys_lsetxattr

ENTRY(sys32_fsetxattr_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# char *
	llgtr	%r4,%r4			# void *
	llgfr	%r5,%r5			# size_t
	lgfr	%r6,%r6			# int
	jg	sys_fsetxattr

ENTRY(sys32_getxattr_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# char *
	llgtr	%r4,%r4			# void *
	llgfr	%r5,%r5			# size_t
	jg	sys_getxattr

ENTRY(sys32_lgetxattr_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# char *
	llgtr	%r4,%r4			# void *
	llgfr	%r5,%r5			# size_t
	jg	sys_lgetxattr

ENTRY(sys32_fgetxattr_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# char *
	llgtr	%r4,%r4			# void *
	llgfr	%r5,%r5			# size_t
	jg	sys_fgetxattr

ENTRY(sys32_listxattr_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# char *
	llgfr	%r4,%r4			# size_t
	jg	sys_listxattr

ENTRY(sys32_llistxattr_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# char *
	llgfr	%r4,%r4			# size_t
	jg	sys_llistxattr

ENTRY(sys32_flistxattr_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# char *
	llgfr	%r4,%r4			# size_t
	jg	sys_flistxattr

ENTRY(sys32_removexattr_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# char *
	jg	sys_removexattr

ENTRY(sys32_lremovexattr_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# char *
	jg	sys_lremovexattr

ENTRY(sys32_fremovexattr_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# char *
	jg	sys_fremovexattr

ENTRY(sys32_sched_setaffinity_wrapper)
	lgfr	%r2,%r2			# int
	llgfr	%r3,%r3			# unsigned int
	llgtr	%r4,%r4			# unsigned long *
	jg	compat_sys_sched_setaffinity

ENTRY(sys32_sched_getaffinity_wrapper)
	lgfr	%r2,%r2			# int
	llgfr	%r3,%r3			# unsigned int
	llgtr	%r4,%r4			# unsigned long *
	jg	compat_sys_sched_getaffinity

ENTRY(sys32_exit_group_wrapper)
	lgfr	%r2,%r2			# int
	jg	sys_exit_group		# branch to system call

ENTRY(sys32_set_tid_address_wrapper)
	llgtr	%r2,%r2			# int *
	jg	sys_set_tid_address	# branch to system call

ENTRY(sys_epoll_create_wrapper)
	lgfr	%r2,%r2			# int
	jg	sys_epoll_create	# branch to system call

ENTRY(sys_epoll_ctl_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	lgfr	%r4,%r4			# int
	llgtr	%r5,%r5			# struct epoll_event *
	jg	sys_epoll_ctl		# branch to system call

ENTRY(sys_epoll_wait_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# struct epoll_event *
	lgfr	%r4,%r4			# int
	lgfr	%r5,%r5			# int
	jg	sys_epoll_wait		# branch to system call

ENTRY(sys32_lookup_dcookie_wrapper)
	sllg	%r2,%r2,32		# get high word of 64bit dcookie
	or	%r2,%r3			# get low word of 64bit dcookie
	llgtr	%r3,%r4			# char *
	llgfr	%r4,%r5			# size_t
	jg	sys_lookup_dcookie

ENTRY(sys32_fadvise64_wrapper)
	lgfr	%r2,%r2			# int
	sllg	%r3,%r3,32		# get high word of 64bit loff_t
	or	%r3,%r4			# get low word of 64bit loff_t
	llgfr	%r4,%r5			# size_t (unsigned long)
	lgfr	%r5,%r6			# int
	jg	sys32_fadvise64

ENTRY(sys32_fadvise64_64_wrapper)
	llgtr	%r2,%r2			# struct fadvise64_64_args *
	jg	sys32_fadvise64_64

ENTRY(sys32_clock_settime_wrapper)
	lgfr	%r2,%r2			# clockid_t (int)
	llgtr	%r3,%r3			# struct compat_timespec *
	jg	compat_sys_clock_settime

ENTRY(sys32_clock_gettime_wrapper)
	lgfr	%r2,%r2			# clockid_t (int)
	llgtr	%r3,%r3			# struct compat_timespec *
	jg	compat_sys_clock_gettime

ENTRY(sys32_clock_getres_wrapper)
	lgfr	%r2,%r2			# clockid_t (int)
	llgtr	%r3,%r3			# struct compat_timespec *
	jg	compat_sys_clock_getres

ENTRY(sys32_clock_nanosleep_wrapper)
	lgfr	%r2,%r2			# clockid_t (int)
	lgfr	%r3,%r3			# int
	llgtr	%r4,%r4			# struct compat_timespec *
	llgtr	%r5,%r5			# struct compat_timespec *
	jg	compat_sys_clock_nanosleep

ENTRY(sys32_timer_create_wrapper)
	lgfr	%r2,%r2			# timer_t (int)
	llgtr	%r3,%r3			# struct compat_sigevent *
	llgtr	%r4,%r4			# timer_t *
	jg	compat_sys_timer_create

ENTRY(sys32_timer_settime_wrapper)
	lgfr	%r2,%r2			# timer_t (int)
	lgfr	%r3,%r3			# int
	llgtr	%r4,%r4			# struct compat_itimerspec *
	llgtr	%r5,%r5			# struct compat_itimerspec *
	jg	compat_sys_timer_settime

ENTRY(sys32_timer_gettime_wrapper)
	lgfr	%r2,%r2			# timer_t (int)
	llgtr	%r3,%r3			# struct compat_itimerspec *
	jg	compat_sys_timer_gettime

ENTRY(sys32_timer_getoverrun_wrapper)
	lgfr	%r2,%r2			# timer_t (int)
	jg	sys_timer_getoverrun

ENTRY(sys32_timer_delete_wrapper)
	lgfr	%r2,%r2			# timer_t (int)
	jg	sys_timer_delete

ENTRY(sys32_io_setup_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# u32 *
	jg	compat_sys_io_setup

ENTRY(sys32_io_destroy_wrapper)
	llgfr	%r2,%r2			# (aio_context_t) u32
	jg	sys_io_destroy

ENTRY(sys32_io_getevents_wrapper)
	llgfr	%r2,%r2			# (aio_context_t) u32
	lgfr	%r3,%r3			# long
	lgfr	%r4,%r4			# long
	llgtr	%r5,%r5			# struct io_event *
	llgtr	%r6,%r6			# struct compat_timespec *
	jg	compat_sys_io_getevents

ENTRY(sys32_io_submit_wrapper)
	llgfr	%r2,%r2			# (aio_context_t) u32
	lgfr	%r3,%r3			# long
	llgtr	%r4,%r4			# struct iocb **
	jg	compat_sys_io_submit

ENTRY(sys32_io_cancel_wrapper)
	llgfr	%r2,%r2			# (aio_context_t) u32
	llgtr	%r3,%r3			# struct iocb *
	llgtr	%r4,%r4			# struct io_event *
	jg	sys_io_cancel

ENTRY(compat_sys_statfs64_wrapper)
	llgtr	%r2,%r2			# const char *
	llgfr	%r3,%r3			# compat_size_t
	llgtr	%r4,%r4			# struct compat_statfs64 *
	jg	compat_sys_statfs64

ENTRY(compat_sys_fstatfs64_wrapper)
	llgfr	%r2,%r2			# unsigned int fd
	llgfr	%r3,%r3			# compat_size_t
	llgtr	%r4,%r4			# struct compat_statfs64 *
	jg	compat_sys_fstatfs64

ENTRY(compat_sys_mq_open_wrapper)
	llgtr	%r2,%r2			# const char *
	lgfr	%r3,%r3			# int
	llgfr	%r4,%r4			# mode_t
	llgtr	%r5,%r5			# struct compat_mq_attr *
	jg	compat_sys_mq_open

ENTRY(sys32_mq_unlink_wrapper)
	llgtr	%r2,%r2			# const char *
	jg	sys_mq_unlink

ENTRY(compat_sys_mq_timedsend_wrapper)
	lgfr	%r2,%r2			# mqd_t
	llgtr	%r3,%r3			# const char *
	llgfr	%r4,%r4			# size_t
	llgfr	%r5,%r5			# unsigned int
	llgtr	%r6,%r6			# const struct compat_timespec *
	jg	compat_sys_mq_timedsend

ENTRY(compat_sys_mq_timedreceive_wrapper)
	lgfr	%r2,%r2			# mqd_t
	llgtr	%r3,%r3			# char *
	llgfr	%r4,%r4			# size_t
	llgtr	%r5,%r5			# unsigned int *
	llgtr	%r6,%r6			# const struct compat_timespec *
	jg	compat_sys_mq_timedreceive

ENTRY(compat_sys_mq_notify_wrapper)
	lgfr	%r2,%r2			# mqd_t
	llgtr	%r3,%r3			# struct compat_sigevent *
	jg	compat_sys_mq_notify

ENTRY(compat_sys_mq_getsetattr_wrapper)
	lgfr	%r2,%r2			# mqd_t
	llgtr	%r3,%r3			# struct compat_mq_attr *
	llgtr	%r4,%r4			# struct compat_mq_attr *
	jg	compat_sys_mq_getsetattr

ENTRY(compat_sys_add_key_wrapper)
	llgtr	%r2,%r2			# const char *
	llgtr	%r3,%r3			# const char *
	llgtr	%r4,%r4			# const void *
	llgfr	%r5,%r5			# size_t
	llgfr	%r6,%r6			# (key_serial_t) u32
	jg	sys_add_key

ENTRY(compat_sys_request_key_wrapper)
	llgtr	%r2,%r2			# const char *
	llgtr	%r3,%r3			# const char *
	llgtr	%r4,%r4			# const void *
	llgfr	%r5,%r5			# (key_serial_t) u32
	jg	sys_request_key

ENTRY(sys32_remap_file_pages_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgfr	%r3,%r3			# unsigned long
	llgfr	%r4,%r4			# unsigned long
	llgfr	%r5,%r5			# unsigned long
	llgfr	%r6,%r6			# unsigned long
	jg	sys_remap_file_pages

ENTRY(compat_sys_kexec_load_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgfr	%r3,%r3			# unsigned long
	llgtr	%r4,%r4			# struct kexec_segment *
	llgfr	%r5,%r5			# unsigned long
	jg	compat_sys_kexec_load

ENTRY(sys_ioprio_set_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	lgfr	%r4,%r4			# int
	jg	sys_ioprio_set

ENTRY(sys_ioprio_get_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	jg	sys_ioprio_get

ENTRY(sys_inotify_add_watch_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const char *
	llgfr	%r4,%r4			# u32
	jg	sys_inotify_add_watch

ENTRY(sys_inotify_rm_watch_wrapper)
	lgfr	%r2,%r2			# int
	llgfr	%r3,%r3			# u32
	jg	sys_inotify_rm_watch

ENTRY(sys_mkdirat_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const char *
	lgfr	%r4,%r4			# int
	jg	sys_mkdirat

ENTRY(sys_mknodat_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const char *
	lgfr	%r4,%r4			# int
	llgfr	%r5,%r5			# unsigned int
	jg	sys_mknodat

ENTRY(sys_fchownat_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const char *
	llgfr	%r4,%r4			# uid_t
	llgfr	%r5,%r5			# gid_t
	lgfr	%r6,%r6			# int
	jg	sys_fchownat

ENTRY(compat_sys_futimesat_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# char *
	llgtr	%r4,%r4			# struct timeval *
	jg	compat_sys_futimesat

ENTRY(sys32_fstatat64_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# char *
	llgtr	%r4,%r4			# struct stat64 *
	lgfr	%r5,%r5			# int
	jg	sys32_fstatat64

ENTRY(sys_unlinkat_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const char *
	lgfr	%r4,%r4			# int
	jg	sys_unlinkat

ENTRY(sys_renameat_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const char *
	lgfr	%r4,%r4			# int
	llgtr	%r5,%r5			# const char *
	jg	sys_renameat

ENTRY(sys_linkat_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const char *
	lgfr	%r4,%r4			# int
	llgtr	%r5,%r5			# const char *
	lgfr	%r6,%r6			# int
	jg	sys_linkat

ENTRY(sys_symlinkat_wrapper)
	llgtr	%r2,%r2			# const char *
	lgfr	%r3,%r3			# int
	llgtr	%r4,%r4			# const char *
	jg	sys_symlinkat

ENTRY(sys_readlinkat_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const char *
	llgtr	%r4,%r4			# char *
	lgfr	%r5,%r5			# int
	jg	sys_readlinkat

ENTRY(sys_fchmodat_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const char *
	llgfr	%r4,%r4			# mode_t
	jg	sys_fchmodat

ENTRY(sys_faccessat_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const char *
	lgfr	%r4,%r4			# int
	jg	sys_faccessat

ENTRY(compat_sys_pselect6_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# fd_set *
	llgtr	%r4,%r4			# fd_set *
	llgtr	%r5,%r5			# fd_set *
	llgtr	%r6,%r6			# struct timespec *
	llgt	%r0,164(%r15)		# void *
	stg	%r0,160(%r15)
	jg	compat_sys_pselect6

ENTRY(compat_sys_ppoll_wrapper)
	llgtr	%r2,%r2			# struct pollfd *
	llgfr	%r3,%r3			# unsigned int
	llgtr	%r4,%r4			# struct timespec *
	llgtr	%r5,%r5			# const sigset_t *
	llgfr	%r6,%r6			# size_t
	jg	compat_sys_ppoll

ENTRY(sys_unshare_wrapper)
	llgfr	%r2,%r2			# unsigned long
	jg	sys_unshare

ENTRY(sys_splice_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# loff_t *
	lgfr	%r4,%r4			# int
	llgtr	%r5,%r5			# loff_t *
	llgfr	%r6,%r6			# size_t
	llgf	%r0,164(%r15)		# unsigned int
	stg	%r0,160(%r15)
	jg	sys_splice

ENTRY(sys_sync_file_range_wrapper)
	lgfr	%r2,%r2			# int
	sllg	%r3,%r3,32		# get high word of 64bit loff_t
	or	%r3,%r4			# get low word of 64bit loff_t
	sllg	%r4,%r5,32		# get high word of 64bit loff_t
	or	%r4,%r6			# get low word of 64bit loff_t
	llgf	%r5,164(%r15)		# unsigned int
	jg	sys_sync_file_range

ENTRY(sys_tee_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	llgfr	%r4,%r4			# size_t
	llgfr	%r5,%r5			# unsigned int
	jg	sys_tee

ENTRY(compat_sys_vmsplice_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# compat_iovec *
	llgfr	%r4,%r4			# unsigned int
	llgfr	%r5,%r5			# unsigned int
	jg	compat_sys_vmsplice

ENTRY(sys_getcpu_wrapper)
	llgtr	%r2,%r2			# unsigned *
	llgtr	%r3,%r3			# unsigned *
	llgtr	%r4,%r4			# struct getcpu_cache *
	jg	sys_getcpu

ENTRY(compat_sys_epoll_pwait_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# struct compat_epoll_event *
	lgfr	%r4,%r4			# int
	lgfr	%r5,%r5			# int
	llgtr	%r6,%r6			# compat_sigset_t *
	llgf	%r0,164(%r15)		# compat_size_t
	stg	%r0,160(%r15)
	jg	compat_sys_epoll_pwait

ENTRY(compat_sys_utimes_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# struct compat_timeval *
	jg	compat_sys_utimes

ENTRY(compat_sys_utimensat_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# char *
	llgtr	%r4,%r4			# struct compat_timespec *
	lgfr	%r5,%r5			# int
	jg	compat_sys_utimensat

ENTRY(compat_sys_signalfd_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# compat_sigset_t *
	llgfr	%r4,%r4			# compat_size_t
	jg	compat_sys_signalfd

ENTRY(sys_eventfd_wrapper)
	llgfr	%r2,%r2			# unsigned int
	jg	sys_eventfd

ENTRY(sys_fallocate_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	sllg	%r4,%r4,32		# get high word of 64bit loff_t
	lr	%r4,%r5			# get low word of 64bit loff_t
	sllg	%r5,%r6,32		# get high word of 64bit loff_t
	l	%r5,164(%r15)		# get low word of 64bit loff_t
	jg	sys_fallocate

ENTRY(sys_timerfd_create_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	jg	sys_timerfd_create

ENTRY(compat_sys_signalfd4_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# compat_sigset_t *
	llgfr	%r4,%r4			# compat_size_t
	lgfr	%r5,%r5			# int
	jg	compat_sys_signalfd4

ENTRY(sys_eventfd2_wrapper)
	llgfr	%r2,%r2			# unsigned int
	lgfr	%r3,%r3			# int
	jg	sys_eventfd2

ENTRY(sys_inotify_init1_wrapper)
	lgfr	%r2,%r2			# int
	jg	sys_inotify_init1

ENTRY(sys_pipe2_wrapper)
	llgtr	%r2,%r2			# u32 *
	lgfr	%r3,%r3			# int
	jg	sys_pipe2		# branch to system call

ENTRY(sys_dup3_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# unsigned int
	lgfr	%r4,%r4			# int
	jg	sys_dup3		# branch to system call

ENTRY(sys_epoll_create1_wrapper)
	lgfr	%r2,%r2			# int
	jg	sys_epoll_create1	# branch to system call

ENTRY(sys32_readahead_wrapper)
	lgfr	%r2,%r2			# int
	llgfr	%r3,%r3			# u32
	llgfr	%r4,%r4			# u32
	lgfr	%r5,%r5			# s32
	jg	sys32_readahead		# branch to system call

ENTRY(sys32_sendfile64_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	llgtr	%r4,%r4			# compat_loff_t *
	lgfr	%r5,%r5			# s32
	jg	sys32_sendfile64	# branch to system call

ENTRY(sys_tkill_wrapper)
	lgfr	%r2,%r2			# pid_t
	lgfr	%r3,%r3			# int
	jg	sys_tkill		# branch to system call

ENTRY(sys_tgkill_wrapper)
	lgfr	%r2,%r2			# pid_t
	lgfr	%r3,%r3			# pid_t
	lgfr	%r4,%r4			# int
	jg	sys_tgkill		# branch to system call

ENTRY(compat_sys_keyctl_wrapper)
	llgfr	%r2,%r2			# u32
	llgfr	%r3,%r3			# u32
	llgfr	%r4,%r4			# u32
	llgfr	%r5,%r5			# u32
	llgfr	%r6,%r6			# u32
	jg	compat_sys_keyctl	# branch to system call

ENTRY(compat_sys_preadv_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgtr	%r3,%r3			# compat_iovec *
	llgfr	%r4,%r4			# unsigned long
	llgfr	%r5,%r5			# u32
	llgfr	%r6,%r6			# u32
	jg	compat_sys_preadv	# branch to system call

ENTRY(compat_sys_pwritev_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgtr	%r3,%r3			# compat_iovec *
	llgfr	%r4,%r4			# unsigned long
	llgfr	%r5,%r5			# u32
	llgfr	%r6,%r6			# u32
	jg	compat_sys_pwritev	# branch to system call

ENTRY(sys_perf_event_open_wrapper)
	llgtr	%r2,%r2			# const struct perf_event_attr *
	lgfr	%r3,%r3			# pid_t
	lgfr	%r4,%r4			# int
	lgfr	%r5,%r5			# int
	llgfr	%r6,%r6			# unsigned long
	jg	sys_perf_event_open	# branch to system call

ENTRY(sys_clone_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgfr	%r3,%r3			# unsigned long
	llgtr	%r4,%r4			# int *
	llgtr	%r5,%r5			# int *
	jg	sys_clone		# branch to system call

ENTRY(sys32_execve_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# compat_uptr_t *
	llgtr	%r4,%r4			# compat_uptr_t *
	jg	compat_sys_execve	# branch to system call

ENTRY(sys_fanotify_init_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# unsigned int
	jg	sys_fanotify_init	# branch to system call

ENTRY(sys_fanotify_mark_wrapper)
	lgfr	%r2,%r2			# int
	llgfr	%r3,%r3			# unsigned int
	sllg	%r4,%r4,32		# get high word of 64bit mask
	lr	%r4,%r5			# get low word of 64bit mask
	llgfr	%r5,%r6			# unsigned int
	llgt	%r6,164(%r15)		# char *
	jg	sys_fanotify_mark	# branch to system call

ENTRY(sys_prlimit64_wrapper)
	lgfr	%r2,%r2			# pid_t
	llgfr	%r3,%r3			# unsigned int
	llgtr	%r4,%r4			# const struct rlimit64 __user *
	llgtr	%r5,%r5			# struct rlimit64 __user *
	jg	sys_prlimit64		# branch to system call

ENTRY(sys_name_to_handle_at_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const char __user *
	llgtr	%r4,%r4			# struct file_handle __user *
	llgtr	%r5,%r5			# int __user *
	lgfr	%r6,%r6			# int
	jg	sys_name_to_handle_at

ENTRY(compat_sys_clock_adjtime_wrapper)
	lgfr	%r2,%r2			# clockid_t (int)
	llgtr	%r3,%r3			# struct compat_timex __user *
	jg	compat_sys_clock_adjtime

ENTRY(sys_syncfs_wrapper)
	lgfr	%r2,%r2			# int
	jg	sys_syncfs

ENTRY(sys_setns_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	jg	sys_setns

ENTRY(compat_sys_process_vm_readv_wrapper)
	lgfr	%r2,%r2			# compat_pid_t
	llgtr	%r3,%r3			# struct compat_iovec __user *
	llgfr	%r4,%r4			# unsigned long
	llgtr	%r5,%r5			# struct compat_iovec __user *
	llgfr	%r6,%r6			# unsigned long
	llgf	%r0,164(%r15)		# unsigned long
	stg	%r0,160(%r15)
	jg	compat_sys_process_vm_readv

ENTRY(compat_sys_process_vm_writev_wrapper)
	lgfr	%r2,%r2			# compat_pid_t
	llgtr	%r3,%r3			# struct compat_iovec __user *
	llgfr	%r4,%r4			# unsigned long
	llgtr	%r5,%r5			# struct compat_iovec __user *
	llgfr	%r6,%r6			# unsigned long
	llgf	%r0,164(%r15)		# unsigned long
	stg	%r0,160(%r15)
	jg	compat_sys_process_vm_writev

ENTRY(sys_s390_runtime_instr_wrapper)
	lgfr	%r2,%r2			# int
	lgfr	%r3,%r3			# int
	jg	sys_s390_runtime_instr

ENTRY(sys_kcmp_wrapper)
	lgfr	%r2,%r2			# pid_t
	lgfr	%r3,%r3			# pid_t
	lgfr	%r4,%r4			# int
	llgfr	%r5,%r5			# unsigned long
	llgfr	%r6,%r6			# unsigned long
	jg	sys_kcmp

ENTRY(sys_finit_module_wrapper)
	lgfr	%r2,%r2			# int
	llgtr	%r3,%r3			# const char __user *
	lgfr	%r4,%r4			# int
	jg	sys_finit_module
