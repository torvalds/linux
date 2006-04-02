#include <stdio.h>
#include <signal.h>
#include <asm/ptrace.h>
#include <asm/user.h>
#include <linux/stddef.h>
#include <sys/poll.h>

#define DEFINE(sym, val) \
	asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define DEFINE_LONGS(sym, val) \
	asm volatile("\n->" #sym " %0 " #val : : "i" (val/sizeof(unsigned long)))

#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem));

void foo(void)
{
	OFFSET(HOST_SC_IP, sigcontext, eip);
	OFFSET(HOST_SC_SP, sigcontext, esp);
	OFFSET(HOST_SC_FS, sigcontext, fs);
	OFFSET(HOST_SC_GS, sigcontext, gs);
	OFFSET(HOST_SC_DS, sigcontext, ds);
	OFFSET(HOST_SC_ES, sigcontext, es);
	OFFSET(HOST_SC_SS, sigcontext, ss);
	OFFSET(HOST_SC_CS, sigcontext, cs);
	OFFSET(HOST_SC_EFLAGS, sigcontext, eflags);
	OFFSET(HOST_SC_EAX, sigcontext, eax);
	OFFSET(HOST_SC_EBX, sigcontext, ebx);
	OFFSET(HOST_SC_ECX, sigcontext, ecx);
	OFFSET(HOST_SC_EDX, sigcontext, edx);
	OFFSET(HOST_SC_EDI, sigcontext, edi);
	OFFSET(HOST_SC_ESI, sigcontext, esi);
	OFFSET(HOST_SC_EBP, sigcontext, ebp);
	OFFSET(HOST_SC_TRAPNO, sigcontext, trapno);
	OFFSET(HOST_SC_ERR, sigcontext, err);
	OFFSET(HOST_SC_CR2, sigcontext, cr2);
	OFFSET(HOST_SC_FPSTATE, sigcontext, fpstate);
	OFFSET(HOST_SC_SIGMASK, sigcontext, oldmask);
	OFFSET(HOST_SC_FP_CW, _fpstate, cw);
	OFFSET(HOST_SC_FP_SW, _fpstate, sw);
	OFFSET(HOST_SC_FP_TAG, _fpstate, tag);
	OFFSET(HOST_SC_FP_IPOFF, _fpstate, ipoff);
	OFFSET(HOST_SC_FP_CSSEL, _fpstate, cssel);
	OFFSET(HOST_SC_FP_DATAOFF, _fpstate, dataoff);
	OFFSET(HOST_SC_FP_DATASEL, _fpstate, datasel);
	OFFSET(HOST_SC_FP_ST, _fpstate, _st);
	OFFSET(HOST_SC_FXSR_ENV, _fpstate, _fxsr_env);

	DEFINE(HOST_FRAME_SIZE, FRAME_SIZE);
	DEFINE_LONGS(HOST_FP_SIZE, sizeof(struct user_i387_struct));
	DEFINE_LONGS(HOST_XFP_SIZE, sizeof(struct user_fxsr_struct));

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
}
