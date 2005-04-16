/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "asm/unistd.h"
#include "sysdep/ptrace.h"

typedef long syscall_handler_t(struct pt_regs);

/* Not declared on x86, incompatible declarations on x86_64, so these have
 * to go here rather than in sys_call_table.c
 */
extern syscall_handler_t sys_ptrace;
extern syscall_handler_t sys_rt_sigaction;

extern syscall_handler_t old_mmap_i386;

#define EXECUTE_SYSCALL(syscall, regs) \
	((long (*)(struct syscall_args)) (*sys_call_table[syscall]))(SYSCALL_ARGS(&regs->regs))

extern long sys_mmap2(unsigned long addr, unsigned long len,
		      unsigned long prot, unsigned long flags,
		      unsigned long fd, unsigned long pgoff);

/* On i386 they choose a meaningless naming.*/
#define __NR_kexec_load __NR_sys_kexec_load

#define ARCH_SYSCALLS \
	[ __NR_waitpid ] = (syscall_handler_t *) sys_waitpid, \
	[ __NR_break ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_oldstat ] = (syscall_handler_t *) sys_stat, \
	[ __NR_umount ] = (syscall_handler_t *) sys_oldumount, \
	[ __NR_stime ] = um_stime, \
	[ __NR_oldfstat ] = (syscall_handler_t *) sys_fstat, \
	[ __NR_stty ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_gtty ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_nice ] = (syscall_handler_t *) sys_nice, \
	[ __NR_ftime ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_prof ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_signal ] = (syscall_handler_t *) sys_signal, \
	[ __NR_lock ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_mpx ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_ulimit ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_oldolduname ] = (syscall_handler_t *) sys_olduname, \
	[ __NR_sigaction ] = (syscall_handler_t *) sys_sigaction, \
	[ __NR_sgetmask ] = (syscall_handler_t *) sys_sgetmask, \
	[ __NR_ssetmask ] = (syscall_handler_t *) sys_ssetmask, \
	[ __NR_sigsuspend ] = (syscall_handler_t *) sys_sigsuspend, \
	[ __NR_sigpending ] = (syscall_handler_t *) sys_sigpending, \
	[ __NR_oldlstat ] = (syscall_handler_t *) sys_lstat, \
	[ __NR_readdir ] = old_readdir, \
	[ __NR_profil ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_socketcall ] = (syscall_handler_t *) sys_socketcall, \
	[ __NR_olduname ] = (syscall_handler_t *) sys_uname, \
	[ __NR_iopl ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_idle ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_ipc ] = (syscall_handler_t *) sys_ipc, \
	[ __NR_sigreturn ] = (syscall_handler_t *) sys_sigreturn, \
	[ __NR_sigprocmask ] = (syscall_handler_t *) sys_sigprocmask, \
	[ __NR_bdflush ] = (syscall_handler_t *) sys_bdflush, \
	[ __NR__llseek ] = (syscall_handler_t *) sys_llseek, \
	[ __NR__newselect ] = (syscall_handler_t *) sys_select, \
	[ __NR_vm86 ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_mmap ] = (syscall_handler_t *) old_mmap_i386, \
	[ __NR_ugetrlimit ] = (syscall_handler_t *) sys_getrlimit, \
	[ __NR_mmap2 ] = (syscall_handler_t *) sys_mmap2, \
	[ __NR_truncate64 ] = (syscall_handler_t *) sys_truncate64, \
	[ __NR_ftruncate64 ] = (syscall_handler_t *) sys_ftruncate64, \
	[ __NR_stat64 ] = (syscall_handler_t *) sys_stat64, \
	[ __NR_lstat64 ] = (syscall_handler_t *) sys_lstat64, \
	[ __NR_fstat64 ] = (syscall_handler_t *) sys_fstat64, \
	[ __NR_fcntl64 ] = (syscall_handler_t *) sys_fcntl64, \
	[ __NR_sendfile64 ] = (syscall_handler_t *) sys_sendfile64, \
	[ __NR_statfs64 ] = (syscall_handler_t *) sys_statfs64, \
	[ __NR_fstatfs64 ] = (syscall_handler_t *) sys_fstatfs64, \
	[ __NR_fadvise64_64 ] = (syscall_handler_t *) sys_fadvise64_64, \
	[ __NR_select ] = (syscall_handler_t *) old_select, \
	[ __NR_vm86old ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_modify_ldt ] = (syscall_handler_t *) sys_modify_ldt, \
	[ __NR_lchown32 ] = (syscall_handler_t *) sys_lchown, \
	[ __NR_getuid32 ] = (syscall_handler_t *) sys_getuid, \
	[ __NR_getgid32 ] = (syscall_handler_t *) sys_getgid, \
	[ __NR_geteuid32 ] = (syscall_handler_t *) sys_geteuid, \
	[ __NR_getegid32 ] = (syscall_handler_t *) sys_getegid, \
	[ __NR_setreuid32 ] = (syscall_handler_t *) sys_setreuid, \
	[ __NR_setregid32 ] = (syscall_handler_t *) sys_setregid, \
	[ __NR_getgroups32 ] = (syscall_handler_t *) sys_getgroups, \
	[ __NR_setgroups32 ] = (syscall_handler_t *) sys_setgroups, \
	[ __NR_fchown32 ] = (syscall_handler_t *) sys_fchown, \
	[ __NR_setresuid32 ] = (syscall_handler_t *) sys_setresuid, \
	[ __NR_getresuid32 ] = (syscall_handler_t *) sys_getresuid, \
	[ __NR_setresgid32 ] = (syscall_handler_t *) sys_setresgid, \
	[ __NR_getresgid32 ] = (syscall_handler_t *) sys_getresgid, \
	[ __NR_chown32 ] = (syscall_handler_t *) sys_chown, \
	[ __NR_setuid32 ] = (syscall_handler_t *) sys_setuid, \
	[ __NR_setgid32 ] = (syscall_handler_t *) sys_setgid, \
	[ __NR_setfsuid32 ] = (syscall_handler_t *) sys_setfsuid, \
	[ __NR_setfsgid32 ] = (syscall_handler_t *) sys_setfsgid, \
	[ __NR_pivot_root ] = (syscall_handler_t *) sys_pivot_root, \
	[ __NR_mincore ] = (syscall_handler_t *) sys_mincore, \
	[ __NR_madvise ] = (syscall_handler_t *) sys_madvise, \
	[ 222 ] = (syscall_handler_t *) sys_ni_syscall, \
	[ 223 ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_set_thread_area ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_get_thread_area ] = (syscall_handler_t *) sys_ni_syscall, \
	[ 251 ] = (syscall_handler_t *) sys_ni_syscall, \
	[ 285 ] = (syscall_handler_t *) sys_ni_syscall,

/* 222 doesn't yet have a name in include/asm-i386/unistd.h */

#define LAST_ARCH_SYSCALL 285

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
