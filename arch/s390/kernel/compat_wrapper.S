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

ENTRY(compat_sys_fcntl64_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgfr	%r3,%r3			# unsigned int
	llgfr	%r4,%r4			# unsigned long
	jg	compat_sys_fcntl64	# branch to system call

ENTRY(sys32_stime_wrapper)
	llgtr	%r2,%r2			# long *
	jg	compat_sys_stime	# branch to system call

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

ENTRY(sys32_io_setup_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# u32 *
	jg	compat_sys_io_setup

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

ENTRY(compat_sys_kexec_load_wrapper)
	llgfr	%r2,%r2			# unsigned long
	llgfr	%r3,%r3			# unsigned long
	llgtr	%r4,%r4			# struct kexec_segment *
	llgfr	%r5,%r5			# unsigned long
	jg	compat_sys_kexec_load

ENTRY(compat_sys_futimesat_wrapper)
	llgfr	%r2,%r2			# unsigned int
	llgtr	%r3,%r3			# char *
	llgtr	%r4,%r4			# struct timeval *
	jg	compat_sys_futimesat

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

ENTRY(compat_sys_keyctl_wrapper)
	llgfr	%r2,%r2			# u32
	llgfr	%r3,%r3			# u32
	llgfr	%r4,%r4			# u32
	llgfr	%r5,%r5			# u32
	llgfr	%r6,%r6			# u32
	jg	compat_sys_keyctl	# branch to system call

ENTRY(sys32_execve_wrapper)
	llgtr	%r2,%r2			# char *
	llgtr	%r3,%r3			# compat_uptr_t *
	llgtr	%r4,%r4			# compat_uptr_t *
	jg	compat_sys_execve	# branch to system call

ENTRY(compat_sys_clock_adjtime_wrapper)
	lgfr	%r2,%r2			# clockid_t (int)
	llgtr	%r3,%r3			# struct compat_timex __user *
	jg	compat_sys_clock_adjtime

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
