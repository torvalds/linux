/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SYS_SIGCONTEXT_I386_H
#define __SYS_SIGCONTEXT_I386_H

#include <sysdep/sc.h>

#define IP_RESTART_SYSCALL(ip) ((ip) -= 2)

#define SC_RESTART_SYSCALL(sc) IP_RESTART_SYSCALL(SC_IP(sc))
#define SC_SET_SYSCALL_RETURN(sc, result) SC_EAX(sc) = (result)

#define SC_FAULT_ADDR(sc) SC_CR2(sc)
#define SC_FAULT_TYPE(sc) SC_ERR(sc)

#define FAULT_WRITE(err) (err & 2)
#define TO_SC_ERR(is_write) ((is_write) ? 2 : 0)

#define SC_FAULT_WRITE(sc) (FAULT_WRITE(SC_ERR(sc)))

#define SC_TRAP_TYPE(sc) SC_TRAPNO(sc)

/* ptrace expects that, at the start of a system call, %eax contains
 * -ENOSYS, so this makes it so.
 */
#define SC_START_SYSCALL(sc) do SC_EAX(sc) = -ENOSYS; while(0)

/* This is Page Fault */
#define SEGV_IS_FIXABLE(trap) (trap == 14)

#define SC_SEGV_IS_FIXABLE(sc) (SEGV_IS_FIXABLE(SC_TRAPNO(sc)))

extern unsigned long *sc_sigmask(void *sc_ptr);
extern int sc_get_fpregs(unsigned long buf, void *sc_ptr);

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
