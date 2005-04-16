#include <stdio.h>
#include <signal.h>
#include <linux/stddef.h>

#define SC_OFFSET(name, field) \
  printf("#define " name "(sc) *((unsigned long *) &(((char *) (sc))[%d]))\n",\
	 offsetof(struct sigcontext, field))

#define SC_FP_OFFSET(name, field) \
  printf("#define " name \
	 "(sc) *((unsigned long *) &(((char *) (SC_FPSTATE(sc)))[%d]))\n",\
	 offsetof(struct _fpstate, field))

#define SC_FP_OFFSET_PTR(name, field, type) \
  printf("#define " name \
	 "(sc) ((" type " *) &(((char *) (SC_FPSTATE(sc)))[%d]))\n",\
	 offsetof(struct _fpstate, field))

int main(int argc, char **argv)
{
  SC_OFFSET("SC_IP", eip);
  SC_OFFSET("SC_SP", esp);
  SC_OFFSET("SC_FS", fs);
  SC_OFFSET("SC_GS", gs);
  SC_OFFSET("SC_DS", ds);
  SC_OFFSET("SC_ES", es);
  SC_OFFSET("SC_SS", ss);
  SC_OFFSET("SC_CS", cs);
  SC_OFFSET("SC_EFLAGS", eflags);
  SC_OFFSET("SC_EAX", eax);
  SC_OFFSET("SC_EBX", ebx);
  SC_OFFSET("SC_ECX", ecx);
  SC_OFFSET("SC_EDX", edx);
  SC_OFFSET("SC_EDI", edi);
  SC_OFFSET("SC_ESI", esi);
  SC_OFFSET("SC_EBP", ebp);
  SC_OFFSET("SC_TRAPNO", trapno);
  SC_OFFSET("SC_ERR", err);
  SC_OFFSET("SC_CR2", cr2);
  SC_OFFSET("SC_FPSTATE", fpstate);
  SC_OFFSET("SC_SIGMASK", oldmask);
  SC_FP_OFFSET("SC_FP_CW", cw);
  SC_FP_OFFSET("SC_FP_SW", sw);
  SC_FP_OFFSET("SC_FP_TAG", tag);
  SC_FP_OFFSET("SC_FP_IPOFF", ipoff);
  SC_FP_OFFSET("SC_FP_CSSEL", cssel);
  SC_FP_OFFSET("SC_FP_DATAOFF", dataoff);
  SC_FP_OFFSET("SC_FP_DATASEL", datasel);
  SC_FP_OFFSET_PTR("SC_FP_ST", _st, "struct _fpstate");
  SC_FP_OFFSET_PTR("SC_FXSR_ENV", _fxsr_env, "void");
  return(0);
}
