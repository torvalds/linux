/*
 * System call callback functions for SPUs
 */

#define DEBUG

#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/syscalls.h>

#include <asm/spu.h>
#include <asm/syscalls.h>
#include <asm/unistd.h>

/*
 * This table defines the system calls that an SPU can call.
 * It is currently a subset of the 64 bit powerpc system calls,
 * with the exact semantics.
 *
 * The reasons for disabling some of the system calls are:
 * 1. They interact with the way SPU syscalls are handled
 *    and we can't let them execute ever:
 *	restart_syscall, exit, for, execve, ptrace, ...
 * 2. They are deprecated and replaced by other means:
 *	uselib, pciconfig_*, sysfs, ...
 * 3. They are somewhat interacting with the system in a way
 *    we don't want an SPU to:
 *	reboot, init_module, mount, kexec_load
 * 4. They are optional and we can't rely on them being
 *    linked into the kernel. Unfortunately, the cond_syscall
 *    helper does not work here as it does not add the necessary
 *    opd symbols:
 *	mbind, mq_open, ipc, ...
 */

void *spu_syscall_table[] = {
	[__NR_restart_syscall]		sys_ni_syscall, /* sys_restart_syscall */
	[__NR_exit]			sys_ni_syscall, /* sys_exit */
	[__NR_fork]			sys_ni_syscall, /* ppc_fork */
	[__NR_read]			sys_read,
	[__NR_write]			sys_write,
	[__NR_open]			sys_open,
	[__NR_close]			sys_close,
	[__NR_waitpid]			sys_waitpid,
	[__NR_creat]			sys_creat,
	[__NR_link]			sys_link,
	[__NR_unlink]			sys_unlink,
	[__NR_execve]			sys_ni_syscall, /* sys_execve */
	[__NR_chdir]			sys_chdir,
	[__NR_time]			sys_time,
	[__NR_mknod]			sys_mknod,
	[__NR_chmod]			sys_chmod,
	[__NR_lchown]			sys_lchown,
	[__NR_break]			sys_ni_syscall,
	[__NR_oldstat]			sys_ni_syscall,
	[__NR_lseek]			sys_lseek,
	[__NR_getpid]			sys_getpid,
	[__NR_mount]			sys_ni_syscall, /* sys_mount */
	[__NR_umount]			sys_ni_syscall,
	[__NR_setuid]			sys_setuid,
	[__NR_getuid]			sys_getuid,
	[__NR_stime]			sys_stime,
	[__NR_ptrace]			sys_ni_syscall, /* sys_ptrace */
	[__NR_alarm]			sys_alarm,
	[__NR_oldfstat]			sys_ni_syscall,
	[__NR_pause]			sys_ni_syscall, /* sys_pause */
	[__NR_utime]			sys_ni_syscall, /* sys_utime */
	[__NR_stty]			sys_ni_syscall,
	[__NR_gtty]			sys_ni_syscall,
	[__NR_access]			sys_access,
	[__NR_nice]			sys_nice,
	[__NR_ftime]			sys_ni_syscall,
	[__NR_sync]			sys_sync,
	[__NR_kill]			sys_kill,
	[__NR_rename]			sys_rename,
	[__NR_mkdir]			sys_mkdir,
	[__NR_rmdir]			sys_rmdir,
	[__NR_dup]			sys_dup,
	[__NR_pipe]			sys_pipe,
	[__NR_times]			sys_times,
	[__NR_prof]			sys_ni_syscall,
	[__NR_brk]			sys_brk,
	[__NR_setgid]			sys_setgid,
	[__NR_getgid]			sys_getgid,
	[__NR_signal]			sys_ni_syscall, /* sys_signal */
	[__NR_geteuid]			sys_geteuid,
	[__NR_getegid]			sys_getegid,
	[__NR_acct]			sys_ni_syscall, /* sys_acct */
	[__NR_umount2]			sys_ni_syscall, /* sys_umount */
	[__NR_lock]			sys_ni_syscall,
	[__NR_ioctl]			sys_ioctl,
	[__NR_fcntl]			sys_fcntl,
	[__NR_mpx]			sys_ni_syscall,
	[__NR_setpgid]			sys_setpgid,
	[__NR_ulimit]			sys_ni_syscall,
	[__NR_oldolduname]		sys_ni_syscall,
	[__NR_umask]			sys_umask,
	[__NR_chroot]			sys_chroot,
	[__NR_ustat]			sys_ni_syscall, /* sys_ustat */
	[__NR_dup2]			sys_dup2,
	[__NR_getppid]			sys_getppid,
	[__NR_getpgrp]			sys_getpgrp,
	[__NR_setsid]			sys_setsid,
	[__NR_sigaction]		sys_ni_syscall,
	[__NR_sgetmask]			sys_sgetmask,
	[__NR_ssetmask]			sys_ssetmask,
	[__NR_setreuid]			sys_setreuid,
	[__NR_setregid]			sys_setregid,
	[__NR_sigsuspend]		sys_ni_syscall,
	[__NR_sigpending]		sys_ni_syscall,
	[__NR_sethostname]		sys_sethostname,
	[__NR_setrlimit]		sys_setrlimit,
	[__NR_getrlimit]		sys_ni_syscall,
	[__NR_getrusage]		sys_getrusage,
	[__NR_gettimeofday]		sys_gettimeofday,
	[__NR_settimeofday]		sys_settimeofday,
	[__NR_getgroups]		sys_getgroups,
	[__NR_setgroups]		sys_setgroups,
	[__NR_select]			sys_ni_syscall,
	[__NR_symlink]			sys_symlink,
	[__NR_oldlstat]			sys_ni_syscall,
	[__NR_readlink]			sys_readlink,
	[__NR_uselib]			sys_ni_syscall, /* sys_uselib */
	[__NR_swapon]			sys_ni_syscall, /* sys_swapon */
	[__NR_reboot]			sys_ni_syscall, /* sys_reboot */
	[__NR_readdir]			sys_ni_syscall,
	[__NR_mmap]			sys_mmap,
	[__NR_munmap]			sys_munmap,
	[__NR_truncate]			sys_truncate,
	[__NR_ftruncate]		sys_ftruncate,
	[__NR_fchmod]			sys_fchmod,
	[__NR_fchown]			sys_fchown,
	[__NR_getpriority]		sys_getpriority,
	[__NR_setpriority]		sys_setpriority,
	[__NR_profil]			sys_ni_syscall,
	[__NR_statfs]			sys_ni_syscall, /* sys_statfs */
	[__NR_fstatfs]			sys_ni_syscall, /* sys_fstatfs */
	[__NR_ioperm]			sys_ni_syscall,
	[__NR_socketcall]		sys_socketcall,
	[__NR_syslog]			sys_syslog,
	[__NR_setitimer]		sys_setitimer,
	[__NR_getitimer]		sys_getitimer,
	[__NR_stat]			sys_newstat,
	[__NR_lstat]			sys_newlstat,
	[__NR_fstat]			sys_newfstat,
	[__NR_olduname]			sys_ni_syscall,
	[__NR_iopl]			sys_ni_syscall,
	[__NR_vhangup]			sys_vhangup,
	[__NR_idle]			sys_ni_syscall,
	[__NR_vm86]			sys_ni_syscall,
	[__NR_wait4]			sys_wait4,
	[__NR_swapoff]			sys_ni_syscall, /* sys_swapoff */
	[__NR_sysinfo]			sys_sysinfo,
	[__NR_ipc]			sys_ni_syscall, /* sys_ipc */
	[__NR_fsync]			sys_fsync,
	[__NR_sigreturn]		sys_ni_syscall,
	[__NR_clone]			sys_ni_syscall, /* ppc_clone */
	[__NR_setdomainname]		sys_setdomainname,
	[__NR_uname]			ppc_newuname,
	[__NR_modify_ldt]		sys_ni_syscall,
	[__NR_adjtimex]			sys_adjtimex,
	[__NR_mprotect]			sys_mprotect,
	[__NR_sigprocmask]		sys_ni_syscall,
	[__NR_create_module]		sys_ni_syscall,
	[__NR_init_module]		sys_ni_syscall, /* sys_init_module */
	[__NR_delete_module]		sys_ni_syscall, /* sys_delete_module */
	[__NR_get_kernel_syms]		sys_ni_syscall,
	[__NR_quotactl]			sys_ni_syscall, /* sys_quotactl */
	[__NR_getpgid]			sys_getpgid,
	[__NR_fchdir]			sys_fchdir,
	[__NR_bdflush]			sys_bdflush,
	[__NR_sysfs]			sys_ni_syscall, /* sys_sysfs */
	[__NR_personality]		ppc64_personality,
	[__NR_afs_syscall]		sys_ni_syscall,
	[__NR_setfsuid]			sys_setfsuid,
	[__NR_setfsgid]			sys_setfsgid,
	[__NR__llseek]			sys_llseek,
	[__NR_getdents]			sys_getdents,
	[__NR__newselect]		sys_select,
	[__NR_flock]			sys_flock,
	[__NR_msync]			sys_msync,
	[__NR_readv]			sys_readv,
	[__NR_writev]			sys_writev,
	[__NR_getsid]			sys_getsid,
	[__NR_fdatasync]		sys_fdatasync,
	[__NR__sysctl]			sys_ni_syscall, /* sys_sysctl */
	[__NR_mlock]			sys_mlock,
	[__NR_munlock]			sys_munlock,
	[__NR_mlockall]			sys_mlockall,
	[__NR_munlockall]		sys_munlockall,
	[__NR_sched_setparam]		sys_sched_setparam,
	[__NR_sched_getparam]		sys_sched_getparam,
	[__NR_sched_setscheduler]	sys_sched_setscheduler,
	[__NR_sched_getscheduler]	sys_sched_getscheduler,
	[__NR_sched_yield]		sys_sched_yield,
	[__NR_sched_get_priority_max]	sys_sched_get_priority_max,
	[__NR_sched_get_priority_min]	sys_sched_get_priority_min,
	[__NR_sched_rr_get_interval]	sys_sched_rr_get_interval,
	[__NR_nanosleep]		sys_nanosleep,
	[__NR_mremap]			sys_mremap,
	[__NR_setresuid]		sys_setresuid,
	[__NR_getresuid]		sys_getresuid,
	[__NR_query_module]		sys_ni_syscall,
	[__NR_poll]			sys_poll,
	[__NR_nfsservctl]		sys_ni_syscall, /* sys_nfsservctl */
	[__NR_setresgid]		sys_setresgid,
	[__NR_getresgid]		sys_getresgid,
	[__NR_prctl]			sys_prctl,
	[__NR_rt_sigreturn]		sys_ni_syscall, /* ppc64_rt_sigreturn */
	[__NR_rt_sigaction]		sys_ni_syscall, /* sys_rt_sigaction */
	[__NR_rt_sigprocmask]		sys_ni_syscall, /* sys_rt_sigprocmask */
	[__NR_rt_sigpending]		sys_ni_syscall, /* sys_rt_sigpending */
	[__NR_rt_sigtimedwait]		sys_ni_syscall, /* sys_rt_sigtimedwait */
	[__NR_rt_sigqueueinfo]		sys_ni_syscall, /* sys_rt_sigqueueinfo */
	[__NR_rt_sigsuspend]		sys_ni_syscall, /* sys_rt_sigsuspend */
	[__NR_pread64]			sys_pread64,
	[__NR_pwrite64]			sys_pwrite64,
	[__NR_chown]			sys_chown,
	[__NR_getcwd]			sys_getcwd,
	[__NR_capget]			sys_capget,
	[__NR_capset]			sys_capset,
	[__NR_sigaltstack]		sys_ni_syscall, /* sys_sigaltstack */
	[__NR_sendfile]			sys_sendfile64,
	[__NR_getpmsg]			sys_ni_syscall,
	[__NR_putpmsg]			sys_ni_syscall,
	[__NR_vfork]			sys_ni_syscall, /* ppc_vfork */
	[__NR_ugetrlimit]		sys_getrlimit,
	[__NR_readahead]		sys_readahead,
	[192]				sys_ni_syscall,
	[193]				sys_ni_syscall,
	[194]				sys_ni_syscall,
	[195]				sys_ni_syscall,
	[196]				sys_ni_syscall,
	[197]				sys_ni_syscall,
	[__NR_pciconfig_read]		sys_ni_syscall, /* sys_pciconfig_read */
	[__NR_pciconfig_write]		sys_ni_syscall, /* sys_pciconfig_write */
	[__NR_pciconfig_iobase]		sys_ni_syscall, /* sys_pciconfig_iobase */
	[__NR_multiplexer]		sys_ni_syscall,
	[__NR_getdents64]		sys_getdents64,
	[__NR_pivot_root]		sys_pivot_root,
	[204]				sys_ni_syscall,
	[__NR_madvise]			sys_madvise,
	[__NR_mincore]			sys_mincore,
	[__NR_gettid]			sys_gettid,
	[__NR_tkill]			sys_tkill,
	[__NR_setxattr]			sys_setxattr,
	[__NR_lsetxattr]		sys_lsetxattr,
	[__NR_fsetxattr]		sys_fsetxattr,
	[__NR_getxattr]			sys_getxattr,
	[__NR_lgetxattr]		sys_lgetxattr,
	[__NR_fgetxattr]		sys_fgetxattr,
	[__NR_listxattr]		sys_listxattr,
	[__NR_llistxattr]		sys_llistxattr,
	[__NR_flistxattr]		sys_flistxattr,
	[__NR_removexattr]		sys_removexattr,
	[__NR_lremovexattr]		sys_lremovexattr,
	[__NR_fremovexattr]		sys_fremovexattr,
	[__NR_futex]			sys_futex,
	[__NR_sched_setaffinity]	sys_sched_setaffinity,
	[__NR_sched_getaffinity]	sys_sched_getaffinity,
	[__NR_tuxcall]			sys_ni_syscall,
	[226]				sys_ni_syscall,
	[__NR_io_setup]			sys_io_setup,
	[__NR_io_destroy]		sys_io_destroy,
	[__NR_io_getevents]		sys_io_getevents,
	[__NR_io_submit]		sys_io_submit,
	[__NR_io_cancel]		sys_io_cancel,
	[__NR_set_tid_address]		sys_ni_syscall, /* sys_set_tid_address */
	[__NR_fadvise64]		sys_fadvise64,
	[__NR_exit_group]		sys_ni_syscall, /* sys_exit_group */
	[__NR_lookup_dcookie]		sys_ni_syscall, /* sys_lookup_dcookie */
	[__NR_epoll_create]		sys_epoll_create,
	[__NR_epoll_ctl]		sys_epoll_ctl,
	[__NR_epoll_wait]		sys_epoll_wait,
	[__NR_remap_file_pages]		sys_remap_file_pages,
	[__NR_timer_create]		sys_timer_create,
	[__NR_timer_settime]		sys_timer_settime,
	[__NR_timer_gettime]		sys_timer_gettime,
	[__NR_timer_getoverrun]		sys_timer_getoverrun,
	[__NR_timer_delete]		sys_timer_delete,
	[__NR_clock_settime]		sys_clock_settime,
	[__NR_clock_gettime]		sys_clock_gettime,
	[__NR_clock_getres]		sys_clock_getres,
	[__NR_clock_nanosleep]		sys_clock_nanosleep,
	[__NR_swapcontext]		sys_ni_syscall, /* ppc64_swapcontext */
	[__NR_tgkill]			sys_tgkill,
	[__NR_utimes]			sys_utimes,
	[__NR_statfs64]			sys_statfs64,
	[__NR_fstatfs64]		sys_fstatfs64,
	[254]				sys_ni_syscall,
	[__NR_rtas]			ppc_rtas,
	[256]				sys_ni_syscall,
	[257]				sys_ni_syscall,
	[258]				sys_ni_syscall,
	[__NR_mbind]			sys_ni_syscall, /* sys_mbind */
	[__NR_get_mempolicy]		sys_ni_syscall, /* sys_get_mempolicy */
	[__NR_set_mempolicy]		sys_ni_syscall, /* sys_set_mempolicy */
	[__NR_mq_open]			sys_ni_syscall, /* sys_mq_open */
	[__NR_mq_unlink]		sys_ni_syscall, /* sys_mq_unlink */
	[__NR_mq_timedsend]		sys_ni_syscall, /* sys_mq_timedsend */
	[__NR_mq_timedreceive]		sys_ni_syscall, /* sys_mq_timedreceive */
	[__NR_mq_notify]		sys_ni_syscall, /* sys_mq_notify */
	[__NR_mq_getsetattr]		sys_ni_syscall, /* sys_mq_getsetattr */
	[__NR_kexec_load]		sys_ni_syscall, /* sys_kexec_load */
	[__NR_add_key]			sys_ni_syscall, /* sys_add_key */
	[__NR_request_key]		sys_ni_syscall, /* sys_request_key */
	[__NR_keyctl]			sys_ni_syscall, /* sys_keyctl */
	[__NR_waitid]			sys_ni_syscall, /* sys_waitid */
	[__NR_ioprio_set]		sys_ni_syscall, /* sys_ioprio_set */
	[__NR_ioprio_get]		sys_ni_syscall, /* sys_ioprio_get */
	[__NR_inotify_init]		sys_ni_syscall, /* sys_inotify_init */
	[__NR_inotify_add_watch]	sys_ni_syscall, /* sys_inotify_add_watch */
	[__NR_inotify_rm_watch]		sys_ni_syscall, /* sys_inotify_rm_watch */
	[__NR_spu_run]			sys_ni_syscall, /* sys_spu_run */
	[__NR_spu_create]		sys_ni_syscall, /* sys_spu_create */
	[__NR_pselect6]			sys_ni_syscall, /* sys_pselect */
	[__NR_ppoll]			sys_ni_syscall, /* sys_ppoll */
	[__NR_unshare]			sys_unshare,
	[__NR_splice]			sys_splice,
};

long spu_sys_callback(struct spu_syscall_block *s)
{
	long (*syscall)(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6);

	BUILD_BUG_ON(ARRAY_SIZE(spu_syscall_table) != __NR_syscalls);

	syscall = spu_syscall_table[s->nr_ret];

	if (s->nr_ret >= __NR_syscalls) {
		pr_debug("%s: invalid syscall #%ld", __FUNCTION__, s->nr_ret);
		return -ENOSYS;
	}

#ifdef DEBUG
	print_symbol(KERN_DEBUG "SPU-syscall %s:", (unsigned long)syscall);
	printk("syscall%ld(%lx, %lx, %lx, %lx, %lx, %lx)\n",
			s->nr_ret,
			s->parm[0], s->parm[1], s->parm[2],
			s->parm[3], s->parm[4], s->parm[5]);
#endif

	return syscall(s->parm[0], s->parm[1], s->parm[2],
		       s->parm[3], s->parm[4], s->parm[5]);
}
EXPORT_SYMBOL_GPL(spu_sys_callback);
