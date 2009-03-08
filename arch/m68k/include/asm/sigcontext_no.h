#ifndef _ASM_M68KNOMMU_SIGCONTEXT_H
#define _ASM_M68KNOMMU_SIGCONTEXT_H

struct sigcontext {
	unsigned long  sc_mask; 	/* old sigmask */
	unsigned long  sc_usp;		/* old user stack pointer */
	unsigned long  sc_d0;
	unsigned long  sc_d1;
	unsigned long  sc_a0;
	unsigned long  sc_a1;
	unsigned long  sc_a5;
	unsigned short sc_sr;
	unsigned long  sc_pc;
	unsigned short sc_formatvec;
};

#endif
