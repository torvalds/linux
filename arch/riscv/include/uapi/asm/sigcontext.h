/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _UAPI_ASM_RISCV_SIGCONTEXT_H
#define _UAPI_ASM_RISCV_SIGCONTEXT_H

#include <asm/ptrace.h>

/* The Magic number for signal context frame header. */
#define RISCV_V_MAGIC	0x53465457
#define END_MAGIC	0x0

/* The size of END signal context header. */
#define END_HDR_SIZE	0x0

#ifndef __ASSEMBLY__

struct __sc_riscv_v_state {
	struct __riscv_v_ext_state v_state;
} __attribute__((aligned(16)));

/*
 * Signal context structure
 *
 * This contains the context saved before a signal handler is invoked;
 * it is restored by sys_rt_sigreturn.
 */
struct sigcontext {
	struct user_regs_struct sc_regs;
	union {
		union __riscv_fp_state sc_fpregs;
		struct __riscv_extra_ext_header sc_extdesc;
	};
};

#endif /*!__ASSEMBLY__*/

#endif /* _UAPI_ASM_RISCV_SIGCONTEXT_H */
