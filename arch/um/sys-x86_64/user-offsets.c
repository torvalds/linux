#include <stdio.h>
#include <stddef.h>
#include <signal.h>
#define __FRAME_OFFSETS
#include <asm/ptrace.h>
#include <asm/types.h>
/* For some reason, x86_64 defines u64 and u32 only in <pci/types.h>, which I
 * refuse to include here, even though they're used throughout the headers.
 * These are used in asm/user.h, and that include can't be avoided because of
 * the sizeof(struct user_regs_struct) below.
 */
typedef __u64 u64;
typedef __u32 u32;
#include <asm/user.h>

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem));

void foo(void)
{
	OFFSET(SC_RBX, sigcontext, rbx);
	OFFSET(SC_RCX, sigcontext, rcx);
	OFFSET(SC_RDX, sigcontext, rdx);
	OFFSET(SC_RSI, sigcontext, rsi);
	OFFSET(SC_RDI, sigcontext, rdi);
	OFFSET(SC_RBP, sigcontext, rbp);
	OFFSET(SC_RAX, sigcontext, rax);
	OFFSET(SC_R8, sigcontext, r8);
	OFFSET(SC_R9, sigcontext, r9);
	OFFSET(SC_R10, sigcontext, r10);
	OFFSET(SC_R11, sigcontext, r11);
	OFFSET(SC_R12, sigcontext, r12);
	OFFSET(SC_R13, sigcontext, r13);
	OFFSET(SC_R14, sigcontext, r14);
	OFFSET(SC_R15, sigcontext, r15);
	OFFSET(SC_IP, sigcontext, rip);
	OFFSET(SC_SP, sigcontext, rsp);
	OFFSET(SC_CR2, sigcontext, cr2);
	OFFSET(SC_ERR, sigcontext, err);
	OFFSET(SC_TRAPNO, sigcontext, trapno);
	OFFSET(SC_CS, sigcontext, cs);
	OFFSET(SC_FS, sigcontext, fs);
	OFFSET(SC_GS, sigcontext, gs);
	OFFSET(SC_EFLAGS, sigcontext, eflags);
	OFFSET(SC_SIGMASK, sigcontext, oldmask);
#if 0
	OFFSET(SC_ORIG_RAX, sigcontext, orig_rax);
	OFFSET(SC_DS, sigcontext, ds);
	OFFSET(SC_ES, sigcontext, es);
	OFFSET(SC_SS, sigcontext, ss);
#endif

	DEFINE(HOST_FRAME_SIZE, FRAME_SIZE);
	DEFINE(HOST_RBX, RBX);
	DEFINE(HOST_RCX, RCX);
	DEFINE(HOST_RDI, RDI);
	DEFINE(HOST_RSI, RSI);
	DEFINE(HOST_RDX, RDX);
	DEFINE(HOST_RBP, RBP);
	DEFINE(HOST_RAX, RAX);
	DEFINE(HOST_R8, R8);
	DEFINE(HOST_R9, R9);
	DEFINE(HOST_R10, R10);
	DEFINE(HOST_R11, R11);
	DEFINE(HOST_R12, R12);
	DEFINE(HOST_R13, R13);
	DEFINE(HOST_R14, R14);
	DEFINE(HOST_R15, R15);
	DEFINE(HOST_ORIG_RAX, ORIG_RAX);
	DEFINE(HOST_CS, CS);
	DEFINE(HOST_SS, SS);
	DEFINE(HOST_EFLAGS, EFLAGS);
#if 0
	DEFINE(HOST_FS, FS);
	DEFINE(HOST_GS, GS);
	DEFINE(HOST_DS, DS);
	DEFINE(HOST_ES, ES);
#endif

	DEFINE(HOST_IP, RIP);
	DEFINE(HOST_SP, RSP);
	DEFINE(__UM_FRAME_SIZE, sizeof(struct user_regs_struct));
}
