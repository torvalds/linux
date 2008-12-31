#ifndef __SYSDEP_I386_SC_H
#define __SYSDEP_I386_SC_H

#include <user_constants.h>

#define SC_OFFSET(sc, field) \
	*((unsigned long *) &(((char *) (sc))[HOST_##field]))
#define SC_FP_OFFSET(sc, field) \
	*((unsigned long *) &(((char *) (SC_FPSTATE(sc)))[HOST_##field]))
#define SC_FP_OFFSET_PTR(sc, field, type) \
	((type *) &(((char *) (SC_FPSTATE(sc)))[HOST_##field]))

#define SC_IP(sc) SC_OFFSET(sc, SC_IP)
#define SC_SP(sc) SC_OFFSET(sc, SC_SP)
#define SC_FS(sc) SC_OFFSET(sc, SC_FS)
#define SC_GS(sc) SC_OFFSET(sc, SC_GS)
#define SC_DS(sc) SC_OFFSET(sc, SC_DS)
#define SC_ES(sc) SC_OFFSET(sc, SC_ES)
#define SC_SS(sc) SC_OFFSET(sc, SC_SS)
#define SC_CS(sc) SC_OFFSET(sc, SC_CS)
#define SC_EFLAGS(sc) SC_OFFSET(sc, SC_EFLAGS)
#define SC_EAX(sc) SC_OFFSET(sc, SC_EAX)
#define SC_EBX(sc) SC_OFFSET(sc, SC_EBX)
#define SC_ECX(sc) SC_OFFSET(sc, SC_ECX)
#define SC_EDX(sc) SC_OFFSET(sc, SC_EDX)
#define SC_EDI(sc) SC_OFFSET(sc, SC_EDI)
#define SC_ESI(sc) SC_OFFSET(sc, SC_ESI)
#define SC_EBP(sc) SC_OFFSET(sc, SC_EBP)
#define SC_TRAPNO(sc) SC_OFFSET(sc, SC_TRAPNO)
#define SC_ERR(sc) SC_OFFSET(sc, SC_ERR)
#define SC_CR2(sc) SC_OFFSET(sc, SC_CR2)
#define SC_FPSTATE(sc) SC_OFFSET(sc, SC_FPSTATE)
#define SC_SIGMASK(sc) SC_OFFSET(sc, SC_SIGMASK)
#define SC_FP_CW(sc) SC_FP_OFFSET(sc, SC_FP_CW)
#define SC_FP_SW(sc) SC_FP_OFFSET(sc, SC_FP_SW)
#define SC_FP_TAG(sc) SC_FP_OFFSET(sc, SC_FP_TAG)
#define SC_FP_IPOFF(sc) SC_FP_OFFSET(sc, SC_FP_IPOFF)
#define SC_FP_CSSEL(sc) SC_FP_OFFSET(sc, SC_FP_CSSEL)
#define SC_FP_DATAOFF(sc) SC_FP_OFFSET(sc, SC_FP_DATAOFF)
#define SC_FP_DATASEL(sc) SC_FP_OFFSET(sc, SC_FP_DATASEL)
#define SC_FP_ST(sc) SC_FP_OFFSET_PTR(sc, SC_FP_ST, struct _fpstate)
#define SC_FXSR_ENV(sc) SC_FP_OFFSET_PTR(sc, SC_FXSR_ENV, void)

#endif
