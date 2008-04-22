#include <stdio.h>
#include <stddef.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <sys/user.h>
#define __FRAME_OFFSETS
#include <asm/ptrace.h>
#include <asm/types.h>

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define DEFINE_LONGS(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val/sizeof(unsigned long)))

#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem));

void foo(void)
{
	OFFSET(HOST_SC_CR2, sigcontext, cr2);
	OFFSET(HOST_SC_ERR, sigcontext, err);
	OFFSET(HOST_SC_TRAPNO, sigcontext, trapno);

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

	DEFINE(UM_PROT_READ, PROT_READ);
	DEFINE(UM_PROT_WRITE, PROT_WRITE);
	DEFINE(UM_PROT_EXEC, PROT_EXEC);
}
