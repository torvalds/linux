/*
 * uctx.h: Sparc64 {set,get}context() register state layouts.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_UCTX_H
#define __SPARC64_UCTX_H

#define MC_TSTATE	0
#define MC_PC		1
#define MC_NPC		2
#define MC_Y		3
#define MC_G1		4
#define MC_G2		5
#define MC_G3		6
#define MC_G4		7
#define MC_G5		8
#define MC_G6		9
#define MC_G7		10
#define MC_O0		11
#define MC_O1		12
#define MC_O2		13
#define MC_O3		14
#define MC_O4		15
#define MC_O5		16
#define MC_O6		17
#define MC_O7		18
#define MC_NGREG	19

typedef unsigned long mc_greg_t;
typedef mc_greg_t mc_gregset_t[MC_NGREG];

#define MC_MAXFPQ	16
struct mc_fq {
	unsigned long	*mcfq_addr;
	unsigned int	mcfq_insn;
};

struct mc_fpu {
	union {
		unsigned int	sregs[32];
		unsigned long	dregs[32];
		long double	qregs[16];
	} mcfpu_fregs;
	unsigned long	mcfpu_fsr;
	unsigned long	mcfpu_fprs;
	unsigned long	mcfpu_gsr;
	struct mc_fq	*mcfpu_fq;
	unsigned char	mcfpu_qcnt;
	unsigned char	mcfpu_qentsz;
	unsigned char	mcfpu_enab;
};
typedef struct mc_fpu mc_fpu_t;

typedef struct {
	mc_gregset_t	mc_gregs;
	mc_greg_t	mc_fp;
	mc_greg_t	mc_i7;
	mc_fpu_t	mc_fpregs;
} mcontext_t;

struct ucontext {
	struct ucontext		*uc_link;
	unsigned long		uc_flags;
	sigset_t		uc_sigmask;
	mcontext_t		uc_mcontext;
};
typedef struct ucontext ucontext_t;

#endif /* __SPARC64_UCTX_H */
