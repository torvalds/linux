#ifndef __SPARC_SIGCONTEXT_H
#define __SPARC_SIGCONTEXT_H

#include <asm/ptrace.h>
#include <uapi/asm/sigcontext.h>

#ifndef __ASSEMBLY__

#define __SUNOS_MAXWIN   31

/* This is what SunOS does, so shall I unless we use new 32bit signals or rt signals. */
struct sigcontext32 {
	int sigc_onstack;      /* state to restore */
	int sigc_mask;         /* sigmask to restore */
	int sigc_sp;           /* stack pointer */
	int sigc_pc;           /* program counter */
	int sigc_npc;          /* next program counter */
	int sigc_psr;          /* for condition codes etc */
	int sigc_g1;           /* User uses these two registers */
	int sigc_o0;           /* within the trampoline code. */

	/* Now comes information regarding the users window set
	 * at the time of the signal.
	 */
	int sigc_oswins;       /* outstanding windows */

	/* stack ptrs for each regwin buf */
	unsigned int sigc_spbuf[__SUNOS_MAXWIN];

	/* Windows to restore after signal */
	struct reg_window32 sigc_wbuf[__SUNOS_MAXWIN];
};


/* This is what we use for 32bit new non-rt signals. */

typedef struct {
	struct {
		unsigned int psr;
		unsigned int pc;
		unsigned int npc;
		unsigned int y;
		unsigned int u_regs[16]; /* globals and ins */
	}			si_regs;
	int			si_mask;
} __siginfo32_t;

#define __SIGC_MAXWIN	7

typedef struct {
	unsigned long locals[8];
	unsigned long ins[8];
} __siginfo_reg_window;

typedef struct {
	int			wsaved;
	__siginfo_reg_window	reg_window[__SIGC_MAXWIN];
	unsigned long		rwbuf_stkptrs[__SIGC_MAXWIN];
} __siginfo_rwin_t;

#ifdef CONFIG_SPARC64
typedef struct {
	unsigned   int si_float_regs [64];
	unsigned   long si_fsr;
	unsigned   long si_gsr;
	unsigned   long si_fprs;
} __siginfo_fpu_t;

/* This is what SunOS doesn't, so we have to write this alone
   and do it properly. */
struct sigcontext {
	/* The size of this array has to match SI_MAX_SIZE from siginfo.h */
	char			sigc_info[128];
	struct {
		unsigned long	u_regs[16]; /* globals and ins */
		unsigned long	tstate;
		unsigned long	tpc;
		unsigned long	tnpc;
		unsigned int	y;
		unsigned int	fprs;
	}			sigc_regs;
	__siginfo_fpu_t *	sigc_fpu_save;
	struct {
		void	*	ss_sp;
		int		ss_flags;
		unsigned long	ss_size;
	}			sigc_stack;
	unsigned long		sigc_mask;
	__siginfo_rwin_t *	sigc_rwin_save;
};

#else

typedef struct {
	unsigned long si_float_regs [32];
	unsigned long si_fsr;
	unsigned long si_fpqdepth;
	struct {
		unsigned long *insn_addr;
		unsigned long insn;
	} si_fpqueue [16];
} __siginfo_fpu_t;
#endif /* (CONFIG_SPARC64) */


#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC_SIGCONTEXT_H) */
