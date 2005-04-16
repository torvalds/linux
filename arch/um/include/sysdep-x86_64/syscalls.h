/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#ifndef __SYSDEP_X86_64_SYSCALLS_H__
#define __SYSDEP_X86_64_SYSCALLS_H__

#include <linux/msg.h>
#include <linux/shm.h>

typedef long syscall_handler_t(void);

extern syscall_handler_t *ia32_sys_call_table[];

#define EXECUTE_SYSCALL(syscall, regs) \
	(((long (*)(long, long, long, long, long, long)) \
	  (*sys_call_table[syscall]))(UPT_SYSCALL_ARG1(&regs->regs), \
		 		      UPT_SYSCALL_ARG2(&regs->regs), \
				      UPT_SYSCALL_ARG3(&regs->regs), \
				      UPT_SYSCALL_ARG4(&regs->regs), \
				      UPT_SYSCALL_ARG5(&regs->regs), \
				      UPT_SYSCALL_ARG6(&regs->regs)))

extern long old_mmap(unsigned long addr, unsigned long len,
		     unsigned long prot, unsigned long flags,
		     unsigned long fd, unsigned long pgoff);
extern syscall_handler_t wrap_sys_shmat;
extern syscall_handler_t sys_modify_ldt;
extern syscall_handler_t sys_arch_prctl;

#define ARCH_SYSCALLS \
	[ __NR_mmap ] = (syscall_handler_t *) old_mmap, \
	[ __NR_select ] = (syscall_handler_t *) sys_select, \
	[ __NR_mincore ] = (syscall_handler_t *) sys_mincore, \
	[ __NR_madvise ] = (syscall_handler_t *) sys_madvise, \
	[ __NR_shmget ] = (syscall_handler_t *) sys_shmget, \
	[ __NR_shmat ] = (syscall_handler_t *) wrap_sys_shmat, \
	[ __NR_shmctl ] = (syscall_handler_t *) sys_shmctl, \
	[ __NR_semop ] = (syscall_handler_t *) sys_semop, \
	[ __NR_semget ] = (syscall_handler_t *) sys_semget, \
	[ __NR_semctl ] = (syscall_handler_t *) sys_semctl, \
	[ __NR_shmdt ] = (syscall_handler_t *) sys_shmdt, \
	[ __NR_msgget ] = (syscall_handler_t *) sys_msgget, \
	[ __NR_msgsnd ] = (syscall_handler_t *) sys_msgsnd, \
	[ __NR_msgrcv ] = (syscall_handler_t *) sys_msgrcv, \
	[ __NR_msgctl ] = (syscall_handler_t *) sys_msgctl, \
	[ __NR_pivot_root ] = (syscall_handler_t *) sys_pivot_root, \
	[ __NR_tuxcall ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_security ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_epoll_ctl_old ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_epoll_wait_old ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_modify_ldt ] = (syscall_handler_t *) sys_modify_ldt, \
	[ __NR_arch_prctl ] = (syscall_handler_t *) sys_arch_prctl, \
	[ __NR_socket ] = (syscall_handler_t *) sys_socket, \
	[ __NR_connect ] = (syscall_handler_t *) sys_connect, \
	[ __NR_accept ] = (syscall_handler_t *) sys_accept, \
	[ __NR_recvfrom ] = (syscall_handler_t *) sys_recvfrom, \
	[ __NR_recvmsg ] = (syscall_handler_t *) sys_recvmsg, \
	[ __NR_sendmsg ] = (syscall_handler_t *) sys_sendmsg, \
	[ __NR_bind ] = (syscall_handler_t *) sys_bind, \
	[ __NR_listen ] = (syscall_handler_t *) sys_listen, \
	[ __NR_getsockname ] = (syscall_handler_t *) sys_getsockname, \
	[ __NR_getpeername ] = (syscall_handler_t *) sys_getpeername, \
	[ __NR_socketpair ] = (syscall_handler_t *) sys_socketpair, \
	[ __NR_sendto ] = (syscall_handler_t *) sys_sendto, \
	[ __NR_shutdown ] = (syscall_handler_t *) sys_shutdown, \
	[ __NR_setsockopt ] = (syscall_handler_t *) sys_setsockopt, \
	[ __NR_getsockopt ] = (syscall_handler_t *) sys_getsockopt, \
	[ __NR_iopl ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_set_thread_area ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_get_thread_area ] = (syscall_handler_t *) sys_ni_syscall, \
	[ __NR_semtimedop ] = (syscall_handler_t *) sys_semtimedop, \
	[ 251 ] = (syscall_handler_t *) sys_ni_syscall,

#define LAST_ARCH_SYSCALL 251
#define NR_syscalls 1024

#endif

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
