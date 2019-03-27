/*===- llvm/Support/Solaris/sys/regset.h ------------------------*- C++ -*-===*
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===*
 *
 * This file works around excessive name space pollution from the system header
 * on Solaris hosts.
 *
 *===----------------------------------------------------------------------===*/

#ifndef LLVM_SUPPORT_SOLARIS_SYS_REGSET_H

#include_next <sys/regset.h>

#undef CS
#undef DS
#undef ES
#undef FS
#undef GS
#undef SS
#undef EAX
#undef ECX
#undef EDX
#undef EBX
#undef ESP
#undef EBP
#undef ESI
#undef EDI
#undef EIP
#undef UESP
#undef EFL
#undef ERR
#undef TRAPNO

#endif
