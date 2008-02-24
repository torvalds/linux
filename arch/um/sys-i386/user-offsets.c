#include <stdio.h>
#include <stddef.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <asm/ptrace.h>

#define DEFINE(sym, val) \
	asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define DEFINE_LONGS(sym, val) \
	asm volatile("\n->" #sym " %0 " #val : : "i" (val/sizeof(unsigned long)))

#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem));

void foo(void)
{
	OFFSET(HOST_SC_TRAPNO, sigcontext, trapno);
	OFFSET(HOST_SC_ERR, sigcontext, err);
	OFFSET(HOST_SC_CR2, sigcontext, cr2);

	DEFINE_LONGS(HOST_FP_SIZE, sizeof(struct user_fpregs_struct));
	DEFINE_LONGS(HOST_XFP_SIZE, sizeof(struct user_fpxregs_struct));

	DEFINE(HOST_IP, EIP);
	DEFINE(HOST_SP, UESP);
	DEFINE(HOST_EFLAGS, EFL);
	DEFINE(HOST_EAX, EAX);
	DEFINE(HOST_EBX, EBX);
	DEFINE(HOST_ECX, ECX);
	DEFINE(HOST_EDX, EDX);
	DEFINE(HOST_ESI, ESI);
	DEFINE(HOST_EDI, EDI);
	DEFINE(HOST_EBP, EBP);
	DEFINE(HOST_CS, CS);
	DEFINE(HOST_SS, SS);
	DEFINE(HOST_DS, DS);
	DEFINE(HOST_FS, FS);
	DEFINE(HOST_ES, ES);
	DEFINE(HOST_GS, GS);
	DEFINE(UM_FRAME_SIZE, sizeof(struct user_regs_struct));

	/* XXX Duplicated between i386 and x86_64 */
	DEFINE(UM_POLLIN, POLLIN);
	DEFINE(UM_POLLPRI, POLLPRI);
	DEFINE(UM_POLLOUT, POLLOUT);

	DEFINE(UM_PROT_READ, PROT_READ);
	DEFINE(UM_PROT_WRITE, PROT_WRITE);
	DEFINE(UM_PROT_EXEC, PROT_EXEC);
}
