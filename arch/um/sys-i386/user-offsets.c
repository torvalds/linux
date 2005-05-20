#include <stdio.h>
#include <signal.h>
#include <asm/ptrace.h>
#include <asm/user.h>
#include <linux/stddef.h>

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem));

void foo(void)
{
	OFFSET(SC_IP, sigcontext, eip);
	OFFSET(SC_SP, sigcontext, esp);
	OFFSET(SC_FS, sigcontext, fs);
	OFFSET(SC_GS, sigcontext, gs);
	OFFSET(SC_DS, sigcontext, ds);
	OFFSET(SC_ES, sigcontext, es);
	OFFSET(SC_SS, sigcontext, ss);
	OFFSET(SC_CS, sigcontext, cs);
	OFFSET(SC_EFLAGS, sigcontext, eflags);
	OFFSET(SC_EAX, sigcontext, eax);
	OFFSET(SC_EBX, sigcontext, ebx);
	OFFSET(SC_ECX, sigcontext, ecx);
	OFFSET(SC_EDX, sigcontext, edx);
	OFFSET(SC_EDI, sigcontext, edi);
	OFFSET(SC_ESI, sigcontext, esi);
	OFFSET(SC_EBP, sigcontext, ebp);
	OFFSET(SC_TRAPNO, sigcontext, trapno);
	OFFSET(SC_ERR, sigcontext, err);
	OFFSET(SC_CR2, sigcontext, cr2);
	OFFSET(SC_FPSTATE, sigcontext, fpstate);
	OFFSET(SC_SIGMASK, sigcontext, oldmask);
	OFFSET(SC_FP_CW, _fpstate, cw);
	OFFSET(SC_FP_SW, _fpstate, sw);
	OFFSET(SC_FP_TAG, _fpstate, tag);
	OFFSET(SC_FP_IPOFF, _fpstate, ipoff);
	OFFSET(SC_FP_CSSEL, _fpstate, cssel);
	OFFSET(SC_FP_DATAOFF, _fpstate, dataoff);
	OFFSET(SC_FP_DATASEL, _fpstate, datasel);
	OFFSET(SC_FP_ST, _fpstate, _st);
	OFFSET(SC_FXSR_ENV, _fpstate, _fxsr_env);

	DEFINE(HOST_FRAME_SIZE, FRAME_SIZE);
	DEFINE(HOST_FP_SIZE,
		sizeof(struct user_i387_struct) / sizeof(unsigned long));
	DEFINE(HOST_XFP_SIZE,
	       sizeof(struct user_fxsr_struct) / sizeof(unsigned long));

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
	DEFINE(__UM_FRAME_SIZE, sizeof(struct user_regs_struct));
}
