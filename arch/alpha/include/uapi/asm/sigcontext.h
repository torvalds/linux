#ifndef _ASMAXP_SIGCONTEXT_H
#define _ASMAXP_SIGCONTEXT_H

struct sigcontext {
	/*
	 * What should we have here? I'd probably better use the same
	 * stack layout as OSF/1, just in case we ever want to try
	 * running their binaries.. 
	 *
	 * This is the basic layout, but I don't know if we'll ever
	 * actually fill in all the values..
	 */
	 long		sc_onstack;
	 long		sc_mask;
	 long		sc_pc;
	 long		sc_ps;
	 long		sc_regs[32];
	 long		sc_ownedfp;
	 long		sc_fpregs[32];
	 unsigned long	sc_fpcr;
	 unsigned long	sc_fp_control;
	 unsigned long	sc_reserved1, sc_reserved2;
	 unsigned long	sc_ssize;
	 char *		sc_sbase;
	 unsigned long	sc_traparg_a0;
	 unsigned long	sc_traparg_a1;
	 unsigned long	sc_traparg_a2;
	 unsigned long	sc_fp_trap_pc;
	 unsigned long	sc_fp_trigger_sum;
	 unsigned long	sc_fp_trigger_inst;
};


#endif
