#ifndef _SPARC64_VISASM_H
#define _SPARC64_VISASM_H

/* visasm.h:  FPU saving macros for VIS routines
 *
 * Copyright (C) 1998 Jakub Jelinek (jj@ultra.linux.cz)
 */

#include <asm/pstate.h>
#include <asm/ptrace.h>

/* Clobbers %o5, %g1, %g2, %g3, %g7, %icc, %xcc */

#define VISEntry					\
	rd		%fprs, %o5;			\
	andcc		%o5, (FPRS_FEF|FPRS_DU), %g0;	\
	be,pt		%icc, 297f;			\
	 sethi		%hi(297f), %g7;			\
	sethi		%hi(VISenter), %g1;		\
	jmpl		%g1 + %lo(VISenter), %g0;	\
	 or		%g7, %lo(297f), %g7;		\
297:	wr		%g0, FPRS_FEF, %fprs;		\

#define VISExit						\
	wr		%g0, 0, %fprs;

/* Clobbers %o5, %g1, %g2, %g3, %g7, %icc, %xcc.
 * Must preserve %o5 between VISEntryHalf and VISExitHalf */

#define VISEntryHalf					\
	rd		%fprs, %o5;			\
	andcc		%o5, FPRS_FEF, %g0;		\
	be,pt		%icc, 297f;			\
	 sethi		%hi(298f), %g7;			\
	sethi		%hi(VISenterhalf), %g1;		\
	jmpl		%g1 + %lo(VISenterhalf), %g0;	\
	 or		%g7, %lo(298f), %g7;		\
	clr		%o5;				\
297:	wr		%o5, FPRS_FEF, %fprs;		\
298:

#define VISEntryHalfFast(fail_label)			\
	rd		%fprs, %o5;			\
	andcc		%o5, FPRS_FEF, %g0;		\
	be,pt		%icc, 297f;			\
	 nop;						\
	ba,a,pt		%xcc, fail_label;		\
297:	wr		%o5, FPRS_FEF, %fprs;

#define VISExitHalf					\
	wr		%o5, 0, %fprs;

#ifndef __ASSEMBLY__
static inline void save_and_clear_fpu(void) {
	__asm__ __volatile__ (
"		rd %%fprs, %%o5\n"
"		andcc %%o5, %0, %%g0\n"
"		be,pt %%icc, 299f\n"
"		 sethi %%hi(298f), %%g7\n"
"		sethi %%hi(VISenter), %%g1\n"
"		jmpl %%g1 + %%lo(VISenter), %%g0\n"
"		 or %%g7, %%lo(298f), %%g7\n"
"	298:	wr %%g0, 0, %%fprs\n"
"	299:\n"
"		" : : "i" (FPRS_FEF|FPRS_DU) :
		"o5", "g1", "g2", "g3", "g7", "cc");
}
extern int vis_emul(struct pt_regs *, unsigned int);
#endif

#endif /* _SPARC64_ASI_H */
