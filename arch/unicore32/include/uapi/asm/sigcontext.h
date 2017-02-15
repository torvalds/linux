/*
 * linux/arch/unicore32/include/asm/sigcontext.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_SIGCONTEXT_H__
#define __UNICORE_SIGCONTEXT_H__

#include <asm/ptrace.h>
/*
 * Signal context structure - contains all info to do with the state
 * before the signal handler was invoked.  Note: only add new entries
 * to the end of the structure.
 */
#ifndef CONFIG_UNICORE32_OLDABI
struct sigcontext {
	unsigned long trap_no;
	unsigned long error_code;
	unsigned long oldmask;
	unsigned long fault_address;
	struct pt_regs regs;
};
#else
struct pt_regs_sigcontext {
	unsigned long uregs[33];
};

struct sigcontext {
	unsigned long trap_no;
	unsigned long error_code;
	unsigned long oldmask;
	struct pt_regs_sigcontext regs;
	unsigned long fault_address;
};
#endif /* CONFIG_UNICORE32_OLDABI */

#endif
