#ifndef __ASM_SPARC_PERF_EVENT_H
#define __ASM_SPARC_PERF_EVENT_H

#ifdef CONFIG_PERF_EVENTS
#include <asm/ptrace.h>

extern void init_hw_perf_events(void);

#define perf_arch_fetch_caller_regs(regs, ip)		\
do {							\
	unsigned long _pstate, _asi, _pil, _i7, _fp;	\
	__asm__ __volatile__("rdpr %%pstate, %0\n\t"	\
			     "rd %%asi, %1\n\t"		\
			     "rdpr %%pil, %2\n\t"	\
			     "mov %%i7, %3\n\t"		\
			     "mov %%i6, %4\n\t"		\
			     : "=r" (_pstate),		\
			       "=r" (_asi),		\
			       "=r" (_pil),		\
			       "=r" (_i7),		\
			       "=r" (_fp));		\
	(regs)->tstate = (_pstate << 8) |		\
		(_asi << 24) | (_pil << 20);		\
	(regs)->tpc = (ip);				\
	(regs)->tnpc = (regs)->tpc + 4;			\
	(regs)->u_regs[UREG_I6] = _fp;			\
	(regs)->u_regs[UREG_I7] = _i7;			\
} while (0)
#else
static inline void init_hw_perf_events(void)	{ }
#endif

#endif
