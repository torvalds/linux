/* Copyright (C) 2003 - 2004 PathScale, Inc
 * Released under the GPL
 */

#include <stdio.h>
#include <signal.h>
#include <linux/stddef.h>

#define SC_OFFSET(name, field) \
  printf("#define " name \
	 "(sc) *((unsigned long *) &(((char *) (sc))[%ld]))\n",\
	 offsetof(struct sigcontext, field))

#define SC_FP_OFFSET(name, field) \
  printf("#define " name \
	 "(sc) *((unsigned long *) &(((char *) (SC_FPSTATE(sc)))[%ld]))\n",\
	 offsetof(struct _fpstate, field))

#define SC_FP_OFFSET_PTR(name, field, type) \
  printf("#define " name \
	 "(sc) ((" type " *) &(((char *) (SC_FPSTATE(sc)))[%d]))\n",\
	 offsetof(struct _fpstate, field))

int main(int argc, char **argv)
{
  SC_OFFSET("SC_RBX", rbx);
  SC_OFFSET("SC_RCX", rcx);
  SC_OFFSET("SC_RDX", rdx);
  SC_OFFSET("SC_RSI", rsi);
  SC_OFFSET("SC_RDI", rdi);
  SC_OFFSET("SC_RBP", rbp);
  SC_OFFSET("SC_RAX", rax);
  SC_OFFSET("SC_R8", r8);
  SC_OFFSET("SC_R9", r9);
  SC_OFFSET("SC_R10", r10);
  SC_OFFSET("SC_R11", r11);
  SC_OFFSET("SC_R12", r12);
  SC_OFFSET("SC_R13", r13);
  SC_OFFSET("SC_R14", r14);
  SC_OFFSET("SC_R15", r15);
  SC_OFFSET("SC_IP", rip);
  SC_OFFSET("SC_SP", rsp);
  SC_OFFSET("SC_CR2", cr2);
  SC_OFFSET("SC_ERR", err);
  SC_OFFSET("SC_TRAPNO", trapno);
  SC_OFFSET("SC_CS", cs);
  SC_OFFSET("SC_FS", fs);
  SC_OFFSET("SC_GS", gs);
  SC_OFFSET("SC_EFLAGS", eflags);
  SC_OFFSET("SC_SIGMASK", oldmask);
#if 0
  SC_OFFSET("SC_ORIG_RAX", orig_rax);
  SC_OFFSET("SC_DS", ds);
  SC_OFFSET("SC_ES", es);
  SC_OFFSET("SC_SS", ss);
#endif
  return(0);
}
