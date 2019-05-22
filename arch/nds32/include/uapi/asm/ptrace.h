/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __UAPI_ASM_NDS32_PTRACE_H
#define __UAPI_ASM_NDS32_PTRACE_H

#ifndef __ASSEMBLY__

/*
 * User structures for general purpose register.
 */
struct user_pt_regs {
	long uregs[26];
	long fp;
	long gp;
	long lp;
	long sp;
	long ipc;
	long lb;
	long le;
	long lc;
	long syscallno;
};
#endif
#endif
