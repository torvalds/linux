/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_SH_SIGCONTEXT_H
#define __ASM_SH_SIGCONTEXT_H

struct sigcontext {
	unsigned long	oldmask;

#if defined(__SH5__) || defined(CONFIG_CPU_SH5)
	/* CPU registers */
	unsigned long long sc_regs[63];
	unsigned long long sc_tregs[8];
	unsigned long long sc_pc;
	unsigned long long sc_sr;

	/* FPU registers */
	unsigned long long sc_fpregs[32];
	unsigned int sc_fpscr;
	unsigned int sc_fpvalid;
#else
	/* CPU registers */
	unsigned long sc_regs[16];
	unsigned long sc_pc;
	unsigned long sc_pr;
	unsigned long sc_sr;
	unsigned long sc_gbr;
	unsigned long sc_mach;
	unsigned long sc_macl;

	/* FPU registers */
	unsigned long sc_fpregs[16];
	unsigned long sc_xfpregs[16];
	unsigned int sc_fpscr;
	unsigned int sc_fpul;
	unsigned int sc_ownedfp;
#endif
};

#endif /* __ASM_SH_SIGCONTEXT_H */
