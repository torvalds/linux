#ifndef __SYSDEP_X86_64_SC_H
#define __SYSDEP_X86_64_SC_H

/* Copyright (C) 2003 - 2004 PathScale, Inc
 * Released under the GPL
 */

#include <user_constants.h>

#define SC_OFFSET(sc, field) \
	 *((unsigned long *) &(((char *) (sc))[HOST_##field]))

#define SC_RBX(sc) SC_OFFSET(sc, SC_RBX)
#define SC_RCX(sc) SC_OFFSET(sc, SC_RCX)
#define SC_RDX(sc) SC_OFFSET(sc, SC_RDX)
#define SC_RSI(sc) SC_OFFSET(sc, SC_RSI)
#define SC_RDI(sc) SC_OFFSET(sc, SC_RDI)
#define SC_RBP(sc) SC_OFFSET(sc, SC_RBP)
#define SC_RAX(sc) SC_OFFSET(sc, SC_RAX)
#define SC_R8(sc) SC_OFFSET(sc, SC_R8)
#define SC_R9(sc) SC_OFFSET(sc, SC_R9)
#define SC_R10(sc) SC_OFFSET(sc, SC_R10)
#define SC_R11(sc) SC_OFFSET(sc, SC_R11)
#define SC_R12(sc) SC_OFFSET(sc, SC_R12)
#define SC_R13(sc) SC_OFFSET(sc, SC_R13)
#define SC_R14(sc) SC_OFFSET(sc, SC_R14)
#define SC_R15(sc) SC_OFFSET(sc, SC_R15)
#define SC_IP(sc) SC_OFFSET(sc, SC_IP)
#define SC_SP(sc) SC_OFFSET(sc, SC_SP)
#define SC_CR2(sc) SC_OFFSET(sc, SC_CR2)
#define SC_ERR(sc) SC_OFFSET(sc, SC_ERR)
#define SC_TRAPNO(sc) SC_OFFSET(sc, SC_TRAPNO)
#define SC_CS(sc) SC_OFFSET(sc, SC_CS)
#define SC_FS(sc) SC_OFFSET(sc, SC_FS)
#define SC_GS(sc) SC_OFFSET(sc, SC_GS)
#define SC_EFLAGS(sc) SC_OFFSET(sc, SC_EFLAGS)
#define SC_SIGMASK(sc) SC_OFFSET(sc, SC_SIGMASK)
#define SC_SS(sc) SC_OFFSET(sc, SC_SS)
#if 0
#define SC_ORIG_RAX(sc) SC_OFFSET(sc, SC_ORIG_RAX)
#define SC_DS(sc) SC_OFFSET(sc, SC_DS)
#define SC_ES(sc) SC_OFFSET(sc, SC_ES)
#endif

#endif
