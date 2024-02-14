/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_LINKAGE_H
#define _ASM_POWERPC_LINKAGE_H

#include <asm/types.h>

#ifdef CONFIG_PPC64_ELF_ABI_V1
#define cond_syscall(x) \
	asm ("\t.weak " #x "\n\t.set " #x ", sys_ni_syscall\n"		\
	     "\t.weak ." #x "\n\t.set ." #x ", .sys_ni_syscall\n")
#define SYSCALL_ALIAS(alias, name)					\
	asm ("\t.globl " #alias "\n\t.set " #alias ", " #name "\n"	\
	     "\t.globl ." #alias "\n\t.set ." #alias ", ." #name)
#endif

#endif	/* _ASM_POWERPC_LINKAGE_H */
