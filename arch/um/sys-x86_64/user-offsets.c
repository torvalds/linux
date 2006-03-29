#include <stdio.h>
#include <stddef.h>
#include <signal.h>
#include <sys/poll.h>
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

#define DEFINE_LONGS(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val/sizeof(unsigned long)))

#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem));

void foo(void)
{
	OFFSET(HOST_SC_RBX, sigcontext, rbx);
	OFFSET(HOST_SC_RCX, sigcontext, rcx);
	OFFSET(HOST_SC_RDX, sigcontext, rdx);
	OFFSET(HOST_SC_RSI, sigcontext, rsi);
	OFFSET(HOST_SC_RDI, sigcontext, rdi);
	OFFSET(HOST_SC_RBP, sigcontext, rbp);
	OFFSET(HOST_SC_RAX, sigcontext, rax);
	OFFSET(HOST_SC_R8, sigcontext, r8);
	OFFSET(HOST_SC_R9, sigcontext, r9);
	OFFSET(HOST_SC_R10, sigcontext, r10);
	OFFSET(HOST_SC_R11, sigcontext, r11);
	OFFSET(HOST_SC_R12, sigcontext, r12);
	OFFSET(HOST_SC_R13, sigcontext, r13);
	OFFSET(HOST_SC_R14, sigcontext, r14);
	OFFSET(HOST_SC_R15, sigcontext, r15);
	OFFSET(HOST_SC_IP, sigcontext, rip);
	OFFSET(HOST_SC_SP, sigcontext, rsp);
	OFFSET(HOST_SC_CR2, sigcontext, cr2);
	OFFSET(HOST_SC_ERR, sigcontext, err);
	OFFSET(HOST_SC_TRAPNO, sigcontext, trapno);
	OFFSET(HOST_SC_CS, sigcontext, cs);
	OFFSET(HOST_SC_FS, sigcontext, fs);
	OFFSET(HOST_SC_GS, sigcontext, gs);
	OFFSET(HOST_SC_EFLAGS, sigcontext, eflags);
	OFFSET(HOST_SC_SIGMASK, sigcontext, oldmask);
#if 0
	OFFSET(HOST_SC_ORIG_RAX, sigcontext, orig_rax);
	OFFSET(HOST_SC_DS, sigcontext, ds);
	OFFSET(HOST_SC_ES, sigcontext, es);
	OFFSET(HOST_SC_SS, sigcontext, ss);
#endif

	DEFINE_LONGS(HOST_FRAME_SIZE, FRAME_SIZE);
	DEFINE(HOST_FP_SIZE, sizeof(struct _fpstate) / sizeof(unsigned long));
	DEFINE(HOST_XFP_SIZE, 0);
	DEFINE_LONGS(HOST_RBX, RBX);
	DEFINE_LONGS(HOST_RCX, RCX);
	DEFINE_LONGS(HOST_RDI, RDI);
	DEFINE_LONGS(HOST_RSI, RSI);
	DEFINE_LONGS(HOST_RDX, RDX);
	DEFINE_LONGS(HOST_RBP, RBP);
	DEFINE_LONGS(HOST_RAX, RAX);
	DEFINE_LONGS(HOST_R8, R8);
	DEFINE_LONGS(HOST_R9, R9);
	DEFINE_LONGS(HOST_R10, R10);
	DEFINE_LONGS(HOST_R11, R11);
	DEFINE_LONGS(HOST_R12, R12);
	DEFINE_LONGS(HOST_R13, R13);
	DEFINE_LONGS(HOST_R14, R14);
	DEFINE_LONGS(HOST_R15, R15);
	DEFINE_LONGS(HOST_ORIG_RAX, ORIG_RAX);
	DEFINE_LONGS(HOST_CS, CS);
	DEFINE_LONGS(HOST_SS, SS);
	DEFINE_LONGS(HOST_EFLAGS, EFLAGS);
#if 0
	DEFINE_LONGS(HOST_FS, FS);
	DEFINE_LONGS(HOST_GS, GS);
	DEFINE_LONGS(HOST_DS, DS);
	DEFINE_LONGS(HOST_ES, ES);
#endif

	DEFINE_LONGS(HOST_IP, RIP);
	DEFINE_LONGS(HOST_SP, RSP);
	DEFINE(UM_FRAME_SIZE, sizeof(struct user_regs_struct));

	/* XXX Duplicated between i386 and x86_64 */
	DEFINE(UM_POLLIN, POLLIN);
	DEFINE(UM_POLLPRI, POLLPRI);
	DEFINE(UM_POLLOUT, POLLOUT);
}
