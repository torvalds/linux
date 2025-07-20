/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_X86_PTRACE_ABI_H
#define _ASM_X86_PTRACE_ABI_H

#ifdef __i386__

#define EBX 0
#define ECX 1
#define EDX 2
#define ESI 3
#define EDI 4
#define EBP 5
#define EAX 6
#define DS 7
#define ES 8
#define FS 9
#define GS 10
#define ORIG_EAX 11
#define EIP 12
#define CS  13
#define EFL 14
#define UESP 15
#define SS   16
#define FRAME_SIZE 17

#else /* __i386__ */

#if defined(__ASSEMBLER__) || defined(__FRAME_OFFSETS)
/*
 * C ABI says these regs are callee-preserved. They aren't saved on kernel entry
 * unless syscall needs a complete, fully filled "struct pt_regs".
 */
#define R15 0
#define R14 8
#define R13 16
#define R12 24
#define RBP 32
#define RBX 40
/* These regs are callee-clobbered. Always saved on kernel entry. */
#define R11 48
#define R10 56
#define R9 64
#define R8 72
#define RAX 80
#define RCX 88
#define RDX 96
#define RSI 104
#define RDI 112
/*
 * On syscall entry, this is syscall#. On CPU exception, this is error code.
 * On hw interrupt, it's IRQ number:
 */
#define ORIG_RAX 120
/* Return frame for iretq */
#define RIP 128
#define CS 136
#define EFLAGS 144
#define RSP 152
#define SS 160
#endif /* __ASSEMBLER__ */

/* top of stack page */
#define FRAME_SIZE 168

#endif /* !__i386__ */

/* Arbitrarily choose the same ptrace numbers as used by the Sparc code. */
#define PTRACE_GETREGS            12
#define PTRACE_SETREGS            13
#define PTRACE_GETFPREGS          14
#define PTRACE_SETFPREGS          15
#define PTRACE_GETFPXREGS         18
#define PTRACE_SETFPXREGS         19

#define PTRACE_OLDSETOPTIONS      21

/* only useful for access 32bit programs / kernels */
#define PTRACE_GET_THREAD_AREA    25
#define PTRACE_SET_THREAD_AREA    26

#ifdef __x86_64__
# define PTRACE_ARCH_PRCTL	  30
#endif

#define PTRACE_SYSEMU		  31
#define PTRACE_SYSEMU_SINGLESTEP  32

#define PTRACE_SINGLEBLOCK	33	/* resume execution until next branch */

#ifndef __ASSEMBLER__
#include <linux/types.h>
#endif

#endif /* _ASM_X86_PTRACE_ABI_H */
