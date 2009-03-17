#ifndef _ASM_M68k_SIGCONTEXT_H
#define _ASM_M68k_SIGCONTEXT_H

struct sigcontext {
	unsigned long  sc_mask;		/* old sigmask */
	unsigned long  sc_usp;		/* old user stack pointer */
	unsigned long  sc_d0;
	unsigned long  sc_d1;
	unsigned long  sc_a0;
	unsigned long  sc_a1;
	unsigned short sc_sr;
	unsigned long  sc_pc;
	unsigned short sc_formatvec;
	unsigned long  sc_fpregs[2*3];  /* room for two fp registers */
	unsigned long  sc_fpcntl[3];
	unsigned char  sc_fpstate[216];
};

#endif
