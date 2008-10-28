#ifndef _M68KNOMMU_UCONTEXT_H
#define _M68KNOMMU_UCONTEXT_H

typedef int greg_t;
#define NGREG 18
typedef greg_t gregset_t[NGREG];

typedef struct fpregset {
	int f_pcr;
	int f_psr;
	int f_fpiaddr;
	int f_fpregs[8][3];
} fpregset_t;

struct mcontext {
	int version;
	gregset_t gregs;
	fpregset_t fpregs;
};

#define MCONTEXT_VERSION 2

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct mcontext	  uc_mcontext;
	unsigned long	  uc_filler[80];
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

#endif
