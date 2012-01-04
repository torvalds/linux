/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_I386_PTRACE_H
#define __SYSDEP_I386_PTRACE_H

#include <generated/user_constants.h>
#include "sysdep/faultinfo.h"

#define MAX_REG_NR (UM_FRAME_SIZE / sizeof(unsigned long))
#define MAX_REG_OFFSET (UM_FRAME_SIZE)

static inline void update_debugregs(int seq) {}

/* syscall emulation path in ptrace */

#ifndef PTRACE_SYSEMU
#define PTRACE_SYSEMU 31
#endif

void set_using_sysemu(int value);
int get_using_sysemu(void);
extern int sysemu_supported;

#define REGS_IP(r) ((r)[HOST_IP])
#define REGS_SP(r) ((r)[HOST_SP])
#define REGS_EFLAGS(r) ((r)[HOST_EFLAGS])
#define REGS_EAX(r) ((r)[HOST_AX])
#define REGS_EBX(r) ((r)[HOST_BX])
#define REGS_ECX(r) ((r)[HOST_CX])
#define REGS_EDX(r) ((r)[HOST_DX])
#define REGS_ESI(r) ((r)[HOST_SI])
#define REGS_EDI(r) ((r)[HOST_DI])
#define REGS_EBP(r) ((r)[HOST_BP])
#define REGS_CS(r) ((r)[HOST_CS])
#define REGS_SS(r) ((r)[HOST_SS])
#define REGS_DS(r) ((r)[HOST_DS])
#define REGS_ES(r) ((r)[HOST_ES])
#define REGS_FS(r) ((r)[HOST_FS])
#define REGS_GS(r) ((r)[HOST_GS])

#define REGS_SET_SYSCALL_RETURN(r, res) REGS_EAX(r) = (res)

#define IP_RESTART_SYSCALL(ip) ((ip) -= 2)
#define REGS_RESTART_SYSCALL(r) IP_RESTART_SYSCALL(REGS_IP(r))

#ifndef PTRACE_SYSEMU_SINGLESTEP
#define PTRACE_SYSEMU_SINGLESTEP 32
#endif

struct uml_pt_regs {
	unsigned long gp[MAX_REG_NR];
	unsigned long fp[HOST_FPX_SIZE];
	struct faultinfo faultinfo;
	long syscall;
	int is_user;
};

#define EMPTY_UML_PT_REGS { }

#define UPT_IP(r) REGS_IP((r)->gp)
#define UPT_SP(r) REGS_SP((r)->gp)
#define UPT_EFLAGS(r) REGS_EFLAGS((r)->gp)
#define UPT_EAX(r) REGS_EAX((r)->gp)
#define UPT_EBX(r) REGS_EBX((r)->gp)
#define UPT_ECX(r) REGS_ECX((r)->gp)
#define UPT_EDX(r) REGS_EDX((r)->gp)
#define UPT_ESI(r) REGS_ESI((r)->gp)
#define UPT_EDI(r) REGS_EDI((r)->gp)
#define UPT_EBP(r) REGS_EBP((r)->gp)
#define UPT_ORIG_EAX(r) ((r)->syscall)
#define UPT_CS(r) REGS_CS((r)->gp)
#define UPT_SS(r) REGS_SS((r)->gp)
#define UPT_DS(r) REGS_DS((r)->gp)
#define UPT_ES(r) REGS_ES((r)->gp)
#define UPT_FS(r) REGS_FS((r)->gp)
#define UPT_GS(r) REGS_GS((r)->gp)

#define UPT_SYSCALL_ARG1(r) UPT_EBX(r)
#define UPT_SYSCALL_ARG2(r) UPT_ECX(r)
#define UPT_SYSCALL_ARG3(r) UPT_EDX(r)
#define UPT_SYSCALL_ARG4(r) UPT_ESI(r)
#define UPT_SYSCALL_ARG5(r) UPT_EDI(r)
#define UPT_SYSCALL_ARG6(r) UPT_EBP(r)

extern int user_context(unsigned long sp);

#define UPT_IS_USER(r) ((r)->is_user)

struct syscall_args {
	unsigned long args[6];
};

#define SYSCALL_ARGS(r) ((struct syscall_args) \
			 { .args = { UPT_SYSCALL_ARG1(r),	\
				     UPT_SYSCALL_ARG2(r),	\
				     UPT_SYSCALL_ARG3(r),	\
				     UPT_SYSCALL_ARG4(r),	\
				     UPT_SYSCALL_ARG5(r),	\
				     UPT_SYSCALL_ARG6(r) } } )

#define UPT_RESTART_SYSCALL(r) REGS_RESTART_SYSCALL((r)->gp)

#define UPT_ORIG_SYSCALL(r) UPT_EAX(r)
#define UPT_SYSCALL_NR(r) UPT_ORIG_EAX(r)
#define UPT_SYSCALL_RET(r) UPT_EAX(r)

#define UPT_FAULTINFO(r) (&(r)->faultinfo)

extern void arch_init_registers(int pid);

#endif
