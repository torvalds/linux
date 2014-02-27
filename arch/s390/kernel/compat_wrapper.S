/*
*    wrapper for 31 bit compatible system calls.
*
*    Copyright IBM Corp. 2000, 2006
*    Author(s): Gerhard Tonn (ton@de.ibm.com),
*		Thomas Spatzier (tspat@de.ibm.com)
*/

#include <linux/linkage.h>

ENTRY(sys32_time_wrapper)
	llgtr	%r2,%r2			# int *
	jg	compat_sys_time		# branch to system call

#sys32_getpid_wrapper				# void

ENTRY(sys32_mount_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# char *
	llgtr	%r4,%r4			# char *
	llgfr	%r5,%r5			# unsigned long
	llgtr	%r6,%r6			# void *
	jg	compat_sys_mount	# branch to system call

ENTRY(sys32_ptrace_wrapper)
	lgfr	%r2,%r2			# long
	lgfr	%r3,%r3			# long
	llgtr	%r4,%r4			# long
	llgfr	%r5,%r5			# long
	jg	compat_sys_ptrace	# branch to system call

ENTRY(compat_sys_utime_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# struct compat_utimbuf *
	jg	compat_sys_utime	# branch to system call

ENTRY(compat_sys_times_wrapper)
	llgtr	%r2,%r2			# struct compat_tms *
	jg	compat_sys_times	# branch to system call

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

ENTRY(sys32_ustat_wrapper)
	llgfr	%r2,%r2			# dev_t
	llgtr	%r3,%r3			# struct ustat *
	jg	compat_sys_ustat

ENTRY(compat_sys_sigpending_wrapper)
	llgtr	%r2,%r2			# compat_old_sigset_t *
	jg	compat_sys_sigpending	# branch to system call

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

ENTRY(compat_sys_gettimeofday_wrapper)
	llgtr	%r2,%r2			# struct timeval_emu31 *
	llgtr	%r3,%r3			# struct timezone *
	jg	compat_sys_gettimeofday	# branch to system call

ENTRY(compat_sys_settimeofday_wrapper)
	llgtr	%r2,%r2			# struct timeval_emu31 *
	llgtr	%r3,%r3			# struct timezone *
	jg	compat_sys_settimeofday	# branch to system call

ENTRY(old32_readdir_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# void *
	llgfr	%r4,%r4			# unsigned int
	jg	compat_sys_old_readdir	# branch to system call

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

ENTRY(compat_sys_sysinfo_wrapper)
	llgtr	%r2,%r2			# struct sysinfo_emu31 *
	jg	compat_sys_sysinfo	# branch to system call

ENTRY(compat_sys_adjtimex_wrapper)
	llgtr	%r2,%r2			# struct compat_timex *
	jg	compat_sys_adjtimex	# branch to system call

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

ENTRY(compat_sys_nanosleep_wrapper)
	llgtr	%r2,%r2			# struct compat_timespec *
	llgtr	%r3,%r3			# struct compat_timespec *
	jg	compat_sys_nanosleep		# branch to system call

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

ENTRY(sys32_stime_wrapper)
	llgtr	%r2,%r2			# long *
	jg	compat_sys_stime	# branch to system call

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

ENTRY(sys_getcpu_wrapper)
	llgtr	%r2,%r2			# unsigned *
	llgtr	%r3,%r3			# unsigned *
	llgtr	%r4,%r4			# struct getcpu_cache *
	jg	sys_getcpu

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

ENTRY(sys_sched_setattr_wrapper)
	lgfr	%r2,%r2			# pid_t
	llgtr	%r3,%r3			# struct sched_attr __user *
	jg	sys_sched_setattr

ENTRY(sys_sched_getattr_wrapper)
	lgfr	%r2,%r2			# pid_t
	llgtr	%r3,%r3			# const char __user *
	llgfr	%r4,%r4			# unsigned int
	jg	sys_sched_getattr
